// SPDX-License-Identifier: Apache-2.0
//
// The v0.12 split's other three pure cores: buildBarsModel,
// buildMantleModel and buildWirebugModel — the transformations that turn
// one GameSnapshot into what the HP/ST window, the mantle window and the
// wirebug window draw.
//
// Each model lives in its own header next to the panel it belongs to
// (panel_player_split_bars.h / _mantles.h / _wirebug.h), pulled in for
// their inline halves only. This target deliberately does NOT link
// mhw-ui, the same way player_status_strip_tests and
// player-layout-tests take a header's pure functions without dragging in
// a widget library — and for the same second reason: the reference sweep
// treats a header-only inclusion as a reference, so these targets are
// what keeps the corresponding .cpp from measuring ZERO on the
// zero-reference gate.
//
// What is pinned here, per model:
//   bars    — the Rise/World stamina scaling (the whole reason
//             staminaDisplayValue exists), the `known` gating that makes
//             an unreadable bar print a placeholder instead of a number,
//             and that health is NEVER scaled in either game.
//   mantles — the active/cooling branch (timer vs cooldown, and the two
//             different progress formulas), the -1 sentinel dropping a
//             slot, and Rise producing an empty model without any
//             game-specific branch in the caller.
//   wirebug — the ready filter (cooldown <= epsilon earns no capsule),
//             the source slot travelling through the model rather than
//             the vector index, and World producing an empty model.

#include "ui/panel_player_split_bars.h"
#include "ui/panel_player_split_mantles.h"
#include "ui/panel_player_split_wirebug.h"
// mhw::kRiseWirebugSlotCap — the reader's own four-source-slot cap, which
// one of the checks below pins. Included explicitly rather than relying on
// it arriving transitively: a transitive include is one refactor away
// from breaking the build in a file that never named the header.
#include "rise/mhr_reader.h"
// mhw::groupNumber / mhw::percentage — the formatters the bars model
// hands its numbers to. Included directly so the test's expected strings
// are built by the same code the model uses, instead of the test spelling
// out a locale-specific grouping and disagreeing with the shipped binary.
#include "ui/formatters.h"

#include <QCoreApplication>
#include <QString>
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

// Named vector for the Rise stamina scaling: raw 3000/3000 -> 100/100,
// raw 7200 -> 240, raw 3000/4500 -> 100/150. These are the measurements
// panel_player_metrics.h records (the /30 raw ratio, confirmed live
// 2026-09-17 against the earlier /10 pass's 3x overshoot). A bar-scale
// regression that reaches the model rather than the painter is caught
// here instead of by a user reporting "耐力显示 720".
float riseStamina(int rawThirtieths)
{
    return static_cast<float>(rawThirtieths);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ===================== buildBarsModel =====================

    // ---- empty snapshot: both bars unknown, both placeholders ---------
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::World;
        const mhw::BarsModel m = mhw::buildBarsModel(snap);
        check(!m.health.known, "empty World snapshot -> HP unknown");
        check(!m.stamina.known, "empty World snapshot -> ST unknown");
        check(m.isEmpty(), "empty World snapshot -> isEmpty()");
        check(m.health.valueText == QStringLiteral("-- / --"),
              "unknown HP prints the placeholder pair");
        check(m.health.pctText == QStringLiteral("--%"),
              "unknown HP prints the placeholder percentage");
        check(m.stamina.valueText == QStringLiteral("-- / --"),
              "unknown ST prints the placeholder pair");
        check(m.health.family == mhw::BarsRow::Family::Hp,
              "row 1 is the HP family");
        check(m.stamina.family == mhw::BarsRow::Family::St,
              "row 2 is the ST family");
    }

    // ---- World: raw values pass through unchanged ---------------------
    // World's HUD stamina is already the bar value (player_reader.cpp
    // reads ~150 max), so the numbers must be the raw ones, not scaled.
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::World;
        snap.player.valid = true;
        snap.player.health = 132.0F;
        snap.player.maxHealth = 150.0F;
        snap.player.stamina = 90.0F;
        snap.player.maxStamina = 150.0F;
        const mhw::BarsModel m = mhw::buildBarsModel(snap);
        check(m.health.known, "World: HP known once valid && maxHealth > 0");
        check(!m.isEmpty(), "World: known bars -> !isEmpty()");
        check(m.health.pct > 0.87F && m.health.pct < 0.89F,
              "World: HP pct is health / maxHealth");
        check(m.health.valueText == QStringLiteral("132 / 150"),
              "World: HP prints the grouped raw pair");
        check(m.stamina.valueText == QStringLiteral("90 / 150"),
              "World: stamina is NOT scaled (raw already the bar value)");
        check(m.stamina.pct > 0.59F && m.stamina.pct < 0.61F,
              "World: ST pct is stamina / maxStamina");
    }

    // ---- Rise: raw stamina is divided by 30 ---------------------------
    // THE vector that matters. A regression to /10 gives 3000/10 = 300
    // for a bar the game itself shows as 100; a regression to "no
    // scaling" gives 3000. Both are caught here.
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        snap.player.valid = true;
        snap.player.health = 150.0F;
        snap.player.maxHealth = 150.0F;
        // raw 3000 / 3000 reads in-game as 100 / 100.
        snap.player.stamina = riseStamina(3000);
        snap.player.maxStamina = riseStamina(3000);
        const mhw::BarsModel m = mhw::buildBarsModel(snap);
        check(m.stamina.valueText == QStringLiteral("100 / 100"),
              "Rise: raw 3000/3000 -> \"100 / 100\" (measured vectors)");
        check(m.stamina.pct > 0.99F && m.stamina.pct <= 1.0F,
              "Rise: ST pct is unit-invariant (raw/raw), so still full");
    }

    // raw 7200 -> 240 (the exact value the /10 bug printed as 720).
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        snap.player.valid = true;
        snap.player.maxHealth = 150.0F;
        snap.player.stamina = riseStamina(7200);
        snap.player.maxStamina = riseStamina(9000);
        const mhw::BarsModel m = mhw::buildBarsModel(snap);
        check(m.stamina.valueText == QStringLiteral("240 / 300"),
              "Rise: raw 7200/9000 -> \"240 / 300\"");
    }

    // raw 3000 / 4500 -> 100 / 150 (the extendable-stamina bar).
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        snap.player.valid = true;
        snap.player.maxHealth = 150.0F;
        snap.player.stamina = riseStamina(3000);
        snap.player.maxStamina = riseStamina(4500);
        const mhw::BarsModel m = mhw::buildBarsModel(snap);
        check(m.stamina.valueText == QStringLiteral("100 / 150"),
              "Rise: raw 3000/4500 -> \"100 / 150\"");
    }

    // ---- Health is never scaled, in EITHER game ----------------------
    // pinning that the Health row keeps the raw pair while the row below
    // it is divided — the two rows must not share a scaler.
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        snap.player.valid = true;
        snap.player.health = 132.0F;
        snap.player.maxHealth = 150.0F;
        snap.player.stamina = riseStamina(3000);
        snap.player.maxStamina = riseStamina(1000);
        const mhw::BarsModel m = mhw::buildBarsModel(snap);
        check(m.health.valueText == QStringLiteral("132 / 150"),
              "Rise: health is NOT scaled (raw HP is already the bar value)");
        // 3000 / 30 = 100, 1000 / 30 = 33.33 -> truncated to 33 by the
        // int cast the panel uses (panel_player.cpp:1086-1087).
        check(m.stamina.valueText == QStringLiteral("100 / 33"),
              "Rise: only the stamina row is scaled, in the same snapshot");
    }

    // ---- the same numbers, both games, one answer each ----------------
    // The World arm of the same raw pair must differ from the Rise arm,
    // which is the whole reason staminaDisplayScale is a per-game table
    // and not a ternary. If they ever agree, someone has collapsed the
    // table back into "no scaling everywhere".
    //
    // The expected strings are built through mhw::groupNumber() rather
    // than spelled out, because that helper is locale-aware (it prints
    // "3,000" under a C locale and would print something else under a
    // different default): the test pins the MODEL's decision (which
    // numbers reach the formatter), and lets the formatter own how the
    // digits are grouped — the same split of responsibility the panel
    // itself has.
    {
        auto pair = [](mhw::GameId game) {
            mhw::GameSnapshot snap;
            snap.game = game;
            snap.player.valid = true;
            snap.player.maxHealth = 150.0F;
            snap.player.stamina = riseStamina(3000);
            snap.player.maxStamina = riseStamina(3000);
            return mhw::buildBarsModel(snap).stamina.valueText;
        };
        const QString worldExpected =
            QStringLiteral("%1 / %2").arg(mhw::groupNumber(3000),
                                         mhw::groupNumber(3000));
        const QString riseExpected =
            QStringLiteral("%1 / %2").arg(mhw::groupNumber(100),
                                         mhw::groupNumber(100));
        const QString worldText = pair(mhw::GameId::World);
        const QString riseText = pair(mhw::GameId::Rise);
        check(worldText == worldExpected,
              "World: identical raw pair stays ungrouped-scaled (3000 / 3000)");
        check(riseText == riseExpected,
              "Rise: identical raw pair becomes 100 / 100");
        check(worldText != riseText,
              "the two games must not answer the same raw pair identically");
    }

    // ---- valid but unreadable health ---------------------------------
    // `valid` true with maxHealth == 0 (a resolved struct whose health
    // read missed): the row stays reserved but prints placeholders, and
    // the model still collapses (nothing about the hunter is known).
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::World;
        snap.player.valid = true;
        snap.player.maxHealth = 0.0F;
        snap.player.stamina = 90.0F;
        snap.player.maxStamina = 150.0F;
        const mhw::BarsModel m = mhw::buildBarsModel(snap);
        check(!m.health.known, "maxHealth == 0 -> HP unknown");
        check(!m.stamina.known,
              "ST inherits HP's unknown (panel nesting: hpKnown && maxStamina > 0)");
        check(m.stamina.valueText == QStringLiteral("-- / --"),
              "unknown ST prints placeholders even though stamina is set");
        check(m.health.pct == 0.0F, "unknown HP draws an empty track, not a full one");
        check(m.isEmpty(), "nothing resolved -> isEmpty() (the window collapses)");
    }

    // ===================== buildMantleModel =====================

    // ---- empty snapshot ----------------------------------------------
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::World;
        const mhw::MantleModel m = mhw::buildMantleModel(snap);
        check(m.isEmpty(), "empty World snapshot -> no mantle slots");
    }

    // ---- World: both slots, one active and one cooling ----------------
    // The active slot prints its remaining time and fills by timer/120;
    // the cooling slot prints its cooldown and fills by 1 - cd/cdMax.
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::World;
        snap.player.valid = true;
        snap.player.mantleSlot0Id = 0;      // Ghillie, running
        snap.player.mantleSlot0Timer = 42.0F;
        snap.player.mantleSlot0Cooldown = 0.0F;
        snap.player.mantleSlot1Id = 3;      // Rocksteady, cooling
        snap.player.mantleSlot1Timer = 0.0F;
        snap.player.mantleSlot1Cooldown = 96.0F;
        const mhw::MantleModel m = mhw::buildMantleModel(snap);
        check(m.boxes.size() == 2, "World: both equipped slots carried");
        check(!m.isEmpty(), "World: two slots -> !isEmpty()");

        check(m.boxes[0].id == 0, "slot 0 keeps its id");
        check(m.boxes[0].active, "positive timer -> active slot");
        check(m.boxes[0].timer == QStringLiteral("42s"),
              "active slot prints its REMAINING time");
        check(m.boxes[0].progress > 0.34F && m.boxes[0].progress < 0.36F,
              "active progress = timer / 120");

        check(m.boxes[1].id == 3, "slot 1 keeps its id");
        check(!m.boxes[1].active, "zero timer -> cooling slot");
        check(m.boxes[1].timer == QStringLiteral("96s"),
              "cooling slot prints its COOLDOWN");
        // 1 - 96/270 = 0.6444...
        check(m.boxes[1].progress > 0.64F && m.boxes[1].progress < 0.65F,
              "cooling progress = 1 - cooldown / cooldownMax");
    }

    // ---- a negative id drops the slot, without shifting ---------------
    // The panel draws slot 0 in box 0 and slot 1 in box 1 regardless of
    // which are equipped, so the model must keep the unequipped one
    // absent rather than renumbering the equipped one into its place.
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::World;
        snap.player.valid = true;
        snap.player.mantleSlot0Id = -1;     // nothing equipped
        snap.player.mantleSlot1Id = 7;      // Fire Resistance cooling
        snap.player.mantleSlot1Cooldown = 135.0F;
        snap.player.mantleSlot1CooldownMax = 270.0F;
        const mhw::MantleModel m = mhw::buildMantleModel(snap);
        check(m.boxes.size() == 1, "only the equipped slot contributes a row");
        check(m.boxes[0].id == 7,
              "the surviving row keeps ITS OWN id, not index 1");
        check(!m.boxes[0].active, "the surviving slot is cooling");
    }

    // ---- progress is clamped, never out of range ----------------------
    // A cooling slot whose cooldown already ran past its max (or a
    // negative cooldown from a reader glitch) must not paint a strip
    // outside its track.
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::World;
        snap.player.valid = true;
        snap.player.mantleSlot0Id = 0;
        snap.player.mantleSlot0Timer = 0.0F;
        snap.player.mantleSlot0Cooldown = 0.0F;      // available again
        snap.player.mantleSlot0CooldownMax = 270.0F;
        snap.player.mantleSlot1Id = 3;
        snap.player.mantleSlot1Timer = 1000.0F;      // longer than 120
        const mhw::MantleModel m = mhw::buildMantleModel(snap);
        check(m.boxes[0].progress > 0.99F && m.boxes[0].progress <= 1.0F,
              "a 0-cooldown slot is fully recharged (1 - 0/270)");
        check(m.boxes[1].progress == 1.0F,
              "an active timer past 120 clamps to a full strip");
    }

    // ---- Rise has no mantle system ------------------------------------
    // The important case: a Rise snapshot carrying World-shaped mantle
    // data must produce an EMPTY model, because the mantle window must
    // never mount under Rise. No game-specific branch in the caller —
    // the emptiness is what the data says.
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        snap.player.valid = true;
        // Populated even under Rise: the reader that fills this for a
        // World process must not be able to make a Rise window appear.
        snap.player.mantleSlot0Id = 0;
        snap.player.mantleSlot0Timer = 42.0F;
        snap.player.mantleSlot1Id = 3;
        snap.player.mantleSlot1Cooldown = 96.0F;
        const mhw::MantleModel m = mhw::buildMantleModel(snap);
        check(m.isEmpty(),
              "Rise: a snapshot carrying mantle data yields no rows");
    }

    // ---- the model is a pure function of the snapshot ----------------
    // The demo path and the live path route through the same builder, so
    // an edit-mode preview predicts its live shape exactly. This is the
    // regression guard for a second, divergent demo transformation.
    {
        mhw::GameSnapshot live;
        live.game = mhw::GameId::World;
        live.player.valid = true;
        live.player.mantleSlot0Id = 0;
        live.player.mantleSlot0Timer = 42.0F;
        const mhw::MantleModel a = mhw::buildMantleModel(live);
        const mhw::MantleModel b = mhw::buildMantleModel(live);
        check(a.boxes.size() == b.boxes.size()
                  && (!a.boxes.isEmpty()
                          && a.boxes[0].timer == b.boxes[0].timer),
              "the same snapshot builds the same model twice");
    }

    // ===================== buildWirebugModel =====================

    // ---- empty snapshot ----------------------------------------------
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        const mhw::WirebugModel m = mhw::buildWirebugModel(snap);
        check(m.isEmpty(), "empty Rise snapshot -> no capsules");
    }

    // ---- ready wirebugs earn no capsule -------------------------------
    // "冷却完了直接消失" (v0.7.2): a cooldown <= epsilon entry is filtered
    // out, and an all-ready hunter collapses the window entirely.
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        snap.player.valid = true;
        snap.player.wirebugs = {
            mhw::WirebugSnapshot{},   // cooldown 0, ready
        };
        for (auto &w : snap.player.wirebugs) {
            w.cooldown = 0.0F;
            w.maxCooldown = 30.0F;
        }
        const mhw::WirebugModel m = mhw::buildWirebugModel(snap);
        check(m.isEmpty(),
              "a ready wirebug contributes no capsule (the row collapses)");
    }

    // ---- recovering wirebugs, in source order, with their own slots ---
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        snap.player.valid = true;
        mhw::WirebugSnapshot w0;
        w0.slot = 0; w0.cooldown = 12.0F; w0.maxCooldown = 30.0F;
        mhw::WirebugSnapshot w1;
        w1.slot = 1; w1.isTemporary = true;
        w1.cooldown = 8.0F; w1.maxCooldown = 30.0F;
        mhw::WirebugSnapshot w2;
        w2.slot = 2; w2.isTemporary = true;
        w2.cooldown = 20.0F; w2.maxCooldown = 30.0F;
        snap.player.wirebugs = {w0, w1, w2};

        const mhw::WirebugModel m = mhw::buildWirebugModel(snap);
        check(m.capsules.size() == 3, "Rise: three recovering capsules");
        check(!m.isEmpty(), "Rise: recovering capsules -> !isEmpty()");
        check(m.capsules[0].slot == 0
                  && m.capsules[1].slot == 1
                  && m.capsules[2].slot == 2,
              "the SOURCE slot travels through the model, not the index");
        check(!m.capsules[0].temporary,
              "slot 0 is the default wirebug (thick accent stripe)");
        check(m.capsules[1].temporary && m.capsules[2].temporary,
              "environment/skill wirebugs are marked temporary");
        check(m.capsules[0].progress > 0.39F && m.capsules[0].progress < 0.41F,
              "progress = cooldown / maxCooldown");
        check(m.capsules[2].progress > 0.66F && m.capsules[2].progress < 0.67F,
              "progress rises with the remaining cooldown");
    }

    // ---- a ready entry in the middle does not renumber ---------------
    // The reader keeps each snapshot's original Mono-array slot even when
    // None entries are omitted (panel_player_metrics.h:73-76), so a
    // wirebug that comes back ready must not shift the others' slot
    // numbers — otherwise the labels rename themselves mid-fight.
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        snap.player.valid = true;
        mhw::WirebugSnapshot w0;
        w0.slot = 0; w0.cooldown = 12.0F; w0.maxCooldown = 30.0F;
        mhw::WirebugSnapshot ready;
        ready.slot = 1; ready.cooldown = 0.0F; ready.maxCooldown = 30.0F;
        mhw::WirebugSnapshot w2;
        w2.slot = 2; w2.cooldown = 20.0F; w2.maxCooldown = 30.0F;
        snap.player.wirebugs = {w0, ready, w2};

        const mhw::WirebugModel m = mhw::buildWirebugModel(snap);
        check(m.capsules.size() == 2, "the ready entry is filtered out");
        check(m.capsules[0].slot == 0 && m.capsules[1].slot == 2,
              "the survivors keep slots 0 and 2 — not 0 and 1");
    }

    // ---- a corrupt slot keeps the generic label, not a number --------
    // wirebugSlotLabel() maps out-of-range slots to the plain label
    // rather than inventing a number; the model must carry the slot so
    // that logic still runs at paint time.
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::Rise;
        snap.player.valid = true;
        mhw::WirebugSnapshot w;
        w.slot = 99;                        // corrupt: no trustworthy identity
        w.cooldown = 5.0F; w.maxCooldown = 30.0F;
        snap.player.wirebugs = {w};
        const mhw::WirebugModel m = mhw::buildWirebugModel(snap);
        check(m.capsules.size() == 1, "a corrupt slot still earns a capsule");
        check(m.capsules[0].slot == 99,
              "the corrupt slot number is carried verbatim for label resolution");
        // The label itself is resolved at paint time from the locale pair,
        // which is what panel_player.cpp:1161-1162 does; the model only
        // has to preserve the slot it came from.
    }

    // ===================== World has no wirebug system =====================

    // ---- World: an empty model, from data, not from a branch ----------
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::World;
        snap.player.valid = true;
        // Populated even though this is World: a Rise-shaped wirebug
        // vector must not be able to mount a wirebug window under World.
        mhw::WirebugSnapshot w;
        w.slot = 0; w.cooldown = 12.0F; w.maxCooldown = 30.0F;
        snap.player.wirebugs = {w};
        const mhw::WirebugModel m = mhw::buildWirebugModel(snap);
        check(m.isEmpty(),
              "World: a wirebug vector yields no capsules");
    }

    // ---- one snapshot, three independent models -----------------------
    // The three windows are three views over ONE snapshot, and none of
    // them may perturb another's answer. This is the pin on that: a
    // single snapshot carrying both games' data hands each builder the
    // same fields and each takes only its own.
    {
        mhw::GameSnapshot snap;
        snap.game = mhw::GameId::World;
        snap.player.valid = true;
        snap.player.maxHealth = 150.0F;
        snap.player.health = 120.0F;
        snap.player.stamina = 90.0F;
        snap.player.maxStamina = 150.0F;
        snap.player.mantleSlot0Id = 0;
        snap.player.mantleSlot0Timer = 42.0F;
        mhw::WirebugSnapshot w;
        w.slot = 0; w.cooldown = 12.0F; w.maxCooldown = 30.0F;
        snap.player.wirebugs = {w};

        const mhw::BarsModel bars = mhw::buildBarsModel(snap);
        const mhw::MantleModel mantles = mhw::buildMantleModel(snap);
        const mhw::WirebugModel wirebugs = mhw::buildWirebugModel(snap);
        check(!bars.isEmpty(), "World: HP/ST window has content");
        check(!mantles.isEmpty(), "World: the mantle window has content");
        check(wirebugs.isEmpty(),
              "World: the wirebug window has NONE (no wirebug system)");

        const mhw::WirebugModel riseWirebugs =
            mhw::buildWirebugModel(snap);   // same snapshot
        check(riseWirebugs.capsules.size() == 0,
              "each builder sees the same snapshot untouched");
    }

    // ---- the demo path and the live path cannot disagree --------------
    {
        mhw::GameSnapshot preview;
        preview.game = mhw::GameId::Rise;
        preview.player.valid = true;
        mhw::WirebugSnapshot w0;
        w0.slot = 0; w0.cooldown = 12.0F; w0.maxCooldown = 30.0F;
        mhw::WirebugSnapshot w1;
        w1.slot = 1; w1.isTemporary = true;
        w1.cooldown = 8.0F; w1.maxCooldown = 30.0F;
        preview.player.wirebugs = {w0, w1};
        const mhw::WirebugModel m = mhw::buildWirebugModel(preview);
        check(m.capsules.size() == 2,
              "the demo preview's three-capsule row predicts its live shape");
    }

    // ---- the three windows' slot caps are independent -----------------
    // mhw::kRiseWirebugSlotCap is the reader's own cap (4); the model
    // must not invent a different one, or the panel's "four source slots
    // maximum" comment and the split window would disagree.
    {
        check(mhw::kRiseWirebugSlotCap == 4,
              "the wirebug cap the readers use is still four source slots");
    }

    if (gFailures == 0)
        std::printf("ALL PASS: player split strip models "
                    "(bars + mantles + wirebug)\n");
    else
        std::printf("%d check(s) FAILED\n", gFailures);
    std::fflush(stdout);
    return gFailures == 0 ? 0 : 1;
}
