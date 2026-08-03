#include "mhw_reader.h"

#include <algorithm>


namespace mhw {

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
        }
    }

    // ---- equipment mantles (EQUIPMENT_ADDRESS -> EQUIPMENT_OFFSETS) ----
    const std::uintptr_t equipmentBase = followPointerChain(
        memory_,
        absolute(QStringLiteral("EQUIPMENT_ADDRESS")),
        map_.offsets(QStringLiteral("EQUIPMENT_OFFSETS")),
        nullptr);
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
        }
    }

    // ---- debuffs (abnormalityBase + offset) ----
    if (abnormalityBase) {
        struct DebuffDef { int offset; const char *name; int dependsOn; int withValue; };
        static const DebuffDef kDebuffs[] = {
            {0x5DC, "毒",       0, 0},
            {0x5E0, "猛毒",     0, 0},
            {0x5EC, "火异常",   0, 0},
            {0x5F0, "雷异常",   0, 0},
            {0x5F4, "水异常",   0, 0},
            {0x5F8, "冰异常",   0, 0},
            {0x5FC, "龙异常",   0, 0},
            {0x600, "裂伤",     0, 0},
            {0x608, "瘴气",     0, 0},
            {0x60C, "防御↓",   0, 0},
            {0x614, "耐性↓",   0, 0},
            {0x620, "爆破",     0, 0},
            {0x63C, "爆破灾祸", 0x62C, 1},
        };
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
            ab.name    = QString::fromUtf8(d.name);
            ab.timer   = *timer;
            result.debuffs.push_back(ab);
        }

        // ---- buffs: Songs (hunting horn, from float array) ----
        // HunterPie GetHuntingHornAbnormalities: index = (Id - 0x38) / 4
        struct SongDef { int id; const char *name; };
        static const SongDef kSongs[] = {
            {0x38, "自我强化"}, {0x3C, "攻击强化"}, {0x40, "攻击强化大"},
            {0x44, "体力强化"}, {0x48, "体力强化大"},
            {0x4C, "耐力消耗↓"}, {0x50, "耐力消耗↓大"},
            {0x54, "风压无效"}, {0x58, "风压完全无效"},
            {0x5C, "防御强化"}, {0x60, "防御强化大"},
            {0x64, "道具消耗↓"}, {0x68, "道具消耗↓大"},
            {0x80, "体力回复"}, {0x84, "体力回复大"},
            {0x88, "耳栓"}, {0x8C, "耳栓+"},
            {0x90, "精灵加护"}, {0x94, "导虫强化"},
            {0x98, "环境无害"}, {0x9C, "气绝无效"},
            {0xA0, "麻痹无效"}, {0xA4, "震动无效"},
            {0xA8, "深渊抵抗"},
            {0xAC, "火耐性"}, {0xB0, "火耐性大"},
            {0xB4, "水耐性"}, {0xB8, "水耐性大"},
            {0xBC, "雷耐性"}, {0xC0, "雷耐性大"},
            {0xC4, "冰耐性"}, {0xC8, "冰耐性大"},
            {0xCC, "龙耐性"}, {0xD0, "龙耐性大"},
            {0xD4, "属性攻击↑"}, {0xD8, "全异常无效"},
            {0xE4, "击退无效"}, {0xEC, "全耐性↑"},
            {0xF0, "会心强化"}, {0xF4, "全状态异常无效"},
            {0xFC, "异常攻击↑"},
            {0x10C, "最大耐力回复"}, {0x110, "体力回复量↑"},
            {0x114, "速度·回避↑"}, {0x118, "全属性强化"},
        };
        // Reuse the 75-float array already read above for mantles.
        if (timersValid) {
            for (const auto &s : kSongs) {
                const int idx = (s.id - 0x38) / 4;
                if (idx < 0 || idx >= static_cast<int>(kSlotCount)) continue;
                const float t = timers[idx];
                if (!std::isfinite(t) || t <= 0.0F) continue;
                PlayerAbnormality ab;
                ab.offset  = s.id;
                ab.name    = QString::fromUtf8(s.name);
                ab.timer   = t;
                result.buffs.push_back(ab);
            }
        }

        // ---- buffs: Consumables + Skills (direct offset read) ----
        struct BuffDef { int offset; const char *name; int dependsOn; int withValue; };
        static const BuffDef kBuffs[] = {
            // Consumables
            {0x690, "急奔饮料",   0, 0},
            {0x694, "活力剂",     0, 0},
            {0x698, "星辰肉干",   0, 0},
            {0x6A0, "怪力种子",   0x6A4, 10},
            {0x6A0, "怪力药丸",   0x6A4, 25},
            {0x6B0, "忍耐种子",   0x6B4, 20},
            {0x6B0, "忍耐药丸",   0x6BC, 1},
            {0x6C4, "鬼人粉尘",   0, 0},
            {0x6C8, "硬化粉尘",   0, 0},
            {0x6CC, "鬼人药",     0x6D4, 1},
            {0x6CC, "大鬼人药",   0x6D4, 2},
            {0x6D0, "硬化药",     0x6D8, 1},
            {0x6D0, "大硬化药",   0x6D8, 2},
            {0x6EC, "冷饮",       0, 0},
            {0x6F0, "热饮",       0, 0},
            {0x6F8, "体力回复",   0, 0},
            {0x6FC, "耐寒强化",   0, 0},
            {0x718, "力量松果",   0, 0},
            {0x71C, "耐热强化",   0, 0},
            // Skills
            {0x764, "不屈",       0, 0},
            {0x76C, "刚刃研磨",   0, 0},
            {0x770, "滑走强化",   0, 0},
            {0x730, "属性加速",   0, 0},
            {0x738, "力量解放",   0, 0},
            {0x754, "肾上腺素",   0, 0},
            {0x788, "冰气炼成",   0, 0},
            {0x79C, "攻击守势",   0, 0},
            {0x7A0, "转福",       0, 0},
        };
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
            ab.name    = QString::fromUtf8(b.name);
            ab.timer   = *timer;
            result.buffs.push_back(ab);
        }
    }

    return result;
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
//     +0x20F8 int Sharpness           (raw hit count, current segment)
//     +0x20FC int Level               (enum Red=0..Purple=6, Broken=-1)
//   WEAPON_ADDRESS + WEAPON_ID_OFFSETS
//     int weaponId                    (HunterPie 0..13, <-1 on failure)
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
    //   3. weaponId is only used for the (cached) per-weapon threshold
    //      array, not for gating the read.
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
    result.level = *level;

    if (const auto raw = memory_.read<std::int32_t>(sharpPtr + 0x20F8ULL))
        result.currentHits = *raw;

    // Always re-fetch thresholds when the weaponId changes. The caller
    // may pass -1 on first tick before the player struct's weapon byte
    // resolves; fall back to WEAPON_ID_OFFSETS as a second source.
    int sharpWeaponId = weaponId;
    if (sharpWeaponId < 0 || sharpWeaponId > 13) {
        const std::uintptr_t idPtr = followPointerChain(
            memory_,
            absolute(QStringLiteral("WEAPON_ADDRESS")),
            map_.offsets(QStringLiteral("WEAPON_ID_OFFSETS")),
            nullptr);
        if (idPtr) {
            if (const auto w = memory_.read<std::int32_t>(idPtr))
                sharpWeaponId = *w;
        }
    }
    // Ranged weapons (Bow=11, HBG=13, LBG=14) have no sharpness.
    if (sharpWeaponId == 11 || sharpWeaponId == 13 || sharpWeaponId == 14) {
        return result;
    }
    if (sharpWeaponId < 0 || sharpWeaponId > 10) {
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
