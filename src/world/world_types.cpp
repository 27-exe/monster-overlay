#include "world_types.h"

namespace mhw {

const char *zoneName(Zone zone)
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
            return "未知";
        }
    }
    switch (zone) {
    case Zone::MainMenu: return "主菜单";
    case Zone::AncientForest: return "古代树森林";
    case Zone::WildspireWaste: return "荒野大陆";
    case Zone::CoralHighlands: return "珊瑚高地";
    case Zone::RottenVale: return "龙结晶之地";
    case Zone::EldersRecess: return "龙之墓场";
    case Zone::GreatRavine: return "大溪谷";
    case Zone::GreatRavine2: return "大溪谷·深层";
    case Zone::HoarfrostReach: return "冰呪之地";
    case Zone::GuidingLands: return "引导之地";
    case Zone::SpecialArena: return "特殊斗技场";
    case Zone::Arena: return "斗技场";
    case Zone::SelianaSupplyCache: return "月辰补给所";
    case Zone::Astera: return "阿斯特拉";
    case Zone::AsteraGatheringHub: return "阿斯特拉·集会所";
    case Zone::ResearchBase: return "研究基地";
    case Zone::Seliana: return "月辰";
    case Zone::SelianaGatheringHub: return "月辰·集会所";
    case Zone::Introduction: return "新大陆入门区";
    case Zone::Everstream: return "不绝的河流";
    case Zone::ConfluenceOfFates: return "命运的交汇";
    case Zone::AncientForest2: return "古代树森林·深层";
    case Zone::CavernsOfElDorado: return "黄金洞窟";
    case Zone::SelianaSupplyCache2: return "月辰补给所·深层";
    case Zone::OriginIsle: return "原点之岛";
    case Zone::OriginIsle2: return "原点之岛·深层";
    case Zone::SecludedValley: return "秘境之谷";
    case Zone::SecludedValley2: return "秘境之谷·深层";
    case Zone::CastleSchrade: return "城塞高地·修雷德";
    case Zone::LivingQuarters: return "猎人生活区";
    case Zone::PrivateQuarters: return "私人房间";
    case Zone::PrivateSuite: return "私人套房";
    case Zone::TrainingArea: return "训练区";
    case Zone::ChamberOfFive: return "五星之间";
    case Zone::SelianaRoom: return "月辰·休息室";
    case Zone::RiseTrainingRoom: return "训练场";
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
    // reports them) and keep the generic placeholder.
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
    case Zone::RiseLoc0:  return "未知狩猎区"; // StageId 200: no entry upstream
    case Zone::RiseLoc1:  return "废神社";
    case Zone::RiseLoc2:  return "沙原";
    case Zone::RiseLoc3:  return "水没林";
    case Zone::RiseLoc4:  return "冰封群岛";
    case Zone::RiseLoc5:  return "熔岩洞";
    case Zone::RiseLoc6:  return "未知狩猎区"; // StageId 206: no entry upstream
    case Zone::RiseLoc7:  return "翡叶要塞";
    case Zone::RiseLoc8:  return "未知狩猎区"; // StageId 208: no entry upstream
    case Zone::RiseLoc9:  return "狱泉乡";
    case Zone::RiseLoc10: return "斗技场";
    case Zone::RiseLoc11: return "龙宫古城";
    case Zone::RiseLoc12: return "密林";
    case Zone::RiseLoc13: return "城塞高地";
    case Zone::RiseLoc14: return "塔之秘境";
    case Zone::RiseLoc15: return "渊劫地狱";
    case Zone::RiseLoc16: return "未知狩猎区"; // StageId 216: no entry upstream
    case Zone::Unknown: return "未知";
    }
    return "未知";
}

bool isHuntingZone(Zone zone)
{
    switch (zone) {
    case Zone::AncientForest:
    case Zone::WildspireWaste:
    case Zone::CoralHighlands:
    case Zone::RottenVale:
    case Zone::EldersRecess:
    case Zone::GreatRavine:
    case Zone::GreatRavine2:
    case Zone::HoarfrostReach:
    case Zone::GuidingLands:
    case Zone::SpecialArena:
    case Zone::Arena:
    case Zone::SelianaSupplyCache:
    case Zone::Introduction:
    case Zone::Everstream:
    case Zone::ConfluenceOfFates:
    case Zone::AncientForest2:
    case Zone::CavernsOfElDorado:
    case Zone::SelianaSupplyCache2:
    case Zone::OriginIsle:
    case Zone::OriginIsle2:
    case Zone::SecludedValley:
    case Zone::SecludedValley2:
    case Zone::CastleSchrade:
    case Zone::TrainingArea:
    case Zone::ChamberOfFive:
    case Zone::RiseTrainingRoom:
    case Zone::RiseLoc0:
    case Zone::RiseLoc1:
    case Zone::RiseLoc2:
    case Zone::RiseLoc3:
    case Zone::RiseLoc4:
    case Zone::RiseLoc5:
    case Zone::RiseLoc6:
    case Zone::RiseLoc7:
    case Zone::RiseLoc8:
    case Zone::RiseLoc9:
    case Zone::RiseLoc10:
    case Zone::RiseLoc11:
    case Zone::RiseLoc12:
    case Zone::RiseLoc13:
    case Zone::RiseLoc14:
    case Zone::RiseLoc15:
    case Zone::RiseLoc16:
        return true;
    default:
        return false;
    }
}

bool isPeaceZone(Zone zone)
{
    switch (zone) {
    case Zone::Astera:
    case Zone::AsteraGatheringHub:
    case Zone::ResearchBase:
    case Zone::Seliana:
    case Zone::SelianaGatheringHub:
    case Zone::LivingQuarters:
    case Zone::PrivateQuarters:
    case Zone::PrivateSuite:
    case Zone::SelianaRoom:
        return true;
    default:
        return false;
    }
}

} // namespace mhw