# Changelog

本文件记录 monster-overlay 的变更。格式参考 Keep a Changelog。

**留在营地继续探索后，下一局会多出一行不属于该局的玩家。** 四人局打完后若有
多人选择「留在营地继续探索」，退出探索再开下一局时，DPS 面板会显示 5 行，其中
一人根本不在这一局、伤害恒为 0。回到集会区域再开一把则正常。

原因是状态机的一处 fall-through。结算后选择继续探索时，quest 结构已被清零
（`world_reader.cpp` 在非狩猎区先清 quest、后清 party），因此存在一帧
`quest.state ≤ 1` 而 `snap.party` 仍非空。此时 `questEnded_ && atLobby` 分支
会清空 `names_` **并把 `questEnded_` 置回 false**，下一帧掉进记录路径——而那里
的 `inQuest` 为 false，这违反了它自己声明的前置条件（"we're guaranteed
!questEnded_ && inQuest"）。记录路径把营地 roster 播种进了 `names_`，而下一局
唯一能清 identity 的路径 `inQuest && questEnded_` 需要 `questEnded_`，它刚被置
false，于是营地的人活进了新一局。

回到集会区域之所以能自愈，只是因为那条路上 roster 恰好是空的，`hasData_` 为
false 让记录路径提前返回、从没播种。两条路的唯一差别就是状态掉到 ≤1 那一刻
party 空不空。复现率低是因为它需要「quest 已清而 party 指针尚未被下一次读取
撞空」的那一个轮询窗口真实存在。

现在补一个 `atLobby` 早退 —— 修的是被违反的前置条件，不是加兜底。

三个新回归测试：复现本 bug 的那一例，以及两个控制组（集会区域自愈路径、单人
留守不复现），用于钉住「为什么只有特定形状才复现」。

---

## [0.11.3] — 多人伤害面板掉线错位修复

**World 多人局有人中途掉线时，其余玩家的行会错位。** 四人局、第三位掉线时：
第四行（自己）的数据冻结在掉线那一刻，第三行变成了自己的名字、伤害从零重新
计数，而真正掉线的那位玩家整行消失。

原因是 `snap.party` 是紧凑数组 —— `readParty()` 遍历四个内存槽位、空槽跳过、
存活成员压紧，而伤害面板的全部 per-player 状态（名字、武器、伤害、首次命中
tick、基线、槽位）都按**数组下标**索引。第 3 位掉线后，原本第 4 位的玩家被压到
下标 2，接管了掉线者的历史状态并触发基线重置；而下标 3 被判定为"掉线行"冻住，
冻住的恰好是自己那份。

现在改为按**名字**建立 live 下标到行号的置换表：行号不变、数据跟着行号走。
同时修掉一个越界写 —— 队伍从满员缩到 0 人再重新出现时，`firstHitTick_` 会在
`size == 0` 上被写入（debug 构建直接 assert 中止，release 下是静默越界）。

**伤害面板的未知武器不再显示空白框。** 武器 id 解析不出来时（<0 或 >13），该行
现在显示一个灰色 `?`，明确表示"这一行没有武器数据"，而不是一个看不出含义的
深色空框。0–13 的正常武器路径完全未变（`kDirs[]` 14 项与 qrc 的 14 个武器目录
一一对应）。用项目自带字体程序化绘制，没有新增二进制资产。

** Rise 的盟友武器读取逻辑未改动。** 某些任务的"房主 + 3 位玩家 + 1 位盟友"
组合下盟友武器为空，根因是名册在多人局被整体跳过（`mhr_reader.cpp:141-144`
比上游多了「观察到非本地玩家就抑制随从」这一条），放宽它可能引入幽灵盟友行，
风险高于空白图标，故本轮只做 UI 兜底。多数常规任务的盟友武器读取不受影响。

`git log` 口径：本次含 6 个架构重构 commit 与 2 个修复 commit。

---

## [Unreleased] — 四库架构重构

把 `monster-core` 拆成「游戏无关内核 + 每游戏一个库」。`git log --oneline
main..HEAD` 共 **14 个 commit**：13 个是重构本体（`5ab840e`..`dd6e3b0`），
末一个 `dbbac03` 是架构文档重写。每个 commit 都是独立可回滚的改动。

（口径说明：按「阶段」算是 12 个，`dd6e3b0` 是对前 12 个阶段中三条已实测为假
的陈述的修正，不是新阶段；下面提交列表按 git 顺序全列 14 条，不合并。）

**用户可见变化为零。** 行为、快捷键、面板显示、配置格式、`--game` / `--map` /
`--poll` / `--edit` / `--mask-*` / `--no-*` 的语义全部逐字未变；`panels.ini` 与
`monster-overlay.conf` 的字段一个不多一个不少。这是纯内部重构——如果你的面板
明天看起来和今天一模一样，那正是预期结果。想了解新架构请读
`docs/ARCHITECTURE.md`（已同步重写），本文只记录工程账。

### 做了什么

一次「按游戏归属」的库切分。重构前 `monster-core` 是一个装了 56 个 TU 的
单库，其中 26 个是 Rise、2 个 World：内核的 include graph 看起来没有游戏，
但 `libmonster-core.a` 导出的 140 个符号里有 60 个属于两款具体游戏。文件搬动
解决不了这件事——得先让内核停止*认识*具体游戏，然后才谈得上把代码搬出去。
13 个 commit 的顺序正是这个方向：

1. `5ab840e` ZoneRef 契约带 game 标签，砍掉 core→world 这条边
2. `c81bfdf` 只构建本次要轮询的那个 reader
3. `fd9368e` crown / capture 查表换成 GameId 注册表（`core/game_tables.*`）
4. `63b6748` UI 问「支持什么」而不是「是哪个游戏」（`core/game_capabilities.*`）
5. `bf29401` 轮询循环改为面向 `IGameReader` 契约 + `reader_factory`（不在 core）
6. `c18373d` 异常状态色板按游戏分表；REFramework 版本号收敛为单一常量
7. `b629ae5` 怪物头像改为向 qrc 资源系统查询存在性
8. `12a3db3` 耐力缩放从三元表达式改为 traits 表的一列
9. `f028712` 拆库：kernel / game-rise / game-world，附 `lib-purity-gate`
10. `67bcf4b` 内核不再硬编码 Rise（map stem 参数化、Steam 查找移到 rise）
11. `40f19a4` Rise 的 18 个区域词条从 `WorldZone` 搬到 `src/rise/rise_zones.h`
12. `cae0ed2` 补齐两处「新 GameId 会静默漏过」的 seam
13. `dd6e3b0` 修正审计实测为假的三处陈述
   （`dbbac03` 重写架构文档，不计入阶段）

### 可观察变化

- **编译产物**：`libmonster-core.a` 20664 KiB → 5798 KiB（56 TU → 8 个 `.cpp`
  / 15 TU 口径），游戏符号 0；新增 `libgame-rise.a` 13607 KiB、`libgame-world.a`
  8205 KiB（原 `mhw-reader` 改名，仍是 World reader）； `libmhw-ui.a` 29669 KiB。
  归档大小实测来源 `bash scripts/assert_lib_purity.sh build`。
- **测试**：`ctest` 47 → 51。新增 `reader-factory-tests`（钉住两个 map 文件名
  与两条选择规则）、`ailment-style-tests`（钉住 World 1 是中毒而 Rise 1 不是，
  即异常 id 跨游戏不同号）、`diagnostic-targets-build`（编译但按设计不运行那
  5 个需要真游戏的 target——此前 `rise-probe` 坏了一整个重构周期没人发现，
  因为它是 `EXCLUDE_FROM_ALL`，默认 `ninja` 从不编它而 ctest 一路绿灯）、
  `lib-purity-gate`。`mhw-reader-direct-tests` 更名 `game-world-direct-tests`。
- **TU 登记**：`tests/tu_census.json` 56 → 61 个 src TU，`zero-reference-gate`
  的 ZERO 计数回到 11。
- **构建健康度**：**洁净从零构建 0 warning / 0 error**
  （`cmake -G Ninja` + `ninja`，RelWithDebInfo，`-Wall -Wextra -Wpedantic`）；
  `lib-purity-gate` 6/6 PASS，三个 i18n 门禁 PASS。
  注意 commit 消息里的 target 数（「316」「336」）**不可复现也不该引用**——
  按边数计是 339，取决于 target 集合与 configure 时的 target 发现结果。

### 审计修正了什么

13 个 commit 里有两条修正记录，留在这里是因为它们是「声称」而不是改动：

- `gameDisplayName()` 对缺失 key 的保护被写成「编译期失败」。实测是假的：
  traits 表的 `static_assert` 拿行数与字面量 `2` 比，加一个枚举器它照样通过；
  真正兜住的是六个 per-game switch 上的 `-Wswitch` **警告**（10 条），而本仓
  不设 `-Werror`，所以新游戏默认能编过、只在构建日志里自报。加入
  `GameId::Wilds` 实测得此结果。
- `lib-purity-gate` 的第一版只匹配 CamelCase 拼写，把
  `src/rise/mhr_abnormalities.cpp` 注入内核后它报 0：那个 TU 导出
  `riseAilmentNameEn` / `riseFindAbnormality` 等 24 个符号，没有一个被模式
  点名。**不能在自己要抓的东西上失败的门禁比没有门禁更糟**，因为它的 PASS
  会被当成证据。现改为大小写不敏感地匹配游戏名词干，三项合法豁免按名列举，
  重放注入得 7 个符号且 exit 非零。

### 🔗 提交列表（`git log --oneline main..HEAD`，按时间正序）

| | commit | 主题 |
|---|---|---|
| 1 | `5ab840e` | refactor(core): give the zone contract a game tag, cut the core→world edge |
| 2 | `c81bfdf` | perf(main): build only the reader this run will poll |
| 3 | `fd9368e` | refactor(core): replace the GameId ternary table lookup with a registry |
| 4 | `63b6748` | refactor(ui): ask what a game supports instead of branching on which it is |
| 5 | `bf29401` | refactor(main): give the poll loop a reader contract instead of a ternary |
| 6 | `c18373d` | refactor(ui): give each game its own ailment palette, pin the REFramework version |
| 7 | `b629ae5` | refactor(ui): ask the resource system which monster portraits exist |
| 8 | `12a3db3` | refactor(ui): make the stamina scale a per-game value, not a ternary |
| 9 | `f028712` | build: split monster-core into kernel / game-rise / game-world |
| 10 | `67bcf4b` | refactor(core): stop hardcoding Rise inside the kernel |
| 11 | `40f19a4` | refactor(zone): give each game its own zone vocabulary |
| 12 | `cae0ed2` | refactor(core): close the two per-game seams a new GameId could slip through |
| 13 | `dd6e3b0` | fix(core): correct three claims the audit measured as false |
| — | `dbbac03` | docs: rewrite ARCHITECTURE for the four-library architecture |

`dd6e3b0` 是修正而非阶段，所以上表 1–12 即 12 个阶段；`dbbac03` 是本文档
引用的那份架构文档。

## [0.11.2] — 2026-09-28

把「能测的」和「测不到」之间的缝隙补上，再把库边界的方向扶正。
全程 6 个 commit，每个都是独立可回滚的改动。

### 测试盲区

`src/ui/panel_player.cpp` 原本从第一行 `#if defined(MHW_WIREBUG_SLOT_LABEL_TEST)` 到
文件末尾都被 guard 包着，Core-only 测试 target 为了够到两个纯函数，得以**文本
include 1557 行 Qt View 代码**。实测：往 guard 之后的第 97 行注入
`this is not C++ ##`，`monster-reader-tests` **编译通过、链接成功、退出码 0**；
同样错误放第 50 行则报 3 个编译错误。也就是 guard 之后新增的任何代码
（包括此后要抽的 ViewModel）既不编译也不报错，而盲区随文件增长。

两个纯函数搬进 `src/ui/panel_player_metrics.h`（纯 `inline` 头，测试直接
include），guard 全部消失，**没有窗口可掉**。文件 1651 → 1578 行。

### update() 路径纳入回归保护

现有两张 player 基准只调 `setGameForDemo`，**从不调 `update(const GameSnapshot&)`**——
那 72 行的镜像、party 覆盖与跨帧累积逻辑零保护。新增 `player_update_world.png`
与 `player_update_rise.png` 覆盖真实路径，包括只有该路径才有的 party 覆盖规则。

写这两张基准时发现：`Panel::paintEvent()` 在 edit 模式首次绘制就跑
`setupDemoData()`，会把 `update()` 刚镜像的字段全冲掉——照原有用例只加一次
`renderPanel()`，生成的 PNG 与旧基准**逐字节相同**。所以先渲染一次消耗掉
demo seed，再 `update()`，再渲染。

### 状态与绘制

- `update(GameSnapshot)` 的 21 行合并规则进 `mhw::PlayerViewModel`
  （普通类，单一写入路径，不需要信号）。函数体逐字搬迁，`break` 保留。
- 删三处冗余：`debuffMaxTimers_` / `buffMaxTimers_` 写而不读（`drawBar` 换成
  `drawPill` 后分母不再被读，循环留下）、零调用的 `update(PlayerSnapshot&)`、
  `sharpness_` 对 `player_.sharpness` 的冗余别名（改引用后删成员）。
- 三份逐行同构的 pill 网格循环收进一个 `drawPillGrid`；Rise 的溢出胶囊与
  World 的 `"%1s"` 各自留在调用点。
- World 的 pill 行间距此前散落 4 处裸 `4`，与 Rise 的 `kRiseStatusRowGap` 同值。
  命名为 `kPillRowGap` 并保留两个独立 token——共用一个会让调 Rise 间距时
  拖着 World 一起动。

### 库边界

- `monster-core` 曾反向依赖 `mhw-reader`：`src/rise/mhr_reader.cpp` 调用 6 处
  `MhwReader::`，而后者在下游库。倒置之所以没暴露，是 13 个 core-only 测试
  碰不到那个 TU。四个被调用的符号都是 `static` 无状态函数，入参 `ProcessMemory`
  是与「MhwReader（World 侧 reader）」无语义关系的内存封装——两者一并搬到
  `src/core/process_memory.*`，`MhwReader` 留四个 inline 委托，World 侧调用点
  零改动。`findGamePid` 的 5 秒 PID 缓存仍是同一个函数局部 static。
  **链接层验证**：`nm` 显示 `libmonster-core.a` 对 `MhwReader::` 的未定义引用
  为 0——不只是源码里不写那几个字。
- `part_schemas.cpp` / `world_types.cpp` / `string_table.cpp` 原本被逐 target
  重复编译（源列表 93 行、唯一文件 45 个）。收进库后降到 50 行，消 43 次
  重复编译，不改一行 C++。

### i18n 门禁

- check 门禁的正则只认 `mh::tr` / `trMessage` / `table.tr`，**漏掉 `trVm`
  别名、三元表达式里的 key、静态数组里的 key**。注入 typo 三门禁全绿——
  「i18n 已覆盖」的账面数字偏乐观。补齐后覆盖从 297 升到 312 个 key，注入
  typo 实测变红。
- 三门禁接进 ctest（`i18n-gate-{check,parity,unused}`），此前它们纯人工运行。
  注入一个 zh-only key 实测 `i18n-gate-parity` 变红。
- `rise_reframework_bridge.cpp` 15 处诊断串迁入 i18n（`main.cpp` 的 CLI 描述与
  `rise_damage_reader.cpp` 的 stderr 文本经核实是格式不是翻译，保持不动——
  `StringTable::load()` 在 CLI 选项构造之后才执行）。

### 验证

洁净从零构建 127 个 target、0 error / 0 warning；`ctest` 40/40；i18n 三门禁
PASS；7 张像素基准逐张零 diff。`PlayerViewModel` 的 `break` 语义做过变异测试
（删掉 `break;` 后 10 处断言精确失败，还原后全过）。

## [0.11.1] — 2026-09-27

World 与 Rise 平级化的第一步：把「游戏叫什么」从 8 个调用点的私有三元表达式
收敛成一份共享词表。

### 新增

- `src/core/game_profile.{h,cpp}` — `GameId` ↔ wire 字符串的唯一转换点。解析
  是全函数的：未识别的取值显式失败，而不是悄悄回落到某个游戏，这样损坏的
  设置值会走到调用方的 else 分支，不会替玩家选一款游戏。`gameIdToString`
  用 switch 而非三元——新增第三款游戏时编译器会指向那一行，而不是让某个
  分支被静默继承。
- `tests/game_profile_tests.cpp` — 钉住双向映射、大小写与空白容忍、非法值
  拒绝、失败解析不污染调用方的 out 参数、null out 指针不被解引用。

### 变更

- `control_panel.cpp` / `overlay_process_controller.cpp` / `main.cpp` 共 8 处
  `"rise"` / `"world"` 字面量改为调用 `gameIdToString` / `gameIdFromString`。
  字面量现在只存在于 `game_profile.cpp` 一处。对外格式（`--game` 参数、
  QSettings 值、子进程参数）逐字未变。

### 验证

`ctest` 36/36、`i18n_gate` 三门禁 PASS、洁净从零构建 0 error / 0 warning、
面板像素基准零 diff。

## [0.11.0] — 2026-09-27

一次以「不影响功能」为硬约束的重构。所有改动均通过 ctest、i18n 门禁与
离屏像素回归验证：三个发布二进制的动态依赖集合与重构前完全一致，
五张面板基准 PNG 逐像素零差异。

### i18n — 覆盖面与规范化

- 域文件从 2 个扩到 4 个，切分依据由「哪个进程读取」改为「谁负责重绘」：
  `overlay.json`（面板 chrome）、`console.json`（控制台 chrome）、
  `reader.json`（reader 状态/诊断）、`data.json`（数据名与演示数据）。
  键总数 337 → 325，删除 84 个无人引用的死键（最大一块是与 `zoneName()`
  重复的 `ui.zone_*`）。
- key 命名统一为 `<域>.<模块>.<语义>` 三层、leaf 一律 snake_case，
  共改名 132 个；`console.*` 的 62 个 camelCase leaf 全部规范化。
- **英文诊断串全部本地化**：`rise_reframework_manager.cpp` 33 条、
  `reframework_fetcher.cpp` 36 条改为 `trMessage(reader.reframework.*)`，
  两侧 zh/en 严格对称（含 `%N` 占位符数量与顺序）。
- 废弃代码内的中文兜底数组（`kFallback`），兜底策略收敛到
  「缺 key 时显示 key 本身」，规则写入 `docs/I18N.md`。
- 新增 `scripts/i18n_unused_keys.py`（死键报告）与
  `scripts/i18n_gate.py`（三个门禁统一入口，含何时跑哪个的说明）。

### 稳定性 — 用 trait 取代展示文案判据

`debuffAccent()` / `buffAccent()` 原先用 `name.contains("爆破")` /
`name.contains("blast")` 这类**中英子串同时匹配**来判断配色家族——界面
一旦翻译，配色就静默丢失。现改为：

- 新增 `AbnormalityAccent` 枚举（None/Blast/Fire/Defense/Sleep/Paralysis/Attack/Drink）
- World `kDebuffs`(13) + `kBuffs`(28) 与 Rise schema(16) 逐条标注 accent，
  由 reader 从**稳定 id**（内存 offset / schema id）填入快照
- `nameHas()` 及其 13 处调用全部删除，UI 只读 trait
- 演示数据与真实 reader 走**同一张表**反查，不存在第二份会腐烂的映射

### 架构 — MVVM 分层与编译边界

新增 `src/ui/viewmodel/`，三个零 QWidget 依赖的 VM：

- `DamageViewModel`：搬走 `panel_damage.cpp` 约 550 行伤害/DPS 统计引擎
- `MonsterPartListBuilder`：搬走 `buildPcList` 及其 mutate 的 `partTrack_`，
  **消除了 `friend` 声明**这个借道私有成员的坏味道
- `OverlayProcessController`：搬走子进程生命周期（`overlayPid_`、
  250ms `kill(pid,0)` 轮询、`pendingRestart_`、`currentGame_`）
- `riseReframeworkStatusLines()`：抽出 REFramework 状态派生纯逻辑

测试覆盖从 32 增至 34，新增 **139 条数值断言**（DamagePanel 126 +
REFramework 10 个 characterization 用例）。此前这些统计与状态机逻辑
**零直接测试覆盖**。

编译边界：新增 `mhw-reader` 与 `mhw-ui` 两个 STATIC 库，
`src/mhw_reader.cpp` 的编译份数 17 → 1；`src/ui/` 从散列 12 个 target
收敛为一个库（Qt 私有 qpa 头 PRIVATE 隔离）。

### 清理

删除 5 项死物（共 1151 行）：两个 qrc 编译但零代码读取的占位 JSON
`monsters/{parts,ailments}.json`（内容还是 "v0.1 占位"）、
`part_schemas.cpp.pre-PRA`、`spike_web.cpp`、孤儿测试
`monster_probe_buildup_fast.cpp`。二进制体积减少约 1.5 MB。
全部备份并附 sha256 校验清单（5/5 通过）。

### 安全网

新增离屏像素回归测试 `tests/snapshot_regression_tests.cpp` + 5 张基准
PNG。此前 `snap_*` 只是肉眼看图的诊断工具，没有任何断言兜底；
现在任何渲染漂移都会让 ctest 变红（反向验证过：改坏 50 像素会被精确
报出 `50 differing pixel(s); first at (60,80)`）。

### 已知限制

- ControlPanel 仍有约 2946 行，权重/位置持久化与 REFramework 按钮编排
  尚未拆分（原计划中边界不清的两块，留待后续版本）。
- qrc 无法下沉为共享库：实测 INTERFACE 与 STATIC 库持有均构建通过但
  运行时取不到资源（AUTORCC 注册被链接器丢弃）。这是 Qt 的硬约束。
