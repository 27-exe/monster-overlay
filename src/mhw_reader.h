#pragma once

#include "core/game_snapshot.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mhw {

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
    void readMonsterAilments(MonsterSnapshot &monster);
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