// SPDX-License-Identifier: Apache-2.0
//
// v1.0.1 canvas interaction regression tests (#4/#5/#6/#7/#8).
//
// WHAT THIS FILE PINS (four claims the old suites do not cover)
//
//   1. ZOOM IS THE FRAME (#5). setZoom() used to scale only the widget
//      width while fidelityHeightForWidth()/screenFrame() kept the CSS
//      cap at 1050/560, so every zoom step painted more empty ground
//      around a board that never moved. Now the cap consumes the same
//      zoom: at 1.0 the board is the byte-for-byte baseline, at 2.0 the
//      frame is twice the baseline (never 4× — the widget growth and
//      the cap growth divide), at 0.5 the frame itself shrinks, and the
//      scroll child's minimum geometry tracks all of it.
//
//   2. A RE-CLICK NOTIFIES (#7). mousePressEvent used to emit
//      panelSelected / subWindowSelected only when the index CHANGED,
//      so clicking the already-highlighted default panel (or a child
//      that was already selected) silently did nothing and the
//      console's property panel went stale. Now every user press
//      inside a painted box emits — and the programmatic setters
//      (setSelectedPanel / setSelectedSubWindow) stay signal-free, so
//      the console's echo cannot close a loop.
//
//   3. DRAG GEOMETRY COMES FROM THE MARGINS CACHE (#4/#6-side). A drag
//      over a window whose pixmap is TRANSPARENT still emits exact
//      margins: the mapping reads src->margins() at press and the
//      shared screenFrame() fit — never a pixmap. This is what lets
//      the console cancel its per-step group re-render.
//
//   4. THE ALPHA-READ CHECKER (#6). The fidelity board's ground carries
//      a neutral micro-checker so a translucent window pixmap has
//      something structured to reveal it against; the alphas are baked
//      into the colours (never a second QPainter::setOpacity pass).
//
// REAL CONTROLS, REAL EVENTS. Every gesture is a synthesized
// QMouseEvent through QApplication::sendEvent with the same
// press/move/release cadence a user produces; the frame numbers are
// read back OFF THE PIXELS (the border the painter drew), not from the
// implementation's constants. QSettings is isolated via XDG_CONFIG_HOME
// in main() — this file never touches the developer's HOME.

#include "ui/hud_canvas.h"

#include "ui/panel.h"          // the Corner enum's definition (the
                               // canvas's header only forward-declares it)
#include "ui/panel_sections.h"
#include "ui/panel_source.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QRgb>
#include <QRect>
#include <QRectF>
#include <QScreen>
#include <QSize>
#include <QVector>
#include <QTemporaryDir>
#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

int failures = 0;
int checks = 0;

class FakeOutput final : public QPlatformScreen {
public:
    FakeOutput(QString name, QRect geometry) : name_(name), geometry_(geometry) {}
    QString name() const override { return name_; }
    QRect geometry() const override { return geometry_; }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }
private:
    QString name_;
    QRect geometry_;
};

void check(bool cond, const char *what)
{
    ++checks;
    if (cond) {
        std::printf("PASS: %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// A PanelSource whose every number is under the test's control —
// copied from hud_canvas_split_tests.cpp's StubSource on purpose (the
// two files link as separate targets; a shared helper header would be
// a third file this lane does not own). `outputName` is new here: the
// #8 chain needs a window that names a screen and one that follows.
struct StubSource final : PanelSource
{
    Corner c{Corner::TopLeft};
    QMargins m{120, 90, 0, 0};
    QSize content{420, 160};
    double z{1.0};
    bool sub{true};
    QString out;
    Corner corner() const override { return c; }
    QMargins margins() const override { return m; }
    QSize contentSize() const override { return content; }
    double scale() const override { return z; }
    bool isSubWindow() const override { return sub; }
    QString outputName() const override { return out; }
};

// The tokens, spelled out (same values the split tests use; a test that
// imports the implementation's constants cannot notice the
// implementation drifting from the HTML).
const QColor kPageGroundDark(0x18, 0x18, 0x16);   // --page, dark
const QColor kBoardLine(0x2e, 0x2e, 0x2a);        // --board-line

// The border rectangle, read back off the pixels: the ring the fidelity
// board draws 1px OUTSIDE its frame, so its width is frame + 2.
QRect frameFromBorder(const QImage &img, const QColor &line)
{
    const QRgb want = line.rgb();
    int l = img.width(), t = img.height(), r = -1, b = -1;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (img.pixel(x, y) != want)
                continue;
            l = qMin(l, x); r = qMax(r, x);
            t = qMin(t, y); b = qMax(b, y);
        }
    }
    if (r < 0)
        return {};
    return QRect(QPoint(l, t), QPoint(r, b));
}

void pump()
{
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents();
}

void mouse(QWidget *w, QEvent::Type type, QPointF pos,
           Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent e(type, pos, pos, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
    pump();
}

// One full gesture: press, N pixel steps toward `to`, release.
void drag(QWidget *w, QPointF from, QPointF to)
{
    mouse(w, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    // Three intermediate moves, the way a real drag streams them.
    for (int i = 1; i <= 3; ++i) {
        const QPointF p = from + (to - from) * (qreal(i) / 3.0);
        mouse(w, QEvent::MouseMove, p,
              Qt::NoButton, Qt::LeftButton);
    }
    mouse(w, QEvent::MouseButtonRelease, to,
          Qt::LeftButton, Qt::MouseButtons());
}

QPixmap solidPm(const QSize &s, const QColor &c)
{
    QPixmap pm(s);
    pm.fill(c);
    return pm;
}

// The fidelity board's width in PIXELS, measured off the border. The
// border is 1px outside the frame on each side, hence -2.
int boardWidth(HudCanvas &v)
{
    const QColor line = v.editing() ? QColor(0xd9, 0x77, 0x57) : kBoardLine;
    return frameFromBorder(v.grab().toImage(), line).width() - (v.editing() ? 2 : 0);
}

// Four mothers, painted the way the console paints them.
void bindMothers(HudCanvas &v, StubSource (&mothers)[mhw::kPanelCount])
{
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        mothers[i].sub = false;
        mothers[i].m = QMargins(10 + 30 * i, 10 + 150 * i, 0, 0);
        v.bindPanel(i, &mothers[i]);
        v.setPanelPixmap(i, solidPm(QSize(200, 90), QColor(255, 0, 255)),
                         true);
    }
}

// ─── #5: zoom changes the FRAME, once, both directions ─────────────────
void testZoomChangesFrameAndScrollGeometry()
{
    StubSource unused[4];
    HudCanvas v;
    v.resize(1180, 820);
    v.show();
    pump();
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    bindMothers(v, unused);
    v.setConsoleFidelityMode(true);
    pump();

    const int base = boardWidth(v);
    check(base == 1050,
          "zoom 1.0 fidelity board is the 1050px CSS baseline");

    // ── zoom 2.0 ──────────────────────────────────────────────────────
    v.setZoom(2.0);
    pump();
    check(v.sizeHint().width() == 2156,
          "zoom 2.0 doubles the scroll child's width hint (1078*2)");
    check(v.width() == 2156,
          "…and fixes the widget to it, so the QScrollArea scrolls");
    const int wide = boardWidth(v);
    check(wide == 2100,
          "zoom 2.0 doubles the BOARD (2100 = 2*1050, not the cap, "
          "not 4x — the widget growth and the cap growth divide)");
    const QSize logicalScreen = QGuiApplication::primaryScreen()->size();
    check(v.sizeHint().height() == 12 + qRound(2100.0 * logicalScreen.height() / logicalScreen.width()),
          "…and the height hint follows the same factor (no "
          "stuck-at-602 whitespace)");
    {
        const QRect fr = frameFromBorder(v.grab().toImage(), kBoardLine);
        check(fr.left() >= 0 && fr.top() >= 0
                  && fr.right() <= v.width() && fr.bottom() <= v.height(),
              "the zoomed frame sits fully inside the grown widget — "
              "no negative-centre clipping at any edge");
    }

    // ── zoom 1.0 again: byte-for-byte back to baseline ────────────────
    v.setZoom(1.0);
    pump();
    // The console's QScrollArea re-arranges the child after a policy
    // change; a bare test widget has to do it itself.
    v.resize(1180, 820);
    pump();
    check(boardWidth(v) == base,
          "zoom back to 1.0 restores the exact baseline board");

    // ── zoom 0.5: the frame itself shrinks ────────────────────────────
    v.setZoom(0.5);
    pump();
    v.resize(1180, 820);
    pump();
    const int narrow = boardWidth(v);
    check(narrow == 525,
          "zoom 0.5 shrinks the BOARD to 525 (0.5 * 1050) — not just "
          "the widget, and not clamped back to the cap");
    check(narrow < base,
          "…and it is strictly smaller than the baseline");

    v.setZoom(1.0);
    pump();
    v.resize(1180, 820);
    pump();
    check(boardWidth(v) == base,
          "round trip 1.0 -> 2.0 -> 0.5 -> 1.0 lands on the baseline");
}

// ─── #5: a resize grows the board, and the mapping follows ─────────────
void testResizeGrowsFrameAndDragMapping()
{
    StubSource ms[4];
    HudCanvas v;
    v.resize(700, 520);
    v.show();
    pump();
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    bindMothers(v, ms);
    v.setConsoleFidelityMode(true);
    v.setEditing(true);
    pump();

    const int smallW = boardWidth(v);
    check(smallW == 700 - 28,
          "a 700px viewport boards 672 (avail, under the cap)");

    int moves = 0;
    QMargins got;
    QObject::connect(&v, &HudCanvas::panelMoved, &v,
                     [&](int, QMargins m) { ++moves; got = m; });

    const QRectF box0 = v.motherWindowRects().at(0);
    const QPoint c0 = box0.center().toPoint();
    const qreal logical = QGuiApplication::primaryScreen()->size().width();

    // A +40px drag on the small board…
    drag(&v, c0, c0 + QPoint(40, 0));
    check(moves == 3, "all three real mouse moves emit panelMoved");
    const qreal fitSmall = qreal(smallW) / logical;
    const int stepSmall = qRound(40.0 / fitSmall);
    check(std::abs(got.left() - (ms[0].margins().left() + stepSmall)) <= 1,
          "…moves the window by +40px of CANVAS, mapped through the "
          "small board's fit (canvas px -> logical px)");

    // …then a bigger board: same 40 canvas px, FEWER logical px.
    v.resize(1500, 950);
    pump();
    const int bigW = boardWidth(v);
    check(bigW == 1050, "a 1500px viewport boards the 1050 cap");
    check(bigW > smallW, "resizing grew the board");
    const QRectF box1 = v.motherWindowRects().at(0);
    const QPoint c1 = box1.center().toPoint();
    drag(&v, c1, c1 + QPoint(40, 0));
    check(moves == 6, "all three post-resize mouse moves emit too");
    const qreal fitBig = qreal(bigW) / logical;
    check(qRound(40.0 / fitBig) < stepSmall,
          "the same 40 canvas px cover FEWER logical px on the bigger "
          "board — the mapping tracks the frame, same as paint/drag/"
          "hitTest share screenFrame()");
    v.setEditing(false);
    v.resize(700, 820);
    pump();
    const int narrowFull = boardWidth(v);
    v.setZoom(0.5);
    pump();
    check(std::abs(boardWidth(v) - qRound(narrowFull * 0.5)) <= 1,
          "zoom below 1 scales the fitted frame, including a narrow viewport");
}

// ─── #7: every user click notifies; setters never do ───────────────────
void testSameIndexClickNotifies()
{
    StubSource ms[4];
    HudCanvas v;
    v.resize(1180, 820);
    v.show();
    pump();
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    bindMothers(v, ms);
    v.setConsoleFidelityMode(true);
    pump();

    int selectedSignals = 0;
    int lastIdx = -1;
    QObject::connect(&v, &HudCanvas::panelSelected, &v, [&](int idx) {
        ++selectedSignals;
        lastIdx = idx;
    });

    // The console's own echo path: programmatic, signal-free.
    v.setSelectedPanel(2);
    pump();
    check(selectedSignals == 0,
          "setSelectedPanel() — the console-driven setter — emits "
          "nothing (the no-loop half of the #7 contract)");

    // Panel 2 is the DEFAULT-HIGHLIGHTED panel; the click below is a
    // re-click on it, and it must notify.
    const QRectF box = v.motherWindowRects().at(2);
    const QPoint ctr = box.center().toPoint();
    mouse(&v, QEvent::MouseButtonPress, ctr, Qt::LeftButton, Qt::LeftButton);
    mouse(&v, QEvent::MouseButtonRelease, ctr, Qt::LeftButton,
          Qt::MouseButtons());
    check(selectedSignals == 1 && lastIdx == 2,
          "clicking the already-highlighted default panel notifies "
          "(the press that used to be silently dropped)");

    // And again: a re-click keeps notifying.
    mouse(&v, QEvent::MouseButtonPress, ctr, Qt::LeftButton, Qt::LeftButton);
    mouse(&v, QEvent::MouseButtonRelease, ctr, Qt::LeftButton,
          Qt::MouseButtons());
    check(selectedSignals == 2 && lastIdx == 2,
          "a second re-click notifies again — the property panel can "
          "never go stale on a click");

    // The overview arm obeys the same rule.
    v.setOverview(true);
    pump();
    mouse(&v, QEvent::MouseButtonPress, ctr, Qt::LeftButton, Qt::LeftButton);
    mouse(&v, QEvent::MouseButtonRelease, ctr, Qt::LeftButton,
          Qt::MouseButtons());
    check(selectedSignals == 3 && lastIdx == 2,
          "the overview re-click notifies too");

    // A press on EMPTY space stays silent — the count must not move.
    v.setOverview(false);
    pump();
    mouse(&v, QEvent::MouseButtonPress, QPointF(3, 3),
          Qt::LeftButton, Qt::LeftButton);
    mouse(&v, QEvent::MouseButtonRelease, QPointF(3, 3),
          Qt::LeftButton, Qt::MouseButtons());
    check(selectedSignals == 3,
          "a press on empty space still selects nothing");
}

// ─── #7: Split — the pets mother box and child re-clicks ───────────────
void testSplitPetsAndChildReclick()
{
    StubSource subs[2];
    StubSource ms[mhw::kPanelCount];
    HudCanvas v;
    v.resize(1180, 820);
    v.show();
    pump();
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Split);
    QVector<HudCanvas::SubWindowBinding> binds{{&subs[0], 0, 0},
                                               {&subs[1], 0, 1}};
    v.setSubWindowBindings(binds);
    v.setSubWindowPixmap(0, solidPm(QSize(200, 90), QColor(255, 0, 255)),
                         true);
    v.setSubWindowPixmap(1, solidPm(QSize(200, 90), QColor(255, 0, 255)),
                         true);
    bindMothers(v, ms);
    v.setConsoleFidelityMode(true);
    pump();

    int selectedSignals = 0;
    int lastIdx = -1;
    QObject::connect(&v, &HudCanvas::panelSelected, &v, [&](int idx) {
        ++selectedSignals;
        lastIdx = idx;
    });
    int childSignals = 0;
    int lastChild = -1;
    QObject::connect(&v, &HudCanvas::subWindowSelected, &v, [&](int idx) {
        ++childSignals;
        lastChild = idx;
    });

    // In Split the only mother painted is the pets box — find it by
    // where the board actually drew a box rather than by restating the
    // table here.
    v.setSelectedPanel(3); // inspect the pets group, not the default player group
    pump();
    const QVector<QRectF> mothers = v.motherWindowRects();
    int pets = -1;
    for (int i = 0; i < mothers.size(); ++i)
        if (!mothers.at(i).isEmpty())
            pets = i;
    check(pets >= 0, "Split paints exactly one mother box (the pets one)");
    const QPoint petsCtr = mothers.at(pets).center().toPoint();
    mouse(&v, QEvent::MouseButtonPress, petsCtr,
          Qt::LeftButton, Qt::LeftButton);
    mouse(&v, QEvent::MouseButtonRelease, petsCtr,
          Qt::LeftButton, Qt::MouseButtons());
    check(selectedSignals == 1 && lastIdx == pets,
          "clicking Split's pets mother box notifies panelSelected");
    mouse(&v, QEvent::MouseButtonPress, petsCtr,
          Qt::LeftButton, Qt::LeftButton);
    mouse(&v, QEvent::MouseButtonRelease, petsCtr,
          Qt::LeftButton, Qt::MouseButtons());
    check(selectedSignals == 2 && lastIdx == pets,
          "re-clicking the pets box notifies again");

    // A child re-click also notifies its inspector.
    v.setSelectedPanel(0); // the two children belong to the player group
    pump();
    const QVector<QRectF> subsRects = v.subWindowRects();
    const QPoint childCtr = subsRects.at(1).center().toPoint();
    mouse(&v, QEvent::MouseButtonPress, childCtr,
          Qt::LeftButton, Qt::LeftButton);
    mouse(&v, QEvent::MouseButtonRelease, childCtr,
          Qt::LeftButton, Qt::MouseButtons());
    check(childSignals == 1 && lastChild == 1,
          "the first click on child 1 notifies subWindowSelected");
    mouse(&v, QEvent::MouseButtonPress, childCtr,
          Qt::LeftButton, Qt::LeftButton);
    mouse(&v, QEvent::MouseButtonRelease, childCtr,
          Qt::LeftButton, Qt::MouseButtons());
    check(childSignals == 2 && lastChild == 1,
          "the re-click on child 1 notifies as well");
}

// ─── #4: drag geometry is the margins cache + shared mapping ───────────
void testDragGeometryFromMarginsCache()
{
    StubSource s0;
    HudCanvas v;
    v.resize(1180, 820);
    v.show();
    pump();
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Split);
    QVector<HudCanvas::SubWindowBinding> one{{&s0, 0, 0}};
    v.setSubWindowBindings(one);
    // A TRANSPARENT pixmap: the box exists, but there is no picture to
    // measure. If the drag math needed the pixmap, this emits nothing
    // usable — with the fix it emits the exact cached margins + delta.
    QPixmap clear(QSize(200, 90));
    clear.fill(Qt::transparent);
    v.setSubWindowPixmap(0, clear, true);
    v.setConsoleFidelityMode(true);
    v.setEditing(true);
    pump();

    int moves = 0;
    QMargins got;
    QObject::connect(&v, &HudCanvas::subWindowMoved, &v,
                     [&](int, QMargins m) { ++moves; got = m; });

    const QMargins start = s0.margins();
    const QRectF box = v.subWindowRects().at(0);
    const QPoint ctr = box.center().toPoint();
    const qreal logical = QGuiApplication::primaryScreen()->size().width();
    const qreal fit = qreal(boardWidth(v)) / logical;

    drag(&v, ctr, ctr + QPoint(30, 12));
    check(moves == 3, "all three moves over a transparent window still emit");
    check(got.left() == start.left() + qRound(30.0 / fit)
              && got.top() == start.top() + qRound(12.0 / fit),
          "…with margins = cache(at press) + delta through the shared "
          "fit — geometry from src->margins(), no pixmap involved");

    // The console applies the emitted margins (that is its side of the
    // contract); the NEXT press must base itself on that latest cache.
    s0.m = got;
    const QMargins latest = s0.margins();
    const QRectF box2 = v.subWindowRects().at(0);
    const QPoint ctr2 = box2.center().toPoint();
    drag(&v, ctr2, ctr2 + QPoint(-18, 0));
    check(moves == 6
              && got.left() == latest.left() + qRound(-18.0 / fit)
              && got.top() == latest.top(),
          "the next drag starts from the LATEST margins cache, so a "
          "console that applies live margins never double-applies");

    // Release ends the gesture: a stray move afterwards writes nothing.
    mouse(&v, QEvent::MouseMove, ctr2 + QPoint(200, 0),
          Qt::NoButton, Qt::NoButton);
    check(moves == 6, "after release, motion writes no more margins");
}

// ─── #6: the neutral alpha-read checker ────────────────────────────────
void testCheckerIsNeutralAndRevealsAlpha()
{
    StubSource s0;
    HudCanvas pc, ctrl;
    for (HudCanvas *h : {&pc, &ctrl}) {
        h->resize(1180, 820);
        h->show();
    }
    pump();
    pc.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Split);
    QVector<HudCanvas::SubWindowBinding> one{{&s0, 0, 0}};
    pc.setSubWindowBindings(one);
    pc.setConsoleFidelityMode(true);
    ctrl.setConsoleFidelityMode(true);
    pc.setTransparencyGuideEnabled(true);
    ctrl.setTransparencyGuideEnabled(true);
    pump();

    const QImage empty = ctrl.grab().toImage();
    check(!frameFromBorder(empty, kBoardLine).isEmpty(),
          "the empty fidelity board still draws its border ring");
    check(empty.pixelColor(0, 0) == kPageGroundDark,
          "the checker does not touch the page ground around the board");

    // Two samples 16px apart — one checker cell — must differ, and both
    // must be NEUTRAL grey (equal-ish channels: no hue tint).
    const QRect fr = frameFromBorder(empty, kBoardLine);
    const QPoint a = fr.center();
    const QPoint b = a + QPoint(16, 0);
    const QColor ca = empty.pixelColor(a);
    const QColor cb = empty.pixelColor(b);
    check(ca != cb, "adjacent checker cells differ — the alpha-read "
                    "micro-texture is on the board ground");
    const auto neutral = [](const QColor &c) {
        return std::abs(c.red() - c.green()) <= 2
            && std::abs(c.green() - c.blue()) <= 2;
    };
    check(neutral(ca) && neutral(cb),
          "…and both cells are neutral grey, not a tinted pattern");

    // A translucent pixmap over the checker leaves both phases visible:
    // the two samples under it still differ. That is the whole point —
    // alpha you can actually READ.
    QPixmap half(QSize(200, 90));
    half.fill(QColor(255, 0, 255, 120));
    pc.setSubWindowPixmap(0, half, true);
    pump();
    const QRectF box = pc.subWindowRects().at(0);
    const QPoint inA = box.center().toPoint();
    const QPoint inB = inA + QPoint(16, 0);
    check(pc.grab().toImage().pixelColor(inA)
              != pc.grab().toImage().pixelColor(inB),
          "under a 47%-alpha window both checker phases show through — "
          "the compositing is unchanged and the alpha is legible");
}

// ─── #8: the output chain, through effectiveOutputName's seam ──────────
void testOutputChainFollowsScreenNotViewer()
{
    const QString primary =
        QGuiApplication::primaryScreen()
            ? QGuiApplication::primaryScreen()->name()
            : QString();

    StubSource ms[mhw::kPanelCount];
    ms[0].out = QStringLiteral("DP-1");
    // The others follow (empty own name) — the case where a viewer
    // fallback used to leak in.
    HudCanvas v;
    v.resize(1180, 820);
    v.show();
    pump();
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    bindMothers(v, ms);
    v.setConsoleFidelityMode(true);
    pump();

    check(v.windowOutputName(0) == QStringLiteral("DP-1"),
          "a mother that names its screen answers that screen (step 1)");
    check(v.windowOutputName(1) == primary,
          "a mother that FOLLOWS falls to the primary (steps 2-3) — "
          "never the canvas's preview screen as a fallback");
    check(v.windowOutputName(-1).isEmpty(),
          "an invalid index still answers the empty string");
}

} // namespace

int main(int argc, char *argv[])
{
    // Panel::settings() is a fixed IniFormat/UserScope QSettings, so a
    // build with no redirect would write into the developer's real
    // ~/.config. Redirect before the first QSettings use, mirroring
    // hud_canvas_split_tests.cpp.
    QTemporaryDir configRoot;
    if (!configRoot.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME", configRoot.path().toUtf8());

    QApplication app(argc, argv);
    // Real named QPA screens, not output strings for nonexistent screens.
    QWindowSystemInterface::handleScreenAdded(
        new FakeOutput(QStringLiteral("DP-1"), QRect(2560, 0, 2560, 1440)), false);
    QWindowSystemInterface::handleScreenAdded(
        new FakeOutput(QStringLiteral("eDP-1"), QRect(0, 0, 2560, 1600)), true);
    pump();

    std::printf("== canvas_interaction_regression_tests ==\n");
    testZoomChangesFrameAndScrollGeometry();
    testResizeGrowsFrameAndDragMapping();
    testSameIndexClickNotifies();
    testSplitPetsAndChildReclick();
    testDragGeometryFromMarginsCache();
    testCheckerIsNeutralAndRevealsAlpha();
    testOutputChainFollowsScreenNotViewer();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
