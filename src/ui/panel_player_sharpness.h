#pragma once

class QPainter;

namespace mhw {
struct SharpnessSnapshot;
}

// The player panel's sharpness gauge — a 7-segment coloured bar plus a
// numeric badge, mounted on the right edge of the weapon row.
//
// This block used to live at the bottom of `src/ui/panel_player.cpp`
// (lines 542-698, 157 lines): its banner comment, the `kSharpColors`
// palette, the four `kShp*` layout tokens, and the `drawSharpnessBar()`
// definition itself. It moved here 1:1, so the pixel output is unchanged.
//
// Why it moves as a whole: `kSharpColors` is referenced only by
// `drawSharpnessBar()`, so leaving the palette behind would trip
// `-Wunused-const-variable` in panel_player.cpp (the anonymous namespace
// closed at old line 564, i.e. nothing else in that file reads it).
//
// It is a free function with no `this`, no member access and no
// `PlayerPanel` knowledge of its own: in comes a `SharpnessSnapshot`, out
// go pixels plus the gauge's left X. That is why the header needs no
// `friend` and no `#include "panel_player.h"` back-reference.

// Returns the gauge's left X on success (so the caller can trim
// the MR/name rect to avoid overlap). Returns `rightX` (no shrink)
// when the snapshot is invalid / ranged weapon — caller keeps the
// full row width for MR/name.
int drawPlayerSharpnessBar(QPainter &p, const mhw::SharpnessSnapshot &s,
                           int rightX, int rowY, int rowH);

// Total pixel width the [gauge + gap + badge] block occupies, the same
// number the panel's centering math used to recompute out of the two
// now-private `kShpGap` / `kShpNum` tokens plus its own copy of the
// 130px gauge width. Exported so that the tokens stay defined in
// exactly one place — next to the body that draws with them — and the
// caller cannot drift out of sync with it.
int playerSharpnessBarTotalWidth();
