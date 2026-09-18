#include "core/map_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace mhw {
namespace {

// Append `dir` unless it is empty or already present — keeps the priority
// order stable and never probes the same directory twice.
void appendUnique(QStringList &out, const QString &dir)
{
    if (dir.isEmpty())
        return;
    const QString clean = QDir::cleanPath(dir);
    if (clean.isEmpty() || clean == QLatin1String(".") || out.contains(clean))
        return;
    out << clean;
}

} // namespace

QStringList dataSearchDirs(const QString &appDir, const QString &xdgDataHome,
                           const QStringList &xdgDataDirs)
{
    QStringList dirs;

    // 1. next to the binary — how the release tarball ships
    if (!appDir.isEmpty())
        appendUnique(dirs, appDir + QStringLiteral("/data"));

    // 2. user data dir — where install.sh puts the maps
    QString home = xdgDataHome;
    if (home.isEmpty())
        home = QDir::homePath() + QStringLiteral("/.local/share");
    appendUnique(dirs, home + QStringLiteral("/monster-overlay/data"));

    // 3. system data dirs
    for (const QString &base : xdgDataDirs)
        appendUnique(dirs, base + QStringLiteral("/monster-overlay/data"));

    return dirs;
}

QStringList defaultDataSearchDirs()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString xdgDataHome = qEnvironmentVariable("XDG_DATA_HOME");
    const QStringList xdgDataDirs = qEnvironmentVariable("XDG_DATA_DIRS")
                                        .split(QLatin1Char(':'), Qt::SkipEmptyParts);
    return dataSearchDirs(appDir, xdgDataHome, xdgDataDirs);
}

QString firstDataDirContaining(const QStringList &dirs, const QString &glob,
                               const QString &fallbackPath)
{
    for (const QString &dir : dirs) {
        const QDir candidate(dir);
        if (!candidate.exists())
            continue;
        if (!candidate.entryList({glob}, QDir::Files).isEmpty())
            return candidate.absolutePath();
    }
    if (!fallbackPath.isEmpty()) {
        const QFileInfo fi(fallbackPath);
        if (fi.exists())
            return fi.absolutePath();
    }
    return {};
}

QString resolveDataFile(const QString &explicitPath, const QString &fileName,
                        const QStringList &dirs, const QString &fallbackPath)
{
    if (!explicitPath.isEmpty())
        return explicitPath;

    if (!fileName.isEmpty()) {
        for (const QString &dir : dirs) {
            const QString candidate = dir + QLatin1Char('/') + fileName;
            if (QFileInfo::exists(candidate))
                return candidate;
        }
    }

    if (!fallbackPath.isEmpty() && QFileInfo::exists(fallbackPath))
        return fallbackPath;

    // Nothing found. Return a concrete candidate anyway (the release layout
    // first, then the build default) so the reader's error message names a
    // path the user can actually act on instead of an empty string.
    if (!dirs.isEmpty())
        return dirs.first() + QLatin1Char('/') + fileName;
    return fallbackPath;
}

} // namespace mhw
