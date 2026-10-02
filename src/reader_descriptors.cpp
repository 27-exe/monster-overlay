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

// A switch rather than an array indexed by the enum: adding a GameId has to
// be visible here, where a third game's map file name is decided. Note the
// honest limit of that protection — this tree carries no -Werror, so a new
// enumerator produces a -Wswitch *warning* in the build log, not a compile
// error, and the build still succeeds. The switch after the cases therefore
// ends in __builtin_unreachable() rather than a fallback return: a
// `return kWorld` here would hand a new game World's map file and parse its
// structures with the wrong reader, which is the silent failure this file
// exists to prevent.
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
    // Every enumerator returns above. Reaching here means the caller passed a
    // GameId this file does not know about, which is undefined behaviour the
    // caller owns — not a case to answer with World's map file.
    __builtin_unreachable();
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
