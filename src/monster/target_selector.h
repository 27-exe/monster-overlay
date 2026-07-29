#pragma once

#include "monster_types.h"

#include <cstdint>

namespace mhw {

// HunterPie default TargetMode is LockOn. Returns the selected vector index,
// or -1 when no live monster exists. currentAddress stabilizes the single-panel
// fallback while the game has no lock-on target.
int selectMonsterTarget(const QVector<MonsterSnapshot> &monsters,
                        std::uintptr_t currentAddress);

} // namespace mhw
