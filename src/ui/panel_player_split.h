#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// PlayerStatusStripPanel — the v0.12 "block → window" pilot.
//
// Monster-overlay v0.12 splits PlayerPanel's independently-toggleable
// blocks (ui/panel_sections.h → PlayerSection) into their own layer-shell
// windows, the way HunterPie ships AbnormalityTray / WirebugWidget /
// SpecializedTool as separate top-level widgets instead of one PlayerHud
// with eight sections. Design: experiment/monster-overlay-v012/design/
// 01-hunterpie-split.md §2.1 (the widget list) and §10.2 (the candidate
// sections — Debuff / Buff is first because it maps onto HunterPie's
// AbnormalityTray and carries the least state).
//
// This panel is the FIRST one out, so its job is as much to validate the
// extraction pattern as to ship a feature. Two promises it must keep:
//
//   1. It is PURELY ADDITIVE. PlayerPanel itself is untouched, so
//      single-panel mode behaves exactly as it did in v0.11.
//   2. It borrows nothing from PlayerPanel at the symbol level. The pill
//      drawing sub-routines it mirrors (drawPill / drawPillGrid /
//      accentFor) live in panel_player.cpp's anonymous namespace and are
//      therefore unreachable from here. The .cpp carries its own small
//      copies with the same tokens, on purpose: reaching into another
//      translation unit's anonymous namespace is not possible, and
//      hoisting them into a shared header is a change to a file that is
//      not in this step's remit. The duplication is confined to the block
//      that is being extracted, so it disappears with the panel it
//      mirrors.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row. A status strip is content-only: HunterPie's
// AbnormalityTray has no panel-title concept either, and its only
// design-mode affordance is a WidgetHeader that exists purely so the user
// can drag the window. The constructor calls the base class's
// setChromeVisible(false) and isSubWindow() answers true — both landed by
// the parent agent in the same v0.12 step; with the switch off
// Panel::paintPanelChrome() returns without touching the painter.

#include "panel.h"
#include "core/game_snapshot.h"
#include "player/player_types.h"

#include <QKeyEvent>
#include <QPainter>
#include <QString>
#include <QVector>

namespace mhw {

// One capsule on the strip: what to show, and which colour family it
// belongs to. The accent is NOT derived here — the reader (or the demo
// seeder) already resolved it from the stable id/offset, which is the
// v0.11.0 rule that a translated display name must never be able to
// change a pill's colour.
struct StatusPill {
    QString name;
    QString timer;
    mhw::AbnormalityAccent accent{mhw::AbnormalityAccent::None};
};

// The strip's whole content model: one block of debuffs, one block of
// buffs, in the order the strip draws them. Both are empty under a
// snapshot with nothing active, which is what collapses the window.
struct StatusStripModel {
    QVector<StatusPill> debuffs;
    QVector<StatusPill> buffs;

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return debuffs.isEmpty() && buffs.isEmpty();
    }
    [[nodiscard]] int total() const noexcept
    {
        return debuffs.size() + buffs.size();
    }
};

// Capsules per wrapped row — the default the strip shipped with, and
// what PlayerPanel uses for its debuff / buff rows and its Rise 状态
// grid, so a strip that grew up next to the panel lines up with it.
// Its own constant rather than a shared one: retuning the strip must
// not be able to move the panel it was extracted from.
//
// RUNTIME-ADJUSTABLE: the value below is only the default. A
// PlayerStatusStripPanel dials its own `columns_` (setPillsPerRow) so a
// user can trade the strip's width for its height on a small screen
// without touching the width-token arithmetic the panel still uses.
inline constexpr int kStatusStripPillsPerRow = 3;

// The dial's range. 1 (a column of stacked capsules: tall, narrow,
// every pill at full measured width) to 6 (three default-width rows
// side by side). Below 1 is not a layout — it is a divide-by-zero —
// and above 6 the window is wider than the panel it was carved out of.
inline constexpr int kStatusStripMinColumns = 1;
inline constexpr int kStatusStripMaxColumns = 6;

// Rows needed to lay `count` pills out `perRow`-wide, wrapping like
// PlayerPanel's debuff/buff rows. 0 and negatives answer 0 so callers
// never have to re-guard the divide, which matters here because
// `perRow` is now a runtime value: a 0-col strip must answer 0 rows,
// not divide by zero.
[[nodiscard]] inline constexpr int statusStripRows(int count,
                                                   int perRow) noexcept
{
    return (count > 0 && perRow > 0)
               ? (count + perRow - 1) / perRow
               : 0;
}

// The one pure transformation in this panel: GameSnapshot in, the two
// pill blocks out. Both the live path (PlayerStatusStripPanel::update)
// and the edit-mode demo path (setupDemoData) go through it, so the
// preview and a real session cannot disagree about which rows exist or
// what colour they are.
//
// Deliberately free of any widget or painter type, and deliberately
// dispatched through a switch on GameId (not an if/else chain): World
// reads PlayerSnapshot::debuffs / ::buffs, Rise reads
// PlayerSnapshot::abnormalities and splits it by AbnormalityKind. Adding
// a third game means giving it its own arm, and -Wswitch makes an
// unhandled one a build error rather than a silent borrow of another
// game's table — the same seam panel_player.cpp:162 uses. The entries a
// game never fills (a World snapshot has no `abnormalities`, a Rise one
// has no `debuffs`/`buffs`) simply contribute nothing, which is what
// makes an idle game collapse the strip.
[[nodiscard]] StatusStripModel buildStatusStripModel(
    const mhw::GameSnapshot &snap);

} // namespace mhw

// Status capsule strip: debuff row + buff row, as its own layer-shell
// window with its own panels.ini group ("player-status", NOT "player" —
// reusing that key would make this panel and PlayerPanel fight over one
// position/scale/opacity section).
class PlayerStatusStripPanel : public Panel {
    Q_OBJECT

public:
    explicit PlayerStatusStripPanel(QWidget *parent = nullptr);

    // Full snapshot. Caches the pill model for paintPanel() to read,
    // mirroring PlayerPanel's `player_` member + update() pattern.
    void update(const mhw::GameSnapshot &snap);

    // v0.12: the console / main loop needs to be able to retarget the
    // demo preview at the game the user selected on the rail, the same
    // way PlayerPanel::setGameForDemo() does. Sets game_ and drops the
    // one-shot demo seed so setupDemoData() rebuilds the preview with
    // the new game's flavour (World debuff/buff vectors vs Rise
    // abnormalities).
    void setGameForDemo(mhw::GameId game);

    // i18n: re-query the cached window title and re-seed the demo
    // labels (they come from tr() at seed time). Called by the overlay's
    // locale-watch loop after StringTable::load() swapped the locale.
    void retranslateUi() override;

    // COLUMNS DIAL (v0.12): capsules per wrapped row. Defaults to
    // mhw::kStatusStripPillsPerRow (=3) so an untouched strip renders
    // exactly as it did before the dial existed.
    //
    // Why a setter on THIS panel rather than a virtual on Panel: the
    // dial is a layout question, and the width/height arithmetic that
    // answers it lives in panel_sizing.h, which this panel (and only
    // this panel, for now) feeds. Putting `nudgeColumns(int)` on the
    // base class would make every panel declare an opinion about a
    // token it does not use — the same "borrow nothing at the symbol
    // level" rule the panel already follows for drawPill(). The base
    // class also has no columns member and no columns key in panels.ini,
    // so a virtual there would be a pure hook with no default body and
    // no storage. When a second panel needs the dial the shared form
    // can be promoted out of this panel with its invariants already
    // pinned here; until then it stays local.
    //
    // Clamped into [kStatusStripMinColumns, kStatusStripMaxColumns] —
    // 0 would make statusStripRows() divide by zero and contentWidth()
    // would answer a 0-wide grid.
    void setPillsPerRow(int perRow);
    [[nodiscard]] int pillsPerRow() const noexcept { return columns_; }

    // The columns dial survives an exit. Public because main.cpp calls
    // saveConfig() on this panel the same way it calls it on every other
    // sub-window when the user leaves edit mode — that call is the trigger
    // for persisting, not an implementation detail.
    //
    // Not `virtual` and not `override`: Panel's versions are plain
    // non-virtual members reached through Panel*, so these would hide
    // rather than extend them, and every call site would have to know
    // which of the two it is talking to. Both bodies therefore call the
    // base explicitly — the same discipline Panel::loadConfig() documents
    // for its own callers.
    void loadConfig();
    void saveConfig();

    // v0.12: this window was carved out of PlayerPanel, so it declares
    // itself a sub-window (Panel::isSubWindow, whose header comment names
    // a DebuffStrip as the example). That is what lets the console /
    // HudCanvas slot registry keep it out of the four fixed slots the
    // base panels occupy, instead of forcing kPanelCount from 4 to 5
    // while the base panels' contract still says four.
    [[nodiscard]] bool isSubWindow() const override { return true; }

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // THE COLUMNS DIAL'S KEY HANDLER. Panel::keyPressEvent is an
    // override on QMainWindow (panel.h:226) with no virtual hook and no
    // column plumbing of its own, and Panel::keyPressEvent's edit-mode
    // arm unconditionally forwards unhandled keys to QMainWindow — so
    // deriving an override here is both legal and the narrowest place
    // to intercept: this class handles [ / ] and then defers to
    // Panel::keyPressEvent(e) for EVERY other key, which keeps Space
    // (minimize), Esc (quit) and the arrow-key margin nudge exactly as
    // they were. No base-class edit, no second virtual to keep in sync.
    //
    // Edit mode only, matching the other edit-mode affordances: a live
    // hunter's keystrokes belong to the game, not the overlay.
    void keyPressEvent(QKeyEvent *e) override;

    // HunterPie's core "no data, no window" mechanism, three layers deep
    // (§6.3 of the design report): with nothing to show the strip
    // answers false here, the main loop's hasVisibleContent() answers
    // false, and the layer-shell surface is never mounted. An empty
    // strip must not leave a grabbable-but-pointless frame on the game.
    //
    // Edit mode is the exception and it is deliberate: a design-mode
    // surface has to stay mounted so the user can drag it, so once the
    // demo has been seeded the answer is true even though the rows are
    // synthetic. In edit mode the caller (main.cpp, like PetDamagePanel)
    // shows the panel explicitly rather than asking this question, so
    // this answer is what gates the LIVE mount.
    bool hasContent() const override;

private:
    // The pill blocks as the section mask leaves them. Called once per
    // paint (and by contentWidth(), which runs inside the same paint),
    // so width, height reservation and the actual draw all see the SAME
    // set of rows — the v0.10.3 lesson from MonsterPanel's pc grid: two
    // gates that derive their own set is how a strip was drawn at one
    // height and reserved at another.
    [[nodiscard]] QVector<mhw::StatusPill> debuffRows() const;
    [[nodiscard]] QVector<mhw::StatusPill> buffRows() const;

    // Width the strip needs for the pills it is currently showing, in
    // logical px, from the widest capsule's name + timer. Never a fixed
    // 378 — that was the whole PlayerPanel width and belongs to it.
    //
    // A GRID width, not a single-capsule width: see the definition in
    // panel_player_split.cpp and docs/SIZING.md. It is the exact
    // inverse of the gridSlotWidth() drawPillGrid() feeds each capsule,
    // which is the invariant that makes the strip adaptive at all.
    [[nodiscard]] int contentWidth() const;

    // Height of `count` pills laid out `columns_`-wide with inter-row
    // gaps, using the same formula as panel_player.cpp:714-726 so the
    // strip and the panel it was cut from wrap identically at the
    // default column count.
    [[nodiscard]] int blockHeight(int count) const;

    mhw::GameId            game_{mhw::GameId::World};
    mhw::StatusStripModel  rows_;
    // Capsules per wrapped row: the columns dial's current value, the
    // only one of the strip's layout numbers a user can change at
    // runtime. Starts at kStatusStripPillsPerRow (=3) so the shipped
    // behaviour is the default and no config migration is needed.
    int                    columns_{
        mhw::kStatusStripPillsPerRow};
    // True while the content on screen came from setupDemoData(). The
    // first authoritative snapshot clears it and the rows with it, so a
    // demo preview can never survive into a live session (same guard
    // PetDamagePanel applies).
    bool                   demoData_{false};
};
