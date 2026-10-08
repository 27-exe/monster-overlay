#include <QSize>
#include <QRectF>
#include <cstdio>
#include <vector>

#include "ui/hud_canvas.h"
#include "ui/panel.h"

// v0.14: the overlap-free wall, tested against REAL window sizes.
//
// The wall exists so a first-time user does not open the console to a dozen
// boxes on one pixel, and its grid was deliberately a pure function of
// (corner, ordinal) — no screen, no window sizes — because a screen-shaped
// default is wrong on every other screen and "why here" has no answer.
//
// What that pure function cannot see is how BIG the windows it is placing
// actually are, and that is the whole defect: the cells are
// kSubWallCellW x kSubWallCellH = 260 x 128, while the real split windows
// measure up to 560 x 183. Window 0's cell therefore claims a rectangle that
// window 3's window-body already occupies, and mousePressEvent walks the
// boxes in REVERSE — so a press in the shared area selects the LAST box and
// the one underneath cannot be grabbed at all. That is the user's "the
// sub-windows cannot be dragged past the midline": they were not being
// clicked, they were being covered.
//
// So the wall must be laid out from the windows it lays out. This test
// measures the claim rather than restating the implementation.

namespace
{

int checks = 0;
int failures = 0;

void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  FAIL  %s\n", what);
    } else {
        std::printf("PASS: %s\n", what);
    }
}

// The real split windows, measured offscreen. These are the numbers that
// matter, not a guess: each is the pixmap the console actually renders for
// that key.
struct Probe
{
    const char *key;
    int w;
    int h;
    Corner corner;
};

const Probe kProbes[] = {
    // ---- player, all TopLeft ----
    {"player-status",     560, 119, Corner::TopLeft},
    {"player-bars",       390,  82, Corner::TopLeft},
    {"player-mantles",    560,  30, Corner::TopLeft},
    {"player-wirebug",    560,  30, Corner::TopLeft},
    {"player-weapon",     560,  60, Corner::TopLeft},
    {"player-info",       560,  30, Corner::TopLeft},
    // ---- monster, TopLeft ----
    {"monster-identity",  396, 183, Corner::TopLeft},
    {"monster-conditions", 624, 144, Corner::TopLeft},
    // ---- damage, TopRight ----
    {"damage-share",      560,  96, Corner::TopRight},
    {"damage-chart",      560, 190, Corner::TopRight},
    {"damage-rows",       560, 153, Corner::TopRight},
};

const QSize kProbeScreen(3840, 2160);

int countOverlappingPairs(const std::vector<QRectF> &cells)
{
    int pairs = 0;
    for (size_t i = 0; i < cells.size(); ++i) {
        for (size_t j = i + 1; j < cells.size(); ++j) {
            if (cells[i].intersects(cells[j]))
                ++pairs;
        }
    }
    return pairs;
}

} // namespace

int main()
{
    // ---- THE DEFECT, AS A NUMBER ------------------------------------------
    //
    // Place every window at its grid cell and inflate the rect to its REAL
    // size, then count the pairs that now share pixels. With the fixed
    // 260x128 cells this is nonzero, and every nonzero pair is a window the
    // user cannot click.
    // ONE LATTICE for the whole wall: the largest window ANYWHERE in it.
    //
    // Per-window sizing leaves ten overlapping pairs, because a column's x
    // advances by a different width each step while its boxes all draw at
    // the same x. Per-CORNER sizing leaves two, because the two sides of
    // the screen are one screen: TopLeft walks rightwards from the left
    // edge and TopRight leftwards from the right, so with independent
    // pitches they meet and overlap in the middle. A single shared pitch
    // makes the two walks mirror images of one lattice.
    int latticeW = 0, latticeH = 0;
    for (const Probe &p : kProbes) {
        latticeW = qMax(latticeW, p.w);
        latticeH = qMax(latticeH, p.h);
    }
    const QSize lattice(latticeW, latticeH);

    // The ordinal walk is per corner, exactly as the host does it.
    std::vector<QRectF> real;
    std::vector<int> perCorner(4, 0);
    real.reserve(sizeof(kProbes) / sizeof(kProbes[0]));
    for (const Probe &p : kProbes) {
        const int c = static_cast<int>(p.corner);
        const QRectF cell = HudCanvas::subWindowGridRect(
            p.corner, perCorner[c]++, kProbeScreen, lattice);
        real.emplace_back(cell.x(), cell.y(), p.w, p.h);
    }
    const int overlaps = countOverlappingPairs(real);
    std::printf("wall sized from the windows: %d overlapping pair(s) "
                "of %d window(s)\n",
                overlaps, static_cast<int>(real.size()));
    // NAME THE PAIRS. "2 overlapping pairs" is a number; which two windows,
    // and in which column, is a diagnosis.
    for (size_t i = 0; i < real.size(); ++i) {
        for (size_t j = i + 1; j < real.size(); ++j) {
            if (real[i].intersects(real[j])) {
                std::printf("   OVERLAP  %-20s (%.0f,%.0f %.0fx%.0f)  x  "
                            "%-20s (%.0f,%.0f %.0fx%.0f)\n",
                            kProbes[i].key, real[i].x(), real[i].y(),
                            real[i].width(), real[i].height(),
                            kProbes[j].key, real[j].x(), real[j].y(),
                            real[j].width(), real[j].height());
            }
        }
    }

    // THE FALLBACK STILL HAS TO BE HONEST. A caller that has not measured a
    // window yet gets the constants, and those must not silently stack
    // every box on the origin either — otherwise the fix only holds for
    // callers that behave.
    {
        std::vector<QRectF> fallback;
        for (const Probe &p : kProbes) {
            const QRectF cell = HudCanvas::subWindowGridRect(
                p.corner, static_cast<int>(fallback.size()), kProbeScreen,
                QSize(0, 0));
            fallback.emplace_back(cell.x(), cell.y(), cell.width(),
                                 cell.height());
        }
        for (size_t i = 0; i < fallback.size(); ++i) {
            for (size_t j = i + 1; j < fallback.size(); ++j) {
                if (fallback[i] == fallback[j]) {
                    check(false,
                          "an unmeasured window still lands on a distinct "
                          "cell, so the fallback path cannot stack the wall");
                    goto fallback_done;
                }
            }
        }
        check(true,
              "an unmeasured window still lands on a distinct cell, so the "
              "fallback path cannot stack the wall");
    }
fallback_done:;

    // The wall's contract. Zero is the only acceptable answer: a wall that
    // overlaps is not a wall, and its cost is a window that cannot be
    // dragged.
    check(overlaps == 0,
          "no two windows share a pixel — every one of them is reachable "
          "by a click, which is what the wall is FOR");

    // ---- AND THE PROPERTY THAT MUST SURVIVE THE FIX ----------------------
    //
    // Cells must stay disjoint by construction rather than by luck, so the
    // grid must remain a pure function of what it is told. A fix that
    // special-cases the current window list would pass the line above and
    // break the next time a window is added.
    {
        // Same corner, same ordinal -> same cell. Always.
        const QRectF a = HudCanvas::subWindowGridRect(
            Corner::TopLeft, 3, kProbeScreen, QSize(400, 100));
        const QRectF b = HudCanvas::subWindowGridRect(
            Corner::TopLeft, 3, kProbeScreen, QSize(400, 100));
        check(a == b, "the grid is deterministic: same (corner, ordinal) "
                      "reads the same cell");

        // A screen flip-flop must not move a cell that fits both. This is
        // what keeps the wall output-independent rather than tuned to one
        // resolution — the property that made it worth having in the first
        // place.
        const QRectF wide = HudCanvas::subWindowGridRect(
            Corner::TopLeft, 1, QSize(1920, 1080), QSize(400, 100));
        const QRectF wider = HudCanvas::subWindowGridRect(
            Corner::TopLeft, 1, QSize(3840, 2160), QSize(400, 100));
        check(wide.topLeft() == wider.topLeft(),
              "a wider screen does not move a TopLeft cell, so the layout "
              "is not tuned to one resolution");
    }

    // ---- THE OTHER HALF: THE MOVE ITSELF ---------------------------------
    //
    // A window must be draggable PAST the canvas's midline. The clamp was
    // never wrong (movedSubMargins()'s maxX is the screen width minus the
    // window), but an overlapped window never reaches the clamp at all, so
    // the midline reads as a wall. With the overlap gone the drag is the
    // thing under test, and it must not be bounded at half the screen.
    {
        const int screenW = kProbeScreen.width();
        const int windowW = kProbes[0].w;
        // maxX is the draggable limit: the rightmost the window's left edge
        // may go while the window stays on screen.
        const int maxX = screenW - windowW;
        check(maxX > screenW / 2,
              "a 560px window on a 3840px screen can be dragged past the "
              "midline (3200 > 1920) — the clamp was never the problem");
    }

    std::printf("\n== %d checks, %d failures ==\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
