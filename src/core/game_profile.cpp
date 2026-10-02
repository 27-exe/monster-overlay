// SPDX-License-Identifier: Apache-2.0
#include "core/game_profile.h"

namespace mhw {

QString gameIdToString(GameId game)
{
    // A switch, not a ternary: adding a third game must not silently compile
    // by falling through to the other branch. The net that actually catches a
    // missing case is -Wswitch, which is a WARNING, not a compile error —
    // this tree carries no -Werror, so a new game builds by default and the
    // warning appears only in the build log. (The old ternary answered with
    // World's name and produced no warning at all.)
    switch (game) {
    case GameId::World:
        return QStringLiteral("world");
    case GameId::Rise:
        return QStringLiteral("rise");
    }
    // Every enumerator returns above. Without this the compiler warns that
    // control reaches the end of a non-void function even though it cannot
    // for a valid GameId. Unreachable rather than a fallback return — the
    // fallback `return QStringLiteral("world");` is what this switch
    // replaced: it would label a GameId nobody wired up as "world". An
    // invalid value is undefined behaviour the caller already owns.
    __builtin_unreachable();
}

bool gameIdFromString(const QString &text, GameId *out)
{
    if (!out)
        return false;
    // Compare case-insensitively but emit the canonical spelling via
    // gameIdToString, so a persisted "Rise" normalises on the next write.
    const QString normalised = text.trimmed().toLower();
    if (normalised == QStringLiteral("world")) {
        *out = GameId::World;
        return true;
    }
    if (normalised == QStringLiteral("rise")) {
        *out = GameId::Rise;
        return true;
    }
    return false;
}

GameId gameIdFromStringOr(const QString &text, GameId fallback)
{
    GameId parsed = fallback;
    if (gameIdFromString(text, &parsed))
        return parsed;
    return fallback;
}

} // namespace mhw
