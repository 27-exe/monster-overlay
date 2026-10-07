#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// MonsterAilStripPanel — the v0.12 monster-panel split's 异常状态卡
// (.srow) window.
//
// The card grid MonsterPanel builds inline inside paintPanel (the
// `QVector<ScEntry> scList` loop at panel_monster.cpp:878-963) with the
// 15 s change-based AutoHide table (ailTrack_) and the game-keyed colour
// lookup (ailmentStyle) that goes with it.
//
// It is the clearest case among the five split targets for a window,
// because its width is a WRAP question: the .srow is 4 columns in a
// 362px panel, so a monster with six ailments gets one row of four and
// one row of two, and the panel has no way to say "six wide instead" —
// while a window can, and its grid arithmetic is panel_sizing's.
//
// Same three-piece pattern as every v0.12 split window — see
// panel_player_split.h for the pilot's two promises and
// panel_monster_split_hp.h for this split's variant.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row, no cap of its own: the panel's .srow has no
// heading either, the cards carry their own names. A hidden (auto-hidden)
// card is not an empty card — it is not in the list at all, which is the
// same rule the panel's `if (!timerRunning && !recent) continue;` enforces.
//
// THE AUTO-HIDE STATE. This window carries its own 32-slot tracker, one
// per World ailment id, with the same (sig, stampMs) shape the panel's
// ailTrack_ has. It cannot simply share that member: it lives in
// MonsterPanel's private section and is advanced inside its paint. Same
// pattern MonsterPartListBuilder uses for the .pc cards' PartAutoHide —
// a state machine next to the model that owns it, so it is testable
// without a widget. The panel keeps its own copy for single mode.

#include "panel.h"
#include "monster/monster_types.h"
#include "ui/formatters.h"
// MonsterSection::Ail — the mask axis, the same bit MonsterPanel reads
// for this block (panel_monster.cpp:967).
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm. Included here, not only in the .cpp, because
// contentMargin() is an inline member that names a constant.
#include "ui/panel_player_split_layout.h"

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QVector>

#include <QElapsedTimer>

#include <QtGlobal>

#include <array>
#include <cmath>

namespace mhw {

// One capsule on the grid: what to show, and which colour family it
// belongs to. The accent is NOT derived here — ailmentStyle() resolves
// it from the STABLE id through the game-keyed registry, which is the
// v0.11.0 rule that a translated display name must never be able to
// change a card's colour.
struct MonsterAilCard {
    QString name;
    QString iconPath;     // optional /icons/Traps/item_id_*.svg
    QColor  sc;           // --sc (border + timer + mini fill colour)
    float   pct{0.0F};    // mini progress 0..1
    QString tm;           // right-aligned text ("12s", "8s", "45%", "×2")
    bool    active{false};
};

// The window's whole content model: the cards the panel's .srow would
// draw, resolved once, for whatever game produced the snapshot.
struct MonsterAilModel {
    QVector<MonsterAilCard> cards;
    // The monster identity the cards were resolved from, carried so the
    // window can detect a target switch and reset its AutoHide table.
    // NOT the address the panel prints (0xDEADBEEF-shaped); it is the
    // same key the panel's update() compares, which is what makes the
    // reset fire exactly once per switch.
    std::uintptr_t address{0};

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return cards.isEmpty();
    }
};

// Cards per wrapped row at the DEFAULT the window ships with. The same
// 4 the panel's .srow uses, so a strip that grew up next to the panel
// lines up with it. Its own constant rather than a shared one:
// retuning the window must not be able to move the panel it was
// extracted from (the same rule panel_player_split.h states for its own).
//
// RUNTIME-ADJUSTABLE: the value below is only the default. A
// MonsterAilStripPanel dials its own `columns_` (setCardsPerRow) so a
// user can trade the window's width for its height on a small screen.
inline constexpr int kMonsterAilCardsPerRow = 4;

// The dial's range. 1 (a column of stacked cards) to 6 (wider than the
// panel's grid, which is the point — the window is not the panel).
inline constexpr int kMonsterAilMinColumns = 1;
inline constexpr int kMonsterAilMaxColumns = 6;

// Rows needed to lay `count` cards out `perRow`-wide, wrapping like
// PlayerPanel's debuff/buff rows. 0 and negatives answer 0 so callers
// never re-guard the divide — `perRow` is now a runtime value, and a
// 0-col window must answer 0 rows rather than divide by zero. (Same
// helper shape as panel_player_split.h's statusStripRows.)
[[nodiscard]] inline constexpr int monsterAilRows(int count,
                                                  int perRow) noexcept
{
    return (count > 0 && perRow > 0) ? (count + perRow - 1) / perRow : 0;
}

// The card's height in logical px: panel_monster.cpp's
// `kScCellH = kScPadY + kScTopFont + 4 + kScMiniH + kScPadY`
// (:1010 and again at :1348), spelled from the tokens this file owns.
[[nodiscard]] inline constexpr int monsterAilCardHeight() noexcept
{
    return 4 + 9 + 4 + 5 + 4;   // kScPadY + kScTopFont + gap + kScMiniH
}

// The disease tracker: the panel's ailTrack_ member, moved out verbatim.
// One slot per World ailment id (0..24); MonsterAilmentSnapshot.id is the
// same id HunterPie's MonsterAilmentRepository keys on. `sig` is the
// quantized (timer, buildup) signature, `stampMs` the wall-clock
// millisecond at which it last changed (see kAilAutoHideMs below).
//
// WHY IT MOVES with the block it belongs to, the same way PartTrack moved
// with MonsterPartListBuilder: a visibility rule that ends where the
// widget it drives started is not testable, and this one had ZERO
// coverage because the only way to reach it was a private member of a
// QWidget. The reset-on-identity-change call the panel makes on a target
// switch is resetAutoHide() below.
class MonsterAilmentTracker {
public:
    struct Slot {
        quint64 sig{0};
        qint64  stampMs{0};
    };

    // HunterPie's AutoVisibilityViewModel defaults: every Timer /
    // BuildUp / IsTimerActive change restarts the per-cell timer and the
    // cell hides once it elapses with no further change
    // (MonsterAilmentContextHandler.cs:61-68).
    static constexpr qint64 kAutoHideMs = 15000;

    // THE PANEL'S OWN GATE, VERBATIM (panel_monster.cpp:919):
    //
    //     if (!timerRunning && !recent)
    //         continue;
    //
    // where `recent` is editMode ? true : (stampMs > 0 && nowMs - stampMs
    // < 15 s) with stampMs re-stamped on every signature change. Three
    // consequences that the earlier, simpler version of this function got
    // wrong and that are now pinned by tests:
    //
    //   * A build-up-only card is NOT unconditionally visible. It is
    //     visible while it is "recent" — i.e. for 15 s after its last
    //     change, or forever in edit mode. A card that stopped changing
    //     20 s ago is gone.
    //   * The signature is refreshed (and the card re-pinned) on the
    //     SAME call that tests visibility, because ONE build() per paint
    //     both observes and filters. The panel does this in one loop for
    //     the same reason: a second loop would need a second clock.
    //   * An id outside the table answers "visible", never "hidden".
    //     The table is sized for World's 0..24; a Rise id the runtime
    //     has not seen must not silently disappear by overflowing a
    //     32-slot array.
    [[nodiscard]] bool isVisible(const mhw::MonsterAilmentSnapshot &a,
                                 bool editMode, qint64 nowMs)
    {
        // A non-finite reader float collapses every ailment signature
        // to the same bit pattern, so one dirty read would stamp every
        // card at once and then keep them all "recent" for 15 s.
        // Quantizing at 0.1 keeps the signature meaningful per card —
        // the panel's own ailSig arithmetic (:908-916).
        const float timerSafe   = std::isfinite(a.timer)   ? a.timer   : 0.0F;
        const float buildupSafe = std::isfinite(a.buildup) ? a.buildup : 0.0F;
        const quint64 sig =
            (static_cast<quint64>(
                 static_cast<quint32>(qRound(timerSafe * 10.0F))) << 32)
            | static_cast<quint32>(qRound(buildupSafe * 10.0F));

        // Counter changes deliberately do NOT refresh the card —
        // HunterPie's OnCounterUpdate is a plain SetValue
        // (MonsterAilmentContextHandler.cs:87), so a historical counter
        // alone can never pin a card. Only timer / buildup do.
        bool recent = true;   // edit-mode demo: never auto-hide
        if (!editMode && a.id >= 0 && a.id < static_cast<int>(slots_.size())) {
            Slot &s = slots_[static_cast<size_t>(a.id)];
            if (s.sig != sig) {
                s.sig      = sig;
                s.stampMs  = nowMs;
            }
            recent = s.stampMs > 0 && (nowMs - s.stampMs) < kAutoHideMs;
        }

        // The panel's gate, word for word. For ids outside the table
        // `recent` stays at its edit-mode default, so the card is shown.
        const bool timerRunning = a.timer > 0.0F;
        return timerRunning || recent;
    }

    // Advance the table with `nowMs` as the clock. MUTATES — called once
    // per paint, the same discipline the PartAutoHide builder has.
    void observe(const mhw::MonsterAilmentSnapshot &a, qint64 nowMs)
    {
        if (a.id < 0 || a.id >= static_cast<int>(slots_.size()))
            return;
        const float timerSafe  = std::isfinite(a.timer)  ? a.timer  : 0.0F;
        const float buildupSafe = std::isfinite(a.buildup) ? a.buildup : 0.0F;
        const quint64 sig =
            (static_cast<quint64>(
                 static_cast<quint32>(qRound(timerSafe * 10.0F))) << 32)
            | static_cast<quint32>(qRound(buildupSafe * 10.0F));
        Slot &s = slots_[static_cast<size_t>(a.id)];
        if (s.sig != sig) {
            s.sig     = sig;
            s.stampMs = nowMs;
        }
    }

    // Clears the table, exactly where the panel's update() does on the
    // monster identity change: a stale (sig, stampMs) from the previous
    // target would make the new monster's card judge "already silent"
    // (hidden on its first paint) or "already fresh" (pinned for a
    // spurious 15 s).
    void resetAutoHide() { slots_ = {}; }

private:
    std::array<Slot, 32> slots_{};
};

// The pure transformation for this window: the snapshot's ailments,
// filtered and resolved, into the cards the grid draws. It runs the
// tracker's visibility rule, so `nowMs` and `editMode` are inputs rather
// than reads of the live clock — the same sampling discipline the panel
// uses (one timestamp per paint, so every card in one frame is compared
// against ONE clock).
[[nodiscard]] MonsterAilModel buildMonsterAilModel(
    const QVector<mhw::MonsterAilmentSnapshot> &ailments,
    mhw::GameId game, bool editMode, qint64 nowMs,
    MonsterAilmentTracker &tracker);

} // namespace mhw

// 异常状态卡 as its own layer-shell window, with its own panels.ini group
// ("monster-ail", NOT "monster" — reusing it would make two windows fight
// with MonsterPanel over one position / scale / opacity section).
class MonsterAilStripPanel : public Panel {
    Q_OBJECT

public:
    // `subWindowIndex` is this window’s row in its parent’s split table. It
    // arrives at construction time because the base Panel constructor calls
    // loadConfig(), and the index exists only to pick that call’s first-run
    // default — see defaultMarginsForSubWindow() in panel.cpp. A setter
    // would always be one call too late.
    explicit MonsterAilStripPanel(QWidget *parent = nullptr, int subWindowIndex = 0);

    // Full monster snapshot. Caches the resolved cards for paintPanel()
    // to read, mirroring MonsterPanel's `monster_` member + update().
    void update(const mhw::MonsterSnapshot &m);

    // i18n: the card names come from the snapshot (already translated by
    // the reader) and no label of this window's own is cached beyond the
    // title, so this re-queries the title and schedules a repaint.
    void retranslateUi() override;

    // A window carved out of MonsterPanel declares itself a sub-window
    // so the console's slot registry keeps it out of the four fixed
    // slots the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

    // ─── Panel's polymorphic layout contract ────────────────────────

    // What the grid asks for. The floor and the lock are applied later,
    // inside Panel::setContentSize().
    [[nodiscard]] int contentWidth() const override;

    // Panel's height hook: one row of cards, or the collapsed size when
    // there are none. paintPanel() calls the same function.
    [[nodiscard]] int contentHeight() const override;

    // The outer inset, the same kSplitMargin every other split window
    // answers.
    [[nodiscard]] int contentMargin() const override
    {
        return mhw::split_layout::kSplitMargin;
    }

    // The wrapped-row dial, in the base class's vocabulary. A card grid
    // is the one row-shaped block in this split, so it is the one window
    // here that answers columns() rather than the base's 0.
    void setCardsPerRow(int perRow);
    [[nodiscard]] int cardsPerRow() const noexcept { return columns_; }
    [[nodiscard]] bool supportsColumns() const override { return true; }
    [[nodiscard]] int  columns() const override { return columns_; }

    // What a LAYOUT reads to derive a wrap: the data's item count, one
    // item's natural width, the gap between items and the vertical pitch.
    [[nodiscard]] int itemCount() const override
    {
        return cards_.cards.size();
    }
    [[nodiscard]] int naturalItemWidth() const override;
    [[nodiscard]] int itemGap() const override;
    [[nodiscard]] int rowPitch() const override;

    // Which game this window's demo seed represents. The ailment ids are
    // game-specific (Rise 4 is poison, World 2 is paralysis) and their
    // colours come from the game-keyed table, so a preview that ignores
    // the game would tint the wrong palette — the same class of mismatch
    // the user reported for 衣装/翔虫.
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

    // "No data, no window", the split's shared rule. Edit mode is the
    // deliberate exception — a design-mode surface stays mounted and
    // draggable — so once the demo is seeded the answer is true even
    // though the cards are synthetic.
    bool hasContent() const override;

private:
    // The section this window draws, as the mask leaves it. Reusing
    // MonsterSection::Ail means the console's existing monster toggle
    // drives this window with no new UI.
    [[nodiscard]] bool ailEnabled() const;

    // Height of the whole grid, in logical px: the wrapped rows, the
    // gaps between them, and the margins at both ends. Called from one
    // place, from the same rows paintPanel() draws.
    [[nodiscard]] int gridBlockHeight() const;

    // Width ONE card is drawn at, to exactly fill `available` with
    // `columns_` columns — the inverse of what contentWidth() reserves,
    // through panel_sizing's own gridSlotWidth so the two cannot drift.
    [[nodiscard]] int slotWidth() const;

    mhw::MonsterAilModel cards_;
    // The 15 s AutoHide table, owned by this window the way
    // MonsterPartListBuilder's partTrack_ is owned by the parts grid.
    mhw::MonsterAilmentTracker tracker_;
    // v0.12: the game the demo below seeds and the snapshot's own game.
    mhw::GameId        game_{mhw::GameId::World};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it so a demo preview cannot survive
    // into a live session.
    bool               demoData_{false};
    // The columns dial's current position, the default above at first.
    int                columns_{mhw::kMonsterAilCardsPerRow};
};
