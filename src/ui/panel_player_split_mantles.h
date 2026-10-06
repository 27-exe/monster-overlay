#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// PlayerMantlesStripPanel — the v0.12 World mantle sub-window.
//
// Third piece of the v0.12 "block → window" split of PlayerPanel (see
// panel_player_split.h for the pilot and the pattern's two promises, and
// panel_player_split_bars.h for the bars pair written in the same step).
// This one mirrors PlayerPanel's Mantles block: the two equipped-mantle
// boxes with their icon, localized name, remaining/cooldown seconds and
// vertical progress strip — panel_player.cpp:1100-1140.
//
// WORLD ONLY, AND WHY THAT IS "NO CONTENT" RATHER THAN A BRANCH
// --------------------------------------------------------------
// Rise has no mantle system: PlayerPanel gates the same block on
// `(smask & PlayerSection::Mantles) && game_ != GameId::Rise`
// (panel_player.cpp:646-650) and Rise leaves the two mantleSlot* fields at
// their -1 / 270.0F defaults. So under Rise this window's model is empty
// and hasContent() answers false — the window is simply never mounted,
// which is the same no-data-no-window mechanism the strip uses for an
// empty pill list. No `if (game == Rise)` special case is needed here and
// none is written: the emptiness is a property of the snapshot, not of
// this file.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row — the same as the other two split windows. The
// mantle-box painter below is this file's own copy of drawMantleBox()
// (panel_player.cpp:239-323), written out in full on purpose: that one
// lives in an anonymous namespace in another translation unit, and
// hoisting it into a shared header would mean editing a file this change
// may not touch. The copy names the source line it mirrors.

#include "panel.h"
#include "core/game_snapshot.h"
#include "player/player_types.h"
#include "panel_player_split_layout.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPixmap>
#include <QString>
#include <QVector>

// The mantle row's layout tokens. The dial's inline accessors and the
// wrapped painter both read them, so they live in the header rather
// than the .cpp's anonymous namespace — a duplicated copy is a second
// spelling of one box, which is exactly how a strip ends up reserving
// one geometry and drawing another.
// See the note on kWirebugMargin: file-scope constants shared by name
// across two moc-bearing headers collide when AUTOMOC merges them
// into one translation unit.
inline constexpr int kMantlesMargin = mhw::split_layout::kSplitMargin;
inline constexpr int kMbH        = 60;   // .mb svg ~26px + cn + tm
                                         // (panel_player.cpp:729)
// Both gaps now read the split's shared within-block value. What were
// two window-local 8s are the same token the wirebug window reads, so a
// stacked mantle pair and a wrapped wirebug row are pitched alike — the
// one difference the user could see as "these are two different
// systems" and that this change exists to remove.
inline constexpr int kMbGap      = mhw::split_layout::kSplitGapWithin;
inline constexpr int kMantleRowGap = mhw::split_layout::kSplitGapWithin;

// The default dial position and range. 2 = the historical two-box row;
// 1 = the two boxes stacked vertically (the vertical layout the user
// asked for). A mantles window holds at most two slots, so the ceiling
// is 2 and the floor is 1 — there is no "zero columns" state, and
// nothing above 2 to wrap into.
inline constexpr int kMantlesColumns = 2;
inline constexpr int kMantlesMaxColumns = 2;

// The floor the draw side puts under one box's slot when a locked
// budget is narrower than the box's natural width. Below this the
// icon (32px) plus the strip (10px) plus padding no longer fit and the
// box starts cropping its own furniture.
inline constexpr int kMinMantleBoxW = 60;

namespace mhw {

// One mantle slot's resolved display state. `id` is the in-game item id
// (<0 = nothing equipped), and every other field is the already-resolved
// display value: the name comes from mhw::mantleName() (the localized
// data.mantle.id.<n> table, NOT a UI-derived string — see
// core/string_table.h), the timer from the same "%1s" formatting the
// panel uses, and progress from the same two formulas the panel uses
// (active: timer / kMantleMaxSec; cooling: 1 - cooldown / cooldownMax).
struct MantleRow {
    int     id{-1};
    bool    active{false};   // timer > 0 == currently running, teal border
    QString name;            // localized mantle name
    QString timer;           // "42s" remaining while active, "96s" while cooling
    float   progress{0.0F};  // 0..1, the vertical strip's fill
};

// A stable denominator for the active-mantle progress strip. The panel
// uses 120s (a common mantle active duration, panel_player.cpp:61-63).
[[nodiscard]] inline constexpr float mantleProgressDenominator()
{
    return 120.0F;
}

// The two slots in draw order.
struct MantleModel {
    QVector<MantleRow> boxes;

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return boxes.isEmpty();
    }
};

// The pure transformation: which of the two slots have a mantle equipped,
// with their resolved display state.
//
// Deliberately dispatched on GameId with no default arm. Today both arms
// agree, because the World reader is the only one that fills
// mantleSlot*Id and the fields are already game-agnostic; the switch is
// the seam that makes a third game's answer explicit rather than an
// inherited one (-Wswitch, the same convention
// panel_player_split.cpp's buildStatusStripModel uses). Rise collapsing
// the block is then a data fact, not a UI special case.
[[nodiscard]] MantleModel buildMantleModel(const mhw::GameSnapshot &snap);

} // namespace mhw

// Two equipped-mantle boxes as their own layer-shell window, with its own
// panels.ini group ("player-mantles", NOT "player" or "player-status" —
// reusing either would make two windows fight over one position / scale /
// opacity section).
class PlayerMantlesStripPanel : public Panel {
    Q_OBJECT

public:
    [[nodiscard]] int  itemCount() const override { return rows_.boxes.size(); }
    [[nodiscard]] int  naturalItemWidth() const override;
    [[nodiscard]] int  itemGap() const override { return kMbGap; }
    // The fourth of the set, and the one whose absence is invisible until
    // a layout asks: without it the window answers the base's 0, so a
    // vertical pair wraps at a zero pitch. supportsColumns() above is
    // true, which is exactly why all four have to be present.
    [[nodiscard]] int  rowPitch() const override { return kMbH + kMantleRowGap; }
    void paintPanel(QPainter &p) override;
    [[nodiscard]] bool supportsColumns() const override { return true; }

    // Columns per row from the width this window will ACTUALLY be, so a
    // lock narrows the layout instead of being ignored.
    [[nodiscard]] int  wrappedColumns() const;

    [[nodiscard]] int  contentMargin() const override { return kMantlesMargin; }

    [[nodiscard]] int contentWidth() const override;
    [[nodiscard]] int contentHeight() const override;
    [[nodiscard]] int  columns() const override { return columns_; }
    // `subWindowIndex` is this window’s row in its parent’s split table. It
    // has to arrive at construction time (not via a setter) because
    // loadConfig() runs inside the base constructor and the index only
    // affects that call’s first-run default — see
    // defaultMarginsForSubWindow() in panel.cpp.
    explicit PlayerMantlesStripPanel(QWidget *parent = nullptr, int subWindowIndex = 0);

    // Full snapshot. Caches the resolved mantles for paintPanel() to read,
    // mirroring PlayerPanel's `player_` member + update() pattern.
    void update(const mhw::GameSnapshot &snap);

    // i18n: re-query the cached window title (the demo path carries no
    // labels of its own — the demo ids resolve through the localized
    // data.mantle.id.<n> table at build time, like a live read does — so
    // there is no seeded string that a locale flip would leave stale) and
    // schedule a repaint. Paint-time tr() follows on its own.
    void retranslateUi() override;

    // A window carved out of PlayerPanel declares itself a sub-window so
    // the console's slot registry keeps it out of the four fixed slots
    // the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

    // The columns dial survives an exit. Not virtual and not an override
    // for the reason PlayerStatusStripPanel's header gives for the same
    // pair (panel_player_split.h): Panel's versions are plain
    // non-virtual members reached through Panel*, so these hide rather
    // than extend them and both bodies call the base explicitly.
    //
    // main.cpp is the caller, exactly as it is for the strip.
    void loadConfig();
    void saveConfig();

    // v0.13: the game this window's demo seed represents. Mantles are a
    // World system, so the World arm seeds both slots; the Rise arm seeds
    // nothing and buildMantleModel answers empty (see the switch in
    // panel_player_split_mantles.cpp). This is what stops a Rise preview
    // from claiming mantle data a Rise session cannot have.
    //
    // Same shape and same caller as PlayerStatusStripPanel::setGameForDemo.
    void setGameForDemo(mhw::GameId game);

protected:

    // The row-layout vocabulary, same contract as the wirebug row's.
    void setupDemoData() override;


    // "No data, no window". Empty under Rise (no mantle system, so
    // neither slot ever resolves), and empty whenever both slots are
    // unequipped — the main loop's hasVisibleContent() answers false and
    // the layer-shell surface is never mounted. Edit mode is the
    // deliberate exception: a design-mode surface has to stay mounted
    // and draggable, so once the demo is seeded the answer is true even
    // though the boxes are synthetic.
    bool hasContent() const override;

private:
    [[nodiscard]] bool mantlesEnabled() const;
    // Height the two boxes need, in logical px. Both boxes are one row;
    // kMbH is the same 60px the panel reserves (panel_player.cpp:729).

    // ─── the columns dial (v0.13) ─────────────────────────────────────
    //
    // Same promotion as the wirebug row: the `[` / `]` keys are handled
    // once in Panel::keyPressEvent and routed through the base class's
    // virtuals, so no subclass owns a key it then has to forward.
    //
    // The default is kMantlesColumns (=2): the historical two-box row.
    // Pressing `[` gives 1, which stacks the two boxes vertically —
    // "衣装两格竖排 = 上下堆叠", the other half of the direction the
    // user asked for.
    void nudgeColumns(int delta) override;

    mhw::MantleModel rows_{};
    // v0.13: the game the demo below seeds. Defaults to World, which is
    // the only game whose arm produces boxes; main.cpp overwrites it
    // before the first paint.
    mhw::GameId     game_{mhw::GameId::World};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it (and the rows with it) so a demo
    // preview can never survive into a live session.
    bool             demoData_{false};
    // The dial position: 2 boxes side by side (default), or 1 = stacked.
    int              columns_{kMantlesColumns};
};
