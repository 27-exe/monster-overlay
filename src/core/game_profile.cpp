// SPDX-License-Identifier: Apache-2.0
#include "core/game_profile.h"

namespace mhw {

QString gameIdToString(GameId game)
{
    // A switch, not a ternary: adding a third game must not silently compile
    // by falling through to the other branch. The compiler will now point at
    // this line when the enum grows.
    switch (game) {
    case GameId::World:
        return QStringLiteral("world");
    case GameId::Rise:
        return QStringLiteral("rise");
    }
    return QStringLiteral("world");
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
