# Monster Overlay 架构（HEAD `dbbac03`）

> 本文记录分支 `refactor/game-keyed-arch` HEAD `dbbac03` 的代码分层与数据流，目标是
> 让下一个改这个项目的人一眼看到「我想加一个新面板 / 加一款游戏 / 改 DPS 算法 /
> 加一种语言，该动哪些文件」。
>
> 每个论断都带 `file:line` 或可复现的 grep / wc 证据；行数是 `wc -l` 实测值，
> 不是估算。**凡不能实测的，本文写「未测」或不写**——不写「已达成」「完全解耦」
> 这类没有证据的断言。
>
> 相关文档：`docs/I18N.md` 是本地化的唯一规范（权威，本文不重复、不与之冲突），
> `docs/USAGE.md` 是用户操作指南。重构过程的设计理由与逐阶段验证记录在
> `~/experiment/workspaces/monster-overlay-arch-audit/`（仓库外，不在 git 里）。

---

## 0. 如何读这份文档

**先读哪节**，取决于你要做什么：

| 你想做的事 | 先读 |
|---|---|
| 加第三款游戏（Wilds / 下一作） | §7，它是本文最重要的一节 |
| 加 / 改一个 overlay 面板 | §4 数据流 + §10 常见改动路线 |
| 判断一段代码该放哪个库 | §2 库边界 + §3 契约头 |
| 读懂 reader 到画面的完整链路 | §4 |
| 改门禁 / 加新 TU / 加测试 target | §9 |

**这份文档描述什么状态。** HEAD `dbbac03`，`refactor/game-keyed-arch` 分支，`ctest` **51/51 全绿**（7 个无游戏进程的 probe 测试按设计 Skipped）。本文所有行号、计数、`wc -l` 都在这个 HEAD 上实测。

**什么时候必须回来更新它。** 下列任一改动发生后，本文的对应章节立即过期：
`CMakeLists.txt` 增删库或改库的源列表，`src/core/` 下任何文件的增删或行号位移，
新增 per-game seam，新增门禁脚本，`docs/` 引用的 gate 名字变更，i18n 域数量变化。
上一次本文过期是 20 个 commit：记录停在 `90e41ce`，而记的三库结构、`part_schemas_data.cpp` 归属、
三个文件路径全部与实物不符。**改完上面任何一类，回来改文档，不要等。**

---

## 1. 目录树（HEAD `dbbac03` 实测）

```
src/                                    总计 134 个文件 / 125 个 .h·.cpp / 30707 行
├── main.cpp                   680       # overlay 装配 + 主循环
├── main_control.cpp           221       # monster-control 角色入口
├── reader_factory.{h,cpp}     65+54     # 装配点：契约 × 实现的唯一交汇 TU
├── reader_descriptors.cpp     49        # per-game 描述符，不含任何 reader
├── core/                       21 文件 / 2287 行    → monster-core
│   ├── game_snapshot.h            40    # 契约：一次 poll 的全部输出
│   ├── game_reader.h              56    # 契约：IGameReader（2 个虚函数）
│   ├── zone_id.h                  79    # 契约：ZoneRef{GameId, int}
│   ├── game_tables.{h,cpp}       144+128 # 契约 + 注册表实现
│   ├── game_capabilities.{h,cpp}   84+99 # 契约 + 能力表实现
│   ├── game_profile.{h,cpp}       37+46  # wire 名 "world"/"rise" 唯一定义
│   ├── game_detector.{h,cpp}      21+49  # /proc 扫描 + exe 名行表
│   ├── process_memory.{h,cpp}    142+268 # attach/read/指针链/PID 缓存
│   ├── map_paths.{h,cpp}          77+239 # 数据文件查找（全参数化）
│   ├── steam_game_locator.{h,cpp} 30+228 # VDF 解析 + 通用 install 查找
│   ├── string_table.{h,cpp}       88+95  # i18n 运行时表
│   ├── locale_conf.h             214    # ← 不在任何 add_library 里
│   └── locale_sync.h              74    # ← 同上
├── rise/                      20 文件 / 5346 行     → game-rise
│   ├── mhr_reader.cpp           1355    # 本目录最大
│   ├── mhr_reader_sharpness / _quest
│   ├── mhr_abnormalities.{h,cpp}
│   ├── mhr_part_names.{h,cpp} / mhr_monster_names.{h,cpp}
│   ├── rise_damage_reader.{h,cpp} / rise_damage_roster.{h,cpp} / _types.h
│   ├── part_schemas.cpp          96    # registerRiseTables()
│   ├── rise_zones.h                      # Rise 区域词条（18 个具名区）
│   ├── rise_install_locator.{h,cpp}      # appId 1446780 + exe 名，Rise 私有
│   ├── mhr_types.h
│   ├── data/                      840 行 # mhr_{part,monster}_names_data.cpp
│   └── reframework/            2468 行    # manager / config / fetcher
├── world/                       9 文件 / 2702 行     → game-world
│   ├── world_reader.cpp          305 + .h          # MhwReader façade
│   ├── monster_reader.cpp / player_reader.cpp / quest_reader.cpp
│   ├── world_types.{h,cpp}                      # WorldZone 词条
│   ├── monster_names.cpp                        # World 显示名表
│   └── world_severable_scan.h
├── monster/                      4 文件 / 1379 行
│   ├── monster_types.h          326    # GameId / Snapshot / 表声明（跨库契约纸）
│   ├── target_selector.{h,cpp}   15+23  → game-world
│   └── data/part_schemas_data.cpp 1015    → game-world（World 全部数据表）
├── player/player_types.h         267    # 跨游戏契约纸，无 .cpp，不进任何库
├── quest/quest_types.h            76    # 同上
├── ui/                         41 文件 / 11839 行    → mhw-ui
│   ├── control_panel.cpp        2328
│   ├── panel_player.cpp         1445 / panel_monster.cpp 1499 / panel_damage.cpp 585
│   ├── panel.cpp / hud_canvas.cpp 602 / panel_pet_damage.cpp 448
│   ├── panel_player_sharpness.cpp / rise_reframework_bridge.cpp 293
│   ├── rise_reframework_card.cpp / screen_query.cpp / icon.cpp
│   ├── panel_sections.h         242    # 每面板的 section 位掩码
│   ├── panel_player_metrics.h           # 纯 inline 换算（stamina 缩放在这）
│   └── viewmodel/              15 文件 / 2434 行
└── resources/
    ├── resources.qrc                     # 只列 i18n，8 个文件
    └── i18n/{zh-CN,en-US}/{console,data,overlay,reader}.json
```

`src/monster/`、`src/player/`、`src/quest/` 三个目录今天都**不是**「有自己模块身份」的目录：
`.cpp` 要么归了别的库（`src/monster/target_selector.cpp` 与
`src/monster/data/part_schemas_data.cpp` 都在 `game-world`），要么整个不存在
（`src/player/`、`src/quest/` 只剩契约纸头）。**目录名不代表库归属，库归属只看
`CMakeLists.txt` 的 `add_library` 源列表。**

---

## 2. 库边界与依赖方向

`CMakeLists.txt` 有 4 个 `add_library`（`:41` monster-core、`:89` game-rise、
`:143` game-world、`:199` mhw-ui）。依赖方向是单向的：

```
        main.cpp（exe）
           │
     ┌─────┴──────────────────────────┐
     │                                │
reader_factory.cpp                mhw-ui          ← 两者互相不知道对方细节
 reader_descriptors.cpp        （全部 GUI）
     │                                │
     ├── game-rise ─┐          ┌──────┴──────┐
     └── game-world ─┤          │             │
                    ├── monster-core ◄───────┘
                    │   (Qt6::Core only)
```

| 库 | 源列表 | 源文件 | `wc -l` 合计 | 归档大小 |
|---|---|---|---|---|
| `monster-core` | `CMakeLists.txt:41` | 17 项 / 8 个 `.cpp` | 1880 | 5798.6 KiB |
| `game-rise` | `:89` | 28 项 / 15 个 `.cpp` | 8654 | 13607.4 KiB |
| `game-world` | `:143` | 11 项 / 8 个 `.cpp` | 3742 | 8204.5 KiB |
| `mhw-ui` | `:199` | 55 项 / 26 个 `.cpp` | 14030 | 29669.2 KiB |

（归档大小来自 `bash scripts/assert_lib_purity.sh build` 的实测输出。）

`target_link_libraries`：`monster-core PUBLIC Qt6::Core Threads::Threads`（`:75`）；
`game-rise PUBLIC Qt6::Core monster-core`（`:131`）；
`game-world PUBLIC Qt6::Core monster-core`（`:176`）；
`mhw-ui PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg LayerShellQt::Interface monster-core game-rise game-world`（`:257-265`）。

### 2.1 `monster-core`：游戏无关内核（15 个 TU 口径见下）

契约头自身不算 TU。**编译单元实测 8 个 `.cpp`，加上 7 个契约头就是常说的 15 个 TU**：

```
string_table game_detector game_profile steam_game_locator map_paths
process_memory game_tables game_capabilities            ← 8 个 .cpp
game_snapshot game_reader zone_id（+ monster_types.h）   ← 7 个头（5 契约 + 2 共享）
```

**什么可以放进去**：`src/core/**`、`src/monster/monster_types.h`、`src/player/`、
`src/quest/`——即契约与共享词汇。

**什么不许放进去**：任何具体游戏的 reader、数据表、类型、偏移常量。
违禁的判据是符号面，不是源码文本：`nm -C --defined-only libmonster-core.a`
导出的 94 个符号里，`mhw::` 顶层函数 44 个，全部是参数化的通用助手。

**「零游戏硬编码」的准确边界（如实记录）**：

- **无**游戏文件名、Steam appId、exe 名的**硬编码调用**。`findRiseInstallDir()`
  连同 appId `1446780` 与 `MonsterHunterRise.exe` 已移到 `src/rise/rise_install_locator.{h,cpp}`；
  `riseMapCandidates()` 已改成参数化的 `versionedMapCandidates(explicitPath, fileStem, ...)`
  （`src/core/map_paths.h:67`，版本正则与 glob 在 `.cpp:151` 起）。后者见 commit `67bcf4b`。
- **有**：`src/core/game_detector.cpp:20-21` 的 `defaultGameExecutables()` 两行
  字面量——但它是**数据行表**，加游戏是加一行，且 `detectGame(executables)` 是可覆盖形式（`:29`）。
- **有**：`src/core/process_memory.h:64` 与 `:119` 两个**默认实参**写死
  `"monsterhunterworld.exe"`。头部注释明说这是为了让 World 侧省略实参的调用点保持原拼写。
  这是现状的**已知不匀**：新 reader 若照 World 的习惯省略参数，会去 `/proc` 找 World 的 exe。
  `findGamePid` 的默认值同理（`:119`）。
- **有**：`worldMapCandidates()` 这个名字在 core（`src/core/map_paths.cpp:132`），
  是 lib-purity-gate 唯一一处**显式豁免**的符号
  （`scripts/assert_lib_purity.sh` 的 `GENERIC_GAME_NAMED='riseMapCandidates|worldMapCandidates'`）。
  它是枚举豁免，不是模式漏洞。该列表原有三项，另外两个名字今天只剩注释提及、已无定义
  （`findRiseInstallDir` 已迁到 `game-rise`，`riseMapCandidates` 已被 `versionedMapCandidates`
  取代），已相应收缩；**加新豁免项时请顺手核对它是否真的没有定义**，否则豁免表会靠惯性通过。

### 2.2 `game-rise`：Rise 全量

Reader（含 `mhr_reader_sharpness` / `mhr_reader_quest`）、异常状态、伤害链
（`rise_damage_reader` / `_roster` / `_types`）、名称表（`mhr_{part,monster}_names` + `data/`）、
REFramework 安装与配置（`reframework/` 三个 TU）、`rise_zones.h` 区域词条、
`rise_install_locator` Steam 定位、`part_schemas.cpp` 里的 `registerRiseTables()`。

**不许放**：任何 `world/` 头文件。`nm` 断言：`libgame-rise.a` 对
`mhw::(Mhw|World|world|zoneName|isHuntingZone)` 的 undefined 引用为 0。

### 2.3 `game-world`：World reader + 数据表 + 区域词条

`MhwReader`（`world_reader.{h,cpp}`）+ 四个域 reader + `world_types.{h,cpp}` 词条 +
`monster_names.cpp` 显示名 + `target_selector.cpp`（两 reader 共用的选怪规则，
CMake 注释说明放这里是「不是 core 契约」，迁进 core 同样说得通）+
`part_schemas_data.cpp`（**注意路径在 `src/monster/data/`，但归 game-world**，
里面每一张表都是 World 表）。

**不许放**：任何 `rise/` 头文件。`nm` 断言：`libgame-world.a` 对
`mhw::(Mhr|Rise|rise|ReFramework)` 的 undefined 引用为 0。

### 2.4 `mhw-ui`：整个 GUI 层，只编一次

所有 `src/ui/**`。**依赖方向单向向上**：UI include core / world / rise 的头
（实测 `src/ui/` 下 include `rise/` 或 `world/` 的 12 处，多为 `mhr_reader.h`
只为取 `kRiseWirebugSlotCap` 这类常量），但没有任何 `core/`、`rise/`、`world/` 下的文件
include `ui/`（实测 0 命中）。

**不许放**：reader 的具体实现。`nm` 断言：`libmhw-ui.a` 无
`MhwReader|MhrReader|AddressMap|readMonsters|readParty|followPointerChain` 符号。

### 2.5 qrc **不下沉**

`src/resources/resources.qrc` 与 `assets/icons.qrc` 不在任何 `add_library` 里，
只出现在 exe target 的源列表（首次例 `:318-319`）。加新 exe target 时**必须自己列这两个**。

图标探测机制见 §8.2。

---

## 3. 五个契约头

这五个头是「游戏无关内核」这个说法的实体。任何一个 include 了具体游戏头文件，
core 就不再是 core——判据由 lib-purity-gate 用 `nm` 守（§9）。

### 3.1 `core/game_snapshot.h`（40 行）——一次 poll 的全部输出

`struct GameSnapshot` 14 个字段：`game`（`GameId`）、`attached`、`pid`、`imageBase`、
`status`、`zone`（`ZoneRef`）、`monsters`、`player`、`party`、`quest`、
`playerCount`、`isMultiplayer`。

它是 **UI 与 reader 之间唯一的来回类型**。UI 拿到它就不再需要知道 reader 是谁；
reader 产出它就不再需要知道 UI 怎么画。改字段只动这一个文件——代价是每个面板
都要重新决定「这个字段我用不用」。

### 3.2 `core/game_reader.h`（56 行）——`IGameReader`

两个纯虚函数，就这两个：

```cpp
[[nodiscard]] virtual GameSnapshot poll() = 0;
[[nodiscard]] virtual const QString &mapPath() const = 0;
```

**为什么存在**：`main.cpp` 原来用 `std::function<GameSnapshot()>` 包住两个不相关的
reader 类型。lambda 抹平了类型差异，但装配处仍然写死 `isRise` —— 造哪个对象、搜哪张
map、接哪个伤害 feed、装哪个 lambda，四处都要按游戏分叉，漏一处**不报错**，只是新游戏的
面板空着。IGameReader 把这两个类本来就满足的契约说出来（`MhwReader` / `MhrReader`
本来就都有 `poll()` 和 `mapPath()`），其余全部是 per-game 细节（`findBestMap`、
`findRisePid`、`readZone`），归造 reader 的人，不归轮询循环。

`poll()` 的契约：**不抛异常**，失败通过返回快照的 `status` / `attached` 报告
（`src/main.cpp:476-490` 仍有异常屏障兜底，C1 审计产物）。

### 3.3 `core/zone_id.h`（79 行）——`ZoneRef{GameId game; int id}`

**id 是 `int` 不是 enum，这是实测决定的**，理由在头注释 `:23-38`：
- World：`readZone()`（`src/world/world_reader.cpp:296-302`）把 int32 原样透传，
  只做 `0..1000` 范围过滤，未映射值就是裸整数。
- Rise：`computeZoneId()`（`src/rise/mhr_reader.cpp:42-54`）会发 `-2`（LoadingScreen）、
  `199`（CharSelection），加上 `villageId + 700`、`huntingId + 600`。

枚举化等于造一张永远追不上游戏补丁的表，而且新游戏一上来就背这个债。
所以「这个 id 有没有名字」留在游戏侧（`hasZoneName` / `hasRiseZoneName`），
core 只回答「是不是 -1」（`isUnknownZone`）。

默认构造是 `{GameId::World, kZoneUnknownId}`——为了让从未填过区域的快照
还能参与比较，而不是读成一个真值。

### 3.4 `core/game_tables.h`（144 行）——注册表

三个注册函数（`registerCrownThresholds` / `registerCaptureRules` / `registerAilmentStyles`）
+ 三个查询（`crownThresholdsFor` / `captureThresholdFor` / `ailmentStyle`），
全部按 `(GameId, id)` 问，**core 从不点名任何一款游戏的表**。

**为什么是注册表不是 switch**：原来 `monster_types.h` 用
`game == Rise ? kRiseCrownThresholds : kCrownThresholds` 选表。三个后果：
1. 加游戏要改一个被所有层 include 的头；
2. 两边的查询语义**根本不同**——`captureThresholdFor()` 里 World 查阈值表、
   Rise 查「不可捕获集合」并答 0，所以「加个 arm」从来不是机械操作；
3. 表搬进各自库之后，core 里的三元会迫使内核同时链接两个游戏的数据。

注册表的代价：**必须先注册再查询**。当前装配下这是安全的——注册发生在
数据 TU 的静态初始化器里（见 §3.4.1），而不是 reader 构造函数。
`nullptr` / `nullopt` 表示「这款游戏没有这项静态数据」，**永远不表示「退回另一款游戏」**。

#### 3.4.1 注册时机：静态初始化器，不是 reader 构造函数

`registerWorldTables()` 在 `src/monster/data/part_schemas_data.cpp:1000`，
`registerRiseTables()` 在 `src/rise/part_schemas.cpp:118`，各自由匿名命名空间里的
一个 `struct Register…TablesAtStartup` 静态对象驱动
（`part_schemas_data.cpp:1011`、`part_schemas.cpp:131`）。

**这是被测试逼出来的**：放在 reader 构造函数里注册，会让 edit-mode demo 面板
（不构造 reader）静默丢掉 crown 芯片，`all_monster.png` 730 像素差异。
选择静态初始化的安全性论据：同一个 TU 内初始化器自上而下，取地址的表先构造；
跨 TU 顺序无关紧要，因为注册表只存指针。

### 3.5 `core/game_capabilities.h`（84 行）——能力表

三个查询：`gameDisplayName(GameId)`、`usesNormalisedPartGauges(GameId)`、
`staminaDisplayScale(GameId)`。

**这份表刻意很窄**。UI 里原有 38 处 `game == GameId::X` 比较，实测剩 **27 处**
`==`/`!=` 加 12 个 `case` 标签（§6.1 给分布），其中大部分是**合法的游戏专属视图**
——Rise 独有的 REFramework 卡、World 独有的血量条逻辑。这些**留在原处**：
只有一款游戏有的功能，写成功能检查才是诚实形式，改成能力查询只是把同一个事实
套一层间接。进这张表的只有两类：
1. **per-game 数据**（显示名、stamina 除数）——数据属于表，不属于比较；
2. **今天恰好每款游戏都一样的行为**（`usesNormalisedPartGauges`）——
   `monster_view_model.cpp:99` 原来两个 arm 逐行相同，`default` 走的是第三条路；
   「两款游戏答案相同」不是「一款行为」，是「一份还没抽出来的行为」被 switch 遮住了。

`AilmentStyle` 用打包的 ARGB `quint32` 而不是 `QColor`：这个头在 monster-core 里，
只链 QtCore，`QColor` 在 QtGui。缺省值 `0xFF9E9E9E` 是旧 switch 两个 arm 的 `default:`
中立灰——未注册的游戏拿到中立灰，而不是借另一款游戏的调色板。

### 3.6 为什么工厂不在 core

`reader_factory.h` 的头注释把理由写在开头：造 reader 需要这款游戏的 map 文件名、
exe 名、候选路径规则。工厂若住在 core，就得把这三样全点名一遍，那正是
IGameReader 要消除的耦合——core 从「我能轮询任何游戏」退回「我知道每一款游戏」。

所以工厂住在 core **之上**、main.cpp 旁边，与描述符分离：
- `reader_factory.h`：只暴露**数据**（`GameReaderDescriptor{mapFileName, mapSelection}`、
  `mapFileNameFor` / `mapSelectionFor`），**不含任何 reader 头**——
  测「这款游戏期望哪个 map」不必链接整条 reader 链。
- `reader_descriptors.cpp`（49 行）：描述符定义，`switch (game)` 无 default，
  新游戏漏注册 → `-Wswitch`。
- `reader_factory.cpp`（54 行）：唯一 include `rise/mhr_reader.h` 与
  `world/world_reader.h` 的 TU，`createReader()` 按 `switch (game)` 造对象。
  这是整个程序里**唯一同时知道契约与全部实现**的翻译单元。

---

## 4. 数据流：reader → IGameReader → GameSnapshot → ViewModel → View

```
 [进程内存]                                       tests/baseline/*.png
     │                                                     ▲
     ▼                                                     │ 像素回归
 MhwReader::poll()   MhrReader::poll()          PlayerPanel / MonsterPanel / DamagePanel /
 (world_reader.cpp:215) (mhr_reader.cpp:1311)    PetDamagePanel::update(...)
     │                       │                          ▲
     └───────────┬───────────┘                          │
                 ▼                                      │
        mhw::GameSnapshot  (core/game_snapshot.h)       │
                 │                                      │
                 │  reader = createReader(gameId, explicitMap, dataDirs)  ← main.cpp:392
                 │   ┌──────────────────────────────────────────────────┐
                 └──►│ mhw::GameReaderPtr（mhw::IGameReader）            │
                    └──────────────────────────────────────────────────┘
                 │  reader->poll()                        ← main.cpp:478
                 ▼
        ┌── 纯转换层（无 widget 依赖，可单测）───────────────┐
        │ ui/viewmodel/player_view_model.cpp   resolveIdentity() │
        │ ui/viewmodel/monster_view_model.cpp  partDisplayLayers()│
        │ ui/viewmodel/damage_view_model.cpp                   │
        └────────────────────────────────────────────────────┘
                 │
                 ▼
        PlayerPanel::update(const GameSnapshot&)   ← panel_player.cpp:539
          monsterPanel.update(snap.monsters[selected])  ← main.cpp:527
          damagePanel.update(snap) / updateRiseDamage(...)  ← main.cpp:593/663
```

逐段说明，带实测行号：

1. **Reader 取数**。`MhwReader::poll()`（`src/world/world_reader.cpp:215`）与
   `MhrReader::poll()`（`src/rise/mhr_reader.cpp:1311`）各填一个 `GameSnapshot`。
   两者都把区域写 `ZoneRef`：World 在 `world_reader.cpp:225`
   `snapshot.zone = ZoneRef{GameId::World, static_cast<int>(worldZone)}`，
   Rise 在 `mhr_reader.cpp:1320`。

2. **契约边界**。`main.cpp` 只经 `createReader()`（`:392`）拿到
   `mhw::GameReaderPtr`，轮询只调 `reader->poll()`（`:478`）。整个异常屏障
   在 `:476-490`。装配处**不再**按游戏分叉造对象、选 map、接 feed——
   这是 S5（commit `bf29401`）的收益。

3. **Panel 消费**。`PlayerPanel::update(const GameSnapshot&)`（`panel_player.cpp:539`）
   先做 9 行整结构体镜像（`player_` / `zone_` / `quest_` / `game_` / `attached_` /
   `pid_` / `imageBase_` / `status_` / `hasData_`），注释（`:552-559`）明确解释
   **这些镜像为什么不在 ViewModel 里**：整结构体拷贝不含决策，抽出去只会多一个
   九字段结果类型，还把 `hasData_` 和文件底部直接写它的 demo 路径拆开。
   真正的转换走 `m_vm.resolveIdentity(snap)`（`:566`）。

4. **View / ViewModel 边界规则**（`src/ui/viewmodel/*.h` 的硬规则）：
   ViewModel 拥有**转换**（多输入、一个分支、一个答案），可以用手搓快照单测；
   不拥有 `QWidget` / `QPainter` / canvas。`PlayerViewModel` 的头注释明确说它是
   「move, not a rewrite」——每条 guard 和顺序都是原来的。

5. **主循环的游戏残留**：`main.cpp:305` 的 `const bool isRise =
   (gameId == mhw::GameId::Rise)`。注释（`:386-390`）说明它只为**真正 Rise 独有的
   功能**存在：REFramework 伤害 feed、宠物伤害面板的门控。这是 per-game feature check，
   按 §3.5 的理由**不改成能力查询**。

---

## 5. 六个 per-game seam（加新游戏报 `-Wswitch`）

给 `GameId` 加第三个枚举值时，下列 6 处漏一个 → `-Wswitch` **警告**。
**实测**：HEAD `dbbac03` 上把枚举改成 `{World, Rise, Wilds}` 全量构建，共 **10 条**
`-Wswitch`，分布为 `game_tables.cpp` 3（被 3 个 target 各编一次）、
`reader_descriptors.cpp` 2、`reader_factory.cpp` 2、`game_profile.cpp` 1、
`game_capabilities.cpp` 1、`panel_player.cpp` 1（`zoneLabel()`）。

| 文件 | seam | 形态 |
|---|---|---|
| `src/core/game_tables.cpp:35` | `tableFor()` 注册表槽位 | `switch` 无 default，**switch 后有 `return world` 兜底** |
| `src/core/game_profile.cpp:11` | `gameIdToString()` wire 名 | `switch` 无 default，**switch 后有 `return "world"` 兜底** |
| `src/reader_descriptors.cpp:28` | `descriptorFor()` map 描述符 | `switch` 无 default，**switch 后有 `return kWorld` 兜底** |
| `src/reader_factory.cpp:34` | `createReader()` 造 reader | `switch` **有** `return nullptr` 兜底 |
| `src/core/game_capabilities.cpp:61` | `gameTraits()` 能力表 | `switch` 无 default，**`__builtin_unreachable()`** |
| `src/ui/panel_player.cpp:162` | `zoneLabel()` 区域词条派发 | `switch` **无** default，落到函数末尾的「据点 N / 区域 N」兜底 |

**这是警告，不是编译错误。** 树中无 `-Werror`（四个库与 exe target 都是
`-Wall -Wextra -Wpedantic`，没有 `-Werror`），加第三款游戏默认能构建，
只在构建日志里宣布自己。**把「加游戏会编译失败」当保护是错的**——
`cae0ed2` 之前的 `game_capabilities.cpp` 就同时犯过两个错：`static_assert(std::size(kTraits) == 2)`
比较的是**行数与字面量 2**，加枚举值不改变行数，断言永远通过；而它守护的下标形式
`gameTraits(static_cast<size_t>(game))` 会把新游戏索引到 `kTraits[0]`，即 World 的
显示名与 stamina 除数。`dd6e3b0` 把它改成无 default 的 switch + `__builtin_unreachable()`。

四处的「switch 后兜底」都是**故意的**：GCC 对switch-return-无default-非void函数
会额外警告 control reaches end；这些兜底让那条警告消失，同时它们对合法枚举不可达
（只有越界 cast 才命中，那是调用方的 UB）。

---

## 6. 已知边界（如实记录，不粉饰）

### 6.1 仍按 GameId 分支的地方，以及为什么留着

实测 `src/` 下 **27 处** `==`/`!=` `GameId` 比较 + **12 个 `case` 标签**（注释已排除）：

- `src/ui/control_panel.cpp` 19 处：多为「第 3 个面板页只在 World 有」这类
  每游戏视图布局，控制台本身就是按游戏列开关的。合法。
- `src/ui/rise_reframework_card.cpp` 6 处：REFramework 卡是 Rise 独有功能。
  `owner_->currentGame() == GameId::Rise` 是诚实的功能检查。
- `src/ui/panel_player.cpp` 6 处 + 2 个 `case`：wirebug 槽 vs 衣装行，Rise / World
  各画自己那套。
- `src/ui/icon.cpp` 2 处：怪物头像的**文件名前缀**（`Rise_` / `World_`）——
  header 注释明确说「这是这里剩下的唯一 per-game 事实，是数据不是行为分支」。
- `src/main.cpp` 1 处、`src/rise/rise_damage_roster.cpp` 1 处、
  `src/ui/viewmodel/monster_view_model.cpp` 1 处、`src/ui/control_panel.cpp`
  若干写入。全部是「这款游戏有这个功能」的直接表达。

**结论口径**：这不是「已完全解耦」，是「数据与共性行为进了表，功能判断留在原地」。

### 6.2 `MhwReader` 是一个跨文件 façade

`grep -rn 'MhwReader::' src --include=*.cpp` → **19 处 / 5 个文件**：
`src/world/player_reader.cpp` 6、`src/world/monster_reader.cpp` 3、
`src/world/world_reader.cpp` 7、`src/world/quest_reader.cpp` 1、
`src/reader_factory.cpp` 1。
World 侧的 `src/core/process_memory.h` 四个助手经 `src/world/world_reader.h:20-55`
的 static 委托成员保持 `MhwReader::` 旧拼写，实现在 core。

### 6.3 三个游戏库之间的循环依赖是零，但这是 `nm` 证明的当前状态

`nm` 实测：`libgame-world.a` undefined 中无 `mhw::(Mhr|Rise|rise|ReFramework)`；
`libgame-rise.a` undefined 中无 `mhw::(Mhw|World|world|zoneName|isHuntingZone)`。
`libmonster-core.a` 对两边的 undefined 引用都是 0。这条由 `lib-purity-gate` 每跑
`ctest` 都验一次，不是一次性的历史结论。

### 6.4 进程内 static PID 缓存被两个 reader 实例共享

`src/core/process_memory.cpp` 里的 function-local static PID 缓存（约 `:173-184`）
是一份，5 s 有效窗口，exe 变化即失效。`MhwReader` 与 `MhrReader` 各持实例、
共用这份缓存。**已知隐患，未修。** 拆库没拆它——它是 core 实现细节，不是库边界问题。

### 6.5 `Panel::onSnapshot()` 是死钩子

`grep -rn 'onSnapshot' src/ tests/` → 仅 `src/ui/panel.h:142` 定义，零调用、
零覆写。主循环直接调各面板的 `update()`。留着是历史惯性，删它需要确认外部插件。

### 6.6 其它实测出来的坑

- **`src/core/locale_conf.h` / `locale_sync.h` 不在 monster-core 的源列表里**。
  要用它们的 target 得自己列源文件（`src/ui/control_panel.cpp:25`、
  `src/main.cpp:4-5`、`src/ui/viewmodel/panel_mask_codec.h` 的注释都引用了它们）。
- **反向 include 为零**：`grep -rn 'include "ui/' src/core src/rise src/world
  src/monster src/player src/quest` → 0 命中。这条每加一次反向依赖就破一次，
  `lib-purity-gate` 只查符号不查 include。
- **census 的 11 个 ZERO TU** 靠 `scripts/zero_reference_gate.py` 的 `EXEMPT` 表逐条
  说明理由；`tests/tu_census.json` 快照 61 个 TU。ZERO 集：`main.cpp`、`main_control.cpp`、
  两个 `src/rise/data/*_data.cpp`、`control_panel_styles.cpp`、`icon.cpp`、
  `rise_reframework_card.cpp`、`section_count_bar.cpp`、`toggle_chip.cpp`、
  `viewmodel/overlay_process_controller.cpp`、`src/world/quest_reader.cpp`。

---

## 7. 如何加第三款游戏（操作手册）

这是本节存在的理由。**目标：`src/core/` 下一个文件都不改。**
（这一条在 `dd6e3b0` 的审计中是实测成立的；本文照抄该结论，不重新证明。）

总成本分四类：**必改**（不加就错）、**新写**（游戏的代码）、**按需**（有对应功能才做）、
**验证**。

### 7.1 必改：5 处，全在 core 之外

| # | 文件 | 改什么 |
|---|---|---|
| 1 | `src/monster/monster_types.h:18` | `enum class GameId { World, Rise }` → 加枚举值 |
| 2 | `src/core/game_profile.cpp:11` | `gameIdToString()` 加 `case`；`gameIdFromString()`（`:27`）加一条 if |
| 3 | `src/reader_descriptors.cpp:21` | 加一个 `constexpr GameReaderDescriptor`（map 文件名 + 选择策略）+ `descriptorFor()` 加 `case` |
| 4 | `src/reader_factory.cpp:34` | `createReader()` 加 `case`：`std::make_unique<WildReader>(...)`，必要时加候选路径构造 |
| 5 | `src/core/game_capabilities.cpp:26` | `kTraits[]` 加一行（显示名 key + ASCII + normalised 标志 + stamina 除数），并把 `:50` 的 `static_assert(… == 2)` 改成 3 |

注意 #3 的 `switch` 后**有兜底返回**，所以漏加时你只会看到 `-Wswitch` 警告，
游戏拿 World 的 map 文件名——不是编译错。#4 漏加则 `createReader` 返回
`nullptr`，`main.cpp:392` 拿到空指针，下一行 `reader->poll()` 段错误。**所以顺序是：
先加枚举 → 立刻按 6 处 seam 的 `-Wswitch` 警告逐个补。**

### 7.2 新写：`src/wild/`

```
src/wild/
├── wild_reader.{h,cpp}     # class WildReader final : public IGameReader
├── wild_zones.h            # 区域词条（inline header，见 rise_zones.h 的做法）
├── wild_part_names...      # 按需
└── part_schemas.cpp        # registerWildTables() —— 若有静态数据
```

`wild_reader.h` 的形状照 `src/world/world_reader.h`：

```cpp
class WildReader final : public IGameReader {
public:
    explicit WildReader(QString mapPath);
    [[nodiscard]] GameSnapshot poll() override;
    [[nodiscard]] const QString &mapPath() const override;
};
```

`poll()` 里填 `zone` 的部分按 §3.3 的理由写 `ZoneRef{GameId::Wild, rawId}`，
不要试图把 rawId 收进 enum。

### 7.3 按需：有对应机制才做

| 机制 | 位置 | 说明 |
|---|---|---|
| Steam 查找 | `src/wild/wild_install_locator.{h,cpp}` | 照 `src/rise/rise_install_locator`：只有 appId + exe 两个薄封装，调 core 的 `findSteamGameInstallDir(appId, exe, roots)` |
| 静态数据表 | `src/wild/part_schemas.cpp` | `registerCrownThresholds(GameId::Wild, …)` + 静态初始化器（**不要**放 reader 构造函数，见 §3.4.1） |
| 异常状态/伤害等 out-of-band feed | `src/wild/...` | core 不知道，全都是 `src/main.cpp` 里按功能分支接 |
| UI 面板的 per-game 视图 | `src/ui/panel_*.cpp` | 按 §6.1 的原则写功能检查 |
| REFramework 卡 | 不适用 | Rise 独有，`src/ui/rise_reframework_card.cpp` |
| 图标 | §8.2 | 前缀进 `Icon::monsterPath` 的三元 + qrc 里放 `Wild_NN.png` |

### 7.4 验证清单

```bash
# 1. 全量构建，确认能数出预期条数的 -Wswitch 警告
ninja -C build 2>&1 | grep -c 'Wswitch'

# 2. 门禁（三条结构性 + 三条 i18n）
ctest --test-dir build -R "lib-purity-gate|zero-reference-gate|diagnostic-targets-build"
ctest --test-dir build -R "i18n-gate"

# 3. core 纯度：这条必须 0，否则你把游戏符号漏进了内核
nm -C --defined-only build/libmonster-core.a | grep -E '^[0-9a-f]+ [TDBRCW] ' \
  | grep -Ei 'rise|mhr|world|monsterhunter|huntingzone|qurio' | wc -l

# 4. 新 TU 必须登记 census，否则 zero-reference-gate 直接失败
python3 scripts/zero_reference_gate.py --repo . --snapshot tests/tu_census.json
```

### 7.5 手册里**不**保证什么

- 不保证「编译失败会拦住你」。§5 已实测：只有警告。
- 不保证 `game_capabilities.cpp` 的 `static_assert` 能拦住遗漏——它比较行数与字面量，
  §5 记录了这是怎么失效的。
- 不保证 UI 侧自动正确：control_panel 的每游戏列、图标前缀、词条派发（`panel_player.cpp:162`）
  都是**手写分支**，新游戏要到那里逐处看过。
- 不保证双游戏同跑时的行为：`detectGame()` 的优先级是 `defaultGameExecutables()` 的
  **列表顺序**（World 优先），新游戏追加在末尾即最低优先。

---

## 8. i18n 与资源

### 8.1 i18n

**权威文档是 `docs/I18N.md`，本文只记架构相关部分。**

- 运行时：`StringTable`（`src/core/string_table.{h,cpp}`）从 qrc 的
  `:/i18n/<locale>/<domain>.json` 加载，`tr()` 单次 QHash 查找，缺 key **返回 key 本身**。
- 域划分 4 个 × 2 locale = 8 个文件，按**谁重绘**划分（不按哪个进程读）：
  `overlay`（`ui.*`）/ `console`（`console.*`）/ `reader` / `data`。
- **实测叶子 key 数**（逐层拍平，`_meta` 跳过）：console **155**、data **57**、
  overlay **46**、reader **145**，每 locale 合计 **403**，zh-CN 与 en-US 数量全等、
  无单边 key。
- 语言运行时切换：`load()` 可再次调用，表原地替换；缓存过字符串的 widget 必须重查——
  控制台走 `ControlPanel::retranslateUi()`，overlay 走 conf 轮询
  （`src/core/locale_sync.h`，`main.cpp` 的 locale poll 每 1 s 一次 mtime stat）。
- 域文件里**唯一的兜底哨兵**：`gameDisplayName()` 拿 `tr()` 结果与 key 比较来判断
  key 是否存在（`game_capabilities.cpp:83-86`），这是 I18N.md §5 认可的例外。

### 8.2 资源：qrc 图标探测机制

`src/resources/resources.qrc` **只列 i18n** 8 个文件；`assets/icons.qrc` 单列
（353 个 SVG + 怪物头像 PNG）。两个 qrc 都**不进任何库**，只进 exe target。

怪物头像的存在性由**问资源系统**回答，而不是维护两份手抄 id 列表
（`src/ui/icon.cpp:209-246`）。原来这个文件里有 `kRiseKnown`（78 个 id）和
`kKnown`（71 个 id）两张手工转录表，与 `assets/icons.qrc` 无门禁同步；
往 qrc 加头像而忘了改列表，新怪物静默渲染成 `Unknown.png`。
现在是 `QFile::exists(":/icons/Monsters/Rise_00.png")` 直接探测，与真正参与渲染的
是同一个 qrc，两者不可能不一致。干净顺带解决了 91 号金狮子的别名问题
（91 miss → 92 hit）。

代价：`game` 到**文件名前缀**的三元留在 `icon.cpp:228`
（`Rise_` / `World_`），加游戏要在这里加第三个前缀，并且把 `Wild_NN.png` 放进 qrc。
这不是 switch，所以 **没有 `-Wswitch` 保护**——这是本篇手册在 §7.3 里单独列它的原因。

---

## 9. 三道结构门禁 + 三条 i18n 门禁

`ctest` 51 个用例中，最后 6 个是静态门禁（`CMakeLists.txt:1246-1332`）。
三条结构门禁都**需要 `build/`**，全新 checkout 无构建目录时它们报 SKIP 而不是失败。

### 9.1 `lib-purity-gate`（`Test #51`）

跑 `scripts/assert_lib_purity.sh build`，用 `nm` 读四个 `.a`：

| 断言 | 查什么 |
|---|---|
| `libmonster-core.a` 无游戏命名符号 | 内核导出符号里出现 `rise\|mhr\|world\|monsterhunter\|huntingzone\|qurio` 的大小写不敏感词干 |
| `libgame-world.a` 无 Rise 符号 | 同上于 world 库 |
| `libgame-rise.a` 无 World 符号 | `mhw::Mhw\|WorldZone\|mhw::zoneName\|mhw::isHuntingZone\|kPartSchemas\|kCrownThresholds` |
| `libmhw-ui.a` 无 reader 符号 | `MhwReader\|MhrReader\|AddressMap\|readMonsters\|readParty\|followPointerChain` |
| 两个反向依赖各 0 | `nm --undefined-only` |

**不能查什么**：它只读**符号名**。它抓不到「core 里写死了一个游戏常量但函数名通用」这类
耦合——例如 §2.1 记录的 `process_memory.h` 两个默认实参与
`game_detector.cpp` 的 exe 名表。也抓不到 `#include` 方向（`src/ui/` include
`src/rise/` 12 处，这是**设计允许**的，但反方向 core include game 头只能靠人看）。
**它也不是万能的模式表**：`GENERIC_GAME_NAMED` 的豁免项必须**逐个核对确无定义**，
曾经的过期项已随 `findRiseInstallDir` 迁出 core 一并收缩（§2.1）。

### 9.2 `diagnostic-targets-build`（`Test #46`）

跑 `scripts/assert_diagnostic_targets.sh build`，**编译** 5 个
`EXCLUDE_FROM_ALL` target（`snap-world-mp` `snap-gauge-fill` `snap-empty-state`
`snap-player-offline` `rise-probe`），一个都不运行。

**为什么存在**：这些 target 被排除在默认构建外，因为只在有游戏运行时才有意义。
这个判断对「运行」成立，对「编译」不成立。`rise-probe` 曾被 ZoneRef 改动打断，
整整一个重构周期里默认 `ninja` 从没编过它，而 ctest 一路绿灯。
`CMakeLists.txt:1240` 的 `MHW_DIAGNOSTIC_TARGETS` 必须与真实的
`EXCLUDE_FROM_ALL` 列表一致。

**不能查什么**：只编译，不执行——它验不出这五个工具的运行时行为。

### 9.3 `zero-reference-gate`（`Test #50`）

跑 `scripts/zero_reference_gate.py --repo . --snapshot tests/tu_census.json`。
**快照门禁**：比较磁盘上的 `src/*.cpp` 集合与 `tests/tu_census.json` 登记的集合，
不需要构建。新 TU 未登记 → 直接失败。实测快照 **61 个 TU，11 个 ZERO**，
每个 ZERO 都在脚本的 `EXEMPT` 表里有理由和解除条件。

**这是「新 TU 必须声明自己」的不变量，不是覆盖率指标。** 它刻意不问「覆盖率够不够」：
`--report` 才给实测数字，但那需要 `build/`，且从不当门禁用。

**不能查什么**（脚本头部原文的四条）：
1. 测**符号引用**，不是执行。被 `QCOMPARE` 摸过一次的 TU 和有 45 条断言的 TU 同分。
2. 无外部可见符号的 TU 只能记 UNKNOWN，永远无法被它打分。
3. 只数一层间接。
4. 符号匹配是 mangled name 精确匹配，`#ifdef` 编出的差异会造成误判 INDIRECT。

### 9.4 三条 i18n 门禁（`Test #47/#48/#49`）

`scripts/i18n_gate.py` 依次跑 `check` / `parity` / `unused`，`-q`。
`CMakeLists.txt:1272` 的 `MHW_I18N_GATES` 列表定义这三条。

- `check`（`i18n_check_tr_keys.py`）：代码要的每个 key 在每个 locale 都存在。硬门禁。
- `parity`（`i18n_key_parity.py`）：zh / en key 集合逐域相等。硬门禁。
- `unused`（`i18n_unused_keys.py`）：报告没人引用的 key。**默认非严格**——
  实测 23 个死 key（`data` 域 21 个退役 mantle 名 + `overlay` 域 2 个），
  门禁仍 PASS。理由：死 key 玩家看不见，拿它当门禁只会把一次 cosmetic 清理变成
  红色构建，然后训练所有人忽略门禁。`--strict` 在发版前用。

---

## 10. 常见改动路线

| 想做的事 | 该动哪里 |
|---|---|
| **加一款游戏** | §7 全节。核心是 5 处必改 + 一个 `src/wild/` |
| 加一个新 overlay 面板 | 新 `src/ui/panel_x.{h,cpp}`：继承 `Panel`、实现 `paintPanel()` 与 `update()` 重载；i18n key 进 `overlay.json`；源文件列进 `CMakeLists.txt` 的 mhw-ui 块，并在 `src/main.cpp` 装配 |
| 改 DPS / 折线图算法 | `src/ui/viewmodel/damage_view_model.{h,cpp}`；测试 `tests/damage_view_model_tests.cpp` |
| 改部位卡聚合 / AutoHide | `src/ui/viewmodel/monster_view_model.{h,cpp}`（守住 ONE call per paint） |
| 改 reader 的地址 / 字段 | 该游戏目录下的域 reader `.cpp`（Rise 在 `src/rise/`，World 在 `src/world/`）；加 `GameSnapshot` 字段改 `src/core/game_snapshot.h` |
| 改 PID 查找 / 指针链 / 地址表加载 | `src/core/process_memory.{h,cpp}`。World 侧写 `MhwReader::` 前缀（四个 static 委托在 `src/world/world_reader.h:20-55`）、Rise 侧写 `mhw::` 前缀，但只有这一份实现 |
| 加区域词条 | `src/rise/rise_zones.h` 或 `src/world/world_types.h`（inline header，不加 .cpp 是为了避开 census 登记）；派发在 `src/ui/panel_player.cpp:162` |
| 加静态数据表（crown / capture / ailment） | 该游戏的 `part_schemas*.cpp` 里加表 + `registerXxxTables()`；**注册用静态初始化器，不是 reader 构造函数** |
| 改某游戏的显示名 / stamina 缩放 / 部位规整标志 | `src/core/game_capabilities.cpp:26` 的 `kTraits[]` 一行 + `static_assert` 的 2 改 3 |
| 改窗口布局持久化 | `src/ui/viewmodel/console_layout_store.{h,cpp}` + `tests/console_layout_store_tests.cpp` |
| 改 REFramework 卡片文案 | `src/ui/viewmodel/rise_reframework_status.{h,cpp}` + `tests/rise_reframework_status_tests.cpp` |
| 改玩家 MR/名/武器/party 的合并规则 | `src/ui/viewmodel/player_view_model.{h,cpp}` + `tests/player_view_model_tests.cpp` |
| 改控制台纯文本（节标签/游戏名/console key 解析） | `src/ui/viewmodel/console_text_helpers.{h,cpp}`（1:1 搬自 control_panel，别在 View 里再抄一份） |
| 改 `.conf` 面板 mask 的文本格式 | `src/ui/viewmodel/panel_mask_codec.h`（header-only）+ `tests/panel_mask_codec_tests.cpp`；文件系统读写与告警留在 `control_panel.cpp` |
| 加 World / Rise 数据表 | `src/monster/data/part_schemas_data.cpp`（`gen_schema.py` 就地改写的那份）/ `src/rise/data/mhr_{part,monster}_names_data.cpp` |
| 在 rise / world / ui 之间共享类型 | `src/player/` 或 `src/quest/` 这类跨游戏契约纸目录（无 .cpp），或 `src/monster/monster_types.h`；**不要**塞进某个游戏目录 |
| 改 i18n key / 域切分 | `docs/I18N.md` 是权威；脚本在 `scripts/i18n_*.py`；加新域要同时改 qrc |
| 加怪物头像 | `assets/icons.qrc` 加 `Wild_NN.png`；`src/ui/icon.cpp:228` 加前缀三元 |
| 新增 exe / 测试 target | **必须**把 `src/resources/resources.qrc` 与 `assets/icons.qrc` 列进自己的源列表（§2.5）；加 `EXCLUDE_FROM_ALL` 同时更新 `MHW_DIAGNOSTIC_TARGETS`（§9.2）；新 TU 登记 census（§9.3） |

---

## 附录：验证记录

以下命令在仓库 `/home/a27exe/Projects/games/monster-overlay`（HEAD `dbbac03`，
分支 `refactor/game-keyed-arch`，工作树干净）实测。构建目录 `build/`
（`RelWithDebInfo`）。

| 命令 | 实测结果 |
|---|---|
| `git rev-parse HEAD` | `dbbac03`（同步本文时） |
| `ctest --test-dir build` | **51/51 全绿**；7 个 probe（`monster-probe*` / `monster-scan`）Skipped（无 MHW 进程，设计行为） |
| `ninja -C build` | `no work to do`；warning 计数 **0** |
| `wc -l CMakeLists.txt` | 1333 |
| `grep -n '^add_library' CMakeLists.txt` | 4 个：`:41` monster-core、`:89` game-rise、`:143` game-world、`:199` mhw-ui |
| `grep -c 'add_executable' CMakeLists.txt` | 53 |
| `grep -c 'add_test(NAME' CMakeLists.txt` | 48（其中 3 条 i18n 由 foreach 展开；`ctest -N` 实测 51 用例） |
| `bash scripts/assert_lib_purity.sh build` | 6 项全 PASS，0 命中；归档 5798.6 / 13607.4 / 8204.5 / 29669.2 KiB |
| `python3 scripts/zero_reference_gate.py --report` | TOTAL **61**，ZERO **11**，INDIRECT **50**，UNKNOWN 0 |
| `ctest -R "diagnostic-targets-build"`（`-V`） | 5/5 target `built` |
| `python3 scripts/i18n_gate.py` | 三域全 PASS |
| `python3 scripts/i18n_unused_keys.py` | 23 unique dead key（report-only，非门禁） |
| `find src -type f \| wc -l` | 134 文件；`.h`/`.cpp` 125 个，`.cpp` 61 个 |
| `nm -C --defined-only build/libmonster-core.a` | 94 个导出符号，`mhw::` 顶层 44 个，无游戏词干命中（豁免项 `worldMapCandidates` 除外） |
| `grep -rn 'include "ui/' src/core src/rise src/world src/monster src/player src/quest` | 0 命中 |
| `grep -rn '#include "rise/\|#include "world/' src/ui/` | 12 行命中（最大块：`rise_reframework_card` / `rise_reframework_bridge`） |
| `grep -rn 'MhwReader::' src --include=*.cpp` | 19 处 / 5 文件（`world_reader` 7、`player_reader` 6、`monster_reader` 3、`quest_reader` 1、`reader_factory` 1） |
| `grep -c 'Monsters/' assets/icons.qrc` | 150（`Rise_` 78、`World_` 71、`Unknown` 1） |
| 加 `GameId::Wilds` 后全量构建（scratch 副本） | 10 条 `-Wswitch`，分布见 §5；`ninja` exit 0、ctest 50/50 仍全绿 |
| `wc -l docs/ARCHITECTURE.md` | 本文；上一版 605 行，记录停在 `90e41ce` |

### 与既有记录的矛盾（如实记录，不粉饰）

写本文时发现以下既有记录与实测不符。它们大部分已被 `cae0ed2` / `dd6e3b0`
的提交信息修正过，本文按实测口径重述；列出来是为了让下一个读者不必重新踩。

1. **库数量**：本文上一版与多份提交信息说 `monster-core / mhw-reader / mhw-ui`
   **三**库。实际 `f028712` 之后是 **四**库（`mhw-reader` 更名 `game-world` 并吸收
   `world_types.{h,cpp}` 与 `src/monster/data/part_schemas_data.cpp`，新增 `game-rise`）。
2. **`part_schemas_data.cpp` 归属**：旧版说它在 monster-core。它在 **game-world**
   （`CMakeLists.txt:175-176`）。整个 `src/monster/` 下没有一个 `.cpp` 属于 monster-core。
3. **三个文件路径已不存在**：`src/core/reframework_fetcher.cpp`、
   `src/core/rise_reframework_manager.cpp` 在旧版出现；它们现在位于
   `src/rise/reframework/`（`game-rise`）。
4. **`game_capabilities` 的保护级别被高估**：`AUDIT-S1-DECOUPLING.md:43` 称
   `static_assert(std::size(kTraits) == 2)` 是「全仓唯一一处加游戏必须编译失败的断言」。
   实测它为假：断言比较行数与**字面量 2**，加枚举值不改变行数，断言永远通过；
   真正守护该文件的是 `-Wswitch` **警告**（树中无 `-Werror`）。`cae0ed2` 之前的
   `game_capabilities.cpp` 还因此把新游戏静默索引到 `kTraits[0]`（World 的显示名与除数）。
   `dd6e3b0` 已修正注释；本文 §5 记录实测。
5. **`EXECUTION-PROGRESS.md` 说「core 零改动成立，已达成」**：这句话的**技术结论**
   （加第三款游戏不改 `src/core/` 源文件）在 `dd6e3b0` 的独立审计中实测成立。但同一份
   文档 §「尚未完成」里还留着「S3 库拆分降级为可剪枝项」「Zone 词条分离未做」——
   这两项后来都完成了（`f028712`、`40f19a4`），那份进度表**落后于 HEAD**。
   本文只采用已被 commit 证实的部分。
6. **FINAL-AUDIT 的结论口径**：`~/experiment/workspaces/monster-overlay-arch-audit/`
   下没有名为 `FINAL-AUDIT.md` 的文件；最终审计的产出散落在 `dd6e3b0` 的提交信息
   （「core proposition held — `src/core/` needed zero changes — but three statements
   did not」）与 `AUDIT-S1-DECOUPLING.md` 里。本文按这个口径写：**解耦成立，
   但仍是有 switch 表形式的硬编码**（§5 的六个 seam、§6.1 的 27+12 处分支），
   不是「完全解耦」。
