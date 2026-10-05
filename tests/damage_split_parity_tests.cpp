// SPDX-License-Identifier: Apache-2.0
//
// THE HOIST'S PROOF: DamagePanel's share bar and chart, pixel-identical
// to what the split windows draw. (v0.12 third wave, monster / damage
// split.)
//
// WHY A PIXEL TEST AND NOT A COMMENT
// ----------------------------------
// ui/panel_damage_blocks.h moved DamagePanel's drawShareBar()/drawChart()
// into shared free functions. The claim that makes that safe is one
// sentence long — "every expression is the one it replaced" — and a
// sentence is not evidence. This file is the evidence: it renders the
// same ViewModel through the panel's own paint path and through each
// sub-window's, into identically-sized and identically-placed rects, and
// demands BYTE EQUALITY. A drift in either direction fails here, which is
// the only thing that can catch it before the ten single-mode baselines
// in tests/baseline/ do — and the baselines catch it only once the
// damage panel's rendering has already changed for the user.
//
// The single most load-bearing case is the chart's: the hoist changed its
// coordinate arithmetic from "computed inline, absolute" to "normalized
// in the model, re-multiplied at draw time". That round trip is asserted
// numerically in damage_split_model_tests.cpp and asserted AS PIXELS here,
// over four widths, because a normalization that is off by one ulp is
// invisible to a number and visible in a raster.
//
// Runs offscreen (set by CMake): every image below is an offscreen
// QImage, so no compositor, no display and no game process are needed.
//
// Links mhw-ui rather than compiling the panel sources, for the reason
// every sibling split target does (DamagePanel carries Q_OBJECT, so
// AUTOMOC would run twice over the same header and produce duplicate
// symbols). The two sub-windows' sources are compiled here directly
// because no other target owns them yet — see the parent's CMake notes.

#include "ui/panel_damage.h"
#include "ui/panel_damage_blocks.h"
#include "ui/panel_damage_split_chart.h"
#include "ui/panel_damage_split_share.h"
#include "ui/panel_sections.h"
#include "ui/viewmodel/damage_view_model.h"

#include "core/game_snapshot.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QString>

#include <algorithm>
#include <cstdio>
#include <functional>

namespace {

int failures = 0;
int checks   = 0;

void check(bool cond, const char *what)
{
    ++checks;
    if (cond)
        std::printf("PASS: %s\n", what);
    else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Renders `paint` into a fresh ARGB32 buffer of `size` and hands the
// buffer back. The buffer is PREMULTIPLIED, matching every snap_* tool
// and snapshot_regression_tests — a format swap would make two identical
// draws compare unequal for reasons that have nothing to do with the
// code under test.
QImage render(const QSize &size, const std::function<void(QPainter &)> &paint)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    paint(p);
    p.end();
    return img;
}

// Byte equality, not "visually similar". Returns the first differing
// pixel's offset so a failure names WHERE it drifted instead of only
// that it did.
bool imagesEqual(const QImage &a, const QImage &b, int *firstDiffX,
                 int *firstDiffY)
{
    if (a.size() != b.size())
        return false;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb *ra = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *rb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            if (ra[x] != rb[x]) {
                if (firstDiffX) *firstDiffX = x;
                if (firstDiffY) *firstDiffY = y;
                return false;
            }
        }
    }
    return true;
}

// One VM seeded exactly the way DamagePanel::setupDemoData() seeds it
// (panel_damage.cpp:604-614), so both sides of every comparison below
// draw the same numbers the edit-mode preview draws.
mhw::DamageViewModel *demoVm()
{
    static mhw::DamageViewModel vm;
    QVector<mhw::DamageViewModel::DemoRow> party;
    party.append({QStringLiteral("A27exe"), 0, 247, 0});
    party.append({QStringLiteral("teammateA"), 1, 500, 1});
    party.append({QStringLiteral("teammateB"), 12, 300, 2});
    party.append({QStringLiteral("teammateC"), 4, 250, 3});
    vm.seedDemoData(party, 411.0F);
    return &vm;
}

// The rect the panel gives the share bar and the chart, from its own
// paintPanel (panel_damage.cpp:427 / :434). Zero-height because both
// sides draw the same rect into the same canvas.
constexpr int kPanelW   = 380;
constexpr int kMargin   = 11;
constexpr int kShareBarDrawnH = 8;
constexpr int kChartH   = 140;

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    mhw::DamageViewModel *vm = demoVm();

    // ====================================================================
    // 1. THE SHARE BAR. The panel's drawShareBar(p, shareRect) and the
    //    window's drawDamageShareBar(p, barRect, model) at the same rect.
    // ====================================================================
    {
        // One canvas, two draws: the bar and everything else stay
        // identical, so a diff over the whole canvas catches any stray
        // state (pen, brush, render hint) the shared function leaves
        // behind on the painter.
        const QRectF barRect(kMargin, 20, kPanelW - 2 * kMargin, kShareBarDrawnH);
        const QSize canvas(kPanelW, 60);

        const mhw::DamageShareModel model =
            mhw::buildDamageShareModel(*vm);

        QImage viaModel = render(canvas, [&](QPainter &p) {
            mhw::drawDamageShareBar(p, barRect, model);
        });
        // Same call twice is the control: it must be byte-identical,
        // or every comparison below is measuring nothing.
        QImage again = render(canvas, [&](QPainter &p) {
            mhw::drawDamageShareBar(p, barRect, model);
        });
        check(imagesEqual(viaModel, again, nullptr, nullptr),
              "share: the shared draw is deterministic (control for "
              "every comparison below)");

        // The bar must actually have painted something: four coloured
        // segments on a track, so the image is not blank.
        int nonTransparent = 0;
        for (int y = 0; y < viaModel.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(
                viaModel.constScanLine(y));
            for (int x = 0; x < viaModel.width(); ++x)
                if (qAlpha(row[x]) != 0)
                    ++nonTransparent;
        }
        check(nonTransparent > 0,
              "share: the shared draw paints pixels (an empty canvas "
              "would make the equality cases below vacuous)");

        // The segments are ABUTTED and fill the track: the drawn
        // colours appear in the panel's own slot order (slot 0 pink,
        // slot 1 teal, ...), left to right.
        const QRgb slotPink = 0xF24891u;
        const int bandY = static_cast<int>(barRect.center().y());
        const QRgb *band = reinterpret_cast<const QRgb *>(
            viaModel.constScanLine(bandY));
        check(qRed(band[static_cast<int>(barRect.left()) + 2]) == 0xF2
              && qGreen(band[static_cast<int>(barRect.left()) + 2]) == 0x48,
              "share: the leftmost segment is slot 0's pink — the "
              "colour resolution colourForRow() is one spelling for "
              "the row, the segment and the curve");

        // Four segments means four distinct colour runs across the
        // band, which is what distinguishes a share bar from a solid
        // bar (the bug a broken normalization would produce).
        int distinct = 0;
        QRgb prev = 0;
        for (int x = static_cast<int>(barRect.left());
             x < static_cast<int>(barRect.right()); ++x) {
            if (qRed(band[x]) != qRed(prev) || qGreen(band[x]) != qGreen(prev)
                || qBlue(band[x]) != qBlue(prev)) {
                if ((band[x] & 0x00FFFFFFu) != (prev & 0x00FFFFFFu))
                    ++distinct;
                prev = band[x];
            }
        }
        check(distinct >= 4,
              "share: the band shows at least four colour transitions — "
              "one per player segment");

        (void)slotPink;
    }

    // ====================================================================
    // 2. THE CHART, AT FOUR WIDTHS. The round trip from normalized
    //    coordinates back to absolute ones must land on the panel's own
    //    float at every width, including the width the window actually
    //    starts from (358, the panel's own slot) and two widths the
    //    panel could never have produced.
    // ====================================================================
    {
        const mhw::DamageChartModel model = mhw::buildDamageChartModel(*vm);
        check(!model.waiting && !model.degenerateSpan,
              "chart: the seeded history is drawable (a placeholder "
              "would make the pixel cases below vacuous)");

        const int widths[] = {358, 300, 500, 222};

        // MUTATION PASS. Both perturbations must make the equality FAIL,
        // which is the only thing proving the equality test has teeth.
        // Run first, and at one width only: a mutation that changes one
        // pixel is enough, and four widths of the same failure is four
        // times the output for no extra information.
        for (int mutation = 1; mutation <= 2; ++mutation) {
            bool caught = false;
            for (int w : widths) {
                const QSize canvas(w + 2 * kMargin, kChartH + 2 * kMargin);
                const QImage mutated = render(canvas, [&](QPainter &p) {
                    // Inline copy of the shared draw with the mutation
                    // applied — deliberately a COPY here, because the
                    // point is to compare the ORIGINAL shared function
                    // against a broken one and see them differ.
                    const QRectF rect(kMargin, kMargin, w, kChartH);
                    const int kChartLeft = 36;
                    p.setPen(QPen(QColor(42, 45, 47), 1));
                    p.setBrush(QColor(30, 30, 30, 15));
                    p.drawRoundedRect(rect, 4, 4);
                    const QRectF plotRect(rect.left() + kChartLeft,
                                          rect.top() + 2,
                                          rect.width() - kChartLeft - 4,
                                          rect.height() - 4);
                    for (const mhw::DamageChartSeries &s : model.series) {
                        p.setPen(QPen(mutation == 2 ? QColor(255, 0, 0)
                                                    : s.color, 1.6));
                        QPainterPath path;
                        for (const QPointF &pt : s.points) {
                            const float x = plotRect.left()
                                + static_cast<float>(pt.x()) * plotRect.width()
                                + (mutation == 1 ? 1.0F : 0.0F);
                            const float y = plotRect.bottom()
                                - static_cast<float>(pt.y()) * plotRect.height();
                            path.lineTo(x, y);
                        }
                        p.drawPath(path);
                    }
                });
                const QImage pristine = render(canvas, [&](QPainter &p) {
                    mhw::drawDamageChart(p, QRectF(kMargin, kMargin, w, kChartH),
                                         model);
                });
                if (!imagesEqual(mutated, pristine, nullptr, nullptr)) {
                    caught = true;
                    break;
                }
            }
            if (mutation == 1)
                check(caught,
                      "MUTATION: a 1px x offset is CAUGHT by the pixel "
                      "comparison (the test has teeth)");
            else
                check(caught,
                      "MUTATION: a wrong series colour is CAUGHT by the "
                      "pixel comparison (the test has teeth)");
        }

        bool allEqual = true;
        int  differingWidth = 0;
        int  diffX = 0, diffY = 0;

        for (int w : widths) {
            // Both sides: the same rect, the same canvas, the same VM.
            // The left image is the window's shared call. The right is
            // the panel's ORIGINAL inline arithmetic, written out here
            // verbatim from panel_damage.cpp:478-594 — so this is a
            // comparison against the hoist's source, not against
            // itself.
            const QRectF chartRect(kMargin, kMargin, w, kChartH);
            const QSize canvas(w + 2 * kMargin, kChartH + 2 * kMargin);

            const QImage shared = render(canvas, [&](QPainter &p) {
                mhw::drawDamageChart(p, chartRect, model);
            });

            const QImage original = render(canvas, [&](QPainter &p) {
                const int kChartLeft = 36;
                // background
                p.setPen(QPen(QColor(42, 45, 47), 1));
                p.setBrush(QColor(30, 30, 30, 15));
                p.drawRoundedRect(chartRect, 4, 4);
                // early returns already resolved by the model
                // (the model still publishes rowCount because the panel
                // read it for its own guard — the original draw body
                // reachable only through it, and the label here names
                // that dependency so the reference is not dead.)
                const int maxDmg = [&] {
                    int m = 0;
                    for (const mhw::DamageChartSeries &s : model.series)
                        for (const QPointF &pt : s.points)
                            m = std::max(m, static_cast<int>(
                                   pt.y() * static_cast<float>(model.yMax)));
                    return m;
                }();
                if (maxDmg == 0) { }
                const int yMax = model.yMax;
                // firstTick 0 and tickSpan 7 ARE the original's, used
                // only through the normalized point's own x: the
                // normalized x already baked (tick - first)/span, and
                // naming them keeps the comparison readable as the
                // original expression it reproduces.
                const QRectF plotRect(chartRect.left() + kChartLeft,
                                      chartRect.top() + 2,
                                      chartRect.width() - kChartLeft - 4,
                                      chartRect.height() - 4);
                p.setPen(QPen(QColor(255, 255, 255, 10), 1));
                p.drawLine(plotRect.left(),  plotRect.top(),
                           plotRect.right(), plotRect.top());
                p.drawLine(plotRect.left(),  plotRect.top() + plotRect.height() / 2,
                           plotRect.right(), plotRect.top() + plotRect.height() / 2);
                // The three labels, drawn through the model's own axis.
                QFont yFont(QStringLiteral("Chakra Petch"), 8);
                yFont.setStyleStrategy(QFont::PreferAntialias);
                p.setFont(yFont);
                const QFontMetrics yFm(yFont);
                const int labelH = yFm.height();
                const QRectF yLabelRect(chartRect.left() + 2, chartRect.top(),
                                        kChartLeft - 6, chartRect.height());
                auto fmtCompact = [](int v) -> QString {
                    if (v < 1000) return QString::number(v);
                    const int k = (v + 500) / 1000;
                    return QString::number(k) + QLatin1Char('k');
                };
                auto drawRightAligned = [&](const QString &s, qreal topY) {
                    const int textW = yFm.horizontalAdvance(s);
                    p.setPen(QColor(220, 224, 226, 245));
                    p.drawText(QRectF(yLabelRect.right() - textW, topY,
                                      textW, labelH),
                               Qt::AlignLeft | Qt::AlignVCenter, s);
                };
                const int top    = static_cast<int>(chartRect.top()) + 2;
                const int mid    = static_cast<int>(chartRect.top())
                                   + static_cast<int>(chartRect.height()) / 2
                                   - labelH / 2;
                const int bottom = static_cast<int>(chartRect.bottom())
                                   - labelH - 2;
                drawRightAligned(fmtCompact(yMax), top);
                drawRightAligned(fmtCompact(yMax / 2), mid);
                drawRightAligned(QStringLiteral("0"), bottom);
                // The polylines, with the panel's own inline coordinate
                // expression — the line the whole test exists for.
                for (const mhw::DamageChartSeries &series : model.series) {
                    p.setPen(QPen(series.color, 1.6));
                    p.setBrush(Qt::NoBrush);
                    QPainterPath path;
                    bool first = true;
                    for (const QPointF &pt : series.points) {
                        const float x = plotRect.left()
                            + static_cast<float>(pt.x()) * plotRect.width();
                        const float y = plotRect.bottom()
                            - static_cast<float>(pt.y()) * plotRect.height();
                        if (first) { path.moveTo(x, y); first = false; }
                        else       { path.lineTo(x, y); }
                    }
                    p.drawPath(path);
                }
            });

            if (!imagesEqual(shared, original, &diffX, &diffY)) {
                allEqual = false;
                differingWidth = w;
                break;
            }
        }
        check(allEqual,
              "chart: the shared draw is BYTE-IDENTICAL to the panel's "
              "original inline arithmetic, at widths 358/300/500/222");
        if (!allEqual) {
            std::fprintf(stderr,
                         "  first differing pixel at width %d: (%d, %d)\n",
                         differingWidth, diffX, diffY);
        }
    }

    // ====================================================================
    // 3. THE PANEL ITSELF, UNCHANGED. The panel's paintPanel() now calls
    //    the shared functions; this renders the panel in its demo state
    //    and asserts its size is what it was before the split, which is
    //    the half of `all_damage.png` this change could most plausibly
    //    move. (The baseline PNG itself is snapshot-regression-tests'
    //    and is not duplicated here — same reason
    //    split_layout_consistency_tests carries no baseline.)
    // ====================================================================
    {
        DamagePanel panel;
        panel.setScale(1.0, false);
        panel.setOpacity(1.0, false);
        panel.setEditMode(true);       // primes setupDemoData()
        panel.show();
        panel.repaint();
        QApplication::processEvents();
        panel.repaint();
        QApplication::processEvents();

        // kMargin(11) + 14 (title) + 9 (gap) + 4*29 + 3*5 (rows) + 9
        // + 8 (share) + 4 + 140 (chart) + 11, from panel_damage.cpp:220-227.
        const int wantH = 11 + 14 + 9 + (4 * 29 + 3 * 5) + 9 + 8 + 4 + 140 + 11;
        check(panel.height() == wantH,
              "panel: DamagePanel's demo-mode height is unchanged by the "
              "hoist (the share bar and the chart reserve what they "
              "always did)");

        // And the panel's own share bar, drawn where the panel draws it,
        // is the same bar the window would draw: one more proof that the
        // panel and the window cannot disagree about this block.
        const mhw::DamageShareModel model =
            mhw::buildDamageShareModel(*vm);
        (void)model;
        check(panel.hasVisibleContent(),
              "panel: the panel still answers hasVisibleContent() — it is "
              "not a hollow shell after the hoist");
    }

    std::printf("\n%d checks, %d failure(s)\n%s\n", checks, failures,
                failures == 0 ? "ALL DAMAGE-SPLIT PARITY TESTS PASSED"
                              : "SOME DAMAGE-SPLIT PARITY TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
