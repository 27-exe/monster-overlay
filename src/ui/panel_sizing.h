// SPDX-License-Identifier: Apache-2.0
//
// panel_sizing.h — the shared size / wrap arithmetic for every
// layer-shell sub-window (v0.12 and later).
//
// WHAT THIS SOLVES
// ----------------
// Every panel that was carved out of a fat panel (status strip, bars,
// wirebug, mantles, …) answered the same three questions with its own
// private arithmetic, and the two questions that mattered most — "how
// wide is the window" and "how wide is one capsule" — were answered
// from DIFFERENT INPUTS by DIFFERENT FORMULAS:
//
//   * panel_player_split.cpp:471  clamped `widest + 2*kMargin` into
//     [2*kMargin + kMinPillW, 420], where `widest` is the width of the
//     SINGLE WIDEST CAPSULE, and
//   * panel_player_split.cpp:159  (drawPillGrid) divided that same
//     width by kStatusStripPillsPerRow = 3 to get each capsule's slot.
//
// The result, measured with a throwaway reproduction of the shipped
// arithmetic (10 pills, each measuring 3+6+name+12+timer):
//
//     World demo, 10 pills, widest 79px   -> W = 91, slot = 23px
//     Rise  demo, 10 pills, widest 113px  -> W = 125, slot = 34px
//
// Each capsule is handed 23px (World) / 34px (Rise) out of the 79 /
// 113px its own name + timer column arithmetic measured. The timer text
// in drawPill() is drawn into `box.width() - 8` = 11px / 22px — less
// than "12s" occupies at 9pt — so it is elided or clipped, and the
// name column with it. Meanwhile the trailing incomplete row gets its
// capsule the full natural width, so ONE pill per strip is readable and
// the rest are not. That is the user's "悬浮窗表现很差，缩放过大字体过大
// 导致几乎完全不可用", and it is not a scaling defect at all: it is
// `setContentSize(w)` and `slotW = w/n` disagreeing about what `w` holds.
//
// Each of those is a second arithmetic for one wrap, which is the exact
// defect class the repo already paid for once (v0.10.3, "drawn at one
// height and reserved at another"). This header makes ONE function the
// single arithmetiser, and makes the draw side its exact inverse (see
// gridSlotWidth), so a caller cannot reserve a width and then draw a
// slot that does not fit inside it.
//
// WHY IT IS NOT IN panel.h
// ------------------------
// Two reasons, both load-bearing:
//
//   1. Layer LIBRARIES may not include `ui/`. panel.h is in `ui/`, so
//      putting this in it would put a ui/ include inside monster-core
//      (and the purity gate greps for exactly that).
//   2. Every test target that wants to check size arithmetic would then
//      have to link Qt6::Widgets and pull QMainWindow in. This header
//      includes NO Qt header at all: every input the widths need is
//      already an `int` by the time the widget layer hands it over (the
//      panel measures `QFontMetrics::horizontalAdvance` once and passes
//      the number in). That is why there is no QString here and why none
//      is needed — the only thing Qt could offer this file is a type it
//      would immediately have to convert away.
//
// CONTRACT WITH THE CALLER (read this before wiring a panel)
// ---------------------------------------------------------
// The header computes INTEGERS in LOGICAL pixels. It never reads a
// widget, and it must never be fed `Panel::contentSize()`:
//
//     Panel::contentSize() returns -1x-1 until the first paint
//     (measured: size()=320x120 / 378x800, contentSize()=-1x-1 before
//     a render; only after a render does it become the real 378x201).
//
// A sizing function that reads the size the painting is supposed to
// choose has a circular dependency and answers -1 on the first paint.
// Everything here takes the raw numbers (count, measured item width,
// margins, gaps) and derives the size; `Panel::setContentSize()` is the
// LAST step, never an input.
//
// WHAT THE USER CAN AND CANNOT TUNE
// ---------------------------------
// See docs/SIZING.md. Short version: Wayland layer-shell has no notion
// of an arbitrary rectangle — a surface's position is (anchor corner +
// margins) only, so the user cannot drag a resize handle and the client
// is never asked for a size the compositor would arbitrate. The only
// shape lever that exists is HOW THE CONTENT IS DISTRIBUTED, and that
// is what this header computes. `columns` (equivalently: a width budget
// from which the columns are derived) is the dial; the aspect ratio is
// a CONSEQUENCE of it, never an independent parameter.

#pragma once

// Only <cmath> is needed: std::lround for scaleRound. Every other
// operation here is a comparison, a division or a ternary, which is
// exactly the point — a size function this central should not grow a
// dependency the moment someone wants a std::min in it.
#include <cmath>

namespace mhw::panel_sizing {

// ─── scale ───────────────────────────────────────────────────────────
//
// Mirrors of panel.cpp:25-27 (kMinScale 0.5, kMaxScale 3.0,
// kScaleStep 0.1). Mirrored on purpose, not shared: including panel.h
// from here would drag QMainWindow + QWidget into every consumer, and
// this header's whole value is being includable from a QtCore-only
// (in fact Qt-free) target. If the wheel range is ever retuned, both
// spellings move together — the test target pins the two sets equal.
inline constexpr double kScaleMin = 0.5;
inline constexpr double kScaleMax = 3.0;
inline constexpr double kScaleStep = 0.1;

// ─── scalar guards ───────────────────────────────────────────────────

// Clamp into [lo, hi]. `lo > hi` is the caller's bug, not this
// function's, so it is NOT sanitised here — callers that receive a
// user-tunable range (console spin boxes, a corrupted INI) must go
// through clampScale / clampWidth, which sanitise the range before
// clamping into it.
[[nodiscard]] constexpr int clampInt(int v, int lo, int hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// The panel's zoom range, sanitised. NaN is handled first, by ordering
// rather than by an explicit isNaN: every comparison against NaN is
// false, so `s > kScaleMin` already evaluates false for it and the first
// arm answers kScaleMin. A reversed range would be std::clamp UB, which
// is why this exists rather than a bare std::clamp in the caller.
[[nodiscard]] inline double clampScale(double s) noexcept
{
    if (!(s > kScaleMin))   // false for s <= kScaleMin AND for NaN
        return kScaleMin;
    if (s > kScaleMax)
        return kScaleMax;
    return s;
}

// Clamp an integer width/height into a caller-supplied, sanitised range.
// hi <= 0 means "no upper bound".
[[nodiscard]] constexpr int clampWidth(int v, int lo, int hi) noexcept
{
    if (lo < 0)
        lo = 0;
    if (hi > 0 && lo > hi)
        lo = hi;
    if (hi > 0 && v > hi)
        return hi;
    return v < lo ? lo : v;
}

// Negative widths/heights are never a size. A caller that lets a
// measurement go negative (a font metric for an empty string is 0, not
// negative — but an int coming back from a subtraction of two measured
// advances can be) must not push it into setFixedSize().
[[nodiscard]] constexpr int nonNegative(int v) noexcept
{
    return v < 0 ? 0 : v;
}

// ─── the shape dial ──────────────────────────────────────────────────
//
// The one number the user owns. Everything else here is derived from it.

// Largest k with 1 <= k <= count such that
//     k items of `itemMinWidth` plus (k-1) gaps of `gap` still fit in
//     `available`.
//
// Solving k*(minW + gap) - gap <= available for k gives the closed form
// below; the `+ gap` in the numerator IS that `- gap` term moved across
// the inequality. Dropping it (mutation A, pinned by the test suite) is
// the naive `available / (minW + gap)`, which under-reports by one
// column whenever the gaps are what the row is short by — see the 175
// vs 170 case in panel_sizing_tests.cpp §1.
//
// Boundaries, all pinned by tests:
//   count <= 0            -> 0   (an empty grid has no row)
//   minW + gap <= 0       -> count (items cost nothing, everything fits)
//   nothing fits at all   -> 1   (see "AT LEAST ONE" below)
//   everything fits       -> count
//
// The sweep in panel_sizing_tests.cpp §1 checks all 12285 combinations
// against a brute-force reference (a different implementation, so a
// wrong closed form cannot agree with itself).
[[nodiscard]] constexpr int gridItemsPerRow(int available, int itemMinWidth,
                                            int gap, int count) noexcept
{
    if (count <= 0)
        return 0;
    const int w = nonNegative(itemMinWidth);
    const int g = nonNegative(gap);
    const int av = nonNegative(available);

    if (w + g <= 0)
        return count;                  // free items: the budget is not binding

    int fit = (av + g) / (w + g);      // floor of the real solution
    if (fit < 1)
        fit = 1;                       // AT LEAST ONE: see gridOverflows()
    if (fit > count)
        fit = count;
    return fit;
}

// True when even ONE item at its minimum width does not fit in
// `available`. THE ARITHMETIC STILL ANSWERS 1 (a grid with zero columns
// is not a layout, it is a divide-by-zero), which is why this exists
// separately: the caller needs it to know it must elide, or collapse the
// window entirely.
//
// NOTE the one-px-short case: available 77 against an item 78 wide
// answers 1 column AND reports overflow, by design. A capsule clipped
// by one pixel is far better than a strip that silently stops drawing
// the row — which is exactly what the fixed-width strip (kStripMaxW
// 420) did to a fully buffed hunter.
[[nodiscard]] constexpr bool gridOverflows(int available, int itemMinWidth,
                                           int gap = 0) noexcept
{
    const int w = nonNegative(itemMinWidth);
    const int g = nonNegative(gap);
    const int av = nonNegative(available);
    if (w + g <= 0)
        return false;
    return w > av || g > av;           // one item, or even its own gap, spills
}

// Rows needed to lay `count` items out `perRow` wide. 0 and negatives
// answer 0 so callers never re-guard the divide. perRow <= 0 with a
// non-empty count answers the count (one item per row) rather than
// dividing by zero.
[[nodiscard]] constexpr int gridRowsFor(int count, int perRow) noexcept
{
    if (count <= 0)
        return 0;
    const int k = perRow > 0 ? perRow : 1;
    return (count + k - 1) / k;
}

// ─── the wrap arithmetic ─────────────────────────────────────────────
//
// THE TWO FUNCTIONS THAT MUST AGREE.
//
// gridContentWidth() is the reservation side. gridSlotWidth() is the
// draw side. One is the exact inverse of the other, which is what makes
// "content adapts its length and width to fill" true with no dead air:
//
//     gridSlotWidth(gridContentWidth(...), perRow, gap) == itemWidth
//
// exactly, with no truncation, because
//     (perRow*W + gap*(perRow-1) - gap*(perRow-1)) / perRow == W
// when perRow >= 1. A caller that reserves with one and draws with the
// other therefore hands every item precisely the width it was measured
// at — no elided glyph, no stretched capsule.
//
// THE INCOMPLETE ROW is the case this pair exists for, and it is the
// one the shipped strip gets wrong: 10 pills at 3-per-row laid out in a
// 91px window get slotW = 23px each because `w` was derived from ONE
// pill's natural 79px. A caller that computes `w` FROM the grid (this
// header's gridTotalSize) and then draws with gridSlotWidth gets
// perRow * 79 + (perRow-1) * 5 + 2*margin = 249 for a full row, and
// 79px slots in the trailing row — the width of the window is decided
// by the ROWS THAT ARE FULL, which is the only layout rule that keeps
// the incomplete row from dictating the width of the full ones.
//
// That identity is swept over 600 combinations in
// panel_sizing_tests.cpp §5 rather than spot-checked, because it is the
// property the "one gateset, three consumers" rule is about.

// Width of the wrapped grid's CELLS ONLY: `perRow` items of `itemWidth`
// plus (perRow-1) gaps. 0 for an empty grid. Does NOT include margins —
// use gridTotalSize() for the number that goes to setContentSize().
[[nodiscard]] constexpr int gridContentWidth(int count, int perRow,
                                             int itemWidth, int gap) noexcept
{
    if (count <= 0)
        return 0;
    const int k = perRow > 0 ? perRow : 1;
    // Never wider than the items actually need: three items laid out
    // four-per-row must not reserve a fourth column.
    const int cols = k > count ? count : k;
    return nonNegative(cols * nonNegative(itemWidth)
                       + (cols - 1) * nonNegative(gap));
}

// Height of `rows` rows of `rowHeight` plus (rows-1) `rowGap`s.
[[nodiscard]] constexpr int gridContentHeight(int rows, int rowHeight,
                                              int rowGap) noexcept
{
    if (rows <= 0)
        return 0;
    return nonNegative(rows * nonNegative(rowHeight)
                       + (rows - 1) * nonNegative(rowGap));
}

// Width ONE item should be drawn at, to exactly fill `available` with
// `perRow` columns. This is the inverse of gridContentWidth().
//
// Answers 0 (not a negative, and never silently 1) when `available` is
// too small to hold even the gaps: such a budget has no room for a
// capsule, and the caller's gridOverflows() handshake exists to catch
// that case before this is ever called for drawing.
[[nodiscard]] constexpr int gridSlotWidth(int available, int perRow,
                                          int gap) noexcept
{
    if (perRow <= 0)
        return 0;
    const int av = nonNegative(available);
    const int w = (av - (perRow - 1) * nonNegative(gap)) / perRow;
    return w < 1 ? 0 : w;
}

// ─── size, in one call ───────────────────────────────────────────────

struct GridSize {
    int width{0};
    int height{0};
};

// The number that goes to Panel::setContentSize(): content + margin on
// both sides. The caller must NOT call setContentSize(0, 0) — an empty
// grid answers the collapsed size here (gridEmptySize) or the panel's
// own empty branch, never a zero surface (see SIZING.md §5).
[[nodiscard]] constexpr GridSize gridTotalSize(int count, int perRow,
                                               int itemWidth, int itemHeight,
                                               int gap, int rowGap,
                                               int margin) noexcept
{
    const int m = nonNegative(margin);
    if (count <= 0)
        return GridSize{0, 0};
    const int cols = perRow > 0 ? perRow : 1;
    const int colsCapped = cols > count ? count : cols;
    const int w = nonNegative(colsCapped * nonNegative(itemWidth)
                              + (colsCapped - 1) * nonNegative(gap));
    const int rows = gridRowsFor(count, colsCapped);
    const int h = nonNegative(rows * nonNegative(itemHeight)
                              + (rows - 1) * nonNegative(rowGap));
    return GridSize{w + 2 * m, h + 2 * m};
}

// The collapsed size for a grid with nothing in it. `2 * kMargin` is
// what every shipped panel already answers (panel_player_split.cpp:470,
// :502, wirebug:306, bars:325) — one spelling, here, so the empty
// branch cannot drift from the populated one by a margin.
[[nodiscard]] constexpr GridSize gridEmptySize(int margin) noexcept
{
    const int m = nonNegative(margin);
    return GridSize{2 * m, 2 * m};
}

// ─── zoom ────────────────────────────────────────────────────────────
//
// Why this is here and not left to panel.cpp: panel.cpp uses
// std::lround and the 12 committed pixel baselines depend on it, so the
// header must not be free to spell the rounding differently.
//
// WHY lround AND NOT TRUNCATION
// -----------------------------
// paintEvent() applies `p.scale(scale_, scale_)` and then draws the
// logical content, so the ink occupies `scale_ * logical` DEVICE pixels.
// The surface is setFixedSize(scale_ * logical) — the same product, or
// the last column is clipped and the last row disappears. The three
// candidate roundings, for logical = 125 and scale_ = 0.5:
//
//     product 62.5
//     truncation -> 62   STRICTLY SMALLER than the ink: half a pixel of
//                        the right edge is gone. Worse at scale_ = 0.7
//                        (87.5 -> 87, 1px short) and it is always
//                        biased down, so every scale_ < 1 loses.
//     lround     -> 63   >= the ink: nothing is clipped.
//     ceil       -> 63   the same, but at scale_ = 3.0 with logical = 7
//                        (product 21.0) they agree too — ceil only
//                        differs when the product has a fraction < 0.5,
//                        and there it adds a dead pixel no ink covers.
//
// So lround is never worse than truncation (identical everywhere except
// fraction >= 0.5, where truncation clips a whole pixel), and differs
// from ceil by at most one pixel of DEAD TRANSPARENT EDGE rather than
// one pixel of missing ink. That is the correct trade for a translucent
// overlay: a dead pixel on a chromeless strip is invisible, a clipped
// pixel is a missing capsule edge. This is also the direction the
// user-visible defect goes: "scale down and the last pill loses its
// right border".
//
// scale_ < 1 direction, stated plainly: the rounding must go UP, never
// down, because the surface is a floor for the ink, not a target for it.
[[nodiscard]] inline int scaleRound(int logical, double scale) noexcept
{
    if (logical <= 0)
        return 0;
    const int d = static_cast<int>(std::lround(logical * scale));
    return d < 0 ? 0 : d;
}

// Device-pixel size of a logical content box at `scale`. Never negative,
// and (0,0) in, (0,0) out — a caller that feeds an empty grid must not
// get a 1x1 surface out of it.
[[nodiscard]] inline GridSize effectiveSize(int logicalWidth, int logicalHeight,
                                            double scale) noexcept
{
    return GridSize{scaleRound(logicalWidth, scale),
                    scaleRound(logicalHeight, scale)};
}

// ─── diagnostics ─────────────────────────────────────────────────────

// width / height as a ratio, 0 when either side is not a real size.
// This is a REPORTING number: it is what the console can show next to
// the columns dial so the user sees the consequence of the turn. It is
// never an input — see docs/SIZING.md §3 on why "set the ratio to 16:9"
// is not a thing that can be honoured here.
[[nodiscard]] inline double aspectRatio(int width, int height) noexcept
{
    if (width <= 0 || height <= 0)
        return 0.0;
    return static_cast<double>(width) / static_cast<double>(height);
}

} // namespace mhw::panel_sizing
