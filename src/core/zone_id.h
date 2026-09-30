#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// zone_id.h — the game-agnostic zone identifier.
//
// WHY THIS EXISTS
// ---------------
// GameSnapshot used to carry `world::Zone`, a single flat enum declared in
// src/world/world_types.h. That made src/core/game_snapshot.h include a World
// header, which is the one edge that stops `monster-core` from being a
// game-agnostic kernel: a World header reachable from core means core knows
// what "World" is. Adding a third game would have meant either widening that
// enum again or reaching sideways into another game's headers.
//
// ZoneRef replaces the enum on the snapshot with a (game, id) pair:
//
//   * the *contract* (which game, which numeric id) is game-agnostic and
//     lives here, in core;
//   * the *vocabulary* (what id 705 is called, whether it is a hunting zone)
//     stays with the game that owns it — src/world/ and src/rise/.
//
// The numeric id is an int, NOT an enum, on purpose. Measured at HEAD 7fa2b5e,
// the runtime value domain is wider than any enumeration can honestly track:
//
//   World:  readZone() (world_reader.cpp:299) passes the raw int32 through
//           with only a 0..1000 range filter, so an unmapped value arrives as a
//           bare integer, never as a named entry.
//   Rise:   computeZoneId() (mhr_reader.cpp:37-49) emits -2 for LoadingScreen
//           and 199 for CharSelection — neither has an enum entry at all —
//           plus stage.villageId + 700 for villages and huntingId + 600 for
//           hunting grounds.
//
// Enumerating -2 / 199 / 0..100 would mean a lookup table that can never keep
// up with a game patch, and a new game would inherit that liability. Carrying
// the id as an int keeps the data layer honest: "we read 199" is a fact, and
// whether anyone can put a name on it is a separate question the vocabulary
// layer answers (or doesn't).
//
// Hence `hasZoneName()` stays on the game side. A caller asks the owning
// game whether an id is named; it never inspects a label string to find out.

#include "monster/monster_types.h" // GameId

namespace mhw {

// The id that means "no zone was read". Matches the historical Zone::Unknown
// = -1, so a reader that produces -1 needs no translation.
inline constexpr int kZoneUnknownId = -1;

// One zone of one game. Default-constructs to the unknown zone of World so a
// snapshot that never saw a zone still round-trips through a comparison
// instead of reading as a real value.
struct ZoneRef {
    GameId game{GameId::World};
    int id{kZoneUnknownId};
};

[[nodiscard]] constexpr bool operator==(const ZoneRef &a, const ZoneRef &b)
{
    return a.game == b.game && a.id == b.id;
}

[[nodiscard]] constexpr bool operator!=(const ZoneRef &a, const ZoneRef &b)
{
    return !(a == b);
}

// True when the id carries no information — either the explicit -1 or a value
// the reader has not classified. NOTE: this is deliberately NOT the same
// question as "does this zone have a name" (hasZoneName, on the game side).
// A named zone can still be a non-hunting zone, and an unnamed id is not
// necessarily unknown.
[[nodiscard]] constexpr bool isUnknownZone(const ZoneRef &z)
{
    return z.id == kZoneUnknownId;
}

} // namespace mhw
