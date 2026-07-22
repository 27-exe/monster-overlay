#pragma once

#include <QString>
#include <QVector>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mhw {

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

struct GameSnapshot {
    bool attached{};
    qint64 pid{-1};
    std::uintptr_t imageBase{};
    QString status;
    QVector<MonsterSnapshot> monsters;
    PlayerSnapshot player;
    QVector<PartyMemberSnapshot> party;
    QuestSnapshot quest;
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
    QVector<MonsterSnapshot> readMonsters(QString *error);
    PlayerSnapshot readPlayer(QString *error);
    QVector<PartyMemberSnapshot> readParty(QString *error);
    QuestSnapshot readQuest(QString *error);

    AddressMap map_;
    ProcessMemory memory_;
    QString mapPath_;
    QString mapError_;
    std::uintptr_t imageBase_{};
};

} // namespace mhw
