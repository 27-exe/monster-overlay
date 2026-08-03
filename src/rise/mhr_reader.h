#pragma once

#include "mhw_reader.h"
#include "rise/mhr_types.h"

#include <QString>
#include <QVector>
#include <optional>

namespace mhw {

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

    // Scan dataDir for MonsterHunterRise.X.Y.Z.W.map files, newest first,
    // and return the first one that resolves a live monster (HP > 0) from
    // MONSTERS_ADDRESS. Falls back to the newest map when the game isn't
    // running or no candidate validates. Empty if dataDir has no maps.
    [[nodiscard]] static QString findBestMap(const QString &dataDir);

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
    void readMonsterAilments(std::uintptr_t monster, MonsterSnapshot &snapshot);
    void readMonsterQurio(std::uintptr_t monster, MonsterSnapshot &snapshot);
    [[nodiscard]] std::uintptr_t readLockOnTarget() const;
    PlayerSnapshot readPlayer(QString *error);
    void readWirebugs(PlayerSnapshot &snapshot, QString *error);
    QuestSnapshot readQuest(QString *error);
    [[nodiscard]] QString readUtf16(std::uintptr_t address, int length) const;

    AddressMap map_;
    ProcessMemory memory_;
    QString mapPath_;
    QString mapError_;
    std::uintptr_t imageBase_ = 0;
};

} // namespace mhw
