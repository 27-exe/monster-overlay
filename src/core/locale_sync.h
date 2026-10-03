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
// Proposing a locale and applying it are deliberately TWO actions:
//
//   PROPOSE  localeSyncPoll()    pure detection — reads the conf and
//                                returns the locale it now asks for.
//                                Never records that the switch happened.
//   COMMIT   localeSyncAccept()  the caller calls this once it really
//                                applied the locale (StringTable::load()
//                                succeeded), to retire the proposal.
//
// The split exists because a proposal can be REFUSED: StringTable::load()
// fails when a locale directory is missing or unreadable, and the overlay
// then keeps its previous language. v0.11.3 and earlier committed
// `lastSeenLocale` inside the poll before the caller had loaded anything,
// so a refused flip was already recorded as delivered — and since the
// conf mtime had not changed either, the mtime short-circuit swallowed
// the candidate on every later poll. The UI sat on the old language
// until the user restarted the overlay. Now the poll records only what it
// READ, and a proposal stays outstanding until it is accepted (or
// superseded by a newer poll), so a refused flip is re-offered on the
// following polls and lands as soon as its resources are there again.
//
// Usage in the overlay's main loop:
//
//   auto sync = mhw::localeSyncInit(confPath);
//   // ... in the 250 ms tick:
//   const QString next = mhw::localeSyncPoll(sync, confPath, table.currentLocale());
//   if (!next.isEmpty() && table.load(next))
//       mhw::localeSyncAccept(sync, table.currentLocale());
//
// Header-only so every target that needs it can include it without a
// CMake source-list edit.

#include <QFileInfo>
#include <QString>

#include "core/locale_conf.h"

namespace mhw {

struct LocaleSyncState {
    // mtime of the conf file as of the last poll that actually read it
    // (-1 = absent). An I/O cache, NOT a record of what the UI applied:
    // advancing it inside the poll keeps the steady state at one stat per
    // tick instead of one file read per tick.
    qint64 lastMtimeMs{-1};
    // locale= value as read by the last poll ("" when the line was
    // absent). A detection record only — while a load is failing the UI
    // is still showing a different language.
    QString lastSeenLocale;
    // Locale proposed by a poll that the caller has NOT accepted yet
    // (empty when nothing is outstanding). It is what makes the next poll
    // re-offer a refused flip: the conf file itself did not change, so
    // without it the mtime short-circuit would hide the candidate until
    // the user flipped back and forth.
    QString pendingLocale;
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

// Poll `path`. Returns the locale the conf now asks for when the file
// changed since the last poll AND its locale= line differs from
// `currentLocale` AND it was not already settled by an earlier poll;
// returns an empty string when nothing changed, the file vanished, or the
// line matches the active locale.
//
// A non-empty result is a PROPOSAL, not a fact. The caller still has to
// apply it and call localeSyncAccept() on success; while it is
// outstanding the same proposal is returned by every following poll, so
// an application that fails (missing / corrupt locale resources) is
// retried instead of being silently dropped.
inline QString localeSyncPoll(LocaleSyncState& st, const QString& path,
                              const QString& currentLocale)
{
    const QFileInfo fi(path);
    const qint64 mtime = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : -1;

    if (mtime == st.lastMtimeMs) {
        // Nothing new on disk — the cheap steady-state no-op. A proposal
        // the caller has not accepted is still live though: the conf never
        // stopped asking for it, so it is offered again (that is the retry
        // path for a refused flip). It stops the moment the UI catches up.
        if (st.pendingLocale.isEmpty())
            return {};
        if (st.pendingLocale == currentLocale) {
            // The UI already shows what was proposed — nothing left to
            // push. Clear it so no later comparison trips over it.
            st.pendingLocale.clear();
            return {};
        }
        return st.pendingLocale;
    }

    st.lastMtimeMs = mtime;
    if (mtime < 0) {
        // The file vanished. There is nothing to propose, and a pending
        // flip cannot be honoured while it is gone — drop it rather than
        // re-offering forever; the poll will propose it again if the file
        // (and the same locale= row) comes back.
        st.pendingLocale.clear();
        return {};
    }

    const QString loc = readLocaleFromConf(path);
    // Compare against the PREVIOUS reading before overwriting it: a
    // repeated value is exactly the case that must not be re-proposed.
    const bool repeat = (loc == st.lastSeenLocale);
    st.lastSeenLocale = loc;

    if (loc.isEmpty() || loc == currentLocale) {
        // No locale= row (the request is gone) or the conf agrees with
        // what the UI already shows.
        st.pendingLocale.clear();
        return {};
    }
    if (repeat && st.pendingLocale != loc) {
        // The conf asks for a value this state has already seen and that
        // is not outstanding. That is the console rewriting the mask rows
        // (or any re-touch) without changing the language: it must not be
        // turned into a flip, or a --locale startup decision would be
        // undone by an unrelated write.
        st.pendingLocale.clear();
        return {};
    }

    // Fresh value, or the same one still waiting to be accepted.
    st.pendingLocale = loc;
    return loc;
}

// Commit a proposal: the caller applied it and `effectiveLocale` is what
// is now live (the value StringTable::currentLocale() reports after a
// successful load), which is normally the proposed locale but is read
// from the UI so the state mirrors reality rather than the proposal.
//
// Only the request that is actually being delivered is retired: a late or
// duplicate accept for a request that is no longer outstanding (the
// console has since asked for something else) must not bury the newer
// one. Such an accept is a no-op.
//
// Deliberately does NOT touch lastSeenLocale: every poll already records
// the value it read (see localeSyncPoll), so rewriting it here would be
// redundant at best and would let an accept for a locale the UI never
// showed poison the "already seen" suppression at best. It also does not
// roll back lastMtimeMs — that field describes the FILE, an I/O cache,
// and rolling it back would cost a file read per tick for the whole time
// the resources stay missing. The outstanding-ness of a request lives in
// pendingLocale, which is where the retry decision is made.
inline void localeSyncAccept(LocaleSyncState& st, const QString& effectiveLocale)
{
    if (st.pendingLocale == effectiveLocale)
        st.pendingLocale.clear();
}

} // namespace mhw
