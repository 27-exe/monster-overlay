// SPDX-License-Identifier: Apache-2.0
//
// round2 · 用户:「这里左右应该分开竖直滚动的,而不是合在一起」.
//
// What this file is allowed to claim, and nothing more:
//
//   1. the inspector face is TWO halves, each with its OWN scroll area:
//      inspectorColumns (the QSplitter, 验收项1's draggable divider) holds
//      inspectorContent on the left (元数据 + 分区/本组子窗) and
//      inspectorContentRight on the right (当前内容状态 card) — the
//      splitter keeps exactly the 2 children the viewport tests assert;
//   2. inspectorContent still IS a QScrollArea and still carries the
//      inspectorGridHost/inspectorGrid tree, because three contracts
//      hang off that objectName: main_control's --snap-scroll=end hook,
//      the interaction regression tests' scroll/range measurements and
//      the fidelity tests' empty-state hide/show;
//   3. both halves scroll — each one's vertical bar gets a real range
//      once its own content is made taller than its own pane;
//   4. the halves do NOT scroll each other: scrolling one to its end
//      leaves the other's bar value and the other content's y-offset
//      (measured against the whole window) EXACTLY where they were;
//   5. the face still owns the empty state as a whole: with nothing
//      selected both scroll boxes are hidden, with a selection both are
//      back (the block that replaces them is console_inspector_fidelity
//      's business, this file only checks the two halves agree);
//   6. the card container: inspectorPreview's layout carries the 12/10
//      contents margins that a stylesheet padding on a container cannot
//      deliver, and the divider drag still resizes the card.
//
// Verification status: run by the parent session through ctest; this
// file needs no window manager and no config of its own (a temp
// XDG_CONFIG_HOME is redirected before QApplication exists).
//
// Build: added to CMakeLists.txt next to console-preview-viewport-tests.

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFrame>
#include <QLayout>
#include <QMargins>
#include <QPoint>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTemporaryDir>
#include <QWidget>

#include <cstdio>

#include "ui/control_panel.h"

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

void pump(int ms = 200)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// Force a layout pass under the scroll area so the bar's range is the
// truth and not an event that is still in the queue.
void settle(QWidget *w)
{
    if (!w)
        return;
    if (QLayout *lay = w->layout())
        lay->activate();
    pump();
}

// A y-offset that can only move when the CONTENT scrolls: measured
// against the whole window, which is the shared ancestor of both halves.
// A scroll of the OTHER half cannot change it unless the two halves are
// still sharing one scroll area — which is the defect this file files.
qint64 globalY(const QWidget *w)
{
    return w ? w->mapToGlobal(QPoint(0, 0)).y() : -1;
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

    ControlPanel cp;
    cp.resize(1600, 1000);
    cp.show();
    pump(300);
    check(cp.width() > 0 && cp.isVisible(), QStringLiteral("the console is up"));

    // ── 1. THE TREE: ONE SPLITTER, TWO SCROLL BOXES ───────────────────────
    auto *split = cp.findChild<QSplitter *>(QStringLiteral("inspectorColumns"));
    auto *left = cp.findChild<QScrollArea *>(QStringLiteral("inspectorContent"));
    auto *right = cp.findChild<QScrollArea *>(QStringLiteral("inspectorContentRight"));
    check(split != nullptr,
          QStringLiteral("inspectorColumns (验收项1's draggable divider) still exists"));
    check(split && split->count() == 2,
          QStringLiteral("…and it still has EXACTLY the 2 children the viewport "
                         "tests assert"),
          split ? QStringLiteral("children=%1").arg(split->count())
                : QStringLiteral("no splitter"));
    check(left != nullptr && right != nullptr,
          QStringLiteral("the face is TWO scroll areas, not one"),
          QStringLiteral("left=%1 right=%2")
              .arg(bool(left))
              .arg(bool(right)));
    check(split && left && right && split->widget(0) == left
              && split->widget(1) == right,
          QStringLiteral("…arranged as [inspectorContent | inspectorContentRight]"));

    auto *gridHost = cp.findChild<QWidget *>(QStringLiteral("inspectorGridHost"));
    auto *grid = gridHost ? qobject_cast<QLayout *>(gridHost->layout()) : nullptr;
    auto *metaCol = cp.findChild<QFrame *>(QStringLiteral("inspectorMeta"));
    auto *groupCol = cp.findChild<QFrame *>(QStringLiteral("inspectorSections"));
    auto *card = cp.findChild<QFrame *>(QStringLiteral("inspectorPreview"));
    check(left && left->widget() == gridHost && grid
              && grid->objectName() == QStringLiteral("inspectorGrid"),
          QStringLiteral("the LEFT half carries the inspectorGridHost/inspectorGrid "
                         "tree (--snap-scroll and the regression tests find it there)"));
    check(gridHost && grid && metaCol && groupCol
              && gridHost->isAncestorOf(metaCol) && gridHost->isAncestorOf(groupCol),
          QStringLiteral("…and the 元数据 + 分区/本组子窗 columns are in it"));
    check(right && card && right->isAncestorOf(card) && !right->isAncestorOf(metaCol)
              && !gridHost->isAncestorOf(card),
          QStringLiteral("the 当前内容状态 card lives in the RIGHT half, not in "
                         "inspectorContent"));
    check(left && !left->isAncestorOf(card),
          QStringLiteral("…and the card is not reachable from inspectorContent at all"));

    // The card container: stylesheet padding does not reach a layout, so
    // the 12/10 the card needs is the LAYOUT's.
    if (card && card->layout()) {
        const QMargins m = card->layout()->contentsMargins();
        check(m.left() == 12 && m.right() == 12 && m.top() == 10 && m.bottom() == 10,
              QStringLiteral("the card's padding is the layout's (12/10) — a QSS "
                             "padding on a container with a layout is not applied"),
              QStringLiteral("%1/%2/%3/%4")
                  .arg(m.left()).arg(m.top()).arg(m.right()).arg(m.bottom()));
    }

    // ── 2. THE EMPTY STATE OWNS BOTH HALVES ───────────────────────────────
    // Nothing selected at startup: the block replaces the WHOLE face, so
    // neither half may be left visible behind it.
    check(left && !left->isVisible() && right && !right->isVisible(),
          QStringLiteral("with nothing selected BOTH halves are hidden (the empty "
                         "block owns the whole face)"),
          QStringLiteral("left vis=%1 right vis=%2")
              .arg(left ? int(left->isVisible()) : -1)
              .arg(right ? int(right->isVisible()) : -1));

    cp.selectWindow(0, -1);
    pump(300);
    check(left && left->isVisible() && right && right->isVisible(),
          QStringLiteral("a selection brings BOTH halves back"),
          QStringLiteral("left vis=%1 right vis=%2")
              .arg(left ? int(left->isVisible()) : -1)
              .arg(right ? int(right->isVisible()) : -1));
    if (!left || !right || !metaCol || !card || !gridHost) {
        std::fprintf(stderr,
                     "console-scroll-split-tests: %d/%d FAILED (missing widgets)\n",
                     failures, checks);
        return 1;
    }

    QScrollBar *leftBar = left->verticalScrollBar();
    QScrollBar *rightBar = right->verticalScrollBar();
    check(leftBar != nullptr && rightBar != nullptr && leftBar != rightBar,
          QStringLiteral("each half has its OWN vertical bar"));
    // The NATURAL ranges (no staging, no minimum-height games) — printed
    // because they are the honest answer to "does this half scroll at
    // this window size", which is a function of font metrics and of the
    // selected window, not something this file may assert a number for.
    const int naturalLeftRange = leftBar ? leftBar->maximum() : -1;
    const int naturalRightRange = rightBar ? rightBar->maximum() : -1;
    std::printf("natural ranges: left=%d right=%d (left pane %dx%d, right pane %dx%d)\n",
                naturalLeftRange, naturalRightRange,
                left->viewport()->width(), left->viewport()->height(),
                right->viewport()->width(), right->viewport()->height());

    // ── 3. BOTH HALVES SCROLL (the bars are real, not just present) ───────
    // Made taller than their own pane, on purpose and per half: the range
    // is the assertion, so it does not depend on which column happens to
    // be the tallest in a given font/translation.
    const int metaTaller = metaCol->height() + 320;
    metaCol->setMinimumHeight(metaTaller);
    settle(left->widget());
    const int leftRange = leftBar->maximum();
    check(leftRange > 0 && leftBar->isVisible(),
          QStringLiteral("把左列内容撑高后 inspectorContent 的竖直滚动条有了真实 "
                         "range (left half scrolls)"),
          QStringLiteral("range=%1 vis=%2 minH=%3")
              .arg(leftRange)
              .arg(int(leftBar->isVisible()))
              .arg(metaCol->height()));

    const int cardTaller = card->height() + 320;
    card->setMinimumHeight(cardTaller);
    settle(right->widget());
    const int rightRange = rightBar->maximum();
    check(rightRange > 0 && rightBar->isVisible(),
          QStringLiteral("把右侧预览列撑高后 inspectorContentRight 的竖直滚动条有了 "
                         "真实 range (right half scrolls)"),
          QStringLiteral("range=%1 vis=%2 minH=%3")
              .arg(rightRange)
              .arg(int(rightBar->isVisible()))
              .arg(card->height()));

    // ── 4. …AND THEY DO NOT SCROLL EACH OTHER ─────────────────────────────
    // The defect was one bar moving all three columns. The proof that it
    // is now two is NOT "two bars exist" but "moving one moves only its
    // own content": each half's content y-offset against the WHOLE WINDOW
    // (the shared ancestor), plus the OTHER bar's value, across a full
    // scroll of each.
    const qint64 gridYBefore = globalY(gridHost);
    const qint64 cardYBefore = globalY(card);
    const int rightValueBeforeLeftScroll = rightBar->value();

    leftBar->setValue(leftBar->maximum());
    settle(left->widget());
    pump(120);
    check(rightBar->value() == rightValueBeforeLeftScroll,
          QStringLiteral("滚左边不动右边的滚动条"),
          QStringLiteral("right %1 -> %2")
              .arg(rightValueBeforeLeftScroll)
              .arg(rightBar->value()));
    check(globalY(card) == cardYBefore,
          QStringLiteral("滚左边不动右边卡片的 y 偏移 (the shared window is the "
                         "reference)"),
          QStringLiteral("card y %1 -> %2")
              .arg(cardYBefore)
              .arg(globalY(card)));
    check(gridYBefore - globalY(gridHost) == leftRange,
          QStringLiteral("…and the left content really did move by its whole range "
                         "(the bar is the left half's, not a leftover)"),
          QStringLiteral("moved %1 for range %2")
              .arg(gridYBefore - globalY(gridHost))
              .arg(leftRange));

    const int leftValueBeforeRightScroll = leftBar->value();
    const qint64 gridYBeforeRightScroll = globalY(gridHost);

    rightBar->setValue(rightBar->maximum());
    settle(right->widget());
    pump(120);
    check(leftBar->value() == leftValueBeforeRightScroll,
          QStringLiteral("滚右边不动左边的滚动条"),
          QStringLiteral("left %1 -> %2")
              .arg(leftValueBeforeRightScroll)
              .arg(leftBar->value()));
    check(globalY(gridHost) == gridYBeforeRightScroll,
          QStringLiteral("滚右边不动左边栅格的 y 偏移"),
          QStringLiteral("grid y %1 -> %2")
              .arg(gridYBeforeRightScroll)
              .arg(globalY(gridHost)));
    check(cardYBefore - globalY(card) == rightRange,
          QStringLiteral("…and the right content really did move by its whole range"),
          QStringLiteral("moved %1 for range %2")
              .arg(cardYBefore - globalY(card))
              .arg(rightRange));

    // The bars are independent objects with independent contents: scroll
    // one halfway and the other must stay put, both ways round.
    leftBar->setValue(0);
    settle(left->widget());
    const int mid = rightRange / 2;
    rightBar->setValue(mid);
    pump(120);
    check(leftBar->value() == 0 && rightBar->value() == mid,
          QStringLiteral("the two bars keep their own values side by side"),
          QStringLiteral("left=%1 right=%2").arg(leftBar->value()).arg(rightBar->value()));

    // ── 5. THE DIVIDER STILL RESIZES THE CARD (验收项1) ───────────────────
    // Two facts, in the order the user meets them. Widening the right
    // half must widen the card exactly (its own padding less) — that is
    // the 「调整预览容器」 the acceptance item is about, and it is what
    // makes a wider container a BIGGER Fit render. Starving it past the
    // card's own minimum must NOT crush the card: the zoom row is one
    // line of buttons that cannot wrap, so the card stops there and the
    // half exposes the overflow HORIZONTALLY — the same contract the
    // left half has, and the one console-interaction pins for the grid.
    const QList<int> before = split->sizes();
    const QMargins pad = card->layout()->contentsMargins();
    // The cell the layout gives the card: the host's width, less the
    // 12/10 padding, less the card's own styled frame (MEASURED at 2px
    // per side: the 1px QSS border plus the frame's own 1px line width —
    // previewFitScale reserves the same kCardBorder for the same reason).
    const auto cardCellWidth = [&] {
        return right->widget()->width() - pad.left() - pad.right() - 4;
    };
    const auto cardFitsViewport = [&] {
        return qAbs(card->width() - cardCellWidth()) <= 2;
    };

    QList<int> wide = before;
    wide[1] = before[1] + 140;
    wide[0] = before[0] - 140;
    split->setSizes(wide);
    settle(right->widget());
    check(split->sizes() != before,
          QStringLiteral("拖动分隔条改变左右比例 (the divider is still live)"),
          QStringLiteral("%1 -> %2").arg(before[1]).arg(split->sizes()[1]));
    check(cardFitsViewport(),
          QStringLiteral("…and a wider right half is a wider card, exactly its "
                         "own padding less (验收项1: 调整预览容器)"),
          QStringLiteral("card=%1 viewport=%2 pad=%3")
              .arg(card->width())
              .arg(right->viewport()->width())
              .arg(pad.left() + pad.right()));

    QList<int> narrow = before;
    narrow[1] = qMax(80, before[1] / 3);
    narrow[0] = before[0] + (before[1] - narrow[1]);
    split->setSizes(narrow);
    settle(right->widget());
    const bool cardStoppedAtItsOwn =
        card->width() >= right->viewport()->width() - pad.left() - pad.right()
        || right->horizontalScrollBar()->maximum() > 0;
    check(cardStoppedAtItsOwn,
          QStringLiteral("a right half narrower than the card's own minimum stops "
                         "at the card and exposes the overflow instead of crushing "
                         "it (the zoom row cannot wrap)"),
          QStringLiteral("card=%1 viewport=%2 hRange=%3")
              .arg(card->width())
              .arg(right->viewport()->width())
              .arg(right->horizontalScrollBar()->maximum()));

    // ── 6. RESTORE: the assertions above were staged, not permanent ───────
    metaCol->setMinimumHeight(0);
    card->setMinimumHeight(0);
    split->setSizes(before);
    settle(left->widget());
    settle(right->widget());
    check(leftBar->maximum() == naturalLeftRange
              && rightBar->maximum() == naturalRightRange,
          QStringLiteral("back to natural height both halves are where they started "
                         "— every range above was earned by content, not by a "
                         "permanent policy"),
          QStringLiteral("left %1 -> %2, right %3 -> %4")
              .arg(naturalLeftRange).arg(leftBar->maximum())
              .arg(naturalRightRange).arg(rightBar->maximum()));

    std::printf("console-scroll-split-tests: %d/%d checks passed\n",
                checks - failures, checks);
    return failures == 0 ? 0 : 1;
}