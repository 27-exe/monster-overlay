#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// rise_zones.h — the Rise zone vocabulary.
//
// WHY THIS IS NOT IN world_types.h
// --------------------------------
// GameSnapshot::zone used to be a bare `WorldZone`, and every named zone in
// the overlay — both games' — was an enumerator in that one enum. S1 replaced
// the field with ZoneRef{GameId, int id} (src/core/zone_id.h) and moved the
// *contract* into core, but the *vocabulary* stayed where it was: src/world/
// still declared RiseLoc0..16 and RiseTrainingRoom, and world_types.cpp
// carried 54 case arms naming Rise zones.
//
// That compiles and links. It also lies: a reader called "world_types.h"
// containing Rise stage ids, and a file in game-world.a defining Rise
// labels, is exactly the confusion the S3/S5 library split exists to remove.
// This header is the last half of S1's contract: the id stays an int on the
// snapshot, and the names live beside the game that owns them.
//
// WHY PURE INLINE, NO .cpp
// ------------------------
// A new TU would have to be registered in tests/tu_census.json (the
// zero-reference gate) and, being invented only by its callers, would score
// ZERO — a new unexplained gate failure for a decision this file makes no
// claim about. So the vocabulary is a header, like src/rise/mhr_monster_names
// does for monsters: the table's ownership is the .cpp that includes it, and
// no archive gains symbols it must then justify. Every function below is
// `inline`, so including this header from several TUs is well-formed.
//
// The tables are the same ones world_types.cpp used to hold (verbatim
// strings, same ids, same provenance), so a label never changes value by
// moving. Where a comment explained a World zone, it was left in
// world_types.h; where it explained a Rise zone, it came here.

#include "core/string_table.h"

namespace mhw {

// A Rise zone with a name in the upstream localization table. The numeric
// values are what the reader emits, NOT a numbering of this enum:
//
//   * RiseLocN == HuntingId N == StageId N + 200. MHRPlayer.cs:216-227
//     emits StageId and resolves the label from Strings/Stages/Rise/Stage;
//     HunterPie's StageId 200..215 band collides with World's 201/202/203,
//     so computeZoneId() (src/rise/mhr_reader.cpp) rebases the same
//     HuntingId into the unused 600..616 band (HuntingId + 600, not + 200).
//     RiseLocN is the rebased value, and equals HuntingId N.
//   * RiseTrainingRoom is stage.type == 4, villageId == 5, rebased by
//     computeZoneId() into 700..799 as villageId + 700. Kept distinct from
//     RiseLoc5 (HuntingId 5) because the two are different places.
//   * Villages are villageId + 700 (700..799) and have NO enumerator —
//     they are bare ints with a name, resolved by riseZoneName()'s 700..799
//     branch, exactly as before. Only zones an enum can honestly name are
//     enumerators.
//
// What is NOT an enumerator, deliberately, because the runtime value domain
// is wider than any enumeration: -1 (MainMenu), -2 (LoadingScreen), 199
// (CharSelection). ZoneRef carries those as ints. A cast of one of them into
// this enum is undefined behaviour rather than a wrong name — which is why
// callers dispatch on the game and the tables answer per id, never the other
// way round.
enum class RiseZone : int {
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
    // stage.type == 4, villageId == 5, rebased to 705.
    RiseTrainingRoom = 705,
};

namespace rise_zone_detail {

// The two name tables, factored so that "does this zone have a name?" is
// answerable without reading a display string.
//
// A lookup returns nullptr when the id carries no table entry. The public
// accessors below turn that nullptr into the placeholder literal ("未知" /
// "Unknown"), so every rendered label stays byte-identical to the
// pre-split behaviour.
//
// nullptr is the ONLY representation of "no entry". The placeholder-looking
// rows are ordinary data rows that DO name a zone: RiseLoc0/6/8/16 are the
// official table's own hole markers (StageId 200/206/208/216 have no entry
// upstream) and keep their own explicit label, so they stay non-null.
//
// Both tables carry the same key set — every RiseZone enumerator plus the
// village ids 0..11 rebased into 700..799 — so the zh lookup doubles as the
// canonical "is this zone named?" test. A row that existed only in en would
// be a table bug to fix here, not a reason for a caller's logic to flip with
// the active language.
inline const char *zoneNameLookup(RiseZone zone)
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
    case RiseZone::RiseLoc0:  return "未知狩猎区"; // StageId 200: no entry upstream
    case RiseZone::RiseLoc1:  return "废神社";
    case RiseZone::RiseLoc2:  return "沙原";
    case RiseZone::RiseLoc3:  return "水没林";
    case RiseZone::RiseLoc4:  return "冰封群岛";
    case RiseZone::RiseLoc5:  return "熔岩洞";
    case RiseZone::RiseLoc6:  return "未知狩猎区"; // StageId 206: no entry upstream
    case RiseZone::RiseLoc7:  return "翡叶要塞";
    case RiseZone::RiseLoc8:  return "未知狩猎区"; // StageId 208: no entry upstream
    case RiseZone::RiseLoc9:  return "狱泉乡";
    case RiseZone::RiseLoc10: return "斗技场";
    case RiseZone::RiseLoc11: return "龙宫古城";
    case RiseZone::RiseLoc12: return "密林";
    case RiseZone::RiseLoc13: return "城塞高地";
    case RiseZone::RiseLoc14: return "塔之秘境";
    case RiseZone::RiseLoc15: return "渊劫地狱";
    case RiseZone::RiseLoc16: return "未知狩猎区"; // StageId 216: no entry upstream
    case RiseZone::RiseTrainingRoom: return "训练场";
    }
    return nullptr;
}

// v0.9 i18n (WS-A). English labels for the exact same id values, sourced
// from HunterPie's official en-us.xml (Strings/Stages/Rise), sha256
// 661d58b54bd1214ae0e9eff92fb4167b8aab54df0452eea238cb9574300030ea.
// Only the *labels* are new — the id mapping and every zh string returned by
// riseZoneName() are untouched.
//
// Notes on the upstream table's own quirks (kept verbatim, no invented
// distinctions):
//   * Rise villages are Stages/Rise ids 0..11 (the same table that gives
//     StageId = HuntingId + 200 for the maps).
//   * StageId 200/206/208/216 (RiseLoc0/6/8/16, en route to a hunt) have no
//     upstream entry: both columns keep a placeholder, ours here. That
//     placeholder is a named hole marker, not "no entry".
inline const char *zoneNameEnLookup(RiseZone zone)
{
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
    case RiseZone::RiseLoc0:  return "Unknown Hunting Zone"; // StageId 200: no entry upstream
    case RiseZone::RiseLoc1:  return "Shrine Ruins";
    case RiseZone::RiseLoc2:  return "Sandy Plains";
    case RiseZone::RiseLoc3:  return "Flooded Forest";
    case RiseZone::RiseLoc4:  return "Frost Islands";
    case RiseZone::RiseLoc5:  return "Lava Caverns";
    case RiseZone::RiseLoc6:  return "Unknown Hunting Zone"; // StageId 206: no entry upstream
    case RiseZone::RiseLoc7:  return "Red Stronghold";
    case RiseZone::RiseLoc8:  return "Unknown Hunting Zone"; // StageId 208: no entry upstream
    case RiseZone::RiseLoc9:  return "Infernal Springs";
    case RiseZone::RiseLoc10: return "Arena";
    case RiseZone::RiseLoc11: return "Coral Palace";
    case RiseZone::RiseLoc12: return "Jungle";
    case RiseZone::RiseLoc13: return "Citadel";
    case RiseZone::RiseLoc14: return "Forlorn Arena";
    case RiseZone::RiseLoc15: return "Yawning Abyss";
    case RiseZone::RiseLoc16: return "Unknown Hunting Zone"; // StageId 216: no entry upstream
    case RiseZone::RiseTrainingRoom: return "Training Area";
    }
    return nullptr;
}

} // namespace rise_zone_detail

// The zh column, locale-independent. "未知" is the placeholder for an id with
// no table entry — the same value the table has always returned for them.
inline const char *riseZoneName(RiseZone zone)
{
    const char *name = rise_zone_detail::zoneNameLookup(zone);
    return name != nullptr ? name : "未知";
}

// The en column, locale-independent (tests / tooling).
inline const char *riseZoneNameEn(RiseZone zone)
{
    const char *name = rise_zone_detail::zoneNameEnLookup(zone);
    return name != nullptr ? name : "Unknown";
}

// The single locale switch for Rise zone labels. An unloaded / non-English
// StringTable (tests, first frames) reads as Chinese, which keeps the
// pre-i18n behaviour.
inline const char *riseZoneNameLocalized(RiseZone zone)
{
    return StringTable::instance().isEnglish()
        ? riseZoneNameEn(zone)
        : riseZoneName(zone);
}

// Data-layer answer to "does this zone have a name?": true means a lookup hit
// a named entry for this id.
//
// Callers pair it with riseZoneNameLocalized() and use the false branch for a
// generic "据点 N" / "区域 N" fallback — instead of reverse-engineering the
// answer out of the *translated label*, which made the branch move whenever a
// translation was reworded.
//
// Locale-independent by design: whether a zone is named is a property of the
// id and the upstream table, never of the active language.
inline bool hasRiseZoneName(RiseZone zone)
{
    return rise_zone_detail::zoneNameLookup(zone) != nullptr;
}

// Hunting zones: the HuntingId band plus the training room, which HunterPie
// treats as a hunting zone even though it is stage.type == 4 (Village) —
// MHRPlayer creates MHRMeleeWeapon there, and the overlay shows the damage
// feed. Villages (700..799 other than 705) are peace zones.
inline bool isRiseHuntingZone(RiseZone zone)
{
    switch (zone) {
    case RiseZone::RiseLoc0:
    case RiseZone::RiseLoc1:
    case RiseZone::RiseLoc2:
    case RiseZone::RiseLoc3:
    case RiseZone::RiseLoc4:
    case RiseZone::RiseLoc5:
    case RiseZone::RiseLoc6:
    case RiseZone::RiseLoc7:
    case RiseZone::RiseLoc8:
    case RiseZone::RiseLoc9:
    case RiseZone::RiseLoc10:
    case RiseZone::RiseLoc11:
    case RiseZone::RiseLoc12:
    case RiseZone::RiseLoc13:
    case RiseZone::RiseLoc14:
    case RiseZone::RiseLoc15:
    case RiseZone::RiseLoc16:
    case RiseZone::RiseTrainingRoom:
        return true;
    }
    return false;
}

} // namespace mhw
