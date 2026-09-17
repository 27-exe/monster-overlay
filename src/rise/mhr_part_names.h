// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "monster/monster_types.h"

#include <QString>

namespace mhw {

struct PartHealthPair {
    float current{};
    float maximum{};
};

// Official Simplified Chinese Rise part name generated from HunterPie
// MonsterData.xml and zh-cn.xml. Returns nullptr for an unknown monster or
// part index so callers can preserve the safe "部位 N" fallback.
const char *risePartName(int monsterId, int partIndex);

QString risePartDisplayName(int monsterId, int partIndex);

// Compact current/max labels for the narrow per-part cards, e.g. "34k/57k".
QString compactPartHealth(float current, float maximum);

// HunterPie selects Sever/MaxSever for severable parts, otherwise
// Health/MaxHealth for breakable parts. The generic flinch branch is retained
// for existing non-Rise callers but Rise filters flinch-only parts before UI.
PartHealthPair partHealthForDisplay(const PartSnapshot &part);

} // namespace mhw
