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

// A new GameId must add a row here.
//
// What actually stops a new game slipping through, and what does not: the
// static_assert below compares the row count against a LITERAL, so it cannot
// fail — adding a GameId enumerator leaves this array at two rows, the assert
// passes, and the array-index form that preceded this switch answered with
// row 0: World's display name and stamina divisor, for a game nobody
// registered.
//
// What does work is the switch below, with no default. A missing case is a
// -Wswitch warning. That is a WARNING, not a compile error — this tree
// deliberately carries no -Werror, so a new game builds by default and
// announces itself only in the build log. An earlier revision of this
// comment claimed the coverage was derived from the enum; the audit measured
// it and the claim was false.
static_assert(std::size(kTraits) == 2,
              "the traits table and this assert are both edited when a game is added");

// A switch, not an index into kTraits. The array form silently answered an
// unknown game with row 0 (World's name and stamina divisor), and the
// static_assert above cannot catch it — it compares against the row total,
// which is unchanged by adding an enum value. A switch with no default is
// covered by -Wswitch instead, which is the same mechanism the four other
// per-game switches in this tree rely on.
const GameTraits &gameTraits(GameId game)
{
    switch (game) {
    case GameId::World:
        return kTraits[0];
    case GameId::Rise:
        return kTraits[1];
    }
    // Every enumerator returns above. Without this, GCC warns that control
    // reaches the end of a non-void function even though it cannot: the
    // switch is exhaustive for a valid GameId, and an invalid one is
    // undefined behaviour the caller already owns. Unreachable rather than a
    // fallback return — a fallback would be the silent World default again.
    __builtin_unreachable();
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
