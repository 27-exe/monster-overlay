#include "mhw_reader.h"

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
    if (abnormalityBase) {
        constexpr std::size_t kSlotCount = 75;
        const auto timers = memory_.readArray<float>(
            abnormalityBase + 0x38ULL, kSlotCount, nullptr);
        if (timers.size() == kSlotCount) {
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

} // namespace mhw
