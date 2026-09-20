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

} // namespace

int main()
{
    if (!lifecycleActionsAreExplicit())
        return 1;
    std::fprintf(stderr, "PASS Rise damage lifecycle actions\n");
    return 0;
}
