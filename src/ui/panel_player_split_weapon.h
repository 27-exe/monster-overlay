#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// PlayerWeaponStripPanel — the v0.12 weapon-identity sub-window.
//
// Fifth window out of the v0.12 "block → window" split of PlayerPanel (see
// panel_player_split.h for the pilot and the pattern's two promises).
// This one draws the IDENTITY TRIO of PlayerPanel's weapon row: the 26x26
// weapon slot, the local player's name, and the MR badge —
// panel_player.cpp:944-1024, minus the sharpness gauge.
//
// WHY PARTIAL, AND WHY THAT IS THE POINT
// -------------------------------------
// The source row is [wslot 26] [name] 8 [MR] ... [gauge 130 + 14 + badge
// 30 = 174], so the whole row needs 352px of the panel's 356px inner
// width. Moving it whole would buy nothing (design
// 02-interaction-design.md §3.2/§3.4): the same 352px, now with the
// compositor in the way, and a window that alone covers the panel it was
// carved from.
//
// The trio alone is the part the panel was getting WRONG. The gauge is
// centred in the row, so the name column got whatever was left between
// the icon and the gauge's effective left edge — 49px for a 135px
// name+MR pair (probe weapon_row_probe.cpp). Every non-empty player name
// therefore took the elide branch (panel_player.cpp:1009-1023) in
// single mode. As its own window the trio is measured instead of
// squeezed: wslot + 8 + name + 8 + MR, ≈170px, no elision, ever.
//
// WHAT IS NOT HERE
// ----------------
// The sharpness gauge. Three reasons, all in §3.4 of the design: it is
// 174px of hard cost with no width return; `player_.sharpness` is Rise
// melee-only data with its own valid branch; and its right-edge
// avoidance arithmetic (gaugeLeftX / gaugeLeftEffective) is welded to
// the name column that just left the panel. The bar therefore stays in
// PlayerPanel and is drawn by the free function in
// panel_player_sharpness.{h,cpp} — that file is untouched, and this one
// does not include it.
//
// The mask consequence is why PlayerSection::Sharpness exists: the
// window claims `Weapon` and NOT `Sharpness`, so in split mode the
// panel keeps the bar's bit and the bar keeps drawing. Turning the
// weapon row off in the console now hides the trio only.
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

#include <QPainter>
#include <QFontMetrics>
#include <QPixmap>
#include <QString>

namespace mhw {

// One weapon row's resolved identity, exactly the four things the panel
// used to mirror into members: the weapon icon id, the player name, the
// master rank, and whether a name resolved at all.
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

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return !hasName && weaponId < 0;
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

// The pure transformation: who am I, and what am I holding.
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
//     needed because it is always visible.
[[nodiscard]] WeaponIdentityRow buildWeaponIdentityModel(
    const mhw::GameSnapshot &snap);

} // namespace mhw

// The weapon identity trio as its own layer-shell window, with its own
// panels.ini group ("player-weapon", NOT "player" or "player-bars" —
// reusing either would make two windows fight over one position / scale
// / opacity section).
class PlayerWeaponStripPanel : public Panel {
    Q_OBJECT

public:
    explicit PlayerWeaponStripPanel(QWidget *parent = nullptr);

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

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // "No data, no window": no resolved name AND no weapon id means the
    // reader has nothing to say about who is playing, so the window must
    // not exist — the main loop's hasVisibleContent() answers false and
    // the layer-shell surface is never mounted. Edit mode is the
    // deliberate exception: a design-mode surface has to stay mounted
    // and draggable, so once the demo is seeded the answer is true even
    // though the hunter is synthetic.
    bool hasContent() const override;

private:
    // The section-mask gate, in the same role barsEnabled() /
    // mantlesEnabled() / wirebugEnabled() play for their windows. Reads
    // `Weapon`, NOT `Sharpness`: the sharpness bar keeps its own bit and
    // stays inside PlayerPanel (panel_sections.h), so turning the weapon
    // row off in the console hides this trio and leaves the bar alone.
    [[nodiscard]] bool weaponEnabled() const;

    // The row's height, in logical px. The panel reserves kGapSection +
    // kWeaponH = 9 + 26 (panel_player.cpp:725-726) for the same block,
    // and this window keeps the 26 plus the 9 leading gap so an icon
    // drawn side by side with the panel's is the same size and sits at
    // the same distance from the window's top edge.
    [[nodiscard]] int contentHeight() const;

    // Width the trio needs, in logical px: wslot + 8 + name + 8 + MR,
    // measured in the two fonts the paint sets (the pair
    // panel_player.cpp:953 / :987 uses). Never 378 — that was
    // PlayerPanel's width and belongs to it.
    [[nodiscard]] int contentWidth() const;

    mhw::WeaponIdentityRow row_{};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it (and the row with it) so a demo
    // preview can never survive into a live session.
    bool demoData_{false};
};
