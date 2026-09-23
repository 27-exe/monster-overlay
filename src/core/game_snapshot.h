#pragma once

#include "monster/monster_types.h"
#include "player/player_types.h"
#include "quest/quest_types.h"
#include "world/world_types.h"

#include <QByteArray>
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
    Zone zone{Zone::MainMenu};
    QVector<MonsterSnapshot> monsters;
    PlayerSnapshot player;
    QVector<PartyMemberSnapshot> party;
    QuestSnapshot quest;
    // World: the session's real player count, read from the session structure
    // (see mhw_reader.cpp readSessionPlayerCount). Distinct from
    // party.size(), which counts every filled 4-slot roster entry — including
    // companions — because MhwReader::readParty never tags a member's
    // PartyMemberKind. 0 means "not in a session" (solo).
    int playerCount{0};
    bool isMultiplayer{false};
    // Diagnostic raw bytes (populated when --diagnose-ailments is set).
    QVector<QByteArray> diagnosisAilmentPointers;
    QByteArray diagnosisAilmentBlock;
};

} // namespace mhw