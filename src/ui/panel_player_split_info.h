#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// PlayerInfoStripPanel — the v0.12 「任务/进程」 sub-window.
//
// Sixth piece of the v0.12 "block → window" split of PlayerPanel (see
// panel_player_split.h for the pilot and the pattern's two promises, and
// panel_player_split_bars.h / _mantles.h / _wirebug.h /
// panel_player_split_weapon.h for the ones written in the same step).
//
// This is the ONE window that merges two blocks instead of taking one:
// PlayerPanel's Quest block (the four qrow lines 区域 / 任务 / 剩余 / 猫车,
// panel_player.cpp:866-939) and its Conn block (the single meta row,
// panel_player.cpp:844-864) — design
// experiment/monster-overlay-v012/design/02-interaction-design.md §4.2
// 「方案 A」.
//
// WHAT "RE-DESIGNING THE LAYOUT" MEANS HERE
// ------------------------------------------
// In the panel the two blocks shared ONE qrow stream: `n_qrow` counted
// Conn's 1 row and Quest's 4 rows into a single number and
// `qrowStreamH = n*14 + (n-1)*3` (panel_player.cpp:732-734) gave every
// one of them the same 14px line and the same 3px gap, whether it was a
// 12px timer or an 8px PID. That is precisely what the user asked to
// change, and the change is that the two blocks now own their own pitch:
//
//   * Quest keeps the panel's 14px / 3px qrow metrics — the four rows are
//     a table of values and must line up with each other.
//   * Conn becomes an 11px meta line with 5px breathing above it, in
//     grey, at the BOTTOM. It is diagnostics (PID / BASE / party), not a
//     value you read at a glance, and 3px of extra height per line was
//     the price the shared stream charged it.
//
// So this window's height is not `n*14 + (n-1)*3` over five rows. It is
// the sum of two independently-measured streams, which is the whole
// point: a block that wants a different line height can now have one.
// The meta line is 11px for the same reason — see kMetaFontSize below.
//
// The vertical order is inverted from the panel on purpose. The panel
// leads with 已连接 · PID · BASE and follows with the four quest rows;
// here the remaining time (the teal qFontL12 number, the one a hunter
// glances at) is the FIRST thing at the top, and the process metadata
// drops to the foot of the window where it reads as provenance rather
// than as content.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row, and no PlayerViewModel: the identity resolver
// is the weapon window's dependency (its name/MR row), not this one's.
// Conn's party count is `snap.party.size()` computed here — the same
// expression PlayerViewModel::resolveIdentity performs (src/ui/viewmodel/
// player_view_model.cpp:26) — rather than a call into the VM, because
// the local-member override it adds is for MR/name/weaponId and this
// window draws none of those. HunterPie has no notion of process
// information at all; PID/BASE/party is our own diagnostic surface
// (the P1 note at panel_player.cpp:582-585, which is why it survives a
// missing quest instead of collapsing with it).
//
// The four quest painters below are this file's own copies of the ones
// panel_player.cpp:866-939 runs, written out in full on purpose: those
// live in that file's anonymous namespace and are unreachable from here,
// and hoisting them into a shared header would mean editing a file this
// change may not touch. Same for rankPrefix / starsStr / questStateLabel /
// zoneLabel / fmtMmSs. Each copy names the source line it mirrors.

#include "panel.h"
#include "core/game_snapshot.h"
#include "quest/quest_types.h"

#include <QString>

namespace mhw {

// One info window's resolved display state. Every field is a value the
// paint can hand straight to drawText — no Int, no state enum, no
// GameId. Only `state` and `isAnomaly` travel as data because they are
// INPUTS the label resolution needs (questStateLabel / the Lv branch),
// exactly the way the wirebug row carries `slot` rather than a resolved
// name (panel_player_split_wirebug.h:57-74).
struct InfoRow {
    QString zoneLabel;      // localized zone name, or 「未知区域」 / 「据点 N」 / 「区域 N」
    QString questName;      //「任务」 column value: rank + stars + state, or the idle placeholder
    QString questState;     // the state word alone (questStateLabel(state)) — re-resolved per paint
    QString timeLeft;      // mm:ss, or "--:--"
    int     deaths{0};
    int     maxDeaths{0};
    // Conn half. Computed here, not resolved by PlayerViewModel: the only
    // one of resolveIdentity's four outputs this window draws is
    // partyCount, and its party-override loop exists for MR / name /
    // weaponId (player_view_model.cpp:30-37), which are the weapon
    // window's business. An empty party is the honest "not in a session"
    // answer, which is what this row reports for a lone hunter.
    qint64          pid{-1};
    quint64         imageBase{0};
    int             partyCount{0};
    bool            connected{false};   // attach state, for the dot's colour
    bool            hasQuest{false};    // quest.active && id > 0 — the "#id" suffix
    int             questId{0};

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return !hasQuest && !connected;
    }
};

// The pure transformation: which metadata the snapshot carries, already
// shaped for paint. Deliberately NOT dispatched on GameId: unlike the
// abnormality blocks, quest and connection data are one struct each
// (QuestSnapshot / GameSnapshot's own fields) that BOTH readers fill with
// the same contract, so there is no per-game vocabulary to choose and a
// switch here would be an empty seam. The zone LABEL is the only
// per-game part, and it keeps the branch panel_player.cpp:162-175 uses.
[[nodiscard]] InfoRow buildInfoModel(const mhw::GameSnapshot &snap);

} // namespace mhw

// Quest + process metadata as its own layer-shell window, with its own
// panels.ini group ("player-info", NOT "player" or "player-status" —
// reusing either would make two windows fight over one position / scale /
// opacity section).
class PlayerInfoStripPanel : public Panel {
    Q_OBJECT

public:
    explicit PlayerInfoStripPanel(QWidget *parent = nullptr);

    // Full snapshot. Caches the resolved row for paintPanel() to read,
    // mirroring PlayerPanel's `player_` member + update() pattern.
    void update(const mhw::GameSnapshot &snap);

    // i18n: re-query the cached window title. Quest labels and the zone
    // name are resolved at paint time from the locale pair (the same
    // every-paint tr() the panel uses, panel_player.cpp:111-121), so a
    // language switch lands on the next paint without a model rebuild
    // and there is no seeded string here to leave stale.
    void retranslateUi() override;

    // A window carved out of PlayerPanel declares itself a sub-window so
    // the console's slot registry keeps it out of the four fixed slots
    // the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // "No data, no window" — with ONE deliberate exception, and it is the
    // reason this window is not shaped like the other five. A hunter who
    // has never entered a quest still has a PID and a base address, and
    // that is the diagnostic line the CachyOS reports are unreadable
    // without (panel_player.cpp:582-585). So an empty quest does NOT
    // collapse the window: it collapses to the meta line. Only a window
    // with neither a connection nor a quest, or one whose section bits
    // are both off, must not exist.
    //
    // Edit mode is the same exception it is everywhere else: a
    // design-mode surface has to stay mounted and draggable, so once the
    // demo is seeded the answer is true even though the row is synthetic.
    bool hasContent() const override;

private:
    // The section-mask gate, in the same role barsEnabled() /
    // mantlesEnabled() / wirebugEnabled() play for their windows. Reads
    // BOTH bits, because this window claims BOTH blocks: with either one
    // cleared the corresponding stream disappears from the height, the
    // layout and the paint at once.
    [[nodiscard]] bool infoEnabled() const;

    // Height the window needs, in logical px — the two streams measured
    // independently (see the header comment): Quest's 14px rows in the
    // panel's own pitch, then the 11px meta line with its own breathing
    // above it. A right margin closes the window on both sides.
    [[nodiscard]] int contentHeight() const;

    // Width the widest drawn line needs, measured in the same fonts the
    // paint sets. Never 378 — that was PlayerPanel's width and belongs
    // to it.
    [[nodiscard]] int contentWidth() const;

    // Which streams this snapshot actually draws. Both gates are read
    // once here and every consumer downstream (height, width, paint)
    // reads these two bools, not the mask — the pattern
    // panel_player.cpp:651-677 established.
    [[nodiscard]] bool questEnabled() const;
    [[nodiscard]] bool connEnabled() const;

    mhw::InfoRow row_{};
    // The two raw inputs the localized label fields are resolved from,
    // cached so retranslateUi() can re-resolve them against a new locale
    // without waiting for the next poll to rebuild the model. Same role
    // PlayerPanel's `zone_` / `quest_` members play for its own
    // retranslateUi().
    mhw::ZoneRef         zone_{};
    mhw::QuestSnapshot   quest_{};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it (and the row with it) so a demo
    // preview can never survive into a live session.
    bool demoData_{false};
};
