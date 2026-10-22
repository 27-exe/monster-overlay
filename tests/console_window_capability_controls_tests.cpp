// SPDX-License-Identifier: Apache-2.0
//
// B 阶段 · 验收项5 + 验收项4: CAPABILITY-DRIVEN PROPERTY ROWS, AND THE TWO
// OVERLAY ENTRIES.
//
//   验收项5
//     * the 列数 row is SHOWN for a window whose supportsColumns() is
//       true and HIDDEN for one that declines it (the four mother panels);
//     * every row reads and writes THE SELECTED WINDOW's own Panel —
//       minW on the strip cannot move the mother's;
//     * a typed number lands on that number (min / lock, both axes);
//     * 恢复自动尺寸 clears both floors and both locks and nothing else;
//     * and after the console's own exit-time write path
//       (saveAllSplitAndBaseWindows(), public because closeEvent() is not
//       reachable offscreen) the values are actually in the config tree.
//
//   验收项4
//     * 编辑预览 (enterEdit) still exists and still toggles THIS console's
//       board — the preview's own edit face;
//     * 在覆盖层中编辑 (overlayEdit) exists as a SEPARATE, visible entry
//       with its own copy.
//     It is deliberately NOT clicked: clicking it really spawns the
//     overlay child (launchOverlayChild(true) → buildOverlayArgv(true) →
//     `--edit`), and this session may not start the overlay or the game.
//     The call site is asserted by code inspection and recorded in
//     REPORT-B.md; the button is asserted to exist here.
//
//   user-8 · 实测缺陷7 / 8 / 9
//     * 跟随母面板 NAMES the mother panel it follows — inside the entry
//       and on the line beneath the dropdown
//       (母面板：<name>（<screen>） → 实际显示到：<screen>), while a mother
//       selection keeps plain 跟随主屏 and names no mother;
//     * 列数 carries a − / + pair that steps EXACTLY 1 through the box's
//       own writer, is dark with its row, and its range is the honest
//       [1, 6] union of the six supportsColumns() clamps, not [1, 64];
//     * the four size rows have PER-AXIS ranges (width 0..2560, height
//       0..1440) instead of one shared 0..4096: a height slider can no
//       longer be dragged to 4096, a drag writes a value inside Panel's
//       own [0, kMaxMinSize] clamp, and a value stored ABOVE the row's
//       range still reads back true (the refill grows the range to fit it
//       and shrinks it back once the value fits).
//
// Verification status: LINKED AND RUN — see the report for the exact
// check count. Registered in ctest as console-window-capability-controls-
// tests; also runnable directly with QT_QPA_PLATFORM=offscreen. Evidence:
// REPORT-mother-columns-size.md.

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QTemporaryDir>
#include <QWidget>

#include <cstdio>

#include "core/string_table.h"
#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/panel.h"
#include "ui/panel_sections.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const QString &what, const QString &detail = QString())
{
    ++checks;
    if (ok) {
        std::printf("ok   %s\n", qPrintable(what));
        return;
    }
    ++failures;
    std::printf("FAIL %s%s\n", qPrintable(what),
                detail.isEmpty() ? "" : qPrintable(QStringLiteral(" — %1").arg(detail)));
}

void pump(int ms = 120)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

void realClick(QWidget *w, const QPointF &local)
{
    if (!w)
        return;
    const QPointF gp = w->mapToGlobal(QPoint(qRound(local.x()), qRound(local.y())));
    QMouseEvent press(QEvent::MouseButtonPress, local, gp, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, local, gp, Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QApplication::sendEvent(w, &release);
    QCoreApplication::processEvents();
}

void clickCenter(QWidget *w)
{
    if (w)
        realClick(w, QPointF(w->width() / 2.0, w->height() / 2.0));
}

// Is `needle` anywhere in the redirected config tree? The console's own
// persistence goes to its own file with its own group names, so searching
// the tree is the honest spelling: it proves a WRITE, not a member change.
bool configTreeContains(const QString &root, const QByteArray &needle)
{
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (f.open(QIODevice::ReadOnly) && f.readAll().contains(needle))
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    QTemporaryDir configRoot;
    qputenv("XDG_CONFIG_HOME", configRoot.path().toLocal8Bit());
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationName(QStringLiteral("monster-control"));
    // Split, so there IS a window with a column axis to find.
    QDir().mkpath(configRoot.path() + QStringLiteral("/monster-overlay"));
    {
        QFile conf(configRoot.path()
                   + QStringLiteral("/monster-overlay/monster-overlay.conf"));
        if (conf.open(QIODevice::WriteOnly))
            conf.write("layout=split\n");
    }

    ControlPanel cp;
    cp.resize(1600, 1000);
    cp.show();
    pump();

    auto *columnsRow = cp.findChild<QWidget *>(QStringLiteral("propColumnsRow"));
    auto *columnsSpin = cp.findChild<QSpinBox *>(QStringLiteral("windowColumns"));
    auto *minWSpin = cp.findChild<QSpinBox *>(QStringLiteral("windowMinWidth"));
    auto *minHSpin = cp.findChild<QSpinBox *>(QStringLiteral("windowMinHeight"));
    auto *lockWSpin = cp.findChild<QSpinBox *>(QStringLiteral("windowFixedWidth"));
    auto *lockHSpin = cp.findChild<QSpinBox *>(QStringLiteral("windowFixedHeight"));
    auto *autoBtn = cp.findChild<QPushButton *>(QStringLiteral("windowAutoSize"));

    check(columnsRow && columnsSpin && minWSpin && minHSpin && lockWSpin
              && lockHSpin && autoBtn,
          QStringLiteral("every capability control was built (列数/最小宽/最小高/"
                         "固定宽/固定高/恢复自动尺寸)"),
          QStringLiteral("row=%1 spin=%2 minW=%3 minH=%4 lockW=%5 lockH=%6 auto=%7")
              .arg(bool(columnsRow)).arg(bool(columnsSpin)).arg(bool(minWSpin))
              .arg(bool(minHSpin)).arg(bool(lockWSpin)).arg(bool(lockHSpin))
              .arg(bool(autoBtn)));
    if (!columnsRow || !columnsSpin || !minWSpin || !minHSpin || !lockWSpin
        || !lockHSpin || !autoBtn) {
        std::fprintf(stderr,
                     "console-window-capability-controls-tests: %d/%d FAILED\n",
                     failures, checks);
        return 1;
    }

    // ── A MOTHER PANEL DECLINES THE COLUMN AXIS ─────────────────────────
    cp.selectWindow(0, -1);
    pump(80);
    Panel *mother = cp.selectedPanelOrNull();
    check(mother != nullptr, QStringLiteral("a mother panel is selectable"));
    check(mother && !mother->supportsColumns(),
          QStringLiteral("…and it honestly reports NO column capability"));
    check(!columnsRow->isVisibleTo(&cp) || !columnsRow->isVisible(),
          QStringLiteral("so the 列数 row is NOT shown for it (验收项5: 无 columns "
                         "能力的窗口不画列数控件)"));
    check(minWSpin->isEnabled() && lockWSpin->isEnabled() && autoBtn->isEnabled(),
          QStringLiteral("…while the size rows stay live for it"));
    const int motherMinWBefore = mother ? mother->minWidth() : -1;
    if (mother)
        check(minWSpin->value() == mother->minWidth(),
              QStringLiteral("the row shows THIS window's 最小宽 (readback)"),
              QStringLiteral("spin=%1 panel=%2")
                  .arg(minWSpin->value()).arg(mother->minWidth()));

    // ── FIND A WINDOW THAT DOES HAVE THE AXIS ───────────────────────────
    Panel *strip = nullptr;
    int stripSub = -1;
    for (int sub = 0; sub < 16 && !strip; ++sub) {
        cp.selectWindow(0, sub);
        pump(40);
        Panel *candidate = cp.selectedPanelOrNull();
        if (candidate && candidate->supportsColumns()) {
            strip = candidate;
            stripSub = sub;
        }
    }
    check(strip != nullptr,
          QStringLiteral("a split strip with a real column axis was found"));
    if (!strip) {
        std::fprintf(stderr,
                     "console-window-capability-controls-tests: %d/%d FAILED "
                     "(no columns-capable window)\n", failures, checks);
        return 1;
    }
    check(columnsRow->isVisible(),
          QStringLiteral("…and its 列数 row IS shown"));
    const int colsBefore = strip->columns();
    check(columnsSpin->value() == colsBefore,
          QStringLiteral("…showing this window's own dial position"),
          QStringLiteral("spin=%1 panel=%2").arg(columnsSpin->value()).arg(colsBefore));

    // ── TYPED NUMBERS LAND ──────────────────────────────────────────────
    const int minHBefore = strip->minHeight();
    const int lockWBefore = strip->lockWidth();
    const int lockHBefore = strip->lockHeight();

    minWSpin->setValue(356);
    pump(120);
    check(strip->minWidth() == 356,
          QStringLiteral("最小宽 356 lands on the SELECTED window"),
          QStringLiteral("panel->minWidth()=%1").arg(strip->minWidth()));
    check(minWSpin->value() == 356,
          QStringLiteral("…and the row reads back the same number"));
    if (mother)
        check(mother->minWidth() == motherMinWBefore,
              QStringLiteral("…while the MOTHER's 最小宽 is untouched "
                             "(作用于正确的实例)"),
              QStringLiteral("%1 -> %2").arg(motherMinWBefore)
                  .arg(mother ? mother->minWidth() : -1));

    minHSpin->setValue(120);
    pump(80);
    check(strip->minHeight() == 120,
          QStringLiteral("最小高 120 lands too"),
          QStringLiteral("panel->minHeight()=%1 (was %2)")
              .arg(strip->minHeight()).arg(minHBefore));

    lockWSpin->setValue(240);
    pump(80);
    check(strip->lockWidth() == 240,
          QStringLiteral("固定宽 240 lands"),
          QStringLiteral("panel->lockWidth()=%1 (was %2)")
              .arg(strip->lockWidth()).arg(lockWBefore));

    lockHSpin->setValue(180);
    pump(80);
    check(strip->lockHeight() == 180,
          QStringLiteral("固定高 180 lands"),
          QStringLiteral("panel->lockHeight()=%1 (was %2)")
              .arg(strip->lockHeight()).arg(lockHBefore));

    // ── THE COLUMN DIAL ITSELF ──────────────────────────────────────────
    // Move DOWN when the dial has room below (every clamp in the tree
    // floors at 1), UP when it is already at the bottom — so the step is
    // legal for every subclass instead of relying on one clamp range.
    const int target = colsBefore >= 3 ? colsBefore - 1 : colsBefore + 1;
    columnsSpin->setValue(target);
    pump(120);
    check(strip->columns() == target,
          QStringLiteral("列数 lands on the dial position that was typed"),
          QStringLiteral("wanted %1, panel=%2, spin=%3")
              .arg(target).arg(strip->columns()).arg(columnsSpin->value()));
    check(columnsSpin->value() == strip->columns(),
          QStringLiteral("…and the row reads back what the clamp actually did"));

    // ── PERSISTENCE: THE CONSOLE'S OWN EXIT WRITE ───────────────────────
    cp.saveAllSplitAndBaseWindows();
    pump(80);
    check(configTreeContains(configRoot.path(), "minW=356"),
          QStringLiteral("…and 最小宽 survives the console's own save path "
                         "(minW=356 is on disk)"));
    check(configTreeContains(configRoot.path(), "lockW=240"),
          QStringLiteral("…same for 固定宽 (lockW=240 is on disk)"));
    check(configTreeContains(configRoot.path(),
                             QStringLiteral("columns=%1").arg(target).toUtf8()),
          QStringLiteral("…and 列数 (columns=%1 is on disk — persistColumns)").arg(target));

    // ── 恢复自动尺寸 ────────────────────────────────────────────────────
    clickCenter(autoBtn);
    pump(150);
    check(strip->minWidth() == 0 && strip->minHeight() == 0
              && strip->lockWidth() == 0 && strip->lockHeight() == 0,
          QStringLiteral("恢复自动尺寸 clears BOTH floors and BOTH locks"),
          QStringLiteral("minW=%1 minH=%2 lockW=%3 lockH=%4")
              .arg(strip->minWidth()).arg(strip->minHeight())
              .arg(strip->lockWidth()).arg(strip->lockHeight()));
    check(minWSpin->value() == 0 && minHSpin->value() == 0
              && lockWSpin->value() == 0 && lockHSpin->value() == 0,
          QStringLiteral("…and every row reads 0 back"));
    check(strip->columns() == target,
          QStringLiteral("…without touching the column dial (a different axis)"),
          QStringLiteral("columns=%1, expected %2")
              .arg(strip->columns()).arg(target));

    // ── 实测缺陷8 / 缺陷9 (user-8): −/+ ON 列数, AND RANGES THAT ARE TRUE ──
    // 「列数控件加 −/+ 按钮（步进 1）」 and 「上下限要合理」. The dial's range
    // is the union of the six supportsColumns() clamps ([1, 6] — every one
    // floors at 1, none exceeds 6); the box used to claim [1, 64], a number
    // no Panel in this tree would honour (typing 64 came back as 6).
    auto *minusBtn =
        cp.findChild<QPushButton *>(QStringLiteral("windowColumnsMinus"));
    auto *plusBtn =
        cp.findChild<QPushButton *>(QStringLiteral("windowColumnsPlus"));
    check(minusBtn != nullptr && plusBtn != nullptr,
          QStringLiteral("the 列数 row carries a − and a + button "
                         "(实测缺陷8)"),
          QStringLiteral("minus=%1 plus=%2")
              .arg(bool(minusBtn)).arg(bool(plusBtn)));
    check(columnsSpin->minimum() == 1 && columnsSpin->maximum() == 6,
          QStringLiteral("…and 列数's range is the honest union of the six "
                         "supportsColumns() clamps: [1, 6], not [1, 64]"),
          QStringLiteral("%1..%2")
              .arg(columnsSpin->minimum()).arg(columnsSpin->maximum()));
    if (minusBtn && plusBtn) {
        check(columnsRow->isAncestorOf(minusBtn)
                  && columnsRow->isAncestorOf(plusBtn),
              QStringLiteral("…both buttons live INSIDE propColumnsRow (the "
                             "stepper widget nests them around the box), so "
                             "the row's hide() takes them with it"));
        check(minusBtn->isEnabled() && plusBtn->isEnabled(),
              QStringLiteral("…and the pair is live for a window that HAS "
                             "the column axis"));
        // Park the dial somewhere with room below it. The park VALUE is
        // read back rather than assumed: a subclass may clamp lower than
        // 3 (mantles tops out at 2), and asserting a number the panel
        // would not accept would be asserting the fixture's mistake.
        columnsSpin->setValue(3);
        pump(150);
        const int park = strip->columns();
        check(park >= 2 && park <= 3,
              QStringLiteral("parking 列数 for the step test (read back through "
                             "the subclass's own clamp)"),
              QStringLiteral("columns=%1").arg(park));
        clickCenter(minusBtn);
        pump(150);
        check(strip->columns() == park - 1 && columnsSpin->value() == park - 1,
              QStringLiteral("clicking − steps the dial by EXACTLY 1, through "
                             "the box's own writer (setColumnsTo + persist)"),
              QStringLiteral("panel=%1 spin=%2 (parked at %3)")
                  .arg(strip->columns()).arg(columnsSpin->value()).arg(park));
        clickCenter(plusBtn);
        pump(150);
        check(strip->columns() == park && columnsSpin->value() == park,
              QStringLiteral("clicking + steps it back by EXACTLY 1 — a "
                             "double-fired signal would overshoot here"),
              QStringLiteral("panel=%1 spin=%2")
                  .arg(strip->columns()).arg(columnsSpin->value()));
        // At the ceiling the + is a no-op, not a silent 7: the box's clamp
        // is the guard and the panel's own clamp is what reads back.
        columnsSpin->setValue(columnsSpin->maximum());
        pump(150);
        const int atTop = strip->columns();
        clickCenter(plusBtn);
        pump(150);
        check(strip->columns() == atTop && atTop <= 6,
              QStringLiteral("…and + at the ceiling changes nothing (the "
                             "clamp, not an out-of-range write)"),
              QStringLiteral("columns=%1").arg(strip->columns()));
        clickCenter(minusBtn);
        pump(150);
        const int afterStep = strip->columns();
        check(afterStep == atTop - 1,
              QStringLiteral("…while − from the ceiling still steps down by 1"),
              QStringLiteral("%1 -> %2").arg(atTop).arg(afterStep));
        // The button-written value persists like any other write.
        cp.saveAllSplitAndBaseWindows();
        pump(80);
        check(configTreeContains(configRoot.path(),
                                 QStringLiteral("columns=%1").arg(afterStep)
                                     .toUtf8()),
              QStringLiteral("…and it reaches the config tree (persistColumns)"),
              QStringLiteral("columns=%1").arg(afterStep));
        // A mother has no column axis: the pair goes dark with its row.
        cp.selectWindow(0, -1);
        pump(100);
        check(!minusBtn->isEnabled() && !plusBtn->isEnabled(),
              QStringLiteral("…with a MOTHER selected the pair goes dark with "
                             "the row it lives in"));
        cp.selectWindow(0, stripSub);
        pump(100);
        check(minusBtn->isEnabled() && plusBtn->isEnabled(),
              QStringLiteral("…and comes back with the strip"));
        // Leave the dial exactly where the surrounding suite expects it.
        columnsSpin->setValue(target);
        pump(150);
        check(strip->columns() == target,
              QStringLiteral("…and 列数 is restored to the value the checks "
                             "below assume"),
              QStringLiteral("columns=%1 expected %2")
                  .arg(strip->columns()).arg(target));
    }

    // ── 实测缺陷6: THE FOUR SIZE ROWS ARE DRAGGABLE ──────────────────────
    // 「这里的细节尺寸我希望能更有交互的调整，而不是手填数值」. Each row is now
    // a slider AND its box, and the point of the assertions below is that
    // they are ONE field: a drag writes through the box's own writer, a
    // typed number moves the handle, a refill and 恢复自动尺寸 move both.
    auto *minWSlider = cp.findChild<QSlider *>(QStringLiteral("windowMinWidthSlider"));
    auto *minHSlider = cp.findChild<QSlider *>(QStringLiteral("windowMinHeightSlider"));
    auto *lockWSlider = cp.findChild<QSlider *>(QStringLiteral("windowFixedWidthSlider"));
    auto *lockHSlider = cp.findChild<QSlider *>(QStringLiteral("windowFixedHeightSlider"));
    check(minWSlider && minHSlider && lockWSlider && lockHSlider,
          QStringLiteral("all four size rows carry a slider (最小宽/最小高/固定宽/"
                         "固定高)"),
          QStringLiteral("minW=%1 minH=%2 lockW=%3 lockH=%4")
              .arg(bool(minWSlider)).arg(bool(minHSlider))
              .arg(bool(lockWSlider)).arg(bool(lockHSlider)));
    if (minWSlider && minHSlider && lockWSlider && lockHSlider) {
        /* ── 实测缺陷9 (user-8): PER-ROW RANGES, NOT ONE SHARED 4096 ──────
           「尺寸四行滑条改为逐行合理量程而非一律 0–4096」 — 宽 and 高 are
           different dimensions, so each axis gets the largest number that
           still means something: a window is sized in LOGICAL pixels of
           its bound output, and a window bigger than that output cannot
           even be dragged (HudCanvas::movedSubMargins' clamp collapses to
           one corner). 2560×1440 is the real landscape output this
           project's own code documents (DP-1: hud_canvas.cpp:430,
           panel.h:559), and 1440 is HALF of Panel::kMaxMinSize — which is
           the point: the height rows can no longer be dragged to 4096.
           The BOXES carry the same ranges (they are the writer), so the
           two halves are still one field. ── end 实测缺陷9 ── */
        check(minWSlider->minimum() == 0 && minWSlider->maximum() == 2560
                  && lockWSlider->minimum() == 0
                  && lockWSlider->maximum() == 2560,
              QStringLiteral("…the two WIDTH rows span 0..2560 (their own "
                             "honest ceiling), not 0..4096"),
              QStringLiteral("minW %1..%2  lockW %3..%4")
                  .arg(minWSlider->minimum()).arg(minWSlider->maximum())
                  .arg(lockWSlider->minimum()).arg(lockWSlider->maximum()));
        check(minHSlider->maximum() == 1440 && lockHSlider->maximum() == 1440,
              QStringLiteral("…the two HEIGHT rows span 0..1440 — the "
                             "「离谱」 4096 ceiling is gone from them"),
              QStringLiteral("minH %1..%2  lockH %3..%4")
                  .arg(minHSlider->minimum()).arg(minHSlider->maximum())
                  .arg(lockHSlider->minimum()).arg(lockHSlider->maximum()));
        check(minHSlider->maximum() < Panel::maxDialValue()
                  && lockHSlider->maximum() < Panel::maxDialValue(),
              QStringLiteral("…so a HEIGHT slider can no longer be dragged to "
                             "Panel::maxDialValue() = 4096"),
              QStringLiteral("maxH=%1 lockH=%2 vs %3")
                  .arg(minHSlider->maximum()).arg(lockHSlider->maximum())
                  .arg(Panel::maxDialValue()));
        check(minWSpin->maximum() == 2560 && minHSpin->maximum() == 1440
                  && lockWSpin->maximum() == 2560
                  && lockHSpin->maximum() == 1440,
              QStringLiteral("…and every BOX carries the same range as its "
                             "slider (one field, two controls)"),
              QStringLiteral("minW=%1 minH=%2 lockW=%3 lockH=%4")
                  .arg(minWSpin->maximum()).arg(minHSpin->maximum())
                  .arg(lockWSpin->maximum()).arg(lockHSpin->maximum()));
        // A drag to the very end still writes a value Panel accepts: the
        // write travels the box's writer, so what lands is bounded by the
        // row's range AND inside Panel's own [0, kMaxMinSize] clamp.
        minHSlider->setValue(minHSlider->maximum());
        pump(150);
        check(strip->minHeight() == 1440,
              QStringLiteral("dragging 最小高 to the very end lands 1440 — a "
                             "legal write, not an out-of-range one"),
              QStringLiteral("panel->minHeight()=%1").arg(strip->minHeight()));
        check(strip->minHeight() <= Panel::maxDialValue(),
              QStringLiteral("…and every value the row can write is inside "
                             "Panel's own clamp [0, kMaxMinSize]"),
              QStringLiteral("%1 <= %2").arg(strip->minHeight())
                  .arg(Panel::maxDialValue()));
        clickCenter(autoBtn);            // back to the state below
        pump(150);
        check(strip->minHeight() == 0 && minHSlider->value() == 0,
              QStringLiteral("…and 恢复自动尺寸 clears that drag like any "
                             "other (both halves back to 0)"),
              QStringLiteral("panel=%1 slider=%2")
                  .arg(strip->minHeight()).arg(minHSlider->value()));

        // A drag IS a valueChanged: setting the value is the drag, and it
        // has to land through the same writer the typed number uses.
        minWSlider->setValue(300);
        pump(120);
        check(strip->minWidth() == 300,
              QStringLiteral("dragging 最小宽's slider lands on the SELECTED "
                             "window"),
              QStringLiteral("panel->minWidth()=%1").arg(strip->minWidth()));
        check(minWSpin->value() == 300,
              QStringLiteral("…and the box beside it shows the same number "
                             "(one field, two controls)"),
              QStringLiteral("spin=%1").arg(minWSpin->value()));

        // The other direction: typing moves the handle.
        minHSpin->setValue(150);
        pump(80);
        check(minHSlider->value() == 150,
              QStringLiteral("typing into 最小高 moves its slider"),
              QStringLiteral("slider=%1").arg(minHSlider->value()));
        check(strip->minHeight() == 150,
              QStringLiteral("…and the panel still sees exactly one write"));

        lockWSlider->setValue(480);
        lockHSlider->setValue(360);
        pump(120);
        check(strip->lockWidth() == 480 && strip->lockHeight() == 360,
              QStringLiteral("the 固定宽/固定高 sliders land too"),
              QStringLiteral("lockW=%1 lockH=%2")
                  .arg(strip->lockWidth()).arg(strip->lockHeight()));
        check(lockWSpin->value() == 480 && lockHSpin->value() == 360,
              QStringLiteral("…with both boxes in step"));

        // 恢复自动尺寸 clears the handles as well as the boxes and the
        // panel (the button itself is asserted above; this is the halves).
        clickCenter(autoBtn);
        pump(150);
        check(minWSlider->value() == 0 && minHSlider->value() == 0
                  && lockWSlider->value() == 0 && lockHSlider->value() == 0,
              QStringLiteral("恢复自动尺寸 zeroes the sliders too"),
              QStringLiteral("%1 %2 %3 %4")
                  .arg(minWSlider->value()).arg(minHSlider->value())
                  .arg(lockWSlider->value()).arg(lockHSlider->value()));

        // A selection change refills BOTH halves: the slider must never be
        // left describing the previous window.
        minWSlider->setValue(321);
        pump(80);
        cp.selectWindow(0, -1);
        pump(80);
        const int motherNow = mother ? mother->minWidth() : -1;
        check(mother && minWSlider->value() == motherNow
                  && minWSpin->value() == motherNow,
              QStringLiteral("switching windows refills the slider with the NEW "
                             "window's value (no stale handle)"),
              QStringLiteral("slider=%1 spin=%2 mother=%3")
                  .arg(minWSlider->value()).arg(minWSpin->value())
                  .arg(motherNow));
        cp.selectWindow(0, stripSub);
        pump(80);
        check(minWSlider->value() == 321 && minWSpin->value() == 321,
              QStringLiteral("…and coming back restores it on both halves"),
              QStringLiteral("slider=%1 spin=%2")
                  .arg(minWSlider->value()).arg(minWSpin->value()));
        /* ── 实测缺陷9: A STORED VALUE ABOVE THE ROW'S RANGE READS TRUE ────
           Panel clamps its OWN stores to kMaxMinSize (panel.cpp:1909…1981),
           so the overlay's edit keys (Panel::kMinSizeStep) can put 4096
           into a HEIGHT row whose honest dial range is 1440. The refill
           has to GROW the row's range to show it: a widget that clamps
           what it displays would print 1440 and, on the next drag, write
           that lie back into the panel. It then shrinks to the default the
           moment the value fits — the narrow span is the normal state and
           the wide one only ever appears while it is telling the truth. */
        strip->setExactMinHeight(99999);          // Panel clamps it
        cp.refreshWindowModel();
        pump(150);
        check(strip->minHeight() == Panel::maxDialValue(),
              QStringLiteral("Panel clamps a 99999 floor to its own "
                             "kMaxMinSize — the row never invents a cap"),
              QStringLiteral("minHeight=%1").arg(strip->minHeight()));
        check(minHSpin->value() == Panel::maxDialValue()
                  && minHSlider->value() == Panel::maxDialValue()
                  && minHSpin->maximum() >= Panel::maxDialValue()
                  && minHSlider->maximum() >= Panel::maxDialValue(),
              QStringLiteral("…and the height row GROWS its range to tell the "
                             "truth about that value (both halves agree)"),
              QStringLiteral("spin=%1/%2  slider=%3/%4")
                  .arg(minHSpin->value()).arg(minHSpin->maximum())
                  .arg(minHSlider->value()).arg(minHSlider->maximum()));
        clickCenter(autoBtn);
        pump(150);
        check(strip->minHeight() == 0 && minHSlider->maximum() == 1440
                  && minHSpin->maximum() == 1440,
              QStringLiteral("…and 恢复自动尺寸 shrinks the row back to the "
                             "honest 0..1440 — the wide span is never the "
                             "default"),
              QStringLiteral("maxH slider=%1 spin=%2")
                  .arg(minHSlider->maximum()).arg(minHSpin->maximum()));
        clickCenter(autoBtn);            // leave the panel clean
        pump(80);
    }

    // ── 实测缺陷5: THE 目标显示器 DROPDOWN NAMES WHAT IT DOES ────────────
    // 「这里这个目标显示器是什么？我完全没看懂他的逻辑，『跟随所属面板』这个是不是
    // 多余选项？」 — the code answer: it is not redundant (it is the only
    // automatic mode there is), it was mis-named. The entry now follows
    // the case — 跟随母面板 for a window row, 跟随主屏 for a panel — and the
    // line under the dropdown states the screen the own→parent→primary
    // chain resolves to right now.
    auto *outputCombo = cp.findChild<QComboBox *>(QStringLiteral("metaOutput"));
    auto *outputHint = cp.findChild<QLabel *>(QStringLiteral("metaOutputHint"));
    check(outputCombo != nullptr && outputHint != nullptr,
          QStringLiteral("the 目标显示器 combo and its resolved-screen line "
                         "both exist"),
          QStringLiteral("combo=%1 hint=%2")
              .arg(bool(outputCombo)).arg(bool(outputHint)));
    if (outputCombo && outputHint) {
        const QString followParent =
            mhw::StringTable::instance().tr(
                QStringLiteral("console.proto.follow_parent"));
        const QString followPrimary =
            mhw::StringTable::instance().tr(
                QStringLiteral("console.proto.follow_primary"));
        check(followParent != followPrimary,
              QStringLiteral("…and the two automatic modes have genuinely "
                             "different names"));
        const QString legacy = mhw::StringTable::instance().tr(
            QStringLiteral("console.proto.follow"));
        check(legacy != followPrimary,
              QStringLiteral("…and the old fixed 跟随所属面板 wording is "
                             "superseded by the case-specific pair (it is no "
                             "longer any entry's name)"),
              QStringLiteral("legacy='%1'").arg(legacy));

        // 实测缺陷7 (user-8): 「母面板到底是哪个？」 — the entry names WHICH
        // panel it follows, and the line under the dropdown repeats that
        // name next to the screen the chain lands on. Slot 0 is 玩家
        // (kMotherShortTitle[0] = console.proto.win.short.player).
        const QString motherTitle =
            mhw::StringTable::instance().tr(
                QStringLiteral("console.proto.win.short.player"));
        const QString followNamed =
            mhw::StringTable::instance().tr(
                QStringLiteral("console.proto.follow_parent_named"));
        check(!followNamed.startsWith(QStringLiteral("console.")),
              QStringLiteral("…and the new follow_parent_named key resolves "
                             "in this locale (parity of both locales is "
                             "i18n-gate-parity's job)"),
              followNamed);

        cp.selectWindow(0, -1);
        pump(80);
        check(outputCombo->itemText(0) == followPrimary,
              QStringLiteral("a MOTHER panel's automatic mode is named "
                             "跟随主屏 (it has no panel above it to follow)"),
              outputCombo->itemText(0));
        check(!outputHint->text().contains(motherTitle),
              QStringLiteral("…and a MOTHER selection's line names no mother "
                             "panel — there is none above it to name"),
              outputHint->text());
        cp.selectWindow(0, stripSub);
        pump(80);
        check(outputCombo->itemText(0).startsWith(followParent),
              QStringLiteral("a SUB-window's automatic mode is still named "
                             "跟随母面板 (the chain itself did not change)"),
              outputCombo->itemText(0));
        check(outputCombo->itemText(0).contains(motherTitle),
              QStringLiteral("…AND it names the mother panel it will follow — "
                             "「跟随母面板」 with no name was the gap "
                             "(实测缺陷7)"),
              outputCombo->itemText(0));
        check(!outputCombo->toolTip().trimmed().isEmpty(),
              QStringLiteral("…and the combo carries the chain as a tooltip"),
              outputCombo->toolTip());

        // The line under it must name the screen the chain ACTUALLY
        // resolves to — the same string the object-title meta prints, so
        // the two can never disagree.
        const QString hint = outputHint->text();
        check(!hint.trimmed().isEmpty(),
              QStringLiteral("the line under the dropdown is written"),
              hint);
        auto *titleMeta = cp.findChild<QLabel *>(QStringLiteral("windowTitleMeta"));
        bool agrees = true;
        QString detail = hint;
        if (titleMeta && titleMeta->text().contains(QStringLiteral(" · "))) {
            const QString resolved =
                titleMeta->text().section(QStringLiteral(" · "), 1, 1).trimmed();
            agrees = resolved.isEmpty()
                         ? hint.contains(QStringLiteral("—"))
                         : hint.contains(resolved);
            detail = QStringLiteral("meta='%1' hint='%2'")
                         .arg(titleMeta->text()).arg(hint);
        }
        check(agrees,
              QStringLiteral("…and it names the very screen the object-title "
                             "meta resolves to (own → parent → primary)"),
              detail);
        // 实测缺陷7: the line is now the WHOLE chain, not only its last hop.
        check(hint.contains(motherTitle) && hint.contains(QStringLiteral(" → ")),
              QStringLiteral("…and it reads as a chain: 「母面板：<name>"
                             "（<screen>） → 实际显示到：<screen>」 — the direct "
                             "answer to 「母面板到底是哪个」"),
              hint);
    }

    // ── 验收项4: THE TWO ENTRIES ARE TWO ENTRIES ────────────────────────
    auto *previewEdit = cp.findChild<QPushButton *>(QStringLiteral("enterEdit"));
    auto *overlayEdit = cp.findChild<QPushButton *>(QStringLiteral("overlayEdit"));
    check(previewEdit != nullptr,
          QStringLiteral("编辑预览 (enterEdit) is still there — the preview's own "
                         "edit face"));
    check(overlayEdit != nullptr,
          QStringLiteral("在覆盖层中编辑 (overlayEdit) exists as its own button "
                         "(验收项4)"));
    if (overlayEdit) {
        check(overlayEdit->isVisible(),
              QStringLiteral("…and it is really on screen"));
        check(!overlayEdit->text().trimmed().isEmpty(),
              QStringLiteral("…carrying its own copy (console.edit.overlay_start)"),
              overlayEdit->text());
        check(!overlayEdit->toolTip().trimmed().isEmpty(),
              QStringLiteral("…and a tooltip (console.edit.overlay_tip)"),
              overlayEdit->toolTip());
    }
    auto *canvas = cp.findChild<HudCanvas *>();
    check(canvas != nullptr, QStringLiteral("the console built a HudCanvas"));
    if (previewEdit && canvas) {
        const bool before = canvas->editing();
        clickCenter(previewEdit);
        pump(80);
        check(canvas->editing() != before,
              QStringLiteral("clicking 编辑预览 toggles THIS console's board — the "
                             "two entries are not the same switch"),
              QStringLiteral("editing %1 -> %2").arg(before).arg(canvas->editing()));
        clickCenter(previewEdit);   // leave it as it was
        pump(60);
    }
    // overlayEdit is NOT clicked on purpose — see the file header: its
    // handler is launchOverlayChild(true), which would spawn a real
    // overlay child process, and this session may not start one.

    if (failures == 0)
        std::printf("console-window-capability-controls-tests: ALL PASSED "
                    "(%d checks)\n", checks);
    else
        std::fprintf(stderr,
                     "console-window-capability-controls-tests: %d/%d FAILED\n",
                     failures, checks);
    return failures == 0 ? 0 : 1;
}
