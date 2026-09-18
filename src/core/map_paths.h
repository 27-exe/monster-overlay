#pragma once

#include <QString>
#include <QStringList>

namespace mhw {

// Runtime lookup for the address-map files shipped with the overlay.
//
// A release tarball keeps its maps next to the binary (<appdir>/data) and
// install.sh copies them under $XDG_DATA_HOME/monster-overlay/data. The
// compile-time default (MHW_DEFAULT_MAP / MHR_DEFAULT_MAP) is a
// development/CTest convenience only: a released binary must never depend
// on the build machine's source tree. Historically it did, which made
// every relocated release fail to load a map and render an empty HUD.
QStringList dataSearchDirs(const QString &appDir,
                           const QString &xdgDataHome,
                           const QStringList &xdgDataDirs);

// Same, reading QCoreApplication::applicationDirPath() and the XDG_* env
// variables. Requires a QCoreApplication instance.
QStringList defaultDataSearchDirs();

// First directory in `dirs` holding at least one entry matching `glob`
// (e.g. "MonsterHunterRise.*.map"); when none does, the directory of
// `fallbackPath` if that path exists. Empty string when nothing matches.
QString firstDataDirContaining(const QStringList &dirs, const QString &glob,
                               const QString &fallbackPath = QString());

// Resolve `fileName` for runtime use. Priority:
//   1. explicitPath (--map) — returned verbatim, even when missing, so the
//      reported error names the path the user actually asked for;
//   2. <dir>/<fileName> for each dir in `dirs`, in order;
//   3. fallbackPath when that file exists.
// An empty return means "not found anywhere".
QString resolveDataFile(const QString &explicitPath, const QString &fileName,
                        const QStringList &dirs,
                        const QString &fallbackPath = QString());

} // namespace mhw
