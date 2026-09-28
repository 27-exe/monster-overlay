// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QList>
#include <QSet>

namespace mhw {

// QtCore-only manager for the Monster Hunter Rise REFramework payload used by
// monster-overlay. Archive extraction and game-process checks intentionally
// live outside this class.
class RiseReFrameworkManager {
public:
    enum class CoreState {
        Absent,
        External,
        Managed,
        Conflict,
    };

    enum class LuaState {
        Missing,
        Current,
        Modified,
    };

    enum class ManifestState {
        Missing,
        Valid,
        Invalid,
    };

    struct Status {
        bool gameDirValid{false};
        CoreState core{CoreState::Absent};
        LuaState lua{LuaState::Missing};
        ManifestState manifest{ManifestState::Missing};
        QString detail;
    };

    struct Result {
        bool ok{false};
        QString detail;
        QStringList changed;
        QStringList preserved;
    };

    explicit RiseReFrameworkManager(QString applicationDir);

    [[nodiscard]] Status status(const QString &gameDir) const;
    // Compatibility spelling for callers that use the query as a verb.
    [[nodiscard]] Status inspect(const QString &gameDir) const { return status(gameDir); }

    // Installs from an already-extracted MHRISE.zip staging directory. If an
    // unowned dinput8.dll exists, only the overlay Lua and its ownership
    // manifest are installed; the external REFramework core is never touched.
    [[nodiscard]] Result installFromStaging(const QString &gameDir,
                                            const QString &stagingDir) const;
    [[nodiscard]] Result install(const QString &gameDir, const QString &stagingDir) const
    {
        return installFromStaging(gameDir, stagingDir);
    }

    // Repair the project Lua after a Lua-only removal while retaining an
    // unchanged, manifest-owned REFramework core. This operation never
    // downloads, extracts, or replaces core files. It is idempotent when the
    // current Lua and its ownership entry are already present.
    [[nodiscard]] Result repairManagedLua(const QString &gameDir) const;

    // Removes only reframework/autorun/mhr-overlay-damage.lua. A Lua whose
    // hash differs from the packaged source is retained unless force is true.
    [[nodiscard]] Result removeLua(const QString &gameDir, bool force = false) const;

    // Removes files named by a valid project manifest only when their current
    // hashes still match the hashes recorded at install time.
    [[nodiscard]] Result removeManaged(const QString &gameDir) const;

    [[nodiscard]] static bool verifyArchive(const QString &archivePath,
                                            QString *detail = nullptr);
    [[nodiscard]] bool verifyBundledArchive(QString *detail = nullptr) const;

    [[nodiscard]] static QString upstreamTag();
    [[nodiscard]] static QString archiveAssetName();
    [[nodiscard]] static QString archiveUrl();
    [[nodiscard]] static qint64 expectedArchiveSize();
    [[nodiscard]] static QByteArray expectedArchiveSha256();
    [[nodiscard]] static QString bundledArchivePath(const QString &applicationDir);
    [[nodiscard]] static QString luaSourcePath(const QString &applicationDir);

private:
    QString m_applicationDir;
};

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
