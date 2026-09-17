// SPDX-License-Identifier: Apache-2.0
//
// AUTO-GENERATED — do not edit by hand.
// Regenerate with scripts/extract_rise_monster_names.py
//   python3 scripts/extract_rise_monster_names.py --en-xml <en-us.xml> --zh-xml <zh-cn.xml> --cpp-out src/rise/mhr_monster_names.cpp
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

struct RiseMonsterName {
    int id;
    const char *name;    // UTF-8, Simplified Chinese, verbatim from zh-cn.xml
    const char *nameEn;  // UTF-8, English, verbatim from en-us.xml
};

// Sorted by id: the lookup below binary-searches it.
constexpr std::array<RiseMonsterName, 79> kRiseMonsterNames = {{
    { 0, "雌火龙", "Rathian" },
    { 1, "霸主·雌火龙", "Apex Rathian" },
    { 2, "火龙", "Rathalos" },
    { 3, "霸主·火龙", "Apex Rathalos" },
    { 4, "奇怪龙", "Khezu" },
    { 5, "岩龙", "Basarios" },
    { 6, "角龙", "Diablos" },
    { 7, "霸主·角龙", "Apex Diablos" },
    { 8, "金狮子", "Rajang" },
    { 9, "钢龙", "Kushala Daora" },
    { 10, "霞龙", "Chameleos" },
    { 11, "炎王龙", "Teostra" },
    { 12, "轰龙", "Tigrex" },
    { 13, "迅龙", "Nargacuga" },
    { 14, "冰牙龙", "Barioth" },
    { 15, "土砂龙", "Barroth" },
    { 16, "水兽", "Royal Ludroth" },
    { 17, "眠狗龙王", "Great Baggi" },
    { 18, "雷狼龙", "Zinogre" },
    { 19, "霸主·雷狼龙", "Apex Zinogre" },
    { 20, "毒狗龙王", "Great Wroggi" },
    { 21, "青熊兽", "Arzuros" },
    { 22, "霸主·青熊兽", "Apex Arzuros" },
    { 23, "白兔兽", "Lagombi" },
    { 24, "赤甲兽", "Volvidon" },
    { 25, "泡狐龙", "Mizutsune" },
    { 26, "霸主·泡狐龙", "Apex Mizutsune" },
    { 27, "神秘红光天彗龙", "Crimson Glow Valstrax" },
    { 28, "怨虎龙", "Magnamalo" },
    { 29, "天狗兽", "Bishaten" },
    { 30, "伞鸟", "Aknosom" },
    { 31, "河童蛙", "Tetranadon" },
    { 32, "人鱼龙", "Somnacanth" },
    { 33, "妃蜘蛛", "Rakna-Kadaki" },
    { 34, "泥翁龙", "Almudron" },
    { 35, "风神龙", "Wind Serpent Ibushi" },
    { 36, "雪鬼兽", "Goss Harag" },
    { 37, "镰鼬龙王", "Great Izuchi" },
    { 38, "雷神龙", "Thunder Serpent Narwa" },
    { 39, "百龙渊源雷神龙", "Narwa the Allmother" },
    { 40, "蛮颚龙", "Anjanath" },
    { 41, "毒妖鸟", "Pukei-Pukei" },
    { 42, "骚鸟", "Kulu-Ya-Ku" },
    { 43, "泥鱼龙", "Jyuratodus" },
    { 44, "飞雷龙", "Tobi-Kadachi" },
    { 45, "爆鳞龙", "Bazelgeuse" },
    { 46, "机关蛙", "Toadversary" },
    { 76, "金火龙", "Gold Rathian" },
    { 77, "银火龙", "Silver Rathalos" },
    { 78, "大名盾蟹", "Daimyo Hermitaur" },
    { 79, "将军镰蟹", "Shogun Ceanataur" },
    { 80, "激昂金狮子", "Furious Rajang" },
    { 81, "月迅龙", "Lucent Nargacuga" },
    { 82, "黑蚀龙", "Gore Magala" },
    { 83, "天廻龙", "Shagaru Magala" },
    { 84, "千刃龙", "Seregios" },
    { 85, "电龙", "Astalos" },
    { 86, "焰狐龙", "Violet Mizutsune" },
    { 87, "嗟怨震天怨虎龙", "Scorned Magnamalo" },
    { 88, "绯天狗兽", "Blood Orange Bishaten" },
    { 89, "冰人鱼龙", "Aurora Somnacanth" },
    { 90, "炽妃蜘蛛", "Pyre Rakna-kadaki" },
    { 91, "熔翁龙", "Magma Almudron" },
    { 92, "红莲爆鳞龙", "Seething Bezelgeuse" },
    { 93, "爵银龙", "Malzeno" },
    { 94, "冰狼龙", "Lunagaron" },
    { 95, "刚缠兽", "Garangolm" },
    { 96, "冥渊龙", "Gaismagorm" },
    { 97, "棘龙", "Espinas" },
    { 98, "棘茶龙", "Flaming Espinas" },
    { 107, "怪异克服钢龙", "Risen Kushala Daora" },
    { 108, "怪异克服霞龙", "Risen Chameleos" },
    { 109, "怪异克服炎王龙", "Risen Teostra" },
    { 110, "怪异克服天廻龙", "Risen Shagaru Magala" },
    { 111, "怪异克服天彗龙", "Risen Crimson Glow Valstrax" },
    { 112, "原初形态爵银龙", "Primordial Malzeno" },
    { 113, "混沌黑蚀龙", "Chaotic Gore Magala" },
    { 114, "冰呪龙", "Velkhana" },
    { 115, "岚龙", "Amatsu" },
}};

// A regeneration that ever emits an unsorted or duplicated id must fail the
// build instead of silently returning wrong names at runtime.
constexpr bool riseMonsterNamesStrictlyIncreasing()
{
    for (std::size_t i = 1; i < kRiseMonsterNames.size(); ++i) {
        if (kRiseMonsterNames[i - 1].id >= kRiseMonsterNames[i].id)
            return false;
    }
    return true;
}
static_assert(riseMonsterNamesStrictlyIncreasing(),
              "riseMonsterName() binary search requires strictly increasing ids");

// Both locale columns must be present for every row: a regenerated table with
// an empty column would otherwise show blank names instead of failing.
constexpr bool riseMonsterNamesCarryBothColumns()
{
    for (const RiseMonsterName &entry : kRiseMonsterNames) {
        if (entry.name == nullptr || entry.name[0] == '\0'
            || entry.nameEn == nullptr || entry.nameEn[0] == '\0')
            return false;
    }
    return true;
}
static_assert(riseMonsterNamesCarryBothColumns(),
              "every Rise monster row carries a zh and an en name");

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
