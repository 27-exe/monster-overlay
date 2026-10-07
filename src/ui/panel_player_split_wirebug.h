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
// The shared split rhythm. The .cpp already includes it for its own use;
// the header needs it too because kPillRowGap below is an alias of a
// constant in it, and an alias in a header whose include moved elsewhere
// compiles until someone includes this header first.
#include "ui/panel_player_split_layout.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QString>
#include <QVector>

// The wirebug row's layout tokens. These are the four numbers the
// generic column dial needs (item width, gap, row pitch, margin) and
// they are the SAME values panel_player.cpp:752-753 reserved, so a
// capsule drawn here is the same size as the panel's. They live in the
// header rather than the .cpp's anonymous namespace because the dial's
// inline accessors read them, and duplicating them as header constants
// would let the two spellings drift.
inline constexpr int kPillH      = 18;   // HTML: .pill height (panel_player.cpp:753)
// The between-row pitch, aliased to the shared grade rather than spelled
// with its own 4: the split now has ONE "inside a block" gap, and this
// window claimed it was 4 while every other window said 7 — which is the
// kind of difference a user reads as "these are two different systems".
inline constexpr int kPillRowGap = mhw::split_layout::kSplitGapWithin;
inline constexpr int kPillGap    = 5;    // horizontal gap between capsules on a row
// Named for this window rather than the shared `kMargin`: AUTOMOC
// compiles every moc-bearing header into ONE translation unit, so a
// file-scope name in two headers is a redefinition at build time even
// though each header is only ever included alone elsewhere.
inline constexpr int kWirebugMargin = 11;   // HTML: .op padding:11

// The dial's default and range. The DEFAULT IS THE FULL ROW — every
// capsule on one row — because that is the layout this window shipped
// with and the one PlayerPanel shows: `gridContentWidth(n, n, itemW,
// gap)` reproduces the historical reservation exactly, and any other
// default would move the window on the first paint after upgrade.
//
// Pressing `[` steps down: 4 → 3 → 2 → 1, and 1 is the vertical list
// the user asked for ("无法通过调整来调整成竖排排列"). The ceiling is the
// Rise slot cap, because more columns than there are capsules is not a
// layout — it is empty columns.
inline constexpr int kWirebugColumns = 4;             // kRiseWirebugSlotCap
inline constexpr int kWirebugMaxColumns = 4;   // kRiseWirebugSlotCap

// The floor the draw side puts under one capsule's slot when a locked
// budget is narrower than the capsule's natural width. A slot cut below
// this has no room for the timer, so the pill would be clipped instead
// of merely narrow — and "a pill cut in half beats no pill" is the same
// trade drawPillGrid() already makes for the status strip.
inline constexpr int kMinCapsuleW = 60;

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
    // The columns dial, on Panel's virtual chain.
    [[nodiscard]] int  columns() const override { return columns_; }
    [[nodiscard]] bool supportsColumns() const override { return true; }

    // The row-layout vocabulary the generic wrap is derived from. These
    // answer "what is one item", and the shared arithmetic in
    // panel_sizing.h turns them plus a width budget into columns and
    // rows — the same answers the status strip gives from its own
    // tokens, so the two strips cannot drift about what a capsule is.
    [[nodiscard]] int  itemCount() const override
    {
        return rows_.capsules.size();
    }
    [[nodiscard]] int  naturalItemWidth() const override;
    [[nodiscard]] int  itemGap() const override { return kPillGap; }
    [[nodiscard]] int  rowPitch() const override { return kPillH + kPillRowGap; }

    // The width of the capsule grid, derived from the width this window
    // will ACTUALLY be (appliedWidth, so a lock is honoured) and from
    // the dial. Both the reservation and the draw go through here, which
    // is what keeps the capsule slot inside the window's real extent
    // instead of the one the content asked for.
    [[nodiscard]] int  wrappedColumns() const;

    // `subWindowIndex` is this window’s row in its parent’s split table. It
    // has to arrive at construction time (not via a setter) because
    // loadConfig() runs inside the base constructor and the index only
    // affects that call’s first-run default — see
    // defaultMarginsForSubWindow() in panel.cpp.
    explicit PlayerWirebugStripPanel(QWidget *parent = nullptr, int subWindowIndex = 0);

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

    // ─── Panel's polymorphic layout contract ────────────────────────
    //
    // Answered publicly so a layout question — the console's preview,
    // main.cpp, the layout consistency test — gets this window's real
    // numbers without knowing which window it is holding.

    // What the wrapped capsules ask for, before the floor and the lock.
    [[nodiscard]] int  contentWidth() const override;
    [[nodiscard]] int  contentHeight() const override;

    // The outer inset: the same kSplitMargin every other split window
    // answers, so the ring can be asked instead of seven private
    // constants being trusted to agree.
    [[nodiscard]] int  contentMargin() const override { return kWirebugMargin; }

    // The columns dial, lifted onto Panel's virtual chain. The base's
    // default no-op is what keeps every non-row window's [ ] key dead,
    // which is the whole reason this lives on the base rather than in
    // three copies of a passthrough contract.
    // The columns dial survives an exit. Not virtual and not an override:
    // Panel's versions are plain non-virtual members reached through
    // Panel*, so these hide rather than extend them — every call site
    // would have to know which of the two it is talking to. Both bodies
    // therefore call the base explicitly, the same discipline
    // PlayerStatusStripPanel::loadConfig/saveConfig documents for the
    // same pair (panel_player_split.h).
    //
    // main.cpp is the caller: it calls saveConfig() on this panel the
    // same way it calls it on every other sub-window when the user
    // leaves edit mode, so this window reaches its own body the same way
    // the strip reaches its own. The base keys are on disk before this
    // one adds to them.
    void loadConfig();
    void saveConfig();

    // v0.13: the game this window's demo seed represents. Wirebugs are a
    // Rise system, so the Rise arm seeds three recovering capsules and
    // the World arm seeds nothing (buildWirebugModel's World arm
    // deliberately reads nothing — see the switch in
    // panel_player_split_wirebug.cpp). This is what stops a World-rail
    // preview from claiming wirebugs a World session cannot have: the
    // exact defect the user reported as "衣装 and 翔虫 at the same time".
    //
    // Same shape and same caller as PlayerStatusStripPanel::setGameForDemo.
    void setGameForDemo(mhw::GameId game);

    // v0.15: the virtual dispatch path — see panel_player_split_mantles.h
    // for why a Panel* call site needs an override rather than another
    // non-virtual overload.
    void setDemoGame(mhw::GameId game) override;

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
    // Height the capsule row needs, in logical px. The panel reserves
    // kWirebugGap + kWirebugH = 9 + 18 (panel_player.cpp:752-753); this
    // window keeps the same 18 so a capsule drawn side by side with the
    // panel's is the same size.

    // ─── the columns dial (v0.13) ─────────────────────────────────────
    //
    // The wirebug row is the second window to gain a column axis; the
    // status strip was first and its dial was kept local to it. Both
    // now answer the base class's virtuals instead of each carrying a
    // private keyPressEvent handler, so the `[` `]` keys are handled
    // once in Panel::keyPressEvent and no subclass has to forward a key
    // it does not claim.
    //
    // The default is kWirebugColumns (= the slot cap), which is the
    // full one-row layout this window shipped with. Clamped to
    // [1, kWirebugMaxColumns]: 1 is the vertical list the user asked
    // for, and above the slot cap there are no columns to draw.
    void nudgeColumns(int delta) override;

    mhw::WirebugModel rows_{};
    // v0.13: the game the demo below seeds. Defaults to Rise, which is
    // the only game whose arm produces capsules; main.cpp overwrites it
    // before the first paint.
    mhw::GameId      game_{mhw::GameId::Rise};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it (and the rows with it) so a demo
    // preview can never survive into a live session.
    bool              demoData_{false};
    // The dial position. 1 = one capsule per row (a vertical list), 4 =
    // every slot side by side. Clamped by nudgeColumns(); the default is
    // the full row, not 1, so an untouched window is unchanged.
    int               columns_{kWirebugColumns};
};
