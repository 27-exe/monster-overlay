#include "screen_query.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QProcess>
#include <QRect>
#include <QRegularExpression>
#include <QScreen>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <functional>
#include <functional>

// Private Qt API for X11 fractional-DPI bypass. Documented as
// "unstable" in some Qt versions but has been stable since 5.x
// (geometry() is the only method we call, and is pure virtual in
// QPlatformScreen so all backends implement it).
#include <qpa/qplatformscreen.h>

namespace screen_query {

namespace {

// Parse "2560x1600" out of xrandr/WlrRandr lines. Picks the * marked
// (current) output if present, else the first line with a mode.
QSize parseModeLine(const QString &line)
{
    static const QRegularExpression rx(QStringLiteral(
        "([0-9]{3,5})x([0-9]{3,5})"));
    const auto m = rx.match(line);
    if (!m.hasMatch()) return {};
    return {m.captured(1).toInt(), m.captured(2).toInt()};
}

QSize parseXrandrOutput(const QString &stdout)
{
    // xrandr --current output: lines like
    //   "   2560x1600     59.99*+  60.00 ..."
    // The * marks the current mode. Some compositors also emit a
    // header line ending in "connected ..." — skip those.
    //
    // COORDINATE SPACE: the value this returns is the MODE SIZE, i.e.
    // the framebuffer the CRTC is programmed with. It is NOT rotated.
    // A 90°-rotated 1080p panel still lists 1920x1080 here, while Qt
    // reports 1080x1920. Only ever compare this against an X11
    // QScreen::geometry() — never against a Wayland one. See
    // usableAlongsideWaylandGeometry().
    QSize fallback;
    for (const QString &line : stdout.split('\n')) {
        if (line.contains("disconnected") || line.contains("connected") ) {
            // Output header, not a mode line.
            continue;
        }
        const QSize s = parseModeLine(line);
        if (s.isEmpty()) continue;
        if (line.contains('*')) return s;   // current mode, take it
        if (!fallback.isValid()) fallback = s;
    }
    return fallback;
}

QSize parseWlrRandr(const QString &stdout)
{
    // wlr-randr output (no JSON flag in older versions):
    //   "Output 'eDP-1' ... modes: 1920x1080, 2560x1600, ..."
    //
    // COORDINATE SPACE: same caveat as parseXrandrOutput() — these are
    // mode sizes, not rotated geometry.
    for (const QString &line : stdout.split('\n')) {
        const QSize s = parseModeLine(line);
        if (!s.isEmpty()) return s;
    }
    return {};
}

QSize parseKscreenJson(const QString &stdout)
{
    // kscreen-doctor -j emits a JSON document. The primary output
    // usually has "primary":true but on freshly-rebooted Wayland
    // sessions we observed "primary":null — so we pick the first
    // "enabled":true output instead. Then we accept the physical
    // pixel size from "size" (always present), and prefer
    // "geometry" when it's set (KDE ≥ 5.27 sometimes emits logical
    // coords there, sometimes physical — ambiguous, so we only
    // trust it when "size" is missing).
    //
    // COORDINATE SPACE: "size" is the MODE size, unrotated, exactly
    // like the xrandr/wlr-randr parsers.
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(stdout.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    const QJsonArray outputs = doc.object().value(QStringLiteral("outputs")).toArray();
    QJsonObject primary, firstEnabled;
    for (const QJsonValue &v : outputs) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("primary")).toBool(false))
            primary = o;
        if (firstEnabled.isEmpty() && o.value(QStringLiteral("enabled")).toBool(false))
            firstEnabled = o;
    }
    const QJsonObject chosen = primary.isEmpty() ? firstEnabled : primary;
    if (chosen.isEmpty()) return {};
    // "size" is the physical pixel mode (always present on real
    // outputs). "geometry" sometimes overlaps with "size" on
    // identical-DPI setups but on Wayland fractional-DPI is the
    // LOGICAL rectangle — ignore it and trust "size".
    const QJsonObject sz = chosen.value(QStringLiteral("size")).toObject();
    if (!sz.isEmpty()) {
        const int w = sz.value(QStringLiteral("width")).toInt();
        const int h = sz.value(QStringLiteral("height")).toInt();
        if (w >= 640 && h >= 480) return QSize(w, h);
    }
    // Last-ditch: pick the current mode.
    const QString modeId = chosen.value(QStringLiteral("currentModeId")).toString();
    for (const QJsonValue &m : chosen.value(QStringLiteral("modes")).toArray()) {
        const QJsonObject mo = m.toObject();
        if (mo.value(QStringLiteral("id")).toString() == modeId) {
            const QJsonObject msz = mo.value(QStringLiteral("size")).toObject();
            const int w = msz.value(QStringLiteral("width")).toInt();
            const int h = msz.value(QStringLiteral("height")).toInt();
            if (w >= 640 && h >= 480) return QSize(w, h);
        }
    }
    return {};
}

QSize runCommand(const QString &program, const QStringList &args,
                 std::function<QSize(const QString &)> parser)
{
    QProcess p;
    p.start(program, args);
    // 2 s is generous for a tool that normally finishes in <30 ms.
    // Compositors occasionally stall KScreen during session resume
    // or when re-allocating the CRTC; the previous 800 ms cap was
    // too aggressive and turned those transient stalls into false
    // negatives, making detect() fall through to a wrong answer.
    if (!p.waitForFinished(2000)) {
        p.kill();
        p.waitForFinished(200);
        return {};
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) return {};
    return parser(QString::fromLocal8Bit(p.readAllStandardOutput()));
}

// Linux kernel exposes the connected display's actual mode under
// /sys/class/drm/card*-eDP-*/edid (preferred), or the mode lines
// under /sys/class/drm/card*-eDP-*/modes. Both are authoritative
// (they're what the driver actually programmed the CRTC with) and
// independent of Qt's session-type remapping or any compositor
// tool. The downside: it requires the edid/modes files to be
// readable, which they usually are for the active user. We only
// take this as a last-resort signal because parsing EDID is heavy
// and the mode-list format varies between drivers.
//
// COORDINATE SPACE: unrotated mode size, same caveat as above.
// Additionally this is a GLOBAL answer — it has no idea which
// QScreen we were asked about — so it must never be allowed to
// contradict the geometry of the screen we are actually reporting.
QSize tryDrmSysfsImpl()
{
    QDir drm(QStringLiteral("/sys/class/drm"));
    if (!drm.exists()) return {};
    // Look at every subdir that has a `modes` file. They look like
    //   card0-eDP-1, card0-HDMI-A-1, renderD128, ...
    // Only the connectors have modes; render nodes don't.
    const QStringList entries = drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : entries) {
        QFile f(drm.filePath(sub + QStringLiteral("/modes")));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString modes = QString::fromLocal8Bit(f.readAll()).trimmed();
        if (modes.isEmpty()) continue;
        // Prefer the first non-empty mode (kernel lists the active
        // one first); fall back to the largest parsed mode.
        QSize first, largest;
        for (const QString &m : modes.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QStringList wh = m.split(QLatin1Char('x'));
            if (wh.size() != 2) continue;
            bool okW = false, okH = false;
            const int w = wh[0].toInt(&okW);
            const int h = wh[1].toInt(&okH);
            if (!okW || !okH || w < 640 || h < 480) continue;
            const QSize s(w, h);
            if (!first.isValid()) first = s;
            if (w * h > largest.width() * largest.height()) largest = s;
        }
        if (first.isValid()) return first;
        if (largest.isValid()) return largest;
    }
    return {};
}

// No forward declarations: these helpers are defined here, before the
// dispatch table in detect() uses them (the old anonymous-namespace decls
// were dead code and warned under -Wunused-function).

QSize tryXrandr()      { return runCommand("xrandr", {"--current"}, parseXrandrOutput); }
QSize tryKscreen()     { return runCommand("kscreen-doctor", {"-j"}, parseKscreenJson); }
QSize tryWlrRandr()    { return runCommand("wlr-randr", {}, parseWlrRandr); }
QSize tryDrmSysfs()    { return tryDrmSysfsImpl(); }

// ---------------------------------------------------------------------------
// Cross-checking a mode-sized candidate against Qt's rotated geometry
// ---------------------------------------------------------------------------
// A "physical" candidate from xrandr / wlr-randr / kscreen-doctor /
// /sys/class/drm is a MODE size: the framebuffer the CRTC is programmed
// with. Qt's QScreen::geometry() is a LOGICAL size with the compositor
// transform already folded in. On a rotated output those two disagree in
// SHAPE, and dividing one by the other manufactures a devicePixelRatio
// out of nothing:
//
//   real 1080p monitor, transform 90°, real DPR 1.0:
//     mode / panel space : 1920x1080   (what xrandr lists)
//     logical / Qt space : 1080x1920   (what geometry() reports)
//     dpr as computed    : 1920/1080 = 1.778   <-- FABRICATED
//
// Measured on this machine's own Hyprland session (see the audit
// probes): xrandr under XWayland printed "1920x1080 59.96*+" for
// HDMI-A-1 while Qt's wayland plugin reported geometry() 1080x1920,
// and detect() duly reported dpr=1.7778 source=XRandr for a screen
// whose real devicePixelRatio() is exactly 1.0.
//
// The check below is the only thing standing between that measurement
// and the canvas: the candidate must have the SAME aspect ratio as the
// screen it is being attributed to. A rotated candidate is transposed
// relative to Qt's answer, so the ratio differs and the candidate is
// discarded. Note that this is deliberately NOT a rotation detector —
// Qt's own orientation API turned out to be inert on Wayland (see
// nativeRotationDegrees()).
} // namespace

bool sameShapeAsLogical(const QSize &candidate, const QSize &logical)
{
    if (candidate.isEmpty() || logical.isEmpty()) return false;
    // Cross-multiply so integer widths can't lose precision.
    return candidate.height() * logical.width()
        == logical.height() * candidate.width();
}

namespace {

// ---------------------------------------------------------------------------
// Rotation, and why there is no in-process source for it
// ---------------------------------------------------------------------------
// Every plausible API was measured against the live Hyprland session
// with hyprctl reporting transform 1 (90°) for HDMI-A-1:
//
//   QScreen::orientation()          -> Portrait   (correct-looking)
//   QScreen::nativeOrientation()    -> Primary
//   QScreen::angleBetween(n, o)     -> 0 degrees  (WRONG: claims no rotation)
//   QScreen::mapBetween(n, o, r)    -> identity   (WRONG: claims no rotation)
//   QScreen::transformBetween(n,o,r)-> identity   (WRONG: claims no rotation)
//   QPlatformScreen::orientation()  -> 1
//   QPlatformScreen::nativeOrientation() -> 0
//   QPlatformScreen::geometry()     -> 1080x1920  (already rotated, right)
//
// The first two look right only because Qt derives "Portrait" from the
// shape of geometry() itself, not from the compositor's transform.
// angleBetween/mapBetween/transformBetween are driven by
// QGuiApplication::setScreenOrientation(), which nothing calls in a
// desktop session, so they report the identity rotation for a screen
// that is genuinely rotated 90°.
//
// There is therefore NO trustworthy in-process rotation signal. What Qt
// DOES give us is the rotated geometry() itself, which is all the canvas
// needs (it draws in logical space and clamps in logical space). So no
// rotation field is added to Result: any number we put there would be
// either a duplicate of geometry()'s shape or a value we cannot compute.
// This comment is the record of that decision — there is deliberately
// no function here to call, because there is nothing to call it with.

Result makeResult(QSize physical, QSize logical, Source src)
{
    Result r;
    r.physical = physical;
    r.logical  = logical;
    r.source   = src;
    if (logical.width() > 0)
        r.dpr = qreal(physical.width()) / qreal(logical.width());
    else
        r.dpr = 1.0;
    return r;
}

} // namespace

QString sourceLabel(Source s)
{
    switch (s) {
    case Source::QScreen:         return QStringLiteral("QScreen");
    case Source::QPlatformScreen: return QStringLiteral("QPlatformScreen");
    case Source::XRandr:          return QStringLiteral("XRandr");
    case Source::KScreen:         return QStringLiteral("KScreen");
    case Source::WlrRandr:        return QStringLiteral("WlrRandr");
    case Source::DrmSysfs:        return QStringLiteral("DrmSysfs");
    case Source::Fallback:        return QStringLiteral("Fallback");
    }
    return {};
}

Result detect(const QScreen *screen)
{
    if (!screen) screen = QGuiApplication::primaryScreen();

    // `logical` is what Qt's widgets actually see — used to position
    // panels in the canvas. `QScreen::geometry()` is reliable on
    // Wayland but lies on some X11 fractional-DPI sessions (where Qt
    // remaps the geometry into logical pixels even though the
    // application didn't ask for it). We don't try to detect the
    // lie here; we just take Qt's value at face value and let the
    // header's physical-vs-logical comparison surface the
    // disagreement to the user.
    //
    // COORDINATE SPACE: this is the ROTATED logical size — exactly the
    // space the Result contract in screen_query.h describes. A
    // rotated 1920×1080 output yields 1080×1920 here and that is the
    // correct answer, not a bug to be corrected.
    QSize logical;
    if (screen) logical = screen->geometry().size();
    if (logical.width() < 800 || logical.height() < 600) {
        // Offscreen / undersized — Qt can't be trusted for logical
        // either. Drop it; the compositor tools below will supply
        // both numbers.
        logical = QSize();
    }

    const bool isWayland = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
    const bool isX11     = qEnvironmentVariableIsSet("DISPLAY");
    QSize shell;
    Source shellSource = Source::Fallback;

    auto takeShell = [&](Source s, const QSize &sz) {
        if (sz.isEmpty() || sz.width() < 640 || sz.height() < 480) return false;
        shell = sz; shellSource = s; return true;
    };

    // First successful probe wins: `||` keeps the original evaluation order
    // and short-circuits exactly like the if/else-if chain did; `shell` and
    // `shellSource` (set inside takeShell) carry the result.
    //
    // On a Wayland session xrandr is deliberately NOT probed. The comment
    // that used to live here ("X11 tools often also work via XWayland as a
    // last resort") was checked against the live session and is false in
    // the only way that matters:
    //
    //   * xrandr does run — XWayland answers it — and it prints
    //     "WARNING: running xrandr against an Xwayland server".
    //   * What it prints is the XWayland X screen's own mode list, which
    //     is the UNROTATED panel mode, not the real output's rotated
    //     geometry. On this machine it said 1920x1080 while Qt's wayland
    //     plugin said 1080x1920.
    //   * So the tool "works" as a subprocess and still describes a
    //     different screen than the one detect() is reporting.
    //
    // Kscreen-doctor and wlr-randr stay in the Wayland branch because
    // they speak the compositor's own protocol and report the same
    // output Qt sees. Their mode sizes are still cross-checked against
    // Qt's geometry below, so a rotated output is not turned into a fake
    // DPR even on a compositor that does provide one of these tools.
    if (isWayland) {
        static_cast<void>(takeShell(Source::KScreen, tryKscreen())
                          || takeShell(Source::WlrRandr, tryWlrRandr()));
    } else if (isX11) {
        static_cast<void>(takeShell(Source::XRandr, tryXrandr())
                          // KDE users on X11 still benefit from KScreen if installed.
                          || takeShell(Source::KScreen, tryKscreen())
                          || takeShell(Source::WlrRandr, tryWlrRandr()));
    } else {
        // No session env (tty / offscreen / CI). Try everything in
        // order of likelihood. xrandr is included here because with
        // neither WAYLAND_DISPLAY nor DISPLAY set there is no
        // compositor answer to contradict.
        static_cast<void>(takeShell(Source::XRandr, tryXrandr())
                          || takeShell(Source::KScreen, tryKscreen())
                          || takeShell(Source::WlrRandr, tryWlrRandr()));
    }

    // Last-resort: kernel sysfs. Works on any Linux machine with a
    // DRM-capable GPU, even when no compositor tool is installed and
    // Qt's `geometry()` is broken. /sys/class/drm/card*-eDP-*/modes
    // lists the active mode first, so we always know which one is
    // "the screen". Same coordinate-space caveat as the other shell
    // tools: it is a mode size, and it is global rather than
    // per-output, so the cross-check below has to approve it too.
    if (shell.isEmpty())
        takeShell(Source::DrmSysfs, tryDrmSysfs());

    if (!shell.isEmpty()) {
        // A shell tool gave us a MODE-sized candidate. Decide whether
        // it may be used as `physical` alongside Qt's rotated
        // `logical`:
        //
        //   1. sameShapeAsLogical() — the candidate must have the same
        //      aspect ratio as the screen it describes. This is what
        //      rejects a rotated mode (1920x1080 against a 1080x1920
        //      screen) and what rejects a global sysfs answer that
        //      happens to belong to a different, unrotated output.
        //   2. the implied DPR must be a DISCRETE legal value. The old
        //      test was a range (`dpr > 0.95 && dpr < 3.05`), which
        //      1.778 sails straight through: it is a perfectly
        //      plausible-looking number that is not a DPR at all.
        //      Membership in {1.0, 1.25, 1.5, 2.0, 2.5, 3.0} is the
        //      property the code has always claimed to want, so test
        //      that property instead of a superset of it.
        //
        // A candidate that fails either check is DISCARDED, not
        // "reinterpreted as a 1:1 screen". The mode size lives in the
        // unrotated panel frame; pairing it with a rotated logical (or
        // with itself, as the old code did) just relocates the
        // contradiction instead of removing it. Dropping it lets the
        // Qt-based answers below speak, and those are already in the
        // rotated frame — which is the whole coordinate-system
        // contract in screen_query.h.
        bool usable = true;
        if (!logical.isEmpty()) {
            const qreal dpr = qreal(shell.width()) / qreal(logical.width());
            usable = sameShapeAsLogical(shell, logical) && isLegalDpr(dpr);
        }
        if (usable)
            return makeResult(shell, logical.isEmpty() ? shell : logical,
                              shellSource);
        // Keep `shellSource` out of the picture but remember why it was
        // dropped, so the fall-through below is auditable.
        shell = QSize();
        shellSource = Source::Fallback;
    }

    // No shell tool worked. Fall back to Qt's answers.
    // 1. QPlatformScreen (private API) for the physical size.
    QSize nativePhys;
    if (screen) {
        if (auto *ps = screen->handle()) {
            const QRect ng = ps->geometry();
            if (ng.width() >= 1280 && ng.height() >= 720)
                nativePhys = ng.size();
        }
    }
    if (!nativePhys.isEmpty() && !logical.isEmpty()) {
        // QPlatformScreen::geometry() is in the SAME rotated logical
        // space as QScreen::geometry() (verified on this machine: both
        // reported 1080x1920 for the 90°-rotated HDMI-A-1), so the
        // product below is a genuine physical size and the quotient
        // is a genuine DPR — no rotation arithmetic required.
        const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
        const QSize phys(qRound(nativePhys.width()  * dpr),
                         qRound(nativePhys.height() * dpr));
        if (phys.width() >= 640 && phys.height() >= 480)
            return makeResult(phys, logical, Source::QPlatformScreen);
        return makeResult(nativePhys, logical, Source::QPlatformScreen);
    }

    // 2. QScreen × devicePixelRatio. Unreliable on X11 fractional
    //    DPI but still our only signal here.
    if (!logical.isEmpty()) {
        const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
        const QSize phys(int(logical.width()  * dpr),
                         int(logical.height() * dpr));
        if (phys.width() >= 640 && phys.height() >= 480)
            return makeResult(phys, logical, Source::QScreen);
    }

    // 3. Final fallback. Honour QT_SCALE_FACTOR for offscreen `--snap`
    //    so the user can simulate a HiDPI display in CI.
    const qreal scale = qEnvironmentVariable("QT_SCALE_FACTOR", "1.0").toDouble();
    const QSize phys(1920, 1080);
    return makeResult(QSize(int(phys.width()  * scale),
                           int(phys.height() * scale)),
                      QSize(int(phys.width()),
                            int(phys.height())),
                      Source::Fallback);
}

// v0.8: per-screen listing for the control console. Walks every QScreen
// Qt knows about and returns its (name, geometry, dpr, primary) tuple.
// Cheap (no I/O) and safe to call from the GUI thread on every dropdown
// refresh — QGuiApplication::screens() is in-process.
QList<OutputInfo> listOutputs()
{
    QList<OutputInfo> out;
    const auto screens = QGuiApplication::screens();
    QScreen *primary = QGuiApplication::primaryScreen();
    out.reserve(screens.size());
    for (QScreen *s : screens) {
        if (!s) continue;
        OutputInfo oi;
        oi.name     = s->name();
        oi.geometry = s->geometry();
        oi.dpr      = s->devicePixelRatio();
        oi.primary  = (s == primary);
        // Fallback name for the rare case Qt hasn't populated it yet
        // (e.g. session just started): keep the geometry so the user
        // can still tell which screen this is.
        if (oi.name.isEmpty())
            oi.name = QStringLiteral("screen@%1,%2")
                          .arg(oi.geometry.x()).arg(oi.geometry.y());
        out.push_back(oi);
    }
    return out;
}

// The shared core: `names` is every live output in Qt's order and
// `primary` is the live primary's name (possibly empty, possibly not in
// `names` — both are checked, so an empty/garbage primary can never win
// over a real screen). Pure function, no I/O, no subprocess — which is
// the point: this runs per mouse-move while the user drags a window.
static QString effectiveFrom(const QString &requested,
                             const QStringList &names,
                             const QString &primary)
{
    // No outputs at all: empty out, deterministically. A guessed name
    // here would mount a layer-shell surface on an output the
    // compositor may not have, which is worse than mounting nothing.
    if (names.isEmpty())
        return QString();

    // 1. Online and named: return it VERBATIM. The stored intent is the
    //    user's data; identity must never be normalised on read.
    if (!requested.isEmpty() && names.contains(requested))
        return requested;

    // 2. Offline or empty -> Qt's primary. Only if Qt really calls that
    //    screen by a name we can see, so the answer is always a name
    //    that exists in `names`.
    if (!primary.isEmpty() && names.contains(primary))
        return primary;

    // 2b. Qt has no usable primary (offscreen, mid-hotplug): the first
    //     live screen that has a name, still a real Qt name.
    for (const QString &n : names)
        if (!n.isEmpty())
            return n;

    // 2c. Screens exist but every one of them is nameless (offscreen's
    //     default screen). No synthetic `screen@x,y` label: that string
    //     is a listOutputs() presentation fallback and no compositor
    //     matches it, so handing it to a window system would be a lie.
    //     Empty means "no usable identity", and callers fall back to
    //     their own default screen handling.
    return QString();
}

// Production overload: live Qt screens only. QGuiApplication::screens()
// is an in-process list — no QProcess, no detect(), no hyprctl — so the
// cost is a QList copy on the drag hot path.
QString effectiveOutputName(const QString &requested)
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    QStringList names;
    names.reserve(screens.size());
    for (const QScreen *s : screens)
        if (s)
            names.append(s->name());
    const QScreen *primary = QGuiApplication::primaryScreen();
    return effectiveFrom(requested, names,
                         primary ? primary->name() : QString());
}

// Test-override overload: same rules over an explicit list, with the
// `primary`-flagged entry standing in for QGuiApplication::primaryScreen().
QString effectiveOutputName(const QString &requested,
                            const QList<OutputInfo> &live)
{
    QStringList names;
    names.reserve(live.size());
    QString primary;
    for (const OutputInfo &o : live) {
        names.append(o.name);
        if (o.primary && primary.isEmpty())
            primary = o.name;
    }
    return effectiveFrom(requested, names, primary);
}

} // namespace screen_query
