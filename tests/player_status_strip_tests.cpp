// SPDX-License-Identifier: Apache-2.0
//
// The v0.12 split's pure core: mhw::buildStatusStripModel and
// mhw::statusStripRows, which turn one GameSnapshot into the two pill
// blocks the PlayerStatusStripPanel draws.
//
// These live in src/ui/panel_player_split.h, but this target deliberately
// does NOT link mhw-ui. The header is pulled in for its inline half only,
// the same way panel-mask-codec-tests and console-text-helpers-tests take a
// header's pure functions without dragging in a widget library. That is
// also what makes this target load-bearing for the zero-reference gate:
// the split panel's .cpp is measured ZERO until some target references one
// of its symbols, and this one reaches the header's inline definitions.
//
// What is pinned here:
//   * the World / Rise seam — each game reads its own vectors, and a
//     snapshot carrying the OTHER game's data contributes nothing;
//   * Rise's kind split (AbnormalityKind::Debuff vs ::Buff), including
//     that Buildup entries land in the same block as their kind rather
//     than being dropped;
//   * the empty case answering isEmpty(), which is what collapses the
//     window instead of leaving an empty strip on screen;
//   * statusStripRows() wrapping, including the 0 / negative guards, since
//     the height reservation and the drawing both divide by it;
//   * the v0.12 width/slot inversion — gridContentWidth() and
//     gridSlotWidth() over every column count and item count, because
//     the strip reserved a single-capsule width for a three-capsule
//     row and clipped 23px out of a 91px window.
//
// The last group is the one that makes the strip "adaptive" rather than
// merely non-broken: if the reservation and the draw ever stop being
// exact inverses, the panels that look fine are the ones that look
// wrong.

#include "ui/panel_player_split.h"
#include "ui/panel_sizing.h"

#include <QCoreApplication>
#include <algorithm>
#include <cstdio>

namespace {

int gFailures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++gFailures;
    }
}

// World carries PlayerAbnormality (no `kind` — the two vectors it fills
// already say which is which); Rise carries PlayerAbnormalitySnapshot,
// which is one vector discriminated by `kind`. Two constructors, because
// a shared one would have to invent a `kind` the World type does not have.
mhw::PlayerAbnormality worldPill(const char *name,
                                  mhw::AbnormalityAccent accent
                                      = mhw::AbnormalityAccent::None)
{
    mhw::PlayerAbnormality p;
    p.offset = 0;
    p.name = QString::fromUtf8(name);
    p.accent = accent;
    p.timer = 30.0F;
    p.maxTimer = 60.0F;
    return p;
}

mhw::PlayerAbnormalitySnapshot risePill(const char *name,
                                        mhw::AbnormalityKind kind,
                                        mhw::AbnormalityAccent accent
                                            = mhw::AbnormalityAccent::None,
                                        float timer = 30.0F)
{
    mhw::PlayerAbnormalitySnapshot p;
    p.name = QString::fromUtf8(name);
    p.kind = kind;
    p.accent = accent;
    p.timer = timer;
    p.maxTimer = 60.0F;
    return p;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ---- rows(): the wrap arithmetic both height paths divide by --------
    // The one-arg form was pinned to the 3-column default; the dial made
    // `perRow` runtime, so every case is stated per column count. The
    // 3-column column re-states the historical numbers exactly, which is
    // what keeps the default behaviour-neutral.
    check(mhw::statusStripRows(0, 3) == 0, "rows(0,3) == 0");
    check(mhw::statusStripRows(-5, 3) == 0, "rows(negative,3) == 0 (guarded divide)");
    check(mhw::statusStripRows(1, 3) == 1, "rows(1,3) == 1");
    check(mhw::statusStripRows(3, 3) == 1, "rows(3,3) == 1 (exactly one full row)");
    check(mhw::statusStripRows(4, 3) == 2, "rows(4,3) == 2 (one over wraps)");
    check(mhw::statusStripRows(6, 3) == 2, "rows(6,3) == 2 (two full rows)");
    check(mhw::statusStripRows(7, 3) == 3, "rows(7,3) == 3 (two rows plus one)");

    // The dial's other stops: 1 column never wraps, 6 columns swallow
    // everything the strip can realistically hold.
    check(mhw::statusStripRows(7, 1) == 7, "rows(7,1) == 7 (1 col never wraps)");
    check(mhw::statusStripRows(6, 6) == 1, "rows(6,6) == 1 (6 col: one row)");
    check(mhw::statusStripRows(7, 6) == 2, "rows(7,6) == 2");
    check(mhw::statusStripRows(12, 6) == 2, "rows(12,6) == 2 (exactly full)");

    // A 0-col strip has no layout, not an infinite one: this is the
    // divide-by-zero the dial's clamp exists to prevent, stated at the
    // arithmetic so the guard is load-bearing before setPillsPerRow is
    // ever called.
    check(mhw::statusStripRows(5, 0) == 0, "rows(5,0-col) == 0 (guarded divide)");
    check(mhw::statusStripRows(0, 0) == 0, "rows(0,0-col) == 0");
    check(mhw::statusStripRows(-3, 0) == 0,
          "rows(negative, 0-col) == 0 (both guards)");

    // ---- empty snapshot collapses the strip ----------------------------
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::World;
        const mhw::StatusStripModel m = mhw::buildStatusStripModel(snap);
        check(m.isEmpty(), "empty World snapshot -> isEmpty()");
        check(m.total() == 0, "empty World snapshot -> total() == 0");
    }

    // ---- World: reads debuffs / buffs, in that order -------------------
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::World;
        snap.player.debuffs = {
            worldPill("poison", mhw::AbnormalityAccent::Paralysis),
            worldPill("blight", mhw::AbnormalityAccent::Fire),
        };
        snap.player.buffs = {
            worldPill("dango", mhw::AbnormalityAccent::None),
        };
        const mhw::StatusStripModel m = mhw::buildStatusStripModel(snap);
        check(!m.isEmpty(), "World snapshot with data -> !isEmpty()");
        check(m.debuffs.size() == 2, "World: both debuffs carried");
        check(m.buffs.size() == 1, "World: both buffs carried");
        check(m.total() == 3, "World: total() counts both blocks");
        // Accent travels with the pill, never inferred from the name —
        // a translated name must not be able to change a colour.
        check(m.debuffs[0].accent == mhw::AbnormalityAccent::Paralysis,
              "World: debuff accent preserved from the snapshot");
        check(m.debuffs[1].accent == mhw::AbnormalityAccent::Fire,
              "World: second debuff accent preserved");
    }

    // ---- World ignores Rise-only abnormalities ------------------------
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::World;
        snap.player.debuffs = {
            worldPill("poison", mhw::AbnormalityAccent::Paralysis),
        };
        // Populated even though this is a World snapshot: the World arm
        // must not reach for it, or a future reader that fills both would
        // double-count.
        snap.player.abnormalities = {
            risePill("ABN_X", mhw::AbnormalityKind::Debuff),
            risePill("ABN_Y", mhw::AbnormalityKind::Buff),
        };
        const mhw::StatusStripModel m = mhw::buildStatusStripModel(snap);
        check(m.total() == 1, "World: Rise-only abnormalities ignored");
        check(m.buffs.isEmpty(),
              "World: Rise-only buff abnormality not smuggled into buffs");
    }

    // ---- Rise: reads abnormalities and splits by kind -----------------
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        snap.player.abnormalities = {
            risePill("ABN_POISON", mhw::AbnormalityKind::Debuff,
                     mhw::AbnormalityAccent::Paralysis),
            risePill("ABN_DANGO", mhw::AbnormalityKind::Buff,
                     mhw::AbnormalityAccent::None),
            risePill("ABN_BLIGHT", mhw::AbnormalityKind::Debuff,
                     mhw::AbnormalityAccent::Fire),
        };
        const mhw::StatusStripModel m = mhw::buildStatusStripModel(snap);
        check(m.debuffs.size() == 2, "Rise: two Debuff-kind entries");
        check(m.buffs.size() == 1, "Rise: one Buff-kind entry");
        check(m.debuffs[0].name == QStringLiteral("ABN_POISON")
                  && m.debuffs[1].name == QStringLiteral("ABN_BLIGHT"),
              "Rise: debuffs keep their source order");
    }

    // ---- Rise ignores World-only vectors ------------------------------
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        snap.player.abnormalities = {
            risePill("ABN_DANGO", mhw::AbnormalityKind::Buff),
        };
        snap.player.debuffs = {
            worldPill("world-poison", mhw::AbnormalityAccent::Paralysis),
        };
        snap.player.buffs = {
            worldPill("world-buff"),
        };
        const mhw::StatusStripModel m = mhw::buildStatusStripModel(snap);
        check(m.total() == 1,
              "Rise: World-only debuffs/buffs ignored, not merged in");
        check(m.debuffs.isEmpty(), "Rise: World debuff vector contributes none");
    }

    // ---- Rise buildup entries keep their kind ------------------------
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        mhw::PlayerAbnormalitySnapshot buildup =
            risePill("ABN_BUILDUP", mhw::AbnormalityKind::Debuff,
                     mhw::AbnormalityAccent::Fire, /*timer=*/4.0F);
        buildup.isBuildup = true;
        buildup.maxTimer = 0.0F;   // buildup entries carry a counter, not a timer
        snap.player.abnormalities = {buildup};
        const mhw::StatusStripModel m = mhw::buildStatusStripModel(snap);
        check(m.total() == 1, "Rise: a buildup entry is not dropped");
        check(m.debuffs.size() == 1, "Rise: buildup lands in its kind's block");
    }

    // ---- the demo path and the live path cannot disagree ---------------
    // Both route through buildStatusStripModel, so a World preview and a
    // World session answer the same question the same way. This case is
    // the regression guard for that: if someone later adds a second,
    // divergent transformation for the demo, the strip's preview stops
    // predicting its live shape.
    {
        mhw::GameSnapshot preview;
        preview.game = mhw::GameId::World;
        preview.player.debuffs = {
            worldPill("a"), worldPill("b"), worldPill("c"), worldPill("d"),
        };
        const mhw::StatusStripModel m = mhw::buildStatusStripModel(preview);
        check(mhw::statusStripRows(m.debuffs.size(), 3) == 2,
              "4 debuffs at 3-per-row reserve two rows of height");
        // The same strip at the dial's 1-column stop: four rows, not two.
        check(mhw::statusStripRows(m.debuffs.size(), 1) == 4,
              "4 debuffs at 1-per-row reserve four rows");
    }

    // ---- THE INVERSION: reservation and draw are exact inverses -------
    //
    // gridContentWidth() is what contentWidth() reserves with;
    // gridSlotWidth() is what drawPillGrid() hands each capsule. They
    // must be exact inverses or the strip clips — that disagreement is
    // the 23px-slot bug, where a 3-capsule row got a 1-capsule
    // reservation divided by 3.
    //
    // Every case below is stated on the grid width drawPillGrid is
    // actually handed (window - 2*kMargin), which is why the expected
    // slot is the FULL capsule width and not a remainder of it.
    {
        using mhw::panel_sizing::gridContentWidth;
        using mhw::panel_sizing::gridSlotWidth;

        const int gap = 5;
        const int pillW = 100;

        // Exact inversion: reserving for N columns and dividing for N
        // columns returns the very capsule that was measured. This is
        // the single property the whole fix rests on.
        check(gridSlotWidth(gridContentWidth(1, 1, pillW, gap), 1, gap)
                  == pillW, "1 col: slot == capsule (exact inverse)");
        check(gridSlotWidth(gridContentWidth(3, 3, pillW, gap), 3, gap)
                  == pillW, "3 col: slot == capsule (exact inverse)");
        check(gridSlotWidth(gridContentWidth(6, 6, pillW, gap), 6, gap)
                  == pillW, "6 col: slot == capsule (exact inverse)");

        // Widening the dial widens the window, one capsule per step.
        check(gridContentWidth(1, 1, pillW, gap) == pillW,
              "1 col: gridW == capsule, no gaps to spend");
        check(gridContentWidth(2, 2, pillW, gap) == 2 * pillW + gap,
              "2 col: gridW == 2 capsules + 1 gap");
        check(gridContentWidth(3, 3, pillW, gap) == 3 * pillW + 2 * gap,
              "3 col: gridW == 3 capsules + 2 gaps (the default)");
        check(gridContentWidth(4, 4, pillW, gap) == 4 * pillW + 3 * gap,
              "4 col: gridW == 4 capsules + 3 gaps");

        // Shrinking the dial shrinks the window the same way — the
        // user's width/height trade, in both directions.
        check(gridContentWidth(2, 2, pillW, gap) <
                  gridContentWidth(3, 3, pillW, gap),
              "narrower dial -> narrower window");
        check(gridContentWidth(3, 3, pillW, gap) <
                  gridContentWidth(4, 4, pillW, gap),
              "wider dial -> wider window");

        // 0 items: an empty grid, which is what collapses the strip.
        check(gridContentWidth(0, 3, pillW, gap) == 0,
              "0 items: empty grid, regardless of columns");
        check(gridSlotWidth(0, 3, gap) == 0,
              "0 budget: no slot (never a negative one)");
        check(gridSlotWidth(-50, 3, gap) == 0,
              "negative budget: no slot (guarded)");

        // 1 item at 3 columns: the reservation is still a full row
        // (3 capsules), and the draw divides by the row it is ABOUT to
        // place (1 item), so the lone capsule fills the window. That is
        // the "adapt its length and width to fill" contract.
        check(gridContentWidth(3, 3, pillW, gap) == 3 * pillW + 2 * gap,
              "1 item: window still reserves a full 3-col row");
        check(gridSlotWidth(gridContentWidth(3, 3, pillW, gap), 1, gap)
                  == 3 * pillW + 2 * gap,
              "1 item: lone capsule fills the whole reserved row");

        // The incomplete-row case the old code got backwards: 4 items at 3
        // columns. Row 1 is three slots at `pillW`; row 2 is ONE item, so
        // it takes the whole row rather than a third of it. `itemsInRow`
        // is the same expression drawPillGrid computes.
        const int itemsInRow = std::min(3, 4 - 1 * 3);
        check(gridSlotWidth(gridContentWidth(3, 3, pillW, gap), itemsInRow,
                            gap) == 3 * pillW + 2 * gap,
              "4 items @3col: trailing row of 1 fills the window");

        // Exact division: 6 items / 3 columns / 6 items / 6 columns
        // leave nothing over, so the slot is the capsule with no
        // remainder to redistribute.
        check(gridSlotWidth(gridContentWidth(3, 3, pillW, gap), 3, gap)
                  == pillW, "6 items @3col: slot == capsule, exact division");
        check(gridSlotWidth(gridContentWidth(6, 6, pillW, gap), 6, gap)
                  == pillW, "12 items @6col: slot == capsule, exact division");

        // "Does not fit": a budget smaller than one capsule. The slot
        // must still be positive (drawPillGrid floors it at kMinPillW
        // and elides the name) — answering 0 here would hide the pill
        // entirely rather than clip it.
        check(gridSlotWidth(20, 3, gap) >= 0, "tiny budget: slot >= 0");
        check(gridSlotWidth(1, 3, gap) >= 0, "1px budget: slot >= 0");
    }

    if (gFailures == 0)
        std::printf("ALL PASS: player status strip model\n");
    else
        std::printf("%d check(s) FAILED\n", gFailures);
    std::fflush(stdout);
    return gFailures == 0 ? 0 : 1;
}