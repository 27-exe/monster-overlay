// SPDX-License-Identifier: Apache-2.0
//
// The assembly point: the one translation unit that knows both the
// IGameReader contract and every implementation of it.
//
// The per-game configuration is in reader_descriptors.cpp, which does not
// include either reader. This file includes both, so it is where "build a
// reader for this game" is answered, and where a third game is wired in.
//
// The reader-factory-tests census entry used to be INDIRECT because the
// target's object file left `createReader` undefined: this TU was then
// compiled by the target itself. That coupling went away when the map
// candidate resolution was generalised — reader_factory_tests.cpp now
// exercises the descriptors and nothing here, so the census measures this
// TU as ZERO until a test links it deliberately (see the census gate).

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
            versionedMapCandidates(explicitMap, QStringLiteral("MonsterHunterRise"),
                                    dataDirs, fallback);
        return std::make_unique<MhrReader>(MhrReader::findBestMap(candidates));
    }
    }
    return nullptr;
}

} // namespace mhw
