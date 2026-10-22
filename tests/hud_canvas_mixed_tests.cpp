// SPDX-License-Identifier: Apache-2.0
//
// v1.0.1 Mixed topology (#11): mother panels and detached sub-windows
// COEXIST on one board — measured off real paints, never off source.
//
// WHAT THIS FILE PINS
//
//   1. MIXED DRAWS BOTH HALVES. With bindings present, the old gate
//      (`subSlots_.isEmpty()`) hid EVERY mother in any mode that had
//      sub-windows. In Mixed the retained mother paints AND the
//      detached children paint: colour-counted off grab().toImage(),
//      with the differential (Mixed vs Split vs Single) computed from
//      the same image reader. The pixel DIFFERENCE between Mixed and
//      pure Split is asserted to be real (>100 pixels), so a suite that
//      accidentally compares a canvas with itself cannot pass.
//
//   2. PURE SPLIT IS UNCHANGED. Same bindings, mode flipped to Split:
//      zero mother pixels (except the pets exception below), the
//      children's colour count IDENTICAL to Mixed — a child's box does
//      not depend on the mothers, and the mothers' box does not eat the
//      children's.
//
//   3. SINGLE STILL CLEARS THE CHILDREN. setLayoutMode(Single) empties
//      the bindings — child count 0, mother back — the v0.13 pixel
//      guarantee, re-asserted here because Mixed must not have loosened
//      it.
//
//   4. THE PETS EXCEPTION HOLDS IN BOTH MODES. Non-fidelity Split still
//      paints only pets from the four-slot arm (the historical
//      exception), and Mixed paints all four present mothers — pets
//      must not be caught by any new gate either way.
//
//   5. THE MOTHER SHRINKS WITH THE ROWS IT KEEPS. The mother's box
//      follows its source's contentSize() — the one geometry path — so
//      a source reporting a half-height retained extent paints a
//      half-height box (exact ratio asserted), and a source reporting
//      NO extent (every row detached) paints NO shell: no pixels, no
//      hit rect (lastTarget_ stays at its reset default), while its
//      children keep painting. The control: in Single the same
//      zero-extent source still takes the legacy shell path (positioned
//      rect, painted tag) — the guard is Mixed-only.
//
// WHY STUB SOURCES: PanelSource is the canvas's ONLY window of
// geometry (corner/margins/contentSize/scale), and under test every
// number must be the test's. The single fidelity case that matters for
// the console's view scope uses the same stub — the view filter is the
// existing v1.0.0 contract, not this lane's.
//
// REAL PAINTS ONLY. Every claim is colour-counted off
// grab().toImage() or read from the geometry the last real paint left
// behind (motherWindowRects / subWindowRects). No source matching.
// QSettings is isolated via XDG_CONFIG_HOME in main().

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
#include <QPixmap>
#include <QRgb>
#include <QRect>
#include <QRectF>
#include <QScreen>
#include <QSize>
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

// One distinct body colour per window. The predicates, not exact-match
// counts, because an unselected window is painted at 55% opacity over
// the board's neutral ground. Measured, not guessed: a dimmed body
// lands at src*0.55 + ground*0.45 with ground in the board's own range
// (the legacy gradient runs 34..6 per channel, the fidelity checker a
// little higher), which puts dimmed red's red-channel at 143..155 and
// dimmed green's green-channel at 144..163 — the thresholds sit just
// under those bands, and every OTHER colour is kept out by a different
// channel (magenta carries blue ≥ 143, cyan carries blue ≥ 143 while
// green carries blue ≤ 59, blue carries green ≤ 88, teal's red is
// exactly 80). The diagnostic counts that pinned these bands are in
// the run log: magenta 13450 / blue 13368 / red 955→full band / green
// 270→full band once the thresholds sit under the measured range.
const QColor kMagenta(255, 0, 255);    // mother 0 (player)
const QColor kRed(255, 40, 0);         // mother 1 (monster)
const QColor kGreen(0, 255, 80);       // mother 2 (damage)
const QColor kBlue(60, 120, 255);      // mother 3 (pets)
const QColor kCyan(0, 255, 255);       // every detached child

bool isMagenta(const QColor &c)
{
    return c.red() > 100 && c.blue() > 100 && c.green() < 70;
}
bool isRed(const QColor &c)
{
    // >140, not >150: dimmed red measures 143..155 over the legacy
    // gradient, and a 150 cut was measured to keep 955 of ~13000 body
    // pixels (the failure that pinned the band).
    return c.red() > 140 && c.green() < 100 && c.blue() < 80;
}
bool isGreen(const QColor &c)
{
    // >135, not >150: dimmed green measures 144..163; blue <130 is
    // what keeps CYAN (blue ≥143) out despite the raised green cut.
    return c.green() > 135 && c.red() < 80 && c.blue() < 130;
}
bool isBlue(const QColor &c)
{
    return c.blue() > 140 && c.red() < 120 && c.green() < 130;
}
bool isCyan(const QColor &c)
{
    return c.green() > 140 && c.blue() > 140 && c.red() < 80;
}

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

// A PanelSource whose every number is under the test's control — the
// same stub the other two suites carry (separate link targets; a shared
// header would be a third file this lane does not own).
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

// Colour-counted off the real paint: how many pixels carry `pred`.
template <class Pred>
int countIf(const QImage &img, Pred pred)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (pred(img.pixelColor(x, y)))
                ++n;
    return n;
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

// Four mothers, one colour each, all anchored TopLeft at the four
// corners of the logical screen — so per-mother presence is
// colour-addressable and no mother's body can sit under a child.
void bindMothers(HudCanvas &v, StubSource (&mothers)[mhw::kPanelCount])
{
    static const QColor kBody[mhw::kPanelCount] = {
        kMagenta, kRed, kGreen, kBlue
    };
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        mothers[i].sub = false;
        mothers[i].c = Corner::TopLeft;
        mothers[i].m = QMargins(10 + 1990 * (i / 2), 10 + 1000 * (i % 2),
                                0, 0);
        v.bindPanel(i, &mothers[i]);
        v.setPanelPixmap(i, solidPm(QSize(420, 160), kBody[i]), true);
    }
}

// Two detached children, cyan, placed in the middle of the screen —
// clear of every mother body at every mode's frame (the wall lays sub
// boxes out from their own corners/lattice only, never from the
// mothers, so their positions are mode-independent by construction).
QVector<HudCanvas::SubWindowBinding> bindChildren(
    HudCanvas &v, StubSource (&kids)[2])
{
    for (int i = 0; i < 2; ++i) {
        kids[i].sub = true;
        kids[i].c = Corner::TopLeft;
        kids[i].m = QMargins(900 + 500 * i, 700, 0, 0);
        kids[i].content = QSize(300, 100);
    }
    QVector<HudCanvas::SubWindowBinding> bindings;
    for (int i = 0; i < 2; ++i) {
        HudCanvas::SubWindowBinding b;
        b.src = &kids[i];
        b.parentSlot = 0;          // both rows detached from player
        b.tableRow = i;
        bindings.append(b);
    }
    v.setSubWindowBindings(bindings);
    for (int i = 0; i < 2; ++i)
        v.setSubWindowPixmap(i, solidPm(QSize(300, 100), kCyan), true);
    return bindings;
}

// ─── #11 + fidelity view: mothers and children on one board ────────────
//
// The console's own shape: panel view (fidelity + selected panel 0)
// shows THAT panel's retained mother AND its detached rows side by
// side. Same canvas, same bindings, three modes.
void testMixedCoexistenceFidelity()
{
    StubSource mothers[mhw::kPanelCount];
    StubSource kids[2];
    HudCanvas v;
    v.resize(1180, 820);
    v.show();
    pump();
    v.setFixedViewportMode(false);   // this lane's goal is mode-independent
    v.setConsoleFidelityMode(true);
    v.setSelectedPanel(0);
    bindMothers(v, mothers);
    bindChildren(v, kids);
    pump();

    // ── Mixed: BOTH halves paint ────────────────────────────────────────
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Mixed);
    pump();
    const QImage imgMixed = v.grab().toImage();
    check(countIf(imgMixed, isMagenta) > 1000,
          "Mixed: the RETAINED mother's body is painted (magenta pixels "
          "present — the old gate hid every mother)");
    check(countIf(imgMixed, isCyan) > 1000,
          "Mixed: the DETACHED children's bodies are painted too "
          "(cyan pixels present) — mother and children COEXIST");
    check(v.motherWindowRects().at(0).width() > 0
              && v.motherWindowRects().at(0).height() > 0,
          "Mixed: geometry agrees with paint — mother 0's rect is live");
    check(v.motherWindowRects().at(1).isEmpty()
              && v.motherWindowRects().at(3).isEmpty(),
          "Mixed: the panels outside the console's view scope stay "
          "unpainted (the existing v1.0.0 filter, untouched)");
    check(v.subWindowRects().size() == 2
              && !v.subWindowRects().at(0).isEmpty()
              && !v.subWindowRects().at(1).isEmpty(),
          "Mixed: both child boxes are laid out and hit-testable");
    const int cyanMixed = countIf(imgMixed, isCyan);

    // ── pure Split: children only, byte-identical children ─────────────
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Split);
    pump();
    const QImage imgSplit = v.grab().toImage();
    check(countIf(imgSplit, isMagenta) == 0,
          "pure Split is unchanged: no mother pixels, same bindings");
    check(countIf(imgSplit, isCyan) == cyanMixed,
          "…and the children paint the SAME pixels as in Mixed — the "
          "mothers appearing did not move, dim or duplicate a child box");
    check(v.motherWindowRects().at(0).isEmpty(),
          "…with mother 0's rect reset (nothing to hit)");
    const int diffMixedSplit = differingPixels(imgMixed, imgSplit);
    check(diffMixedSplit > 100,
          "the Mixed-vs-Split grab() difference is a real, large pixel "
          "delta (not a self-comparison)");
    check(countIf(imgSplit, isMagenta) < countIf(imgMixed, isMagenta),
          "…and its direction: Mixed paints strictly more mother body");

    // ── Single: bindings cleared, mother only ──────────────────────────
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    pump();
    check(v.subWindowCount() == 0,
          "Single still CLEARS the sub-bindings (v0.13 guarantee kept)");
    const QImage imgSingle = v.grab().toImage();
    check(countIf(imgSingle, isCyan) == 0,
          "…so no child pixels survive into Single");
    check(countIf(imgSingle, isMagenta) > 1000,
          "…and the mother paints again");

    // ── back to Mixed with fresh bindings: the round trip ──────────────
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Mixed);
    bindChildren(v, kids);
    pump();
    const QImage imgBack = v.grab().toImage();
    check(countIf(imgBack, isMagenta) > 1000
              && countIf(imgBack, isCyan) > 1000,
          "Mixed → Single → Mixed with a re-pushed binding list restores "
          "the coexistence");
    check(countIf(imgBack, isCyan) == cyanMixed,
          "…at exactly the same child pixels as the first Mixed pass");
}

// ─── #11 without the console: the gate itself, and the pets exception ──
void testMixedCoexistenceBare()
{
    StubSource mothers[mhw::kPanelCount];
    StubSource kids[2];
    HudCanvas v;
    v.resize(1180, 820);
    v.show();
    pump();
    // fidelity OFF: no view scope, so all four mothers are candidates —
    // this isolates the drawMothers gate from the v1.0.0 view filter.
    v.setConsoleFidelityMode(false);
    bindMothers(v, mothers);
    bindChildren(v, kids);
    pump();

    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Mixed);
    pump();
    const QImage mixed = v.grab().toImage();
    std::printf("  [diag] bare Mixed counts: magenta=%d red=%d green=%d blue=%d cyan=%d\n",
                countIf(mixed, isMagenta), countIf(mixed, isRed),
                countIf(mixed, isGreen), countIf(mixed, isBlue),
                countIf(mixed, isCyan));
    check(countIf(mixed, isMagenta) > 1000,
          "bare Mixed: player mother painted");
    check(countIf(mixed, isRed) > 1000,
          "bare Mixed: monster mother painted");
    check(countIf(mixed, isGreen) > 1000,
          "bare Mixed: damage mother painted");
    check(countIf(mixed, isBlue) > 1000,
          "bare Mixed: PETS mother painted — the pets exception cannot "
          "become a hiding gate in Mixed either");
    check(countIf(mixed, isCyan) > 1000,
          "bare Mixed: the two children painted alongside all four "
          "mothers");

    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Split);
    pump();
    const QImage split = v.grab().toImage();
    check(countIf(split, isMagenta) == 0 && countIf(split, isRed) == 0
              && countIf(split, isGreen) == 0,
          "pure Split (bare) hides the three splittable mothers exactly "
          "as before");
    check(countIf(split, isBlue) > 1000,
          "pure Split (bare) keeps drawing PETS — the historical "
          "exception is byte-for-byte intact");
    check(countIf(split, isCyan) > 1000,
          "pure Split (bare) draws the children as always");

    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    pump();
    const QImage single = v.grab().toImage();
    std::printf("  [diag] bare Single counts: magenta=%d red=%d green=%d blue=%d cyan=%d\n",
                countIf(single, isMagenta), countIf(single, isRed),
                countIf(single, isGreen), countIf(single, isBlue),
                countIf(single, isCyan));
    check(v.subWindowCount() == 0,
          "Single clears the bindings in the bare canvas too");
    check(countIf(single, isCyan) == 0,
          "…so the children are gone");
    check(countIf(single, isMagenta) > 1000
              && countIf(single, isRed) > 1000
              && countIf(single, isGreen) > 1000
              && countIf(single, isBlue) > 1000,
          "…and all four mothers paint — Single's contract unchanged");
}

// ─── #11: the mother's box follows its retained extent ─────────────────
void testMotherShrinksWithRetainedRows()
{
    StubSource mothers[mhw::kPanelCount];
    StubSource kids[2];
    HudCanvas v;
    v.resize(1180, 820);
    v.show();
    pump();
    v.setConsoleFidelityMode(true);
    v.setSelectedPanel(0);
    bindMothers(v, mothers);
    bindChildren(v, kids);
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Mixed);
    pump();

    // FULL extent.
    mothers[0].content = QSize(420, 160);
    pump();
    const QImage fullImg = v.grab().toImage();
    const QRectF full = v.motherWindowRects().at(0);
    const int magFull = countIf(fullImg, isMagenta);
    check(full.width() > 0 && full.height() > 0 && magFull > 1000,
          "Mixed: a full-extent mother paints its full body");

    // HALF extent — the console reporting "half the rows stay": the box
    // must lose exactly the height and keep the width (a full-width row
    // stacks vertically; that IS what contentSize() answers).
    mothers[0].content = QSize(420, 80);
    pump();
    const QImage halfImg = v.grab().toImage();
    const QRectF half = v.motherWindowRects().at(0);
    const int magHalf = countIf(halfImg, isMagenta);
    check(std::abs(half.height() * 2.0 - full.height()) < 1.0,
          "the retained-extent mother's box is EXACTLY half the height");
    check(half.width() == full.width(),
          "…with the width unchanged — the shrink is the row axis, no "
          "second geometry path");
    check(magHalf < magFull && magHalf > 0,
          "…and the PAINTED body shrank with it (fewer magenta pixels, "
          "still painted — no dropped rows)");
    check(countIf(halfImg, isCyan) == countIf(fullImg, isCyan),
          "the children did not change while the mother shrank");

    // ZERO extent — every row detached: no shell at all, children intact.
    mothers[0].content = QSize(0, 0);
    pump();
    const QImage emptyImg = v.grab().toImage();
    const QRectF reset = v.motherWindowRects().at(0);
    check(countIf(emptyImg, isMagenta) == 0,
          "an all-rows-detached mother paints NO body (no empty shell)");
    check(reset.isEmpty() && reset.left() == 0 && reset.top() == 0,
          "…and leaves no hit rect behind (lastTarget_ stays at its "
          "paint-time reset — not a zero-size box at a live position)");
    check(countIf(emptyImg, isCyan) > 1000,
          "…while its children keep painting — nothing falls off the "
          "board with the mother");

    // THE CONTROL: Single, same zero-extent source, no bindings (they
    // were cleared by the mode switch) — the LEGACY shell path must be
    // untouched: a positioned rect and a painted selection tag.
    v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    pump();
    const QImage singleImg = v.grab().toImage();
    const QRectF shell = v.motherWindowRects().at(0);
    check(!shell.isEmpty() && (shell.left() > 0 || shell.top() > 0),
          "CONTROL: in Single the same zero-extent source still takes "
          "the legacy shell path (positioned rect) — the guard is "
          "Mixed-only");
    // The tag the shell path paints sits 18px above that rect; count the
    // pixels the shell has there that the guarded Mixed pass did not.
    const QRect tagBand(qRound(shell.left()) - 4, qRound(shell.top()) - 22,
                        160, 20);
    int tagPixels = 0;
    for (int y = std::max(0, tagBand.top());
         y <= std::min(singleImg.height() - 1, tagBand.bottom()); ++y) {
        for (int x = std::max(0, tagBand.left());
             x <= std::min(singleImg.width() - 1, tagBand.right()); ++x) {
            if (singleImg.pixel(x, y) != emptyImg.pixel(x, y))
                ++tagPixels;
        }
    }
    check(tagPixels > 20,
          "…and it really paints the tag chrome the Mixed guard skips "
          "(>20 differing pixels inside the tag band)");
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

    std::printf("== hud_canvas_mixed_tests ==\n");
    testMixedCoexistenceFidelity();
    testMixedCoexistenceBare();
    testMotherShrinksWithRetainedRows();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
