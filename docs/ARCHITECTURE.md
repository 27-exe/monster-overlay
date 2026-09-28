# Monster Overlay 架构（v0.11.0）

> 本文记录 v0.11.0 的代码分层与数据流，目标是让下一个改这个项目的人一眼看到
> 「我想加一个新面板 / 改 DPS 算法 / 加一种语言，该动哪些文件」。
>
> 本文的每个论断都带 `file:line` 或可复现的 grep / wc 证据；行数是 `wc -l`
> 实测值，不是估算。验证命令汇总见文末「附录：验证记录」。
>
> 相关文档：`docs/I18N.md` 是本地化的唯一规范（权威，本文不重复、不与之冲突），
> `docs/USAGE.md` 是用户操作指南。

---

## 1. 总览

三个编译单元（`CMakeLists.txt:27` `:74` `:107` 的 `add_library`）加上若干可执行
target。依赖方向严格单向：

```
                  monster-core (STATIC)
                  src/core/* + src/rise/* + world/world_types
                  + monster/part_schemas + core/process_memory
                  Qt6::Core, Threads::Threads          (CMakeLists.txt:63)
                        |
                        | PUBLIC
                        v
                  mhw-reader (STATIC)
                  src/mhw_reader.* + 各域 reader 的 .cpp
                  Qt6::Core, monster-core              (CMakeLists.txt:84)
                        |
                        | PUBLIC
                        v
                  mhw-ui (STATIC)
                  src/ui/* + src/ui/viewmodel/*
                  Qt Gui/Widgets/Svg + LayerShellQt
                  + monster-core + mhw-reader          (CMakeLists.txt:156-164)
                        |
                        | PRIVATE
                        v
              每个 exe target 把 resources.qrc / icons.qrc 列在自己的源列表里
              （qrc 不下沉，见 §2 末）
```

反向依赖为零：`grep -rn 'include "ui/' src/core src/monster src/player
src/quest src/world` 无输出；reader 层同样不 include UI 头。UI 层也不直接
include reader 头（`grep -rn 'include "mhw_reader' src/ui/` 无输出，它只把
`mhw::GameSnapshot` 前置声明在 `src/ui/panel.h:14-16`）。

本地化横跨三层：`mhw::StringTable` 在 monster-core 里（`src/core/string_table.h`），
`src/ui/*` 的 View 与 `src/mhw_reader.cpp` 都是它的调用方
（`docs/I18N.md` 被 `src/core/string_table.h:32`、`src/mhw_reader.cpp:23`、
`src/ui/panel_monster.cpp:1448`、`src/ui/panel_player.cpp:29`、
`scripts/i18n_split_domains.py:6` 五处引用）。

---

## 2. 库边界（`CMakeLists.txt`）

| 库 | 源文件数 | 实测行数（`wc -l`） | PUBLIC 依赖 |
|---|---|---|---|
| `monster-core` STATIC | 35 | 10741 | `Qt6::Core`、`Threads::Threads`（`CMakeLists.txt:63`） |
| `mhw-reader` STATIC | 7 | 2385 | `Qt6::Core`、`monster-core`（`CMakeLists.txt:84`） |
| `mhw-ui` STATIC | 46 | 13523 | `Qt6::Core` `Qt6::Gui` `Qt6::Widgets` `Qt6::Svg` `LayerShellQt::Interface` `monster-core` `mhw-reader`（`CMakeLists.txt:156-164`） |

`monster-core` 最近扩容了两处：`src/core/process_memory.{h,cpp}`（S3）与
`src/monster/part_schemas.cpp`（S2，数据表只编译一次）。前者同时消除了
「Rise 依赖 World 库」的倒置，详见下文与 §6。

### `monster-core`（`CMakeLists.txt:27`）

`src/core/*`（string_table / game_detector / steam_game_locator /
rise_reframework_manager / reframework_fetcher / map_paths /
**process_memory**）、
`src/rise/*`（mhr_reader / mhr_part_names / mhr_monster_names /
mhr_abnormalities / rise_damage_reader / rise_damage_roster）、
`src/rise/data/mhr_part_names_data.cpp`、
`src/world/world_types.*`、`src/monster/part_schemas.cpp`。

`src/rise/mhr_part_names.{h,cpp}` 拆成了「数据 TU + 逻辑 TU」：`mhr_part_names.cpp`
原本 764 行，其中 598 行是一张 `constexpr` 表。数据整表搬到
`src/rise/data/mhr_part_names_data.cpp`（682 行），原文件只剩 133 行逻辑
（`risePartNameEntry()` 的 `lower_bound` 二分 + 两个 locale 包装 + 显示名/格式化
助手）。**这次必须改头文件**：598 行表原来在匿名 namespace 里，内部链接，搬到
另一个 TU 后逻辑 TU 看不见它。解法是把 `struct RisePartName` 与
`extern const std::array<RisePartName, 598> kRisePartNames;` 提升到
`mhr_part_names.h`，数据 TU 里的定义保留 `constexpr`（两个 static_assert 因此
仍能逐行审查提交进来的数据），逻辑 TU 经 `extern const` 引用同一份定义。
`nm` 证明跨 TU 链接成立：数据 TU 里是 `D mhw::kRisePartNames`，逻辑 TU 里是
`U mhw::kRisePartNames`。责任分工写进了生成器的文档头：`--cpp-out` 现在默认
指向数据 TU，`--check` 实测通过（三个输入 sha256 与记录一致）。

**S3 把进程内存原语下沉到了这里**：`ProcessMemory`、`AddressMap` 和四个
stateless 助手（`findGamePid` / `selectLoadableMap` / `followPointerChain` /
`followPointerChainOffsetThenDeref`）现在住在 `src/core/process_memory.{h,cpp}`
（142 + 268 行）。它们原先定义在 `src/mhw_reader.{h,cpp}`，但 Rise 的 reader
（同在 monster-core 的 `src/rise/mhr_reader.cpp`）也要用，于是形成了
「World 的 mhw-reader 被 Rise 依赖」的倒置。搬过来之后倒置消失。为了不破坏
World 侧已有的调用点拼写，`src/mhw_reader.h:20-55` 保留了四个 static 委托
成员（`MhwReader::findGamePid` 等），实现仍只有 core 里那一份。

`src/core/game_snapshot.h`（37 行）不在 `add_library` 列表里：它是纯数据结构，
被 13 个头文件 include（`grep -rln 'core/game_snapshot.h' src/`：
`core/game_detector.h`、`core/process_memory.h`、`main.cpp`、`monster/monster_types.h`、
`rise/mhr_reader.h`、`rise/rise_damage_roster.h`、`ui/control_panel.h`、`ui/icon.h`、
`ui/panel_damage.h`、`ui/panel_player.h`、`ui/viewmodel/damage_view_model.h`、
`ui/viewmodel/overlay_process_controller.h`、`ui/viewmodel/player_view_model.h`），
随各自的 TU 编译。

`src/core/locale_conf.h` 与 `src/core/locale_sync.h` 也不在该库里
（`grep -n 'locale_conf\|locale_sync' CMakeLists.txt` 只命中 `:640` `:641` `:649`
`:652` `:653` `:748`，全部属于 monster-locale-tests target），由用到它们的
target 自行列出。

### `mhw-reader`（`CMakeLists.txt:74`）

`src/mhw_reader.{h,cpp}` + `src/monster/monster_reader.cpp` +
`src/monster/target_selector.cpp` + `src/player/player_reader.cpp` +
`src/quest/quest_reader.cpp` + `src/world/world_reader.cpp`。

S3 之后 `src/mhw_reader.cpp` 瘦身 237 行（从原 523 行降到 286 行）：进程内存
原语全部搬去了 monster-core，只剩 World 专用的读取逻辑。`src/mhw_reader.h`
里 4 个 static 成员是薄委托，不是实现（`src/mhw_reader.h:20-55`）。

`src/rise/mhr_reader.cpp` 在 monster-core 里，不在这里：Rise 的 reader 读的是
REFramework 写的 JSON 文件，不是进程内存，所以它属于核心层。但它仍然通过
monster-core 里的 `findGamePid()` / `ProcessMemory` 附着游戏进程并读
abnormality 内存块（`src/rise/mhr_reader.cpp:188` `:202`）。

### `mhw-ui`（`CMakeLists.txt:107`）

`src/ui` 下全部 46 个源文件（`src/ui/*.cpp|.h` 加 `src/ui/viewmodel/*`）。
它持有 screen_query.cpp 需要的私有 QtGui 头（`qpa/qplatformscreen.h`），
所以桌面后端屏幕尺寸的探测只有这里编译一次（`CMakeLists.txt:167-192`）。

### qrc **不下沉**

`add_library()` 块里没有任何 qrc：`grep -n 'qrc' CMakeLists.txt` 的命中全部落在
exe target 的源列表里。`src/resources/resources.qrc` 被 22 处、
`assets/icons.qrc` 被 12 处列出（如 `CMakeLists.txt:208-209`）。

实测 INTERFACE 库与 STATIC 库持有 qrc 都是「configure/build 0 error，运行时
`QFile(":/...").open()` 失败」——AUTORCC 的注册代码不传播、静态库的 `qrc_*.cpp`
未被 exe 直接引用而被链接器丢弃。实测过程与结论记录在父目录的
`QRC-SINK-INFEASIBLE.md`。**这是 Qt 的硬约束，不是原项目的疏忽**，加新 target
时记得把两个 qrc 一起列上。

---

## 3. ViewModel 层（`src/ui/viewmodel/`）

零 QWidget / QPainter 依赖。验证：`grep -n '#include' src/ui/viewmodel/*.h
src/ui/viewmodel/*.cpp` 的全部输出里没有任何 widget / painter / QtWidgets
头文件；唯一的 Qt Widgets 邻接物是 `console_layout_store.cpp:8` 的
`<QSettings>`（QtCore，且只出现在 .cpp —— 头文件不 include、不前向声明、
也不在签名里提名该类型，见该行下文的注释块）。`QWidget` / `QPainter` 字样
本身仍会出现在两个头文件的**注释**里（说明「本类型不含它们」），但那不是
依赖。

合计 12 个文件 / 2076 行（`wc -l src/ui/viewmodel/*`）；5 个类 / 1 个函数，
其中 4 类已有直接单测（下表末三行）。

| 类 / 函数（头 `.h` 行 / 实现 `.cpp` 行） | 抽自如 | 职责与边界 |
|---|---|---|
| `DamageViewModel`（134 / 620） | DamagePanel | 伤害/DPS 统计引擎 + 折线图 history。`computeDps()` 是原面板的 DPS 公式：250 ms 轮询 → ×4 每秒，`damage_view_model.cpp:605-618`。依赖 `core/game_snapshot.h` + `rise/rise_damage_types.h` + QtCore |
| `MonsterPartListBuilder`（220 / 337） | MonsterPanel | 部位卡（`.pgrid`）的**唯一**装配器。持有可变 `partTrack_`（2048 项，`monster_view_model.h:179`）实现 HunterPie 15 s PartAutoHide。`build()` 会 mutate `partTrack_`，故每帧 paint 只许调一次（`monster_view_model.h:153-157`） |
| `OverlayProcessController`（95 / 157） | ControlPanel | 子进程生命周期：`launch()` 追加 `--game=` 后 `QProcess::startDetached()`、`stop()` 发 SIGTERM（`overlay_process_controller.cpp:75`）、250 ms `kill(pid,0)` 轮询（`:114-118`）、`pendingRestart_` 热切换（`:96`）。QProcess/QTimer 刻意允许：进程管理就是这个类的本职 |
| `ConsoleLayoutStore`（127 / 125） | ControlPanel | `ui/*` 窗口布局持久化：6 个键 `ui/geometry` `ui/windowState` `ui/leftSplitter` `ui/zoom` `ui/splitter` `ui/stageHeight`（`console_layout_store.cpp:23-28`），7 个调用点（`control_panel.cpp:445` `:653` `:658` `:841` `:873` `:900` `:1304`）。不持后端句柄，因此可拷贝、可每次新建 |
| `PlayerViewModel`（61 / 51） | PlayerPanel | v0.11（S1）新增。玩家身份解析器：`resolveIdentity(snap) -> PlayerIdentity{masterRank,name,weaponId,partyCount}`，把原先散在 `PlayerPanel::update()` 里的三块搬进来——player 结构的初始镜像、偏好本地成员字段的 party 覆写循环、无数据时把 `weaponId` 复位为 -1 的兜底。纯函数、const，测试用手搓 snapshot 直接调用（`tests/player_view_model_tests.cpp`）。依赖 `core/game_snapshot.h` + `player/player_types.h` + QtCore |
| `riseReframeworkStatusLines()`（56 / 93） | ControlPanel | 从 `RiseReframeworkStatusInput`（`Status`、`gameDir`、`gameRunning`、`operationPending`、`hasResult`、`resultOk`、`resultDetail`）派生 REFramework 卡片可读行。纯函数，除 `core/string_table.h` 与 `core/rise_reframework_manager.h` 外不碰任何东西 |

### 边界规则

- VM 只依赖 QtCore + 数据层，证据是 §3 表"职责与边界"列的 include 事实。
  `grep -n '#include' src/ui/viewmodel/*.h src/ui/viewmodel/*.cpp` 里没有
  widget / painter / QtWidgets 的 `#include`；`QWidget` / `QPainter` 字样只以
  注释形式出现在 `monster_view_model.h`、`player_view_model.h` 的说明文字里。
- **`canvas()->update()` 留在 View**：`DamageViewModel::changed()` 由 View 连到
  `QWidget::update()`（`src/ui/panel_damage.cpp:161-162`）；其余面板仍用
  `canvas()->update()`（`src/ui/panel_monster.cpp:789` `:796`）或
  `triggerUpdate()`（`src/ui/panel.h:39`）。VM 自己不调 update。
- **PlayerViewModel 的调用点在三处之外还要守一条**：`PlayerPanel` 的成员
  `playerMR_` / `playerName_` / `weaponId_` / `partyCount_` 仍由 panel 持有，
  VM 只返回一个 `PlayerIdentity` 值，panel 自己拷（`src/ui/panel_player.cpp:523-527`）。
  这正是「VM 是形状解析、panel 是所有权」的分界，改动身份逻辑时只动 VM 与测试。
- **每帧一次且会 mutate 的调用必须标 "ONE call per paint"**：
  `MonsterPartListBuilder::build()` 的调用点 `src/ui/panel_monster.cpp:1003`
  上方有该注释，`:1371` 另有交叉引用。

### 已有测试覆盖（纯逻辑，无 GUI）

`tests/damage_view_model_tests.cpp`（559 行）、
`tests/rise_reframework_status_tests.cpp`（363 行）、
`tests/console_layout_store_tests.cpp`（311 行）、
`tests/player_view_model_tests.cpp`（355 行）。它们正是 v0.10/v0.11 做这些
抽出的动机：原先这些判定藏在 2916 行的 widget 文件里，状态机不可达。

四个 ViewModel 直接单测合计 **1588 行**（`wc -l` 四个文件）。它们各自的
case 表在文件末的 `main()` 里（`damage_view_model_tests` 9 例、
`rise_reframework_status_tests` 10 例、另两个用 `check()` 逐段断言）。
全项目 ctest 注册 **40** 个 case（见附录）。

`MonsterPartListBuilder` **没有**直接单测：`grep -rln 'MonsterPartListBuilder'
tests/` 只命中 `tests/snap_gauge_fill.cpp` 的间接引用。跨帧的 PartAutoHide
状态机目前只被离屏回归测试间接覆盖。

---

## 4. 数据流向

```
  pollGame()                                       — src/main.cpp:409-411
    （World: MhwReader::poll()，读进程内存，src/mhw_reader.cpp:215；
      Rise:  MhrReader::poll()，读 REFramework 写的 JSON，src/rise/mhr_reader.cpp:1386；
      两个 reader 各自持有，均为 GameSnapshot 的取数入口）
        |  mhw::GameSnapshot
        |  src/core/game_snapshot.h（37 行：game/attached/pid/imageBase/status/
        |  zone/monsters/player/party/quest/playerCount/isMultiplayer/诊断字段）
        v
  src/main.cpp 的 pollGame() 拿单帧                — src/main.cpp:409-411
        |
        +-> panel->update(snap) / panel->updateRiseDamage(dmg)      — View 层
        |     （DamagePanel::update 只是转发：panel_damage.cpp:195-198 -> m_vm.updateWorld(snap)）
        v
  VM 处理状态 -> emit changed()                     — ViewModel
        |   （damage_view_model.cpp 共 12 处 emit changed()）
        v
  View 把该信号连到 update()                        — src/ui/panel_damage.cpp:161
        v
  paintPanel() 读 VM 的 const 访问器绘制            — src/ui/panel_damage.cpp:200+
```

要点：

- **Reader 与 UI 只经 snapshot 耦合**：Reader 不 include UI 头，UI 不 include
  reader 头（§1 的 grep 结果）。GameSnapshot 有两个对等的 reader 入口：
  World 的 `MhwReader::poll()` 与 Rise 的 `MhrReader::poll()`，二者都返回
  `GameSnapshot`，UI 侧完全不区分。所以准确表述是「**每个游戏各有一个取数入口，
  且入口签名一致**」，而非「唯一入口」。
- **VM 变更 → 重绘**走信号；VM 从不调 `update()`。
- 面板自己保存一份快照成员（如 `mhw::MonsterSnapshot monster_;`，
  `src/ui/panel_monster.h:105`；`PlayerPanel` 的 `player_`/`zone_`/`quest_`/
  `game_`/`attached_`/`pid_`/`imageBase_`/`status_`，
  `src/ui/panel_player.h:40-51`），paint 从这些成员读，不从 reader 读。
- **Rise 的伤害数据不走 `GameSnapshot.poll()`**：主循环另有一组
  `riseDamageReaders`，逐个 `update()` 后挑 seq/timestamp 最新的那份
  （`src/main.cpp:540-567`），再用 World 侧 snapshot 调
  `enrichRiseDamageSnapshot()` 补齐（`:576-577`）。这是与 World 平行的第二条 feed。
- 轮询节流来自 `--poll` 命令行选项，默认 250 ms
  （`src/main.cpp:150-154`）。

---

## 5. 本地化（四域）

目录 `src/resources/i18n/<locale>/`，两个 locale：`zh-CN` / `en-US`；
每个语言下四个域文件 `overlay.json` `console.json` `reader.json` `data.json`
（`find src/resources/i18n -type f` 命中 8 个文件）。它们在
`src/resources/resources.qrc:4-11` 被列出，运行时由 `StringTable::load()`
从 `:/i18n/<locale>/` 目录合并（`src/core/string_table.cpp:30`）：递归把嵌套
对象拍平为点号 key，顶层 `_meta` 跳过。

各域实测叶子 key 数（zh-CN，剔除 `_meta` 后逐层拍平统计；en-US 同数）：

| 域文件 | 顶层命名空间 | 叶子 key 数 | 备注 |
|---|---|---|---|
| `overlay.json` | `ui` | 45 | 顶层 38 个平铺 key 加 `ui.quest_state.*` 下 7 个二级 key；**尚未三层化** |
| `console.json` | `console` | 154 | 顶层 3 个平铺 key 加 16 个二级分组（`status`/`rail`/`game`/`nav`/…）；下文细述 |
| `reader.json` | `reader` | 144 | 顶层 14 个平铺 key 加二级分组 |
| `data.json` | `data` | 56 | `data.mantle.id.*`、`data.abnormality.*`、`data.demo.*` |

切分依据是**谁负责重绘**：`overlay` 的各面板在 `paintPanel()` 每次重绘重新
`tr()`；`console` 由 `ControlPanel::retranslateUi()` 重放注册表；`reader` 的
status 串每 poll tick 自然刷新；`data` 是数据名与演示数据。

命名规范是 `<域>.<模块>.<语义>` 三层、leaf snake_case。
**规范本身以 `docs/I18N.md` 为权威**，本文只记录布局事实，不重复、不冲突。
已知遗留：`overlay.json` 仍是扁平 `ui.*`，三层化改名批次未做——它的 `_meta`
自己记了这件事。加新语言的四步见 `docs/I18N.md` §7。

`console.json` 的 154 个叶子拆开看：3 个顶层平铺 key（`console.brand`、
`console.brand_sub`、`console.window_title`）+ 151 个分组内 key。16 个分组里
`console.reframework` 一家占 51 个（第二层到底，无第三层），最大的三块是
`reframework` 51、`inspector` 17（其中 13 个平铺、`inspector.sub` 4）、
`rail` 12。另有 `console.section` 下有四个第三层组 `player`(8) / `monster`(6) /
`damage`(4) / `pets`(2)，共 20 个 key——所以它是四域中唯一已经三层化到
`panel`/`section` 层级的。这 51 个 REFramework key 是 v0.11 前后两批新加的
（`result_*` / `menu_state_*` / `state_*` 三族），文档此前记的 138 已失效。

---

## 6. 已知边界（如实记录，不粉饰）

- **`control_panel.cpp` 2916 行**（`wc -l src/ui/control_panel.cpp`）。
  REFramework 按钮编排刻意未抽：模态确认本就是 UI 关注点，硬抽只会把
  `QMessageBox::question()` 包进假 VM。证据：该文件 4 处模态确认
  （`:2251` `:2279` `:2327` `:2360`），QMessageBox 相关共 17 行命中。
  已抽出的三块见 §3（进程生命周期、窗口布局、RF 状态行派生）。
- **`MhwReader::` 实现横跨 5 个目录的 .cpp，是一个 façade**：
  `grep -rn 'MhwReader::' src --include=*.cpp` 命中 19 处，分布在 6 个文件：
  `src/mhw_reader.cpp`、`src/monster/monster_reader.cpp`、
  `src/player/player_reader.cpp`、`src/quest/quest_reader.cpp`、
  `src/world/world_reader.cpp`、`src/main.cpp`。
  其中按返回类型识别的成员函数定义 15 处（如 `readMonsters` 在
  `src/monster/monster_reader.cpp:390`、`readParty` 在
  `src/player/player_reader.cpp:500`）；其余 4 处分别是 `main.cpp:379` 的
  `MhwReader::selectLoadableMap` **调用**、`src/mhw_reader.cpp:132` 的
  构造函数、`player_reader.cpp:468` 注释里的提及、`mhw_reader.cpp` 里
  `MhwReader::poll()` 自身的声明。类声明在 `src/mhw_reader.h:12-173`。
  对比 S3 之前是 63 处 / 7 文件 / 19 处定义：当时 `src/rise/mhr_reader.cpp` 一家
  独大 40 处（全是 `MhwReader::followPointerChain` 等静态助手的调用），随着
  助手搬进 monster-core、调用点改拼成不带 `MhwReader::` 前缀的 `mhw::findGamePid`
  形式，这 40 处已全部归零，`mhr_reader.cpp` 从命中列表里整份消失。
- **进程内 static PID 缓存被两个 reader 实例共享**：三个 static
  （`static qint64 cachedPid` / `static qint64 lastScanMs` /
  `static QString cachedExeName`）现在位于
  `src/core/process_memory.cpp:173-175`。S3 之前它们在 `src/mhw_reader.cpp`
  的 287-289 行——那份文件现在只剩 286 行，所以旧行号已经全部越界，
  别再拿它当索引。
  **它们仍是同一个函数局部 static、同一份缓存**，只是随 `findGamePid()`
  一并搬进了 monster-core 的实现文件——不是被换成了类成员，也没有按实例拆开。
  5 s 有效窗口，`exeName` 变化即失效并重扫（`:180-184`）。World 的
  `MhwReader` 与 Rise 的 `MhrReader` 各自持有一个实例
  （`src/main.cpp:380` 构造 worldReader，`:389` 构造 riseReader），两个实例都要
  调这份 `findGamePid()`，因此共享同一份 static 缓存。**已知隐患，未修**。
- **`Panel::onSnapshot()` 是死钩子**：`grep -rn 'onSnapshot' src/ tests/` 只有
  `src/ui/panel.h:142` 一处定义，零调用、零覆写。主循环自己调
  `panel->update(snap)` / `monsterPanel.update(...)` / `damagePanel.updateRiseDamage(...)`
  （`src/main.cpp:497` `:502` `:519` `:579` `:581` `:603` `:605`），没走这个虚函数。
- **`MonsterPartListBuilder` 无直接单测**（§3 末）。
- **`src/ui/locale_conf` / `locale_sync` 不在 monster-core**（§2 注），
  加用到它们的新 target 时要自己列源文件。
- **其余大文件**（`wc -l src/ui/*.cpp`）：`control_panel.cpp` 2916、
  `panel_player.cpp` 1562、`panel_monster.cpp` 1519、`panel_damage.cpp` 586、
  `hud_canvas.cpp` 587、`panel_pet_damage.cpp` 451、
  `rise_reframework_bridge.cpp` 293。

---

## 7. 常见改动路线

| 想做的事 | 该动哪里 |
|---|---|
| 加一个新 overlay 面板 | 新 `src/ui/panel_x.{h,cpp}`：继承 `Panel`、实现 `paintPanel()` 与 `update()` 重载；i18n key 加进 `overlay.json`；源文件列进 `CMakeLists.txt` 的 mhw-ui 块，并在 `src/main.cpp` 装配 |
| 改 DPS / 折线图算法 | `src/ui/viewmodel/damage_view_model.{h,cpp}`；测试加进 `tests/damage_view_model_tests.cpp` |
| 改部位卡聚合 / AutoHide | `src/ui/viewmodel/monster_view_model.{h,cpp}`（守住 "ONE call per paint"） |
| 改 reader 的地址 / 字段 | 对应域 reader 的 .cpp（见 §2）；加字段改 `src/core/game_snapshot.h` |
| 改 PID 查找 / 指针链 / 地址表加载 | `src/core/process_memory.{h,cpp}`（monster-core）。World 侧写 `MhwReader::` 前缀、Rise 侧写 `mhw::` 前缀，但都要改这里这一份实现 |
| 加一种新语言 | `docs/I18N.md` §7：建 locale 目录 + 四个域文件 + qrc 列出 + 跑 parity 脚本 |
| 改窗口布局持久化 | `src/ui/viewmodel/console_layout_store.{h,cpp}` + `tests/console_layout_store_tests.cpp` |
| 改 REFramework 卡片文案 | `src/ui/viewmodel/rise_reframework_status.{h,cpp}` + `tests/rise_reframework_status_tests.cpp` |
| 改玩家 MR/名/武器/party 的合并规则 | `src/ui/viewmodel/player_view_model.{h,cpp}` + `tests/player_view_model_tests.cpp` |
| 改 i18n key / 域切分 | `docs/I18N.md` 是权威；门禁脚本在 `scripts/i18n_*.py` |
| 新增 exe / 测试 target | 记得把 `src/resources/resources.qrc` 与 `assets/icons.qrc` 列进自己的源列表（§2 末） |

---

## 附录：验证记录

以下命令在 worktree `/home/a27exe/experiment/monster-overlay-v0.11.0/wt-s5`
（HEAD `863d8b2`，即 S1/S2/S3 全部落地后）执行；括号内的旧值来自上一次记录
（HEAD `0dac433`，worktree `wt-t19`）。输出摘要：

| 命令 | 摘要 |
|---|---|
| `grep -n 'add_library' CMakeLists.txt` | 3 个：`:27` monster-core、`:74` mhw-reader、`:107` mhw-ui（旧 `:27` `:69` `:102`） |
| `grep -c 'add_executable' CMakeLists.txt` | 45 个 exe target（旧 43） |
| `grep -n 'target_link_libraries' CMakeLists.txt` | 三个库分别在 `:63` `:84` `:156-164`（旧 `:58` `:79` `:148-154`） |
| `grep -n 'qrc' CMakeLists.txt` | 无 `add_library` 块含 qrc；`resources.qrc` 22 处、`icons.qrc` 12 处，全部在 exe target（首次例：`:208-209`） |
| `grep -rn 'include "ui/' src/core src/monster src/player src/quest src/world` | 无输出（core/reader 不依赖 UI） |
| `grep -rn 'include "mhw_reader' src/ui/` | 无输出（UI 不 include reader 头；`panel.h:14-16` 前置声明 `GameSnapshot`） |
| `wc -l`（三个 `add_library` 块列出的全部源文件） | monster-core **35 文件 / 10741 行**；mhw-reader **7 / 2385**；mhw-ui **46 / 13523**（旧 28/9041、7/2690、43/13423） |
| `grep -n '#include' src/ui/viewmodel/*.h src/ui/viewmodel/*.cpp` | 无 QWidget / QPainter / QtWidgets 的 `#include`；仅 `console_layout_store.cpp:8` 有 `<QSettings>`，且只在 .cpp |
| `wc -l src/ui/viewmodel/*` | **12 文件 / 2076 行**；各类行数见 §3 表（旧 10/1964） |
| `grep -rn 'onSnapshot' src/ tests/` | 仅 `src/ui/panel.h:142` 定义，零调用 |
| `grep -rn 'MhwReader::' src --include=*.cpp` | **19 处 / 6 个文件**；按返回类型识别的成员函数定义 **15** 处（旧 63/7/19） |
| `grep -n 'static qint64 cachedPid' src/core/process_memory.cpp` | 三个 static 在 `:173` `:174` `:175`，exeName 失效分支 `:180-184`。S3 之前它们在 `src/mhw_reader.cpp` 的 287-289 行；那份文件现在只有 286 行，旧索引已失效 |
| `wc -l src/ui/control_panel.cpp` | 2916（旧 2910） |
| `grep -n 'QMessageBox' src/ui/control_panel.cpp` | 17 行命中，含 4 处 `QMessageBox::question()` 模态确认（`:2251` `:2279` `:2327` `:2360`） |
| `find src/resources/i18n -type f` | 8 个文件：2 locale × 4 域 |
| `python3` 逐层拍平统计各域叶子 key | 45 / **154** / 144 / 56（zh-CN），en-US 同数（旧 console 138） |
| `wc -l tests/{damage_view_model,rise_reframework_status,console_layout_store,player_view_model}_tests.cpp` | 559 / 363 / 311 / **355** |
| `grep -rln 'MonsterPartListBuilder' tests/` | 只命中 `tests/snap_gauge_fill.cpp`（间接引用，无直接单测） |
| `grep -n 'pollOption' src/main.cpp` | `:150` 声明、`:154` 默认轮询间隔 250 ms |
| `grep -n 'MhrReader::poll\\|MhwReader::poll' src/` | `src/rise/mhr_reader.cpp:1386`、`src/mhw_reader.cpp:215`——两个对等取数入口 |
| `wc -l src/ui/*.cpp` | 最大的四个：`control_panel.cpp` 2916、`panel_player.cpp` 1562、`panel_monster.cpp` 1519（`panel_damage.cpp` 与 `hud_canvas.cpp` 同居第五：586 / 587） |
| `grep -c 'add_test(NAME' CMakeLists.txt` + `MHW_I18N_GATES` 展开 | **40 个 ctest case**：37 个 `add_test(NAME …)` 加 3 个 `foreach` 生成的 i18n 门禁（`check` / `parity` / `unused`，`CMakeLists.txt:886-895`）。S1 加了 `player-view-model-tests` 这一个 |
