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
    XRandr,          // Shell-out: xrandr --current, parsed. X11 sessions
                     // ONLY — see the note in detect() about why this is
                     // never trusted on a Wayland session.
    KScreen,         // Shell-out: kscreen-doctor -j (KDE Wayland).
    WlrRandr,        // Shell-out: wlr-randr (Sway / Hyprland).
    DrmSysfs,        // /sys/class/drm/card*-eDP-*/modes. Last-resort
                     // Linux-native path; works without any
                     // compositor tool.
    Fallback,        // Could not determine. Hard-coded 1920×1080.
};

// ---------------------------------------------------------------------------
// Coordinate systems — read this before touching any field below
// ---------------------------------------------------------------------------
// There are TWO size spaces in play and they are NOT interchangeable:
//
//   * the PANEL/mode space: what the display's video mode says, i.e. the
//     framebuffer the GPU programs the CRTC with. 1920×1080 for a 1080p
//     monitor, regardless of how it is mounted.
//   * the LOGICAL/DIP space: what QScreen::geometry() reports and what
//     every Qt widget coordinate is expressed in. When the compositor
//     applies a transform (wl_output.transform), Qt folds the rotation
//     into this number, so a 90°-rotated 1920×1080 monitor reports
//     1080×1920 here.
//
// A rotated monitor therefore has geometry().width() < geometry().height()
// even though its panel is 1920×1080. That is the whole trap: anything
// that reads the panel/mode size (xrandr's mode list, /sys/class/drm
// .../modes, wlr-randr's mode list, kscreen-doctor's "size") gets the
// UNROTATED numbers, and mixing that with Qt's rotated geometry() is what
// produced a devicePixelRatio of 1.778 on a plain 1.0 screen.
//
// Every field of Result is in the LOGICAL space, i.e. the same space as
// QScreen::geometry(). physical is logical × dpr, NOT the panel mode.
struct Result {
    QSize physical;   // logical × dpr, in the ROTATED frame (see above).
                      // This is what the user sees in the OS display
                      // settings for a transformed output.
    QSize logical;    // what Qt uses for widget coordinates (DIPs) —
                      // exactly QScreen::geometry().size() whenever a
                      // QScreen is available.
    qreal dpr = 1.0;  // physical.width() / max(logical.width(), 1). Must
                      // always be one of kLegalDpr below.
    Source source = Source::Fallback;
};

// The only device-pixel ratios the code accepts as "clean". Anything
// outside this set (with a small tolerance) is treated as a bug signal,
// not as a DPR — see detect() for the 1.778 story.
inline constexpr qreal kLegalDpr[] = {1.0, 1.25, 1.5, 2.0, 2.5, 3.0};

// Tolerance for matching kLegalDpr. A real DPR is exact (1.0, 2.0, …) or
// one of a handful of well-known fractional values; 0.02 is far wider
// than any rounding error and far narrower than the gap between any two
// legal values.
inline constexpr qreal kDprTolerance = 0.02;

// True when `dpr` is one of the six legal ratios. A rotated screen on a
// real DPR of 1.0 yields 1920/1080 = 1.778, which is NOT in the set —
// that is the whole point of testing membership instead of a range.

// Whether a candidate PHYSICAL size describes the same screen shape as
// the LOGICAL size Qt reports. Cross-multiplied, so integer widths cannot
// lose precision on the ratio.
//
// Exported rather than left file-local because it is half of the contract
// detect() relies on, and a test that re-implements it proves nothing about
// the code that runs: 1920x1080 against a 1080x1920 logical is the case
// this exists to reject, and the only way to assert it is to call it.
[[nodiscard]] bool sameShapeAsLogical(const QSize &candidate,
                                      const QSize &logical);
inline constexpr bool isLegalDpr(qreal dpr)
{
    for (qreal v : kLegalDpr)
        if (dpr > v - kDprTolerance && dpr < v + kDprTolerance) return true;
    return false;
}

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
// QScreen, then shells out to kscreen-doctor / wlr-randr on a Wayland
// session (xrandr deliberately EXCLUDED there — XWayland's xrandr reports
// the panel mode, not the rotated geometry, and would manufacture a fake
// DPR), then falls back. Safe to call from the GUI thread; the shell-out
// branch is bounded by a hard 2 s timeout via QProcess so a slow
// compositor tool never freezes the canvas.
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

// v1.0.1: THE one spelling of "which output does this name mean right
// now" for the console/canvas hot paths (drag handlers call it per
// mouse-move event).
//
// Contract, in order:
//   1. `requested` names an output Qt has RIGHT NOW  -> return
//      `requested` verbatim. Identity is preserved byte for byte; this
//      is the user's stored intent and nothing normalises it.
//   2. `requested` is empty OR names an output that is not attached
//      (undocked / slept / renamed) -> return the live Qt primary's
//      name, i.e. QGuiApplication::primaryScreen()->name(), falling to
//      the FIRST live screen with a non-empty name when Qt reports no
//      primary (offscreen, mid-hotplug). This is the "missing DP ->
//      内屏" rule: the window mounts somewhere real, the stored intent
//      is untouched.
//   3. Qt knows about no outputs at all -> QString() , deterministically.
//      Empty in, empty out; no guessing, no synthetic names.
//
// Deliberately IDENTITY-only and compositor-blind:
//   * it reads QGuiApplication::screens() and NOTHING else — no
//     hyprctl / xrandr / wlr-randr / kscreen-doctor, no
//     screen_query::detect(). detect() answers a PHYSICAL question
//     (size, DPR) and may shell out with a 2 s timeout; that is fine
//     once per screen change and fatal per drag event, which is why the
//     two are kept in separate functions.
//   * names come from QScreen::name() only. listOutputs() may synthesise
//     a `screen@x,y` label for a nameless screen; this function never
//     does — an identity a compositor cannot match is worse than none.
//
// The two-argument overload is the TEST OVERRIDE seam: tests pass the
// list they want (including the empty list, which no real
// QGuiApplication ever produces) instead of injecting QPA screens.
// Callers in production use the one-argument form.
[[nodiscard]] QString effectiveOutputName(const QString &requested);
[[nodiscard]] QString effectiveOutputName(const QString &requested,
                                          const QList<OutputInfo> &live);

} // namespace screen_query
