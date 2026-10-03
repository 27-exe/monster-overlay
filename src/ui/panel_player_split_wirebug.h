#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// PlayerWirebugStripPanel — the v0.12 Rise wirebug sub-window.
//
// Fourth piece of the v0.12 "block → window" split of PlayerPanel (see
// panel_player_split.h for the pilot and the pattern's two promises, and
// panel_player_split_bars.h / panel_player_split_mantles.h for the two
// written in the same step). This one mirrors PlayerPanel's Wirebug
// block: the capsule row of wirebugs currently recovering — those whose
// cooldown is still running. panel_player.cpp:1142-1168.
//
// RISE ONLY, AND WHY THAT IS "NO CONTENT" RATHER THAN A BRANCH
// --------------------------------------------------------------
// World has no wirebug system: PlayerPanel gates the same block on
// `(smask & PlayerSection::Wirebug) && game_ == GameId::Rise`
// (panel_player.cpp:675-677) and the World reader leaves
// PlayerSnapshot::wirebugs empty. So under World this window's model is
// empty and hasContent() answers false — the window is simply never
// mounted, which is the same no-data-no-window mechanism the strip uses
// for an empty pill list. No `if (game == World)` special case is needed
// here and none is written: the emptiness is a property of the snapshot,
// not of this file.
//
// READY WIREBUGS ARE NOT DRAWN
// -----------------------------
// The panel renders only entries whose cooldown is still running
// (panel_player.cpp:629-638, v0.7.2) — a ready wirebug contributes no
// capsule, so the row shrinks as the hunter's wirebugs come back and
// collapses entirely when every one of them is ready. That is the
// behaviour this window reproduces: the model is the recovering subset,
// in source order, and an all-ready hunter produces an empty model.
// "冷却完了直接消失" is the user-facing rule it encodes.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row — the same as the other split windows. The
// capsule painter below is this file's own copy of drawWirebug()
// (panel_player.cpp:394-459), written out in full on purpose: that one
// lives in an anonymous namespace in another translation unit, and
// hoisting it into a shared header would mean editing a file this change
// may not touch. The copy names the source line it mirrors.

#include "panel.h"
#include "core/game_snapshot.h"
#include "player/player_types.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QString>
#include <QVector>

namespace mhw {

// One wirebug capsule's resolved display state. `slot` is the source
// slot the capsule belongs to (WirebugSnapshot::slot) — kept in the model
// rather than the label, because the label is resolved at paint time from
// the locale pair, exactly as panel_player.cpp:1161-1162 resolves it, so
// a language switch lands on the next paint without a model rebuild.
struct WirebugRow {
    int   slot{-1};          // source slot: 翔虫中默认槽, 1..cap = extras, else corrupt
    float cooldown{0.0F};
    float maxCooldown{0.0F};
    // Environment / skill wirebugs get a thinner accent stripe and a
    // slightly muted colour, so the default wirebug is still readable at
    // a glance (panel_player.cpp:405-409).
    bool    temporary{false};
    // cooldown / maxCooldown, already clamped to [0,1].
    float   progress{0.0F};

    [[nodiscard]] bool ready() const noexcept { return cooldown <= 0.001F; }
};

// The amount of cooldown left below which a wirebug is considered ready
// and therefore drawn not at all. panel_player.cpp:1158 uses the same
// 0.001F epsilon on both the count gate and the per-entry skip.
[[nodiscard]] inline constexpr float wirebugReadyEpsilon()
{
    return 0.001F;
}

// The recovering capsules in draw order.
struct WirebugModel {
    QVector<WirebugRow> capsules;

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return capsules.isEmpty();
    }
};

// The pure transformation: which wirebugs are still recovering, and their
// display state.
//
// Neither game arm filters anything by itself — the entries that survive
// are the ones the PASSING game's reader actually published, which is the
// same data fact that empties the mantle row under Rise. The switch with
// no default arm is the seam that makes a third game's answer explicit
// (-Wswitch, the convention panel_player_split.cpp and this step's other
// two windows use).
[[nodiscard]] WirebugModel buildWirebugModel(const mhw::GameSnapshot &snap);

} // namespace mhw

// Recovering-wirebug capsules as their own layer-shell window, with its
// own panels.ini group ("player-wirebug", NOT "player" or "player-status"
// — reusing either would make two windows fight over one position /
// scale / opacity section).
class PlayerWirebugStripPanel : public Panel {
    Q_OBJECT

public:
    explicit PlayerWirebugStripPanel(QWidget *parent = nullptr);

    // Full snapshot. Caches the resolved capsules for paintPanel() to
    // read, mirroring PlayerPanel's `player_` member + update() pattern.
    void update(const mhw::GameSnapshot &snap);

    // i18n: re-query the cached window title. The demo path carries no
    // labels of its own (the demo slots are numbered, not named, and the
    // numbers come from the same wirebugSlotLabel() the painter calls),
    // so unlike the status strip's demo seed there is nothing here that a
    // locale flip would leave stale.
    void retranslateUi() override;

    // A window carved out of PlayerPanel declares itself a sub-window so
    // the console's slot registry keeps it out of the four fixed slots
    // the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // "No data, no window": empty under World (no wirebug system, so the
    // reader publishes nothing) and when every wirebug is ready. The main
    // loop's hasVisibleContent() answers false and the layer-shell
    // surface is never mounted. Edit mode is the deliberate exception: a
    // design-mode surface has to stay mounted and draggable, so once the
    // demo is seeded the answer is true even though the capsules are
    // synthetic.
    bool hasContent() const override;

private:
    [[nodiscard]] bool wirebugEnabled() const;
    [[nodiscard]] int  contentWidth() const;
    // Height the capsule row needs, in logical px. The panel reserves
    // kWirebugGap + kWirebugH = 9 + 18 (panel_player.cpp:752-753); this
    // window keeps the same 18 so a capsule drawn side by side with the
    // panel's is the same size.
    [[nodiscard]] int  contentHeight() const;

    mhw::WirebugModel rows_{};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it (and the rows with it) so a demo
    // preview can never survive into a live session.
    bool              demoData_{false};
};
