#pragma once

#include <QString>
#include <QVector>
#include <QSet>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mhw {

struct PartSnapshot {
    int index{-1};        // part index (matches MHWMonsterPartStructure.Index)
    QString name;         // e.g. "PART_HEAD"
    // Break layer: Health / MaxHealth. Per-layer values; in multiplayer
    // on the client these stay frozen at 100% (Capcom does not sync
    // per-part HP deltas — only Counter + total HP).
    float health{};
    float maxHealth{};
    float extraHealth{};
    float extraMaxHealth{};
    // Flinch: cumulative stagger damage on this part. Updates every hit
    // on the local client (both solo and multi), because the flinch
    // accumulator runs locally. MaxFlinch = the per-layer single-hit cap
    // when the part has BreakThresholds; otherwise the per-hit cap.
    float flinch{};
    float maxFlinch{};
    int counter{};        // number of BreakThresholds already crossed
    int firstThreshold{0};
    bool isSeverable{false};   // from MonsterData.xml IsSeverable
    bool isBreakable{false};   // BreakThresholds.Count > 0
    bool isBroken{false};      // computed: Counter >= last threshold OR part severed
};

struct MonsterSnapshot {
    std::uintptr_t address{};
    int id{-1};
    QString internalName;
    float health{};
    float maxHealth{};
    float stamina{};
    float maxStamina{};
    float enrageSeconds{};
    float enrageMaxSeconds{};
    bool enraged{};
    QVector<PartSnapshot> parts;
};

struct PlayerSnapshot {
    float health{};
    float maxHealth{};
    float stamina{};
    float maxStamina{};
    bool valid{};
};

struct PartyMemberSnapshot {
    QString name;
    int weaponId{-1};
    int masterRank{};
    int damage{};
    bool local{};
};

struct QuestSnapshot {
    int id{};
    int stars{};
    int state{};
    int category{};
    int deaths{};
    int maxDeaths{};
    float timeLeftSeconds{};
    bool active{};
};

enum class Zone : int {
    MainMenu = 0,
    AncientForest = 101,
    WildspireWaste = 102,
    CoralHighlands = 103,
    RottenVale = 104,
    EldersRecess = 105,
    GreatRavine = 106,
    GreatRavine2 = 107,
    HoarfrostReach = 108,
    GuidingLands = 109,
    SpecialArena = 201,
    Arena = 202,
    SelianaSupplyCache = 203,
    Astera = 301,
    AsteraGatheringHub = 302,
    ResearchBase = 303,
    Seliana = 305,
    SelianaGatheringHub = 306,
    Introduction = 401,
    Everstream = 403,
    ConfluenceOfFates = 405,
    AncientForest2 = 406,
    CavernsOfElDorado = 409,
    SelianaSupplyCache2 = 411,
    OriginIsle = 412,
    OriginIsle2 = 413,
    SecludedValley = 415,
    SecludedValley2 = 416,
    CastleSchrade = 417,
    LivingQuarters = 501,
    PrivateQuarters = 502,
    PrivateSuite = 503,
    TrainingArea = 504,
    ChamberOfFive = 505,
    SelianaRoom = 506,
    Unknown = -1
};

const char *zoneName(Zone zone);
bool isHuntingZone(Zone zone);
bool isPeaceZone(Zone zone);

struct GameSnapshot {
    bool attached{};
    qint64 pid{-1};
    std::uintptr_t imageBase{};
    QString status;
    Zone zone{Zone::MainMenu};
    QVector<MonsterSnapshot> monsters;
    PlayerSnapshot player;
    QVector<PartyMemberSnapshot> party;
    QuestSnapshot quest;
    bool isMultiplayer{false};   // party.size() > 1
};

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
    [[nodiscard]] std::uintptr_t imageBase(QString *error = nullptr) const;
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

// One schema entry per Part in HunterPie's MonsterData.xml. `isSeverable`
// dispatches readMonsters to either the normal table (0x40, stride 0x1F8)
// or the severable table (0x1FC8, stride 0x78). `id` is the per-monster
// local part index from MonsterData.xml; for severable parts it is the
// Index field stored in the live part structure.
struct PartSchema { int id; bool isSeverable; const char* name; const char* thresholds; };

// Generated from data/MonsterHunterWorld.421810.map / MonsterData.xml.
// Exposed for tests and diagnostic tooling.
extern const QHash<int, QVector<PartSchema>> kPartSchemas;

class MhwReader {
public:
    explicit MhwReader(QString mapPath);

    [[nodiscard]] GameSnapshot poll();
    [[nodiscard]] const QString &mapPath() const;

    static std::optional<qint64> findGamePid();
    static std::uintptr_t followPointerChain(const ProcessMemory &memory,
                                             std::uintptr_t address,
                                             const std::vector<std::uintptr_t> &offsets,
                                             QString *error = nullptr);

private:
    bool ensureAttached(GameSnapshot &snapshot);
    std::uintptr_t absolute(const QString &key) const;
    QString readUtf8(std::uintptr_t address, std::size_t maxLength) const;
    QString joinOffsets() const;
    void discoverMonsterTable();
    Zone readZone(QString *error);
    QVector<MonsterSnapshot> readMonsters(QString *error);
    PlayerSnapshot readPlayer(QString *error);
    QVector<PartyMemberSnapshot> readParty(QString *error);
    QuestSnapshot readQuest(QString *error);

    AddressMap map_;
    ProcessMemory memory_;
    QString mapPath_;
    QString mapError_;
    std::uintptr_t imageBase_ = 0;
    std::uintptr_t monsterTableBase_ = 0;
    std::size_t monsterTableCount_ = 0;
    std::vector<HpCluster> hpClusters_;
    struct CachedMonster { MonsterSnapshot snapshot; float maxHP; };
    std::unordered_map<std::uintptr_t, CachedMonster> monsterCache_;
    std::vector<std::uintptr_t> cachedArray_;
    std::uintptr_t cachedArrayBase_ = 0;
};

} // namespace mhw

#include <QString>

namespace mhw {

inline const char *zoneName(Zone zone)
{
    switch (zone) {
    case Zone::MainMenu: return "主菜单";
    case Zone::AncientForest: return "古代树森林";
    case Zone::WildspireWaste: return "荒野大陆";
    case Zone::CoralHighlands: return "珊瑚高地";
    case Zone::RottenVale: return "龙结晶之地";
    case Zone::EldersRecess: return "龙之墓场";
    case Zone::GreatRavine: return "大溪谷";
    case Zone::GreatRavine2: return "大溪谷·深层";
    case Zone::HoarfrostReach: return "冰呪之地";
    case Zone::GuidingLands: return "引导之地";
    case Zone::SpecialArena: return "特殊斗技场";
    case Zone::Arena: return "斗技场";
    case Zone::SelianaSupplyCache: return "月辰补给所";
    case Zone::Astera: return "阿斯特拉";
    case Zone::AsteraGatheringHub: return "阿斯特拉·集会所";
    case Zone::ResearchBase: return "研究基地";
    case Zone::Seliana: return "月辰";
    case Zone::SelianaGatheringHub: return "月辰·集会所";
    case Zone::Introduction: return "新大陆入门区";
    case Zone::Everstream: return "不绝的河流";
    case Zone::ConfluenceOfFates: return "命运的交汇";
    case Zone::AncientForest2: return "古代树森林·深层";
    case Zone::CavernsOfElDorado: return "黄金洞窟";
    case Zone::SelianaSupplyCache2: return "月辰补给所·深层";
    case Zone::OriginIsle: return "原点之岛";
    case Zone::OriginIsle2: return "原点之岛·深层";
    case Zone::SecludedValley: return "秘境之谷";
    case Zone::SecludedValley2: return "秘境之谷·深层";
    case Zone::CastleSchrade: return "城塞高地·修雷德";
    case Zone::LivingQuarters: return "猎人生活区";
    case Zone::PrivateQuarters: return "私人房间";
    case Zone::PrivateSuite: return "私人套房";
    case Zone::TrainingArea: return "训练区";
    case Zone::ChamberOfFive: return "五星之间";
    case Zone::SelianaRoom: return "月辰·休息室";
    case Zone::Unknown: return "未知";
    }
    return "未知";
}

inline bool isHuntingZone(Zone zone)
{
    switch (zone) {
    case Zone::AncientForest:
    case Zone::WildspireWaste:
    case Zone::CoralHighlands:
    case Zone::RottenVale:
    case Zone::EldersRecess:
    case Zone::GreatRavine:
    case Zone::GreatRavine2:
    case Zone::HoarfrostReach:
    case Zone::GuidingLands:
    case Zone::SpecialArena:
    case Zone::Arena:
    case Zone::SelianaSupplyCache:
    case Zone::Introduction:
    case Zone::Everstream:
    case Zone::ConfluenceOfFates:
    case Zone::AncientForest2:
    case Zone::CavernsOfElDorado:
    case Zone::SelianaSupplyCache2:
    case Zone::OriginIsle:
    case Zone::OriginIsle2:
    case Zone::SecludedValley:
    case Zone::SecludedValley2:
    case Zone::CastleSchrade:
    case Zone::TrainingArea:
    case Zone::ChamberOfFive:
        return true;
    default:
        return false;
    }
}

inline bool isPeaceZone(Zone zone)
{
    switch (zone) {
    case Zone::Astera:
    case Zone::AsteraGatheringHub:
    case Zone::ResearchBase:
    case Zone::Seliana:
    case Zone::SelianaGatheringHub:
    case Zone::LivingQuarters:
    case Zone::PrivateQuarters:
    case Zone::PrivateSuite:
    case Zone::SelianaRoom:
        return true;
    default:
        return false;
    }
}

} // namespace mhw
