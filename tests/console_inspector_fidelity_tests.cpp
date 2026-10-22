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
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QDir>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QScopedPointer>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
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
#include "ui/panel.h"
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

// A real POINTER gesture, delivered the way QWidgetWindow delivers one:
// hit-test the DEEPEST widget under the global point, send press/release
// to THAT widget, and let Qt's own parent propagation carry whatever the
// child does not handle. Delivering straight to the SectionRow (which is
// what a sendEvent-to-target test does) skips the one step this file's E
// block is about: a SectionRow's own label covers the middle of the row,
// so the user's pointer lands on the label, and the chip only moves if
// that event reaches the row behind it.
QWidget *hitTargetAt(QWidget *window, const QPoint &globalPos)
{
    QWidget *hit = window->childAt(window->mapFromGlobal(globalPos));
    return hit ? hit : window;
}

void hitTestClick(QWidget *window, const QPoint &globalPos)
{
    QWidget *target = hitTargetAt(window, globalPos);
    const QPointF local = target->mapFromGlobal(globalPos);
    QMouseEvent press(QEvent::MouseButtonPress, local, globalPos,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, local, globalPos,
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &release);
    QCoreApplication::processEvents();
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

            // ── D1: a live selection is live in BOTH directions ──────────
            // The controls are enabled, so the write must land; and the
            // readouts must come from the panel the form is showing.
            auto *metaScale = cp.findChild<QSlider *>(
                QStringLiteral("metaScale"));
            auto *metaOpacity = cp.findChild<QSlider *>(
                QStringLiteral("metaOpacity"));
            auto *metaVisible = cp.findChild<QCheckBox *>(
                QStringLiteral("metaVisible"));
            auto *metaX = cp.findChild<QSpinBox *>(QStringLiteral("metaX"));
            check(metaScale && metaScale->isEnabled()
                      && metaOpacity && metaOpacity->isEnabled()
                      && metaVisible && metaVisible->isEnabled()
                      && metaX && metaX->isEnabled()
                      && output && output->isEnabled(),
                  "D1: with a window selected all five controls are live");
            Panel *selPanel = cp.selectedPanelOrNull();
            if (metaScale && selPanel) {
                metaScale->setValue(175);
                pump();
                check(qRound(selPanel->scale() * 100) == 175,
                      "D1: the 缩放 write lands on the panel the form shows",
                      QStringLiteral("panel reads %1%")
                          .arg(qRound(selPanel->scale() * 100)));
                check(metaScale->value() == 175,
                      "D1: and the readout still agrees with it");
            }

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

            // ── D1: with NOTHING editable the five controls go dark AND ──
            // their readouts must not keep the last window's numbers. A
            // disabled slider still reading 175% is the lying reading the
            // audit recorded — grey-ing a control does not un-say what it
            // displays.
            check(cp.currentEditableSelection().panel == nullptr
                      && cp.selectedPanelOrNull() == nullptr,
                  "D1: after the scope change ONE accessor answers \"nothing "
                  "editable\"");
            check(metaScale && !metaScale->isEnabled()
                      && metaOpacity && !metaOpacity->isEnabled()
                      && metaVisible && !metaVisible->isEnabled()
                      && metaX && !metaX->isEnabled()
                      && output && !output->isEnabled(),
                  "D1: all five controls are disabled while nothing is selected",
                  QStringLiteral("scale%1 opacity%2 visible%3 posX%4 out%5")
                      .arg(metaScale && metaScale->isEnabled())
                      .arg(metaOpacity && metaOpacity->isEnabled())
                      .arg(metaVisible && metaVisible->isEnabled())
                      .arg(metaX && metaX->isEnabled())
                      .arg(output && output->isEnabled()));
            check(metaScale && metaScale->value() == 100
                      && metaOpacity && metaOpacity->value() == 100,
                  "D1: the sliders read neutral, not the last window's 175%",
                  QStringLiteral("scale=%1 opacity=%2")
                      .arg(metaScale ? metaScale->value() : -1)
                      .arg(metaOpacity ? metaOpacity->value() : -1));
            // The value labels belong to THIS column: the rail's own
            // sliders spell the same objectName, so looking them up
            // globally would read the rail's numbers instead of the form's.
            const auto valueLabels = metaScale && metaScale->parentWidget()
                ? metaScale->parentWidget()->findChildren<QLabel *>(
                      QStringLiteral("sliderValue"))
                : QList<QLabel *>();
            check(valueLabels.size() >= 2 && valueLabels[0]->text().isEmpty()
                      && valueLabels[1]->text().isEmpty(),
                  "D1: the 缩放/透明度 value labels are CLEARED (no stale "
                  "reading left behind a disabled control)",
                  QStringLiteral("%1 | %2")
                      .arg(valueLabels.size() >= 1 ? valueLabels[0]->text()
                                                   : QStringLiteral("?"))
                      .arg(valueLabels.size() >= 2 ? valueLabels[1]->text()
                                                   : QStringLiteral("?")));
            check(metaVisible && !metaVisible->isChecked(),
                  "D1: the 显示该窗口 switch is reset with everything else");

            // And a click works again in the new scope.
            clickGameButton(&cp, QStringLiteral("console.game.world"));
            pump();
            clickPanelBox(canvas, 0);
            pump();
            check(cp.selectedWindow().panelIndex >= 0,
                  "B: a click selects again after the scope change");
            // Put the panel back where the next block found it.
            if (metaScale && cp.selectedPanelOrNull()) {
                metaScale->setValue(100);
                pump();
            }
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

        // v1.0.1 (验收项5): THE ZOOM CONTROLS ARE VISIBLE — and only the
        // zoom controls are. safe-area / grid stay hidden preview-console
        // scaffolding (still findable through findChild), while − / + /
        // 1:1 and the % label are what make the board's zoom
        // discoverable — Ctrl+wheel alone was undiscoverable, which is
        // exactly 用户验收项5.
        int visibleToggles = 0;
        for (QWidget *w : cp.findChildren<QWidget *>(QStringLiteral("stageToggle")))
            if (w->isVisible())
                ++visibleToggles;
        check(visibleToggles == 3,
              "D: exactly the three zoom buttons (− / + / 1:1) are "
              "visible on the stage head",
              QStringLiteral("%1 visible").arg(visibleToggles));
        auto *zoomLabel = cp.findChild<QLabel *>(
            QStringLiteral("stageToggleLabel"));
        check(zoomLabel != nullptr && zoomLabel->isVisible(),
              "D: …and the zoom percentage label sits with them");
        auto *theme = cp.findChild<QPushButton *>(
            QStringLiteral("themeToggle"));
        check(theme == nullptr || !theme->isVisible(),
              "D: the theme button is not on the stage (settings only)");
        auto *localeChip = cp.findChild<QWidget *>(
            QStringLiteral("localeChip"));
        check(localeChip == nullptr || !localeChip->isVisible(),
              "D: the locale chip is not on the stage (settings only)");
    }

    // ── (E): 实测缺陷(user-8)③ — a REAL click on a 显示内容 chip ─────────
    //
    // The user's own state: a `layout=mixed` conf row (which resolves to
    // Mixed with every row detached when the file carries no plan) and a
    // focus on ONE DETACHED sub-window. That is the only state where the
    // mother's mask and that window's own mask are two different numbers:
    // the mother holds plan.retainedMask() — every detached row stripped
    // out — while the window paints from maskFor(ctl_[parentSlot]). The
    // column read the mother and the click wrote the mother, so the write
    // landed on a value rebuildAndRender() immediately re-derived; the
    // refilled chip was still off and the count still 0/2, which is what
    // 「点击无效」 meant. Both halves now name the selection's own Panel.
    {
        cp.close();
        pump();
        QDir().mkpath(configRoot.path()
                      + QStringLiteral("/monster-overlay"));
        QFile conf(configRoot.path()
                   + QStringLiteral("/monster-overlay/monster-overlay.conf"));
        check(conf.open(QIODevice::WriteOnly | QIODevice::Text),
              "E: the temp config can take a `layout=mixed` row");
        if (conf.isOpen())
            conf.write("layout=mixed\n");
        conf.close();

        ControlPanel cpMixed;
        cpMixed.show();
        pump();
        // The mode, read the way the user reads it: the 布局 segment's own
        // 混合 button carries the in-force mode (layoutModeInForce() is
        // private, and this file drives controls, not members).
        auto *mixedSeg = cpMixed.findChild<QPushButton *>(
            QStringLiteral("layoutSegMixed"));
        check(mixedSeg != nullptr && mixedSeg->isChecked(),
              "E: the 布局 segment shows 混合 selected for that conf row",
              mixedSeg ? (mixedSeg->isChecked() ? QStringLiteral("mixed")
                                                : QStringLiteral("not mixed"))
                       : QStringLiteral("button missing"));

        int statusSub = -1;
        mhw::WindowState status;
        for (const mhw::WindowState &w : cpMixed.buildWindowStates())
            if (w.key == QStringLiteral("player-status")) {
                status = w;
                statusSub = w.subIndex;
            }
        check(statusSub >= 0 && status.sectionsTotal == 2,
              "E: precondition — a detached player-status row claims two "
              "sections",
              QStringLiteral("sub=%1 sections=%2/%3")
                  .arg(statusSub)
                  .arg(status.sectionsOn)
                  .arg(status.sectionsTotal));
        cpMixed.selectWindow(0, statusSub);
        cpMixed.refreshWindowModel();
        pump(300);

        auto chipOf = [&cpMixed](uint32_t bit) -> SectionRow * {
            auto *h = cpMixed.findChild<QFrame *>(
                QStringLiteral("windowSectionList"));
            if (!h)
                return nullptr;
            for (SectionRow *r : h->findChildren<SectionRow *>())
                if (r->property("sectionBit").toUInt() == bit)
                    return r;
            return nullptr;
        };
        auto countText = [&cpMixed] {
            auto *c = cpMixed.findChild<QLabel *>(
                QStringLiteral("groupSections"));
            return c ? c->text() : QString();
        };
        auto windowMask = [&cpMixed] {
            const auto s = cpMixed.currentEditableSelection();
            return s.panel ? s.panel->sectionMask() : 0u;
        };
        auto onCountOf = [&cpMixed](const QString &key) {
            for (const mhw::WindowState &w : cpMixed.buildWindowStates())
                if (w.key == key)
                    return w.sectionsOn;
            return -1;
        };

        check(countText() == QStringLiteral("2/2"),
              "E: 显示内容 reads 2/2 — both sections the window IS drawing",
              countText());
        check(onCountOf(QStringLiteral("player-status")) == 2,
              "E: the model agrees: that row's on-count is 2 of 2",
              QString::number(onCountOf(QStringLiteral("player-status"))));

        SectionRow *first = nullptr;
        uint32_t bit = 0;
        {
            auto *h = cpMixed.findChild<QFrame *>(
                QStringLiteral("windowSectionList"));
            const auto rows = h ? h->findChildren<SectionRow *>()
                                : QList<SectionRow *>();
            check(rows.size() == 2,
                  "E: one chip per section of this window (two)",
                  QString::number(rows.size()));
            if (!rows.isEmpty()) {
                first = rows.first();
                bit = first->property("sectionBit").toUInt();
            }
        }
        if (first) {
            check(first->isEnabled(),
                  "E: the chip is enabled (a disabled switch is not "
                  "clickable)");
            check(first->isChecked(),
                  "E: the chip starts checked, because its section is on");

            // The pointer's own target: the row's own text label. Aim at
            // the LABEL's centre, not the row's. The label starts at a
            // fixed 35px, so at a narrow inspector (a 68px row) the row's
            // centre is 34 — one pixel left of the label — and an
            // assertion on the row's centre silently became a test of the
            // row's width instead of the label path it was written for.
            QLabel *label = nullptr;
            for (QLabel *l : first->findChildren<QLabel *>())
                if (l->isVisible() && l->width() > 0) {
                    label = l;
                    break;
                }
            check(label != nullptr,
                  "E: the row's text is a real, visible label");
            const QPoint at =
                label ? label->mapToGlobal(label->rect().center())
                      : first->mapToGlobal(first->rect().center());
            QWidget *hit = hitTargetAt(cpMixed.window(), at);
            check(label != nullptr && hit == label,
                  "E: at the label's centre the pointer lands on that label",
                  hit ? QString::fromLatin1(hit->metaObject()->className())
                      : QStringLiteral("null"));

            QPixmap before;
            const auto selBefore = cpMixed.currentEditableSelection();
            if (selBefore.panel) {
                selBefore.panel->repaint();
                before = selBefore.panel->grab();
            }
            const uint32_t maskBefore = windowMask();

            int fired = 0;
            QObject::connect(first, &SectionRow::stateChanged,
                             [&fired](int) { ++fired; });
            hitTestClick(cpMixed.window(), at);
            pump(300);

            check(fired == 1,
                  "E: the real press/release emits stateChanged exactly once",
                  QString::number(fired));
            check((maskBefore & bit) != 0 && (windowMask() & bit) == 0,
                  "E: the WINDOW's own mask took the click (its bit is clear)",
                  QStringLiteral("0x%1 -> 0x%2")
                      .arg(maskBefore, 0, 16)
                      .arg(windowMask(), 0, 16));
            check(countText() == QStringLiteral("1/2"),
                  "E: the count moved to 1/2 with the click", countText());
            SectionRow *afterChip = chipOf(bit);
            check(afterChip != nullptr && !afterChip->isChecked(),
                  "E: the chip itself is now OFF (it did not bounce back)");
            check(onCountOf(QStringLiteral("player-status")) == 1,
                  "E: the model's on-count followed, 2 -> 1",
                  QString::number(
                      onCountOf(QStringLiteral("player-status"))));

            QPixmap paint;
            const auto selAfter = cpMixed.currentEditableSelection();
            if (selAfter.panel) {
                selAfter.panel->repaint();
                paint = selAfter.panel->grab();
            }
            check(!before.isNull() && !paint.isNull()
                      && before.toImage() != paint.toImage(),
                  "E: the window's own PAINT changed — the section really "
                  "left the card",
                  QStringLiteral("%1x%2 -> %3x%4")
                      .arg(before.width())
                      .arg(before.height())
                      .arg(paint.width())
                      .arg(paint.height()));

            // Back on: the same gesture in the other direction, so the
            // switch is proved two-way rather than one-shot.
            if (afterChip) {
                hitTestClick(cpMixed.window(),
                             afterChip->mapToGlobal(
                                 afterChip->rect().center()));
                pump(300);
                check(countText() == QStringLiteral("2/2"),
                      "E: a second click puts the count back to 2/2",
                      countText());
                SectionRow *again = chipOf(bit);
                check(again != nullptr && again->isChecked(),
                      "E: …and the chip is checked again");
                check((windowMask() & bit) != 0,
                      "E: …and the window's mask has its bit back",
                      QStringLiteral("0x%1").arg(windowMask(), 0, 16));
            }
        }

        // ── (F): 实测缺陷(user-8)④ — 窗口分组 follows the focus, and the
        //          bottom 说明 is gone ───────────────────────────────────
        //
        // "相关" is taken from the model the column already uses for its
        // own 本组子窗 chips: a row belongs to the focus's group when its
        // registry `parentSlot` equals the selection's panelIndex.
        {
            auto listedKeys = [&cpMixed] {
                QStringList keys;
                for (QCheckBox *box : cpMixed.findChildren<QCheckBox *>(
                         QStringLiteral("mixedWinToggle"))) {
                    const QWidget *rowHost = box->parentWidget();
                    // isHidden(), not isVisible() — the flag
                    // refreshMixedGroupBlock() itself sets, not whatever
                    // an ancestor happens to be doing (console_mixed_
                    // layout_tests §8 uses the same reading for the
                    // game half of this filter).
                    if (rowHost && !rowHost->isHidden())
                        keys << box->property("settingsKey").toString();
                }
                keys.sort();
                return keys;
            };
            auto sorted = [](QStringList keys) {
                keys.sort();
                return keys;
            };

            check(cpMixed.findChildren<QLabel *>(
                          QStringLiteral("mixedGroupHint"))
                      .isEmpty(),
                  "F: the bottom 说明 label is gone (no mixedGroupHint)");
            check(cpMixed
                          .findChildren<QCheckBox *>(
                              QStringLiteral("mixedWinToggle"))
                          .size()
                      == 11,
                  "F: the eleven registry rows still exist (hidden, never "
                  "deleted)");
            check(listedKeys()
                      == sorted({QStringLiteral("player-bars"),
                                 QStringLiteral("player-info"),
                                 QStringLiteral("player-mantles"),
                                 QStringLiteral("player-status"),
                                 QStringLiteral("player-weapon")}),
                  "F: a PLAYER window focused → only the player group is "
                  "listed (World, so 翔虫 is out by the game filter too)",
                  listedKeys().join(QLatin1Char(',')));

            int sub = -1;
            for (const mhw::WindowState &w : cpMixed.buildWindowStates())
                if (w.key == QStringLiteral("monster-identity"))
                    sub = w.subIndex;
            check(sub >= 0, "F: precondition — a monster sub-window exists");
            cpMixed.selectWindow(1, sub);
            cpMixed.refreshWindowModel();
            pump(300);
            check(listedKeys()
                      == sorted({QStringLiteral("monster-conditions"),
                                 QStringLiteral("monster-identity")}),
                  "F: a MONSTER window focused → only the monster group",
                  listedKeys().join(QLatin1Char(',')));

            sub = -1;
            for (const mhw::WindowState &w : cpMixed.buildWindowStates())
                if (w.key == QStringLiteral("damage-share"))
                    sub = w.subIndex;
            check(sub >= 0, "F: precondition — a damage sub-window exists");
            cpMixed.selectWindow(2, sub);
            cpMixed.refreshWindowModel();
            pump(300);
            check(listedKeys()
                      == sorted({QStringLiteral("damage-chart"),
                                 QStringLiteral("damage-rows"),
                                 QStringLiteral("damage-share")}),
                  "F: a DAMAGE window focused → only the damage group",
                  listedKeys().join(QLatin1Char(',')));

            // NO focus: a scope change clears the selection, so the list
            // is the game filter alone — the case console_mixed_layout_
            // tests §8 asserts, kept working by the focus filter. Driven
            // through the real 崛起 button, not through switchGame().
            clickGameButton(&cpMixed, QStringLiteral("console.game.rise"));
            cpMixed.refreshWindowModel();
            pump(300);
            check(cpMixed.currentEditableSelection().panel == nullptr,
                  "F: precondition — a scope change clears the focus");
            check(listedKeys()
                      == sorted({QStringLiteral("damage-chart"),
                                 QStringLiteral("damage-rows"),
                                 QStringLiteral("damage-share"),
                                 QStringLiteral("monster-conditions"),
                                 QStringLiteral("monster-identity"),
                                 QStringLiteral("player-bars"),
                                 QStringLiteral("player-info"),
                                 QStringLiteral("player-status"),
                                 QStringLiteral("player-weapon"),
                                 QStringLiteral("player-wirebug")}),
                  "F: with no focus the whole game-filtered registry is "
                  "listed (Rise: 衣装 out, 翔虫 in)",
                  listedKeys().join(QLatin1Char(',')));

            // A focus that owns no registry row — the pets panel has none —
            // takes the block with it rather than leaving a caption and
            // two buttons over an empty list. Selected as the row the
            // overview lists for it (selectPanel() is private; selectWindow
            // is the path a click on that row takes anyway).
            cpMixed.selectWindow(3, -1);
            cpMixed.refreshWindowModel();
            pump(300);
            const auto petsSel = cpMixed.currentEditableSelection();
            check(petsSel.panelIndex == 3,
                  "F: precondition — the pets window row is the focus",
                  QStringLiteral("panel=%1 sub=%2")
                      .arg(petsSel.panelIndex)
                      .arg(petsSel.subIndex));
            auto *block = cpMixed.findChild<QFrame *>(
                QStringLiteral("mixedGroupBlock"));
            check(block != nullptr && block->isHidden(),
                  "F: a pets focus hides the 窗口分组 block (no registry "
                  "row belongs to it)");
        }
        cpMixed.close();
        pump();
    }

    std::printf("%s (%d checks, %d failures)\n",
                failures == 0 ? "CONSOLE INSPECTOR FIDELITY PASS"
                              : "CONSOLE INSPECTOR FIDELITY FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
