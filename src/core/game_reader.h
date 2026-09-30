#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// game_reader.h — the one thing main.cpp needs from a game.
//
// WHY THIS EXISTS
// ---------------
// main.cpp held two readers of unrelated types behind a std::function:
//
//     std::function<GameSnapshot()> pollGame =
//         isRise ? std::function<GameSnapshot()>([&riseReader] { return riseReader->poll(); })
//                : std::function<GameSnapshot()>([&worldReader] { return worldReader->poll(); });
//
// The lambdas type-erased the difference, which made the call site
// game-agnostic while the wiring stayed game-specific: `isRise` chose which
// object to build, which map to search, which damage feed to create, and
// which lambda to install. A third game meant another arm in all four
// places, and nothing failed when one was missed — the process started, and
// the new game showed an empty panel.
//
// IGameReader is the contract those two classes already satisfied without
// saying so. MhwReader and MhrReader each expose poll() and mapPath(); that
// is the whole surface main.cpp uses, and everything else on either class
// is per-game detail (findBestMap, findRisePid, readZone) that belongs to
// whoever builds the reader, not to the loop that polls it.
//
// The loop is now written once against this interface. Registering a third
// game is adding a factory entry, not editing a ternary chain.

#include "core/game_snapshot.h"

#include <QString>

#include <memory>

namespace mhw {

class IGameReader {
public:
    virtual ~IGameReader() = default;

    // One poll cycle. Never throws, never blocks on anything but the single
    // game's own reads; a failure to attach is reported through the
    // returned snapshot's `status` and `attached` fields, not by signalling.
    [[nodiscard]] virtual GameSnapshot poll() = 0;

    // The offset map this reader loaded, for diagnostics. A reader that
    // failed to load still answers this — with the path it tried — because
    // that path is the only clue the user gets for "empty panel".
    [[nodiscard]] virtual const QString &mapPath() const = 0;
};

using GameReaderPtr = std::unique_ptr<IGameReader>;

} // namespace mhw
