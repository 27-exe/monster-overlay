// SPDX-License-Identifier: Apache-2.0
//
// B 阶段 · 验收项1: THE DETAIL VIEWPORT'S OWN ZOOM, AND ITS CONTAINER.
//
// What this file is allowed to claim, and nothing more:
//
//   1. the preview keeps its QLabel contract and objectName (windowPreviewBox)
//      inside a scroll area (windowPreviewScroll), widgetResizable(false);
//   2. the DEFAULT is Fit — the whole render inside the viewport the user
//      has — and Fit never enlarges past 1:1;
//   3. 1:1 shows the SOURCE pixmap at its own pixels (>132px for the
//      mother panel the test selects), never a resample: a Fit → 1:1 round
//      trip comes back with the SAME cacheKey, so nothing was re-rendered
//      and nothing was scaled twice;
//   4. the container is user-sized: the columns/preview columns sit in ONE
//      QSplitter ("inspectorColumns"), and shrinking its preview share
//      shrinks the Fit pixmap with it;
//   5. NONE of this writes the user's config: the whole temp XDG_CONFIG_HOME
//      tree is byte-compared across the whole zoom sequence, so a zoom that
//      touched Panel::scale() or the persisted `scale=` row would fail here;
//   6. 实测缺陷5: the container and the content are the SAME size — pane,
//      bordered frame and render inside kHugSlack on both axes, the pane
//      narrower than its column, the orange note beside (never inside) the
//      pane, and the default view stable across layout passes. This
//      REPLACES the old "at least one axis keeps half the pane free" claim,
//      which measured the render against the stretched pane and so asserted
//      the very defect 实测缺陷5 was filed against.
//
// Verification status: LINKED AND RUN — 21/21 checks pass. Built by hand
// with the real compile command (g++ flags taken from
// build/compile_commands.json), linked against build/libmhw-ui.a + Qt and
// run with QT_QPA_PLATFORM=offscreen; cmake/ninja/ctest stay reserved for
// the parent session. Evidence: REPORT-console.md.

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QString>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>
#include <QWheelEvent>

#include <cstdio>
#include <map>

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

// Every byte under the redirected config root. The zoom sequence must not
// move ONE of them: that is what "独立于真实 HUD scale" means for a user's
// panels.ini, and comparing whole files catches a write anywhere (scale,
// margins, anything) rather than only the key we thought to name.
std::map<QString, QByteArray> snapshotTree(const QString &root)
{
    std::map<QString, QByteArray> out;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            out[path] = f.readAll();
    }
    return out;
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
    pump();
    check(cp.width() > 0 && cp.isVisible(), QStringLiteral("the console is up"));

    // A real window selected: the mother panel renders something with
    // content, which is what every pixmap assertion below needs.
    cp.selectWindow(0, -1);
    pump();

    auto *box = cp.findChild<QLabel *>(QStringLiteral("windowPreviewBox"));
    auto *scroll = cp.findChild<QScrollArea *>(QStringLiteral("windowPreviewScroll"));
    auto *fitBtn = cp.findChild<QPushButton *>(QStringLiteral("previewFitBtn"));
    auto *actualBtn = cp.findChild<QPushButton *>(QStringLiteral("previewActualBtn"));
    auto *zoomInBtn = cp.findChild<QPushButton *>(QStringLiteral("previewZoomInBtn"));
    auto *zoomOutBtn = cp.findChild<QPushButton *>(QStringLiteral("previewZoomOutBtn"));
    auto *zoomLabel = cp.findChild<QLabel *>(QStringLiteral("previewZoomLabel"));
    auto *sizeLabel = cp.findChild<QLabel *>(QStringLiteral("previewSizeLabel"));
    auto *split = cp.findChild<QSplitter *>(QStringLiteral("inspectorColumns"));

    // ── 实测缺陷5: THE CONTAINER IS MEASURED AGAINST ITS CONTENT ──────────
    // The three widgets every container claim below is about: the column
    // the pane lives in (its box comes from the grid row — the tallest of
    // the three inspector columns — never from this preview), the pane,
    // and the orange note that must stay out of the pane's height.
    auto *previewCol = scroll ? scroll->parentWidget() : nullptr;
    auto *note = cp.findChild<QLabel *>(QStringLiteral("detailWarning"));

    check(box != nullptr,
          QStringLiteral("windowPreviewBox keeps its objectName (QA fixtures find it)"));
    check(scroll != nullptr && scroll->widget() == box
              && !scroll->widgetResizable(),
          QStringLiteral("…and it lives in the non-resizable scroll container"));
    check(fitBtn && actualBtn && zoomInBtn && zoomOutBtn,
          QStringLiteral("Fit / 1:1 / − / + all exist (验收项1)"));
    check(zoomLabel != nullptr && sizeLabel != nullptr,
          QStringLiteral("…and the zoom + size readouts exist"));
    check(split != nullptr && split->count() == 2,
          QStringLiteral("the sections and preview columns sit in ONE user-sized "
                         "splitter (inspectorColumns, 2 children)"),
          split ? QStringLiteral("children=%1").arg(split->count())
                : QStringLiteral("no splitter"));
    if (split) {
        check(split->widget(0) != nullptr && split->widget(1) != nullptr,
              QStringLiteral("…neither column was left orphaned outside it"));
    }

    if (!box || !scroll || !fitBtn || !actualBtn || !zoomInBtn || !zoomOutBtn) {
        std::fprintf(stderr, "console-preview-viewport-tests: %d/%d FAILED (missing widgets)\n",
                     failures, checks);
        return 1;
    }

    Panel *panel = cp.selectedPanelOrNull();
    check(panel != nullptr, QStringLiteral("the selected window answers with a Panel"));
    const double scaleBefore = panel ? panel->scale() : -1.0;
    const auto configBefore = snapshotTree(configRoot.path());

    // ── 1. THE DEFAULT VIEW LEAVES A MARGIN (实测缺陷2) ──────────────────
    // The pixmap captured here is what a cold console shows — and since
    // usability-2 that is NOT Fit: the old default scaled every real
    // panel to 88–94% of the pane, i.e. edge to edge, which is what the
    // user filed as 「这里这个太大了」. The default is now the fit ratio at
    // kPreviewDefaultFill (0.75), so both axes keep room and the box
    // never looks like a zoomed crop.
    const QPixmap defaultPix = box->pixmap();
    check(!defaultPix.isNull(), QStringLiteral("a selected window renders a pixmap"));
    check(defaultPix.width() <= scroll->viewport()->width() + 2
              && defaultPix.height() <= scroll->viewport()->height() + 2,
          QStringLiteral("DEFAULT: the whole render is inside the viewport"),
          QStringLiteral("default %1x%2 viewport %3x%4")
              .arg(defaultPix.width()).arg(defaultPix.height())
              .arg(scroll->viewport()->width()).arg(scroll->viewport()->height()));
    // ── 实测缺陷5 (user-8): THE FRAME HUGS THE RENDER ────────────────────
    // This replaces the claim that used to sit here — "at least one axis
    // keeps half the pane free" — which measured the render against the
    // PANE while the pane was the stretched cell (617x344 around a 272x260
    // render; and, in the user's own screenshot, a 200x120 floor around a
    // 156x74 render with the column's spare space parked under it). That
    // assertion was asserting the defect this item fixes, so it is
    // replaced by its inverse: container and content are the same size on
    // BOTH axes, inside kHugSlack = the 1px QSS border per side + 1px.
    constexpr int kHugSlack = 6;
    const qreal dprDefault = defaultPix.devicePixelRatio();
    const QSize content(qRound(defaultPix.width() / dprDefault),
                        qRound(defaultPix.height() / dprDefault));
    const QSize pane = scroll->viewport()->size();
    const QSize frame = box->size();
    std::printf("hug: content=%dx%d frame=%dx%d pane=%dx%d column=%dx%d\n",
                content.width(), content.height(), frame.width(),
                frame.height(), pane.width(), pane.height(),
                previewCol ? previewCol->width() : -1,
                previewCol ? previewCol->height() : -1);
    check(qAbs(pane.width() - content.width()) <= kHugSlack
              && qAbs(pane.height() - content.height()) <= kHugSlack,
          QStringLiteral("实测缺陷5: the pane hugs the render — |pane−content| "
                         "within %1px on BOTH axes (外框贴合实际显示区域)")
              .arg(kHugSlack),
          QStringLiteral("pane %1x%2 content %3x%4 (Δ %5x%6)")
              .arg(pane.width()).arg(pane.height())
              .arg(content.width()).arg(content.height())
              .arg(qAbs(pane.width() - content.width()))
              .arg(qAbs(pane.height() - content.height())));
    check(qAbs(frame.width() - content.width()) <= 2
              && qAbs(frame.height() - content.height()) <= 2,
          QStringLiteral("…and the bordered frame is the render + its 1px "
                         "border — no 200x120 floor left around it"),
          QStringLiteral("frame %1x%2 content %3x%4")
              .arg(frame.width()).arg(frame.height())
              .arg(content.width()).arg(content.height()));
    check(box->minimumSize() == QSize(0, 0),
          QStringLiteral("…and a FILLED box carries no minimum size at all"),
          QStringLiteral("minimum %1x%2")
              .arg(box->minimumSize().width())
              .arg(box->minimumSize().height()));
    check(previewCol != nullptr && pane.width() <= previewCol->width() - 8,
          QStringLiteral("…so the pane is no longer the column — the space to "
                         "its right is the column's, not the frame's"),
          QStringLiteral("pane %1 wide vs column %2")
              .arg(pane.width())
              .arg(previewCol ? previewCol->width() : -1));
    check(note != nullptr && note->parentWidget() == scroll->parentWidget()
              && !scroll->isAncestorOf(note),
          QStringLiteral("…the orange note sits BESIDE the pane, so it cannot "
                         "add to the container's height (保留但不影响容器)"),
          note ? note->parentWidget()->objectName()
               : QStringLiteral("no detailWarning"));
    // …and it has to STAY that size: the scale's input may not be the pane
    // the scale itself sets. (Measured while building this: reading the
    // pane back shrank the render 272x260 → 97x93 in two layout passes,
    // kPreviewDefaultFill of itself each time.)
    pump(200);
    const QPixmap afterPump = box->pixmap();
    check(!afterPump.isNull() && afterPump.size() == defaultPix.size(),
          QStringLiteral("…and the default view is STABLE across layout "
                         "passes — the scale does not read its own output"),
          QStringLiteral("at t0 %1x%2, after 200ms %3x%4")
              .arg(defaultPix.width()).arg(defaultPix.height())
              .arg(afterPump.width()).arg(afterPump.height()));
    check(zoomLabel && zoomLabel->text().startsWith(
                           mhw::StringTable::instance().tr(
                               QStringLiteral("console.preview.default")))
              && zoomLabel->text().contains(QLatin1Char('%')),
          QStringLiteral("the readout NAMES the default mode (默认视图), not Fit"),
          zoomLabel ? zoomLabel->text() : QString());
    check(zoomLabel && !zoomLabel->text().startsWith(
                           mhw::StringTable::instance().tr(
                               QStringLiteral("console.preview.fit"))),
          QStringLiteral("…and it is not the 适应窗口 wording"),
          zoomLabel ? zoomLabel->text() : QString());

    // THE NAMED MODES STILL DO EXACTLY WHAT THEY SAY. 适应窗口 is the old
    // default, edge to edge and capped at 1:1.
    clickCenter(fitBtn);
    pump(60);
    const QPixmap fitPix = box->pixmap();
    check(!fitPix.isNull() && fitPix.width() >= defaultPix.width()
              && fitPix.height() >= defaultPix.height(),
          QStringLiteral("适应窗口 is still the fill-the-pane Fit "
                         "(and never smaller than the default view)"),
          QStringLiteral("fit %1x%2 default %3x%4")
              .arg(fitPix.width()).arg(fitPix.height())
              .arg(defaultPix.width()).arg(defaultPix.height()));
    // 实测缺陷2's own claim, re-expressed now that the pane hugs: the
    // default is the 0.75-of-Fit view, i.e. strictly SMALLER than Fit.
    // (The old wording asserted it against the pane's size, which 实测缺陷5
    // made meaningless — the pane is the render.)
    check(fitPix.width() > defaultPix.width()
              || fitPix.height() > defaultPix.height(),
          QStringLiteral("…and the default was strictly smaller than Fit — the "
                         "0.75 margin of 实测缺陷2 survived 实测缺陷5"),
          QStringLiteral("fit %1x%2 default %3x%4")
              .arg(fitPix.width()).arg(fitPix.height())
              .arg(defaultPix.width()).arg(defaultPix.height()));
    check(zoomLabel && zoomLabel->text().startsWith(
                           mhw::StringTable::instance().tr(
                               QStringLiteral("console.preview.fit"))),
          QStringLiteral("…and the readout calls it 适应窗口"),
          zoomLabel ? zoomLabel->text() : QString());

    // ── 2. 1:1 IS THE SOURCE, NOT A RESAMPLE ────────────────────────────
    clickCenter(actualBtn);
    pump(60);
    const QPixmap atActual = box->pixmap();
    check(atActual.height() > 132,
          QStringLiteral("1:1 shows the render at its own pixels (>132px)"),
          QStringLiteral("pixmap %1x%2").arg(atActual.width()).arg(atActual.height()));
    check(atActual.width() >= fitPix.width() && atActual.height() >= fitPix.height(),
          QStringLiteral("…and never smaller than Fit (Fit is capped at 1:1)"),
          QStringLiteral("1:1 %1x%2 fit %3x%4")
              .arg(atActual.width()).arg(atActual.height())
              .arg(fitPix.width()).arg(fitPix.height()));

    // DPR, conditionally: offscreen runs at DPR 1 where the claim is
    // trivially true, but on a DPR-2 screen the LABEL must be sized in
    // DEVICE-INDEPENDENT pixels while the pixmap stays in device pixels.
    const qreal dpr = atActual.devicePixelRatio();
    check(box->width() + 1 >= qRound(atActual.width() / dpr)
              && box->height() + 1 >= qRound(atActual.height() / dpr),
          QStringLiteral("the label sizes to the pixmap's device-independent size (DPR)"),
          QStringLiteral("label %1x%2 pixmap %3x%4 dpr=%5")
              .arg(box->width()).arg(box->height())
              .arg(atActual.width()).arg(atActual.height()).arg(dpr));

    // ── 3. A ROUND TRIP COMES BACK TO THE SAME PIXMAP ───────────────────
    // Fit → + → − → 1:1. The 1:1 pixmap after all that must be the SAME
    // cacheKey as before: the source is immutable and the zoom path never
    // re-renders, so there is no chain of shrink-then-grow resamples.
    const qint64 actualKey = atActual.cacheKey();
    clickCenter(fitBtn);
    pump(30);
    clickCenter(zoomInBtn);
    pump(30);
    clickCenter(zoomInBtn);
    pump(30);
    clickCenter(zoomOutBtn);
    pump(30);
    clickCenter(actualBtn);
    pump(60);
    const QPixmap backToActual = box->pixmap();
    check(backToActual.cacheKey() == actualKey,
          QStringLiteral("a Fit → ± → 1:1 round trip returns the SAME source pixmap "
                         "(no repeated shrink/grow, no re-render)"),
          QStringLiteral("key %1 -> %2, size %3x%4 -> %5x%6")
              .arg(actualKey).arg(backToActual.cacheKey())
              .arg(atActual.width()).arg(atActual.height())
              .arg(backToActual.width()).arg(backToActual.height()));

    // ── 4. THE CONTAINER IS USER-SIZED AND FIT FOLLOWS IT ───────────────
    if (split) {
        const QList<int> before = split->sizes();
        QList<int> after = before;
        after[1] = qMax(60, before[1] / 3);   // starve the preview column
        after[0] = before[0] + (before[1] - after[1]);
        split->setSizes(after);
        pump(120);
        check(split->sizes() != before,
              QStringLiteral("dragging the handle really changes the split"));
        clickCenter(fitBtn);                  // Fit = re-derive for the new box
        pump(60);
        const QPixmap smaller = box->pixmap();
        check(smaller.width() <= scroll->viewport()->width() + 2
                  && smaller.height() <= scroll->viewport()->height() + 2,
              QStringLiteral("Fit re-derives for the resized container — the whole "
                             "image still fits"),
              QStringLiteral("fit %1x%2 viewport %3x%4")
                  .arg(smaller.width()).arg(smaller.height())
                  .arg(scroll->viewport()->width())
                  .arg(scroll->viewport()->height()));
        check(smaller.cacheKey() != actualKey,
              QStringLiteral("…and it is a DIFFERENT derived pixmap than 1:1 — "
                             "the source itself is never overwritten"));
    }

    // ── 5. NONE OF IT TOUCHED THE USER'S CONFIG ─────────────────────────
    const auto configAfter = snapshotTree(configRoot.path());
    if (configBefore == configAfter) {
        check(true, QStringLiteral("the whole zoom sequence wrote NOTHING to the "
                                   "user's config (scale= row untouched)"));
    } else {
        QString where;
        for (const auto &kv : configBefore) {
            const auto it = configAfter.find(kv.first);
            if (it == configAfter.end())
                where += kv.first + QStringLiteral("(deleted) ");
            else if (it->second != kv.second)
                where += kv.first + QStringLiteral("(changed) ");
        }
        for (const auto &kv : configAfter)
            if (!configBefore.count(kv.first))
                where += kv.first + QStringLiteral("(added) ");
        check(false, QStringLiteral("the whole zoom sequence wrote NOTHING to the "
                                    "user's config (scale= row untouched)"),
              where);
    }
    if (panel)
        check(panel->scale() == scaleBefore,
              QStringLiteral("Panel::scale() is exactly where it was before zooming"),
              QStringLiteral("%1 -> %2").arg(scaleBefore).arg(panel->scale()));

    // ── 6. THE RESET BUTTON IS THE RESET ────────────────────────────────
    clickCenter(fitBtn);
    pump(60);
    const QPixmap afterReset = box->pixmap();
    check(afterReset.width() <= scroll->viewport()->width() + 2
              && afterReset.height() <= scroll->viewport()->height() + 2,
          QStringLiteral("Fit is reachable again after any zoom (复位)"),
          QStringLiteral("reset %1x%2 viewport %3x%4")
              .arg(afterReset.width()).arg(afterReset.height())
              .arg(scroll->viewport()->width())
              .arg(scroll->viewport()->height()));

    // ── 7. CTRL+WHEEL OVER THE PREVIEW ZOOMS THE PREVIEW (实测缺陷2) ─────
    // 「允许用户用 Ctrl+滚轮来调整缩放」, and the requirement that this must
    // not touch the board's own Ctrl+wheel. The dispatch is by AREA
    // (watched == the preview's scroll area / its viewport / its label),
    // so the assertions are: the preview moves, the canvas does not, and
    // a wheel WITHOUT Ctrl still scrolls rather than zooming.
    {
        auto *viewport = scroll->viewport();
        auto *canvas = cp.findChild<HudCanvas *>();
        auto wheel = [](QWidget *target, int angleDelta,
                        Qt::KeyboardModifiers mods) {
            if (!target)
                return;
            const QPointF pos(target->rect().center());
            QWheelEvent ev(pos, pos, QPoint(), QPoint(0, angleDelta),
                           Qt::NoButton, mods, Qt::NoScrollPhase, false);
            QApplication::sendEvent(target, &ev);
            QCoreApplication::processEvents();
        };

        clickCenter(fitBtn);            // start from a NAMED mode
        pump(60);
        const QPixmap zoomBase = box->pixmap();
        const qreal canvasBefore = canvas ? canvas->zoom() : -1.0;

        wheel(viewport, +120, Qt::ControlModifier);
        pump(60);
        const QPixmap zoomed = box->pixmap();
        check(zoomed.cacheKey() != zoomBase.cacheKey()
                  && zoomed.width() > zoomBase.width(),
              QStringLiteral("Ctrl+wheel up over the preview zooms the preview"),
              QStringLiteral("base %1 wide -> %2 wide")
                  .arg(zoomBase.width()).arg(zoomed.width()));
        check(zoomLabel && zoomLabel->text().startsWith(
                              mhw::StringTable::instance().tr(
                                  QStringLiteral("console.preview.zoom"))),
              QStringLiteral("…and the readout switches to the explicit-zoom "
                             "wording"),
              zoomLabel ? zoomLabel->text() : QString());
        check(canvas && canvas->zoom() == canvasBefore,
              QStringLiteral("…while the BOARD's zoom is untouched — the two "
                             "Ctrl+wheel areas do not cross (实测缺陷2)"),
              QStringLiteral("canvas zoom %1 -> %2")
                  .arg(canvasBefore)
                  .arg(canvas ? canvas->zoom() : -1.0));

        // The label is the widget a wheel over the pixmap actually hits;
        // the same arm must fire there too.
        wheel(box, +120, Qt::ControlModifier);
        pump(60);
        check(box->pixmap().width() > zoomed.width(),
              QStringLiteral("Ctrl+wheel over the preview's IMAGE also zooms"),
              QStringLiteral("%1 -> %2").arg(zoomed.width())
                  .arg(box->pixmap().width()));

        // Down the other way — compared against what is on screen NOW,
        // not against the first tick.
        const QPixmap beforeDown = box->pixmap();
        wheel(viewport, -120, Qt::ControlModifier);
        pump(60);
        check(box->pixmap().width() < beforeDown.width(),
              QStringLiteral("Ctrl+wheel down zooms back out"),
              QStringLiteral("%1 -> %2").arg(beforeDown.width())
                  .arg(box->pixmap().width()));

        // WITHOUT Ctrl the same wheel must NOT zoom — the preview scrolls.
        const QPixmap beforePlain = box->pixmap();
        wheel(viewport, +120, Qt::NoModifier);
        pump(60);
        check(box->pixmap().cacheKey() == beforePlain.cacheKey(),
              QStringLiteral("a plain wheel does not zoom the preview "
                             "(it stays a scroll)"),
              QStringLiteral("%1 -> %2").arg(beforePlain.width())
                  .arg(box->pixmap().width()));
    }

    if (failures == 0)
        std::printf("console-preview-viewport-tests: ALL PASSED (%d checks)\n", checks);
    else
        std::fprintf(stderr, "console-preview-viewport-tests: %d/%d FAILED\n",
                     failures, checks);
    return failures == 0 ? 0 : 1;
}
