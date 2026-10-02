// SPDX-License-Identifier: Apache-2.0
//
// zone_id_tests.cpp — the game-agnostic zone contract.
//
// ZoneRef is what replaced a flat World-only enum on GameSnapshot. Nothing
// about the *vocabulary* belongs here (that is world_types.cpp / rise_zones.h);
// what is asserted is the *contract* core actually owns:
//
//   * a ZoneRef compares by (game, id), so two games' zone 0 are not the same
//     zone and must not compare equal;
//   * the default-constructed value round-trips as unknown rather than as a
//     plausible-looking id 0;
//   * isUnknownZone asks "did the reader fail to read a zone", which is a
//     different question from "does this zone have a name" — the latter is
//     answered by hasZoneName on the game side, and a test that blurred the two
//     would lock in exactly the confusion this header's comment warns about.

#include "core/zone_id.h"

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("  FAIL  %s\n", what);
        ++g_failures;
    } else {
        std::printf("  ok    %s\n", what);
    }
}

} // namespace

// GameId is declared in monster/monster_types.h, which zone_id.h includes to
// reach the GameId member. It is not re-exported into mhw by that header, so
// spell it once here rather than at every use site.
using mhw::GameId;

int main()
{
    using mhw::ZoneRef;
    using mhw::isUnknownZone;
    using mhw::kZoneUnknownId;

    std::printf("zone_id: identity is (game, id), not id alone\n");

    // The whole reason the type is a pair: the same numeric id under a
    // different game is a different place.
    const ZoneRef worldVillage{GameId::World, 705};
    const ZoneRef riseVillage{GameId::Rise, 705};
    check(!(worldVillage == riseVillage), "same id, different game -> not equal");
    check(worldVillage != riseVillage, "same id, different game -> != holds");

    // ... and the game half is the discriminator, not incidental.
    const ZoneRef worldVillageCopy{GameId::World, 705};
    check(worldVillage == worldVillageCopy, "same id, same game -> equal");
    check(!(worldVillage != worldVillageCopy), "same id, same game -> != is false");

    std::printf("zone_id: default is unknown, not a plausible id\n");

    // A default-constructed ZoneRef must not read as a real zone. If the
    // default were 0 it would silently render as whatever game zone 0 is.
    const ZoneRef defaulted;
    check(defaulted.id == kZoneUnknownId, "default id is the unknown sentinel");
    check(isUnknownZone(defaulted), "default reads as unknown");
    check(defaulted.game == GameId::World, "default game is World");
    check(!isUnknownZone(worldVillage), "a real id is not unknown");

    std::printf("zone_id: unknown is about the read, not about naming\n");

    // The distinction the header exists to protect: an unnamed id is not the
    // same as an unknown one, and a named zone can be non-hunting. core cannot
    // answer either question — that is what hasZoneName is for on the game side.
    // Asserting only that core *declines* to conflate them.
    const ZoneRef charSelection{GameId::Rise, 199}; // emitted by computeZoneId
    const ZoneRef loadingScreen{GameId::Rise, -2}; // emitted by computeZoneId
    check(!isUnknownZone(charSelection), "Rise 199 is read-but-unnamed, not unknown");
    check(!isUnknownZone(loadingScreen), "Rise -2 is read-but-unnamed, not unknown");
    check(isUnknownZone({GameId::Rise, kZoneUnknownId}),
          "unknown id stays unknown under any game");

    std::printf("zone_id: negative and out-of-range ids are data, not errors\n");

    // The measured runtime domain is wider than an enum could track. These must
    // round-trip as plain values rather than being rejected or clamped.
    const ZoneRef wide{GameId::World, 999999};
    check(wide.id == 999999, "large id passes through unchanged");
    check(!isUnknownZone(wide), "large id is not mistaken for unknown");
    const ZoneRef negative{mhw::GameId::World, -42};
    check(negative.id == -42, "negative id passes through unchanged");
    check(negative != defaulted, "a negative id differs from the default");

    if (g_failures == 0) {
        std::printf("zone_id: all checks passed\n");
        return 0;
    }
    std::printf("zone_id: %d check(s) FAILED\n", g_failures);
    return 1;
}
