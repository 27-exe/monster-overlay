#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>

#include "ui/console_fidelity_windows.h"
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
// THE FIX, CORRECTED BY THE OWNER (OUTPUT-OWNERSHIP-DECISION.md): the
// console pushes each sub-window's OWN OWNERSHIP CHAIN — its parent
// panel's output, or the primary when that parent follows — into
// setPreviewFallbackOutput(), and PanelSource grew the two accessors that
// let the canvas ask a window which screen IT is on rather than assuming
// the canvas's own. The viewer (the canvas's preview screen, which is
// what the SCREEN dropdown moves) is PRESENTATION ONLY and is not part of
// the chain.
//
// THIS FILE'S PREVIOUS ASSERTION WAS THE OPPOSITE RULE, and it is
// corrected rather than kept: `effective == canvasScreen` made "which
// screen am I looking at" the definition of "which screen does this
// window live on", which is exactly what let a viewer switch move a
// Follow window's drag ceiling from 851 to 2331 and flush ml=2331 into
// panels.ini — 1480 px past the end of the window's own display.
//
// WHAT THIS TEST CAN AND CANNOT SEE, stated so the next reader does not
// mistake a green run for more than it is.
//
// It asserts the CHAIN (own → parent → primary), that the chain does not
// move when the viewer does, and that the drag ceiling is the window's own
// screen's. Those need two screens with different widths, so this file
// now installs them itself through Qt's QPA seam (QWindowSystemInterface +
// QPlatformScreen) — the same DP-1 / portrait HDMI-A-1 pair the reporter
// had, and the same fixture tests/main_output_wiring_tests.cpp uses — and
// drives the real SCREEN dropdown to change the viewer in between.
//
// Without two screens the file still says so and skips rather than
// asserting something vacuous; with them it reproduces the numbers above.
// The full view/board/chip matrix lives in
// tests/console_output_ownership_tests.cpp.

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL  %s\n", what); }
    else std::printf("PASS: %s\n", what);
}

void check(bool ok, const char *what, const QString &detail)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  FAIL  %s [%s]\n", what, qPrintable(detail));
    } else {
        std::printf("PASS: %s [%s]\n", what, qPrintable(detail));
    }
}

void pump()
{
    for (int i = 0; i < 40; ++i)
        QCoreApplication::processEvents();
}

// ── the reporter's topology, installed through Qt's own QPA seam ───────
//
// The non-primary goes in first so that asking for the primary second
// leaves the offscreen platform's own nameless screen off the primary
// slot — same ordering as tests/main_output_wiring_tests.cpp:197.
const char *kPrimaryName = "HDMI-A-1";   // 1080x1920, primary, PORTRAIT
const char *kOtherName   = "DP-1";       // 2560x1440, NOT primary

class FakeOutput : public QPlatformScreen
{
public:
    FakeOutput(const QString &name, const QRect &g) : m_name(name), m_g(g) {}
    QString name() const override { return m_name; }
    QRect   geometry() const override { return m_g; }
    int     depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }

private:
    QString m_name;
    QRect   m_g;
};

void installFakeOutputs()
{
    QWindowSystemInterface::handleScreenAdded(
        new FakeOutput(QString::fromLatin1(kOtherName), QRect(0, 0, 2560, 1440)),
        /*isPrimary*/ false);
    QWindowSystemInterface::handleScreenAdded(
        new FakeOutput(QString::fromLatin1(kPrimaryName), QRect(0, 0, 1080, 1920)),
        /*isPrimary*/ true);
    pump();
}

// Drive the real SCREEN dropdown. Returns the viewer the canvas reports
// afterwards, so a caller can assert a switch actually happened rather
// than assuming a signal fired.
QString pickViewer(ControlPanel &cp, const QString &output)
{
    QComboBox *c = cp.findChild<QComboBox *>(QStringLiteral("screenCombo"));
    if (!c)
        return QString();
    const int at = c->findData(output);
    if (at >= 0 && at != c->currentIndex())
        c->setCurrentIndex(at);
    pump();
    HudCanvas *cv = cp.findChild<HudCanvas *>();
    return cv ? cv->previewScreen() : QString();
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

    // AN EXPLICIT PARENT OUTPUT, so the chain's step 2 is a REAL answer
    // and not the primary's. Without it every split window would resolve
    // to the primary, the viewer would be on the same screen by accident,
    // and the drag assertion below could not tell the two selectors apart
    // — which is precisely the vacuous-green this file used to risk.
    {
        const QString ini = home.filePath(
            QStringLiteral("monster-overlay/panels.ini"));
        QSettings s(ini, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("world/split/player"));
        s.setValue(QStringLiteral("outputName"), QStringLiteral("DP-1"));
        s.endGroup();
        s.sync();
    }

    mhw::setPanelSettingsGame(mhw::GameId::World);
    mhw::setPanelSettingsLayout(mhw::PlayerSection::PlayerLayoutMode::Split);

    // BEFORE the first Panel is built: Panel::setOutputName() validates
    // against the live QScreen list and buildInspector()'s SCREEN dropdown
    // is populated from it, so a console built without these two would
    // reject every output name and every assertion below would pass
    // vacuously on offscreen's single nameless screen.
    installFakeOutputs();

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
    // The contract is reference.html:421's chain — own output → parent
    // panel's output → primary — and it is NOT "equal to the canvas's
    // screen". The viewer is presentation only, so the two selectors are
    // allowed to name DIFFERENT screens; asserting they agreed was the
    // opposite rule and is what let a viewer switch move a Follow window's
    // ceiling (see the header and OUTPUT-OWNERSHIP-DECISION.md).
    Panel *mantles = cp.splitPreviewWindow(QStringLiteral("player-mantles"));
    check(mantles != nullptr, "the mantles sub-window exists");
    if (!canvas || !mantles) {
        std::printf("\n== %d checks, %d failures ==\n", checks, failures);
        return 1;
    }

    const QString canvasScreen = canvas->previewScreen();
    // Step 1 is the window's own outputName; otherwise the fallback the
    // console pushed — its parent panel's output, or the primary's NAME
    // when that parent follows (never empty while a screen is attached).
    const auto effectiveOf = [mantles]() {
        return mantles->outputName().isEmpty()
                   ? mantles->previewFallbackOutput()
                   : mantles->outputName();
    };
    const QString effective = effectiveOf();

    std::printf("mantles outputName='%s' previewFallback='%s' canvas='%s'\n",
                qPrintable(mantles->outputName()),
                qPrintable(mantles->previewFallbackOutput()),
                qPrintable(canvasScreen));

    check(!effective.isEmpty(),
          "a split sub-window resolves to a screen through own → parent → "
          "primary (the fallback the console pushed is never empty)");
    check(effective == QLatin1String(kOtherName),
          "…and it is the PARENT panel's output, not the primary — step 2 "
          "of the chain is really exercised",
          QStringLiteral("effective=%1").arg(effective));

    // THE VIEWER IS NOT PART OF IT. Move the real SCREEN dropdown to the
    // other output and back; neither the window's answer nor its identity
    // may change, which is reference.html:513 (the display selector only
    // ever writes state.display).
    const QString otherViewer =
        (canvasScreen == QLatin1String(kOtherName))
            ? QString::fromLatin1(kPrimaryName)
            : QString::fromLatin1(kOtherName);
    const QString movedTo = pickViewer(cp, otherViewer);
    check(!movedTo.isEmpty() && movedTo != canvasScreen,
          "the SCREEN dropdown really moved the viewer (the check below is "
          "not vacuous)",
          QStringLiteral("viewer '%1' -> '%2'").arg(canvasScreen, movedTo));
    const QString effectiveWhileElsewhere = effectiveOf();
    const QString movedBack = pickViewer(cp, canvasScreen);
    const QString effectiveBack = effectiveOf();

    check(effectiveWhileElsewhere == effective && effectiveBack == effective,
          "switching the VIEWER leaves the window's own screen untouched — "
          "the chain is own → parent → primary, with no viewer step",
          QStringLiteral("was=%1 elsewhere=%2 back=%3")
              .arg(effective, effectiveWhileElsewhere, effectiveBack));
    check(!movedBack.isEmpty() && movedBack == canvasScreen,
          "…and the dropdown returns to where it was",
          QStringLiteral("viewer=%1").arg(movedBack));

    // ---- THE DRAG, AGAINST THE WINDOW'S OWN SCREEN ------------------------
    //
    // The screen is the WINDOW's, read from the chain above. The canvas's
    // preview screen is deliberately not consulted: with the viewer parked
    // on the NARROWER output, the pre-fix code measured this window
    // against 1080px and answered 717 where its own screen allows 2197 —
    // the reported 「分块的仍然无法越过竖中线」, from the other direction.
    const QString viewerForTheDrag =
        (effective == QLatin1String(kOtherName))
            ? QString::fromLatin1(kPrimaryName)
            : QString::fromLatin1(kOtherName);
    const QString draggedViewer = pickViewer(cp, viewerForTheDrag);
    std::printf("drag with viewer='%s' window on '%s'\n",
                qPrintable(draggedViewer), qPrintable(effective));

    const QScreen *dragged = nullptr;
    for (QScreen *s2 : QGuiApplication::screens())
        if (s2 && s2->name() == effective) { dragged = s2; break; }
    if (!dragged)
        dragged = prim;
    const QSize logical = dragged ? dragged->geometry().size() : QSize(2560, 1440);
    const int W = logical.width();
    const int mid = W / 2;
    // The ceiling marginLimits() itself derives (panel.cpp:1457-1465):
    // the screen's width minus the panel's own extent.
    const int want = W - mantles->width();

    // THE INDEX THE CANVAS USES FOR THIS WINDOW. The old code moved index
    // 1 unconditionally and then measured `mantles` — correct only while
    // the split table happened to put mantles second, and a silently
    // meaningless comparison the day it did not.
    int mantlesIndex = -1;
    for (const mhw::WindowState &w : cp.buildWindowStates())
        if (w.key == QLatin1String("player-mantles"))
            mantlesIndex = w.subIndex;
    check(mantlesIndex >= 0,
          "the window model says which box is the mantles window",
          QStringLiteral("index=%1").arg(mantlesIndex));

    QMargins target = mantles->margins();
    target.setLeft(want);
    if (mantlesIndex >= 0)
        emit canvas->subWindowMoved(mantlesIndex, target);
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();

    const int got = mantles->margins().left();
    std::printf("\nlogical %dx%d  midline x=%d  asked left=%d -> got %d\n",
                W, logical.height(), mid, want, got);

    check(got >= mid,
          "the window CROSSES the midline of the screen IT is drawn on, "
          "while the viewer sits on a different display");
    check(got == want,
          "and lands exactly where the drag asked — neither clamp rewrote "
          "it against the screen being previewed",
          QStringLiteral("got=%1 want=%2").arg(got).arg(want));

    std::printf("\n== %d checks, %d failures ==\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
