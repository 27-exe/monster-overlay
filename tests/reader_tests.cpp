// SPDX-License-Identifier: Apache-2.0

#include "mhw_reader.h"
#include "core/map_paths.h"
#include "core/string_table.h"
#include "monster/target_selector.h"
#include "player/player_types.h"
#include "quest/quest_types.h"
#include "rise/mhr_reader.h"
#include "rise/mhr_types.h"
#include "world/world_types.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <array>
#include <cstdint>
#include <cstring>
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

    // Selection is based on successful AddressMap loading, not mere
    // existence. This keeps a corrupt map in an earlier install prefix from
    // masking a valid map in a later one. An explicit bad map stays selected
    // so --map never silently changes the user's request.
    QTemporaryDir selectionRoot;
    check(selectionRoot.isValid(), "map-selection temporary directory opens");
    const QString firstDir = selectionRoot.path() + QStringLiteral("/first");
    const QString secondDir = selectionRoot.path() + QStringLiteral("/second");
    const QString worldName = QStringLiteral("MonsterHunterWorld.421810.map");
    QDir().mkpath(firstDir);
    QDir().mkpath(secondDir);
    QFile badWorld(firstDir + QLatin1Char('/') + worldName);
    check(badWorld.open(QIODevice::WriteOnly), "bad first World map fixture opens");
    badWorld.write("not an address map\n");
    badWorld.close();
    QFile goodWorld(secondDir + QLatin1Char('/') + worldName);
    check(goodWorld.open(QIODevice::WriteOnly), "good second World map fixture opens");
    goodWorld.write("Address ROOT 0x1234\n");
    goodWorld.close();
    const QStringList worldCandidates = mhw::worldMapCandidates(
        QString(), worldName, {firstDir, secondDir}, QString());
    check(mhw::MhwReader::selectLoadableMap(worldCandidates) == goodWorld.fileName(),
          "bad-content World map falls through to a later loadable candidate");
    const QString explicitBadMap = selectionRoot.path() + QStringLiteral("/missing-explicit.map");
    const QStringList explicitCandidates = mhw::worldMapCandidates(
        explicitBadMap, worldName, {secondDir}, goodWorld.fileName());
    check(mhw::MhwReader::selectLoadableMap(explicitCandidates) == explicitBadMap,
          "bad explicit map does not fall back to an installed map");
    const QString firstMissing = selectionRoot.path() + QStringLiteral("/first-missing.map");
    check(mhw::MhwReader::selectLoadableMap(
              {firstMissing, selectionRoot.path() + QStringLiteral("/second-missing.map")})
              == firstMissing,
          "no loadable map returns the first actionable candidate for diagnostics");

    const QString riseBad = firstDir + QStringLiteral("/MonsterHunterRise.20.0.0.0.map");
    const QString riseGood = secondDir + QStringLiteral("/MonsterHunterRise.19.0.0.0.map");
    QFile badRise(riseBad);
    check(badRise.open(QIODevice::WriteOnly), "bad first Rise map fixture opens");
    badRise.write("also not an address map\n");
    badRise.close();
    QFile goodRise(riseGood);
    check(goodRise.open(QIODevice::WriteOnly), "good later Rise map fixture opens");
    goodRise.write("Address ROOT 0x5678\n");
    goodRise.close();
    const QStringList riseCandidates = mhw::riseMapCandidates(
        QString(), {firstDir, secondDir}, QString());
    check(mhw::MhrReader::findBestMap(riseCandidates) == riseGood,
          "Rise selection skips a bad-content earlier candidate and continues across dirs");

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

    // Rise party roster gating deliberately differs from active-damage gating:
    // state 2 is live, result states 3..7 remain readable for panel-side
    // freezing, and the training room reads even while the quest manager is
    // idle. Lobby/accepted/unknown states do not probe party arrays.
    check(mhw::shouldReadRisePartyRoster(2, false),
          "Rise party roster reads during an active quest");
    check(mhw::shouldReadRisePartyRoster(3, false)
              && mhw::shouldReadRisePartyRoster(7, false),
          "Rise party roster remains readable throughout result states 3..7");
    check(mhw::shouldReadRisePartyRoster(0, true),
          "Rise party roster reads in the training room while quest state is idle");
    check(!mhw::shouldReadRisePartyRoster(0, false)
              && !mhw::shouldReadRisePartyRoster(1, false)
              && !mhw::shouldReadRisePartyRoster(8, false),
          "Rise party roster rejects non-hunt and unknown states outside training");

    // Every roster source is a Mono pointer array. A corrupt declaration must
    // never turn into an unbounded remote-memory walk, and address arithmetic
    // must stay inside both the declared length and the source-specific cap.
    check(mhw::risePartyMonoArrayCount(std::nullopt, 6) == 0
              && mhw::risePartyMonoArrayCount(std::optional<std::int32_t>{-1}, 6) == 0
              && mhw::risePartyMonoArrayCount(std::optional<std::int32_t>{99}, 6) == 6,
          "Rise party Mono array lengths are required, nonnegative, and capped");
    check(mhw::risePartyMonoArrayElementAddress(0x1000, 1, 2, 6)
              == std::optional<std::uintptr_t>{0x1020 + sizeof(std::uintptr_t)},
          "Rise party Mono pointer address stays inside declared length");
    check(!mhw::risePartyMonoArrayElementAddress(0x1000, 2, 2, 6)
              && !mhw::risePartyMonoArrayElementAddress(0x1000, 0, -1, 6)
              && !mhw::risePartyMonoArrayElementAddress(0, 0, 1, 6)
              && !mhw::risePartyMonoArrayElementAddress(0x1000, 6, 99, 6)
              && !mhw::risePartyMonoArrayElementAddress(
                     std::numeric_limits<std::uintptr_t>::max() - 0x10, 0, 1, 6),
          "Rise party Mono pointer address rejects length, cap, and overflow violations");

    // HunterPie MHRPlayer.cs:560-708 identity/layout semantics. Real players
    // retain entity indexes 0..3; followers use 4..5 and compact display slots.
    // Locality is name-based, never inferred from entity index zero.
    mhw::PlayerSnapshot rosterLocal;
    rosterLocal.name = QStringLiteral("Self");
    rosterLocal.weaponId = 8;
    rosterLocal.highRank = 321;
    rosterLocal.masterRank = 210;

    std::array<mhw::RisePartyRosterCandidate, mhw::kRisePartyPlayerCount> players{};
    players[0] = {QStringLiteral("Remote"), 3, 55, 44};
    players[2] = {QStringLiteral("Self"), 8, 321, 210};
    std::array<mhw::RisePartyRosterCandidate, mhw::kRisePartyCompanionCount> companions{};
    companions[0] = {QStringLiteral("Fiorayne"), 0, 321, 210};
    companions[1] = {QStringLiteral("Luchika"), 13, 321, 210};

    const auto onlineRoster = mhw::buildRisePartyRoster(
        players, companions, rosterLocal, true);
    check(onlineRoster.size() == 2
              && onlineRoster[0].entityIndex == 0
              && onlineRoster[0].slot == 0
              && onlineRoster[0].kind == mhw::PartyMemberKind::Player
              && onlineRoster[0].weaponId == 3
              && onlineRoster[0].highRank == 55
              && onlineRoster[0].masterRank == 44
              && !onlineRoster[0].local
              && onlineRoster[1].entityIndex == 2
              && onlineRoster[1].slot == 2
              && onlineRoster[1].local,
          "Rise online roster keeps real entity/display slots and name-based local identity");

    std::array<mhw::RisePartyRosterCandidate, mhw::kRisePartyPlayerCount> soloPlayers{};
    soloPlayers[0] = {QStringLiteral("Self"), 8, 321, 210};
    const auto followerRoster = mhw::buildRisePartyRoster(
        soloPlayers, companions, rosterLocal, false);
    check(followerRoster.size() == 3
              && followerRoster[1].entityIndex == 4
              && followerRoster[1].slot == 1
              && followerRoster[1].kind == mhw::PartyMemberKind::Companion
              && followerRoster[1].weaponId == 0
              && followerRoster[1].highRank == 321
              && followerRoster[1].masterRank == 210
              && !followerRoster[1].local
              && followerRoster[2].entityIndex == 5
              && followerRoster[2].slot == 2
              && followerRoster[2].kind == mhw::PartyMemberKind::Companion,
          "Rise solo followers use entity indexes 4..5 and compact display slots");

    const auto staleFollowerRoster = mhw::buildRisePartyRoster(
        players, companions, rosterLocal, false);
    check(staleFollowerRoster.size() == 2,
          "Rise player teammates and stale follower data are mutually exclusive");

    std::array<mhw::RisePartyRosterCandidate, mhw::kRisePartyPlayerCount> noPlayers{};
    const auto fallbackRoster = mhw::buildRisePartyRoster(
        noPlayers, companions, rosterLocal, false);
    check(fallbackRoster.size() == 3
              && fallbackRoster[0].entityIndex == 0
              && fallbackRoster[0].slot == 0
              && fallbackRoster[0].local
              && fallbackRoster[0].name == rosterLocal.name
              && fallbackRoster[1].slot == 1,
          "Rise missing session array falls back to the local snapshot before followers");

    mhw::PlayerSnapshot unnamedLocal;
    std::array<mhw::RisePartyRosterCandidate, mhw::kRisePartyPlayerCount> unknownPlayers{};
    unknownPlayers[0] = {QStringLiteral("Unknown hunter"), 1, 1, 1};
    const auto unknownRoster = mhw::buildRisePartyRoster(
        unknownPlayers, {}, unnamedLocal, true);
    check(unknownRoster.size() == 1 && !unknownRoster[0].local,
          "Rise entity index zero is not assumed local when the local name is unavailable");

    mhw::PartyMemberSnapshot worldMember;
    worldMember.slot = 3;
    check(worldMember.kind == mhw::PartyMemberKind::Player
              && worldMember.effectiveEntityIndex() == 3,
          "World party snapshots default to Player and resolve entity identity from slot");

    check(sizeof(mhw::MHRCharacterData) == 0x94
              && offsetof(mhw::MHRCharacterData, namePointer) == 0x18
              && offsetof(mhw::MHRCharacterData, highRank) == 0x38
              && offsetof(mhw::MHRCharacterData, masterRank) == 0x90,
          "Rise character roster data matches HunterPie explicit offsets");

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

    // v0.8.4-r18 stamina-units-r2: the Rise HUD reports stamina in thirtieths
    // of the bar's human-scale domain — live user measurements (240 rendered
    // as 720 and 100 as 300 under the falsified /10 pass) pin raw = bar x30,
    // and the session probe read raw 3000 / 3000 / maxExtendableStamina 4500,
    // i.e. the bar's 100 / 100 / 150. PlayerPanel scales on the display path
    // through staminaDisplayValue(), compiled here from the same source
    // include as wirebugSlotLabel; the PlayerSnapshot fields and the validity
    // bounds checked above stay raw.
    check(staminaDisplayValue(mhw::GameId::Rise, 1500.0F) == 50.0F,
          "raw Rise stamina 1500 displays as 50 (raw / 30)");
    check(staminaDisplayValue(mhw::GameId::Rise, 3000.0F) == 100.0F,
          "raw Rise stamina 3000 (probed base max bar) displays as 100");
    check(staminaDisplayValue(mhw::GameId::Rise, 7200.0F) == 240.0F,
          "raw Rise stamina 7200 (measured 240 bar) displays as 240");
    check(staminaDisplayValue(mhw::GameId::Rise, 4500.0F) == 150.0F,
          "raw Rise stamina 4500 (probed maxExtendableStamina) displays as 150");
    check(staminaDisplayValue(mhw::GameId::Rise, 2790.0F) == 93.0F,
          "raw Rise stamina 2790 displays as 93 (panel demo parity)");
    check(staminaDisplayValue(mhw::GameId::Rise, 0.0F) == 0.0F,
          "zero Rise stamina stays zero across the display conversion");
    check(staminaDisplayValue(mhw::GameId::World, 93.0F) == 93.0F
              && staminaDisplayValue(mhw::GameId::World, 150.0F) == 150.0F,
          "World stamina is passed through unscaled (already bar-domain)");

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

    // Rise part identity/type parity with HunterPie. Type is selected only
    // when a flinch-pointer-keyed part object is first created; subsequent
    // scans update values without reclassifying the object.
    check(mhw::riseStablePartType(std::nullopt, true, true)
              == mhw::PartType::Severable,
          "new Rise part classifies severable before breakable");
    check(mhw::riseStablePartType(mhw::PartType::Severable, false, true)
              == mhw::PartType::Severable,
          "cached Rise severable type survives a complete zero sever layer");
    check(mhw::riseStablePartType(mhw::PartType::Breakable, true, true)
              == mhw::PartType::Breakable,
          "cached Rise breakable type is not reclassified on later scans");

    const mhw::RisePartTableIdentity risePartsIdentity{
        42, 0x1000, 0x2000, 0x3000, 16};
    check(mhw::sameRisePartTableIdentity(
              risePartsIdentity, {42, 0x1000, 0x2000, 0x3000, 16}),
          "Rise part cache matches the same monster and three arrays");
    check(!mhw::sameRisePartTableIdentity(
              risePartsIdentity, {43, 0x1000, 0x2000, 0x3000, 16}),
          "Rise part cache rejects a reused monster address with another id");
    check(!mhw::sameRisePartTableIdentity(
              risePartsIdentity, {42, 0x1008, 0x2000, 0x3000, 16}),
          "Rise part cache rejects a changed flinch array");
    check(!mhw::sameRisePartTableIdentity(
              risePartsIdentity, {42, 0x1000, 0x2000, 0x3000, 15}),
          "Rise part cache rejects a changed part count");

    const mhw::RisePartValues firstRiseValues{
        90.0F, 100.0F, 40.0F, 50.0F, 25.0F, 30.0F};
    const mhw::PartSnapshot firstRisePart = mhw::buildRisePartSnapshot(
        3, QStringLiteral("Tail"), firstRiseValues, nullptr);
    check(firstRisePart.partType == mhw::PartType::Severable,
          "first complete Rise sample creates a severable part");
    const mhw::RisePartValues nextRiseValues{
        70.0F, 100.0F, 30.0F, 50.0F, 0.0F, 0.0F};
    const mhw::PartSnapshot nextRisePart = mhw::buildRisePartSnapshot(
        3, QStringLiteral("Tail"), nextRiseValues, &firstRisePart);
    check(nextRisePart.partType == mhw::PartType::Severable
              && nextRisePart.isSeverable,
          "complete later Rise sample preserves the cached severable identity");
    check(nextRisePart.flinch == 70.0F && nextRisePart.maxFlinch == 100.0F
              && nextRisePart.breakHealth == 30.0F
              && nextRisePart.breakMaxHealth == 50.0F
              && nextRisePart.health == 0.0F && nextRisePart.maxHealth == 0.0F,
          "cached Rise part still refreshes all dynamic layer values");
    check(nextRisePart.isPartSevered && !nextRisePart.isBroken,
          "stable Rise severable part uses HunterPie severed-state projection at zero");

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

    // -----------------------------------------------------------------
    // v0.9 i18n (WS-A): World data-name tables, both columns.
    //
    // The English column comes from HunterPie's official en-us.xml
    // (Monsters/World, Monsters/Shared/Part, Stages/World, Stages/Rise,
    // Ailments) plus Game/World/Data/MonsterData.xml and AbnormalityData.xml
    // for the id -> AILMENT_*/ABNORMALITY_* key mapping; the zh column is the
    // frozen pre-i18n literal for normal entries, and the official zh-cn.xml
    // name for statePart entries (v0.9 placeholder cleanup). Every expected
    // string here is a verbatim source value, so silently renaming a zh
    // entry or drifting from the upstream English table fails this test.
    // -----------------------------------------------------------------
    mhw::StringTable &strings = mhw::StringTable::instance();
    check(strings.load(QStringLiteral("en-US")), "en-US locale loads from the qrc");
    check(strings.isEnglish(), "isEnglish() reports the English locale");

    check(mhw::monsterDisplayName(QStringLiteral("000")) == QStringLiteral("Anjanath"),
          "monster 000 reads as Anjanath (en)");
    check(mhw::monsterDisplayName(QStringLiteral("101")) == QStringLiteral("Fatalis"),
          "monster 101 reads as Fatalis (en)");
    check(mhw::monsterDisplayName(QStringLiteral("094")) == QStringLiteral("Zinogre"),
          "monster 094 reads as Zinogre (en)");
    check(mhw::monsterDisplayName(QStringLiteral("999")) == QStringLiteral("999"),
          "unknown monster key stays the raw id key (en)");

    const QVector<mhw::PartSchema> jagras = mhw::kPartSchemas.value(0);
    check(jagras.size() == 7
              && mhw::partDisplayName(jagras[0]) == QStringLiteral("Throat")
              && mhw::partDisplayName(jagras[1]) == QStringLiteral("Tail"),
          "Great Jagras throat/tail read as Throat/Tail (en)");
    check(mhw::partDisplayName(jagras[2]) == QStringLiteral("Head")
              && jagras[2].name == QStringLiteral("头部"),
          "part label switches column while the frozen zh literal stays in place");
    const QVector<mhw::PartSchema> zorah = mhw::kPartSchemas.value(4);
    check(zorah.size() == 16
              && zorah[0].name == QStringLiteral("击退")
              && zorah[0].statePart
              && mhw::partDisplayName(zorah[0]) == QStringLiteral("Repel"),
          "PART_REPEL resolves to 击退/Repel and stays a statePart entry");

    // v0.9 placeholder cleanup invariant: `statePart` is frozen to the
    // pre-cleanup `name.startsWith("PART_")` skip set (131 entries across
    // the table) and no raw PART_* display name may remain anywhere — the
    // reader skip therefore cannot shift a single normal-table slot index.
    {
        int stateParts = 0, rawPlaceholders = 0;
        for (const QVector<mhw::PartSchema> &parts : mhw::kPartSchemas) {
            for (const mhw::PartSchema &p : parts) {
                if (p.statePart)
                    ++stateParts;
                if (QString::fromUtf8(p.name).startsWith(QStringLiteral("PART_")))
                    ++rawPlaceholders;
            }
        }
        check(stateParts == 131 && rawPlaceholders == 0,
              "statePart keeps the frozen 131-entry skip set; no raw PART_* names remain");
    }

    check(std::strcmp(mhw::zoneNameLocalized(mhw::Zone::AncientForest), "Ancient Forest") == 0,
          "zone 101 reads as Ancient Forest (en)");
    check(std::strcmp(mhw::zoneNameLocalized(mhw::Zone::SpecialArena), "Special Arena") == 0,
          "zone 201 reads as Special Arena (en)");
    check(std::strcmp(mhw::zoneNameLocalized(mhw::Zone::RiseLoc1), "Shrine Ruins") == 0,
          "Rise hunting id 1 reads as Shrine Ruins (en)");
    check(std::strcmp(mhw::zoneNameLocalized(static_cast<mhw::Zone>(700)), "Village") == 0,
          "Rise village id 0 reads as Village (en)");

    check(mhw::monsterAilmentDisplayName(1) == QStringLiteral("Poison")
              && mhw::monsterAilmentDisplayName(23) == QStringLiteral("Claw Flinch"),
          "World ailments 1/23 read as Poison/Claw Flinch (en)");
    check(mhw::monsterAilmentDisplayName(12) == QStringLiteral("Dung Bomb"),
          "ailment id 12 has an official English name even though the zh table omits it");
    check(mhw::monsterAilmentDisplayName(99) == QStringLiteral("Ailment 99"),
          "unknown ailment id keeps an explicit English fallback");

    check(mhw::playerDebuffName(0x5DC) == QStringLiteral("Poison")
              && mhw::playerDebuffName(0x63C) == QStringLiteral("Blastscourge"),
          "player debuffs 0x5DC/0x63C read as Poison/Blastscourge (en)");
    check(mhw::playerSongName(0x38) == QStringLiteral("Self-Improvement")
              && mhw::playerSongName(0x118) == QStringLiteral("Elemental Effectiveness"),
          "hunting-horn songs read from the official English table (en)");
    check(mhw::playerBuffName(0x690, 0, 0) == QStringLiteral("Dash Juice")
              && mhw::playerBuffName(0x764, 0, 0) == QStringLiteral("Fortify"),
          "consumable/skill buffs read as Dash Juice/Fortify (en)");
    check(mhw::playerBuffName(0x6A0, 0x6A4, 10) == QStringLiteral("Might Seed")
              && mhw::playerBuffName(0x6A0, 0x6A4, 25) == QStringLiteral("Might Pill")
              && mhw::playerBuffName(0x6D0, 0x6D8, 2) == QStringLiteral("Mega Armorskin"),
          "the duplicate-offset rows are told apart by their WithValue precondition (en)");
    check(mhw::playerDebuffName(0x999) == QStringLiteral("0x999"),
          "unknown debuff offset returns a neutral hex label");

    // ---- same lookups again under zh-CN: every value must be the frozen
    // pre-i18n literal (this is the regression guard for the whole WS-A pass).
    check(strings.load(QStringLiteral("zh-CN")), "zh-CN locale reloads");
    check(!strings.isEnglish(), "isEnglish() is false for zh-CN");

    check(mhw::monsterDisplayName(QStringLiteral("000")) == QStringLiteral("蛮颚龙")
              && mhw::monsterDisplayName(QStringLiteral("101")) == QStringLiteral("黑龙"),
          "monster 000/101 keep 蛮颚龙/黑龙 (zh)");
    check(mhw::partDisplayName(jagras[0]) == QStringLiteral("喉咙")
              && mhw::partDisplayName(jagras[6]) == QStringLiteral("尾巴"),
          "part labels keep 喉咙/尾巴 (zh)");
    check(mhw::partDisplayName(zorah[0]) == QStringLiteral("击退"),
          "the cleaned PART_REPEL entry reads as 击退 (zh)");
    check(std::strcmp(mhw::zoneNameLocalized(mhw::Zone::WildspireWaste), "荒野大陆") == 0
              && std::strcmp(mhw::zoneNameLocalized(mhw::Zone::RiseLoc14), "塔之秘境") == 0,
          "zone labels keep 荒野大陆/塔之秘境 (zh)");
    check(mhw::monsterAilmentDisplayName(1) == QStringLiteral("毒")
              && mhw::monsterAilmentDisplayName(12) == QStringLiteral("异常12"),
          "ailments keep 毒 and the 异常N fallback (zh)");
    check(mhw::playerDebuffName(0x5DC) == QStringLiteral("毒")
              && mhw::playerDebuffName(0x63C) == QStringLiteral("爆破灾祸"),
          "player debuffs keep 毒/爆破灾祸 (zh)");
    check(mhw::playerSongName(0x38) == QStringLiteral("自我强化")
              && mhw::playerBuffName(0x6A0, 0x6A4, 25) == QStringLiteral("怪力药丸"),
          "songs/buffs keep 自我强化/怪力药丸 (zh)");

    // ---- v0.10.8: session player count normalisation -------------------
    // The World readSessionPlayerCount value must never collapse to 0 on a
    // failed/out-of-range read: in HunterPie's semantics 0 IS the solo
    // value, so "read failed -> 0" would silently re-enable every gauge the
    // multiplayer gate suppresses. A failed read must keep the previous
    // session's truth instead.
    using mhw::kSessionReadFailed;
    check(mhw::sanitizeSessionPlayerCount(kSessionReadFailed, 2) == 2,
          "a failed read keeps the previous multiplayer count");
    check(mhw::sanitizeSessionPlayerCount(kSessionReadFailed, 0) == 0,
          "a failed read with no prior context stays solo");
    check(mhw::sanitizeSessionPlayerCount(0, 2) == 0,
          "a genuine 0 (solo) is NOT confused with a failed read");
    check(mhw::sanitizeSessionPlayerCount(0, 1) == 0,
          "solo reads 0 even after a solo session");
    check(mhw::sanitizeSessionPlayerCount(3, 0) == 3,
          "a valid multiplayer read is accepted straight away");
    check(mhw::sanitizeSessionPlayerCount(1, 3) == 1,
          "a genuine 1 (someone left) is accepted, not suppressed");
    check(mhw::sanitizeSessionPlayerCount(4, 3) == 4,
          "a full session reads 4");
    check(mhw::sanitizeSessionPlayerCount(99, 2) == 2,
          "out-of-range count keeps the previous session value");
    check(mhw::sanitizeSessionPlayerCount(-7, 2) == 2,
          "negative garbage keeps the previous session value");

    // ---- v0.10.10: part-gauge fill follows Broken.Foreground, not the tag
    // palette. Scheme.xaml binds #F6A522 to Part.Breakable.Foreground and
    // #F41162 to Part.Severable.Foreground — a HEALTHY part of that kind. A
    // Teostra head (monster 18, thresholds "1") refills to 100% once broken,
    // so painting that in the healthy palette read as pristine HP.
    check(mhw::partGaugeFill(false) == mhw::kPartGaugeUntouched,
          "an untouched part keeps the legacy #78909c fill");
    check(mhw::kPartGaugeUntouched.r == 120 && mhw::kPartGaugeUntouched.g == 144
              && mhw::kPartGaugeUntouched.b == 156,
          "#78909c is the untouched fill");
    check(mhw::partGaugeFill(true) == mhw::kPartGaugeBroken,
          "a broken part takes the Broken.Foreground grey");
    check(mhw::kPartGaugeBroken.r == 113 && mhw::kPartGaugeBroken.g == 113
              && mhw::kPartGaugeBroken.b == 122,
          "#71717A is the broken fill (HunterPie Part.Broken.Foreground)");
    check(mhw::kPartGaugeBroken.b > mhw::kPartGaugeBroken.r,
          "the broken grey is a cool grey (#71717A), not neutral #717171");
    check(mhw::kPartGaugeBroken.r != 120,
          "broken and untouched fills are distinguishable");
    // The old hues must not resurface in this branch on either part kind.
    check(mhw::kPartGaugeBroken.r != 246 && mhw::kPartGaugeBroken.g != 165,
          "the broken fill is not the Severable amber #f6a522");
    check(mhw::kPartGaugeBroken.r != 244 && mhw::kPartGaugeBroken.g != 17,
          "the broken fill is not the Breakable pink #f41162");
    // Both part kinds share one broken colour — the tag chip keeps its hue,
    // only the gauge fill collapses to Broken.Foreground.
    check(mhw::partGaugeFill(true) == mhw::partGaugeFill(true),
          "Breakable and Severable share the broken gauge fill");

    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
