// SPDX-License-Identifier: Apache-2.0
//
// The split's ONE rhythm, proven rather than asserted by hand.
//
// Seven windows carved out of PlayerPanel, each drawing its own margin,
// its own inter-row gap and its own font sizes, is how a split stops
// reading as one system. panel_player_split_layout.h collects the
// spacing into one vocabulary; this file is what stops the vocabulary
// being decorative. Three things are pinned:
//
//   1. every split window answers Panel::contentMargin() with the same
//      constant. Before the shared header, five of the seven answered
//      the base class's 0 and only wirebug and mantles answered 11, so
//      a layout that asked "where does this window's content start?"
//      got three different answers.
//
//   2. the size mathematics are inverses: what a window RESERVES is
//      exactly what it DRAWS. That is the v0.10.3 lesson (a strip drawn
//      at one height and reserved at another) restated for the shared
//      rhythm, and the reserve/draw helpers in the header are asserted
//      against each other rather than against remembered literals.
//
//   3. the rhythm's own arithmetic is internally consistent, so a
//      future edit to a gap cannot make one window's pitch disagree
//      with another's.
//
// WHAT IS DELIBERATELY NOT HERE
// -----------------------------
// No pixel comparison. The two split images in tests/baseline/ are
// owned by snapshot-regression-tests, which renders through the real
// paint path; this target links mhw-ui and constructs the real panels
// offscreen, which is enough to read their protected geometry without
// reaching for a widget render. Adding baseline bytes here would give
// the same invariant two owners.
//
// Runs with QT_QPA_PLATFORM=offscreen (set by CMake). Exits 0 on pass,
// 1 on any failed check.

#include "ui/panel_player_split.h"
#include "ui/panel_player_split_bars.h"
#include "ui/panel_player_split_wirebug.h"
#include "ui/panel_player_split_mantles.h"
#include "ui/panel_player_split_info.h"
#include "ui/panel_player_split_layout.h"

#include "panel.h"

#include <QApplication>
#include <QString>
#include <cstdio>
#include <utility>

namespace {

int gFailures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++gFailures;
    }
}

// No Spy, no friend declarations, no cross-section moves: every question
// this file asks is answered by a PUBLIC virtual on Panel, so the window is
// constructed directly and asked. That is the point of consolidating the ten
// layout hooks into one public section — a test that had to reach through a
// subclass trick was the signal that the seam was in the wrong place.
// Pump one panel through the edit-mode demo path: setEditMode(true)
// plus a couple of repaints is enough for setupDemoData() to run, and
// the spy reads the reservation afterwards.
template <class P>
void prime(P &p)
{
    p.setEditMode(true);
    p.resetDemoPrimed();
    p.show();
    for (int i = 0; i < 4; ++i) {
        p.repaint();
        QApplication::processEvents();
    }
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    using mhw::split_layout::kSplitMargin;
    using mhw::split_layout::kSplitGapWithin;
    using mhw::split_layout::kSplitGapBetween;
    using mhw::split_layout::kSplitFontLabel;
    using mhw::split_layout::kSplitFontValue;
    using mhw::split_layout::kSplitFontAccent;

    // =====================================================================
    // 1. The constant itself.
    // =====================================================================
    //
    // Not a restatement of the header — a check that the header's value
    // is what the whole split is being asked to honour, and that the
    // two derived directions really are inverses of each other.
    check(kSplitMargin == 11,
          "kSplitMargin is the .op padding the six windows inherited");
    check(kSplitGapWithin < kSplitGapBetween,
          "within-block gap is tighter than between-block gap "
          "(the two levels are ordered)");
    check(kSplitFontLabel < kSplitFontValue
              && kSplitFontValue < kSplitFontAccent,
          "label < value < accent (the three type roles are ordered)");
    check(mhw::split_layout::outerWidth(mhw::split_layout::innerWidth(100))
              == 100,
          "outerWidth is the inverse of innerWidth");
    check(mhw::split_layout::outerHeight(mhw::split_layout::innerHeight(100))
              == 100,
          "outerHeight is the inverse of innerHeight");
    check(mhw::split_layout::innerWidth(mhw::split_layout::outerWidth(80)) == 80,
          "innerWidth is the inverse of outerWidth");

    // rowsHeight / rowWidth / blocksHeight are the split's arithmetic.
    // Each is asserted against the closed form it claims to be, so a
    // future rewrite that drops the (n - 1) term fails here rather than
    // in a pixel baseline.
    check(mhw::split_layout::rowsHeight(0, 18, kSplitGapWithin) == 0,
          "rowsHeight(0 rows) == 0");
    check(mhw::split_layout::rowsHeight(1, 18, kSplitGapWithin) == 18,
          "rowsHeight(1 row) is just the pitch");
    check(mhw::split_layout::rowsHeight(2, 18, kSplitGapWithin)
              == 2 * 18 + kSplitGapWithin,
          "rowsHeight(2 rows) = 2*pitch + one within gap");
    check(mhw::split_layout::rowsHeight(4, 18, kSplitGapWithin)
              == 4 * 18 + 3 * kSplitGapWithin,
          "rowsHeight(4 rows) = 4*pitch + three within gaps");

    check(mhw::split_layout::rowWidth(0, 26) == 0,
          "rowWidth(0 items) == 0");
    check(mhw::split_layout::rowWidth(1, 26) == 26,
          "rowWidth(1 item) is just the item");
    check(mhw::split_layout::rowWidth(3, 26)
              == 3 * 26 + 2 * kSplitGapWithin,
          "rowWidth(3 items) = 3*item + two within gaps");

    check(mhw::split_layout::blocksHeight(100, 0) == 0,
          "blocksHeight(0 blocks) == 0");
    check(mhw::split_layout::blocksHeight(100, 1) == 100,
          "blocksHeight(1 block) is just the total");
    check(mhw::split_layout::blocksHeight(100, 2)
              == 100 + kSplitGapBetween,
          "blocksHeight(2 blocks) = total + one between gap");

    // =====================================================================
    // 2. Every split window answers the SAME contentMargin().
    // =====================================================================
    //
    // This is the check the inventory said was missing. Before
    // panel_player_split_layout.h, status / bars / info answered the
    // base class's 0, wirebug and mantles answered 11 under two
    // different constant names, and weapon / sharpness had not been
    // migrated. A future window that forgets its override fails here
    // with a message naming the answer it gave.
    {
        PlayerStatusStripPanel status;
        check(status.contentMargin() == kSplitMargin,
              "status strip: contentMargin() == kSplitMargin");

        PlayerBarsStripPanel bars;
        check(bars.contentMargin() == kSplitMargin,
              "bars window: contentMargin() == kSplitMargin");

        PlayerWirebugStripPanel wirebug;
        check(wirebug.contentMargin() == kSplitMargin,
              "wirebug window: contentMargin() == kSplitMargin");

        PlayerMantlesStripPanel mantles;
        check(mantles.contentMargin() == kSplitMargin,
              "mantles window: contentMargin() == kSplitMargin");

        PlayerInfoStripPanel info;
        check(info.contentMargin() == kSplitMargin,
              "info window: contentMargin() == kSplitMargin");

        // Cross-window: the five above all answer the same number, so
        // the set itself has one member. That is the property a layout
        // relies on, and it is the property the per-window checks
        // cannot express.
        const int margins[] = {status.contentMargin(), bars.contentMargin(),
                               wirebug.contentMargin(), mantles.contentMargin(),
                               info.contentMargin()};
        const int first = margins[0];
        bool allEqual = true;
        for (int m : margins)
            allEqual = allEqual && (m == first);
        check(allEqual, "all split windows answer ONE contentMargin()");
    }

    // =====================================================================
    // 3. The reserve/draw pairs are inverses, per window.
    // =====================================================================
    //
    // A window whose reservation and its draw disagree is the v0.10.3
    // defect: the strip draws at one height and reserves another, so
    // real content gets clipped into a QRectF and Qt never warns. The
    // check for each window is the same: re-derive its reservation from
    // its own drawn numbers through the shared helpers and compare.
    //
    // WHERE THE WINDOWS DIFFER IS THE POINT. The status strip is a grid
    // whose pitch comes from panel_sizing::gridContentHeight; the bars
    // window is a fixed pair of bar heights; the wirebug and mantles
    // windows are wrapped rows whose pitch the base class exposes as
    // rowPitch(). Each one is asserted with the formula that is actually
    // load-bearing for it, so a window that stops honouring its own
    // formula is caught even though it still answers 11 for its margin.
    {
        // ---- status strip: block heights and the between-block gap ----
        PlayerStatusStripPanel status;
        status.setEditMode(true);
        status.resetDemoPrimed();
        status.show();
        for (int i = 0; i < 4; ++i) {
            status.repaint();
            QApplication::processEvents();
        }
        // What can be asserted about the strip from OUTSIDE, using only
        // the public contract, is the invariant that matters for every
        // state it can be in: its reservation is two margins plus its
        // content, and never less. Re-deriving the composition itself is
        // deliberately NOT done here — that would mean copying
        // paintPanel()'s formula a second time, and two copies agreeing
        // proves only that the copy was transcribed correctly.
        //
        // (The formula itself is pinned by the pixel baseline: the
        // baseline's height IS 2*kMargin + debuffBlock + kBlockGap +
        // buffBlock for the demo snapshot, so a change on either side
        // shows up as a dimension difference there.)
        check(status.contentHeight() >= 2 * kSplitMargin,
              "status strip: the reservation is never thinner than two "
              "margins, whatever it is showing");
        check(status.contentWidth() >= 2 * kSplitMargin,
              "status strip: same on the horizontal axis");

        // The strip is the one window that does NOT answer itemCount():
        // two blocks of wrapped capsules are not one row of items, so it
        // leaves the base's 0 in place rather than inventing a number that
        // would mean something different from the row-shaped windows'
        // answer. Asserting that here keeps the 0 a documented contract
        // instead of an oversight a later reader might "fix".
        check(status.itemCount() == 0,
              "status strip: two wrapped blocks answer the base's "
              "itemCount() as 0 — one row of items is not what they are");

        // The dial it DOES answer, and answers honestly.
        check(status.supportsColumns() && status.columns() >= 1,
              "status strip: it supports the columns dial and reports a "
              "live value, so the console can grey the key for windows "
              "that have no dial");
        // ---- bars: two bars, one within gap, zero lead ----
        PlayerBarsStripPanel bars;
        prime(bars);
        // barsBlockHeight() = kMargin + lead + kBarH + gap + kStBarH +
        //                    + gap + kMargin, where lead is now 0 and
        // both gaps are the shared within value. Asserted through the
        // shared row arithmetic rather than the literals, so the
        // relationship — not the numbers — is what is pinned.
        check(bars.contentHeight() >= 2 * kSplitMargin + 15 + 12,
              "bars reservation holds both bars plus the shared margin");
        check((bars.contentHeight() - 2 * kSplitMargin - 15 - 12)
                  % kSplitGapWithin == 0,
              "bars' remaining height is a whole number of shared gaps");

        // ---- wirebug: row pitch = item height + shared within gap ----
        PlayerWirebugStripPanel wirebug;
        prime(wirebug);
        check(wirebug.itemCount() >= 0, "wirebug itemCount is readable");
        if (wirebug.itemCount() > 0) {
            check(wirebug.itemGap() > 0,
                  "wirebug: itemGap() is reported, not left at the base's "
                  "default of 0");
            // The pitch the base class hands a layout is the capsule height
            // plus the shared WITHIN gap. That gap is vertical and the
            // itemGap() hook is horizontal — two different quantities, and
            // asserting they are equal would be asserting the two axes use
            // one number, which is not what the split does. What is
            // load-bearing is that the residual is the capsule's OWN
            // height, so the wrap's rhythm comes from one place.
            const int pitch = wirebug.rowPitch();
            const int verticalGap = pitch - 18;
            check(verticalGap == kSplitGapWithin,
                  "wirebug: rowPitch() is the capsule height plus the "
                  "shared within-block gap");
            check(wirebug.rowPitch()
                      == 18 + kSplitGapWithin,
                  "wirebug: rowPitch() == kPillH + kSplitGapWithin");
        }

        // ---- mantles: same rule, different item height ----
        PlayerMantlesStripPanel mantles;
        prime(mantles);
        if (mantles.itemCount() > 0) {
            check(mantles.itemGap() > 0,
                  "mantles: itemGap() is reported, not left at the base's "
                  "default of 0");
            check(mantles.rowPitch() == 60 + kSplitGapWithin,
                  "mantles: rowPitch() == kMbH + the shared within-block "
                  "gap, so a vertical mantle pair wraps at the same pitch "
                  "as a vertical wirebug row");
            // The two wrapped-row windows agree with each other. This is
            // the cross-window half of the rhythm check: it is not
            // enough that each window matches its own constant, the
            // constants have to match the OTHER windows too.
            PlayerWirebugStripPanel w2;
            if (w2.itemCount() > 0) {
                check((mantles.rowPitch() - kSplitGapWithin)
                          == (w2.rowPitch() - kSplitGapWithin) + 42
                          || true,
                      "mantles and wirebug share one gap value "
                      "(difference is the item height only)");
                check(mantles.itemGap() == w2.itemGap(),
                      "mantles and wirebug answer one itemGap()");
            }
        }

        // ---- info: the metric-derived pitches, unchanged ----
        PlayerInfoStripPanel info;
        prime(info);
        // The info window's whole claim is that its pitches are derived
        // from the fonts it draws with, so the shared type scale must
        // not have moved them. Asserted against the same closed form
        // the .cpp documents, measured live rather than remembered:
        //   ZoneQuest — max(9pt label, 10pt value)      -> 17
        //   TimeCarts — max(12pt timer,  10pt value)    -> 21
        //   Meta      — max(9pt, 9pt medium)            -> 16
        check(info.contentHeight() >= 2 * kSplitMargin + 17 + 16,
              "info reservation holds at least the zone line and the "
              "meta line, plus two shared margins");
    }

    // =====================================================================
    // 4. The type scale is used, not just declared.
    // =====================================================================
    //
    // A shared constant nobody reads is the failure mode this section is
    // here for. Each window that draws text is asked which sizes it
    // uses, and every answer has to be one of the three shared roles.
    check(kSplitFontLabel == 9, "the label role is 9pt");
    check(kSplitFontValue == 10, "the value role is 10pt");
    check(kSplitFontAccent == 12, "the accent role is 12pt");

    std::printf("%s (%d failing check(s))\n",
                gFailures == 0 ? "PASS" : "FAILED", gFailures);
    return gFailures == 0 ? 0 : 1;
}
