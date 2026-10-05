#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// panel_damage_blocks.h — the DamagePanel's share bar and line chart as
// SHARED drawing code (v0.12 third wave: the monster / damage split).
//
// WHAT THIS FILE IS
// ----------------
// DamagePanel (src/ui/panel_damage.cpp) draws four blocks behind one
// section mask; two of them — the single-line share bar and the
// four-polyline accumulation chart — moved into their own layer-shell
// windows (DamageShareStripPanel / DamageChartStripPanel). Their paint
// code used to be private members of DamagePanel
// (drawShareBar / drawChart, panel_damage.cpp:437-594), unreachable
// from any other translation unit.
//
// It now lives here, as free functions over a pure data model, and
// BOTH the panel and the two windows call the same code. That is the
// whole point: a copy would have given the panel and the windows two
// spellings of one block, and the day one moved a pixel the
// single-mode baseline `all_damage.png` would have disagreed with what
// the split window draws — the drift this repo has already paid for
// (panel_player_split_bars.cpp carries the "copied on purpose" note
// for the blocks that could NOT be hoisted, because their source file
// was off-limits to that change).
//
// HOISTING vs COPYING, AND WHY IT IS A HOIST
// ------------------------------------------
// `drawShareBar` and `drawChart` were already free-standing functions
// with exactly one caller each and no access to any other member of
// DamagePanel except the ViewModel — so the body could be lifted
// without touching a single expression. Their one structural change is
// that the curve's per-sample coordinates are now precomputed in
// NORMALIZED form (see DamageChartSeries) and re-multiplied at draw
// time, which is what lets a 358px panel and a 300px window share one
// curve without either re-deriving the other's geometry.
//
// The normalization is NOT a re-derivation of the panel's arithmetic,
// and that claim is checkable line by line. The panel computed
//     x = plotRect.left() + (tick - first) / tickSpan * plotRect.width()
//     y = plotRect.bottom() - (damage / yMax)   * plotRect.height()
// and this file computes
//     tx = (tick - first) / tickSpan          (float, same operands)
//     ty = (damage / yMax)                    (float, same operands)
//     x  = plotRect.left() + tx * plotRect.width()
//     y  = plotRect.bottom() - ty * plotRect.height()
// Every intermediate is the SAME float with the SAME promotion path
// (float / int is float division in both spellings, and the multiply
// promotes the float to the qreal plot dimension in both), so the
// float x and y that reach path.moveTo()/lineTo() are bit-identical.
// What the model buys on top is that the whole curve is now a plain
// QVector<QPointF> a pure test can read without a QPainter.
//
// The proof that it stays pixel-identical is not an assertion in a
// comment — it is tests/damage_split_parity_tests.cpp, which renders
// the panel and the two windows at the same rect over the same
// ViewModel and demands byte equality of the crops. That is the same
// contract split-layout-consistency-tests enforces for the rhythm
// constants: reserve and draw are each other's inverse.
//
// LAYERING
// --------
// No widget header, no Q_OBJECT, no new translation unit: the content
// models are pure functions over mhw::DamageViewModel (QtCore only)
// and the draw half is inline. So the header is includable from the
// panel, from the sub-windows, and from a test that links nothing but
// Qt6::Core — which is why the zero-reference-gate census needs no new
// entry for it (same reason panel_sections.h carries no census row).

#include "ui/panel_player_split_layout.h"
#include "ui/viewmodel/damage_view_model.h"

#include "core/string_table.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cstdint>

// ─── the one i18n spelling, and why it is not called mh::tr ─────────────
//
// Every .cpp in the tree spells `namespace mh { inline QString tr(...) }`
// at its own top, which is ODR-safe precisely BECAUSE each lives in its
// own translation unit. Spelling that SAME name in a header breaks the
// assumption: a .cpp that already has its own copy and includes this
// header sees two, which is a redefinition error, not a harmless
// duplicate. control_panel.cpp is exactly that file — it has its own mh::
// tr and it includes this one.
//
// A shared header therefore needs a name no .cpp already claims. The
// qualified call outlives a second `mh` namespace: it is one function in
// one place, and the window .cpp files below call it rather than each
// reopening the question.

namespace damage_blocks {
// The shared lookup for the drawing code and its two windows. Named for
// the header, not `mh`, for the ODR reason spelled above.
[[nodiscard]] inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace damage_blocks

namespace mhw {
namespace damage_block {

// ---- Layout tokens -----------------------------------------------------
//
// Mirrors of panel_damage.cpp's anonymous-namespace constants
// (:30-52). kPanelW (380) is deliberately NOT carried over: it is the
// SOURCE panel's width and no sub-window reserves it — the bars window
// in the player split makes the same choice and documents why there.

// panel_damage.cpp:46 spells this as a literal 11, which is also what
// every split window answers contentMargin() from (v0.12's shared
// rhythm), so the two cannot drift by one margin again.
constexpr int kMargin = mhw::split_layout::kSplitMargin;
// The SOURCE panel's own width (panel_damage.cpp:45). Not reserved as a
// content size by anything in this header; it is what the share window
// starts its own width from, so "what the bar used to be" is one number
// in one place instead of a literal in the window's source.
constexpr int kShareBarPanelW = 380;
// The CHART's slot width in DamagePanel (panel_damage.cpp:434), which is
// the panel width minus its two margins. The start position for the
// chart window's own width, for the same reason.
constexpr int kChartPanelW    = 380 - 2 * mhw::split_layout::kSplitMargin;
constexpr int kChartH = 140;    // HTML: .chart height:140px
constexpr int kChartLeft = 36;  // widened from 28 so 3-char "184k"
                                // labels at 8px don't clip
constexpr int kShareBarH = 10;  // clean single-line bar (was 16)

// The share bar's DRAWN height. The panel draws `kShareBarH - 2` (an
// 8px bar inside the 10px block); the window reserves the split margin
// on both sides of that 8, exactly as the panel reserved it.
constexpr int kShareBarDrawnH = kShareBarH - 2;

// The party roster's cap. panel_damage.cpp:52.
constexpr int kMaxPlayers = 4;

// The player colour for a row: purple for self, otherwise the slot's
// normal hue. Identical expression to panel_damage.cpp:67-71, so the
// row's contribution bar, the share segment and the chart polyline all
// resolve the same colour for the same player.
[[nodiscard]] inline QColor colorForRow(int slot, bool isSelf)
{
    if (isSelf)
        return QColor(0xA7, 0x4F, 0xFF);   // --dc self
    // HunterPie default damage meter colors (PlayerConfigHelper.cs):
    // slot 0 = #F24891 (pink), slot 1 = #50C5B7 (teal),
    // slot 2 = #49CFF5 (sky blue), slot 3 = #FF8040 (orange).
    static const QColor kPlayerColors[] = {
        QColor(0xF2, 0x48, 0x91),
        QColor(0x50, 0xC5, 0xB7),
        QColor(0x49, 0xCF, 0xF5),
        QColor(0xFF, 0x80, 0x40),
    };
    return kPlayerColors[(slot % kMaxPlayers + kMaxPlayers) % kMaxPlayers];
}

// The chart's y-axis tick rounding, panel_damage.cpp:524-529. Kept
// here, and exported, because the axis labels and the curve
// normalization share one denominator: a window that labelled its axis
// with a different rounding than the curves were scaled to would read
// as a lie.
[[nodiscard]] inline int chartAxisMax(int maxDamage) noexcept
{
    int scale = 100;
    while (scale < maxDamage)
        scale *= 2;
    return std::max(scale, maxDamage);
}

} // namespace damage_block

// ---- Share bar ---------------------------------------------------------

// One abutted segment of the share bar. The damage is kept RAW (not
// pre-divided) so the draw side keeps the panel's own pixel rounding:
// segment width = barWidth * damage / total, floored to int, minimum
// 1. Pre-dividing here would round the ratio twice.
struct DamageShareSegment {
    int    damage{0};
    QColor color;
};

// The share bar's content model.
struct DamageShareModel {
    QVector<DamageShareSegment> segments;
    qint64 total{0};

    // "Nothing to draw a bar for". Deliberately NOT rowCount() > 0: a
    // party of four who have all hit 0 damage draws a bare grey track,
    // and a bare track is not a reason to mount a window — the same
    // "no data, no window" rule every split window answers
    // hasVisibleContent() from.
    [[nodiscard]] bool isEmpty() const noexcept
    {
        for (const DamageShareSegment &s : segments)
            if (s.damage > 0)
                return false;
        return true;
    }

    // The party size the bar speaks for, which is what the window
    // composes its height from even when every segment is empty.
    [[nodiscard]] int  rowCount() const noexcept { return segments.size(); }
    // The panel's own block height for this bar (the 9px gap above it
    // belongs to the panel's layout, not to the bar).
    [[nodiscard]] static constexpr int blockHeight() noexcept
    {
        return damage_block::kShareBarDrawnH;
    }
};

// A row's identity, as the three consumers of a row colour resolve it:
// the party slot (colour by position) and the self flag (colour
// override). This is the pair the panel read off its ViewModel at draw
// time (panel_damage.cpp:302), and it is the complete input the share
// bar and the chart need — so they can be driven by plain data, which
// is what makes both testable without a ViewModel.
struct DamageRowIdentity {
    int  slot{0};
    bool isSelf{false};
};

// THE CORE. The share bar's per-row damage split and its total, from
// plain data. The body is panel_damage.cpp:437-452.
//
// Overloaded rather than wrapped: the caller hands the last history
// sample's damage directly, so the model has no opinion about where a
// sample came from and the test can drive every state the panel can
// reach (including "last sample shorter than the roster", which the
// panel tolerated with its `i < last.damage.size()` guard) without
// running a poll loop to reach it.
[[nodiscard]] inline DamageShareModel buildDamageShareModel(
    const QVector<int> &lastSampleDamage,
    const QVector<DamageRowIdentity> &ident)
{
    DamageShareModel model;
    const int n = ident.size();
    if (n == 0)
        return model;

    model.segments.resize(n);
    for (int i = 0; i < n && i < lastSampleDamage.size(); ++i) {
        model.segments[i].damage = lastSampleDamage[i];
        model.segments[i].color  = damage_block::colorForRow(
            ident[i].slot, ident[i].isSelf);
        model.total += lastSampleDamage[i];
    }
    return model;
}

// The production entry point: the ViewModel's own read-only views, in
// the same order the panel used to read them. Delegates to the core
// above rather than re-implementing it, so the panel, the window and
// the test all get one arithmetic.
[[nodiscard]] inline DamageShareModel buildDamageShareModel(
    const mhw::DamageViewModel &vm)
{
    const auto &history = vm.history();
    const QVector<int> lastSample =
        history.isEmpty() ? QVector<int>() : history.last().damage;

    QVector<DamageRowIdentity> ident;
    ident.resize(vm.rowCount());
    for (int i = 0; i < vm.rowCount(); ++i)
        ident[i] = {vm.partySlots().value(i, i),
                    vm.locals().value(i, false)};

    return mhw::buildDamageShareModel(lastSample, ident);
}

// panel_damage.cpp:437-476, moved.
inline void drawDamageShareBar(QPainter &p, const QRectF &barRect,
                               const DamageShareModel &model)
{
    const int n = model.segments.size();
    if (n == 0)
        return;

    qint64 total = model.total;
    if (total == 0)
        total = 1;

    // Invisible track — dark grey, no rounded corners.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(42, 45, 47));          // --line, same as panel chrome
    p.drawRect(barRect);

    // Four colored segments, flat abutted edges, no gaps, no text.
    int xCursor = static_cast<int>(barRect.x());
    const int trackRight = static_cast<int>(barRect.right());
    for (int i = 0; i < n; ++i) {
        if (model.segments[i].damage <= 0)
            continue;
        const int segW = std::max(
            1, static_cast<int>(barRect.width()
                                * static_cast<double>(model.segments[i].damage)
                                / static_cast<double>(total)));
        if (xCursor >= trackRight)
            break;
        const int segWidth = std::min(segW, trackRight - xCursor);
        const QRectF seg(xCursor, barRect.y(), segWidth, barRect.height());
        p.setBrush(model.segments[i].color);
        p.drawRect(seg);
        xCursor += segWidth;
    }
}

// ---- Line chart --------------------------------------------------------

// One player's curve, NORMALIZED: x is the sample's fraction of the
// tick span, y is the sample's fraction of the axis maximum. Draw-time
// maps them into whatever rect it is given, which is what lets the
// panel (358px wide) and the window (its own, narrower) share one
// curve without either re-deriving the other's geometry.
struct DamageChartSeries {
    QColor color;
    QVector<QPointF> points;
};

// The chart's content model.
//
// The two flags are the panel's two early returns, preserved exactly:
// `waiting` is `history.size() < 2 || rowCount == 0` (the panel answers
// with a centred placeholder), `degenerateSpan` is `lastTick-
// firstTick <= 0` (the panel returns having drawn ONLY the background,
// no grid, no axis, no curve). Collapsing them into one "empty"
// boolean would have made the placeholder and the blank chart the
// same state, which they are not — one is "no data yet" and the other
// is "one sample recorded".
struct DamageChartModel {
    QVector<DamageChartSeries> series;
    int rowCount{0};
    // The rounded-up axis maximum, which is also the denominator the
    // normalized points were built on. 1 while there is nothing to
    // scale, so neither the axis labels nor the draw side ever divides
    // by zero.
    int  yMax{1};
    bool waiting{true};
    bool degenerateSpan{false};

    // "No curve to draw at all" — only the waiting placeholder state.
    // A chart whose four polylines all sit on y == 0 is still four
    // flat lines on a scale the axis labelled, which is a real answer
    // to "am I doing anything", so it is NOT empty here.
    [[nodiscard]] bool isEmpty() const noexcept
    {
        return waiting && series.isEmpty();
    }
};

// The pure transformation: everything the painter needs that is a
// decision rather than a stroke. The data half of
// panel_damage.cpp:478-594, in the same order.
//
// Overloaded on plain data as well as on the ViewModel, for the reason
// the share builder is: the degenerate-span arm (a history whose
// samples all carry the same tick) is a real state the panel handled
// explicitly and no poll loop can be driven into it, so the test must
// be able to hand that history in directly.
[[nodiscard]] inline DamageChartModel buildDamageChartModel(
    const QVector<mhw::DamageViewModel::Sample> &history,
    const QVector<DamageRowIdentity> &ident)
{
    DamageChartModel model;
    const int n = ident.size();
    model.rowCount = n;

    if (history.size() < 2 || n == 0)
        return model;                       // waiting stays true
    model.waiting = false;

    // Global maximum across history, then rounded up to a nice number
    // for the top label so it reads as a real scale (panel_damage
    // .cpp:496-501 and :524-529, in the same order).
    int maxDmg = 0;
    for (const mhw::DamageViewModel::Sample &s : history)
        for (int i = 0; i < s.damage.size() && i < n; ++i)
            maxDmg = std::max(maxDmg, s.damage[i]);
    if (maxDmg == 0)
        maxDmg = 1;
    const int yMax = damage_block::chartAxisMax(maxDmg);
    model.yMax = yMax;

    const int firstTick = history.first().tick;
    const int lastTick  = history.last().tick;
    const int tickSpan  = lastTick - firstTick;
    if (tickSpan <= 0) {
        model.degenerateSpan = true;        // background only, then return
        return model;
    }

    model.series.resize(n);
    for (int pi = 0; pi < n; ++pi) {
        DamageChartSeries &series = model.series[pi];
        series.color = damage_block::colorForRow(
            ident[pi].slot, ident[pi].isSelf);
        series.points.reserve(history.size());
        for (const mhw::DamageViewModel::Sample &s : history) {
            const float tx = static_cast<float>(s.tick - firstTick) / tickSpan;
            const float ty = static_cast<float>(s.damage.value(pi, 0))
                             / static_cast<float>(yMax);
            series.points.append(QPointF(tx, ty));
        }
    }
    return model;
}

// The production entry point: the ViewModel's read-only views, delegated
// to the core above so one arithmetic serves the panel, the window and
// the test.
[[nodiscard]] inline DamageChartModel buildDamageChartModel(
    const mhw::DamageViewModel &vm)
{
    QVector<DamageRowIdentity> ident;
    ident.resize(vm.rowCount());
    for (int i = 0; i < vm.rowCount(); ++i)
        ident[i] = {vm.partySlots().value(i, i),
                    vm.locals().value(i, false)};

    return mhw::buildDamageChartModel(vm.history(), ident);
}

// panel_damage.cpp:478-594, moved: the translucent background, the two
// faint grid lines, the three right-anchored y-axis labels and one
// 1.6px polyline per player, in the order the panel drew them.
inline void drawDamageChart(QPainter &p, const QRectF &chartRect,
                            const DamageChartModel &model)
{
    // HTML: .chart {background:rgba(30,30,30,.6); border:1px solid --line;}
    // 2026-07-27: dropped chart alpha 153 → 100 → 65 → 35 → 15 (~6%) to
    // match the translucent panel body and let the wallpaper show.
    p.setPen(QPen(QColor(42, 45, 47), 1));
    p.setBrush(QColor(30, 30, 30, 15));
    p.drawRoundedRect(chartRect, 4, 4);

    if (model.waiting) {
        p.setPen(QColor(150, 150, 150));
        p.setFont(QFont(QStringLiteral("Chakra Petch"), 8));
        p.drawText(chartRect, Qt::AlignCenter,
                   damage_blocks::tr(QStringLiteral("ui.damage_chart_waiting")));
        return;
    }
    if (model.degenerateSpan)
        return;

    const int yMax = model.yMax;
    const int yMid = yMax / 2;

    // Y-axis area: leave room for the 24px label column on the left
    // (HTML: .chart .ylabel 24px wide, .chart svg starts at left:28).
    const QRectF plotRect(chartRect.left() + damage_block::kChartLeft,
                           chartRect.top() + 2,
                           chartRect.width() - damage_block::kChartLeft - 4,
                           chartRect.height() - 4);

    // y-grid (HTML: .chart .ygrid i.t/i.m, two faint horizontal lines).
    p.setPen(QPen(QColor(255, 255, 255, 10), 1));
    p.drawLine(plotRect.left(),  plotRect.top(),
               plotRect.right(), plotRect.top());
    p.drawLine(plotRect.left(),  plotRect.top() + plotRect.height() / 2,
               plotRect.right(), plotRect.top() + plotRect.height() / 2);

    // Compact formatter for y-axis labels (HTML .ylabel 8px, --t4 #505355):
    //   < 1,000       → "123"  (integer)
    //   1k–9,999      → "1k"   (integer k, always no decimal — looks
    //                          tighter than "1.2k" and matches HTML v8
    //                          visual rhythm at typical quest lengths)
    //   10k–999,999   → "184k" (integer k)
    auto fmtCompact = [](int v) -> QString {
        if (v < 1000)
            return QString::number(v);
        // 1000..999 → 1k..999k (rounded to nearest 1k).
        const int k = (v + 500) / 1000;
        return QString::number(k) + QLatin1Char('k');
    };

    // Right-anchored inside yLabelRect so wider "184k" never clips.
    const QRectF yLabelRect(chartRect.left() + 2, chartRect.top(),
                            damage_block::kChartLeft - 6, chartRect.height());
    QFont yFont(QStringLiteral("Chakra Petch"), 8);
    yFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(yFont);
    const QFontMetrics yFm(yFont);
    const int labelH = yFm.height();
    auto drawRightAligned = [&](const QString &s, qreal topY) {
        const int textW = yFm.horizontalAdvance(s);
        // Higher-contrast direct label: the chart stays visually clean and
        // transparent, while brighter text survives non-black scenes.
        p.setPen(QColor(220, 224, 226, 245));
        p.drawText(QRectF(yLabelRect.right() - textW, topY,
                          textW, labelH),
                   Qt::AlignLeft | Qt::AlignVCenter, s);
    };
    const int top    = static_cast<int>(chartRect.top()) + 2;
    const int mid    = static_cast<int>(chartRect.top())
                      + static_cast<int>(chartRect.height()) / 2 - labelH / 2;
    const int bottom = static_cast<int>(chartRect.bottom()) - labelH - 2;
    drawRightAligned(fmtCompact(yMax), top);
    drawRightAligned(fmtCompact(yMid), mid);
    drawRightAligned(QStringLiteral("0"), bottom);

    // Polyline per player (HTML: 4 polylines, no fill, stroke-width 1.6).
    // Colour must match the row's colour exactly: the row reads
    // slots_/locals_ at draw time; the model resolved the same pair, so
    // the line and the row header are visually linked.
    for (const DamageChartSeries &series : model.series) {
        p.setPen(QPen(series.color, 1.6));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        bool first = true;
        for (const QPointF &pt : series.points) {
            const float x = plotRect.left()
                + static_cast<float>(pt.x()) * plotRect.width();
            const float y = plotRect.bottom()
                - static_cast<float>(pt.y()) * plotRect.height();
            if (first) {
                path.moveTo(x, y);
                first = false;
            } else {
                path.lineTo(x, y);
            }
        }
        p.drawPath(path);
    }

    // No elapsed-time label here: the previous '+1s' demo marker was
    // neither a real x-axis nor useful gameplay information, and it
    // collided with the top y-axis value on transparent backgrounds.
}

} // namespace mhw
