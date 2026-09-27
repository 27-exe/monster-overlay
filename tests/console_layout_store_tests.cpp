// SPDX-License-Identifier: Apache-2.0
// Unit tests for mhw::ConsoleLayoutStore — the window-layout persistence
// the control console used to open inline in seven places inside
// ControlPanel (window geometry, window state, left splitter, main
// splitter, stage height, canvas zoom).
//
// The contract this test pins down is deliberately narrow and literal:
// every key spelling, every default and the read-back behaviour of a
// value written by an earlier instance. A rename, a re-defaulted value
// or a lost key is a regression the user sees as a forgotten window
// position, so the assertions here assert the spellings, not just "some
// value came back".
//
// Isolation: every case writes through a (organization, application)
// pair that exists nowhere else on this machine, under a
// XDG_CONFIG_HOME redirected into a QTemporaryDir. The developer's real
// ~/.config/monster-overlay/monster-control.conf is never opened, let
// alone written, and two test runs cannot observe each other's file.

#include "ui/viewmodel/console_layout_store.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int failures = 0;
void check(bool cond, const char *what)
{
    if (cond) std::printf("PASS: %s\n", what);
    else { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

// Redirection must happen before the first settings object is built —
// the backend caches the writable-config location per process on some
// platforms — so the QCoreApplication is created after this in main().
void sandboxConfigDir(QTemporaryDir &tmp)
{
    qputenv("XDG_CONFIG_HOME", tmp.path().toLocal8Bit());
}

// A second instance must observe what the first one wrote — that is the
// whole point of a settings round-trip, and the only way to catch a
// missing flush is to construct a fresh store against the same identity.
const char *kOrg = "monster-overlay-console-layout-tests";
const char *kApp = "console-layout-store-cases";

// Qt writes user-scope settings as <org>/<app>.conf on Linux.
QString expectedFile(const QString &configDir, const QString &org,
                     const QString &app)
{
    return configDir + "/" + org + "/" + app + ".conf";
}

// Non-trivial byte arrays: saveGeometry()/saveState() are opaque, but a
// store that returns the empty array for a stored empty-looking value
// would be indistinguishable from "nothing stored", so the round-trip
// needs payloads that cannot be confused with a default.
const QByteArray kStateA = QByteArray::fromRawData(
    "\x01\x00\x00\x00\xff\x00", 6);

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::fprintf(stderr, "FATAL: no temp dir\n");
        return 2;
    }
    sandboxConfigDir(tmp);
    const QString configDir = tmp.path();

    QCoreApplication app(argc, argv);

    // ---------------------------------------------------------------- defaults
    //
    // A store pointed at an identity with no settings file must return
    // exactly the defaults the console documented — the zoom default is
    // a user-visible contract (the preview opens at 2.0x) and the stage
    // height default is what the animation restores before the first drag.
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        check(!QFileInfo::exists(expectedFile(configDir,
                                              QString::fromLatin1(kOrg),
                                              QString::fromLatin1(kApp))),
              "no settings file exists before any write");
        check(qFuzzyCompare(store.zoom(), mhw::kConsoleZoomDefault),
              "zoom defaults to 2.0 on a fresh store");
        check(store.zoom() == mhw::kConsoleZoomDefault,
              "zoom default is exactly 2.0 (not merely close)");
        check(store.splitterLayout().splitterState.isEmpty(),
              "main splitter state is empty on a fresh store");
        check(store.splitterLayout().stageHeight
                  == mhw::kConsoleStageHeightDefault,
              "stage height defaults to 570 on a fresh store");
        check(store.leftSplitterState().isEmpty(),
              "left splitter state is empty on a fresh store");
        check(store.windowFrame().geometry.isEmpty()
                  && store.windowFrame().windowState.isEmpty(),
              "geometry and window state are empty on a fresh store");
    }

    // Default construction binds the application's own identity. This
    // must not throw, and the organization/application are whatever
    // QCoreApplication has (unset here -> empty names), so the only
    // assertion available is that reads return defaults rather than
    // crashing.
    {
        mhw::ConsoleLayoutStore store;
        check(qFuzzyCompare(store.zoom(), mhw::kConsoleZoomDefault),
              "default-constructed store reads zoom without crashing");
        check(store.windowFrame().geometry.isEmpty(),
              "default-constructed store reads geometry without crashing");
    }

    // ---------------------------------------------------------------- geometry
    //
    // The two window keys travel together: the console writes both in one
    // block on close and reads both in one block on open. Writing one
    // must not disturb the other, in either order.
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        store.saveWindowFrame(kStateA, 1.5, kStateA);
    }
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        const mhw::ConsoleLayoutStore::WindowFrame frame = store.windowFrame();
        check(frame.geometry == kStateA,
              "geometry round-trips through a second store instance");
        check(frame.windowState == kStateA,
              "window state round-trips through a second store instance");
        check(store.zoom() == 1.5,
              "zoom round-trips with the frame it was written beside");
    }
    // Overwrite with different payloads: a store that merged instead of
    // replacing would leave stale bytes behind.
    {
        const QByteArray second = QByteArray::fromRawData(
            "\x02\x00\x00\x00", 4);
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        store.saveWindowFrame(second, 3.0, QByteArray());
    }
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        const mhw::ConsoleLayoutStore::WindowFrame frame = store.windowFrame();
        check(frame.geometry == QByteArray::fromRawData("\x02\x00\x00\x00", 4),
              "geometry is replaced, not merged, on rewrite");
        check(frame.windowState.isEmpty(),
              "window state can be written back empty");
    }

    // ---------------------------------------------------------------- zoom
    //
    // 2.0 is the documented default and 1.0 is the fallback the console
    // writes when there is no canvas to ask, so a 1.0 write followed by a
    // read must return 1.0 — not the 2.0 default. That is the case a
    // default-vs-stored confusion would break.
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        store.saveWindowFrame(QByteArray(), 1.0, QByteArray());
    }
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        check(store.zoom() == 1.0,
              "stored zoom 1.0 is read back, not shadowed by the 2.0 default");
    }
    // Fractional zoom survives the QVariant round-trip the settings
    // backend performs.
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        store.saveWindowFrame(QByteArray(), 2.5, QByteArray());
    }
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        check(store.zoom() == 2.5,
              "fractional zoom 2.5 round-trips");
    }

    // ---------------------------------------------------------- left splitter
    //
    // Written on every left-splitter move, one key only.
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        store.saveLeftSplitterState(kStateA);
    }
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        check(store.leftSplitterState() == kStateA,
              "left splitter state round-trips through a second instance");
        // The left-splitter write must not touch the frame keys.
        check(store.windowFrame().geometry.isEmpty()
                  && store.windowFrame().windowState.isEmpty(),
              "left splitter write leaves the window frame keys untouched");
    }
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        store.saveLeftSplitterState(QByteArray());
    }
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        check(store.leftSplitterState().isEmpty(),
              "left splitter state can be written back empty");
    }

    // ---------------------------------------------------------- main splitter
    //
    // The two keys the splitterMoved slot writes together.
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        store.saveSplitterLayout(kStateA, 720);
    }
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        const mhw::ConsoleLayoutStore::SplitterLayout l = store.splitterLayout();
        check(l.splitterState == kStateA,
              "main splitter state round-trips through a second instance");
        check(l.stageHeight == 720,
              "stage height round-trips as the plain int that was written");
    }
    // A stage height of 0 is a legitimate collapsed-stage write, so the
    // reader must not confuse it with "unset" and fall back to 570.
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        store.saveSplitterLayout(QByteArray(), 0);
    }
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        check(store.splitterLayout().stageHeight == 0,
              "stage height 0 is read back, not defaulted to 570");
    }

    // ------------------------------------------------------------- key names
    //
    // Assert the spellings directly through a plain QSettings on the same
    // file, so a key silently prefixed or pluralized in the store still
    // fails here instead of being self-consistently wrong. The console's
    // existing config files use these exact spellings.
    {
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        store.saveWindowFrame(kStateA, 4.0, kStateA);
        store.saveLeftSplitterState(kStateA);
        store.saveSplitterLayout(kStateA, 600);
    }
    {
        const QString path = expectedFile(configDir,
                                          QString::fromLatin1(kOrg),
                                          QString::fromLatin1(kApp));
        QSettings raw(path, QSettings::IniFormat);
        check(raw.contains(QStringLiteral("ui/geometry")),
              "settings file exposes ui/geometry under the original spelling");
        check(raw.contains(QStringLiteral("ui/windowState")),
              "settings file exposes ui/windowState under the original spelling");
        check(raw.contains(QStringLiteral("ui/zoom")),
              "settings file exposes ui/zoom under the original spelling");
        check(raw.contains(QStringLiteral("ui/leftSplitter")),
              "settings file exposes ui/leftSplitter under the original spelling");
        check(raw.contains(QStringLiteral("ui/splitter")),
              "settings file exposes ui/splitter under the original spelling");
        check(raw.contains(QStringLiteral("ui/stageHeight")),
              "settings file exposes ui/stageHeight under the original spelling");
        check(raw.value(QStringLiteral("ui/zoom")).toDouble() == 4.0,
              "ui/zoom holds exactly the value the store wrote");
        check(raw.value(QStringLiteral("ui/stageHeight")).toInt() == 600,
              "ui/stageHeight holds exactly the value the store wrote");
    }

    // ------------------------------------------------------------- isolation
    //
    // Two stores with different identities must not see each other's
    // values — this is what keeps a test off a developer's real config,
    // and it is the property the console relies on to separate its own
    // identity from the panel overlay's "monster-overlay"/"panels" file.
    {
        const char *kOtherOrg = "monster-overlay-console-layout-tests-other";
        const char *kOtherApp = "unrelated-identity";
        mhw::ConsoleLayoutStore other(QString::fromLatin1(kOtherOrg),
                                      QString::fromLatin1(kOtherApp));
        other.saveWindowFrame(kStateA, 7.0, kStateA);
        mhw::ConsoleLayoutStore store(QString::fromLatin1(kOrg),
                                      QString::fromLatin1(kApp));
        check(store.zoom() == 4.0,
              "a different identity's values do not leak into this store");
    }

    std::printf("%s: %d failure(s)\n",
                failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
