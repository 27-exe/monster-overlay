#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// world_types.h — the World zone vocabulary.
//
// This enum used to carry both games' named zones (RiseLoc0..16 and
// RiseTrainingRoom sat at the bottom, and world_types.cpp's lookups answered
// for the 600..616 / 700..799 bands). S1 replaced the snapshot field with
// ZoneRef{GameId, int id} (src/core/zone_id.h) but left the vocabulary here,
// so the file was the last place where one game answered for another. Rise's
// zones now live in src/rise/rise_zones.h.
//
// What is NOT an enumerator, deliberately: the runtime value domain is wider
// than any enumeration of it. readZone() passes the raw int32 through with
// only a 0..1000 range filter, so an unmapped value arrives as a bare
// integer. hasZoneName() is the honest answer to "can anyone name this id".
//
// World is unaffected: its own enumerators, strings, ids and call sites are
// unchanged. A World caller now calls ZoneRef-agnostic helpers, not a
// two-games-in-one table.

namespace mhw {

enum class WorldZone : int {
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
    Unknown = -1
};

const char* zoneName(WorldZone zone);
const char* zoneNameEn(WorldZone zone);
// v0.9 i18n (WS-A): switch between the pre-i18n zh literals and the English
// labels using the active StringTable locale (isEnglish()). Same ids, same
// enum: only the returned literal changes. Callers that already cache the
// string must re-resolve after a locale reload.
const char* zoneNameLocalized(WorldZone zone);
// Data-layer answer to "does this zone have a name in the table?".
// true  = the zone lookup hit a named entry (including the upstream hole
//         markers, which are entries of their own).
// false = no entry: an id passed straight to static_cast<WorldZone>() from a
//         raw villageId (0..100), or a value outside the table.
//
// UI logic must ask THIS instead of comparing a localized display label
// against a sentinel: whether a zone is named is a property of the data,
// never of the active language, and a rewording of a translation cannot
// move the branch. Locale-independent by construction.
bool hasZoneName(WorldZone zone);
bool isHuntingZone(WorldZone zone);
bool isPeaceZone(WorldZone zone);

} // namespace mhw
