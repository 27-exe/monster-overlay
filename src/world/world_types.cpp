#include "world_types.h"

namespace mhw {

const char *zoneName(Zone zone)
{
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