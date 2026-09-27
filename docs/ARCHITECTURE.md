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

三个编译单元（`CMakeLists.txt:27` `:69` `:102` 的 `add_library`）加上若干可执行
target。依赖方向严格单向：

```
                  monster-core (STATIC)
                  src/core/* + src/rise/* + world/world_types
                  Qt6::Core, Threads::Threads          (CMakeLists.txt:58)
                        |
                        | PUBLIC
                        v
                  mhw-reader (STATIC)
                  src/mhw_reader.* + 各域 reader 的 .cpp
                  Qt6::Core, monster-core              (CMakeLists.txt:79)
                        |
                        | PUBLIC
                        v
                  mhw-ui (STATIC)
                  src/ui/* + src/ui/viewmodel/*
                  Qt Gui/Widgets/Svg + LayerShellQt
                  + monster-core + mhw-reader          (CMakeLists.txt:148-154)
                        |
                        | PRIVATE
                        v
              每个 exe target 把 resources.qrc / icons.qrc 列在自己的源列表里
              （qrc 不下沉，见 §2 末）
```

反向依赖为零：`grep -rn 'include "ui/' src/core src/monster src/player
src/quest src/world` 无输出；reader 层同样不 include UI 头。UI 层也不直接
include reader 头（`grep -rn 'include "mhw_reader' src/ui/` 无输出，它只把
`mhw::GameSnapshot` 前置声明在 `src/ui/panel.h:16-18`）。

本地化横跨三层：`mhw::StringTable` 在 monster-core 里（`src/core/string_table.h`），
`src/ui/*` 的 View 与 `src/mhw_reader.cpp` 都是它的调用方
（`docs/I18N.md` 被 `src/core/string_table.h:32`、`src/mhw_reader.cpp:32`、
`src/ui/panel_monster.cpp:1448`、`src/ui/panel_player.cpp:101`、
`scripts/i18n_split_domains.py:6` 五处引用）。

---

## 2. 库边界（`CMakeLists.txt`）

| 库 | 源文件数 | 实测行数（`wc -l`） | PUBLIC 依赖 |
|---|---|---|---|
| `monster-core` STATIC | 28 | 9041 | `Qt6::Core`、`Threads::Threads`（`CMakeLists.txt:58`） |
| `mhw-reader` STATIC | 7 | 2690 | `Qt6::Core`、`monster-core`（`CMakeLists.txt:79`） |
| `mhw-ui` STATIC | 43 | 13423 | `Qt6::Core` `Qt6::Gui` `Qt6::Widgets` `Qt6::Svg` `LayerShellQt::Interface` `monster-core` `mhw-reader`（`CMakeLists.txt:148-154`） |

### `monster-core`（`CMakeLists.txt:27`）

`src/core/*`（string_table / game_detector / steam_game_locator /
rise_reframework_manager / reframework_fetcher / map_paths）、
`src/rise/*`（mhr_reader / mhr_part_names / mhr_monster_names /
mhr_abnormalities / rise_damage_reader / rise_damage_roster）、
`src/world/world_types.*`。

`src/core/game_snapshot.h`（37 行）不在 `add_library` 列表里：它是纯数据结构，
被 11 个头文件 include（`grep -rln 'core/game_snapshot.h' src/`：
`core/game_detector.h`、`main.cpp`、`mhw_reader.h`、`monster/monster_types.h`、
`rise/rise_damage_roster.h`、`ui/control_panel.h`、`ui/icon.h`、
`ui/panel_damage.h`、`ui/panel_player.h`、`ui/viewmodel/damage_view_model.h`、
`ui/viewmodel/overlay_process_controller.h`），随各自的 TU 编译。

`src/core/locale_conf.h` 与 `src/core/locale_sync.h` 也不在该库里
（`grep -n 'locale_conf\|locale_sync' CMakeLists.txt` 只命中 `:662` `:663`
的 monster-locale-tests target），由用到它们的 target 自行列出。

### `mhw-reader`（`CMakeLists.txt:69`）

`src/mhw_reader.{h,cpp}` + `src/monster/monster_reader.cpp` +
`src/monster/target_selector.cpp` + `src/player/player_reader.cpp` +
`src/quest/quest_reader.cpp` + `src/world/world_reader.cpp`。

`src/rise/mhr_reader.cpp` 在 monster-core 里，不在这里：Rise 的 reader 读的是
REFramework 写的 JSON 文件，不是进程内存，所以它属于核心层。

### `mhw-ui`（`CMakeLists.txt:102`）

`src/ui` 下全部 43 个源文件（`src/ui/*.cpp|.h` 加 `src/ui/viewmodel/*`）。
它持有 screen_query.cpp 需要的私有 QtGui 头（`qpa/qplatformscreen.h`），
所以桌面后端屏幕尺寸的探测只有这里编译一次（`CMakeLists.txt:157-181`）。

### qrc **不下沉**

`add_library()` 块里没有任何 qrc：`grep -n 'qrc' CMakeLists.txt` 的命中全部落在
exe target 的源列表里。`src/resources/resources.qrc` 被 22 处、
`assets/icons.qrc` 被 12 处列出（如 `CMakeLists.txt:201-202`）。

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
也不在签名里提名该类型，见该行下文的注释块）。

合计 10 个文件 / 1964 行（`wc -l src/ui/viewmodel/*`）。

| 类 / 函数（头 `.h` 行 / 实现 `.cpp` 行） | 抽自如 | 职责与边界 |
|---|---|---|
| `DamageViewModel`（134 / 620） | DamagePanel | 伤害/DPS 统计引擎 + 折线图 history。`computeDps()` 是原面板的 DPS 公式：250 ms 轮询 → ×4 每秒，`damage_view_model.cpp:605-618`。依赖 `core/game_snapshot.h` + `rise/rise_damage_types.h` + QtCore |
| `MonsterPartListBuilder`（220 / 337） | MonsterPanel | 部位卡（`.pgrid`）的**唯一**装配器。持有可变 `partTrack_`（2048 项，`monster_view_model.h:179`）实现 HunterPie 15 s PartAutoHide。`build()` 会 mutate `partTrack_`，故每帧 paint 只许调一次（`monster_view_model.h:153-157`） |
| `OverlayProcessController`（95 / 157） | ControlPanel | 子进程生命周期：`launch()` 追加 `--game=` 后 `QProcess::startDetached()`、`stop()` 发 SIGTERM（`overlay_process_controller.cpp:75`）、250 ms `kill(pid,0)` 轮询（`:114-118`）、`pendingRestart_` 热切换（`:96`）。QProcess/QTimer 刻意允许：进程管理就是这个类的本职 |
| `ConsoleLayoutStore`（127 / 125） | ControlPanel | `ui/*` 窗口布局持久化：6 个键 `ui/geometry` `ui/windowState` `ui/leftSplitter` `ui/zoom` `ui/splitter` `ui/stageHeight`（`console_layout_store.cpp:23-28`），7 个调用点（`control_panel.cpp:436` `:644` `:649` `:832` `:864` `:891` `:1298`）。不持后端句柄，因此可拷贝、可每次新建 |
| `riseReframeworkStatusLines()`（56 / 93） | ControlPanel | 从 `RiseReframeworkStatusInput`（`Status`、`gameDir`、`gameRunning`、`operationPending`、`hasResult`、`resultOk`、`resultDetail`）派生 REFramework 卡片可读行。纯函数，除 `core/string_table.h` 与 `core/rise_reframework_manager.h` 外不碰任何东西 |

### 边界规则

- VM 只依赖 QtCore + 数据层，证据是 §3 表"职责与边界"列的 include 事实。
- **`canvas()->update()` 留在 View**：`DamageViewModel::changed()` 由 View 连到
  `QWidget::update()`（`src/ui/panel_damage.cpp:161-162`）；其余面板仍用
  `canvas()->update()`（`src/ui/panel_monster.cpp:789` `:796`）或
  `triggerUpdate()`（`src/ui/panel.cpp:39`）。VM 自己不调 update。
- **每帧一次且会 mutate 的调用必须标 "ONE call per paint"**：
  `MonsterPartListBuilder::build()` 的调用点 `src/ui/panel_monster.cpp:1003`
  上方有该注释，`:1371` 另有交叉引用。

### 已有测试覆盖（纯逻辑，无 GUI）

`tests/damage_view_model_tests.cpp`（559 行）、
`tests/rise_reframework_status_tests.cpp`（363 行）、
`tests/console_layout_store_tests.cpp`（311 行）。它们正是 v0.10/v0.11 做这些
抽出的动机：原先这些判定藏在 2910 行的 widget 文件里，状态机不可达。

`MonsterPartListBuilder` **没有**直接单测：`grep -rln 'MonsterPartListBuilder'
tests/` 只命中 `tests/snap_gauge_fill.cpp` 的间接引用。跨帧的 PartAutoHide
状态机目前只被离屏回归测试间接覆盖。

---

## 4. 数据流向

```
  MhwReader::poll()                                — mhw-reader（src/mhw_reader.cpp:452）
    （World 读进程内存；Rise 走 mhr_reader.cpp 的 JSON producer）
        |  mhw::GameSnapshot
        |  src/core/game_snapshot.h（37 行：game/attached/pid/imageBase/status/
        |  zone/monsters/player/party/quest/playerCount/isMultiplayer/诊断字段）
        v
  src/main.cpp 的 pollGame() 拿单帧                — src/main.cpp:411-412
        |
        +-> panel->update(snap) / panel->updateRiseDamage(dmg)      — View 层
        |     （DamagePanel::update 只是转发：panel_damage.cpp:196-198 -> m_vm.updateWorld(snap)）
        v
  VM 处理状态 -> emit changed()                     — ViewModel
        |   （damage_view_model.cpp 共 11 处 emit changed()）
        v
  View 把该信号连到 update()                        — src/ui/panel_damage.cpp:161
        v
  paintPanel() 读 VM 的 const 访问器绘制            — src/ui/panel_damage.cpp:206+
```

要点：

- **Reader 与 UI 只经 snapshot 耦合**：Reader 不 include UI 头，UI 不 include
  reader 头（§1 的 grep 结果）。`MhwReader::poll()` 是唯一取数入口。
- **VM 变更 → 重绘**走信号；VM 从不调 `update()`。
- 面板自己保存一份快照成员（如 `mhw::MonsterSnapshot monster_;`，
  `src/ui/panel_monster.h:107`；`PlayerPanel` 的 `player_`/`zone_`/`quest_`/
  `game_`/`attached_`/`pid_`/`imageBase_`/`status_`，
  `src/ui/panel_player.cpp:534-547`），paint 从这些成员读，不从 reader 读。
- **Rise 的伤害数据不走 `GameSnapshot.poll()`**：主循环另有一组
  `riseDamageReaders`，逐个 `update()` 后挑 seq/timestamp 最新的那份
  （`src/main.cpp:544-561`），再用 World 侧 snapshot 调
  `enrichRiseDamageSnapshot()` 补齐（`:577-578`）。这是与 World 平行的第二条 feed。
- 轮询节流来自 `--poll` 命令行选项，默认 250 ms
  （`src/main.cpp:152-153`）。

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
| `overlay.json` | `ui` | 45 | 顶层 39 个平铺 key 加 6 个二级 key；**尚未三层化** |
| `console.json` | `console` | 138 | 顶层 3 个平铺 key 加 16 个二级分组（`status`/`rail`/`game`/`nav`/…） |
| `reader.json` | `reader` | 144 | 顶层 14 个平铺 key 加二级分组 |
| `data.json` | `data` | 56 | `data.mantle.id.*`、`data.abnormality.*`、`data.demo.*` |

切分依据是**谁负责重绘**：`overlay` 的各面板在 `paintPanel()` 每次重绘重新
`tr()`；`console` 由 `ControlPanel::retranslateUi()` 重放注册表；`reader` 的
status 串每 poll tick 自然刷新；`data` 是数据名与演示数据。

命名规范是 `<域>.<模块>.<语义>` 三层、leaf snake_case。
**规范本身以 `docs/I18N.md` 为权威**，本文只记录布局事实，不重复、不冲突。
已知遗留：`overlay.json` 仍是扁平 `ui.*`，三层化改名批次未做——它的 `_meta`
自己记了这件事。加新语言的四步见 `docs/I18N.md` §7。

---

## 6. 已知边界（如实记录，不粉饰）

- **`control_panel.cpp` 2910 行**（`wc -l src/ui/control_panel.cpp`）。
  REFramework 按钮编排刻意未抽：模态确认本就是 UI 关注点，硬抽只会把
  `QMessageBox::question()` 包进假 VM。证据：该文件 4 处模态确认
  （`:2244` `:2272` `:2320` `:2353`），QMessageBox 相关共 17 行命中。
  已抽出的三块见 §3（进程生命周期、窗口布局、RF 状态行派生）。
- **`MhwReader::` 实现横跨 5 个目录的 .cpp，是一个 façade**：
  `grep -rn 'MhwReader::' src --include=*.cpp` 命中 63 处，分布在 7 个文件：
  `src/mhw_reader.cpp`、`src/monster/monster_reader.cpp`、
  `src/player/player_reader.cpp`、`src/quest/quest_reader.cpp`、
  `src/world/world_reader.cpp`、`src/rise/mhr_reader.cpp`、`src/main.cpp`。
  其中按返回类型识别的成员函数定义 19 处（如 `readMonsters` 在
  `src/monster/monster_reader.cpp:390`、`readParty` 在
  `src/player/player_reader.cpp:500`）；`mhr_reader.cpp` 的 40 处是**调用**
  静态/公有 helper，不是定义。类声明在 `src/mhw_reader.h:101-241`。
- **`mhw_reader.cpp` 的函数内 static PID 缓存被两个 reader 实例共享**：
  `src/mhw_reader.cpp:287-289` 的 `static qint64 cachedPid` /
  `static qint64 lastScanMs` / `static QString cachedExeName`，
  5 s 有效窗口，`exeName` 变化即失效并重扫（`:294-298`）。World 与 Rise 各自
  持有一个 `MhwReader` 实例（`src/main.cpp:381` 构造 worldReader），因此两者
  共享这份缓存。**已知隐患，未修**。
- **`Panel::onSnapshot()` 是死钩子**：`grep -rn 'onSnapshot' src/ tests/` 只有
  `src/ui/panel.h:142` 一处定义，零调用、零覆写。主循环自己调
  `panel->update(snap)` / `monsterPanel.update(...)` / `damagePanel.updateRiseDamage(...)`
  （`src/main.cpp:498` `:503` `:520` `:583` `:584`），没走这个虚函数。
- **`MonsterPartListBuilder` 无直接单测**（§3 末）。
- **`src/ui/locale_conf` / `locale_sync` 不在 monster-core**（§2 注），
  加用到它们的新 target 时要自己列源文件。
- **其余大文件**（`wc -l src/ui/*.cpp`）：`control_panel.cpp` 2910、
  `panel_player.cpp` 1650、`panel_monster.cpp` 1519、`panel_damage.cpp` 586、
  `hud_canvas.cpp` 587、`panel_pet_damage.cpp` 451、
  `rise_reframework_bridge.cpp` 283。

---

## 7. 常见改动路线

| 想做的事 | 该动哪里 |
|---|---|
| 加一个新 overlay 面板 | 新 `src/ui/panel_x.{h,cpp}`：继承 `Panel`、实现 `paintPanel()` 与 `update()` 重载；i18n key 加进 `overlay.json`；源文件列进 `CMakeLists.txt` 的 mhw-ui 块，并在 `src/main.cpp` 装配 |
| 改 DPS / 折线图算法 | `src/ui/viewmodel/damage_view_model.{h,cpp}`；测试加进 `tests/damage_view_model_tests.cpp` |
| 改部位卡聚合 / AutoHide | `src/ui/viewmodel/monster_view_model.{h,cpp}`（守住 "ONE call per paint"） |
| 改 reader 的地址 / 字段 | 对应域 reader 的 .cpp（见 §2）；加字段改 `src/core/game_snapshot.h` |
| 加一种新语言 | `docs/I18N.md` §7：建 locale 目录 + 四个域文件 + qrc 列出 + 跑 parity 脚本 |
| 改窗口布局持久化 | `src/ui/viewmodel/console_layout_store.{h,cpp}` + `tests/console_layout_store_tests.cpp` |
| 改 REFramework 卡片文案 | `src/ui/viewmodel/rise_reframework_status.{h,cpp}` + `tests/rise_reframework_status_tests.cpp` |
| 改 i18n key / 域切分 | `docs/I18N.md` 是权威；门禁脚本在 `scripts/i18n_*.py` |
| 新增 exe / 测试 target | 记得把 `src/resources/resources.qrc` 与 `assets/icons.qrc` 列进自己的源列表（§2 末） |

---

## 附录：验证记录

以下命令在 worktree `/homea27exe/experiment/monster-overlay-v0.11.0/wt-t19`
（HEAD `0dac433`）执行。输出摘要：

| 命令 | 摘要 |
|---|---|
| `grep -n 'add_library' CMakeLists.txt` | 3 个：`:27` monster-core、`:69` mhw-reader、`:102` mhw-ui |
| `grep -c 'add_executable' CMakeLists.txt` | 43 个 exe target |
| `grep -n 'target_link_libraries' CMakeLists.txt` | 三个库分别在 `:58` `:79` `:148-154` |
| `grep -n 'qrc' CMakeLists.txt` | 无 `add_library` 块含 qrc；`resources.qrc` 22 处、`icons.qrc` 12 处，全部在 exe target |
| `grep -rn 'include "ui/' src/core src/monster src/player src/quest src/world` | 无输出（core/reader 不依赖 UI） |
| `grep -rn 'include "mhw_reader' src/ui/` | 无输出（UI 不 include reader 头；`panel.h:16-18` 前置声明 `GameSnapshot`） |
| `wc -l`（三个 `add_library` 块列出的全部源文件） | monster-core 28 文件 / 9041 行；mhw-reader 7 / 2690；mhw-ui 43 / 13423 |
| `grep -n '#include' src/ui/viewmodel/*.h src/ui/viewmodel/*.cpp` | 无 QWidget / QPainter / QtWidgets；仅 `console_layout_store.cpp:8` 有 `<QSettings>`，且只在 .cpp |
| `wc -l src/ui/viewmodel/*` | 10 文件 / 1964 行；各类行数见 §3 表 |
| `grep -rn 'onSnapshot' src/ tests/` | 仅 `src/ui/panel.h:142` 定义，零调用 |
| `grep -rn 'MhwReader::' src --include=*.cpp` | 63 处 / 7 个文件；按返回类型识别的成员函数定义 19 处 |
| `grep -n 'static' src/mhw_reader.cpp` | `:287` `:288` `:289` 三行函数内 static PID 缓存 |
| `wc -l src/ui/control_panel.cpp` | 2910 |
| `grep -n 'QMessageBox' src/ui/control_panel.cpp` | 17 行命中，含 4 处 `QMessageBox::question()` 模态确认 |
| `find src/resources/i18n -type f` | 8 个文件：2 locale × 4 域 |
| `python3` 逐层拍平统计各域叶子 key | 45 / 138 / 144 / 56（zh-CN），en-US 同数 |
| `wc -l tests/{damage_view_model,rise_reframework_status,console_layout_store}_tests.cpp` | 559 / 363 / 311 |
| `grep -rln 'MonsterPartListBuilder' tests/` | 只命中 `tests/snap_gauge_fill.cpp`（间接引用，无直接单测） |
| `grep -n '250' src/main.cpp` | `:153` 默认轮询间隔 250 ms |
| `wc -l src/ui/*.cpp` | 最大的四个：`control_panel.cpp` 2910、`panel_player.cpp` 1650、`panel_monster.cpp` 1519、`panel_damage.cpp` 586 |
