#pragma once

// Game identity: the one place that knows how a game is named.
//
// v0.11.2: nine call sites used to spell "rise"/"world" inline, each as its
// own ternary, so the wire format had no single definition and the console,
// the overlay entry point and the process controller could drift apart. This
// header is that single definition.
//
// The two games are PEERS: nothing here treats World as the default and Rise
// as the exception. gameIdFromString() is total over the wire vocabulary and
// reports failure explicitly rather than silently falling back.

#include <QString>

#include "monster/monster_types.h"

namespace mhw {

// The canonical wire spelling, used for the CLI --game flag, the persisted
// QSettings value and the overlay subprocess argument. Lower case, stable:
// these strings are written to disk and passed on a command line, so they are
// a format, not a translation.
[[nodiscard]] QString gameIdToString(GameId game);

// Parses the canonical spelling. Returns false for anything else, including
// an empty string, so a corrupt settings value cannot silently select a game.
// Case-insensitive on input; `ok` is cleared first so a caller can pass the
// same variable across several attempts.
[[nodiscard]] bool gameIdFromString(const QString &text, GameId *out);

// Convenience for call sites that have a sensible fallback (e.g. "default to
// whatever the game detector said"). Prefer the two-argument form wherever an
// unrecognised value would indicate real corruption.
[[nodiscard]] GameId gameIdFromStringOr(const QString &text, GameId fallback);

} // namespace mhw
