#pragma once

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

} // namespace screen_query
