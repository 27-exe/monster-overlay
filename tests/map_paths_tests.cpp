// SPDX-License-Identifier: Apache-2.0
// Unit tests for the runtime data-file lookup (core/map_paths).
//
// These never touch /proc or the network: every case builds its own
// temporary directory tree and passes explicit inputs, so the resolver
// is exercised exactly as a relocated release binary would use it.

#include "core/map_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

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

    // 3. compile-time fallback is used when only it exists
    touch(QFileInfo(fallback).absolutePath(), worldName);
    check(mhw::resolveDataFile(QString(), worldName, dirs, fallback) == fallback,
          "compile-time fallback is the last resort");

    // 4. appdir/data beats the fallback
    touch(appData, worldName);
    check(mhw::resolveDataFile(QString(), worldName, dirs, fallback) == appData + "/" + worldName,
          "appdir/data beats the compile-time fallback");

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
          "glob scan falls back to the compile-time directory");
    check(mhw::firstDataDirContaining({riseDirA}, QStringLiteral("MonsterHunterRise.*.map")).isEmpty(),
          "glob scan returns empty when nothing matches");

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

    QDir(tmp).removeRecursively();

    if (failures == 0)
        std::printf("\nmap-path-tests: ALL PASSED\n");
    else
        std::fprintf(stderr, "\nmap-path-tests: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
