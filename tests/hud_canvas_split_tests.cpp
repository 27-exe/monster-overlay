// SPDX-License-Identifier: Apache-2.0
//
// v0.13: the split-mode preview's two halves, pinned.
//
// WHAT THIS FILE COVERS
// ---------------------
// The user's requirement was 「每一个面板都有两种模式 … 注意控制台的预览也要同步」
// — the console's HUD canvas must describe the same window topology the
// overlay will start in, and Single mode must not move while doing it.
// That is two claims and this file pins both:
//
//   1. THE CANVAS (HudCanvas::setLayoutMode / setSubWindowBindings /
//      setSubWindowPixmap / subWindowCount). The four fixed `slots_` stay
//      the Single-mode renderer; the split's sub-windows arrive in a
//      separate vector that is EMPTY in Single mode. The emptiness is
//      asserted directly, and again after a Split→Single round trip,
//      because that is the pixel-identity guarantee stated as a fact a
//      test can see.
//
//   2. THE SIZE CONTRACT. Each sub-window box is measured from the
//      window's own contentWidth()/contentHeight(), never re-derived:
//      the boxes asserted here come from four real split windows, and
//      the test asks the window for the number it would draw with
//      rather than restating it. A mutation that guesses a size fails
//      here.
//
// THE MUTATION TEST. The task requires proving the Single-mode guard is
// load-bearing: break `setLayoutMode(Single)` so it keeps the sub-slots
//      (i.e. make Single behave like Split), confirm this file FAILS,
//      then restore. The real output of that run is in the commit's
//      report.
//
// WHY THIS LINKS mhw-ui RATHER THAN COMPILING THE PANELS: the fourteen
// split windows carry Q_OBJECT + AUTOMOC, and compiling their sources
//      into a second target would run moc twice over the same headers
//      (the same reason panel_capabilities_tests.cpp links mhw-ui).

#include "ui/hud_canvas.h"

#include "ui/panel.h"
#include "ui/panel_player.h"
#include "ui/panel_monster.h"
#include "ui/panel_player_split.h"
#include "ui/panel_player_split_mantles.h"
#include "ui/panel_player_split_weapon.h"
#include "ui/panel_monster_split_hp.h"
#include "ui/panel_damage_split_share.h"
#include "ui/panel_sections.h"
#include "ui/panel_player_split_layout.h"
#include "ui/panel_source.h"
#include "ui/ui_theme.h"

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
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

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

// Prime a window the way the split consistency tests and the damage
// window tests do: setEditMode plus a repaint is enough for the base
// class's setupDemoData() to run, which is what gives the content-model
// windows their demo rows and therefore their real width.
template <class P>
P &prime(P &p)
{
    p.setEditMode(true);
    p.resetDemoPrimed();
    p.setAttribute(Qt::WA_DontShowOnScreen);
    p.show();
    for (int i = 0; i < 4; ++i) {
        p.repaint();
        QApplication::processEvents();
    }
    return p;
}

// One real split window, held alive for the whole test. The type is a
// template parameter so each window below gets its own storage rather
// than slicing through a Panel* (the point of this test is the window's
// OWN contentWidth()/contentHeight()).
template <class P>
struct LiveWindow {
    P window;
    PanelSourceAdapter source;
    explicit LiveWindow()
        : window()
        , source(nullptr) {}
    void prime()
    {
        ::prime(window);
        source = PanelSourceAdapter(&window);
    }
};

// ─── §1. The tables, and what they say the preview must show ──────────
//
// The three tables in ui/panel_sections.h are the source of truth for
// "which blocks became windows". The preview derives its sub-window set
// from them, so their row counts are the bound the canvas's vector must
// respect — not a number this file invents.
void testTableDerivation()
{
    check(mhw::PlayerSection::kPlayerSplitTargetCount == 6,
          "kPlayerSplitTargets has 6 rows (one per player sub-window)");
    // v0.13: the monster table was regrouped from five single-block rows
    // to two, on the user's verdict that the split was「切太狠了」 — the
    // portrait/enrage/HP glance belongs together, and the ailments/parts
    // plan belongs together. Fewer windows, each with more in it.
    check(mhw::kMonsterSplitTargetCount == 2,
          "kMonsterSplitTargets has 2 rows (one per monster sub-window)");
    // v0.15: 3, not 2. The per-player roster became a damage sub-window so
    // the live overlay and the console preview would stop disagreeing —
    // the preview had been building a rows window the overlay never
    // mounted, and the overlay was drawing those rows inside DamagePanel.
    //
    // Spelled from the three constants rather than as a literal total, so
    // the same number is not written in two places that can drift.
    check(mhw::DamageSplitSlot::kCount == 3,
          "kDamageSplitTargets has 3 rows (share, chart, and the per-player "
          "roster)");
    check(mhw::PlayerSection::kPlayerSplitTargetCount
              + mhw::kMonsterSplitTargetCount
              + mhw::DamageSplitSlot::kCount == 11,
          "the three tables' rows sum to 11 sub-windows");

    // Every row must claim at least one bit. A row that claims nothing
    // would produce a preview box for a window that draws nothing, and
    // would say so with no i18n key and no paint to back it up.
    bool allRowsClaim = true;
    for (int i = 0; i < mhw::PlayerSection::kPlayerSplitTargetCount; ++i)
        if (mhw::PlayerSection::kPlayerSplitTargets[i].mask == 0)
            allRowsClaim = false;
    for (int i = 0; i < mhw::kMonsterSplitTargetCount; ++i)
        if (mhw::kMonsterSplitTargets[i].mask == 0)
            allRowsClaim = false;
    for (int i = 0; i < mhw::DamageSplitSlot::kCount; ++i)
        if (mhw::kDamageSplitTargets[i].mask == 0)
            allRowsClaim = false;
    check(allRowsClaim, "every split-table row claims at least one section bit");

    // The Single arm of each *PanelMaskFor is `return mask` verbatim.
    // That is the guarantee the 10 single-mode pixel baselines rest on,
    // and it is asserted here next to the canvas's own Single arm so
    // the two halves of "single mode is untouched" are in one place.
    constexpr uint32_t kProbe = 0xDEADBEEFu;
    check(mhw::PlayerSection::playerPanelMaskFor(
              mhw::PlayerSection::PlayerLayoutMode::Single, kProbe) == kProbe,
          "playerPanelMaskFor(Single, mask) returns mask verbatim");
    check(mhw::monsterPanelMaskFor(
              mhw::PlayerSection::PlayerLayoutMode::Single, kProbe) == kProbe,
          "monsterPanelMaskFor(Single, mask) returns mask verbatim");
    check(mhw::damagePanelMaskFor(
              mhw::PlayerSection::PlayerLayoutMode::Single, kProbe) == kProbe,
          "damagePanelMaskFor(Single, mask) returns mask verbatim");
}

// ─── §2. Single mode leaves the canvas's sub-slot vector empty ────────
//
// THE PIXEL-IDENTITY GUARANTEE, as an assertion. The four `slots_` are
// untouched by every call below (they are a different member, and the
// split arm is gated on the vector being non-empty); the claim that
// matters is that nothing populates the vector in Single mode.
void testSingleModeLeavesSubSlotsEmpty()
{
    HudCanvas canvas;

    // A canvas nobody ever told about the split has no sub-windows.
    check(canvas.subWindowCount() == 0,
          "a default canvas has 0 sub-windows (Single is the default)");
    check(canvas.layoutMode() == mhw::PlayerSection::PlayerLayoutMode::Single,
          "a default canvas is in Single mode");

    // Setting Single explicitly — the no-op path a restart takes — must
    // not invent anything.
    canvas.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    check(canvas.subWindowCount() == 0,
          "setLayoutMode(Single) on a Single canvas keeps 0 sub-windows");

    // An explicit Split, then back to Single. This is the round trip a
    // user flipping the combo produces, and the Single half is the one
    // the guarantee depends on.
    LiveWindow<PlayerStatusStripPanel> status;
    LiveWindow<MonsterHpStripPanel> hp;
    status.prime();
    hp.prime();

    QVector<HudCanvas::SubWindowBinding> bindings;
    HudCanvas::SubWindowBinding a;
    a.src = &status.source;
    a.parentSlot = 0;
    a.tableRow = 0;
    HudCanvas::SubWindowBinding b;
    b.src = &hp.source;
    b.parentSlot = 1;
    b.tableRow = 0;
    bindings.append(a);
    bindings.append(b);

    canvas.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Split);
    canvas.setSubWindowBindings(bindings);
    check(canvas.subWindowCount() == 2,
          "Split mode with two bindings yields 2 sub-windows");

    canvas.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    check(canvas.subWindowCount() == 0,
          "Split -> Single clears every sub-window (the pixel guard)");
}

// ─── §3. One sub-window per table row, in table order ─────────────────
//
// A canvas told the full table-row set must hold exactly that many sub-windows,
// because the split's window count is a property of the tables and not
// of anything the preview chose. The derivation itself (which row maps
// to which class) is in control_panel.cpp; what is pinned here is the
// CANVAS's side of the contract — that it never invents, drops or
// reorders a binding it was given.
void testSubWindowCountMatchesTableRows()
{
    HudCanvas canvas;

    // Thirteen copies of one window's adapter would do; what is under
    // test is the canvas's accounting, not the windows. One real window
    // supplies a real PanelSource.
    LiveWindow<PlayerStatusStripPanel> status;
    status.prime();

    QVector<HudCanvas::SubWindowBinding> bindings;
    // The 13 rows in the tables' order: player 0..5, monster 0..4,
    // damage 0..1. parentSlot/tableRow mirror what control_panel.cpp's
    // build table passes.
    const int kPlayerRows = mhw::PlayerSection::kPlayerSplitTargetCount;
    const int kMonsterRows = mhw::kMonsterSplitTargetCount;
    const int kDamageRows = mhw::DamageSplitSlot::kCount;
    for (int row = 0; row < kPlayerRows; ++row) {
        HudCanvas::SubWindowBinding b;
        b.src = &status.source;
        b.parentSlot = 0;
        b.tableRow = row;
        bindings.append(b);
    }
    for (int row = 0; row < kMonsterRows; ++row) {
        HudCanvas::SubWindowBinding b;
        b.src = &status.source;
        b.parentSlot = 1;
        b.tableRow = row;
        bindings.append(b);
    }
    for (int row = 0; row < kDamageRows; ++row) {
        HudCanvas::SubWindowBinding b;
        b.src = &status.source;
        b.parentSlot = 2;
        b.tableRow = row;
        bindings.append(b);
    }

    canvas.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Split);
    canvas.setSubWindowBindings(bindings);
    // The count is the three tables' sizes, never a literal: a literal in a
    // test like this one does not move when a table grows, and a test that
    // quietly checks the old number is worse than no test.
    check(canvas.subWindowCount() == kPlayerRows + kMonsterRows + kDamageRows,
          "one sub-window per split-table row, counted from the tables");
    check(canvas.subWindowCount() == 11,
          "the bound is 11 = 6 player + 2 monster + 3 damage");

    // A null source in the list is a row whose window did not resolve.
    // It must not become a box: a null dereference in the paint path is
    // a crash, and a blank box is a lie about what the overlay draws.
    HudCanvas::SubWindowBinding nullRow;
    nullRow.src = nullptr;
    nullRow.parentSlot = 0;
    nullRow.tableRow = 3;
    bindings.append(nullRow);
    canvas.setSubWindowBindings(bindings);
    // 11 live bindings + 1 null -> still 11. The count before the null was
    // appended is the number to compare against, so it is computed rather
    // than written: a literal here is how this assertion went stale when
    // DamageSplitSlot::kCount moved from 2 to 3.
    check(canvas.subWindowCount() == kPlayerRows + kMonsterRows + kDamageRows,
          "a binding with a null source is skipped, not counted");

    // A re-set replaces rather than appends: the caller re-derives the
    // whole set each time, and appending would draw a window whose
    // parent is gone.
    QVector<HudCanvas::SubWindowBinding> justOne;
    HudCanvas::SubWindowBinding only;
    only.src = &status.source;
    only.parentSlot = 2;
    only.tableRow = 1;
    justOne.append(only);
    canvas.setSubWindowBindings(justOne);
    check(canvas.subWindowCount() == 1,
          "a second setSubWindowBindings() REPLACES the previous set");
}

// ─── §4. The size each box is measured at ─────────────────────────────
//
// THE v0.12 CONTRACT, AND WHICH NUMBER IS THE HONEST ONE.
//
// A sub-window box's extent is read through PanelSource — the same
// read-only interface the four base panels' rectangles use, and the
// same one the live overlay's geometry comes from. That interface
// exposes contentSize(), which is the window's logicalSize_: the value
// setContentSize() recorded after it applied the user's FLOOR and LOCK
// on top of the content's own contentWidth()/contentHeight(). So
// reading contentSize() is reading the window's contract rather than
// guessing, AND it is the number that survives a user's "=" lock —
// which a raw contentWidth() read would silently ignore.
//
// (PanelSource was audited as needing no extension for this: it has no
// contentWidth/contentHeight/contentMargin accessors and none is added
// here. Every assertion below goes through contentSize() plus the
// window's own answers, so the two are cross-checked rather than one
// being trusted.)
//
// This file asserts the identity that ties the two together, over four
// windows chosen to span the split's range: a pill grid whose width is
// text-driven, a bar whose width is fixed, a two-slot row, and a chart.
void testSubWindowSizesComeFromTheWindow()
{
    LiveWindow<PlayerStatusStripPanel> status;
    LiveWindow<PlayerMantlesStripPanel> mantles;
    LiveWindow<DamageShareStripPanel> share;
    LiveWindow<MonsterHpStripPanel> hp;
    status.prime();
    mantles.prime();
    share.prime();
    hp.prime();

    struct Case {
        const char *name;
        Panel *window;
        PanelSourceAdapter *source;
    };
    const Case cases[] = {
        {"player-status (pill grid)", &status.window, &status.source},
        {"player-mantles (two slots)", &mantles.window, &mantles.source},
        {"damage-share (fixed bar)", &share.window, &share.source},
        {"monster-hp (bar + labels)", &hp.window, &hp.source},
    };

    bool allPositive = true;
    bool identityHolds = true;
    for (const Case &c : cases) {
        const int cw = c.window->contentWidth();
        const int ch = c.window->contentHeight();
        const QSize bound = c.source->contentSize();
        if (cw <= 0 || ch <= 0 || bound.isEmpty()) {
            allPositive = false;
            std::printf("  (non-positive: %s content %dx%d, bound %dx%d)\n",
                        c.name, cw, ch, bound.width(), bound.height());
        }
        // THE IDENTITY. contentSize() is the content's number with the
        // floor raised and the lock substituted: exactly
        //     max(contentWidth, minWidth)   with lockWidth overriding,
        //     max(contentHeight, minHeight) with lockHeight overriding.
        // panel_sizing.h spells this as effectiveSize(). A preview that
        // used contentWidth() alone would draw a SHORTER box than the
        // window whenever the user raised a floor, and a WIDER one the
        // moment they set a lock — so the identity, not the content
        // number, is what makes the box the same shape as the window.
        const int wantW = c.window->lockWidth() > 0
                              ? c.window->lockWidth()
                              : std::max(cw, c.window->minWidth());
        const int wantH = c.window->lockHeight() > 0
                              ? c.window->lockHeight()
                              : std::max(ch, c.window->minHeight());
        // The user has dialled nothing in this test, so the identity
        // should read content == bound exactly. If it does not, the two
        // halves of the sizing contract have drifted apart, which is
        // what made the "0.11.4 preview lies" class of bug.
        if (bound.width() != wantW || bound.height() != wantH) {
            identityHolds = false;
            std::printf("  (identity broke for %s: bound %dx%d, expected %dx%d)\n",
                        c.name, bound.width(), bound.height(), wantW, wantH);
        }
        std::printf("  %-28s content %dx%d  bound %dx%d  margin %d\n",
                    c.name, cw, ch, bound.width(), bound.height(),
                    c.window->contentMargin());
    }
    check(allPositive,
          "every split window answers a positive extent through the "
          "PanelSource it binds");
    check(identityHolds,
          "bound contentSize == the window's floor/lock-adjusted content "
          "extent (the v0.12 sizing identity)");

    // status and mantles are both player windows but they are NOT the
    // same size: a pill grid is text-driven, a two-slot row is not. If
    // the preview had one hardcoded sub-window size, these two would
    // collide — and the assertion below is what proves it cannot.
    check(status.source.contentSize() != mantles.source.contentSize(),
          "two different split windows have different extents "
          "(a single hardcoded box would hide that)");

    // The one deviation found by this test, pinned so it cannot be
    // mistaken for intended behaviour:
    //
    //   PlayerWeaponStripPanel does NOT override contentMargin(), so it
    //   answers the base class's 0 while every other split window
    //   answers mhw::split_layout::kSplitMargin (11). Its own paint uses
    //   a local margin constant instead.
    //
    // This is a GAP IN panel_player_split_weapon.h (the split's shared
    // rhythm header documents that all of them must answer
    // kSplitMargin), and tests/split_layout_consistency_tests.cpp does
    // not cover that window — its five-window list omits it, which is
    // how the missing override survived. The preview is unaffected: it
    // reads contentSize(), which this window does answer correctly.
    //
    // FIXED in the tree this test now runs against: the parent session added
    // the override, which had been missing and had made the console's
    // preview draw this window flush to its own edge while every sibling
    // sat 11px in. The consistency test's five-window list is what let it
    // through — there were six windows.
    //
    // The check below is the corrected form of the original, which asserted
    // the WRONG value (== 0) and FAILed by construction. A test that
    // documents a bug by asserting the bug is only useful until somebody
    // fixes it; then it fails and looks exactly like a regression. So the
    // assertion now states what the contract says.
    LiveWindow<PlayerWeaponStripPanel> weapon;
    weapon.prime();
    check(weapon.window.contentMargin() == mhw::split_layout::kSplitMargin,
          "weapon window: contentMargin() == kSplitMargin — it answers the "
          "shared rhythm like the other six instead of the base's 0");
    check(mhw::split_layout::kSplitMargin == 11,
          "the split's shared margin constant is 11, the value every "
          "window answers");
    check(weapon.source.contentSize().width() > 0,
          "the weapon window answers a positive extent, so its preview "
          "box is drawn from real content rather than a placeholder");
}

// ─── §5. Pixmap plumbing, and out-of-range indices ────────────────────
//
// setSubWindowPixmap() addresses by the index the matching binding was
// passed at, and must IGNORE an unknown index rather than growing the
// vector — a growth would renumber every later sub-window's binding,
// so a mask change would paint player mantles where player bars was.
void testSubWindowPixmapPlumbing()
{
    HudCanvas canvas;
    LiveWindow<PlayerStatusStripPanel> status;
    LiveWindow<MonsterHpStripPanel> hp;
    status.prime();
    hp.prime();

    QVector<HudCanvas::SubWindowBinding> bindings;
    HudCanvas::SubWindowBinding a;
    a.src = &status.source;
    a.parentSlot = 0;
    a.tableRow = 0;
    HudCanvas::SubWindowBinding b;
    b.src = &hp.source;
    b.parentSlot = 1;
    b.tableRow = 0;
    bindings.append(a);
    bindings.append(b);

    canvas.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Split);
    canvas.setSubWindowBindings(bindings);

    // These must be no-ops, not growth.
    canvas.setSubWindowPixmap(-1, QPixmap(10, 10), true);
    canvas.setSubWindowPixmap(2, QPixmap(10, 10), true);
    canvas.setSubWindowPixmap(99, QPixmap(10, 10), true);
    check(canvas.subWindowCount() == 2,
          "an out-of-range setSubWindowPixmap() does not grow the vector");

    // A real pixmap at a real index is accepted (and a paint must not
    // crash with pixmaps present).
    QPixmap pix(64, 24);
    pix.fill(QColor(40, 44, 48));
    canvas.setSubWindowPixmap(0, pix, true);
    canvas.setSubWindowPixmap(1, pix, false);
    check(canvas.subWindowCount() == 2,
          "a valid setSubWindowPixmap() keeps the count");

    // Bound the four main panels, so a paint with sub-windows AND main
    // panels is exercised — the same combination the console produces.
    LiveWindow<PlayerPanel> player;
    LiveWindow<MonsterPanel> monster;
    player.prime();
    monster.prime();
    canvas.bindPanel(0, &player.source);
    canvas.bindPanel(1, &monster.source);
    canvas.setPanelPixmap(0, pix, true);
    canvas.setPanelPixmap(1, pix, true);
    canvas.resize(960, 600);
    canvas.show();
    QCoreApplication::processEvents();
    {
        QPixmap surface(canvas.size());
        canvas.render(&surface);
        check(!surface.isNull() && !surface.size().isEmpty(),
              "a Split-mode paint with 2 main panels and 2 sub-windows "
              "renders without failing");
    }

    // And the Single round trip again, with pixmaps in hand, because
    // the guard has to hold after real content has been pushed.
    canvas.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Single);
    check(canvas.subWindowCount() == 0,
          "Single after a fully-populated Split still has 0 sub-windows");
}

} // namespace

// ─── §7. v1.0.0: THE CONSOLE-FIDELITY BOARD ──────────────────────────────
//
// The frozen `reference.html` is the only authority for what the
// console's board looks like, and this section pins the five things the
// flag `setConsoleFidelityMode()` turns on: the board's ground, its
// border and inset band, its width cap and aspect ratio, its lattice in
// the edit state, and the two window-visibility filters it consumes.
//
// IT ALSO PINS THE NEGATIVE. The hard boundary for this whole change is
// that a canvas nobody put into fidelity mode paints exactly what it
// painted before the flag existed, and the assertions below check that
// directly (round-trip pixel identity, and "the legacy colours are
// absent from the fidelity canvas") rather than trusting that every
// branch is a short-circuit.
//
// MEASURED, NOT RECOMPUTED. Every geometry assertion reads the frame
// back OUT of the rendered pixels — the border hairline is the only
// thing on the board painted in --board-line at rest and the only thing
// painted in --clay while editing, so its extreme pixels ARE the frame.
// Recomputing `min(width-28, 1050)` from the widget size and comparing
// it to itself would prove nothing; measuring means the assertions hold
// whatever screen the offscreen platform reports, and it is the same
// number the user sees.
namespace {

// A PanelSource whose every number is under the test's control. The
// file's earlier sections drive a REAL Panel through PanelSourceAdapter
// — right for a drag test, wrong here: these checks are about the
// board's own chrome, and a live Panel brings a live theme, a live
// layout mode and a live persisted margin with it.
struct StubSource final : PanelSource
{
    Corner c{Corner::TopLeft};
    QMargins m{120, 90, 0, 0};
    QSize content{420, 160};
    double z{1.0};
    // `false` for the four base panels the single-mode board draws, and
    // `true` for a split sub-window. The canvas gates the mother panel's
    // hit-test target on this (hud_canvas.cpp's "the base panel's blocks
    // have moved into sub-windows"), so a base panel that answered true
    // would be painted but never clickable — which would quietly break
    // the panel-filter checks below.
    bool sub{true};
    Corner corner() const override { return c; }
    QMargins margins() const override { return m; }
    QSize contentSize() const override { return content; }
    double scale() const override { return z; }
    bool isSubWindow() const override { return sub; }
};

// The tokens, spelled out. Duplicated from hud_canvas.cpp on purpose —
// a test that imports the implementation's constants cannot notice the
// implementation drifting from the HTML, and the HTML's own values are
// short enough to write twice.
const QColor kPageGroundDark(0x18, 0x18, 0x16);   // --page, dark
const QColor kBoardGround(0x10, 0x10, 0x10);      // --board
const QColor kBoardLine(0x2e, 0x2e, 0x2a);        // --board-line
const QColor kBoardClayDark(0xd9, 0x77, 0x57);    // --clay, dark
const QColor kBoardClayLight(0x9c, 0x46, 0x23);   // --clay, light
const QColor kLegacyGround(6, 8, 10);             // the old stage's field
const QColor kLegacyFrameBright(34, 50, 42);      // its gradient's top stop

int countExact(const QImage &img, const QColor &c)
{
    const QRgb want = c.rgb();
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixel(x, y) == want)
                ++n;
    return n;
}

// The border rectangle, read back off the pixels.
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

// Where the lattice lines actually landed on one scanline: the centres
// of every run of non-ground pixels. Returning CENTRES rather than raw
// hits means a line the painter straddled across two pixels still reads
// as one line, which is what the browser would show too.
std::vector<qreal> gridLineCentres(const QImage &img, int y, int x0, int x1,
                                   const QColor &ground)
{
    const QRgb g = ground.rgb();
    std::vector<qreal> out;
    int runStart = -1;
    for (int x = x0; x <= x1; ++x) {
        const bool hit = img.pixel(x, y) != g;
        if (hit && runStart < 0)
            runStart = x;
        if (!hit && runStart >= 0) {
            out.push_back((runStart + x - 1) / 2.0);
            runStart = -1;
        }
    }
    if (runStart >= 0)
        out.push_back((runStart + x1) / 2.0);
    return out;
}

// ── the traffic the canvas reacts to ───────────────────────────────────
//
// Delivered with sendEvent rather than through a real window system: the
// canvas does not care whether a press came from a device, and
// synthesising it keeps these checks runnable on the offscreen platform
// with no window grab or focus dance.
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

void key(QWidget *w, int k, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent e(QEvent::KeyPress, k, mods);
    QApplication::sendEvent(w, &e);
    pump();
}

// How much of the test pixmap actually reached the canvas.
//
// THE THRESHOLDS ARE DELIBERATELY LOOSE, and the reason is the canvas's
// own selection dimming: a mother panel that is not the selected one is
// drawn at 55% opacity (hud_canvas.cpp's `selDim`), so a solid magenta
// arrives as (140,0,140) rather than (255,0,255). Counting only the full
// strength colour would report exactly ONE panel on a board that is
// showing four — which is how these thresholds were chosen.
int countMagenta(const QImage &img)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = img.pixel(x, y);
            const int r = qRed(px), g = qGreen(px), b = qBlue(px);
            if (r > 100 && g < 40 && qAbs(r - b) < 10)
                ++n;
        }
    }
    return n;
}

// The viewport padding `.board-viewport` declares: 12px top, 14px each
// side, 0 bottom.
constexpr int kFidPadX = 14;
constexpr int kFidPadTop = 12;
constexpr qreal kFidCapLandscape = 1050.0;

void sectionFidelityBoard()
{
    std::printf("\n=== §7 console-fidelity board ===\n");
    const QColor magenta(255, 0, 255);
    QPixmap magentaPm(QSize(200, 80));
    magentaPm.fill(magenta);

    // ── 7.1 the flag itself, and the round trip ─────────────────────────
    setUiTheme(true);
    // TALL ENOUGH that the board fits. The offscreen platform reports a
    // small square screen, and `.board` is width-driven — so a 820px
    // canvas would clip a board taller than itself and the height
    // assertions below would be measuring the clip, not the mapping.
    HudCanvas c;
    c.resize(1180, 1400);
    c.show();
    pump();
    const int cw = c.rect().width();
    const int ch = c.rect().height();
    // The output the board describes. The canvas resolves an EMPTY
    // preview screen to the primary, so the primary's geometry is the
    // number the board is being asked to reproduce — and it is reached
    // through the public screen API rather than through the canvas's
    // own private resolver.
    const QScreen *qs = QGuiApplication::primaryScreen();
    check(qs != nullptr, "the offscreen platform reports a screen");
    const QSize phys = qs ? qs->size() : QSize(1600, 1200);
    const qreal ar = phys.height() / qreal(phys.width());
    const int capW = qRound(kFidCapLandscape);
    const int expW = qMin(cw - 2 * kFidPadX, capW);
    const int expH = qRound(expW * ar);

    check(!c.consoleFidelityMode(),
          "the fidelity flag defaults to false on a fresh canvas");

    const QImage legacy = c.grab().toImage();
    c.setConsoleFidelityMode(true);
    pump();
    const QImage fid = c.grab().toImage();
    c.setConsoleFidelityMode(false);
    pump();
    const QImage restored = c.grab().toImage();
    check(legacy != fid, "fidelity on repaints a different board");
    check(legacy == restored,
          "fidelity off restores the legacy canvas pixel for pixel");

    // ── 7.2 what the fidelity board replaced ───────────────────────────
    check(legacy.pixelColor(0, 0) == kLegacyGround,
          "the legacy canvas is #06080a at its corner");
    check(fid.pixelColor(0, 0) == kPageGroundDark,
          "the fidelity canvas is --page at its corner");
    check(countExact(fid, kLegacyGround) == 0,
          "the fidelity board contains no #06080a ground at all");
    check(countExact(fid, kLegacyFrameBright) == 0,
          "and no trace of the legacy frame's green gradient either");
    check(countExact(fid, kBoardGround) > 0,
          "the fidelity board contains --board #101010");

    // ── 7.3 the frame: cap, aspect ratio, centring ─────────────────────
    c.setConsoleFidelityMode(true);
    pump();
    const QImage fidOn = c.grab().toImage();
    const QRect fr = frameFromBorder(fidOn, kBoardLine);
    std::printf("   screen %dx%d (ar %.5f)  canvas %dx%d\n"
                "   measured frame (%d,%d) %dx%d   expected %dx%d\n",
                phys.width(), phys.height(), ar, cw, ch,
                fr.left(), fr.top(), fr.width(), fr.height(), expW, expH);
    check(!fr.isEmpty(), "the fidelity board's hairline is --board-line");
    check(fr.width() == expW,
          "board width is min(padded viewport, 1050) — the CSS cap holds");
    check(fr.height() == expH,
          "board height is its own width times the OUTPUT's aspect ratio");
    check(std::abs(fr.height() / qreal(fr.width()) - ar) < 0.004,
          "board aspect ratio equals the output's, not a hardcoded 16:9");
    check(fr.left() == qRound((cw - expW) / 2.0),
          "board is horizontally centred in the 14px-padded viewport");
    check(std::abs(fr.center().y() - (kFidPadTop + (ch - kFidPadTop) / 2.0))
              <= 1.0,
          "board is vertically centred in the viewport once its 12px pad");
    check(fr.top() >= kFidPadTop,
          "and never starts above that 12px pad");

    // Below the cap the board must follow the widget.
    {
        HudCanvas narrow;
        narrow.resize(560, 400);
        narrow.setConsoleFidelityMode(true);
        narrow.show();
        pump();
        const QRect fr2 =
            frameFromBorder(narrow.grab().toImage(), kBoardLine);
        check(fr2.width() == qMin(narrow.rect().width() - 2 * kFidPadX,
                                  capW),
              "below the cap the board width follows the widget");
        check(fr2.top() == kFidPadTop,
              "and the board still keeps its 12px top pad");
    }

    // ── 7.4 the inset band, and the NATURAL-OVERFLOW contract ──────────
    // `.board { box-shadow: inset 0 0 0 12px rgba(255,255,255,.015) }` —
    // a 12px neutral wash INSIDE the border, over the board ground.
    const QColor band = fidOn.pixelColor(fr.left() + 5, fr.center().y());
    const QColor inner = fidOn.pixelColor(fr.left() + 14, fr.center().y());
    check(band.red() == band.green() && band.green() == band.blue()
              && band.red() > kBoardGround.red() && band.red() < 40,
          "the resting inset band is a light neutral wash over --board");
    check(inner == kBoardGround,
          "inside the 12px band the board is plain --board #101010");

    // THE POINT OF THE WHOLE GEOMETRY: the board never shrinks to fit.
    // The frozen page lets the ROOT grow — measured at a 1600x1000
    // viewport it comes out 1134.9375 tall for the landscape display and
    // 1742.3125 for the portrait one, with the board still exactly
    // 1050x590.625 / 560x995.546875. So a widget shorter than the board
    // must keep the board at its CSS size and lose only the part it
    // cannot show, rather than compressing the board and calling that a
    // match — and the height the container needs is handed to it by
    // sizeHint() / heightForWidth() below.
    {
        HudCanvas squeezed;
        squeezed.resize(cw, 420);
        squeezed.setConsoleFidelityMode(true);
        squeezed.show();
        pump();
        const QRect frS =
            frameFromBorder(squeezed.grab().toImage(), kBoardLine);
        check(frS.width() == expW,
              "a widget shorter than the board does NOT shrink the board "
              "— the CSS width survives and the container must scroll");
        check(frS.top() == kFidPadTop,
              "and the board keeps its top edge instead of clipping it");
        check(c.heightForWidth(cw) == kFidPadTop + expH,
              "heightForWidth hands the container the height the board "
              "actually needs at this width");
        check(c.sizeHint() == QSize(capW + 2 * kFidPadX, kFidPadTop + expH),
              "sizeHint is the natural size of the board plus its pad");
    }

    // ── 7.5 the edit state: border, ring, band, lattice ────────────────
    setUiTheme(true);
    pump();
    {
        HudCanvas e;
        e.resize(1180, 820);
        e.setConsoleFidelityMode(true);
        e.setEditing(true);
        e.show();
        pump();
        const QImage dark = e.grab().toImage();
        // `box-shadow:0 0 0 1px var(--clay)` puts a SECOND clay ring one
        // pixel OUTSIDE the border, so the clay pixels on an editing
        // board span the border plus the ring — 1052 not 1050 — and that
        // is itself the assertion. `fed.left()+1` is the border.
        const QRect fed = frameFromBorder(dark, kBoardClayDark);
        std::printf("   dark edit frame (%d,%d) %dx%d\n",
                    fed.left(), fed.top(), fed.width(), fed.height());
        check(fed.width() == expW + 2,
              "the outer ring is exactly 1px beyond the border on both "
              "sides (0 0 0 1px var(--clay))");
        check(dark.pixelColor(fed.left(), fed.center().y())
                  == kBoardClayDark,
              "that outermost pixel is --clay — the ring, not the border");
        check(frameFromBorder(dark, kBoardLine).isEmpty(),
              "in the edit state no --board-line is left anywhere — the "
              "border itself went to --clay");
        // THE SUBTLETY: the inset band is a hardcoded rgba(217,119,87,.06)
        // in the CSS, NOT var(--clay) — so it stays warm even when the
        // theme has moved --clay to #9c4623.
        const QColor eband =
            dark.pixelColor(fed.left() + 6, fed.center().y());
        check(eband.red() > eband.green() && eband.green() > eband.blue(),
              "the edit-mode inset band is warm — rgba(217,119,87,.06), "
              "a literal rather than var(--clay)");
        check(eband != kBoardClayDark,
              "and it is NOT simply the border colour reused");

        // The lattice: `inset:12px`, `opacity:.28`, `background-size:5% 5%`
        // → 20 lines across the INSET rect, at a 6/255 alpha.
        // `fed` spans the ring, so the board itself starts one pixel in.
        const int bl = fed.left() + 1, bt = fed.top() + 1;
        const QRect g(bl + kFidPadTop, bt + kFidPadTop,
                      fed.width() - 2 - 2 * kFidPadTop,
                      fed.height() - 2 - 2 * kFidPadTop);
        const int scanY = g.top() + 20;   // below the label, above centre
        const std::vector<qreal> lines =
            gridLineCentres(dark, scanY, g.left(), g.right(), kBoardGround);
        check(lines.size() >= 19 && lines.size() <= 21,
              "the edit lattice paints 20 lines across the inset rect");
        int maxRed = 0;
        for (int x = g.left(); x <= g.right(); ++x)
            maxRed = qMax(maxRed, dark.pixelColor(x, scanY).red());
        check(maxRed <= kBoardGround.red() + 8,
              "the lattice line is a ~6/255 wash — rgba(255,255,255,.08) "
              "at opacity .28, not a full-strength line");
        if (lines.size() >= 2) {
            const qreal pitch = g.width() / 20.0;
            qreal worst = 0;
            for (size_t i = 1; i < lines.size(); ++i)
                worst = std::max(worst,
                                 std::abs((lines[i] - lines[i - 1]) - pitch));
            check(worst <= 1.0,
                  "the lattice pitch is 5% of the inset rect, and it "
                  "stays 5% to the last cell");
        } else {
            check(false, "the lattice pitch is 5% of the inset rect");
        }

        // The SAME board in the light theme: --clay is #9c4623 there, and
        // the border must follow it.
        setUiTheme(false);
        pump();
        HudCanvas l;
        l.resize(1180, 820);
        l.setConsoleFidelityMode(true);
        l.setEditing(true);
        l.show();
        pump();
        const QImage light = l.grab().toImage();
        const QRect fl = frameFromBorder(light, kBoardClayLight);
        std::printf("   light edit frame (%d,%d) %dx%d\n",
                    fl.left(), fl.top(), fl.width(), fl.height());
        check(!fl.isEmpty() && fl.width() == expW + 2,
              "edit-mode border follows --clay into the light theme "
              "(#9c4623), ring and all");
        check(frameFromBorder(light, kBoardClayDark).isEmpty(),
              "and the light board contains no dark-theme clay at all");

        // Editing must not move a single window.
        StubSource s0;
        s0.m = QMargins(60, 50, 0, 0);
        HudCanvas em;
        em.resize(1180, 820);
        em.show();
        pump();
        em.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Split);
        QVector<HudCanvas::SubWindowBinding> bind0{{&s0, 0, 0}};
        em.setSubWindowBindings(bind0);
        em.setConsoleFidelityMode(true);
        pump();
        const QRectF before = em.subWindowRects().at(0);
        em.setEditing(true);
        pump();
        em.setEditing(false);
        pump();
        check(em.subWindowRects().at(0) == before,
              "entering and leaving edit mode moves no window");
    }
    setUiTheme(true);
    pump();

    // ── 7.6 window visibility: geometry, hit test, keys, round trip ────
    {
        StubSource s0, s1;
        s0.m = QMargins(120, 90, 0, 0);
        s1.m = QMargins(700, 480, 0, 0);
        HudCanvas v;
        v.resize(1180, 820);
        v.show();
        pump();
        v.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Split);
        QVector<HudCanvas::SubWindowBinding> binds{{&s0, 0, 0}, {&s1, 0, 1}};
        v.setSubWindowBindings(binds);
        v.setSubWindowPixmap(0, magentaPm, true);
        v.setSubWindowPixmap(1, magentaPm, true);
        v.setConsoleFidelityMode(true);
        pump();
        check(v.subWindowVisible(0) && v.subWindowVisible(1),
              "both windows start visible");
        const QVector<QRectF> both = v.subWindowRects();
        check(both.size() == 2 && !both[0].isEmpty() && !both[1].isEmpty(),
              "both windows are laid out before any hide");

        // The hit test is measured through the SIGNAL, not through the
        // selection: a click on empty space deliberately does NOT clear
        // an existing selection (mousePressEvent's non-overview arm only
        // ever sets one), so `selectedSubWindow()` cannot tell "the press
        // missed everything" from "the press landed here again".
        // subWindowSelected fires only from a press inside a box, which
        // makes its count the honest probe.
        int selectedSignals = 0;
        QObject::connect(&v, &HudCanvas::subWindowSelected,
                         [&selectedSignals](int) { ++selectedSignals; });

        // Click the second one first, so the test below has a selection
        // to lose.
        mouse(&v, QEvent::MouseButtonPress, both[1].center(),
              Qt::LeftButton, Qt::LeftButton);
        mouse(&v, QEvent::MouseButtonRelease, both[1].center(),
              Qt::LeftButton, Qt::MouseButtons());
        check(v.selectedSubWindow() == 1 && selectedSignals == 1,
              "a click selects the second window while it is visible");

        v.setSubWindowVisible(1, false);
        pump();
        const QVector<QRectF> oneHidden = v.subWindowRects();
        check(!v.subWindowVisible(1), "setSubWindowVisible(1,false) sticks");
        check(oneHidden[1].isEmpty(),
              "a hidden window is not laid out at all");
        check(oneHidden[0] == both[0],
              "hiding one window does not move its neighbour");
        check(v.selectedSubWindow() == 1,
              "a hide does not rewrite the selection");

        // The keyboard path.
        v.setEditing(true);
        const QMargins keyBefore = s1.m;
        mouse(&v, QEvent::MouseButtonPress, both[0].center(),
              Qt::LeftButton, Qt::LeftButton);
        mouse(&v, QEvent::MouseButtonRelease, both[0].center(),
              Qt::LeftButton, Qt::MouseButtons());
        v.setSelectedSubWindow(1);
        key(&v, Qt::Key_Right);
        check(s1.m == keyBefore,
              "a hidden window does not move on the arrow keys");
        v.setEditing(false);

        // The hit test.
        v.setSelectedSubWindow(1);
        const int beforePress = selectedSignals;
        mouse(&v, QEvent::MouseButtonPress, both[1].center(),
              Qt::LeftButton, Qt::LeftButton);
        mouse(&v, QEvent::MouseButtonRelease, both[1].center(),
              Qt::LeftButton, Qt::MouseButtons());
        std::printf("   press at (%.0f,%.0f) -> signals %d->%d, selected=%d\n",
                    both[1].center().x(), both[1].center().y(),
                    beforePress, selectedSignals, v.selectedSubWindow());
        check(selectedSignals == beforePress,
              "a hidden window is not hit-testable — the press that used "
              "to select it now lands on empty space");
        // And the press path still WORKS, so the check above is not
        // passing because something upstream stopped delivering events.
        mouse(&v, QEvent::MouseButtonPress, both[0].center(),
              Qt::LeftButton, Qt::LeftButton);
        mouse(&v, QEvent::MouseButtonRelease, both[0].center(),
              Qt::LeftButton, Qt::MouseButtons());
        check(selectedSignals == beforePress + 1 && v.selectedSubWindow() == 0,
              "the same press on the VISIBLE window still selects it");

        // Restore, and nothing about the window was rewritten.
        v.setSubWindowVisible(1, true);
        pump();
        check(v.subWindowRects().at(1) == both[1],
              "restoring visibility re-lays the window out at the "
              "same box");
        check(s1.margins() == QMargins(700, 480, 0, 0)
                  && s1.corner() == Corner::TopLeft
                  && s1.contentSize() == QSize(420, 160)
                  && s1.scale() == 1.0,
              "hide/restore rewrites nothing in the window's own source");
        check(v.subWindowRects().at(0) == both[0],
              "and still does not move its neighbour");

        // Unknown index, both directions.
        const int n = v.subWindowCount();
        v.setSubWindowVisible(n, false);
        v.setSubWindowVisible(-1, false);
        v.setSubWindowVisible(n + 40, false);
        check(v.subWindowCount() == n,
              "an out-of-range index never grows the binding list");
        check(v.subWindowVisible(n) && v.subWindowVisible(-1),
              "and an out-of-range index reads as the DEFAULT, visible");

        // Consumption is fidelity-gated.
        v.setSubWindowVisible(1, false);
        v.setConsoleFidelityMode(false);
        pump();
        check(!v.subWindowRects().at(1).isEmpty(),
              "outside fidelity mode the flag is stored and not consumed");
        v.setConsoleFidelityMode(true);
        pump();
        check(v.subWindowRects().at(1).isEmpty(),
              "and is consumed again the moment fidelity comes back");

        // A rebind resets it — the documented convention.
        v.setSubWindowBindings(binds);
        pump();
        check(v.subWindowVisible(0) && v.subWindowVisible(1),
              "a rebind resets visibility to the default, visible — the "
              "shell has to push its filter again");
    }

    // ── 7.7 the panel filter: one display's worth of windows ───────────
    {
        HudCanvas p;
        p.resize(1180, 820);
        p.show();
        pump();
        // The four mother panels, painted the way the console paints
        // them: bound to a source, a real pixmap per slot, enabled.
        // Single mode is what shows them (drawMothers is "no
        // sub-windows"), and what this block is measuring is the board's
        // OWN display filter — one display's worth of windows — not the
        // split.
        //
        // IN THE OVERVIEW, AND THAT IS NOT A DETAIL. v1.0.0's panel
        // view narrows the four-slot arm to `d.panel === state.view`
        // (the same clause reference.html:442 applies to the ONE list
        // visibleWindows() builds), so on a panel-view board only the
        // SELECTED panel is painted — four independent show/hide
        // observations are exactly what the cross-panel overview is
        // for. Left in the panel view this block would have measured
        // the scope instead of the visibility flag: hiding panel 1
        // while panel 0 is selected changes no pixel, and every
        // assertion below it goes vacuous or red.
        //
        // One SOURCE PER PANEL, and each at a different place — four
        // panels sharing one margin would stack on the same pixels, and
        // hiding one of four overlapping pictures would leave the count
        // of magenta pixels exactly where it was.
        StubSource mothers[mhw::kPanelCount];
        for (int i = 0; i < mhw::kPanelCount; ++i) {
            mothers[i].sub = false;
            mothers[i].m = QMargins(10, 10 + 190 * i, 0, 0);
            p.bindPanel(i, &mothers[i]);
            p.setPanelPixmap(i, magentaPm, true);
        }
        p.setOverview(true);
        p.setConsoleFidelityMode(true);
        pump();
        const int all = countMagenta(p.grab().toImage());
        check(all > 0, "the single-mode fidelity board draws the panels");
        {
            int painted = 0;
            for (const QRectF &r : p.motherWindowRects())
                if (!r.isEmpty())
                    ++painted;
            check(painted == mhw::kPanelCount,
                  "…all four of them, each with a painted box to hit");
            std::printf("    painted mothers: %d of %d\n", painted,
                        mhw::kPanelCount);
        }
        p.setPanelWindowVisible(1, false);
        pump();
        const int oneFewer = countMagenta(p.grab().toImage());
        check(oneFewer < all,
              "hiding a panel takes its picture off the board");
        for (int i = 0; i < mhw::kPanelCount; ++i)
            p.setPanelWindowVisible(i, false);
        pump();
        check(countMagenta(p.grab().toImage()) == 0,
              "hiding every panel empties the board");
        for (int i = 0; i < mhw::kPanelCount; ++i)
            p.setPanelWindowVisible(i, true);
        pump();
        check(countMagenta(p.grab().toImage()) == all,
              "restoring every panel brings the board back exactly");
        check(p.panelWindowVisible(-1) && p.panelWindowVisible(4),
              "an unknown panel index reads as visible");
        p.setPanelWindowVisible(-1, false);
        p.setPanelWindowVisible(4, false);
        pump();
        check(countMagenta(p.grab().toImage()) == all,
              "and writing one is a safe no-op");

        // The empty state, structurally: with everything hidden the
        // frozen board is still a board — its ground is intact and the
        // legacy field never comes back.
        for (int i = 0; i < mhw::kPanelCount; ++i)
            p.setPanelWindowVisible(i, false);
        pump();
        const QImage empty = p.grab().toImage();
        const QRect fre = frameFromBorder(empty, kBoardLine);
        check(!fre.isEmpty(),
              "the empty board still draws its frozen chrome");
        check(empty.pixelColor(0, 0) == kPageGroundDark,
              "the empty board still sits on --page");
        check(countExact(empty, kLegacyGround) == 0,
              "and the empty state does not resurrect the legacy field");
    }

    // ── 7.8 the REAL pixmap still composites over the fidelity board ───
    {
        StubSource ps;
        ps.m = QMargins(40, 40, 0, 0);
        ps.content = QSize(300, 120);
        HudCanvas pc, ctrl;
        for (HudCanvas *h : {&pc, &ctrl}) {
            h->resize(1180, 820);
            h->show();
        }
        pump();
        pc.setLayoutMode(mhw::PlayerSection::PlayerLayoutMode::Split);
        QVector<HudCanvas::SubWindowBinding> one{{&ps, 0, 0}};
        pc.setSubWindowBindings(one);
        pc.setConsoleFidelityMode(true);
        ctrl.setConsoleFidelityMode(true);
        pump();
        QPixmap solid(ps.contentSize());
        solid.fill(magenta);
        pc.setSubWindowPixmap(0, solid, true);
        pump();
        const QRectF box = pc.subWindowRects().at(0);
        const QPoint ctr = box.center().toPoint();
        check(pc.grab().toImage().pixelColor(ctr) == magenta,
              "a real sub-window pixmap reaches the fidelity board");
        QPixmap clear(ps.contentSize());
        clear.fill(Qt::transparent);
        pc.setSubWindowPixmap(0, clear, true);
        pump();
        check(pc.grab().toImage().pixel(ctr)
                  == ctrl.grab().toImage().pixel(ctr),
              "a transparent pixmap leaves the fidelity board's own "
              "pixel showing through — the compositing path is unchanged");
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // Panel::settings() is a fixed IniFormat/UserScope QSettings
    // ("monster-overlay"/"panels"), so a build with no redirect writes
    // into the developer's real ~/.config/monster-overlay/panels.ini
    // — including the split windows' own [player-status] /
    // [monster-hp] sections this test primes. Point XDG_CONFIG_HOME at
    // a throwaway dir before the first QSettings use, mirroring
    // control_panel_p0_test.cpp and control_l2_smoke.cpp.
    {
        const QString testRoot =
            QDir::tempPath() + QStringLiteral("/hud-canvas-split-config");
        QDir().mkpath(testRoot);
        if (!qEnvironmentVariableIsEmpty("XDG_CONFIG_HOME"))
            qunsetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", testRoot.toUtf8());
    }

    QApplication app(argc, argv);

    std::printf("== hud_canvas_split_tests ==\n");
    testTableDerivation();
    testSingleModeLeavesSubSlotsEmpty();
    testSubWindowCountMatchesTableRows();
    testSubWindowSizesComeFromTheWindow();
    testSubWindowPixmapPlumbing();
    sectionFidelityBoard();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
