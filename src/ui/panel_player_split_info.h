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
//   * Quest keeps 9pt labels with 10pt values.
//   * Conn becomes a 9pt meta line in grey, at the BOTTOM. It is
//     diagnostics (PID / BASE / party), not a value you read at a
//     glance.
//
// So this window's height is not `n*14 + (n-1)*3` over five rows. It is
// the sum of three independently-measured LINES, each reserving
// ceil(QFontMetrics::height()) for the tallest font that line draws —
// which is the whole point: a block that wants a different line height
// can now have one, and the reservation can never again be a font size
// reused as a row height. See the .cpp's rowPitch() for why that one
// formula and not the other two metrics on offer.
//
// THE LAYOUT (v0.12.1)
// --------------------
// Four stacked qrows became two lines of two columns each, because both
// pairs fit side by side and the width was already being paid for:
//
//     line 1   区域 | 任务          (9pt labels, 10pt values)
//     line 2   剩余 | 猫车          (12pt teal timer, 10pt carts)
//     line 3   ● 已连接 · PID · BASE    4人 · #66801
//
// The old five-row layout cost 101px of height and had the widest line
// (the meta text at 11pt) dictate a 420px window for rows carrying
// 60–120px of content each. The window is now 345x82 — see the .cpp's
// streamHeight() for the arithmetic, and the tests for the numbers as
// assertions.
//
// The vertical order is inverted from the panel on purpose. The panel
// leads with 已连接 · PID · BASE and follows with the four quest rows;
// here the remaining time (the teal 12pt number, the one a hunter
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
// THE THREE STATES (restored)
// ---------------------------
// The split left this window with a BINARY conn line: connected ?
// 已连接 · PID · BASE : ○ 未连接. PlayerPanel had THREE
// (panel_player.cpp:586-623) — offline, attached-but-nothing-resolved,
// and live — and the middle one is the one the CachyOS reports were
// unreadable without, because it carried the reader's own reason on its
// own row ("cannot open the address map" / "waiting for
// MonsterHunterWorld.exe" / "read denied (EPERM)" all painted the same
// unexplained empty panel before v0.9.1 P1).
//
// So the model now carries `hasData` and `readerStatus` next to the
// attach flag `connected` already holds, and the window draws the
// reader's reason as its own line whenever the state is not live. The
// three are distinguishable at a glance, and the diagnostic is never
// swallowed by a "Connected · PID --" that is not true.
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
#include "panel_player_split_layout.h"

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
    //
    // `connected` is the ATTACH state (snap.attached) — whether a process
    // was found and its memory opened. It is NOT "the overlay has
    // something to show", which is the conflation that lost PlayerPanel's
    // middle state in the split: a snapshot with no game running still
    // answered attached=true on the first tick after a reader reload.
    // `hasData` is the data state (snap.player.valid) and `readerStatus`
    // is the reader's own reason for the state it is in (snap.status) —
    // the two inputs panel_player.cpp:545-548 read together, restored
    // here so the diagnostic line can be drawn again.
    qint64          pid{-1};
    quint64         imageBase{0};
    int             partyCount{0};
    bool            connected{false};   // attach state, for the dot's colour
    bool            hasData{false};     // player.valid — anything resolved at all
    QString         readerStatus;       // snap.status — the reader's own reason
    bool            hasQuest{false};    // quest.active && id > 0 — the "#id" suffix
    int             questId{0};

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return !hasQuest && !connected;
    }

    // The three states of the conn stream, in the order PlayerPanel
    // distinguished them (panel_player.cpp:586-587):
    //
    //   Offline   — nothing attached; no game process was found.
    //   NoData    — attached, but not a single field resolved. The state
    //               the CachyOS reports were filed under, because a
    //               denied read, a missing address map and a waiting
    //               process all painted the same unexplained empty panel
    //               until v0.9.1 P1 made each one print its own reason.
    //   Live      — attached and the player struct resolved.
    //
    // A predicate, not an enum field, so the model cannot carry a state
    // that disagrees with the two booleans that produce it.
    enum class State { Offline, NoData, Live };

    [[nodiscard]] State state() const noexcept
    {
        if (!connected)
            return State::Offline;
        return hasData ? State::Live : State::NoData;
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
    // ─── the split's shared rhythm, answered the same way by all seven ──
    //
    // kSplitMargin, the same constant the .cpp's own kMargin is now an
    // alias for. This window already had the tightest pitch arithmetic
    // of the seven (every row height measured off QFontMetrics) and the
    // shared type scale 9/10/12 came from here, so it is the window the
    // others were made to match — this override is the half of that
    // promise a layout can actually verify.
    [[nodiscard]] int contentMargin() const override
    {
        return mhw::split_layout::kSplitMargin;
    }

    [[nodiscard]] int contentHeight() const override;
    [[nodiscard]] int contentWidth() const override;
    // `subWindowIndex` is this window’s row in its parent’s split table. It
    // has to arrive at construction time (not via a setter) because
    // loadConfig() runs inside the base constructor and the index only
    // affects that call’s first-run default — see
    // defaultMarginsForSubWindow() in panel.cpp.
    explicit PlayerInfoStripPanel(QWidget *parent = nullptr, int subWindowIndex = 0);

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

    // v0.18: TWO PREVIEWS, NOT ONE. This window's seed used to answer
    // "what will the overlay draw when a quest is running", which is the
    // right question for a LAYOUT preview and the wrong one for a
    // STATUS preview — and the console's split preview is the second.
    //
    // The user's report, verbatim: with no game running, the console's
    // player-info tile showed 「区域 古代森林 · 任务 … / 剩余 … / 猫车 …」
    // plus a live connection line. What he wanted was the verdict:
    // 「○ 未连接」. Nothing in the preview told him the truth about his
    // overlay, because Panel::paintEvent() runs setupDemoData() on the
    // first paint while editMode_ is true (panel.cpp:1153-1156), and
    // this window's seed unconditionally built an attached+live+in-quest
    // snapshot — the one state the three-state machine was built to
    // distinguish, papered over on every paint.
    //
    // So the seed no longer answers alone. The live overlay keeps the
    // real seed (a design-mode surface has to be draggable and
    // identifiable, and `--edit` is where that matters); the console
    // asks for the honest state instead, through setStatePreview().
    //
    // WHY THE STATE IS A SNAPSHOT AND NOT AN ENUM. A GameSnapshot is
    // what the live path takes and what buildInfoModel() consumes, so
    // routing the console through it means the preview runs the SAME
    // transformation the overlay runs — including the zone vocabulary,
    // the quest-state wording and the reader-diagnostic line. A
    // three-valued enum would have forced a second answer to "what
    // does each state print" that drifts from the first one the moment
    // a wording changes.
    //
    // WHY IT IS A VIRTUAL ON THE WINDOW AND NOT A CONSOLE-SIDE BRANCH.
    // The console holds these windows as std::unique_ptr<Panel>, so a
    // call site on a Panel* needs runtime dispatch (the v0.15 lesson:
    // a Derived* overload set is resolved statically and never runs).
    void setStatePreview(const mhw::GameSnapshot &snap);

    // v0.13: the game this window's demo seed represents. The zone label
    // is resolved through a per-game vocabulary, so a preview that
    // ignores the game asks the wrong table for the zone's name. Same
    // shape and same caller as PlayerStatusStripPanel::setGameForDemo.
    void setGameForDemo(mhw::GameId game);

    // v0.15: the virtual dispatch path — see panel_player_split_mantles.h
    // for why a Panel* call site (the Console's split preview host) needs
    // an override rather than another non-virtual overload.
    void setDemoGame(mhw::GameId game) override;
    // The other half of the same pair: a setter with no reader is a
    // change that cannot be observed, and the seed bug this round
    // fixed was a dispatch that could not be observed.
    [[nodiscard]] mhw::GameId demoGame() const override { return game_; }

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

    // Height the window needs, in logical px — one metric-derived pitch
    // per drawn line (ceil(QFontMetrics::height()) for that line's
    // tallest font), one gap between consecutive drawn lines, margins on
    // both sides. See the header comment and the .cpp's streamHeight().

    // Width the widest drawn line needs, measured in the same fonts the
    // paint sets and ceiled the same way. Never 378 — that was
    // PlayerPanel's width and belongs to it.

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
    // v0.13: the game the demo below seeds. The demo's zone label comes
    // from a per-game vocabulary (zoneLabel() dispatches on
    // ZoneRef::game), so a world-shaped id under a Rise rail would ask
    // the World table for a name Rise never assigned — or fall through to
    // "outpost 4". Same shape and same caller as
    // PlayerStatusStripPanel::setGameForDemo.
    mhw::GameId          game_{mhw::GameId::World};
    // v0.18: which of the two previews is on screen. False means the
    // honest one: the row came from setStatePreview(), a real (possibly
    // empty) snapshot describing what the reader would actually find
    // right now. True means the layout one: the row came from
    // setupDemoData(), an attached+live+in-quest fiction whose job is to
    // make a design-mode surface draggable and identifiable.
    //
    // This is deliberately NOT the base's demoPrimed_ (which is public
    // and one-shot) and not the old demoData_ (which was set by the seed
    // and never had a second value). The distinction it carries is the
    // one the console could not see: a window in edit mode with a
    // truthful row and a window in edit mode with a seeded one, which
    // are the same state under every other flag this class has.
    bool                 demoSeed_{false};
};
