# SIZING — 悬浮窗的尺寸与自适应

> 面向贡献者：怎么给一个子窗（sub-window）定尺寸、怎么接「用户可调长宽比」这个需求、
> 以及为什么 layer-shell 下这个需求只能做成一种样子。
>
> 运行时事实以 `src/ui/panel_sizing.h` 的注释为准，本文是规范，不是补丁说明。
> 相关文档：`docs/USAGE.md` 是用户操作指南，`docs/ARCHITECTURE.md` 是全仓分层。

---

## 1. 这套机制解决什么问题

用户对 v0.12 悬浮窗的反馈是原话：

> 「同时我希望用户可自定义调整长宽比，滚轮变为调整缩放。同时悬浮窗内容，比如这个buff会自适应长度和宽度来填充」

以及一句实测：

> 「悬浮窗表现很差，缩放过大字体过大导致几乎完全不可用」

第二句**不是缩放 bug**。把 `panel_player_split.cpp` 的发布算法用一次性代码
复现出来量了一遍（10 个胶囊，每个按 `3 + 6 + name + 12 + timer` 量宽）：

```
World demo，10 个胶囊，最宽 79px   -> 窗口 W = 91，每个胶囊槽 slotW = 23
Rise  demo，10 个胶囊，最宽 113px  -> 窗口 W = 125，每个胶囊槽 slotW = 34
```

每个胶囊拿到 23px（World）/ 34px（Rise），而它自己的 name + timer 列宽是
79 / 113。`drawPill()` 里计时器文字画在 `box.width() - 8` = 11px / 22px，
装不下 9pt 的 "12s"，于是「缩放调小一点」看上去更糟、调大更糟——因为根本
不是 scale，是**窗口宽度由一个胶囊的宽度决定，然后被 3 个胶囊瓜分**。

两处算术、两个输入：

| 位置 | 在算什么 | 输入 |
|---|---|---|
| `panel_player_split.cpp:471` | 窗口宽 `w` | **单个最宽胶囊**的自然宽度 |
| `panel_player_split.cpp:159` | 每个胶囊的槽宽 `slotW` | 上面那个 `w`，除以 3 |

`w` 是「一个胶囊有多宽」，`slotW` 想要的是「三个胶囊排完还剩多少」。
这个差就是 v0.10.3 那个「按一个高度画、按另一个高度留」的同类缺陷，
同一个类：**一个 wrap 有两套算术**。

`panel_sizing.h` 把这两套合成一套：窗口宽度**由网格自己算**，
画的时候用它的**精确逆运算**取槽宽，于是预留的和画出来的不可能不一致。

---

## 2. 「长宽比可调」在 layer-shell 下到底是什么

### 2.1 合成器不提供的东西

`src/ui/panel.h:18-27` 已经写明了，但值得再说一次，因为它界定了整个需求：

> Wayland layer-shell cannot place windows at arbitrary screen coordinates —
> position is defined solely by the anchor corner plus margins.

后果有三：

1. **没有 resize 语义**。layer-shell 协议没有「客户端请求尺寸 / 合成器仲裁」这
   回事。`setFixedSize()` 是客户端单方面声明，合成器只管按锚角和 margins 摆。
   所以「用户拖出一个矩形」在协议层不存在，ide (c) 方向不可实现。
2. **没有拖拽**。位置靠方向键改 margins（`Panel::nudgeMargins`，10px / Shift
   50px），不是靠鼠标。这是 USAGE 里明说的用户契约。
3. **没有「屏幕有多大」这个输入**。面板不知道可用工作区尺寸，
   `Panel::contentSize()` 在首次绘制之前返回 `-1×-1`（父会话实测：
   `size()=320x120 / 378x800, contentSize()=-1x-1`，只有渲染过一次之后
   才变成真实的 `378x201`）。所以**任何读当前尺寸来决定新尺寸的代码都是循环
   依赖，第一帧就拿到 -1**。这是 `panel_sizing.h` 只吃原始数字、
   `Panel::setContentSize()` 只当最后一步的原因。

### 2.2 结论：长宽比 = 内容分布方式，不是独立参数

所以「用户可自定义调整长宽比」正确的实现是选 **方向 (b)：用户调列数，
宽高随之自适应**。

理由，逐条：

- **(a) 宽度系数**（内容按系数换行）看着更平滑，但它把两个量绑在了一起：
  系数一变，既改列数又改槽宽，而槽宽由字体度量决定、不由系数决定。
  一个 0.6 的系数对 78px 的胶囊和对 200px 的条含义完全不同，
  用户拧的时候得不到可预测的结果。列数是一个整数，拧一圈就是多一列。
- **(b) 列数**是内容分布唯一的整数自由度。10 个胶囊：1 列 → 10 行，
  一列 79px 宽；2 列 → 5 行；3 列 → 4 行（发布值）；5 列 → 2 行，
  约 415px。宽高比从 79/180 到 415/22 连续变化，每一步都是合法布局。
  它同时解释了「自适应填充」：**窗口宽度由占满的列决定，槽宽由列数反推**，
  于是内容铺满、没有死区。
- **(c) 拖矩形** 上面说了，协议不提供。
- **(d) 直接设一个宽高比数值** 不可实现，且方向错了：给定一个 16:9 的比例，
  客户端既不能要求合成器腾出这块地方，也不知道屏幕有多大。
  更糟的是它把用户引向一个必然失败的期望——「我设了 16:9，
  为什么窗口不是 16:9？」因为最后一个不满行。

### 2.3 用户能调什么 / 不能调什么

| | 用户能否调 | 机制 |
|---|---|---|
| 每个子窗的列数 | **能**，编辑模式下调 | 新增一个面板键（见 §6），`1..6` 一档一档 |
| 缩放（字号、胶囊整体大小） | **能**，现状不变 | 滚轮，`Panel::wheelEvent`，`0.5×–3×` |
| 位置 | **能**，现状不变 | 方向键改 margins |
| 窗口「拖」成任意矩形 | **不能** | layer-shell 无 resize 语义 |
| 直接设一个宽宽高比数值 | **不能**，也不该做 | 见 §2.2 (d) |
| 长宽比本身 | **只能作为结果看** | `panel_sizing::aspectRatio()` 是给控制台显示的数字，不是输入 |

**滚轮和列数不冲突**：滚轮继续只调缩放（这是用户要求的「滚轮变为调整缩放」，
也就是现状），列数走另一个键 / 控制台的 Appearace 折。两者正交：
缩放改**字多大**，列数改**排多宽**。

---

## 3. 每个函数的契约

全部是 `constexpr` / `inline` 的纯整数算术，**不 include 任何 Qt 头**，
输入单位统一为逻辑像素。

### 3.1 `gridItemsPerRow(available, itemMinWidth, gap, count)`

「每行放几个」。返回 `k`，其中 `1 <= k <= count` 且
`k*itemMinWidth + (k-1)*gap <= available` 取最大。

| 输入 | 返回 | 理由 |
|---|---|---|
| `count <= 0` | `0` | 空网格没有行 |
| `itemMinWidth + gap <= 0` | `count` | 项不占宽，预算不构成约束 |
| 一个都放不下 | `1` | 0 列不是布局，是除零；调用方用 `gridOverflows()` 知道这事 |
| `k` 会有 `count` 那么大 | `count` | 3 项永不预留第 4 列 |
| 负数预算 / 负项宽 / 负 gap | 各自 sanitise 后照常算 | 负数永远不作为答案返回 |

**闭环解里的 `+ gap` 就是不等式里的 `- gap` 移项**，写成朴素的
`available / (itemMinWidth + gap)` 会少报一列——突变测试 A 专打这个：
`available=175, itemMinWidth=80, gap=10`（两个需要 170，三个需要 260），
正确答 2，改坏后答 1，整行少一个胶囊。

### 3.2 `gridOverflows(available, itemMinWidth, gap = 0)`

「连一个都放不下了吗」。**这是单独的快捷判断，不是 `perRow == 1` 的等价物**：
`available=77, itemMinWidth=78` 时 `gridItemsPerRow` 仍然答 1，而本函数答
`true`——调用方据此决定省略文字、还是把窗口收成 `gridEmptySize()`。
**一个像素的裁切远好过整行消失**，这是发布态那个 420 上限教给我们的。

### 3.3 `gridRowsFor(count, perRow)`

行数。`count <= 0` 或 `perRow <= 0` 答 0，`perRow <= 0` 配合非空 count 时
按每行一个算而不是除零。和 `panel_player_split.h:90` 的
`statusStripRows()` 是同一个公式（后者把它写死在 3 列上）。

### 3.4 `gridContentWidth(count, perRow, itemWidth, gap)`

**预留侧**。`count` 个项按 `perRow` 列排、每项 `itemWidth` 宽、
列间距 `gap` 的**格子总宽**。不含 margin——要 `setContentSize()` 的数字用
`gridTotalSize()`。

**不满行不改变列宽**：`gridContentWidth(3, 4, 78, 5)` 答 244 而不是
4\*78+3\*5=327，因为第 4 列根本不存在。

### 3.5 `gridContentHeight(rows, rowHeight, rowGap)`

`rows * rowHeight + (rows-1) * rowGap`。`rows <= 0` 答 0。
这也是 `panel_player.cpp:714-726` 和 `panel_player_split.cpp:483`
的 `blockHeight` 公式。

### 3.6 `gridSlotWidth(available, perRow, gap)`

**绘制侧**，是 `gridContentWidth` 的精确逆。下取整，
`available` 连 gap 都装不下时答 0（不是负数，也不是偷偷答 1）。

**这才是「自适应填充」的数学基础**：

```cpp
// 网格自己算出窗口宽（cells），再反推出每个槽的宽
const int cells  = gridContentWidth(count, perRow, itemW, gap);
const double w   = cells + 2 * margin;          // 送去 setContentSize()
const int slotW  = gridSlotWidth(w - 2 * margin, perRow, gap);
// slotW == itemW，精确成立，测试 §5 把 perRow 1..6 × 宽 1..140 × gap 0..30
// 全扫了一遍
```

预留用一只函数、绘制用它的逆，绘制就不可能超出预留——这是「一套算术、
三个消费者（宽 / 高 / 画）」规则在这里的落地。

### 3.7 `gridTotalSize(count, perRow, itemWidth, itemHeight, gap, rowGap, margin)`

一次算完 `{width, height}`，就是 `setContentSize()` 该收到的值。
`count <= 0` 答 `{0, 0}`——**调用方必须在这时候改用 `gridEmptySize()`，
不要把 0x0 送去 `setFixedSize()`**。

### 3.8 `gridEmptySize(margin)`

空网格的收起尺寸：`{2*margin, 2*margin}`。这是发布态四个面板已经各自拼写
的四份 `2 * kMargin`（`panel_player_split.cpp:470` 和 `:502`、
wirebug `:306`、bars `:325`）的**单一出处**——空分支和满分支不可能差一个
margin。

### 3.9 `clampScale(s)` / `clampWidth(v, lo, hi)` / `nonNegative(v)` / `clampInt(v, lo, hi)`

兜底四件：

- `clampScale` 把缩放钳到 panel.cpp:25-26 自己的区间（`0.5–3.0`，镜像而非
  共享，理由见 `panel.h` 分层讨论）。**NaN 排在区间前面**：所有 NaN 比较都是
  false，所以 `!(s > kScaleMin)` 先命中，答下界；`std::clamp` 在 `lo > hi` 上
  是 UB，所以这里显式重排而不是裸 `std::clamp`。
- `clampWidth` 处理用户可调范围（控制台 spin box、损坏的 INI）：`lo < 0` 抬到
  0，`lo > hi` 塌到 `hi`（而不是 UB），`hi <= 0` 表示无上界。
- `nonNegative` 保证任何负数度量都不会变成 `setFixedSize()` 的负参数。
- `clampInt` 是**不兜底的裸钳**，`lo > hi` 由调用方负责；给
  `gridItemsPerRow` 这种自己已经 sanitise 过的值用，别拿去接用户输入。

### 3.10 `effectiveSize(w, h, scale)` / `scaleRound(logical, scale)`

设备像素尺寸。**为什么必须是 `lround` 而不是截断**：

`paintEvent()` 先 `p.scale(scale_, scale_)` 再画逻辑内容，所以**墨迹占
`lround(logical * scale)` 个设备像素**；surface 是
`setFixedSize(lround(logical * scale))`——同一个乘积，否则最后一列被裁、
最后一行消失。三个候选，以 `logical=125, scale=0.5`（积 62.5）为例：

| 取整 | 结果 | 后果 |
|---|---|---|
| 截断 | 62 | **比墨迹小**，右边缘半个像素没了。`scale=0.7` 时 87.5→87 少 1px，
  而且永远偏小，`scale < 1` 必然亏 |
| `lround` | 63 | ≥ 墨迹，不裁 |
| `ceil` | 63 | 同；只在积的小数 < 0.5 时多一个墨迹盖不住的死像素 |

所以 `lround` 处处不劣于截断（只在积 ≥ x.5 时不同，而那正是截断会裁掉
整整一个像素的地方），和 `ceil` 最多差一个**死透明像素**而不是一个
缺失墨迹像素。对半透明 overlay 这是正确的取舍：死像素看不见，
被裁的胶囊边框看得见。**`scale < 1` 的方向必须是向上取整**，
因为 surface 是墨迹的下界，不是墨迹的靶子。

突变测试 B `lround` → 截断：`125×79 @0.5` 变 62，扫 400 个逻辑宽 ×
0.5–3.0 之后三千多个组合同时变红。

### 3.11 `aspectRatio(w, h)`

**报告用，不是输入**。宽高比是列数的**结果**：控制台可以在列数旋钮旁边显示
「现在约 6.7:1」，让用户看见这一档拧下去是什么后果。任何一边不是真实尺寸
答 `0.0`（不是 NaN）。§2.2 (d) 解释了为什么不能反过来喂。

---

## 4. layer-shell 的位置约束如何影响尺寸设计

三条，都是硬约束：

1. **位置 = 锚角 + margins**（`panel.h:18-27`，`nudgeMargins` 改它）。
   尺寸变了**锚点附近的边不动**：TopLeft 锚的窗向右下长。
   这意味着子窗加宽会把 PlayerPanel 旁边的空间吃掉——列数调大之前
   要考虑它旁边有没有东西。
2. **不知道可用工作区**，所以「别超出屏幕」这类判断客户端做不了。
   `clampWidth` 的 420 上界不是算出来的，是拍的一个「装得下」的经验值，
   注释里就这么写的（`kStripMaxW = 420 // a fully buffed hunter stays sane`）。
   接子窗时保留同类注释，别把这个数说成屏幕算出来的。
3. **`Panel::contentSize()` 首绘前是 `-1×-1`**。见 §2.1 第 3 条。
   `panel_sizing.h` 的输入全是原始数字（count、度量出来的项宽、margins、
   gaps），输出才是 `setContentSize()` 的参数——方向是单向的。

---

## 5. 接入一个子窗要动哪些文件

`panel_sizing.h` **目前还没有任何子窗在用**。以下是我列出但**没有动手**的
清单（越界文件由父会话统一改）：

| 文件 | 改什么 |
|---|---|
| `src/ui/panel_player_split.cpp` | `contentWidth()` 改成「由网格算宽」；`drawPillGrid()` 的 `slotW` 改成 `gridSlotWidth()`；把 `kStatusStripPillsPerRow` 从 3 常量化成 `columns_` 成员 |
| `src/ui/panel_player_split.h` | 加 `columns_` 成员 + `setColumns(int)` / `columns()`；`contentWidth()` / `blockHeight()` 声明留着，实现改走本 header |
| `src/ui/panel_{wirebug,mantles,bars}.cpp` | 同上模式。这四个是 v0.12 已经开了头的四个子窗，**优先只接 strip 一个作样板**，另三个等样板落地 |
| `src/ui/panel.cpp` | **列数的编辑模式输入**：滚轮已被 `scale_` 占用，加 `[` / `]`（或 `-` / `=`）改列数，走 `virtual void nudgeColumns(int)`；同时 `saveConfig()` 落盘这个键 |
| `src/ui/panel.h` | `nudgeColumns` 的虚声明 + 子窗默认实现 |
| `src/main.cpp` | 每帧把列数从 settings 推给子窗（和 `setSectionMask` 同一个位置） |
| `src/resources/i18n/*/overlay.json` | 若列数要做成控制台 UI，加一行文案；只做键盘绑定则不需要 |
| `CMakeLists.txt` | `player-layout-tests` 旁边加 `panel-sizing-tests` target（清单见下） |

**两张 split 像素基线会变**：`tests/baseline/player_split_world`（113×79）和
`player_split_rise`（140×57）的宽度来自旧的 `contentWidth()`。
接进去之后窗口变宽（10 个胶囊按 3 列排，natural 79/113，
cells = 3\*79+2\*5 = 247 → 窗口 259；Rise 同理 3\*113+2\*5 = 349 → 361），
高度不变。**父会话需要 `./build/snapshot-regression-tests --write-baseline`
重新生成这两张，然后人工确认**：胶囊不再挤成 23px，是这次改动的目的，
所以基线变化是预期的，不是回归。

---

## 6. 测试与配套

### 6.1 target

照 `player-layout-tests` 的范式（header-only 列为源、只链 Qt6::Core、
不碰 `mhw-ui`）。**父会话加到 `CMakeLists.txt`（我没有动）**：

```cmake
# v0.12+: the shared sub-window size / wrap arithmetic — ui/panel_sizing.h.
# QtCore-only and header-only like player-layout-tests: every function
# under test is pure integer arithmetic with no Qt type in its
# signature, so the target drops Qt entirely and links no library.
add_executable(panel-sizing-tests
    tests/panel_sizing_tests.cpp
    src/ui/panel_sizing.h
)
target_link_libraries(panel-sizing-tests PRIVATE Qt6::Core)
target_include_directories(panel-sizing-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(panel-sizing-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME panel-sizing-tests COMMAND panel-sizing-tests)
```

`tests/panel_sizing_tests.cpp` 会被 **zero-reference-gate 拦下**（不在 census），
这是预期的。**census 不需要登记**：门禁扫的是 `src/**/*.cpp`，
`panel_sizing.h` 是 header-only，不产生 TU。要做的只是上边这个 target 落地，
然后父会话按流程 `zero_reference_gate.py --update-snapshot` 一次。

### 6.2 覆盖

84 项检查，其中三组是扫全空间而不是抽样：

| 组 | 断言 | 覆盖 |
|---|---|---|
| §1 wrap | `gridItemsPerRow` 与一份**暴力参照**逐点一致 | counts 0..8 × 宽 {0,1,10,77,78,79,120} × gap {0,1,4,5,20} × 预算 0..500 step 13，**12285 组** |
| §5 identity | `gridSlotWidth(gridContentWidth(...)) == itemWidth` | perRow 1..6 × 宽 1..140 step 7 × gap 0..30 step 7，**600 组** |
| §6 zoom | 设备尺寸 ≥ 墨迹尺寸 | 逻辑宽 1..400 × 0.5–3.0 step 0.1，**10400 组** |

边界用例按任务要求逐条点名的：0 项、1 项、恰好整除（`gridSlotWidth(6,3,0)==2`）、
差一个放不下（169 vs 170）、宽度放不下一个（10 vs 78）、负数输入
（负预算 / 负项宽 / 负 gap / 负 count / 负 margin / 负 logical）。

暴力参照是**另一个实现**而不是同一个公式的第二种拼写，所以 header 里的
公式没法「自己同意自己」。

---

## 7. 没验证的部分

- **没有跑 cmake / ninja / ctest**（父会话 build/ 串行占用中，明确禁止）。
  语法验证用 `g++ -std=c++20 -fsyntax-only`，实跑用
  `g++ -std=c++20 -I src -Wall -Wextra -Wpedantic` 单独编
  `tests/panel_sizing_tests.cpp`——这个 target 不链 Qt 库也能编过并运行，
  所以 84/84 是真实执行结果，但**它进 ctest 之后的输出未经确认**。
- **两张 split 像素基线我没有重生成**（§5）。数字 259 / 361 是我从
  `gridContentWidth` 公式算的期望值，不是渲染出来的实测值。
- **没有 QtGui 参与**，所以「`gridSlotWidth` 的返回值喂给
  `QPainter::drawRoundedRect` 之后胶囊真的不裁字」这件事**没有端到端验证**。
  单测证的是整数关系；要证渲染，需要父会话在接入后跑一次
  offscreen 探针（skill 里的 ink-counting 套路）。
- **`aspectRatio()` 的 UI 呈现没做**：控制台 affordance 不在本任务范围。
