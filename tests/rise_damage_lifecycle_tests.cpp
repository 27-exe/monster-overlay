#include "rise/rise_damage_types.h"

#include <cstdio>

namespace {

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        if ((actual) != (expected)) {                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s != %s\n", __FILE__, __LINE__,\
                         #actual, #expected);                                   \
            return false;                                                      \
        }                                                                      \
    } while (false)

bool lifecycleActionsAreExplicit()
{
    using mhw::RiseDamageLifecycleAction;
    using mhw::RiseDamageSnapshot;

    RiseDamageSnapshot snapshot;
    snapshot.valid = false;
    CHECK_EQ(mhw::riseDamageLifecycleAction(snapshot),
             RiseDamageLifecycleAction::Clear);

    snapshot.valid = true;
    snapshot.questStateValid = false;
    snapshot.collectionPaused = true;
    CHECK_EQ(mhw::riseDamageLifecycleAction(snapshot),
             RiseDamageLifecycleAction::Keep);

    snapshot.questStateValid = true;
    snapshot.collectionPaused = false;
    snapshot.questActive = true;
    snapshot.questState = 0;
    snapshot.training = true;
    CHECK_EQ(mhw::riseDamageLifecycleAction(snapshot),
             RiseDamageLifecycleAction::Record);

    snapshot.questActive = false;
    snapshot.training = false;
    for (int state = 3; state <= 7; ++state) {
        snapshot.questState = state;
        CHECK_EQ(mhw::riseDamageLifecycleAction(snapshot),
                 RiseDamageLifecycleAction::Freeze);
    }

    for (const int state : {0, 1, 2, 8}) {
        snapshot.questState = state;
        CHECK_EQ(mhw::riseDamageLifecycleAction(snapshot),
                 RiseDamageLifecycleAction::Clear);
    }
    return true;
}

bool epochPredicateMatchesBothOriginalExpressions()
{
    using mhw::RiseDamageSnapshot;

    RiseDamageSnapshot snapshot;
    snapshot.valid = true;
    snapshot.questActive = true;
    snapshot.questState = 0;
    snapshot.questEpoch = 7;

    // First-ever frame: no previous epoch, so it is never a "new hunt".
    // This is the guard both call sites kept; without it the very first
    // Record frame would wipe rows it had just been handed.
    CHECK_EQ(mhw::riseDamageQuestEpochChanged(
                 /*hasPrevEpoch=*/false, /*prevEpoch=*/0, snapshot),
             false);
    CHECK_EQ(mhw::riseDamageQuestEpochChanged(
                 /*hasPrevEpoch=*/true, /*prevEpoch=*/0, snapshot),
             true);

    // Same epoch, however remembered: not a change.
    CHECK_EQ(mhw::riseDamageQuestEpochChanged(true, 7, snapshot), false);

    // A different epoch is a change — and the comparison is strict, so the
    // direction of the move (backwards as well as forwards) still resets.
    snapshot.questEpoch = 8;
    CHECK_EQ(mhw::riseDamageQuestEpochChanged(true, 7, snapshot), true);
    CHECK_EQ(mhw::riseDamageQuestEpochChanged(false, 7, snapshot), false);

    snapshot.questEpoch = 6;
    CHECK_EQ(mhw::riseDamageQuestEpochChanged(true, 7, snapshot), true);

    // The predicate is the single shared source of truth for both consumers
    // (DamageViewModel::updateRise and PetDamagePanel::updateRiseDamage), so
    // it must be a pure function of its arguments: identical inputs give an
    // identical verdict no matter which consumer asks, and it must not touch
    // the snapshot it was handed.
    const RiseDamageSnapshot before = snapshot;
    for (int call = 0; call < 3; ++call) {
        const bool verdict = mhw::riseDamageQuestEpochChanged(
            /*hasPrevEpoch=*/true, /*prevEpoch=*/7, snapshot);
        CHECK_EQ(verdict, true);
    }
    CHECK_EQ(snapshot.questEpoch, before.questEpoch);
    CHECK_EQ(snapshot.valid, before.valid);
    CHECK_EQ(snapshot.questActive, before.questActive);
    CHECK_EQ(snapshot.questState, before.questState);

    // Exhaustively: for every (hasPrev, prev, epoch) combination the verdict
    // equals the boolean expression both consumers used to inline. This is
    // the equivalence proof the refactor rests on, expressed as a test.
    for (int prev = -2; prev <= 2; ++prev) {
        for (int epoch = -1; epoch <= 3; ++epoch) {
            RiseDamageSnapshot frame;
            frame.valid = true;
            frame.questActive = true;
            frame.questState = 0;
            frame.questEpoch = epoch;
            for (const bool hasPrev : {false, true}) {
                // The old inline form at both call sites, verbatim.
                const bool oldForm = hasPrev && frame.questEpoch != prev;
                const bool newForm = mhw::riseDamageQuestEpochChanged(
                    hasPrev, prev, frame);
                CHECK_EQ(newForm, oldForm);
            }
        }
    }
    return true;
}

} // namespace

int main()
{
    if (!lifecycleActionsAreExplicit())
        return 1;
    if (!epochPredicateMatchesBothOriginalExpressions())
        return 1;
    std::fprintf(stderr, "PASS Rise damage lifecycle actions\n");
    return 0;
}
