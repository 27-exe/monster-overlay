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

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPixmap>
#include <QString>
#include <QVector>

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
    explicit PlayerMantlesStripPanel(QWidget *parent = nullptr);

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

protected:
    void paintPanel(QPainter &p) override;
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
    [[nodiscard]] int  contentWidth() const;
    // Height the two boxes need, in logical px. Both boxes are one row;
    // kMbH is the same 60px the panel reserves (panel_player.cpp:729).
    [[nodiscard]] int  contentHeight() const;

    mhw::MantleModel rows_{};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it (and the rows with it) so a demo
    // preview can never survive into a live session.
    bool             demoData_{false};
};
