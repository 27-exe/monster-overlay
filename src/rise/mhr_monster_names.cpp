// SPDX-License-Identifier: Apache-2.0
//
// AUTO-GENERATED — do not edit by hand.
// Regenerate with scripts/extract_rise_monster_names.py
//   python3 scripts/extract_rise_monster_names.py --en-xml <en-us.xml> --zh-xml <zh-cn.xml> --data-out src/rise/data/mhr_monster_names_data.cpp
//
// This TU is the LOGIC half of the split: the binary search and the two public
// accessors. The table, the RiseMonsterName struct and the two static_asserts
// that gate the regenerated data live in src/rise/data/mhr_monster_names_data.cpp.
//
// Source: HunterPie official localization — BOTH columns verbatim.
//   zh column: zh-cn.xml  https://cdn.hunterpie.com/localization/zh-cn.xml
//     sha256 2a4b1bb318fc21a55c0b5b978e2d33cb8c2ea74457b34fcac88b9236c19a06db
//     /Strings/Monsters/Rise/Monster[@Id] (zh-cn.xml lines 981-1140)
//   en column: en-us.xml  https://cdn.hunterpie.com/localization/en-us.xml
//     sha256 661d58b54bd1214ae0e9eff92fb4167b8aab54df0452eea238cb9574300030ea
//     /Strings/Monsters/Rise/Monster[@Id] (en-us.xml lines 964-1123)
//   Both files are byte-identical to HunterPie/localization@main/localization/
//   <lang>.xml and to the hashes published by
//   https://api.hunterpie.com/v1/localization/checksum (fetch transcript:
//   .../v0.8.4-r18/zone-names/evidence/localization/FETCH.md).
//   Extracted 2026-09-18. The two locales carry the SAME 79 Ids — the
//   generator asserts the Id sets are equal, so every row has both columns.
//
// 79 entries covering Id 0..115; the id gaps are upstream —
// HunterPie's localization carries no <Monster> entry for e.g. 47..75 / 99..106,
// so a miss here is a real "no localized name", not an extraction artefact.
//
// Locale selection (v0.9 i18n, WS-B): riseMonsterName() returns the en column
// while mhw::StringTable::instance().isEnglish() is true and the zh column
// otherwise; an empty locale (before any load()) reads as Chinese, so the
// pre-i18n behaviour is the default. riseMonsterNameEn() exposes the en column
// explicitly (tests / tooling) and is locale-independent.

#include "rise/mhr_monster_names.h"

#include "core/string_table.h"

#include <array>
#include <cstddef>

namespace mhw {
namespace {

const RiseMonsterName *riseMonsterNameEntry(int id)
{
    std::size_t lo = 0;
    std::size_t hi = kRiseMonsterNames.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (kRiseMonsterNames[mid].id < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < kRiseMonsterNames.size() && kRiseMonsterNames[lo].id == id)
        return &kRiseMonsterNames[lo];
    return nullptr;
}

} // namespace

const char *riseMonsterName(int id)
{
    const RiseMonsterName *entry = riseMonsterNameEntry(id);
    if (entry == nullptr)
        return nullptr;
    return StringTable::instance().isEnglish() ? entry->nameEn : entry->name;
}

const char *riseMonsterNameEn(int id)
{
    const RiseMonsterName *entry = riseMonsterNameEntry(id);
    return entry == nullptr ? nullptr : entry->nameEn;
}

} // namespace mhw
