// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "core/game_snapshot.h"
#include "rise/rise_damage_types.h"

namespace mhw {

// Joins the REFramework damage producer's stable entity ids to the independently
// read Rise roster. An actor is changed only when its entity/owner id has one
// unambiguous roster match; missing or duplicate ids preserve producer data.
// Raw follower-layout streams are attributed through the same roster once the
// session shape proves them: entity 4 is the single local hunter's second buddy
// (solo or follower session), entities 5/6 are follower-slot 0/1 buddies.
void enrichRiseDamageSnapshot(RiseDamageSnapshot &damage,
                              const GameSnapshot &game);

} // namespace mhw
