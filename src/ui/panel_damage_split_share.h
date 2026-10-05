#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// DamageShareStripPanel — the v0.12 third wave's share-bar sub-window.
//
// The same "block → window" split PlayerPanel took in v0.12
// (panel_player_split.h is the pilot and carries the pattern's two
// promises), applied to the DPS panel's 1px-abreast horizontal share
// bar — panel_damage.cpp:422-430. Its paint code was NOT copied: it is
// now shared through ui/panel_damage_blocks.h, so the panel and this
// window draw one expression each.
//
// WHY THIS BLOCK AND NOT ANOTHER ONE
// ----------------------------------
//   * the bar was already a one-caller member function
//     (drawShareBar) with no dependency on any other part of the panel
//     — the shape that splits cleanly;
//   * its aspect is the one thing a window can fix: in the panel it is
//     pinned to the panel's 380px width, so with four players each
//     segment is ~74px wide and reads as "four coloured strips in a
//     row". As its own window the user can narrow it (segments shrink,
//     the bar becomes a compact budget indicator) or widen it (segments
//     become readable colour fields) with the [ / ] columns dial and
//     the size floor, neither of which the panel could offer;
//   * it is the only block whose WIDTH is arbitrary by construction: a
//     share bar has no natural width at all, it is whatever the parent
//     panel happened to be. That is the exact property this split
//     exists to hand to the user.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row: a share bar is a fragment of the damage
// panel, not a panel. The constructor calls setChromeVisible(false)
// and isSubWindow() answers true. The per-row palette and the bar's
// body are shared with DamagePanel through
// ui/panel_damage_blocks.h — see that header for why they were hoisted
// rather than duplicated.

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

// The share bar as its own layer-shell window, with its own panels.ini
// group ("damage-share", NOT "dps" — reusing the damage panel's group
// would make two windows fight over one position / scale / opacity
// section).
class DamageShareStripPanel : public Panel {
    Q_OBJECT

public:
    explicit DamageShareStripPanel(QWidget *parent = nullptr);

    // Full snapshot, the same entry point DamagePanel has. The World
    // feed and the Rise feed both land in the ViewModel, so one update()
    // serves both.
    void update(const mhw::GameSnapshot &snap);

    // Rise feed. DamagePanel owns three update entry points because it
    // also owns the Rise display options; this window keeps the same
    // shape so main.cpp can treat the two identically when it wires the
    // riseDamageReaders loop — but it reads no options itself, and
    // forwards the request verbatim so the two can never disagree
    // about what the feed said.
    void updateRiseDamage(const mhw::RiseDamageSnapshot &dmg);

    // i18n: the title is the one cached string, set here and re-set by
    // main.cpp's applyLocale. Everything else the bar draws is a colour,
    // not a word, so there is no demo label to rebuild on a locale flip.
    void retranslateUi() override;

    // A window carved out of a panel declares itself a sub-window so
    // the console's slot registry keeps it out of the four fixed slots
    // the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

    // ─── Panel's polymorphic layout contract ────────────────────────
    //
    // Answered rather than left at the base's "I do not do that"
    // defaults, so a layout question — from the console's preview, from
    // main.cpp, or from the split consistency test — gets this window's
    // real numbers without knowing which window it is holding.

    // The bar's natural width: the panel's own 380, minus its margins,
    // plus this window's. The panel reserving 380 for the bar's slot is
    // the number this window starts from so the two agree about how
    // much bar the user was getting before.
    [[nodiscard]] int contentWidth() const override;

    // The bar's height plus the margin on both sides. The 9px gap the
    // panel put above the bar belongs to the PANEL's layout, not to the
    // block, so it is deliberately not reserved here — a window that
    // carried it would have a hole at the top that nothing draws into.
    [[nodiscard]] int contentHeight() const override
    {
        return 2 * mhw::split_layout::kSplitMargin
               + mhw::DamageShareModel::blockHeight();
    }

    // The outer inset, the same kSplitMargin every other split window
    // answers. Reported so the ring can be asked instead of several
    // private constants being trusted to agree.
    [[nodiscard]] int contentMargin() const override
    {
        return mhw::split_layout::kSplitMargin;
    }

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // "No data, no window", the split's shared rule: the mask bit is read
    // FIRST, then the demo exception, then emptiness. An all-zero party
    // draws a bare grey track, and a bare track is not a reason to mount
    // a surface: the main loop's hasVisibleContent() answers false and
    // the layer-shell surface is never created. Edit mode is the
    // deliberate exception (and comes AFTER the mask, so a user who
    // switched the block off gets no preview surface for it either) — a
    // design-mode surface has to stay mounted and draggable, so once the
    // demo is seeded the answer is true even though the numbers are
    // synthetic.
    bool hasContent() const override;

private:
    // The section mask is a separate axis from the master switch
    // (Panel::setPanelEnabled), exactly as in the player split's
    // windows: DamageSection::Share is what the console's existing
    // damage-section toggle drives this window with, so a user who has
    // already turned the share bar off in the single panel gets the
    // same layout here with no new console UI.
    [[nodiscard]] bool shareEnabled() const;

    // The window's own demo seed. Same idea as the panel's: four
    // synthetic damage values in the same proportions the panel seeds,
    // so the preview looks like the block it came from.
    void seedDemoModel();

    // One DamageViewModel, exactly like DamagePanel's. It is this
    // window's own copy so the panel and the window can be handed
    // different feeds (main.cpp calls update() on both) and so the
    // model this window paints is the model the test can build.
    mhw::DamageViewModel m_vm;
    // The resolved bar, cached from update()/setupDemoData() the way
    // DamagePanel caches nothing and reads its VM at paint time. The
    // cache exists because paintPanel() runs off the paint thread's
    // schedule, not the poll's, and building the model once per poll
    // keeps the paint side free of that timing.
    mhw::DamageShareModel rows_{};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it (and the model with it) so a
    // demo preview can never survive into a live session.
    bool demoData_{false};
};
