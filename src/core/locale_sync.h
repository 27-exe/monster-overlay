#pragma once

// Runtime locale-change detection for the overlay process.
//
// The console is the sole writer of monster-overlay.conf (see
// locale_conf.h); the overlay polls it from its paint loop (~1 s) and
// reloads the StringTable + repaints every panel when the `locale=` line
// changes. Polling (mtime stat + one tiny read on change) keeps the whole
// handshake free of IPC, sockets, watchers and their edge cases; a locale
// flip is a once-per-session event, so 1 s of latency is fine.
//
// Usage in the overlay's main loop:
//
//   auto sync = mhw::localeSyncInit(confPath);
//   // ... in the 250 ms tick:
//   const QString next = mhw::localeSyncPoll(sync, confPath, table.currentLocale());
//   if (!next.isEmpty()) { table.load(next); applyLocaleToUi(); }
//
// Header-only so every target that needs it can include it without a
// CMake source-list edit.

#include <QFileInfo>
#include <QString>

#include "core/locale_conf.h"

namespace mhw {

struct LocaleSyncState {
    // mtime of the conf file as of the last poll (-1 = absent).
    qint64 lastMtimeMs{-1};
    // locale= value at the last poll ("" when the line was absent).
    QString lastSeenLocale;
};

// Prime the state from the CURRENT file so the first poll only reports
// changes that happen after startup. Callers that honoured --locale (or
// a startup conf read) must not have that decision undone by the first
// poll reporting the pre-existing value.
inline LocaleSyncState localeSyncInit(const QString& path)
{
    LocaleSyncState st;
    const QFileInfo fi(path);
    if (fi.exists()) {
        st.lastMtimeMs = fi.lastModified().toMSecsSinceEpoch();
        st.lastSeenLocale = readLocaleFromConf(path);
    }
    return st;
}

// Poll `path`. Returns the new locale code when the conf file changed
// since the last poll AND its locale= line differs from `currentLocale`;
// returns an empty string when nothing changed, the file vanished, or the
// line matches the active locale.
inline QString localeSyncPoll(LocaleSyncState& st, const QString& path,
                              const QString& currentLocale)
{
    const QFileInfo fi(path);
    const qint64 mtime = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : -1;
    if (mtime == st.lastMtimeMs)
        return {};
    st.lastMtimeMs = mtime;
    if (mtime < 0)
        return {};
    const QString loc = readLocaleFromConf(path);
    if (loc.isEmpty() || loc == st.lastSeenLocale)
        return {};
    st.lastSeenLocale = loc;
    if (loc == currentLocale)
        return {};
    return loc;
}

} // namespace mhw
