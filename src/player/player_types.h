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
    QString name;          // character name (HunterPie MHWPlayer.Name)
    float health{};
    float maxHealth{};
    float stamina{};
    float maxStamina{};
    // Self-only fields read directly from the local player struct so
    // PlayerPanel can show them even when the party array is empty
    // (e.g. in the gathering hub before joining a quest).
    int masterRank{};      // MHWPlayerLevelStructure.MasterRank (+0x70+0x2)
    int highRank{};        // save header +0x90 (int16)
    int weaponId{-1};      // MHWPlayerEquipmentData.WeaponType (+0x7C)
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
    float mantleSlot0CooldownMax{270.0F};  // HunterPie cooldowns[id+20]
    int mantleSlot1Id{-1};
    float mantleSlot1Timer{};
    float mantleSlot1Cooldown{};
    float mantleSlot1CooldownMax{270.0F};
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
