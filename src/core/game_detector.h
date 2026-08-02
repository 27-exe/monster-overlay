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

// Scan /proc/[0-9]*/maps for a running Monster Hunter process. When both
// World and Rise are present, World wins. Returns nullopt when neither is
// found. Uses QFile (no popen).
std::optional<GameDetection> detectGame();

} // namespace mhw
