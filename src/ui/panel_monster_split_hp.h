#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// MonsterHpStripPanel — the v0.12 monster-panel split's 总 HP 条 window.
//
// The same three-piece pattern PlayerPanel's split shipped and validated
// (design: experiment/monster-overlay-v012/design/03-monster-damage-split-design.md,
// and for the pilot see panel_player_split.h's two promises):
//
//   1. PURELY ADDITIVE. MonsterPanel keeps drawing the block; in single
//      mode nothing here mounts. MonsterPanel itself only changes where
//      the one bit split demanded it — see the Stamina note below.
//   2. Borrows nothing at the symbol level. healthColor(), drawBarV()
//      and the bar text layout live in panel_monster.cpp's anonymous
//      namespace, so this .cpp carries its own small copies with the
//      same tokens and a comment naming the source line each mirrors.
//
// Two promises broken by ONE decision, recorded here because they are
// the ones a reviewer has to be able to check:
//
//   * 10 single-mode baselines byte-identical. MonsterSection grew bit 6
//     (Stamina), and the panel's `stamDrawn` used to read
//     `onHp && maxStamina > 0`. kAll carries both bits, so a stock mask
//     has both set and the row follows the HP toggle exactly as before.
//     The migration (migrateOldMonsterStaminaBit) covers the
//     pre-v0.12 conf that could not have stored bit 6.
//   * No block drawn twice. panel_sections.h's kMonsterSplitTargets row
//     for this window claims Hp alone (NOT Hp|Stamina — the fatigue
//     meter is its own window), and the static_asserts there prove the
//     rows pairwise-disjoint while monsterPanelMaskFor() subtracts them
//     from what the panel keeps.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row: an HP bar is a fragment of the monster panel,
// not a panel. The constructor calls setChromeVisible(false) and
// isSubWindow() answers true. The fatigue meter is NOT in this window —
// it is MonsterStaminaStripPanel (settingsKey "monster-stamina"),
// because that is the whole point of the new bit.
//
// SCAFFOLDING NOTE (scale): this window's first launch writes
// `panels.ini[monster-hp]scale=2` because Panel::loadConfig defaults
// `scale` to 2.0 (panel.cpp:239, a v0.3 leftover). That default belongs
// to src/ui/panel.cpp, which another agent is editing concurrently, so
// this file deliberately does NOT override it — a local fix here would
// give this window one answer and the four shipped panels another.

#include "panel.h"
#include "core/game_tables.h"
#include "monster/monster_types.h"
#include "ui/formatters.h"
// MonsterSection::Hp — the mask axis, the same bit MonsterPanel reads
// for this block (panel_monster.cpp:965).
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm (kSplitMargin, the font stack, the gap grades)
// every split window answers contentMargin() from. Included here, not
// only in the .cpp, because contentMargin() is an inline member.
#include "ui/panel_player_split_layout.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QString>

namespace mhw {

// One bar's resolved display state — what paintPanel() draws and what
// the test pins. Built from the same snapshot fields, through the same
// conversions, for the live path and the demo path alike.
struct MonsterBarRow {
    // Fraction of the track filled, always within [0,1]. 0 when the
    // maximum is unknown (maxHealth == 0 — the monster resolved but the
    // health read did not), so the bar draws its empty track rather than
    // a full one that claims a value it does not have.
    float pct{0.0F};
    // Whether the numbers under the bar are trustworthy. False when
    // maxHealth == 0, which renders the placeholder pair instead of a
    // number and leaves the caller still reserving the full row.
    bool known{false};
    // "18,420 / 25,800" (left of the bar) and "71.5%" (right of it).
    // Both already formatted: formatters.h owns the grouping and the
    // precision so the window and the panel cannot drift on either.
    QString valueText;
    QString pctText;
};

// The window's whole content model: the one bar MonsterSection::Hp
// describes. A struct rather than a bare float pair because the model
// also carries the two strings the paint draws, and a test that only
// saw the fraction could not pin the "0.0%"/"--" fold.
struct MonsterHpModel {
    MonsterBarRow health;
    // The monster id and game, carried so the paint resolves the same
    // capture colour the panel does. NOT resolved into the model as a
    // QColor: the game-keyed threshold lookup is a paint-time decision
    // (captureThresholdFor consults a registry a test need not load),
    // and keeping the two plain fields makes the pure half free of any
    // registry dependency.
    mhw::GameId game{mhw::GameId::World};
    int         monsterId{-1};

    [[nodiscard]] bool isEmpty() const noexcept
    {
        // The bar never disappears on its own once the section is on
        // (the panel reserves it whenever Hp is on), so "empty" means
        // nothing about this monster resolved at all — the state that
        // must not mount a window.
        return !health.known;
    }
};

// A stable value for the placeholder pair the bar falls back to, so the
// demo seeder and the reader path print identical dashes.
[[nodiscard]] inline constexpr const char *monsterBarPlaceholderValue()
{
    return "-- / --";
}

// A stable placeholder percentage. The panel's HP row uses
// mhw::percentage(), which answers "--" for an unusable maximum.
[[nodiscard]] inline constexpr const char *monsterBarPlaceholderPct()
{
    return "--";
}

// The pure transformation for this window: the bar's display state out
// of a monster snapshot, for whatever game produced it.
//
// Dispatched through healthColor() (this file's copy of the panel's)
// because the capture thresholds are game-keyed: a Rise monster has no
// Capture= percentage in MonsterData.xml and must never inherit a World
// threshold. That rule lives on the panel's copy and is mirrored here
// with the same tokens, since anonymous-namespace symbols cannot be
// reached across translation units.
[[nodiscard]] MonsterHpModel buildMonsterHpModel(
    const mhw::MonsterSnapshot &m);

} // namespace mhw

// 总 HP 条 as its own layer-shell window, with its own panels.ini group
// ("monster-hp", NOT "monster" — reusing it would make two windows
// fight over one position / scale / opacity section).
class MonsterHpStripPanel : public Panel {
    Q_OBJECT

public:
    // `subWindowIndex` is this window’s row in its parent’s split table. It
    // arrives at construction time because the base Panel constructor calls
    // loadConfig(), and the index exists only to pick that call’s first-run
    // default — see defaultMarginsForSubWindow() in panel.cpp. A setter
    // would always be one call too late.
    explicit MonsterHpStripPanel(QWidget *parent = nullptr, int subWindowIndex = 0);

    // Full monster snapshot. Caches the resolved bar for paintPanel()
    // to read, mirroring MonsterPanel's `monster_` member + update().
    void update(const mhw::MonsterSnapshot &m);

    // i18n: no cached strings of this window's own (the title is set
    // from the constructor's key and re-set by main.cpp's applyLocale,
    // and the bar carries only numbers), so this re-queries the title
    // and schedules a repaint.
    void retranslateUi() override;

    // A window carved out of MonsterPanel declares itself a sub-window
    // so the console's slot registry keeps it out of the four fixed
    // slots the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

    // ─── Panel's polymorphic layout contract ────────────────────────
    //
    // The window answers the base's extent and inset hooks rather than
    // leaving them at the base's "I do not do that" defaults, so a
    // layout question — from the console's preview, from main.cpp, or
    // from the layout consistency test — gets this window's real
    // numbers without anyone having to know which window it is.

    // What the bar and its numbers ask for. The floor and the lock are
    // applied later, inside Panel::setContentSize().
    [[nodiscard]] int contentWidth() const override;

    // Panel's height hook. paintPanel() calls the same function, so this
    // is the other spelling of it rather than a second definition.
    [[nodiscard]] int contentHeight() const override
    {
        return barBlockHeight();
    }

    // The outer inset, the same kSplitMargin every other split window
    // answers.
    [[nodiscard]] int contentMargin() const override
    {
        return mhw::split_layout::kSplitMargin;
    }

    // Which game this window's demo seed represents. The HP family
    // thresholds are the same for both games, so the demo does not
    // actually vary — present for the same reason every sibling has it:
    // main.cpp resolves the game once and pushes it into every window
    // whose demo COULD vary, so a future per-game seed needs no new
    // wiring and a preview can never print the wrong game's shape.
    void setGameForDemo(mhw::GameId game);

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // "No data, no window", the split's shared rule. With the maximum
    // unresolved the window answers false here and the main loop's
    // hasVisibleContent() never mounts the surface. Edit mode is the
    // deliberate exception — a design-mode surface has to stay mounted
    // and draggable — so once the demo is seeded the answer is true
    // even though the number is synthetic.
    bool hasContent() const override;

private:
    // The section this window draws, as the mask leaves it. The mask
    // axis is separate from the data axis; reusing MonsterSection::Hp
    // means the console's existing monster toggle drives this window
    // with no new UI.
    [[nodiscard]] bool hpEnabled() const;

    // Height of the whole block, in logical px: the bar, its vertical
    // padding, and the margins at both ends. Mirrors the panel's
    // `kBarH + 2 * kBarVPad` inside its kRowGap-folded block table
    // (panel_monster.cpp's hpH), called from one place so the
    // reservation and the draw cannot disagree — the v0.10.3 lesson.
    [[nodiscard]] int barBlockHeight() const;

    // Width the row needs for its two numbers, in logical px, from the
    // fonts paintPanel() actually sets. Never 380 — that was
    // MonsterPanel's width and belongs to it.
    [[nodiscard]] int barContentWidth() const;

    mhw::MonsterHpModel model_{};
    // v0.12: the game the demo below seeds. Seeded to World so the
    // window's very first (pre-injection) preview is the historical
    // one; main.cpp overwrites it before the first paint.
    mhw::GameId        game_{mhw::GameId::World};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it so a demo preview cannot survive
    // into a live session.
    bool               demoData_{false};
};
