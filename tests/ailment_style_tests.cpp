// SPDX-License-Identifier: Apache-2.0
//
// Ailment-palette tests.
//
// The palette used to be two switches inside panel_monster.cpp:900, one per
// game, because the two id spaces are unrelated. These tests pin the part
// that a refactor can silently break: that a given id resolves to a
// DIFFERENT colour depending on the game.
//
// If someone ever "simplifies" this by keying one table on id alone, the
// World id 1 would be painted with whatever id 1 means in that table —
// which is poison in World and sleep in Rise. No compiler would complain and
// no existing test would notice; these assertions are the only thing that
// would.

#include "core/game_tables.h"

#include <cstdio>

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

constexpr quint32 kPoison = 0xFFA74FFFU;   // #a74fff
constexpr quint32 kParalysis = 0xFFF6A522U; // #f6a522
constexpr quint32 kNeutral = 0xFF9E9E9EU;   // #9e9e9e

} // namespace

int main(int, char **)
{
    using namespace mhw;
    registerWorldTables();
    registerRiseTables();

    // The same id must resolve to different colours per game. These four
    // pairs are the whole hazard: 1/2/4/0 all differ between the tables.
    check(ailmentStyle(GameId::World, 1).statusColorRgba == kPoison,
          "World id 1 is poison (#a74fff)");
    check(ailmentStyle(GameId::Rise, 1).statusColorRgba != kPoison,
          "Rise id 1 is NOT poison — a shared id-keyed table would be wrong");
    check(ailmentStyle(GameId::World, 2).statusColorRgba == kParalysis,
          "World id 2 is paralysis (#f6a522)");
    check(ailmentStyle(GameId::Rise, 0).statusColorRgba == kParalysis,
          "Rise id 0 is paralysis — the same colour lives at a different id");

    // The trap icons moved with the palette and are keyed by the same
    // per-game ids: World 14 is the paralysis trap, Rise 13 is the same
    // trap. Losing these silently drops an icon from the card.
    check(ailmentStyle(GameId::World, 14).trapIconPath
              == QStringLiteral(":/icons/Traps/item_id_81.svg"),
          "World id 14 keeps its paralysis-trap icon");
    check(ailmentStyle(GameId::Rise, 13).trapIconPath
              == QStringLiteral(":/icons/Traps/item_id_81.svg"),
          "Rise id 13 keeps the same icon at its own id");
    check(ailmentStyle(GameId::World, 5).trapIconPath.isEmpty(),
          "a non-trap ailment carries no icon");

    // An id neither table knows answers the neutral grey, exactly as the
    // old switches' `default:` did. It must NOT fall through to another
    // game's palette.
    check(ailmentStyle(GameId::World, 99).statusColorRgba == kNeutral,
          "an unknown World id answers the neutral grey");
    check(ailmentStyle(GameId::Rise, 99).statusColorRgba == kNeutral,
          "an unknown Rise id answers the neutral grey");
    check(ailmentStyle(GameId::Rise, 99).trapIconPath.isEmpty(),
          "an unknown id carries no icon");

    std::printf("%s (%d failure(s))\n", failures == 0 ? "PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
