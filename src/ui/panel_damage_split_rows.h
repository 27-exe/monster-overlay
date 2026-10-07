#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// DamageRowsStripPanel — the v0.15 DPS per-player rows sub-window.
//
// The fourth "block → window" of the damage split, and the one whose
// absence the user reported: with Share and Chart carved out of
// DamagePanel, the Console's split preview drew two boxes and nothing
// else, and "我们按玩家统计的那部分不见了" is exactly what that looks
// like from the outside. kDamageSplitTargets shipped two rows on the
// recorded grounds that "Rows is the DPS panel's SUBSTANCE"; that is
// true of the OVERLAY (the panel keeps drawing the rows) and false of
// the CONSOLE (which previews only the split's sub-windows), and the
// user was right that the result is a preview that lies.
//
// WHAT THIS WINDOW DOES NOT DO
// ----------------------------
// It does not take Rows away from DamagePanel. damagePanelMaskFor() is
// untouched by this change, so in Split mode the panel still draws
// rows exactly as it always has and the ten single-mode baselines
// cannot move. This window is a THIRD surface drawing the same block
// from the same model — the same relationship DamageShareStripPanel has
// with the share bar, where the panel and the window both draw and
// neither reads the other's state.
//
// That has one honest consequence, recorded here rather than
// discovered later: a real `--layout=split` overlay launch that wires
// this window would draw the rows twice (panel + window). Whether to
// wire it in main.cpp is a decision for the session that owns main.cpp;
// this class compiles, mounts, paints and persists on its own, and the
// Console's preview uses it unconditionally.

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

// The rows as their own layer-shell window, with their own panels.ini
// group ("damage-rows"). That key is chosen for the reason
// panel_damage_split_share.h gives for its own: reusing the panel's
// "dps" group would make two windows fight over one position / scale /
// opacity section.
class DamageRowsStripPanel : public Panel {
    Q_OBJECT

public:
    // `subWindowIndex` is this window's row in its parent's split table.
    // It arrives at construction time because the base Panel constructor
    // calls loadConfig(), and the index exists only to pick that call's
    // first-run default — see defaultMarginsForSubWindow() in panel.cpp.
    // A setter would always be one call too late.
    explicit DamageRowsStripPanel(QWidget *parent = nullptr,
                                  int subWindowIndex = 0);

    // Full snapshot, the same entry point DamagePanel has. The World
    // feed and the Rise feed both land in the ViewModel, so one
    // update() serves both.
    void update(const mhw::GameSnapshot &snap);

    // Rise feed. Same forwarding rule as the share and chart windows:
    // the feed's own row filter decides which actors become rows, and
    // this window does not own that policy.
    void updateRiseDamage(const mhw::RiseDamageSnapshot &dmg);

    // i18n: the title is the one cached string, set here and re-set by
    // main.cpp's applyLocale. The row names arrive on the snapshot and
    // are re-read from the reader every poll, so there is no demo label
    // to rebuild on a locale flip.
    void retranslateUi() override;

    // A window carved out of a panel declares itself a sub-window so
    // the console's slot registry keeps it out of the four fixed slots
    // the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

    // ─── Panel's polymorphic layout contract ────────────────────────
    //
    // Answered rather than left at the base's "I do not do that"
    // defaults, so a layout question — from the console's preview, from
    // main.cpp, or from a test — gets this window's real numbers
    // without knowing which window it is holding.

    // The rows' natural width: the panel's own 380, which is what the
    // block used to be. The user can narrow it (the name column
    // ellipsizes first, exactly as it does in the panel) or widen it
    // with the [ / ] columns dial and the size floor.
    [[nodiscard]] int contentWidth() const override;

    // Every row plus the split margin on both sides. The inter-row gaps
    // are inside blockHeight(); the margins are not, for the same
    // reason the share window gives — a hole at the top that nothing
    // draws into.
    [[nodiscard]] int contentHeight() const override;

    // The outer inset, the same kSplitMargin every other split window
    // answers.
    [[nodiscard]] int contentMargin() const override
    {
        return mhw::split_layout::kSplitMargin;
    }

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // "No data, no window", the split's shared rule: the mask bit is
    // read FIRST, then the demo exception, then emptiness. Edit mode is
    // the deliberate exception (and comes AFTER the mask, so a user who
    // switched the block off gets no preview surface for it either) — a
    // design-mode surface has to stay mounted and draggable, so once
    // the demo is seeded the answer is true even though the numbers are
    // synthetic.
    //
    // Note the emptiness rule is the OPPOSITE of the share bar's, on
    // purpose: DamageRowsModel::isEmpty() answers false only for an
    // EMPTY roster, because a party of four who have all hit 0 damage
    // is still four rows on screen. A bar with nothing to divide is a
    // bare grey track; a row list with nothing to name is nothing.
    bool hasContent() const override;

private:
    // The section mask is a separate axis from the master switch
    // (Panel::setPanelEnabled), exactly as in the player split's
    // windows: DamageSection::Rows is what the console's existing
    // damage-section toggle drives this window with, so a user who has
    // already turned the rows off in the single panel gets the same
    // layout here with no new console UI.
    [[nodiscard]] bool rowsEnabled() const;

    // The window's own demo seed: the same four-row party the panel
    // seeds, resolved through this window's own ViewModel so the model
    // this window paints is the model a test can build.
    void seedDemoModel();

    // One DamageViewModel, exactly like DamagePanel's. It is this
    // window's own copy for the reason panel_damage_split_share.h gives:
    // the panel and the window can be handed different feeds (main.cpp
    // calls update() on both) and each paints the model it was given.
    mhw::DamageViewModel m_vm;
    // The resolved rows, cached from update()/setupDemoData() the way
    // the share window caches its bar: paintPanel() runs off the paint
    // thread's schedule, not the poll's, so the model is built once per
    // update and the paint side stays free of that timing.
    mhw::DamageRowsModel rows_{};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it (and the model with it) so a
    // demo preview can never survive into a live session.
    bool demoData_{false};
};
