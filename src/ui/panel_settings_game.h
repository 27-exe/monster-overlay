// SPDX-License-Identifier: Apache-2.0
#pragma once

// panel_settings_game.h — which game, and which layout mode, a Panel's
// settings section belongs to.
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
// So the layout namespace became PER GAME: the same `player-status` key
// resolved to `[world/player-status]` under World and
// `[rise/player-status]` under Rise, in the SAME file. The two games
// could not read or overwrite each other's window geometry, scale,
// margins, floors, columns dial or lock.
//
// WHY ONE MORE AXIS — THE LAYOUT MODE (v0.14)
// --------------------------------------------
// The game axis fixed half the problem. The user found the other half
// immediately: 「分块和合并疑似是用的同一个配置文件？这不可取，双方布局
// 会互相影响，分开」.
//
// They were right, and the mechanism was exactly as suspected. The
// namespace had one dimension — the game — while the thing being stored
// has two: a window's GEOMETRY is a fact about the game AND about which
// window topology produced it. Under the one-dimensional spelling,
// `[rise/monster]` held `mt=212 mr=1130` (a single-mode position the
// user had dragged the large panel to) beside `[rise/player-bars]`
// `ml=2260` (a split-mode position for a window that only exists in
// split). Reading the game axis alone, both look like "Rise geometry",
// and neither can be used to answer the question the reader actually
// asks: "where was this WINDOW, in THIS MODE".
//
// So the namespace is now GAME × MODE, four domains in one file:
//
//     [world/single/player-status]     [world/split/player-status]
//     [rise/single/player-status]      [rise/split/player-status]
//
// The two modes of one game can no longer read or overwrite each other,
// and neither can they reach across games. A drag in the console's
// Rise split preview lands in `[rise/split/...]` and nothing else.
//
// WHY THE MODE IS A SEGMENT AND NOT A SEPARATE FILE
// --------------------------------------------------
// File-per-mode (`panels-split.ini` / `panels-single.ini`) loses on the
// same three counts file-per-game lost on, argued at length in the
// block below and not repeated here: migration surface, QSettings
// caching by (organization, application, format), and "which windows do
// I have?" becoming two greps. A segment costs one string
// concatenation at the one place the group name is spelled.
//
// WHY THE SCOPE IS INJECTED AND NOT ASKED OF Panel
// -------------------------------------------------
// `Panel` is the base class for all ten windows. It deliberately knows
// nothing about a GameSnapshot, a reader, or which process is running —
// it persists a corner, a scale and a few margins. Handing it a GameId
// member would make every window (including the ones that are
// game-agnostic) carry a game it never reads; handing it a layout mode
// would be worse still, because the mode is not a property of the panel
// at all — it is a property of the whole session's window topology.
//
// So BOTH axes are launch-time facts, set once before any panel is
// constructed: main() resolves the game from --game / process detection
// and the mode from --layout, calls setPanelSettingsGame() and
// setPanelSettingsLayout(), and every Panel built afterwards reads
// them. The overlay constructs all windows in one burst before the
// event loop starts (src/main.cpp), so one-shot injection is
// sufficient.
//
// THE MODE IS IMMUTABLE IN-PROCESS. `--layout` is read once at startup;
// nothing in a running overlay changes it. The CONSOLE does change its
// own mode at runtime, and that is not a contradiction: the console
// owns its preview windows (SplitWindowPreviewHostImpl) and rebuilds
// them from scratch whenever the mode or the game moves, so a fresh set
// of Panel constructors re-reads the (game, mode) pair each time. What
// no live Panel does is have the scope retargeted underneath it — that
// is the failure the v0.13 rail-switch bug taught, where re-pointing
// the global after construction left a window sitting on the geometry
// it had already loaded.

#include <QSettings>
#include <QString>

#include "monster/monster_types.h"
#include "ui/panel_sections.h"

namespace mhw {

// The layout-mode enum, used unqualified throughout this header. It
// lives in PlayerSection because that is where the mode's whole
// vocabulary is (parsePlayerLayoutMode, playerPanelMaskFor, ...), and
// the settings namespace is a CONSUMER of it rather than an owner.
// Pulling the name in here is what lets every signature below spell
// `PlayerLayoutMode` rather than the fully-qualified
// `PlayerSection::PlayerLayoutMode`, which is 20-odd characters of
// nesting for a type this file names far more often than the sections
// themselves.
using PlayerSection::PlayerLayoutMode;

// The settings group prefix under which a game's panels live.
// Result is "world" or "rise" — the same canonical spelling
// gameIdToString() produces and the config file stores.
[[nodiscard]] QString panelSettingsGameGroup(GameId game);

// Resolve a panel's settings key into its game-scoped group path:
// "player-status" -> "world/player-status".
//
// This is the ONE place the concatenation is spelled. Every writer and
// every reader of a panel section goes through it, so the prefix and
// the separator cannot drift between the base class and the split
// windows that open their own QSettings handle.
//
// TWO DIMENSIONS NOW, NOT ONE. `game` selects [world|rise] and `mode`
// selects [single|split]; together they name one of the four domains.
// The old single-axis spelling (game only) is gone from this signature
// because every caller had both values in hand and was dropping one.
[[nodiscard]] QString panelSettingsGroupFor(GameId game,
                                            PlayerLayoutMode mode,
                                            const QString &settingsKey);

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

// Set the layout mode whose panels namespace this process reads and
// writes. Startup-only, for the same reason as setPanelSettingsGame():
// the mode decides which windows exist, and a window's geometry is a
// fact about the mode it was created in.
//
// Split-mode and single-mode windows are DIFFERENT windows — the split
// carve-up exists only when the mode is Split — so a shared section
// would let one topology's position overwrite the other's. The user
// reported exactly that: 「双方布局会互相影响」.
void setPanelSettingsLayout(PlayerLayoutMode mode);

// The layout mode this process is persisting into. Single until
// setPanelSettingsLayout() says otherwise — because Single is the
// topology every pre-v0.14 user ran, and an unset scope must resolve
// to the domain their layout is already stored in.
[[nodiscard]] PlayerLayoutMode panelSettingsLayout();

// The mode segment: "single" or "split". Same spellings
// parsePlayerLayoutMode() accepts and the config file stores, so a
// hand-edited `[world/split/...]` matches what the code writes.
[[nodiscard]] QString panelLayoutModeSegment(PlayerLayoutMode mode);

// The two axes of one namespace domain, as one value. Grouped so a
// caller that has both reads them off one value rather than from two
// globals, and so the legacy-migration guard below can be handed
// exactly what was in force at the call site.
struct PanelSettingsScope {
    GameId game{GameId::World};
    PlayerLayoutMode mode{PlayerLayoutMode::Single};

    bool operator==(const PanelSettingsScope &) const = default;
};

// Resolve a panel's settings key into one of the four domains:
// "player-status" -> "world/single/player-status".
//
// The one place the two axes are combined. Panel::settingsGroup() and
// scopedKey() both route through here, and the three column-dial
// windows call Panel::settingsGroup() rather than repeating it — so
// "game / mode / key" is spelled once for the whole tree.
[[nodiscard]] QString panelSettingsScopeFor(GameId game,
                                            PlayerLayoutMode mode,
                                            const QString &settingsKey);

// The scope group in force right now: panelSettingsScopeFor() reading
// the two globals. The spelling every Panel constructor uses.
[[nodiscard]] QString panelSettingsScopeGroup(const QString &settingsKey);

// Just the domain prefix ("world/single"), without a panel key. Used by
// the console's preview host to answer "is this domain non-empty"
// without naming a window.
[[nodiscard]] QString panelSettingsScopePrefix();

// ─── the one-time legacy migration ─────────────────────────────────────
//
// A panels.ini written before the namespace was per-game has one FLAT
// section per window: `[player-status]`, `[player-wirebug]`, with no
// game and no mode anywhere in the spelling. Those sections mean today
// exactly what they meant then — scale, margins, floors, columns — so
// they are copied verbatim into the current game's namespace rather
// than abandoned.
//
// A panels.ini written before the MODE axis existed (v0.13) has TWO
// domains rather than four: `[world/player]`, `[rise/player]`. Those
// are single-mode sections, because v0.13's only topology was single —
// the split did not exist to have a geometry of its own. So the
// v0.13 -> v0.14 migration folds each `[<game>/<key>]` into
// `[<game>/single/<key>]`, which is what keeps a v0.13 user's split
// preview reading the position they had already dragged the panel to.
//
// This function is the whole migration, and it takes the settings
// handle rather than opening its own so it can be driven from a test
// against a throwaway file. The production caller is
// Panel::loadConfig() (src/ui/panel.cpp), which opens the same handle
// and calls this once per panel section before reading it.
//
// The guarantees, each pinned by
// tests/panel_settings_scope_tests.cpp:
//
//   1. ONLY the current scope's namespace is written. The other games'
//      and the other mode's sections are never read, never rewritten,
//      never removed.
//   2. A section that already exists under the current scope's path is
//      authoritative and is NOT overwritten — the legacy section is
//      left alone instead. Re-copying would destroy a layout the user
//      has edited for this scope since migrating.
//   3. It is idempotent. A marker (`legacyMigrated=1`) under the copied
//      section makes a second call a no-op, so a repeated run cannot
//      re-import a section the user has since changed.
//   4. It takes the handle as a parameter and performs no I/O of its
//      own, so it can never open the developer's real config from a
//      test.
//   5. A flat section whose key ALREADY exists in another domain is not
//      migrated here. One flat section is one history; the first domain
//      to construct that window owns it.
//
// Returns true when a migration actually happened (the log line in the
// caller is gated on it, so a clean run prints nothing).
bool migrateLegacyPanelSection(QSettings &settings,
                               const PanelSettingsScope &scope,
                               const QString &settingsKey);

// ─── the Mixed scope seed ───────────────────────────────────────────────
//
// Mixed (v1.1.0) is the fifth domain: [world/mixed/<key>] and
// [rise/mixed/<key>]. It is the only topology whose WINDOWS are not a
// fixed list — each row of the three split tables is either its own
// window or merged back into its mother panel, per game, per user
// (ui/window_layout_plan.h) — so a user's Mixed run starts from
// wherever they were, not from factory defaults.
//
// WHAT THIS DOES, and nothing more: when the user EXPLICITLY enters
// Mixed, every window parameter (scale, mt/mr/ml/mb, minW, columns,
// …) that already exists under the <game>/<fromMode>/<window> path is
// copied to the SAME key under <game>/mixed/<window> — but only when
// that Mixed key does not already exist.
//
//   * EXISTING Mixed values are authoritative and are never touched.
//     They are the layout the user already dragged and saved in Mixed,
//     and re-seeding them would be exactly the data loss
//     migrateLegacyPanelSection's rule 2 refuses.
//   * the source domain (single or split) is READ, never written — a
//     Split run still owns its own geometry afterwards.
//   * the other game is never read and never written: the prefix is
//     game-scoped, so World's seed cannot reach Rise's sections.
//   * copy-missing-only makes it IDEMPOTENT, so a second explicit entry
//     (or a second process racing the first) changes nothing. No marker
//     key is written — a marker would be a second record of a fact the
//     missing-key check already answers, and it would have to be
//     removed by hand if a user ever deletes a Mixed section.
//
// WHEN IT MUST NOT RUN: on a read-only load. Panels are constructed on
// startup whatever the topology is, and none of them may seed Mixed just
// by being constructed — the trigger is the user's explicit choice of
// Mixed, not a mode that happens to be in force. So this is NOT called
// from Panel::loadConfig() or from the launch-time scope injection; the
// explicit-entry path (console combo / --layout=mixed) is the only
// caller.
//
// fromMode == Mixed means "already there": nothing is copied, false is
// returned, and the file is not touched at all.
//
// The two-argument form opens the same (IniFormat, UserScope,
// "monster-overlay", "panels") handle Panel opens, so it addresses the
// live panels.ini. The handle-taking form below is the whole
// implementation and takes the handle rather than opening one, so a test
// can drive it against a throwaway file and can never reach the
// developer's real config. It returns true when anything was seeded, for
// the same caller-gated log-line reason migrateLegacyPanelSection does.
[[nodiscard]] bool ensureMixedPanelSettingsScope(QSettings &settings,
                                                 GameId game,
                                                 PlayerSection::PlayerLayoutMode fromMode);
void ensureMixedPanelSettingsScope(GameId game,
                                   PlayerSection::PlayerLayoutMode fromMode);

} // namespace mhw
