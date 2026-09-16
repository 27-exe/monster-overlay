// SPDX-License-Identifier: Apache-2.0

#include "mhw_reader.h"
#include "monster/target_selector.h"
#include "quest/quest_types.h"
#include "rise/mhr_reader.h"
#include "rise/mhr_types.h"

#include <QCoreApplication>
#include <QTemporaryFile>

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

// Compile the production's standalone label helper without pulling the Qt
// widgets panel into this Core-only reader test target. The helper is the exact
// source used by PlayerPanel in normal builds.
#define MHW_WIREBUG_SLOT_LABEL_TEST
#include "../src/ui/panel_player.cpp"
#undef MHW_WIREBUG_SLOT_LABEL_TEST

namespace mhw {
// Exposed here for the schema sanity test below.
extern const QHash<int, QVector<PartSchema>> kPartSchemas;
}

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition) {
        std::cout << "PASS: " << message << '\n';
    } else {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryFile mapFile;
    check(mapFile.open(), "temporary map opens");
    const QByteArray map = R"MAP(
# fixture
Address ROOT 0x1234
Offset CHAIN 0x10,0x20,0x0
Address OTHER 0xCAFE # inline comment
)MAP";
    mapFile.write(map);
    mapFile.flush();

    mhw::AddressMap parsed;
    QString error;
    check(parsed.load(mapFile.fileName(), &error), "legacy map parses");
    check(parsed.address(QStringLiteral("ROOT")) == 0x1234, "address value parsed");
    check(parsed.address(QStringLiteral("OTHER")) == 0xCAFE, "inline comment ignored");
    check(parsed.offsets(QStringLiteral("CHAIN")).size() == 3, "offset chain length parsed");
    check(parsed.offsets(QStringLiteral("CHAIN"))[1] == 0x20, "offset chain value parsed");

    mhw::ProcessMemory ownMemory;
    check(ownMemory.attach(QCoreApplication::applicationPid(), &error), "reader opens own /proc/pid/mem");
    const std::uint64_t marker = 0x1122334455667788ULL;
    const auto observed = ownMemory.read<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&marker), &error);
    check(observed.has_value(), "process_vm_readv reads own process");
    check(observed && *observed == marker, "read value is exact");

    const std::uintptr_t markerAddress = reinterpret_cast<std::uintptr_t>(&marker);
    const std::uintptr_t levelTwo = markerAddress;
    const std::uintptr_t levelOne = reinterpret_cast<std::uintptr_t>(&levelTwo);
    const auto chained = mhw::MhwReader::followPointerChain(
        ownMemory,
        reinterpret_cast<std::uintptr_t>(&levelOne),
        {0, 0},
        &error);
    check(chained == markerAddress, "HunterPie pointer-chain semantics match");
    const auto chainedValue = ownMemory.read<std::uint64_t>(chained, &error);
    check(chainedValue && *chainedValue == marker, "pointer chain resolves readable target");

    // HunterPie ReadPtrAsync dereferences at address + offset, unlike
    // ReadAsync which dereferences first and then adds the offset. The Rise
    // health-component map starts with this 0x308 multi-hop chain.
    std::array<std::uintptr_t, 1024> ptrOffsetFixture{};
    const auto fixtureBase = reinterpret_cast<std::uintptr_t>(ptrOffsetFixture.data());
    const auto node1 = fixtureBase + 0x400ULL;
    const auto node2 = fixtureBase + 0x500ULL;
    const auto node3 = fixtureBase + 0x600ULL;
    const auto node4 = fixtureBase + 0x700ULL;
    const auto node5 = fixtureBase + 0x800ULL;
    const auto final = fixtureBase + 0x900ULL;
    const auto writePointer = [](std::uintptr_t address, std::uintptr_t value) {
        *reinterpret_cast<std::uintptr_t *>(address) = value;
    };
    const std::vector<std::uintptr_t> readPtrOffsets{0x308ULL, 0x48ULL, 0x10ULL,
                                                      0x20ULL, 0x10ULL, 0x20ULL};
    writePointer(fixtureBase, 0); // ReadAsync must not accidentally pass.
    writePointer(fixtureBase + 0x308ULL, node1);
    writePointer(node1 + 0x48ULL, node2);
    writePointer(node2 + 0x10ULL, node3);
    writePointer(node3 + 0x20ULL, node4);
    writePointer(node4 + 0x10ULL, node5);
    writePointer(node5 + 0x20ULL, final);
    const auto readPtrChain = mhw::MhwReader::followPointerChainOffsetThenDeref(
        ownMemory, fixtureBase, readPtrOffsets, &error);
    const auto readAsyncChain = mhw::MhwReader::followPointerChain(
        ownMemory, fixtureBase, readPtrOffsets, &error);
    check(readPtrChain == final,
          "ReadPtr chain resolves the Rise 0x308 health-component fixture");
    check(readAsyncChain != final,
          "ReadAsync semantics remain distinct from Rise ReadPtr chains");
    writePointer(fixtureBase + 0x308ULL, 1);
    check(mhw::MhwReader::followPointerChainOffsetThenDeref(
              ownMemory, fixtureBase, {0x308ULL}, &error) == 0,
          "ReadPtr chain rejects an invalid dereferenced pointer");
    check(mhw::MhwReader::followPointerChainOffsetThenDeref(
              ownMemory, std::numeric_limits<std::uintptr_t>::max() - 0x10ULL,
              {0x20ULL}, &error) == 0,
          "ReadPtr chain rejects offset-address overflow before reading memory");

    check(ownMemory.imageBase(nullptr) == 0, "non-MHW fixture has no MHW image base");

    // Schema must include severable parts. Without them, multi-player quests
    // show 100% HP on severable parts (tails, horns, charges) because the
    // normal table is only populated on the host's client.
    check(mhw::kPartSchemas.size() == 72, "schema covers 72 monsters");
    int severableTotal = 0;
    for (auto it = mhw::kPartSchemas.cbegin(); it != mhw::kPartSchemas.cend(); ++it) {
        for (const auto &p : it.value()) {
            if (p.isSeverable) ++severableTotal;
        }
    }
    check(severableTotal == 120, "schema contains 120 severable parts (HunterPie MonsterData.xml)");
    // Tigrex (Id=94) has charge horn + breakable tail as severable.
    const auto tigrex = mhw::kPartSchemas.value(94);
    check(tigrex.size() >= 2, "Tigrex (94) schema has entries");
    int tigrexSev = 0;
    for (const auto &p : tigrex) if (p.isSeverable) ++tigrexSev;
    check(tigrexSev == 2, "Tigrex has 2 severable parts (PART_CHARGE + PART_TAIL)");

    QVector<mhw::MonsterSnapshot> targets(3);
    targets[0].address = 0x1000; targets[0].health = 100; targets[0].maxHealth = 100;
    targets[1].address = 0x2000; targets[1].health = 200; targets[1].maxHealth = 1000;
    targets[2].address = 0x3000; targets[2].health = 300; targets[2].maxHealth = 300; targets[2].enraged = true;
    targets[0].isLockOnTarget = true;
    check(mhw::selectMonsterTarget(targets, 0x3000) == 0,
          "HunterPie LockOn target wins over current/enraged/max-health monsters");
    targets[0].isLockOnTarget = false;
    check(mhw::selectMonsterTarget(targets, 0x3000) == 2,
          "no LockOn target keeps current live monster stable");
    targets[2].health = 0;
    check(mhw::selectMonsterTarget(targets, 0x3000) == 0,
          "dead current monster falls back to first live monster");

    // HunterPie quest timer semantics:
    //   elapsed  = max(0, questMaxTimer - timeLeft)
    //   maxTimer = ApproximateHigh(questMaxTimerRaw,
    //                {54000, 72000, 108000, 126000, 180000}) / 60
    //   timeLeft = literallyWhyCapcom(ticks) / 60
    // Pure helper inputs are the post-division values (seconds).
    check(mhw::questElapsedSeconds(1500.0F, 1431.0F) == 69.0F,
          "elapsed = 69s for a 25min hunt with 23:51 remaining");
    check(mhw::questElapsedSeconds(1200.0F, 0.0F) == 1200.0F,
          "elapsed = full 20-minute investigation on completion");
    check(mhw::questElapsedSeconds(1200.0F, 999999.0F) == 0.0F,
          "elapsed clamped to 0 when remaining > max (post-quest state)");
    // questMaxTimerSeconds takes the raw uint32 read from memory and
    // returns the snapped value already in seconds (/60).
    check(mhw::questMaxTimerSeconds(125000) == 2100.0F,
          "125000 raw snaps to 126000 step → 2100s (35min hunt)");
    check(mhw::questMaxTimerSeconds(55000) == 1200.0F,
          "55000 raw snaps to 72000 step → 1200s (20min investigation)");
    check(mhw::questMaxTimerSeconds(50000) == 900.0F,
          "50000 raw snaps to 54000 step → 900s (15min hunt)");
    check(mhw::questMaxTimerSeconds(181000) == 181000.0F / 60.0F,
          "raw above highest step falls back to raw / 60");

    // Rise sharpness arrays are Mono int[] objects. The reader validates the
    // declared length before consuming payload values and fills missing colours
    // with zero while retaining HunterPie's cumulative-threshold semantics.
    std::array<int, 7> sharpnessThresholds{};
    check(!mhw::riseBuildSharpnessThresholds({}, &sharpnessThresholds),
          "Rise sharpness array length zero is rejected");
    const std::vector<std::int32_t> fullSharpness{5, 10, 15, 20, 25, 30, 35};
    check(mhw::riseBuildSharpnessThresholds(fullSharpness, &sharpnessThresholds),
          "Rise sharpness array length seven is accepted");
    check(sharpnessThresholds == std::array<int, 7>{5, 15, 30, 50, 75, 105, 140},
          "Rise sharpness length-seven payload becomes cumulative thresholds");
    const std::vector<std::int32_t> shortSharpness{5, 10, 15};
    check(mhw::riseBuildSharpnessThresholds(shortSharpness, &sharpnessThresholds)
              && sharpnessThresholds == std::array<int, 7>{5, 15, 30, 0, 0, 0, 0},
          "Rise sharpness absent colours are zero-filled");
    check(!mhw::riseBuildSharpnessThresholds({5, -1}, &sharpnessThresholds),
          "negative Rise sharpness segment is rejected");
    check(!mhw::riseBuildSharpnessThresholds({10001}, &sharpnessThresholds),
          "implausible Rise sharpness segment is rejected");

    // Same weapon type must still refresh when the backing Mono array changes.
    check(mhw::riseSharpnessCacheNeedsRefresh(0x1000, true, 0x2000),
          "same weapon type with another sharpness array refreshes cache");
    check(!mhw::riseSharpnessCacheNeedsRefresh(0x1000, true, 0x1000),
          "unchanged sharpness array keeps cache");
    check(mhw::riseSharpnessCacheNeedsRefresh(0x1000, false, 0x1000),
          "invalid sharpness cache refreshes even for same array");

    // The HUD must subtract the preceding threshold for every non-red active
    // segment. 75 total hits in green starting at 50 means 25 hits remain.
    mhw::SharpnessSnapshot greenSharpness;
    greenSharpness.level = 3;
    greenSharpness.threshold = 50;
    greenSharpness.thresholds[3] = 75;
    greenSharpness.currentHits = 74;
    const auto greenSegment = mhw::riseSharpnessCurrentSegment(greenSharpness);
    check(greenSegment.valid && greenSegment.total == 25 && greenSegment.remaining == 24,
          "non-red sharpness badge/fill use threshold-subtracted remaining hits");
    mhw::SharpnessSnapshot redSharpness;
    redSharpness.level = 0;
    redSharpness.thresholds[0] = 20;
    redSharpness.currentHits = 7;
    const auto redSegment = mhw::riseSharpnessCurrentSegment(redSharpness);
    check(redSegment.valid && redSegment.total == 20 && redSegment.remaining == 7,
          "red sharpness keeps a zero threshold");
    greenSharpness.thresholds[3] = 50;
    check(!mhw::riseSharpnessCurrentSegment(greenSharpness).valid,
          "zero-width current sharpness segment is rejected");
    greenSharpness.thresholds[3] = 75;
    greenSharpness.currentHits = -1;
    check(mhw::riseSharpnessCurrentSegment(greenSharpness).remaining == 0,
          "negative current sharpness clamps to an empty segment");

    // Rise quest semantics mirror HunterPie MHRQuestDataStructure:
    // normal Stars is zero-based in memory while anomaly Level is an
    // explicit, independently tagged value.
    check(mhw::riseNormalQuestStars(0) == 1,
          "Rise normal raw zero stars displays as one star");
    check(mhw::riseNormalQuestStars(6) == 7,
          "Rise normal stars are incremented exactly once");
    check(mhw::riseNormalQuestStars(-1) == 0,
          "negative Rise normal stars do not produce a display value");
    check(mhw::riseNormalQuestStars(std::numeric_limits<int>::max())
              == std::numeric_limits<int>::max(),
          "Rise normal stars do not overflow at INT_MAX");

    mhw::QuestSnapshot normalQuest;
    normalQuest.stars = mhw::riseNormalQuestStars(100);
    check(!normalQuest.isAnomaly && normalQuest.stars == 101,
          "a three-digit normal star value remains a normal quest");
    mhw::QuestSnapshot anomalyQuest;
    anomalyQuest.isAnomaly = true;
    anomalyQuest.stars = 1;
    check(anomalyQuest.isAnomaly && anomalyQuest.stars == 1,
          "anomaly level is explicitly marked even at level one");

    check(mhw::isRiseQuestActive(1, mhw::kRiseQuestStateInQuest, 1),
          "Rise Normal quest is active only when it has an id and is in quest state");
    check(!mhw::isRiseQuestActive(1, 1, 1),
          "accepted Rise quest is not active before state two");
    check(!mhw::isRiseQuestActive(1, 3, 1),
          "completed Rise quest is not active after state two");
    check(!mhw::isRiseQuestActive(0, mhw::kRiseQuestStateInQuest, 1),
          "Rise state two without a quest id is not active");
    check(mhw::isRiseQuestTypeSupported(1)
              && mhw::isRiseQuestTypeSupported(2)
              && mhw::isRiseQuestTypeSupported(4)
              && mhw::isRiseQuestTypeSupported(8)
              && mhw::isRiseQuestTypeSupported(16)
              && mhw::isRiseQuestTypeSupported(64)
              && mhw::isRiseQuestTypeSupported(128),
          "Rise quest types supported by HunterPie ToQuestType remain active candidates");
    check(!mhw::isRiseQuestTypeSupported(0)
              && !mhw::isRiseQuestTypeSupported(32)
              && !mhw::isRiseQuestTypeSupported(256)
              && !mhw::isRiseQuestTypeSupported(512)
              && !mhw::isRiseQuestTypeSupported(1024)
              && !mhw::isRiseQuestTypeSupported(3),
          "unsupported and combined Rise quest types remain inactive");
    check(!mhw::isRiseQuestActive(1, mhw::kRiseQuestStateInQuest, 512),
          "Rise training quest type does not appear as an active supported quest");

    check(mhw::isRiseSaveSlotNameMatch(0x1000, std::uintptr_t{0x1000}),
          "Rise save slot accepts the exact current character name pointer");
    check(!mhw::isRiseSaveSlotNameMatch(0x1000, std::nullopt),
          "Rise save slot rejects an unreadable name pointer");
    check(!mhw::isRiseSaveSlotNameMatch(0x1000, std::uintptr_t{0}),
          "Rise save slot rejects a readable zero name pointer");
    check(!mhw::isRiseSaveSlotNameMatch(0, std::uintptr_t{0x1000}),
          "Rise save slot rejects an unreadable current character name pointer");
    check(!mhw::isRiseSaveSlotNameMatch(0x1000, std::uintptr_t{0x2000}),
          "Rise save slot rejects another character name pointer");

    const mhw::MHRStageStructure trainingRoom{4, 5, 0, 0, 0, 0};
    const mhw::MHRStageStructure village{4, 4, 0, 0, 0, 0};
    const mhw::MHRStageStructure hunting{5, 0, 0, 0, 0, 0};
    check(mhw::isRiseTrainingRoom(trainingRoom)
              && mhw::isRiseHuntingZone(trainingRoom),
          "Rise training room is the VillageId 5 hunting-zone exception");
    check(!mhw::isRiseTrainingRoom(village)
              && !mhw::isRiseHuntingZone(village),
          "other Rise villages are not hunting zones");
    check(mhw::isRiseHuntingZone(hunting),
          "normal Rise hunting stage remains a hunting zone");
    check(mhw::isHuntingZone(mhw::Zone::RiseTrainingRoom),
          "rebased Rise training room reaches the overlay hunting-zone gate");

    check(mhw::isRiseMeleeWeaponId(0) && mhw::isRiseMeleeWeaponId(10),
          "known Rise core melee weapons may read sharpness");
    check(!mhw::isRiseMeleeWeaponId(-1)
              && !mhw::isRiseMeleeWeaponId(11)
              && !mhw::isRiseMeleeWeaponId(12)
              && !mhw::isRiseMeleeWeaponId(13)
              && !mhw::isRiseMeleeWeaponId(255),
          "invalid and ranged Rise weapon ids cannot read sharpness");

    check(mhw::isRisePlayerValid(QStringLiteral("Hunter"), 0.0F, 0.0F,
                                 0.0F, 0.0F),
          "named Rise player remains valid while HUD data is unavailable");
    check(mhw::isRisePlayerValid({}, 100.0F, 150.0F, 75.0F, 150.0F),
          "finite plausible Rise HUD vitals validate an unnamed player");
    check(!mhw::isRisePlayerValid({}, std::numeric_limits<float>::quiet_NaN(),
                                  150.0F, 75.0F, 150.0F),
          "non-finite Rise health does not validate an unnamed player");
    check(!mhw::isRisePlayerValid({}, 100.0F, 1001.0F, 75.0F, 150.0F),
          "Rise health maximum above the safety bound is invalid");
    check(!mhw::isRisePlayerValid({}, 100.0F, 150.0F, 75.0F, 10001.0F),
          "Rise stamina maximum above the safety bound is invalid");

    // Rise lock-on reads the selected slot in two pointer stages. The helper
    // only derives the first slot address; keeping the actual dereferences in
    // the reader means this test stays offline and does not mock /proc.
    check(mhw::riseLockOnSlotEntryAddress(0x1000, 3)
              == std::optional<std::uintptr_t>{0x1000 + 3 * sizeof(std::uintptr_t)},
          "Rise lock-on selects camera-style slot before entry dereference");
    check(!mhw::riseLockOnSlotEntryAddress(0x1000, -1),
          "negative Rise lock-on type is rejected");
    check(!mhw::riseLockOnSlotEntryAddress(
              std::numeric_limits<std::uintptr_t>::max() - 3, 1),
          "overflowing Rise lock-on slot address is rejected");

    // Monster-list pointer chains resolve the Mono array header. Header count
    // lives at +0x1C; pointer elements begin at +0x20 and map probing must
    // never consider more than Rise's five monster slots.
    check(mhw::riseMonsterListCount(std::optional<std::int32_t>{0}) == 0,
          "Rise monster-list length zero produces no slots");
    check(mhw::riseMonsterListCount(std::optional<std::int32_t>{mhw::kRiseMonsterListMax + 3})
              == mhw::kRiseMonsterListMax,
          "Rise monster-list length above cap clamps to five slots");
    check(mhw::riseMonsterListElementAddress(0x1000, 0)
              == std::optional<std::uintptr_t>{0x1020},
          "Rise first monster pointer follows the Mono header");
    check(mhw::riseMonsterListElementAddress(0x1000, mhw::kRiseMonsterListMax - 1)
              == std::optional<std::uintptr_t>{0x1020
                  + static_cast<std::uintptr_t>(mhw::kRiseMonsterListMax - 1)
                      * sizeof(std::uintptr_t)},
          "Rise final capped monster pointer uses pointer-size stride");
    check(!mhw::riseMonsterListElementAddress(0x1000, mhw::kRiseMonsterListMax),
          "Rise monster-list element past cap is rejected");
    check(!mhw::riseMonsterListElementAddress(
              std::numeric_limits<std::uintptr_t>::max() - 0x10, 0),
          "overflowing Rise monster-list header address is rejected");

    // A readable ID of zero is valid in the Rise monster list. Map probing
    // accepts only finite health values with max > 0 and current HP >= 0.
    check(mhw::hasRiseMonsterId(std::optional<std::int32_t>{0}),
          "Rise monster id zero is accepted");
    check(mhw::hasRiseMonsterId(std::optional<std::int32_t>{42}),
          "nonzero Rise monster id is accepted");
    check(!mhw::hasRiseMonsterId(std::optional<std::int32_t>{}),
          "missing Rise monster id is rejected");
    check(mhw::isRiseMapProbeMonster(std::optional<std::int32_t>{0},
                                     std::optional<float>{100.0F},
                                     std::optional<float>{0.0F}),
          "Rise map probe accepts id zero with a sane fainted monster");
    check(!mhw::isRiseMapProbeMonster(std::optional<std::int32_t>{7},
                                      std::optional<float>{0.0F},
                                      std::optional<float>{0.0F}),
          "Rise map probe rejects a nonpositive maximum health");
    check(!mhw::isRiseMapProbeMonster(std::optional<std::int32_t>{7},
                                      std::optional<float>{100.0F},
                                      std::optional<float>{-1.0F}),
          "Rise map probe rejects a negative current health");
    check(!mhw::isRiseMapProbeMonster(std::optional<std::int32_t>{7},
                                      std::optional<float>{100.0F},
                                      std::optional<float>{}),
          "Rise map probe rejects a list with no valid monster");

    // HunterPie dispatches severable before breakable, then flinch. This also
    // chooses the primary health pair used for the broken-state projection.
    check(mhw::risePartType(true, true) == mhw::PartType::Severable,
          "Rise severable part takes precedence over breakable");
    check(mhw::risePartType(false, true) == mhw::PartType::Breakable,
          "Rise breakable part takes precedence over flinch");
    check(mhw::risePartType(false, false) == mhw::PartType::Flinch,
          "Rise flinch is the fallback part type");

    // v0.8 alignment: HunterPie v2 MHRiseUtils.cs:25-44 ToWeaponId table.
    // The Rise memory byte at WEAPON_ADDRESS + 0x8C is a WeaponType enum
    // (Rise-internal order), NOT a Core Weapon enum. The reader must
    // translate via riseWeaponTypeToCore() before downstream consumers
    // (Icon::weaponPath() in src/ui/icon.cpp:142-160) can index kDirs[]
    // correctly. Drift here = wrong weapon icon shown in the player
    // panel — silent to the user until they swap weapons.
    //
    // Memory idx → expected Core enum value:
    //   0   GreatSword        →  0
    //   1   SwitchAxe         →  8
    //   2   LongSword         →  3
    //   3   LightBowgun       → 13   (← easy to miss; rounded-down SnS trap)
    //   4   HeavyBowgun       → 12   (← easy to miss; rounded-down DB trap)
    //   5   Hammer            →  4
    //   6   GunLance          →  7
    //   7   Lance             →  6
    //   8   SwordAndShield    →  1
    //   9   DualBlades        →  2
    //  10   HuntingHorn       →  5
    //  11   ChargeBlade       →  9
    //  12   InsectGlaive      → 10
    //  13   Bow               → 11
    struct WeaponCase { int memoryIdx; int coreId; const char *name; };
    static const WeaponCase kWeaponTable[] = {
        { 0,  0, "GreatSword"     },
        { 1,  8, "SwitchAxe"      },
        { 2,  3, "LongSword"      },
        { 3, 13, "LightBowgun"    },
        { 4, 12, "HeavyBowgun"    },
        { 5,  4, "Hammer"         },
        { 6,  7, "GunLance"       },
        { 7,  6, "Lance"          },
        { 8,  1, "SwordAndShield" },
        { 9,  2, "DualBlades"     },
        {10,  5, "HuntingHorn"    },
        {11,  9, "ChargeBlade"    },
        {12, 10, "InsectGlaive"   },
        {13, 11, "Bow"            },
    };
    for (const auto &c : kWeaponTable) {
        const int got = mhw::riseWeaponTypeToCore(c.memoryIdx);
        check(got == c.coreId,
              (std::string("weapon translation memoryIdx=") + std::to_string(c.memoryIdx)
               + " (" + c.name + ") -> Core enum "
               + std::to_string(got) + " (expected " + std::to_string(c.coreId) + ")")
                  .c_str());
    }
    // Out-of-range memory values must fall through to -1 so Icon::weaponPath
    // returns "" and the panel hides the weapon slot (mirrors HunterPie's
    // Weapon.None = 0xFF behavior).
    check(mhw::riseWeaponTypeToCore(-1)  == -1, "weapon translation out-of-range (-1) -> -1");
    check(mhw::riseWeaponTypeToCore(14)  == -1, "weapon translation out-of-range (14) -> -1");
    check(mhw::riseWeaponTypeToCore(999) == -1, "weapon translation out-of-range (999) -> -1");

    // v0.8.4-r7 fix-hud-layout: MHRPlayerHudStructure (HunterPie
    // MHRPlayerHudStructure.cs) layout. The reader feeds this struct
    // straight to PlayerSnapshot.health / maxHealth / stamina /
    // maxStamina — a wrong field order or missing field would mis-display
    // the HUD. C++ re-derives the byte offsets at compile time via
    // static_assert; any drift from the C# struct (Pack=1, 0x34 bytes
    // total, 11 fields) breaks the static_assert. We re-check the field
    // offsets here so a future edit that changes the C++ layout without
    // updating the test gets flagged.
    //
    // Earlier v0.8 alignment (health@0x10, maxHealth@0x14, stamina@0x18,
    // maxStamina@0x1C, sharpness@0x30, sizeof=0x68) was synthesised in
    // this project and diverged from upstream — see final-audit §5.1
    // (反证 #1): tests were green but the struct was reading the bottom
    // half of an engine pointer as health and a near-2^31 value as
    // maxHealth in village hubs. Mirrors HunterPie's sequential Pack=1
    // layout exactly: Health 0x00 → RecoverableHealth 0x04 → MaxHealth
    // 0x08 → CurrentHealth 0x0C → MaximumHealth 0x10 → Unk(long) 0x14 →
    // Heal 0x1C → Unk1(long) 0x20 → Stamina 0x28 → MaxStamina 0x2C →
    // MaxExtendableStamina 0x30.
    check(offsetof(mhw::MHRPlayerHudStructure, health)               == 0x00,
          "HUD health offset 0x00 (HunterPie Health first field)");
    check(offsetof(mhw::MHRPlayerHudStructure, recoverableHealth)    == 0x04,
          "HUD recoverableHealth offset 0x04");
    check(offsetof(mhw::MHRPlayerHudStructure, maxHealth)            == 0x08,
          "HUD maxHealth offset 0x08");
    check(offsetof(mhw::MHRPlayerHudStructure, currentHealth)        == 0x0C,
          "HUD currentHealth offset 0x0C");
    check(offsetof(mhw::MHRPlayerHudStructure, maximumHealth)        == 0x10,
          "HUD maximumHealth offset 0x10");
    check(offsetof(mhw::MHRPlayerHudStructure, unk14)                == 0x14,
          "HUD unk14 (long) offset 0x14");
    check(offsetof(mhw::MHRPlayerHudStructure, heal)                 == 0x1C,
          "HUD heal offset 0x1C");
    check(offsetof(mhw::MHRPlayerHudStructure, unk20)                == 0x20,
          "HUD unk20 (long) offset 0x20");
    check(offsetof(mhw::MHRPlayerHudStructure, stamina)              == 0x28,
          "HUD stamina offset 0x28");
    check(offsetof(mhw::MHRPlayerHudStructure, maxStamina)           == 0x2C,
          "HUD maxStamina offset 0x2C");
    check(offsetof(mhw::MHRPlayerHudStructure, maxExtendableStamina) == 0x30,
          "HUD maxExtendableStamina offset 0x30");
    check(sizeof(mhw::MHRPlayerHudStructure) == 0x34,
          "MHRPlayerHudStructure total size 0x34 bytes (HunterPie Pack=1, 11 fields)");

    check(sizeof(mhw::MHRWirebugStructure) == 0x20,
          "Rise wirebug uses default x64 sequential size 0x20, not Pack=1 size 0x1C");
    check(offsetof(mhw::MHRWirebugStructure, cooldown) == 0x10
              && offsetof(mhw::MHRWirebugStructure, maxCooldown) == 0x14
              && offsetof(mhw::MHRWirebugStructure, extraCooldown) == 0x18,
          "Rise wirebug cooldown fields retain HunterPie sequential offsets");
    check(sizeof(mhw::MHRWirebugExtrasStructure) == 4
              && offsetof(mhw::MHRWirebugExtrasStructure, timer) == 0x00,
          "Rise wirebug extras timer is the sole first field");

    // The reader compacts `None` entries from QVector but retains the original
    // Mono-array slot in WirebugSnapshot::slot. Panel labels must use that
    // source identity, not vector position, and corrupt values must remain
    // unnumbered.
    mhw::WirebugSnapshot wirebugSlot0;
    mhw::WirebugSnapshot wirebugSlot1;
    mhw::WirebugSnapshot wirebugSlot2;
    wirebugSlot0.slot = 0;
    wirebugSlot1.slot = 1;
    wirebugSlot2.slot = 2;
    check(wirebugSlotLabel(wirebugSlot0.slot) == QStringLiteral("翔虫")
              && wirebugSlotLabel(wirebugSlot1.slot) == QStringLiteral("翔虫·2")
              && wirebugSlotLabel(wirebugSlot2.slot) == QStringLiteral("翔虫·3"),
          "Rise wirebug labels preserve consecutive source slots zero through two");
    mhw::WirebugSnapshot wirebugAfterHole;
    wirebugAfterHole.slot = 3;
    check(wirebugSlotLabel(wirebugAfterHole.slot) == QStringLiteral("翔虫·4"),
          "Rise wirebug label preserves source slot three after a compacted hole");
    check(wirebugSlotLabel(-1) == QStringLiteral("翔虫")
              && wirebugSlotLabel(mhw::kRiseWirebugSlotCap) == QStringLiteral("翔虫"),
          "Rise wirebug corrupt slots use an unnumbered safe fallback");

    const mhw::MHRWirebugCountStructure defaultOnly{1, 0, 0};
    check(mhw::riseWirebugType(defaultOnly, 0) == mhw::RiseWirebugType::Default
              && mhw::riseWirebugType(defaultOnly, 1) == mhw::RiseWirebugType::None,
          "Rise wirebug default partition exposes only its declared slot");
    const mhw::MHRWirebugCountStructure envAndSkill{1, 1, 1};
    check(mhw::riseWirebugType(envAndSkill, 0) == mhw::RiseWirebugType::Default
              && mhw::riseWirebugType(envAndSkill, 1) == mhw::RiseWirebugType::Environment
              && mhw::riseWirebugType(envAndSkill, 2) == mhw::RiseWirebugType::Skill,
          "Rise wirebug default/environment/skill partitions use exact indexes");
    const mhw::MHRWirebugCountStructure envHole{1, 2, 1};
    check(mhw::riseWirebugType(envHole, 1) == mhw::RiseWirebugType::Environment
              && mhw::riseWirebugType(envHole, 2) == mhw::RiseWirebugType::None
              && mhw::riseWirebugType(envHole, 3) == mhw::RiseWirebugType::Skill,
          "Rise wirebug partition preserves the environment-count hole before skill");
    const mhw::MHRWirebugCountStructure skillOnly{1, 0, 1};
    check(mhw::riseWirebugType(skillOnly, 1) == mhw::RiseWirebugType::Skill,
          "Rise wirebug skill partition follows default when no environment exists");
    const mhw::MHRWirebugCountStructure fourSlots{2, 1, 1};
    check(mhw::riseWirebugType(fourSlots, 0) == mhw::RiseWirebugType::Default
              && mhw::riseWirebugType(fourSlots, 1) == mhw::RiseWirebugType::Default
              && mhw::riseWirebugType(fourSlots, 2) == mhw::RiseWirebugType::Environment
              && mhw::riseWirebugType(fourSlots, 3) == mhw::RiseWirebugType::Skill
              && mhw::riseWirebugSlotsToRead(fourSlots, 4) == 4,
          "Rise wirebug proven fourth slot is read only with a valid partition and Mono length");
    const mhw::MHRWirebugCountStructure invalidWirebugCount{-1, 1, 1};
    const mhw::MHRWirebugCountStructure excessiveWirebugCount{
        mhw::kRiseWirebugCountLimit + 1, 0, 0};
    check(mhw::riseWirebugSlotsToRead(defaultOnly, 0) == 0
              && mhw::riseWirebugSlotsToRead(defaultOnly, mhw::kRiseWirebugSlotCap + 5)
                     == mhw::kRiseWirebugSlotCap
              && mhw::riseWirebugSlotsToRead(invalidWirebugCount, 3) == 0
              && mhw::riseWirebugSlotsToRead(excessiveWirebugCount, 3) == 0,
          "Rise wirebug Mono length zero, over-cap, and corrupt counts are safe");
    check(mhw::riseWirebugElementAddress(0x1000, 0)
              == std::optional<std::uintptr_t>{0x1020}
              && mhw::riseWirebugElementAddress(0x1000, 2)
                     == std::optional<std::uintptr_t>{0x1020 + 2 * sizeof(std::uintptr_t)}
              && !mhw::riseWirebugElementAddress(0x1000, mhw::kRiseWirebugSlotCap)
              && !mhw::riseWirebugElementAddress(
                     std::numeric_limits<std::uintptr_t>::max() - 0x10, 0),
          "Rise wirebug pointer elements follow Mono header and reject invalid indexes");
    check(mhw::riseWirebugExtraDataSource(mhw::RiseWirebugType::Environment)
              == mhw::RiseWirebugExtraDataSource::Environment
              && mhw::riseWirebugExtraDataSource(mhw::RiseWirebugType::Skill)
                     == mhw::RiseWirebugExtraDataSource::Skill,
          "Rise temporary wirebugs select distinct environment and skill extra chains");
    check(mhw::riseWirebugSeconds(180.0F) == 3.0F
              && mhw::riseWirebugTemporaryTimer(-60.0F) == 0.0F,
          "Rise wirebug timers convert ticks to seconds and clamp display timers");
    const auto activeWirebug = mhw::riseWirebugTimers(120.0F, 300.0F, 60.0F);
    check(activeWirebug.cooldown == 3.0F && activeWirebug.maxCooldown == 6.0F,
          "Rise wirebug cooldown and maximum include extra cooldown in seconds");
    const auto readyWirebug = mhw::riseWirebugTimers(-60.0F, 300.0F, 60.0F);
    check(readyWirebug.cooldown == 0.0F && readyWirebug.maxCooldown == 0.0F,
          "Rise ready wirebug reports zero cooldown maximum");
    const auto nonFiniteWirebug = mhw::riseWirebugTimers(
        std::numeric_limits<float>::quiet_NaN(), 300.0F, 60.0F);
    check(nonFiniteWirebug.cooldown == 0.0F && nonFiniteWirebug.maxCooldown == 0.0F,
          "Rise non-finite wirebug cooldown is rejected safely");

    const auto temporarySnapshot = mhw::riseWirebugSnapshotData(
        120.0F, 300.0F, 60.0F, 180.0F);
    check(temporarySnapshot.cooldown == 3.0F && temporarySnapshot.maxCooldown == 6.0F
              && temporarySnapshot.timer == 3.0F && temporarySnapshot.maxTimer == 3.0F,
          "Rise temporary wirebug snapshot converts all display timers to seconds");
    const auto missingTemporarySnapshot = mhw::riseWirebugSnapshotData(
        120.0F, 300.0F, 60.0F);
    check(missingTemporarySnapshot.timer == 0.0F && missingTemporarySnapshot.maxTimer == 0.0F,
          "Rise wirebug snapshot does not invent a temporary timer without a read");

    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
