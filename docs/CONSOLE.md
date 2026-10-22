# 控制台指南 / Native Console Guide

## 中文

### 看哪块屏，与窗口属于哪块屏，是两件事

顶栏“显示器”只改变当前查看屏。侧栏总览显示该屏上的所有分组窗口，分组导航只显示当前组；重复点总览仍是总览。选中画布窗口只改变检查器，不会偷偷切换查看屏或退出总览。

三列检查器分别用于窗口属性、分区/同组窗口和真实 HUD 预览。目标显示器作用于**选中窗口**，不是整个同组的所有子窗。跟随解析顺序是：窗口显式归属 → 所属主面板归属 → 系统主屏。查看一块没有归属窗口的屏幕时，`0 个窗口` 是合法空态。内屏与外接屏都可选；保存的归属离线时临时回退在线主屏，运行时位置按该屏约束，但不会把原输出名称/边距重置写回。拔插后，下拉框、画布查看屏和窗口计数一起刷新。

### 编辑与保存

1. 选择游戏与布局：整块 / 分块 / 混合；配置按游戏 × 布局作用域隔离。混合模式允许**逐个窗口**决定合并回主面板还是独立成窗（「全部独立 / 全部合并」是它的两个快捷操作，只作用于当前分组列出的那些窗口；没有选中分组时才作用于整份清单），该选择存进 `monster-overlay.conf` 的 `layout.windows.<game>=` 行，每个游戏一行、互不覆盖。从混合切回整块时，主面板恢复自己完整的分区，画布照常画出它们。
2. 选分组，并在画布点击目标窗口。总览是只读查看。
3. 点“编辑布局”，在预览内拖动；方向键移动1逻辑像素，Shift+方向键移动10逻辑像素。检查器坐标按窗口的目标屏解释，不按当前查看屏解释。
4. 使用检查器调整窗口属性、分区或显隐。整窗隐藏与所有分区关闭不是同一种状态。
5. 改动即写真实配置：**关闭控制台时自动保存**当前作用域，直接启动悬浮窗前也会先保存（侧栏没有单独的“保存”按钮）；预览仍然是演示数据，启动覆盖层后才读取真实游戏状态。
6. 侧栏底部的布局操作：「重置布局」把当前分组的布局恢复为出厂默认并立即刷新画布；「全部重置」覆盖两个游戏、三种布局下的全部设置（有确认框）；「导入布局包」从文件按声明的作用域覆盖当前配置（导入前自动备份，可回滚）；「导出布局」把当前这一套窗口设置写成文本。
7. 预览画布只挂载当前游戏确实拥有的窗口：世界下不建翔虫窗、崛起下不建衣装窗（与真实悬浮窗的挂载门一致），两个游戏的窗口数因此各为 10 而不是 11。

显示器示例可用可见的 `−` / `+` 或 `Ctrl`+滚轮在 50–400% 间缩放；`1:1` 恢复示例的默认适配比例，不是把真实显示器按物理像素一比一放进主窗。示例的**显示区域由你决定**：缩放只改变画面内部的内容与滚动范围，不会把舞台和检查器往下顶，也不会覆盖你拖动分隔条调过的高度。

“当前内容状态”默认**适应窗口**即可看全内容，不必先拖滑块再滚动。它有一套**独立的预览缩放**（`−` / `+` / `实际大小` 1:1 / 复位），与真实窗口的缩放值互不影响——拖动预览不会改写 `panels.ini` 的 `scale`，也只对预览生效。原高分辨率图源保持不可变，`实际大小` 显示的就是原始像素，不做插值放大；旁边始终显示当前缩放百分比与尺寸。预览容器可拖动右侧分隔条调整大小。

窗口不透明度变化会立即刷新图源；棋盘底纹是控制台参考背景，选中边框不属于 HUD 内容，不随透明度变淡。拖动只更新几何与位置字段，不逐事件重绘整组或重建分区控件。

检查器的窗口属性区按**该窗口真实支持的能力**显示控件：有列数的窗口才出现「列数」，最小宽/高、固定宽/高与恢复自动尺寸始终可用。主面板与分块子窗各有自己的属性，改动只作用于当前选中对象。

顶栏「编辑布局」只修改控制台画布；另有一个**在悬浮窗中编辑**的按钮，会真的以 `--edit` 启动悬浮窗，在真实屏幕上拖动窗口，关闭时保存并返回控制台。两者都保留，作用对象不同。

### 主题、语言与环境

设置包含深/浅色、中文/English，以及 Rise 的 REFramework 环境管理。主题和语言选项彼此独立。World 没有伙伴伤害数据源，不显示对应窗口；Rise 在 Single 与 Split 中都保留真实伙伴窗口。

设置采用贴合内容的紧凑模态窗：宽度最多 680，高度按正文自适应（上限 700px，且不超出屏幕与控制台窗口），外圈不再有独立黑框与多余留白；正文超出时在窗内滚动，关闭按钮始终可达。帮助面板与设置同为 680 宽的紧凑模态，正文是**五个操作步骤**（每节一个标题一段说明，节间留出可见间距）：预览与选择 / 目标显示器 / 三种布局 / 保存与导出 / 布局编辑，末尾附一张快捷键表（方向键移动 1 逻辑像素、Shift+方向键 10、`1` `2` `3` `4` 切换面板、`Ctrl`+滚轮缩放示例 50–400%、`Esc` 退出编辑）——表内只列真正的键盘按键；布局区的缩放按钮 `−` / `+` / `1:1`（1:1 恢复默认比例）是按钮而非快捷键，在表下单独注明；导出使用全窗模态层。Esc或遮罩可关闭模态，关闭后可以重新打开。控制台 CLI 保持 `--locale`、`--light`、`--snap`、`--print-locale`、`--print-screen-info`、`--version` 和 `--help`；另有三个只用于验收截图的抓图参数，都配合 `--snap` 使用：`--snap-select=<面板>,<子窗>`（抓图前先选中一个窗口，让检查器以填充状态入镜，子窗写 `-1` 表示主面板）、`--snap-scroll=<start|end>`（抓图前把检查器内容区滚到顶部或底部）、`--snap-dialog=<help|settings|export>`（改为拍摄帮助 / 设置 / 导出三个模态窗，抓图发生在该模态自己的事件循环里）。用 `monster-control --help` 查看。

### 数据边界

- 控制台预览使用真实 Panel 绘制代码与演示数据，不代表已经连接游戏。
- 保存会写配置；“当前查看屏”不会写入窗口归属或改变其移动量程。
- `monster-overlay.conf` 保存开关/语言/布局选择，`panels.ini` 保存游戏 × 布局下的窗口属性。
- 控制台自身窗口、分栏、zoom由 Qt 设置身份 `monster-control` 保存。
- 需要真实游戏的观测探针不等同于离线单元测试，未运行时不能宣称实机通过。

## English

### Viewing an output is not assigning a window to it

The topbar display selector changes only the output being viewed. Overview shows all groups on that output; a group view shows only that group. Repeated Overview clicks keep Overview active. Clicking a board window updates the inspector without changing the viewed output or leaving Overview.

The inspector contains window properties, sections/group windows, and a real HUD preview. Its target-output field changes the **selected window**, not every sibling. Follow resolves explicit window output → parent-panel output → system primary. Viewing an output with no assigned windows legitimately displays zero windows. Internal and external outputs are selectable. Offline assignments temporarily resolve to the live primary and clamp runtime placement without rewriting saved output names/margins. Hotplug refreshes the picker, canvas viewer and counts together.

### Editing and saving

1. Select the game and topology: Single / Split / Mixed. Settings are isolated by game × layout scope. Mixed lets you decide **per window** whether it merges back into its parent panel or runs as its own window (Detach all / Merge all are its two shortcuts, and they act only on the rows the current group lists — the whole list only when no group is focused); that choice is stored in the `layout.windows.<game>=` row of `monster-overlay.conf`, one row per game, so the two games cannot overwrite each other. Switching from Mixed back to Single restores each parent panel's full sections, and the board draws them as before.
2. Choose a group and click the actual window on the board. Overview is read-only.
3. Enter Edit Layout and drag in the preview. Arrow keys move 1 logical pixel; Shift+arrows move 10. Inspector coordinates use the window's target output, not the viewer output.
4. Adjust properties, sections or window visibility. A hidden window and a window with every section disabled are distinct states.
5. Changes are written as real settings the moment you make them: **closing the console saves** the active scope, and launching the overlay saves first if you have not closed (the rail has no separate Save button). The preview remains demo data; launching the overlay enables real-game reading.
6. The rail foot's layout verbs: **Reset Layout** restores the focused group to the shipped default and repaints the board immediately; **Reset All** covers both games and all three layouts (with a confirmation); **Import** overwrites the current config from a file's declared scope (auto-backed-up first, rollback-able); **Export** writes the current set to text.
7. The preview board mounts only the windows the current game actually has: no 翔虫 window under World and no 衣装 window under Rise — the same mount gate the live overlay uses — so each game shows ten windows rather than eleven.

Visible `−` / `+` controls and Ctrl+wheel zoom the display example between 50–400%. `1:1` restores the example's default fit, not physical-pixel equality with the monitor. The example's **display area is yours to set**: zooming changes only what happens inside the picture and its scroll extent, so it never pushes the stage and inspector down the page, and never overwrites a height you set by dragging the splitter.

The **HUD detail preview fits its container by default**, so you can read all of it without dragging a scrollbar first. It has its **own preview zoom** (`−` / `+` / `Actual size` 1:1 / reset), independent of a window's real scale — changing the preview never writes a `scale` row into `panels.ini`. The source image stays untouched: `Actual size` shows the original pixels with no interpolated upscaling, and the current percentage and size are always shown beside it. Drag the splitter to resize the preview container.

Opacity changes refresh the source image immediately; the checker guide and selection border belong to the console, not HUD content. Dragging updates geometry/readouts without rebuilding section widgets or rendering the entire group on every move.

The inspector's window-property area shows controls **the selected window actually supports**: Columns appears only for windows that have them, while min width/height, fixed width/height and auto size are always available. A parent panel and a split sub-window each carry their own properties, and edits apply to the current selection only.

The toolbar's Edit Layout changes the console canvas only. A separate **Edit in overlay** button really launches the overlay with `--edit`, lets you drag windows on the real screen, and saves and returns to the console on close. Both are kept; they act on different things.

### Theme, language and environment

Settings provides dark/light themes, Chinese/English, and Rise REFramework environment management. Theme and language groups are independent. World has no pets-damage data source; Rise retains its real pets window in both Single and Split.

Settings uses a compact modal that hugs its content: at most 680px wide and as tall as the body needs (ceiling 700px, never past the screen or the console window), with no separate black frame or extra padding around it. Long content scrolls inside the window and the close button is always reachable. Help uses the same 680px compact shell and its body is now **five operational steps** (one caption and one paragraph each, with visible spacing between sections): Preview and selection / Target display / The three layouts / Save and export / Layout editing, followed by a shortcut table (arrows move 1 logical pixel, Shift+arrows 10, `1` `2` `3` `4` switch the panel cards, `Ctrl`+wheel zooms the display example 50–400%, `Esc` leaves edit mode). The table lists keyboard keys only; the layout area's zoom buttons `−` / `+` / `1:1` (1:1 restores the default fit) are buttons, not shortcuts, and are noted as such under the table. Export keeps the full-window modal layer. Escape or the backdrop closes a modal, and it can be reopened. CLI flags remain `--locale`, `--light`, `--snap`, `--print-locale`, `--print-screen-info`, `--version` and `--help`; three more flags exist purely for acceptance screenshots and all pair with `--snap`: `--snap-select=<panel>,<sub>` selects a window before the grab so the inspector is captured filled (sub `-1` means the parent panel), `--snap-scroll=<start|end>` scrolls the inspector's content box to the top or the end before the grab, and `--snap-dialog=<help|settings|export>` photographs that modal instead of the console window, from inside the modal's own event loop. Run `monster-control --help` for details.

### Data boundaries

The console preview uses real Panel paint code with demo data. Save writes settings, while changing the viewer never changes target ownership or movement bounds. `monster-overlay.conf` stores choices; `panels.ini` stores scoped window properties. Console frame/splitter/zoom settings use the `monster-control` Qt identity. Offline automation is not a substitute for unperformed live-game observation tests.
