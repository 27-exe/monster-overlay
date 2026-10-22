# v1.1.5

A redesigned console with flexible window layouts.

## New

- **Console:** display overview and window inspector for arranging the HUD.
- **Layouts:** Single, Split and Mixed. Mixed lets you merge or separate
  individual windows; settings are saved per game and layout.
- **Window controls:** choose each window's display, drag it in the console
  or overlay, and import, export or reset layouts.

## Fixed

- Resetting or importing a layout now refreshes the preview immediately.
- Removed empty Wirebug windows in World and Mantle windows in Rise.

## Upgrade notes

- Changes are saved when the console closes; the redundant Save and Reset
  demo buttons were removed. Existing configuration remains supported.
- Prebuilt binaries need x86_64 Linux, **Wayland**, **Qt 6.12**, `layer-shell-qt`,
  glibc 2.34+ and libstdc++ with `CXXABI_1.3.15` (GCC 14+). Rise damage tracking
  requires the bundled REFramework Lua producer.

## Assets

`monster-overlay-v1.1.5-linux-x86_64.tar.gz` — 62.2 MiB

SHA-256: `5c4ff6576554d85b630f5d21c961cc18b8944f0877b631aab0ed3b3177c32f86`

## 中文要点

- 控制台重新设计：提供显示器总览和窗口检查器，可视化调整 HUD 布局。
- 支持整块 / 分块 / 混合布局；混合模式可逐窗拆分或并回主面板，各游戏、各布局的
  设置分别保存。
- 可逐窗指定显示器、拖动定位，并导入、导出或重置布局。
- 修复重置 / 导入后预览未立即刷新，以及 World 空翔虫窗、Rise 空衣装窗。
- 关闭控制台时自动保存，移除「保存」「重置演示」按钮；旧配置继续可用。
  随包二进制需 **Qt 6.12 + Wayland**。
