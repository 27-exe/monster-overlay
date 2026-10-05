#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// MonsterEnrageStripPanel — the v0.12 monster-panel split's
// 怒气计量条 (.bar.er) window.
//
// The block that is least coupled to anything else in MonsterPanel and
// the one most often wanted on its own: a hunter watching for a rage
// window needs the meter, not the hex portrait it used to sit under. Its
// value as a window is the pulse — the 1.6 s brightness cycle the panel
// runs (MonsterPanel::enragePhase_ / kPulsePeriodMs) reads as a
// countdown to something about to happen, and on a surface of its own it
// becomes a standalone alarm instead of one line inside a stacked panel.
//
// Same three-piece pattern as every v0.12 split window — see
// panel_player_split.h for the pilot's two promises and
// panel_monster_split_hp.h for this split's variant.
//
// WHAT IS NOT HERE
// ----------------
// No chrome, no title row, and no left label: the panel's .bar.er is
// deliberately label-free ("Compact: no left label, just the value at
// the right"), so the window keeps that shape and is the narrowest of
// the three bar windows. The 激怒 %1s / 激怒 %1% strings this window
// does NOT draw — those belong to the Info block's enrage tag, which
// stays in the panel.
//
// The value this window shows is one of two things, exactly as the
// panel's block decides:
//   * "MAX" while the monster is actually enraged and the timer is live,
//   * "N%" from enrageBuildup / enrageMaxBuildup otherwise,
// so the model carries the enum, not a pre-baked string: which of the
// two is shown is a fact about the snapshot, and the string is a paint
// decision the locale participates in.

#include "panel.h"
#include "monster/monster_types.h"
// MonsterSection::Enrage — the mask axis, the same bit MonsterPanel
// reads for this block (panel_monster.cpp:966).
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm. Included here, not only in the .cpp, because
// contentMargin() is an inline member that names a constant.
#include "ui/panel_player_split_layout.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QString>

#include <QElapsedTimer>

namespace mhw {

// What the right-aligned text on the bar says. An enum rather than a
// string so a test can assert WHICH of the two readings the snapshot
// produced, and so the paint resolves the text through the table on
// every call (a locale swap relabels without re-seeding anything).
enum class MonsterEnrageReading {
    // The buildup pair is unknown and the monster is not enraged: the
    // bar is an empty track and the panel shows 0%. Distinct from
    // Percent so the model never has to lie about a denominator it does
    // not have.
    Empty,
    // A buildup percentage, 0..100, from enrageBuildup / enrageMaxBuildup.
    Percent,
    // The monster is enraged with a live timer: "MAX".
    Max,
};

// The window's whole content model.
struct MonsterEnrageModel {
    // Fraction of the track filled, always within [0,1]. Zero when the
    // buildup pair is unknown and the monster is not enraged.
    float pct{0.0F};
    // Which reading the right-aligned text carries. Panel_monster.cpp's
    // .bar.er block decides this from the same three fields.
    MonsterEnrageReading reading{MonsterEnrageReading::Empty};
    // Whether the monster is enraged AND the timer is live — the
    // condition that drives the pulse. Carried separately from `reading`
    // because a snapshot can be Max without a live timer (an enraged
    // monster whose seconds have run out), and the pulse must not fire
    // for a bar that is not counting down.
    bool pulsing{false};

    [[nodiscard]] bool isEmpty() const noexcept
    {
        // The panel draws this block whenever showRage is true
        // (`enrageMaxBuildup > 0 || enraged`), and only then. With
        // none of that, there is no meter — which for a window means
        // "must not mount".
        return reading == MonsterEnrageReading::Empty && !pulsing;
    }
};

// The pure transformation for this window: the meter's state out of a
// monster snapshot. No dispatch on GameId — the enrage fields are the
// same shape in both games (panel_monster.cpp's .bar.er block reads no
// game branch either).
[[nodiscard]] MonsterEnrageModel buildMonsterEnrageModel(
    const mhw::MonsterSnapshot &m);

} // namespace mhw

// 怒气计量条 as its own layer-shell window, with its own panels.ini
// group ("monster-enrage", NOT "monster" — reusing it would make two
// windows fight with MonsterPanel over one position / scale / opacity
// section).
class MonsterEnrageStripPanel : public Panel {
    Q_OBJECT

public:
    explicit MonsterEnrageStripPanel(QWidget *parent = nullptr);

    // Full monster snapshot. Caches the resolved meter for paintPanel()
    // to read, mirroring MonsterPanel's `monster_` member + update().
    void update(const mhw::MonsterSnapshot &m);

    // i18n: no cached strings of this window's own beyond the title (the
    // bar's text is "MAX" or a number, both locale-free), so this
    // re-queries the title and schedules a repaint.
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

    // Which game this window's demo seed represents. The enrage meter's
    // content is game-agnostic (see buildMonsterEnrageModel), so the
    // seed does not vary today; the setter exists for the same reason
    // every sibling has it.
    void setGameForDemo(mhw::GameId game);

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;

    // "No data, no window", the split's shared rule. Edit mode is the
    // deliberate exception — a design-mode surface stays mounted and
    // draggable — so once the demo is seeded the answer is true even
    // though the numbers are synthetic.
    bool hasContent() const override;

private:
    // The section this window draws, as the mask leaves it. Reusing
    // MonsterSection::Enrage means the console's existing monster
    // toggle drives this window with no new UI.
    [[nodiscard]] bool enrageEnabled() const;

    // Height of the whole block, in logical px: the bar, its vertical
    // padding, and the margins at both ends. Mirrors the panel's
    // `kRageBarH + 2 * kBarVPad` (panel_monster.cpp's rageH), one owner.
    [[nodiscard]] int barBlockHeight() const;

    // Pulse phase in [0,1), refreshed every kPulsePeriodMs while the
    // monster is actually enraged. Same period and same cos() curve the
    // panel's .bar.er fill uses, so the two cannot drift.
    double pulsePhase() const;

    mhw::MonsterEnrageModel model_{};
    // v0.12: the game the demo below seeds. Seeded to World so the
    // window's very first (pre-injection) preview is the historical
    // one; main.cpp overwrites it before the first paint.
    mhw::GameId        game_{mhw::GameId::World};
    // True while what is on screen came from setupDemoData(); the first
    // authoritative snapshot clears it so a demo preview cannot survive
    // into a live session.
    bool               demoData_{false};

    // The pulse clock. Advanced by a QTimer ONLY while pulsing, like
    // MonsterPanel's — a stopped monster costs nothing, and the phase
    // still resolves to a stable value (0) when the timer is off.
    QElapsedTimer phaseClock_;
};
