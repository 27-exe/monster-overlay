// SPDX-License-Identifier: Apache-2.0

#include "rise/reframework/rise_reframework_manager.h"

#include "core/string_table.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <optional>
#include <utility>

namespace mhw {
namespace {

constexpr qint64 kExpectedArchiveSize = 5280871;
constexpr auto kTag = "v1.5.9.1";
constexpr auto kAssetName = "MHRISE.zip";
constexpr auto kArchiveSha256 = "91a6089e8fa3a17faba81a43f14f5c1f54b27cdd24d5282fc069d2cdadd693c5";
constexpr auto kArchiveUrl = "https://github.com/praydog/REFramework/releases/download/v1.5.9.1/MHRISE.zip";
constexpr auto kProject = "monster-overlay";
constexpr int kManifestSchema = 1;
constexpr auto kGameExe = "MonsterHunterRise.exe";
constexpr auto kLuaRelative = "reframework/autorun/mhr-overlay-damage.lua";
constexpr auto kManifestRelative = "reframework/monster-overlay-install.json";

// i18n: the installer diagnostics reach the console UI verbatim (they end up
// in `detail` and are printed by rise_reframework_bridge.cpp), so they must
// flip with the active locale. Lookup happens at the point of production —
// never cached — so a locale change shows up in the next status refresh.
// Mirrors the pattern in src/mhw_reader.cpp / src/rise/mhr_reader.cpp.
inline QString trMessage(const QString &key) { return StringTable::instance().tr(key); }

// Joins the state pins that `status()` reports ("core managed; Lua current;
// ..."). Localised because the whole string it builds is displayed raw.
inline QString detailPinSeparator() { return trMessage("reader.reframework.pin_separator"); }

// The rollback outcome word reported by the install/repair failure details.
inline QString rollbackStateText(bool complete)
{
    return trMessage(complete ? "reader.reframework.rollback_completed"
                              : "reader.reframework.rollback_incomplete");
}

const QStringList &coreRelativePaths()
{
    static const QStringList paths{
        QStringLiteral("dinput8.dll"),
        QStringLiteral("openvr_api.dll"),
        QStringLiteral("openxr_loader.dll"),
        QStringLiteral("reframework_revision.txt"),
        QStringLiteral("DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR"),
    };
    return paths;
}

QStringList allowedOwnedPaths()
{
    QStringList paths = coreRelativePaths();
    paths.append(QString::fromLatin1(kLuaRelative));
    return paths;
}

struct OwnedFile {
    QString path;
    QByteArray sha256;
};

struct ManifestData {
    QList<OwnedFile> owned;
    QByteArray originalBytes;
};

enum class ManifestLoadState {
    Missing,
    Valid,
    Invalid,
};

bool pathEntryExists(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

QString absoluteRoot(const QString &directory)
{
    if (directory.trimmed().isEmpty())
        return {};

    const QFileInfo info(directory);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty())
        return QDir::cleanPath(canonical);
    return QDir::cleanPath(info.absoluteFilePath());
}

bool resolveDirectory(const QString &directory, QString *root, QString *error)
{
    const QString resolved = absoluteRoot(directory);
    if (resolved.isEmpty()) {
        if (error)
            *error = trMessage("reader.reframework.game_dir_empty");
        return false;
    }
    const QFileInfo info(resolved);
    if (!info.exists() || !info.isDir()) {
        if (error)
            *error = trMessage("reader.reframework.game_dir_not_directory").arg(resolved);
        return false;
    }
    *root = resolved;
    return true;
}

QString joinedPath(const QString &root, const QString &relative)
{
    return QDir(root).filePath(relative);
}

bool hasSymlinkComponent(const QString &root, const QString &relative, bool includeLeaf,
                         QString *error)
{
    const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString current = root;
    const qsizetype count = includeLeaf ? parts.size() : qMax<qsizetype>(0, parts.size() - 1);
    for (qsizetype i = 0; i < count; ++i) {
        current = QDir(current).filePath(parts.at(i));
        const QFileInfo info(current);
        if (info.isSymLink()) {
            if (error)
                *error = trMessage("reader.reframework.symlinked_path_component").arg(current);
            return true;
        }
    }
    return false;
}

QByteArray fileSha256(const QString &path, bool *ok = nullptr, QString *error = nullptr)
{
    if (ok)
        *ok = false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = trMessage("reader.reframework.file_open_failed")
                         .arg(path, file.errorString());
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer;
    buffer.resize(1024 * 1024);
    while (true) {
        const qint64 count = file.read(buffer.data(), buffer.size());
        if (count < 0) {
            if (error)
                *error = trMessage("reader.reframework.file_read_failed")
                             .arg(path, file.errorString());
            return {};
        }
        if (count == 0)
            break;
        hash.addData(QByteArrayView(buffer.constData(), count));
    }

    if (ok)
        *ok = true;
    return hash.result().toHex();
}

bool isLowerHexSha256(const QString &value)
{
    if (value.size() != 64)
        return false;
    for (const QChar ch : value) {
        const ushort code = ch.unicode();
        if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f')))
            return false;
    }
    return true;
}

bool jsonHasExactKeys(const QJsonObject &object, const QSet<QString> &expected)
{
    QSet<QString> actual;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        actual.insert(it.key());
    return actual == expected;
}

bool isSafeOwnedPath(const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QLatin1Char('\\')))
        return false;
    if (QDir::cleanPath(path) != path)
        return false;
    return allowedOwnedPaths().contains(path);
}

bool ownershipShapeIsValid(const QList<OwnedFile> &owned)
{
    const QString lua = QString::fromLatin1(kLuaRelative);
    QSet<QString> paths;
    for (const OwnedFile &file : owned)
        paths.insert(file.path);

    int coreCount = 0;
    for (const QString &core : coreRelativePaths()) {
        if (paths.contains(core))
            ++coreCount;
    }

    if (coreCount == 0)
        return paths.size() == 1 && paths.contains(lua);
    if (coreCount != coreRelativePaths().size())
        return false;
    return paths.size() == coreRelativePaths().size()
        || (paths.size() == coreRelativePaths().size() + 1 && paths.contains(lua));
}

ManifestLoadState loadManifest(const QString &root, ManifestData *manifest, QString *error)
{
    const QString relative = QString::fromLatin1(kManifestRelative);
    const QString path = joinedPath(root, relative);
    if (!pathEntryExists(path))
        return ManifestLoadState::Missing;

    QString symlinkError;
    if (hasSymlinkComponent(root, relative, true, &symlinkError)) {
        if (error)
            *error = symlinkError;
        return ManifestLoadState::Invalid;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = trMessage("reader.reframework.manifest_read_failed").arg(file.errorString());
        return ManifestLoadState::Invalid;
    }
    const QByteArray bytes = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = trMessage("reader.reframework.manifest_not_json")
                     .arg(parseError.errorString());
        return ManifestLoadState::Invalid;
    }

    const QJsonObject object = document.object();
    const QJsonValue schema = object.value(QStringLiteral("schema"));
    const QSet<QString> manifestKeys{
        QStringLiteral("schema"),
        QStringLiteral("project"),
        QStringLiteral("tag"),
        QStringLiteral("archiveSha256"),
        QStringLiteral("owned"),
    };
    if (!jsonHasExactKeys(object, manifestKeys)
        || !schema.isDouble() || schema.toDouble() != kManifestSchema
        || object.value(QStringLiteral("project")).toString() != QString::fromLatin1(kProject)
        || object.value(QStringLiteral("tag")).toString() != QString::fromLatin1(kTag)
        || object.value(QStringLiteral("archiveSha256")).toString()
            != QString::fromLatin1(kArchiveSha256)
        || !object.value(QStringLiteral("owned")).isArray()) {
        if (error)
            *error = trMessage("reader.reframework.manifest_identity_invalid");
        return ManifestLoadState::Invalid;
    }

    QList<OwnedFile> owned;
    QSet<QString> seen;
    const QSet<QString> entryKeys{QStringLiteral("path"), QStringLiteral("sha256")};
    const QJsonArray entries = object.value(QStringLiteral("owned")).toArray();
    for (const QJsonValue &entryValue : entries) {
        if (!entryValue.isObject()) {
            if (error)
                *error = trMessage("reader.reframework.manifest_entry_not_object");
            return ManifestLoadState::Invalid;
        }
        const QJsonObject entry = entryValue.toObject();
        const QString relativePath = entry.value(QStringLiteral("path")).toString();
        const QString sha = entry.value(QStringLiteral("sha256")).toString();
        if (!jsonHasExactKeys(entry, entryKeys) || !isSafeOwnedPath(relativePath)) {
            if (error)
                *error = trMessage("reader.reframework.manifest_unsafe_path")
                             .arg(relativePath);
            return ManifestLoadState::Invalid;
        }
        if (!isLowerHexSha256(sha) || seen.contains(relativePath)) {
            if (error)
                *error = trMessage("reader.reframework.manifest_duplicate_or_bad_hash");
            return ManifestLoadState::Invalid;
        }
        seen.insert(relativePath);
        owned.append(OwnedFile{relativePath, sha.toLatin1()});
    }

    if (!ownershipShapeIsValid(owned)) {
        if (error)
            *error = trMessage("reader.reframework.manifest_ownership_incomplete");
        return ManifestLoadState::Invalid;
    }

    if (manifest) {
        manifest->owned = std::move(owned);
        manifest->originalBytes = bytes;
    }
    return ManifestLoadState::Valid;
}

QByteArray serializeManifest(const QList<OwnedFile> &owned)
{
    QJsonArray entries;
    for (const OwnedFile &file : owned) {
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), file.path);
        entry.insert(QStringLiteral("sha256"), QString::fromLatin1(file.sha256));
        entries.append(entry);
    }

    QJsonObject object;
    object.insert(QStringLiteral("schema"), kManifestSchema);
    object.insert(QStringLiteral("project"), QString::fromLatin1(kProject));
    object.insert(QStringLiteral("tag"), QString::fromLatin1(kTag));
    object.insert(QStringLiteral("archiveSha256"), QString::fromLatin1(kArchiveSha256));
    object.insert(QStringLiteral("owned"), entries);
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

bool ensureParentDirectory(const QString &path, QString *error)
{
    const QString parent = QFileInfo(path).absolutePath();
    if (QDir().mkpath(parent))
        return true;
    if (error)
        *error = trMessage("reader.reframework.destination_mkdir_failed").arg(parent);
    return false;
}

bool writeBytesAtomically(const QString &target, const QByteArray &bytes, QString *error)
{
    if (!ensureParentDirectory(target, error))
        return false;

    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error)
            *error = trMessage("reader.reframework.destination_open_failed")
                         .arg(target, output.errorString());
        return false;
    }
    if (output.write(bytes) != bytes.size()) {
        if (error)
            *error = trMessage("reader.reframework.destination_write_failed")
                         .arg(target, output.errorString());
        output.cancelWriting();
        return false;
    }
    if (!output.commit()) {
        if (error)
            *error = trMessage("reader.reframework.destination_commit_failed")
                         .arg(target, output.errorString());
        return false;
    }
    return true;
}

bool copyFileAtomically(const QString &source, const QString &target, QString *error)
{
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error)
            *error = trMessage("reader.reframework.staging_open_failed")
                         .arg(source, input.errorString());
        return false;
    }
    if (!ensureParentDirectory(target, error))
        return false;

    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error)
            *error = trMessage("reader.reframework.destination_open_failed")
                         .arg(target, output.errorString());
        return false;
    }

    QByteArray buffer;
    buffer.resize(1024 * 1024);
    while (true) {
        const qint64 count = input.read(buffer.data(), buffer.size());
        if (count < 0) {
            if (error)
                *error = trMessage("reader.reframework.staging_read_failed")
                             .arg(source, input.errorString());
            output.cancelWriting();
            return false;
        }
        if (count == 0)
            break;
        if (output.write(buffer.constData(), count) != count) {
            if (error)
                *error = trMessage("reader.reframework.destination_write_failed")
                             .arg(target, output.errorString());
            output.cancelWriting();
            return false;
        }
    }

    if (!output.commit()) {
        if (error)
            *error = trMessage("reader.reframework.destination_commit_failed")
                         .arg(target, output.errorString());
        return false;
    }
    return true;
}

void removeEmptyManagedDirectories(const QString &root)
{
    QDir rootDir(root);
    rootDir.rmdir(QStringLiteral("reframework/autorun"));
    rootDir.rmdir(QStringLiteral("reframework"));
}

QString coreStateText(RiseReFrameworkManager::CoreState state)
{
    switch (state) {
    case RiseReFrameworkManager::CoreState::Absent:
        return trMessage("reader.reframework.core_absent");
    case RiseReFrameworkManager::CoreState::External:
        return trMessage("reader.reframework.core_external");
    case RiseReFrameworkManager::CoreState::Managed:
        return trMessage("reader.reframework.core_managed");
    case RiseReFrameworkManager::CoreState::Conflict:
        return trMessage("reader.reframework.core_conflict");
    }
    return trMessage("reader.reframework.core_unknown");
}

QString luaStateText(RiseReFrameworkManager::LuaState state)
{
    switch (state) {
    case RiseReFrameworkManager::LuaState::Missing:
        return trMessage("reader.reframework.lua_missing");
    case RiseReFrameworkManager::LuaState::Current:
        return trMessage("reader.reframework.lua_current");
    case RiseReFrameworkManager::LuaState::Modified:
        return trMessage("reader.reframework.lua_modified");
    }
    return trMessage("reader.reframework.lua_unknown");
}

QString manifestStateText(RiseReFrameworkManager::ManifestState state)
{
    switch (state) {
    case RiseReFrameworkManager::ManifestState::Missing:
        return trMessage("reader.reframework.manifest_missing");
    case RiseReFrameworkManager::ManifestState::Valid:
        return trMessage("reader.reframework.manifest_valid");
    case RiseReFrameworkManager::ManifestState::Invalid:
        return trMessage("reader.reframework.manifest_invalid");
    }
    return trMessage("reader.reframework.manifest_unknown");
}

const OwnedFile *findOwned(const ManifestData &manifest, const QString &relative)
{
    for (const OwnedFile &file : manifest.owned) {
        if (file.path == relative)
            return &file;
    }
    return nullptr;
}

} // namespace

RiseReFrameworkManager::RiseReFrameworkManager(QString applicationDir)
    : m_applicationDir(absoluteRoot(applicationDir))
{
}

RiseReFrameworkManager::Status RiseReFrameworkManager::status(const QString &gameDir) const
{
    Status status;
    QString root;
    QString directoryError;
    if (!resolveDirectory(gameDir, &root, &directoryError)) {
        status.detail = directoryError;
        return status;
    }

    const QFileInfo exe(joinedPath(root, QString::fromLatin1(kGameExe)));
    status.gameDirValid = exe.exists() && exe.isFile();

    ManifestData manifest;
    QString manifestError;
    const ManifestLoadState loaded = loadManifest(root, &manifest, &manifestError);
    switch (loaded) {
    case ManifestLoadState::Missing:
        status.manifest = ManifestState::Missing;
        break;
    case ManifestLoadState::Valid:
        status.manifest = ManifestState::Valid;
        break;
    case ManifestLoadState::Invalid:
        status.manifest = ManifestState::Invalid;
        break;
    }

    const QString luaRelative = QString::fromLatin1(kLuaRelative);
    const QString targetLua = joinedPath(root, luaRelative);
    if (!pathEntryExists(targetLua)) {
        status.lua = LuaState::Missing;
    } else {
        bool sourceOk = false;
        bool targetOk = false;
        const QByteArray sourceHash = fileSha256(luaSourcePath(m_applicationDir), &sourceOk);
        const QFileInfo targetInfo(targetLua);
        const QByteArray targetHash = targetInfo.isSymLink()
            ? QByteArray{}
            : fileSha256(targetLua, &targetOk);
        status.lua = sourceOk && targetOk && sourceHash == targetHash
            ? LuaState::Current
            : LuaState::Modified;
    }

    const QString dinputRelative = coreRelativePaths().first();
    const QString dinputPath = joinedPath(root, dinputRelative);
    if (loaded == ManifestLoadState::Valid && findOwned(manifest, dinputRelative)) {
        bool allCoreCurrent = true;
        for (const QString &relative : coreRelativePaths()) {
            const OwnedFile *owned = findOwned(manifest, relative);
            const QString target = joinedPath(root, relative);
            bool hashOk = false;
            const QFileInfo info(target);
            const QByteArray hash = (!owned || info.isSymLink())
                ? QByteArray{}
                : fileSha256(target, &hashOk);
            if (!owned || !hashOk || hash != owned->sha256) {
                allCoreCurrent = false;
                break;
            }
        }
        status.core = allCoreCurrent ? CoreState::Managed : CoreState::Conflict;
    } else if (pathEntryExists(dinputPath)) {
        status.core = CoreState::External;
    } else {
        bool partialCore = false;
        for (qsizetype i = 1; i < coreRelativePaths().size(); ++i) {
            if (pathEntryExists(joinedPath(root, coreRelativePaths().at(i)))) {
                partialCore = true;
                break;
            }
        }
        status.core = partialCore ? CoreState::Conflict : CoreState::Absent;
    }

    QStringList details;
    details.append(status.gameDirValid
                       ? trMessage("reader.reframework.game_exe_found")
                       : trMessage("reader.reframework.game_exe_missing"));
    details.append(coreStateText(status.core));
    details.append(luaStateText(status.lua));
    details.append(manifestStateText(status.manifest));
    if (!manifestError.isEmpty())
        details.append(manifestError);
    status.detail = details.join(detailPinSeparator());
    return status;
}

RiseReFrameworkManager::Result RiseReFrameworkManager::installFromStaging(
    const QString &gameDir, const QString &stagingDir) const
{
    Result result;
    QString root;
    QString error;
    if (!resolveDirectory(gameDir, &root, &error)) {
        result.detail = error;
        return result;
    }
    if (!QFileInfo(joinedPath(root, QString::fromLatin1(kGameExe))).isFile()) {
        result.detail = trMessage("reader.reframework.install_refused_exe_missing");
        return result;
    }

    ManifestData existingManifest;
    QString manifestError;
    const ManifestLoadState manifestState = loadManifest(root, &existingManifest, &manifestError);
    if (manifestState != ManifestLoadState::Missing) {
        result.detail = manifestState == ManifestLoadState::Valid
            ? trMessage("reader.reframework.install_refused_managed_exists")
            : trMessage("reader.reframework.install_refused_manifest_invalid")
                      .arg(manifestError);
        return result;
    }

    const QString manifestRelative = QString::fromLatin1(kManifestRelative);
    if (hasSymlinkComponent(root, manifestRelative, false, &error)) {
        result.detail = error;
        return result;
    }

    const QString sourceLua = luaSourcePath(m_applicationDir);
    const QFileInfo sourceLuaInfo(sourceLua);
    bool luaHashOk = false;
    const QByteArray luaHash = sourceLuaInfo.isSymLink()
        ? QByteArray{}
        : fileSha256(sourceLua, &luaHashOk, &error);
    if (!sourceLuaInfo.isFile() || sourceLuaInfo.isSymLink() || !luaHashOk) {
        result.detail = trMessage("reader.reframework.packaged_lua_unavailable").arg(error);
        return result;
    }

    const QString stagingRoot = absoluteRoot(stagingDir);
    const QFileInfo stagingInfo(stagingRoot);
    if (stagingRoot.isEmpty() || !stagingInfo.exists() || !stagingInfo.isDir()) {
        result.detail = trMessage("reader.reframework.staging_dir_unavailable");
        return result;
    }

    const QString dinput = joinedPath(root, coreRelativePaths().first());
    const bool externalCore = pathEntryExists(dinput);
    QList<OwnedFile> owned;
    QList<QPair<QString, QString>> copies;

    if (!externalCore) {
        for (const QString &relative : coreRelativePaths()) {
            const QString source = joinedPath(stagingRoot, relative);
            const QString target = joinedPath(root, relative);
            const QFileInfo sourceInfo(source);
            if (!sourceInfo.exists() || !sourceInfo.isFile() || sourceInfo.isSymLink()) {
                result.detail =
                    trMessage("reader.reframework.staging_missing_file").arg(relative);
                return result;
            }
            if (pathEntryExists(target)) {
                result.detail =
                    trMessage("reader.reframework.install_refused_core_exists").arg(relative);
                return result;
            }
            if (hasSymlinkComponent(root, relative, false, &error)) {
                result.detail = error;
                return result;
            }
            bool hashOk = false;
            const QByteArray hash = fileSha256(source, &hashOk, &error);
            if (!hashOk) {
                result.detail = error;
                return result;
            }
            copies.append(qMakePair(source, target));
            owned.append(OwnedFile{relative, hash});
        }
    }

    const QString luaRelative = QString::fromLatin1(kLuaRelative);
    const QString targetLua = joinedPath(root, luaRelative);
    bool luaAlreadyCurrent = false;
    if (pathEntryExists(targetLua)) {
        const QFileInfo targetInfo(targetLua);
        bool targetHashOk = false;
        const QByteArray targetHash = targetInfo.isSymLink()
            ? QByteArray{}
            : fileSha256(targetLua, &targetHashOk, &error);
        if (!targetHashOk || targetHash != luaHash) {
            result.detail = trMessage("reader.reframework.install_refused_lua_modified");
            return result;
        }
        luaAlreadyCurrent = true;
    } else if (hasSymlinkComponent(root, luaRelative, false, &error)) {
        result.detail = error;
        return result;
    }
    if (!luaAlreadyCurrent)
        copies.append(qMakePair(sourceLua, targetLua));
    owned.append(OwnedFile{luaRelative, luaHash});

    QStringList created;
    auto rollback = [&]() {
        bool complete = true;
        for (auto it = created.crbegin(); it != created.crend(); ++it) {
            if (pathEntryExists(*it) && !QFile::remove(*it))
                complete = false;
        }
        removeEmptyManagedDirectories(root);
        return complete;
    };

    for (const auto &copy : std::as_const(copies)) {
        if (!copyFileAtomically(copy.first, copy.second, &error)) {
            const bool rolledBack = rollback();
            result.changed.clear();
            result.detail = trMessage("reader.reframework.install_failed_rollback")
                                .arg(error, rollbackStateText(rolledBack));
            return result;
        }
        created.append(copy.second);
        result.changed.append(QDir(root).relativeFilePath(copy.second));
    }

    // Verify installed bytes before recording ownership.
    for (const OwnedFile &file : std::as_const(owned)) {
        bool hashOk = false;
        const QByteArray installedHash = fileSha256(joinedPath(root, file.path), &hashOk, &error);
        if (!hashOk || installedHash != file.sha256) {
            const bool rolledBack = rollback();
            result.changed.clear();
            result.detail = trMessage("reader.reframework.install_verification_failed")
                                .arg(file.path, rollbackStateText(rolledBack));
            return result;
        }
    }

    const QString manifestPath = joinedPath(root, manifestRelative);
    if (!writeBytesAtomically(manifestPath, serializeManifest(owned), &error)) {
        const bool rolledBack = rollback();
        result.changed.clear();
        result.detail = trMessage("reader.reframework.install_manifest_write_failed")
                            .arg(error, rollbackStateText(rolledBack));
        return result;
    }
    result.changed.append(manifestRelative);
    result.ok = true;
    result.detail = externalCore
        ? trMessage("reader.reframework.install_lua_only_external_core")
        : trMessage("reader.reframework.install_managed_core_and_lua");
    return result;
}

RiseReFrameworkManager::Result RiseReFrameworkManager::repairManagedLua(
    const QString &gameDir) const
{
    Result result;
    QString root;
    QString error;
    if (!resolveDirectory(gameDir, &root, &error)) {
        result.detail = error;
        return result;
    }
    if (!QFileInfo(joinedPath(root, QString::fromLatin1(kGameExe))).isFile()) {
        result.detail = trMessage("reader.reframework.repair_refused_exe_missing");
        return result;
    }

    ManifestData manifest;
    QString manifestError;
    const ManifestLoadState manifestState = loadManifest(root, &manifest, &manifestError);
    if (manifestState != ManifestLoadState::Valid) {
        result.detail = manifestState == ManifestLoadState::Missing
            ? trMessage("reader.reframework.repair_refused_manifest_missing")
            : trMessage("reader.reframework.repair_refused_manifest_invalid")
                      .arg(manifestError);
        return result;
    }

    // Repair is intentionally narrower than install: every managed core file
    // must still be present, regular, non-symlinked, and byte-identical to its
    // ownership record. A conflict requires diagnosis rather than a partial
    // overwrite that could mix versions.
    for (const QString &relative : coreRelativePaths()) {
        const OwnedFile *owned = findOwned(manifest, relative);
        const QString target = joinedPath(root, relative);
        const QFileInfo targetInfo(target);
        QString symlinkError;
        if (!owned || hasSymlinkComponent(root, relative, false, &symlinkError)
            || targetInfo.isSymLink() || !targetInfo.isFile()) {
            result.detail = owned
                ? trMessage("reader.reframework.repair_refused_core_missing_unsafe")
                          .arg(relative)
                : trMessage("reader.reframework.repair_refused_manifest_not_owning_core");
            return result;
        }
        bool hashOk = false;
        const QByteArray currentHash = fileSha256(target, &hashOk, &error);
        if (!hashOk || currentHash != owned->sha256) {
            result.detail =
                trMessage("reader.reframework.repair_refused_core_modified").arg(relative);
            return result;
        }
    }

    const QString sourceLua = luaSourcePath(m_applicationDir);
    const QFileInfo sourceInfo(sourceLua);
    bool sourceHashOk = false;
    const QByteArray sourceHash = sourceInfo.isSymLink()
        ? QByteArray{}
        : fileSha256(sourceLua, &sourceHashOk, &error);
    if (!sourceInfo.isFile() || sourceInfo.isSymLink() || !sourceHashOk) {
        result.detail = trMessage("reader.reframework.packaged_lua_unavailable").arg(error);
        return result;
    }

    const QString luaRelative = QString::fromLatin1(kLuaRelative);
    const QString targetLua = joinedPath(root, luaRelative);
    if (hasSymlinkComponent(root, luaRelative, false, &error)) {
        result.detail = error;
        return result;
    }

    bool luaChanged = false;
    bool targetExisted = pathEntryExists(targetLua);
    QByteArray previousLua;
    const OwnedFile *ownedLua = findOwned(manifest, luaRelative);
    if (targetExisted) {
        const QFileInfo targetInfo(targetLua);
        bool targetHashOk = false;
        const QByteArray targetHash = targetInfo.isSymLink()
            ? QByteArray{}
            : fileSha256(targetLua, &targetHashOk, &error);
        if (!targetInfo.isFile() || targetInfo.isSymLink() || !targetHashOk) {
            result.detail = trMessage("reader.reframework.repair_refused_lua_unsafe");
            return result;
        }
        if (targetHash != sourceHash) {
            // Package upgrade: replace only when the on-disk Lua still equals
            // the old ownership hash. Any user edit remains protected.
            if (!ownedLua || targetHash != ownedLua->sha256) {
                result.detail = trMessage("reader.reframework.repair_refused_lua_modified");
                return result;
            }
            QFile oldLua(targetLua);
            if (!oldLua.open(QIODevice::ReadOnly)) {
                result.detail = trMessage("reader.reframework.rollback_preserve_failed");
                return result;
            }
            previousLua = oldLua.readAll();
            if (!copyFileAtomically(sourceLua, targetLua, &error)) {
                result.detail = trMessage("reader.reframework.lua_upgrade_failed").arg(error);
                return result;
            }
            luaChanged = true;
            result.changed.append(luaRelative);
        }
    } else {
        if (!copyFileAtomically(sourceLua, targetLua, &error)) {
            result.detail = trMessage("reader.reframework.lua_repair_failed").arg(error);
            return result;
        }
        luaChanged = true;
        result.changed.append(luaRelative);
    }

    const bool manifestNeedsUpdate = !ownedLua || ownedLua->sha256 != sourceHash;
    if (manifestNeedsUpdate) {
        QList<OwnedFile> updated = manifest.owned;
        bool replacedOwnership = false;
        for (OwnedFile &owned : updated) {
            if (owned.path == luaRelative) {
                owned.sha256 = sourceHash;
                replacedOwnership = true;
                break;
            }
        }
        if (!replacedOwnership)
            updated.append(OwnedFile{luaRelative, sourceHash});

        const QString manifestPath = joinedPath(root, QString::fromLatin1(kManifestRelative));
        if (!writeBytesAtomically(manifestPath, serializeManifest(updated), &error)) {
            bool rolledBack = true;
            if (luaChanged) {
                rolledBack = targetExisted
                    ? writeBytesAtomically(targetLua, previousLua, nullptr)
                    : QFile::remove(targetLua);
            }
            result.changed.clear();
            result.detail = trMessage("reader.reframework.lua_repair_manifest_failed")
                                .arg(error, rollbackStateText(rolledBack));
            return result;
        }
        result.changed.append(QString::fromLatin1(kManifestRelative));
    }

    result.ok = true;
    if (luaChanged && targetExisted) {
        result.detail = trMessage("reader.reframework.lua_upgraded_unchanged");
    } else if (luaChanged) {
        result.detail = trMessage("reader.reframework.lua_restored");
    } else if (manifestNeedsUpdate) {
        result.detail = trMessage("reader.reframework.lua_ownership_updated");
    } else {
        result.detail = trMessage("reader.reframework.lua_already_current");
    }
    return result;
}

RiseReFrameworkManager::Result RiseReFrameworkManager::removeLua(const QString &gameDir,
                                                                  bool force) const
{
    Result result;
    QString root;
    QString error;
    if (!resolveDirectory(gameDir, &root, &error)) {
        result.detail = error;
        return result;
    }

    const QString relative = QString::fromLatin1(kLuaRelative);
    const QString target = joinedPath(root, relative);
    if (hasSymlinkComponent(root, relative, false, &error)) {
        result.detail = error;
        return result;
    }

    const QFileInfo targetInfo(target);
    const bool targetExists = pathEntryExists(target);
    if (targetExists) {
        bool sourceOk = false;
        bool targetOk = false;
        const QByteArray sourceHash = fileSha256(luaSourcePath(m_applicationDir), &sourceOk);
        const QByteArray targetHash = targetInfo.isSymLink()
            ? QByteArray{}
            : fileSha256(target, &targetOk);
        const bool modified = !sourceOk || !targetOk || sourceHash != targetHash;
        if (modified && !force) {
            result.preserved.append(relative);
            result.detail = trMessage("reader.reframework.lua_remove_refused_differs");
            return result;
        }
    }

    ManifestData manifest;
    QString manifestError;
    const ManifestLoadState manifestState = loadManifest(root, &manifest, &manifestError);
    QList<OwnedFile> remaining;
    bool manifestOwnedLua = false;
    if (manifestState == ManifestLoadState::Valid) {
        for (const OwnedFile &file : std::as_const(manifest.owned)) {
            if (file.path == relative)
                manifestOwnedLua = true;
            else
                remaining.append(file);
        }
    }

    const QString manifestPath = joinedPath(root, QString::fromLatin1(kManifestRelative));
    bool manifestChanged = false;
    if (manifestOwnedLua) {
        if (remaining.isEmpty()) {
            if (!QFile::remove(manifestPath)) {
                result.detail = trMessage("reader.reframework.lua_manifest_remove_failed");
                return result;
            }
        } else if (!writeBytesAtomically(manifestPath, serializeManifest(remaining), &error)) {
            result.detail =
                trMessage("reader.reframework.lua_manifest_update_failed").arg(error);
            return result;
        }
        manifestChanged = true;
    }

    if (targetExists && !QFile::remove(target)) {
        if (manifestChanged && !writeBytesAtomically(manifestPath, manifest.originalBytes, &error)) {
            result.detail =
                trMessage("reader.reframework.lua_remove_unrestorable").arg(error);
        } else {
            result.detail = trMessage("reader.reframework.lua_remove_failed");
        }
        return result;
    }

    if (targetExists)
        result.changed.append(relative);
    if (manifestChanged)
        result.changed.append(QString::fromLatin1(kManifestRelative));
    removeEmptyManagedDirectories(root);
    result.ok = true;
    result.detail = targetExists
        ? trMessage("reader.reframework.lua_removed")
        : trMessage("reader.reframework.lua_already_absent");
    return result;
}

RiseReFrameworkManager::Result RiseReFrameworkManager::removeManaged(const QString &gameDir) const
{
    Result result;
    QString root;
    QString error;
    if (!resolveDirectory(gameDir, &root, &error)) {
        result.detail = error;
        return result;
    }

    ManifestData manifest;
    QString manifestError;
    const ManifestLoadState state = loadManifest(root, &manifest, &manifestError);
    if (state != ManifestLoadState::Valid) {
        result.detail = state == ManifestLoadState::Missing
            ? trMessage("reader.reframework.remove_refused_manifest_missing")
            : trMessage("reader.reframework.remove_refused_detail").arg(manifestError);
        return result;
    }

    QStringList deletionFailures;
    for (const OwnedFile &owned : std::as_const(manifest.owned)) {
        const QString target = joinedPath(root, owned.path);
        QString symlinkError;
        const QFileInfo info(target);
        if (hasSymlinkComponent(root, owned.path, false, &symlinkError) || info.isSymLink()) {
            result.preserved.append(owned.path);
            continue;
        }
        if (!info.exists())
            continue;
        if (!info.isFile()) {
            result.preserved.append(owned.path);
            continue;
        }

        bool hashOk = false;
        const QByteArray currentHash = fileSha256(target, &hashOk);
        if (!hashOk || currentHash != owned.sha256) {
            result.preserved.append(owned.path);
            continue;
        }
        if (!QFile::remove(target)) {
            deletionFailures.append(owned.path);
            continue;
        }
        result.changed.append(owned.path);
    }

    if (!deletionFailures.isEmpty()) {
        result.detail = trMessage("reader.reframework.remove_partially_failed")
                            .arg(deletionFailures.join(trMessage("reader.reframework.list_separator")));
        return result;
    }

    const QString manifestRelative = QString::fromLatin1(kManifestRelative);
    if (!QFile::remove(joinedPath(root, manifestRelative))) {
        result.detail = trMessage("reader.reframework.remove_manifest_left");
        return result;
    }
    result.changed.append(manifestRelative);
    removeEmptyManagedDirectories(root);

    result.ok = true;
    result.detail = result.preserved.isEmpty()
        ? trMessage("reader.reframework.remove_all_unchanged")
        : trMessage("reader.reframework.remove_unchanged_preserved")
              .arg(result.preserved.join(trMessage("reader.reframework.list_separator")));
    return result;
}

bool RiseReFrameworkManager::verifyArchive(const QString &archivePath, QString *detail)
{
    const QFileInfo info(archivePath);
    if (!info.exists() || !info.isFile()) {
        if (detail)
            *detail = trMessage("reader.reframework.archive_missing");
        return false;
    }
    if (info.size() != kExpectedArchiveSize) {
        if (detail) {
            *detail = trMessage("reader.reframework.archive_size_mismatch")
                          .arg(kExpectedArchiveSize)
                          .arg(info.size());
        }
        return false;
    }

    bool hashOk = false;
    QString error;
    const QByteArray hash = fileSha256(archivePath, &hashOk, &error);
    if (!hashOk) {
        if (detail)
            *detail = error;
        return false;
    }
    if (hash != QByteArray(kArchiveSha256)) {
        if (detail) {
            *detail = trMessage("reader.reframework.archive_sha_mismatch")
                          .arg(QString::fromLatin1(kArchiveSha256), QString::fromLatin1(hash));
        }
        return false;
    }

    if (detail)
        *detail = trMessage("reader.reframework.archive_verified")
                      .arg(QString::fromLatin1(kTag));
    return true;
}

bool RiseReFrameworkManager::verifyBundledArchive(QString *detail) const
{
    return verifyArchive(bundledArchivePath(m_applicationDir), detail);
}

QString RiseReFrameworkManager::upstreamTag()
{
    return QString::fromLatin1(kTag);
}

QString RiseReFrameworkManager::archiveAssetName()
{
    return QString::fromLatin1(kAssetName);
}

QString RiseReFrameworkManager::archiveUrl()
{
    return QString::fromLatin1(kArchiveUrl);
}

qint64 RiseReFrameworkManager::expectedArchiveSize()
{
    return kExpectedArchiveSize;
}

QByteArray RiseReFrameworkManager::expectedArchiveSha256()
{
    return QByteArray(kArchiveSha256);
}

QString RiseReFrameworkManager::bundledArchivePath(const QString &applicationDir)
{
    return QDir(applicationDir).filePath(
        QStringLiteral("third_party/REFramework/MHRISE-v1.5.9.1.zip"));
}

QString RiseReFrameworkManager::luaSourcePath(const QString &applicationDir)
{
    // The release payload keeps the Lua beside the runtime's autorun copy.
    // Keeping one canonical source avoids silently installing a stale second
    // copy when the executable is relocated.
    return QDir(applicationDir).filePath(
        QStringLiteral("reframework/autorun/mhr-overlay-damage.lua"));
}
} // namespace mhw
