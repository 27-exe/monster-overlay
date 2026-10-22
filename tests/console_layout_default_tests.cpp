// SPDX-License-Identifier: Apache-2.0
//
// 重置布局 — the shipped default, applied to the group the rail has in
// front.
//
// WHAT THE USER ASKED FOR: 「粒度默认重置当前选中的组，以及一个全部重置的
// 按钮」. The whole point of that sentence is the boundary — a reset that
// reaches one group further than the control that summoned it is a reset
// that silently rewrites windows the user was not looking at. So the test
// is not "does the margin change" (that passes for an unscoped reset too);
// it is "does the OTHER group survive".
//
// THE SHAPE OF THE TEST. Two windows from two different groups get probe
// margins; one reset is fired for the first group. The first must land on
// the shipped default and the second must still hold its probe. A reset
// that touched both would leave no evidence of the boundary at all, which
// is why the untouched-window assertion is the one that carries the rule.
//
// v1.1.5: AND THE SECOND HALF — THE BOARD. The user's follow-up report:
// 「我点击重置布局，但是不会立即刷新。需要手动点一下别的窗口才显示重置后
// 的布局」. Writing the Panel was always right (the assertions below prove
// it); what never happened was the REPAINT. The last section pins that:
// subWindowRects() is filled by exactly one function — layoutSubWindows(),
// called from exactly one place — paintEvent — so it is the console's own
// record of what the board is currently showing, readable WITHOUT a grab()
// (a grab would force the paint and hide the bug). One reset, no second
// click, and the box must have moved.

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QRectF>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

#define private public
#define protected public
#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/layout_package.h"
#include "ui/panel.h"
#include "ui/window_layout_plan.h"
#undef private
#undef protected

namespace {

int failures = 0;

void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "PASS:" : "FAIL:",
                what.toUtf8().constData());
    if (!ok)
        ++failures;
}

void pump(int rounds = 40)
{
    for (int i = 0; i < rounds; ++i)
        QCoreApplication::processEvents();
}

// The config a real Rise + Mixed user has: the layout row, the four masks,
// and every one of the eleven windows detached so the sub rows exist.
bool writeConf(const QString &xdg)
{
    QDir dir(xdg + QStringLiteral("/monster-overlay"));
    dir.mkpath(QStringLiteral("."));
    {
        QFile f(dir.filePath(QStringLiteral("monster-control.conf")));
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write("[General]\ngame=rise\ndetectedGame=rise\n");
    }
    {
        QFile f(dir.filePath(QStringLiteral("monster-overlay.conf")));
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write("player=1ff\nmonster=3f\ndamage=f\npets=3\nmaskVersion=1\n"
                "locale=zh-CN\nlayout=mixed\n"
                "layout.windows.rise=player-status,player-mantles,"
                "player-wirebug,player-bars,player-weapon,player-info,"
                "monster-identity,monster-conditions,damage-share,"
                "damage-chart,damage-rows\n");
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QTemporaryDir home;
    if (!home.isValid()) {
        std::fprintf(stderr, "FAIL: no scratch HOME\n");
        return 2;
    }
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationName(QStringLiteral("monster-control"));
    check(writeConf(home.path()),
          "precondition: the scratch config was written");

    std::printf("== console_layout_default_tests ==\n");

    ControlPanel cp;
    cp.show();
    pump();

    // ── precondition: this console really is rise/mixed ───────────────
    check(cp.currentGame() == mhw::GameId::Rise,
          "precondition: the console is on Rise");
    check(cp.layoutModeInForce_ == mhw::PlayerSection::PlayerLayoutMode::Mixed,
          "precondition: the console is in Mixed");

    mhw::LayoutPackage pkg;
    QString error;
    check(cp.loadShippedDefault(pkg, &error),
          QStringLiteral("the shipped default loads -- %1").arg(error));

    Panel *playerBars = cp.windowForKey(QStringLiteral("player-bars"));
    Panel *damageRows = cp.windowForKey(QStringLiteral("damage-rows"));
    check(playerBars && damageRows,
          "precondition: one window from each of two groups resolves");
    if (!playerBars || !damageRows) {
        std::printf("\nRESULT: FAIL (%d failures)\n", ++failures);
        return 1;
    }

    // ── probe both groups with margins neither can confuse ────────────
    //
    // SMALL ON PURPOSE. setMargins() runs the values through
    // commitMargins()'s screen-relative clamp, so a probe that is
    // plausible on the user's 2560x1440 can be REJECTED on the offscreen
    // test display and the whole precondition turns into a test of the
    // clamp instead of the reset. player-bars (0,524) and damage-rows
    // (0,151) are two windows whose shipped values fit either way.
    const QMargins playerProbe(30, 40, 50, 60);
    const QMargins damageProbe(70, 80, 90, 100);
    playerBars->setMargins(playerProbe, true);
    damageRows->setMargins(damageProbe, true);
    check(playerBars->margins() == playerProbe
              && damageRows->margins() == damageProbe,
          QStringLiteral("precondition: both windows hold their probes -- "
                         "player-bars (%1,%2) want (%3,%4)")
              .arg(playerBars->margins().left())
              .arg(playerBars->margins().top())
              .arg(playerProbe.left())
              .arg(playerProbe.top()));

    // ── reset the PLAYER group only ───────────────────────────────────
    cp.resetLayoutToDefault(0);
    pump();

    const mhw::LayoutWindowRow defaultRow =
        pkg.scopes.value(QStringLiteral("rise/mixed"))
            .value(QStringLiteral("player-bars"));
    check(defaultRow.ml.has_value() && defaultRow.mt.has_value(),
          "precondition: the shipped default says what player-bars looks like");

    const QMargins restored = playerBars->margins();
    check(defaultRow.ml && restored.left() == *defaultRow.ml
              && defaultRow.mt && restored.top() == *defaultRow.mt,
          QStringLiteral("player-bars went back to the default (%1,%2) -- "
                         "now (%3,%4)")
              .arg(*defaultRow.ml)
              .arg(*defaultRow.mt)
              .arg(restored.left())
              .arg(restored.top()));

    // THE BOUNDARY. This is the assertion the feature was specified in
    // terms of: the other group must be untouched.
    check(damageRows->margins() == damageProbe,
          QStringLiteral("damage-rows kept its probe (%1) — the reset "
                         "did not reach the damage group")
              .arg(damageProbe.left()));

    // ── and 全部重置 does reach it ────────────────────────────────────
    cp.resetLayoutToDefault(-1);
    pump();
    const mhw::LayoutWindowRow damageDefault =
        pkg.scopes.value(QStringLiteral("rise/mixed"))
            .value(QStringLiteral("damage-rows"));
    const QMargins damageNow = damageRows->margins();
    check(damageDefault.ml && damageNow.left() == *damageDefault.ml
              && damageDefault.mt && damageNow.top() == *damageDefault.mt,
          QStringLiteral("after 全部重置, damage-rows is back to "
                         "default (%1,%2) -- now (%3,%4)")
              .arg(*damageDefault.ml)
              .arg(*damageDefault.mt)
              .arg(damageNow.left())
              .arg(damageNow.top()));

    // ── a scope the package does not cover must refuse, not guess ─────
    {
        mhw::LayoutPackage oneScope;
        oneScope.containsScopes.insert(QStringLiteral("world/single"),
                                       QStringList{QStringLiteral("player")});
        oneScope.scopes.insert(QStringLiteral("world/single"), {});
        QString refused;
        check(!cp.applyLayoutPackage(oneScope, -1, &refused)
                  && !refused.isEmpty(),
              QStringLiteral("a package that does not cover the current "
                             "scope is refused with a reason -- %1")
                  .arg(refused));
    }

    // ── v1.1.5: the reset reaches the BOARD, not just the Panel ──────
    //
    // The write half was never the bug: the assertions above prove the
    // Panel's own margins land on the shipped default. The bug was that
    // NOTHING REPAINTED — applyLayoutPackage()'s deferred half used to run
    // syncSplitPreviewToCanvas() (no rebuild here: game, mode and plan
    // signature are all unchanged, so it falls straight through to
    // pushWindowVisibility(), which returns without update() when no
    // visibility moved) plus refreshWindowModel(). The canvas's pixmaps
    // and its subRects_ are only refreshed by rebuildAndRender() /
    // refreshSplitWindowPreviews(), so the board kept painting the
    // pre-reset image until an unrelated action (the user's 「手动点一下
    // 别的窗口」) happened to repaint it.
    //
    // THE OBSERVABLE. subWindowRects() is the canvas's own record of what
    // the last paint drew — filled by layoutSubWindows() from paintEvent
    // and nowhere else. Reading it forces no paint, so "did the board
    // follow the reset" is answerable without hiding the bug behind a
    // grab(). The probe's paint below DOES grab (it is only the
    // precondition); the reset's half must not — one click, one pump,
    // nothing else, exactly the user's sequence.
    //
    // MUTATION: cut the deferred lambda back to its two old calls and
    // resetRect comes back equal to probeRect — no paint was requested,
    // which is the defect itself.
    {
        int barsIdx = -1;
        for (const mhw::WindowState &w : cp.buildWindowStates())
            if (w.key == QStringLiteral("player-bars"))
                barsIdx = w.subIndex;
        check(barsIdx >= 0,
              "precondition: player-bars is a live sub-window of the host");
        if (barsIdx >= 0 && cp.canvas_) {
            // A probe the shipped default cannot be confused with: the
            // default row is ml=0, mt=524 (data/default-layout.json,
            // rise/mixed), this is (30,40,50,60).
            playerBars->setMargins(QMargins(30, 40, 50, 60), true);
            cp.canvas_->grab();   // precondition paint only
            const QRectF probeRect =
                cp.canvas_->subWindowRects().value(barsIdx);
            check(!probeRect.isEmpty(),
                  QStringLiteral("precondition: the board painted the probe "
                                 "position (x=%1 y=%2)")
                      .arg(probeRect.x()).arg(probeRect.y()));

            cp.resetLayoutToDefault(-1);   // 全部重置 — every group
            pump();                        // NO grab, NO click, NO update

            const QRectF resetRect =
                cp.canvas_->subWindowRects().value(barsIdx);
            check(!resetRect.isEmpty() && resetRect != probeRect,
                  QStringLiteral("one 重置布局 click moves the board's own "
                                 "box — the repaint the reset was missing "
                                 "(probe=(%1,%2) reset=(%3,%4))")
                      .arg(probeRect.x()).arg(probeRect.y())
                      .arg(resetRect.x()).arg(resetRect.y()));
            const mhw::LayoutWindowRow def =
                pkg.scopes.value(QStringLiteral("rise/mixed"))
                    .value(QStringLiteral("player-bars"));
            check(def.ml && playerBars->margins().left() == *def.ml,
                  "…and the window the board now draws is the one at the "
                  "default margin (Panel state and board agree)");
        }
    }

    std::printf("\nRESULT: %s (%d failures)\n",
                failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
