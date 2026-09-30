// SPDX-License-Identifier: Apache-2.0
//
// The assembly point: the one translation unit that knows both the
// IGameReader contract and every implementation of it.
//
// The per-game configuration is in reader_descriptors.cpp, which does not
// include either reader. This file includes both, so it is where "build a
// reader for this game" is answered, and where a third game is wired in.

#include "reader_factory.h"

#include "rise/mhr_reader.h"
#include "world/world_reader.h"

namespace mhw {

GameReaderPtr createReader(GameId game, const QString &explicitMap,
                           const QStringList &dataDirs)
{
    const QString mapFileName = mapFileNameFor(game);
    const QString fallback = developmentMapFallback(mapFileName);

    // A switch, not a descriptor-indexed array: a new GameId has to be
    // handled here by hand, because only this file knows the concrete
    // reader types. Forgetting is a compile error rather than a game that
    // silently gets no reader.
    switch (game) {
    case GameId::World: {
        const QStringList candidates =
            worldMapCandidates(explicitMap, mapFileName, dataDirs, fallback);
        return std::make_unique<MhwReader>(
            MhwReader::selectLoadableMap(candidates));
    }
    case GameId::Rise: {
        // Rise does not take the file name: riseMapCandidates matches the
        // versioned name by regex, because the build number moves with
        // every patch and a literal name would break each time.
        const QStringList candidates =
            riseMapCandidates(explicitMap, dataDirs, fallback);
        return std::make_unique<MhrReader>(MhrReader::findBestMap(candidates));
    }
    }
    return nullptr;
}

} // namespace mhw
