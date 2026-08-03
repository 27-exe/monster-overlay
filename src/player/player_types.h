#pragma once

#include <QString>
#include <QVector>

namespace mhw {

// Sharpness - HunterPie MHWMeleeWeapon.GetWeaponSharpness. The
// player panel renders a 7-segment coloured bar (red->purple) plus
// a numeric badge showing the current segment's remaining hits.
//
//   level         Red=0..Purple=6, Broken=-1, Invalid=7
//   currentHits   raw int read from weaponSharpness+0x20F8
//   maxHits       currentLevel fixed upper bound + handicraft bonus
//   threshold     end-of-previous-level value (where the coloured
//                 bar segment starts)
//   thresholds[7] per-weapon fixed upper bounds (red..purple) read
//                 from the in-game weapon data array; zero entries
//                 mean the weapon doesn't reach that level.
struct SharpnessSnapshot {
    int level{-1};          // Sharpness enum value; -1 = invalid
    int currentHits{0};
    int maxHits{0};
    int threshold{0};
    int thresholds[7]{};
    bool valid{false};      // true once a melee weapon is equipped
                            // and the memory read succeeded
};

struct PlayerAbnormality {
    int offset;          // memory offset for identification
    QString name;        // Chinese display name
    float timer{0.0F};   // remaining seconds (>0 = active)
    float maxTimer{0.0F};// tracked max for progress bar scaling
};

// v0.7.1: wirebug (翔虫) snapshot. Rise-specific — World has no wirebug
// system. Read once per poll from MHRWirebugStructure + the in-game
// extras array; rendered by PlayerPanel as a horizontal capsule row.
struct WirebugSnapshot {
    int   slot{0};
    bool  isAvailable{false};
    bool  isTemporary{false};
    float cooldown{0.0F};
    float maxCooldown{0.0F};
    float timer{0.0F};
    float maxTimer{0.0F};
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
    // Buffs (songs, consumables, skills — positive effects)
    QVector<PlayerAbnormality> buffs;
    // Sharpness — valid only when the equipped weapon is melee.
    // Ranged weapons leave valid=false; the panel hides the bar in
    // that case.
    SharpnessSnapshot sharpness;
    // v0.7.1: wirebug (翔虫) state — Rise only. The hunter has 1-3
    // wirebugs at a time depending on equipment and switch skills. The
    // panel renders one capsule per entry, coloured by cooldown progress.
    QVector<WirebugSnapshot> wirebugs;
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
