# 控制台指南 / Native Console Guide

## 中文

### 看哪块屏，与窗口属于哪块屏，是两件事

顶栏“显示器”只改变当前查看屏。侧栏总览显示该屏上的所有分组窗口，分组导航只显示当前组；重复点总览仍是总览。选中画布窗口只改变检查器，不会偷偷切换查看屏或退出总览。

三列检查器分别用于窗口属性、分区/同组窗口和真实 HUD 预览。目标显示器作用于**选中窗口**，不是整个同组的所有子窗。跟随解析顺序是：窗口显式归属 → 所属母面板归属 → 系统主屏。查看一块没有归属窗口的屏幕时，`0 个窗口` 是合法空态。

### 编辑与保存

1. 选择游戏与 Single/Split 布局；配置按游戏 × 布局作用域隔离。
2. 选分组，并在画布点击目标窗口。总览是只读查看。
3. 点“编辑布局”，在预览内拖动；方向键移动1逻辑像素，Shift+方向键移动10逻辑像素。检查器坐标按窗口的目标屏解释，不按当前查看屏解释。
4. 使用检查器调整窗口属性、分区或显隐。整窗隐藏与所有分区关闭不是同一种状态。
5. “保存”写入当前作用域的真实配置；预览仍然是演示数据，启动覆盖层后才读取真实游戏状态。

`Ctrl`+滚轮缩放的是画布，不是窗口的内容比例。若显示器预览放大而出现内部滚动，可用此操作回到适合查看的缩放。控制台编辑不会自行拉起覆盖层；覆盖层自身的 `--edit` 快捷键仍见 [使用指南 §4](USAGE.zh-CN.md)。

### 主题、语言与环境

设置包含深/浅色、中文/English，以及 Rise 的 REFramework 环境管理。主题和语言选项彼此独立。World 没有伙伴伤害数据源，不显示对应窗口；Rise 在 Single 与 Split 中都保留真实伙伴窗口。

设置/帮助/导出采用模态窗口，内容可滚动。Esc或遮罩可关闭模态，关闭后可以重新打开。控制台 CLI 保持 `--locale`、`--light`、`--snap`、`--print-locale`、`--print-screen-info`、`--version` 和 `--help`，用 `monster-control --help` 查看。

### 数据边界

- 控制台预览使用真实 Panel 绘制代码与演示数据，不代表已经连接游戏。
- 保存会写配置；“当前查看屏”不会写入窗口归属或改变其移动量程。
- `monster-overlay.conf` 保存开关/语言/布局选择，`panels.ini` 保存游戏 × 布局下的窗口属性。
- 控制台自身窗口、分栏、zoom由 Qt 设置身份 `monster-control` 保存。
- 需要真实游戏的观测探针不等同于离线单元测试，未运行时不能宣称实机通过。

## English

### Viewing an output is not assigning a window to it

The topbar display selector changes only the output being viewed. Overview shows all groups on that output; a group view shows only that group. Repeated Overview clicks keep Overview active. Clicking a board window updates the inspector without changing the viewed output or leaving Overview.

The inspector contains window properties, sections/group windows, and a real HUD preview. Its target-output field changes the **selected window**, not every sibling. Follow resolves explicit window output → parent-panel output → system primary. Viewing an output with no assigned windows legitimately displays zero windows.

### Editing and saving

1. Select the game and Single/Split topology. Settings are isolated by game × layout scope.
2. Choose a group and click the actual window on the board. Overview is read-only.
3. Enter Edit Layout and drag in the preview. Arrow keys move 1 logical pixel; Shift+arrows move 10. Inspector coordinates use the window's target output, not the viewer output.
4. Adjust properties, sections or window visibility. A hidden window and a window with every section disabled are distinct states.
5. Save writes real settings for the active scope. The preview remains demo data; launching the overlay enables real-game reading.

Ctrl+wheel zooms the canvas, not a window's content scale. If a zoomed preview has internal scrollbars, use this control to return to a comfortable viewing scale. Console editing does not launch an overlay child. Overlay `--edit` shortcuts remain documented in [Usage §4](USAGE.md).

### Theme, language and environment

Settings provides dark/light themes, Chinese/English, and Rise REFramework environment management. Theme and language groups are independent. World has no pets-damage data source; Rise retains its real pets window in both Single and Split.

Settings, Help and Export use scrollable modal content. Escape or the backdrop closes a modal, and it can be reopened. CLI flags remain `--locale`, `--light`, `--snap`, `--print-locale`, `--print-screen-info`, `--version` and `--help`; run `monster-control --help` for details.

### Data boundaries

The console preview uses real Panel paint code with demo data. Save writes settings, while changing the viewer never changes target ownership or movement bounds. `monster-overlay.conf` stores choices; `panels.ini` stores scoped window properties. Console frame/splitter/zoom settings use the `monster-control` Qt identity. Offline automation is not a substitute for unperformed live-game observation tests.
