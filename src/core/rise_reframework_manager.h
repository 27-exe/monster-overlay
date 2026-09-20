// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>

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

} // namespace mhw
