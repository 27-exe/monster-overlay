#pragma once

#include <QString>
#include <QStringList>

namespace mhw {

// Runtime lookup for the address-map files shipped with the overlay.
//
// A release tarball keeps its maps next to the binary (<appdir>/data) and
// install.sh copies them under $XDG_DATA_HOME/monster-overlay/data. The
// development fallback (developmentMapFallback below) is a build-tree/CTest
// convenience only: a released binary must never depend on the build
// machine's source tree. Historically it did (compile-time absolute paths),
// which made every relocated release fail to load a map and render an empty
// HUD — the fallback is now computed at runtime and stays relative to the
// binary, so no binary carries a machine-specific path at all.
QStringList dataSearchDirs(const QString &appDir,
                           const QString &xdgDataHome,
                           const QStringList &xdgDataDirs);

// Same, reading QCoreApplication::applicationDirPath() and the XDG_* env
// variables. Requires a QCoreApplication instance.
QStringList defaultDataSearchDirs();

// Development/CTest fallback: <appdir>/../data/<fileName> — the standard
// CMake build-tree layout (binary in <repo>/build, maps in <repo>/data).
// Release payloads ship <appdir>/data, which the search list already covers
// first, so this only ever resolves on a development checkout. Computed at
// runtime so no machine-specific path is compiled into any binary.
QString developmentMapFallback(const QString &fileName);

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

// Build the complete World candidate list without checking file contents.
// Explicit --map is intentionally the sole candidate; otherwise every data
// directory contributes the fixed filename and the compile-time fallback is
// last. The order is shared by the product and monster-doctor.
QStringList worldMapCandidates(const QString &explicitPath,
                               const QString &fileName,
                               const QStringList &dirs,
                               const QString &fallbackPath = QString());

// Build the complete Rise candidate list. Each data directory is considered
// in priority order, and only exact four-component numeric version names are
// included, newest first within that directory. The fallback is last.
QStringList riseMapCandidates(const QString &explicitPath,
                              const QStringList &dirs,
                              const QString &fallbackPath = QString());

// Replace complete HOME path occurrences in diagnostic text. Empty and root
// HOME values are deliberately not redacted: replacing "/" would rewrite
// every slash, and a prefix-only replacement turns /home/user2 into ~2.
QString redactHomePath(const QString &text, const QString &home);

} // namespace mhw
