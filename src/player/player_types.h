#pragma once

#include <QString>
#include <QVector>

namespace mhw {

struct PlayerAbnormality {
    int offset;          // memory offset for identification
    QString name;        // Chinese display name
    float timer{0.0F};   // remaining seconds (>0 = active)
    float maxTimer{0.0F};// tracked max for progress bar scaling
};

struct PlayerSnapshot {
    float health{};
    float maxHealth{};
    float stamina{};
    float maxStamina{};
    bool valid{};
    // Mantle equipped timers
    float mantleHealthTimer{};
    float mantleHealthLargeTimer{};
    float mantleStaminaTimer{};
    float mantleStaminaLargeTimer{};
    float mantleToolTimer{};
    float mantleToolLargeTimer{};
    float earplugTimer{};
    int mantleSlot0Id{-1};
    float mantleSlot0Timer{};
    float mantleSlot0Cooldown{};
    int mantleSlot1Id{-1};
    float mantleSlot1Timer{};
    float mantleSlot1Cooldown{};
    // Debuffs (poison, paralysis, blast, etc.)
    QVector<PlayerAbnormality> debuffs;
};

struct PartyMemberSnapshot {
    QString name;
    int weaponId{-1};
    int masterRank{};
    int damage{};
    bool local{};
    int slot{-1};  // party slot 0-3, used for color assignment
};

} // namespace mhw