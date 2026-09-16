#include "world_types.h"

namespace mhw {

const char *zoneName(Zone zone)
{
    // Rise villages (stage.type == 4) are mapped by computeZoneId()
    // into 700..799. Inline Chinese name table (was mhrVillageName()
    // in v0.8.x; simplified here since the table is small).
    // Mapping derived from HunterPie WirebugWidgetContextHandler.cs /
    // MHRGame.cs + community reverse-engineering of village sub-section
    // IDs: 1=Room, 3=GatheringHub, 4=HubPrepPlaza, 5=TrainingRoom;
    // 0/2/6/7 = Kamura main/Buddy plaza/Elgado main/Elgado hunter room.
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
    // v0.8 / v0.8.x: Rise maps — HunterPie emits `HuntingId + 200`
    // (MHRPlayer.cs:222), the overlay re-bases them into the
    // 600..616 range to keep the two games' enums disjoint
    // (world_types.h:42-47). The Chinese names below match the
    // HunterPie MHRise community mapping — i.e. the canonical
    // Capcom "Stage" order encoded in mhrice's MapProductId and the
    // GameCat / fextralife localisation tables.
    //
    // v0.8.x (symptom 5): previous entries were arbitrarily named
    // ("古代林", "冰海龙宫" …) and did not line up with the in-game
    // hunting area the user actually stood in. The ordering below
    // is the community-verified HuntingId → stage name mapping:
    //   hid 0  Shrine Ruins   → 废神社 / 大社遗迹
    //   hid 1  Frost Islands  → 冰封群岛
    //   hid 2  Sandy Plains   → 沙原
    //   hid 3  Flooded Forest → 水没林
    //   hid 4  Lava Caverns   → 溶岩洞
    //   hid 5  Arena          → 斗技场
    //   hid 6  Red Stronghold → 翡叶要塞 (百龙夜行)
    //   hid 7  StageId 207   → 百龙夜行 (HunterPie treats StageId 207 as Rampage)
    //   hid 8  Infernal Springs → 狱泉乡
    //   hid 9  Jungle         → 密林
    //   hid 10 Citadel        → 城塞高地
    //   hid 11 Yawning Abyss  → 渊劫地狱
    //   hid 12 Forlorn Arena  → 塔之秘境
    // HunterPie never decodes hid 13..16 to a name (those IDs only
    // appear in Sunbreak's MR-rank hunting sub-tours and the
    // overlay's reader clamps them at computeZoneId() anyway), so
    // they are kept as a generic placeholder for completeness.
    case Zone::RiseLoc0:  return "大社遗迹";
    case Zone::RiseLoc1:  return "冰封群岛";
    case Zone::RiseLoc2:  return "沙原";
    case Zone::RiseLoc3:  return "水没林";
    case Zone::RiseLoc4:  return "溶岩洞";
    case Zone::RiseLoc5:  return "斗技场";
    case Zone::RiseLoc6:  return "翡叶要塞";
    case Zone::RiseLoc7:  return "百龙夜行";
    case Zone::RiseLoc8:  return "狱泉乡";
    case Zone::RiseLoc9:  return "密林";
    case Zone::RiseLoc10: return "城塞高地";
    case Zone::RiseLoc11: return "渊劫地狱";
    case Zone::RiseLoc12: return "塔之秘境";
    case Zone::RiseLoc13: return "未知狩猎区";
    case Zone::RiseLoc14: return "未知狩猎区";
    case Zone::RiseLoc15: return "未知狩猎区";
    case Zone::RiseLoc16: return "未知狩猎区";
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