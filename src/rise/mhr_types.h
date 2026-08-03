#pragma once

#include <cstdint>

namespace mhw {

// Rise-specific memory structures. All fields are laid out exactly as they
// appear in the game process, so every struct is byte-packed.
//
// Offsets/structures derived from HunterPie (Apache-2.0) for the
// Monster Hunter Rise 16.0.2.0 address map.

#pragma pack(push, 1)

struct MHRStageStructure {
    std::int32_t type;
    std::int32_t villageId;
    std::int32_t unk1;
    std::int32_t section;
    std::int32_t unk2;
    std::int32_t huntingId;
};
static_assert(sizeof(MHRStageStructure) == 24);

struct MHREnrageStructure {
    std::int64_t reference;
    std::int32_t unk0;
    std::int32_t unk1;
    std::int64_t unk2;
    std::int32_t unk3;
    float buildup;
    float maxBuildup;
    float unk4;
    float timer;
    float maxTimer;
    float unk5;
    std::int32_t counter;
};
static_assert(sizeof(MHREnrageStructure) == 56);

struct MHRSizeStructure {
    float sizeMultiplier;
    float unkMultiplier;
};
static_assert(sizeof(MHRSizeStructure) == 8);

struct MHRPlayerLevelStructure {
    std::int32_t highRank;
    std::int32_t masterRank;
};
static_assert(sizeof(MHRPlayerLevelStructure) == 8);

struct MHRQurioThresholdStructure {
    float maxThreshold;
    float threshold;
};
static_assert(sizeof(MHRQurioThresholdStructure) == 8);

// v0.7.1: wirebug (翔虫) state — HunterPie 16.0.2.0 map. Each wirebug
// entry tracks its own cooldown (recovery timer after use), max cooldown
// (skill-scaled ceiling), and extra cooldown (wirebug-specific bonus).
// The game keeps three wirebug objects (default + environment + skill);
// the count comes from ABNORMALITIES_ADDRESS + WIREBUG_COUNT_OFFSETS and
// the actual pointers come from WIREBUG_DATA_OFFSETS.
struct MHRWirebugStructure {
    std::int64_t ref;
    std::int32_t unk0;
    std::int32_t unk1;
    float        cooldown;
    float        maxCooldown;
    float        extraCooldown;
};
static_assert(sizeof(MHRWirebugStructure) == 28);

struct MHRWirebugCountStructure {
    std::int32_t default_;
    std::int32_t environment;
    std::int32_t skill;
};
static_assert(sizeof(MHRWirebugCountStructure) == 12);

// Extra fields for environment / skill wirebugs. Only the recovery timer
// is consumed by the panel right now; the rest are reserved.
struct MHRWirebugExtrasStructure {
    float unk0;
    float unk1;
    float unk2;
    float timer;
};
static_assert(sizeof(MHRWirebugExtrasStructure) == 16);

#pragma pack(pop)

// Ailment fields are sparse, so they are read with explicit offsets rather
// than a packed struct. Offsets are relative to each ailment pointer.
namespace mhr_ailment {
constexpr std::uintptr_t kCounterPtr = 0x18;
constexpr std::uintptr_t kMaxTimer = 0x44;
constexpr std::uintptr_t kTimer = 0x48;
constexpr std::uintptr_t kBuildUpPtr = 0x68;
constexpr std::uintptr_t kMaxBuildUpPtr = 0x78;
} // namespace mhr_ailment

} // namespace mhw
