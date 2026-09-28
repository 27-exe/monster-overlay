# Monster Overlay 架构（v0.11.2）

> 本文记录当前 HEAD `90e41ce` 的代码分层与数据流，目标是让下一个改这个项目的人一眼看到
> 「我想加一个新面板 / 改 DPS 算法 / 加一种语言，该动哪些文件」。
>
> 本文的每个论断都带 `file:line` 或可复现的 grep / wc 证据；行数是 `wc -l`
> 实测值，不是估算。验证命令汇总见文末「附录：验证记录」。
>
> **版本口径**：本文此前记为 v0.11.0（文档最后一次实质更新是 `4b9e9b4`）。此后
> CHANGELOG 追加了 `[0.11.1]` 与 `[0.11.2]`，`CMakeLists.txt:2` 现为
> `project(monster_overlay VERSION 0.11.2 ...)`，代码侧又落了 14 个 commit
> （`src/world/` 迁移、数据 TU 拆出、viewmodel 层扩容等）。本文已按 HEAD
> `90e41ce` 全面重测，下文所有行号/统计都是这个 HEAD 上的实测值。
>
> 相关文档：`docs/I18N.md` 是本地化的唯一规范（权威，本文不重复、不与之冲突），
> `docs/USAGE.md` 是用户操作指南。

---

## 1. 总览

### 1.1 目录树（HEAD `90e41ce` 实测）

```
src/
├── main.cpp                    652    # 装配各 panel/reader + 主循环（pollGame）
├── main_control.cpp            221    # monster-control 角色入口
├── core/                       19 文件 / 4000 行
│   ├── locale_conf.h locale_sync.h      ← 唯一两个没进任何 add_library 的头
│   └── （其余 9 组 .h/.cpp，含 process_memory / string_table / game_detector）
├── rise/                       16 个目录项 / 17 文件 / 5725 行
│   ├── mhr_reader.cpp 1529 + .h 626     ← 本目录最大
│   ├── mhr_abnormalities.{h,cpp} 225+898
│   ├── rise_damage_reader.{h,cpp} 50+506
│   ├── rise_damage_roster.{h,cpp} 19+169
│   ├── mhr_part_names.{h,cpp} 71+133
│   ├── mhr_monster_names.{h,cpp} 67+77
│   ├── mhr_types.h 308 / rise_damage_types.h 111
│   ├── part_schemas.cpp 96              ← Rise 3 张表，78cbb3e 新建
│   └── data/  mhr_part_names_data.cpp 682 + mhr_monster_names_data.cpp 158
├── world/                      8 文件 / 2861 行
│   ├── world_reader.cpp 304 + .h 174    ← MhwReader façade，见 §2/§6
│   ├── monster_reader.cpp 1038           ← 4 处 MhwReader:: 成员定义
│   ├── player_reader.cpp 788            ← 6 处
│   ├── world_types.cpp 362 + .h 91
│   ├── quest_reader.cpp 55             ← 1 处
│   └── world_severable_scan.h 49
├── monster/                    5 个目录项 / 4 个源 372 行（另有 data/）
│   ├── monster_types.h 323
│   ├── target_selector.{h,cpp} 15+23
│   ├── part_schemas.cpp 11              ← 11 行空壳，仅注释 + 空 namespace
│   └── data/part_schemas_data.cpp 964   ← 真数据在此，gen_schema.py 就地改写这份
├── player/                     1 文件 / 245 行   ← 不是模块，是跨游戏契约纸（§1.2）
│   └── player_types.h
├── quest/                      1 文件 / 61 行    ← 同上
│   └── quest_types.h
├── ui/                         49 文件 / 13833 行（48 个进 mhw-ui 库）
│   ├── control_panel.cpp 2857          ← 全项目最大的源文件
│   ├── panel_player.cpp 1562 / panel_monster.cpp 1519 / panel.cpp 741
│   ├── panel_damage.cpp 586 / hud_canvas.cpp 587 / panel_pet_damage.cpp 451
│   ├── rise_reframework_bridge.cpp 293 / screen_query.cpp 366
│   ├── 其余小件：section_row / toggle_chip / section_count_bar / ui_theme /
│   │             formatters / icon / panel_source / panel_player_metrics.h /
│   │             panel_sections.h
│   └── viewmodel/  15 文件 / 2438 行   ← §3
└── resources/
    ├── resources.qrc
    └── i18n/{zh-CN,en-US}/{overlay,console,reader,data}.json   ← 8 个，见 §5
```

`src/monster/`、`src/player/`、`src/quest/` 三个目录今天都已经**不是**「有自己
.cpp 的模块」：`.cpp` 全部搬去了别处（`src/world/`）或者只剩空壳。其中
`src/monster/` 还留着 `monster_types.h` + `target_selector` 是共享数据类型；
`src/player/` 与 `src/quest/` 则纯粹是跨游戏契约纸，见下一节。

### 1.2 `src/player/` 与 `src/quest/` 不是模块，是跨游戏契约纸

这两个目录**不是「没写完的模块」**，也不是 `.cpp` 被漏搬——它们本来就不该有
模块身份：

- 它们各自只剩一个纯类型头：`src/player/player_types.h`（245 行）与
  `src/quest/quest_types.h`（61 行），里面只有 `PlayerSnapshot` /
  `PartyMemberSnapshot`（+ `AbnormalityAccent` / `AbnormalityKind` /
  `PlayerAbnormalitySnapshot` / `WirebugSnapshot`）与 `QuestSnapshot` 两个 struct
  （`src/player/player_types.h:99`、`src/quest/quest_types.h:9`）。
- 原先的 `.cpp` 已搬到 `src/world/`（`player_reader.cpp` 788 行、
  `quest_reader.cpp` 55 行）。搬的理由很硬：那两个 `.cpp` 里定义的每一个函数
  都是 `MhwReader::` 成员（`readPlayer` / `readParty` / `readSharpness` /
  `readSessionPlayerCount` / `readQuest`，逐个见 §6），而 `MhwReader` 的类声明住
  在 `src/world/world_reader.h:12`。成员函数定义必须和 `#include` 它的上下文
  一起留在 `mhw-reader` 库里，因此它们属于 `src/world/`，而不是
  `src/player/`。
- 构建系统也已确认这件事：`grep -c 'src/player\|src/quest' CMakeLists.txt`
  **= 0**。没有任何 target 从这两个目录取源文件。
- 它们被 core / rise / world / ui 四方共用：`AbnormalityAccent` 在
  `src/rise/mhr_abnormalities.cpp` 与 `src/rise/mhr_types.h` 里用（Rise），
  `PlayerSnapshot` / `QuestSnapshot` 被 `src/core/game_snapshot.h`、
  `src/rise/mhr_reader.{h,cpp}`、`src/world/{world_reader,player_reader}.cpp`
  与 `src/ui/panel_player.{h,cpp}` 同时 include。正因为**没有单一的游戏目录
  可以安置一份 Rise 和 World 都要用的类型**，它们才住在独立目录里。

所以：往这里加东西之前，先判断你要加的是「类型」还是「行为」。类型（新 snapshot
字段、新枚举）——直接改头文件，World/Rise 两边都会看到；行为（读内存、读
JSON）——改 `src/world/*_reader.cpp` 或 `src/rise/mhr_reader.cpp`，不要在这两个
目录里新建 `.cpp`。

### 1.3 三个编译单元

三个编译单元（`CMakeLists.txt:27`、`:82`、`:115` 的 `add_library`）加上若干可执行
target。依赖方向严格单向：

```
                  monster-core (STATIC)
                  src/core/* + src/rise/* + world/world_types
                  + monster/part_schemas + monster/data/part_schemas_data
                  + rise/part_schemas + rise/data/*_data + core/process_memory
                  Qt6::Core, Threads::Threads          (CMakeLists.txt:71)
                        |
                        | PUBLIC
                        v
                  mhw-reader (STATIC)
                  src/world/world_reader.* + src/world/ 下三个域 reader
                  Qt6::Core, monster-core              (CMakeLists.txt:92)
                        |
                        | PUBLIC
                        v
                  mhw-ui (STATIC)
                  src/ui/* + src/ui/viewmodel/*
                  Qt Gui/Widgets/Svg + LayerShellQt
                  + monster-core + mhw-reader          (CMakeLists.txt:166-174)
                        |
                        | PRIVATE
                        v
              每个 exe target 把 resources.qrc / icons.qrc 列在自己的源列表里
              （qrc 不下沉，见 §2 末）
```

反向依赖为零：`grep -rn 'include "ui/' src/core src/monster src/player
src/quest src/world` 无输出；reader 层同样不 include UI 头——注意这条得用当前
文件名来问：`grep -rn 'include "world/world_reader' src/ui/` 同样无输出
（mhw-reader 的头只在 UI 之外被 include；UI 只前置声明 `mhw::GameSnapshot` 于
`src/ui/panel.h:14-16`，以及 include monster-core 的 `world/world_types.h`）。

本地化横跨三层：`mhw::StringTable` 在 monster-core 里（`src/core/string_table.h`），
调用方从 core 一路铺到 ui/viewmodel：`src/core/locale_sync.h`、
`src/core/reframework_fetcher.cpp`、`src/core/rise_reframework_manager.cpp`、
`src/main.cpp`、`src/main_control.cpp`、`src/monster/monster_types.h`、
`src/rise/mhr_abnormalities.{h,cpp}`、`src/rise/mhr_{part,monster}_names.{h,cpp}`、
`src/rise/mhr_reader.cpp`、`src/ui/**`（含 viewmodel 的
`console_text_helpers.{cpp,h}`、`monster_view_model.cpp`、
`rise_reframework_status.cpp`）以及 reader 层的 `src/world/world_reader.cpp` /
`src/world/monster_reader.cpp` / `src/world/player_reader.cpp` /
`src/world/world_types.cpp`。
（`docs/I18N.md` 被 8 个文件共 8 处引用：`src/core/string_table.h:32`、
`src/core/process_memory.cpp:33`、`src/world/world_reader.cpp:23`、
`src/ui/viewmodel/console_text_helpers.cpp:19`、`src/ui/panel_monster.cpp:1448`、
`src/ui/rise_reframework_bridge.cpp:25`、`src/ui/panel_player.cpp:29`、
`scripts/i18n_split_domains.py:6`。）

---

## 2. 库边界（`CMakeLists.txt`）

| 库 | 源文件数 | 实测行数（`wc -l`） | PUBLIC 依赖 |
|---|---|---|---|
| `monster-core` STATIC | 37 | 10828 | `Qt6::Core`、`Threads::Threads`（`CMakeLists.txt:71`） |
| `mhw-reader` STATIC | 7 | 2431 | `Qt6::Core`、`monster-core`（`CMakeLists.txt:92`） |
| `mhw-ui` STATIC | 48 | 13583 | `Qt6::Core` `Qt6::Gui` `Qt6::Widgets` `Qt6::Svg` `LayerShellQt::Interface` `monster-core` `mhw-reader`（`CMakeLists.txt:166-174`） |

> 行数是把对应 `add_library()` 块列出的全部源文件 `wc -l` 后求和，与
> 「目录里有哪些文件」不是同一口径：`src/ui` 下实际有 49 个源文件，但只有 48 个
> 进了库（`src/ui/viewmodel/panel_mask_codec.h` 由它自己的测试 target 直接列举，
> 见 §3）；反过来 `src/core/game_snapshot.h`（37 行）是纯数据头、不进任何库，
> 由 13 个 include 它的 TU 各自编译。

`monster-core` 的清单按其自身结构描述：

- `src/core/*` 9 组：`string_table`、`game_detector`、`game_profile`、
  `steam_game_locator`、`rise_reframework_manager`、`reframework_fetcher`、
  `map_paths`、**`process_memory`**。共 16 个文件 / 4000 行。
- `src/rise/*` 13 个：`mhr_reader.{h,cpp}`（626+1529）、
  `mhr_abnormalities.{h,cpp}`（225+898）、`mhr_part_names.{h,cpp}`（71+133）、
  `mhr_monster_names.{h,cpp}`（67+77）、`rise_damage_reader.{h,cpp}`（50+506）、
  `rise_damage_roster.{h,cpp}`（19+169）、`mhr_types.h`（308）、
  `rise_damage_types.h`（111）。
- `src/rise/data/` 2 个数据 TU：`mhr_part_names_data.cpp`（682）、
  `mhr_monster_names_data.cpp`（158）。
- `src/rise/part_schemas.cpp`（96，Rise 的 3 张表：`kRiseAilmentNames` 等）。
- `src/world/world_types.{h,cpp}`（91+362）。
- `src/monster/part_schemas.cpp`（**11 行空壳**）+ `src/monster/data/part_schemas_data.cpp`（**964 行**）。

`src/monster/part_schemas.cpp` 从 1047 行变成 11 行空壳，数据整表搬到了
`src/monster/data/part_schemas_data.cpp`（964 行）；空壳里只剩注释，说明
「数据 TU 是哪一个、`scripts/gen_schema.py` 就地改写哪一份」，加 World 怪物
数据表时请改 data/ 那份，空壳是给 include 它做符号引用的落点。
`src/monster/target_selector.{h,cpp}`（15+23）则**不在** monster-core 里，归
mhw-reader。

### `monster-core`（`CMakeLists.txt:27`）

**数据 TU 的三次拆出**是 v0.11 最重要的一次结构变化，三份表格的拆分形状一致
（「数据 TU + 逻辑 TU」），但有三处细节差异值得记住：

1. `src/rise/mhr_part_names.{h,cpp}`：`mhr_part_names.cpp` 原本 764 行，其中
   598 行是一张 `constexpr` 表。数据整表搬到
   `src/rise/data/mhr_part_names_data.cpp`（682 行），原文件只剩 133 行逻辑
   （`risePartNameEntry()` 的 `lower_bound` 二分 + 两个 locale 包装 + 显示名/格式化
   助手）。**这次必须改头文件**：598 行表原来在匿名 namespace 里，内部链接，搬到
   另一个 TU 后逻辑 TU 看不见它。解法是把 `struct RisePartName` 与
   `extern const std::array<RisePartName, 598> kRisePartNames;` 提升到
   `mhr_part_names.h:39`，数据 TU 里的定义保留 `constexpr`（两个 static_assert 因此
   仍能逐行审查提交进来的数据），逻辑 TU 经 `extern const` 引用同一份定义。
   `nm` 证明跨 TU 链接成立：数据 TU 里是 `D mhw::kRisePartNames`，逻辑 TU 里是
   `U mhw::kRisePartNames`。责任分工写进了生成器的文档头：`--cpp-out` 现在默认
   指向数据 TU，`--check` 实测通过（三个输入 sha256 与记录一致）。
2. `src/rise/mhr_monster_names.{h,cpp}`：同一形状的第二次应用，数据在
   `src/rise/data/mhr_monster_names_data.cpp`（158 行），
   `extern const std::array<RiseMonsterName, 79> kRiseMonsterNames;` 声明在
   `mhr_monster_names.h:59`。
3. `src/monster/data/part_schemas_data.cpp`：World 侧的对应物。逻辑 TU
   `src/monster/part_schemas.cpp` 收缩到 11 行，五个表（`kPartSchemas` /
   `kAilmentNames` / `kAilmentNamesEn` / `kCrownThresholds` /
   `kMonsterCaptureThresholds`）全在 data/ 那份里，CMake 注释明确
   「`scripts/gen_schema.py` rewrites THIS file in place」。

**S3 把进程内存原语下沉到了这里**：`ProcessMemory`、`AddressMap` 和四个
stateless 助手（`findGamePid` / `selectLoadableMap` / `followPointerChain` /
`followPointerChainOffsetThenDeref`）现在住在 `src/core/process_memory.{h,cpp}`
（142 + 268 行）。它们原先定义在顶层 Reader 的头/实现（`mhw_reader.{h,cpp}`，
已随 `8a12d5c` 删除，落地为 `src/world/world_reader.{h,cpp}`），但 Rise 的 reader
（同在 monster-core 的 `src/rise/mhr_reader.cpp`）也要用，于是形成了
「World 的 mhw-reader 被 Rise 依赖」的倒置。搬过来之后倒置消失。为了不破坏
World 侧已有的调用点拼写，`src/world/world_reader.h:20-55` 保留了四个 static 委托
成员（`MhwReader::findGamePid` 在 `:29`、`selectLoadableMap` 在 `:36`、
`followPointerChain` 在 `:40`、`followPointerChainOffsetThenDeref` 在 `:50`），
实现仍只有 core 里那一份；它们只是转调 `mhw::` 自由函数。

`src/core/game_snapshot.h`（37 行）不在 `add_library` 列表里：它是纯数据结构，
被 13 个头文件 include（`grep -rln 'core/game_snapshot.h' src/`：
`core/game_detector.h`、`core/process_memory.h`、`main.cpp`、`monster/monster_types.h`、
`rise/mhr_reader.h`、`rise/rise_damage_roster.h`、`ui/control_panel.h`、`ui/icon.h`、
`ui/panel_damage.h`、`ui/panel_player.h`、`ui/viewmodel/damage_view_model.h`、
`ui/viewmodel/overlay_process_controller.h`、`ui/viewmodel/player_view_model.h`），
随各自的 TU 编译。它把四方契约拼在一个 struct 里：`MonsterSnapshot` 来自
`monster/`，`PlayerSnapshot` / `PlayerAbnormalitySnapshot` / `AbnormalityAccent`
来自 `player/`，`QuestSnapshot` 来自 `quest/`，`Zone` 来自 `world/`——这也是
为什么 §1.2 那两个目录必须独立存在。

`src/core/locale_conf.h` 与 `src/core/locale_sync.h` 也不在该库里
（`grep -n 'locale_conf\|locale_sync' CMakeLists.txt` 只命中 `:654-667`，
全部属于 monster-locale-tests target；另有 `:762` `:782` 是
panel-mask-codec-tests / console-layout-store-tests target 的注释提及），
由用到它们的 target 自行列出。

### `mhw-reader`（`CMakeLists.txt:82`）

`src/world/world_reader.{h,cpp}`（174 + 304）+ `src/world/monster_reader.cpp`
（1038）+ `src/world/quest_reader.cpp`（55）+ `src/world/player_reader.cpp`
（788）+ `src/world/world_severable_scan.h`（49）+
`src/monster/target_selector.cpp`（23）。合计 7 源 / 2431 行。

顶层的 `mhw_reader.{h,cpp}` 已随 commit `8a12d5c` 整份删除，由
`src/world/world_reader.{h,cpp}` 取代；同一次迁移把三个域 reader 的 `.cpp`
从 `src/monster/` / `src/player/` / `src/quest/` 搬进了 `src/world/`（原因见
§1.2：它们定义的全是 `MhwReader::` 成员）。四个 static 委托成员现在是薄委托，
不是实现（`src/world/world_reader.h:20-55`，见上文 S3 段）。

`src/rise/mhr_reader.cpp` 在 monster-core 里，不在这里：Rise 的 reader 读的是
REFramework 写的 JSON 文件，不是进程内存，所以它属于核心层。但它仍然通过
monster-core 里的 `findGamePid()` / `ProcessMemory` 附着游戏进程并读
abnormality 内存块（`src/rise/mhr_reader.cpp:188` `:202`，
`MhrReader::poll()` 在 `:1386`）。

### `mhw-ui`（`CMakeLists.txt:115`）

`src/ui` 下共有 **49 个源文件**（`find src/ui -name '*.cpp' -o -name '*.h'`：
非 viewmodel 的 34 个 + viewmodel 的 15 个），其中 **48 个**列进了
`add_library(mhw-ui)`；唯一的例外是 `src/ui/viewmodel/panel_mask_codec.h`
（250 行）——它没进库列表，只被 `panel-mask-codec-tests` target 直接列举
（`CMakeLists.txt:792`）。它 header-only（4 个自由函数 + 1 个 struct 全 inline），
所以不列也不影响链接，但这意味着新 target 想用它得自己列源。

它持有 screen_query.cpp 需要的私有 QtGui 头（`qpa/qplatformscreen.h`）：
该块在 `CMakeLists.txt:177-202`（`file(GLOB _qt6_gui_version_dirs …)` 在
`:185-195`，找不到时的 `FATAL_ERROR` 在 `:196-198`，`target_include_directories
(mhw-ui PRIVATE …)` 在 `:199-201`），所以桌面后端屏幕尺寸的探测只有这里编译
一次。

### qrc **不下沉**

`add_library()` 块里没有任何 qrc：`grep -n 'qrc' CMakeLists.txt` 的命中全部落在
exe target 的源列表里。`src/resources/resources.qrc` 被 22 处、
`assets/icons.qrc` 被 12 处列出（如 `CMakeLists.txt:218-219`，即
`monster-overlay` target 的源列表）。

实测 INTERFACE 库与 STATIC 库持有 qrc 都是「configure/build 0 error，运行时
`QFile(":/...").open()` 失败」——AUTORCC 的注册代码不传播、静态库的 `qrc_*.cpp`
未被 exe 直接引用而被链接器丢弃。实测过程与结论记录在
`/home/a27exe/experiment/monster-overlay-v0.11.0/QRC-SINK-INFEASIBLE.md`
（仓库外、git 未跟踪）。**这是 Qt 的硬约束，不是原项目的疏忽**，加新 target
时记得把两个 qrc 一起列上。

---

## 3. ViewModel 层（`src/ui/viewmodel/`）

零 QWidget / QPainter 依赖。验证：`grep -n '#include' src/ui/viewmodel/*.h
src/ui/viewmodel/*.cpp` 的全部输出里没有任何 widget / painter / QtWidgets
头文件；唯一的 Qt Widgets 邻接物是 `console_layout_store.cpp:8` 的
`<QSettings>`（QtCore，且只出现在 .cpp —— 头文件不 include、不前向声明、
也不在签名里提名该类型，见该行下文的注释块）。`QWidget` / `QPainter` 字样
本身仍会出现在**四个头文件**的**注释**里（说明「本类型不含它们」：
`console_text_helpers.h`、`monster_view_model.h`、`panel_mask_codec.h`、
`player_view_model.h`；其中 `QPainter` 字样只在 `player_view_model.h`），
但那不是依赖。

合计 15 个文件 / 2438 行（`ls src/ui/viewmodel/ | wc -l` = 15；
`wc -l src/ui/viewmodel/*` = 2438 total）。

| 类 / 函数（头 `.h` 行 / 实现 `.cpp` 行） | 抽自如 | 职责与边界 |
|---|---|---|
| `DamageViewModel`（134 / 620） | DamagePanel | 伤害/DPS 统计引擎 + 折线图 history。`computeDps()` 是原面板的 DPS 公式：250 ms 轮询 → ×4 每秒，`src/ui/viewmodel/damage_view_model.cpp:605-618`；`.cpp` 共 12 处 `emit changed()`。依赖 `core/game_snapshot.h` + `rise/rise_damage_types.h` + QtCore |
| `MonsterPartListBuilder`（220 / 337） | MonsterPanel | 部位卡（`.pgrid`）的**唯一**装配器。持有可变 `partTrack_`（2048 项，`src/ui/viewmodel/monster_view_model.h:179`）实现 HunterPie 15 s PartAutoHide。`build()` 会 mutate `partTrack_`，故每帧 paint 只许调一次（`src/ui/viewmodel/monster_view_model.h:153-157`） |
| `OverlayProcessController`（95 / 157） | ControlPanel | 子进程生命周期：`launch()` 追加 `--game=` 后 `QProcess::startDetached()`、`stop()` 发 SIGTERM（`src/ui/viewmodel/overlay_process_controller.cpp:75`）、250 ms `kill(pid,0)` 轮询（`:114`）、`pendingRestart_` 热切换（`:96`）。QProcess/QTimer 刻意允许：进程管理就是这个类的本职 |
| `ConsoleLayoutStore`（127 / 125） | ControlPanel | `ui/*` 窗口布局持久化：6 个键 `ui/geometry` `ui/windowState` `ui/leftSplitter` `ui/zoom` `ui/splitter` `ui/stageHeight`（`src/ui/viewmodel/console_layout_store.cpp:23-28`），7 个调用点（`src/ui/control_panel.cpp:451` `:659` `:664` `:847` `:879` `:906` `:1310`）。不持后端句柄，因此可拷贝、可每次新建 |
| `PlayerViewModel`（61 / 51） | PlayerPanel | v0.11（S1）新增。玩家身份解析器：`resolveIdentity(snap) -> PlayerIdentity{masterRank,name,weaponId,partyCount}`，把原先散在 `PlayerPanel::update()` 里的三块搬进来——player 结构的初始镜像、偏好本地成员字段的 party 覆写循环、无数据时把 `weaponId` 复位为 -1 的兜底。纯函数、const，测试用手搓 snapshot 直接调用（`tests/player_view_model_tests.cpp`）。依赖 `core/game_snapshot.h` + `player/player_types.h` + QtCore |
| `riseReframeworkStatusLines()`（56 / 93） | ControlPanel | 从 `RiseReframeworkStatusInput`（`Status`、`gameDir`、`gameRunning`、`operationPending`、`hasResult`、`resultOk`、`resultDetail`）派生 REFramework 卡片可读行。纯函数，除 `core/string_table.h` 与 `core/rise_reframework_manager.h` 外不碰任何东西 |
| `consoleText()` / `sectionLabel()` / `gameName()`（54 / 58） | ControlPanel | `console_text_helpers.{h,cpp}`，commit `ad46e65` 新建。三个纯文本派发器，1:1 搬自 `src/ui/control_panel.cpp` 顶部的匿名 namespace（原 commit `0a88737` 的 85-403 行）：`consoleText(key)` 走 StringTable、`sectionLabel(panel,index)` 查动态 `src/ui/panel_sections.h` 表、`gameName(id)` 给自动探测徽章与 GAME 列共用一个解析器。**不持任何 widget 依赖**，所以能从普通逻辑测试触达 |
| `PanelMaskSet` / `PanelMaskLineKind` + `classifyPanelMaskLine()` / `parsePanelMaskValue()` / `parsePanelMasks()` / `serializePanelMasks()`（250 / —） | ControlPanel | `panel_mask_codec.h`，90e41ce 新建、header-only（无 .cpp）。`.conf` 里四行 section mask 的文本契约：同序的前缀测试、`toUInt(&ok,16)` 接受规则、旧三行迁移、重复行收敛、行分类与输出顺序，全部 1:1 搬自 `ControlPanel::loadMaskFromDisk` / `saveMaskToDisk`。widget 拿解析结果干什么、碰文件系统、打开失败告警——留在 ControlPanel。注意它 include `ui/panel_sections.h`，仍是纯数据头 |
| `consoleLayoutStore` 之外另有两个纯数据头 | — | `src/ui/panel_sections.h`（7662 B）与 `src/ui/panel_player_metrics.h`（4171 B）都属于 viewmodel 的依赖而非 viewmodel 成员：前者提供 `console_text_helpers` / `panel_mask_codec` 共用的 section 表，后者持有从 `panel_player.cpp` 抬出来的 wirebug 标签 / 体力倍率内联助手 |

### 边界规则

- VM 只依赖 QtCore + 数据层，证据是上表「职责与边界」列的 include 事实。
  `grep -n '#include' src/ui/viewmodel/*.h src/ui/viewmodel/*.cpp` 里没有
  widget / painter / QtWidgets 的 `#include`；`QWidget` / `QPainter` 字样只以
  注释形式出现在 `console_text_helpers.h`、`monster_view_model.h`、
  `panel_mask_codec.h`、`player_view_model.h` 四个头的说明文字里。
- **`canvas()->update()` 留在 View**：`DamageViewModel::changed()` 由 View 连到
  `QWidget::update()`（`src/ui/panel_damage.cpp:161-162`）；其余面板仍用
  `canvas()->update()`（`src/ui/panel_monster.cpp:789` `:796`）或
  `triggerUpdate()`（`src/ui/panel.h:39`）。VM 自己不调 update。
- **PlayerViewModel 的调用点在三处之外还要守一条**：`PlayerPanel` 的成员
  `playerMR_` / `playerName_` / `weaponId_` / `partyCount_` 仍由 panel 持有，
  VM 只返回一个 `PlayerIdentity` 值，panel 自己拷
  （`src/ui/panel_player.cpp:523-527`）。这正是「VM 是形状解析、panel 是所有权」
  的分界，改动身份逻辑时只动 VM 与测试。
- **每帧一次且会 mutate 的调用必须标 "ONE call per paint"**：
  `MonsterPartListBuilder::build()` 的调用点 `src/ui/panel_monster.cpp:1003`
  上方有该注释，`:1371` 另有交叉引用。

### 已有测试覆盖（纯逻辑，无 GUI）

`tests/damage_view_model_tests.cpp`（559 行，文件末 `main()` 里 9 例）、
`tests/rise_reframework_status_tests.cpp`（363 行，10 例）、
`tests/console_layout_store_tests.cpp`（311 行，`check()` 逐段 32 处）、
`tests/player_view_model_tests.cpp`（355 行，`check()` 6 处）、
`tests/panel_mask_codec_tests.cpp`（579 行，16 例）。它们正是 v0.10/v0.11 做这些
抽出的动机：原先这些判定藏在 2916 行的 widget 文件里，状态机不可达。

四个老 ViewModel 单测合计 **1588 行**（`wc -l` 那四个文件）；加上
`panel_mask_codec_tests.cpp` 后是 2167 行。
全项目 ctest 注册 **41 个 case**：38 个 `add_test(NAME …)` 加 3 个由
`foreach` 生成的 i18n 门禁（`check` / `parity` / `unused`，
`CMakeLists.txt:920` `set(MHW_I18N_GATES)`、`:922` `foreach`，即 `:915-929`
那一块）。

`MonsterPartListBuilder` **没有**直接单测：`grep -rln 'MonsterPartListBuilder'
tests/` 只命中 `tests/snap_gauge_fill.cpp` 的间接引用。跨帧的 PartAutoHide
状态机目前只被离屏回归测试间接覆盖。

---

## 4. 数据流向

```
  pollGame()                                       — src/main.cpp:409-411
    （World: MhwReader::poll()，读进程内存，src/world/world_reader.cpp:215；
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
  reader 头（§1.3 的 grep 结果）。GameSnapshot 有两个对等的 reader 入口：
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
| `reader.json` | `reader` | 144 | 顶层 14 个平铺 key 加 `reader.reframework` 一组 130 个 |
| `data.json` | `data` | 56 | `data.mantle.id.*` 21、`data.demo.*` 35（`monster_name`/`player_name`/`status` 3 个平铺 key 加 6 个二级组：`abn` 9 / `ail` 4 / `buff` 4 / `debuff` 5 / `part` 6 / `party` 4） |

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
`reframework` 51、`inspector` 17（13 个平铺 key 加 `inspector.sub` 4）、
`rail` 12。另有 `console.section` 下有四个第三层组 `player`(8) / `monster`(6) /
`damage`(4) / `pets`(2)，共 20 个 key——所以它是四域中唯一已经三层化到
`panel`/`section` 层级的。这 51 个 REFramework key 是 v0.11 前后两批新加的
（`result_*` / `menu_state_*` / `state_*` 三族），文档此前记的 138 已失效。

---

## 6. 已知边界（如实记录，不粉饰）

- **`control_panel.cpp` 2857 行**（`wc -l src/ui/control_panel.cpp`）。
  REFramework 按钮编排刻意未抽：模态确认本就是 UI 关注点，硬抽只会把
  `QMessageBox::question()` 包进假 VM。证据：该文件 4 处模态确认
  （`:2257` `:2285` `:2333` `:2366`），QMessageBox 相关共 17 行命中。
  已抽出的三块见 §3（进程生命周期、窗口布局、RF 状态行派生）；ad46e65 又抽走了
  三个纯文本派发器，90e41ce 抽走了 mask 文件读写，所以这个数比本文档历史记录里的
  2916 已经小了几十行，但仍是全项目最大的源文件。
- **`MhwReader::` 实现横跨 5 个 .cpp，是一个 façade**：
  `grep -rn 'MhwReader::' src --include=*.cpp` 命中 19 处，分布在 **5 个文件**
  （其中 4 个在 `src/world/`）：`src/world/monster_reader.cpp`（4）、
  `src/world/player_reader.cpp`（6）、`src/world/quest_reader.cpp`（1）、
  `src/world/world_reader.cpp`（7）、`src/main.cpp`（1）。
  19 处的构成：**16 处成员函数定义**（其中 `readMonsters` 在
  `src/world/monster_reader.cpp:390`、`readParty` 在
  `src/world/player_reader.cpp:500`）+ 1 处构造函数
  （`src/world/world_reader.cpp:132`）+ 1 处调用（`src/main.cpp:379` 的
  `MhwReader::selectLoadableMap`）+ 1 处注释提及（`player_reader.cpp:468`）。
  类声明在 `src/world/world_reader.h:12-173`（`class MhwReader` 在 `:12`，
  闭括号在 `:173`）。
  > 计数口径请注意：按「返回类型 + `MhwReader::`」正则数定义会得到 14 而不是 16——
  > 漏掉的是 `const QString &MhwReader::mapPath() const`
  > （`src/world/world_reader.cpp:138`）与
  > `QString MhwReader::readUtf8(...) const`（`:206`），因为返回类型以 `&` 结尾时
  > 正则要求类型与限定名之间有一个空格，而这两行是 `&MhwReader::` 无空格形式。
  > 本文按人工核对的 16 处写。
  对比 S3 之前是 63 处 / 7 文件 / 19 处定义：当时 `src/rise/mhr_reader.cpp` 一家
  独大 40 处（全是 `MhwReader::followPointerChain` 等静态助手的调用），随着
  助手搬进 monster-core、调用点改拼成不带 `MhwReader::` 前缀的 `mhw::findGamePid`
  形式，这 40 处已全部归零，`mhr_reader.cpp` 从命中列表里整份消失。
- **进程内 static PID 缓存被两个 reader 实例共享**：三个 static
  （`static qint64 cachedPid` / `static qint64 lastScanMs` /
  `static QString cachedExeName`）现在位于
  `src/core/process_memory.cpp:173-175`。S3 之前它们在 `mhw_reader.cpp`
  的 287-289 行——那份文件已随 `8a12d5c` 整份删除（git:
  `git log --oneline --diff-filter=D -- mhw_reader.cpp mhw_reader.h`
  → `8a12d5c`），所以旧行号不只是越界、连文件都没了，别再拿它当索引。
  **它们仍是同一个函数局部 static、同一份缓存**，只是随 `findGamePid()`
  一并搬进了 monster-core 的实现文件——不是被换成了类成员，也没有按实例拆开。
  5 s 有效窗口，`exeName` 变化即失效并重扫（`:180-184`）。World 的
  `MhwReader` 与 Rise 的 `MhrReader` 各自持有一个实例
  （`src/main.cpp:380` 构造 worldReader，`:389` 构造 riseReader），两个实例都要
  调这份 `findGamePid()`，因此共享同一份 static 缓存。**已知隐患，未修**。
- **`Panel::onSnapshot()` 是死钩子**：`grep -rn 'onSnapshot' src/ tests/` 只有
  `src/ui/panel.h:142` 一处定义，零调用、零覆写。主循环自己调
  `panel->update(snap)` / `monsterPanel.update(...)` / `damagePanel.updateRiseDamage(...)`
  / `petDamagePanel.updateRiseDamage(...)`
  （`src/main.cpp:497` `:502` `:519` `:581` `:603` `:605`），没走这个虚函数。
- **`MonsterPartListBuilder` 无直接单测**（§3 末）。
- **`src/core/locale_conf.h` / `src/core/locale_sync.h` 不在 monster-core**（§2 注），
  加用到它们的新 target 时要自己列源文件。
- **其余大文件**（`wc -l src/ui/*.cpp`）：`control_panel.cpp` 2857、
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
| 改 reader 的地址 / 字段 | 对应域 reader 的 .cpp，现在全在 `src/world/`（`world_reader` / `monster_reader` / `player_reader` / `quest_reader`，见 §2）；加字段改 `src/core/game_snapshot.h` |
| 改 PID 查找 / 指针链 / 地址表加载 | `src/core/process_memory.{h,cpp}`（monster-core）。World 侧写 `MhwReader::` 前缀（四个 static 委托在 `src/world/world_reader.h:20-55`）、Rise 侧写 `mhw::` 前缀，但都要改这里这一份实现 |
| 加一种新语言 | `docs/I18N.md` §7：建 locale 目录 + 四个域文件 + qrc 列出 + 跑 parity 脚本 |
| 改窗口布局持久化 | `src/ui/viewmodel/console_layout_store.{h,cpp}` + `tests/console_layout_store_tests.cpp` |
| 改 REFramework 卡片文案 | `src/ui/viewmodel/rise_reframework_status.{h,cpp}` + `tests/rise_reframework_status_tests.cpp` |
| 改玩家 MR/名/武器/party 的合并规则 | `src/ui/viewmodel/player_view_model.{h,cpp}` + `tests/player_view_model_tests.cpp` |
| 改控制台纯文本（节标签/游戏名/console key 解析） | `src/ui/viewmodel/console_text_helpers.{h,cpp}`（1:1 搬自 `control_panel.cpp`，别在 View 里再抄一份） |
| 改 `.conf` 面板 mask 的文本格式 | `src/ui/viewmodel/panel_mask_codec.h`（header-only）+ `tests/panel_mask_codec_tests.cpp`；文件系统读写与告警留在 `control_panel.cpp` |
| 加 World / Rise 数据表 | 改 `src/monster/data/part_schemas_data.cpp` / `src/rise/data/mhr_{part,monster}_names_data.cpp`（`gen_schema.py` 就地改写的那份），**不要**改 11 行的 `src/monster/part_schemas.cpp` 空壳 |
| 在 rise / world / ui 之间共享类型 | 放进 `src/player/` 或 `src/quest/` 这类跨游戏契约纸目录（§1.2），或 `src/monster/monster_types.h`；**不要**塞进某个游戏目录 |
| 改 i18n key / 域切分 | `docs/I18N.md` 是权威；门禁脚本在 `scripts/i18n_*.py` |
| 新增 exe / 测试 target | 记得把 `src/resources/resources.qrc` 与 `assets/icons.qrc` 列进自己的源列表（§2 末） |

---

## 附录：验证记录

以下命令在 worktree `/home/a27exe/experiment/monster-overlay-v0.11.0/wt-i1`
（detached HEAD `90e41ce`）执行，即本文的基线。本文此前的记录停在 HEAD
`863d8b2`，其后 14 个 commit 未回头更新文档；本次已重测。输出摘要：

| 命令 | 摘要 |
|---|---|
| `grep -n '^add_library' CMakeLists.txt` | 3 个：`:27` monster-core、`:82` mhw-reader、`:115` mhw-ui（旧 `:27` `:69` `:102`） |
| `grep -c 'add_executable' CMakeLists.txt` | 46 个 exe target（旧 43 / 45） |
| `grep -n '^target_link_libraries' CMakeLists.txt` | 三个库分别在 `:71` `:92` `:166-174`（旧 `:58` `:79` `:148-154`） |
| `grep -c 'add_test(NAME' CMakeLists.txt` | 38（旧 37）；加 3 个 `MHW_I18N_GATES` 门禁 = ctest **41**（旧 40） |
| `sed -n '915,929p' CMakeLists.txt` | i18n 门禁 `foreach` 块：`set(MHW_I18N_GATES check parity unused)` 在 `:920`、`foreach` 在 `:922`（旧 `:886-895`） |
| `grep -n 'qrc' CMakeLists.txt` | 无 `add_library` 块含 qrc；`resources.qrc` 22 处、`icons.qrc` 12 处，全部在 exe target（首次例：`:218-219`） |
| `grep -rn 'include "ui/' src/core src/monster src/player src/quest src/world` | 无输出（core/reader 不依赖 UI） |
| `grep -rn 'include "world/world_reader' src/ui/` | 无输出（UI 不 include reader 头；`panel.h:14-16` 前置声明 `GameSnapshot`） |
| `wc -l`（三个 `add_library` 块列出的全部源文件） | monster-core **37 文件 / 10828 行**；mhw-reader **7 / 2431**；mhw-ui **48 / 13583**（旧 28/9041、35/10741、7/2690、43/13423） |
| `find src/ui -name '*.cpp' -o -name '*.h' \| wc -l` | **49**（非 viewmodel 34 + viewmodel 15）；48 个进库，`panel_mask_codec.h` 例外（`CMakeLists.txt:792`） |
| `grep -n '#include' src/ui/viewmodel/*.h src/ui/viewmodel/*.cpp` | 无 QWidget / QPainter / QtWidgets 的 `#include`；仅 `console_layout_store.cpp:8` 有 `<QSettings>`，且只在 .cpp |
| `wc -l src/ui/viewmodel/*` | **15 文件 / 2438 行**；各类行数见 §3 表（旧 10/1964、12/2076） |
| `grep -rn 'onSnapshot' src/ tests/` | 仅 `src/ui/panel.h:142` 定义，零调用 |
| `grep -rn 'MhwReader::' src --include=*.cpp` | **19 处 / 5 个文件**（4 个在 `src/world/`）；成员函数定义 **16** 处（旧 63/7/19、15） |
| `sed -n '132p;215p' src/world/world_reader.cpp` | 构造函数 `:132`、`GameSnapshot MhwReader::poll()` `:215`（旧记 `mhw_reader.cpp`，已删） |
| `sed -n '12p;173p' src/world/world_reader.h` | `class MhwReader {` / `};`——类声明跨 `:12-173` |
| `grep -n 'static qint64 cachedPid' src/core/process_memory.cpp` | 三个 static 在 `:173` `:174` `:175`，exeName 失效分支 `:180-184`。S3 之前它们在 `mhw_reader.cpp` 的 287-289 行；那份文件已随 `8a12d5c` 整份删除 |
| `wc -l src/ui/control_panel.cpp` | 2857（旧 2916 / 2910） |
| `grep -n 'QMessageBox' src/ui/control_panel.cpp` | 17 行命中，含 4 处 `QMessageBox::question()` 模态确认（`:2257` `:2285` `:2333` `:2366`） |
| `grep -n 'ConsoleLayoutStore ' src/ui/control_panel.cpp` | `:451 :659 :664 :847 :879 :906 :1310`（7 个，数量不变、行号位移） |
| `wc -l src/monster/part_schemas.cpp` | 11（1047 行的表已搬到 `src/monster/data/part_schemas_data.cpp`，964 行） |
| `wc -l src/rise/part_schemas.cpp` | 96（Rise 3 张表） |
| `wc -l src/rise/data/mhr_part_names_data.cpp` | 682；`mhr_part_names.cpp` 剩 133；`kRisePartNames` 声明在 `mhr_part_names.h:39`（598 项） |
| `wc -l src/rise/data/mhr_monster_names_data.cpp` | 158；`kRiseMonsterNames` 声明在 `mhr_monster_names.h:59`（79 项） |
| `ls src/ui/viewmodel/ \| wc -l` + `wc -l src/ui/viewmodel/*` | 15 文件 / 2438 行；新增 `console_text_helpers.{h,cpp}`（54+58）、`panel_mask_codec.h`（250） |
| `grep -rln 'QWidget' src/ui/viewmodel/*.h` | 4 个头：`console_text_helpers.h`、`monster_view_model.h`、`panel_mask_codec.h`、`player_view_model.h`（仅注释提及） |
| `find src/resources/i18n -type f` | 8 个文件：2 locale × 4 域 |
| `python3` 逐层拍平统计各域叶子 key | 45 / **154** / 144 / 56（zh-CN），en-US 同数（旧 console 138） |
| `wc -l tests/{damage_view_model,rise_reframework_status,console_layout_store,player_view_model}_tests.cpp` | 559 / 363 / 311 / **355**；另 `tests/panel_mask_codec_tests.cpp` 579 |
| `grep -rln 'MonsterPartListBuilder' tests/` | 只命中 `tests/snap_gauge_fill.cpp`（间接引用，无直接单测） |
| `grep -n 'pollOption' src/main.cpp` | `:150` 声明、`:154` 默认轮询间隔 250 ms |
| `grep -n 'MhrReader::poll\\|MhwReader::poll' src/` | `src/rise/mhr_reader.cpp:1386`、`src/world/world_reader.cpp:215`——两个对等取数入口 |
| `wc -l src/ui/*.cpp` | 最大的四个：`control_panel.cpp` 2857、`panel_player.cpp` 1562、`panel_monster.cpp` 1519（`panel_damage.cpp` 与 `hud_canvas.cpp` 同居第五：586 / 587） |
| `grep -c 'src/player\\|src/quest' CMakeLists.txt` | **0**——这两个目录没有任何 target 取源文件（§1.2） |
