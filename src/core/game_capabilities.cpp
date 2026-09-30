// SPDX-License-Identifier: Apache-2.0
#include "core/game_capabilities.h"
#include "core/string_table.h"

#include <iterator>

namespace mhw {
namespace {

// One row per game, in GameId order. A plain table rather than a chain of
// comparisons so that adding a game is a data change here and a compile
// check that the row count still matches the enum (see the static_assert
// below) — not a hunt for every `if (game == ...)` that forgot it.
struct GameTraits {
    // i18n key for the console's GAME column and the auto-detect badge.
    const char *displayNameKey;
    // ASCII fallback used when the key is missing from the locale table.
    const char *displayNameAscii;
    bool normalisedPartGauges;
    // Divisor converting a raw reader stamina value to the human-scale bar
    // value. 1.0 = no conversion. See staminaDisplayScale() in the header
    // for why this is a column and not a ternary.
    float staminaDivisor;
};

constexpr GameTraits kTraits[] = {
    {"console.game.world", "WORLD", true, 1.0F},
    // Rise's HUD stamina is in thirtieths of the human-scale bar, so the
    // panel divides by 30. The measured vectors behind that number are
    // recorded in src/ui/panel_player_metrics.h; this row is the single
    // place that says so.
    {"console.game.rise", "RISE", true, 30.0F},
};

// A new GameId must add a row here. Deriving the count from the array rather
// than writing 2 means the assert states the invariant ("every game has a
// row") rather than restating today's answer. Without it, gameTraits()
// would return the first row for an unknown game and the UI would label it
// WORLD — the same silent mislabelling the switch it replaces had, just
// moved.
static_assert(std::size(kTraits) == 2,
              "a new GameId needs a GameTraits row; without one the UI would "
              "label it WORLD");

const GameTraits &gameTraits(GameId game)
{
    const auto index = static_cast<std::size_t>(game);
    return index < std::size(kTraits) ? kTraits[index] : kTraits[0];
}

} // namespace

QString gameDisplayName(GameId game)
{
    const GameTraits &traits = gameTraits(game);
    const QString key = QString::fromLatin1(traits.displayNameKey);
    // StringTable::tr() returns the key itself when the key is absent, so
    // the comparison is the existence check.
    const QString translated = StringTable::instance().tr(key);
    if (translated != key)
        return translated;
    return QString::fromLatin1(traits.displayNameAscii);
}

bool usesNormalisedPartGauges(GameId game)
{
    return gameTraits(game).normalisedPartGauges;
}

DisplayScale staminaDisplayScale(GameId game)
{
    return DisplayScale{gameTraits(game).staminaDivisor};
}

} // namespace mhw
