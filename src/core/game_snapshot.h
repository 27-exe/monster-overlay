#pragma once

#include "core/zone_id.h"
#include "monster/monster_types.h"
#include "player/player_types.h"
#include "quest/quest_types.h"

#include <QString>
#include <QVector>
#include <cstdint>

namespace mhw {

struct GameSnapshot {
    GameId game{GameId::World};
    bool attached{};
    qint64 pid{-1};
    std::uintptr_t imageBase{};
    QString status;
    // Game-agnostic: which game, and that game's raw zone id. The vocabulary
    // (what the id is called, whether it is a hunting ground) belongs to the
    // game — see world::hasZoneName / rise's equivalent. This header used to
    // include world/world_types.h, which put a World header inside the core
    // contract and made "core knows about World" a structural fact rather than
    // an accident of file layout.
    ZoneRef zone;
    QVector<MonsterSnapshot> monsters;
    PlayerSnapshot player;
    QVector<PartyMemberSnapshot> party;
    QuestSnapshot quest;
    // World: the session's real player count, read from the session structure
    // (see world_reader.cpp readSessionPlayerCount). Distinct from
    // party.size(), which counts every filled 4-slot roster entry — including
    // companions — because MhwReader::readParty never tags a member's
    // PartyMemberKind. 0 means "not in a session" (solo).
    int playerCount{0};
    bool isMultiplayer{false};
};

} // namespace mhw
