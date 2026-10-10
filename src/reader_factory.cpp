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

#include "monster/monster_types.h"

#include <QDebug>

#include "rise/mhr_reader.h"
#include "world/world_reader.h"

namespace mhw {

namespace {
// THE TABLE REGISTRY HAS TO BE PULLED IN EXPLICITLY.
//
// registerWorldTables() / registerRiseTables() each sit in their game's
// data TU and self-register through a file-scope `RegisterTablesAtStartup`
// object. That works for a program that compiles the TU directly, and it
// is what happened to the World tables — the core data TU is referenced for
// the part schemas, so its .init_array entry survives.
//
// Rise's data TU is referenced only for kRiseAilmentNames, and the linker
// keeps what a static-library member is pulled in for at function
// granularity. Measured on monster-overlay:
//
//     registerWorldTables()   in the binary
//     kWorldAilmentStyles     in the binary
//     registerRiseTables()    NOT in the binary
//     kRiseAilmentStyles      not even defined
//
// So registerRiseTables() never ran, hasAilments stayed false for Rise, and
// every ailment id missed the table and came back kUnknown — the neutral
// grey with no icon, which is 「异常状态全变成灰色框框」. Nothing warned: the
// lookup's "unknown id" arm is a legitimate answer, and a table that was
// never registered looks exactly like a table with no entry for that id.
//
// Calling it here is the fix that survives a new build flag: this TU is the
// assembly point — it already names both readers, so both games' tables
// belong to it by the same token that makes the readers part of the binary.
void registerAllTables()
{
    registerWorldTables();
    registerRiseTables();
}
} // namespace

GameReaderPtr createReader(GameId game, const QString &explicitMap,
                           const QStringList &dataDirs)
{
    registerAllTables();
    const QString mapFileName = mapFileNameFor(game);
    const QString fallback = developmentMapFallback(mapFileName);

    // A switch, not a descriptor-indexed array: a new GameId has to be
    // handled here by hand, because only this file knows the concrete
    // reader types. Forgetting is caught by -Wswitch, which is a WARNING,
    // not a compile error — this tree carries no -Werror, so a new game
    // builds by default and announces itself only in the build log. The
    // qWarning below is the second net: if a GameId ever reaches the end of
    // this switch, the log names it instead of the caller crashing on a
    // nullptr it cannot attribute.
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
    // Every enumerator returns above, so this is unreachable for a valid
    // GameId — see the note in core/game_capabilities.cpp. The value is
    // logged rather than dropped: with no static_name() on GameId, the
    // raw int is the only identifier the log can carry, and it is enough
    // to point at the missing arm. An invalid GameId is undefined behaviour
    // the caller already owns; this keeps the diagnostic reachable instead
    // of letting the compiler assume the path away.
    qWarning("createReader: unhandled GameId %d — add a case in "
             "src/reader_factory.cpp", static_cast<int>(game));
    return nullptr;
}

} // namespace mhw
