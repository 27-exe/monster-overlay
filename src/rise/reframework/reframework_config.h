// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace mhw {

// ---------------------------------------------------------------------------
// REFramework user-config overlay (re2_fw_config.txt)
//
// REFramework keeps its settings in `<game_dir>/re2_fw_config.txt` — the
// historical RE2 filename, shared by every REFramework-supported game. Out of
// the box `RememberMenuState` is false, so the ImGui menu pops up on every
// launch even after the player closed it: the close is never persisted.
//
// Location matters. Upstream documents that when REFramework "cannot access
// the current game directory for any reason" it uses `%APPDATA%/REFramework`
// instead — a real case on a read-only game mount or a locked-down Proton
// prefix. Writing only the game directory would silently do nothing there, so
// every path REFramework might read is considered and written together.
//
// `re2_fw_config.txt` is a user file shared with ~80 unrelated settings
// (Camera / VR / FreeCam ...); nothing here may touch a line it does not own.
// ---------------------------------------------------------------------------

struct ReFrameworkConfigSetting {
    QString key;      // e.g. REFrameworkConfig_RememberMenuState
    QString value;    // e.g. "true"
};

// Renders the full file content for `original` with each setting in `settings`
// applied. An existing line for that key is replaced in place (comment lines
// are ignored); a missing key is appended at the end. Returns the original
// content untouched when `settings` is empty.
[[nodiscard]] QString rewriteReFrameworkConfig(const QString &original,
                                               const QList<ReFrameworkConfigSetting> &settings);

// Every location REFramework may read its config from for one game install.
// The game directory comes first because it is the one upstream prefers; the
// AppData fallback is resolved through the running prefix when it can be, and
// the plain user path otherwise. Duplicates are collapsed.
[[nodiscard]] QStringList reframeworkConfigPaths(const QString &gameDir);

// What the menu-state key currently holds, across all candidate paths.
enum class ReFrameworkMenuState {
    Unknown,      // nothing to read (no file, or unreadable)
    Default,      // every readable copy says false (menu pops up on launch)
    Overridden,   // at least one copy already says true
};

struct ReFrameworkMenuStateReport {
    ReFrameworkMenuState state{ReFrameworkMenuState::Unknown};
    QStringList paths;   // the candidate locations that were inspected
    QStringList written; // the locations that were read-or-writable
};

// Inspects the candidate paths without writing anything, so the console can
// show which action is currently in effect.
[[nodiscard]] ReFrameworkMenuStateReport queryReFrameworkMenuState(
    const QString &gameDir);

// Applies (or reverts) the menu-state key across every candidate path so the
// next launch sees it regardless of which directory REFramework ends up
// using. Copies of any pre-existing file are kept alongside it as
// `re2_fw_config.txt.pre-monster-overlay` before the first write, so the
// action stays reversible by hand.
[[nodiscard]] bool applyReFrameworkMenuStateFix(const QString &gameDir,
                                                bool restoreDefault,
                                                ReFrameworkMenuStateReport *report = nullptr);

} // namespace mhw
