// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "monster/monster_types.h"

#include <QString>

namespace mhw {

struct PartHealthPair {
    float current{};
    float maximum{};
};

// Official Rise part name for (monsterId, partIndex), generated from HunterPie
// Game/Rise/Data/MonsterData.xml joined to the official localization
// (zh-cn.xml + en-us.xml, both /Strings/Monsters/Shared/Part[@Id]); see
// scripts/generate_rise_part_names.py for the sha256-verified provenance.
// Returns nullptr for an unknown monster or part index so callers can preserve
// the safe "部位 N" fallback, and also for the upstream PART_TO_BE_MAPPED
// placeholder ("Unknown" in both locales).
//
// Locale selection (v0.9 i18n, WS-B): the en-us.xml column while
// mhw::StringTable::instance().isEnglish() is true, the zh-cn.xml column
// otherwise. An empty locale (before any load()) reads as Chinese, so the
// pre-i18n behaviour is the default.
const char *risePartName(int monsterId, int partIndex);

// The en-us.xml column, independent of the active locale (tests / tooling).
// nullptr under exactly the same conditions as risePartName().
const char *risePartNameEn(int monsterId, int partIndex);

// Locale-aware display name: the localized part name, or the localized
// fallback "部位 N" / "Part N" when the table has no usable entry.
QString risePartDisplayName(int monsterId, int partIndex);

// Compact current/max labels for the narrow per-part cards, e.g. "34k/57k".
QString compactPartHealth(float current, float maximum);

// HunterPie selects Sever/MaxSever for severable parts, otherwise
// Health/MaxHealth for breakable parts. The generic flinch branch is retained
// for existing non-Rise callers but Rise filters flinch-only parts before UI.
PartHealthPair partHealthForDisplay(const PartSnapshot &part);

} // namespace mhw
