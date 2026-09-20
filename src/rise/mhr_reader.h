#pragma once

#include "mhw_reader.h"
#include "rise/mhr_types.h"

#include <QString>
#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace mhw {

// HunterPie QuestState.InQuest. Other nonzero states include accepted,
// completed, failed, and abandoned quests, which are not live hunts.
constexpr int kRiseQuestStateInQuest = 2;

// HunterPie MHRQuestStructure.Type is valid only for the exact values its
// ToQuestType() switch maps to a core quest type. Training, Expedition,
// Rampage, Kyousei, None, and bitwise combinations are intentionally excluded.
inline constexpr bool isRiseQuestTypeSupported(int type)
{
    switch (type) {
    case 1:   // Normal
    case 2:   // Kill
    case 4:   // Capture
    case 8:   // Boss
    case 16:  // Gather
    case 64:  // Arena
    case 128: // Special
        return true;
    default:
        return false;
    }
}

// MHRNormalQuestDataStructure.Stars is zero-based in memory. Avoid both
// displaying a negative star count and overflowing a corrupt INT_MAX read.
inline int riseNormalQuestStars(int rawStars)
{
    if (rawStars < 0)
        return 0;
    if (rawStars == std::numeric_limits<int>::max())
        return rawStars;
    return rawStars + 1;
}

inline bool isRiseQuestActive(int id, int state, int type)
{
    return id > 0 && state == kRiseQuestStateInQuest
        && isRiseQuestTypeSupported(type);
}

// HunterPie compares save-slot and current-character name pointers directly.
// ProcessMemory distinguishes an unreadable slot pointer from a readable zero,
// but neither may match: accepting either would select an arbitrary slot.
inline bool isRiseSaveSlotNameMatch(
    std::uintptr_t characterNamePtr,
    const std::optional<std::uintptr_t> &slotNamePtr)
{
    return characterNamePtr != 0 && slotNamePtr && *slotNamePtr != 0
        && *slotNamePtr == characterNamePtr;
}

// HunterPie MHRPlayer treats the training room as a hunting zone even though
// it is Stage.Type == 4 (Village): VillageId 5 is the documented exception.
inline constexpr bool isRiseTrainingRoom(const MHRStageStructure &stage)
{
    return stage.type == 4 && stage.villageId == 5;
}

inline constexpr bool isRiseHuntingZone(const MHRStageStructure &stage)
{
    return (stage.type >= 5 && stage.type <= 11) || isRiseTrainingRoom(stage);
}

// HunterPie creates MHRMeleeWeapon only for core Weapon values 0 through 10.
// Weapon.None / invalid Rise values are translated to -1 and must never read a
// sharpness gauge.
inline constexpr bool isRiseMeleeWeaponId(int weaponId)
{
    return weaponId >= 0 && weaponId <= 10;
}

// In hubs, the HUD resolves before the saved-character name on some polls.
// Preserve the raw values; this only decides whether the player snapshot is
// usable. A name remains sufficient to retain the existing hub behaviour.
inline bool isRisePlayerValid(const QString &name, float health, float maxHealth,
                              float stamina, float maxStamina)
{
    const bool hasSaneVitals = std::isfinite(health)
        && std::isfinite(maxHealth)
        && std::isfinite(stamina)
        && std::isfinite(maxStamina)
        && maxHealth > 0.0F && maxHealth <= 1000.0F
        && maxStamina > 0.0F && maxStamina <= 10000.0F;
    return !name.isEmpty() || hasSaneVitals;
}

// Rise wirebug and party entries use Mono nint[] objects. Their header length
// is at +0x1C and the pointer payload begins at +0x20, matching HunterPie's
// ReadArraySafeAsync / ReadArrayAsync helpers.
constexpr std::uintptr_t kRiseMonoArrayLengthOffset = 0x1CULL;
constexpr std::uintptr_t kRiseMonoArrayDataOffset = 0x20ULL;

constexpr int kRisePartyPlayerCount = 4;
constexpr int kRisePartyCompanionCount = 2;
constexpr int kRisePartyRosterCount =
    kRisePartyPlayerCount + kRisePartyCompanionCount;

// A stateless reader cannot retain HunterPie's mutable _party collection in
// result states. Keep returning the live roster for states 3..7 so panels can
// freeze their last active rows, and always allow the training-room exception.
inline constexpr bool shouldReadRisePartyRoster(int questState,
                                                bool isTrainingRoom)
{
    return isTrainingRoom || (questState >= kRiseQuestStateInQuest
                              && questState <= 7);
}

inline int risePartyMonoArrayCount(
    const std::optional<std::int32_t> &length, int cap)
{
    if (!length || *length <= 0 || cap <= 0)
        return 0;
    return std::min(*length, cap);
}

inline std::optional<std::uintptr_t> risePartyMonoArrayElementAddress(
    std::uintptr_t header, int index, std::int32_t declaredLength, int cap)
{
    if (declaredLength <= 0 || cap <= 0)
        return std::nullopt;
    const int count = std::min(declaredLength, cap);
    if (header == 0 || index < 0 || index >= count
        || header > std::numeric_limits<std::uintptr_t>::max()
                         - kRiseMonoArrayDataOffset) {
        return std::nullopt;
    }

    const std::uintptr_t data = header + kRiseMonoArrayDataOffset;
    const auto element = static_cast<std::uintptr_t>(index);
    constexpr std::uintptr_t stride = sizeof(std::uintptr_t);
    if (element > (std::numeric_limits<std::uintptr_t>::max() - data) / stride)
        return std::nullopt;
    return data + element * stride;
}

// A fully dereferenced candidate from HunterPie's character/weapon arrays.
// Empty names are invalid and are intentionally omitted by buildRisePartyRoster.
struct RisePartyRosterCandidate {
    QString name;
    int weaponId{-1};
    int highRank{};
    int masterRank{};
};

[[nodiscard]] QVector<PartyMemberSnapshot> buildRisePartyRoster(
    const std::array<RisePartyRosterCandidate, kRisePartyPlayerCount> &players,
    const std::array<RisePartyRosterCandidate, kRisePartyCompanionCount> &companions,
    const PlayerSnapshot &localPlayer,
    bool playersFromSos);

// Rise exposes four source slots in HunterPie's MHRWirebug[] (default plus
// up to three temporary entries). Preserve that proven cap; the panel
// renders up to four source slots.
constexpr int kRiseWirebugSlotCap = 4;
// A count may be larger than the proven four-slot representation, but indexes beyond this
// cap are not inspected. This bounds malformed partitions before their index
// arithmetic reaches the Mono array.
constexpr int kRiseWirebugCountLimit = kRiseWirebugSlotCap + 1;

enum class RiseWirebugType {
    None,
    Default,
    Environment,
    Skill,
};

// Mirror HunterPie MHRiseUtils.ToType exactly. Counts identify positions, not
// a sum/range: environmental and skill slots are present only at their exact
// partition indices, which deliberately preserves holes from corrupt counts.
inline constexpr RiseWirebugType riseWirebugType(
    const MHRWirebugCountStructure &count, int index)
{
    if (index < 0)
        return RiseWirebugType::None;
    if (index < count.default_)
        return RiseWirebugType::Default;
    if (count.environment > 0 && index == count.default_)
        return RiseWirebugType::Environment;
    if (count.skill > 0 && index == count.default_ + count.environment)
        return RiseWirebugType::Skill;
    return RiseWirebugType::None;
}

inline constexpr bool isRiseWirebugCountSane(const MHRWirebugCountStructure &count)
{
    return count.default_ >= 0 && count.environment >= 0 && count.skill >= 0
        && count.default_ <= kRiseWirebugCountLimit
        && count.environment <= kRiseWirebugCountLimit
        && count.skill <= kRiseWirebugCountLimit;
}

inline constexpr bool isRiseWirebugTemporary(RiseWirebugType type)
{
    return type == RiseWirebugType::Environment || type == RiseWirebugType::Skill;
}

inline constexpr int riseWirebugSlotsToRead(const MHRWirebugCountStructure &count,
                                            int monoArrayLength)
{
    if (!isRiseWirebugCountSane(count) || monoArrayLength <= 0)
        return 0;
    return std::min(monoArrayLength, kRiseWirebugSlotCap);
}

inline std::optional<std::uintptr_t> riseWirebugElementAddress(
    std::uintptr_t header, int index)
{
    if (index < 0 || index >= kRiseWirebugSlotCap
        || header > std::numeric_limits<std::uintptr_t>::max() - kRiseMonoArrayDataOffset)
        return std::nullopt;
    const auto stride = static_cast<std::uintptr_t>(index) * sizeof(std::uintptr_t);
    const auto data = header + kRiseMonoArrayDataOffset;
    if (data > std::numeric_limits<std::uintptr_t>::max() - stride)
        return std::nullopt;
    return data + stride;
}

enum class RiseWirebugExtraDataSource {
    None,
    Environment,
    Skill,
};

inline constexpr RiseWirebugExtraDataSource riseWirebugExtraDataSource(RiseWirebugType type)
{
    switch (type) {
    case RiseWirebugType::Environment:
        return RiseWirebugExtraDataSource::Environment;
    case RiseWirebugType::Skill:
        return RiseWirebugExtraDataSource::Skill;
    default:
        return RiseWirebugExtraDataSource::None;
    }
}

inline float riseWirebugSeconds(float ticks)
{
    return ticks / 60.0F;
}

inline float riseWirebugNonnegativeSeconds(float ticks)
{
    const float seconds = riseWirebugSeconds(ticks);
    return std::isfinite(seconds) ? std::max(0.0F, seconds) : 0.0F;
}

struct RiseWirebugTimers {
    float cooldown{0.0F};
    float maxCooldown{0.0F};
};

// MHRWirebug.Update adds extra cooldown to both values. A ready wirebug has no
// meaningful cooldown maximum, so report zero for the panel's ready branch.
inline RiseWirebugTimers riseWirebugTimers(float cooldown, float maxCooldown,
                                           float extraCooldown)
{
    const float extraSeconds = riseWirebugSeconds(extraCooldown);
    const float currentSeconds = riseWirebugSeconds(cooldown) + extraSeconds;
    if (!std::isfinite(currentSeconds) || currentSeconds <= 0.0F)
        return {};

    const float maxSeconds = riseWirebugSeconds(maxCooldown) + extraSeconds;
    return {currentSeconds,
            std::isfinite(maxSeconds) ? std::max(0.0F, maxSeconds) : 0.0F};
}

inline float riseWirebugTemporaryTimer(float ticks)
{
    return riseWirebugNonnegativeSeconds(ticks);
}

struct RiseWirebugSnapshotData {
    float cooldown{0.0F};
    float maxCooldown{0.0F};
    float timer{0.0F};
    float maxTimer{0.0F};
};

// The temporary branch has no persistent object state. Preserve HunterPie's
// current timer conversion and use the current value as this frame's display
// maximum rather than inventing a historic maximum.
inline RiseWirebugSnapshotData riseWirebugSnapshotData(
    float cooldown, float maxCooldown, float extraCooldown,
    std::optional<float> temporaryTimer = std::nullopt)
{
    const RiseWirebugTimers cooldowns = riseWirebugTimers(
        cooldown, maxCooldown, extraCooldown);
    RiseWirebugSnapshotData data{cooldowns.cooldown, cooldowns.maxCooldown};
    if (temporaryTimer) {
        data.timer = riseWirebugTemporaryTimer(*temporaryTimer);
        data.maxTimer = data.timer;
    }
    return data;
}

// first the entry pointer at cameraStyle + type * pointer-size, then the
// target pointer at entry + 0x78. Keeping the first-stage address calculation
// separate makes the required dereference layer explicit and testable.
inline std::optional<std::uintptr_t> riseLockOnSlotEntryAddress(
    std::uintptr_t cameraStyle, std::int32_t type)
{
    if (type < 0)
        return std::nullopt;
    const auto slot = static_cast<std::uintptr_t>(type);
    constexpr std::uintptr_t kPointerSize = sizeof(std::uintptr_t);
    if (slot > (std::numeric_limits<std::uintptr_t>::max() - cameraStyle) / kPointerSize)
        return std::nullopt;
    return cameraStyle + slot * kPointerSize;
}

// A readable ID is a valid Rise monster ID, including 0. The reader must only
// reject failed memory reads rather than assigning meaning to an ID value.
inline constexpr bool hasRiseMonsterId(const std::optional<std::int32_t> &id)
{
    return id.has_value();
}

// HunterPie's ReadArraySafe resolves MONSTER_LIST_OFFSETS to a Mono array
// header. It uses the shared +0x1C length / +0x20 payload layout and exposes
// at most five active monster slots.
constexpr int kRiseMonsterListMax = 5;

inline int riseMonsterListCount(const std::optional<std::int32_t> &length)
{
    if (!length || *length <= 0)
        return 0;
    return std::min(*length, kRiseMonsterListMax);
}

inline std::optional<std::uintptr_t> riseMonsterListElementAddress(
    std::uintptr_t header, int index)
{
    if (index < 0 || index >= kRiseMonsterListMax
        || header > std::numeric_limits<std::uintptr_t>::max() - kRiseMonoArrayDataOffset)
        return std::nullopt;
    const auto stride = static_cast<std::uintptr_t>(index) * sizeof(std::uintptr_t);
    const auto data = header + kRiseMonoArrayDataOffset;
    if (data > std::numeric_limits<std::uintptr_t>::max() - stride)
        return std::nullopt;
    return data + stride;
}

// A candidate map is proven only by a readable monster id plus sane health
// values. Current HP may be zero (fainted/captured state), but must not be
// negative; maximum HP must be finite and positive.
inline bool isRiseMapProbeMonster(const std::optional<std::int32_t> &id,
                                  const std::optional<float> &maxHealth,
                                  const std::optional<float> &health)
{
    return hasRiseMonsterId(id) && maxHealth && health
        && std::isfinite(*maxHealth) && std::isfinite(*health)
        && *maxHealth > 0.0F && *health >= 0.0F;
}

// Rise exposes parallel sever, break, and flinch health tables. Mirror
// HunterPie's PartType dispatch order so a part present in several tables uses
// its sever health first, then break health, then flinch health.
inline constexpr PartType risePartType(bool isSeverable, bool isBreakable)
{
    return isSeverable ? PartType::Severable
                        : (isBreakable ? PartType::Breakable : PartType::Flinch);
}

// Returns cumulative thresholds for a Mono int[] payload. The array length must
// be in [1, 7]; absent colour slots remain zero. Negative or implausibly large
// segment values and sums outside int are rejected before a snapshot is built.
inline bool riseBuildSharpnessThresholds(const std::vector<std::int32_t> &values,
                                         std::array<int, 7> *thresholds)
{
    if (!thresholds || values.empty() || values.size() > thresholds->size())
        return false;

    constexpr std::int64_t kMaxSegmentHits = 10000;
    thresholds->fill(0);
    std::int64_t cumulative = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const std::int64_t value = values[i];
        if (value < 0 || value > kMaxSegmentHits)
            return false;
        if (value == 0)
            continue;
        cumulative += value;
        if (cumulative > std::numeric_limits<int>::max())
            return false;
        (*thresholds)[i] = static_cast<int>(cumulative);
    }
    return true;
}

// Cache refreshes whenever the live Mono array object changes. Weapon type is
// deliberately not a cache key: distinct weapons of the same type have their
// own sharpness arrays.
inline constexpr bool riseSharpnessCacheNeedsRefresh(
    std::uintptr_t cachedArrayPtr, bool cacheValid, std::uintptr_t arrayPtr)
{
    return !cacheValid || cachedArrayPtr != arrayPtr;
}

// Sanitised values for the active colour segment. Rise's CurrentSharpness is
// cumulative, so both the displayed count and fill denominator are relative to
// the preceding threshold. Do subtractions in int64 to tolerate corrupt input.
struct RiseSharpnessSegment {
    int threshold{0};
    int total{0};
    int remaining{0};
    bool valid{false};
};

inline RiseSharpnessSegment riseSharpnessCurrentSegment(const SharpnessSnapshot &snapshot)
{
    RiseSharpnessSegment segment;
    if (snapshot.level < 0 || snapshot.level >= 7 || snapshot.threshold < 0)
        return segment;

    const std::int64_t threshold = snapshot.threshold;
    const std::int64_t upperBound = snapshot.thresholds[snapshot.level];
    const std::int64_t total = upperBound - threshold;
    if (total <= 0 || total > std::numeric_limits<int>::max())
        return segment;

    const std::int64_t rawRemaining = static_cast<std::int64_t>(snapshot.currentHits) - threshold;
    segment.threshold = snapshot.threshold;
    segment.total = static_cast<int>(total);
    segment.remaining = static_cast<int>(std::clamp<std::int64_t>(rawRemaining, 0, total));
    segment.valid = true;
    return segment;
}

// Memory reader for Monster Hunter Rise (16.0.2.0 address map). Mirrors
// MhwReader's structure and reuses the generic ProcessMemory / AddressMap
// and the static followPointerChain() helper. The resulting GameSnapshot
// is tagged GameId::Rise.
class MhrReader {
public:
    explicit MhrReader(QString mapPath);

    [[nodiscard]] GameSnapshot poll();
    [[nodiscard]] const QString &mapPath() const;

    static std::optional<qint64> findRisePid();

    // In candidate order, return the first map whose Mono monster list
    // resolves a readable id and finite health (max > 0, current >= 0).
    // Without a running game, or when no live probe validates, return the
    // first loadable map. If none loads, retain the first path for diagnostics.
    [[nodiscard]] static QString findBestMap(const QStringList &candidates);

private:
    struct StageInfo {
        MHRStageStructure stage{};
        bool inHuntingZone{false};
    };

    bool ensureAttached(GameSnapshot &snapshot);
    [[nodiscard]] std::uintptr_t absolute(const QString &key) const;

    StageInfo readZone(QString *error);
    QVector<MonsterSnapshot> readMonsters(QString *error);
    void readMonsterParts(std::uintptr_t monster, MonsterSnapshot &snapshot);
    // v0.8.4 E1: documented no-op stub. The Rise monster component does
    // not expose a verified Clutch Claw / wound tenderize table the way
    // MHW does at monster+0x1C458 (see B3 REPORT §E1 — Tenderize 缺失).
    // HunterPie's MHRise / MHRPartStructure has Tenderize / MaxTenderize
    // properties but never updates them, confirming the gap. The method
    // is wired into readMonsters() anyway so the per-tick lifecycle is
    // identical to World, and so the first verified offset (or a future
    // Cheat Engine probe) can be added in a single, obvious place.
    void readMonsterTenderizes(std::uintptr_t monster, MonsterSnapshot &snapshot);
    void readMonsterAilments(std::uintptr_t monster, MonsterSnapshot &snapshot);
    void readMonsterQurio(std::uintptr_t monster, MonsterSnapshot &snapshot);
    [[nodiscard]] std::uintptr_t readLockOnTarget() const;
    PlayerSnapshot readPlayer(QString *error);
    QVector<PartyMemberSnapshot> readParty(const PlayerSnapshot &localPlayer,
                                           int questState,
                                           bool isTrainingRoom,
                                           QString *error);
    void readWirebugs(PlayerSnapshot &snapshot, QString *error);
    // v0.8.4-r18 player-abnormalities: consumable buffs + debuffs from
    // ABNORMALITIES_ADDRESS + CONS_/DEBUFF_ABNORMALITIES_OFFSETS. Mirrors
    // HunterPie MHRPlayer.GetConsumableAbnormalities (:436-494),
    // GetPlayerDebuffAbnormalities (:497-554), GetPlayerAbnormalitiesCleanup
    // (:395-405) and GetPlayerConditions (:862-886). Publishes nothing
    // outside a hunting zone (the cleanup path) or when a category blob
    // fails to resolve. The schema table + pure evaluation live in
    // rise/mhr_abnormalities.h.
    void readAbnormalities(PlayerSnapshot &snapshot, bool inHuntingZone,
                           QString *error);
    // BUG #5: read weapon sharpness from SHARPNESS_ADDRESS +
    // SHARPNESS_OFFSETS / SHARPNESS_ARRAY_OFFSETS.
    SharpnessSnapshot readSharpness(int weaponId, QString *error);
    QuestSnapshot readQuest(QString *error);
    [[nodiscard]] QString readUtf16(std::uintptr_t address, int length) const;

    AddressMap map_;
    ProcessMemory memory_;
    QString mapPath_;
    QString mapError_;
    std::uintptr_t imageBase_ = 0;

    // BUG #5: sharpness threshold cache. It keys on the live Mono int[]
    // object rather than weapon type: two weapons of the same type can
    // expose different threshold arrays.
    std::uintptr_t cachedSharpnessArrayPtr_ = 0;
    std::array<int, 7> cachedSharpnessThresholds_{};
    bool cachedSharpnessThresholdsValid_ = false;
};

} // namespace mhw
