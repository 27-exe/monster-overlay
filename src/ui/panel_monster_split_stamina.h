#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// MonsterStaminaStripPanel — the v0.12 monster-panel split's
// 怪物耐力/疲劳条 (.bar.st) window.
//
// THE BLOCK THAT MADE THE BIT. It has shared a toggle with the HP bar
// since v0.8.4-r23 (`stamDrawn = onHp && maxStamina > 0`, with the
// comment "Follows the HP toggle until a dedicated section bit exists"),
// which was the right call for one panel and the wrong one for two
// windows: a fragment whose visibility rides on another fragment's bit
// cannot be switched off on its own. MonsterSection::Stamina (1u << 6)
// is that bit, and this window is what it is for.
//
// Same three-piece pattern as every v0.12 split window — see
// panel_player_split.h for the pilot's two promises (purely additive;
// no symbol-level borrowing) and
// panel_monster_split_hp.h for this split's variant of them.
//
// The fatigue meter is a genuinely separate question from "how hurt is
// my target": HunterPie draws it as its own gauge directly under the HP
// bar (BossMonsterWidget), and the drooling / exhausted state the player
// reacts to is stamina == 0. A window that answers only that question is
// the whole reason to split it out rather than let it ride on the HP
// window alongside the bar it happens to sit under.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row. The HP bar is NOT in this window — it is
// MonsterHpStripPanel (settingsKey "monster-hp"), the first row of
// kMonsterSplitTargets. The 体力 label on the left of the bar is this
// window's own (ui.monster_stamina, the same key the panel's block uses),
// which is why this window does cache a translated string and does drop
// its demo seed on a locale flip.

#include "panel.h"
#include "monster/monster_types.h"
#include "ui/formatters.h"
// MonsterSection::Stamina — the mask axis. This is the bit that did not
// exist before v0.12; see its comment and
// MonsterSection::migrateOldMonsterStaminaBit for the back-compat rule.
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm. Included here, not only in the .cpp, because
// contentMargin() is an inline member that names a constant.
#include "ui/panel_player_split_layout.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QString>

namespace mhw {

// One bar's resolved display state — what paintPanel() draws and what
// the test pins.
struct MonsterStaminaRow {
    // Fraction of the track filled, always within [0,1]. Zero when the
    // maximum is unknown, so the bar draws its empty track rather than a
    // full one that claims a value nobody read.
    float pct{0.0F};
    // Whether the numbers on the bar are trustworthy. maxStamina == 0
    // is the panel's own "the 0x320 fatigue chain did not resolve"
    // condition (panel_monster.cpp's stamDrawn), and the same one here.
    bool known{false};
    // "700 / 1,000" (right of the bar). The label on the left is a
    // translated string, not a model field: the model carries values,
    // the paint asks the table.
    QString valueText;
};

// The window's whole content model. A struct rather than a bare pair
// because the two texts' placeholder fold is the load-bearing part.
struct MonsterStaminaModel {
    MonsterStaminaRow stamina;

    [[nodiscard]] bool isEmpty() const noexcept
    {
        // With the fatigue pair unresolved there is no meter to draw —
        // which is also exactly when the panel hides the row. A window
        // in that state must not mount.
        return !stamina.known;
    }
};

// A stable value for the placeholder pair the bar falls back to, so the
// demo seeder and the reader path print identical dashes.
[[nodiscard]] inline constexpr const char *monsterStaminaPlaceholderValue()
{
    return "-- / --";
}

// The pure transformation for this window: the meter's display state out
// of a monster snapshot. No dispatch on GameId — the fatigue pair is the
// same shape in both games (panel_monster.cpp's .bar.st block reads no
// game branch either), so a game switch here would be a second opinion,
// not a variant of the same one.
[[nodiscard]] MonsterStaminaModel buildMonsterStaminaModel(
    const mhw::MonsterSnapshot &m);

} // namespace mhw

// 怪物耐力条 as its own layer-shell window, with its own panels.ini group
// ("monster-stamina", NOT "monster-hp" or "monster" — reusing either
// would make two windows fight over one position / scale / opacity
// section, and reusing "monster" would fight with MonsterPanel itself).
class MonsterStaminaStripPanel : public Panel {
    Q_OBJECT

public:
    // `subWindowIndex` is this window’s row in its parent’s split table. It
    // arrives at construction time because the base Panel constructor calls
    // loadConfig(), and the index exists only to pick that call’s first-run
    // default — see defaultMarginsForSubWindow() in panel.cpp. A setter
    // would always be one call too late.
    explicit MonsterStaminaStripPanel(QWidget *parent = nullptr, int subWindowIndex = 0);

    // Full monster snapshot. Caches the resolved meter for paintPanel()
    // to read, mirroring MonsterPanel's `monster_` member + update().
    void update(const mhw::MonsterSnapshot &m);

    // i18n: this window DOES cache one translated string of its own —
    // the 体力 label — so a locale swap must re-query it. The demo seed
    // writes only numbers, so it is not dropped; the label is read at
    // paint time from the same table.
    void retranslateUi() override;

    // A window carved out of MonsterPanel declares itself a sub-window
    // so the console's slot registry keeps it out of the four fixed
    // slots the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

    // ─── Panel's polymorphic layout contract ────────────────────────
    [[nodiscard]] int contentWidth() const override;
    [[nodiscard]] int contentHeight() const override
    {
        return barBlockHeight();
    }
    [[nodiscard]] int contentMargin() const override
    {
        return mhw::split_layout::kSplitMargin;
    }

    // Which game this window's demo seed represents. The fatigue meter's
    // content is game-agnostic (see buildMonsterStaminaModel), so the
    // seed does not vary today; the setter exists for the same reason
    // every sibling has it — main.cpp pushes the resolved game into
    // every window so a per-game seed needs no new wiring.
    void setGameForDemo(mhw::GameId game);

    // v0.15: the virtual dispatch path — see panel_player_split_mantles.h
    // for why a Panel* call site (the Console's split preview host) needs
    // an override rather than another non-virtual overload.
    void setDemoGame(mhw::GameId game) override;

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // "No data, no window", the split's shared rule. Edit mode is the
    // deliberate exception — a design-mode surface stays mounted and
    // draggable — so once the demo is seeded the answer is true even
    // though the number is synthetic.
    bool hasContent() const override;

private:
    // The section this window draws, as the mask leaves it. The mask
    // axis is separate from the data axis; reusing MonsterSection::
    // Stamina means the console's monster-section toggle drives this
    // window with no new UI beyond the row the bit itself adds.
    [[nodiscard]] bool staminaEnabled() const;

    // Height of the whole block, in logical px: the bar, its vertical
    // padding, and the margins at both ends. Mirrors the panel's
    // `kStamBarH + 2 * kBarVPad` inside its block table
    // (panel_monster.cpp's stamH), called from one place so the
    // reservation and the draw cannot disagree.
    [[nodiscard]] int barBlockHeight() const;

    // Width the row needs for its label plus its numbers, in logical
    // px, from the fonts paintPanel() actually sets. Never 380.
    [[nodiscard]] int barContentWidth() const;

    mhw::MonsterStaminaModel model_{};
    // v0.12: the game the demo below seeds. Seeded to World so the
    // window's very first (pre-injection) preview is the historical
    // one; main.cpp overwrites it before the first paint.
    mhw::GameId        game_{mhw::GameId::World};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it so a demo preview cannot survive
    // into a live session.
    bool               demoData_{false};
};
