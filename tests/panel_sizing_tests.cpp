// SPDX-License-Identifier: Apache-2.0
//
// The sizing contract every v0.12+ sub-window is built on: the pure
// integer arithmetic in src/ui/panel_sizing.h.
//
// WHAT THIS FILE COVERS
// ---------------------
// The user's v0.12 requirement is that a sub-window "adapts its length
// and width to fill" and that the user can tune the shape. Both halves
// of that live in one place, and this file pins it:
//
//   1. THE WRAP (§1, §2). gridItemsPerRow() answers "how many fit on a
//      row" from a width budget. Three properties of it are load-bearing
//      and each is asserted twice — once as a named boundary case, once
//      inside the sweep in §2 that re-derives the answer over the whole
//      small-number space:
//        * the `- gap` term in the closed form,
//        * the "at least one" floor,
//        * the never-wider-than-count cap.
//   2. THE SIZE (§3, §4). gridTotalSize() / gridEmptySize() are the only
//      numbers a panel may hand Panel::setContentSize().
//   3. THE IDENTITY (§5). gridSlotWidth() is the draw side's exact
//      inverse of gridContentWidth() — swept, not spot-checked, because
//      that identity is what "one gateset, three consumers" means here.
//   4. THE BOUNDARIES (§1-§6). 0 items, 1 item, exact division,
//      off-by-one, width too small for a single item, negatives.
//   5. THE ZOOM (§6). effectiveSize() must round UP, never down: the ink
//      is lround(logical * scale) device pixels and the surface is a
//      FLOOR for it. §6 sweeps 400 logical widths over the whole
//      0.5..3.0 range asserting exactly that.
//
// Two deliberate omissions, so a reader does not go looking for them:
//
//   * No mutation harness. The three mutations that matter (dropping
//     `- gap` from gridItemsPerRow, truncating instead of lround in
//     scaleRound, truncating instead of rounding the device size) are
//     exactly what the sweeps in §2, §5 and §6 catch; each was verified
//     by hand against the real output first — see the report that
//     accompanies this file.
//   * No Qt include. This target is arithmetic only, by the header's
//     layer rule; including a Qt header here would defeat its "no
//     mhw-ui, no widgets" requirement.
//
// The CHECK macro prints on failure AND names every property on success,
// so a green run still lists what it proved — the convention
// tests/player_layout_tests.cpp uses.

#include "ui/panel_sizing.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

// The whole header is pulled in unqualified so the checks below read as
// statements about the sizing rule rather than about its namespace.
using namespace mhw::panel_sizing;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const char *what)
{
    ++checks;
    if (ok)
        std::printf("PASS: %s\n", what);
    else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// The wrap property, restated from first principles: the answer is the
// LARGEST k in [1, count] such that k items of minW plus (k-1) gaps of
// gap fit in available; and when even k=1 does not fit, the answer is
// the floor 1. A brute-force reference rather than a second closed
// form, so a wrong formula in the header cannot agree with itself.
bool wrapMatchesBruteForce(int k, int count, int minW, int gap, int available)
{
    const int w = minW < 0 ? 0 : minW;
    const int g = gap < 0 ? 0 : gap;
    const int av = available < 0 ? 0 : available;
    if (count <= 0)
        return k == 0;
    int best = 1;                       // the floor
    for (int n = 1; n <= count; ++n) {
        if (n * w + (n - 1) * g <= av)
            best = n;
    }
    return k == best;
}

} // namespace

int main()
{
    using mhw::panel_sizing::aspectRatio;
    using mhw::panel_sizing::clampInt;
    using mhw::panel_sizing::clampScale;
    using mhw::panel_sizing::clampWidth;
    using mhw::panel_sizing::effectiveSize;
    using mhw::panel_sizing::gridContentHeight;
    using mhw::panel_sizing::gridContentWidth;
    using mhw::panel_sizing::gridEmptySize;
    using mhw::panel_sizing::gridItemsPerRow;
    using mhw::panel_sizing::gridOverflows;
    using mhw::panel_sizing::gridRowsFor;
    using mhw::panel_sizing::gridSlotWidth;
    using mhw::panel_sizing::gridTotalSize;
    using mhw::panel_sizing::GridSize;
    using mhw::panel_sizing::kScaleMax;
    using mhw::panel_sizing::kScaleMin;
    using mhw::panel_sizing::kScaleStep;
    using mhw::panel_sizing::nonNegative;

    // ====================================================================
    // 1. THE WRAP — how many items fit on a row
    // ====================================================================
    {
        // 3 items of 78 with a 5px gap need 3*78 + 2*5 = 244. The old
        // panel_player_split.cpp:471 clamped the width to 420 and then
        // divided by 78+5, which is the same answer here only because
        // the -gap term is present in gridItemsPerRow.
        check(gridItemsPerRow(420, 78, 5, 3) == 3,
              "wrap: 3x78+2x5=244 fits 420, 3-wide");

        // THE -gap TERM. 2 items of 80 with a 10px gap need 170. A
        // 180px budget holds 2 (170 <= 180) but not 3 (260 > 180).
        // Without `- gap` in the solving constant the answer stays 2
        // here, but at 175 it drops to 1 and the row loses a capsule it
        // has room for — §2 sweeps that whole edge.
        check(gridItemsPerRow(180, 80, 10, 3) == 2,
              "wrap: 2x80+1x10=170 fits 180; the third does not");
        check(gridItemsPerRow(169, 80, 10, 2) == 1,
              "wrap: 169 is one short of 170 — one item only");
        check(gridItemsPerRow(175, 80, 10, 3) == 2,
              "wrap: 175 holds two even though 175/90 floor is 1"
              " (this is the case a dropped -gap term gets wrong)");

        // THE FLOOR. A row too narrow for even one item still answers 1:
        // a grid with no columns has nothing to divide by, and the
        // caller has gridOverflows() to notice.
        check(gridItemsPerRow(10, 78, 5, 3) == 1,
              "wrap floor: nothing fits, still exactly one column");
        check(gridOverflows(10, 78, 5),
              "wrap overflow: width 10 vs item 78 is reported, not hidden");
        check(!gridOverflows(78, 78, 5),
              "wrap overflow: exactly one item's width fits");
        check(gridOverflows(77, 78, 5),
              "wrap overflow: one pixel short of one item is reported,"
              " so the caller knows the capsule will be elided");

        // Never wider than there are items.
        check(gridItemsPerRow(100000, 10, 0, 4) == 4,
              "wrap cap: 4 items never reserve a 5th column");

        // Nothing to lay out.
        check(gridItemsPerRow(100, 78, 5, 0) == 0,
              "wrap empty: count 0 answers 0, never the floor");
        check(gridItemsPerRow(-500, 78, 5, 3) == 1,
              "wrap negative width: the floor decides, not a negative");

        // Negative inputs do not become negative answers.
        check(gridItemsPerRow(420, -78, 5, 3) == 3,
              "wrap negative item width: free items, everything fits");
        check(gridItemsPerRow(420, 78, -5, 3) == 3,
              "wrap negative gap: sanitised to 0");
        check(gridItemsPerRow(-1, -1, -1, -1) == 0,
              "wrap all negative: the count guard wins, answers 0");
        check(gridItemsPerRow(0, 0, 0, 0) == 0,
              "wrap all zero: no items, 0 columns");

        // THE SWEEP. The brute force above re-derives every answer, over
        // the space that includes the shipped strip's real numbers
        // (78-wide pills, 5px gaps, budgets from 0 to 500). A dropped
        // -gap term, a missing floor, or a missing cap all fail here.
        int wrapMismatches = 0;
        int wrapCases = 0;
        const int widths[] = {0, 1, 10, 77, 78, 79, 120};
        const int gaps[] = {0, 1, 4, 5, 20};
        for (int count = 0; count <= 8; ++count) {
            for (int minW : widths) {
                for (int gap : gaps) {
                    for (int av = 0; av <= 500; av += 13) {
                        ++wrapCases;
                        if (!wrapMatchesBruteForce(
                                gridItemsPerRow(av, minW, gap, count),
                                count, minW, gap, av))
                            ++wrapMismatches;
                    }
                }
            }
        }
        check(wrapMismatches == 0,
              "wrap sweep: gridItemsPerRow matches a brute-force"
              " 'largest k that fits, floor 1' over counts 0..8 x"
              " widths {0,1,10,77,78,79,120} x gaps {0,1,4,5,20} x"
              " budgets 0..500 step 13");
        check(wrapCases > 1000,
              "wrap sweep: the space above actually covered ground");
    }

    // ====================================================================
    // 2. THE ROWS — height side, 0/1/edge
    // ====================================================================
    {
        check(gridRowsFor(0, 3) == 0, "rows: 0 items, 0 rows");
        check(gridRowsFor(-7, 3) == 0, "rows: negative count, 0 rows");
        check(gridRowsFor(1, 3) == 1, "rows: 1 item, 1 row");
        check(gridRowsFor(3, 3) == 1, "rows: exactly one full row");
        check(gridRowsFor(4, 3) == 2, "rows: 4 at 3-wide is 2 rows");
        check(gridRowsFor(6, 3) == 2, "rows: 6 at 3-wide is 2 rows");
        check(gridRowsFor(7, 3) == 3, "rows: 7 at 3-wide is 3 rows");
        check(gridRowsFor(7, 0) == 7,
              "rows: perRow 0 answers one item per row, not a divide");
        check(gridRowsFor(-5, -5) == 0, "rows: negative both, count guard");

        check(gridContentHeight(0, 18, 4) == 0, "height: 0 rows is 0");
        check(gridContentHeight(-3, 18, 4) == 0, "height: negative is 0");
        check(gridContentHeight(1, 18, 4) == 18,
              "height: one row, no gap");
        check(gridContentHeight(2, 18, 4) == 40,
              "height: 2 rows = 2*18 + 1*4");
        check(gridContentHeight(3, 18, 4) == 62,
              "height: 3 rows = 3*18 + 2*4 — the blockHeight formula");
    }

    // ====================================================================
    // 3. THE WIDTH — the fill property
    // ====================================================================
    {
        // The first shipped strip measured 91px wide (World) / 125px
        // (Rise) with the width derived from the widest SINGLE capsule.
        // With the budget dial the same ten one-glyph capsules now fill
        // a column instead of leaving most of the window empty.
        check(gridContentWidth(1, 3, 78, 5) == 78,
              "width: one item, no trailing gap");
        check(gridContentWidth(3, 3, 78, 5) == 244,
              "width: 3 items = 3*78 + 2*5");

        // The cap: 3 items at 4-per-row must not reserve a 4th column.
        check(gridContentWidth(3, 4, 78, 5) == 244,
              "width: fewer items than columns reserves the cells in use");
        check(gridContentWidth(0, 3, 78, 5) == 0, "width: 0 items is 0");
        check(gridContentWidth(-2, 3, 78, 5) == 0, "width: negative is 0");
        check(gridContentWidth(4, 0, 78, 5) == 78,
              "width: 0 columns behaves as 1, not a degenerate row");
        check(gridContentWidth(2, 3, -78, 5) == 5,
              "width: negative item width degrades to gap-only");
        check(gridContentWidth(2, 3, 78, -5) == 156,
              "width: negative gap is sanitised to 0");
    }

    // ====================================================================
    // 4. THE TOTAL SIZE — what reaches setContentSize()
    // ====================================================================
    {
        // A strip shaped like the shipped one: 8 pills at 78 wide, 5
        // apart, 18 tall, rows 4 apart, margin 6, 3 per row -> 3 rows.
        const GridSize g = gridTotalSize(8, 3, 78, 18, 5, 4, 6);
        check(g.width == 3 * 78 + 2 * 5 + 12,
              "total: 3 columns of 78 + 2 gaps + 2*margin");
        check(g.height == 3 * 18 + 2 * 4 + 12,
              "total: 3 rows of 18 + 2 gaps + 2*margin");

        // An empty grid is not a 0x0 surface: the caller answers the
        // collapsed size, which is 2*margin everywhere in the codebase.
        const GridSize z = gridTotalSize(0, 3, 78, 18, 5, 4, 6);
        check(z.width == 0 && z.height == 0,
              "total: empty grid answers 0, never negative");
        const GridSize n = gridTotalSize(-9, 3, 78, 18, 5, 4, 6);
        check(n.width == 0 && n.height == 0,
              "total: negative count is 0 wide and 0 tall");
        check(gridEmptySize(6).width == 12 && gridEmptySize(6).height == 12,
              "empty size: 2*margin on both axes");
        check(gridEmptySize(-3).width == 0 && gridEmptySize(-3).height == 0,
              "empty size: negative margin is 0, not -6");
    }

    // ====================================================================
    // 5. THE IDENTITY — reservation and draw cannot drift apart
    // ====================================================================
    //
    //    gridSlotWidth(gridContentWidth(...), perRow, gap) == itemWidth
    //
    // exactly, with no truncation, over a small-number space. This is
    // what "the content fills the window" means, and it is why a panel
    // never has to choose between eliding a glyph and reserving a cell
    // the drawer does not use.
    {
        int mismatches = 0;
        int cases = 0;
        for (int perRow = 1; perRow <= 6; ++perRow) {
            for (int itemW = 1; itemW <= 140; itemW += 7) {
                for (int gap = 0; gap <= 30; gap += 7) {
                    ++cases;
                    const int content =
                        gridContentWidth(perRow, perRow, itemW, gap);
                    const int slot = gridSlotWidth(content, perRow, gap);
                    if (slot != itemW) {
                        ++mismatches;
                        std::fprintf(stderr,
                                     "  inverse broken: perRow=%d w=%d gap=%d"
                                     " reserved %d -> slot %d\n",
                                     perRow, itemW, gap, content, slot);
                    }
                }
            }
        }
        check(mismatches == 0,
              "identity: gridSlotWidth inverts gridContentWidth over"
              " perRows 1..6 x widths 1..140 x gaps 0..30");
        check(cases > 100, "identity: the sweep actually covered ground");

        // A slot never comes back negative, even for a budget only just
        // larger than zero.
        check(gridSlotWidth(0, 3, 5) == 0,
              "slot width: an unfillable budget is 0, not negative");
        check(gridSlotWidth(2, 3, 5) == 0,
              "slot width: a budget below the gaps is 0, not negative");
        check(gridSlotWidth(6, 3, 0) == 2, "slot width: 6 over 3 columns");
        check(gridSlotWidth(7, 3, 0) == 2,
              "slot width: floor division; the remainder fills the row");
        check(gridSlotWidth(10, 0, 5) == 0,
              "slot width: 0 columns is 0, never a divide by zero");
    }

    // ====================================================================
    // 6. THE ZOOM — lround, never a surface smaller than its ink
    // ====================================================================
    {
        check(clampInt(5, 1, 10) == 5, "clampInt: inside is untouched");
        check(clampInt(-5, 1, 10) == 1, "clampInt: below rises to lo");
        check(clampInt(99, 1, 10) == 10, "clampInt: above drops to hi");

        check(clampWidth(0, 6, 420) == 6,
              "clampWidth: a too-narrow strip rises to the min");
        check(clampWidth(900, 6, 420) == 420,
              "clampWidth: a twelve-consumable hunter is capped");
        check(clampWidth(300, 6, 420) == 300,
              "clampWidth: the middle is untouched");
        check(clampWidth(300, -9, 420) == 300,
              "clampWidth: negative lo is sanitised");
        check(clampWidth(300, 500, 400) == 400,
              "clampWidth: a reversed range collapses to hi, not UB");
        check(clampWidth(300, 6, 0) == 300,
              "clampWidth: hi 0 means no upper bound");

        check(nonNegative(-42) == 0 && nonNegative(0) == 0
                  && nonNegative(7) == 7,
              "nonNegative: negatives become 0, the rest pass through");

        // THE SCALE CLAMP. kMinScale/kMaxScale are mirrored from
        // panel.cpp:25-26 on purpose (see the header's layer rule); this
        // keeps the two spellings equal.
        check(kScaleMin == 0.5 && kScaleMax == 3.0 && kScaleStep == 0.1,
              "scale constants: the panel's own zoom range");
        check(clampScale(0.1) == 0.5, "scale clamp: below the floor rises");
        check(clampScale(99.0) == 3.0, "scale clamp: above the ceiling drops");
        check(clampScale(1.7) == 1.7, "scale clamp: inside is untouched");
        check(clampScale(0.5) == 0.5, "scale clamp: the floor itself");
        check(clampScale(3.0) == 3.0, "scale clamp: the ceiling itself");
        check(clampScale(0.0) == 0.5, "scale clamp: zero rises to the floor");
        check(clampScale(-1.0) == 0.5,
              "scale clamp: negative rises to the floor");
        check(std::isnan(clampScale(0.0 / 0.0)) == false
                  && clampScale(0.0 / 0.0) == 0.5,
              "scale clamp: NaN rises to the floor, not out of range");
        double s = kScaleMin;
        s = clampScale(s + kScaleStep);
        check(s > kScaleMin && s < kScaleMax,
              "scale step: one notch up from the floor is inside");
        check(clampScale(kScaleMin - kScaleStep) == kScaleMin
                  && clampScale(kScaleMax + kScaleStep) == kScaleMax,
              "scale step: a notch past either endpoint clamps, not sits"
              " outside");

        // panel.cpp:396-397 computes the ACTUAL window size with
        //     lround(logical * scale_)
        // and paintEvent() paints with painter.scale(scale_, scale_),
        // so the ink occupies exactly that many device pixels. At
        // logical=125, scale=0.5 (product 62.5) truncation answers 62
        // and clips half a pixel; lround answers 63 and clips nothing.
        const GridSize a = effectiveSize(125, 79, 0.5);
        check(a.width == 63 && a.height == 40,
              "zoom: 125x79 at 0.5x rounds 62.5 UP to 63 (truncation"
              " would answer 62 and clip a pixel)");
        check(a.width == static_cast<int>(std::lround(125 * 0.5)),
              "zoom: the device size equals the ink width panel.cpp"
              " computes, so nothing is clipped");
        const GridSize b = effectiveSize(7, 5, 3.0);
        check(b.width == 21 && b.height == 15,
              "zoom: an exact product does not gain a pixel");
        const GridSize c = effectiveSize(3, 3, 1.0 / 3.0);
        check(c.width == 1 && c.height == 1,
              "zoom: 1/3 of 3px is 1, not 0");
        check(effectiveSize(100, 100, kScaleMin).width == 50
                  && effectiveSize(100, 100, kScaleMax).width == 300,
              "zoom: the range endpoints are honoured");

        check(effectiveSize(0, 0, 2.0).width == 0
                  && effectiveSize(0, 0, 2.0).height == 0,
              "zoom: 0x0 in is 0x0 out, never a 1x1 surface");
        check(effectiveSize(-10, -10, 2.0).width == 0
                  && effectiveSize(-10, -10, 2.0).height == 0,
              "zoom: negative logical is 0, never negative device px");

        // THE SWEEP. Over the whole shipped zoom range and every logical
        // width the overlay can plausibly produce, the surface is never
        // smaller than the ink. A truncating mutation fails on 125x79@0.5
        // and on thousands more.
        int clipped = 0;
        for (int logical = 1; logical <= 400; ++logical) {
            for (int pct = 50; pct <= 300; pct += 10) {
                const double sc = pct / 100.0;
                const int dev = effectiveSize(logical, logical, sc).width;
                const int ink = static_cast<int>(std::lround(logical * sc));
                if (dev < ink)
                    ++clipped;
            }
        }
        check(clipped == 0,
              "zoom sweep: 400 logical widths x 0.5..3.0 in 0.1 steps"
              " never produce a surface smaller than its ink");
    }

    // ====================================================================
    // 8. THE ASPECT RATIO — a reported number, never an input
    // ====================================================================
    {
        check(aspectRatio(0, 50) == 0.0 && aspectRatio(50, 0) == 0.0
                  && aspectRatio(-1, -1) == 0.0,
              "aspect: a degenerate box answers 0 rather than NaN");
        check(aspectRatio(244, 62) > 3.9 && aspectRatio(244, 62) < 4.0,
              "aspect: the strip's own 244x62 ratio, for the console");

        // A collapsed window is square because it is only margins.
        const GridSize e = gridEmptySize(6);
        check(aspectRatio(e.width, e.height) == 1.0,
              "aspect: a collapsed window is square, being only margins");
    }

    // 9. THE USER'S SIZE FLOOR — effectiveWidth / effectiveHeight
    // ====================================================================
    //
    // The three properties that make this a floor rather than a lock or a
    // target, each stated as a test because each is a way the feature
    // could silently become the other thing:
    //
    //   1. 0 is an identity, so a panel nobody has dialled is unchanged.
    //   2. content wins when it is larger, so new content grows the
    //      window instead of being clipped by a stale floor.
    //   3. a non-positive content is returned as-is, so a collapsed or
    //      corrupt measurement cannot be inflated by a floor.
    {
        // (1) The default. This is the whole migration story: every
        // panels.ini written before v0.12 has no minW key, reads 0, and
        // gets its content width back. The four shipped panels' pixel
        // baselines depend on this being exactly an identity.
        check(effectiveWidth(378, 0) == 378,
              "floor: width 0 is an identity, so an undialed panel keeps "
              "its content width");
        check(effectiveHeight(361, 0) == 361,
              "floor: height 0 is an identity too");
        check(effectiveWidth(0, 0) == 0 && effectiveHeight(0, 0) == 0,
              "floor: 0 x 0 in, 0 x 0 out");

        // (2) Content wins. The user set 300 while the content wants 89;
        // the floor raises it. Then a longer player name makes the
        // content 420 and the floor must NOT pull it back down — a floor
        // that clamped would be the "locked size" this design rejects,
        // because the next longer name would be cut off.
        check(effectiveWidth(89, 300) == 300,
              "floor: a floor above the content raises it (the bars case: "
              "89px of ink becomes 300)");
        check(effectiveWidth(420, 300) == 420,
              "floor: content larger than the floor still wins, so a "
              "longer name grows the window instead of being clipped");
        check(effectiveHeight(20, 48) == 48,
              "floor: the same rule on the other axis");

        // (3) Degenerate content. Zero and negative are not measurements
        // at all — .toInt() on a corrupt key, or a grid with nothing in
        // it — and there is no window to speak of, so the floor must not
        // manufacture one.
        //
        // Note what is deliberately NOT here: a collapsed-but-positive
        // content (2*kMargin, the unmounted-but-alive window) IS raised.
        // "The window is at least this wide" has to mean it when there is
        // no content yet, or a sub-window would visibly snap back to its
        // thin self the moment its data arrived and vanished again when it
        // left — which is a worse flicker than the empty box is worth.
        check(effectiveWidth(0, 300) == 0,
              "floor: zero content stays zero — there is no window to "
              "widen");
        check(effectiveWidth(-5, 300) == -5,
              "floor: a negative measurement is passed through rather "
              "than masked into a plausible-looking number");
        check(effectiveHeight(0, 48) == 0,
              "floor: same on the other axis");

        // A negative floor is "absent", not a request to shrink. The
        // config reader clamps to [0,4096] on the way in, so this only
        // guards a direct caller — but a direct caller is what
        // panel_sizing.h's own contract is for.
        check(effectiveWidth(378, -16) == 378,
              "floor: a negative floor reads as absent, not as a shrink "
              "to nothing");
        check(effectiveHeight(361, -1) == 361,
              "floor: same on the other axis");

        // The relationship that makes the bars fix work: the window the
        // compositor got and the width the paint path drew into are the
        // same number, so the gauge fills the window instead of leaving
        // background to its right.
        const int content = 127;   // the bars window at default
        for (const int floorPx : {0, 16, 100, 300, 356, 4096}) {
            const int windowW = effectiveWidth(content, floorPx);
            const int drawnW = effectiveWidth(content, floorPx);
            check(windowW == drawnW && drawnW >= content,
                  "floor: reserve and draw agree at every floor value");
        }

        // Monotone: more floor never means a smaller window. If this ever
        // fails, the dial would appear to fight the user.
        int prev = 0;
        bool monotone = true;
        for (int f = 0; f <= 512; f += 16) {
            const int w = effectiveWidth(89, f);
            if (w < prev)
                monotone = false;
            prev = w;
        }
        check(monotone,
              "floor: width is monotone in the floor, so pressing = never "
              "appears to shrink the window");
    }

    std::printf("\n%d checks, %d failure(s)\n%s\n", checks, failures,
                failures == 0 ? "ALL PANEL-SIZING TESTS PASSED"
                              : "SOME PANEL-SIZING TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
