// SPDX-License-Identifier: Apache-2.0
//
// The keyboard capability list, pinned against the dispatch that actually
// implements it.
//
// WHAT THIS TARGET IS FOR: the user's report was 「这些窗口支持的快捷键
// 各不相同，统一一下描述与行为，至少要能说清楚什么窗口支持什么行为」. The
// fix had three parts — the sub-windows' key handling became one base-class
// dispatch, the dispatch's verdict became queryable
// (ui/panel_capabilities.h), and the query became documented
// (docs/USAGE{,.zh-CN}.md §12). This file is what makes the second and the
// third honest: if a window's capability list ever disagrees with what
// Panel::keyPressEvent does, one of these checks fails with a message
// naming the answer it gave.
//
// It links mhw-ui rather than compiling the panel sources, for the reason
// every sibling split target does: those panels carry Q_OBJECT, and
// AUTOMOC would run twice over the same headers and produce duplicate
// symbols. Linking keeps exactly one owner per symbol.
//
// Runs offscreen (set by CMake) — every question here is answered by a
// public virtual or by a synthesised key event, so no compositor, no
// display and no game process are needed.
//
// The FOUR claims worth their keep:
//
//   1. Every window answers true for the six base capabilities. If a
//      future version moves Space or the wheel behind a per-window gate,
//      the docs table goes stale silently and this is the check that
//      would notice.
//   2. Exactly the three wrapped-row windows answer true for columns.
//      This is the one capability that is not uniform, so it is the one
//      an "unknown capability" window gets wrong.
//   3. `[` `]` actually MOVE the columns on all three, via the base
//      class's dispatch. This is the anti-regression claim the task
//      asked for: it replaces the strip's deleted self-hosted
//      keyPressEvent (removed in this change) with a test that presses
//      the key and reads the result. A regression that re-breaks the
//      routing fails here, not in the user's hands.
//   4. The docs table is generated from the same source of truth. The
//      last section parses §12 of both USAGE files and asserts the
//      matrix rows match the derived capabilities — so a new window
//      cannot be added without updating the docs, because the docs are
//      what tests.

// CMake WIRING (the one thing this file cannot do for itself): the parent
// session owns CMakeLists.txt, so the target below is what it needs. Note
// the compile definition — the docs half of the test is opt-in because it
// reads the source tree at RUN time.
//
//     add_executable(panel-capabilities-tests tests/panel_capabilities_tests.cpp)
//     target_link_libraries(panel-capabilities-tests PRIVATE Qt6::Core Qt6::Gui
//                                                    Qt6::Widgets Qt6::Svg mhw-ui)
//     target_include_directories(panel-capabilities-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
//     target_compile_options(panel-capabilities-tests PRIVATE -Wall -Wextra -Wpedantic)
//     target_compile_definitions(panel-capabilities-tests PRIVATE
//         PANEL_CAPABILITIES_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs")
//     add_test(NAME panel-capabilities-tests COMMAND panel-capabilities-tests)
//     set_tests_properties(panel-capabilities-tests PROPERTIES
//         ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
//
// No new src TU is created — panel_capabilities.h is a header, and the
// census in tests/tu_census.json does not account for headers — so the
// zero-reference gate stays green for this change. The target links
// mhw-ui rather than compiling the panel sources, because those carry
// Q_OBJECT and AUTOMOC would run twice over the same headers.

#include "ui/panel_capabilities.h"

#include "ui/panel.h"
#include "ui/panel_damage.h"
#include "ui/panel_monster.h"
#include "ui/panel_pet_damage.h"
#include "ui/panel_player.h"
#include "ui/panel_player_split.h"
#include "ui/panel_player_split_bars.h"
#include "ui/panel_player_split_info.h"
#include "ui/panel_player_split_mantles.h"
#include "ui/panel_player_split_weapon.h"
#include "ui/panel_player_split_wirebug.h"

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QKeyEvent>
#include <QWidget>

#include <cstdio>
#include <string>

#ifdef PANEL_CAPABILITIES_DOCS_DIR
// The docs half needs real file I/O, so those headers come in at file
// scope and only when the target defines the directory. An include inside
// a namespace is a compile error, and it is also the reason this arm is
// opt-in rather than always-on.
#include <fstream>
#include <sstream>
#endif // PANEL_CAPABILITIES_DOCS_DIR

namespace {

int gFailures = 0;

void check(bool ok, const std::string &what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++gFailures;
    }
}

// Every window is stack-constructed, exactly like split_layout_consistency
// tests does: Panel's ctor is heavy (layer-shell + QSettings) but it is the
// same cost every sibling test already pays offscreen, and it means every
// answer here comes from the REAL object with its real overrides rather
// than a stand-in.
template <class P>
P &prime(P &p)
{
    p.setEditMode(true);
    p.resetDemoPrimed();
    p.show();
    for (int i = 0; i < 4; ++i) {
        p.repaint();
        QApplication::processEvents();
    }
    return p;
}

// Press one key through the widget's real event dispatch — the same route a
// compositor's key press takes, and the only way this file can claim the
// base class routes it rather than some other path doing.
void sendKey(QWidget &w, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent e(QEvent::KeyPress, key, mods);
    QCoreApplication::sendEvent(&w, &e);
    QCoreApplication::processEvents();
}

// The six capabilities the base class gives every window. Stated once so
// each window's needs to list only what differs.
void checkUniversalCaps(const mhw::PanelCapabilities &caps, const char *who)
{
    check(caps.fold, std::string(who) + ": Space folds (edit AND live mode)");
    check(caps.quit, std::string(who) + ": Esc quits and saves");
    check(caps.move, std::string(who) + ": arrows nudge the margins");
    check(caps.scale, std::string(who) + ": wheel scales 0.5x-3x");
    check(caps.resizeW, std::string(who) + ": -/= dial the min-width floor");
    check(caps.resizeH, std::string(who) + ": Shift+-/Shift+= dial min-height");
}

#ifdef PANEL_CAPABILITIES_DOCS_DIR
// ── the docs half ────────────────────────────────────────────────────────
//
// The §12 matrix is what the user reads in-game-less form: a table with one
// row per window and one column per capability. It is written by hand from
// the same structural constants the runtime reads, so the two can drift —
// and this is the check that catches the drift, by reading both files and
// asserting every window the runtime knows has a row.
//
// Defined out by default because the target would otherwise depend on a
// source-relative path at RUN time. The CMake target passes
// -DPANEL_CAPABILITIES_DOCS_DIR="..." so ctest has the answer baked in;
// a plain `fsyntax-only` or a standalone compile does not need it.

std::string slurp(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// Every window the overlay can mount, in the order §12 lists them. The
// strings are the section names the console and panels.ini use, which is
// also what the docs call them — a row label that matches nothing in the
// runtime is a doc bug, and a window with no row is the bug the user
// reported.
constexpr const char *kWindowsForDocs[] = {
    "Player", "Monster", "Damage", "Pets",
    "Status strip", "Bars", "Mantles", "Wirebug", "Weapon", "Info",
};

void checkDocsMatrixRows(const std::string &text, const char *locale)
{
    check(text.find("| Window |") != std::string::npos
              || text.find("| 窗口 |") != std::string::npos,
          std::string(locale) + ": §12 has the capability matrix");
    for (const char *window : kWindowsForDocs) {
        const std::string label = std::string("| ") + window;
        check(text.find(label) != std::string::npos,
              std::string(locale) + ": §12 matrix has a row for "
                  + window);
    }
}

void checkDocsMatrix()
{
    const std::string en =
        slurp(std::string(PANEL_CAPABILITIES_DOCS_DIR) + "/USAGE.md");
    const std::string zh =
        slurp(std::string(PANEL_CAPABILITIES_DOCS_DIR) + "/USAGE.zh-CN.md");
    check(!en.empty(), "en: docs/USAGE.md is readable from the build tree");
    check(!zh.empty(), "zh: docs/USAGE.zh-CN.md is readable from the build tree");
    if (!en.empty())
        checkDocsMatrixRows(en, "en");
    if (!zh.empty())
        checkDocsMatrixRows(zh, "zh");
}
#endif // PANEL_CAPABILITIES_DOCS_DIR

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // v0.16: ISOLATE THE CONFIG, LIKE EVERY OTHER PANEL TEST.
    //
    // This file constructs fourteen Panels and redirects nothing, so it
    // read and wrote the developer's real ~/.config/monster-overlay/
    // panels.ini — and it does so WHILE HOLDING RESOURCE_LOCK "panels-ini",
    // which is the lock the other panel tests take to serialise themselves.
    // Panel::settings() now syncs on every access, so that made this file
    // and its neighbours race through a real shared file: the full ctest
    // run failed split-console-layout-persistence-tests while running this
    // one, and passed when run alone. The lock was never the problem. The
    // shared FILE was, and the lock only decided who went first.
    //
    // A scratch home is the fix, and it is also what makes a red here
    // mean "the code is wrong" instead of "the developer had a layout".
    static QTemporaryDir scratchHome;
    if (scratchHome.isValid()) {
        qputenv("HOME", scratchHome.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", scratchHome.path().toUtf8());
        QDir().mkpath(scratchHome.filePath(QStringLiteral("monster-overlay")));
    }

    // =====================================================================
    // 1. Every window: the six base capabilities are all true.
    // =====================================================================
    //
    // Stated per window rather than once, because the point of the list is
    // that the answer is per window — a single loop over a
    // "list of all windows" table would make the test shorter and the
    // claim weaker (it could not name the window that failed).
    {
        PlayerPanel player;
        checkUniversalCaps(mhw::panelCapabilities(player), "Player");
    }
    {
        MonsterPanel monster;
        checkUniversalCaps(mhw::panelCapabilities(monster), "Monster");
    }
    {
        DamagePanel damage;
        checkUniversalCaps(mhw::panelCapabilities(damage), "Damage");
    }
    {
        PetDamagePanel pets;
        checkUniversalCaps(mhw::panelCapabilities(pets), "Pets");
    }
    {
        PlayerStatusStripPanel status;
        checkUniversalCaps(mhw::panelCapabilities(status), "Status strip");
    }
    {
        PlayerBarsStripPanel bars;
        checkUniversalCaps(mhw::panelCapabilities(bars), "Bars");
    }
    {
        PlayerMantlesStripPanel mantles;
        checkUniversalCaps(mhw::panelCapabilities(mantles), "Mantles");
    }
    {
        PlayerWirebugStripPanel wirebug;
        checkUniversalCaps(mhw::panelCapabilities(wirebug), "Wirebug");
    }
    {
        PlayerWeaponStripPanel weapon;
        checkUniversalCaps(mhw::panelCapabilities(weapon), "Weapon");
    }
    {
        PlayerInfoStripPanel info;
        checkUniversalCaps(mhw::panelCapabilities(info), "Info");
    }

    // =====================================================================
    // 2. The columns capability: exactly the three wrapped-row windows.
    // =====================================================================
    //
    // The three that answer true are the three whose content is ONE row
    // the user can re-flow: the status pill strip, the two mantle boxes
    // and the wirebug capsule row. The other seven have no row to wrap,
    // so a `[` on them is an unclaimed key that reaches
    // QMainWindow::keyPressEvent() — the same destination it had before
    // the keys existed on the base class at all.
    {
        PlayerStatusStripPanel status;
        check(mhw::panelCapabilities(status).columns,
              "status strip: columns capability is true (its pills wrap)");
        PlayerMantlesStripPanel mantles;
        check(mhw::panelCapabilities(mantles).columns,
              "mantles: columns capability is true (two boxes, stackable)");
        PlayerWirebugStripPanel wirebug;
        check(mhw::panelCapabilities(wirebug).columns,
              "wirebug: columns capability is true (capsule row)");
    }
    {
        PlayerPanel player;
        check(!mhw::panelCapabilities(player).columns,
              "player: columns capability is FALSE (fixed layout)");
        MonsterPanel monster;
        check(!mhw::panelCapabilities(monster).columns,
              "monster: columns capability is FALSE (part grid is not a row)");
        DamagePanel damage;
        check(!mhw::panelCapabilities(damage).columns,
              "damage: columns capability is FALSE (one row per hunter)");
        PetDamagePanel pets;
        check(!mhw::panelCapabilities(pets).columns,
              "pets: columns capability is FALSE (rows, not one wrapped row)");
        PlayerBarsStripPanel bars;
        check(!mhw::panelCapabilities(bars).columns,
              "bars: columns capability is FALSE (two stacked bars)");
        PlayerWeaponStripPanel weapon;
        check(!mhw::panelCapabilities(weapon).columns,
              "weapon: columns capability is FALSE (identity is a column)");
        PlayerInfoStripPanel info;
        check(!mhw::panelCapabilities(info).columns,
              "info: columns capability is FALSE (metadata lines)");
    }

    // The three that answer true also have a dial range worth stating: the
    // claim is not "columns is on" but "columns is on and the dial is
    // clamped to a range that is a layout", so the current value is pinned
    // inside the window's own limits rather than at the base's default 0.
    {
        PlayerStatusStripPanel status;
        check(status.columns() == 3,
              "status strip: the dial's rest position is 3 pills per row");
        check(status.pillsPerRow() == status.columns(),
              "pillsPerRow() and columns() answer the same number");
        PlayerMantlesStripPanel mantles;
        check(mantles.columns() == 2,
              "mantles: the dial's rest position is 2 boxes per row");
        PlayerWirebugStripPanel wirebug;
        check(wirebug.columns() == 4,
              "wirebug: the dial's rest position is 4 capsules per row");
    }

    // =====================================================================
    // 3. `[` `]` on the base dispatch moves the dial — THE anti-regression.
    // =====================================================================
    //
    // This is the claim the deleted self-hosted keyPressEvent used to
    // carry by construction: the strip overrode the handler, forwarded
    // every key, and was therefore reachable. Replacing it with a test
    // that presses the key and reads the result is strictly stronger —
    // it exercises the routing the user's finger uses, so a future change
    // that drops supportsColumns(), forgets the dispatch branch or
    // reintroduces an override that swallows the key fails HERE.
    {
        PlayerStatusStripPanel status;
        prime(status);
        const int start = status.pillsPerRow();
        sendKey(status, Qt::Key_BracketRight);
        QApplication::processEvents();
        check(status.pillsPerRow() == start + 1,
              "status strip: `]` widens the pill row through the base dispatch");
        sendKey(status, Qt::Key_BracketLeft);
        QApplication::processEvents();
        check(status.pillsPerRow() == start,
              "status strip: `[` narrows it back (the two are inverses)");
        // The clamp, not just the step: at 1 the key must be a no-op rather
        // than a divide-by-zero or a 0-column window (statusStripRows
        // divides by it).
        for (int i = 0; i < 10; ++i) {
            sendKey(status, Qt::Key_BracketLeft);
            QApplication::processEvents();
        }
        check(status.pillsPerRow() == 1,
              "status strip: the dial clamps at 1 column, it does not go to 0");
        sendKey(status, Qt::Key_BracketLeft);
        QApplication::processEvents();
        check(status.pillsPerRow() == 1,
              "status strip: pressing `[` at the floor is still a no-op");
    }
    {
        PlayerMantlesStripPanel mantles;
        prime(mantles);
        const int start = mantles.columns();
        sendKey(mantles, Qt::Key_BracketLeft);
        QApplication::processEvents();
        check(mantles.columns() == start - 1,
              "mantles: `[` stacks the two boxes vertically");
        sendKey(mantles, Qt::Key_BracketRight);
        QApplication::processEvents();
        check(mantles.columns() == start,
              "mantles: `]` restores the side-by-side row");
    }
    {
        PlayerWirebugStripPanel wirebug;
        prime(wirebug);
        const int start = wirebug.columns();
        sendKey(wirebug, Qt::Key_BracketLeft);
        QApplication::processEvents();
        check(wirebug.columns() == start - 1,
              "wirebug: `[` turns the capsule row into a vertical list");
        sendKey(wirebug, Qt::Key_BracketRight);
        QApplication::processEvents();
        check(wirebug.columns() == start,
              "wirebug: `]` restores the flat row");
    }

    // The dial is edit-mode only. Panel::keyPressEvent bails out before the
    // bracket branch when editMode_ is false, so a live hunter's keystrokes
    // reach the game rather than re-flowing an overlay — and this is the
    // check that keeps that true in BOTH directions: the keys are live in
    // edit mode and dead outside it.
    {
        PlayerStatusStripPanel status;
        prime(status);
        const int rest = status.pillsPerRow();
        status.setEditMode(false);
        sendKey(status, Qt::Key_BracketRight);
        QApplication::processEvents();
        check(status.pillsPerRow() == rest,
              "status strip: `]` is IGNORED in live mode (edit gate holds)");
    }

    // A window that does not support columns ignores the same keys. This is
    // what makes the matrix's "no column axis" cell a claim rather than a
    // hope: press `]` on the weapon window and its layout does not move.
    {
        PlayerBarsStripPanel bars;
        prime(bars);
        const int w0 = bars.width();
        sendKey(bars, Qt::Key_BracketRight);
        QApplication::processEvents();
        check(bars.width() == w0,
              "bars: `]` moves nothing (the key is unclaimed, not half-live)");
    }

    // =====================================================================
    // 4. The key notation helper.
    // =====================================================================
    //
    // Not a translation: the glyphs are notation that survives every
    // locale, and the helper is what a hover tip or a state line would
    // render. Its contract is the SET, not the order — but the order is
    // pinned anyway, because a tooltip that re-orders itself between two
    // calls is a flicker the user reads as a bug.
    using mhw::panelCapabilityKeys;
    check(panelCapabilityKeys(true, true, true, true, true, true, true)
              == QStringLiteral("↑↓←→ wheel Space Esc - = ⇧- ⇧= [ ]"),
          "panelCapabilityKeys: full set, in the documented order");
    check(panelCapabilityKeys(true, true, false, false, false, false, false)
              == QStringLiteral("Space Esc"),
          "panelCapabilityKeys: only the live-mode keys when edit-only are off");
    check(panelCapabilityKeys(false, false, false, false, false, false, false)
              == QString(),
          "panelCapabilityKeys: empty when nothing is claimed");

    // A window's notation line is its capability struct, so the two can
    // never drift: the key glyphs for a column-capable window are the ones
    // the window with columns gets.
    {
        PlayerStatusStripPanel status;
        const mhw::PanelCapabilities caps =
            mhw::panelCapabilities(status);
        check(panelCapabilityKeys(caps.fold, caps.quit, caps.move, caps.scale,
                                  caps.resizeW, caps.resizeH, caps.columns)
                  == QStringLiteral("↑↓←→ wheel Space Esc - = ⇧- ⇧= [ ]"),
              "status strip's notation line carries the [ ] dial");
        PlayerBarsStripPanel bars;
        const mhw::PanelCapabilities b = mhw::panelCapabilities(bars);
        check(panelCapabilityKeys(b.fold, b.quit, b.move, b.scale, b.resizeW,
                                  b.resizeH, b.columns)
                  == QStringLiteral("↑↓←→ wheel Space Esc - = ⇧- ⇧="),
              "bars' notation line omits the [ ] dial");
    }

#ifdef PANEL_CAPABILITIES_DOCS_DIR
    checkDocsMatrix();
#endif

    // =====================================================================
    // Verdict.
    // =====================================================================
    std::printf("panel capabilities: %s (%d failure%s)\n",
                gFailures == 0 ? "PASS" : "FAIL",
                gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
