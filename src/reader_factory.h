#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// reader_factory.h — builds a reader for a game.
//
// WHY THIS IS NOT IN core
// -----------------------
// Creating a reader needs that game's map file, that game's exe name and
// that game's candidate-path rules. A factory that lives in core would have
// to name all of them, which is precisely the coupling IGameReader exists to
// remove: core would go from "I can poll any game" back to "I know every
// game", and a third game would edit a core file again.
//
// So the factory sits above the games, next to main.cpp's use of them, and
// this is the ONE place a new game gets registered. Adding Monster Hunter
// Wilds means: a WildReader implementing IGameReader, a case here, and its
// own map data. Nothing in src/core/ changes.
//
// The header is split from the implementation on purpose: the per-game
// DESCRIPTORS below are pure data, and reader_factory.h exposes them without
// including either concrete reader. That keeps a test of "which map does
// each game expect" from having to link the whole reader chain — and it is
// the same separation the library split is trying to establish, applied
// within one file.

#include "core/game_reader.h"
#include "core/map_paths.h"
#include "monster/monster_types.h"

#include <QString>
#include <QStringList>

namespace mhw {

// How to find a game's offset map. Mirrors the difference between the two
// shipped games: World tries every candidate until one parses, Rise keeps
// probing a running game and otherwise takes the first that loads.
enum class MapSelection {
    FirstLoadable,  // MhwReader::selectLoadableMap
    BestWithProbe,  // MhrReader::findBestMap — live probe first
};

// One game's reader configuration. Everything here is data, so a new game
// is a new row rather than a new code path.
struct GameReaderDescriptor {
    // Offset map file name, e.g. MonsterHunterWorld.421810.map.
    const char *mapFileName;
    MapSelection mapSelection;
};

// The per-game answers, with no dependency on either concrete reader.
[[nodiscard]] QString mapFileNameFor(GameId game);
[[nodiscard]] MapSelection mapSelectionFor(GameId game);

// Builds the reader. `explicitMap` is the --map override, empty when
// absent. `dataDirs` is defaultDataSearchDirs().
//
// Defined in reader_factory.cpp, which is where the concrete reader types
// are named — the one translation unit in the program that knows both the
// contract and every implementation of it.
[[nodiscard]] GameReaderPtr createReader(GameId game, const QString &explicitMap,
                                         const QStringList &dataDirs);

} // namespace mhw
