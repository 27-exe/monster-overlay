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
// nullptr is the ONLY representation of "no entry". The placeholder-looking
// rows are ordinary data rows that DO name a zone: RiseLoc0/6/8/16 are the
// official table's own hole markers (StageId 200/206/208/216 have no entry
// upstream) and keep their own explicit label, so they stay non-null.
//
// Both tables carry the same key set — every WorldZone enumerator plus the Rise
// village ids 0..11 rebased into 700..799 — so the zh lookup doubles as the
// canonical "is this zone named?" test. A row that existed only in en
// would be a table bug to fix here, not a reason for the UI's logic to
// flip with the active language.
const char *zoneNameLookup(WorldZone zone)
{
    // Rise villages (stage.type == 4) are mapped by computeZoneId()
    // into 700..799. Inline Chinese name table (was mhrVillageName()
    // in v0.8.x; simplified here since the table is small).
    // IDs verified against the official HunterPie table
    // (Strings/Stages/Rise/Stage in /localization/zh-cn.xml, v0.8.4-r18
    // zone-names): 0=Village 1=Room 2=BuddyPlaza 3=GatheringHub
    // 4=HubPrepPlaza 5=TrainingArea 6=Elgado 7=Elgado'sRoom
    // 11=Elgado's Command Post.
    {
        const int raw = static_cast<int>(zone);
        if (raw >= 700 && raw <= 799) {
            switch (raw - 700) {
            case 0: return "神火村";
            case 1: return "自宅";
            case 2: return "神火村·同伴广场";
            case 3: return "集会所";
            case 4: return "集会所·准备广场";
            case 5: return "训练场";
            case 6: return "艾鲁多";
            case 7: return "艾鲁多·猎人房间";
            // StageId 11 = Elgado's Command Post — present in the
            // official table, was missing here (v0.8.4-r18 zone-names).
            case 11: return "骑士团指挥所";
            default: break;
            }
            return nullptr;
        }
    }
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
    case WorldZone::RiseTrainingRoom: return "训练场";
    // v0.8.4-r18 zone-names: table re-anchored to HunterPie's official
    // stage table. HunterPie emits StageId = HuntingId + 200
    // (MHRPlayer.cs:216-227) and resolves the label from the localization
    // entry `Strings/Stages/Rise/Stage`; the zh-CN strings below are
    // verbatim from that file, fetched from the official CDN:
    //   https://cdn.hunterpie.com/localization/zh-cn.xml
    //   sha256 2a4b1bb318fc21a55c0b5b978e2d33cb8c2ea74457b34fcac88b9236c19a06db
    //   (matches /v1/localization/checksum on api.hunterpie.com and
    //    mirror.hunterpie.com). Full table, provenance and the
    //   three-way comparison: v0.8.4-r18/zone-names/REPORT.md.
    // StageId 200 / 206 / 208 / 216 have no entry upstream (a hunt never
    // reports them) and keep the generic placeholder. That placeholder IS
    // the entry's own data (a named hole marker), so it stays non-null.
    //   hid 1  -> 201 Shrine Ruins      废神社     (in-game verified)
    //   hid 2  -> 202 Sandy Plains      沙原
    //   hid 3  -> 203 Flooded Forest    水没林
    //   hid 4  -> 204 Frost Islands     冰封群岛   (in-game verified)
    //   hid 5  -> 205 Lava Caverns      熔岩洞
    //   hid 7  -> 207 Red Stronghold    翡叶要塞   (百龙夜行 map)
    //   hid 9  -> 209 Infernal Springs  狱泉乡
    //   hid 10 -> 210 Arena             斗技场
    //   hid 11 -> 211 Coral Palace      龙宫古城
    //   hid 12 -> 212 Jungle            密林
    //   hid 13 -> 213 Citadel           城塞高地
    //   hid 14 -> 214 Forlorn Arena     塔之秘境
    //   hid 15 -> 215 Yawning Abyss     渊劫地狱
    // The previous (v0.8.x) table used the community-site *navigation*
    // order (Shrine, Frost, Sandy, Flooded, Lava …) as if it were the
    // memory HuntingId order — every label from hid 4 on was shifted.
    case WorldZone::RiseLoc0:  return "未知狩猎区"; // StageId 200: no entry upstream
    case WorldZone::RiseLoc1:  return "废神社";
    case WorldZone::RiseLoc2:  return "沙原";
    case WorldZone::RiseLoc3:  return "水没林";
    case WorldZone::RiseLoc4:  return "冰封群岛";
    case WorldZone::RiseLoc5:  return "熔岩洞";
    case WorldZone::RiseLoc6:  return "未知狩猎区"; // StageId 206: no entry upstream
    case WorldZone::RiseLoc7:  return "翡叶要塞";
    case WorldZone::RiseLoc8:  return "未知狩猎区"; // StageId 208: no entry upstream
    case WorldZone::RiseLoc9:  return "狱泉乡";
    case WorldZone::RiseLoc10: return "斗技场";
    case WorldZone::RiseLoc11: return "龙宫古城";
    case WorldZone::RiseLoc12: return "密林";
    case WorldZone::RiseLoc13: return "城塞高地";
    case WorldZone::RiseLoc14: return "塔之秘境";
    case WorldZone::RiseLoc15: return "渊劫地狱";
    case WorldZone::RiseLoc16: return "未知狩猎区"; // StageId 216: no entry upstream
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
    //     StageId = HuntingId + 200 for the maps).
    //   * StageId 200/206/208/216 (RiseLoc0/6/8/16, en route to a hunt) have
    //     no upstream entry: both columns keep a placeholder, ours here.
    //     That placeholder is a named hole marker, not "no entry".
    {
        const int raw = static_cast<int>(zone);
        if (raw >= 700 && raw <= 799) {
            switch (raw - 700) {
            case 0: return "Village";
            case 1: return "Room";
            case 2: return "Buddy Plaza";
            case 3: return "Gathering Hub";
            case 4: return "Hub Prep Plaza";
            case 5: return "Training Area";
            case 6: return "Elgado";
            case 7: return "Elgado's Room";
            case 11: return "Elgado's Command Post";
            default: break;
            }
            return nullptr;
        }
    }
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
    case WorldZone::RiseTrainingRoom: return "Training Area";
    case WorldZone::RiseLoc0:  return "Unknown Hunting Zone"; // StageId 200: no entry upstream
    case WorldZone::RiseLoc1:  return "Shrine Ruins";
    case WorldZone::RiseLoc2:  return "Sandy Plains";
    case WorldZone::RiseLoc3:  return "Flooded Forest";
    case WorldZone::RiseLoc4:  return "Frost Islands";
    case WorldZone::RiseLoc5:  return "Lava Caverns";
    case WorldZone::RiseLoc6:  return "Unknown Hunting Zone"; // StageId 206: no entry upstream
    case WorldZone::RiseLoc7:  return "Red Stronghold";
    case WorldZone::RiseLoc8:  return "Unknown Hunting Zone"; // StageId 208: no entry upstream
    case WorldZone::RiseLoc9:  return "Infernal Springs";
    case WorldZone::RiseLoc10: return "Arena";
    case WorldZone::RiseLoc11: return "Coral Palace";
    case WorldZone::RiseLoc12: return "Jungle";
    case WorldZone::RiseLoc13: return "Citadel";
    case WorldZone::RiseLoc14: return "Forlorn Arena";
    case WorldZone::RiseLoc15: return "Yawning Abyss";
    case WorldZone::RiseLoc16: return "Unknown Hunting Zone"; // StageId 216: no entry upstream
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
    case WorldZone::RiseTrainingRoom:
    case WorldZone::RiseLoc0:
    case WorldZone::RiseLoc1:
    case WorldZone::RiseLoc2:
    case WorldZone::RiseLoc3:
    case WorldZone::RiseLoc4:
    case WorldZone::RiseLoc5:
    case WorldZone::RiseLoc6:
    case WorldZone::RiseLoc7:
    case WorldZone::RiseLoc8:
    case WorldZone::RiseLoc9:
    case WorldZone::RiseLoc10:
    case WorldZone::RiseLoc11:
    case WorldZone::RiseLoc12:
    case WorldZone::RiseLoc13:
    case WorldZone::RiseLoc14:
    case WorldZone::RiseLoc15:
    case WorldZone::RiseLoc16:
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
