// SPDX-License-Identifier: Apache-2.0

#include "rise/reframework/reframework_config.h"

#include "core/string_table.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <optional>

namespace mhw {

namespace {

// i18n: the atomic-write failures reach the caller verbatim and are
// displayed raw, so they must flip with the active locale. Lookup happens
// at the point of production — never cached — so a locale change shows up
// in the next attempt. Mirrors the pattern in src/mhw_reader.cpp /
// src/rise/mhr_reader.cpp.
inline QString trMessage(const QString &key) { return StringTable::instance().tr(key); }

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

} // namespace

QString rewriteReFrameworkConfig(const QString &original,
                                 const QList<ReFrameworkConfigSetting> &settings)
{
    if (settings.isEmpty())
        return original;

    // Split keeping the separators so every foreign line — including its
    // original CRLF or LF — is reproduced byte for byte. REFramework writes
    // this file itself with CRLF, and we must not silently convert it.
    QStringList lines = original.split(QStringLiteral("\n"));
    // QString::split on a trailing newline yields one empty tail element that
    // does not represent a line; remember it and re-attach it at the end.
    // An EMPTY original splits to a single empty element as well, and that
    // element is not a line either — it is the whole (absent) file.
    const bool hadTrailingNewline = !original.isEmpty()
                                 && lines.last().isEmpty();
    if (hadTrailingNewline)
        lines.removeLast();
    else if (original.isEmpty())
        lines.clear();

    QSet<QString> pending;
    for (const auto &setting : settings)
        pending.insert(setting.key);

    QStringList result = lines;
    for (auto &line : result) {
        const QString body = line.trimmed();
        if (body.isEmpty() || body.startsWith(QLatin1Char('#')))
            continue;                      // blank or comment: not ours
        const int eq = body.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;                      // not a key=value line
        const QString key = body.left(eq).trimmed();
        const auto it = std::find_if(settings.cbegin(), settings.cend(),
                                     [&key](const ReFrameworkConfigSetting &s) {
                                         return s.key == key;
                                     });
        if (it == settings.cend())
            continue;                      // foreign setting: never touched
        // Replace the value, keep the line's own terminator.
        // endsWith, not lastIndexOf: a stray CR in the middle of a line
        // (malformed file, hand edit) must not be treated as a line
        // terminator, or that CR would swallow everything before it.
        const QString terminator = line.endsWith(QLatin1Char('\r'))
            ? QStringLiteral("\r")
            : QString();
        line = it->key + QLatin1Char('=') + it->value + terminator;
        pending.remove(it->key);
    }

    // Keys with no existing line are appended. The terminator we add matches
    // whatever the file already uses so a mixed file stays consistent.
    for (const auto &setting : settings) {
        if (!pending.contains(setting.key))
            continue;
        const QString terminator = original.contains(QLatin1String("\r\n"))
            ? QStringLiteral("\r\n")
            : QStringLiteral("\n");
        result.append(setting.key + QLatin1Char('=') + setting.value + terminator);
    }

    QString out = result.join(QLatin1Char('\n'));
    if (hadTrailingNewline)
        out += QLatin1Char('\n');
    return out;
}

namespace {

// The single key this feature owns. `true` makes REFramework persist the
// menu open/closed state across launches; `false` is the upstream default and
// the reason the menu reopens every time.
constexpr const char *kMenuStateKey = "REFrameworkConfig_RememberMenuState";

// Reads the whole file. Returns an empty optional when the open failed
// (absent, unreadable, a directory, ...) so callers can tell "cannot
// inspect this file" apart from "this file is legitimately zero bytes" —
// conflating the two makes an emptied config file unwritable forever.
std::optional<QByteArray> readAllBytes(const QString &path)
{
    // Binary mode: QIODevice::Text would translate CRLF to LF on read and
    // rewrite every line ending in the file, which is exactly the kind of
    // unrelated change this feature must never make.
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return f.readAll();
}

// Reads just our key out of one config file. Returns an empty optional when
// the file does not exist or the key is absent, so "no file" and "file without
// our key" can be told apart from "file says false".
std::optional<bool> readMenuStateKey(const QString &path)
{
    const std::optional<QByteArray> bytes = readAllBytes(path);
    if (!bytes.has_value() || bytes->isEmpty())
        return std::nullopt;
    const QStringList lines =
        QString::fromUtf8(*bytes).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        if (line.left(eq).trimmed() == QLatin1String(kMenuStateKey)) {
            const QString value = line.mid(eq + 1).trimmed();
            return value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
        }
    }
    return std::nullopt;
}

} // namespace

QStringList reframeworkConfigPaths(const QString &gameDir)
{
    QStringList paths;
    const QString primary = QDir(gameDir).filePath(
        QStringLiteral("re2_fw_config.txt"));
    paths.append(primary);

    // Upstream fallback: %APPDATA%/REFramework, which under Proton lives in
    // the game's own compat prefix. Steam exports that path to the process it
    // launches, so read it from the environment rather than guessing a prefix.
    const QString prefixRoot = qEnvironmentVariable("STEAM_COMPAT_DATA_PATH");
    if (!prefixRoot.isEmpty()) {
        const QDir users(prefixRoot + QStringLiteral("/drive_c/users"));
        if (users.exists()) {
            const QStringList entries =
                users.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &user : entries) {
                const QString roaming = users.filePath(user)
                    + QStringLiteral("/AppData/Roaming/REFramework/re2_fw_config.txt");
                if (!paths.contains(roaming))
                    paths.append(roaming);
            }
        }
    }

    // Native-ish fallback for a console started outside Steam, where no
    // STEAM_COMPAT_DATA_PATH exists at all.
    const QString local = QDir::home().filePath(
        QStringLiteral(".local/share/REFramework/re2_fw_config.txt"));
    if (!paths.contains(local))
        paths.append(local);
    return paths;
}

ReFrameworkMenuStateReport queryReFrameworkMenuState(const QString &gameDir)
{
    ReFrameworkMenuStateReport report;
    report.paths = reframeworkConfigPaths(gameDir);

    bool sawAny = false;
    for (const QString &path : report.paths) {
        const std::optional<bool> value = readMenuStateKey(path);
        if (!value.has_value())
            continue;
        sawAny = true;
        // Any copy already saying true wins: that is the file REFramework
        // would read, so the fix is in effect regardless of the others.
        if (*value) {
            report.state = ReFrameworkMenuState::Overridden;
            return report;
        }
    }
    report.state = sawAny ? ReFrameworkMenuState::Default
                          : ReFrameworkMenuState::Unknown;
    return report;
}

bool applyReFrameworkMenuStateFix(const QString &gameDir, bool restoreDefault,
                                  ReFrameworkMenuStateReport *report)
{
    const QList<ReFrameworkConfigSetting> settings{
        {QLatin1String(kMenuStateKey),
         restoreDefault ? QStringLiteral("false") : QStringLiteral("true")}};

    if (report) {
        report->paths = reframeworkConfigPaths(gameDir);
        report->written.clear();
    }

    bool wroteAny = false;
    bool allAlreadyCorrect = true;
    for (const QString &configPath : reframeworkConfigPaths(gameDir)) {
        const std::optional<bool> current = readMenuStateKey(configPath);
        const bool target = !restoreDefault;
        const std::optional<QByteArray> existingBytes = readAllBytes(configPath);
        if (!existingBytes.has_value()) {
            // The file exists but we could not read it (permissions, a
            // directory, ...): stop rather than clobber what we cannot
            // inspect. A readable zero-byte file is NOT this case — it is
            // handed to the rewriter below, which happily appends our key.
            if (QFileInfo::exists(configPath))
                continue;
            // Only create a file where REFramework would actually look, and
            // only if that directory exists — do not litter the filesystem.
            if (!QFileInfo::exists(QFileInfo(configPath).absolutePath()))
                continue;
        } else if (current.has_value() && *current == target) {
            continue;   // already right; writing would only churn the file
        }
        allAlreadyCorrect = false;

        // Keep a copy before touching it, so either direction can be undone
        // by hand. Only the first write creates one: a later press must not
        // overwrite the original with an already-modified version, which
        // would destroy the only rollback path.
        const QString backupPath = configPath
            + QStringLiteral(".pre-monster-overlay");
        if (QFileInfo::exists(configPath) && !QFileInfo::exists(backupPath)
            && !QFile::copy(configPath, backupPath)) {
            continue;   // could not make the change reversible
        }

        // A readable-but-empty file is a valid starting point: the rewriter
        // turns it into "our key only". An unreadable one was skipped above.
        const QString existing =
            QString::fromUtf8(existingBytes.value_or(QByteArray()));
        const QString rewritten =
            rewriteReFrameworkConfig(existing, settings);
        if (rewritten == existing)
            continue;
        if (!writeBytesAtomically(configPath, rewritten.toUtf8(), nullptr))
            continue;   // unwritable candidate: try the next one
        wroteAny = true;
        if (report)
            report->written.append(configPath);
    }

    if (report) {
        const ReFrameworkMenuState target = restoreDefault
            ? ReFrameworkMenuState::Default
            : ReFrameworkMenuState::Overridden;
        report->state = (wroteAny || allAlreadyCorrect) ? target
                                                        : ReFrameworkMenuState::Unknown;
    }
    return wroteAny || allAlreadyCorrect;
}

} // namespace mhw
