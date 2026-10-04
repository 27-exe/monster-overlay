#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// PlayerSharpnessStripPanel — the sharpness gauge as its own window.
//
// ---------- WHY THIS FILE EXISTS, AND WHY NOW INSTEAD OF v0.12 ----------
//
// v0.12's weapon split (abaa70e) deliberately left the gauge behind.
// Its own words: "the partial extraction by design — the row in
// the source panel is 352px and the gauge alone is 174px of hard cost
// that buys nothing in its own window". That reasoning was about WEIGHT,
// and it was right about weight. What it could not see is what the
// split did to the leftover.
//
// Take Sharpness out of the table and PlayerPanel's mask in Split mode
// still holds it, so the panel is left with exactly ONE block: the
// 174px gauge inside a 378px panel that also always draws a title row
// ("title always drawn", `totalH = kMargin + kTitleH`). The user's
// verdict on that arrangement is the second question in the same
// report as the missing background: 「1所说的这个面板真有存在必要么？」
// — i.e. "PlayerPanel has been reduced to one bar; is there any point
// keeping it?" The honest answer is no, which means the gauge has to
// leave too, and then PlayerPanel has nothing left to draw.
//
// SO: this window makes the split TOTAL. In Split mode every
// PlayerSection bit is now claimed by kPlayerSplitTargets, PlayerPanel's
// derived mask is 0x00000000, and its `hasContent()` returning true is
// the only thing that kept a window on screen — which the main loop
// gates behind `panelEnabled()`, and which Split mode now takes away
// from it (see main.cpp's wiring notes in the report). Single mode is
// untouched and draws the gauge exactly where it drew it before.
//
// WHAT IS NOT HERE, AND WHAT THIS FILE DELIBERATELY DOES NOT DO
// ------------------------------------------------------------
// No local copy of the painter. `drawPlayerSharpnessBar()` in
// ui/panel_player_sharpness.{h,cpp} is already a free function with no
// `this` and no PlayerPanel knowledge — that is the whole reason this
// window could be written at all without touching the file being
// migrated away from. It is called, not copied. (The six windows before
// this one each had to re-implement their block's painter because those
// live in panel_player.cpp's anonymous namespace; sharpness is the
// first block that was already extracted, and not duplicating it is the
// first concrete payoff of that move.)
//
// No title row, no chrome: chromeVisible(false) + isSubWindow(), the
// same two declarations the other six make. The gauge keeps its own
// surface now that v0.12.1 decoupled the background from the frame
// (Panel::drawSubWindowBody), so a fragment gets a translucent body
// without a stripe and a border.

#include "panel.h"
#include "core/game_snapshot.h"
#include "player/player_types.h"
#include "ui/panel_player_sharpness.h"
#include "ui/panel_sections.h"

#include <QPainter>
#include <QString>

// The sharpness gauge as its own layer-shell window, with its own
// panels.ini group ("player-sharpness" — NOT "player": reusing it would
// make the window and the panel it came from fight over one position,
// scale and opacity section, and on a machine that has already dragged
// the panel around (the shipped panels.ini carries [player] with real
// margins) the fragment would be born at the panel's coordinates).
class PlayerSharpnessStripPanel : public Panel {
    Q_OBJECT

public:
    explicit PlayerSharpnessStripPanel(QWidget *parent = nullptr);

    // Full snapshot. Caches exactly the one field the painter reads,
    // mirroring PlayerPanel's `player_` + update() pattern. Every other
    // mirrored field (name, MR, zone, quest, vitals, wirebugs) is read
    // by neither this window nor the panel — that is the whole point of
    // the split being total.
    void update(const mhw::GameSnapshot &snap);

    // i18n: this window caches no string of its own (the only text it
    // draws is the numeric badge, produced by QString::number), so the
    // base class's title-only repaint is the whole override. Declared
    // anyway so the wiring in main.cpp is the same six lines as the
    // other windows rather than one special case, and so a future
    // placeholder string has a place to go.
    void retranslateUi() override;

    // A window carved out of PlayerPanel declares itself a sub-window so
    // the console's slot registry keeps it out of the four fixed slots
    // the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // "No data, no window": a ranged weapon (SharpnessSnapshot::valid
    // false) and a broken weapon (level -1) are both real live states in
    // which the gauge legitimately draws nothing, and HunterPie's rule
    // says the window must not exist then either — the main loop's
    // hasVisibleContent() answers false and the surface is never
    // mounted. Edit mode is the deliberate exception: a design-mode
    // surface has to stay mounted and draggable, so once the demo is
    // seeded the answer is true even though the hunter is synthetic.
    bool hasContent() const override;

private:
    // The section-mask gate, in the same role barsEnabled() /
    // weaponEnabled() play for their windows. Reads `Sharpness` and
    // nothing else — this window claims exactly one bit.
    [[nodiscard]] bool sharpnessEnabled() const;

    // Content height, in logical px: the row's own kWeaponGap +
    // kWeaponH plus the margin at each end (kMargin + 9 + 26 + kMargin
    // = 57), so a gauge drawn beside what the panel drew is the same
    // height at the same distance from the window's top edge.
    [[nodiscard]] int contentHeight() const;

    // Content width, in logical px: the [gauge 130 + gap 14 + badge 30]
    // block the exported sharpness TU measures, plus the margin on both
    // sides. Never 378 — that was PlayerPanel's width and belongs to it.
    [[nodiscard]] int contentWidth() const;

    // The one field the painter reads. Copied from the snapshot rather
    // than the snapshot kept: GameSnapshot is a large aggregate (party,
    // monsters, damage) and this window needs 60 bytes of it.
    mhw::SharpnessSnapshot sharpness_{};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it (and the row with it) so a demo
    // preview can never survive into a live session.
    bool demoData_{false};
};
