// SPDX-License-Identifier: Apache-2.0

#include "core/rise_reframework_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const QString &message)
{
    if (condition) {
        std::cout << "PASS: " << message.toStdString() << '\n';
    } else {
        std::cerr << "FAIL: " << message.toStdString() << '\n';
        ++failures;
    }
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

void makeGame(const QString &gameDir)
{
    writeFile(gameDir + QStringLiteral("/MonsterHunterRise.exe"), "rise-exe");
}

void makeSource(const QString &applicationDir, const QByteArray &data = "lua-source-v1")
{
    writeFile(applicationDir + QStringLiteral("/reframework/autorun/mhr-overlay-damage.lua"), data);
}

void makeStaging(const QString &stagingDir)
{
    writeFile(stagingDir + QStringLiteral("/dinput8.dll"), "dinput8");
    writeFile(stagingDir + QStringLiteral("/openvr_api.dll"), "openvr");
    writeFile(stagingDir + QStringLiteral("/openxr_loader.dll"), "openxr");
    writeFile(stagingDir + QStringLiteral("/reframework_revision.txt"), "revision");
    writeFile(stagingDir + QStringLiteral("/DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR"), "marker");
}

QString luaPath(const QString &gameDir)
{
    return gameDir + QStringLiteral("/reframework/autorun/mhr-overlay-damage.lua");
}

QString manifestPath(const QString &gameDir)
{
    return gameDir + QStringLiteral("/reframework/monster-overlay-install.json");
}

QByteArray manifestWithOwnedPath(const QString &path)
{
    QJsonObject owned;
    owned.insert(QStringLiteral("path"), path);
    owned.insert(QStringLiteral("sha256"), QString(64, QLatin1Char('0')));

    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"), 1);
    manifest.insert(QStringLiteral("project"), QStringLiteral("monster-overlay"));
    manifest.insert(QStringLiteral("tag"), QStringLiteral("v1.5.9.1"));
    manifest.insert(QStringLiteral("archiveSha256"),
                    QStringLiteral("91a6089e8fa3a17faba81a43f14f5c1f54b27cdd24d5282fc069d2cdadd693c5"));
    manifest.insert(QStringLiteral("owned"), QJsonArray{owned});
    return QJsonDocument(manifest).toJson(QJsonDocument::Compact);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    check(mhw::RiseReFrameworkManager::upstreamTag() == QStringLiteral("v1.5.9.1"),
          "upstream tag is pinned");
    check(mhw::RiseReFrameworkManager::archiveAssetName() == QStringLiteral("MHRISE.zip"),
          "upstream asset name is pinned");
    check(mhw::RiseReFrameworkManager::archiveUrl()
              == QStringLiteral("https://github.com/praydog/REFramework/releases/download/v1.5.9.1/MHRISE.zip"),
          "upstream URL is pinned");
    check(mhw::RiseReFrameworkManager::expectedArchiveSize() == 5280871,
          "upstream archive size is pinned");
    check(mhw::RiseReFrameworkManager::expectedArchiveSha256()
              == QByteArrayLiteral("91a6089e8fa3a17faba81a43f14f5c1f54b27cdd24d5282fc069d2cdadd693c5"),
          "upstream archive SHA-256 is pinned");
    check(mhw::RiseReFrameworkManager::bundledArchivePath(QStringLiteral("/opt/monster-overlay"))
              == QStringLiteral("/opt/monster-overlay/third_party/REFramework/MHRISE-v1.5.9.1.zip"),
          "bundled archive candidate uses the fixed application-relative path");

    {
        QTemporaryDir root;
        const QString applicationDir = root.path() + QStringLiteral("/application");
        QDir().mkpath(applicationDir + QStringLiteral("/third_party/REFramework"));
        check(writeFile(mhw::RiseReFrameworkManager::bundledArchivePath(applicationDir), "bad"),
              "bundled archive rejection fixture writes");
        mhw::RiseReFrameworkManager manager(applicationDir);
        QString detail;
        check(!manager.verifyBundledArchive(&detail),
              "bundled archive verifier rejects a corrupt candidate");
        check(!detail.isEmpty(), "bundled archive rejection returns detail");
    }

    {
        QTemporaryDir root;
        const QString applicationDir = root.path() + QStringLiteral("/application");
        const QString gameDir = root.path() + QStringLiteral("/game");
        const QString stagingDir = root.path() + QStringLiteral("/staging");
        QDir().mkpath(applicationDir);
        QDir().mkpath(gameDir);
        QDir().mkpath(stagingDir);
        makeGame(gameDir);
        makeSource(applicationDir);
        makeStaging(stagingDir);

        mhw::RiseReFrameworkManager manager(applicationDir);
        const auto before = manager.status(gameDir);
        check(before.gameDirValid, "fresh game directory is valid");
        check(before.core == mhw::RiseReFrameworkManager::CoreState::Absent,
              "fresh REFramework core is absent");
        check(before.lua == mhw::RiseReFrameworkManager::LuaState::Missing,
              "fresh overlay Lua is missing");
        check(before.manifest == mhw::RiseReFrameworkManager::ManifestState::Missing,
              "fresh install manifest is missing");
        check(!before.detail.isEmpty(), "status includes human-readable detail");

        const auto installed = manager.installFromStaging(gameDir, stagingDir);
        check(installed.ok, "fresh install succeeds");
        check(QFileInfo::exists(gameDir + QStringLiteral("/dinput8.dll")),
              "fresh install copies dinput8.dll");
        check(QFileInfo::exists(gameDir + QStringLiteral("/openvr_api.dll")),
              "fresh install copies openvr_api.dll");
        check(QFileInfo::exists(gameDir + QStringLiteral("/openxr_loader.dll")),
              "fresh install copies openxr_loader.dll");
        check(QFileInfo::exists(gameDir + QStringLiteral("/reframework_revision.txt")),
              "fresh install copies revision file");
        check(QFileInfo::exists(gameDir + QStringLiteral("/DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR")),
              "fresh install copies marker file");
        check(QFileInfo::exists(luaPath(gameDir)), "fresh install copies overlay Lua");
        check(QFileInfo::exists(manifestPath(gameDir)), "fresh install writes manifest");

        QFile manifestFile(manifestPath(gameDir));
        check(manifestFile.open(QIODevice::ReadOnly), "fresh manifest can be read");
        const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
        check(manifest.value(QStringLiteral("schema")).toInt() == 1,
              "manifest records schema version");
        check(manifest.value(QStringLiteral("tag")).toString() == QStringLiteral("v1.5.9.1"),
              "manifest records upstream tag");
        check(manifest.value(QStringLiteral("archiveSha256")).toString()
                  == QStringLiteral("91a6089e8fa3a17faba81a43f14f5c1f54b27cdd24d5282fc069d2cdadd693c5"),
              "manifest records archive SHA-256");
        const QJsonArray owned = manifest.value(QStringLiteral("owned")).toArray();
        check(owned.size() == 6, "fresh manifest records all six owned files");
        bool ownedEntriesValid = true;
        QSet<QString> ownedPaths;
        for (const QJsonValue &value : owned) {
            const QJsonObject entry = value.toObject();
            const QString path = entry.value(QStringLiteral("path")).toString();
            const QString hash = entry.value(QStringLiteral("sha256")).toString();
            ownedEntriesValid = ownedEntriesValid && !path.isEmpty() && hash.size() == 64;
            ownedPaths.insert(path);
        }
        check(ownedEntriesValid && ownedPaths.contains(QStringLiteral("dinput8.dll"))
                  && ownedPaths.contains(QStringLiteral("reframework/autorun/mhr-overlay-damage.lua")),
              "manifest records relative paths and installed hashes");

        const auto after = manager.status(gameDir);
        check(after.core == mhw::RiseReFrameworkManager::CoreState::Managed,
              "fresh install reports managed core");
        check(after.lua == mhw::RiseReFrameworkManager::LuaState::Current,
              "fresh install reports current Lua");
        check(after.manifest == mhw::RiseReFrameworkManager::ManifestState::Valid,
              "fresh install reports valid manifest");

        // Managed repair regression: this is the real state produced by the
        // console's "REMOVE LUA" action. The install action must be able to
        // restore only the producer without replacing an unchanged managed
        // core or refusing because its manifest already exists.
        check(manager.removeLua(gameDir).ok,
              "managed repair fixture removes only the Lua producer");
        const auto removedLua = manager.status(gameDir);
        check(removedLua.core == mhw::RiseReFrameworkManager::CoreState::Managed,
              "managed core remains current after Lua-only removal");
        check(removedLua.lua == mhw::RiseReFrameworkManager::LuaState::Missing,
              "Lua-only removal reports missing producer");
        check(removedLua.manifest == mhw::RiseReFrameworkManager::ManifestState::Valid,
              "Lua-only removal retains the managed-core manifest");

        const auto repaired = manager.repairManagedLua(gameDir);
        check(repaired.ok, "managed Lua repair succeeds");
        check(repaired.changed.contains(
                  QStringLiteral("reframework/autorun/mhr-overlay-damage.lua")),
              "managed Lua repair reports the restored producer");
        const auto repairedStatus = manager.status(gameDir);
        check(repairedStatus.core == mhw::RiseReFrameworkManager::CoreState::Managed,
              "managed Lua repair preserves the core");
        check(repairedStatus.lua == mhw::RiseReFrameworkManager::LuaState::Current,
              "managed Lua repair restores the current producer");
        check(repairedStatus.manifest == mhw::RiseReFrameworkManager::ManifestState::Valid,
              "managed Lua repair restores manifest ownership");

        const auto repairAgain = manager.repairManagedLua(gameDir);
        check(repairAgain.ok && repairAgain.changed.isEmpty(),
              "managed Lua repair is idempotent when already current");

        // Package upgrade: an on-disk Lua that still matches the previous
        // manifest hash is safe to replace. This must remain distinct from a
        // user-modified Lua, which is covered by the refusal tests below.
        makeSource(applicationDir, "lua-source-v2");
        check(manager.status(gameDir).lua
                  == mhw::RiseReFrameworkManager::LuaState::Modified,
              "new package detects the previous managed Lua as old");
        const auto upgraded = manager.repairManagedLua(gameDir);
        check(upgraded.ok, "unchanged old managed Lua upgrades safely");
        QFile upgradedLua(luaPath(gameDir));
        check(upgradedLua.open(QIODevice::ReadOnly)
                  && upgradedLua.readAll() == "lua-source-v2",
              "managed Lua upgrade writes the new packaged bytes");
        check(manager.status(gameDir).lua
                  == mhw::RiseReFrameworkManager::LuaState::Current,
              "managed Lua upgrade refreshes the ownership hash");

        check(manager.removeManaged(gameDir).ok, "managed remove succeeds");
        check(!QFileInfo::exists(gameDir + QStringLiteral("/dinput8.dll")),
              "managed remove deletes owned core");
        check(!QFileInfo::exists(luaPath(gameDir)), "managed remove deletes owned Lua");
        check(!QFileInfo::exists(manifestPath(gameDir)), "managed remove deletes manifest");
        check(!QFileInfo::exists(gameDir + QStringLiteral("/reframework")),
              "managed remove prunes empty REFramework directories");
    }

    {
        QTemporaryDir root;
        const QString applicationDir = root.path() + QStringLiteral("/application");
        const QString gameDir = root.path() + QStringLiteral("/game");
        const QString stagingDir = root.path() + QStringLiteral("/staging");
        QDir().mkpath(applicationDir);
        QDir().mkpath(gameDir);
        QDir().mkpath(stagingDir);
        makeGame(gameDir);
        makeSource(applicationDir);
        makeStaging(stagingDir);
        check(writeFile(gameDir + QStringLiteral("/dinput8.dll"), "external-core"),
              "external core fixture writes");
        check(writeFile(gameDir + QStringLiteral("/reframework/some-other-mod.dll"), "other-mod"),
              "unrelated mod fixture writes");

        mhw::RiseReFrameworkManager manager(applicationDir);
        const auto result = manager.installFromStaging(gameDir, stagingDir);
        check(result.ok, "external core install succeeds");
        QFile core(gameDir + QStringLiteral("/dinput8.dll"));
        check(core.open(QIODevice::ReadOnly), "external core remains present");
        check(core.readAll() == "external-core", "external core is not overwritten");
        check(QFileInfo::exists(luaPath(gameDir)), "external core install writes Lua");
        check(manager.status(gameDir).core == mhw::RiseReFrameworkManager::CoreState::External,
              "external core remains externally managed");
        check(manager.removeManaged(gameDir).ok, "external managed remove returns safely");
        check(QFileInfo::exists(gameDir + QStringLiteral("/dinput8.dll")),
              "external core survives managed remove");
        check(QFileInfo::exists(gameDir + QStringLiteral("/reframework/some-other-mod.dll")),
              "unrelated mod survives managed remove");
        check(!QFileInfo::exists(luaPath(gameDir)), "managed remove removes project Lua only");
    }

    {
        QTemporaryDir root;
        const QString applicationDir = root.path() + QStringLiteral("/application");
        const QString gameDir = root.path() + QStringLiteral("/game");
        QDir().mkpath(applicationDir);
        QDir().mkpath(gameDir);
        makeGame(gameDir);
        makeSource(applicationDir);
        check(writeFile(luaPath(gameDir), "user-modified"), "modified Lua fixture writes");

        mhw::RiseReFrameworkManager manager(applicationDir);
        check(manager.status(gameDir).lua == mhw::RiseReFrameworkManager::LuaState::Modified,
              "modified Lua is detected by source hash");
        const auto refused = manager.removeLua(gameDir);
        check(!refused.ok, "modified Lua removal is refused by default");
        check(QFileInfo::exists(luaPath(gameDir)), "refused modified Lua remains");
        check(manager.removeLua(gameDir, true).ok, "force Lua removal succeeds");
        check(!QFileInfo::exists(luaPath(gameDir)), "force Lua removal deletes fixed script");
    }

    {
        QTemporaryDir root;
        const QString wrongArchive = root.path() + QStringLiteral("/MHRISE.zip");
        check(writeFile(wrongArchive, "not-the-release"), "wrong archive fixture writes");
        QString detail;
        check(!mhw::RiseReFrameworkManager::verifyArchive(wrongArchive, &detail),
              "wrong archive hash/size is rejected");
        check(!detail.isEmpty(), "wrong archive rejection explains failure");

        const QString wrongHashArchive = root.path() + QStringLiteral("/MHRISE-right-size.zip");
        QFile sizedArchive(wrongHashArchive);
        check(sizedArchive.open(QIODevice::WriteOnly)
                  && sizedArchive.resize(mhw::RiseReFrameworkManager::expectedArchiveSize()),
              "wrong-hash archive fixture has the exact expected size");
        sizedArchive.close();
        detail.clear();
        check(!mhw::RiseReFrameworkManager::verifyArchive(wrongHashArchive, &detail),
              "exact-size archive with wrong SHA-256 is rejected");
        check(detail.contains(QStringLiteral("SHA-256")),
              "exact-size archive rejection identifies SHA-256 mismatch");
    }

    {
        QTemporaryDir root;
        const QString applicationDir = root.path() + QStringLiteral("/application");
        const QString gameDir = root.path() + QStringLiteral("/game");
        const QString stagingDir = root.path() + QStringLiteral("/staging");
        QDir().mkpath(applicationDir);
        QDir().mkpath(gameDir);
        QDir().mkpath(stagingDir);
        makeGame(gameDir);
        makeSource(applicationDir);
        makeStaging(stagingDir);
        check(writeFile(gameDir + QStringLiteral("/reframework/autorun"), "blocking-file"),
              "transaction failure blocker writes");
        mhw::RiseReFrameworkManager manager(applicationDir);
        const auto result = manager.installFromStaging(gameDir, stagingDir);
        check(!result.ok, "transaction failure is reported");
        check(!result.changed.contains(QStringLiteral("dinput8.dll")),
              "transaction failure does not report rolled-back files as changed");
        check(!QFileInfo::exists(gameDir + QStringLiteral("/dinput8.dll")),
              "transaction failure rolls back dinput8.dll");
        check(!QFileInfo::exists(gameDir + QStringLiteral("/openvr_api.dll")),
              "transaction failure rolls back openvr_api.dll");
        check(!QFileInfo::exists(gameDir + QStringLiteral("/openxr_loader.dll")),
              "transaction failure rolls back openxr_loader.dll");
        check(!QFileInfo::exists(gameDir + QStringLiteral("/reframework_revision.txt")),
              "transaction failure rolls back revision file");
        check(!QFileInfo::exists(gameDir + QStringLiteral("/DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR")),
              "transaction failure rolls back marker file");
        check(!QFileInfo::exists(luaPath(gameDir)),
              "transaction failure leaves no overlay Lua");
        check(!QFileInfo::exists(manifestPath(gameDir)),
              "transaction failure leaves no manifest");
        check(QFileInfo(gameDir + QStringLiteral("/reframework/autorun")).isFile(),
              "transaction rollback preserves the pre-existing blocker");
    }

    {
        QTemporaryDir root;
        const QString applicationDir = root.path() + QStringLiteral("/application");
        QDir().mkpath(applicationDir);
        makeSource(applicationDir);
        const QString outside = root.path() + QStringLiteral("/outside.txt");
        check(writeFile(outside, "do-not-delete"), "path traversal outside fixture writes");
        const QStringList hostilePaths{QStringLiteral("../outside.txt"), outside};
        for (qsizetype i = 0; i < hostilePaths.size(); ++i) {
            const QString gameDir = root.path() + QStringLiteral("/game-%1").arg(i);
            QDir().mkpath(gameDir);
            makeGame(gameDir);
            check(writeFile(manifestPath(gameDir), manifestWithOwnedPath(hostilePaths.at(i))),
                  QStringLiteral("hostile manifest fixture %1 writes").arg(i));
            mhw::RiseReFrameworkManager manager(applicationDir);
            check(manager.status(gameDir).manifest
                      == mhw::RiseReFrameworkManager::ManifestState::Invalid,
                  QStringLiteral("hostile manifest %1 has invalid status").arg(i));
            const auto result = manager.removeManaged(gameDir);
            check(!result.ok && result.detail.contains(QStringLiteral("unsafe")),
                  QStringLiteral("hostile manifest path %1 is rejected as unsafe").arg(i));
            check(QFileInfo::exists(outside),
                  QStringLiteral("hostile manifest %1 cannot delete outside file").arg(i));
            check(QFileInfo::exists(manifestPath(gameDir)),
                  QStringLiteral("rejected manifest %1 remains for diagnosis").arg(i));
        }
    }

    {
        QTemporaryDir root;
        const QString applicationDir = root.path() + QStringLiteral("/application");
        const QString gameDir = root.path() + QStringLiteral("/game");
        const QString stagingDir = root.path() + QStringLiteral("/staging");
        QDir().mkpath(applicationDir);
        QDir().mkpath(gameDir);
        QDir().mkpath(stagingDir);
        makeGame(gameDir);
        makeSource(applicationDir);
        makeStaging(stagingDir);
        mhw::RiseReFrameworkManager manager(applicationDir);
        check(manager.installFromStaging(gameDir, stagingDir).ok,
              "modified-owned fixture installs successfully");
        check(writeFile(gameDir + QStringLiteral("/dinput8.dll"), "user-patched-core"),
              "managed core modification writes");
        check(writeFile(luaPath(gameDir), "user-patched-lua"),
              "managed Lua modification writes");
        check(writeFile(gameDir + QStringLiteral("/reframework/plugins/unrelated.dll"), "other-mod"),
              "unrelated managed-tree mod writes");
        check(manager.status(gameDir).core == mhw::RiseReFrameworkManager::CoreState::Conflict,
              "modified managed core reports conflict");

        const auto removed = manager.removeManaged(gameDir);
        check(removed.ok, "managed remove completes while preserving modified files");
        check(removed.preserved.contains(QStringLiteral("dinput8.dll"))
                  && removed.preserved.contains(QStringLiteral("reframework/autorun/mhr-overlay-damage.lua")),
              "managed remove reports every modified owned file");
        check(QFileInfo::exists(gameDir + QStringLiteral("/dinput8.dll")),
              "modified owned core is preserved");
        check(QFileInfo::exists(luaPath(gameDir)), "modified owned Lua is preserved");
        check(!QFileInfo::exists(gameDir + QStringLiteral("/openvr_api.dll")),
              "unchanged owned file is removed");
        check(QFileInfo::exists(gameDir + QStringLiteral("/reframework/plugins/unrelated.dll")),
              "unrelated mod remains after managed removal");
        check(!QFileInfo::exists(manifestPath(gameDir)),
              "managed manifest is removed after safe processing");
    }

    {
        QTemporaryDir root;
        const QString applicationDir = root.path() + QStringLiteral("/application");
        const QString gameDir = root.path() + QStringLiteral("/game");
        QDir().mkpath(applicationDir);
        QDir().mkpath(gameDir);
        makeGame(gameDir);
        makeSource(applicationDir);
        check(writeFile(gameDir + QStringLiteral("/openvr_api.dll"), "orphan-core-file"),
              "partial core fixture writes");
        mhw::RiseReFrameworkManager manager(applicationDir);
        check(manager.status(gameDir).core == mhw::RiseReFrameworkManager::CoreState::Conflict,
              "partial unmanaged core reports conflict");
    }

    {
        QTemporaryDir root;
        const QString applicationDir = root.path() + QStringLiteral("/application");
        const QString gameDir = root.path() + QStringLiteral("/game");
        const QString stagingDir = root.path() + QStringLiteral("/staging");
        QDir().mkpath(applicationDir);
        QDir().mkpath(gameDir);
        QDir().mkpath(stagingDir);
        makeGame(gameDir);
        makeSource(applicationDir);
        makeStaging(stagingDir);
        check(writeFile(luaPath(gameDir), "user-owned-lua"),
              "pre-existing modified Lua fixture writes");
        mhw::RiseReFrameworkManager manager(applicationDir);
        const auto refused = manager.installFromStaging(gameDir, stagingDir);
        check(!refused.ok, "install refuses to overwrite pre-existing modified Lua");
        QFile lua(luaPath(gameDir));
        check(lua.open(QIODevice::ReadOnly) && lua.readAll() == "user-owned-lua",
              "refused install preserves pre-existing modified Lua");
        check(!QFileInfo::exists(gameDir + QStringLiteral("/dinput8.dll")),
              "refused modified-Lua install creates no core files");
        check(!QFileInfo::exists(manifestPath(gameDir)),
              "refused modified-Lua install creates no manifest");
    }

#ifdef Q_OS_UNIX
    {
        QTemporaryDir root;
        const QString applicationDir = root.path() + QStringLiteral("/application");
        const QString gameDir = root.path() + QStringLiteral("/game");
        const QString outsideDir = root.path() + QStringLiteral("/outside");
        QDir().mkpath(applicationDir);
        QDir().mkpath(gameDir + QStringLiteral("/reframework"));
        QDir().mkpath(outsideDir);
        makeGame(gameDir);
        makeSource(applicationDir);
        check(writeFile(outsideDir + QStringLiteral("/mhr-overlay-damage.lua"), "outside-lua"),
              "symlink escape target fixture writes");
        check(QFile::link(outsideDir, gameDir + QStringLiteral("/reframework/autorun")),
              "autorun directory symlink fixture writes");
        mhw::RiseReFrameworkManager manager(applicationDir);
        const auto refused = manager.removeLua(gameDir, true);
        check(!refused.ok, "removeLua rejects a symlinked autorun directory");
        check(QFileInfo::exists(outsideDir + QStringLiteral("/mhr-overlay-damage.lua")),
              "removeLua cannot follow a symlink outside the game directory");
    }
#endif

    // ---- v0.10.10: re2_fw_config.txt overlay ----------------------------
    // The menu-state fix REFramework needs on install. These assert the two
    // properties that matter for a USER file shared with ~80 other settings:
    // foreign lines survive untouched, and the rewrite is idempotent.
    {
        using mhw::ReFrameworkConfigSetting;
        using mhw::rewriteReFrameworkConfig;

        const QList<ReFrameworkConfigSetting> menuFix{
            {QStringLiteral("REFrameworkConfig_RememberMenuState"),
             QStringLiteral("true")}};

        const QString fresh = rewriteReFrameworkConfig(QString(), menuFix);
        check(fresh == QStringLiteral(
                  "REFrameworkConfig_RememberMenuState=true\n"),
              "an absent config gains only the managed key");

        // The real shipped file shape: CRLF, a header comment, foreign keys
        // both before and after the one we own.
        const QString real =
            "Camera_GlobalFOV=81.000000\r\n"
            "REFrameworkConfig_FontSize=16\r\n"
            "REFrameworkConfig_MenuKey_V2=45\r\n"
            "REFrameworkConfig_MenuOpen=false\r\n"
            "REFrameworkConfig_RememberMenuState=false\r\n"
            "REFrameworkConfig_ShowCursorKey=-1\r\n"
            "Scene_TimeScale=1.000000\r\n";
        const QString patched = rewriteReFrameworkConfig(real, menuFix);
        check(patched.contains(QStringLiteral(
                  "REFrameworkConfig_RememberMenuState=true\r\n")),
              "the managed key is flipped in place");
        check(!patched.contains(QStringLiteral(
                   "REFrameworkConfig_RememberMenuState=false")),
              "the managed key is not left behind as a stale duplicate");
        check(patched.contains(QStringLiteral("Camera_GlobalFOV=81.000000\r\n")),
              "a foreign key before ours survives");
        check(patched.contains(QStringLiteral("Scene_TimeScale=1.000000\r\n")),
              "a foreign key after ours survives");
        check(patched.count(QStringLiteral("REFrameworkConfig_RememberMenuState")) == 1,
              "the managed key appears exactly once");
        check(patched.count(QLatin1Char('\n')) == 7,
              "the rewrite does not change the line count");

        // Idempotence: applying twice must equal applying once.
        check(rewriteReFrameworkConfig(patched, menuFix) == patched,
              "pressing the button twice changes nothing the second time");

        // Appended when absent, in the middle of a file that has no trailing
        // newline, without disturbing that last foreign line.
        const QString noTrailing =
            "Camera_Enabled=false\r\nVR_ForceVSync=true";
        const QString appended = rewriteReFrameworkConfig(noTrailing, menuFix);
        check(appended.contains(QStringLiteral("VR_ForceVSync=true")),
              "a final foreign line with no newline survives");
        check(appended.contains(QStringLiteral(
                  "REFrameworkConfig_RememberMenuState=true")),
              "the managed key is appended when absent");
        check(appended.count(QStringLiteral("VR_ForceVSync=true")) == 1,
              "the trailing foreign line is not duplicated");

        // Empty settings must be a pure no-op — never rewrite a user file
        // into something else for no reason.
        check(rewriteReFrameworkConfig(real, {}) == real,
              "an empty setting list returns the file untouched");

        // Multiple keys in one pass keep their order.
        const QList<ReFrameworkConfigSetting> two{
            {QStringLiteral("REFrameworkConfig_MenuOpen"),
             QStringLiteral("false")},
            {QStringLiteral("REFrameworkConfig_DrawCursorWithMenuOpen"),
             QStringLiteral("false")}};
        const QString both = rewriteReFrameworkConfig(
            "REFrameworkConfig_MenuOpen=true\r\n", two);
        check(both.count(QStringLiteral("REFrameworkConfig_MenuOpen")) == 1,
              "the first managed key is replaced, not duplicated");
        check(both.contains(QStringLiteral("REFrameworkConfig_MenuOpen=false")),
              "the first managed key takes the requested value");
        check(both.contains(
                  QStringLiteral("REFrameworkConfig_DrawCursorWithMenuOpen=false")),
              "the second managed key is appended");
    }

    // ---- file-level round trip ------------------------------------------
    // rewriteReFrameworkConfig preserves CRLF, but that is only useful if the
    // caller actually hands it the CRLF. Reading with QIODevice::Text silently
    // strips it, which would rewrite every line ending in a 85-line user file
    // to fix one setting. Assert the whole file comes back byte-identical
    // apart from the single line we own.
    {
        QTemporaryDir dir;
        check(dir.isValid(), "temp dir for the config round trip is usable");
        if (dir.isValid()) {
            const QString path = dir.path()
                + QStringLiteral("/re2_fw_config.txt");
            const QByteArray before =
                "Camera_GlobalFOV=81.000000\r\n"
                "REFrameworkConfig_MenuKey_V2=45\r\n"
                "REFrameworkConfig_RememberMenuState=false\r\n";
            const QByteArray after =
                "Camera_GlobalFOV=81.000000\r\n"
                "REFrameworkConfig_MenuKey_V2=45\r\n"
                "REFrameworkConfig_RememberMenuState=true\r\n";
            const QByteArray tail = "Scene_TimeScale=1.000000\r\n";
            QFile f(path);
            check(f.open(QIODevice::WriteOnly), "can write the fixture");
            f.write(before + tail);
            f.close();

            QString detail;
            check(mhw::applyReFrameworkMenuStateFix(dir.path(), &detail),
                  "the menu-state fix succeeds on a real file");
            check(detail.isEmpty() || detail.contains(path),
                  "the detail names the file it touched");

            QFile f2(path);
            check(f2.open(QIODevice::ReadOnly), "can read the patched file");
            const QByteArray result = f2.readAll();
            f2.close();
            check(result == after + tail,
                  "only the owned line changed; CRLF survived");
            check(result.count("\r\n") == 4,
                  "every line ending is still CRLF");

            // Rollback copy exists and still holds the original bytes.
            const QFileInfo backup(path
                + QStringLiteral(".pre-monster-overlay"));
            check(backup.exists(), "a rollback copy was written");
            if (backup.exists()) {
                QFile b(backup.absoluteFilePath());
                check(b.open(QIODevice::ReadOnly), "can read the rollback copy");
                check(b.readAll() == before + tail,
                      "the rollback copy is the untouched original");
                b.close();
            }

            // Idempotent at the file level, and it must not re-copy the backup.
            check(mhw::applyReFrameworkMenuStateFix(dir.path(), &detail),
                  "pressing again reports success");
            QFile f3(path);
            check(f3.open(QIODevice::ReadOnly), "can re-read the file");
            check(f3.readAll() == result,
                  "a second press leaves the file byte-identical");
            f3.close();
        }
    }

    if (failures == 0)
        std::cout << "ALL TESTS PASSED\n";
    else
        std::cerr << failures << " TESTS FAILED\n";
    return failures == 0 ? 0 : 1;
}
