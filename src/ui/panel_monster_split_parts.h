#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// MonsterPartsStripPanel — the v0.12 monster-panel split's 部位网格
// (.pgrid) window.
//
// The heaviest of the five split targets, and the one that needs the
// least new machinery to be extractable:
//
//   * PcEntry                    — the ONE shape every layout site agrees
//                                  on, already in
//                                  ui/viewmodel/monster_view_model.h.
//   * MonsterPartListBuilder     — the ONLY producer of that shape, same
//                                  header, with no widget dependency.
//   * pcGaugeExtraH()            — the ONLY height calculator... and this
//                                  is the one piece that is NOT in the
//                                  ViewModel. It lives in
//                                  panel_monster.cpp's anonymous
//                                  namespace (inline int
//                                  pcGaugeExtraH(const PcEntry&), :463),
//                                  next to the three predicates it
//                                  composes.
//
// WHY IT IS NOT HOISTED. The obvious move is to promote pcGaugeExtraH(),
// pcHasTenderize() / pcHasFlinch() / pcHasPrimaryGauge() and the kPc*
// geometry tokens into the ViewModel header beside build(). That is the
// right long-term answer and it is NOT this change's: the tokens
// (kPcPadX, kPcMiniH, kPcTnExtra, ...) are draw geometry, and
// panel_monster.cpp's own header comment (T16) records the deliberate
// line that was drawn — the ViewModel owns the DATA and its height
// arithmetic, the panel owns the DRAW. panel_monster.cpp is in this
// step's remit, but the panel is also the file the 10 single-mode
// baselines depend on pixel-for-pixel, and hoisting twelve draw tokens
// through it changes more lines than the split itself.
//
// So this .cpp carries its own copy of pcGaugeExtraH() and the kPc*
// tokens, word for word, with each one naming the line it mirrors — the
// same confined duplication every v0.12 split window accepts for the
// drawing sub-routines it mirrors. The ONE thing that is NOT duplicated
// is the AutoHide state and the card assembly: the builder is owned here
// as a member, exactly as MonsterPanel owns it, and that is what keeps
// the 15 s silence window identical between the two paths.
//
// Same three-piece pattern as every v0.12 split window — see
// panel_player_split.h for the pilot's two promises and
// panel_monster_split_hp.h for this split's variant.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row: a card grid is a fragment of the monster
// panel, not a panel. The 勾爪软化胶囊 (Tenderize bit) is NOT a window of
// its own and NOT a card of its own — since v0.7.4 PR C it is a strip
// INSIDE each .pc card, drawn and height-reserved by the same
// pcHasTenderize() gate. It travels here because the card is what it
// lives in.

#include "panel.h"
#include "monster/monster_types.h"
#include "ui/formatters.h"
// PcEntry + MonsterPartListBuilder — the model this window is a
// fragment of, already extracted and already widget-free.
#include "ui/viewmodel/monster_view_model.h"
// MonsterSection::Parts — the mask axis, the same bit MonsterPanel reads
// for this block (panel_monster.cpp:968).
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm. Included here, not only in the .cpp, because
// contentMargin() is an inline member that names a constant.
#include "ui/panel_player_split_layout.h"

#include <QColor>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace mhw {

// The window's whole content model: the cards the panel's .pgrid would
// draw, assembled by the ONE builder. Not re-derived here, and not
// re-filtered here either — the builder applies the display filter
// (severable/breakable only, with the first-4 flinch fallback) and the
// 15 s PartAutoHide, and this model is exactly its output.
//
// It is a struct with the builder as a member rather than a free
// function's return value, because the builder is MUTABLE: build()
// advances partTrack_, and "ONE call per paint" is a discipline the
// caller enforces by owning the thing that remembers.
struct MonsterPartsModel {
    QVector<PcEntry> cards;
    // The monster identity the cards were resolved from, carried the way
    // the panel carries `monster_.address`: the reset-on-switch call.
    std::uintptr_t address{0};

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return cards.isEmpty();
    }
};

// Assemble the grid for one paint.
//
// Parameters, in the same order and with the same meaning as the panel's
// own build() call (panel_monster.cpp:1027-1028):
//   shownParts   — the display-filtered parts. NOT re-filtered here: the
//                  builder never re-filters, and the caller has the
//                  snapshot's full list plus displayableParts() beside
//                  it. Passing the raw list would change which parts
//                  appear, which is a behaviour change this split must
//                  not make.
//   nowMs        — the paint clock stamp, sampled once per paint so
//                  every card in this frame is compared against ONE
//                  clock.
//   onTenderize  — MonsterSection::Tenderize. Applied inside build(), so
//                  the reservation and the render cannot disagree about
//                  whether the strip is on (the v0.10.3 incident).
//   editMode     — PartAutoHide never hides a card in edit mode, so the
//                  demo / console preview keeps every card pinned.
//   game         — selects the per-PartType layer pairs.
//   multiplayer  — suppresses the primary gauge on World in a session.
//
// MUTABLE: this advances builder.partTrack_. See the class comment in
// ui/viewmodel/monster_view_model.h.
[[nodiscard]] inline MonsterPartsModel buildMonsterPartsModel(
    const QVector<mhw::PartSnapshot> &shownParts, qint64 nowMs,
    bool onTenderize, bool editMode, mhw::GameId game, bool multiplayer,
    MonsterPartListBuilder &builder)
{
    MonsterPartsModel model;
    model.cards = builder.build(shownParts, nowMs, onTenderize, editMode,
                               game, multiplayer);
    return model;
}

} // namespace mhw

// 部位网格 as its own layer-shell window, with its own panels.ini group
// ("monster-parts", NOT "monster" — reusing it would make two windows
// fight with MonsterPanel over one position / scale / opacity section).
class MonsterPartsStripPanel : public Panel {
    Q_OBJECT

public:
    // `subWindowIndex` is this window’s row in its parent’s split table. It
    // arrives at construction time because the base Panel constructor calls
    // loadConfig(), and the index exists only to pick that call’s first-run
    // default — see defaultMarginsForSubWindow() in panel.cpp. A setter
    // would always be one call too late.
    explicit MonsterPartsStripPanel(QWidget *parent = nullptr, int subWindowIndex = 0);

    // Full monster snapshot. Caches the resolved cards for paintPanel()
    // to read, mirroring MonsterPanel's `monster_` member + update().
    void update(const mhw::MonsterSnapshot &m);
    // v0.10.8 World-multiplayer flag: on a non-host client the Health
    // pair is stale local data, so the gauge is suppressed rather than
    // drawn as a broken part at 90-100%. Setter in the .cpp because the
    // header only declares it.
    void setMultiplayer(bool on);

    // i18n: no cached strings of this window's own (the part names
    // arrive already translated on the snapshot, and the tag chips are
    // bare "斩"/"破"), so this re-queries the title and schedules a
    // repaint.
    void retranslateUi() override;

    // A window carved out of MonsterPanel declares itself a sub-window
    // so the console's slot registry keeps it out of the four fixed
    // slots the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

    // ─── Panel's polymorphic layout contract ────────────────────────
    [[nodiscard]] int contentWidth() const override;
    [[nodiscard]] int contentHeight() const override;

    // The outer inset, the same kSplitMargin every other split window
    // answers.
    [[nodiscard]] int contentMargin() const override
    {
        return mhw::split_layout::kSplitMargin;
    }

    // The wrapped-row dial, in the base class's vocabulary. A card grid
    // is the other row-shaped block in this split, so it is the other
    // window here that answers columns() rather than the base's 0.
    void setCardsPerRow(int perRow);
    [[nodiscard]] int cardsPerRow() const noexcept { return columns_; }
    [[nodiscard]] bool supportsColumns() const override { return true; }
    [[nodiscard]] int  columns() const override { return columns_; }

    // What a LAYOUT reads to derive a wrap.
    [[nodiscard]] int itemCount() const override
    {
        return model_.cards.size();
    }
    [[nodiscard]] int naturalItemWidth() const override;
    [[nodiscard]] int itemGap() const override;
    [[nodiscard]] int rowPitch() const override;

    // Which game this window's demo seed represents. The demo parts'
    // names and the per-PartType layer pairs are game-keyed, so a
    // preview that ignored the game would render the wrong half.
    void setGameForDemo(mhw::GameId game);

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // Base-class dial, over this class's own columns_.
    void nudgeColumns(int delta) override;

    // "No data, no window", the split's shared rule. Edit mode is the
    // deliberate exception — a design-mode surface stays mounted and
    // draggable — so once the demo is seeded the answer is true even
    // though the cards are synthetic.
    bool hasContent() const override;

private:
    // The section this window draws, as the mask leaves it. Reusing
    // MonsterSection::Parts means the console's existing monster toggle
    // drives this window for no new console UI.
    [[nodiscard]] bool partsEnabled() const;

    // The panel's displayableParts() (panel_monster.cpp:377-392):
    // severable/breakable only, with the first-4 flinch fallback when a
    // monster has nothing but flinch bars. Copied because it is in that
    // file's anonymous namespace; the rule is load-bearing, so the copy
    // is verbatim.
    [[nodiscard]] static QVector<mhw::PartSnapshot> displayableParts(
        const QVector<mhw::PartSnapshot> &parts);

    // The per-card height the panel's two sites agree on: cellHeights[i]
    // = kPcBaseCellH + pcGaugeExtraH(pcList[i]). This window's own copy
    // of both terms (each names the line it mirrors), called from one
    // place so the reservation and the draw cannot diverge — the
    // v0.10.3 lesson, in the block where it was learned.
    [[nodiscard]] int cardHeight(int index) const;

    // Height of the whole grid: per-row maxima plus the gaps, which is
    // what keeps cells in one row aligned to the tallest of them.
    [[nodiscard]] int gridBlockHeight() const;

    // Width ONE card is drawn at, to exactly fill `available` with
    // `columns_` columns — the inverse of what contentWidth() reserves,
    // through panel_sizing's own gridSlotWidth.
    [[nodiscard]] int slotWidth(int innerW) const;

    mhw::MonsterPartsModel model_{};
    // The .pc assembler, a MEMBER for the same reason MonsterPanel's is:
    // build() advances the 15 s PartAutoHide table and a fresh builder
    // per paint would re-pin every card on every frame.
    mhw::MonsterPartListBuilder partList_;
    // v0.12: the game the demo seeds and the snapshot's own game.
    mhw::GameId        game_{mhw::GameId::World};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it so a demo preview cannot survive
    // into a live session.
    bool               demoData_{false};
    // The v0.10.8 World-multiplayer flag: on a non-host client the
    // Health pair is stale local data, so the gauge is suppressed rather
    // than drawn as a broken part at 90-100%.
    bool               multiplayer_{false};
    // The columns dial's current position.
    int                columns_{3};
};
