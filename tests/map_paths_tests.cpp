// SPDX-License-Identifier: Apache-2.0
// Unit tests for the runtime data-file lookup (core/map_paths).
//
// These never touch /proc or the network: every case builds its own
// temporary directory tree and passes explicit inputs, so the resolver
// is exercised exactly as a relocated release binary would use it.

#include "core/map_paths.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;
void check(bool cond, const char *what)
{
    if (cond) std::printf("PASS: %s\n", what);
    else { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

// Create <dir>/<name> with placeholder content; parent dirs are created.
void touch(const QString &dir, const QString &name)
{
    QDir().mkpath(dir);
    QFile f(dir + QLatin1Char('/') + name);
    if (f.open(QIODevice::WriteOnly))
        f.write("Address X 0x1\n");
}

void restoreEnvironment(const char *name, bool wasSet, const QByteArray &value)
{
    if (wasSet)
        qputenv(name, value);
    else
        qunsetenv(name);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString tmp = QDir::tempPath() + QStringLiteral("/map-paths-tests-") +
                        QString::number(QCoreApplication::applicationPid());
    QDir(tmp).removeRecursively();
    QDir().mkpath(tmp);

    const QString worldName = QStringLiteral("MonsterHunterWorld.421810.map");
    const QString appData   = tmp + QStringLiteral("/app/data");
    const QString xdgHome   = tmp + QStringLiteral("/xdg-home");
    const QString xdgSys1   = tmp + QStringLiteral("/xdg-sys1");
    const QString xdgSys2   = tmp + QStringLiteral("/xdg-sys2");
    const QString fallback  = tmp + QStringLiteral("/src-tree/data/") + worldName;
    const QStringList dirs  = mhw::dataSearchDirs(tmp + QStringLiteral("/app"), xdgHome,
                                                  {xdgSys1, xdgSys2});

    check(!dirs.isEmpty() && dirs.first() == tmp + QStringLiteral("/app/data"),
          "appdir/data is the first search dir");
    check(dirs.contains(xdgHome + QStringLiteral("/monster-overlay/data")),
          "XDG_DATA_HOME/monster-overlay/data is searched");
    check(dirs.indexOf(xdgSys1 + QStringLiteral("/monster-overlay/data")) <
              dirs.indexOf(xdgSys2 + QStringLiteral("/monster-overlay/data")),
          "XDG_DATA_DIRS order is preserved");

    // The development fallback is computed from the running binary, so it is
    // always <appdir>/../data/<name> — never a machine-specific absolute path.
    check(mhw::developmentMapFallback(worldName) ==
              QDir(QCoreApplication::applicationDirPath())
                  .filePath(QStringLiteral("../data/") + worldName),
          "development fallback is appdir-relative");

    // 1. explicit --map wins and is returned verbatim even when missing.
    check(mhw::resolveDataFile(tmp + QStringLiteral("/nope.map"), worldName, dirs, fallback)
              == tmp + QStringLiteral("/nope.map"),
          "explicit path wins and is not validated");

    // 2. nothing anywhere -> the first candidate is named, so the reader's
    //    error message points at an actionable path (never an empty string)
    check(mhw::resolveDataFile(QString(), worldName, dirs, fallback)
              == appData + QLatin1Char('/') + worldName,
          "no candidate anywhere -> first candidate is named");
    check(mhw::resolveDataFile(QString(), worldName, {}, fallback) == fallback,
          "no search dirs at all -> build default is named");

    // 3. build-tree fallback is used when only it exists
    touch(QFileInfo(fallback).absolutePath(), worldName);
    check(mhw::resolveDataFile(QString(), worldName, dirs, fallback) == fallback,
          "build-tree fallback is the last resort");

    // 4. appdir/data beats the fallback
    touch(appData, worldName);
    check(mhw::resolveDataFile(QString(), worldName, dirs, fallback) == appData + "/" + worldName,
          "appdir/data beats the build-tree fallback");

    // 5. XDG home beats the fallback but loses to appdir
    touch(xdgHome + QStringLiteral("/monster-overlay/data"), worldName);
    check(mhw::resolveDataFile(QString(), worldName, dirs, fallback) == appData + "/" + worldName,
          "appdir/data still beats XDG");

    // 6. CJK directory names survive resolution unchanged
    const QString cjkDir = tmp + QStringLiteral("/游戏目录/data");
    const QString cjkFile = cjkDir + QLatin1Char('/') + worldName;
    touch(cjkDir, worldName);
    check(mhw::resolveDataFile(QString(), worldName, {cjkDir}, fallback) == cjkFile,
          "CJK directory path resolves byte-exact");

    // Shared candidate lists preserve directory priority while allowing the
    // final loader to continue after a missing or malformed file.
    const QString explicitBad = tmp + QStringLiteral("/explicit-bad.map");
    check(mhw::worldMapCandidates(explicitBad, worldName, dirs, fallback)
              == QStringList{explicitBad},
          "World explicit map is the only candidate (no implicit fallback)");
    check(mhw::worldMapCandidates(QString(), worldName, {appData, xdgSys1}, fallback)
              == QStringList{appData + QLatin1Char('/') + worldName,
                             xdgSys1 + QLatin1Char('/') + worldName,
                             fallback},
          "World candidates are every data dir in order, then fallback");

    // 7. Rise-style glob: first dir containing any version wins
    const QString riseDirA = tmp + QStringLiteral("/rise-a");
    const QString riseDirB = tmp + QStringLiteral("/rise-b");
    touch(riseDirB, QStringLiteral("MonsterHunterRise.16.0.2.0.map"));
    check(mhw::firstDataDirContaining({riseDirA, riseDirB},
                                      QStringLiteral("MonsterHunterRise.*.map")) == riseDirB,
          "glob scan skips empty dirs and returns the first match");
    check(mhw::firstDataDirContaining({riseDirA, tmp + QStringLiteral("/rise-c")},
                                      QStringLiteral("MonsterHunterRise.*.map"),
                                      riseDirB + QStringLiteral("/MonsterHunterRise.16.0.2.0.map"))
              == riseDirB,
          "glob scan falls back to the fallback directory");
    check(mhw::firstDataDirContaining({riseDirA}, QStringLiteral("MonsterHunterRise.*.map")).isEmpty(),
          "glob scan returns empty when nothing matches");

    // Rise ordering is directory-first, numeric-version-second. A newer map
    // in a lower-priority directory must not outrank any valid map in an
    // earlier directory, and a broad-glob false positive must not mask later
    // directories.
    const QString riseDirInvalid = tmp + QStringLiteral("/rise-invalid");
    const QString riseDirLater = tmp + QStringLiteral("/rise-later");
    touch(riseDirInvalid, QStringLiteral("MonsterHunterRise.boom.map"));
    touch(riseDirLater, QStringLiteral("MonsterHunterRise.16.0.2.0.map"));
    const QString riseFallback = tmp + QStringLiteral("/src-tree/data/MonsterHunterRise.1.0.0.0.map");
    check(mhw::riseMapCandidates(QString(), {riseDirInvalid, riseDirLater}, riseFallback)
              == QStringList{riseDirLater + QStringLiteral("/MonsterHunterRise.16.0.2.0.map"),
                             riseFallback},
          "invalid Rise filename in an earlier dir cannot mask a later valid map");

    const QString risePriorityA = tmp + QStringLiteral("/rise-priority-a");
    const QString risePriorityB = tmp + QStringLiteral("/rise-priority-b");
    touch(risePriorityA, QStringLiteral("MonsterHunterRise.9.2.0.0.map"));
    touch(risePriorityA, QStringLiteral("MonsterHunterRise.10.0.0.0.map"));
    touch(risePriorityB, QStringLiteral("MonsterHunterRise.99.0.0.0.map"));
    check(mhw::riseMapCandidates(QString(), {risePriorityA, risePriorityB}, riseFallback)
              == QStringList{risePriorityA + QStringLiteral("/MonsterHunterRise.10.0.0.0.map"),
                             risePriorityA + QStringLiteral("/MonsterHunterRise.9.2.0.0.map"),
                             risePriorityB + QStringLiteral("/MonsterHunterRise.99.0.0.0.map"),
                             riseFallback},
          "Rise candidates keep dir priority and sort four-part versions descending per dir");
    check(mhw::riseMapCandidates(explicitBad, {risePriorityA, risePriorityB}, riseFallback)
              == QStringList{explicitBad},
          "Rise explicit map is the only candidate (no implicit fallback)");

    // 8. duplicate inputs are collapsed, order preserved
    const QStringList dup = mhw::dataSearchDirs(tmp + QStringLiteral("/app"),
                                                tmp + QStringLiteral("/app"),
                                                {tmp + QStringLiteral("/app")});
    check(dup.count(tmp + QStringLiteral("/app/data")) == 1,
          "duplicate directories are collapsed");
    // A truly identical entry: XDG_DATA_HOME == an XDG_DATA_DIRS entry.
    const QStringList noDup = mhw::dataSearchDirs(tmp + QStringLiteral("/app"), tmp, {tmp});
    check(noDup.count(tmp + QStringLiteral("/monster-overlay/data")) == 1,
          "XDG_DATA_HOME and an identical XDG_DATA_DIRS entry collapse");

    // XDG paths are valid only when absolute. A bad XDG_DATA_HOME is treated
    // as unset (the standard ~/.local/share default), and relative / tilde
    // XDG_DATA_DIRS entries are ignored rather than resolved against cwd.
    const QStringList filteredXdg = mhw::dataSearchDirs(
        tmp + QStringLiteral("/app"), QStringLiteral("relative-home"),
        {QStringLiteral("relative-system"), QStringLiteral("~/shared"), xdgSys1});
    check(filteredXdg.contains(QDir::homePath() + QStringLiteral("/.local/share/monster-overlay/data")),
          "relative XDG_DATA_HOME falls back to the absolute home default");
    check(!filteredXdg.contains(QStringLiteral("relative-system/monster-overlay/data"))
              && !filteredXdg.contains(QStringLiteral("~/shared/monster-overlay/data")),
          "relative and tilde XDG_DATA_DIRS entries are ignored without expansion");
    check(filteredXdg.contains(xdgSys1 + QStringLiteral("/monster-overlay/data")),
          "absolute XDG_DATA_DIRS entry remains in the search list");

    const bool dataDirsWasSet = qEnvironmentVariableIsSet("XDG_DATA_DIRS");
    const QByteArray savedDataDirs = qgetenv("XDG_DATA_DIRS");
    qunsetenv("XDG_DATA_DIRS");
    const QStringList defaultXdgDirs = mhw::defaultDataSearchDirs();
    check(defaultXdgDirs.contains(QStringLiteral("/usr/local/share/monster-overlay/data"))
              && defaultXdgDirs.contains(QStringLiteral("/usr/share/monster-overlay/data"))
              && defaultXdgDirs.indexOf(QStringLiteral("/usr/local/share/monster-overlay/data"))
                     < defaultXdgDirs.indexOf(QStringLiteral("/usr/share/monster-overlay/data")),
          "unset XDG_DATA_DIRS uses /usr/local/share:/usr/share in canonical order");
    qputenv("XDG_DATA_DIRS", QByteArray());
    const QStringList emptyXdgDirs = mhw::defaultDataSearchDirs();
    check(emptyXdgDirs.contains(QStringLiteral("/usr/local/share/monster-overlay/data"))
              && emptyXdgDirs.contains(QStringLiteral("/usr/share/monster-overlay/data")),
          "empty XDG_DATA_DIRS uses the canonical system defaults");
    restoreEnvironment("XDG_DATA_DIRS", dataDirsWasSet, savedDataDirs);

    check(mhw::redactHomePath(QStringLiteral("path=/home/alice/file other=/home/alice2/file"),
                              QStringLiteral("/home/alice"))
              == QStringLiteral("path=~/file other=/home/alice2/file"),
          "home redaction replaces a complete path boundary without prefix damage");
    check(mhw::redactHomePath(QStringLiteral("home=/home/alice, user=/home/alice2"),
                              QStringLiteral("/home/alice"))
              == QStringLiteral("home=~, user=/home/alice2"),
          "home redaction recognizes punctuation after a complete HOME path");
    check(mhw::redactHomePath(QStringLiteral("/one/two"), QStringLiteral("/"))
              == QStringLiteral("/one/two")
              && mhw::redactHomePath(QStringLiteral("/one/two"), QString())
                     == QStringLiteral("/one/two"),
          "empty or root HOME never redacts every slash");

    QDir(tmp).removeRecursively();

    if (failures == 0)
        std::printf("\nmap-path-tests: ALL PASSED\n");
    else
        std::fprintf(stderr, "\nmap-path-tests: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
