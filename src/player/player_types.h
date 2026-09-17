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

// v0.8.4-r18 player-abnormalities: Rise-only. One *active* abnormality from
// the Rise consumable/debuff blob (HunterPie MHRPlayer.GetConsumable-
// Abnormalities / GetPlayerDebuffAbnormalities). The World reader keeps
// filling the buff/debuff vectors above; this vector stays empty under World
// and is what the player panel's 「状态」 block renders under Rise.
enum class AbnormalityKind {
    Buff,    // consumable / skill / dango buff  (Category "Consumables")
    Debuff,  // debuff / blight                  (Category "Debuffs")
};

struct PlayerAbnormalitySnapshot {
    QString id;             // schema id, e.g. "ABN_POISON" (probe/debug)
    QString name;           // zh-cn display name (never empty)
    float   timer{0.0F};    // remaining seconds; buildup entries: the counter
    float   maxTimer{0.0F}; // MaxTimer / MaxBuildup, 0 = none
    AbnormalityKind kind{AbnormalityKind::Buff};
    bool    isBuildup{false};
    bool    isInfinite{false};
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
    // v0.7.1: wirebug (翔虫) state — Rise only. Up to four source slots
    // (default + environment + skill) depending on equipment and switch
    // skills; the Rise reader reads at most kRiseWirebugSlotCap (4)
    // source slots. The panel renders one capsule per entry, coloured by
    // cooldown progress.
    QVector<WirebugSnapshot> wirebugs;
    // v0.8.4-r18: Rise-only — active consumable buffs + debuffs read from
    // ABNORMALITIES_ADDRESS + CONS_/DEBUFF_ABNORMALITIES_OFFSETS (see
    // rise/mhr_abnormalities.h). The reader appends consumables first, then
    // debuffs (upstream call order); the panel re-orders debuffs-first when
    // it builds its rows, so nothing here depends on that order. Empty
    // under World, which keeps using the buffs/debuffs vectors above.
    QVector<PlayerAbnormalitySnapshot> abnormalities;
};

struct PartyMemberSnapshot {
    QString name;
    int weaponId{-1};
    int masterRank{};
    int damage{};
    bool local{};
    int slot{-1};  // party slot 0-3, used for color assignment
};

// ---------------------------------------------------------------------------
// v0.9 i18n (WS-A): locale-aware lookups for the World player-abnormality
// tables in player_reader.cpp. Each entry keeps its frozen pre-i18n zh-CN
// literal and carries an English column resolved from HunterPie
// Game/World/Data/AbnormalityData.xml (Offset/DependsOn/WithValue → the
// ABNORMALITY_* key) + the official en-us.xml Abnormalities table. The lookups
// key rows exactly like the reader does; 0x6A0 / 0x6B0 / 0x6CC / 0x6D0 appear
// twice and are told apart by (dependsOn, withValue). An unknown key returns
// the offset/id as "0xNNN", matching the reader's debug style.
// ---------------------------------------------------------------------------
QString playerDebuffName(int offset);
QString playerSongName(int id);
QString playerBuffName(int offset, int dependsOn, int withValue);

} // namespace mhw
