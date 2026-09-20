#pragma once

#include <cmath>
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

// v0.8.4-r19 monster-identity: crown / body-size data.
//
// HunterPie MHRMonster.GetMonsterCrown (MHRMonster.cs:501-517) resolves the
// object with ReadPtrAsync(monster, MONSTER_CROWN_OFFSETS) — a single 0x308
// hop — and then reads an 8-byte MHRSizeStructure at *that object + 0x24*:
//
//     nint monsterSizePtr = await Memory.ReadPtrAsync(_address, [0x308]);
//     MHRSizeStructure s  = await Memory.ReadAsync<MHRSizeStructure>(monsterSizePtr + 0x24);
//     float ratio         = s.SizeMultiplier * s.UnkMultiplier;
//
// The 0x24 is a *struct-relative* field offset, so it lives here next to the
// struct definition rather than in the .map file, whose MONSTER_CROWN_OFFSETS
// entry deliberately carries only the object hop (0x308) exactly like
// HunterPie's map does.
constexpr std::uintptr_t kMhrSizeStructureOffset = 0x24ULL;

struct MHRSizeStructure {
    float sizeMultiplier;   // [0x00] float SizeMultiplier
    float unkMultiplier;    // [0x04] float UnkMultiplier
};
static_assert(sizeof(MHRSizeStructure) == 8);
static_assert(offsetof(MHRSizeStructure, sizeMultiplier) == 0x00);
static_assert(offsetof(MHRSizeStructure, unkMultiplier) == 0x04);

// Plausibility band for the crown ratio. HunterPie's own Rise metadata
// (Game/Rise/Data/MonsterData.xml, 33 <Crowns> blocks) uses Mini/Silver/Gold
// thresholds spanning 0.765 .. 1.17, and the in-game size tables sit around
// 0.9 .. 1.25, so anything outside [0.5, 1.5] is a read artefact rather than a
// monster size. The band exists so a mis-read can never be mistaken for data.
constexpr float kRiseMinPlausibleSize = 0.5F;
constexpr float kRiseMaxPlausibleSize = 1.5F;

// Crown ratio for a Rise monster (HunterPie MHRMonster.cs:508) with an honest
// failure mode: 0.0F means "not read / not plausible", never a fabricated
// value. 1.0F is a *legitimate* result (a monster rolled at 100 %), which is
// precisely why MonsterSnapshot::size no longer defaults to 1.0F — the old
// default made "read failed" indistinguishable from "size is exactly 100 %",
// and the panel therefore showed a dead `1.00x` for every monster
// (v0.8.4-r19 monster-identity: the reported symptom).
//
// The value returned here is the same number HunterPie compares against
// <Crowns> (src/monster/part_schemas.cpp kRiseCrownThresholds) and that the
// independent MHR-Overlay project prints as `100 * size` percent.
inline float riseMonsterSizeFromFactors(float sizeMultiplier, float unkMultiplier)
{
    const float product = sizeMultiplier * unkMultiplier;
    if (!std::isfinite(product) || product <= 0.0F)
        return 0.0F;
    if (product < kRiseMinPlausibleSize || product > kRiseMaxPlausibleSize)
        return 0.0F;
    return product;
}

// v0.8.4-r23 fatigue-semantics: monster stamina ("fatigue" meter).
//
// HunterPie MHRMonster.GetMonsterStamina (MHRMonster.cs) resolves
// ReadPtrAsync(monster, MONSTER_STAMINA_OFFSETS) — a single 0x320 hop — and
// reads MHRStaminaStructure; the pair at +0x20/+0x24 is what the monster
// widget's stamina gauge draws. Stamina depleted => the monster drools and
// slows down: THIS — not the exhaust ailment (slot 6, a separate 減気
// gauge) — is the value the player perceives as "fatigue".
struct MHRStaminaStructure {
    std::int64_t reference;   // 0x00  long  Reference
    std::int32_t unk0;        // 0x08  int   Unk0
    std::int32_t unk1;        // 0x0C  int   Unk1
    std::int64_t unk2;        // 0x10  long  Unk2
    std::int32_t unk3;        // 0x18  int   Unk3 (maybe proc counter?)
    std::int32_t unk4;        // 0x1C  int   Unk4
    float        stamina;     // 0x20  float Stamina
    float        maxStamina;  // 0x24  float MaxStamina
};
static_assert(sizeof(MHRStaminaStructure) == 40);
static_assert(offsetof(MHRStaminaStructure, stamina) == 0x20);
static_assert(offsetof(MHRStaminaStructure, maxStamina) == 0x24);

// Plausibility bound for the stamina pair. Live Rise values observed at
// 1000-2000 (r23 probe session); anything above 1e5 (or negative /
// non-finite) is a read artefact. In contrast to size, stamina == 0 is REAL
// data — exactly what an exhausted monster shows — so zero is accepted; a
// stamina slightly above max (the two floats come from a live simulation
// read a moment apart) is clamped, not rejected. Returns false without
// touching the outputs on an unusable read; the snapshot then keeps its 0/0
// defaults and the panel hides the row (same honest-zero convention as
// riseMonsterSizeFromFactors).
constexpr float kRiseMaxPlausibleStamina = 1.0e5F;

inline bool riseMonsterStaminaFromPair(float stamina, float maxStamina,
                                       float *outStamina, float *outMaxStamina)
{
    if (!std::isfinite(stamina) || !std::isfinite(maxStamina))
        return false;
    if (maxStamina <= 0.0F || maxStamina > kRiseMaxPlausibleStamina)
        return false;
    if (stamina < 0.0F || stamina > maxStamina * 1.25F)
        return false;
    *outStamina = stamina <= maxStamina ? stamina : maxStamina;
    *outMaxStamina = maxStamina;
    return true;
}

struct MHRPlayerLevelStructure {
    std::int32_t highRank;
    std::int32_t masterRank;
};
static_assert(sizeof(MHRPlayerLevelStructure) == 8);

// HunterPie MHRCharacterData uses explicit layout. Session/SOS arrays contain
// pointers to these records; NamePointer references a managed UTF-16 string.
struct MHRCharacterData {
    std::uint8_t reserved00[0x18];
    std::uintptr_t namePointer;       // 0x18
    std::uint8_t reserved20[0x18];
    std::int32_t highRank;            // 0x38
    std::uint8_t reserved3C[0x54];
    std::int32_t masterRank;          // 0x90
};
static_assert(sizeof(MHRCharacterData) == 0x94);
static_assert(offsetof(MHRCharacterData, namePointer) == 0x18);
static_assert(offsetof(MHRCharacterData, highRank) == 0x38);
static_assert(offsetof(MHRCharacterData, masterRank) == 0x90);

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

// v0.8.4-r18 player-abnormalities: the consumable/debuff blob.
//
// MHRAbnormalityStructure is declared [StructLayout(LayoutKind.Explicit)] with
// an int Timer *and* an MHRConsumableStructure / MHRDebuffStructure at
// FieldOffset(0); both of those carry a single float Timer, so the value slot
// is 4 bytes and the schema table decides how to read it (IsInteger -> int,
// see MHRPlayer.cs:476-477 via MHRAbnormalityAdapter.Convert).
union MHRAbnormalityValue {
    std::int32_t integer;  // MHRAbnormalityStructure.Timer
    float        timer;    // MHRConsumable/MHRDebuffStructure.Timer
};
static_assert(sizeof(MHRAbnormalityValue) == 4);

// The two ulong condition words MHRPlayer.GetPlayerConditions reads from
// LOCAL_PLAYER_DATA_ADDRESS + PLAYER_CONDITION_OFFSETS (MHRPlayer.cs:872-885),
// and the uint action-flag word read from LOCAL_PLAYER_DATA_ADDRESS +
// PLAYER_ACTIONFLAG_OFFSETS (:897-908). Those *offsets* are struct-relative
// fields of the resolved condition object, so they live here next to the
// other Rise field constants rather than in the .map, which carries only the
// object hop (exactly like HunterPie's address map).
constexpr std::uintptr_t kMhrCommonConditionsOffset = 0x10;
constexpr std::uintptr_t kMhrDebuffConditionsOffset = 0x38;
constexpr std::uintptr_t kMhrActionFlagOffset       = 0x20;

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
