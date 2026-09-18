// Standalone launcher for the Monster Overlay control console.
//
// Plain Qt window (NOT layer-shell), so it's safe to focus and use
// alongside the live overlay without stealing keyboard or mapping an
// extra Wayland overlay surface. Renders the three overlay panels
// off-screen (WA_DontShowOnScreen) using the same QPainter code the
// live overlay uses, with mock data seeded by setEditMode(true).
//
// No connection to a running monster-overlay process yet — pure preview.
//
// v0.9 i18n: this file owns the console's startup LOCALE RESOLUTION
// (--locale > conf locale= row > detected system locale, mirroring src/main.cpp so the
// overlay and its console always agree) and forwards the resolved value
// to every overlay it spawns (see ControlPanel::launchOverlay()).

#include "ui/control_panel.h"
#include "ui/screen_query.h"
#include "ui/ui_theme.h"
#include "core/locale_conf.h"
#include "core/string_table.h"

#include <QApplication>
#include <QDebug>
#include <QString>
#include <cstdio>

namespace {

// Accepts both `--flag value` and `--flag=value`; returns the value and
// advances `i` when the value was passed as a separate argument.
QString takeValue(int argc, char *argv[], int &i, const QString &flag)
{
    const QString a = QString::fromLocal8Bit(argv[i]);
    const QString prefix = flag + QLatin1Char('=');
    if (a.startsWith(prefix))
        return a.mid(prefix.size());
    if (a == flag && i + 1 < argc)
        return QString::fromLocal8Bit(argv[++i]);
    return {};
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationVersion(QStringLiteral(MONSTER_VERSION));
    // Same Kvantum blur opt-out as monster-overlay (see src/main.cpp): the
    // Kvantum style plugin auto-requests KWin blur-behind for translucent
    // top-levels via BlurHelper::update(). Fusion skips the plugin so the
    // console's translucent chrome stays crisp on Plasma 6.7.
    app.setStyle(QStringLiteral("Fusion"));
    app.setApplicationName(QStringLiteral("monster-control"));
    // Quit as soon as the last visible window is closed. Required because
    // the three panel previews are QMainWindows in the live overlay —
    // without this, closing the console's main window leaves them around
    // as visible ghosts. With a parent in ControlPanel's ctor, the child
    // panels are NOT in the top-level window list, so closing the
    // console's main window means "no visible windows" → quit.
    app.setQuitOnLastWindowClosed(true);

    // ---- one arg scan -----------------------------------------------------
    // --print-screen-info / --print-locale short-circuit below;
    // --light / --locale / --snap are applied once the scan is done.
    bool printScreenInfo = false;
    bool printLocale     = false;
    bool showVersion     = false;
    bool showHelp        = false;
    bool lightTheme      = false;
    QString snapPath;
    QString cliLocale;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QStringLiteral("--print-screen-info")) {
            printScreenInfo = true;
        } else if (a == QStringLiteral("--print-locale")) {
            printLocale = true;
        } else if (a == QStringLiteral("--light")) {
            lightTheme = true;
        } else if (a == QStringLiteral("--snap")) {
            snapPath = takeValue(argc, argv, i, QStringLiteral("--snap"));
        } else if (a == QStringLiteral("--locale")
                   || a.startsWith(QStringLiteral("--locale="))) {
            cliLocale = takeValue(argc, argv, i, QStringLiteral("--locale"));
        } else if (a == QStringLiteral("--version") || a == QStringLiteral("-v")) {
            showVersion = true;
        } else if (a == QStringLiteral("--help") || a == QStringLiteral("-h")) {
            showHelp = true;
        }
    }

    // --version / --help print and exit BEFORE anything is constructed: an
    // unhandled flag used to fall through to the GUI, which then sat there
    // forever (it hung a release verification, because --version was simply
    // not parsed).
    if (showVersion) {
        std::printf("monster-control %s\n", MONSTER_VERSION);
        return 0;
    }
    if (showHelp) {
        std::printf(
            "Monster Overlay control console\n"
            "\n"
            "Usage: monster-control [options]\n"
            "\n"
            "  --locale <code>       UI locale for this run (zh-CN, en-US)\n"
            "  --light               light console theme\n"
            "  --snap <file>         render the console to a PNG and exit\n"
            "  --print-locale        print the resolved locale and exit\n"
            "  --print-screen-info   print the detected outputs and exit\n"
            "  --version, -v         print the version and exit\n"
            "  --help, -h            print this help and exit\n");
        return 0;
    }

    // ---- locale: --locale > conf locale= > system locale ------------------
    // The console is the sole writer of the conf file, but it also READS it:
    // the locale row is how a previous session's EN/CH choice survives a
    // restart. Resolution order matches the overlay's so the two never
    // disagree at launch.
    const QString confLocale = mhw::readLocaleFromConf();
    QString locale = mhw::resolveStartupLocale(cliLocale, confLocale, mhw::systemLocale());
    const char *localeSource = !cliLocale.isEmpty()    ? "cli"
                               : !confLocale.isEmpty() ? "conf"
                                                       : "system";
    if (!mhw::StringTable::instance().load(locale)) {
        qWarning("failed to load %s strings; falling back to zh-CN",
                 qPrintable(locale));
        locale = QStringLiteral("zh-CN");
        localeSource = "fallback (locale dir missing)";
        if (!mhw::StringTable::instance().load(locale))
            qWarning("failed to load zh-CN strings; falling back to keys");
    }

    if (printLocale) {
        // Evidence hook: asserts the resolution order without opening a GUI.
        printf("locale=%s source=%s\n", qPrintable(locale), localeSource);
        fflush(stdout);
        return 0;
    }

    if (printScreenInfo) {
        const screen_query::Result r = screen_query::detect();
        // qInfo is suppressed by Qt 6's default log filter, so
        // print to stdout directly. The agent greps for "physical="
        // and "source=" to confirm the dispatch landed where
        // expected.
        printf("physical=%dx%d logical=%dx%d dpr=%.3f source=%s\n",
               r.physical.width(), r.physical.height(),
               r.logical.width(),  r.logical.height(),
               r.dpr,
               qPrintable(screen_query::sourceLabel(r.source)));
        fflush(stdout);
        return 0;
    }

    // Optional: start in light theme (default is dark "light-grey deep").
    // Useful for testing and for users who prefer the 浅灰 palette.
    if (lightTheme)
        setUiTheme(false);

    ControlPanel cp;
    cp.show();
    app.processEvents();

    // Self-test mode: --snap <path> renders the whole window to a PNG and
    // exits. Lets the agent verify the layout without a real compositor
    // (run with QT_QPA_PLATFORM=offscreen) and without stealing the user's
    // desktop focus. Prints the locale it rendered with so the i18n
    // screenshots are self-labelling evidence.
    if (!snapPath.isEmpty()) {
        printf("snap locale=%s source=%s\n", qPrintable(locale), localeSource);
        fflush(stdout);
        const QPixmap grab = cp.grab();
        if (!grab.save(snapPath)) {
            qCritical("snap save failed: %s", snapPath.toLocal8Bit().constData());
            return 3;
        }
        qInfo("snapped %s (%dx%d)", snapPath.toLocal8Bit().constData(),
              grab.width(), grab.height());
        return 0;
    }
    return app.exec();
}
