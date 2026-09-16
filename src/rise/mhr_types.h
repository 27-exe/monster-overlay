#pragma once

#include <cstddef>
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

// v0.8.4-r7 fix-hud-layout: byte-for-byte mirror of HunterPie
// HunterPie.Integrations/Datasources/MonsterHunterRise/Definitions/
// MHRPlayerHudStructure.cs ([StructLayout(LayoutKind.Sequential, Pack=1)]).
// Total = 0x34 bytes (52), 11 fields. The earlier v0.8 alignment (0x68
// bytes, health@0x10, sharpness@0x30) was synthesised in this project
// and diverged from upstream; see final-audit §5.1 (反证 #1). Read
// fields surface to PlayerSnapshot.health / maxHealth / stamina /
// maxStamina. RecoverableHealth / CurrentHealth / Heal are read by the
// HunterPie MHRPlayer (MHRPlayer.cs:754-758) but not currently
// consumed by our panel; they're kept in the struct so a future
// Petalace / heal display can pull them without re-deriving offsets.
// Drift here = mis-display of the HUD (wrong field at the wrong offset).
struct MHRPlayerHudStructure {
    float         health;             // 0x00  float Health
    float         recoverableHealth;  // 0x04  float RecoverableHealth
    float         maxHealth;          // 0x08  float MaxHealth
    float         currentHealth;      // 0x0C  float CurrentHealth (dup Health)
    float         maximumHealth;      // 0x10  float MaximumHealth (dup MaxHealth)
    std::int64_t  unk14;              // 0x14  long  Unk (8 bytes)
    float         heal;               // 0x1C  float Heal
    std::int64_t  unk20;              // 0x20  long  Unk1 (8 bytes)
    float         stamina;            // 0x28  float Stamina
    float         maxStamina;         // 0x2C  float MaxStamina
    float         maxExtendableStamina; // 0x30 float MaxExtendableStamina
};
static_assert(sizeof(MHRPlayerHudStructure) == 0x34);
static_assert(offsetof(MHRPlayerHudStructure, health)              == 0x00);
static_assert(offsetof(MHRPlayerHudStructure, recoverableHealth)   == 0x04);
static_assert(offsetof(MHRPlayerHudStructure, maxHealth)           == 0x08);
static_assert(offsetof(MHRPlayerHudStructure, currentHealth)       == 0x0C);
static_assert(offsetof(MHRPlayerHudStructure, maximumHealth)       == 0x10);
static_assert(offsetof(MHRPlayerHudStructure, unk14)               == 0x14);
static_assert(offsetof(MHRPlayerHudStructure, heal)                == 0x1C);
static_assert(offsetof(MHRPlayerHudStructure, unk20)               == 0x20);
static_assert(offsetof(MHRPlayerHudStructure, stamina)             == 0x28);
static_assert(offsetof(MHRPlayerHudStructure, maxStamina)          == 0x2C);
static_assert(offsetof(MHRPlayerHudStructure, maxExtendableStamina) == 0x30);

// Wirebugs use .NET's default sequential layout: unlike most Rise wire
// structures, HunterPie does not specify Pack=1. On x64 that makes this a
// 0x20-byte structure (cooldown@0x10, max@0x14, extra@0x18); the trailing
// four bytes are end padding. Keep it outside the Pack=1 region below.
#pragma pack(pop)
struct MHRWirebugStructure {
    std::int64_t ref;
    std::int32_t unk0;
    std::int32_t unk1;
    float        cooldown;
    float        maxCooldown;
    float        extraCooldown;
};
static_assert(sizeof(MHRWirebugStructure) == 0x20);
static_assert(offsetof(MHRWirebugStructure, cooldown) == 0x10);
static_assert(offsetof(MHRWirebugStructure, maxCooldown) == 0x14);
static_assert(offsetof(MHRWirebugStructure, extraCooldown) == 0x18);
#pragma pack(push, 1)

struct MHRWirebugCountStructure {
    std::int32_t default_;
    std::int32_t environment;
    std::int32_t skill;
};
static_assert(sizeof(MHRWirebugCountStructure) == 12);

// HunterPie declares only Timer, so it is the first and only field.
struct MHRWirebugExtrasStructure {
    float timer;
};
static_assert(sizeof(MHRWirebugExtrasStructure) == 4);
static_assert(offsetof(MHRWirebugExtrasStructure, timer) == 0x00);

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

// v0.8 alignment: Rise weapon memory enum ≠ HunterPie Core enum. The int
// at WEAPON_ADDRESS + 0x8C is an MHRiseUtils.WeaponType value
// (Rise-private ordering from MHRiseUtils.cs:3-20), but every downstream
// consumer (Icon::weaponPath()'s kDirs[], readSharpness()'s threshold
// table, isMeleeWeapon()'s 0..10 range) expects a HunterPie.Core Weapon
// enum value. The two orderings diverge on every melee weapon (mem
// 0..10); only the ranged tail (Bow=11 / HBG=12 / LBG=13) coincides.
// Feeding the raw mem int straight through produces wrong weapon icons
// (e.g. DualBlades mem id 4 → kDirs[4] == "Hammer") and feeds the wrong
// Sharpness threshold slot. Mirrors HunterPie's MHRiseUtils.ToWeaponId().
//
//   mem id  Rise WeaponType    -> Core Weapon enum (also kDirs[] index)
//   0       GreatSword          ->  0 Greatsword
//   1       SwitchAxe           ->  8 SwitchAxe
//   2       LongSword           ->  3 Longsword
//   3       LightBowgun         -> 13 LightBowgun
//   4       HeavyBowgun         -> 12 HeavyBowgun
//   5       Hammer              ->  4 Hammer
//   6       GunLance            ->  7 GunLance
//   7       Lance               ->  6 Lance
//   8       SwordAndShield      ->  1 SwordAndShield
//   9       DualBlades          ->  2 DualBlades
//   10      HuntingHorn         ->  5 HuntingHorn
//   11      ChargeBlade         ->  9 ChargeBlade
//   12      InsectGlaive        -> 10 InsectGlaive
//   13      Bow                 -> 11 Bow
//
// Out-of-range mem values (incl. None=0xFF) yield -1 so downstream
// isMeleeWeapon() / Icon::weaponPath() treat them as invalid.
int riseWeaponTypeToCore(int memoryIdx);

} // namespace mhw
