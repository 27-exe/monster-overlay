#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// MonsterConditionsStripPanel — the v0.13 monster-panel split's
// Conditions window (settingsKey "monster-conditions").
//
// WHY TWO WINDOWS AND NOT FIVE
// -----------------------------
// The user's report on the v0.12 five-window split: 「怪物的切太狠了，
// 应该图标，激怒，血量是在一块，异常状态和部位在一块」. The split now
// asks the ONE question the blocks actually answer, twice.
//
//   Identity   = the monster as an ACTOR (portrait + HP + enrage). A
//                glance.
//   Conditions = the monster as an OPPONENT — what is wrong with it
//                (异常状态卡) and what can be broken (部位网格). A plan.
// This is the second of the two. panel_sections.h's
// kMonsterSplitTargets records the full argument for the grouping.
//
// The layout a window draws, in the panel's own stacking order:
//
//   +---------------------------------------------------+
//   |  [ 麻痹 12s ]  [ 麻痹陷阱 8s ]  [ 落穴 60s ]      |   <- Ail
//   |  [ 毒 30% ]                                      |
//   +---------------------------------------------------+
//   |  +--------+  +--------+  +--------+               |   <- Parts
//   |  | 头  斩 |  | 左翼 斩 |  | 右翼 斩 |               |
//   |  | 80/100 |  | 45/100 |  | 20/100 |               |
//   |  | 45s ▓▓ |  |        |  |        |               |
//   |  +--------+  +--------+  +--------+               |
//   +---------------------------------------------------+
//
// Every block is INDEPENDENTLY TOGGLEABLE, which is what keeps the
// user's 「还可选择性的关闭部分」 true:
//
//   * MonsterSection::Ail (1u<<3) and ::Parts (1u<<4) each gate their
//     own block's WIDTH RESERVATION, HEIGHT RESERVATION and PAINT —
//     never the window's mount. The window mounts while EITHER is on.
//   * The section mask arrives as the row's union (Ail|Parts)
//     intersected with the user's toggles, and the window divides it
//     back out per block. conditionsAilEnabled() / partsEnabled() are
//     called from every consumer of their block, so a turn-off cannot
//     leave a reserved row behind.
//   * Tenderize (1u<<5) is NOT one of this window's toggles in the
//     sense that it has no block of its own: since v0.7.4 PR C it is a
//     strip INSIDE each .pc card, so it rides the Parts bit the way it
//     rides it in the panel. Turning Parts off takes it with it.
//
// Same three-piece pattern as every v0.12/v0.13 split window — see
// panel_player_split.h for the pilot's two promises.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row: this window is a fragment of the monster
// panel, not a panel. setChromeVisible(false) + isSubWindow() true.
//
// The 肖像/名字/HP/耐力/激怒 blocks are NOT here — they are
// MonsterIdentityStripPanel (settingsKey "monster-identity").

#include "panel.h"
#include "monster/monster_types.h"
#include "ui/formatters.h"
// MonsterSection::Ail / ::Parts / ::Tenderize — the mask axes, the same
// bits MonsterPanel reads (panel_monster.cpp :967 / :968 / :976).
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm (kSplitMargin, the gap grades) this window
// answers contentMargin() and builds its grids from. Included here, not
// only in the .cpp, because contentMargin() is an inline member.
#include "ui/panel_player_split_layout.h"
// PcEntry + MonsterPartListBuilder — the .pc card shape, the ONLY
// assembler and the ONLY height calculator, AND the ailment grid's
// model (MonsterAilCard / MonsterAilModel / MonsterAilmentTracker /
// buildMonsterAilModel, plus the two dial bounds). BOTH open
// `namespace mhw` themselves, so both are included HERE, at file
// scope, rather than inside our own block — including them between a
// `namespace mhw {` and its closing brace would qualify every Qt type
// they pull in as mhw::QColor and fail to compile.
//
// They are INCLUDED rather than re-declared for a hard reason: the
// ailments' model and its builder already exist in
// ui/panel_monster_split_ail.h / .cpp (the v0.12 single-block window,
// kept in the tree by the parent session). A second spelling of
// `MonsterAilCard` is an ODR violation the moment both translation
// units are linked, and a second `buildMonsterAilModel` would be two
// answers to "which ailments are visible right now" — the whole point
// of the trait's existence.
#include "ui/panel_monster_split_ail.h"
// MonsterPartsModel / buildMonsterPartsModel come from the window that
// owns the part grid. They are NOT restated here: restating them collides
// with panel_monster_split_parts.h the moment both reach one TU, which
// happens because control_panel.cpp constructs every split window. Same
// lesson, same fix as the identity window's bar rows.
#include "ui/panel_monster_split_parts.h"
#include "ui/viewmodel/monster_view_model.h"

#include <QColor>
#include <QDateTime>
#include <QFontMetrics>
#include <QPixmap>
#include <QPainter>
#include <QString>
#include <QVector>

#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mhw {

// ─── the two grids' models, already widget-free ──────────────────────
//
// BOTH grids' models come from headers this file INCLUDES rather than
// re-declares: the ailments' model from ui/panel_monster_split_ail.h
// (the v0.12 single-block window, kept in the tree by the parent
// session) and the parts' model from ui/viewmodel/monster_view_model.h.
// See the include block at the top of this file for why re-spelling
// either would be an ODR violation rather than a style choice, and why
// both includes sit at file scope (each opens `namespace mhw` itself).

// MonsterPartsModel and buildMonsterPartsModel come from
// panel_monster_split_parts.h — see the include note above. The reason
// they look like they belong here (this window draws the grid) is exactly
// the trap: the MODEL belongs to whoever assembles it, and both windows
// reach it by include.
//
// What is here is what this window adds: how the two blocks are gated
// independently, and the shared rhythm they lay out on.

} // namespace mhw

// 异常状态卡 + 部位网格 as its own layer-shell window, with its own
// panels.ini group ("monster-conditions", NOT "monster" — reusing it
// would make two windows fight with MonsterPanel over one position /
// scale / opacity section).
class MonsterConditionsStripPanel : public Panel {
    Q_OBJECT

public:
    // `subWindowIndex` is this window's row in its parent's split table.
    // It arrives at construction time because the base Panel constructor
    // calls loadConfig(), and the index exists only to pick that call's
    // first-run default — see defaultMarginsForSubWindow() in panel.cpp.
    // A setter would always be one call too late.
    explicit MonsterConditionsStripPanel(QWidget *parent = nullptr,
                                         int subWindowIndex = 0);

    // Full monster snapshot. Caches the resolved cards for paintPanel()
    // to read, mirroring MonsterPanel's `monster_` member + update().
    void update(const mhw::MonsterSnapshot &m);
    // v0.10.8 World-multiplayer flag: on a non-host client the Health
    // pair is stale local data, so the gauge is suppressed rather than
    // drawn as a broken part at 90-100%.
    void setMultiplayer(bool on);

    // i18n: the card names arrive already translated on the snapshot
    // and are re-resolved by the reader, so there is nothing
    // locale-dependent cached here to rebuild.
    void retranslateUi() override;

    // A window carved out of MonsterPanel declares itself a sub-window
    // so the console's slot registry keeps it out of the four fixed
    // slots the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

    // ─── Panel's polymorphic layout contract ────────────────────────
    [[nodiscard]] int contentWidth() const override;
    [[nodiscard]] int contentHeight() const override;
    [[nodiscard]] int contentMargin() const override
    {
        return mhw::split_layout::kSplitMargin;
    }

    // ─── the wrapped-row dial ───────────────────────────────────────
    //
    // TWO dials, not one, and that is deliberate: the two grids have
    // different natural widths (an ailment card is wide and short, a
    // part card is taller and text-heavier) and one number for both
    // would make a usable setting for one unusable for the other.
    //
    // The base class's own dial ([ and ] keys) moves the PARTS dial
    // here, matching the panel's own .pgrid key handling. The ailments'
    // own dial is the public setAilCardsPerRow() below, which the
    // console reaches through the window's capabilities row.
    void setCardsPerRow(int perRow);          // parts (.pc)
    void setAilCardsPerRow(int perRow);       // ailments (.sc)
    [[nodiscard]] int cardsPerRow() const noexcept { return columns_; }
    [[nodiscard]] int ailCardsPerRow() const noexcept { return ailColumns_; }
    [[nodiscard]] bool supportsColumns() const override { return true; }
    [[nodiscard]] int  columns() const override { return columns_; }

    // What a LAYOUT reads to derive a wrap: the data's item count, one
    // item's natural width, the gap between items and the vertical
    // pitch. Answered for the PARTS grid (the taller of the two), which
    // is the one whose wrap a layout would drive.
    [[nodiscard]] int itemCount() const override
    {
        return parts_.cards.size();
    }
    [[nodiscard]] int naturalItemWidth() const override;
    [[nodiscard]] int itemGap() const override;
    [[nodiscard]] int rowPitch() const override;

    // ─── the two per-block gates, PUBLIC ON PURPOSE ─────────────────
    //
    // These are the mechanism behind 「还可选择性的关闭部分」. They are
    // public rather than private because the honest way to prove that
    // turning 异常状态 off really stops the ailments grid from being
    // reserved is to ask the window, not to look at a screenshot.
    //
    // Each reads its OWN MonsterSection bit out of the section mask the
    // window was handed (which is the row's union intersected with the
    // user's toggles). The window itself is mounted by "EITHER block
    // on", never by "both".
    [[nodiscard]] bool conditionsAilEnabled() const;
    [[nodiscard]] bool partsEnabled() const;
    [[nodiscard]] bool tenderizeEnabled() const;

    // Which game this window's demo seed represents. The ailment ids
    // are game-specific (Rise 4 is poison, World 2 is paralysis) and
    // their colours come from the game-keyed table, so a preview that
    // ignores the game would tint the wrong palette — the same class of
    // mismatch the user reported for 衣装/翔虫.
    void setGameForDemo(mhw::GameId game);

    // v0.15: the virtual dispatch path — see panel_player_split_mantles.h
    // for why a Panel* call site (the Console's split preview host) needs
    // an override rather than another non-virtual overload.
    void setDemoGame(mhw::GameId game) override;

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // Base-class dial, over this class's own columns_.
    void nudgeColumns(int delta) override;

    // "No data, no window", the split's shared rule — with the
    // Conditions twist: the window answers TRUE while EITHER of its two
    // blocks is on and has data. Edit mode is the deliberate exception
    // (a design-mode surface stays mounted and draggable), so once the
    // demo is seeded the answer is true even though the cards are
    // synthetic.
    bool hasContent() const override;

private:
    // The ailments grid: the panel's displayableParts() is NOT part of
    // this (the ailments arrive pre-filtered by the reader), but the
    // 15 s AutoHide table and the game-keyed colour lookup are, so the
    // model is resolved with the tracker beside it.
    [[nodiscard]] int ailBlockHeight() const;
    [[nodiscard]] int ailGridBlockHeight() const;
    [[nodiscard]] int ailContentWidth() const;
    [[nodiscard]] int ailSlotWidth(int innerW) const;

    // The parts grid, with the builder as a MEMBER for the same reason
    // MonsterPanel owns it: build() mutates the 15 s PartAutoHide table
    // and a fresh one per paint would re-pin every card every frame.
    [[nodiscard]] int partsBlockHeight() const;
    [[nodiscard]] int partsGridBlockHeight() const;
    [[nodiscard]] int partsContentWidth() const;
    [[nodiscard]] int partsSlotWidth(int innerW) const;
    [[nodiscard]] int partCardHeight(int index) const;

    // The panel's displayableParts() (panel_monster.cpp:377-392):
    // severable/breakable only, with a first-kFlinchFallbackParts
    // fallback when a monster has nothing but flinch bars.
    [[nodiscard]] static QVector<mhw::PartSnapshot> displayableParts(
        const QVector<mhw::PartSnapshot> &parts);

    mhw::MonsterAilModel   ail_{};
    // The 15 s AutoHide table this window advances — the class next to
    // the model that owns it, so the state machine is testable without a
    // widget. Same pattern MonsterPartListBuilder uses for partTrack_.
    mhw::MonsterAilmentTracker ailTracker_{};
    mhw::MonsterPartsModel parts_{};
    // The ONE assembler for the .pc cards, owned here exactly as
    // MonsterPanel owns it — see the class comment in
    // ui/viewmodel/monster_view_model.h.
    mhw::MonsterPartListBuilder partList_{};
    // v0.13: the game the demo below seeds and the snapshot's own game.
    mhw::GameId game_{mhw::GameId::World};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it so a demo preview cannot survive
    // into a live session.
    bool demoData_{false};
    // The v0.10.8 World-multiplayer flag.
    bool multiplayer_{false};
    // The two dials' current positions.
    int columns_{3};                          // .pc cards per row
    int ailColumns_{mhw::kMonsterAilCardsPerRow};
};
