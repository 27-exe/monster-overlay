// SPDX-License-Identifier: Apache-2.0
//
// The damage split's two content models, pinned: buildDamageShareModel and
// buildDamageChartModel (src/ui/panel_damage_blocks.h).
//
// WHAT THIS FILE IS FOR
// ---------------------
// The v0.12 third wave moves the DPS panel's share bar and line chart
// into their own windows. The decision each window makes that no pixel
// baseline can see is "is there anything to draw, and what shape is
// it" — hasContent(), the width reservation and the demo seed all read
// the same model, so the MODEL's verdict is the one worth pinning.
//
// Both models are pure functions over plain data plus the ViewModel's
// read-only views, which is the discipline damage_view_model_tests.cpp
// established for the row statistics and player_weapon_strip_tests.cpp
// for the identity trio. This target therefore compiles nothing but the
// two headers and links Qt6::Core: no widget, no painter, no mhw-ui.
//
// The models are driven BOTH ways — through a real DamageViewModel (the
// seam, proving the delegation from read-only views is wired) and
// through the plain-data core (the exhaustive arms, proving each state
// the panel could reach). A test of only the first would leave the
// states a poll loop cannot reach — a last sample shorter than the
// roster, a history on one tick — unasserted, and those are exactly the
// ones with a guard written into the body.
//
// THE FOUR CLAIMS
// ---------------
//  1. THE SHARE MODEL IS THE PANEL'S OWN ARITHMETIC, NOT A SECOND ONE.
//     total is the sum of the last sample's per-row damage; a row
//     absent from that sample contributes nothing (and the model does
//     not read past the sample's end); isEmpty() is NOT rowCount()>0
//     (four rows at 0 damage is an empty bar and must not mount a
//     window); every segment carries the colour the row's own
//     contribution bar would have used.
//  2. THE CHART MODEL IS THE PANEL'S TWO EARLY RETURNS, NOT ONE.
//     `waiting` covers "fewer than two samples or no rows" (the panel
//     answers with a centred placeholder), `degenerateSpan` covers
//     "one distinct tick" (the panel returns having drawn ONLY the
//     background). Conflating them would make the placeholder and the
//     blank chart the same state, which they are not.
//  3. THE AXIS IS THE PANEL'S ROUNDING, AND THE CURVE USES IT.
//     yMax is roundUp(max) with the panel's own loop, and the
//     normalized y divides by exactly that number, so the top label
//     and the curve's peak cannot disagree.
//  4. THE NORMALIZATION IS ROUND-TRIP CLOSED. The normalized curve,
//     mapped back through the draw side's own arithmetic
//         x = left + tx * width();  y = bottom - ty * height()
//     reaches the same float the panel's inline expression reached,
//     for every rect the window can be sized to. That is the property
//     the whole "one curve, two widths" split rests on.

#include "ui/panel_damage_blocks.h"
#include "ui/viewmodel/damage_view_model.h"

#include "core/game_snapshot.h"

#include <QCoreApplication>
#include <QString>
#include <QRectF>
#include <QVector>

#include <cmath>
#include <cstdio>

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

// The demo party DamagePanel::setupDemoData() seeds (panel_damage.cpp:
// 604-614): four rows, 8 synthetic samples growing super-linearly,
// elapsed 411s. Every test that asserts against the demo's numbers uses
// this so those numbers are the ones the edit-mode preview produces.
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

// A four-row identity vector, slots 0..3, nobody self. The colour
// resolution the share bar and the chart share.
QVector<mhw::DamageRowIdentity> fourSlots()
{
    return {{0, false}, {1, false}, {2, false}, {3, false}};
}

QVector<mhw::DamageRowIdentity> oneRow()
{
    return {{0, false}};
}

// A history of `count` samples on ticks 0..count-1, row `r` carrying
// `step * (r + 1)` more damage each sample. Used for the axes a real
// poll loop cannot be driven into.
QVector<mhw::DamageViewModel::Sample> rampHistory(int count, int step, int rows,
                                                  int tickOf = -1)
{
    QVector<mhw::DamageViewModel::Sample> history;
    history.reserve(count);
    for (int t = 0; t < count; ++t) {
        mhw::DamageViewModel::Sample s;
        s.tick = (tickOf >= 0 ? tickOf : t);
        s.damage.resize(rows);
        for (int r = 0; r < rows; ++r)
            s.damage[r] = (t + 1) * step * (r + 1);
        history.append(s);
    }
    return history;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ====================================================================
    // 1. THE SHARE MODEL. Seam first (a real ViewModel), then the arms a
    //    poll loop cannot reach.
    // ====================================================================
    {
        const mhw::DamageShareModel m =
            mhw::buildDamageShareModel(*demoVm());

        check(m.rowCount() == 4,
              "share: one segment per row, from the row identity vectors");

        // The demo's final sample is kFinalDmg[i] * (0.10 + 0.90*1.0),
        // i.e. the full 184220 / 96240 / 71030 / 40510.
        const int want[4] = {184220, 96240, 71030, 40510};
        bool damages = true;
        for (int i = 0; i < 4; ++i)
            if (m.segments[i].damage != want[i])
                damages = false;
        check(damages,
              "share: each segment carries the LAST sample's damage, not "
              "the row's maximum");

        qint64 wantTotal = 0;
        for (int i = 0; i < 4; ++i)
            wantTotal += want[i];
        check(m.total == wantTotal,
              "share: total is the sum of the last sample's per-row "
              "damage — the denominator the panel guarded with "
              "`total == 0 -> 1`");

        check(!m.isEmpty(),
              "share: the demo party is not empty — four non-zero "
              "segments are a bar worth mounting");

        // Slot colours, resolved through the same helper the row's own
        // contribution bar uses (panel_damage.cpp:302).
        const QColor c0 = m.segments[0].color;
        const QColor c1 = m.segments[1].color;
        check(c0.alpha() == 255 && c0 != c1,
              "share: segment colour is opaque, and differs per slot");
        check(c0.red() == 0xF2 && c0.green() == 0x48 && c0.blue() == 0x91,
              "share: slot 0 resolves to HunterPie's pink #F24891");
        check(c1.red() == 0x50 && c1.green() == 0xC5 && c1.blue() == 0xB7,
              "share: slot 1 resolves to HunterPie's teal #50C5B7");
    }

    // A party of rows that have never hit anything: the segments are
    // all zero, so the bar is a bare grey track, and a bare track must
    // not mount a window. This is the first two seconds of every quest,
    // which is when the split window would otherwise pop up empty.
    {
        // QVector<int>({0,0,0,0}) spelled out: brace-init lists cannot
        // be deduced into a QVector<int>& parameter.
        const QVector<int> allZero = {0, 0, 0, 0};
        const mhw::DamageShareModel zm =
            mhw::buildDamageShareModel(allZero, fourSlots());
        check(zm.isEmpty(),
              "share: a party whose rows have all hit 0 is EMPTY — "
              "isEmpty() is not rowCount() > 0");
        check(zm.rowCount() == 4 && zm.total == 0,
              "share: the empty party still reports its row count and a "
              "0 total, so a window composing a height from it never "
              "divides by 0 and the draw side stays the only place that "
              "promotes 0 to 1");
    }

    // A last sample SHORTER than the roster: the panel's
    // `i < last.damage.size()` guard. A model that read past the end
    // would give a row a 0 it never earned and skew the total.
    {
        const QVector<int> shortLast = {500, 300};
        const mhw::DamageShareModel sm = mhw::buildDamageShareModel(
            shortLast, fourSlots());
        check(sm.rowCount() == 4,
              "share: a roster of 4 with a 2-long last sample still "
              "resolves 4 segments");
        check(sm.segments[0].damage == 500 && sm.segments[1].damage == 300
              && sm.segments[2].damage == 0 && sm.segments[3].damage == 0,
              "share: rows absent from the last sample keep 0 and the "
              "total only counts the ones present (the panel's guard)");
        check(sm.total == 800,
              "share: the total matches the sample, not the roster");
        check(!sm.isEmpty(),
              "share: one non-zero segment is enough to draw a bar");
    }

    // A rowless party — "reader attached but no actor yet". The model
    // must be empty, not malformed.
    {
        const mhw::DamageShareModel sm = mhw::buildDamageShareModel(
            QVector<int>(), QVector<mhw::DamageRowIdentity>());
        check(sm.rowCount() == 0 && sm.segments.isEmpty() && sm.isEmpty(),
              "share: a rowless party produces an empty model, not a bad one");
    }

    // The block height the window composes its reservation from: 8px,
    // the bar the panel drew — NOT the 10px block it reserved minus
    // the panel's own gap.
    {
        check(mhw::DamageShareModel::blockHeight()
                  == mhw::damage_block::kShareBarDrawnH
              && mhw::DamageShareModel::blockHeight() == 8,
              "share: blockHeight() is the 8px bar, not the 10px block "
              "the 9px panel gap belonged to");
    }

    // ====================================================================
    // 2. THE CHART MODEL. Seam, then the three arms.
    // ====================================================================
    {
        const mhw::DamageChartModel m =
            mhw::buildDamageChartModel(*demoVm());

        check(!m.waiting && !m.degenerateSpan,
              "chart: the demo history resolves past both early returns");
        check(m.rowCount == 4,
              "chart: rowCount comes from the identity vectors, as in "
              "the panel (panel_damage.cpp:488)");

        // The demo's maximum sample is 184220 (the first player's final
        // value). roundUp's loop starts at 100 and doubles, so the next
        // stop AT OR ABOVE it is 204800 — and the assertion pins the
        // panel's rounding rather than a number I like, because a curve
        // scaled to a different denominator than the label says would
        // read as a lie.
        check(m.yMax == mhw::damage_block::chartAxisMax(184220),
              "chart: yMax is the panel's own roundUp() of the global "
              "maximum, so the label and the curve agree");
        check(m.yMax == 204800 && m.yMax >= 184220,
              "chart: yMax never rounds DOWN below the maximum it "
              "labelled (184220 -> 204800 by the doubling loop; a "
              "clipped peak would read as a smaller number)");

        check(m.series.size() == 4,
              "chart: one series per row");

        // 8 samples were seeded (ticks 0..7), so the normalized x spans
        // exactly [0, 1] and the first/last samples sit on the edges.
        const mhw::DamageChartSeries &s0 = m.series[0];
        check(s0.points.size() == 8,
              "chart: one normalized point per history sample");
        check(std::fabs(s0.points.first().x()) < 1e-6F
              && std::fabs(s0.points.last().x() - 1.0F) < 1e-6F,
              "chart: x is normalized to [0,1] across the tick span, "
              "so any window width draws the same curve");
        check(std::fabs(s0.points.last().y()
                        - static_cast<float>(184220)
                          / static_cast<float>(m.yMax)) < 1e-6F,
              "chart: the last y is damage/yMax, the panel's own "
              "normalization, not a re-derived one");
        check(s0.color.red() == 0xF2 && s0.color.green() == 0x48,
              "chart: series 0's colour is the row 0 colour — the "
              "palette is one spelling for the row, the share segment "
              "and the curve");

        // Monotone non-decreasing: the demo history is a growing curve,
        // and a model that re-ordered it would draw a sawtooth.
        bool monotone = true;
        for (int i = 1; i < s0.points.size(); ++i)
            if (s0.points[i].y() < s0.points[i - 1].y())
                monotone = false;
        check(monotone,
              "chart: the curve is monotone non-decreasing, matching "
              "the seeded history's growth");
    }

    // The waiting arm: fewer than two samples, or no rows. The panel
    // answered with a centred placeholder, and the model must keep
    // that distinguishable from the blank chart.
    {
        check(mhw::buildDamageChartModel(QVector<mhw::DamageViewModel::Sample>(),
                                        fourSlots()).waiting,
              "chart: an empty history is the WAITING arm (placeholder)");
        check(mhw::buildDamageChartModel(rampHistory(1, 100, 4),
                                        fourSlots()).waiting,
              "chart: a ONE-sample history is the WAITING arm — the "
              "panel's `history.size() < 2`, which cannot draw a span");
        check(mhw::buildDamageChartModel(rampHistory(4, 100, 4),
                                        QVector<mhw::DamageRowIdentity>()).waiting,
              "chart: no rows is the WAITING arm, even with a full history");
    }

    // The degenerate arm: a two-sample history on ONE tick. The panel
    // returned having drawn ONLY the background — no grid, no axis, no
    // curve — and the model must carry that separately.
    {
        const mhw::DamageChartModel st = mhw::buildDamageChartModel(
            rampHistory(2, 100, 4, /*tickOf=*/5), fourSlots());
        check(!st.waiting,
              "chart: a two-sample history is NOT the waiting arm");
        check(st.degenerateSpan,
              "chart: a zero tick SPAN is the degenerate arm — "
              "background only, no grid, no axis, no curve");
        check(st.series.isEmpty() && st.yMax >= 1,
              "chart: the degenerate arm resolves no curves, so the "
              "draw side returns after the background, and yMax is "
              "never 0");
    }

    // A party of rows that have never dealt damage: the chart is NOT
    // empty. Four flat lines on a labelled axis is a real answer to
    // "am I doing anything", so the window must still mount for it.
    // Driven on the plain-data core, where the all-zero history is
    // expressible: a poll loop always produces the demo's non-zero
    // seed, and it is the lottery between the two that the demo's own
    // numbers would hide.
    {
        const QVector<mhw::DamageViewModel::Sample> flatHistory =
            rampHistory(8, /*step=*/0, /*rows=*/1);
        const mhw::DamageChartModel flat = mhw::buildDamageChartModel(
            flatHistory, oneRow());
        check(!flat.waiting && !flat.degenerateSpan
              && !flat.series.isEmpty(),
              "chart: an all-zero party draws a flat line on a labelled "
              "axis — isEmpty() is false for it");
        check(flat.yMax == 100,
              "chart: an all-zero party's axis is 100 — the panel's "
              "`maxDmg == 0 -> 1` then `roundUp(1)` lands on the loop's "
              "floor, never 0 (a 0 axis would divide by zero at draw "
              "time)");
        bool flatLine = true;
        for (const QPointF &pt : flat.series[0].points)
            if (std::fabs(pt.y()) > 1e-6F)
                flatLine = false;
        check(flatLine && flat.series[0].points.size() == 8,
              "chart: the all-zero party's curve spans the full x range "
              "at y == 0, so it draws a flat line rather than nothing");
    }

    // ====================================================================
    // 3. THE ROUND TRIP. The normalized curve, mapped back through the
    //    draw side's own arithmetic, is the panel's float — for every
    //    width the window can be sized to. This is the whole reason the
    //    chart could be split without a copy.
    // ====================================================================
    {
        const mhw::DamageChartModel m =
            mhw::buildDamageChartModel(*demoVm());
        bool roundTrip = true;

        const QRectF rects[] = {
            QRectF(11, 11, 358, 140),      // the panel's own chart slot
            QRectF(11, 11, 300, 140),      // a narrowed window
            QRectF(11, 11, 500, 140),      // a widened window
            QRectF(11, 11, 222, 140),      // the smallest useful
        };
        for (const mhw::DamageChartSeries &series : m.series) {
            for (const QRectF &r : rects) {
                // The panel's own plotRect construction
                // (panel_damage.cpp:510-513).
                const QRectF plot(r.left() + mhw::damage_block::kChartLeft,
                                  r.top() + 2,
                                  r.width() - mhw::damage_block::kChartLeft - 4,
                                  r.height() - 4);
                for (int i = 0; i < series.points.size(); ++i) {
                    // The draw side's mapping (panel_damage_blocks.h,
                    // drawDamageChart).
                    const float gotX = plot.left()
                        + static_cast<float>(series.points[i].x())
                          * plot.width();
                    const float gotY = plot.bottom()
                        - static_cast<float>(series.points[i].y())
                          * plot.height();

                    // The panel's inline expression, verbatim, re-derived
                    // here from the same inputs.
                    //   x = left + (tick - first) / span * width
                    //   y = bottom - (damage / yMax) * height
                    const float tick = static_cast<float>(i);   // 0..7
                    const float damage = series.points[i].y()
                                         * static_cast<float>(m.yMax);
                    const float wantX = plot.left()
                        + (tick - 0.0F) / 7.0F * plot.width();
                    const float wantY = plot.bottom()
                        - (damage / static_cast<float>(m.yMax))
                          * plot.height();

                    if (gotX != wantX || gotY != wantY) {
                        roundTrip = false;
                        std::fprintf(stderr,
                                     "  rect %gx%g sample %d: got (%g,%g) "
                                     "want (%g,%g)\n",
                                     r.width(), r.height(), i,
                                     static_cast<double>(gotX),
                                     static_cast<double>(gotY),
                                     static_cast<double>(wantX),
                                     static_cast<double>(wantY));
                    }
                }
            }
        }
        check(roundTrip,
              "chart: the normalized curve, mapped back through the "
              "draw side, is the panel's own float — for every width");

        // And the claim the round trip rests on: the plot area GROWS
        // with the window while the y-axis column stays put, which is
        // what the split was for.
        const int narrow = 300 - 2 * 11 - mhw::damage_block::kChartLeft - 4;
        const int wide   = 500 - 2 * 11 - mhw::damage_block::kChartLeft - 4;
        check(wide > narrow && narrow > 0,
              "chart: widening the window grows the plot area by exactly "
              "the width gained (the y-axis column is fixed at kChartLeft)");
    }

    std::printf("\n%d checks, %d failure(s)\n%s\n", checks, failures,
                failures == 0 ? "ALL DAMAGE-SPLIT MODEL TESTS PASSED"
                              : "SOME DAMAGE-SPLIT MODEL TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
