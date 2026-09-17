#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QtGlobal>

class QScreen;

namespace screen_query {

// How the reported physical size was obtained. Surfaced in the canvas
// header so the user can tell whether the number came from Qt itself,
// a compositor tool, or a fallback. Exact spelling matters because
// the user reads it.
enum class Source {
    QScreen,         // QScreen::geometry() × devicePixelRatio() — Qt's
                     // own answer. Right on Wayland, wrong on some X11
                     // fractional-DPI sessions.
    QPlatformScreen, // QPlatformScreen::geometry() via QScreen::handle()
                     // (Qt private API). Bypasses the X11
                     // fractional-scaling remap in some setups.
    XRandr,          // Shell-out: xrandr --current, parsed.
    KScreen,         // Shell-out: kscreen-doctor -j (KDE Wayland).
    WlrRandr,        // Shell-out: wlr-randr (Sway / Hyprland).
    DrmSysfs,        // /sys/class/drm/card*-eDP-*/modes. Last-resort
                     // Linux-native path; works without any
                     // compositor tool.
    Fallback,        // Could not determine. Hard-coded 1920×1080.
};

struct Result {
    QSize physical;   // what the user sees in the OS display settings
    QSize logical;    // what Qt uses for widget coordinates (DIPs)
    qreal dpr = 1.0;  // physical.width() / max(logical.width(), 1)
    Source source = Source::Fallback;
};

// v0.8: per-screen snapshot for the control console's "pick a screen"
// dropdown. `name` is QScreen::name() — on Niri that string is exactly
// the wlr-output identifier (eDP-1 / DP-1 / HDMI-A-1 …), and it's what
// LayerShellQt::Window::setScreen() matches against. `geometry` is in
// the global desktop coordinate space, which the HudCanvas preview
// uses to draw a faithful "this is where the panel will land" picture.
// `primary` flags the OS-reported primary output so the dropdown can
// label it ("DP-1 (primary)").
struct OutputInfo {
    QString name;
    QRect   geometry;
    qreal   dpr = 1.0;
    bool    primary = false;
};

// Pick the best answer from the available signals. Tries QPlatformScreen
// first (private API, but the most reliable in X11 fractional-DPI), then
// QScreen, then shells out to xrandr / kscreen-doctor / wlr-randr in
// order, then falls back. Safe to call from the GUI thread; the shell-out
// branch is bounded by a hard 800 ms timeout via QProcess::execute so
// a slow compositor tool never freezes the canvas.
Result detect(const QScreen *screen = nullptr);

// Short human label for the source, e.g. "QScreen", "XRandr".
// Used in the canvas header.
QString sourceLabel(Source s);

// v0.8: list every QScreen the Qt platform plugin knows about. The
// control console reads this to populate the per-panel "screen"
// dropdown. Names are QScreen::name() — on Niri that is the wlr-output
// identifier, on KDE it's the KScreen output id (eDP-1 / DP-2 / …),
// on X11 it's the Xinerama/XRandR output name. Whatever Qt returns
// here is what LayerShellQt::Window::setScreen() will match against.
QList<OutputInfo> listOutputs();

} // namespace screen_query
