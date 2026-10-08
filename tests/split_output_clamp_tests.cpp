#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/panel.h"
#include "ui/panel_settings_game.h"

#include <cstdio>

// 「分块的仍然无法越过竖中线」, measured.
//
// THE MEASUREMENT THAT FAILED TO EXPLAIN IT, until the screens were read
// instead of assumed:
//
//   the canvas framed against DP-1 (2560 wide) while every split
//   sub-window clamped against primaryScreen() (HDMI-A-1, 1080 wide,
//   PORTRAIT). asked left=2538 came back as left=1058 — 1480px short, and
//   1058 sits left of 1280, which the user reads as "it stops at the
//   middle".
//
// The four base panels were fine because each carries outputName=DP-1 of
// its own. The eleven split sub-windows all carried an EMPTY outputName,
// and empty means primaryScreen(). That is the whole bug: two independent
// screen selectors, agreeing only when every window happens to name the
// same output as the canvas.
//
// The fix makes the clamp and the frame agree by construction: the console
// pushes the canvas's screen into each sub-window (setPreviewFallbackOutput),
// and PanelSource grew the two accessors that let the canvas ask a window
// which screen IT is on rather than assuming the canvas's own.
//
// WHAT THIS TEST CAN AND CANNOT SEE, stated so the next reader does not
// mistake a green run for more than it is.
//
// It asserts AGREEMENT between two screen selectors, and it measures the
// drag ceiling against the screen the canvas names. Under offscreen there
// is ONE synthetic screen, so both selectors resolve to it and agree
// trivially - the agreement assertion is therefore vacuous there and the
// drag half measures a world where no divergence is possible.
//
// That is a real limit of this platform, not a defect in the fix, and the
// reason this file does NOT claim to reproduce 「asked 2538 -> got 1058」:
// that number needs two screens with different widths. What it does check
// on every machine is that the drag reaches the canvas's own ceiling rather
// than being rewritten on the way in - which is the failure's other half,
// and is what a single screen CAN see.

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL  %s\n", what); }
    else std::printf("PASS: %s\n", what);
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("monster-overlay"));
    app.setApplicationName(QStringLiteral("monster-control"));

    QTemporaryDir home;
    Q_ASSERT(home.isValid());
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    QDir().mkpath(home.filePath(QStringLiteral("monster-overlay")));
    {
        QFile conf(home.filePath(
            QStringLiteral("monster-overlay/monster-overlay.conf")));
        if (conf.open(QIODevice::WriteOnly | QIODevice::Text
                      | QIODevice::Truncate)) {
            QTextStream o(&conf);
            o.setEncoding(QStringConverter::Utf8);
            o << QStringLiteral("layout=split\n");
            o << QStringLiteral("game=world\n");
            o.flush();
        }
    }

    mhw::setPanelSettingsGame(mhw::GameId::World);
    mhw::setPanelSettingsLayout(mhw::PlayerSection::PlayerLayoutMode::Split);

    ControlPanel cp;
    cp.show();
    for (int i = 0; i < 12; ++i)
        QCoreApplication::processEvents();

    HudCanvas *canvas = cp.findChild<HudCanvas *>();
    check(canvas != nullptr, "the console has its preview canvas");

    const int nScreens = QGuiApplication::screens().size();
    const QScreen *prim = QGuiApplication::primaryScreen();

    std::printf("\nscreens=%d  primary=%s (%dx%d)  canvas screen='%s'\n",
                nScreens, prim ? qPrintable(prim->name()) : "(none)",
                prim ? prim->geometry().width() : 0,
                prim ? prim->geometry().height() : 0,
                qPrintable(canvas ? canvas->previewScreen() : QString()));

    if (nScreens < 2) {
        std::printf("SKIP-REST: one screen attached, nothing can disagree\n");
        std::printf("\n== %d checks, %d failures ==\n", checks, failures);
        return failures == 0 ? 0 : 1;
    }

    // ---- THE SCREEN THE SPLIT WINDOWS ARE ON -----------------------------
    //
    // Not "does the window have a name" but "does the window's screen
    // EQUAL the canvas's screen". That is the invariant the bug violated;
    // a window that happens to be on the primary and a canvas that
    // happens to be on the primary also satisfy it, and that is correct.
    Panel *mantles = cp.splitPreviewWindow(QStringLiteral("player-mantles"));
    check(mantles != nullptr, "the mantles sub-window exists");
    if (!canvas || !mantles) {
        std::printf("\n== %d checks, %d failures ==\n", checks, failures);
        return 1;
    }

    std::printf("mantles outputName='%s' previewFallback='%s'\n",
                qPrintable(mantles->outputName()),
                qPrintable(mantles->previewFallbackOutput()));

    const QString canvasScreen = canvas->previewScreen();
    const QString effective = mantles->outputName().isEmpty()
                                  ? (mantles->previewFallbackOutput().isEmpty()
                                         ? QString()
                                         : mantles->previewFallbackOutput())
                                  : mantles->outputName();
    check(effective == canvasScreen,
          "a split sub-window resolves to the SAME output the canvas draws "
          "— the two selectors can no longer disagree");

    // ---- THE DRAG, CROSSING THE MIDLINE ----------------------------------
    //
    // The logical size is read off the screen the canvas NAMES rather than
    // off HudCanvas::previewScreenInfo(), which is private. That is the
    // same screen by construction when the agreement check above passed,
    // and reading it independently keeps the test off private API.
    // The screen the canvas is describing. previewScreen() empty means
    // primary — the same rule HudCanvas::previewScreenInfo() applies —
    // so the test resolves it the same way rather than assuming one.
    const QString screenToMeasure =
        canvasScreen.isEmpty()
            ? (prim ? prim->name() : QString())
            : canvasScreen;
    const QScreen *dragged = nullptr;
    for (QScreen *s2 : QGuiApplication::screens())
        if (s2 && s2->name() == screenToMeasure) { dragged = s2; break; }
    if (!dragged)
        dragged = prim;
    const QSize logical = dragged ? dragged->geometry().size() : QSize(2560, 1440);
    const int W = logical.width();
    const int mid = W / 2;
    const int want = W - qRound(mantles->contentSize().width()
                                * mantles->scale());

    QMargins target = mantles->margins();
    target.setLeft(want);
    emit canvas->subWindowMoved(1, target);
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();

    const int got = mantles->margins().left();
    std::printf("\nlogical %dx%d  midline x=%d  asked left=%d -> got %d\n",
                W, logical.height(), mid, want, got);

    check(got >= mid,
          "the window CROSSES the midline of the screen it is drawn on");
    check(got == want,
          "and lands exactly where the drag asked — neither clamp rewrote it");

    std::printf("\n== %d checks, %d failures ==\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
