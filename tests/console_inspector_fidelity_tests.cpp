// SPDX-License-Identifier: Apache-2.0
//
// v1.0.0 (O1): THE CONSOLE'S MAIN-WINDOW STRUCTURE, asserted the way a
// user meets it — through the visible widgets, not through a stack page
// index.
//
// Four closures, one test, because they are one screen:
//
//   (A) THE OLD INSPECTOR IS NOT ON SCREEN. The four panel pages still
//       exist and still drive the panels, but they live in a stack that
//       is parented and HIDDEN, so the visible drawer is the prototype's
//       three-column face and nothing else. The assertion is "the stack
//       is not visible" plus "the face is", which is what a screenshot
//       would see; asserting the stack's index would only prove the
//       controller still works.
//
//   (B) NO SELECTION UNTIL THE USER ASKS FOR ONE. reference.html's
//       defaultState() is `view:'overview', selected:null` and
//       renderInspector's `if(!d||!w)` arm swaps the columns for
//       .empty-inspector (reference.html:486). The console used to start
//       up PRE-SELECTED in Single mode, because the mother panel row has
//       subIndex -1 and the ctor's historical selectPanel(0) already
//       matched it. A click is what creates a selection; a scope change
//       (game switch) is what takes it away.
//
//   (C) THE COUNTS ARE REAL. `1 / (mask != 0)` is replaced by the number
//       of sections the window can actually draw (the panel's `kAll`,
//       game-filtered) and the number that are on — and the middle
//       column now carries the switches that change them, so the test
//       toggles one and watches the count move.
//
//   (D) THE STAGE HEAD IS the prototype's shell-head: h2 + sub + the
//       screen chip + the window-count chip, and none of the preview
//       console's own scaffolding (zoom / safe area / grid) is on screen.
//
// The settings-lifecycle half of the closure (the card's QPointer
// handles) is pinned by monster-control-rise-rf-smoke, which drives the
// real settings entry.

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QScopedPointer>
#include <QSettings>
#include <QStackedWidget>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <cstdio>

#include "core/string_table.h"
#include "ui/console_fidelity_windows.h"
#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/section_row.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const char *what, const QString &detail = QString())
{
    ++checks;
    std::printf("%s: %s", ok ? "PASS" : "FAIL", what);
    if (!detail.isEmpty())
        std::printf(" -- %s", qPrintable(detail));
    std::printf("\n");
    if (!ok)
        ++failures;
}

QString tr(const char *key)
{
    return mhw::StringTable::instance().tr(QString::fromLatin1(key));
}

void pump(int ms = 150)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

void seedGame(const char *game)
{
    QSettings settings;
    settings.setValue(QStringLiteral("game"), QString::fromLatin1(game));
    settings.sync();
}

// The user's own selection path: a press on a box in the preview. The
// canvas emits panelSelected() from its mouse handler; invoking the
// SIGNAL is the same delivery the console's slot sees from a real click,
// without needing a mouse position that depends on the offscreen
// platform's geometry.
void clickPanelBox(HudCanvas *canvas, int panelIndex)
{
    QMetaObject::invokeMethod(canvas, "panelSelected",
                              Qt::DirectConnection,
                              Q_ARG(int, panelIndex));
    QCoreApplication::processEvents();
}

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
    std::printf("FAIL: game button for %s not found\n", qPrintable(key));
    ++failures;
}

} // namespace

int main(int argc, char *argv[])
{
    // panels.ini must be redirected BEFORE anything opens it: this test
    // reads and writes the console's own scope settings.
    QTemporaryDir configRoot;
    qputenv("XDG_CONFIG_HOME", configRoot.path().toLocal8Bit());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationName(QStringLiteral("monster-control"));

    // World is the console's default game and has no pets panel, so the
    // Single window list is exactly three windows: player / monster /
    // damage.
    seedGame("world");

    ControlPanel cp;
    cp.show();
    pump();

    // Looked up once: the block below asserts what it shows, the B block
    // asserts that it comes back when a window is picked.
    auto *content = cp.findChild<QWidget *>(QStringLiteral("inspectorContent"));

    // ── (A): only the new inspector is on screen ────────────────────────
    {
        auto *stack = cp.findChild<QStackedWidget *>(
            QStringLiteral("inspectorStack"));
        check(stack != nullptr, "A: the old inspector stack still exists");
        check(stack && !stack->isVisible(),
              "A: the old inspector stack is NOT visible");
        auto *face = cp.findChild<QFrame *>(
            QStringLiteral("inspectorFace"));
        check(face != nullptr && face->isVisible(),
              "A: the new three-column face is the visible inspector");
        // It is HIDDEN at startup on purpose: with nothing selected the
        // draft swaps the whole content for .empty-inspector
        // (reference.html:486). The B block below asserts it comes back
        // with a selection.
        check(content != nullptr && !content->isVisible(),
              "A: the .inspector-content scroll box is hidden in the empty state");
        auto *groupCol = cp.findChild<QFrame *>(
            QStringLiteral("inspectorSections"));
        check(groupCol && !groupCol->isVisible(),
              "A: the group column is empty (and hidden) until a window is picked");
    }

    // ── (B): no selection at startup, a click makes one, a scope change
    //          takes it away ─────────────────────────────────────────────
    {
        auto *chip = cp.findChild<QLabel *>(
            QStringLiteral("selectionChip"));
        auto *empty = cp.findChild<QWidget *>(
            QStringLiteral("emptyInspector"));
        auto *output = cp.findChild<QComboBox *>(
            QStringLiteral("metaOutput"));
        check(chip && chip->text() == tr("console.proto.insp.chip.none"),
              "B: startup in Single has NO selected window",
              chip ? chip->text() : QString());
        check(empty && empty->isVisible(),
              "B: the .empty-inspector block is what the user sees");
        check(output && !output->isVisible(),
              "B: the metadata form is hidden until a window is picked");
        check(cp.selectedWindow().panelIndex < 0,
              "B: selectedWindow() answers invalid at startup");

        auto *canvas = cp.findChild<HudCanvas *>();
        check(canvas != nullptr, "B: the preview canvas exists");
        if (canvas) {
            // A real click on the player panel's box.
            clickPanelBox(canvas, 0);
            pump();
            check(cp.selectedWindow().panelIndex >= 0
                      && cp.selectedWindow().panelIndex == 0,
                  "B: clicking a box selects that window");
            check(chip && chip->text() != tr("console.proto.insp.chip.none")
                      && !chip->text().isEmpty(),
                  "B: the selection chip names the window",
                  chip ? chip->text() : QString());
            check(empty && !empty->isVisible(),
                  "B: the empty block is replaced by the three columns");
            check(output && output->isVisible(),
                  "B: the metadata form appears with a selection");
            check(content && content->isVisible(),
                  "A/B: the .inspector-content scroll box is back with a "
                  "selection — the three columns replace the empty block");

            // Scope change: switching the game drops the selection and
            // returns to the overview + empty inspector.
            clickGameButton(&cp, QStringLiteral("console.game.rise"));
            pump();
            check(cp.selectedWindow().panelIndex < 0,
                  "B: a game switch drops the window selection");
            check(chip && chip->text() == tr("console.proto.insp.chip.none"),
                  "B: the chip is back to 未选择 after the scope change",
                  chip ? chip->text() : QString());
            check(empty && empty->isVisible(),
                  "B: the empty block is back after the scope change");

            // And a click works again in the new scope.
            clickGameButton(&cp, QStringLiteral("console.game.world"));
            pump();
            clickPanelBox(canvas, 0);
            pump();
            check(cp.selectedWindow().panelIndex >= 0,
                  "B: a click selects again after the scope change");
        }
    }

    // ── (C): real section counts, and the switches that change them ─────
    {
        const mhw::WindowList list = cp.buildWindowStates();
        check(list.size() == 3,
              "C: a World Single scope has three windows (no pets)",
              QStringLiteral("%1").arg(list.size()));
        for (const mhw::WindowState &w : list) {
            check(w.sectionsTotal > 1,
                  "C: a mother panel reports more than the 1/1 placeholder",
                  QStringLiteral("%1 %2/%3")
                      .arg(w.key).arg(w.sectionsOn).arg(w.sectionsTotal));
            check(w.sectionsOn == w.sectionsTotal,
                  "C: the default mask turns every section on",
                  QStringLiteral("%1 %2/%3")
                      .arg(w.key).arg(w.sectionsOn).arg(w.sectionsTotal));
        }
        // Player under World draws nine sections minus Wirebug = eight.
        // This is the number the placeholder could never print.
        for (const mhw::WindowState &w : list) {
            if (w.key != QStringLiteral("player"))
                continue;
            check(w.sectionsTotal == 8,
                  "C: the player panel counts its nine sections minus Wirebug",
                  QStringLiteral("%1").arg(w.sectionsTotal));
        }

        // The middle column now carries the SWITCHES: toggle one and the
        // count has to move, which is what makes `7/8` a statement a user
        // can act on.
        auto *host = cp.findChild<QFrame *>(
            QStringLiteral("windowSectionList"));
        check(host != nullptr && host->isVisible(),
              "C: the selected window's section list is on screen");
        if (host) {
            const QList<SectionRow *> rows = host->findChildren<SectionRow *>();
            check(rows.size() == 8,
                  "C: the player window under World offers eight switches",
                  QStringLiteral("%1").arg(rows.size()));
            if (!rows.isEmpty()) {
                SectionRow *first = rows.first();
                const uint32_t bit = first->property("sectionBit").toUInt();
                check(bit != 0, "C: each row carries its section BIT, not a row index");
                first->setChecked(false);
                QMetaObject::invokeMethod(first, "stateChanged",
                                          Qt::DirectConnection,
                                          Q_ARG(int, 0));
                // The model refresh is deferred one turn on purpose (so
                // the emitting row is not deleted mid-signal).
                pump(250);
                const mhw::WindowList after = cp.buildWindowStates();
                for (const mhw::WindowState &w : after) {
                    if (w.key != QStringLiteral("player"))
                        continue;
                    check(w.sectionsOn == 7 && w.sectionsTotal == 8,
                          "C: switching one section off moves the count to 7/8",
                          QStringLiteral("%1/%2")
                              .arg(w.sectionsOn).arg(w.sectionsTotal));
                }
            }
        }
        // The count line the user reads is the same arithmetic.
        auto *count = cp.findChild<QLabel *>(QStringLiteral("groupSections"));
        check(count && count->text() == QStringLiteral("7/8"),
              "C: the column's own count line reads 7/8",
              count ? count->text() : QString());
        // And the object title states the window, its panel and the
        // output the chain resolved to.
        auto *title = cp.findChild<QLabel *>(QStringLiteral("windowTitle"));
        auto *meta = cp.findChild<QLabel *>(
            QStringLiteral("windowTitleMeta"));
        check(title && !title->text().isEmpty(),
              "C: .object-title names the window",
              title ? title->text() : QString());
        check(meta && meta->text().contains(QChar(0x00B7)),
              "C: .object-title's meta is `panel · output`",
              meta ? meta->text() : QString());
        auto *note = cp.findChild<QLabel *>(
            QStringLiteral("windowPosNote"));
        const mhw::WindowState selected = cp.selectedWindow();
        const QString noteText = tr("console.proto.pos.note")
                                     .arg(selected.outputResolved);
        check(note && note->text() == noteText,
              "C: the coordinate note is the prototype's copy, and names "
              "the window's RESOLVED output",
              note ? note->text() : QString());
    }

    // ── (D): the stage head, and nothing else on it ─────────────────────
    {
        auto *title = cp.findChild<QLabel *>(QStringLiteral("stageTitle"));
        check(title && title->text() == tr("console.stage.title"),
              "D: the stage head's h2 is 显示器布局预览",
              title ? title->text() : QString());
        auto *sub = cp.findChild<QLabel *>(QStringLiteral("stageSub"));
        check(sub && !sub->text().isEmpty(),
              "D: the stage head's sub line is set",
              sub ? sub->text() : QString());
        auto *screen = cp.findChild<QLabel *>(
            QStringLiteral("stageScreenChip"));
        check(screen && !screen->text().isEmpty(),
              "D: the screen chip is set",
              screen ? screen->text() : QString());
        auto *count = cp.findChild<QLabel *>(
            QStringLiteral("stageWindowCountChip"));
        check(count && !count->text().isEmpty(),
              "D: the window-count chip is set",
              count ? count->text() : QString());

        // The prototype's scaffolding is constructed but off screen: no
        // − / + / 缩放 / 安全区 / 网格 is visible anywhere.
        int visibleToggles = 0;
        for (QWidget *w : cp.findChildren<QWidget *>(QStringLiteral("stageToggle")))
            if (w->isVisible())
                ++visibleToggles;
        check(visibleToggles == 0,
              "D: no zoom / safe-area / grid control is visible",
              QStringLiteral("%1 visible").arg(visibleToggles));
        auto *zoomLabel = cp.findChild<QLabel *>(
            QStringLiteral("stageToggleLabel"));
        check(zoomLabel == nullptr || !zoomLabel->isVisible(),
              "D: the zoom percentage label is not visible");
        auto *theme = cp.findChild<QPushButton *>(
            QStringLiteral("themeToggle"));
        check(theme == nullptr || !theme->isVisible(),
              "D: the theme button is not on the stage (settings only)");
        auto *localeChip = cp.findChild<QWidget *>(
            QStringLiteral("localeChip"));
        check(localeChip == nullptr || !localeChip->isVisible(),
              "D: the locale chip is not on the stage (settings only)");
    }

    std::printf("%s (%d checks, %d failures)\n",
                failures == 0 ? "CONSOLE INSPECTOR FIDELITY PASS"
                              : "CONSOLE INSPECTOR FIDELITY FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
