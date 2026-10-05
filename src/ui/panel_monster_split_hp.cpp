// SPDX-License-Identifier: Apache-2.0
//
// MonsterHpStripPanel implementation. Every drawing sub-routine below is
// this file's own small copy of the one it names in panel_monster.cpp,
// written out in full on purpose: those live in that file's anonymous
// namespace and are unreachable from here, and hoisting them into a
// shared header would mean editing a file this change may not touch. The
// copy is confined to the block being extracted, so it disappears the
// day the two merge again (panel_player_split_bars.cpp carries the same
// note for the player bars it mirrors).

#include "panel_monster_split_hp.h"

#include "core/game_tables.h"
#include "core/string_table.h"
#include "monster/monster_types.h"
// MonsterSection::Hp — the mask axis, the same bit MonsterPanel reads
// for this block (panel_monster.cpp:965).
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm (kSplitMargin, the gap grades) this window
// answers contentMargin() and builds its block height from.
#include "ui/panel_player_split_layout.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>

#include <algorithm>
#include <cmath>

// i18n lookup, defined the same way panel_monster.cpp:59-63 defines it.
// This file asks for no keys of its own — the bar texts are pure numbers
// (formatters.h) and the one label it could carry is the fatigue
// meter's, which is the other window's. The lookup is here so the demo
// path and any future label go through the same table rather than an
// inline zh literal.
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- Layout tokens (panel_monster.cpp:76-99, the ones this block owns) --
// The window's own margin is the split's shared one, not the panel's
// kPanelPad = 9: every window in the ring is inset by the same distance.
constexpr int kMargin  = mhw::split_layout::kSplitMargin;
constexpr int kBarH    = 15;   // .bar height (panel_monster.cpp:82)
constexpr int kBarVPad = 3;    // Vertical padding inside .bar
                               // (panel_monster.cpp:89). Visual height =
                               // kBarH + 2*kBarVPad = 21.

// The bar text's font pair, and the inset it sits at. Panel_monster.cpp
// uses 11px Medium for the HP row's overlay text and an 8px inset on
// both sides. Measuring with anything else is how a window ends up one
// elided glyph short.
constexpr int kBarValueFontSize = 11;
constexpr int kBarTextInset     = 8;

// ---- Colour palette (panel_monster.cpp:104-109) ----
constexpr int kErBar1R = 255, kErBar1G = 112, kErBar1B = 67;   // #ff7043
constexpr int kHpHighR =  76, kHpHighG = 175, kHpHighB =  80;  // green
constexpr int kHpMidR  = 251, kHpMidG  = 192, kHpMidB  =  45;  // amber
constexpr int kHpLowR  = 244, kHpLowG  =  67, kHpLowB  =  54;  // red
constexpr float kHpAmberPct = 0.50F;  // ≤50% flips to amber

// panel_monster.cpp:152-164, "healthColor". HunterPie V2 /
// MonsterData.xml capture mechanic: > 50% green, ≤ capture% red, amber
// between. Rise has no Capture= percentage, so its entries and unknown
// ids stay green/amber and never inherit a World threshold. Copied
// verbatim apart from being reachable from this translation unit.
QColor healthColor(float pct, mhw::GameId game, int monsterId)
{
    if (pct > kHpAmberPct)
        return QColor(kHpHighR, kHpHighG, kHpHighB);

    const auto cap = mhw::captureThresholdFor(game, monsterId);
    if (cap && *cap > 0 && pct <= static_cast<float>(*cap) / 100.0F)
        return QColor(kHpLowR, kHpLowG, kHpLowB);

    return QColor(kHpMidR, kHpMidG, kHpMidB);
}

// panel_monster.cpp:200-221, "drawBarV". The rounded track, the
// lighter-top gradient fill, and no hairline (the panel's .bar has
// none — only the player's does). Copied verbatim apart from being
// reachable from this translation unit.
void drawBarV(QPainter &p, const QRectF &rect, float pct,
              const QColor &hi, const QColor &lo)
{
    const float clamped = std::clamp(pct, 0.0F, 1.0F);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(29, 32, 34));        // --bg-cell
    p.drawRoundedRect(rect, 2, 2);
    if (clamped > 0.001F) {
        const QRectF fill(rect.x(), rect.y(),
                          rect.width() * clamped, rect.height());
        QLinearGradient grad(fill.topLeft(), fill.bottomLeft());
        grad.setColorAt(0.0, hi);
        grad.setColorAt(1.0, lo);
        p.setBrush(grad);
        p.drawRoundedRect(fill, 2, 2);
    }
}

} // namespace

namespace mhw {

MonsterHpModel buildMonsterHpModel(const mhw::MonsterSnapshot &m)
{
    MonsterHpModel model;
    model.game      = m.game;
    model.monsterId = m.id;
    // maxHealth == 0 is the panel's own "the health read did not
    // resolve" condition (panel_monster.cpp's hpPct), and it is the same
    // one here: a zero maximum must draw an empty track with the
    // placeholder pair, never a full bar that claims a value nobody
    // read.
    model.health.known = m.maxHealth > 0.0F;
    model.health.pct   = m.maxHealth > 0.0F
                             ? std::clamp(m.health / m.maxHealth, 0.0F, 1.0F)
                             : 0.0F;

    // The two numbers, formatted once, here. groupNumber() is the panel's
    // own grouping (locale-aware, thousands separator) and percentage()
    // carries its NaN/Inf fold, so a dead reader value cannot reach
    // Qt's number formatting and abort the paint.
    model.health.valueText = model.health.known
        ? QStringLiteral("%1 / %2")
              .arg(mhw::groupNumber(static_cast<int>(m.health)))
              .arg(mhw::groupNumber(static_cast<int>(m.maxHealth)))
        : QString::fromLatin1(mhw::monsterBarPlaceholderValue());
    model.health.pctText   = model.health.known
        ? mhw::percentage(m.health, m.maxHealth)
        : QString::fromLatin1(mhw::monsterBarPlaceholderPct());
    return model;
}

} // namespace mhw

MonsterHpStripPanel::MonsterHpStripPanel(QWidget *parent)
    : Panel(QStringLiteral("monster-hp"), Corner::TopLeft, parent)
{
    // No chrome, no title row: an HP bar is a fragment of the monster
    // panel, not a panel. setChromeVisible(false) suppresses the v0.3
    // rounded body, left accent stripe and border — the base class's
    // paintPanelChrome() then returns without touching the painter, so
    // every pixel the bar does not paint is untouched. The translucent
    // body is still painted by Panel::paintEvent(), which is what keeps
    // a fragment legible over the game.
    setChromeVisible(false);

    // Own window-title key (distinct from ui.monster_title, the panel's
    // own). The bar shows no label of its own — it is a number pair on
    // a coloured track — so the key is the one main.cpp's applyLocale
    // re-sets on a locale flip.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
}

void MonsterHpStripPanel::update(const mhw::MonsterSnapshot &m)
{
    // Same shape as MonsterPanel::update(): cache what paintPanel()
    // needs, schedule one repaint. The panel's other mirrored fields
    // (ailments, parts, enrage, size) are not read here — this window
    // draws one bar.
    model_ = mhw::buildMonsterHpModel(m);

    // Demo numbers are never live state (the rule PlayerBarsStripPanel
    // and the pilot strip both apply): the first authoritative snapshot
    // replaces them outright, so an edit-mode preview cannot survive
    // into a live session and start reporting a monster that does not
    // exist.
    demoData_ = false;

    triggerUpdate();
}

void MonsterHpStripPanel::retranslateUi()
{
    // Cached strings first, exactly like the pilot's retranslateUi() and
    // for the same reason: main.cpp's applyLocale re-sets the title from
    // the same key, so the second set is the deliberate redundancy every
    // panel carries. No demo seed is dropped: the demo path writes
    // numbers, not labels, so there is nothing locale-dependent to
    // rebuild.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
    triggerUpdate();
}

void MonsterHpStripPanel::setGameForDemo(mhw::GameId game)
{
    if (game_ == game)
        return;
    game_ = game;
    // Drop the one-shot demo seed so the next paint re-runs
    // setupDemoData() with the new game. Without this the window would
    // keep whatever the previous game's seed produced — harmless today
    // (the bar's content is game-agnostic) and a live defect the day a
    // per-game seed is added.
    resetDemoPrimed();
    triggerUpdate();
}

void MonsterHpStripPanel::setupDemoData()
{
    // Edit-mode preview: the bar at a plausible live value, mid-range so
    // the amber colour family and the gradient fill are both visible.
    // Numbers only — no labels — so there is nothing to re-seed on a
    // locale flip (see retranslateUi()).
    //
    // Same numbers MonsterPanel::setupDemoData() uses (火龙, 71%), so the
    // preview and the panel agree on what a real monster looks like.
    mhw::MonsterSnapshot m;
    m.address    = 0xDEADBEEFULL;
    m.id         = 1;
    m.game       = game_;
    m.maxHealth  = 25800.0F;
    m.health     = 18420.0F;
    model_ = mhw::buildMonsterHpModel(m);
    demoData_ = true;
}

bool MonsterHpStripPanel::hpEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing MonsterSection::Hp means the
    // console's existing monster-section toggle drives this window for
    // free — a user who has already turned the HP bar off in the single
    // panel gets the same layout here, with no new console UI.
    return (sectionMask() & mhw::MonsterSection::Hp) != 0;
}

bool MonsterHpStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    // With the mask bit off, or with the health maximum unresolved,
    // there is nothing to show, so the window must not exist: the main
    // loop's hasVisibleContent() answers false and the layer-shell
    // surface is never mounted.
    if (!hpEnabled())
        return false;
    return !model_.isEmpty();
}

int MonsterHpStripPanel::barBlockHeight() const
{
    // panel_monster.cpp's hpH = kBarH + 2 * kBarVPad, plus the trailing
    // margin the panel's block-table formula adds for every enabled
    // block. Bar, then the margin at both ends — same arithmetic, same
    // numbers, one owner. The row is always reserved once the block is
    // on; that is the behaviour the panel already has.
    return kMargin + kBarH + 2 * kBarVPad + kMargin;
}

int MonsterHpStripPanel::barContentWidth() const
{
    // Content width, not a chrome width: the widest of the bar's two
    // texts, measured in the same font the paint sets, plus the bar's
    // 8px inset on both sides and the margin on both sides. The
    // "25,800 / 25,800" pair is what actually governs — a percentage
    // never exceeds "100.0%".
    const QFont valueFont(QStringLiteral("Chakra Petch"),
                          kBarValueFontSize, QFont::Medium);
    const QFontMetrics valueMetrics(valueFont);

    if (!hpEnabled())
        return 2 * kMargin;
    const int widest =
        valueMetrics.horizontalAdvance(model_.health.valueText)
        + valueMetrics.horizontalAdvance(model_.health.pctText);
    if (widest <= 0)
        return 2 * kMargin;
    return widest + 2 * kBarTextInset + 2 * kMargin;
}

int MonsterHpStripPanel::contentWidth() const
{
    return barContentWidth();
}

void MonsterHpStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = barBlockHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an
    // unmounted-but-alive window never renders a leftover sliver. The
    // live path never reaches here — the main loop asks
    // hasVisibleContent() first. This is the edit-preview / defensive
    // branch.
    if (!hpEnabled() || model_.isEmpty()) {
        setContentSize(2 * kMargin, h);
        return;
    }

    // setContentSize has an idempotent guard inside Panel, so calling it
    // per repaint (and therefore per drag frame) is free.
    setContentSize(w, h);

    // Re-read the width the window actually got. setContentSize()
    // applied the user's minimum-width floor to its arguments, but `w`
    // is still the pre-floor content number — using it here is exactly
    // the bug the floor was added to fix: the window would widen and
    // the bar would stay narrow, leaving empty background to the right
    // of the bar.
    //
    // The window only ever grows, so the same max() the base class
    // applied reproduces it without asking the compositor anything.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE. drawV03Chrome() is deliberately not called — an HP
    // bar is a fragment of the monster panel, and a panel frame around
    // one progress bar is the shape the split exists to remove. The
    // translucent body still comes from Panel::paintEvent().
    const int innerW = drawnW - 2 * kMargin;
    const int innerX = kMargin;
    const int y      = kMargin;

    // ---- the HP bar ----
    const float pct = model_.health.pct;
    const QColor hpC = healthColor(pct, model_.game, model_.monsterId);
    const QRectF barRect(innerX, y, innerW, kBarH + 2 * kBarVPad);
    drawBarV(p, barRect, pct, hpC.lighter(115), hpC);

    {
        QFont barFont(QStringLiteral("Chakra Petch"), kBarValueFontSize);
        barFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(barFont);
        p.setPen(QColor(245, 246, 247));
        const QRectF textRect(innerX, y + kBarVPad, innerW, kBarH);
        p.drawText(textRect.adjusted(kBarTextInset, 0, -kBarTextInset, 0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   model_.health.valueText);
        p.drawText(textRect.adjusted(kBarTextInset, 0, -kBarTextInset, 0),
                   Qt::AlignRight | Qt::AlignVCenter,
                   model_.health.pctText);
    }
}
