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
#include "ui/layout_package.h"
#include "ui/rise_reframework_bridge.h"
#include "ui/screen_query.h"
#include "ui/ui_theme.h"
#include "core/locale_conf.h"
#include "core/string_table.h"

#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
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
    // systemd/journald may otherwise consume Qt diagnostics before shell and
    // CI stderr redirection sees them. Respect an explicit user override.
    if (!qEnvironmentVariableIsSet("QT_FORCE_STDERR_LOGGING"))
        qputenv("QT_FORCE_STDERR_LOGGING", "1");

    QApplication app(argc, argv);
    // Default QSettings must have stable identities before the first instance
    // is created. Older releases omitted the organization and therefore wrote
    // under "Unknown Organization".
    app.setOrganizationName(QStringLiteral("monster-overlay"));
    app.setApplicationName(QStringLiteral("monster-control"));
    {
        QSettings current;
        const QString legacy = QStandardPaths::writableLocation(
                                   QStandardPaths::GenericConfigLocation)
                               + QStringLiteral(
                                   "/Unknown Organization/monster-control.conf");
        if (!mhw::migrateLegacySettingsFile(legacy, current.fileName())) {
            qWarning("monster-control: failed to migrate legacy settings from %s to %s",
                     qPrintable(legacy), qPrintable(current.fileName()));
        }
    }

    QApplication::setApplicationVersion(QStringLiteral(MONSTER_VERSION));
    // Same Kvantum blur opt-out as monster-overlay (see src/main.cpp): the
    // Kvantum style plugin auto-requests KWin blur-behind for translucent
    // top-levels via BlurHelper::update(). Fusion skips the plugin so the
    // console's translucent chrome stays crisp on Plasma 6.7.
    app.setStyle(QStringLiteral("Fusion"));
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
    // v1.1.0: `--snap <panel>,<sub>` selects a window BEFORE the grab.
    //
    // The detail-preview controls (item 1) and the capability rows (item 8)
    // live in the inspector, which renders its EMPTY state on a cold start —
    // so a plain --snap screenshot cannot show either of them, and their
    // acceptance had been stuck on "they exist in code and in offscreen
    // tests" with no visual proof. A sub-index of -1 selects the mother panel,
    // which is the case that must show no Columns row.
    //
    // This drives the SAME public entry the rail and the canvas use
    // (selectWindow), so the screenshot is the real user path rather than a
    // state assembled for the camera.
    int  snapPanel = -2;     // -2 = "not given", distinct from a valid 0
    int  snapSub   = -1;
    // `--snap-scroll=end` scrolls the inspector's content box before the grab.
    // The inspector fills from y≈1240 in a 1268x1386 window, so its capability
    // rows (item 8) and the detail-preview controls (item 1) sit BELOW the
    // visible area and a plain grab photographs the top of the form only.
    // Scrolling is what lets the same screenshot show the rows being accepted.
    QString snapScroll;
    // `--snap-dialog=<help|settings|export>` photographs a MODAL instead of
    // the console window. The three dialogs are private and open with
    // exec(), so the panel owns the opening (snapOpenDialog) and the CLI
    // side owns the grab, from inside that dialog's nested event loop.
    QString snapDialog;
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
        } else if (a.startsWith(QStringLiteral("--snap-select="))) {
            const QStringList parts =
                a.mid(int(sizeof("--snap-select=")) - 1).split(QLatin1Char(','));
            bool okPanel = false, okSub = false;
            snapPanel = parts.value(0).toInt(&okPanel);
            snapSub   = parts.value(1).toInt(&okSub);
            if (!okPanel || parts.value(1).isEmpty() || !okSub) {
                qCritical("--snap-select wants <panel>,<sub> (e.g. 0,-1 for the "
                          "player mother panel, 0,3 for player-bars)");
                return 2;
            }
        } else if (a.startsWith(QStringLiteral("--snap-scroll="))) {
            snapScroll = a.mid(int(sizeof("--snap-scroll=")) - 1);
            if (snapScroll != QStringLiteral("end")
                && snapScroll != QStringLiteral("start")) {
                qCritical("--snap-scroll wants 'start' or 'end'");
                return 2;
            }
        } else if (a.startsWith(QStringLiteral("--snap-dialog="))) {
            snapDialog = a.mid(int(sizeof("--snap-dialog=")) - 1);
            if (snapDialog != QStringLiteral("help")
                && snapDialog != QStringLiteral("settings")
                && snapDialog != QStringLiteral("export")) {
                qCritical("--snap-dialog wants 'help', 'settings' or 'export'");
                return 2;
            }
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
            "  --snap-select=<panel>,<sub>\n"
            "                        with --snap: select a window first (sub -1 = the\n"
            "                        mother panel) so the inspector is captured filled\n"
            "  --snap-scroll=<start|end>\n"
            "                        with --snap: scroll the inspector's own content box\n"
            "                        before the grab\n"
            "  --snap-dialog=<help|settings|export>\n"
            "                        with --snap: photograph that modal instead of the\n"
            "                        console window\n"
            "  --print-locale        print the resolved locale and exit\n"
            "  --print-screen-info   print the detected outputs and exit\n"
            "  --version, -v         print the version and exit\n"
            "  --help, -h            print this help and exit\n"
            "Console preview: Ctrl+wheel zooms 50-400%%; the -/+ buttons step and "
            "1:1 resets (buttons, not keys).\n"
            "HUD detail previews open fitted to their pane and carry their own\n"
            "Fit / -/+ / actual-size controls; the source stays at native pixels.\n"
            "Offline output assignments fall back to the live primary, not the viewer.\n");
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
        qWarning("failed to load %s strings; falling back to en-US",
                 qPrintable(locale));
        locale = QStringLiteral("en-US");
        localeSource = "fallback (locale dir missing)";
        if (!mhw::StringTable::instance().load(locale))
            qWarning("failed to load en-US strings; falling back to keys");
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

    // v1.1.5: FIRST RUN SEEDS THE SHIPPED DEFAULT. A brand-new user would
    // otherwise get the code default — Single mode, four mother panels at
    // the corner margins — which on a real screen means every window
    // stacked in the top-left corner. It runs HERE, on the console's start
    // path and not inside the settings reader, for two reasons: the overlay
    // must not become a writer of the configuration, and an ambient hook
    // would fire inside the test suite as well, handing every test a
    // layout it never asked for. Guarded on the store not existing, so an
    // existing configuration is never touched.
    {
        QString seedError;
        const bool seeded = mhw::seedShippedDefaultIfFresh(&seedError);
        if (seeded)
            qInfo("monster-control: first run — seeded the shipped default "
                  "layout into %s",
                  qPrintable(mhw::layoutPackageStorePath()));
        else if (!seedError.isEmpty())
            // NOT fatal: an unwritable config directory or a build without
            // the default leaves the code defaults in place, which is
            // survivable. Saying so beats a first run that silently looks
            // like the seed worked.
            qWarning("monster-control: could not seed the default layout "
                     "(%s) — code defaults apply",
                     qPrintable(seedError));
    }

    ControlPanel cp;
    // REFramework install/removal: the console only emits request signals;
    // the bridge runs archive preparation (bundled local copy first, pinned
    // download as fallback), installation and removal on its worker thread,
    // then reports back through finishRiseReframeworkOperation().
    RiseReFrameworkBridge reframeworkBridge(QCoreApplication::applicationDirPath());
    QObject::connect(&cp, &ControlPanel::installRiseReframeworkRequested,
                     &reframeworkBridge, &RiseReFrameworkBridge::install);
    QObject::connect(&cp, &ControlPanel::removeRiseLuaRequested,
                     &reframeworkBridge, &RiseReFrameworkBridge::removeLua);
    QObject::connect(&cp, &ControlPanel::removeRiseReframeworkRequested,
                     &reframeworkBridge, &RiseReFrameworkBridge::removeReframework);
    QObject::connect(&reframeworkBridge, &RiseReFrameworkBridge::finished,
                     &cp, &ControlPanel::finishRiseReframeworkOperation);
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
        // ── --snap-dialog=<help|settings|export> ──────────────────────────
        // The modal dialogs open with exec(), so a grab cannot run beside
        // them: it has to run INSIDE their nested event loop. Two timers do
        // that — the first one blocks in exec(), the second one is already
        // queued behind it and therefore runs while the modal is up.
        bool dialogGrabbed = false;
        if (!snapDialog.isEmpty()) {
            QTimer::singleShot(0, &app, [&cp, &snapDialog]{
                cp.snapOpenDialog(snapDialog);
            });
            QTimer::singleShot(500, &app, [&]{
                QWidget *dlg = app.activeModalWidget();
                if (!dlg) {
                    qCritical("snap-dialog: no modal window appeared");
                    return;
                }
                const QPixmap grab = dlg->grab();
                if (grab.save(snapPath)) {
                    qInfo("snapped dialog %s (%dx%d)",
                          snapPath.toLocal8Bit().constData(),
                          grab.width(), grab.height());
                    dialogGrabbed = true;
                } else {
                    qCritical("snap save failed: %s",
                              snapPath.toLocal8Bit().constData());
                }
                dlg->close();
            });
            QElapsedTimer modal;
            modal.start();
            while (modal.elapsed() < 4000 && !dialogGrabbed)
                app.processEvents(QEventLoop::AllEvents, 10);
            if (dialogGrabbed)
                return 0;
        }
        // Drive the selection through the same public entry the rail uses, then
        // let the whole layout settle before the grab: the capability rows and
        // the Mixed toggle block are built during the fill, so grabbing in the
        // same event-loop turn would photograph the state BEFORE the selection.
        if (snapPanel != -2) {
            cp.selectWindow(snapPanel, snapSub);
            QElapsedTimer settle;
            settle.start();
            while (settle.elapsed() < 600)
                app.processEvents(QEventLoop::AllEvents, 10);
        }
        if (!snapScroll.isEmpty()) {
            // The inspector's content box, by the name its builder gives it.
            // Absent when the console has not built the face yet, which is a
            // real state (--snap does not need a selection to be useful).
            if (auto *area = cp.findChild<QScrollArea *>(
                    QStringLiteral("inspectorContent"))) {
                QScrollBar *bar = area->verticalScrollBar();
                bar->setValue(snapScroll == QStringLiteral("end")
                                  ? bar->maximum()
                                  : bar->minimum());
                QElapsedTimer settle;
                settle.start();
                while (settle.elapsed() < 250)
                    app.processEvents(QEventLoop::AllEvents, 10);
            }
        }
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
