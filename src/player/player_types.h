#pragma once

#include <QString>

namespace mhw {

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
};

struct PartyMemberSnapshot {
    QString name;
    int weaponId{-1};
    int masterRank{};
    int damage{};
    bool local{};
};

} // namespace mhw