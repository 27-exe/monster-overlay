#pragma once

// Shared locale persistence for the console <-> overlay handshake.
//
// The control console is the sole writer of
//   ~/.config/monster-overlay/monster-overlay.conf
// which already carries the section-mask state:
//
//   player=<hex32>
//   monster=<hex32>
//   damage=<hex32>
//   locale=zh-CN        <- added with the i18n feature
//
// The overlay never writes this file; it reads the `locale=` line at
// startup (unless --locale overrides) and polls it while running — see
// locale_sync.h. Keep this header-only: both monster-control and
// monster-overlay (plus the monster-locale-tests target) include it
// directly, so it must not add a translation unit to any target.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>

namespace mhw {

// Canonical path. Mirrors ControlPanel's private maskConfigPath() — the
// mask writer and this helper must never disagree.
inline QString localeConfPath()
{
    const QString dir = QStandardPaths::writableLocation(
                            QStandardPaths::GenericConfigLocation)
                        + QStringLiteral("/monster-overlay");
    return dir + QStringLiteral("/monster-overlay.conf");
}

// Value of the `locale=` line, or an empty string when the file or the
// line is absent (first run / older config). Never throws; malformed
// content degrades to "no locale".
inline QString readLocaleFromConf(const QString& path = localeConfPath())
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QStringLiteral("locale=")))
            return line.mid(7).trimmed();
    }
    return {};
}

// Atomically set `locale=` while preserving every other line (the mask
// rows). QSaveFile gives us write-temp+rename so the overlay's poll can
// never observe a half-written file. Returns false when the write failed;
// the previous file content is left intact in that case.
inline bool writeLocaleToConf(const QString& locale,
                              const QString& path = localeConfPath())
{
    if (locale.isEmpty())
        return false;

    QStringList keep;
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            in.setEncoding(QStringConverter::Utf8);
            while (!in.atEnd()) {
                const QString line = in.readLine();
                if (!line.trimmed().startsWith(QStringLiteral("locale=")))
                    keep << line;
            }
        }
    }
    keep << QStringLiteral("locale=") + locale;

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QByteArray data;
    for (const QString& line : keep)
        data += line.toUtf8() + '\n';
    if (out.write(data) != data.size())
        return false;
    return out.commit();
}

} // namespace mhw
