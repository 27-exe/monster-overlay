// SPDX-License-Identifier: Apache-2.0
//
// world_types.cpp — the World zone vocabulary, World zones only.
//
// Rise's zones (RiseLoc0..16 = 600..616, RiseTrainingRoom = 705, and the
// village band 700..799) used to be case arms in every lookup below. They
// moved to src/rise/rise_zones.h when the snapshot field became
// ZoneRef{GameId, int id} (src/core/zone_id.h): the id stays an int, and the
// answering table for an id now lives beside the game that emits it.
//
// Nothing a World caller sees changes — same enum, same strings, same ids.

#include "world_types.h"

#include "core/string_table.h"

namespace mhw {
namespace {

// The two name tables, factored so that "does this zone have a name?" is
// answerable without reading a display string.
//
// A lookup returns nullptr when the WorldZone carries no table entry. The public
// accessors below turn that nullptr into the placeholder literal
// ("未知" / "Unknown"), so every rendered label stays byte-identical to the
// pre-refactor behaviour.
//
// Why the indirection exists: UI logic used to infer "this zone has no
// name" by comparing the localized display label against the literals
// that mean "no name" (the zh placeholder and the translated
// ui.zone_unknown value). That is display-layer data driving a branch, so
// editing a translation silently changed the branch. The answer belongs to
// the data layer, where the zone's identity already lives — hence
// hasZoneName(). No source file may re-introduce a comparison of a name
// string; scripts/i18n_gate.py's callers and this comment carry the history.
//
// nullptr is the ONLY representation of "no entry".
//
// The zh lookup is also the canonical "is this zone named?" test: every
// WorldZone enumerator that has a name has a row in both tables. A row that
// existed only in en would be a table bug to fix here, not a reason for a
// caller's logic to flip with the active language.
//
// Rise zones are NOT here — they moved to src/rise/rise_zones.h when the
// snapshot field became ZoneRef{GameId, int id} (core/zone_id.h).
const char *zoneNameLookup(WorldZone zone)
{
    switch (zone) {
    case WorldZone::MainMenu: return "主菜单";
    case WorldZone::AncientForest: return "古代树森林";
    case WorldZone::WildspireWaste: return "荒野大陆";
    case WorldZone::CoralHighlands: return "珊瑚高地";
    case WorldZone::RottenVale: return "龙结晶之地";
    case WorldZone::EldersRecess: return "龙之墓场";
    case WorldZone::GreatRavine: return "大溪谷";
    case WorldZone::GreatRavine2: return "大溪谷·深层";
    case WorldZone::HoarfrostReach: return "冰呪之地";
    case WorldZone::GuidingLands: return "引导之地";
    case WorldZone::SpecialArena: return "特殊斗技场";
    case WorldZone::Arena: return "斗技场";
    case WorldZone::SelianaSupplyCache: return "月辰补给所";
    case WorldZone::Astera: return "阿斯特拉";
    case WorldZone::AsteraGatheringHub: return "阿斯特拉·集会所";
    case WorldZone::ResearchBase: return "研究基地";
    case WorldZone::Seliana: return "月辰";
    case WorldZone::SelianaGatheringHub: return "月辰·集会所";
    case WorldZone::Introduction: return "新大陆入门区";
    case WorldZone::Everstream: return "不绝的河流";
    case WorldZone::ConfluenceOfFates: return "命运的交汇";
    case WorldZone::AncientForest2: return "古代树森林·深层";
    case WorldZone::CavernsOfElDorado: return "黄金洞窟";
    case WorldZone::SelianaSupplyCache2: return "月辰补给所·深层";
    case WorldZone::OriginIsle: return "原点之岛";
    case WorldZone::OriginIsle2: return "原点之岛·深层";
    case WorldZone::SecludedValley: return "秘境之谷";
    case WorldZone::SecludedValley2: return "秘境之谷·深层";
    case WorldZone::CastleSchrade: return "城塞高地·修雷德";
    case WorldZone::LivingQuarters: return "猎人生活区";
    case WorldZone::PrivateQuarters: return "私人房间";
    case WorldZone::PrivateSuite: return "私人套房";
    case WorldZone::TrainingArea: return "训练区";
    case WorldZone::ChamberOfFive: return "五星之间";
    case WorldZone::SelianaRoom: return "月辰·休息室";
    case WorldZone::Unknown: break;
    }
    return nullptr;
}

const char *zoneNameEnLookup(WorldZone zone)
{
    // v0.9 i18n (WS-A). English labels for the exact same WorldZone values,
    // sourced from HunterPie's official en-us.xml (Strings/Stages/World and
    // Strings/Stages/Rise), sha256
    // 661d58b54bd1214ae0e9eff92fb4167b8aab54df0452eea238cb9574300030ea.
    // Only the *labels* are new — the enum, the id mapping and every zh
    // string returned by zoneName() are untouched.
    //
    // Notes on the upstream table's own quirks (kept verbatim, no invented
    // distinctions):
    //   * Stages/World has 106 = 107 = "Great Ravine" and 101 = 406 =
    //     "Ancient Forest" (203 = 411, 412 = 413, 415 = 416 likewise). The
    //     zh table decorates the second variant with "·深层"; English has no
    //     such suffix upstream, so the duplicate official string is used.
    //   * Rise villages are Stages/Rise ids 0..11 (the same table that gives
    //     StageId = HuntingId + 200 for the maps). Rise's whole table —
    //     villages included — moved to src/rise/rise_zones.h.
    switch (zone) {
    case WorldZone::MainMenu: return "Main Menu";
    case WorldZone::AncientForest: return "Ancient Forest";
    case WorldZone::WildspireWaste: return "Wildspire Waste";
    case WorldZone::CoralHighlands: return "Coral Highlands";
    case WorldZone::RottenVale: return "Rotten Vale";
    case WorldZone::EldersRecess: return "Elder's Recess";
    case WorldZone::GreatRavine: return "Great Ravine";
    case WorldZone::GreatRavine2: return "Great Ravine";
    case WorldZone::HoarfrostReach: return "Hoarfrost Reach";
    case WorldZone::GuidingLands: return "The Guiding Lands";
    case WorldZone::SpecialArena: return "Special Arena";
    case WorldZone::Arena: return "Arena";
    case WorldZone::SelianaSupplyCache: return "Seliana Supply Cache";
    case WorldZone::Astera: return "Astera";
    case WorldZone::AsteraGatheringHub: return "Astera Gathering Hub";
    case WorldZone::ResearchBase: return "Research Base";
    case WorldZone::Seliana: return "Seliana";
    case WorldZone::SelianaGatheringHub: return "Seliana Gathering Hub";
    case WorldZone::Introduction: return "Introduction";
    case WorldZone::Everstream: return "Everstream";
    case WorldZone::ConfluenceOfFates: return "Confluence of Fates";
    case WorldZone::AncientForest2: return "Ancient Forest";
    case WorldZone::CavernsOfElDorado: return "Caverns of El Dorado";
    case WorldZone::SelianaSupplyCache2: return "Seliana Supply Cache";
    case WorldZone::OriginIsle: return "Origin Isle";
    case WorldZone::OriginIsle2: return "Origin Isle";
    case WorldZone::SecludedValley: return "Secluded Valley";
    case WorldZone::SecludedValley2: return "Secluded Valley";
    case WorldZone::CastleSchrade: return "Castle Schrade";
    case WorldZone::LivingQuarters: return "Living Quarters";
    case WorldZone::PrivateQuarters: return "Private Quarters";
    case WorldZone::PrivateSuite: return "Private Suite";
    case WorldZone::TrainingArea: return "Training Area";
    case WorldZone::ChamberOfFive: return "Chamber of Five";
    case WorldZone::SelianaRoom: return "Seliana Room";
    case WorldZone::Unknown: break;
    }
    return nullptr;
}

} // namespace

const char *zoneName(WorldZone zone)
{
    // "未知" is the zh column's placeholder for a WorldZone with no table
    // entry — the same value zoneName() has always returned for them.
    const char *name = zoneNameLookup(zone);
    return name != nullptr ? name : "未知";
}

const char *zoneNameEn(WorldZone zone)
{
    const char *name = zoneNameEnLookup(zone);
    return name != nullptr ? name : "Unknown";
}

const char *zoneNameLocalized(WorldZone zone)
{
    // The single locale switch for zone labels. An unloaded / non-English
    // StringTable (tests, first frames) reads as Chinese, which keeps the
    // pre-i18n behaviour.
    return StringTable::instance().isEnglish()
        ? zoneNameEn(zone)
        : zoneName(zone);
}

// Data-layer answer to "does this zone have a name?": true means a lookup
// hit a named entry for this WorldZone.
//
// Callers pair it with zoneNameLocalized() and use the false branch for a
// generic "outpost N" / "area N" fallback — instead of reverse-engineering
// the answer out of the *translated label*, which made the branch move
// whenever ui.zone_unknown or the zh table was reworded.
//
// Locale-independent by design: whether a zone is named is a property of
// the WorldZone and the upstream table, never of the active language.
// WorldZone::Unknown answers false — "no zone" is not "an unnamed zone".
bool hasZoneName(WorldZone zone)
{
    return zoneNameLookup(zone) != nullptr;
}

bool isHuntingZone(WorldZone zone)
{
    switch (zone) {
    case WorldZone::AncientForest:
    case WorldZone::WildspireWaste:
    case WorldZone::CoralHighlands:
    case WorldZone::RottenVale:
    case WorldZone::EldersRecess:
    case WorldZone::GreatRavine:
    case WorldZone::GreatRavine2:
    case WorldZone::HoarfrostReach:
    case WorldZone::GuidingLands:
    case WorldZone::SpecialArena:
    case WorldZone::Arena:
    case WorldZone::SelianaSupplyCache:
    case WorldZone::Introduction:
    case WorldZone::Everstream:
    case WorldZone::ConfluenceOfFates:
    case WorldZone::AncientForest2:
    case WorldZone::CavernsOfElDorado:
    case WorldZone::SelianaSupplyCache2:
    case WorldZone::OriginIsle:
    case WorldZone::OriginIsle2:
    case WorldZone::SecludedValley:
    case WorldZone::SecludedValley2:
    case WorldZone::CastleSchrade:
    case WorldZone::TrainingArea:
    case WorldZone::ChamberOfFive:
        return true;
    default:
        return false;
    }
}

bool isPeaceZone(WorldZone zone)
{
    switch (zone) {
    case WorldZone::Astera:
    case WorldZone::AsteraGatheringHub:
    case WorldZone::ResearchBase:
    case WorldZone::Seliana:
    case WorldZone::SelianaGatheringHub:
    case WorldZone::LivingQuarters:
    case WorldZone::PrivateQuarters:
    case WorldZone::PrivateSuite:
    case WorldZone::SelianaRoom:
        return true;
    default:
        return false;
    }
}

} // namespace mhw
