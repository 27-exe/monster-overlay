// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "monster/monster_types.h"

#include <QString>

#include <array>

namespace mhw {

struct PartHealthPair {
    float current{};
    float maximum{};
};

// ---------------------------------------------------------------------------
// Generated Rise part-name table (data lives in
// src/rise/data/mhr_part_names_data.cpp).
//
// struct RisePartName and the table declaration live HERE rather than inside
// the data translation unit: the 598-row array used to be a constexpr in an
// anonymous namespace, so the two static_asserts that police it could only run
// where it was defined. Declaring the struct and an extern declaration of the
// array in this header lets the data TU keep both static_asserts (they still
// compile against the definition in that TU) while the lookup functions in
// src/rise/mhr_part_names.cpp use the table across the TU boundary.
// ---------------------------------------------------------------------------
struct RisePartName {
    int monsterId;
    int partIndex;
    const char *name;    // UTF-8, Simplified Chinese, verbatim from zh-cn.xml
    const char *nameEn;  // UTF-8, English, verbatim from en-us.xml
};

// One row per (monsterId, partIndex), strictly increasing, so risePartNameEntry()
// can binary-search it. Defined in src/rise/data/mhr_part_names_data.cpp,
// which also static_asserts the ordering and that both columns are populated.
extern const std::array<RisePartName, 598> kRisePartNames;

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
