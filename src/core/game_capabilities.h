#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// game_capabilities.h — what each game supports, asked as a question.
//
// WHY THIS EXISTS
// ---------------
// The UI carried 38 `game == GameId::X` comparisons. Most were legitimate —
// a Rise-only card, a World-only gauge — and stay. The ones that were not
// fall into two shapes, and both are answered here:
//
//   1. Game-specific DISPLAY NAMES. console_text_helpers.cpp:38 was
//      `id == Rise ? "console.game.rise" : "console.game.world"` — a
//      two-way binary where a third game has no arm at all, so it renders as
//      "WORLD". A name is data, not a branch.
//
//   2. Behaviour that happens to be IDENTICAL for every game today.
//      monster_view_model.cpp:92 is the clearest case: its `case Rise` and
//      `case World` bodies are line-for-line the same, and a third game
//      lands in `default`, which takes a different path through
//      partHealthForDisplay. Identical arms are not "two games agreed" —
//      they are one behaviour that has not been factored out yet, and the
//      switch is what hides that.
//
// What this is NOT: a place to move the Rise-only and World-only branches.
// A feature that only one game has is correctly written as a feature check;
// it is not a capability, and pretending otherwise trades an honest
// `if (game == GameId::Rise)` for an indirection that hides the same fact.
// AXIS-B measured 24 of the 38 branches as exactly that, and they stay
// as they are.
//
// The failure mode this replaces is silent: adding GameId::Wilds compiled
// clean and every comparison fell into an existing arm, because the two
// switches that exist have defaults and the tree carries no -Werror.

#include "monster/monster_types.h"

#include <QString>

namespace mhw {

// How a game is written in the UI. Resolved through the locale table, with
// the canonical ASCII spelling as the fallback so a missing translation
// shows a usable name rather than the raw i18n key.
[[nodiscard]] QString gameDisplayName(GameId game);

// Whether a game's part gauges are driven by the shared Severable/Breakable
// normalisation (readers fill PartSnapshot::health/maxHealth) or need the
// raw per-part values. True for both shipped games today, which is exactly
// why monster_view_model.cpp:92 had two identical arms: they were one
// behaviour written twice, and the `default` arm was a third path that no
// game had ever taken.
[[nodiscard]] bool usesNormalisedPartGauges(GameId game);

} // namespace mhw
