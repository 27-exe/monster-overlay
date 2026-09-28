#pragma once

// Player-side shared contract types — the vocabulary both games and the UI
// agree on about the local player. Declared here, defined elsewhere.
//
// This directory carries types only, and that is the intended shape. The
// functions this header declares are members of the World reader class —
// MhwReader::readPlayer, MhwReader::readParty and the World
// player-abnormality table lookups — so their definitions moved with the class
// into src/world/player_reader.cpp; `grep 'src/player|src/quest' CMakeLists.txt`
// returns nothing, so neither this directory nor src/quest is in any source
// list.
//
// None of that makes these types World's. core/game_snapshot.h holds
// PlayerSnapshot and PartyMemberSnapshot, src/rise/mhr_reader.cpp fills the same
// PlayerSnapshot from Rise, and src/ui/panel_player.{h,cpp} plus
// viewmodel/player_view_model.h render it. AbnormalityAccent is the accent
// vocabulary both readers and both panels share — src/rise/mhr_abnormalities.h
// includes this header for that one enum alone. So this file is the cross-game
// contract, not one game's implementation detail.
//
// "Only a header, no .cpp" is therefore correct and complete, not a leftover of
// an unfinished move.

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

// v0.11.0: colour family of a player-abnormality pill, resolved ONCE by the
// data table from a stable id (World memory offset, Rise schema id) instead
// of by substring-matching the display name. The old
// debuffAccent(name) / buffAccent(name) helpers matched zh needles ("爆破")
// against the zh column and silently lost the colour family the day the UI
// switched to English — name.contains("blast") never matches "爆破" and the
// reverse, so a translated label left every pill in the default colour.
//
// The values are presentation-only: they NEVER change which abnormality is
// detected, only which family colour accentFor() paints.
enum class AbnormalityAccent {
    None,        // family default (debuff purple / buff green)
    Blast,       // blast blight / blastscourge / blast affliction
    Fire,        // fireblight / fire ailments
    Defense,     // defense down, armor/defense buffs
    Sleep,       // sleep
    Paralysis,   // paralysis
    Attack,      // demon / might / attack buffs
    Drink,       // dash juice / cool / hot drink / elemental res
};

struct PlayerAbnormality {
    int offset;          // memory offset for identification
    QString name;        // Chinese display name
    float timer{0.0F};   // remaining seconds (>0 = active)
    float maxTimer{0.0F};// tracked max for progress bar scaling
    // v0.11.0: colour family resolved from the stable id/offset by the data
    // table, never from the (translated) display name. See accentFor() in
    // panel_player.cpp.
    AbnormalityAccent accent{AbnormalityAccent::None};
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
    // v0.11.0: colour family, copied straight from the generating schema
    // (RiseAbnormalitySchema.accent) so the panel never infers it from the
    // translated name.
    AbnormalityAccent accent{AbnormalityAccent::None};
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

enum class PartyMemberKind {
    Player,
    Companion,
};

struct PartyMemberSnapshot {
    QString name;
    int weaponId{-1};
    int highRank{};
    int masterRank{};
    int damage{};
    bool local{};
    int slot{-1};  // display slot used for stable row colour/order

    // Rise damage events identify hunters/followers by entity index. World has
    // no separate identity today, so an unset value resolves to the existing
    // party slot and preserves all World reader/UI behaviour.
    int entityIndex{-1};
    PartyMemberKind kind{PartyMemberKind::Player};

    [[nodiscard]] int effectiveEntityIndex() const
    {
        return entityIndex >= 0 ? entityIndex : slot;
    }
};

// The console's OtherMembers filter is shared by Rise and World. Keep the
// complete roster for PlayerPanel; only the damage consumer receives this view.
inline QVector<PartyMemberSnapshot> visibleDamageParty(
    const QVector<PartyMemberSnapshot> &party, bool showOtherMembers)
{
    if (showOtherMembers)
        return party;

    QVector<PartyMemberSnapshot> visible;
    visible.reserve(1);
    for (const PartyMemberSnapshot &member : party) {
        if (member.local) {
            visible.append(member);
            break;
        }
    }
    return visible;
}

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

// v0.11.0: the colour family for the same keys, resolved by the same tables
// so a pill's accent comes from a stable id rather than from a substring
// test against the (translated) display name. Declared here (not in the
// UI) so both the reader and PlayerPanel::setupDemoData() — which seeds
// hand-written PlayerAbnormality structs the reader never sees — resolve
// accents from one place. Parameter counts differ from the *Name helpers
// above so neither overload ever needs a default argument.
AbnormalityAccent playerDebuffAccent(int offset);
AbnormalityAccent playerBuffAccent(int offset, int dependsOn, int withValue);

// ---------------------------------------------------------------------------
// v0.10.8: session player-count normalisation.
//
// HunterPie's MHWPlayer.GetParty (MHWPlayer.cs:344-355) treats partySize 0 as
// "solo", which makes 0 a MEANINGFUL value rather than a failure code. A raw
// read that misses the session structure (map transition, quest-start, target
// switch) therefore cannot be allowed to surface as 0: doing so would re-enable
// every per-part gauge that the World multiplayer gate suppresses, at exactly
// the moments the session is least stable.
//
// `raw` == kSessionReadFailed means the read itself failed; any other value is
// a genuine session reading, including 0 (solo). `previous` is the last count
// that came from a successful read (0 = none).
// ---------------------------------------------------------------------------
inline constexpr int kSessionReadFailed = -1;

inline int sanitizeSessionPlayerCount(int raw, int previous)
{
    if (raw == kSessionReadFailed)
        return previous;          // a miss keeps the last resolved session
    if (raw < 0 || raw > 4)
        return previous > 0 ? previous : 1;
    return raw;                   // 0..4 taken at face value, 0 = solo
}

} // namespace mhw
