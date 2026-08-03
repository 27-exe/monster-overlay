#pragma once

#include "core/game_snapshot.h"
#include "monster/monster_types.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mhw {

// Shared helpers used by the per-domain reader functions (monster, player,
// quest — each lives in its own .cpp linked into the same target).
inline bool isSanePointer(std::uintptr_t value)
{
    return value >= 0x10000 && value < 0x0000800000000000ULL;
}

#pragma pack(push, 1)
struct QuestData {
    std::int32_t maxDeaths;
    std::int32_t deaths;
};
#pragma pack(pop)
static_assert(sizeof(QuestData) == 8);

class AddressMap {
public:
    bool load(const QString &path, QString *error = nullptr);
    [[nodiscard]] std::uintptr_t address(const QString &key) const;
    [[nodiscard]] const std::vector<std::uintptr_t> &offsets(const QString &key) const;
    [[nodiscard]] bool hasAddress(const QString &key) const;
    [[nodiscard]] bool hasOffsets(const QString &key) const;

private:
    std::unordered_map<std::string, std::uintptr_t> addresses_;
    std::unordered_map<std::string, std::vector<std::uintptr_t>> offsets_;
};

class ProcessMemory {
public:
    ProcessMemory() = default;
    ~ProcessMemory();
    ProcessMemory(const ProcessMemory &) = delete;
    ProcessMemory &operator=(const ProcessMemory &) = delete;

    bool attach(qint64 pid, QString *error = nullptr);
    void detach();
    [[nodiscard]] bool attached() const;
    [[nodiscard]] qint64 pid() const;
    [[nodiscard]] std::uintptr_t imageBase(QString *error = nullptr,
                                           const QString &exeName = QStringLiteral("monsterhunterworld.exe")) const;
    bool readBytes(std::uintptr_t address, void *destination, std::size_t size, QString *error = nullptr) const;

    template <typename T>
    std::optional<T> read(std::uintptr_t address, QString *error = nullptr) const
    {
        T value{};
        if (!readBytes(address, &value, sizeof(T), error))
            return std::nullopt;
        return value;
    }

    template <typename T>
    std::vector<T> readArray(std::uintptr_t address, std::size_t count, QString *error = nullptr) const
    {
        // C1 (v0.7.5 audit): clamp garbage counts before they feed the
        // vector allocator. Every current call site passes a small
        // constant (2..128), so a bogus count can only arrive if future
        // code reads it from game memory at a TOCTOU moment (process
        // death / zone transition); an uncapped std::vector<T>(count)
        // would then throw bad_alloc/length_error straight into the
        // poll loop and terminate the whole overlay.
        constexpr std::size_t kMaxElements = 4096;
        if (count > kMaxElements) {
            if (error)
                *error = QStringLiteral("readArray: count %1 exceeds safety cap")
                             .arg(static_cast<qulonglong>(count));
            return {};
        }
        std::vector<T> values(count);
        if (count == 0)
            return values;
        if (!readBytes(address, values.data(), sizeof(T) * count, error))
            return {};
        return values;
    }

private:
    qint64 pid_{-1};
    int memFd_{-1};
};

struct HpCluster { std::uintptr_t hpAddr = 0; float maxHealth = 0.0F; };

class MhwReader {
public:
    explicit MhwReader(QString mapPath,
                       QString exeName = QStringLiteral("monsterhunterworld.exe"));

    [[nodiscard]] GameSnapshot poll();
    [[nodiscard]] const QString &mapPath() const;

    static std::optional<qint64> findGamePid(
        const QString &exeName = QStringLiteral("monsterhunterworld.exe"));
    static std::uintptr_t followPointerChain(const ProcessMemory &memory,
                                             std::uintptr_t address,
                                             const std::vector<std::uintptr_t> &offsets,
                                             QString *error = nullptr);

private:
    bool ensureAttached(GameSnapshot &snapshot);
    std::uintptr_t absolute(const QString &key) const;
    QString readUtf8(std::uintptr_t address, std::size_t maxLength) const;
    QString joinOffsets() const;
    void refreshPlayerIdentity(PlayerSnapshot &player);
    void discoverMonsterTable();
    Zone readZone(QString *error);
    QVector<MonsterSnapshot> readMonsters(QString *error);
    void readMonsterAilments(MonsterSnapshot &monster);
    // v0.7.4: Tenderize is folded into PartSnapshot; this routine walks
    // the 10 in-memory TenderizeInfoStructure slots and writes each slot's
    // (Duration, MaxDuration) into every PartSnapshot whose PartSchema
    // declares the slot's PartId in its tenderizeIds list.
    void applyTenderizesToParts(MonsterSnapshot &monster);
    PlayerSnapshot readPlayer(QString *error);
    QVector<PartyMemberSnapshot> readParty(QString *error);
    QuestSnapshot readQuest(QString *error);
    // Sharpness — HunterPie MHWMeleeWeapon.GetWeaponSharpness. Returns
    // a zero-initialised snapshot when the equipped weapon is ranged
    // (bow, hbg, lbg) or the memory read fails. Force a fresh read on
    // every poll so the bar tracks sharpening / hitting in real time.
    SharpnessSnapshot readSharpness(int weaponId, QString *error);

    AddressMap map_;
    ProcessMemory memory_;
    QString mapPath_;
    QString exeName_;
    QString mapError_;
    std::uintptr_t imageBase_ = 0;
    std::uintptr_t monsterTableBase_ = 0;
    std::size_t monsterTableCount_ = 0;
    std::vector<HpCluster> hpClusters_;
    struct CachedMonster { MonsterSnapshot snapshot; float maxHP; };
    std::unordered_map<std::uintptr_t, CachedMonster> monsterCache_;
    std::vector<std::uintptr_t> cachedArray_;
    std::uintptr_t cachedArrayBase_ = 0;
    // Last manual target the player pinned on the map (zero if none / map
    // closed). Read once per poll from MHWMapMonsterSelectionStructure.
    std::uintptr_t manualTargetAddress_ = 0;
    // Last quest-pinned monster pointer. HunterPie reads both:
    //   quest target (cap quest / investigation mark)
    //   manual map pin (player opened map and pinned)
    // Each monster picks quest target first, falling back to manual
    // pin.  Address comes from MONSTER_QUEST_TARGET_ADDRESS →
    // MONSTER_QUEST_TARGET_OFFSETS → 0x48,0x1760,0x100.
    std::uintptr_t questTargetAddress_ = 0;
    // Sharpness cache: thresholds depend on the equipped weapon id,
    // so we re-read them only when the weapon changes. knocks the
    // ~7 pointer-chase + 7 short reads out of the per-tick critical
    // path during sustained combat.
    int  cachedSharpnessWeaponId_ = -1;
    int  cachedSharpnessThresholds_[7] = {0,0,0,0,0,0,0};
    bool cachedSharpnessThresholdsValid_ = false;
    // S1 (v0.7.5 audit): HunterPie MHWMeleeWeapon.MaximumSharpness needs
    // the MINIMUM_SHARPNESSES_ADDRESS table (8 ints, static in game
    // memory — HunterPie caches it with `??=`) and the weapon's MaxLevel
    // field to decide whether handicraft bonus applies. Cached here the
    // same way thresholds are.
    int  cachedMinimumSharpnesses_[8] = {0,0,0,0,0,0,0,0};
    bool cachedMinimumSharpnessesValid_ = false;
    // HunterPie LockOn mode: LOCKON chain resolves a list node whose +0x950
    // contains the targeted monster's double-linked-list index.
    int lockOnTargetIndex_ = -1;
};

} // namespace mhw