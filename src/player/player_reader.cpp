#include "mhw_reader.h"

namespace mhw {

PlayerSnapshot MhwReader::readPlayer(QString *error)
{
    PlayerSnapshot result;
    const std::uintptr_t hud = followPointerChain(memory_, absolute(QStringLiteral("EQUIPMENT_ADDRESS")),
                                                  map_.offsets(QStringLiteral("PLAYER_BASIC_INFORMATION_OFFSETS")), error);
    if (!hud)
        return result;
    const auto maxHealth = memory_.read<float>(hud + 0x60);
    const auto health = memory_.read<float>(hud + 0x64);
    const auto stamina = memory_.read<float>(hud + 0x12C);
    const auto maxStamina = memory_.read<float>(hud + 0x130);
    if (maxHealth && health && stamina && maxStamina) {
        result.maxHealth = *maxHealth;
        result.health = *health;
        result.stamina = *stamina;
        result.maxStamina = *maxStamina;
        result.valid = std::isfinite(result.health) && result.maxHealth > 0.0F && result.maxHealth < 10000.0F;
    }

    // HunterPie MHWAbnormalityStructure: 75-slot float array at
    // ABNORMALITY_BASE + 0x38, NOT at the player struct itself.
    // ABNORMALITY_BASE = EQUIPMENT_ADDRESS -> ABNORMALITY_OFFSETS.
    const std::uintptr_t abnormalityBase = followPointerChain(memory_,
                                                              absolute(QStringLiteral("EQUIPMENT_ADDRESS")),
                                                              map_.offsets(QStringLiteral("ABNORMALITY_OFFSETS")),
                                                              nullptr);
    if (abnormalityBase) {
        constexpr std::size_t kSlotCount = 75;
        const auto timers = memory_.readArray<float>(abnormalityBase + 0x38ULL, kSlotCount, nullptr);
        if (timers.size() == kSlotCount) {
            auto slot = [&](int abnormalityId) -> float {
                const int idx = (abnormalityId - 0x38) / 4;
                if (idx < 0 || idx >= static_cast<int>(kSlotCount)) return 0.0F;
                const float t = timers[idx];
                return std::isfinite(t) && t > 0.0F ? t : 0.0F;
            };
            // 衣装 timers (HunterPie AbnormalityData.xml IDs)
            result.mantleHealthTimer      = slot(0x44);   // 体力衣装
            result.mantleHealthLargeTimer = slot(0x48);   // 体力衣装(大)
            result.mantleStaminaTimer     = slot(0x4C);   // 耐力衣装
            result.mantleStaminaLargeTimer= slot(0x50);   // 耐力衣装(大)
            result.mantleToolTimer        = slot(0x64);   // 道具衣装
            result.mantleToolLargeTimer   = slot(0x68);   // 道具衣装(大)
            result.earplugTimer           = slot(0x88);   // 耳栓
        }
    }

    // HunterPie MHWPlayer GetMantlesData(): mantles are on EQUIPMENT_ADDRESS
    // + EQUIPMENT_OFFSETS -> +0x99C (40 cooldowns) / +0xA8C (40 timers).
    // Layout per SpecializedToolType Id (0..19):
    //   timers[id + 0]      = current timer (seconds remaining)
    //   timers[id + 20]     = max timer
    //   cooldowns[id + 0]   = current cooldown
    //   cooldowns[id + 20]  = max cooldown
    const std::uintptr_t equipmentBase = followPointerChain(memory_,
                                                             absolute(QStringLiteral("EQUIPMENT_ADDRESS")),
                                                             map_.offsets(QStringLiteral("EQUIPMENT_OFFSETS")),
                                                             nullptr);
    if (equipmentBase) {
        // HunterPie reads equipped mantle IDs at +0x34, but on Wine/Proton this
        // field appears to be unreliable (probe showed -1/0 even when a mantle
        // was actively equipped). Scan all 20 timers and cooldowns directly —
        // any non-zero timer/cooldown means that mantle is in that state.
        const auto timers = memory_.readArray<float>(equipmentBase + 0xA8CULL, 20, nullptr);
        const auto cooldowns = memory_.readArray<float>(equipmentBase + 0x99CULL, 20, nullptr);
        if (timers.size() == 20 && cooldowns.size() == 20) {
            int slot = 0;
            for (int id = 0; id < 20; ++id) {
                const float t = timers[id];
                const float cd = cooldowns[id];
                const bool active = std::isfinite(t) && t > 0.0F;
                const bool cooling = std::isfinite(cd) && cd > 0.0F;
                if (!active && !cooling) continue;
                if (slot >= 2) break;
                if (slot == 0) {
                    result.mantleSlot0Id = id;
                    result.mantleSlot0Timer = active ? t : 0.0F;
                    result.mantleSlot0Cooldown = cooling ? cd : 0.0F;
                } else {
                    result.mantleSlot1Id = id;
                    result.mantleSlot1Timer = active ? t : 0.0F;
                    result.mantleSlot1Cooldown = cooling ? cd : 0.0F;
                }
                ++slot;
            }
        }
    }

    // Debuffs (HunterPie AbnormalityData.xml <Debuffs> section).
    // Each is a float Timer at abnormalityBase + offset. Timer > 0 = active.
    // Blastscourge (0x63C) requires a precondition check at 0x62C == 1.
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
                const auto pre = memory_.read<std::int32_t>(abnormalityBase + static_cast<std::uintptr_t>(d.dependsOn));
                if (!pre || *pre != d.withValue)
                    continue;
            }
            const auto timer = memory_.read<float>(abnormalityBase + static_cast<std::uintptr_t>(d.offset));
            if (!timer || !std::isfinite(*timer) || *timer <= 0.0F)
                continue;
            PlayerAbnormality ab;
            ab.offset = d.offset;
            ab.name = QString::fromUtf8(d.name);
            ab.timer = *timer;
            result.debuffs.push_back(ab);
        }
    }

    return result;
}

QVector<PartyMemberSnapshot> MhwReader::readParty(QString *error)
{
    QVector<PartyMemberSnapshot> result;
    const std::uintptr_t party = followPointerChain(memory_, absolute(QStringLiteral("PARTY_ADDRESS")),
                                                    map_.offsets(QStringLiteral("PARTY_OFFSETS")), error);
    const std::uintptr_t damage = followPointerChain(memory_, absolute(QStringLiteral("DAMAGE_ADDRESS")),
                                                     map_.offsets(QStringLiteral("DAMAGE_OFFSETS")), nullptr);
    if (!party)
        return result;

    // MHWPartyMemberStructure is 0x58 bytes; its first field is the actual member address.
    constexpr std::size_t stride = 0x58;
    for (int index = 0; index < 4; ++index) {
        const auto memberAddress = memory_.read<std::uintptr_t>(party + static_cast<std::uintptr_t>(index) * stride);
        if (!memberAddress || !isSanePointer(*memberAddress))
            continue;
        const QString name = readUtf8(*memberAddress + 0x49, 32);
        if (name.isEmpty())
            continue;

        PartyMemberSnapshot member;
        member.name = name;
        if (const auto rank = memory_.read<std::int16_t>(*memberAddress + 0x70 + 0x2))
            member.masterRank = *rank;
        if (const auto weapon = memory_.read<std::uint8_t>(*memberAddress + 0x7C))
            member.weaponId = *weapon;
        if (damage) {
            if (const auto dealt = memory_.read<std::int32_t>(damage + static_cast<std::uintptr_t>(index) * 0x2A0))
                member.damage = *dealt;
        }
        result.push_back(member);
    }
    return result;
}


} // namespace mhw