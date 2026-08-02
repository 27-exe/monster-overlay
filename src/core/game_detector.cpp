#include "core/game_detector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>

namespace mhw {

namespace {
constexpr const char *kWorldExe = "monsterhunterworld.exe";
constexpr const char *kRiseExe = "monsterhunterrise.exe";
} // namespace

std::optional<GameDetection> detectGame()
{
    std::optional<GameDetection> world;
    std::optional<GameDetection> rise;

    QDir proc(QStringLiteral("/proc"));
    const QFileInfoList entries = proc.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (world && rise)
            break;

        bool numeric = false;
        const qint64 pid = entry.fileName().toLongLong(&numeric);
        if (!numeric || pid <= 0)
            continue;

        QFile maps(entry.filePath() + QStringLiteral("/maps"));
        if (!maps.open(QIODevice::ReadOnly))
            continue;
        const QByteArray raw = maps.readAll().toLower();

        if (!world && raw.contains(kWorldExe)) {
            world = GameDetection{GameId::World, pid, QString::fromLatin1(kWorldExe)};
        }
        if (!rise && raw.contains(kRiseExe)) {
            rise = GameDetection{GameId::Rise, pid, QString::fromLatin1(kRiseExe)};
        }
    }

    if (world)
        return world;
    return rise;
}

} // namespace mhw
