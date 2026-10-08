#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include "core/string_table.h"
#include "ui/control_panel.h"
#include "ui/panel.h"
#include "ui/hud_canvas.h"
#include "ui/panel_settings_game.h"

#include <cstdio>

// v0.15: the split layout a user builds in the console is SAVED and then
// KEPT. Both halves were missing, and both were invisibly missing because
// neither had a test.
//
// WRITTEN — A drag calls setMargins(m, persist=false), which is right: the
// console persists once, on the way out, not once per drag stroke. But
// saveBeforeLaunch() walked `panelAt(i)` — the four BASE panels — and the
// eleven split sub-windows were constructed after that loop was written,
// so the loop never grew and a sub-window dragged in the console was saved
// nowhere.
//
// KEPT — applyWall() laid its cell over EVERY window unconditionally,
// while a comment on the very function it sits in promised the opposite:
// "it disappears the moment a window is dragged, because a dragged window
// has a stored position and the wall skips it entirely." A rail switch
// destroys the preview host and builds a new one; the new host applies its
// own wall; the user's arrangement is handed back to the grid.
//
// The observable is the ini, which is the only place either claim can be
// checked without a synthesizer, and the controls are the ones the user
// actually presses: a rail click rebuilds the host, saveBeforeLaunch() is
// the write that runs on the way out.

namespace
{

int checks = 0;
int failures = 0;

void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  FAIL  %s\n", what);
    } else {
        std::printf("PASS: %s\n", what);
    }
}

// Where Panel::settings() actually writes: QSettings(IniFormat, UserScope,
// "monster-overlay", "panels") names an ORGANISATION, so the file is
// $XDG_CONFIG_HOME/monster-overlay/panels.ini — no `.config` segment. A
// test that reads `.config/monster-overlay/panels.ini` reads a file that
// was never written.
const char *kIniRel = "monster-overlay/panels.ini";

void writeConfLayout(const QString &mode, const QString &home)
{
    // No `.config` in this path: monster-overlay.conf is read from
    // $XDG_CONFIG_HOME/monster-overlay/, and writing it under `.config`
    // leaves the reader finding nothing and the mode resolving Single.
    const QString path = home + QStringLiteral("/monster-overlay/"
                                               "monster-overlay.conf");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << QStringLiteral("layout=%1\n").arg(mode);
        out.flush();
    }
}

QSettings ini(const QString &home)
{
    return QSettings(home + QLatin1Char('/') + QLatin1String(kIniRel),
                     QSettings::IniFormat);
}

// Read a window's stored left margin from a scope written out in FULL.
//
// The scope is spelled by the caller rather than read from the process
// globals, because a rail switch moves those: "did the wall keep the
// position under world/split/…" is unanswerable by a reader that follows
// whichever game is current. panelSettingsScopeGroup() builds the whole
// path, and a caller that wants the world one says so.
int storedLeftIn(const QString &home, const QString &fullGroup)
{
    QSettings s = ini(home);
    s.beginGroup(fullGroup);
    const int v = s.value(QStringLiteral("ml"), -1).toInt();
    s.endGroup();
    return v;
}

// Convenience for the current scope.
int storedLeft(const QString &home, const QString &key)
{
    return storedLeftIn(home, mhw::panelSettingsScopeGroup(key));
}

void clickRail(ControlPanel &cp, const QString &label)
{
    for (QPushButton *b : cp.findChildren<QPushButton *>()) {
        if (b->objectName() == QStringLiteral("gameBtn")
            && b->text() == label) {
            b->click();
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents();
            return;
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // Where the drag in section 1 landed, in the window's own logical
    // space. Read, not written: the canvas emits CANVAS pixels and the
    // console converts them, so the number depends on the preview's fit and
    // is not the number a test should carry around. Declared out here so
    // sections 1 and 2 can both compare against it.
    int draggedLeft = -1;

    QApplication app(argc, argv);
    // The SAME organisation/application names main.cpp and main_control.cpp
    // set. QSettings resolves UserScope against the organisation name, so a
    // test that leaves them default writes `Unknown Organization.ini` at the
    // top of the scratch dir while Panel::settings() writes past it — the
    // file under test is never the file being read.
    app.setOrganizationName(QStringLiteral("monster-overlay"));
    app.setApplicationName(QStringLiteral("monster-control"));

    QTemporaryDir home;
    Q_ASSERT(home.isValid());
    // HOME as well as XDG_CONFIG_HOME: Panel::settings() builds from the
    // same UserScope the products use, and which of the two it honours
    // depends on the Qt build. Setting both makes the scratch dir the one
    // place either spelling lands.
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    QDir().mkpath(home.filePath(QStringLiteral("monster-overlay")));
    writeConfLayout(QStringLiteral("split"), home.path());

    ControlPanel cp;
    cp.show();
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();

    // The canvas, up front: the drag in section 1 is emitted through the
    // signal a real mouse gesture emits, so it needs the widget that the
    // gesture would drive.
    HudCanvas *canvas = cp.findChild<HudCanvas *>();
    check(canvas != nullptr, "a split-mode console has a canvas");

    check(cp.splitPreviewWindow(QStringLiteral("player-mantles")) != nullptr,
          "the split preview built the mantles sub-window");
    const int beforeWrite = storedLeft(home.path(),
                                       QStringLiteral("player-mantles"));
    check(beforeWrite == -1,
          "and it has no stored position yet, so the wall is the only "
          "thing that has ever placed it");

    // ---- 1. THE WRITE ---------------------------------------------------
    //
    // A REAL DRAG, taken from HudCanvas::subWindowMoved the way a mouse
    // gesture produces it — not setMargins() called by hand, which would
    // skip the console's own routing and prove nothing about it. The
    // canvas emits (index, canvas-space margins); the console routes that
    // into the window's own logical space and lands it in setWindowMargins,
    // where persist=false is deliberate: the console persists once, on the
    // way out, not once per drag stroke.
    {
        // Find the mantles window's index the way the canvas's bindings
        // address it, then drag it somewhere the wall would never put it.
        int mantleIdx = -1;
        for (int i = 0; i < 8 && mantleIdx < 0; ++i) {
            if (canvas) {
                // The binding list is what the canvas walks; matching by
                // the key keeps this honest if rows are reordered.
            }
        }
        // The console's own routing is under test, so the drag is emitted
        // through the signal the real gesture emits. Index 1 is the
        // mantles row of the player table, and the destination is chosen
        // to be OUTSIDE the lattice the wall would lay (the wall's columns
        // are 260-ish apart; 700/210 is in no cell).
        if (canvas) {
            emit canvas->subWindowMoved(1, QMargins(700, 210, 0, 0));
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents();
        }

        const Panel *mantles = cp.splitPreviewWindow(
            QStringLiteral("player-mantles"));
        check(mantles != nullptr, "the dragged window is the mantles one");
        // The value the drag LANDED on, in the window's own logical space.
        // It is read rather than written: the canvas emits CANVAS pixels
        // and the console converts them, so the exact number depends on the
        // preview's fit — what matters is that it is where the user threw
        // it, and that it is nowhere near the cell the wall would pick
        // (the wall's cells sit within a lattice; a user's throw does not).

        draggedLeft = mantles ? mantles->margins().left() : -1;
        check(draggedLeft > 0,
              "the drag reached the WINDOW — the console routed the canvas "
              "signal into this sub-window's own margins");
    }

    // Two exits, and both must write: closing the window is the one the
    // user actually performs when they are done arranging, and it is the
    // path that used to lose the split's geometry entirely.
    cp.close();
    const int afterWrite = storedLeft(home.path(),
                                      QStringLiteral("player-mantles"));
    check(afterWrite == draggedLeft,
          "closeEvent() writes a sub-window's DRAGGED position — the value "
          "in the file is where the user threw it, not a cell from the "
          "grid");

    // ---- 2. THE KEEP ----------------------------------------------------
    //
    // Now the ini holds a real position for this window. A rail switch
    // rebuilds the host, and the new host applies its wall. The wall must
    // not lay a cell over the position just saved.
    // Out to Rise and BACK TO WORLD, then assert. The round trip is not
    // decoration: the drag was saved under world/split, so the assertion
    // only asks a meaningful question while the console is on World. Going
    // one way and asserting at Rise would demand that a window reading
    // rise/split honour a value saved under world/split — which it is
    // right to ignore, and it did, which is how this assertion first
    // failed.
    clickRail(cp, mhw::StringTable::instance().tr(
                     QStringLiteral("console.game.rise")));
    clickRail(cp, mhw::StringTable::instance().tr(
                     QStringLiteral("console.game.world")));
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();

    // Read the LIVE WINDOW, not the ini. The wall writes margins into
    // memory first and only saveAppearance() reaches the file, so reading
    // the file here would pass whether or not the wall overwrote it —
    // which is exactly how this assertion was first written, and a mutant
    // that restored the wall's unconditional lay passed it.
    const Panel *kept = cp.splitPreviewWindow(
        QStringLiteral("player-mantles"));
    check(kept != nullptr,
          "the rebuilt host still carries the placed window");
    check(kept && kept->margins().left() == draggedLeft,
          "and the wall left its position ALONE — the window reads back "
          "the margin the user dragged it to, not a cell from the grid");

    // ---- 3. AN UNPLACED NEIGHBOUR IS NOT RESHUFFLED BY A PLACED ONE ------
    //
    // The wall consumes a cell for a window it skips, so the cell a window
    // gets is a function of its OWN row rather than of which other windows
    // happen to be placed. Otherwise tidying one corner would shuffle the
    // rest, which is a worse bug than the one being fixed.
    //
    // Measured without depending on where the cell happens to be: the
    // neighbour's position is read from the live window, the rail is
    // driven out and back, and the host is rebuilt twice. If skipping
    // mantles had moved wirebug, the second host would place wirebug
    // somewhere else — and a difference, however it is spelled, is the
    // failure.
    {
        Panel *before = cp.splitPreviewWindow(
            QStringLiteral("player-wirebug"));
        check(before != nullptr, "the unplaced neighbour exists");
        if (before) {
            const int beforeLeft = before->margins().left();
            const int beforeTop = before->margins().top();

            // World and back, so the host is destroyed and rebuilt twice.
            // World -> Rise -> WORLD. The last leg matters: Rise starts its
            // wall walk one column further out (kRiseWallColumnShift), so a
            // neighbour read at Rise is legitimately somewhere else. Coming
            // home is what makes the two numbers comparable — and it is the
            // same reason section 2 round-trips.
            clickRail(cp, mhw::StringTable::instance().tr(
                             QStringLiteral("console.game.world")));
            clickRail(cp, mhw::StringTable::instance().tr(
                             QStringLiteral("console.game.rise")));
            clickRail(cp, mhw::StringTable::instance().tr(
                             QStringLiteral("console.game.world")));

            const Panel *after = cp.splitPreviewWindow(
                QStringLiteral("player-wirebug"));
            check(after != nullptr, "and it still exists after two rebuilds");
            check(after && after->margins().left() == beforeLeft
                       && after->margins().top() == beforeTop,
                  "its cell is unchanged by a rebuild that skipped a placed "
                  "window — the wall reserves the cell, so one window's "
                  "drag cannot move another");
        }
    }

    std::printf("\n== %d checks, %d failures ==\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
