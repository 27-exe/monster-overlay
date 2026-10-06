#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// DamageChartStripPanel — the v0.12 third wave's line-chart sub-window.
//
// The same "block → window" split PlayerPanel took in v0.12
// (panel_player_split.h is the pilot and carries the pattern's two
// promises), applied to the DPS panel's four-polyline accumulation
// chart — panel_damage.cpp:432-434. Its paint code was NOT copied: it
// is now shared through ui/panel_damage_blocks.h.
//
// WHY THIS BLOCK AND NOT ANOTHER ONE
// ----------------------------------
//   * the chart was already a one-caller member function (drawChart)
//     with no dependency on any other part of the panel — the shape
//     that splits cleanly;
//   * it is the block the WIDTH most starves. In the panel the chart
//     gets kPanelW - 2*kMargin = 358px, and kChartLeft eats 36 of that
//     for the y-axis labels, so the plot area is 318px for four curves.
//     As its own window the user can give the window any width (the [
//     / ] columns dial, the size floor, the lock) and the plot area
//     grows with it — the axis stays 36px and the curves get the rest,
//     which is the opposite of what the panel could do;
//   * the chart's height is already fixed at 140 (HTML .chart) and its
//     aspect is arbitrary, so widening it changes nothing about how
//     readable the numbers are; it only gives the curves more room to
//     separate, which is the one thing four same-ish growth curves need.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row: a chart is a fragment of the damage panel,
// not a panel. The constructor calls setChromeVisible(false) and
// isSubWindow() answers true. The chart's body (the grid, the axis
// labels, the polylines) is shared with DamagePanel through
// ui/panel_damage_blocks.h — see that header for the normalization the
// width change is built on, and for why the code was hoisted rather
// than duplicated.

#include "panel.h"
#include "core/game_snapshot.h"
#include "ui/panel_damage_blocks.h"
#include "ui/viewmodel/damage_view_model.h"

// The shared split rhythm. Included here rather than only in the .cpp
// because this header answers contentMargin() from it: an inline
// member that names a constant it does not include compiles until
// someone includes the header first, and then stops.
#include "ui/panel_player_split_layout.h"

#include <QPainter>

// The line chart as its own layer-shell window, with its own panels.ini
// group ("damage-chart", NOT "dps" — reusing the damage panel's group
// would make two windows fight over one position / scale / opacity
// section).
class DamageChartStripPanel : public Panel {
    Q_OBJECT

public:
    // `subWindowIndex` is this window’s row in its parent’s split table. It
    // arrives at construction time because the base Panel constructor calls
    // loadConfig(), and the index exists only to pick that call’s first-run
    // default — see defaultMarginsForSubWindow() in panel.cpp. A setter
    // would always be one call too late.
    explicit DamageChartStripPanel(QWidget *parent = nullptr, int subWindowIndex = 0);

    // Full snapshot, the same entry point DamagePanel has. The World
    // feed and the Rise feed both land in the ViewModel, so one update()
    // serves both.
    void update(const mhw::GameSnapshot &snap);

    // Rise feed. Forwarded verbatim (see the sibling share window for
    // why) so the panel and the window cannot disagree about which
    // actors became rows.
    void updateRiseDamage(const mhw::RiseDamageSnapshot &dmg);

    // i18n: the title is the one cached string; the chart's wait
    // placeholder is read at draw time from the shared table, and the
    // demo path seeds numbers, so there is nothing else to rebuild on
    // a locale flip.
    void retranslateUi() override;

    // A window carved out of a panel declares itself a sub-window so
    // the console's slot registry keeps it out of the four fixed slots
    // the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

    // ─── Panel's polymorphic layout contract ────────────────────────
    //
    // Answered rather than left at the base's "I do not do that"
    // defaults, so a layout question gets this window's real numbers.

    // The chart's own width. 358 is the width of the chart's slot in
    // DamagePanel (panel_damage.cpp:434) and is the number the user was
    // getting before, so the window starts there and every dial from
    // there is a change they chose.
    [[nodiscard]] int contentWidth() const override;

    // The panel's height hook. The chart's own 140 plus the margin on
    // both sides. The 4px gap the panel put before the chart belongs to
    // the PANEL's layout, not to the block.
    [[nodiscard]] int contentHeight() const override
    {
        return 2 * mhw::split_layout::kSplitMargin
               + mhw::damage_block::kChartH;
    }

    // The outer inset, the same kSplitMargin every other split window
    // answers.
    [[nodiscard]] int contentMargin() const override
    {
        return mhw::split_layout::kSplitMargin;
    }

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // "No data, no window", the split's shared rule: mask bit first, then
    // the demo exception, then emptiness. An empty history means the
    // chart would draw only its background and a centred placeholder, and
    // a placeholder is not a reason to mount a surface. Edit mode is the
    // deliberate exception (again AFTER the mask) — a design-mode surface
    // has to stay mounted and draggable — so once the demo is seeded the
    // answer is true even though the curves are synthetic.
    bool hasContent() const override;

private:
    // DamageSection::Chart, the same bit DamagePanel reads for this
    // block (panel_damage.cpp:215). The mask axis is separate from the
    // master switch, so the console's existing damage-section toggle
    // drives this window with no new console UI.
    [[nodiscard]] bool chartEnabled() const;

    // The window's own demo seed: the same four-row party the panel's
    // setupDemoData() seeds, so the preview's curves match the block it
    // came from.
    void seedDemoModel();

    // One DamageViewModel, exactly like DamagePanel's, for the same
    // reasons the sibling share window carries one.
    mhw::DamageViewModel m_vm;
    // The resolved chart, cached from update()/setupDemoData().
    mhw::DamageChartModel rows_{};
    // True while what is on screen came from setupDemoData().
    bool demoData_{false};
};
