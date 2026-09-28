# Changelog

本文件记录 monster-overlay 的变更。格式参考 Keep a Changelog。

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
全部备份于工作区并附 sha256 校验清单（5/5 通过）。

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
