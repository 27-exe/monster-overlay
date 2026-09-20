#include "core/map_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVector>

#include <algorithm>
#include <array>
#include <utility>

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

void appendUniqueCandidate(QStringList &out, const QString &path)
{
    if (!path.isEmpty() && !out.contains(path))
        out << path;
}

bool isPathCharacter(QChar c)
{
    return c.isLetterOrNumber() || c == u'_' || c == u'-' || c == u'.'
        || c == u'~' || c == u'/';
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
    if (home.isEmpty() || !QDir::isAbsolutePath(home))
        home = QDir::homePath() + QStringLiteral("/.local/share");
    if (QDir::isAbsolutePath(home))
        appendUnique(dirs, home + QStringLiteral("/monster-overlay/data"));

    // 3. system data dirs
    for (const QString &base : xdgDataDirs) {
        if (QDir::isAbsolutePath(base))
            appendUnique(dirs, base + QStringLiteral("/monster-overlay/data"));
    }

    return dirs;
}

QStringList defaultDataSearchDirs()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString xdgDataHome = qEnvironmentVariable("XDG_DATA_HOME");
    QString xdgDataDirsValue = qEnvironmentVariable("XDG_DATA_DIRS");
    if (xdgDataDirsValue.isEmpty())
        xdgDataDirsValue = QStringLiteral("/usr/local/share:/usr/share");
    const QStringList xdgDataDirs =
        xdgDataDirsValue.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    return dataSearchDirs(appDir, xdgDataHome, xdgDataDirs);
}

QString developmentMapFallback(const QString &fileName)
{
    if (fileName.isEmpty())
        return {};
    const QString appDir = QCoreApplication::applicationDirPath();
    if (appDir.isEmpty())
        return {};
    return QDir(appDir).filePath(QStringLiteral("../data/") + fileName);
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

QStringList worldMapCandidates(const QString &explicitPath,
                               const QString &fileName,
                               const QStringList &dirs,
                               const QString &fallbackPath)
{
    if (!explicitPath.isEmpty())
        return {explicitPath};

    QStringList candidates;
    if (!fileName.isEmpty()) {
        for (const QString &dir : dirs) {
            if (!dir.isEmpty())
                appendUniqueCandidate(candidates, QDir(dir).filePath(fileName));
        }
    }
    appendUniqueCandidate(candidates, fallbackPath);
    return candidates;
}

QStringList riseMapCandidates(const QString &explicitPath,
                              const QStringList &dirs,
                              const QString &fallbackPath)
{
    if (!explicitPath.isEmpty())
        return {explicitPath};

    const QRegularExpression versionPattern(
        QStringLiteral("^MonsterHunterRise\\.(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)\\.map$"));
    struct Candidate {
        QString path;
        std::array<qulonglong, 4> version{};
    };

    QStringList result;
    for (const QString &dataDir : dirs) {
        QVector<Candidate> inDirectory;
        const QDir dir(dataDir);
        const QStringList names =
            dir.entryList({QStringLiteral("MonsterHunterRise.*.map")}, QDir::Files);
        for (const QString &name : names) {
            const QRegularExpressionMatch match = versionPattern.match(name);
            if (!match.hasMatch())
                continue;

            Candidate candidate;
            candidate.path = dir.filePath(name);
            bool validVersion = true;
            for (int component = 0; component < 4; ++component) {
                bool ok = false;
                candidate.version[component] =
                    match.captured(component + 1).toULongLong(&ok);
                if (!ok) {
                    validVersion = false;
                    break;
                }
            }
            if (validVersion)
                inDirectory.push_back(std::move(candidate));
        }
        std::sort(inDirectory.begin(), inDirectory.end(),
                  [](const Candidate &left, const Candidate &right) {
                      if (left.version != right.version)
                          return left.version > right.version;
                      return left.path > right.path;
                  });
        for (const Candidate &candidate : inDirectory)
            appendUniqueCandidate(result, candidate.path);
    }
    appendUniqueCandidate(result, fallbackPath);
    return result;
}

QString redactHomePath(const QString &text, const QString &home)
{
    if (home.isEmpty())
        return text;
    const QString cleanHome = QDir::cleanPath(home);
    if (cleanHome == QLatin1String("/") || !QDir::isAbsolutePath(cleanHome))
        return text;

    QString result = text;
    qsizetype from = 0;
    while (from < result.size()) {
        const qsizetype start = result.indexOf(cleanHome, from);
        if (start < 0)
            break;
        const qsizetype end = start + cleanHome.size();
        const bool startsAtBoundary = start == 0 || !isPathCharacter(result.at(start - 1));
        const bool endsAtBoundary = end == result.size() || result.at(end) == u'/'
            || !isPathCharacter(result.at(end));
        if (startsAtBoundary && endsAtBoundary) {
            result.replace(start, cleanHome.size(), QStringLiteral("~"));
            from = start + 1;
        } else {
            from = start + cleanHome.size();
        }
    }
    return result;
}

} // namespace mhw
