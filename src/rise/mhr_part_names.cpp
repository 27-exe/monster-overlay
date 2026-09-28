// SPDX-License-Identifier: Apache-2.0
//
// Lookup layer for the Rise part-name table.
//
// The DATA (struct RisePartName, the 598-row kRisePartNames table and the two
// static_asserts that police it) lives in src/rise/data/mhr_part_names_data.cpp.
// This translation unit holds only the LOGIC: risePartNameEntry()'s binary
// search plus the locale-aware wrappers risePartName() / risePartNameEn() /
// risePartDisplayName() and the part-card formatting helpers.
//
// The table used to be a constexpr array inside an anonymous namespace in this
// file, which gave it internal linkage — no other TU could see it, and the
// static_asserts had no choice but to sit next to the rows. It is now an
// extern-declared const std::array defined in namespace mhw in the data TU
// (see the declaration in rise/mhr_part_names.h), so the rows can live in
// their own file while the lookups stay here.
//
// Regenerate the DATA TU with scripts/generate_rise_part_names.py:
//   python3 scripts/generate_rise_part_names.py --monster-data <MonsterData.xml>
//       --en-xml <en-us.xml> --zh-xml <zh-cn.xml>
//       --cpp-out src/rise/data/mhr_part_names_data.cpp
//
// Sources (all three sha256-verified by the generator; the full provenance
// block with hashes is reproduced at the top of the data TU):
//   MonsterData.xml  HunterPie/Game/Rise/Data/MonsterData.xml
//     (/Monsters/Monster/Parts/Part — the part list every Rise monster
//      carries; partIndex is the part's Id attribute)
//   zh column        https://cdn.hunterpie.com/localization/zh-cn.xml
//   en column        https://cdn.hunterpie.com/localization/en-us.xml
//
// PART_TO_BE_MAPPED is "Unknown" in BOTH locales (upstream placeholder);
// risePartName() keeps suppressing it so the overlay shows its safe
// "部位 N" / "Part N" fallback instead of the literal "Unknown".
//
// Locale selection (v0.9 i18n, WS-B): risePartName() / risePartDisplayName()
// return the en column while mhw::StringTable::instance().isEnglish() is true
// and the zh column otherwise; an empty locale (before any load()) reads as
// Chinese, so the pre-i18n behaviour is the default. risePartNameEn() exposes
// the en column explicitly (tests / tooling) and is locale-independent.

#include "rise/mhr_part_names.h"

#include "core/string_table.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace mhw {
namespace {

const RisePartName *risePartNameEntry(int monsterId, int partIndex)
{
    const auto it = std::lower_bound(
        kRisePartNames.begin(), kRisePartNames.end(),
        std::pair{monsterId, partIndex},
        [](const RisePartName &entry, const std::pair<int, int> &key) {
            return entry.monsterId < key.first
                || (entry.monsterId == key.first && entry.partIndex < key.second);
        });
    if (it == kRisePartNames.end() || it->monsterId != monsterId
        || it->partIndex != partIndex)
        return nullptr;
    return &*it;
}

// HunterPie's tables intentionally leave PART_TO_BE_MAPPED as "Unknown" in
// both locales. Preserve the existing localized fallback instead of exposing
// that upstream placeholder in the overlay.
const char *risePartNameFromEntry(const RisePartName *entry, bool english)
{
    if (entry == nullptr)
        return nullptr;
    const char *name = english ? entry->nameEn : entry->name;
    if (std::strcmp(name, "Unknown") == 0)
        return nullptr;
    return name;
}

QString compactPartHealthValue(float value)
{
    if (!std::isfinite(value) || value < 0.0F)
        return QStringLiteral("--");
    const int rounded = static_cast<int>(std::lround(value));
    if (rounded < 1000)
        return QString::number(rounded);
    const float thousands = static_cast<float>(rounded) / 1000.0F;
    if (rounded % 1000 == 0 || thousands >= 10.0F)
        return QStringLiteral("%1k").arg(static_cast<int>(std::lround(thousands)));
    return QStringLiteral("%1k").arg(thousands, 0, 'f', 1);
}

} // namespace

const char *risePartName(int monsterId, int partIndex)
{
    return risePartNameFromEntry(risePartNameEntry(monsterId, partIndex),
                                 StringTable::instance().isEnglish());
}

const char *risePartNameEn(int monsterId, int partIndex)
{
    return risePartNameFromEntry(risePartNameEntry(monsterId, partIndex), true);
}

QString risePartDisplayName(int monsterId, int partIndex)
{
    const bool english = StringTable::instance().isEnglish();
    if (const char *name = risePartNameFromEntry(
            risePartNameEntry(monsterId, partIndex), english))
        return QString::fromUtf8(name);
    return english ? QStringLiteral("Part %1").arg(partIndex)
                   : QStringLiteral("部位 %1").arg(partIndex);
}

QString compactPartHealth(float current, float maximum)
{
    if (!std::isfinite(maximum) || maximum <= 0.0F)
        return QStringLiteral("--/--");
    return QStringLiteral("%1/%2")
        .arg(compactPartHealthValue(current))
        .arg(compactPartHealthValue(maximum));
}

PartHealthPair partHealthForDisplay(const PartSnapshot &part)
{
    if (part.partType == PartType::Flinch)
        return {part.flinch, part.maxFlinch};
    return {part.health, part.maxHealth};
}

} // namespace mhw
