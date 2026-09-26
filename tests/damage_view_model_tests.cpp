// Pure-logic tests for mhw::DamageViewModel — the statistic that used to be
// buried inside DamagePanel's private members (and therefore paint-only and
// untested).
//
// Every case drives the model through its public update/seed entry points
// and asserts a concrete number. The model carries no widget dependency, so
// no QApplication / QT_QPA_PLATFORM=offscreen is required.
//
// The expected numbers below are the values the *unmodified* model produces
// (they were read off the implementation, not derived from the design doc).
// Where the model's behaviour is surprising, the case says so explicitly —
// this is a characterization test, not an endorsement.

#include "ui/viewmodel/damage_view_model.h"

#include "core/game_snapshot.h"
#include "player/player_types.h"
#include "quest/quest_types.h"
#include "rise/rise_damage_types.h"

#include <cstdio>

namespace {

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        if ((actual) != (expected)) {                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, \
                         #actual, #expected);                                   \
            return false;                                                      \
        }                                                                      \
    } while (false)

using mhw::DamageViewModel;

// ---------------------------------------------------------------- helpers

mhw::PartyMemberSnapshot member(const QString &name, int slot, int damage,
                                bool local, int weaponId)
{
    mhw::PartyMemberSnapshot value;
    value.name = name;
    value.slot = slot;
    value.damage = damage;
    value.local = local;
    value.weaponId = weaponId;
    return value;
}

// A World snapshot inside an active quest (state 2, id > 0) whose quest timer
// is armed, so the elapsed-time capture runs.
mhw::GameSnapshot inQuestSnapshot(const QVector<mhw::PartyMemberSnapshot> &party,
                                  float elapsed)
{
    mhw::GameSnapshot snap;
    snap.quest.id = 4242;
    snap.quest.state = 2;
    snap.quest.maxTimerSeconds = 1800.0F;
    snap.quest.elapsedSeconds = elapsed;
    snap.party = party;
    return snap;
}

mhw::GameSnapshot lobbySnapshot()
{
    mhw::GameSnapshot snap;
    snap.quest.state = 0;   // ≤1 → CLEAR
    return snap;
}

mhw::RiseDamageActor riseActor(const QString &key, const QString &name,
                               int slot, qint64 total, bool local,
                               mhw::RiseDamageActorKind kind =
                                   mhw::RiseDamageActorKind::Player,
                               int weaponId = 3, int masterRank = 100)
{
    mhw::RiseDamageActor actor;
    actor.key = key;
    actor.name = name;
    actor.kind = kind;
    actor.displaySlot = slot;
    actor.total = total;
    actor.local = local;
    actor.weaponId = weaponId;
    actor.masterRank = masterRank;
    return actor;
}

mhw::RiseDamageSnapshot riseRecording(const QVector<mhw::RiseDamageActor> &actors,
                                      int epoch)
{
    mhw::RiseDamageSnapshot snap;
    snap.valid = true;
    snap.questActive = true;      // → Record
    snap.questStateValid = true;
    snap.questEpoch = epoch;
    snap.actors = actors;
    return snap;
}

// ---------------------------------------------------------------- cases

// World: the tail of the party dropping out mid-quest keeps its frozen rows
// and their last recorded damage instead of being zeroed (the v0.5.x bug).
bool dropOutFreezesTheCarriedOverRows()
{
    DamageViewModel vm;
    const QVector<mhw::PartyMemberSnapshot> three{
        member(QStringLiteral("A27exe"), 0, 1000, true, 0),
        member(QStringLiteral("Buddy"), 1, 600, false, 1),
        member(QStringLiteral("Pal"), 2, 300, false, 2),
    };
    vm.updateWorld(inQuestSnapshot(three, 10.0F));
    vm.updateWorld(inQuestSnapshot(three, 20.0F));
    QVector<mhw::PartyMemberSnapshot> climbing = three;
    climbing[1].damage = 900;
    vm.updateWorld(inQuestSnapshot(climbing, 30.0F));

    CHECK_EQ(vm.rowCount(), 3);
    CHECK_EQ(vm.names()[1], QStringLiteral("Buddy"));
    CHECK_EQ(vm.left().value(1), false);
    CHECK_EQ(vm.history().size(), 3);
    CHECK_EQ(vm.tick(), 3);

    // Party shrinks to one member. The layout must NOT shrink: the two
    // dropped rows stay visible, flagged, and frozen at their last sample.
    const QVector<mhw::PartyMemberSnapshot> alone{
        member(QStringLiteral("A27exe"), 0, 2000, true, 0),
    };
    vm.updateWorld(inQuestSnapshot(alone, 40.0F));

    CHECK_EQ(vm.rowCount(), 3);
    CHECK_EQ(vm.left().value(0), false);       // the live row stays live
    CHECK_EQ(vm.left().value(1), true);        // Buddy's row is frozen
    CHECK_EQ(vm.left().value(2), true);        // Pal's row is frozen
    CHECK_EQ(vm.names()[1], QStringLiteral("Buddy"));   // name preserved
    CHECK_EQ(vm.history().last().damage.value(1), 300); // 900 - 600 baseline
    CHECK_EQ(vm.history().last().damage.value(2), 0);   // 300 - 300 baseline
    // The live row advances on this same frame: 2000 - 1000 = 1000, and its
    // DPS is measured from the first-hit tick 1 → 1000 × 4 / 2.
    CHECK_EQ(vm.history().last().damage.value(0), 1000);
    CHECK_EQ(vm.computeDps(0), 2000);
    return true;
}

// Rise: the quest epoch is the authoritative hunt identity. A new epoch
// wipes the history, the identity vectors and the DPS baselines, even
// though no invalid frame was delivered between the two hunts.
bool riseEpochChangeResetsHistory()
{
    mhw::RiseDamageDisplayOptions options;
    options.showOtherMembers = true;

    DamageViewModel vm;
    const QVector<mhw::RiseDamageActor> actors{
        riseActor(QStringLiteral("k-local"), QStringLiteral("A27exe"), 0, 5000, true),
        riseActor(QStringLiteral("k-buddy"), QStringLiteral("Buddy"), 1, 3000, false),
    };

    vm.updateRise(riseRecording(actors, 7), options);
    CHECK_EQ(vm.rowCount(), 2);
    CHECK_EQ(vm.tick(), 1);
    CHECK_EQ(vm.history().size(), 1);
    CHECK_EQ(vm.hasData(), true);

    vm.updateRise(riseRecording(actors, 7), options);   // same epoch
    CHECK_EQ(vm.tick(), 2);
    CHECK_EQ(vm.history().size(), 2);
    CHECK_EQ(vm.riseQuestEpoch(), 7);
    CHECK_EQ(vm.hasRiseQuestEpoch(), true);

    // A later frame carries a different epoch while totals keep rising: the
    // whole previous hunt must be discarded, not continued.
    QVector<mhw::RiseDamageActor> next{
        riseActor(QStringLiteral("k-local"), QStringLiteral("A27exe"), 0, 9000, true),
        riseActor(QStringLiteral("k-buddy"), QStringLiteral("Buddy"), 1, 6000, false),
    };
    vm.updateRise(riseRecording(next, 8), options);

    CHECK_EQ(vm.riseQuestEpoch(), 8);
    CHECK_EQ(vm.history().size(), 1);
    CHECK_EQ(vm.tick(), 1);
    CHECK_EQ(vm.history().last().tick, 0);
    CHECK_EQ(vm.history().last().damage.value(0), 9000);
    CHECK_EQ(vm.history().last().damage.value(1), 6000);
    return true;
}

// World: once the quest ends (settlement state 3..7) the model freezes —
// no further samples, no further tick, no DPS growth — and only a fresh
// quest restart or the lobby clears it.
bool questEndFreezesDps()
{
    DamageViewModel vm;
    const QVector<mhw::PartyMemberSnapshot> solo{
        member(QStringLiteral("A27exe"), 0, 1000, true, 0),
    };
    vm.updateWorld(inQuestSnapshot(solo, 5.0F));
    QVector<mhw::PartyMemberSnapshot> f1 = solo;
    f1[0].damage = 1500;
    vm.updateWorld(inQuestSnapshot(f1, 15.0F));
    QVector<mhw::PartyMemberSnapshot> f2 = f1;
    f2[0].damage = 2000;
    vm.updateWorld(inQuestSnapshot(f2, 25.0F));

    CHECK_EQ(vm.questEnded(), false);
    CHECK_EQ(vm.history().size(), 3);
    CHECK_EQ(vm.history().last().damage.value(0), 500);   // 2000 - 1500
    const int dpsBefore = vm.computeDps(0);
    CHECK_EQ(dpsBefore, 2000);                            // 500 × 4 / 1

    // Result screen: freeze immediately, chart untouched.
    mhw::GameSnapshot result = inQuestSnapshot(f2, 39.0F);
    result.quest.state = 3;                        // Success
    vm.updateWorld(result);
    CHECK_EQ(vm.questEnded(), true);
    const int samplesAtFreeze = vm.history().size();
    const int tickAtFreeze = vm.tick();

    // More settlement frames (counter still "rising" in memory): nothing
    // moves — no samples, no ticks, no DPS growth.
    for (int i = 0; i < 4; ++i)
        vm.updateWorld(result);
    CHECK_EQ(vm.history().size(), samplesAtFreeze);
    CHECK_EQ(vm.tick(), tickAtFreeze);
    CHECK_EQ(vm.computeDps(0), dpsBefore);
    CHECK_EQ(vm.lastElapsedSeconds(), 39.0F);   // timer cached before freeze

    // Lobby (state ≤1): everything clears.
    vm.updateWorld(lobbySnapshot());
    CHECK_EQ(vm.questEnded(), false);
    CHECK_EQ(vm.hasData(), false);
    CHECK_EQ(vm.history().size(), 0);
    CHECK_EQ(vm.tick(), 0);
    CHECK_EQ(vm.rowCount(), 0);
    CHECK_EQ(vm.lastElapsedSeconds(), 0.0F);

    // New quest: recording resumes from a clean slate. The first frame of a
    // hunt takes its baseline at the current counter, so its sample is 0.
    QVector<mhw::PartyMemberSnapshot> grown = solo;
    grown[0].damage = 2500;
    vm.updateWorld(inQuestSnapshot(grown, 12.0F));
    CHECK_EQ(vm.hasData(), true);
    CHECK_EQ(vm.history().size(), 1);
    CHECK_EQ(vm.history().last().damage.value(0), 0);
    CHECK_EQ(vm.computeDps(0), 0);
    return true;
}

// World: firstHitTick / baselineDamage. The baseline is the counter value on
// the row's first non-zero frame, and a counter that drops below its
// baseline is rebaselined instead of producing a negative spike.
bool firstHitBaselineExcludesPreHitDamage()
{
    DamageViewModel vm;
    const QVector<mhw::PartyMemberSnapshot> preHit{
        member(QStringLiteral("A27exe"), 0, 0, true, 0),
        member(QStringLiteral("Buddy"), 1, 500, false, 1),
    };
    vm.updateWorld(inQuestSnapshot(preHit, 0.0F));

    // Row 0 has not dealt damage yet: no baseline, zero sample, zero DPS.
    CHECK_EQ(vm.computeDps(0), 0);
    CHECK_EQ(vm.history().last().damage.value(0), 0);

    // Second frame: both rows have damage now. Because a first hit recorded
    // on tick 0 is indistinguishable from "never hit", the identity pass
    // re-captures the baseline on this frame, so the samples stay 0 — that
    // is the model's existing (frozen) behaviour.
    QVector<mhw::PartyMemberSnapshot> hitting = preHit;
    hitting[0].damage = 300;
    hitting[1].damage = 1500;
    vm.updateWorld(inQuestSnapshot(hitting, 5.0F));
    CHECK_EQ(vm.history().last().damage.value(0), 0);
    CHECK_EQ(vm.history().last().damage.value(1), 0);

    // Third frame: baselines are now stable (300 / 1500), so the deltas are
    // real and DPS is measured across one tick.
    QVector<mhw::PartyMemberSnapshot> later = hitting;
    later[0].damage = 600;
    later[1].damage = 2500;
    vm.updateWorld(inQuestSnapshot(later, 10.0F));
    CHECK_EQ(vm.history().last().damage.value(0), 300);    // 600 - 300
    CHECK_EQ(vm.history().last().damage.value(1), 1000);   // 2500 - 1500
    CHECK_EQ(vm.computeDps(0), 1200);                      // 300 × 4 / 1
    CHECK_EQ(vm.computeDps(1), 4000);                      // 1000 × 4 / 1

    // A counter that drops below its baseline is rebaselined instead of
    // producing a giant negative spike in the chart.
    QVector<mhw::PartyMemberSnapshot> reset = later;
    reset[1].damage = 1200;
    vm.updateWorld(inQuestSnapshot(reset, 15.0F));
    CHECK_EQ(vm.history().last().damage.value(1), 0);
    return true;
}

// World: party shrink. The row set never grows beyond the reader's party
// cap, never invents rows, never drops a frozen one, and a party that
// empties entirely keeps the carry-over names while reporting no data.
bool partyShrinkKeepsRowsAndNeverInvites()
{
    DamageViewModel vm;
    const QVector<mhw::PartyMemberSnapshot> full{
        member(QStringLiteral("One"), 0, 100, true, 0),
        member(QStringLiteral("Two"), 1, 100, false, 1),
        member(QStringLiteral("Three"), 2, 100, false, 2),
        member(QStringLiteral("Four"), 3, 100, false, 3),
    };
    vm.updateWorld(inQuestSnapshot(full, 1.0F));
    CHECK_EQ(vm.rowCount(), 4);

    // Shrink to a single member with a raised counter.
    QVector<mhw::PartyMemberSnapshot> alone{
        member(QStringLiteral("One"), 0, 200, true, 0),
    };
    vm.updateWorld(inQuestSnapshot(alone, 6.0F));
    CHECK_EQ(vm.rowCount(), 4);               // carry-over rows preserved
    CHECK_EQ(vm.left().value(0), false);
    CHECK_EQ(vm.left().value(1), true);
    CHECK_EQ(vm.left().value(2), true);
    CHECK_EQ(vm.left().value(3), true);

    // Third frame: the live row finally advances (baseline 200 → 300).
    QVector<mhw::PartyMemberSnapshot> growing{
        member(QStringLiteral("One"), 0, 300, true, 0),
    };
    vm.updateWorld(inQuestSnapshot(growing, 11.0F));
    CHECK_EQ(vm.history().last().damage.value(0), 100);
    CHECK_EQ(vm.left().value(1), true);       // still frozen

    // A party that empties entirely clears the samples and the tick but
    // keeps the carry-over names and flags.
    vm.updateWorld(inQuestSnapshot({}, 16.0F));
    CHECK_EQ(vm.hasData(), false);
    CHECK_EQ(vm.history().size(), 0);
    CHECK_EQ(vm.tick(), 0);
    CHECK_EQ(vm.rowCount(), 4);
    CHECK_EQ(vm.names()[3], QStringLiteral("Four"));
    CHECK_EQ(vm.left().value(3), true);

    // A fifth member in a later frame is ignored (the reader cap is 4).
    QVector<mhw::PartyMemberSnapshot> five = full;
    five.append(member(QStringLiteral("Five"), 4, 10, false, 4));
    vm.updateWorld(inQuestSnapshot(five, 21.0F));
    CHECK_EQ(vm.rowCount(), 4);
    CHECK_EQ(vm.names()[3], QStringLiteral("Four"));
    return true;
}

// Rise: the row filter. Pets and Unknown actors never enter the main damage
// table, and "other members off" leaves only the local player.
bool riseRowFilterAndOptionGate()
{
    mhw::RiseDamageDisplayOptions all;
    all.showOtherMembers = true;
    mhw::RiseDamageDisplayOptions soloOnly;
    soloOnly.showOtherMembers = false;

    const QVector<mhw::RiseDamageActor> roster{
        riseActor(QStringLiteral("p-remote"), QStringLiteral("Remote"), 2, 100, false),
        riseActor(QStringLiteral("p-local"), QStringLiteral("A27exe"), 0, 900, true),
        riseActor(QStringLiteral("p-pet"), QStringLiteral("Canyne"), 3, 50, false,
                  mhw::RiseDamageActorKind::Palico),
        riseActor(QStringLiteral("p-unknown"), QStringLiteral("???"), 4, 70, false,
                  mhw::RiseDamageActorKind::Unknown),
        riseActor(QStringLiteral("p-local-pet"), QStringLiteral("Own Doggo"), 1, 60, true,
                  mhw::RiseDamageActorKind::Palamute),
    };

    DamageViewModel allVm;
    allVm.updateRise(riseRecording(roster, 1), all);
    CHECK_EQ(allVm.rowCount(), 2);                 // local first, then remote
    CHECK_EQ(allVm.names()[0], QStringLiteral("A27exe"));
    CHECK_EQ(allVm.riseKeys()[0], QStringLiteral("p-local"));
    CHECK_EQ(allVm.names()[1], QStringLiteral("Remote"));
    CHECK_EQ(allVm.locals().value(0), true);
    CHECK_EQ(allVm.locals().value(1), false);

    DamageViewModel soloVm;
    soloVm.updateRise(riseRecording(roster, 1), soloOnly);
    CHECK_EQ(soloVm.rowCount(), 1);
    CHECK_EQ(soloVm.names()[0], QStringLiteral("A27exe"));

    // An actor list with no supported actor removes existing rows rather
    // than leaving a stale member behind.
    DamageViewModel clearVm;
    clearVm.updateRise(riseRecording(roster, 1), all);
    CHECK_EQ(clearVm.hasData(), true);
    const QVector<mhw::RiseDamageActor> onlyPets{
        riseActor(QStringLiteral("p-pet"), QStringLiteral("Canyne"), 3, 50, false,
                  mhw::RiseDamageActorKind::Palico),
    };
    clearVm.updateRise(riseRecording(onlyPets, 1), all);
    CHECK_EQ(clearVm.hasData(), false);
    CHECK_EQ(clearVm.rowCount(), 0);
    return true;
}

// Rise: an invalid feed (missing / stale file) clears the model; a
// transient failure (Keep) preserves the last visible frame; Freeze latches
// questEnded only when data had been recorded.
bool riseLifecycleClearKeepAndFreeze()
{
    mhw::RiseDamageDisplayOptions options;
    options.showOtherMembers = true;

    DamageViewModel vm;
    const QVector<mhw::RiseDamageActor> actors{
        riseActor(QStringLiteral("k1"), QStringLiteral("A27exe"), 0, 800, true),
    };
    vm.updateRise(riseRecording(actors, 5), options);
    CHECK_EQ(vm.rowCount(), 1);
    CHECK_EQ(vm.history().size(), 1);

    // Transient source failure → Keep → the frozen frame survives.
    mhw::RiseDamageSnapshot paused;
    paused.valid = true;
    paused.questStateValid = false;
    vm.updateRise(paused, options);
    CHECK_EQ(vm.rowCount(), 1);
    CHECK_EQ(vm.history().size(), 1);
    CHECK_EQ(vm.hasRiseQuestEpoch(), true);   // epoch not forgotten

    // Stale / invalid feed → Clear → everything resets.
    const mhw::RiseDamageSnapshot feedLost;   // valid == false
    vm.updateRise(feedLost, options);
    CHECK_EQ(vm.hasData(), false);
    CHECK_EQ(vm.rowCount(), 0);
    CHECK_EQ(vm.history().size(), 0);
    CHECK_EQ(vm.tick(), 0);
    CHECK_EQ(vm.hasRiseQuestEpoch(), false);

    // Freeze requires previously-recorded data to latch questEnded_.
    vm.updateRise(riseRecording(actors, 6), options);
    CHECK_EQ(vm.questEnded(), false);
    mhw::RiseDamageSnapshot ended = riseRecording(actors, 6);
    ended.questActive = false;
    ended.questState = 3;                     // → Freeze
    vm.updateRise(ended, options);
    CHECK_EQ(vm.questEnded(), true);
    const int samplesAtFreeze = vm.history().size();
    vm.updateRise(ended, options);            // more settlement frames
    CHECK_EQ(vm.history().size(), samplesAtFreeze);
    CHECK_EQ(vm.tick(), samplesAtFreeze);
    return true;
}

// The demo seed is what the pixel baseline renders: row identities, the
// 8-sample curve, the seeded 411 s timer and the resulting DPS numbers are
// all pinned here so a change in the model cannot silently alter the
// pixels of tests/baseline/all_damage.png.
bool demoSeedMatchesBaseline()
{
    DamageViewModel vm;
    QVector<DamageViewModel::DemoRow> party;
    party.append({QStringLiteral("A27exe"), 0, 247, 0});
    party.append({QStringLiteral("b"), 1, 500, 1});
    party.append({QStringLiteral("c"), 12, 300, 2});
    party.append({QStringLiteral("d"), 4, 250, 3});
    vm.seedDemoData(party, 411.0F);

    CHECK_EQ(vm.hasData(), true);
    CHECK_EQ(vm.rowCount(), 4);
    CHECK_EQ(vm.names()[0], QStringLiteral("A27exe"));
    CHECK_EQ(vm.weaponIds()[2], 12);
    CHECK_EQ(vm.masterRanks()[1], 500);
    CHECK_EQ(vm.partySlots()[3], 3);
    CHECK_EQ(vm.history().size(), 8);
    CHECK_EQ(vm.tick(), 8);
    CHECK_EQ(vm.lastElapsedSeconds(), 411.0F);

    // 0..7 ticks, super-linear growth: sample t of player 0 is
    // round(184220 * (0.10 + 0.90 * (t/7)^2)).
    CHECK_EQ(vm.history()[0].tick, 0);
    CHECK_EQ(vm.history()[7].tick, 7);
    CHECK_EQ(vm.history()[0].damage.value(0), 18422);
    CHECK_EQ(vm.history()[3].damage.value(0), 48874);
    CHECK_EQ(vm.history()[7].damage.value(0), 184220);
    CHECK_EQ(vm.history()[7].damage.value(1), 96240);
    CHECK_EQ(vm.history()[7].damage.value(2), 71030);
    CHECK_EQ(vm.history()[7].damage.value(3), 40510);
    // Monotonic: every sample is >= the previous one.
    for (int i = 1; i < vm.history().size(); ++i) {
        if (vm.history()[i].damage.value(0) < vm.history()[i - 1].damage.value(0))
            return false;
    }
    // DPS is measured from first-hit tick 1: damage over the elapsed ticks.
    CHECK_EQ(vm.computeDps(0), 122813);
    CHECK_EQ(vm.computeDps(1), 64160);
    CHECK_EQ(vm.computeDps(3), 27006);
    CHECK_EQ(vm.computeDps(4), 0);         // out of range
    CHECK_EQ(vm.computeDps(-1), 0);        // negative index
    return true;
}

// World: a damage-counter rollback. The per-row path rebaselines to the new
// counter and records 0, and the *deferred* cross-frame new-hunt detector
// does not fire — because a live row's rawDamage_ was already overwritten
// with this frame's value before the comparison runs, the
// "snap value < rawDamage_" test can never be true for a live row. This
// case pins that behaviour so a future fix to the detector is deliberate.
bool counterRollbackIsAbsorbedByTheReBaseline()
{
    DamageViewModel vm;
    const QVector<mhw::PartyMemberSnapshot> first{
        member(QStringLiteral("A27exe"), 0, 5000, true, 0),
    };
    vm.updateWorld(inQuestSnapshot(first, 30.0F));
    vm.updateWorld(inQuestSnapshot(first, 35.0F));
    CHECK_EQ(vm.history().size(), 2);

    // Counter rewinds to a smaller value while the quest is still active.
    QVector<mhw::PartyMemberSnapshot> second = first;
    second[0].damage = 120;
    vm.updateWorld(inQuestSnapshot(second, 41.0F));

    // The sample is rebaselined to 0 (not negative) and the chart keeps
    // growing; the deferred new-hunt reset does not trip.
    CHECK_EQ(vm.history().size(), 3);
    CHECK_EQ(vm.tick(), 3);
    CHECK_EQ(vm.history().last().damage.value(0), 0);
    CHECK_EQ(vm.history().last().tick, 2);
    CHECK_EQ(vm.lastElapsedSeconds(), 41.0F);
    return true;
}

} // namespace

int main()
{
    struct { const char *name; bool (*fn)(); } cases[] = {
        {"world drop-out freezes carry-over rows",     dropOutFreezesTheCarriedOverRows},
        {"rise quest epoch change resets history",     riseEpochChangeResetsHistory},
        {"quest end freezes dps",                      questEndFreezesDps},
        {"first-hit baseline excludes pre-hit dmg",    firstHitBaselineExcludesPreHitDamage},
        {"party shrink keeps rows, never invites",     partyShrinkKeepsRowsAndNeverInvites},
        {"rise row filter and option gate",            riseRowFilterAndOptionGate},
        {"rise lifecycle clear keep and freeze",       riseLifecycleClearKeepAndFreeze},
        {"demo seed matches the pixel baseline",       demoSeedMatchesBaseline},
        {"counter rollback absorbed by re-baseline",   counterRollbackIsAbsorbedByTheReBaseline},
    };

    int failed = 0;
    for (const auto &c : cases) {
        const bool ok = c.fn();
        std::fprintf(ok ? stdout : stderr, "%s: %s\n",
                     ok ? "PASS" : "FAIL", c.name);
        if (!ok)
            ++failed;
    }
    if (failed > 0) {
        std::fprintf(stderr, "%d of %zu cases FAILED\n",
                     failed, sizeof(cases) / sizeof(cases[0]));
        return 1;
    }
    std::fprintf(stdout, "all %zu cases passed\n",
                 sizeof(cases) / sizeof(cases[0]));
    return 0;
}
