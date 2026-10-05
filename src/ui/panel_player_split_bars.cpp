// SPDX-License-Identifier: Apache-2.0
//
// PlayerBarsStripPanel implementation. Every drawing sub-routine below is
// this file's own small copy of the one it names in panel_player.cpp,
// written out in full on purpose: those live in that file's anonymous
// namespace and are unreachable from here, and hoisting them into a
// shared header would mean editing a file this change may not touch. The
// copy is confined to the block being extracted, so it disappears the day
// the two merge again (panel_player_split.h carries the same note for the
// status strip).

#include "panel_player_split_bars.h"

#include "core/string_table.h"
#include "player/player_types.h"
// PlayerSection::Bars — the mask axis, the same bit PlayerPanel reads for
// this block (panel_player.cpp:645) and the pilot strip reads for its own
// (:429 / :436). Declared in namespace PlayerSection, not mhw.
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm (kSplitMargin, the font stack, the two gap
// grades) that every split window answers contentMargin() from.
#include "ui/panel_player_split_layout.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

// i18n lookup, defined the same way panel_player.cpp:31-33 defines it.
// This file asks for no keys of its own — the bar texts are pure numbers
// (formatters.h), and the only literal that would need a key is the
// "READY" style word, which the bars row does not have. The lookup is
// here so the demo path and any future label go through the same table
// rather than an inline zh literal.
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- Layout tokens (panel_player.cpp:37-60, the ones this block owns) ---
constexpr int kPanelW   = 378;     // HTML: .op width:378px — the SOURCE
                                   // panel's width, carried only as the
                                   // reference the extracted numbers came
                                   // from. This window never sets a
                                   // content size of 378.
constexpr int kMargin   = mhw::split_layout::kSplitMargin;  // HTML: .op padding:11
constexpr int kBarH     = 15;      // HTML: .bar height:15px (HP)
constexpr int kStBarH   = 12;      // HTML: .bar.st height (ST). The panel's
                                   // comment records that this was raised
                                   // from the HTML spec's 9 so the 8px
                                   // glyph sits comfortably and aligns with
                                   // the HP bar padding.
// The HP/ST pair is ONE block, so its internal gap is the split's
// within-block value. What used to be the HTML .bar margin-bottom:7px
// copied off the panel is now the same constant the wirebug and mantles
// windows read.
constexpr int kGapBar   = mhw::split_layout::kSplitGapWithin;
constexpr int kBarsGap  = mhw::split_layout::kSplitLead;   // panel_player.cpp:726

// ---- Colour palette (panel_player.cpp:76-80) ----
constexpr int kHpGreen    = 76;  constexpr int kHpGreenG  = 175; constexpr int kHpGreenB  = 80;
constexpr int kHpAmber    = 255; constexpr int kHpAmberG  = 193; constexpr int kHpAmberB  = 7;
constexpr int kHpRed      = 244; constexpr int kHpRedG    = 67;  constexpr int kHpRedB    = 54;
constexpr int kStBlue     = 33;  constexpr int kStBlueG   = 150; constexpr int kStBlueB   = 243;
constexpr int kStOrange   = 255; constexpr int kStOrangeG = 152; constexpr int kStOrangeB = 0;

// The bar text's font pair, and the numbers it needs for contentWidth().
// panel_player.cpp:1037 (HP: 9px Medium) and :1071 (ST: 8px regular).
// Measuring with anything else is how a strip ends up one elided glyph
// short on every row.
constexpr int kBarValueFontSize  = 9;   // HP row's overlay text
constexpr int kBarStValueSize    = 8;   // ST row's overlay text

// panel_player.cpp:95-100
QColor hpColor(float pct)
{
    if (pct > 0.60F)  return QColor(kHpGreen,  kHpGreenG,  kHpGreenB);
    if (pct > 0.25F)  return QColor(kHpAmber,  kHpAmberG,  kHpAmberB);
    return                 QColor(kHpRed,    kHpRedG,    kHpRedB);
}

// panel_player.cpp:102-107
QColor stColor(float pct)
{
    if (pct > 0.50F)  return QColor(kStBlue,   kStBlueG,   kStBlueB);
    if (pct > 0.20F)  return QColor(kStOrange, kStOrangeG, kStOrangeB);
    return                 QColor(kHpRed,     kHpRedG,     kHpRedB);
}

QColor familyColor(mhw::BarsRow::Family family, float pct)
{
    return family == mhw::BarsRow::Family::Hp ? hpColor(pct) : stColor(pct);
}

// panel_player.cpp:195-221, "drawBarV15". The rounded track, the lighter
// top -> darker bottom gradient fill, and the 1px #2a2d2f hairline. Copied
// verbatim apart from being reachable from this translation unit.
void drawBarV15(QPainter &p, const QRectF &rect, float pct,
                const QColor &fill, int radius = 3)
{
    const float c = std::clamp(pct, 0.0F, 1.0F);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    // Track: --bg-cell #1d2022
    p.setBrush(QColor(29, 32, 34));
    p.drawRoundedRect(rect, radius, radius);

    if (c > 0.001F) {
        const QRectF fillRect(rect.x(), rect.y(),
                              rect.width() * c, rect.height());
        QLinearGradient fg(fillRect.topLeft(), fillRect.bottomLeft());
        fg.setColorAt(0.0, fill.lighter(108));
        fg.setColorAt(0.5, fill);
        fg.setColorAt(1.0, fill.darker(118));
        p.setBrush(fg);
        p.drawRoundedRect(fillRect, radius, radius);
    }

    // 1px hairline border (HTML: --line #2a2d2f)
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(42, 45, 47), 1));
    p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

// panel_player.cpp:1030-1062 — the HP bar plus its "current / max" (left)
// and percentage (right). The text is drawn over the bar with 8px
// insets; the value column is the same rect in the same paint, so the
// text can never disagree with the bar it labels.
void drawBarRow(QPainter &p, const QRectF &barRect,
                const mhw::BarsRow &row, int fontSize)
{
    drawBarV15(p, barRect, row.pct, familyColor(row.family, row.pct));

    QFont vFont(QStringLiteral("Chakra Petch"), fontSize,
                row.family == mhw::BarsRow::Family::Hp ? QFont::Medium
                                                      : QFont::Normal);
    vFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(vFont);
    p.setPen(QColor(245, 246, 247));
    // panel_player.cpp:1058 / :1092 — the same 8px inset on both sides,
    // value left-aligned, percentage right-aligned.
    const QRectF textRect = barRect.adjusted(8, 0, -8, 0);
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, row.valueText);
    p.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, row.pctText);
}

} // namespace

namespace mhw {

// The pure transformation. Two bars, five snapshot fields, one scaling
// decision.
//
// Health is never scaled in either game (raw HP is already the bar value —
// see panel_player_metrics.h:47). Stamina is scaled only where the game's
// HUD is not in bar units, and that is a table row, not a comparison:
// staminaDisplayValue() reads mhw::staminaDisplayScale(game) so a third
// game has to answer for itself instead of inheriting the World arm.
//
// The `known` flags follow panel_player.cpp:1032 / :1066 exactly, because
// an unknown bar still reserves its row (the block does not collapse
// because one read missed) but must not print a number it does not have.
BarsModel buildBarsModel(const mhw::GameSnapshot &snap)
{
    BarsModel model;
    const mhw::PlayerSnapshot &pl = snap.player;

    const bool hpKnown = (pl.valid && pl.maxHealth > 0.0F);
    model.health.pct = hpKnown ? pl.health / pl.maxHealth : 0.0F;
    model.health.known = hpKnown;
    model.health.family = mhw::BarsRow::Family::Hp;
    model.health.valueText = hpKnown
        ? QStringLiteral("%1 / %2")
              .arg(mhw::groupNumber(static_cast<int>(pl.health)))
              .arg(mhw::groupNumber(static_cast<int>(pl.maxHealth)))
        : QString::fromUtf8(mhw::barsPlaceholderValue());
    model.health.pctText = hpKnown
        ? mhw::percentage(pl.health, pl.maxHealth)
        : QString::fromUtf8(mhw::barsPlaceholderPct());

    // v0.8.4-r18 stamina-units-r2 mirrors the panel's nesting: ST is only
    // known when HP is, so a read that resolves neither draws both bars
    // empty rather than half-resolved.
    const bool stKnown = hpKnown && pl.maxStamina > 0.0F;
    model.stamina.pct = stKnown ? pl.stamina / pl.maxStamina : 0.0F;
    model.stamina.known = stKnown;
    model.stamina.family = mhw::BarsRow::Family::St;
    // Percentage above is raw/raw and therefore unit-invariant; the two
    // numbers below are what the game's HUD prints, hence the conversion.
    const float stShown = stKnown
        ? staminaDisplayValue(snap.game, pl.stamina) : 0.0F;
    const float stMaxShown = stKnown
        ? staminaDisplayValue(snap.game, pl.maxStamina) : 0.0F;
    model.stamina.valueText = stKnown
        ? QStringLiteral("%1 / %2")
              .arg(mhw::groupNumber(static_cast<int>(stShown)))
              .arg(mhw::groupNumber(static_cast<int>(stMaxShown)))
        : QString::fromUtf8(mhw::barsPlaceholderValue());
    model.stamina.pctText = stKnown
        ? mhw::percentage(pl.stamina, pl.maxStamina)
        : QString::fromUtf8(mhw::barsPlaceholderPct());

    // The switch documents the one axis this model actually varies on.
    // Neither arm differs today (health is unscaled in both games and the
    // stamina scale is per-game DATA), and that is the point: a third
    // game's arm is where a new conversion rule would go, and -Wswitch
    // with no default makes leaving it out visible at build time rather
    // than inheriting one of these two.
    switch (snap.game) {
    case mhw::GameId::World:
    case mhw::GameId::Rise:
        break;
    }

    return model;
}

} // namespace mhw

PlayerBarsStripPanel::PlayerBarsStripPanel(QWidget *parent)
    : Panel(QStringLiteral("player-bars"), Corner::TopLeft, parent)
{
    // No chrome, no title row: a bar pair is a fragment of the player
    // panel, not a panel. setChromeVisible(false) suppresses the v0.3
    // rounded body, left accent stripe and border entirely — the base
    // class's paintPanelChrome() then returns without touching the
    // painter, so every pixel the two bars do not paint is untouched.
    setChromeVisible(false);

    // Own window-title key (distinct from ui.player_status_section, the
    // label a nameless status pill falls back to). The bars row shows no
    // label of its own — it is two numbers on a coloured bar — so the key
    // is the one main.cpp's applyLocale already re-sets on a locale flip.
    setWindowTitle(mh::tr("ui.player_strip_title"));
}

void PlayerBarsStripPanel::update(const mhw::GameSnapshot &snap)
{
    // Same shape as PlayerPanel::update(): cache what paintPanel() needs,
    // schedule one repaint. The panel's other nine mirrored fields and its
    // ViewModel-resolved identity are not read here — this window draws
    // neither a name nor an MR nor a weapon slot.
    rows_ = mhw::buildBarsModel(snap);

    // Demo numbers are never live state (the rule PetDamagePanel and the
    // pilot strip both apply): the first authoritative snapshot replaces
    // them outright, so an edit-mode preview cannot survive into a live
    // session and start reporting a hunter that does not exist.
    demoData_ = false;

    triggerUpdate();
}

void PlayerBarsStripPanel::retranslateUi()
{
    // Cached strings first, exactly like the pilot's retranslateUi() and
    // for the same reason: main.cpp's applyLocale re-sets the title from
    // the same key, so the second set is the deliberate redundancy every
    // panel carries — it keeps the answer correct if this is ever called
    // from anywhere else. No demo seed is dropped: the demo path writes
    // numbers, not labels, so there is nothing locale-dependent to
    // rebuild (unlike the status strip, whose demo pills are named).
    setWindowTitle(mh::tr("ui.player_strip_title"));
    triggerUpdate();
}

bool PlayerBarsStripPanel::barsEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing PlayerSection::Bars means the
    // console's existing player-section toggle drives this window for
    // free — a user who has already turned the HP/ST row off in the
    // single panel gets the same layout here, with no new console UI.
    return (sectionMask() & mhw::PlayerSection::Bars) != 0;
}

bool PlayerBarsStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    // With the mask bit off or with neither bar resolved there is nothing
    // to show, so the window must not exist: the main loop's
    // hasVisibleContent() answers false and the layer-shell surface is
    // never mounted.
    if (!barsEnabled())
        return false;
    return !rows_.isEmpty();
}

int PlayerBarsStripPanel::barsBlockHeight() const
{
    // panel_player.cpp:726-727, kBarsGap + kBarsH, plus the trailing
    // margin the panel's block-table formula adds for every enabled
    // block. HP bar, then the gap, then ST bar, then the gap, then the
    // bottom margin — same arithmetic, same numbers, one owner. Both rows
    // are always reserved once the block is on; that is the behaviour the
    // panel already has.
    return kMargin + kBarsGap + kBarH + kGapBar + kStBarH + kGapBar
           + kMargin;
}

int PlayerBarsStripPanel::contentWidth() const
{
    // Content width, not a chrome width: the widest of the two rows'
    // number pairs, measured in the same fonts the paint sets, plus the
    // bar's 8px inset on both sides and the margin on both sides. The
    // Win/150" pair is what actually governs — a percentage never exceeds
    // "100.0%".
    const QFont hpFont(QStringLiteral("Chakra Petch"), kBarValueFontSize,
                       QFont::Medium);
    const QFontMetrics hpMetrics(hpFont);
    const QFont stFont(QStringLiteral("Chakra Petch"), kBarStValueSize);
    const QFontMetrics stMetrics(stFont);

    int widest = 0;
    if (barsEnabled()) {
        widest = std::max(
            hpMetrics.horizontalAdvance(rows_.health.valueText)
                + hpMetrics.horizontalAdvance(rows_.health.pctText),
            stMetrics.horizontalAdvance(rows_.stamina.valueText)
                + stMetrics.horizontalAdvance(rows_.stamina.pctText));
    }
    if (widest == 0)
        return 2 * kMargin;
    return widest + 2 * 8 + 2 * kMargin;
}

void PlayerBarsStripPanel::setGameForDemo(mhw::GameId game)
{
    if (game_ == game)
        return;
    game_ = game;
    // Drop the one-shot demo seed so the next paint re-runs
    // setupDemoData() with the new game's stamina units. Without this the
    // window would keep printing World-shaped stamina under a Rise rail —
    // or Rise-shaped (÷30) numbers under a World one.
    resetDemoPrimed();
    triggerUpdate();
}

void PlayerBarsStripPanel::setupDemoData()
{
    // Edit-mode preview: the two bars at plausible live values, one of
    // them mid-range so both colour families and the gradient fill are
    // visible. Numbers only — no labels — so there is nothing to
    // re-seed on a locale flip (see retranslateUi()).
    mhw::PlayerSnapshot pl;
    pl.valid = true;
    pl.health = 132.0F;
    pl.maxHealth = 150.0F;
    // v0.13: the same unit rule PlayerPanel::setupDemoData() applies —
    // seed RAW stamina for Rise (bar value x30, so the preview exercises
    // the real conversion) and bar-domain values for World. Both render
    // the identical "90 / 100", which is what makes the two games'
    // previews agree while the underlying data stays honest.
    if (game_ == mhw::GameId::Rise) {
        pl.stamina = 90.0F * 30.0F;   // 90 displayed (90 x 30)
        pl.maxStamina = 100.0F * 30.0F;
    } else {
        pl.stamina = 90.0F;
        pl.maxStamina = 100.0F;
    }
    mhw::GameSnapshot snap;
    snap.game = game_;
    snap.player = pl;
    rows_ = mhw::buildBarsModel(snap);
    demoData_ = true;
}

void PlayerBarsStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = barsBlockHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an unmounted-but-alive
    // window never renders a leftover sliver. The live path never reaches
    // here — the main loop asks hasVisibleContent() first. This is the
    // edit-preview / defensive branch.
    if (!barsEnabled() || rows_.isEmpty()) {
        setContentSize(2 * kMargin, h);
        return;
    }

    // setContentSize has an idempotent guard inside Panel, so calling it
    // per repaint (and therefore per drag frame) is free.
    setContentSize(w, h);

    // Re-read the width the window actually got. setContentSize() applied
    // the user's minimum-width floor to its arguments, but `w` is still
    // the pre-floor content number — using it here is exactly the bug the
    // floor was added to fix: the window would widen and the gauge would
    // stay 89px, leaving empty background to the right of the bar.
    //
    // The window only ever grows, so the same max() the base class applied
    // reproduces it without asking the compositor anything.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE. drawV03Chrome() is deliberately not called — a bar
    // pair is a fragment of the player panel, and a panel frame around
    // two progress bars is the shape the split exists to remove. This
    // panel calls neither drawV03Chrome() nor paintPanelChrome(), exactly
    // like the pilot status strip.
    const int innerW = drawnW - 2 * kMargin;
    int y = kMargin + kBarsGap;

    // ---- HP bar ----
    drawBarRow(p, QRectF(kMargin, y, innerW, kBarH),
               rows_.health, kBarValueFontSize);
    y += kBarH + kGapBar;

    // ---- ST bar ----
    drawBarRow(p, QRectF(kMargin, y, innerW, kStBarH),
               rows_.stamina, kBarStValueSize);
}
