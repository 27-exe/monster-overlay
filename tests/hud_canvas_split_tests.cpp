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

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QPixmap>
#include <algorithm>
#include <cstdint>
#include <cstdio>

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
    check(mhw::DamageSplitSlot::kCount == 2,
          "kDamageSplitTargets has 2 rows (one per damage sub-window)");
    check(mhw::PlayerSection::kPlayerSplitTargetCount
              + mhw::kMonsterSplitTargetCount
              + mhw::DamageSplitSlot::kCount == 10,
          "the three tables' rows sum to 10 sub-windows");

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
    check(canvas.subWindowCount() == kPlayerRows + kMonsterRows + kDamageRows,
          "one sub-window per split-table row (13 for the three tables)");
    check(canvas.subWindowCount() == 10,
          "the bound is 10 = 6 player + 2 monster + 2 damage");

    // A null source in the list is a row whose window did not resolve.
    // It must not become a box: a null dereference in the paint path is
    // a crash, and a blank box is a lie about what the overlay draws.
    HudCanvas::SubWindowBinding nullRow;
    nullRow.src = nullptr;
    nullRow.parentSlot = 0;
    nullRow.tableRow = 3;
    bindings.append(nullRow);
    canvas.setSubWindowBindings(bindings);
    check(canvas.subWindowCount() == 10,
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

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
