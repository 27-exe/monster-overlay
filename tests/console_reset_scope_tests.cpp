// SPDX-License-Identifier: Apache-2.0
//
// 「我反复点击重置演示,结果崩溃了.……并且这个按钮好像没用」
//
// THE CRASH. The user's core dump:
//     #0 PanelSourceAdapter::outputName() const      <- SIGSEGV
//     #1 HudCanvas::windowOutputName(int, int)
//     #2 HudCanvas::overviewDrawsPanel(int)
//     #3 HudCanvas::paintEvent(QPaintEvent*)
// PanelSourceAdapter holds a raw `const Panel*`, and the canvas keeps
// non-owning PanelSource* for the whole session. resetPrototypeScope() calls
// rebuildMotherPanels() — which deletes the four Panels — and the contract
// written in rebuildMotherPanels()'s own comment ("the caller must run
// rebindMotherPanels() straight after this function returns") was honoured by
// only two of the three call sites. The next paint asked a freed Panel for
// its screen. Two clicks, because the first one's paint may land before the
// overview is up.
//
// THE OTHER HALF. 「这个按钮好像没用」 is the same defect: bound to the freed
// adapters, the canvas read freed memory for every window's geometry, so the
// panels the rebuild had just reloaded from disk never reached the board.
//
// WHAT THIS PINS. Three consecutive resets, a REAL paint between each (a
// paint is what died), and after every one the slot's bound source must be a
// NEW adapter — which is the only observable proof that the canvas is no
// longer holding the previous set's objects. The second check then reads the
// canvas's own screen answer, which is the call that crashed.

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

// Same access seam the other console suites use: the assertions are about
// private members (resetPrototypeScope(), boundSources_) and this TU must
// not be built as a second, differently-accessed object layout.
#define private public
#define protected public
#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/panel.h"
#undef private
#undef protected

namespace {

int failures = 0;

void check(bool ok, const QString &what, const QString &detail = QString())
{
    // qPrintable() hands back a QByteArray; %s wants the char* inside it.
    std::printf("%s %s%s%s\n", ok ? "PASS:" : "FAIL:",
                what.toUtf8().constData(),
                detail.isEmpty() ? "" : " -- ",
                detail.isEmpty() ? "" : detail.toUtf8().constData());
    if (!ok)
        ++failures;
}

void pump(int rounds = 60)
{
    for (int i = 0; i < rounds; ++i)
        QCoreApplication::processEvents();
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
    {
        // game=world before the first ControlPanel, or detectGame() finds a
        // running game and every pets-related assertion flips to Rise.
        QSettings game;
        game.setValue(QStringLiteral("game"), QStringLiteral("world"));
        game.sync();
    }

    std::printf("== console_reset_scope_tests ==\n");

    ControlPanel cp;
    cp.show();
    pump();

    check(cp.canvas_ != nullptr, "precondition: the console has a canvas");
    if (!cp.canvas_) {
        std::printf("\nRESULT: FAIL (1 failure)\n");
        return 1;
    }

    const PanelSource *before = cp.boundSources_[0].get();
    check(before != nullptr,
          "precondition: mother slot 0 has a bound source");
    check(cp.boundSources_[0]
              && cp.canvas_->boundOutputName(cp.boundSources_[0].get())
                     == cp.panelAt(0)->outputName(),
          "precondition: the canvas reads the screen of the LIVE panel");

    // Three resets, a real paint between each. The paint is the point: the
    // user's crash was in paintEvent, not in the click handler.
    for (int round = 1; round <= 3; ++round) {
        cp.resetPrototypeScope();
        cp.canvas_->update();
        pump();

        const PanelSource *now = cp.boundSources_[0].get();
        check(now != nullptr,
              QStringLiteral("round %1: slot 0 still has a source").arg(round)
                  );
        // Identity first, and the accessor second: without the rebind the
        // adapter is the OLD one, and calling boundOutputName() on it is
        // the crash itself rather than a clean failure.
        check(now != before,
              QStringLiteral("round %1: the bound source is a NEW adapter — "
                             "the old one pointed at a deleted panel")
                  .arg(round));
        if (now && now != before) {
            check(cp.canvas_->boundOutputName(now)
                      == cp.panelAt(0)->outputName(),
                  QStringLiteral("round %1: the canvas reads the panel that "
                                 "exists NOW").arg(round));
            before = now;   // so round N+1 also compares against its own
        }
    }

    // And the button actually DOES something: a margin the user typed is
    // in-memory only (setMargins(persist=false)), so a reset has to bring
    // back what the file holds.
    cp.panelAt(0)->setMargins(QMargins(7, 11, 13, 17), /*persist=*/false);
    const QMargins typed = cp.panelAt(0)->margins();
    cp.resetPrototypeScope();
    cp.canvas_->update();
    pump();
    check(cp.panelAt(0)->margins() != typed,
          "a reset re-reads geometry from the file instead of keeping the "
          "in-memory edit",
          QStringLiteral("typed=%1,%2,%3,%4 now=%5,%6,%7,%8")
              .arg(typed.left()).arg(typed.top())
              .arg(typed.right()).arg(typed.bottom())
              .arg(cp.panelAt(0)->margins().left())
              .arg(cp.panelAt(0)->margins().top())
              .arg(cp.panelAt(0)->margins().right())
              .arg(cp.panelAt(0)->margins().bottom()));

    std::printf("\nRESULT: %s (%d failures)\n",
                failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
