# v1.1.5

The control console is a native window now, with a per-window layout model.

## New

- **Three window layouts.** *Single* (one window per panel), *Split* (every
  section group gets its own window) and *Mixed* — new: decide **per window**
  whether it stays separate or merges back into its main panel. The choice is
  stored per game × layout, so World and Rise never overwrite each other, and
  `--print-layout-plan` prints the resolved topology as JSON without touching
  a config file.
- **Per-window output ownership.** Pick the screen each window goes to; the
  inspector names the panel it follows and prints the screen it actually
  resolves to right now.
- **Layout editing that behaves.** Drag windows on the console board, or
  launch the real overlay and drag them over the game. Reset one group to the
  shipped default, reset everything, import a layout package, export the
  current one as text.
- **A preview that tells the truth.** The console draws the same panel code the
  overlay uses, with demo data, and mounts exactly the windows the overlay
  mounts for the game in force: ten under World (no wirebug), ten under Rise
  (no outfit).

## Changed

- The console is a native Qt window — sidebar, display overview, three-column
  inspector — instead of an embedded HTML page.
- Changes are written when the console closes. There is no separate Save
  button (the write was already automatic), and the side "Reset demo" button
  is gone too.
- The help panel lists the real keyboard shortcuts; the zoom `−` / `+` / `1:1`
  are buttons, and the panel says so.
- Facing users it is now "main panel" (the old "mother panel" read oddly); the
  literal `mothers` field in `--print-layout-plan` output keeps its name.

## Fixed

- Reset Layout and Import Layout repaint the board and the overlay windows
  immediately, instead of waiting for the next click on another window.
- The preview no longer mounts an empty wirebug window under World or an empty
  outfit window under Rise.

## Requirements

Needs **Qt 6.12** runtime (the shipped binaries are built against it; an older
Qt fails to load with missing symbols), glibc 2.34+, GCC 13+ runtime, and
**Wayland** — X11 is not supported. Rise damage tracking additionally needs
the bundled REFramework Lua producer, which the console installs for you from
**Settings → Rise environment · REFramework & scripts**. Configuration keeps
working: `~/.config/monster-overlay/panels.ini` and `monster-overlay.conf`
are read exactly as before.

## Assets

`monster-overlay-v1.1.5-linux-x86_64.tar.gz` — 62.2 MB
sha256 `a12894af22799e59795c23371c9b9e42a9e549cb62c03856cf8f6898e84abf01`

Binaries: `monster-overlay` (the HUD), `monster-control` (the console),
`monster-doctor` (a read-only diagnostic report when the overlay shows
nothing). See the packaged README for requirements and startup.

## 中文要点

- 控制台换成原生窗口：侧栏 + 显示器总览 + 三列检查器，不再是内嵌网页。
- 新增三种布局：整块 / 分块 / 混合；混合模式可**逐窗**决定独立还是并回主面板，
  选择按「游戏 × 布局」分别保存，两个游戏互不覆盖。
- 每个窗口可选目标显示器，检查器显示它跟随的主面板与实际解析到的屏幕。
- 修复：重置布局 / 导入布局包后立即刷新画布与悬浮窗；预览不再多出空的翔虫窗
  （World）和空的衣装窗（Rise）。
- 变更：改动在关闭控制台时写入，去掉「保存」「重置演示」两个按钮；帮助面板只列
  真按键，zoom 按钮说明移到表下；面向用户统一改称「主面板」。
- 依赖：随包二进制需要 **Qt 6.12** 运行时；配置兼容，`panels.ini` 与
  `monster-overlay.conf` 照旧读取。
