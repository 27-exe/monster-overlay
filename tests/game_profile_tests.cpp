// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the shared game-identity vocabulary.
//
// The wire spelling is a format, not a translation: it reaches a command line
// and a QSettings file. These tests pin both directions so a rename cannot
// silently break a persisted setting or the overlay subprocess argument.

#include "core/game_profile.h"

#include <QString>

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void roundTrips(mhw::GameId game)
{
    const QString text = mhw::gameIdToString(game);
    mhw::GameId back = mhw::GameId::World;
    check(mhw::gameIdFromString(text, &back),
          "canonical spelling parses back");
    check(back == game, "round trip preserves the game");
}

} // namespace

int main()
{
    using mhw::GameId;

    // The two games are peers: both spellings are first-class.
    check(mhw::gameIdToString(GameId::World) == QStringLiteral("world"),
          "World spells as world");
    check(mhw::gameIdToString(GameId::Rise) == QStringLiteral("rise"),
          "Rise spells as rise");

    roundTrips(GameId::World);
    roundTrips(GameId::Rise);

    // Input is forgiving, output is canonical.
    GameId parsed = GameId::World;
    check(mhw::gameIdFromString(QStringLiteral("RISE"), &parsed)
              && parsed == GameId::Rise,
          "upper-case input parses");
    check(mhw::gameIdFromString(QStringLiteral("  World  "), &parsed)
              && parsed == GameId::World,
          "surrounding whitespace is trimmed");

    // Anything else is a failure, not a silent default. A corrupt settings
    // value must reach the caller's else-branch instead of quietly selecting
    // a game the player did not choose.
    check(!mhw::gameIdFromString(QStringLiteral("wilds"), &parsed),
          "unknown spelling is rejected");
    check(!mhw::gameIdFromString(QString(), &parsed),
          "empty string is rejected");
    check(!mhw::gameIdFromString(QStringLiteral("auto"), &parsed),
          "auto is not a game id");

    // A rejected parse must leave the caller's value alone, so a later
    // fallback decision sees exactly what it started with.
    parsed = GameId::Rise;
    (void)mhw::gameIdFromString(QStringLiteral("nonsense"), &parsed);
    check(parsed == GameId::Rise, "a failed parse does not clobber the out param");

    check(mhw::gameIdFromStringOr(QStringLiteral("nope"), GameId::Rise)
              == GameId::Rise,
          "the Or-variant falls back on rejection");
    check(mhw::gameIdFromStringOr(QStringLiteral("rise"), GameId::World)
              == GameId::Rise,
          "the Or-variant honours a valid value");

    check(!mhw::gameIdFromString(QStringLiteral("world"), nullptr),
          "a null out pointer is rejected, not dereferenced");

    if (failures == 0)
        std::printf("all game-profile cases passed\n");
    return failures == 0 ? 0 : 1;
}
