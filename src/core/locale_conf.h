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

// ---------------------------------------------------------------- detection
//
// v0.9.2: a first run has no conf file, so the UI used to open in zh-CN for
// every user no matter what their desktop language was. Detection now asks
// the environment: a Chinese locale (zh*) selects zh-CN, anything else —
// including a missing, empty or `C`/`POSIX` locale — selects en-US, the
// shipping default. An explicit `--locale` or a `locale=` row (written only
// by `--locale` or the console's EN/CH chip) always wins, so a manual choice
// is sticky while a machine that never chose keeps following the desktop.

// "zh_CN.UTF-8@euro" -> "zh-cn"; "C"/"POSIX"/"" -> "" (not a language request).
inline QString normalizeLocaleCode(const QString& raw)
{
    QString value = raw.trimmed();
    const int dot = value.indexOf(QLatin1Char('.'));
    if (dot >= 0)
        value.truncate(dot);
    const int at = value.indexOf(QLatin1Char('@'));
    if (at >= 0)
        value.truncate(at);
    value.replace(QLatin1Char('_'), QLatin1Char('-'));
    value = value.toLower();
    if (value.isEmpty() || value == QLatin1String("c") || value == QLatin1String("posix"))
        return {};
    return value;
}

// Precedence mirrors what the desktop itself uses for messages:
// LC_ALL > LC_MESSAGES > LANG, then gettext's LANGUAGE list.
inline QString detectLocaleFromEnv(const QString& lcAll, const QString& lcMessages,
                                   const QString& lang,
                                   const QString& language = QString())
{
    for (const QString& raw : {lcAll, lcMessages, lang}) {
        const QString code = normalizeLocaleCode(raw);
        if (code.isEmpty())
            continue;
        return code.startsWith(QLatin1String("zh")) ? QStringLiteral("zh-CN")
                                                    : QStringLiteral("en-US");
    }
    for (const QString& entry : language.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString code = normalizeLocaleCode(entry);
        if (code.isEmpty())
            continue;
        return code.startsWith(QLatin1String("zh")) ? QStringLiteral("zh-CN")
                                                    : QStringLiteral("en-US");
    }
    return QStringLiteral("en-US");
}

inline QString systemLocale()
{
    return detectLocaleFromEnv(qEnvironmentVariable("LC_ALL"),
                               qEnvironmentVariable("LC_MESSAGES"),
                               qEnvironmentVariable("LANG"),
                               qEnvironmentVariable("LANGUAGE"));
}

// Startup policy, shared by the overlay (src/main.cpp) and the console
// (src/main_control.cpp) so the two can never disagree at launch:
//   --locale  >  conf `locale=` row  >  detected system locale
inline QString resolveStartupLocale(const QString& cliOverride, const QString& confLocale,
                                    const QString& detected)
{
    if (!cliOverride.isEmpty())
        return cliOverride;
    if (!confLocale.isEmpty())
        return confLocale;
    return detected;
}

inline QString resolveStartupLocale(const QString& cliOverride,
                                    const QString& path = localeConfPath())
{
    return resolveStartupLocale(cliOverride, readLocaleFromConf(path), systemLocale());
}

} // namespace mhw
