// SPDX-License-Identifier: Apache-2.0
#pragma once

// panel_settings_game.h — which game a Panel's settings section belongs to.
//
// WHY THIS EXISTS
// ---------------
// Every panel's persisted layout used to live in ONE flat namespace:
// `~/.config/monster-overlay/panels.ini` had a single `[player-status]`
// section, a single `[player-wirebug]` section, and no way to say which
// game they were tuned for. The user found the consequence immediately:
// one preview showed 衣装 (World-only) and 翔虫 (Rise-only) side by side,
// because the two games' demo data landed in the same section and the
// split windows both read it.
//
// So the layout namespace is now PER GAME: the same `player-status` key
// resolves to `[world/player-status]` under World and
// `[rise/player-status]` under Rise, in the SAME file. The two games
// cannot read or overwrite each other's window geometry, scale, margins,
// floors, columns dial or lock.
//
// WHY GROUP PREFIXING AND NOT ONE FILE PER GAME
// ---------------------------------------------
// File-per-game (`panels-world.ini` / `panels-rise.ini`) was the
// alternative and it loses on three counts:
//
//   1. It doubles the migration surface. A user who has BOTH games'
//      settings today (they are the same settings, by construction) has
//      to decide which file each section belongs in — a question the
//      code can answer and a user cannot, and one that silently loses
//      whichever answer is wrong.
//   2. QSettings caches by (organization, application, format). Two
//      different application names means two independent cache entries
//      and two flush-on-destruction lifecycles writing the same
//      directory; the existing code already carries three duplicated
//      QSettings constructions (panel.cpp, panel_player_split.cpp,
//      panel_player_split_wirebug.cpp,
//      panel_player_split_mantles.cpp) precisely because each TU has to
//      open its own handle to the same file. Prefixing keeps that to one
//      file and one handle.
//   3. Single-file keeps the file hand-editable: the alternatives make a
//      "which windows do I have for Rise?" question two greps.
//
// Group prefixing also keeps the MIGRATION mechanical. Every section key
// is already a Panel constructor argument (`settingsKey`), so prefixing
// is one string concatenation at the two places that build a group name,
// and nothing else in the tree changes.
//
// WHY THE GAME IS INJECTED AND NOT ASKED OF Panel
// -----------------------------------------------
// `Panel` is the base class for all ten windows. It deliberately knows
// nothing about a GameSnapshot, a reader, or which process is running —
// it persists a corner, a scale and a few margins. Handing it a GameId
// member would make every window (including the four that are
// game-agnostic today) carry a game it never reads.
//
// So the game is a launch-time FACT, set once before any panel is
// constructed: main() resolves it from --game / process detection, calls
// mhw::setPanelSettingsGame(gameId), and every Panel built afterwards
// reads it. The overlay constructs all ten windows in one burst before
// the event loop starts (src/main.cpp), so a one-shot injection is
// sufficient and there is no "game changed underneath a live panel"
// case to handle: the reader is immutable for the process lifetime and
// switching games restarts the process.
//
// The default when nothing injected a game is World. That is not "World
// is the default game" — it is "an unset game resolves to the game the
// un-prefixed legacy sections belong to", which is what keeps a
// pre-v0.13 file readable at all (see below).

#include <QSettings>
#include <QString>

#include "monster/monster_types.h"

namespace mhw {

// The settings group prefix under which a game's panels live.
// Result is "world" or "rise" — the same canonical spelling
// gameIdToString() produces and the config file stores.
[[nodiscard]] QString panelSettingsGameGroup(GameId game);

// Resolve a panel's settings key into its game-scoped group path:
// "player-status" -> "world/player-status".
//
// This is the ONE place the concatenation is spelled. Every writer and
// every reader of a panel section goes through it, so the prefix and
// the separator cannot drift between the base class and the four split
// windows that open their own QSettings handle.
[[nodiscard]] QString panelSettingsGroupFor(GameId game, const QString &settingsKey);

// Set the game whose panels namespace this process reads and writes.
//
// Call ONCE, before the first Panel is constructed. Calling it again
// after panels exist re-points the namespace underneath live windows,
// which is why it is not a setter on Panel: nothing in the UI has a
// legitimate reason to call it mid-session.
void setPanelSettingsGame(GameId game);

// The game this process is persisting into. World until
// setPanelSettingsGame() says otherwise.
[[nodiscard]] GameId panelSettingsGame();

// ─── the one-time legacy migration ─────────────────────────────────────
//
// A panels.ini written before the namespace was per-game has one FLAT
// section per window: `[player-status]`, `[player-wirebug]`, with no
// game anywhere in the spelling. Those sections mean today exactly what
// they meant then — scale, margins, floors, columns — so they are copied
// verbatim into the current game's namespace rather than abandoned.
//
// This function is the whole migration, and it takes the settings
// handle rather than opening its own so it can be driven from a test
// against a throwaway file. The production caller is
// Panel::loadConfig() (src/ui/panel.cpp), which opens the same handle
// and calls this once per panel section before reading it.
//
// The four guarantees, each pinned by
// tests/panel_settings_game_scoping_tests.cpp:
//
//   1. ONLY the current game's namespace is written. The other game's
//      sections are never read, never rewritten, never removed.
//   2. A section that already exists under the current game's prefix is
//      authoritative and is NOT overwritten — the legacy section is left
//      alone instead. Re-copying would destroy a layout the user has
//      edited for this game since migrating.
//   3. It is idempotent. A marker (`legacyMigrated=1`) under the copied
//      section makes a second call a no-op, so a repeated run cannot
//      re-import a section the user has since changed.
//   4. It takes the handle as a parameter and performs no I/O of its
//      own, so it can never open the developer's real config from a
//      test.
//
// Returns true when a migration actually happened (the log line in the
// caller is gated on it, so a clean run prints nothing).
bool migrateLegacyPanelSection(QSettings &settings, GameId game,
                               const QString &settingsKey);

} // namespace mhw
