// SPDX-License-Identifier: Apache-2.0
#include "core/game_tables.h"

#include <array>

namespace mhw {
namespace {

// One entry per game. A game that has not registered is simply absent, and
// the queries below answer "no data" for it.
//
// The `registered` flags are load-bearing: without them, a game that
// registered an *empty* table would be indistinguishable from a game that
// never registered at all. Both answer nullptr today, but only the second
// is a state worth being able to assert about — see kRegistrationOrder.
struct Tables {
    bool hasCrowns{false};
    const QHash<int, std::array<float, 3>> *crowns{nullptr};
    bool hasCapture{false};
    const QHash<int, int> *captureThresholds{nullptr};
    const QSet<int> *notCapturable{nullptr};
    bool hasAilments{false};
    const QHash<int, AilmentStyle> *ailmentStyles{nullptr};
};

Tables &tableFor(GameId game)
{
    // A switch, not an array indexed by the enum: adding a third game is
    // caught here by -Wswitch, which is a WARNING, not a compile error —
    // this tree carries no -Werror, so a new game builds by default and
    // announces itself only in the build log. (The old ternary had the
    // opposite property — it compiled and answered with World data, which
    // is how a Rise id could pick up a World threshold.)
    static Tables world;
    static Tables rise;
    switch (game) {
    case GameId::World:
        return world;
    case GameId::Rise:
        return rise;
    }
    // Every enumerator returns above. Without this, the compiler warns that
    // control reaches the end of a non-void function even though it cannot
    // for a valid GameId. Unreachable rather than a fallback return — a
    // fallback `return world;` would silently answer with World storage for
    // a GameId nobody wired up here, which is exactly the failure this
    // switch replaced. An invalid value is undefined behaviour the caller
    // already owns.
    __builtin_unreachable();
}

} // namespace

CrownTables crownTablesFor(GameId game)
{
    const Tables &t = tableFor(game);
    return CrownTables{t.hasCrowns ? t.crowns : nullptr};
}

CaptureTables captureTablesFor(GameId game)
{
    const Tables &t = tableFor(game);
    return CaptureTables{t.hasCapture ? t.captureThresholds : nullptr,
                         t.notCapturable};
}

const std::array<float, 3> *crownThresholdsFor(GameId game, int monsterId)
{
    const CrownTables tables = crownTablesFor(game);
    if (!tables.byMonster)
        return nullptr;
    const auto it = tables.byMonster->constFind(monsterId);
    return it == tables.byMonster->cend() ? nullptr : &it.value();
}

std::optional<int> captureThresholdFor(GameId game, int monsterId)
{
    const CaptureTables rules = captureTablesFor(game);
    if (!rules.thresholdByMonster && !rules.notCapturable)
        return std::nullopt;

    // The not-capturable set wins when present: a game that publishes it
    // (Rise) is telling us the answer outright, and its threshold table is
    // absent. When a game publishes thresholds instead (World), the set is
    // empty and the table answers. This preserves the pre-S3 behaviour of
    // the two separate inline functions exactly, including the difference
    // that made "just add an arm" wrong: Rise's answer for an unknown id is
    // nullopt, and so is World's, but they get there by different routes.
    if (rules.notCapturable && rules.notCapturable->contains(monsterId))
        return 0;
    if (!rules.thresholdByMonster)
        return std::nullopt;
    const auto it = rules.thresholdByMonster->constFind(monsterId);
    return it == rules.thresholdByMonster->cend()
        ? std::nullopt
        : std::optional<int>{it.value()};
}

void registerCrownThresholds(GameId game, const QHash<int, std::array<float, 3>> &table)
{
    Tables &t = tableFor(game);
    t.crowns = &table;
    t.hasCrowns = true;
}

void registerAilmentStyles(GameId game, const QHash<int, AilmentStyle> &styles)
{
    Tables &t = tableFor(game);
    t.ailmentStyles = &styles;
    t.hasAilments = true;
}

AilmentStyle ailmentStyle(GameId game, int ailmentId)
{
    // The neutral grey the old switch used as its `default:` in both arms.
    // Keeping it here means an unknown id looks the same as before rather
    // than inheriting another game's palette.
    static const AilmentStyle kUnknown{0xFF9E9E9EU, QString()};

    const Tables &t = tableFor(game);
    if (!t.hasAilments || !t.ailmentStyles)
        return kUnknown;
    const auto it = t.ailmentStyles->constFind(ailmentId);
    return it == t.ailmentStyles->cend() ? kUnknown : it.value();
}

void registerCaptureRules(GameId game, const QHash<int, int> *thresholdByMonster,
                          const QSet<int> &notCapturable)
{
    Tables &t = tableFor(game);
    t.captureThresholds = thresholdByMonster;
    t.notCapturable = &notCapturable;
    t.hasCapture = true;
}

} // namespace mhw
