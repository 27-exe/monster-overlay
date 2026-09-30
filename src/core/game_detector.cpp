// SPDX-License-Identifier: Apache-2.0
#include "core/game_detector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>

namespace mhw {
namespace detail {

QList<QPair<GameId, QString>> defaultGameExecutables()
{
    // One row per shipping game, and the ONLY place this mapping is written
    // down. Adding a third game appends here; the scanner body and every call
    // site are untouched. Order is precedence, so an earlier row wins when
    // several are running — which is the World-beats-Rise rule the original
    // hardcoded `if (world) return world;` expressed.
    return {
        {GameId::World, QStringLiteral("monsterhunterworld.exe")},
        {GameId::Rise, QStringLiteral("monsterhunterrise.exe")},
    };
}

} // namespace detail

std::optional<GameDetection> detectGame()
{
    return detectGame(detail::defaultGameExecutables());
}

std::optional<GameDetection> detectGame(
    const QList<QPair<GameId, QString>> &executables)
{
    if (executables.isEmpty())
        return std::nullopt;

    // One slot per requested game, indexed rather than named, so a third game
    // is a row in the caller's list instead of another local here. An empty
    // optional means "not seen yet", which is what lets the scan stop early.
    QList<std::optional<GameDetection>> found(executables.size());
    int remaining = executables.size();

    QDir proc(QStringLiteral("/proc"));
    const QFileInfoList entries = proc.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (remaining == 0)
            break;

        bool numeric = false;
        const qint64 pid = entry.fileName().toLongLong(&numeric);
        if (!numeric || pid <= 0)
            continue;

        QFile maps(entry.filePath() + QStringLiteral("/maps"));
        if (!maps.open(QIODevice::ReadOnly))
            continue;
        const QByteArray raw = maps.readAll().toLower();

        for (int i = 0; i < executables.size(); ++i) {
            if (found[i].has_value())
                continue;
            const QString &exeName = executables[i].second;
            // `raw` came from maps.readAll().toLower(), so the needle is
            // lowercased too. These names are ASCII literals from the
            // caller, so toLower() on them is exact, not locale-folded.
            if (raw.contains(exeName.toLower().toLatin1())) {
                found[i] = GameDetection{executables[i].first, pid, exeName};
                --remaining;
            }
        }
    }

    // First match in the caller's order wins, so the caller decides which
    // game takes precedence when several are running.
    for (const auto &detection : found) {
        if (detection.has_value())
            return detection;
    }
    return std::nullopt;
}

} // namespace mhw
