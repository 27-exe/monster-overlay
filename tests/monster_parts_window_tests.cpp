// SPDX-License-Identifier: Apache-2.0
//
// The parts window as an OBJECT, not as a model.
//
// monster_split_models_tests.cpp covers buildMonsterPartsModel, which is
// inline in the header — so it emits no symbol and reaches this .cpp only
// through the class below. That left the translation unit with 21 exported
// members and nothing asking for any of them, which is what the
// zero-reference gate reports as ZERO.
//
// What is actually worth pinning here, and is NOT covered by the model
// test, is the window's own behaviour:
//
//   * the columns dial and its clamp. Below 1 divides by zero in the row
//     height; above the part count produces empty columns that still
//     reserve space. Both ends are tested, because a dial that runs past
//     its ceiling is how a window ends up wider than any output.
//   * the row-layout vocabulary the base class reads (itemCount /
//     naturalItemWidth / itemGap / rowPitch). supportsColumns() is true,
//     and a window that claims the dial without answering those is a dead
//     key — the exact defect the layout consistency test looks for
//     elsewhere.
//   * the reserve/draw inversion: the height the window reports must equal
//     the height it asks the compositor for. Two formulas for one number
//     is the v0.10.3 incident, where a strip drew at one height and
//     reserved another and real content was clipped into a QRectF with no
//     warning.
//
// Everything is asked through the PUBLIC surface. The window's own
// displayableParts() and nudgeColumns() are private/protected by design —
// paintPanel() is their only legitimate caller — and widening them for a
// test would trade a real encapsulation for a green run.

#include "ui/panel_monster_split_parts.h"

#include <QApplication>

#include <cstdio>

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool cond, const char *what)
{
    ++gChecks;
    if (cond) {
        std::printf("PASS: %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++gFailures;
    }
}

// Show the window long enough for panelEnabled/paintPanel to have run, so
// contentWidth()/contentHeight() describe a real reservation rather than
// the construct-time default.
void prime(MonsterPartsStripPanel &w)
{
    w.setEditMode(true);
    w.setGameForDemo(mhw::GameId::World);
    w.resetDemoPrimed();
    w.show();
    for (int i = 0; i < 6; ++i) {
        w.repaint();
        QCoreApplication::processEvents();
    }
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // ── 1. the columns dial and its clamp ──────────────────────────
    //
    // The clamp is the contract, not an implementation detail.
    {
        MonsterPartsStripPanel w;

        w.setCardsPerRow(0);
        check(w.cardsPerRow() >= 1,
              "dial: 0 clamps up to 1 — 0 would make the row-height formula "
              "divide by zero");

        w.setCardsPerRow(-5);
        check(w.cardsPerRow() >= 1,
              "dial: a negative clamps up to 1 rather than wrapping");

        w.setCardsPerRow(99);
        const int ceiling = w.cardsPerRow();
        check(ceiling < 99,
              "dial: an absurd value is capped, so the window can never "
              "reserve a 99-column grid");

        w.setCardsPerRow(1);
        check(w.cardsPerRow() == 1,
              "dial: 1 is accepted — one card per row is the vertical list "
              "the user asked for");

        // The public accessor and the base's virtual must agree, because
        // the console greys the key on supportsColumns() while the layout
        // arithmetic reads columns().
        check(w.cardsPerRow() == w.columns(),
              "dial: cardsPerRow() and the base columns() are the same value");
        check(w.supportsColumns(),
              "dial: the parts window answers supportsColumns() — it is a "
              "wrapped row, unlike the bar windows");
    }

    // ── 2. the row-layout vocabulary ───────────────────────────────
    //
    // supportsColumns() true with any of these left at the base default 0
    // is a dial with nothing behind it. Asserted as a set, which is how a
    // future edit that drops one of them fails here rather than in game.
    {
        MonsterPartsStripPanel w;
        prime(w);

        check(w.itemCount() > 0,
              "vocabulary: itemCount() reports the demo cards rather than "
              "the base's 0");
        check(w.naturalItemWidth() > 0,
              "vocabulary: naturalItemWidth() is measured, not left at 0");
        check(w.itemGap() > 0,
              "vocabulary: itemGap() is answered");
        check(w.rowPitch() > 0,
              "vocabulary: rowPitch() is answered — the one whose absence "
              "is invisible until a layout asks for a wrap");
        check(w.contentMargin() > 0,
              "vocabulary: the window reports the split's shared inset");
    }

    // ── 3. the reserve/draw inversion ──────────────────────────────
    {
        MonsterPartsStripPanel w;
        prime(w);

        check(w.contentHeight() == w.contentSize().height(),
              "inversion: the height the window reports is the height it "
              "asked the compositor for");
        check(w.contentHeight() >= 2 * w.contentMargin(),
              "inversion: the height is never thinner than two margins");
        check(w.contentWidth() == w.contentSize().width(),
              "inversion: the width the window reports is the width it "
              "asked for");
        check(w.contentWidth() >= 2 * w.contentMargin(),
              "inversion: the width is never thinner than two margins");
    }

    // ── 4. the dial actually changes the reservation ───────────────
    //
    // A dial that moves a number without moving the geometry is the
    // failure mode of a dial wired to nothing. Fewer columns means
    // narrower and taller: the stacking direction the user asked for.
    {
        MonsterPartsStripPanel w;
        prime(w);
        const int wideW = w.contentWidth();
        const int wideH = w.contentHeight();

        w.setCardsPerRow(1);
        for (int i = 0; i < 6; ++i) {
            w.repaint();
            QCoreApplication::processEvents();
        }
        const int narrowW = w.contentWidth();
        const int narrowH = w.contentHeight();

        check(narrowW < wideW,
              "dial effect: stacking to one column makes the window NARROWER "
              "— the vertical arrangement the user asked for");
        check(narrowH > wideH,
              "dial effect: and TALLER, so no card is lost on the way down");
    }

    std::printf("\n%s: %d checks, %d failures\n",
                gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
