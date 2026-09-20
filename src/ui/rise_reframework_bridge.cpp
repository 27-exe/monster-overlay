// SPDX-License-Identifier: Apache-2.0

#include "ui/rise_reframework_bridge.h"

#include "core/game_detector.h"
#include "core/reframework_fetcher.h"
#include "core/rise_reframework_manager.h"

#include <QDebug>
#include <QDir>

#include <exception>
#include <utility>

namespace {

using mhw::ReFrameworkFetcher;
using mhw::RiseReFrameworkManager;

const QString kGameRunningDetail = QStringLiteral(
    "Refusing to change REFramework files while a Monster Hunter game is running.");

QString unusableGameDir(const RiseReFrameworkManager::Status &status)
{
    return status.detail.isEmpty()
        ? QStringLiteral("The Rise installation directory is not usable.")
        : status.detail;
}

// The console's INSTALL button: prepares the pinned archive (bundled copy
// first, upstream download only as fallback), then installs the managed
// REFramework core (unless the user already has an external one) plus the
// overlay Lua.
bool runInstall(const QString &applicationDir, const QString &gameDir,
                std::atomic<bool> &cancel, QString *detail)
{
    // Fresh, race-safe gate: the game may have been launched after the last
    // UI status refresh.
    if (mhw::detectGame().has_value()) {
        *detail = kGameRunningDetail;
        return false;
    }

    RiseReFrameworkManager manager(applicationDir);
    const RiseReFrameworkManager::Status status = manager.status(gameDir);
    if (!status.gameDirValid) {
        *detail = unusableGameDir(status);
        return false;
    }
    // A hand-edited Lua is never overwritten silently. A manifest-owned Lua
    // that still matches its previous ownership hash is different: the manager
    // can safely upgrade that unchanged old package version and roll it back if
    // the manifest update fails.
    const bool managedUpgrade =
        status.lua == RiseReFrameworkManager::LuaState::Modified
        && status.core == RiseReFrameworkManager::CoreState::Managed
        && status.manifest == RiseReFrameworkManager::ManifestState::Valid;
    if (status.lua == RiseReFrameworkManager::LuaState::Modified
        && !managedUpgrade) {
        *detail = QStringLiteral(
            "The overlay Lua differs from the packaged copy; remove it first "
            "(\"REMOVE LUA\"), then install again.");
        return false;
    }

    const bool usableCore =
        status.core == RiseReFrameworkManager::CoreState::Managed
        || status.core == RiseReFrameworkManager::CoreState::External;
    if (status.lua == RiseReFrameworkManager::LuaState::Current
        && usableCore
        && status.manifest == RiseReFrameworkManager::ManifestState::Valid) {
        *detail = QStringLiteral("REFramework and the overlay Lua are already installed.");
        return true;
    }

    // A Lua-only removal intentionally leaves the managed core manifest. This
    // repair path restores just the producer and merges its ownership record;
    // it never downloads or replaces the verified core files.
    if (status.manifest == RiseReFrameworkManager::ManifestState::Valid) {
        const RiseReFrameworkManager::Result repaired =
            manager.repairManagedLua(gameDir);
        *detail = repaired.detail;
        return repaired.ok;
    }
    if (status.manifest == RiseReFrameworkManager::ManifestState::Invalid) {
        *detail = QStringLiteral("Cannot install while the managed manifest is invalid: %1")
                      .arg(status.detail);
        return false;
    }

    const QString bundled = RiseReFrameworkManager::bundledArchivePath(applicationDir);
    const ReFrameworkFetcher::Result prepared = ReFrameworkFetcher::prepareArchive(
        bundled, QDir::tempPath(), {},
        [](ReFrameworkFetcher::State, const QString &step) {
            qInfo().noquote() << "[reframework]" << step;
        },
        [&cancel] { return cancel.load(); });
    if (!prepared.ok) {
        *detail = prepared.cancelled ? QStringLiteral("Operation cancelled.")
                                     : prepared.detail;
        return false;
    }

    const RiseReFrameworkManager::Result installed =
        manager.installFromStaging(gameDir, prepared.stagingDir);
    if (!installed.ok) {
        *detail = installed.detail;
        return false;
    }

    *detail = prepared.source == ReFrameworkFetcher::Source::LocalArchive
        ? QStringLiteral("Installed REFramework %1 and the overlay Lua from the bundled archive.")
              .arg(RiseReFrameworkManager::upstreamTag())
        : QStringLiteral("Downloaded REFramework %1 and installed it with the overlay Lua.")
              .arg(RiseReFrameworkManager::upstreamTag());
    return true;
}

// The console's REMOVE OVERLAY LUA button: removes only
// reframework/autorun/mhr-overlay-damage.lua. The user confirmed a dialog
// naming the exact directory, so the modified-copy safeguard is bypassed for
// this fixed, project-owned path; symlinked paths are still refused.
bool runRemoveLua(const QString &applicationDir, const QString &gameDir,
                  std::atomic<bool> &cancel, QString *detail)
{
    Q_UNUSED(cancel);
    if (mhw::detectGame().has_value()) {
        *detail = kGameRunningDetail;
        return false;
    }

    RiseReFrameworkManager manager(applicationDir);
    const RiseReFrameworkManager::Status status = manager.status(gameDir);
    if (!status.gameDirValid) {
        *detail = unusableGameDir(status);
        return false;
    }

    const bool wasPresent = status.lua != RiseReFrameworkManager::LuaState::Missing;
    const bool wasModified = status.lua == RiseReFrameworkManager::LuaState::Modified;
    const RiseReFrameworkManager::Result result = manager.removeLua(gameDir, /*force=*/true);
    if (!result.ok) {
        *detail = result.detail;
        return false;
    }

    *detail = !wasPresent
        ? QStringLiteral("The overlay Lua was not present.")
        : wasModified
            ? QStringLiteral("Removed the overlay Lua (it differed from the packaged copy).")
            : QStringLiteral("Removed the overlay Lua.");
    return true;
}

// The console's REMOVE LUA + REFRAMEWORK button: removes the overlay Lua and
// every manifest-owned file whose hash still matches the recorded install
// time hash. Externally installed REFramework files and unrelated mods are
// never touched.
bool runRemoveReframework(const QString &applicationDir, const QString &gameDir,
                          std::atomic<bool> &cancel, QString *detail)
{
    Q_UNUSED(cancel);
    if (mhw::detectGame().has_value()) {
        *detail = kGameRunningDetail;
        return false;
    }

    RiseReFrameworkManager manager(applicationDir);
    const RiseReFrameworkManager::Status status = manager.status(gameDir);
    if (!status.gameDirValid) {
        *detail = unusableGameDir(status);
        return false;
    }

    const RiseReFrameworkManager::Result lua = manager.removeLua(gameDir, /*force=*/true);
    QString luaText;
    if (!lua.ok) {
        luaText = lua.detail;
    } else if (status.lua == RiseReFrameworkManager::LuaState::Missing) {
        luaText = QStringLiteral("The overlay Lua was already absent.");
    } else if (status.lua == RiseReFrameworkManager::LuaState::Modified) {
        luaText = QStringLiteral("Removed the modified overlay Lua.");
    } else {
        luaText = QStringLiteral("Removed the overlay Lua.");
    }

    // Re-read the state: removeLua() may have rewritten or removed the
    // ownership manifest, which decides what removeManaged() can still do.
    const RiseReFrameworkManager::Status afterLua = manager.status(gameDir);
    QString coreText;
    bool coreOk = true;
    if (afterLua.manifest == RiseReFrameworkManager::ManifestState::Missing) {
        coreText = afterLua.core == RiseReFrameworkManager::CoreState::External
            ? QStringLiteral("An externally installed REFramework core was left in place.")
            : QStringLiteral("No managed REFramework files were found.");
    } else {
        const RiseReFrameworkManager::Result core = manager.removeManaged(gameDir);
        coreOk = core.ok;
        coreText = core.detail;
    }

    *detail = luaText + QLatin1Char(' ') + coreText;
    return lua.ok && coreOk;
}

} // namespace

RiseReFrameworkBridge::RiseReFrameworkBridge(QString applicationDir, QObject *parent)
    : QObject(parent)
    , m_applicationDir(std::move(applicationDir))
{
}

RiseReFrameworkBridge::~RiseReFrameworkBridge()
{
    // Abort a running download/extraction so the console can exit promptly,
    // then join: after this the worker can no longer touch this object.
    m_cancel = true;
    joinWorker();
}

void RiseReFrameworkBridge::install(const QString &gameDir)
{
    start(QStringLiteral("install"),
          [this, gameDir](std::atomic<bool> &cancel, QString *detail) {
              return runInstall(m_applicationDir, gameDir, cancel, detail);
          });
}

void RiseReFrameworkBridge::removeLua(const QString &gameDir)
{
    start(QStringLiteral("remove-lua"),
          [this, gameDir](std::atomic<bool> &cancel, QString *detail) {
              return runRemoveLua(m_applicationDir, gameDir, cancel, detail);
          });
}

void RiseReFrameworkBridge::removeReframework(const QString &gameDir)
{
    start(QStringLiteral("remove-reframework"),
          [this, gameDir](std::atomic<bool> &cancel, QString *detail) {
              return runRemoveReframework(m_applicationDir, gameDir, cancel, detail);
          });
}

void RiseReFrameworkBridge::start(QString label, Operation operation)
{
    if (m_busy.exchange(true)) {
        // The UI gates re-entry, so this is defensive only.
        emit finished(false,
                      QStringLiteral("Another REFramework operation is already running."));
        return;
    }
    joinWorker(); // reap the previous, already-finished worker thread
    m_cancel = false;

    m_worker = std::thread([this, label = std::move(label), operation = std::move(operation)] {
        QString detail;
        bool ok = false;
        try {
            ok = operation(m_cancel, &detail);
        } catch (const std::exception &error) {
            detail = QStringLiteral("Unexpected error: %1").arg(QString::fromUtf8(error.what()));
        } catch (...) {
            detail = QStringLiteral("Unexpected error while performing the operation.");
        }
        qInfo().noquote() << "[reframework]" << label << (ok ? "finished" : "failed") << detail;

        // Marshal back to the GUI thread. If the console is already tearing
        // down, the destructor is joining this thread and ~QObject discards
        // the posted event, so this never runs after destruction.
        QMetaObject::invokeMethod(this, [this, ok, detail] {
            m_busy = false;
            emit finished(ok, detail);
        }, Qt::QueuedConnection);
    });
}

void RiseReFrameworkBridge::joinWorker()
{
    if (m_worker.joinable())
        m_worker.join();
}
