#pragma once

#include <QString>
#include <QStringList>
#include <QtTypes>

namespace mhw {

// Return the existing Linux Steam roots below `homeDirectory`, in standard
// priority order, with symlink aliases and duplicates collapsed.
QStringList defaultSteamRoots(const QString &homeDirectory);
QStringList defaultSteamRoots();

// Return existing Steam libraries reachable from explicit Steam roots. Each
// result is an absolute canonical directory; relative, missing, and duplicate
// entries are discarded. Both supported libraryfolders.vdf locations are read.
QStringList steamLibraryPaths(const QStringList &steamRoots);

// Locate the requested executable in Rise's Steam app (1446780).
QString findRiseInstallDir(const QStringList &steamRoots);
QString findRiseInstallDir();

// Locate a Steam app under explicit Steam roots. The returned directory is
// empty unless the app manifest names an install directory whose requested
// executable exists.
QString findSteamGameInstallDir(quint64 appId, const QString &executableName,
                                const QStringList &steamRoots = defaultSteamRoots());

} // namespace mhw
