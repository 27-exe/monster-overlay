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

## [0.12.0] — 玩家面板拆分：第一步子窗

### 做了什么

把玩家面板里的**异常状态 / 强化状态胶囊条**拆成独立的 layer-shell 悬浮窗，
由新开关 `--split-player-strip` 启用，**默认关闭**。开关关闭时行为与 0.11.4
逐像素相同——这不是「大部分相同」，是有测量背书的：

```
world default  panel=378x255  stripShown=no   doubleDraw=no
world split    panel=378x201  stripShown=yes  doubleDraw=no
rise default   panel=378x242  stripShown=no   doubleDraw=no
rise split     panel=378x201  stripShown=yes  doubleDraw=no
```

（`experiment/monster-overlay-v012/probes/split_verify.cpp`，真实渲染后数墨迹
像素。默认路径 `snapshot-regression-tests` 的 10 张像素基线也逐字节未变。）

### 拆分的地基

**`Panel` 现在可以画一个「没有 chrome 的窗」。** 新增 `setChromeVisible(bool)`
（默认 true）与 protected `paintPanelChrome()`。关键实现细节：关闭时
`paintPanelChrome()` **完全不触碰 painter**——不发透明 fill、不清 pen
（清 pen 也会落像素）。四个既有面板继续调原来的 `drawV03Chrome()`，
逐字未改，所以它们的输出不可能变。

**`virtual bool isSubWindow() const`**（默认 false）让 HudCanvas 与槽位注册表
不必 `dynamic_cast`。

**宽度本来就是 per-panel。** `drawV03Chrome()` 的 rect 读 `logicalSize_`，
而 `logicalSize_` 由各面板自己的 `setContentSize()` 写入（玩家 378、怪物 380、
伤害 380、宠物 300）。子窗因此可以是 91px / 125px 而不是被标题带撑到 378px。

这直接解决了审计量到的第一道障碍：`panel_player.cpp` 的 `// title always drawn`
让 `mask=0` 时的玩家面板收缩成一条 **378×50 的标题带**（墨迹横跨 [0..366]）
而不是消失。子窗不带标题，所以不存在这个问题。

### `kPanelCount` 怎么解耦

`kPanelCount` 现在是 `kBasePanelCount`（=4）的别名，`control_panel.h` 的
`std::array<PanelCtl, mhw::kPanelCount>` 照常编译——名字与值都没动。扩展走
新的索引区间：`kPanelSubWindowBase=4` / `kMaxPanelSubWindows=32` /
`kPanelSlotMax=36`，配 `isSubWindowIndex()` / `isAnyPanelIndex()`。

**为什么子窗槽位不可能与 base 槽位冲突**：所有子窗 slot 来自 `[4, 36)`，
必然不满足 `isPanelIndex()`（只认 0..3），所以 `std::array<PanelCtl, 4>` 与
HudCanvas 的 `slots_` 不可能被这些下标索引。

### 开关为什么是 CLI flag 而不是复用 mask 位

`PlayerSection::Debuff/Buff` 已经表示「用户想不想看这两段」。再让它同时表示
「哪个窗口画这两段」就把两个不相关的问题压到一个 boolean 上——`--mask-player 0`
会顺带回答窗口拓扑问题，控制台存档的一个 toggle 会无声改变 overlay 的窗口结构。
而且 mask 有 conf 回落链，持久化位被后续默认值翻掉正是 0.11.4 刚修的那类 bug。

### debuff/buff 只出现在一个窗里

```
maskPlayer                 = M
strip 收到的 mask          = M                    （未清位）
PlayerPanel 收到的 mask    = M & ~(Debuff|Buff)   （清位）

PlayerPanel:      onDebuff = (M & ~D) & D = 0   -> 不画
StatusStrip:      读 (M & D)                    -> 画
```

两个窗读的是同一个 `M`，strip 读未清的、panel 读清过的，二者相与恒为 0。
用户怎么 toggle，`D` 位上恰好一个窗为 1。这不需要两处代码「记得同步」——
派生发生在同一个表达式、同一个作用域、传递给两个消费者的同一帧。

顺带的好处：`onDebuff`/`onBuff` 是那两段**唯一**的 gate，下游三个消费方
（高度预留、World pill 行、Rise 状态网格）都读这个 bool 而非 mask，所以
高度、布局、绘制同时消失，没有第二条代码路径。

### 控制台侧的地基

新增 header-only `mhw::SubWindowRegistry`（在 `panel_source.h`，约 180 行）：
登记子窗槽位 + `Panel*` + adapter + 标题 + section 标签。**没有复用 `PanelCtl`**
——它是 10 个 widget 指针，为每个子窗造一整套 inspector 代价不成比例，而且
rail 只放得下四块；子窗本来就是内容型窗口（`setChromeVisible(false)`），
没有可拨的 mask 位。

**刻意没有在 `registerSubWindow()` 里调 `HudCanvas::bindPanel()`**：canvas 的
`slots_` 是 `std::array<Slot, 4>`，`bindPanel` 的守卫是
`if (!mhw::isPanelIndex(index)) return;`——子窗 slot ≥ 4 会被**按设计**拒绝。
放宽守卫会改变四个已有面板的预览，这正是本任务承诺不做的。几何源存在
`subWindows_` 里经 `entry()->adapter` 可达，等 HudCanvas 学会子窗槽位后才显示。
代码注释已写明这一点，避免下一个人误以为是漏接线。

### 顺带修掉 0.11.4 留下的那个缺陷

**「控制台保存的 mask 开关，手动跑 overlay 时不生效」。** `parseMask` 只从
`--mask-*` CLI 读，未设置时直接返回全开，而 README 与 USAGE 教的都是手动跑
`./monster-overlay`。现在优先级链是
`显式且可解析的 CLI > conf 行 > 0xFFFFFFFF`，形状同 `resolveStartupLocale`。

把决策抽成了 header-only `mhw::ui/viewmodel/panel_mask_fallback.h`
（`resolveStartupPanelMask(cli, conf, slot)`）。放 `viewmodel/` 而不是
`core/`：core 不得 include `ui/`，而它与其消费的 `panel_mask_codec.h` 同域；
「没有 widget 依赖」不等于「属于内核」。

### 验收

- 干净树全量构建：**0 warning、0 error**
- `ctest`：**55/55 通过**（比 0.11.4 多 2 个新测试 target）
- 门禁 7/7 PASS，含 `snapshot-regression-tests` 的 10 张像素基线逐字节未变
- 拆分契约实测：4 组（world/rise × default/split）**0 violation**
  （`probes/split_verify.cpp`）
- 新测试：`mask-fallback-tests`（优先级矩阵 + 双向突变测试）、
  `player-status-strip-tests`（剥离模型的纯函数变换）

### 已知未做

- **子窗还不能在控制台里定位**：`HudCanvas` 只认 4 个 base 槽位（见上文），
  所以拆出的 strip 现在只能用 panels.ini 手工编辑位置，或先在编辑模式里
  用方向键调。
- **控制台启动子进程时不带 `--split-player-strip`**：手动启动的用户会看到
  控制台显示旧拓扑而 overlay 已是拆分态。
- 只拆了第一个窗。武器/锋利度、HP/ST、衣装/翔虫、任务信息仍在单面板里。
  拆第二个窗时 `SubWindowRegistry` 已经就位，但「块 → 窗」的抽取经验
  （复用 `PlayerSection` 位、清位而非加 gate、chrome 关掉）只走通了一遍。

---

## [0.11.4] — 审计收尾：显示正确性 + 生命周期正确性

一轮只读审计（6 个并行子代理，覆盖 UI / reader / core / 构建门禁）产出 5 份报告，
逐条复核后修掉 21 项。分四类：**每局必见的显示错误**、**会静默断连的进程级状态**、
**配置往返丢失**、**构建门禁形同虚设**。全部修复以
「干净树全量构建 0 warning + 53/53 测试通过」验收。

其中 15 项由子代理并行修复（文件归属互不重叠，权威构建与 ctest 由父会话串行
执行），6 项由父会话在通读与复核审计结论时直接定位。审计本身推翻了子代理的
两处初判（详见第四节与 `audit/01-player-panel-bugs.md`）。

### 一、每局必见的显示错误

**DPS 面板在真实狩猎的前两秒显示假数据。** `panel_damage.cpp` 的 demo 数值
（311/240/177/101）判据是 `history().size() <= 8`，而真实开局的头 8 个
poll tick 恰好也只有 8 个采样——于是每局开头所有人的 DPS 都是那四个硬编码值，
两秒后才跳回真实数字。判据改为 `editMode()`：demo 数据只在编辑模式出现。

**任务计时器在非有限值下显示垃圾数字。** `lastElapsedSeconds` 跨 reader 边界，
`Inf > 0` 能过门禁，随后 cast 成 int 是平台定义值（实测通常是 `INT_MIN`）。
且原代码 `/60` 与 `% 60` 两次独立截断，在负值上会自相矛盾。现在一次截断喂给
两个数字，并加 `isfinite` 门禁。

**Rows 段关闭时标题行被覆盖。** `panel_damage.cpp` 的行循环是
`for (...) { if (!onRows) break; }`——裸 `break` 不推进 `y`，于是 Share 也关掉
时折线图会画到标题行那一行上。改为 `if (onRows) { for (...) { … } }`。

**新一局重置用了另一个容器的长度。** `damage_view_model.cpp` 的
`firstHitTick_.fill(0, names_.size())` 用身份表的长度去零化两个 per-row 向量。
`QList::fill(n, len)` 在 `len > size()` 时**扩容**、在 `len < size()` 时**截断**，
所以长度一旦分叉就会被悄悄改形状而不是报错。改用 `std::fill(begin, end, 0)`，
各按自己的长度。

**异常状态卡在切怪后首帧错判。** `ailTrack_` 按 **ailment id** 索引，不是按怪
id——两只怪共享同一个异常 id（旧的被麻痹、新的也被麻痹）就会撞上，新怪的卡
被旧怪的时间戳判成「还新鲜」或「已过期」。原注释以「HunterPie 有同生命周期」
为由豁免清理，但那个论证不成立：HunterPie 的 Widget 是**每怪一个实例**，
残留随 widget 一起死；我们是一张所有目标共享的定长表。切怪时一并清零。
（已核实 `!editMode()` 守卫，编辑模式 demo 预览不受影响。）

**切怪时怒气条与部位卡可能画出 `-2147483648%`。** 脏读的 NaN/Inf 进了
`qRound` / 除法。新增共享 `sanitizeQ10`，套用在异常签名、部位卡百分比与
计时文本、怒气条三处。

**部位卡显示 `--/180`。** `readPartStruct` 的守卫只挡 `isfinite`，不挡负号；
`chp` 是**剩余**计数，损坏部位读回的是一个小负数。`Valid` 判据只看
`mhp > 0`，于是负值原样下发。加符号卫兵。

**同一地址读两次却用第一次的值。** `monster_reader.cpp` 读 schema id 时，
`if (read(+0x12280)) m.id = hunterId;` 用第二次读的成功与否去 assign 第一次
读的值：此处成功而 :367 失败会赋值 -1。同一地址两次读不可能对值有分歧，
只可能对取没取到有分歧。改为单次读。

**缓存热路径上读失败会清空整个怪物列表。** 只有 `else` 分支查
`fresh.size() != 128`。缓存命中时若读失败，`components` 为空，落到下面的
despawn sweep 把 `monsterCache_` 整表清空，面板空一帧。现在两分支共用一次
读 + 一次校验，且**失败时不污染已有效的缓存**，下一 tick 仍能复用。

**断尾判定里的一个子句是死代码。** 注释称「`UpdateSeverableData` 不动
Flinch，所以第二句塌缩成同一 epsilon 测试」——实际该分支从未给 `p.flinch` /
`p.maxFlinch` 赋值，两者恒为初值 0，`sevFl` 恒 false。（本项目注释密度很高，
多处「因为 X 所以 Y」里的 X 已不成立；修 bug 必须回到赋值点。）

**衣装名在中文界面漏英文。** `mantleName()` 的 fallback 硬编码
`"Mantle #%1"`，而 id 来自游戏内存、无上界，是热路径。改走已存在的
`data.mantle.id.default`（"衣装" / "Mantle"）。

**`displayName()` 三处死存储。** `panel_sections.h` 的 Monster / Damage /
PetDamage 三个 `displayName()` 都写了 `const QString val = tr(key)` 赋值后弃用，
return 又调一次 `tr()`，且数组闭合括号与后面的 `if` 挤在同一行。

### 二、会静默断连的进程级状态

**一个 reader 会因为另一个 reader 查了游戏而被断开。** `process_memory.cpp`
的 PID 缓存在 `findGamePid()` 里是三个**函数级 static**——整个进程共用一条，
而它服务的 `ProcessMemory` 是 per-instance。exe 名一变就互相清缓存；5 秒节流
窗口内第二个 reader 看不到缓存又不能重扫，于是拿到 `nullopt`，调用方据此
`detach()`——**健康的 reader 被误判成「游戏已退出」**。缓存改为实例成员、
按 exe 名建表，且已 attach 且进程仍存活的目标直接复用，不受节流窗口影响。

（为那些还没有 `ProcessMemory` 实例的调用点保留了进程级入口，并在注释里
写明那个残留陷阱。）

**切区域会互相清对方的怪物缓存。** `world_reader.cpp` 的 `static WorldZone
lastZone` 同理：它是 `poll()` 里的函数级 static，守护的 `monsterCache_` /
`cachedArray_` / `cachedArrayBase_` 却都是成员。改为成员 `lastZone_`。
这一条与 ARCHITECTURE §6.4 记载的 PID 缓存是**两个不同的 static**。

### 三、配置往返丢失

**改语言失败后永远切不过去。** `localeSyncPoll()` 在把候选返回给调用方**之前**
就把 `lastSeenLocale` 提交了；调用方随后才 `StringTable::load()`。load 失败时
conf 的 mtime 没变，下一轮在 mtime 比较处直接早退，这个候选再也不会被提出——
用户改一次 conf 切语言，若那个语言的资源缺失/损坏，界面就卡在旧语言直到重启。
现在「探测」与「提交」拆成两个动作，只在 load 成功后才提交。新增回归测试
（`tests/locale_sync_tests.cpp`，150 行）钉住「失败后同一份 conf 仍能再次被提出」。

**插上新显示器后控制台预览永远看不到它。** `hud_canvas.cpp` 的
`previewScreenInfo()` 里两张表都用函数级 static lambda 初始化，整个进程只跑
一次。拔屏后 `kByName` 还留着已不存在的 output name，查不到就静默回落到**同样
陈旧**的 fallback。原注释把这判为「acceptable」，那个判断不成立。改为可变成员，
`setPreviewScreen()` 时重建。

**面板在 4K 屏存的位置，拔掉屏后永久飞出屏幕且不自愈。** `clampMargin` 上限
8000 且不含屏幕尺寸，任何路径都不会把它拉回来。现在**存**的时候按原值落盘
（不 clamp），**推**的时候按当前屏幕几何 clamp，并接上 `screenAdded` /
`screenRemoved` / `primaryScreenChanged`——拔屏时 surface 落回 primary 并按
primary 重新推 margin，插回时若指向它就重新解析。这比「存时 clamp」更对：
存时 clamp 会让用户在拔掉 4K 的那个 session 里**丢失**自己在 4K 上的设置。

**控制台每次打开都泄漏 4 个 adapter。** `bindPanel(i, new PanelSourceAdapter(...))`
从无释放，adapter 还存裸 `Panel*`，安全只靠一个没写下来的析构顺序保证。改为
`unique_ptr` 成员数组，并在 `panel_source.h` 把「canvas 不拥有、绑定方拥有」
这条契约写成明示注释——它之前是隐形的，正是泄漏的原因。

### 四、构建门禁形同虚设

**`diagnostic-targets-build` 门禁在干净树上从不注册。** `CMakeLists.txt` 里
`find_program(BASH_EXECUTABLE bash)` 写在它的使用者 `if(BASH_EXECUTABLE)`
**之后** 270 行。`find_program` 结果进 cache，**首次** configure 时变量还没被
写入，判假，门禁不注册。后果：默认的 `cmake --build build && ctest --test-dir build`
一路绿灯（52 个测试），却从未验过「5 个 `EXCLUDE_FROM_ALL` target 能不能编译」
——而这是唯一能发现 `rise-probe` 编译断裂的门禁。把 `find_program` 上移后，
干净树首配即 53 个测试。

**「sectionMask_ 从不持久化」是误报，但真缺陷在隔壁。** mask 确实有持久化，
走 `monster-overlay.conf` 的 `player=/monster=/damage=/pets=` 四行（用户契约，
用户手改，`locale_sync.h` 还会轮询同一个文件），编解码在
`viewmodel/panel_mask_codec.h`。真缺陷是**反方向不往返**：live overlay 的
`parseMask` 只从 `--mask-*` CLI 读，未设置时返回全开——mask 只在控制台按
START 启动子进程时（走 argv）才传得过去。用户手动跑 `monster-overlay`
（README 与 USAGE 里的常规启动方式）时，保存的开关状态全部不生效。
**此项未修**：它要动 `src/main.cpp`，与本轮 locale 改动同一文件，留给 v0.12
一并处理，避免同一版本里两次改同一个 poll 循环。

### 五、验收

- 干净树全量构建：**347/347 edge，`BUILD_EXIT=0`，`grep -c warning: = 0`，
  `grep -c error: = 0`**
- `ctest`：**53/53 通过**，0 失败（7 个 `monster-probe*` 按设计 SKIP——它们
  需要一个真实运行的游戏进程）
- 门禁 **6/6 PASS**：`lib-purity-gate` / `zero-reference-gate` /
  `diagnostic-targets-build` / `i18n-gate-check` / `-parity` / `-unused`
- 变更规模：25 个文件，+1449 / −232（含本文件与两处 README 的版本号同步）

### 已知未修（留给 v0.12）

- **mask 不往返 live overlay**（见上文第四节）
- **玩家面板标题行不受 section mask 管辖**：`panel_player.cpp:736` 注释
  `// title always drawn`，8 个 section 全关后面板收缩成一条 378×50 的标题带
  而不是消失。这是拆分子窗的第一道障碍——拆出的胶囊条不该带 378px 宽的
  标题与 chrome，需在拆分契约里决定
- `ARCHITECTURE.md` 5 处计数口径与实测不符（`mhw::` 符号数 53→58、
  §6.1 正文 27 实测 26、`zoneLabel()`「零测试」整节已过期等）
- 3 个 target 未列 qrc，违反 ARCHITECTURE §2.5

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
