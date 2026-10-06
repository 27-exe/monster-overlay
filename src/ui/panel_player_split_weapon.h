#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// PlayerWeaponStripPanel — the v0.12 weapon sub-window.
//
// Fifth window out of the v0.12 "block → window" split of PlayerPanel (see
// panel_player_split.h for the pilot and the pattern's two promises).
// This one draws PlayerPanel's whole weapon row: the 26x26 weapon slot,
// the local player's name, the MR badge and the sharpness gauge —
// panel_player.cpp:944-1024, none of it left behind.
//
// WHY IT IS ONE WINDOW AGAIN
// ---------------------------
// v0.12 split the gauge out to its own window
// (PlayerSharpnessStripPanel, settingsKey "player-sharpness") and the
// user's verdict on that arrangement was that「武器身份三件套」and the
// gauge belong together: in HunterPie they are literally one control
// (HunterPie.UI/Overlay/Widgets/Player/Views/WeaponSharpnessView.xaml
// mounted in PlayerHudView.xaml:380-387). Two fragments at two
// addresses, both dragged on their own, is over-splitting — so the
// gauge moves here and its window disappears. `settingsKey` stays
// "player-weapon" (the primary key); the gauge window's key was never
// user-configured, so nothing has to migrate.
//
// WHAT IS NOT HERE
// ----------------
// No second copy of the gauge painter. `drawPlayerSharpnessBar()` in
// ui/panel_player_sharpness.{h,cpp} is a free function with no `this`
// and no PlayerPanel knowledge, and it is called from here the way
// panel_player.cpp calls it — the file is otherwise untouched and this
// is the second concrete payoff of that extraction (the first was the
// short-lived gauge window).
//
// The two Sections STAY INDEPENDENT. The window claims
// `Weapon | Sharpness`, and each bit still answers its own question
// (kPlayerSplitTargets, row 4): turning the weapon row off in the
// console hides the identity trio and keeps the gauge; turning the
// gauge off keeps the trio. Two bits, one window — the claim is a
// union, not a merge of the toggles.
//
// No chrome, no title row. The weapon-slot painter below is this file's
// own copy of drawWslot() (panel_player.cpp:224-236), which lives in an
// anonymous namespace in another translation unit; hoisting it into a
// shared header would mean editing a file this step may not touch. The
// copy names the source line it mirrors.

#include "panel.h"
#include "core/game_snapshot.h"
#include "player/player_types.h"
#include "ui/viewmodel/player_view_model.h"
#include "ui/panel_player_sharpness.h"
// v0.12 fourth wave: the shared split rhythm (kSplitMargin and the type
// stack). It was already the source of the numbers this file's own layout
// used; adding the override meant this header needed it by name.
#include "ui/panel_player_split_layout.h"

#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QPixmap>
#include <QString>

#include <cmath>

namespace mhw {

// One weapon row's resolved identity, exactly the four things the panel
// used to mirror into members plus the one the gauge reads: the weapon
// icon id, the player name, the master rank, whether a name resolved at
// all, and the sharpness block.
struct WeaponIdentityRow {
    int     weaponId{-1};      // <0 = no weapon; the slot renders empty
    QString name;              // empty = no local player resolved yet
    int     masterRank{0};     // <=0 renders the "MR --" placeholder
    // True when a real name came back. Distinguished from an empty
    // name because the panel's placeholder ("--") and a genuinely
    // absent name are different answers: one still reserves the row,
    // the other is what hasContent() reads to decide whether the
    // window should exist at all.
    bool    hasName{false};
    // The sharpness block, verbatim from GameSnapshot::player. Copied
    // in whole rather than field-by-field: it is 60 bytes, it is what
    // drawPlayerSharpnessBar() and riseSharpnessCurrentSegment() read,
    // and a copy that fell behind the struct would be a silent defect.
    mhw::SharpnessSnapshot sharpness{};

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return !hasName && weaponId < 0
               && !sharpness.valid;
    }
};

// The placeholder both the name column and the MR badge fall back to.
// The same strings panel_player.cpp:976 / :986 uses, so the two windows
// print identical dashes for identical unknowns.
[[nodiscard]] inline const QString &weaponIdentityNoName()
{
    static const QString kNoName = QStringLiteral("--");
    return kNoName;
}

// Whether the sharpness gauge has anything to draw: exactly the guard
// drawPlayerSharpnessBar() itself takes on entry
// (panel_player_sharpness.cpp:75-76), re-exported here so a caller who
// wants the answer without painting — the width reservation, and
// hasContent() — reads it from the same place rather than duplicating
// the range test.
[[nodiscard]] inline bool weaponIdentitySharpnessDrawable(
    const mhw::SharpnessSnapshot &s) noexcept
{
    return s.valid && s.level >= 0 && s.level <= 6;
}

// ---- The merged window's size arithmetic -----------------------------
//
// THE LAYOUT, as text. Two stacked rows that SHARE one width budget,
// with the weapon slot as the identity row's own left column:
//
//   [Weapon + Sharpness]        [Weapon only]         [Sharpness only]
//
//    m  m                        m  m                    m  m
//    m  +--------+-----+         m  +--------+           m  +--------+
//    m  |     9  |Name |   9     m  |     9  |Name |9    m  |     9  |
//    m  | +----+ + MR  |         m  | +----+ + MR  |     m  |        |
//    m  | |slot| +     |         m  | |slot| +      |     m  |        |
//        | | 26 |      |             | | 26 |       |         | [130]  |
//        | +----+      |             | +----+       |         |   +14  |
//        |             |             |              |         | +[30]  |
//        +------6------+             +--------------+         +--------+
//        m  +---------------------------+
//        m  |       [130] + 14 + [30]  |
//            +---------------------------+
//    m  m                        m  m                    m  m
//
//   m = kMargin (11), 9 = kWeaponGap, 6 = kIdentityLineGap.
//
// WHY THIS SHAPE. In the single-panel layout the gauge is centred in the
// row and the name gets the leftover band — a 49px strip that elided
// every non-empty player name. The identity-only window then fixed the
// name by dropping the gauge, and the gauge window fixed the gauge by
// dropping the name, and the two windows together measured 331px for
// what is one 196px row in HunterPie. Merging them back and putting the
// gauge UNDER the identity restores both without either cost: the name
// reads full width, the gauge gets its own 30px row so its 30px badge is
// no longer clipped by the panel's 26px prow (it used to start 2px
// ABOVE the row), and the two share one width.
//
// ---------------------------------------------------------------
// These five functions are the *whole* arithmetic, in the header and
// inline, for the same reason PlayerViewController's helpers and
// panel_sizing.h are: the width and the height are decisions the user
// sees, they are read by three callers (the reservation, the paint's
// placement, and the collapse branch) that must not disagree, and a
// test that cannot reach them is a test of nothing. It is QtCore-only
// here (QString + QFontMetrics + the sharpness width contract), so
// player_weapon_strip_tests can pin the actual numbers without a
// QGuiApplication — the same shape panel_player_split_info.cpp uses
// for its streamHeight().
// ---------------------------------------------------------------
namespace weapon_row {

constexpr int kMargin          = 11;   // HTML: .op padding:11
constexpr int kWslot           = 26;   // HTML: .wslot 26x26
constexpr int kWeaponGap       = 9;    // panel_player.cpp:724 (kGapSection)
constexpr int kIconTextGap     = 8;    // panel_player.cpp:960 (.prow gap:8)
constexpr int kIdentityLineGap = 6;    // THE NEW ONE — between the rows
// The gauge row's height. NOT the gauge's own 13px bar (kShpGH, private
// inside the painter) but its BADGE (kShpNum = 30), which is the tallest
// element that painter draws and the reason the panel's 26px prow let
// the badge start 2px above the row (panel_player_sharpness.cpp:89-90).
constexpr int kGaugeRowH       = 30;

// The identity row's height: the larger of the glyph pitch and the slot.
// THE PITCH RULE says a row must be at least ceil(QFontMetrics::height())
// for its font — 12pt Chakra Petch measures 21 here — and sizing the row
// to the font size (12) is the kMetaRowH = 11 mistake that clipped six of
// seventeen ink rows (panel_player_split_info.cpp:46-57). The slot lives
// on that row too, at the panel's own 26px, so the row is the max() of
// the two. That max() is load-bearing: a plain pitch (21) let a 26px slot
// stick 5px into the gauge row below it in the first draft of this
// layout.
inline int identityRowHeight() noexcept
{
    const int pitch = [] {
        QFont f(QStringLiteral("Chakra Petch"), 12, QFont::Medium);
        f.setStyleStrategy(QFont::PreferAntialias);
        return static_cast<int>(std::ceil(QFontMetrics(f).height()));
    }();
    return pitch > kWslot ? pitch : kWslot;
}

// The gauge block's width, in logical px: [130 gauge + 14 gap + 30 badge]
// — the number the exported playerSharpnessBarTotalWidth() returns, read
// through that accessor rather than re-spelled so the two cannot drift.
inline int gaugeStreamWidth() noexcept
{
    return playerSharpnessBarTotalWidth();
}

// The identity stream's width: the slot, the name, the MR badge and the
// two gaps between siblings. `nameW` must be measured in the font the
// painter sets.
inline int identityStreamWidth(int nameW, int mrW) noexcept
{
    return kWslot + kIconTextGap + nameW + kIconTextGap + mrW;
}

// Total content height for the streams that are on. Every stream pays
// for itself and nothing more — the rule panel_player_split_info.cpp's
// streamHeight() states, and the reason a gauge-only window is SHORTER
// than the panel's own 26px prow rather than the same height.
//
// NOTHING ON IS 2*kMargin AND NOTHING MORE. The first draft added the
// leading gap unconditionally, so a collapsed window (both bits off)
// measured 11 + 9 + 11 = 31 while the paint branch reserved
// 2*kMargin = 22 for the same state — a 9px disagreement between the
// window's own two answers about its own height. The gap belongs to
// the BAND, so it is paid only when there is a band to attach it to,
// and a window that mounts nothing is exactly the two margins.
inline int contentHeight(bool withIdentity, bool withGauge) noexcept
{
    int bandH = 0;
    if (withIdentity && withGauge)
        bandH = identityRowHeight() + kIdentityLineGap + kGaugeRowH;
    else if (withIdentity)
        bandH = identityRowHeight();
    else if (withGauge)
        bandH = kGaugeRowH;
    return bandH > 0 ? kMargin + kWeaponGap + bandH + kMargin
                     : 2 * kMargin;
}

// Total content width: the SHARED budget of the two streams, then the
// margin at both ends. The merge's one real saving — the pair's widths
// added up (135 + 196 = 331), the merged window takes max(135, 174) + 22
// = 196 for the same content, because the identity row's leftover width
// is exactly what the gauge row now gets for free.
inline int contentWidth(int identityW, int gaugeW) noexcept
{
    return (identityW > gaugeW ? identityW : gaugeW) + 2 * kMargin;
}

} // namespace weapon_row

// The pure transformation: who am I, what am I holding, and how sharp
// is it.
//
// Deliberately a thin, testable wrapper over PlayerViewModel::
// resolveIdentity() rather than a second implementation of it. That VM
// is the "hard rule" one — no widget or painter dependency — so it is
// reachable from a plain logic test and this window gets the same merge
// (party override, not-attached fallback) the panel gets, for free,
// including any future fix made there. The two decisions this function
// adds on top are the ones that are actually this window's own:
//
//   * hasName — resolveIdentity() returns an empty name both for "no
//     local player in the party" and for "the player struct is not
//     valid"; the panel's placeholder hides that distinction, this
//     window's hasContent() needs it.
//   * isEmpty — the "no data, no window" answer, which the panel never
//     needed because it is always visible. A live sharpness block now
//     counts as data on its own: a ranged weapon leaves it invalid, but
//     a melee weapon reads its every poll and the bar is worth a window
//     even when both the id and the name fail to resolve.
[[nodiscard]] WeaponIdentityRow buildWeaponIdentityModel(
    const mhw::GameSnapshot &snap);

} // namespace mhw

// The weapon identity trio plus the sharpness gauge as its own
// layer-shell window, with its own panels.ini group ("player-weapon",
// NOT "player" or "player-bars" — reusing either would make two windows
// fight over one position / scale / opacity section).
class PlayerWeaponStripPanel : public Panel {
    Q_OBJECT

public:
    // Panel's polymorphic layout contract. The weapon window answers
    // the extent hooks so a layout question gets its real numbers.
    [[nodiscard]] int contentWidth() const override;
    [[nodiscard]] int contentHeight() const override;

    // The outer inset — the same kSplitMargin every other split window
    // answers. It was missing here, which meant the console's preview drew
    // this window flush to its own edge while every sibling sat 11px in,
    // and the layout consistency test did not catch it because its list of
    // windows stopped at five.
    //
    // Reported rather than assumed, for the same reason the others are: a
    // ring asks the window instead of trusting six private constants to
    // agree by accident.
    [[nodiscard]] int contentMargin() const override
    {
        return mhw::split_layout::kSplitMargin;
    }

    // `subWindowIndex` is this window’s row in its parent’s split table. It
    // has to arrive at construction time (not via a setter) because
    // loadConfig() runs inside the base constructor and the index only
    // affects that call’s first-run default — see
    // defaultMarginsForSubWindow() in panel.cpp.
    explicit PlayerWeaponStripPanel(QWidget *parent = nullptr, int subWindowIndex = 0);

    // Full snapshot. Caches the resolved identity for paintPanel() to
    // read, mirroring PlayerPanel's `player_` member + update() pattern
    // — and unlike every other split window, resolving the identity
    // itself here rather than taking it as an argument, because
    // resolveIdentity() is pure and this is the same call the panel
    // makes (panel_player.cpp:564-568). One snapshot in, one model out;
    // the two windows cannot disagree about who the local player is.
    void update(const mhw::GameSnapshot &snap);

    // i18n: re-query the cached window title and schedule a repaint.
    // The demo name is re-seeded by that path (see setupDemoData()).
    void retranslateUi() override;

    // A window carved out of PlayerPanel declares itself a sub-window so
    // the console's slot registry keeps it out of the four fixed slots
    // the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

    // v0.13: the game this window's demo seed represents. Same shape and
    // same caller as PlayerStatusStripPanel::setGameForDemo. The harness
    // (validity, weapon id, MR, sharpness levels) is currently identical
    // in both games, so this changes nothing visible today — which is
    // exactly why it belongs here: it is the seam a future per-game
    // weapon (a Rise weapon id this table does not name) fills, instead
    // of a World-shaped hard-code that silently mislabels it.
    void setGameForDemo(mhw::GameId game);

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;


    // "No data, no window": no resolved name AND no weapon id AND no
    // drawable sharpness means the reader has nothing to say about who
    // is playing, so the window must not exist — the main loop's
    // hasVisibleContent() answers false and the layer-shell surface is
    // never mounted. Edit mode is the deliberate exception: a
    // design-mode surface has to stay mounted and draggable, so once
    // the demo is seeded the answer is true even though the hunter is
    // synthetic.
    bool hasContent() const override;

private:
    // The section-mask gates, in the same role barsEnabled() /
    // mantlesEnabled() / wirebugEnabled() play for their windows. TWO
    // of them, one per bit the window claims, and deliberately NOT
    // folded into a single `weaponEnabled() || sharpnessEnabled()`: the
    // user toggled them apart on purpose (panel_sections.h's bit
    // comments record why), and each one drops its own stream from the
    // width, the height and the paint independently.
    [[nodiscard]] bool weaponEnabled() const;
    [[nodiscard]] bool sharpnessEnabled() const;
    [[nodiscard]] bool identityOrSharpnessOn() const
    {
        return weaponEnabled() || sharpnessEnabled();
    }

    // ---- Layout ------------------------------------------------------
    //
    // The arithmetic lives above, in namespace mhw::weapon_row — the
    // geometry this window draws from, in the same place the model test
    // reads it, so there is exactly one definition of "how wide is this
    // row".
    //
    // The shape is HunterPie's: two stacked rows sharing one width
    // budget, with the weapon slot as the identity row's own left
    // column (their icon column spans both grid rows of the weapon
    // cell, PlayerHudView.xaml:361-377). In the single-panel layout the
    // gauge is centred in the row and the name gets the leftover band —
    // the 49px strip that elided every non-empty player name. Stacking
    // instead restores both: the name reads full width, and the gauge
    // gets its own 30px row so its 30px badge is no longer clipped by
    // the panel's 26px prow (it used to start 2px ABOVE the row).
    //
    // Row two's top is bandY + identityRowH + kIdentityLineGap, which is
    // why the band height is defined as exactly the rows it contains:
    // the offset and the height cannot disagree, so no stream can be
    // drawn outside the window.

    // Height, in logical px. The band for the streams that are on plus
    // the margin at both ends — see the arithmetic above. The default
    // (both bits on) is 93px, against the pair's 57 + 61 = 118 as two
    // windows.

    // Width, in logical px — the merge's one real saving, and why the
    // window is not just the two fragments' widths added:
    //
    //   identityStreamW = kWslot + 8 + nameW + 8 + mrW   (measured)
    //   gaugeStreamW    = playerSharpnessBarTotalWidth()  (130+14+30 = 174)
    //   contentW        = max(the streams that are on) + 2 * kMargin
    //
    // Both streams are drawn at the LEFT, flush with the same margin, so
    // their widths are directly comparable and the wider one is the
    // window's. A long player name is the only thing that beats the
    // gauge: the 9pt demo names measure 113 for the identity stream
    // against the gauge's 174, so the default window is 174 + 22 = 196
    // and the identity row simply leaves 61px of empty right edge.
    //
    // Every fallback is still a real measurement, never an elide: the
    // name is measured in the font it is drawn in, and each stream is
    // measured in the same helper paintPanel() places with.

    mhw::WeaponIdentityRow row_{};
    // v0.13: the game the demo below seeds. Same seam as the other five
    // split windows; see setGameForDemo above for why it is here even
    // though both games' harnesses agree today.
    mhw::GameId          game_{mhw::GameId::World};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it (and the row with it) so a demo
    // preview can never survive into a live session.
    bool demoData_{false};
};
