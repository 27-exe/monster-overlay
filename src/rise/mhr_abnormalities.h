// SPDX-License-Identifier: Apache-2.0
#pragma once

// Rise player abnormalities — consumable buffs + debuffs.
//
// Port of HunterPie's MHRise local-player abnormality pipeline:
//   data    HunterPie/Game/Rise/Data/AbnormalityData.xml, categories
//           "Consumables" and "Debuffs" (AbnormalityRepository
//           .FindAllAbnormalitiesBy(GameType.Rise, category)) — the two lists
//           MHRPlayer.GetConsumableAbnormalities (MHRPlayer.cs:436-494) and
//           GetPlayerDebuffAbnormalities (MHRPlayer.cs:497-554) iterate.
//           Generated table: mhr_abnormalities.cpp.
//   reader  MHRPlayer.cs:395-405 (cleanup), :862-886 (conditions),
//           MHRiseUtils.ToAbnormalitySeconds() = raw / 60.
//   names   zh-cn.xml <Abnormalities>/Abnormality[@Id = schema.Name].
//
// Hunting-horn songs are deliberately NOT ported: they carry
// Category="Songs" (so neither reader list contains them) and HunterPie
// reads them from HH_ABNORMALITIES_OFFSETS through a different struct.

#include <QString>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace mhw {

// HunterPie.Core.Game.Enums.AbnormalityFlagType (Rise subset). Each non-None
// value maps to a condition word through MHRAbnormalityFlagTypeParser.
enum class RiseAbnormalityFlagType {
    None = 0,     // condition is always considered satisfied
    RiseCommon,   // CommonConditions  (ulong, conditionPtr + 0x10)
    RiseDebuff,   // DebuffConditions  (ulong, conditionPtr + 0x38)
    RiseAction,   // ActionFlags       (uint,  actionFlagPtr + 0x20)
};

// Presentation grouping: which upstream category the entry comes from. The
// memory path is identical for both; the kind only decides buff/debuff
// colouring and ordering in the player panel.
enum class RiseAbnormalityKind {
    Buff = 0,   // Category "Consumables"
    Debuff = 1, // Category "Debuffs"
};

// One row of the generated table. Field semantics mirror
// XmlNodeToAbnormalityDefinitionMapper.Map():
//   offset / dependsOn  hex attributes, 0 when absent
//   withValue           decimal attribute, 0 when absent
//   flag / flagType     parsed from FlagType + Flag; flag == 0 means either
//                       "no flag" or "unknown member" — .NET's
//                       Enum.HasFlag(0) is true either way, and
//                       riseAbnormalityFlagSatisfied() reproduces that.
//   maxTimer            MaxTimer attribute, 0 = none. When set, the raw field
//                       counts *elapsed* time and the remaining time is
//                       maxTimer - raw/60 (MHRPlayer.cs:482-483).
//   maxBuildup          MaxBuildup attribute of IsBuildup entries.
struct RiseAbnormalitySchema {
    const char *id;       // schema.Id, e.g. "Consumables_ABN_DEMONDRUG"
    const char *nameKey;  // schema.Name — zh-cn localization key
    const char *name;     // zh-cn display name (never nullptr/empty)
    const char *group;    // <Consumables>|<Skills>|<Foods>|<Debuffs>
    const char *flagName; // Flag enum member ("" when FlagType is None)
    RiseAbnormalityFlagType flagType;
    RiseAbnormalityKind kind;
    std::uint64_t flag;    // bit value of flagName
    std::int32_t offset;   // struct-relative, inside the category blob
    std::int32_t dependsOn;// struct-relative sub-id value
    std::int32_t withValue;// required value at dependsOn
    std::int32_t maxBuildup;
    float maxTimer;
    bool isInfinite;
    bool isInteger;
    bool isBuildup;
};

// Generated tables (69 consumables + 28 debuffs, XML document order — the
// order HunterPie's repository iterates them).
const RiseAbnormalitySchema *riseConsumableAbnormalities(std::size_t &count);
const RiseAbnormalitySchema *riseDebuffAbnormalities(std::size_t &count);

// Linear lookup across both tables by schema.Id; nullptr when absent.
const RiseAbnormalitySchema *riseFindAbnormality(const char *id);

// ---------------------------------------------------------------------------
// Pure evaluation core — no memory access, offline testable.
// ---------------------------------------------------------------------------

// The condition words read once per poll from the local player struct.
struct RiseAbnormalityConditions {
    std::uint64_t common{0};   // CommonConditions @ conditionPtr + 0x10
    std::uint64_t debuff{0};   // DebuffConditions @ conditionPtr + 0x38
    std::uint32_t action{0};   // ActionFlags      @ actionFlagPtr + 0x20
};

// MHRiseUtils.TIMER_MULTIPLIER: the game stores these timers in sixtieths of
// a second.
constexpr float kRiseAbnormalityTimerMultiplier = 60.0F;

inline float riseAbnormalitySeconds(float rawTimer)
{
    return rawTimer / kRiseAbnormalityTimerMultiplier;
}

// The flag gate of MHRPlayer.cs:460-466 / :520-526:
//   RiseCommon -> _commonCondition.HasFlag(flag)
//   RiseDebuff -> _debuffCondition.HasFlag(flag)
//   RiseAction -> _actionFlag.HasFlag(flag)
//   anything else (including None) -> true
// Enum.HasFlag(0) is always true in .NET; (value & 0) == 0 reproduces it.
inline bool riseAbnormalityFlagSatisfied(const RiseAbnormalitySchema &schema,
                                         const RiseAbnormalityConditions &conditions)
{
    switch (schema.flagType) {
    case RiseAbnormalityFlagType::RiseCommon:
        return (conditions.common & schema.flag) == schema.flag;
    case RiseAbnormalityFlagType::RiseDebuff:
        return (conditions.debuff & schema.flag) == schema.flag;
    case RiseAbnormalityFlagType::RiseAction: {
        const auto flag = static_cast<std::uint32_t>(schema.flag);
        return (conditions.action & flag) == flag;
    }
    case RiseAbnormalityFlagType::None:
    default:
        return true;
    }
}

// Outcome of one schema evaluation.
struct RiseAbnormalityEvaluation {
    bool active{false};
    float timer{0.0F};  // seconds left; buildup entries carry the raw counter
};

// Pure port of the upstream per-schema body (MHRPlayer.cs:452-492, mirrored
// at :512-552 for debuffs).
//   subId      int read at blobBase + dependsOn (0 when dependsOn == 0)
//   rawValue   value read at blobBase + offset — the float interpretation,
//              or the int reinterpreted as float when schema.isInteger
//   valueRead  false when that offset read failed. Upstream's failed read
//              yields default(0), which is indistinguishable from "inactive"
//              for a timer, so a failed read is reported as inactive.
inline RiseAbnormalityEvaluation riseEvaluateAbnormality(
    const RiseAbnormalitySchema &schema,
    const RiseAbnormalityConditions &conditions,
    int subId,
    float rawValue,
    bool valueRead)
{
    RiseAbnormalityEvaluation result;

    if (!riseAbnormalityFlagSatisfied(schema, conditions))
        return result;
    if (subId != schema.withValue)
        return result;

    if (schema.isInfinite) {
        // Upstream assigns 1 so the entry is displayed without a timer.
        result.active = true;
        result.timer = 1.0F;
        return result;
    }

    if (!valueRead)
        return result;

    float timer = rawValue;
    if (!schema.isInteger && !schema.isBuildup)
        timer = riseAbnormalitySeconds(timer);
    if (schema.maxTimer > 0.0F)
        timer = std::max(0.0F, schema.maxTimer - timer);

    result.timer = timer;
    result.active = timer > 0.0F;
    return result;
}

// Display text for one active abnormality: "∞" for IsInfinite, "43/120" for
// buildup counters, "123s" otherwise. The player panel renders this verbatim;
// it lives here so the offline tests can pin the format.
QString riseAbnormalityTimerText(float timer, bool infinite, bool buildup,
                                 float maxBuildup);

} // namespace mhw
