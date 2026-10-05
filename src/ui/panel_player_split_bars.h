#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// PlayerBarsStripPanel — the v0.12 HP + Stamina sub-window.
//
// The second/third/fourth window out of the v0.12 "block → window" split
// of PlayerPanel (see panel_player_split.h for the pilot and the pattern's
// two promises). This one mirrors PlayerPanel's `Bars` block, i.e. the HP
// progress bar and the Stamina progress bar underneath it —
// panel_player.cpp:1027-1098. Preceded (in this same step) by
// PlayerMantlesStripPanel and PlayerWirebugStripPanel, so all three new
// windows are described in the same place.
//
// WHAT IT SHOWS
// -------------
//   row 1  HP bar  — filled by health/maxHealth, coloured by hpColor()
//                     (green > 60%, amber > 25%, red), with the in-game
//                     "current / max" on the left and the percentage on
//                     the right (panel_player.cpp:1032-1062).
//   row 2  ST bar  — filled by stamina/maxStamina, coloured by stColor()
//                     (blue > 50%, orange > 20%), same text convention.
//
// STAMINA UNITS. The percentage and the fill are raw/raw and therefore
// unit-invariant; only the two numbers on the raw Stamina row are scaled,
// and only for the game whose HUD is not in bar units. That conversion is
// `mhw::staminaDisplayValue(game, raw)` in ui/panel_player_metrics.h (Rise
// ÷30, everything else ÷1 through the per-game DisplayScale table) and it
// is called from here, never reimplemented — the x30 rise-stamina vectors
// and the World "already bar value" contract are documented at length on
// that header and pinned by tests/reader_tests.cpp.
//
// SCAFFOLDING NOTE (scale): this window's first launch writes
// `panels.ini[<key>]scale=2` because Panel::loadConfig defaults `scale`
// to 2.0 (src/ui/panel.cpp:239, a v0.3 leftover the user has measured as
// "缩放过大字体过大导致几乎完全不可用"). That default belongs to
// src/ui/panel.cpp, which is NOT in this step's remit and is being edited
// by another agent concurrently, so this file deliberately does NOT
// override it locally. Duplicating the fix here would give the three new
// windows one answer and the four shipped panels another — exactly the
// inconsistency the parent step is removing. This note is the record of
// that decision until panel.cpp carries the fix for everybody.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row: a bar pair is a fragment of the player panel,
// not a panel. The constructor calls Panel::setChromeVisible(false) and
// isSubWindow() answers true. The bar painter, the colour helpers and the
// text layout below are this file's own small copies of the ones in
// panel_player.cpp's anonymous namespace (unreachable across translation
// units, and hoisting them into a shared header would touch a file this
// step may not edit). Each copy names the source line it mirrors.

#include "panel.h"
#include "core/game_snapshot.h"
#include "player/player_types.h"
#include "ui/formatters.h"
#include "ui/panel_player_metrics.h"
// The shared split rhythm. Included here rather than only in the .cpp
// because this header answers contentMargin() from it: an inline member
// that names a constant it does not include compiles until someone
// includes the header first, and then stops.
#include "ui/panel_player_split_layout.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QString>

namespace mhw {

// One bar's resolved display state — what paintPanel() draws and what the
// test pins. Built from the same snapshot fields, through the same
// conversions, for both the live path and the demo path.
struct BarsRow {
    // Fraction of the track filled, always within [0,1]. 0 when the
    // maximum is unknown (maxHealth == 0 — the player struct resolved but
    // the health read did not), so the bar draws its empty track rather
    // than a full one that claims a value it does not have.
    float pct{0.0F};
    // Whether the numbers under the bar are trustworthy. hpKnown in the
    // panel = (valid && maxHealth > 0); stKnown = hpKnown && maxStamina > 0
    // (panel_player.cpp:1032 / :1066). False renders the placeholder pair
    // instead of a number, and the caller still reserves the full row.
    bool known{false};
    // "132 / 150" (left of the bar) and "88.0%" (right of it). Both are
    // already-formatted strings: formatters.h owns the grouping and the
    // precision so the strip and the panel cannot drift on either.
    QString valueText;
    QString pctText;
    // Colour family the bar fills with. The palette lives in this file's
    // anonymous namespace (a copy of the panel's); the model carries the
    // family rather than a QColor so the pure half stays Qt-paint-free.
    enum class Family { Hp, St } family{Family::Hp};
};

// A stable value for the placeholder pair both bars fall back to, so the
// demo seeder and the reader path print identical dashes.
[[nodiscard]] inline constexpr const char *barsPlaceholderValue()
{
    return "-- / --";
}

// A stable value for the placeholder percentage. panel_player.cpp uses
// "--%" (no space) for both bars.
[[nodiscard]] inline constexpr const char *barsPlaceholderPct()
{
    return "--%";
}

// The two rows in the order they are drawn: HP above Stamina.
struct BarsModel {
    BarsRow health;
    BarsRow stamina;

    [[nodiscard]] bool isEmpty() const noexcept
    {
        // Neither row ever disappears on its own (the panel reserves both
        // whenever the Bars section is on), so "empty" means nothing about
        // the local player has resolved at all — which is the state that
        // must not mount a window of its own.
        return !health.known && !stamina.known;
    }
};

// The pure transformation for this window: the two bars' display state
// out of a snapshot, for whatever game produced it.
//
// `raw` values are kept out of the model on purpose — the model answers
// "what does the bar show", and every scaling decision happens here, once,
// through the same registered helpers the panel uses. Dispatched on
// GameId with no default arm, so -Wswitch makes a third game an explicit
// decision rather than an inherited answer (the seam panel_player.cpp:162
// and panel_player_split.cpp's buildStatusStripModel both use).
[[nodiscard]] BarsModel buildBarsModel(const mhw::GameSnapshot &snap);

} // namespace mhw

// HP + Stamina progress bar pair as its own layer-shell window, with its
// own panels.ini group ("player-bars", NOT "player" or "player-status" —
// reusing either would make two windows fight over one position / scale /
// opacity section).
class PlayerBarsStripPanel : public Panel {
    Q_OBJECT

public:
    explicit PlayerBarsStripPanel(QWidget *parent = nullptr);

    // Full snapshot. Caches the resolved bars for paintPanel() to read,
    // mirroring PlayerPanel's `player_` member + update() pattern.
    void update(const mhw::GameSnapshot &snap);

    // i18n: no cached strings of this window's own (the title is set from
    // the constructor's key and re-set by main.cpp's applyLocale, the
    // demo path carries no labels), so this only re-queries the title for
    // the case where retranslateUi() is called from anywhere else, and
    // schedules a repaint. Paint-time tr()/formatting follows on its own.
    void retranslateUi() override;

    // A window carved out of PlayerPanel declares itself a sub-window so
    // the console's slot registry keeps it out of the four fixed slots
    // the base panels occupy (Panel::isSubWindow).
    [[nodiscard]] bool isSubWindow() const override { return true; }

    // ─── Panel's polymorphic layout contract ────────────────────────
    //
    // The window answers the base's extent and inset hooks rather than
    // leaving them at the base's "I do not do that" defaults, so a layout
    // question — from the console's preview, from main.cpp, or from the
    // layout consistency test — gets this window's real numbers without
    // anyone having to know which window they are holding.

    // What the two gauges and their numbers ask for. The floor and the
    // lock are applied later, inside Panel::setContentSize().
    [[nodiscard]] int contentWidth() const override;

    // Panel's height hook. The window already had a name for this number
    // and paintPanel() calls it directly, so this is the other spelling
    // of it rather than a second definition.
    [[nodiscard]] int contentHeight() const override { return barsBlockHeight(); }

    // The outer inset, the same kSplitMargin every other split window
    // answers. Reported so the ring can be asked instead of seven private
    // constants being trusted to agree.
    [[nodiscard]] int contentMargin() const override
    {
        return mhw::split_layout::kSplitMargin;
    }

    // v0.13: the game this window's demo seed represents. The two bars'
    // demo values are unit-sensitive (staminaDisplayValue() divides by
    // 30 on Rise and by 1 on World), so a preview that ignores the game
    // prints World-shaped numbers under a Rise-rail selection — the
    // exact class of mismatch the user reported for 衣装/翔虫.
    //
    // PUBLIC, matching PlayerStatusStripPanel::setGameForDemo and
    // PlayerPanel::setGameForDemo: main.cpp is the caller and it holds
    // plain stack objects, not pointers through a base class.
    //
    // Same shape as the strip's, and for the same reason: main.cpp
    // resolves the game once and pushes it into every window whose demo
    // varies by game. Calling it after the demo is primed drops the seed
    // so the next paint re-runs it.
    void setGameForDemo(mhw::GameId game);

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;


    // "No data, no window", the split's shared rule: with neither bar
    // resolved the window answers false here and the main loop's
    // hasVisibleContent() never mounts the surface. Edit mode is the
    // deliberate exception — a design-mode surface has to stay mounted
    // and draggable — so once the demo is seeded the answer is true even
    // though the numbers are synthetic.
    bool hasContent() const override;

private:
    // The two rows this window draws, as the section mask leaves them.
    // The mask axis is separate from the data axis (the pilot strip reads
    // PlayerSection::Debuff / ::Buff in exactly this place and role);
    // reusing PlayerSection::Bars means the console's existing player
    // toggle drives this window with no new UI.
    [[nodiscard]] bool barsEnabled() const;

    // Height of the whole pair, in logical px: two bars, the same gap
    // between them, and the margins at both ends. Mirrors the panel's
    // kBarsH = kBarH + kGapBar + kStBarH + kGapBar plus the leading
    // kBarsGap (panel_player.cpp:726-727). Called from one place, from
    // the same rows paintPanel() draws — the v0.10.3 lesson (a strip
    // drawn at one height and reserved at another).
    [[nodiscard]] int barsBlockHeight() const;

    // Width the widest row needs for its two numbers, in logical px, from
    // the fonts paintPanel() actually sets. Never 378 — that was
    // PlayerPanel's width and belongs to it.

    mhw::BarsModel  rows_{};
    // v0.13: the game the demo below seeds. Seeded to World so the
    // window's very first (pre-injection) preview is the historical one;
    // main.cpp overwrites it before the first paint.
    mhw::GameId     game_{mhw::GameId::World};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it (and the rows with it) so a demo
    // preview can never survive into a live session.
    bool            demoData_{false};
};
