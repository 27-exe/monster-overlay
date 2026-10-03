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
//     the height reservation and the drawing both divide by it.

#include "ui/panel_player_split.h"

#include <QCoreApplication>
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
    check(mhw::statusStripRows(0) == 0, "rows(0) == 0");
    check(mhw::statusStripRows(-5) == 0, "rows(negative) == 0 (guarded divide)");
    check(mhw::statusStripRows(1) == 1, "rows(1) == 1");
    check(mhw::statusStripRows(3) == 1, "rows(3) == 1 (exactly one full row)");
    check(mhw::statusStripRows(4) == 2, "rows(4) == 2 (one over wraps)");
    check(mhw::statusStripRows(6) == 2, "rows(6) == 2 (two full rows)");
    check(mhw::statusStripRows(7) == 3, "rows(7) == 3 (two rows plus one)");

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
        check(mhw::statusStripRows(m.debuffs.size()) == 2,
              "4 debuffs at 3-per-row reserve two rows of height");
    }

    if (gFailures == 0)
        std::printf("ALL PASS: player status strip model\n");
    else
        std::printf("%d check(s) FAILED\n", gFailures);
    std::fflush(stdout);
    return gFailures == 0 ? 0 : 1;
}