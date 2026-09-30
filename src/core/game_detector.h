#pragma once

#include "core/game_snapshot.h"

#include <QString>
#include <optional>

namespace mhw {

struct GameDetection {
    GameId game{GameId::World};
    qint64 pid{-1};
    QString exeName;
};

// Scan /proc/[0-9]*/maps for a running Monster Hunter process. Returns nullopt
// when none of the shipped games is running. Uses QFile (no popen).
//
// Which games count is `executables` below, not code inside the scanner: a
// third game appends a row to defaultGameExecutables() and this file is done.
// Precedence is list order, so World keeps winning when both are running.
std::optional<GameDetection> detectGame();

// The same scan over an explicit game list, for callers that need to decide
// which games count (or their precedence) themselves.
std::optional<GameDetection> detectGame(
    const QList<QPair<GameId, QString>> &executables);

namespace detail {
// The shipped games paired with the executable name each one's Proton prefix
// maps, in precedence order. One definition shared by every call site rather
// than a copy of the list inside each of them.
[[nodiscard]] QList<QPair<GameId, QString>> defaultGameExecutables();
} // namespace detail

} // namespace mhw
