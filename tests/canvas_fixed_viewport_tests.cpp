// SPDX-License-Identifier: Apache-2.0
//
// v1.0.1 fixed-viewport mode (#9) + the board-label switch (#10).
//
// WHAT THIS FILE PINS (the two defects, as measured geometry — never as
// source matching):
//
//   1. ZOOM MUST NOT GROW THE PAGE (#9). The old behaviour, measured by
//      the parent as RED evidence: a bare canvas's hint went 1078 wide
//      at zoom 1 to 2156 wide (1078x1062 → 2156x2112) at zoom 2, and
//      inside a fixed 1400x760 QScrollArea the CHILD grew 1398x758 →
//      2156x2112, dragging scroll ranges of 1368/772 — the page, not
//      the picture, got bigger. With setFixedViewportMode(true) those
//      numbers are pinned CONSTANT: the outer viewport size, the
//      child's size, sizeHint() itself (the ancestor-facing natural
//      height), and the OUTER scrollbars. What MUST change with zoom is
//      the inside: the frame, the content extent (viewportContentSize)
//      and the scroll range (viewportScrollRange).
//
//   2. THE ZOOM LANDS ONCE, NEVER z² (#9). The content extent at zoom
//      1/2/4 in a 700px viewport is exactly 672·z + 28 wide — CSS cap
//      and fit each consumed once. Below 1 the frame shrinks to 336
//      (0.5·672) and the content falls back to the viewport (range 0).
//
//   3. ONE TRANSFORM (#9). Paint, hit-test and drag all read
//      screenFrame(), so a scroll offset moves the painted mother box
//      and the click position that hits it by the SAME amount: the
//      magenta body bbox shifts by EXACTLY the offset, a press at the
//      shifted centre notifies, a press beside it does not.
//
//   4. PORTRAIT / NARROW / ZOOM 0.5: no negative clipping. The frame
//      lands on its CSS pads (left 14, top 12) whenever the content
//      overflows, the full ring is pixel-measurable when it fits, and
//      portrait overflow yields a vertical range with a horizontal
//      range of 0 — the big image scrolls INSIDE the viewport.
//
//   5. THE USER'S SPLITTER HEIGHT SURVIVES ZOOM (#9), proven two ways:
//      four zoom steps leave the splitter's sizes byte-identical, and
//      the same canvas switched OUT of the mode at zoom 2 does move it
//      — so the pass above the control is load-bearing, not vacuous.
//
//   6. DEFAULT = THE OLD CONTRACT. A canvas nobody switched over still
//      grows its hint (1078 → 2156) and still fixes itself to the
//      zoomed size — the pre-existing behaviour is the default.
//
//   7. THE BOARD LABEL SWITCH (#10). Default visible; hiding changes
//      only pixels inside the `.board-label` band (every differing
//      pixel is inside it), moves no geometry, round-trips to the
//      identical image, and has no effect outside console (fidelity)
//      mode — the "console-only" half.
//
// REAL EVENTS, REAL PIXELS. Frames are read off the painted border ring
// (kBoardLine), bodies off counted colours, gestures are synthesized
// QMouseEvents through QApplication::sendEvent. Fake QPA screens make
// the numbers deterministic (primary eDP-1 2560×1600 ⇒ AR = h/w of the
// primary; HDMI-A-1 is the portrait 1080×1920). QSettings is isolated
// via XDG_CONFIG_HOME in main().

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
#include <QScrollBar>
#include <QScrollArea>
#include <QSize>
#include <QSplitter>
#include <QString>
#include <QTemporaryDir>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>

namespace {

int failures = 0;
int checks = 0;

// The tokens, spelled out (same values the split tests use; a test that
// imports the implementation's constants cannot notice the
// implementation drifting from the HTML).
const QColor kBoardLine(0x2e, 0x2e, 0x2a);        // --board-line
const QColor kMagenta(255, 0, 255);

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

// A PanelSource whose every number is under the test's control — the
// same stub the interaction suite carries (the two files link as
// separate targets; a shared header would be a third file this lane
// does not own).
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

QPixmap solidPm(const QSize &s, const QColor &c)
{
    QPixmap pm(s);
    pm.fill(c);
    return pm;
}

// The border rectangle, read back off the pixels: the fidelity board
// draws it on the frame's own edge line, so its width/height ARE the
// frame's (same reading boardWidth() uses in the interaction suite).
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

// Bounding box of every pixel matching `pred` — the body-tracking
// helper the scroll test needs: the whole mother artwork moves with one
// transform, so its bbox must move by exactly the offset.
template <class Pred>
QRect boxOf(const QImage &img, Pred pred)
{
    int l = img.width(), t = img.height(), r = -1, b = -1;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (!pred(img.pixelColor(x, y)))
                continue;
            l = qMin(l, x); r = qMax(r, x);
            t = qMin(t, y); b = qMax(b, y);
        }
    }
    if (r < 0)
        return {};
    return QRect(QPoint(l, t), QPoint(r, b));
}

bool isMagenta(const QColor &c)
{
    return c.red() > 100 && c.blue() > 100 && c.green() < 70;
}

int differingPixels(const QImage &a, const QImage &b)
{
    if (a.size() != b.size())
        return -1;
    int n = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.pixel(x, y) != b.pixel(x, y))
                ++n;
    return n;
}

qreal primaryAR()
{
    const QSize s = QGuiApplication::primaryScreen()->size();
    return qreal(s.height()) / qreal(s.width());
}

// Four mothers, painted the way the console paints them.
void bindMothers(HudCanvas &v, StubSource (&mothers)[mhw::kPanelCount])
{
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        mothers[i].sub = false;
        mothers[i].m = QMargins(10 + 30 * i, 10 + 150 * i, 0, 0);
        v.bindPanel(i, &mothers[i]);
        v.setPanelPixmap(i, solidPm(QSize(200, 90), kMagenta), true);
    }
}

// ─── #9: fixed outer QScrollArea — viewport const, inside changes ──────
void testFixedViewportUnderScrollArea()
{
    StubSource ms[mhw::kPanelCount];
    QWidget host;
    QScrollArea sa(&host);
    sa.setWidgetResizable(false);
    sa.resize(1400, 760);
    HudCanvas *v = new HudCanvas;
    check(!v->fixedViewportMode(),
          "a fresh canvas is NOT in fixed-viewport mode (default = old contract)");
    check(v->fidelityBoardLabelVisible(),
          "…and the board label is visible by default");
    v->setFixedViewportMode(true);
    v->setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    bindMothers(*v, ms);
    v->setConsoleFidelityMode(true);
    sa.setWidget(v);
    v->resize(1398, 758);            // the parent-measured child size
    host.resize(1400, 760);          // the parent-measured outer size
    host.show();
    pump();

    QSize lastContent;
    int contentSignals = 0;
    QObject::connect(v, &HudCanvas::viewportContentChanged, v,
                     [&](QSize c) { lastContent = c; ++contentSignals; });

    const QSize vp0 = sa.viewport()->size();
    const QSize child0 = v->size();
    const QSize hint0 = v->sizeHint();
    const int outerV0 = sa.verticalScrollBar()->maximum();
    const int outerH0 = sa.horizontalScrollBar()->maximum();

    const QRect ring1 = frameFromBorder(v->grab().toImage(), kBoardLine);
    check(ring1.width() == 1050,
          "fixed mode at zoom 1 keeps the 1050px CSS board baseline");
    check(std::abs(ring1.height() - qRound(1050.0 * primaryAR())) <= 1,
          "…with the height from the output's own aspect ratio");
    check(v->viewportContentSize() == QSize(1398, 758),
          "at zoom 1 the content fits: content == viewport");
    check(v->viewportScrollRange() == QSize(0, 0),
          "…so there is nothing to scroll yet");

    // ── zoom 2: the picture grows, the PAGE does not ───────────────────
    v->setZoom(2.0);
    pump();
    check(sa.viewport()->size() == vp0,
          "zoom 2.0 does not change the fixed outer viewport size");
    check(v->size() == child0,
          "…and does not change the child's size (RED evidence: "
          "the child used to grow 1398x758 -> 2156x2112)");
    check(v->sizeHint() == hint0,
          "…nor the natural hint the ancestors lay out from (RED: "
          "1078 -> 2156 wide)");
    check(hint0.width() == 1078,
          "the fixed-mode hint is the zoom-1 CSS width (1050 + 2*14)");
    check(sa.verticalScrollBar()->maximum() == outerV0
              && sa.horizontalScrollBar()->maximum() == outerH0,
          "the OUTER scroll range does not follow the zoom — the page "
          "does not grow");
    check(v->viewportContentSize() == QSize(2128, 12 + qRound(2100.0 * primaryAR())),
          "the CONTENT grows instead: 2*1050 + 28 = 2128 wide and "
          "padTop + 2x-board tall — the zoom consumed ONCE (never 4x, "
          "never the old widget-growth path)");
    const QSize r2 = v->viewportScrollRange();
    check(r2.width() == 2128 - 1398
              && r2.height() == v->viewportContentSize().height() - 758
              && r2.width() > 0 && r2.height() > 0,
          "…and the scroll range opens up inside the fixed viewport");
    check(lastContent == v->viewportContentSize(),
          "viewportContentChanged reported the new extent");
    const QRect ring2 = frameFromBorder(v->grab().toImage(), kBoardLine);
    check(!ring2.isEmpty(), "the zoomed frame is painted");
    check(ring2.left() == 14 && ring2.top() == 12,
          "…sitting exactly on its CSS pads (left 14, top 12) — no "
          "negative-centre clipping at zoom > 1");

    // ── zoom 0.5: the frame shrinks, back to range 0 ───────────────────
    v->setZoom(0.5);
    pump();
    check(sa.viewport()->size() == vp0 && v->size() == child0,
          "zoom 0.5 leaves the outer viewport and the child untouched");
    check(v->viewportContentSize() == QSize(1398, 758),
          "the half-size content fits: content falls back to the viewport");
    check(v->viewportScrollRange() == QSize(0, 0),
          "…and the scroll range closes again");
    const QRect ring05 = frameFromBorder(v->grab().toImage(), kBoardLine);
    check(ring05.width() == 525,
          "zoom 0.5 shrinks the BOARD to 525 (0.5 * 1050) — the frame "
          "itself, fully painted inside the viewport");
    check(ring05.height() == qRound(525.0 * primaryAR()),
          "…and its height follows the same single factor");
    check(contentSignals >= 2,
          "every extent change was announced on viewportContentChanged");

    // ── round trip back to 1 ────────────────────────────────────────────
    v->setZoom(1.0);
    pump();
    check(sa.viewport()->size() == vp0 && v->size() == child0
              && v->sizeHint() == hint0,
          "round trip 1 -> 2 -> 0.5 -> 1 leaves viewport/child/hint "
          "exactly where they were");
    check(frameFromBorder(v->grab().toImage(), kBoardLine).width() == 1050,
          "…and restores the exact baseline board");
}

// ─── #9: ONE TRANSFORM — paint, hit-test and scroll share the offset ───
void testScrollMovesPaintAndHitTestTogether()
{
    StubSource ms[mhw::kPanelCount];
    HudCanvas v;
    v.resize(1398, 758);
    v.show();
    pump();
    v.setFixedViewportMode(true);
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    // Mother 0 alone, with enough top margin that its selection tag
    // (18px above the box) survives a 6px scroll without clipping —
    // then the body bbox must move by EXACTLY the offset.
    ms[0].sub = false;
    ms[0].m = QMargins(10, 30, 0, 0);
    v.bindPanel(0, &ms[0]);
    v.setPanelPixmap(0, solidPm(QSize(420, 160), kMagenta), true);
    v.setConsoleFidelityMode(true);
    v.setZoom(2.0);
    pump();

    check(v.viewportScrollRange().width() > 40
              && v.viewportScrollRange().height() > 40,
          "zoom 2 opens a real two-axis scroll range to pan inside");

    const QImage img0 = v.grab().toImage();
    const QRect box0 = boxOf(img0, isMagenta);
    check(!box0.isEmpty(), "the mother's magenta body is painted at offset 0");

    int scrollSignals = 0;
    QObject::connect(&v, &HudCanvas::viewportScrollChanged, &v,
                     [&](QPoint) { ++scrollSignals; });

    const QPoint off(10, 6);
    v.setViewportScrollOffset(off);
    const QImage img1 = v.grab().toImage();
    const QRect box1 = boxOf(img1, isMagenta);
    check(scrollSignals == 1,
          "a real offset change announces itself once");
    check(v.viewportScrollOffset() == off,
          "the stored offset is the one we asked for (inside the range)");
    check(box1 == box0.translated(-off.x(), -off.y()),
          "PAINT follows the offset: the body bbox moved by exactly "
          "(-10, -6) — no rounding drift, no second transform");
    check(v.viewportContentSize() == QSize(2128, 1325),
          "scrolling changes neither the content extent nor the widget");

    // HIT-TEST follows the same offset: the press at the SHIFTED centre
    // notifies, a press beside the box does not.
    int selected = 0;
    int lastIdx = -1;
    QObject::connect(&v, &HudCanvas::panelSelected, &v,
                     [&](int idx) { ++selected; lastIdx = idx; });
    const QPoint c1 = box1.center();
    mouse(&v, QEvent::MouseButtonPress, QPointF(c1), Qt::LeftButton,
          Qt::LeftButton);
    mouse(&v, QEvent::MouseButtonRelease, QPointF(c1), Qt::LeftButton,
          Qt::MouseButtons());
    check(selected == 1 && lastIdx == 0,
          "a press at the SCROLLED position of the box hits it (hit-test "
          "consumes the same offset frame as paint)");
    const QPoint empty = c1 + QPoint(400, 400);   // clear of every box
    mouse(&v, QEvent::MouseButtonPress, QPointF(empty), Qt::LeftButton,
          Qt::LeftButton);
    mouse(&v, QEvent::MouseButtonRelease, QPointF(empty), Qt::LeftButton,
          Qt::MouseButtons());
    check(selected == 1,
          "a press beside the box hits nothing — no phantom geometry "
          "left behind at the old position");

    // The clamp: an out-of-range push is written CLAMPED, never ignored.
    v.setViewportScrollOffset(QPoint(100000, 100000));
    const QSize rangeNow = v.viewportScrollRange();
    check(v.viewportScrollOffset() == QPoint(rangeNow.width(), rangeNow.height()),
          "an out-of-range offset is clamped into the range (scrollbar "
          "and canvas can never desynchronise)");
    v.setViewportScrollOffset(QPoint(-50, -50));
    check(v.viewportScrollOffset() == QPoint(0, 0),
          "…in both directions");
    check(scrollSignals == 3,
          "each of the three real moves announced exactly once");
    v.setViewportScrollOffset(off);
    check(v.viewportScrollOffset() == off && scrollSignals == 4,
          "re-pushing an offset moves it and announces it once more");
    v.setViewportScrollOffset(off);
    check(scrollSignals == 4,
          "pushing the CURRENT offset again is a no-op signal-wise");

    // Leaving the mode zeroes the pan.
    v.setFixedViewportMode(false);
    check(v.viewportScrollOffset() == QPoint(0, 0)
              && v.viewportScrollRange() == QSize(0, 0),
          "leaving fixed-viewport mode clears the pan and the range");
}

// ─── #9: the zoom lands ONCE — narrow viewport, 0.5/1/2/4 ──────────────
void testNarrowViewportZoomOnce()
{
    StubSource ms[mhw::kPanelCount];
    HudCanvas v;
    v.resize(700, 520);
    v.show();
    pump();
    v.setFixedViewportMode(true);
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    bindMothers(v, ms);
    v.setConsoleFidelityMode(true);
    pump();

    const QSize hint0 = v.sizeHint();

    const QRect ring1 = frameFromBorder(v.grab().toImage(), kBoardLine);
    check(ring1.width() == 672,
          "a 700px viewport boards 672 (=700-28, under the cap) at zoom 1");
    check(ring1.left() == 14,
          "…flush with the left CSS pad");
    check(v.viewportContentSize() == QSize(700, 520)
              && v.viewportScrollRange() == QSize(0, 0),
          "…and fits: no scroll range yet");

    v.setZoom(2.0);
    pump();
    check(v.viewportContentSize()
              == QSize(2 * 672 + 28, 12 + qRound(1344.0 * primaryAR())),
          "zoom 2 content is exactly 2*672 + 28 wide — the cap and the "
          "fit each consumed ONCE (a z² bug reads 4*672, the old "
          "widget-growth path reads something else again)");
    check(v.viewportScrollRange() == QSize(672, v.viewportContentSize().height() - 520),
          "…and the range opens by exactly the overflow");
    check(v.size() == QSize(700, 520) && v.sizeHint() == hint0,
          "the child and its hint are still zoom-independent");

    v.setZoom(4.0);
    pump();
    check(v.viewportContentSize().width() == 4 * 672 + 28,
          "zoom 4 content width is 4*672 + 28 — linear in z at every step");
    check(v.size() == QSize(700, 520) && v.sizeHint() == hint0,
          "…with the widget still at its viewport size");

    v.setZoom(0.5);
    pump();
    const QRect ring05 = frameFromBorder(v.grab().toImage(), kBoardLine);
    check(ring05.width() == 336,
          "zoom 0.5 shrinks the frame itself to 336 (0.5 * 672)");
    check(v.viewportContentSize() == QSize(700, 520)
              && v.viewportScrollRange() == QSize(0, 0),
          "…which fits again: the range closes");

    v.setZoom(1.0);
    pump();
    check(frameFromBorder(v.grab().toImage(), kBoardLine).width() == 672,
          "round trip 0.5 → 1 → 2 → 4 → 1 restores the baseline frame");
    check(v.size() == QSize(700, 520) && v.sizeHint() == hint0,
          "…and never moved the widget or the hint once");
}

// ─── #9: portrait — vertical scroll, no negative clipping ──────────────
void testPortraitNoNegativeClipping()
{
    StubSource ms[mhw::kPanelCount];
    HudCanvas v;
    v.resize(700, 640);
    v.show();
    pump();
    v.setFixedViewportMode(true);
    v.setPreviewScreen(QStringLiteral("HDMI-A-1"));   // the 1080×1920 fake
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    bindMothers(v, ms);
    v.setConsoleFidelityMode(true);
    pump();

    const qreal par = 1920.0 / 1080.0;
    const qreal avail = 700.0 - 28.0;            // the CSS avail width

    // z0.5: fits → the WHOLE ring must be measurable (a negative-clipped
    // frame would come back short or empty).
    v.setZoom(0.5);
    pump();
    const QRect r05 = frameFromBorder(v.grab().toImage(), kBoardLine);
    const qreal bw05 = avail * 0.48 * 0.5;       // 48% CSS fraction
    check(r05.width() == qRound(bw05),
          "portrait zoom 0.5 paints the full 48%-fraction board "
          "(nothing clipped off the left or right)");
    check(r05.height() == qRound(std::max(bw05 * par, 220.0 * 0.5)),
          "…and its full height (the 220px portrait floor is a min, "
          "not a substitution)");
    check(v.viewportScrollRange() == QSize(0, 0),
          "…with no scroll needed");

    // z1: still fits — full ring again.
    v.setZoom(1.0);
    pump();
    const QRect r1 = frameFromBorder(v.grab().toImage(), kBoardLine);
    const qreal bw1 = avail * 0.48;
    check(r1.width() == qRound(bw1) && r1.height() == qRound(bw1 * par),
          "portrait zoom 1 paints the whole board inside the viewport");

    // z2: taller than the viewport → a VERTICAL range only.
    v.setZoom(2.0);
    pump();
    const qreal bw2 = bw1 * 2.0;
    check(v.viewportContentSize().width() == 700,
          "portrait overflow is vertical: the width still fits, so the "
          "content width stays the viewport width");
    check(v.viewportContentSize().height() == 12 + int(std::ceil(bw2 * par)),
          "…and the content height is exactly padTop + board(2x)");
    check(v.viewportScrollRange() == QSize(0, v.viewportContentSize().height() - 640),
          "so the horizontal range is 0 and the vertical range is the "
          "overflow — the image scrolls INSIDE the viewport");
    const QRect r2 = frameFromBorder(v.grab().toImage(), kBoardLine);
    check(!r2.isEmpty()
              && r2.left() == qRound(14.0 + (avail - bw2) / 2.0)
              && r2.top() == 12,
          "at zoom 2 the frame is centred horizontally and pinned to the "
          "top pad — never a negative top edge");
    check(r2.width() == qRound(bw2),
          "…with both side borders visible (nothing clipped horizontally)");
    check(r2.bottom() == 639,
          "…and its bottom runs past the viewport edge — which is what "
          "the vertical scroll is for");

    // Scroll to the end of the vertical range: the bottom edge sits on
    // the viewport floor, the top has scrolled out. Round trip restores.
    v.setViewportScrollOffset(QPoint(0, v.viewportScrollRange().height()));
    const QRect rMax = frameFromBorder(v.grab().toImage(), kBoardLine);
    check(rMax.bottom() == 639 && rMax.top() == 0,
          "at max vertical scroll the frame's bottom edge is visible at "
          "the viewport floor and the top is scrolled out");
    v.setViewportScrollOffset(QPoint(0, 0));
    const QRect rBack = frameFromBorder(v.grab().toImage(), kBoardLine);
    check(rBack.top() == 12 && rBack.width() == qRound(bw2),
          "scrolling back restores the exact frame");
}

// ─── #9: the user's splitter height is never overwritten ───────────────
void testSplitterHeightSurvivesZoom()
{
    QWidget host;
    host.resize(1000, 900);
    QSplitter sp(Qt::Vertical, &host);
    auto *top = new QWidget;
    StubSource ms[mhw::kPanelCount];
    auto *v = new HudCanvas;
    auto *bot = new QWidget;
    sp.addWidget(top);
    sp.addWidget(v);
    sp.addWidget(bot);
    v->setFixedViewportMode(true);
    v->setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    bindMothers(*v, ms);
    v->setConsoleFidelityMode(true);
    sp.resize(980, 880);
    host.show();
    sp.setSizes({200, 400, 200});
    pump();
    const QList<int> s0 = sp.sizes();
    check(s0.size() == 3 && s0[1] > 0 && v->height() == s0[1],
          "the splitter handed the canvas its user-chosen height");

    v->setZoom(2.0);
    pump();
    v->setZoom(4.0);
    pump();
    v->setZoom(0.5);
    pump();
    v->setZoom(2.0);
    pump();
    check(sp.sizes() == s0 && v->height() == s0[1],
          "four zoom steps later the splitter's sizes and the canvas "
          "height are byte-identical — fixed mode never resizes what "
          "the user sized");

    // TWO-SIDED PROOF: the same canvas, switched OUT of the mode at
    // zoom 2, does move the splitter (the legacy arm fixes the widget
    // to its zoomed hint, and QSplitter honours the new minimum). If
    // this never fired, the assertion above could not fail either.
    v->setFixedViewportMode(false);
    pump();
    check(v->height() != s0[1],
          "MUTATION-STYLE CONTROL: leaving fixed mode at zoom 2 DOES "
          "move the splitter (legacy setFixedSize) — the pass above is "
          "load-bearing");
}

// ─── default off: the OLD contract is the default ─────────────────────
void testDefaultKeepsLegacyZoomContract()
{
    StubSource ms[mhw::kPanelCount];
    HudCanvas v;
    v.resize(1180, 820);
    v.show();
    pump();
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    bindMothers(v, ms);
    v.setConsoleFidelityMode(true);
    pump();

    const int w1 = v.sizeHint().width();
    check(w1 == 1078, "default mode, zoom 1: hint is 1078 (1050 + 28)");
    v.setZoom(2.0);
    pump();
    check(v.sizeHint().width() == 2156,
          "default mode, zoom 2: the hint still doubles to 2156 — the "
          "old (page-growing) contract is what a canvas nobody switched "
          "over keeps");
    check(v.width() == 2156,
          "…and the widget still fixes itself to it (the RED numbers, "
          "preserved as the default)");
    check(v.sizeHint().width() > w1,
          "…which is exactly the growth fixed-viewport mode forbids");

    // Migration: switching the mode ON releases both the fixed size and
    // the artifact.
    v.setFixedViewportMode(true);
    pump();
    check(v.fixedViewportMode(), "the mode flag reads back");
    check(v.sizeHint().width() == 1078,
          "…and the hint drops back to the zoom-1 CSS width");
    check(v.width() == 1078,
          "…and the canvas shrinks off the zoomed fixed size it had "
          "inherited from the legacy arm");
    v.setZoom(4.0);
    pump();
    check(v.sizeHint().width() == 1078 && v.width() == 1078,
          "further zooming in the mode never re-grows the widget");
    v.setZoom(1.0);
    pump();
    check(frameFromBorder(v.grab().toImage(), kBoardLine).width() == 1050,
          "…and the painted board still answers the zoom");
}

// ─── #10: hide the redundant in-board resolution label ─────────────────
void testBoardLabelSwitch()
{
    // NO bindings on purpose: a mother's body would paint straight over
    // the label band and hide the very pixels under test.
    StubSource ms[mhw::kPanelCount];
    Q_UNUSED(ms);
    HudCanvas v;
    v.resize(1180, 820);
    v.show();
    pump();
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    v.setConsoleFidelityMode(true);
    pump();

    check(v.fidelityBoardLabelVisible(), "default: the label is visible");
    const QImage on = v.grab().toImage();
    const QRect ring = frameFromBorder(on, kBoardLine);
    check(!ring.isEmpty(), "the fidelity board paints its border ring");

    v.setFidelityBoardLabelVisible(false);
    const QImage off = v.grab().toImage();
    check(!v.fidelityBoardLabelVisible(), "the flag reads back");
    const int diffs = differingPixels(on, off);
    check(diffs > 50,
          "hiding the label changes real painted pixels (not a no-op)");
    // EVERY changed pixel lives inside the `.board-label` band — the
    // frame's top-left strip the label is drawn in (+2px of AA slack).
    // Anything outside would mean the switch touched geometry or other
    // chrome.
    const QRect band = QRect(ring.left() + 14, ring.top() + 12,
                             std::max(0, ring.width() - 28), 16)
                           .adjusted(-2, -2, 2, 2);
    int outside = 0;
    for (int y = 0; y < on.height(); ++y) {
        for (int x = 0; x < on.width(); ++x) {
            if (on.pixel(x, y) != off.pixel(x, y)
                && !band.contains(QPoint(x, y)))
                ++outside;
        }
    }
    check(outside == 0,
          "…and NOTHING outside the label band changed — no frame, no "
          "board, no windows moved");
    check(frameFromBorder(off, kBoardLine) == ring
              && v.viewportContentSize() == QSize(1180, 820),
          "the frame and the content extent are byte-identical with the "
          "label hidden (it has no geometry role)");

    v.setFidelityBoardLabelVisible(true);
    const QImage back = v.grab().toImage();
    check(differingPixels(on, back) == 0,
          "round trip back to visible restores the exact image");

    // Console-only: outside fidelity mode the label was never painted,
    // so the switch must move NOTHING there.
    v.setConsoleFidelityMode(false);
    pump();
    const QImage legacyOn = v.grab().toImage();
    v.setFidelityBoardLabelVisible(false);
    const QImage legacyOff = v.grab().toImage();
    check(differingPixels(legacyOn, legacyOff) == 0,
          "outside console mode the switch paints nothing different — "
          "legacy baselines cannot move");
    // The legacy arm's own ground, same token the split suite's §7
    // reads (#06080a at the corner): the board IS still painted, and
    // it is the legacy chrome, not the fidelity one.
    check(legacyOn.pixelColor(0, 0) == QColor(0x06, 0x08, 0x0a),
          "…and the legacy board chrome is still painted as before");
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
    QWindowSystemInterface::handleScreenAdded(
        new FakeOutput(QStringLiteral("HDMI-A-1"), QRect(5120, 0, 1080, 1920)),
        false);
    pump();

    std::printf("== canvas_fixed_viewport_tests ==\n");
    testFixedViewportUnderScrollArea();
    testScrollMovesPaintAndHitTestTogether();
    testNarrowViewportZoomOnce();
    testPortraitNoNegativeClipping();
    testSplitterHeightSurvivesZoom();
    testDefaultKeepsLegacyZoomContract();
    testBoardLabelSwitch();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
