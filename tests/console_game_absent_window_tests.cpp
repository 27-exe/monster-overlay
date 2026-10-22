// SPDX-License-Identifier: Apache-2.0
//
// 「世界的时候有一个空的翔虫窗口，崛起的时候有一个空的衣装窗口」
//
// THE CONTRACT. The console's split preview mounts exactly the windows the
// live overlay mounts for the game in force. 翔虫 is a Rise block and 衣装
// a World one — panelSectionSchema() masks the bit out of the other game,
// and buildWirebugModel's World arm / buildMantleModel's Rise arm
// deliberately seed nothing — so the overlay's own mount gate,
// hasVisibleContent() (main.cpp), never shows the wrong-game window.
//
// THE DEFECT. The console's preview host used to build all eleven split
// rows for either game, and the v1.0.0 renderAll() formula (enabled =
// section count > 0) then answered "sections all off" for the wrong-game
// row, so the canvas drew the `.hidden-window` placeholder: an empty frame
// promising a window the overlay does not have. That is the user's empty
// 翔虫 box under World and empty 衣装 box under Rise.
//
// v1.1.5: the host no longer BUILDS a row the game does not have
// (SplitWindowPreviewHostImpl's constructor takes the game and skips the
// row whose every section bit is masked out of that game's schema). What
// this file pins, from both sides:
//
//   A. World: no 翔虫 window in the host, no 翔虫 row on the board, ten
//      bindings, ten painted boxes — AND the other ten rows, 衣装
//      included, are still there WITH content (the filter removed one
//      window, not the player group).
//   B. Rise: the exact mirror.
//   C. Mixed obeys the same filter: a World plan that detaches
//      player-wirebug mounts ten windows, not eleven.
//   D. The REGISTRY stays game-agnostic: the plan the console reads for
//      World still lists player-wirebug as detached. Only the preview
//      host filters — the overlay constructs the window and gates it —
//      so a conf written under either game round-trips unchanged.
//
// MUTATION DISCIPLINE. Each contract states the one-line change that must
// make it fail:
//   A/B: drop the game filter from the host constructor -> the wrong-game
//        window is built again, splitPreviewWindow() answers non-null and
//        the counts go back to eleven.
//   C:   same filter, applied only to the Split arm -> Mixed rebuilds the
//        ghost.
//   D:   filter the REGISTRY instead of the host -> the World plan loses
//        its wirebug row and an exported conf can no longer round-trip.

#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/panel.h"
#include "ui/panel_sections.h"
#include "ui/panel_settings_game.h"
#include "ui/window_layout_plan.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>

namespace {

int checks = 0;
int failures = 0;

void check(bool cond, const QString &what, const QString &detail = QString())
{
    ++checks;
    if (cond) {
        std::printf("PASS: %s\n", qPrintable(what));
    } else {
        std::fprintf(stderr, "FAIL: %s", qPrintable(what));
        if (!detail.isEmpty())
            std::fprintf(stderr, " -- %s", qPrintable(detail));
        std::fprintf(stderr, "\n");
        ++failures;
    }
}

// The conf the console reads `layout=` from, under the redirected
// XDG_CONFIG_HOME. mkpath FIRST — a silent open() failure reads as "no
// layout row" and every Split contract would fail as Single.
QString confPath(const QTemporaryDir &home)
{
    return home.path()
           + QStringLiteral("/monster-overlay/monster-overlay.conf");
}

void writeConfRow(const QString &path, const QString &row)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        check(false, "the fixture conf could be created", f.errorString());
        return;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << row;
    out.flush();
}

// QSettings monster-control.conf: the console's own persisted game, so
// the fixture opens on the rail the contract is about.
void seedGame(const char *game)
{
    QSettings s;
    s.setValue(QStringLiteral("game"), QString::fromLatin1(game));
    s.sync();
}

// Clicks the rail's game button by its localized label — the same lookup
// control_reframework_smoke uses, so the real widget path is driven
// rather than switchGame() directly.
void clickGameButton(ControlPanel *cp, const QString &key)
{
    const QString want = mhw::StringTable::instance().tr(key);
    for (QPushButton *button : cp->findChildren<QPushButton *>()) {
        if (button->objectName() == QStringLiteral("gameBtn")
            && button->text() == want) {
            button->click();
            QCoreApplication::processEvents();
            return;
        }
    }
    check(false, QStringLiteral("game button '%1' was found").arg(key));
}

HudCanvas *findCanvas(ControlPanel *cp)
{
    return cp->findChild<HudCanvas *>();
}

// How many board rows carry `key` (sub-window rows only — a mother row
// carries the panel's own key, never a window key).
int boardRowsForKey(const mhw::WindowList &list, const QString &key)
{
    int n = 0;
    for (const mhw::WindowState &w : list)
        if (w.key == key && w.subIndex >= 0)
            ++n;
    return n;
}

// Boxes the last paint actually drew. An empty rect is a window the
// layout pass took out (hidden, or not laid); a mounted one is non-empty.
int paintedBoxes(const HudCanvas *canvas)
{
    int n = 0;
    for (const QRectF &r : canvas->subWindowRects())
        if (!r.isEmpty())
            ++n;
    return n;
}

// ─── A + B: one game's host is missing exactly the other game's row ────
void testWrongGameWindowIsNotBuilt()
{
    std::printf("\nA+B. the wrong game's row is not built (both directions)\n");

    QTemporaryDir home;
    if (!home.isValid()) {
        check(false, "temp config home");
        return;
    }
    qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
    // Split: the host owns every row of the split table, which is the
    // topology where the ghost window was visible.
    writeConfRow(confPath(home), QStringLiteral("layout=split\n"));
    seedGame("world");

    ControlPanel cp;
    cp.show();
    QCoreApplication::processEvents();

    HudCanvas *canvas = findCanvas(&cp);
    check(canvas != nullptr, "the console has a canvas");
    if (!canvas)
        return;
    check(mhw::panelSettingsGame() == mhw::GameId::World,
          "precondition: the console opened on World");

    // subWindowRects() is filled by paintEvent and nowhere else, and this
    // contract is about WHAT IS MOUNTED (not about whether a repaint was
    // requested — that is console_layout_default_tests' job), so the
    // frame is forced here before it is read. A grab() maps no window on
    // the offscreen platform.
    canvas->grab();

    // ── A. WORLD ───────────────────────────────────────────────────────
    check(cp.splitPreviewWindow(QStringLiteral("player-wirebug")) == nullptr,
          "World: the host builds NO 翔虫 window — the empty frame the "
          "user photographed is gone");
    check(boardRowsForKey(cp.buildWindowStates(),
                          QStringLiteral("player-wirebug")) == 0,
          "World: the board lists no 翔虫 row either");
    check(canvas->subWindowCount() == 10,
          "World: ten sub-window bindings (eleven rows minus 翔虫)",
          QStringLiteral("count=%1").arg(canvas->subWindowCount()));
    check(paintedBoxes(canvas) == 10,
          "World: the paint drew exactly those ten boxes — no placeholder "
          "ghost left behind",
          QStringLiteral("painted=%1").arg(paintedBoxes(canvas)));
    // THE REVERSE. The filter removed ONE window, not the player group:
    // 衣装 is a World block and must still be there, with content.
    Panel *mantles = cp.splitPreviewWindow(QStringLiteral("player-mantles"));
    check(mantles != nullptr,
          "World (reverse): the 衣装 window is still built");
    check(mantles && mantles->hasVisibleContent(),
          "World (reverse): …and it has visible content — a World host "
          "without its own block would be a worse bug than the one fixed");

    // ── B. RISE ────────────────────────────────────────────────────────
    clickGameButton(&cp, QStringLiteral("console.game.rise"));
    check(mhw::panelSettingsGame() == mhw::GameId::Rise,
          "precondition: the rail moved to Rise");
    canvas->grab();   // the new host's first frame, forced (see above)

    check(cp.splitPreviewWindow(QStringLiteral("player-mantles")) == nullptr,
          "Rise: the host builds NO 衣装 window");
    check(boardRowsForKey(cp.buildWindowStates(),
                          QStringLiteral("player-mantles")) == 0,
          "Rise: the board lists no 衣装 row either");
    check(canvas->subWindowCount() == 10,
          "Rise: ten sub-window bindings (eleven rows minus 衣装)",
          QStringLiteral("count=%1").arg(canvas->subWindowCount()));
    check(paintedBoxes(canvas) == 10,
          "Rise: the paint drew exactly those ten boxes",
          QStringLiteral("painted=%1").arg(paintedBoxes(canvas)));
    // THE REVERSE.
    Panel *wirebug = cp.splitPreviewWindow(QStringLiteral("player-wirebug"));
    check(wirebug != nullptr,
          "Rise (reverse): the 翔虫 window is still built");
    check(wirebug && wirebug->hasVisibleContent(),
          "Rise (reverse): …and it has visible content");
}

// ─── C: Mixed obeys the same filter ────────────────────────────────────
void testMixedObeysTheFilter()
{
    std::printf("\nC. Mixed: a plan that detaches the wrong game's row\n");

    QTemporaryDir home;
    if (!home.isValid()) {
        check(false, "temp config home");
        return;
    }
    qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
    // A WORLD Mixed plan that detaches ALL eleven rows, player-wirebug
    // included — the conf a user could legitimately have (the registry
    // is game-agnostic on purpose). The host must still mount ten.
    writeConfRow(confPath(home),
                 QStringLiteral("layout=mixed\n"
                                "layout.windows.world="
                                "player-status,player-mantles,"
                                "player-wirebug,player-bars,player-weapon,"
                                "player-info,monster-identity,"
                                "monster-conditions,damage-share,"
                                "damage-chart,damage-rows\n"));
    seedGame("world");

    ControlPanel cp;
    cp.show();
    QCoreApplication::processEvents();

    HudCanvas *canvas = findCanvas(&cp);
    if (!canvas)
        return;
    check(canvas->layoutMode()
              == mhw::PlayerSection::PlayerLayoutMode::Mixed,
          "precondition: the conf put the console in Mixed");
    check(cp.splitPreviewWindow(QStringLiteral("player-wirebug")) == nullptr,
          "World Mixed: the detached 翔虫 row is not mounted");
    check(canvas->subWindowCount() == 10,
          "World Mixed: ten mounted windows (the plan's eleven minus the "
          "game-absent row)",
          QStringLiteral("count=%1").arg(canvas->subWindowCount()));
}

// ─── D: the registry stays game-agnostic ───────────────────────────────
void testRegistryStaysGameAgnostic()
{
    std::printf("\nD. the plan registry is unchanged (host-only filter)\n");

    // The plan the console reads for World still names player-wirebug:
    // the overlay constructs that window and gates it on
    // hasVisibleContent(), so a conf round-trip must not lose the row.
    // Filtering the REGISTRY here would silently rewrite the user's
    // layout on the next save.
    const mhw::WindowLayoutPlan plan = mhw::resolveWindowLayoutPlan(
        mhw::GameId::World,
        mhw::PlayerSection::PlayerLayoutMode::Split,
        QStringLiteral("layout=split\n"));
    const QStringList keys = plan.detachedKeys();
    check(keys.size() == 11,
          "World's split plan still detaches all eleven rows",
          QStringLiteral("count=%1").arg(keys.size()));
    check(keys.contains(QStringLiteral("player-wirebug")),
          "…player-wirebug included — the filter lives in the preview "
          "host, not in the plan the overlay reads");
    const mhw::WindowLayoutPlan risePlan = mhw::resolveWindowLayoutPlan(
        mhw::GameId::Rise,
        mhw::PlayerSection::PlayerLayoutMode::Split,
        QStringLiteral("layout=split\n"));
    check(risePlan.detachedKeys().contains(QStringLiteral("player-mantles")),
          "…and Rise's plan still carries player-mantles");
}

} // namespace

int main(int argc, char *argv[])
{
    QTemporaryDir configRoot;
    qputenv("XDG_CONFIG_HOME", configRoot.path().toLocal8Bit());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationName(QStringLiteral("monster-control"));
    QApplication::setApplicationVersion(QStringLiteral("9.9.9-test"));

    std::printf("console_game_absent_window_tests: the preview mounts what "
                "the overlay mounts\n");
    std::printf("=====================================================\n");

    testWrongGameWindowIsNotBuilt();
    testMixedObeysTheFilter();
    testRegistryStaysGameAgnostic();

    std::printf("=====================================================\n");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
