#include "mhw_reader.h"
#include "core/string_table.h"

#include <algorithm>


namespace mhw {

namespace {
// v0.9 i18n (WS-A): pick the abnormality name column for debuffs / songs /
// buffs. Every zh literal in those tables is the frozen pre-i18n value, and
// an unloaded or non-English StringTable reads as Chinese, so the pre-i18n
// behaviour is preserved verbatim.
const char *localizedAbnormalityName(const char *zh, const char *en)
{
    if (en != nullptr && en[0] != '\0' && StringTable::instance().isEnglish())
        return en;
    return zh;
}

} // namespace

struct DebuffDef { int offset; const char *name; int dependsOn; int withValue; const char *nameEn; };
static const DebuffDef kDebuffs[] = {
    {0x5DC, "毒",       0, 0, "Poison"},
    {0x5E0, "猛毒",     0, 0, "Venom"},
    {0x5EC, "火异常",   0, 0, "Fireblight"},
    {0x5F0, "雷异常",   0, 0, "Thunderblight"},
    {0x5F4, "水异常",   0, 0, "Waterblight"},
    {0x5F8, "冰异常",   0, 0, "Iceblight"},
    {0x5FC, "龙异常",   0, 0, "Dragonblight"},
    {0x600, "裂伤",     0, 0, "Bleed"},
    {0x608, "瘴气",     0, 0, "Effluvia"},
    {0x60C, "防御↓",   0, 0, "Defense Down"},
    {0x614, "耐性↓",   0, 0, "Resistance Down"},
    {0x620, "爆破",     0, 0, "Blastblight"},
    {0x63C, "爆破灾祸", 0x62C, 1, "Blastscourge"},
};

struct SongDef { int id; const char *name; const char *nameEn; };
static const SongDef kSongs[] = {
    {0x38, "自我强化", "Self-Improvement"}, {0x3C, "攻击强化", "Attack Up"}, {0x40, "攻击强化大", "Attack Up (L)"},
    {0x44, "体力强化", "Health Boost"}, {0x48, "体力强化大", "Health Boost (L)"},
    {0x4C, "耐力消耗↓", "Stamina Use Reduced"}, {0x50, "耐力消耗↓大", "Stamina Use Reduced (L)"},
    {0x54, "风压无效", "Wind Pressure Negated"}, {0x58, "风压完全无效", "All Wind Pressure Negated"},
    {0x5C, "防御强化", "Defense Up"}, {0x60, "防御强化大", "Defense Up (L)"},
    {0x64, "道具消耗↓", "Tool Use Drain Red."}, {0x68, "道具消耗↓大", "Tool Use Drain Red. (L)"},
    {0x80, "体力回复", "Health Rec."}, {0x84, "体力回复大", "Health Rec. (L)"},
    {0x88, "耳栓", "Earplugs (S)"}, {0x8C, "耳栓+", "Earplugs (L)"},
    {0x90, "精灵加护", "Divine Protection"}, {0x94, "导虫强化", "Scoutfly Power Up"},
    {0x98, "环境无害", "Env. Damage Negated"}, {0x9C, "气绝无效", "Stun Negated"},
    {0xA0, "麻痹无效", "Paralysis Negated"}, {0xA4, "震动无效", "Tremors Negated"},
    {0xA8, "深渊抵抗", "Muck/Water/Deep Snow Resistance"},
    {0xAC, "火耐性", "Fire Res. Up"}, {0xB0, "火耐性大", "Fire Res. Up (L)"},
    {0xB4, "水耐性", "Water Res. Up"}, {0xB8, "水耐性大", "Water Res. Up (L)"},
    {0xBC, "雷耐性", "Thunder Res. Up"}, {0xC0, "雷耐性大", "Thunder Res. Up (L)"},
    {0xC4, "冰耐性", "Ice Res. Up"}, {0xC8, "冰耐性大", "Ice Res. Up (L)"},
    {0xCC, "龙耐性", "Dragon Res. Up"}, {0xD0, "龙耐性大", "Dragon Res. Up (L)"},
    {0xD4, "属性攻击↑", "Elemental Attack Up"}, {0xD8, "全异常无效", "Blights Negated"},
    {0xE4, "击退无效", "Knockback Negated"}, {0xEC, "全耐性↑", "All Res. Up"},
    {0xF0, "会心强化", "Affinity Up"}, {0xF4, "全状态异常无效", "All Ailments Negated"},
    {0xFC, "异常攻击↑", "Abnormal Status Atk. Up (S)"},
    {0x10C, "最大耐力回复", "Max Stamina Up + Recovery"}, {0x110, "体力回复量↑", "Extended Health Recovery"},
    {0x114, "速度·回避↑", "Speed Boost + Evade Window Up"}, {0x118, "全属性强化", "Elemental Effectiveness"},
};

struct BuffDef { int offset; const char *name; int dependsOn; int withValue; const char *nameEn; };
static const BuffDef kBuffs[] = {
    // Consumables
    {0x690, "急奔饮料",   0, 0, "Dash Juice"},
    {0x694, "活力剂",     0, 0, "Wiggly Litchy"},
    {0x698, "星辰肉干",   0, 0, "Astera Jerky"},
    {0x6A0, "怪力种子",   0x6A4, 10, "Might Seed"},
    {0x6A0, "怪力药丸",   0x6A4, 25, "Might Pill"},
    {0x6B0, "忍耐种子",   0x6B4, 20, "Adamant Seed"},
    {0x6B0, "忍耐药丸",   0x6BC, 1, "Adamant Pill"},
    {0x6C4, "鬼人粉尘",   0, 0, "Demon Powder"},
    {0x6C8, "硬化粉尘",   0, 0, "Hardshell Powder"},
    {0x6CC, "鬼人药",     0x6D4, 1, "Demondrug"},
    {0x6CC, "大鬼人药",   0x6D4, 2, "Mega Demondrug"},
    {0x6D0, "硬化药",     0x6D8, 1, "Armorskin"},
    {0x6D0, "大硬化药",   0x6D8, 2, "Mega Armorskin"},
    {0x6EC, "冷饮",       0, 0, "Cool Drink"},
    {0x6F0, "热饮",       0, 0, "Hot Drink"},
    {0x6F8, "体力回复",   0, 0, "Health Regen."},
    {0x6FC, "耐寒强化",   0, 0, "Cold Res."},
    {0x718, "力量松果",   0, 0, "Powercone"},
    {0x71C, "耐热强化",   0, 0, "Ice Res. (L)"},
    // Skills
    {0x764, "不屈",       0, 0, "Fortify"},
    {0x76C, "刚刃研磨",   0, 0, "Protective Polish"},
    {0x770, "滑走强化",   0, 0, "Affinity Sliding"},
    {0x730, "属性加速",   0, 0, "Element Acceleration"},
    {0x738, "力量解放",   0, 0, "Latent Power"},
    {0x754, "肾上腺素",   0, 0, "Adrenaline"},
    {0x788, "冰气炼成",   0, 0, "Frostcraft"},
    {0x79C, "攻击守势",   0, 0, "Offensive Guard"},
    {0x7A0, "转福",       0, 0, "Coalescence"},
};


// v0.9 i18n (WS-A): the three World player-abnormality tables below are
// keyed exactly the way the reader keys them — memory offset, plus
// HunterPie's DependsOn/WithValue precondition where AbnormalityData.xml
// defines one. 0x6A0 / 0x6B0 / 0x6CC / 0x6D0 each carry TWO rows and are
// told apart only by (dependsOn, withValue), so those are part of the key.
// Each row keeps its frozen zh literal and gains an English column from the
// official localization; StringTable::isEnglish() picks the column.
QString playerDebuffName(int offset)
{
    for (const auto &d : kDebuffs)
        if (d.offset == offset)
            return QString::fromUtf8(localizedAbnormalityName(d.name, d.nameEn));
    return QStringLiteral("0x") + QString::number(offset, 16).toUpper();
}

QString playerSongName(int id)
{
    for (const auto &s : kSongs)
        if (s.id == id)
            return QString::fromUtf8(localizedAbnormalityName(s.name, s.nameEn));
    return QStringLiteral("0x") + QString::number(id, 16).toUpper();
}

QString playerBuffName(int offset, int dependsOn, int withValue)
{
    for (const auto &b : kBuffs) {
        if (b.offset != offset)
            continue;
        if (dependsOn != 0 && b.dependsOn != dependsOn)
            continue;
        if (withValue != 0 && b.withValue != withValue)
            continue;
        return QString::fromUtf8(localizedAbnormalityName(b.name, b.nameEn));
    }
    return QStringLiteral("0x") + QString::number(offset, 16).toUpper();
}

// ===================================================================
// refreshPlayerIdentity — HunterPie MHWPlayer.GetBasicData()
//
// Reads the local player's name, Master Rank, and High Rank from the
// persistent save-header stretch (not the HUD / party area).
// HunterPie path:
//   LEVEL_OFFSET (0x05013950) → LevelOffsets (0xA8)
//   → saveBase
//   → saveBase+0x44 = current save slot (uint32)
//   → saveBase + slot * 0x26CC00 = player save header
//   → saveHeader+0x50  = name (32 bytes)
//   → saveHeader+0x90  = HighRank (int16)
//   → saveHeader+0xD4  = MasterRank (int16)
// ===================================================================
void MhwReader::refreshPlayerIdentity(PlayerSnapshot &player)
{
    const std::uintptr_t saveBase = followPointerChain(
        memory_,
        absolute(QStringLiteral("LEVEL_OFFSET")),
        map_.offsets(QStringLiteral("LevelOffsets")),
        nullptr);
    if (!saveBase)
        return;

    const auto slot = memory_.read<std::uint32_t>(saveBase + 0x44ULL);
    if (!slot.has_value())
        return;
    // HunterPie: firstSaveAddress points to a struct; +0x0 holds a
    // *pointer* to the save-data array base.
    const auto firstSaveHeader = memory_.read<std::uintptr_t>(
        saveBase + 0x0ULL);
    if (!firstSaveHeader.has_value() || !isSanePointer(*firstSaveHeader))
        return;
    const std::uintptr_t header = *firstSaveHeader
        + static_cast<std::uintptr_t>(*slot) * 0x26CC00ULL;
    if (!isSanePointer(header))
        return;

    player.name = readUtf8(header + 0x50ULL, 32);
    if (const auto hr = memory_.read<std::int16_t>(header + 0x90ULL))
        player.highRank = *hr;
    if (const auto mr = memory_.read<std::int16_t>(header + 0xD4ULL))
        player.masterRank = *mr;
}

// ===================================================================
// readPlayer — HunterPie MHWPlayer.ReadVitals / GetMantlesData / debuffs
//
// Mantle / equipment CD sections may briefly fail to read in multiplayer
// (4-player sessions) when the EQUIPMENT_ADDRESS pointer chain is being
// updated by the engine. We cache the last successful per-section read
// (abnormalityBase mantle timers, equipmentBase slot ids/cooldowns) and
// replay it on a transient failure so the panel doesn't flash a stale
// "no mantle equipped" placeholder for a single frame.
//
// See mhw_reader.h:CachedMantles for the field set.
// ===================================================================
PlayerSnapshot MhwReader::readPlayer(QString *error)
{
    PlayerSnapshot result;
    const std::uintptr_t hud = followPointerChain(
        memory_,
        absolute(QStringLiteral("EQUIPMENT_ADDRESS")),
        map_.offsets(QStringLiteral("PLAYER_BASIC_INFORMATION_OFFSETS")),
        error);
    if (!hud)
        return result;

    const auto maxHealth = memory_.read<float>(hud + 0x60);
    const auto health   = memory_.read<float>(hud + 0x64);
    const auto stamina  = memory_.read<float>(hud + 0x12C);
    const auto maxStamina = memory_.read<float>(hud + 0x130);
    // Weapon: HunterPie MHWPlayer.GetWeaponData() uses a dedicated
    // WEAPON_ADDRESS→WEAPON_OFFSETS pointer chain. The byte at
    // hud+0x7C is unreliable for the local player (often stale / 0).
    const std::uintptr_t weaponAddr = followPointerChain(
        memory_,
        absolute(QStringLiteral("WEAPON_ADDRESS")),
        map_.offsets(QStringLiteral("WEAPON_OFFSETS")),
        nullptr);
    if (weaponAddr) {
        if (const auto wp = memory_.read<std::uint8_t>(weaponAddr))
            result.weaponId = static_cast<int>(*wp);
    }
    if (maxHealth && health && stamina && maxStamina) {
        result.maxHealth  = *maxHealth;
        result.health     = *health;
        result.stamina    = *stamina;
        result.maxStamina = *maxStamina;
        result.valid = std::isfinite(result.health)
                    && result.maxHealth > 0.0F
                    && result.maxHealth < 10000.0F;
    }

    // ---- abnormalities (mantle timers -> HUD struct) ----
    const std::uintptr_t abnormalityBase = followPointerChain(
        memory_,
        absolute(QStringLiteral("EQUIPMENT_ADDRESS")),
        map_.offsets(QStringLiteral("ABNORMALITY_OFFSETS")),
        nullptr);
    // 75-float timer array shared by mantles + songs buffs (read once).
    constexpr std::size_t kSlotCount = 75;
    std::vector<float> timers;
    bool timersValid = false;
    bool abnormalitySectionRead = false;  // v0.7.5: cache gate
    if (abnormalityBase) {
        timers = memory_.readArray<float>(
            abnormalityBase + 0x38ULL, kSlotCount, nullptr);
        timersValid = (timers.size() == kSlotCount);
        if (timersValid) {
            auto slot = [&](int abnormalityId) -> float {
                const int idx = (abnormalityId - 0x38) / 4;
                return (idx >= 0 && idx < static_cast<int>(kSlotCount))
                    ? (std::isfinite(timers[idx]) && timers[idx] > 0.0F
                           ? timers[idx] : 0.0F)
                    : 0.0F;
            };
            result.mantleHealthTimer       = slot(0x44);
            result.mantleHealthLargeTimer  = slot(0x48);
            result.mantleStaminaTimer      = slot(0x4C);
            result.mantleStaminaLargeTimer = slot(0x50);
            result.mantleToolTimer         = slot(0x64);
            result.mantleToolLargeTimer    = slot(0x68);
            result.earplugTimer            = slot(0x88);
            abnormalitySectionRead = true;
        }
    }
    // v0.7.5 cache fallback: if the chain failed this poll but we have
    // a fresh cache (within kMantleCacheTtl ticks), copy the abnormality
    // timer fields back so the panel doesn't flicker to "no mantle". The
    // TTL prevents stale data showing up minutes after the player swapped
    // gear.
    if (!abnormalitySectionRead
        && mantlesCachedAtTick_ >= 0
        && (pollTick_ - mantlesCachedAtTick_) <= kMantleCacheTtl) {
        result.mantleHealthTimer       = cachedMantles_.mantleHealthTimer;
        result.mantleHealthLargeTimer  = cachedMantles_.mantleHealthLargeTimer;
        result.mantleStaminaTimer      = cachedMantles_.mantleStaminaTimer;
        result.mantleStaminaLargeTimer = cachedMantles_.mantleStaminaLargeTimer;
        result.mantleToolTimer         = cachedMantles_.mantleToolTimer;
        result.mantleToolLargeTimer    = cachedMantles_.mantleToolLargeTimer;
        result.earplugTimer            = cachedMantles_.earplugTimer;
    }

    // ---- equipment mantles (EQUIPMENT_ADDRESS -> EQUIPMENT_OFFSETS) ----
    const std::uintptr_t equipmentBase = followPointerChain(
        memory_,
        absolute(QStringLiteral("EQUIPMENT_ADDRESS")),
        map_.offsets(QStringLiteral("EQUIPMENT_OFFSETS")),
        nullptr);
    bool equipmentSectionRead = false;  // v0.7.5: cache gate
    if (equipmentBase) {
        // HunterPie GetMantlesData() reads 40 floats per array:
        //   timers[id]      = active timer
        //   timers[id + 20] = max active timer
        //   cooldowns[id]   = current cooldown remaining
        //   cooldowns[id+20]= max cooldown (per-mantle ceiling — this
        //                     is what scales the strip properly for
        //                     long-cooldown mantles like Rocksteady)
        const auto timers = memory_.readArray<float>(
            equipmentBase + 0xA8CULL, 40, nullptr);
        const auto cooldowns = memory_.readArray<float>(
            equipmentBase + 0x99CULL, 40, nullptr);
        if (timers.size() == 40 && cooldowns.size() == 40) {
            int slot = 0;
            for (int id = 0; id < 20; ++id) {
                const float t  = timers[id];
                const float cd = cooldowns[id];
                const bool active  = std::isfinite(t) && t > 0.0F;
                const bool cooling = std::isfinite(cd) && cd > 0.0F;
                if (!active && !cooling) continue;
                if (slot >= 2) break;
                if (slot == 0) {
                    result.mantleSlot0Id           = id;
                    result.mantleSlot0Timer        = active ? t : 0.0F;
                    result.mantleSlot0Cooldown     = cooling ? cd : 0.0F;
                    result.mantleSlot0CooldownMax  =
                        std::isfinite(cooldowns[id + 20])
                            ? cooldowns[id + 20] : 270.0F;
                } else {
                    result.mantleSlot1Id           = id;
                    result.mantleSlot1Timer        = active ? t : 0.0F;
                    result.mantleSlot1Cooldown     = cooling ? cd : 0.0F;
                    result.mantleSlot1CooldownMax  =
                        std::isfinite(cooldowns[id + 20])
                            ? cooldowns[id + 20] : 270.0F;
                }
                ++slot;
            }
            equipmentSectionRead = true;
        }
    }
    // v0.7.5 cache fallback (same TTL semantics as the abnormality
    // section above). Only the equipment slot fields are restored —
    // not the broad "did the chain resolve" flag.
    if (!equipmentSectionRead
        && mantlesCachedAtTick_ >= 0
        && (pollTick_ - mantlesCachedAtTick_) <= kMantleCacheTtl) {
        result.mantleSlot0Id          = cachedMantles_.mantleSlot0Id;
        result.mantleSlot0Timer       = cachedMantles_.mantleSlot0Timer;
        result.mantleSlot0Cooldown    = cachedMantles_.mantleSlot0Cooldown;
        result.mantleSlot0CooldownMax = cachedMantles_.mantleSlot0CooldownMax;
        result.mantleSlot1Id          = cachedMantles_.mantleSlot1Id;
        result.mantleSlot1Timer       = cachedMantles_.mantleSlot1Timer;
        result.mantleSlot1Cooldown    = cachedMantles_.mantleSlot1Cooldown;
        result.mantleSlot1CooldownMax = cachedMantles_.mantleSlot1CooldownMax;
    }
    // Stash whatever we successfully read this tick into the cache, so a
    // future failed tick can replay it. We do this *unconditionally* if
    // at least one section read (the abnormal-only or equipment-only
    // case still has useful data to preserve).
    if (abnormalitySectionRead || equipmentSectionRead) {
        cachedMantles_.mantleHealthTimer       = result.mantleHealthTimer;
        cachedMantles_.mantleHealthLargeTimer  = result.mantleHealthLargeTimer;
        cachedMantles_.mantleStaminaTimer      = result.mantleStaminaTimer;
        cachedMantles_.mantleStaminaLargeTimer = result.mantleStaminaLargeTimer;
        cachedMantles_.mantleToolTimer         = result.mantleToolTimer;
        cachedMantles_.mantleToolLargeTimer    = result.mantleToolLargeTimer;
        cachedMantles_.earplugTimer            = result.earplugTimer;
        cachedMantles_.mantleSlot0Id           = result.mantleSlot0Id;
        cachedMantles_.mantleSlot0Timer        = result.mantleSlot0Timer;
        cachedMantles_.mantleSlot0Cooldown     = result.mantleSlot0Cooldown;
        cachedMantles_.mantleSlot0CooldownMax  = result.mantleSlot0CooldownMax;
        cachedMantles_.mantleSlot1Id           = result.mantleSlot1Id;
        cachedMantles_.mantleSlot1Timer        = result.mantleSlot1Timer;
        cachedMantles_.mantleSlot1Cooldown     = result.mantleSlot1Cooldown;
        cachedMantles_.mantleSlot1CooldownMax  = result.mantleSlot1CooldownMax;
        mantlesCachedAtTick_ = pollTick_;
    }

    // ---- debuffs (abnormalityBase + offset) ----
    if (abnormalityBase) {
        // v0.9 i18n (WS-A): nameEn is the English column, resolved from
        // HunterPie Game/World/Data/AbnormalityData.xml (this row's
        // Offset/DependsOn/WithValue) → ABNORMALITY_* key → en-us.xml. The zh
        // literals are the frozen pre-i18n values; isEnglish() picks a column.
        for (const auto &d : kDebuffs) {
            if (d.dependsOn != 0) {
                const auto pre = memory_.read<std::int32_t>(
                    abnormalityBase + static_cast<std::uintptr_t>(d.dependsOn));
                if (!pre || *pre != d.withValue) continue;
            }
            const auto timer = memory_.read<float>(
                abnormalityBase + static_cast<std::uintptr_t>(d.offset));
            if (!timer || !std::isfinite(*timer) || *timer <= 0.0F)
                continue;
            PlayerAbnormality ab;
            ab.offset  = d.offset;
            ab.name    = playerDebuffName(d.offset);
            ab.timer   = *timer;
            result.debuffs.push_back(ab);
        }

        // ---- buffs: Songs (hunting horn, from float array) ----
        // HunterPie GetHuntingHornAbnormalities: index = (Id - 0x38) / 4
        // Reuse the 75-float array already read above for mantles.
        if (timersValid) {
            for (const auto &s : kSongs) {
                const int idx = (s.id - 0x38) / 4;
                if (idx < 0 || idx >= static_cast<int>(kSlotCount)) continue;
                const float t = timers[idx];
                if (!std::isfinite(t) || t <= 0.0F) continue;
                PlayerAbnormality ab;
                ab.offset  = s.id;
                ab.name    = playerSongName(s.id);
                ab.timer   = t;
                result.buffs.push_back(ab);
            }
        }

        // ---- buffs: Consumables + Skills (direct offset read) ----
        for (const auto &b : kBuffs) {
            if (b.dependsOn != 0) {
                const auto pre = memory_.read<std::int32_t>(
                    abnormalityBase + static_cast<std::uintptr_t>(b.dependsOn));
                if (!pre || *pre != b.withValue) continue;
            }
            const auto timer = memory_.read<float>(
                abnormalityBase + static_cast<std::uintptr_t>(b.offset));
            if (!timer || !std::isfinite(*timer) || *timer <= 0.0F)
                continue;
            PlayerAbnormality ab;
            ab.offset  = b.offset;
            ab.name    = playerBuffName(b.offset, b.dependsOn, b.withValue);
            ab.timer   = *timer;
            result.buffs.push_back(ab);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// readSessionPlayerCount — HunterPie MHWPlayer.GetParty (MHWPlayer.cs:344-355)
//
// The session's real player count, as an int, straight out of the session
// structure. This is the ONLY reliable "are we in a multiplayer session"
// signal on World: the 4-slot roster (readParty above) is filled by name
// and MhwReader::readParty never tags a member's PartyMemberKind, so
// counting roster entries conflates real hunters with companions and
// stale slots. HunterPie reads the same two map symbols for the same
// purpose and returns early with a solo party when the value is 0.
//
// Map symbols (data/MonsterHunterWorld.421810.map):
//   Address SESSION_OFFSET        0x051C46B8
//   Offset SESSION_PARTY_OFFSETS  0x258,0x10,0x6574
// ---------------------------------------------------------------------------
std::optional<int> MhwReader::readSessionPlayerCount(QString *error)
{
    const std::uintptr_t addr = followPointerChain(
        memory_,
        absolute(QStringLiteral("SESSION_OFFSET")),
        map_.offsets(QStringLiteral("SESSION_PARTY_OFFSETS")),
        error);
    if (!addr)
        return std::nullopt;
    const auto count = memory_.read<std::int32_t>(addr);
    if (!count || !std::isfinite(static_cast<double>(*count)))
        return std::nullopt;
    // A session holds at most 4 hunters. Anything outside [0,4] is a
    // misaligned or mid-teardown read, so the value is untrustworthy — report
    // it as "no reading" rather than silently substituting a solo count.
    // NOTE: 0 is a MEANINGFUL value here (HunterPie's solo signal), which is
    // exactly why this returns nullopt for a failed read instead of 0: 0 and
    // "could not read" must never share a return code.
    if (*count < 0 || *count > 4)
        return std::nullopt;
    return static_cast<int>(*count);
}

QVector<PartyMemberSnapshot> MhwReader::readParty(QString *error)
{
    QVector<PartyMemberSnapshot> result;
    const std::uintptr_t party = followPointerChain(
        memory_,
        absolute(QStringLiteral("PARTY_ADDRESS")),
        map_.offsets(QStringLiteral("PARTY_OFFSETS")),
        error);
    const std::uintptr_t damage = followPointerChain(
        memory_,
        absolute(QStringLiteral("DAMAGE_ADDRESS")),
        map_.offsets(QStringLiteral("DAMAGE_OFFSETS")),
        nullptr);
    if (!party)
        return result;

    constexpr std::size_t stride = 0x58;
    const QString localName = [this]() -> QString {
        PlayerSnapshot p;
        refreshPlayerIdentity(p);               // uses save-header path
        return p.name;
    }();

    for (int index = 0; index < 4; ++index) {
        const auto memberAddress = memory_.read<std::uintptr_t>(
            party + static_cast<std::uintptr_t>(index) * stride);
        if (!memberAddress || !isSanePointer(*memberAddress))
            continue;
        const QString name = readUtf8(*memberAddress + 0x49, 32);
        if (name.isEmpty())
            continue;

        PartyMemberSnapshot member;
        member.name  = name;
        member.slot  = index;
        member.local = !localName.isEmpty() && (name == localName);
        if (const auto rank = memory_.read<std::int16_t>(
                *memberAddress + 0x70ULL + 0x2ULL))
            member.masterRank = *rank;
        if (const auto weapon = memory_.read<std::uint8_t>(
                *memberAddress + 0x7CULL))
            member.weaponId = *weapon;
        if (damage) {
            if (const auto dealt = memory_.read<std::int32_t>(
                    damage + static_cast<std::uintptr_t>(index) * 0x2A0ULL))
                member.damage = *dealt;
        }
        result.push_back(member);
    }
    return result;
}


// ===================================================================
// readSharpness — HunterPie MHWMeleeWeapon.GetWeaponSharpness
//
// Reads the local player's weapon sharpness. Returns a zero-initialised
// snapshot when:
//   - the equipped weapon is ranged (bow / hbg / lbg), which have no
//     sharpness bar
//   - the memory read fails (game not running, address not mapped)
//   - the in-game sharpness level is Broken or Invalid
//
// Mem path (all pre-resolved in data/MonsterHunterWorld.421810.map):
//   WEAPON_ADDRESS + WEAPON_SHARPNESS_OFFSETS
//     +0x1D10 int MaxLevel            (Purple = 6, max possible)
//     +0x20F8 int Sharpness           (raw hit counter; the weapon's
//                                      whole-bar remainder — the badge
//                                      subtracts the current level's
//                                      threshold to show "current colour
//                                      remaining", 0 = colour used up)
//     +0x20FC int Level               (enum Red=0..Purple=6, Broken=-1;
//                                      this IS the render colour — the
//                                      game's own current-level field.
//                                      r23 tried deriving the colour from
//                                      the counter instead and a live
//                                      purple weapon rendered blue; that
//                                      experiment is reverted — the field
//                                      keeps the colour, the counter feeds
//                                      the segmented number only)
//   WEAPON_ADDRESS + WEAPON_ID_OFFSETS
//     int weaponType                  (caller-side WeaponType byte, -1 ok)
//   WEAPON_DATA_ADDRESS + WEAPON_DATA_OFFSETS
//     then [weaponId * 8 + 0xC] deref → short[7]  per-level upper bounds
//   MINIMUM_SHARPNESSES_ADDRESS + 0..7 * 4 int  minimum hits per level
//
// Thresholds are cached per-weapon; the per-tick cost after the first
// read is just one 8-byte struct read + 1 int read.
// ===================================================================
SharpnessSnapshot MhwReader::readSharpness(int weaponId, QString *error)
{
    SharpnessSnapshot result;
    // HunterPie MHWMeleeWeapon.GetWeaponSharpness:
    //   1. always read the sharpness struct first
    //   2. only return early if the in-game Level field is invalid
    //   3. the caller's weapon TYPE byte only gates ranged weapons; the
    //      threshold array is indexed by the weapon-data ROW id read from
    //      WEAPON_ID_OFFSETS (hundreds of table rows are legal).
    //
    // We previously early-returned on weaponId<0 — that produced an
    // empty bar even when the game WAS feeding valid sharpness data
    // because the parallel weaponId read sometimes lags by a tick.
    // Removing the early return lets the panel render as soon as the
    // sharpness struct is valid in memory.
    const std::uintptr_t sharpPtr = followPointerChain(
        memory_,
        absolute(QStringLiteral("WEAPON_ADDRESS")),
        map_.offsets(QStringLiteral("WEAPON_SHARPNESS_OFFSETS")),
        error);
    if (!sharpPtr) {
        // Reset the cache so we don't stay stuck on a stale weaponId.
        cachedSharpnessThresholdsValid_ = false;
        return result;
    }

    const auto level = memory_.read<std::int32_t>(sharpPtr + 0x20FCULL);
    if (!level) {
        return result;
    }
    if (*level < 0 || *level > 6) {
        return result;
    }
    // r23 regression note: this field IS the render colour (the game's
    // own current-level value); live sessions kept it correct. An r23
    // experiment that DERIVED the level from the raw counter "mis-fired
    // purple -> blue" — root cause found later the same night: the
    // thresholds it compared against came from the WRONG table row (type
    // byte instead of row id, see below). Derivation removed; with the
    // correct row its scan agrees with this field at every sample.
    result.level = static_cast<int>(*level);

    if (const auto raw = memory_.read<std::int32_t>(sharpPtr + 0x20F8ULL))
        result.currentHits = *raw;

    // Ranged weapons have no sharpness (weapon-type bytes 11+: Bow / HBG /
    // LBG in the game's ordering); >10 also rejects garbage type reads.
    // -1 = "not resolved yet" falls through (the level gate above already
    // protects that case).
    if (weaponId > 10) {
        return result;
    }

    // HunterPie indexes the per-weapon thresholds array with the value read
    // from WEAPON_ID_OFFSETS: the weapon's ROW in the weapon-data table
    // (~hundreds), not the 0..13 type enum. r23 live forensics: this reader
    // indexed with the type byte (1) and therefore read a different row
    // ([140,250,290,330,380,400,0] vs the weapon's real
    // [140,160,180,250,290,320,400]) — raw=370 then derived "blue" for a
    // purple weapon and the panel badge collapsed to 0 (th[6] == 0 in the
    // wrong row). The row id (202 in that session) is validated against
    // the live raw/level transitions.
    const std::uintptr_t idPtr = followPointerChain(
        memory_,
        absolute(QStringLiteral("WEAPON_ADDRESS")),
        map_.offsets(QStringLiteral("WEAPON_ID_OFFSETS")),
        nullptr);
    int sharpWeaponId = -1;
    if (idPtr) {
        if (const auto w = memory_.read<std::int32_t>(idPtr))
            sharpWeaponId = *w;
    }
    // Generous sanity bound for a table row id; garbage -> no thresholds.
    if (sharpWeaponId < 0 || sharpWeaponId > 8191) {
        return result;
    }

    if (cachedSharpnessWeaponId_ != sharpWeaponId || !cachedSharpnessThresholdsValid_) {
        const std::uintptr_t dataPtr = followPointerChain(
            memory_,
            absolute(QStringLiteral("WEAPON_DATA_ADDRESS")),
            map_.offsets(QStringLiteral("WEAPON_DATA_OFFSETS")),
            nullptr);
        if (!dataPtr) {
            return result;
        }

        // HunterPie reads the per-weapon thresholds array via a
        // 2-level pointer chain starting from weaponDataPtr:
        //   Memory.ReadAsync<NUInt>(weaponDataPtr, {weaponId*8, 0xC})
        // We previously read it as a flat 8-byte value at
        // dataPtr + weaponId*8 + 0xC, which always returned 0 because
        // the chain is two dereferences deep, not one.
        const std::uintptr_t arrayPtr = followPointerChain(
            memory_,
            dataPtr,
            {static_cast<std::uintptr_t>(sharpWeaponId) * 8ULL, 0xCULL},
            nullptr);
        if (!arrayPtr) {
            return result;
        }
        if (!isSanePointer(arrayPtr)) {
            return result;
        }
        const auto shorts = memory_.readArray<std::int16_t>(arrayPtr, 7);
        if (shorts.size() != 7) {
            return result;
        }

        for (int i = 0; i < 7; ++i)
            cachedSharpnessThresholds_[i] = static_cast<int>(shorts[i]);
        cachedSharpnessWeaponId_ = sharpWeaponId;
        cachedSharpnessThresholdsValid_ = true;
    }
    for (int i = 0; i < 7; ++i)
        result.thresholds[i] = cachedSharpnessThresholds_[i];

    // Segmented badge ("current colour remaining", Rise semantics): the
    // raw counter is the weapon's whole-bar remainder, so the current
    // colour's remaining hits are `currentHits - thresholds[level - 1]`
    // with the level field's colour (cf4d741). 0 = this colour is used
    // up, the next hit drops a colour. Live validation of these numbers
    // rides the r23 watch probe (logs raw / level / thresholds / this
    // subtraction every second).
    result.threshold = (result.level <= 0) ? 0 : result.thresholds[result.level - 1];

    // S1 (v0.7.5 audit): HunterPie MHWGameUtils.MaximumSharpness, exact.
    // The old code hardcoded `base + 50` capped at 400, which overstated
    // the ceiling for weapons that don't reach purple and ignored that
    // the handicraft bonus only applies at the weapon's FINAL level.
    //
    // HunterPie formula (MHWMeleeWeapon.GetWeaponSharpness →
    // MHWGameUtils.MaximumSharpness):
    //   actualMax = min(thresholds[level], minimumSharpnesses[MaxLevel])
    //   isLastLevel = minimumSharpnesses[MaxLevel] < thresholds[level]
    //   maxHits = actualMax + (isLastLevel ? 10 * min(handicraft, 5) : 0)
    //
    // MaxLevel lives in the same weapon struct at +0x1D10; the minimum
    // table is static game memory (HunterPie caches it with `??=` — we
    // do the same via cachedMinimumSharpnesses_).
    const auto maxLevel = memory_.read<std::int32_t>(sharpPtr + 0x1D10ULL);

    if (!cachedMinimumSharpnessesValid_) {
        // MINIMUM_SHARPNESSES_ADDRESS is a flat table — no pointer chain
        // (HunterPie: Memory.ReadAsync<int>(absolute, count: 8)).
        const auto mins = memory_.readArray<std::int32_t>(
            absolute(QStringLiteral("MINIMUM_SHARPNESSES_ADDRESS")), 8);
        if (mins.size() == 8) {
            for (int i = 0; i < 8; ++i)
                cachedMinimumSharpnesses_[i] = mins[i];
            cachedMinimumSharpnessesValid_ = true;
        }
    }

    const bool haveMaxData = maxLevel.has_value()
                          && *maxLevel >= 0 && *maxLevel < 8
                          && cachedMinimumSharpnessesValid_;
    const int upperBound = result.thresholds[result.level];
    const int actualMax  = haveMaxData
        ? std::min(upperBound, cachedMinimumSharpnesses_[*maxLevel])
        : upperBound;
    result.maxHits = actualMax;

    // Handicraft (skill id 54): only on the weapon's final level. HunterPie
    // reads the gear-skill array via ABNORMALITY_ADDRESS + GEAR_SKILL_OFFSETS;
    // each entry is a 24-byte MHWGearSkill (Pack=1) with LevelGear at +8.
    // One pointer chain + one byte read per tick — cheap, and equipment
    // changes don't always coincide with weapon swaps, so no caching here.
    const bool isLastLevel = haveMaxData
                          && cachedMinimumSharpnesses_[*maxLevel] < upperBound;
    if (isLastLevel) {
        int handicraftLevel = 0;
        const std::uintptr_t gearSkillsPtr = followPointerChain(
            memory_,
            absolute(QStringLiteral("ABNORMALITY_ADDRESS")),
            map_.offsets(QStringLiteral("GEAR_SKILL_OFFSETS")),
            nullptr);
        if (gearSkillsPtr) {
            if (const auto lvl = memory_.read<std::uint8_t>(
                    gearSkillsPtr + 54ULL * 24ULL + 8ULL))
                handicraftLevel = *lvl;
        }
        result.maxHits += 10 * std::min(handicraftLevel, 5);
    }

    result.valid = true;
    return result;
}

// ===================================================================
// isMeleeWeapon — HunterPie Weapon enum: 0..10 are melee, 11..14 ranged.
// Inline so the panel can filter without dragging the reader into the
// UI translation unit.
// ===================================================================
inline bool isMeleeWeapon(int weaponId)
{
    return weaponId >= 0 && weaponId <= 10;
}

} // namespace mhw
