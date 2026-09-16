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
    // Rise maps (HunterPie uses HuntingId + 200, MHRPlayer.cs:218-222,
    // but 200..216 collides with World zones 201/202/203 which use the
    // same ints. To avoid the value collision we shift into the unused
    // 600..616 range and let computeZoneId() do the +400 translation
    // instead. HunterPie's own client only sees 200..216 internally
    // — the overlay's job is to give the panel a stable identifier,
    // not to mirror HunterPie's internal enum exactly.)
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
bool isHuntingZone(Zone zone);
bool isPeaceZone(Zone zone);

} // namespace mhw