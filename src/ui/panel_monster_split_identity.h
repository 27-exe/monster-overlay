#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// MonsterIdentityStripPanel — the v0.13 monster-panel split's Identity
// window (settingsKey "monster-identity").
//
// WHY TWO WINDOWS AND NOT FIVE
// -----------------------------
// The user's report on the v0.12 five-window split: 「怪物的切太狠了，
// 应该图标，激怒，血量是在一块，异常状态和部位在一块」. Five single-block
// windows made the user assemble the picture themselves every hunt; the
// split now asks the ONE question the blocks actually answer, twice.
//
//   Identity   = the monster as an ACTOR. Who it is (六角肖像 + 金冠 +
//                名字 + 大小 + the enrage tag) and how it is doing
//                (HP 条 / 耐力条 / 怒气条). A glance.
//   Conditions  = the monster as an OPPONENT (异常状态卡 + 部位网格).
//                A plan.
// This is the first of the two. panel_sections.h's kMonsterSplitTargets
// records the full argument for the grouping; the short form is that
// identity is the window a hunter looks at reflexively and conditions
// is the one they open deliberately.
//
// The layout a window draws, in the panel's own stacking order:
//
//   +---------------------------------------------------+
//   |  (hex)   火龙 [ID 001]                            |   <- Info
//   |          ⧗ 1.25×       激怒 32s                    |
//   +---------------------------------------------------+
//   | 18,420 / 25,800                          71.5%    |   <- HP
//   +---------------------------------------------------+
//   | 体力                              700 / 1,000     |   <- Stamina
//   +---------------------------------------------------+
//   |                              ████████░░░░░ 62%    |   <- Enrage
//   +---------------------------------------------------+
//
// Every block is INDEPENDENTLY TOGGLEABLE, which is what keeps the
// user's 「还可选择性的关闭部分，比如激怒条」 true:
//
//   * MonsterSection::Info (1u<<0), ::Hp (1u<<1), ::Stamina (1u<<6)
//     and ::Enrage (1u<<2) each gate their own block's WIDTH
//     RESERVATION, HEIGHT RESERVATION and PAINT — never the window's
//     mount. The window mounts while ANY of the four is on.
//   * The section mask arrives as the union (kMonsterSplitTargets row 0
//     = Info|Hp|Enrage|Stamina) intersected with the user's toggles,
//     and the window divides it back out per block. See the
//     identityInfoEnabled() / hpEnabled() / staminaEnabled() /
//     enrageEnabled() quartet: ONE is called from every consumer of
//     its block, so a turn-off cannot leave a reserved row behind.
//   * The row-level test an OUTER layer could apply (is the union set)
//     is exactly what is NOT enough: identity with Enrage off must still
//     be a window. So the block gates read the mask themselves, and the
//     window's hasContent() answers "any block on".
//
// Same three-piece pattern as every v0.12/v0.13 split window — see
// panel_player_split.h for the pilot's two promises.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row: this window is a fragment of the monster
// panel, not a panel. setChromeVisible(false) + isSubWindow() true.
//
// The 异常状态卡 and 部位网格 are NOT here — they are
// MonsterConditionsStripPanel (settingsKey "monster-conditions"), the
// second row of the table. 勾爪软化 (Tenderize) travels with the parts
// cards, as it has since v0.7.4 PR C.

#include "panel.h"
#include "core/game_tables.h"
#include "monster/monster_types.h"
#include "ui/formatters.h"
// The four mask axes, the same bits MonsterPanel reads (panel_monster.cpp
// :965-998). The window's own gates below name each one.
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The three bars' models come from the windows that already own them
// rather than being restated here. Restating them is an ODR violation the
// moment both headers reach one TU, and both DO reach one TU:
// SplitWindowPreviewHostImpl in control_panel.cpp constructs every split
// window, so it sees identity's copies alongside hp's and enrage's own.
//
// This is the same mistake the conditions window already made once (with
// MonsterAilCard) and it fails the same way: "redefinition of struct
// mhw::MonsterBarRow" at compile time, and a link-time multiple-definition
// if the types happen to be inline-only. Including the owner is the fix in
// both cases, and it keeps one spelling of the model.
#include "ui/panel_monster_split_hp.h"
#include "ui/panel_monster_split_enrage.h"
#include "ui/panel_monster_split_stamina.h"
// The shared split rhythm (kSplitMargin, the gap grades, the font stack)
// every split window answers contentMargin() from. Included here, not
// only in the .cpp, because contentMargin() is an inline member.
#include "ui/panel_player_split_layout.h"

#include <QElapsedTimer>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QString>

#include <algorithm>
#include <cmath>

namespace mhw {

// ─── the three bar rows, each resolved once ──────────────────────────
//
// One struct per block rather than one struct for the window, because
// the blocks are separately toggleable and a test that wants to pin
// "Enrage off reserves no row" has to be able to ask the model that
// question without asking the portrait as well. The fields are the
// panel's own, folded through the same conversions.

// MonsterBarRow and MonsterEnrageReading come from the windows that own
// them (panel_monster_split_hp.h / _enrage.h) and are NOT restated here —
// see the include note above for why restating them breaks the build the
// moment control_panel.cpp constructs every split window in one TU.
//
// What IS here is the identity-specific part: the window's whole content
// model, its four independent block gates, and its geometry.

// The Identity window's whole content model. Three bars plus the
// identity fields, resolved from ONE snapshot.
struct MonsterIdentityModel {
    // ── Info block (MonsterSection::Info) ──
    // The monster identity the model was resolved from, carried the way
    // the panel carries `monster_.address`: the reset-on-switch key.
    std::uintptr_t address{0};
    // Whether an authoritative snapshot has arrived at all. Separate
    // from address because a snapshot with an unresolved HP pair still
    // says "a monster exists" — it is the panel's own hasData_.
    bool hasData{false};
    int            monsterId{-1};
    mhw::GameId    game{mhw::GameId::World};
    // The display name. Rise's MonsterData.xml has no localisation key,
    // so the panel's own fallback applies: internalName, else
    // "Monster %1". Resolved in the model so the paint asks for a
    // string, not a rule.
    QString        name;
    // 0 means "unknown" and suppresses the crown AND the size chip.
    float          size{0.0F};
    bool           enraged{false};
    // The enrage timer pair, carried because the identity block's
    // enrage TAG prints the ELAPSED seconds (panel_monster.cpp:1213),
    // which is a subtraction of these two and nothing else. Rounded up
    // nowhere — the panel prints the raw int difference.
    float          enrageMaxSeconds{0.0F};
    float          enrageSeconds{0.0F};

    // ── the three bars ──
    MonsterBarRow health;
    MonsterBarRow stamina;
    // Enrage has no `known`: the panel draws the block whenever
    // `enrageMaxBuildup > 0 || enraged`, which is what `reading`
    // already encodes.
    float pct{0.0F};
    MonsterEnrageReading reading{MonsterEnrageReading::Empty};
    // The monster is enraged AND the timer is live — the condition the
    // 1.6 s pulse runs on. Carried separately from `reading` because a
    // snapshot can be Max without a live seconds value.
    bool pulsing{false};

    [[nodiscard]] bool hasHealth() const noexcept { return health.known; }
    [[nodiscard]] bool hasStamina() const noexcept { return stamina.known; }
    [[nodiscard]] bool hasEnrage() const noexcept
    {
        return reading != MonsterEnrageReading::Empty || pulsing;
    }
};

// A stable value for the placeholder pair the bars fall back to, so
// the demo seeder and the reader path print identical dashes. The same
// two constants panel_monster_split_hp.h / _stamina.h ship — spelled
// here rather than included so the Identity window does not depend on
// the five single-block headers (which the parent session deletes).
[[nodiscard]] inline constexpr const char *monsterIdentityBarPlaceholder()
{
    return "-- / --";
}

// The pure transformation for this window: the four blocks' display
// state out of ONE monster snapshot. Reads the mask only for what the
// snapshot cannot answer (nothing — the model is mask-free; the mask
// gates the DRAW and the RESERVATION, both in the window).
//
// NO GameId dispatch in here: the three bars' folds are game-agnostic
// in the panel too, and the crown / capture lookups stay at paint time.
[[nodiscard]] MonsterIdentityModel buildMonsterIdentityModel(
    const mhw::MonsterSnapshot &m);

// ─── the identity block's geometry, shared by reservation and paint ──
// The panel's own kHexW/kHexH (panel_monster.cpp:78-79) and its crown
// token (:80). The window is 48x54 hex + a gap, exactly the panel's
// hexwrap; the identity block's height is kTitleH + gap + kHexH the
// way panel_monster.cpp's kInfoH is.
inline constexpr int kMonsterIdentityHexW = 48;
inline constexpr int kMonsterIdentityHexH = 54;
inline constexpr int kMonsterIdentityCrownSize = 15;
inline constexpr int kMonsterIdentityTitleH = 14;
inline constexpr int kMonsterIdentityNameFont = 15;

// The pulse period, in ms. panel_monster.cpp:125's kPulsePeriodMs =
// 1600 (HTML's .erpulse 1.6s), run on this window's own clock for the
// same reason the single-block enrage window ran its own: the two can
// be mounted at different times.
inline constexpr int kMonsterEnragePulsePeriodMs = 1600;

// Rows needed to lay the identity block's second row out (size chip +
// enrage tag) — a single row, but spelled through the same helper the
// grids use so the reservation is not a second arithmetic.
[[nodiscard]] inline constexpr int monsterIdentityInfoHeight() noexcept
{
    return kMonsterIdentityTitleH + mhw::split_layout::kSplitGapBetween
           + kMonsterIdentityHexH;
}

} // namespace mhw

// 肖像 + 名字 + 大小 + 三条状态条 as its own layer-shell window, with
// its own panels.ini group ("monster-identity", NOT "monster" —
// reusing it would make two windows fight over one position / scale /
// opacity section).
class MonsterIdentityStripPanel : public Panel {
    Q_OBJECT

public:
    // `subWindowIndex` is this window's row in its parent's split table.
    // It arrives at construction time because the base Panel constructor
    // calls loadConfig(), and the index exists only to pick that call's
    // first-run default — see defaultMarginsForSubWindow() in panel.cpp.
    // A setter would always be one call too late.
    explicit MonsterIdentityStripPanel(QWidget *parent = nullptr,
                                       int subWindowIndex = 0);

    // Full monster snapshot. Caches the resolved model for paintPanel()
    // to read, mirroring MonsterPanel's `monster_` member + update().
    void update(const mhw::MonsterSnapshot &m);

    // i18n: the translated strings this window draws are re-read at the
    // DRAW site (the 体力 label, the demo monster name) rather than
    // cached in the model, so a locale flip needs no re-seed — only the
    // title re-query and a repaint.
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

    // ─── the four per-block gates, PUBLIC ON PURPOSE ────────────────
    //
    // These are the mechanism behind 「还可选择性的关闭部分」. They are
    // public rather than private because the honest way to prove that
    // turning 激怒 off really stops the enrage row from being reserved
    // is to ask the window, not to look at a screenshot.
    //
    // Each reads its OWN MonsterSection bit out of the section mask the
    // window was handed (which is the row's union intersected with the
    // user's toggles). The window itself is mounted by "ANY block on",
    // never by "all of them" — see hasContent().
    [[nodiscard]] bool identityInfoEnabled() const;
    [[nodiscard]] bool hpEnabled() const;
    [[nodiscard]] bool staminaEnabled() const;
    [[nodiscard]] bool enrageEnabled() const;

    // Which game this window's demo seed represents. The crown tables
    // and the capture colour are game-keyed, so a Rise preview would
    // otherwise draw World colours — the same class of mismatch the
    // user reported for 衣装/翔虫.
    void setGameForDemo(mhw::GameId game);

    // v0.15: the virtual dispatch path — see panel_player_split_mantles.h
    // for why a Panel* call site (the Console's split preview host) needs
    // an override rather than another non-virtual overload.
    void setDemoGame(mhw::GameId game) override;

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // "No data, no window", the split's shared rule — with the
    // Identity twist: the window answers TRUE while ANY of its four
    // blocks is on and has data. Edit mode is the deliberate exception
    // (a design-mode surface stays mounted and draggable), so once the
    // demo is seeded the answer is true even though the numbers are
    // synthetic.
    bool hasContent() const override;

private:
    // The four blocks' heights, each leaving no gap behind when its own
    // bit is off. Mirrors panel_monster.cpp's per-block reservation
    // (:1072-1082) rather than re-deriving one.
    [[nodiscard]] int infoBlockHeight() const;
    [[nodiscard]] int hpBlockHeight() const;
    [[nodiscard]] int staminaBlockHeight() const;
    [[nodiscard]] int enrageBlockHeight() const;

    // The width the identity block's text needs. The panel's identity
    // row runs to its natural width inside a 380px panel; a window
    // measures the same strings and reserves that. Both this and the
    // bars' width reservations call the shared
    // mhw::panel_sizing::effectiveWidth() floor.
    [[nodiscard]] int identityTextWidth() const;
    [[nodiscard]] int hpTextWidth() const;
    [[nodiscard]] int staminaTextWidth() const;

    // Pulse phase in [0,1), refreshed every kMonsterEnragePulsePeriodMs
    // while the monster is actually enraged. Same period and same cos()
    // curve the panel's .bar.er fill uses, so the two cannot drift.
    double pulsePhase() const;

    mhw::MonsterIdentityModel model_{};
    // v0.13: the game the demo below seeds. Seeded to World so the
    // window's very first (pre-injection) preview is the historical
    // one; main.cpp overwrites it before the first paint.
    mhw::GameId game_{mhw::GameId::World};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it so a demo preview cannot survive
    // into a live session.
    bool demoData_{false};
    // The enrage pulse's clock. Started from paintPanel() rather than
    // update() because the paint is where the phase is consumed and a
    // bar that is not counting down is never painted at all.
    QElapsedTimer phaseClock_;
};
