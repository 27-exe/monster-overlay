# HTML overlay spike — 实验记录

## 结论(2026-07-27)

**HTML overlay 路线在 KDE Plasma + Qt 6.11 + WebEngine 上不可行**,且即使可行,
单 panel Chromium 占用已达 **~741 MB RSS**(vs 现有 Qt overlay ~30 MB,
**24× 差距**)。**保留 spike 代码作为将来参考,不继续推进**。

---

## 实验尝试

5 次尝试,每次结果像素级相同(屏幕 y=0..40 灰白 KWin decoration,整窗口
`(23,23,23)` panel body 色,无透明):

| # | 修改 | 结果 |
|---|---|---|
| 1 | 基础 layer-shell + WA_TranslucentBackground + `setBackgroundColor(Qt::transparent)` + JS 注入 + `WA_TransparentForMouseEvents` | ❌ 大窗口 + 边框 |
| 2 | + 删 `setWindowTitle` + `setAnchors(Top\|Bottom\|Left\|Right)` | ❌ 同上 |
| 3 | + `(void)winId()` 强制 native window + `WA_OpaquePaintEvent` false | ❌ 同上 |
| 4 | + `QT_WAYLAND_DISABLE_WINDOWDECORATION=1` + `--disable-gpu --use-gl=swiftshader` + `DocumentReady` 注入 | ❌ 同上(且 swiftshader 让 Chromium 启动失败) |
| 5 | 移除所有 `setWindowFlags` 干扰 | ❌ 同上,但这次 Chromium 稳定运行 |

---

## 失败诊断

### 现象
- KWin 加了 OS decoration(屏幕顶部 y=0..40 灰白)
- panel body 完全覆盖整个 2560×1600(说明 layer-shell 没把窗口收成 panel 大小)
- `(23,23,23)` 全屏覆盖桌面(说明 Chromium 透明没生效)

### 已知根因(社区文档)

**Qt 6.3+ WebEngine 透明背景 bug**(QTBUG 跟踪系统有记录,但 Qt 6.3.0 之后
"已修"又被回归)。具体描述(Qt Forum 用户实测):

> I have pinpointed the bug relating to Qt version 6.3.0 and onwards.
> Using Qt6.2.3 everything is working flawlessly every time, then as
> soon as I switched to Qt 6.3.0 (and any version after), it breaks
> again.

**本机 Qt 6.11.1 + qt6-webengine 6.11.1-4 都 >= 6.3** → bug 命中。

### 已知成功案例(对照)

**`0bCdian/wal-qt-host`**(Qt 6 + WebEngine + LayerShellQt)支持:
> Wayland compositor implementing `zwlr_layer_shell_v1` (Hyprland,
> Sway, river, Niri, Wayfire, …). **Does not run on GNOME.**

KWin 在 Plasma 5.27+ 实现 `wlr_layer_shell_v1`,但 KWin 对 Chromium GPU
surface 兼容性差(无单独 fix 报告)。

---

## 占用数据(关键决策依据)

`ps -o pid,pcpu,pmem,rss,cmd -C mhw-overlay-web,QtWebEngineProcess`:

| 进程 | RSS | 说明 |
|---|---|---|
| mhw-overlay-web (主) | 420 MB | Qt + WebEngine 主进程 |
| QtWebEngineProcess (zygote 1) | 79 MB | sandbox |
| QtWebEngineProcess (zygote 2) | 79 MB | sandbox |
| QtWebEngineProcess (zygote 3) | 19 MB | |
| QtWebEngineProcess (renderer) | 161 MB | Chromium renderer |
| **总计** | **~741 MB** | |

**对比:**

| 方案 | RSS | 备注 |
|---|---|---|
| 现有 Qt overlay(3 panel + QPainter) | ~30 MB | 整个 binary |
| HTML overlay spike (1 panel + Chromium) | ~741 MB | **24×** |
| HunterPie 原版(.NET + WPF) | ~150 MB | 估 |

741 MB vs 30 MB — **即使修复透明 bug,占用仍不可接受**。

---

## Spike 代码位置

- `src/spike_web.cpp` — spike binary 源(~200 行)
- `web/spike.html` — spike HTML(single panel mock-up)
- `CMakeLists.txt` line 65+ — `mhw-overlay-web` target

编译:`cmake --build build --target mhw-overlay-web`
运行:
```bash
QT_QPA_PLATFORM=wayland \
  QT_WAYLAND_DISABLE_WINDOWDECORATION=1 \
  ./build/mhw-overlay-web
```

---

## 给未来自己的提醒

如果再次有人/我自己想重启 HTML 路线,**先**做这些前置检查:

1. 退到 Qt 6.2.3 + qt6-webengine 6.2.3 (Arch Linux 仓库不可用,需要自定义编译)
2. 换 Hyprland/Sway/river compositor(KWin 不支持 Qt WebEngine 透明)
3. 或者:换 WebKitGTK(GTK 项目,KWin 友好,但 KDE 6 上有 ghosting bug)
4. 或者:直接用 HunterPie 原版(.NET + WPF,Wine 上能跑)

如果以上都不接受,**继续 Qt + QPainter 路线**。