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
// REFramework keeps its settings in the game directory as `re2_fw_config.txt`
// (the historical RE2 filename, shared by every REFramework-supported game).
// Out of the box `RememberMenuState` is false, so the ImGui menu pops up on
// every launch even after the player closed it — the close is never persisted.
//
// A single pure function owns the rewrite so the console button, the installer
// path and the unit tests all share one implementation. It is deliberately
// additive and idempotent: unknown lines are preserved verbatim, and a key we
// manage is replaced in place rather than appended, so repeated presses never
// duplicate entries or grow the file.
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

// Applies the menu-state fix to <gameDir>/re2_fw_config.txt, the REFramework
// user config. A copy of the pre-existing file is kept next to it as
// `re2_fw_config.txt.pre-monster-overlay` before any write, so the action is
// reversible by hand. `detail` receives a short human summary on failure.
// Returns false when the game directory cannot be written.
[[nodiscard]] bool applyReFrameworkMenuStateFix(const QString &gameDir,
                                               QString *detail = nullptr);

} // namespace mhw
