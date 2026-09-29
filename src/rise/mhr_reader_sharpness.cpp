// SPDX-License-Identifier: Apache-2.0
// Core offsets and structures are derived from HunterPie/HunterPie (Apache-2.0).
//
// readSharpness — moved 1:1 out of `src/rise/mhr_reader.cpp` (old lines 51-59
// and 1429-1527) so the sharpness reader sits in its own translation unit.
// Both moved blocks are byte-for-byte copies of the original: the local
// `MHRSharpnessStructure` (with its static_assert) and the banner-commented
// `MhrReader::readSharpness()` body. The only edits this move forces are the
// file-level docs above, the include list below, and the `namespace mhw` /
// anonymous-namespace scaffolding that the old file supplied once for all of
// its readers.
//
// Why nothing else had to change — and in particular why `mhr_reader.h` is
// untouched: readSharpness is a member function, so it keeps access to
// MhrReader's private state (`map_`, `memory_`, and the BUG #5 threshold
// cache) from any translation unit. The three cache members therefore stay in
// `mhr_reader.h`'s private block, in place, which keeps the direct test's
// member access (tests/mhr_reader_direct_tests.cpp) compiling unchanged.
//
// Includes, and why nothing more is needed:
//   * "rise/mhr_reader.h"        — the `MhrReader` definition plus
//     `mhw::SharpnessSnapshot`, `mhw::isRiseMeleeWeaponId`,
//     `mhw::riseBuildSharpnessThresholds` and
//     `mhw::riseSharpnessCacheNeedsRefresh`. It also pulls in
//     "core/process_memory.h" (`ProcessMemory`, `AddressMap`,
//     `followPointerChain`, `isSanePointer`) and <QString>.
//   * <array> <cstdint> <vector> — the types the moved body names directly.

#include "rise/mhr_reader.h"

#include <array>
#include <cstdint>
#include <vector>

namespace mhw {

namespace {
// v0.8.4-r7 restore-reader: per-weapon sharpness struct layout
// (HunterPie MHRSharpnessStructure: int Level, int Hits, int MaxHits).
// Defined locally because no other translation unit needs it.
struct MHRSharpnessStructure {
    std::int32_t level;
    std::int32_t hits;
    std::int32_t maxHits;
};
static_assert(sizeof(MHRSharpnessStructure) == 12);
} // namespace

// ===================================================================
// readSharpness — HunterPie MHRMeleeWeapon.GetWeaponSharpness
//
// Reads the local player's weapon sharpness. Returns a zero-initialised
// snapshot (i.e. valid=false) when:
//   - the equipped weapon is ranged (Bow / HBG / LBG), which have no
//     sharpness bar in Rise
//   - the memory read fails (game not running, address not mapped)
//   - the in-game level field is Broken (-1) or Invalid (>6)
//
// Mem path (all pre-resolved in data/MonsterHunterRise.16.0.2.0.map):
//   SHARPNESS_ADDRESS + SHARPNESS_OFFSETS
//     MHRSharpnessStructure { int Level, int Hits, int MaxHits }
//   SHARPNESS_ADDRESS + SHARPNESS_ARRAY_OFFSETS
//     int[] thresholds        (7 per-level upper bounds, summed into
//                              cumulative thresholds per HunterPie's
//                              CalculateThresholds)
//
// The threshold array is cached by the Mono int[] object address. Two
// weapons of the same type can have distinct arrays, so weaponId alone
// is not a valid cache key.
// ===================================================================
SharpnessSnapshot MhrReader::readSharpness(int weaponId, QString *error)
{
    SharpnessSnapshot result;

    // HunterPie only runs MHRMeleeWeapon.GetWeaponSharpness() for a known
    // melee weapon.  Do this before any sharpness read so Weapon.None (-1)
    // cannot render data retained at SHARPNESS_ADDRESS.
    if (!isRiseMeleeWeaponId(weaponId))
        return result;

    // 1. Read the live sharpness state. Same as HunterPie: only a valid
    //    in-range level can produce a visible gauge.
    const std::uintptr_t sharpPtr = followPointerChain(
        memory_,
        absolute(QStringLiteral("SHARPNESS_ADDRESS")),
        map_.offsets(QStringLiteral("SHARPNESS_OFFSETS")),
        error);
    if (!sharpPtr) {
        cachedSharpnessThresholdsValid_ = false;
        cachedSharpnessArrayPtr_ = 0;
        return result;
    }

    const auto sharp = memory_.read<MHRSharpnessStructure>(sharpPtr);
    if (!sharp || sharp->level < 0 || sharp->level > 6)
        return result;
    result.level = sharp->level;
    result.currentHits = sharp->hits;
    result.maxHits = sharp->maxHits;

    // 3. The resolved address is the Mono int[] object header. Its length
    //    is at +0x1C and elements begin at +0x20 (MHRiseUtils.ReadArrayAsync).
    const std::uintptr_t arrayPtr = followPointerChain(
        memory_,
        absolute(QStringLiteral("SHARPNESS_ADDRESS")),
        map_.offsets(QStringLiteral("SHARPNESS_ARRAY_OFFSETS")),
        nullptr);
    if (!isSanePointer(arrayPtr)) {
        cachedSharpnessThresholdsValid_ = false;
        cachedSharpnessArrayPtr_ = 0;
        return result;
    }

    if (riseSharpnessCacheNeedsRefresh(cachedSharpnessArrayPtr_,
                                       cachedSharpnessThresholdsValid_, arrayPtr)) {
        const auto length = memory_.read<std::int32_t>(arrayPtr + 0x1CULL);
        if (!length || *length < 1 || *length > 7) {
            cachedSharpnessThresholdsValid_ = false;
            cachedSharpnessArrayPtr_ = 0;
            return result;
        }

        const auto raw = memory_.readArray<std::int32_t>(
            arrayPtr + 0x20ULL, static_cast<std::size_t>(*length));
        std::array<int, 7> thresholds{};
        if (raw.size() != static_cast<std::size_t>(*length)
            || !riseBuildSharpnessThresholds(raw, &thresholds)) {
            cachedSharpnessThresholdsValid_ = false;
            cachedSharpnessArrayPtr_ = 0;
            return result;
        }

        cachedSharpnessThresholds_ = thresholds;
        cachedSharpnessArrayPtr_ = arrayPtr;
        cachedSharpnessThresholdsValid_ = true;
    }

    for (int i = 0; i < 7; ++i)
        result.thresholds[i] = cachedSharpnessThresholds_[static_cast<std::size_t>(i)];

    // threshold = end of previous-level segment (or 0 at Red).
    result.threshold = (result.level <= 0)
        ? 0
        : result.thresholds[result.level - 1];
    result.valid = true;
    return result;
}
} // namespace mhw
