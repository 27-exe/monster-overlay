#pragma once

namespace mhw {

enum class Zone : int {
    MainMenu = 0,
    AncientForest = 101,
    WildspireWaste = 102,
    CoralHighlands = 103,
    RottenVale = 104,
    EldersRecess = 105,
    GreatRavine = 106,
    GreatRavine2 = 107,
    HoarfrostReach = 108,
    GuidingLands = 109,
    SpecialArena = 201,
    Arena = 202,
    SelianaSupplyCache = 203,
    Astera = 301,
    AsteraGatheringHub = 302,
    ResearchBase = 303,
    Seliana = 305,
    SelianaGatheringHub = 306,
    Introduction = 401,
    Everstream = 403,
    ConfluenceOfFates = 405,
    AncientForest2 = 406,
    CavernsOfElDorado = 409,
    SelianaSupplyCache2 = 411,
    OriginIsle = 412,
    OriginIsle2 = 413,
    SecludedValley = 415,
    SecludedValley2 = 416,
    CastleSchrade = 417,
    LivingQuarters = 501,
    PrivateQuarters = 502,
    PrivateSuite = 503,
    TrainingArea = 504,
    ChamberOfFive = 505,
    SelianaRoom = 506,
    // Rise stage.type == 4, villageId == 5.  Kept distinct from RiseLoc5
    // (HuntingId 5) because the reader rebases VillageId values to 700..799.
    RiseTrainingRoom = 705,
    // Rise maps: HunterPie uses StageId = HuntingId + 200 and resolves the
    // label from its localization table (MHRPlayer.cs:216-227); that table
    // covers StageId 201..215 (holes at 200/206/208, v0.8.4-r18
    // zone-names). 200..215 collides with World zones 201/202/203, so the
    // reader rebases the same HuntingId into the unused 600..616 band
    // (HuntingId + 600 instead of + 200). RiseLocN == HuntingId N ==
    // StageId N + 200; names in zoneName() are that table's zh-CN strings.
    RiseLoc0 = 600,
    RiseLoc1 = 601,
    RiseLoc2 = 602,
    RiseLoc3 = 603,
    RiseLoc4 = 604,
    RiseLoc5 = 605,
    RiseLoc6 = 606,
    RiseLoc7 = 607,
    RiseLoc8 = 608,
    RiseLoc9 = 609,
    RiseLoc10 = 610,
    RiseLoc11 = 611,
    RiseLoc12 = 612,
    RiseLoc13 = 613,
    RiseLoc14 = 614,
    RiseLoc15 = 615,
    RiseLoc16 = 616,
    Unknown = -1
};

const char* zoneName(Zone zone);
const char* zoneNameEn(Zone zone);
// v0.9 i18n (WS-A): switch between the pre-i18n zh literals and the English
// labels using the active StringTable locale (isEnglish()). Same ids, same
// enum: only the returned literal changes. Callers that already cache the
// string must re-resolve after a locale reload.
const char* zoneNameLocalized(Zone zone);
bool isHuntingZone(Zone zone);
bool isPeaceZone(Zone zone);

} // namespace mhw