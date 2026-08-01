#include "screen_query.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
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

// Forward decls so the dispatch table in detect() (which lives in
// the parent screen_query namespace) can call helpers defined in
// the anonymous namespace below. C++17 lets us write them in the
// enclosing namespace explicitly so the names resolve cleanly.
namespace screen_query {
namespace { QSize tryXrandr(); QSize tryKscreen(); QSize tryWlrRandr(); QSize tryDrmSysfs(); }
}

QSize tryXrandr()      { return runCommand("xrandr", {"--current"}, parseXrandrOutput); }
QSize tryKscreen()     { return runCommand("kscreen-doctor", {"-j"}, parseKscreenJson); }
QSize tryWlrRandr()    { return runCommand("wlr-randr", {}, parseWlrRandr); }
QSize tryDrmSysfs()    { return tryDrmSysfsImpl(); }

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
    QSize logical;
    if (screen) logical = screen->geometry().size();
    if (logical.width() < 800 || logical.height() < 600) {
        // Offscreen / undersized — Qt can't be trusted for logical
        // either. Drop it; the compositor tools below will supply
        // both numbers.
        logical = QSize();
    }

    // Pick the platform-appropriate compositor tool first, because
    // these report the *physical* pixel mode the user actually sees
    // in the OS display settings, and they don't lie about DPI.
    // The order follows session type so we don't waste 800ms on
    // tools that won't be there.
    const bool isWayland = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
    const bool isX11     = qEnvironmentVariableIsSet("DISPLAY");
    QSize shell;
    Source shellSource = Source::Fallback;

    auto takeShell = [&](Source s, const QSize &sz) {
        if (sz.isEmpty() || sz.width() < 640 || sz.height() < 480) return false;
        shell = sz; shellSource = s; return true;
    };

    if (isWayland) {
        if (takeShell(Source::KScreen,   tryKscreen()))  ;
        else if (takeShell(Source::WlrRandr, tryWlrRandr())) ;
        // X11 tools often also work via XWayland as a last resort.
        else if (takeShell(Source::XRandr, tryXrandr()))   ;
    } else if (isX11) {
        if (takeShell(Source::XRandr,    tryXrandr()))   ;
        // KDE users on X11 still benefit from KScreen if installed.
        else if (takeShell(Source::KScreen, tryKscreen()))  ;
        else if (takeShell(Source::WlrRandr, tryWlrRandr())) ;
    } else {
        // No session env (tty / offscreen / CI). Try everything in
        // order of likelihood.
        if      (takeShell(Source::XRandr,    tryXrandr()))   ;
        else if (takeShell(Source::KScreen,   tryKscreen()))  ;
        else if (takeShell(Source::WlrRandr,  tryWlrRandr())) ;
    }

    // Last-resort: kernel sysfs. Works on any Linux machine with a
    // DRM-capable GPU, even when no compositor tool is installed and
    // Qt's `geometry()` is broken. /sys/class/drm/card*-eDP-*/modes
    // lists the active mode first, so we always know which one is
    // "the screen".
    if (shell.isEmpty())
        takeShell(Source::DrmSysfs, tryDrmSysfs());

    if (!shell.isEmpty()) {
        // Shell tool gave us the truth about physical. Decide
        // logical:
        //   - if Qt's logical is a clean fraction of the physical
        //     (DPR is a clean 1.0/1.25/1.5/2.0/2.5/3.0), use Qt's
        //   - otherwise treat the shell result as a 1:1 screen
        //     (no fractional scaling)
        QSize chosenLogical = logical;
        if (!logical.isEmpty()) {
            const qreal dpr = qreal(shell.width()) / qreal(logical.width());
            const bool clean = dpr > 0.95 && dpr < 3.05;
            if (!clean) chosenLogical = QSize();
        }
        if (chosenLogical.isEmpty()) chosenLogical = shell;
        return makeResult(shell, chosenLogical, shellSource);
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
    if (!nativePhys.isEmpty() && !logical.isEmpty())
        return makeResult(nativePhys, logical, Source::QPlatformScreen);

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

} // namespace screen_query
