// SPDX-License-Identifier: Apache-2.0
//
// The per-game reader configuration lives in its own translation unit so it
// can be tested without linking a reader.
//
// reader_factory.cpp, next door, is the assembly point: it names MhwReader
// and MhrReader and builds one. Keeping the descriptors here means "which
// map does this game expect" is answerable — and testable — without pulling
// in the reader chain, which is the same separation the S3 library split
// establishes across the tree.

#include "reader_factory.h"

namespace mhw {
namespace {

// A switch rather than an array indexed by the enum: a new GameId fails to
// compile here instead of silently reusing World's map file. This is the
// single place a third game must be added, and it is a compile error if
// that is forgotten.
constexpr GameReaderDescriptor kWorld{"MonsterHunterWorld.421810.map",
                                      MapSelection::FirstLoadable};
constexpr GameReaderDescriptor kRise{"MonsterHunterRise.16.0.2.0.map",
                                     MapSelection::BestWithProbe};

const GameReaderDescriptor &descriptorFor(GameId game)
{
    switch (game) {
    case GameId::World:
        return kWorld;
    case GameId::Rise:
        return kRise;
    }
    return kWorld;
}

} // namespace

QString mapFileNameFor(GameId game)
{
    return QString::fromLatin1(descriptorFor(game).mapFileName);
}

MapSelection mapSelectionFor(GameId game)
{
    return descriptorFor(game).mapSelection;
}

} // namespace mhw
