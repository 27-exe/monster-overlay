// SPDX-License-Identifier: Apache-2.0
//
// Reader factory tests.
//
// The factory is the one place that knows every game, so it is exactly the
// place a new game could be forgotten. These tests pin the parts that are
// pure decisions — which map file a game expects, and how its map is chosen —
// without loading a map or touching a game process.
//
// Deliberately NOT tested here: createReader() actually constructing a
// reader. That needs an offset map on disk and a live process, and the
// construction path is already covered end to end by the two game paths in
// main() (which the --game world / --game rise smoke runs exercise).

#include "reader_factory.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    } else {
        std::printf("PASS: %s\n", what);
    }
}

} // namespace

int main(int, char **)
{
    using namespace mhw;

    // The map file name is the thing a new game has to supply, and getting
    // it wrong means an empty panel with no explanation. Pin both current
    // values rather than deriving them, so a change to either is deliberate.
    check(mapFileNameFor(GameId::World) == QStringLiteral("MonsterHunterWorld.421810.map"),
          "World's offset map keeps its exact file name");
    check(mapFileNameFor(GameId::Rise) == QStringLiteral("MonsterHunterRise.16.0.2.0.map"),
          "Rise's offset map keeps its exact file name");

    // The two games select their map differently and the difference is load
    // bearing: World tries each candidate until one parses, Rise prefers a
    // live probe because its map's build number moves with every patch.
    // Collapsing them to one rule would silently change which map is picked.
    check(mapSelectionFor(GameId::World) == MapSelection::FirstLoadable,
          "World selects the first loadable candidate");
    check(mapSelectionFor(GameId::Rise) == MapSelection::BestWithProbe,
          "Rise prefers the live probe before falling back");

    // The two answers must never be the same string: a factory that returned
    // World's file for both games would load a World offset map into a Rise
    // session and produce garbage reads rather than an error.
    check(mapFileNameFor(GameId::World) != mapFileNameFor(GameId::Rise),
          "the games do not share an offset map file name");

    std::printf("%s (%d failure(s))\n", failures == 0 ? "PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
