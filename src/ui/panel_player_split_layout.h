#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// split_layout.h — the v0.12 split windows' ONE rhythm vocabulary.
//
// WHY THIS FILE EXISTS
// --------------------
// Seven windows carved out of PlayerPanel, each writing its own margin,
// its own inter-row gap and its own font sizes, is exactly how a split
// stops reading as one system. The user's verdict on the result was
// 「拆分后的面板的布局可以考虑重构优化一下」, and the measurable part of
// it is in the inventory that motivated this file:
//
//   * margins were spelled three different ways — `kMargin` (status,
//     bars, info), `kWirebugMargin` (wirebug), `kMantlesMargin`
//     (mantles) — all with the value 11, all with a different name.
//   * vertical rhythm was worse: the wirebug window's wrapped-row gap
//     is 4, the mantles window's is 8, and the info window's line gap
//     is 3. Three neighbouring windows, three pitches, no reason that
//     survives an explanation.
//   * `contentMargin()` — the base-class hook a LAYOUT reads — was
//     overridden by only two of the seven. The other five answered the
//     base's 0, so a caller asking "where does this window's content
//     start?" got three different answers depending on the window.
//
// panel_sizing.h is where the column-count mathematics lives and is NOT
// this file's home: it is shared by panels that are not part of the
// split, and a change there is a change outside this step. What is
// shared here is the split's own SPACING — the numbers that decide how
// tight or how airy a split window is — and every split .cpp reads them
// from here instead of re-deriving its own.
//
// HOW IT IS USED
// --------------
// Names live in `mhw::split_layout` so a local `kMargin` cannot shadow
// them by accident: a file that keeps its own copy fails the
// consistency test in tests/split_layout_consistency_tests.cpp, which
// now asserts every split window answers the same contentMargin().

namespace mhw::split_layout {

// ─── 1. The inset ────────────────────────────────────────────────────────
//
// One value, used for three things that used to be three values: the
// window's own padding on all four sides, the top edge of the first row,
// and the bottom edge of the last. All the split windows inherited the
// HTML `.op { padding: 11px }` number from the panel they came from, so
// 11 is not a new decision — it is the removal of three spellings of it.
//
// THIS IS ALSO what `Panel::contentMargin()` answers. That hook exists
// so a caller can ask a window where its content starts without knowing
// which window it is talking to, and before this file only the wirebug
// and mantles windows answered it. All seven answer it now, and they all
// answer kSplitMargin, so the ring of sub-windows is inset by the same
// distance on every side.
inline constexpr int kSplitMargin = 11;

// ─── 2. The row pitches ──────────────────────────────────────────────────
//
// Two gaps, not one per window. The distinction is the one the design
// made implicitly everywhere and named nowhere: rows inside ONE block
// (the HP bar and the stamina bar; the two mantle boxes; two wrapped
// pill rows of the same status family) belong to the same thing and sit
// tight, while two DIFFERENT blocks (the debuff row and the buff row)
// are separate things and need air between them.
//
// Two levels, therefore, with names that say which is which:
//
//   kSplitGapWithin  — inside one block. This is what the split used to
//                       spell three different ways: 7 (bars: the HP/ST
//                       pair), 4 (wirebug: a wrapped pill row), 8
//                       (mantles: the two boxes). The new value is the
//                       MEDIAN, not a mean: 7 already worked for the
//                       only window whose within-block spacing was
//                       measured against legibility, and lowering 8→7
//                       and raising 4→7 keeps every existing row at
//                       least as readable as it is today.
//
//   kSplitGapBetween — between two blocks. The status window alone had
//                       one (its kBlockGap, named locally) and it is
//                       the gap that stops the debuff strip and the
//                       buff strip reading as one undifferentiated
//                       pile. 4 more than the within gap, which is the
//                       smallest difference the eye actually resolves.
inline constexpr int kSplitGapWithin  = 7;
inline constexpr int kSplitGapBetween = 11;

// The leading gap between a block's first row and the top inset. The
// bars window had one (`kBarsGap`, value 7) and the other windows did
// not, which is why the HP bar started 11px and the first mantle box
// started 11px from the same edge. Zero here means "the first row
// begins at the inset", which is what every window now does — the
// within-block gap applies BETWEEN two rows only, never before the
// first one.
inline constexpr int kSplitLead = 0;

// ─── 3. The type scale ───────────────────────────────────────────────────
//
// Three sizes, with the roles named. Every window that drew text used
// to pick its own pair: bars 9/8, wirebug 9/mono, mantles 9/8, info
// 9/10/12/9 — four different stacks for four windows that sit on the
// same screen. The stack below is the one the info window arrived at
// after its second layout round (label 9, value 10, timer 12), because
// it is the only one of the four that was ever validated against
// clipping, and it is now what the other windows use too.
//
// kSplitFontLabel  — the qualifier a value carries ("区域", "剩余").
//                    Never the number itself.
// kSplitFontValue  — the number or name the user came for.
// kSplitFontAccent — the one number that outranks everything else in
//                    the window: the info window's countdown, and
//                    nothing else. An accent is a RANK, not a size, so
//                    a window with no outranking number uses none.
inline constexpr int kSplitFontLabel  = 9;
inline constexpr int kSplitFontValue  = 10;
inline constexpr int kSplitFontAccent = 12;

// ─── 4. Derived helpers ──────────────────────────────────────────────────
//
// Plain arithmetic, no Qt, no state. A window's content box is the
// reservation it hands setContentSize(), and every window now builds it
// from the same three calls rather than from its own copy of
// `2 * kMargin + h`.

// Height of a stack of `rows` single-pitch rows with a gap between
// each adjacent pair: rows * pitch + (rows - 1) * gap.
[[nodiscard]] constexpr int rowsHeight(int rows, int pitch, int gap) noexcept
{
    if (rows <= 0)
        return 0;
    return rows * pitch + (rows - 1) * gap;
}

// Height of a stack of blocks, each of which carries its own inner
// stack: every block contributes its inner height, every adjacent pair
// contributes the BETWEEN gap, and the lead sits before the first only.
// `total` is the sum of the inner heights, `blocks` their count.
[[nodiscard]] constexpr int blocksHeight(int total, int blocks) noexcept
{
    if (blocks <= 0)
        return 0;
    return total + (blocks - 1) * kSplitGapBetween;
}

// Width of a horizontal row of `count` items of `itemW` each, with the
// gap between adjacent pairs. Same shape as panel_sizing's
// gridContentWidth, kept here so a split window needs no second include
// to lay out ONE row — the grid case (status pills) still goes through
// panel_sizing.h and shares the value with the rest of the panels.
[[nodiscard]] constexpr int rowWidth(int count, int itemW) noexcept
{
    if (count <= 0)
        return 0;
    return count * itemW + (count - 1) * kSplitGapWithin;
}

// The content box a window with outer height `h` and outer width `w`
// hands to setContentSize(): the inset on all four sides.
[[nodiscard]] constexpr int outerWidth(int contentW) noexcept
{
    return contentW + 2 * kSplitMargin;
}

[[nodiscard]] constexpr int outerHeight(int contentH) noexcept
{
    return contentH + 2 * kSplitMargin;
}

// The inverse, for the draw side: what a window of outer width `w` has
// between its two insets. Kept next to outerWidth() so the two cannot
// drift — the v0.10.3 lesson (a strip drawn at one height and reserved
// at another) is a reserve/draw asymmetry, and giving both directions
// one home is the cheapest possible defence.
[[nodiscard]] constexpr int innerWidth(int outer) noexcept
{
    return outer - 2 * kSplitMargin;
}

[[nodiscard]] constexpr int innerHeight(int outer) noexcept
{
    return outer - 2 * kSplitMargin;
}

} // namespace mhw::split_layout
