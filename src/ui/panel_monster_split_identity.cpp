// SPDX-License-Identifier: Apache-2.0
//
// MonsterIdentityStripPanel implementation. Every drawing sub-routine
// below is this file's own small copy of the one it names in
// panel_monster.cpp, written out in full on purpose: those live in that
// file's anonymous namespace and are unreachable from here, and hoisting
// them into a shared header would mean editing a file this change may
// not touch in a way that moves the pixels the 10 single-mode baselines
// depend on. The header records the trade in full; the rule here is the
// same one every v0.12/v0.13 split window follows — the copy is confined
// to the blocks being extracted, and every copied symbol names the line
// it mirrors.
//
// What is NOT copied is the DATA: the four models' folds are this
// window's own, built from the same snapshot fields with the same
// guards, and MonsterSection's four bits gate them exactly as
// panel_monster.cpp's four `on*` terminals gate the panel's.

#include "panel_monster_split_identity.h"

#include "core/game_tables.h"
#include "core/string_table.h"
#include "monster/monster_types.h"
// MonsterSection's four bits — the mask axes, the same bits
// MonsterPanel reads (panel_monster.cpp :965-998).
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The hex portrait and the crown icons, the same lookup the panel's
// Info block makes.
#include "ui/icon.h"
// The shared split rhythm (kSplitMargin, the gap grades) this window
// answers contentMargin() and builds its blocks from.
#include "ui/panel_player_split_layout.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdint>

// i18n lookup, defined the same way panel_monster.cpp:59-63 defines it.
// The 体力 label and the demo monster name go through the table (so a
// locale flip resizes the window honestly rather than clipping); the HP
// and enrage bars' texts are pure numbers.
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- Layout tokens (panel_monster.cpp:76-101, the ones we own) ------
// The window's own margin is the split's shared one, not the panel's
// kPanelPad = 9: every window in the ring is inset by the same distance.
constexpr int kMargin   = mhw::split_layout::kSplitMargin;
// The gap between two blocks is the split's BETWEEN grade, not the
// panel's kRowGap = 9 — that is the whole point of a window ring.
constexpr int kBlockGap = mhw::split_layout::kSplitGapBetween;

constexpr int kHexW       = mhw::kMonsterIdentityHexW;
constexpr int kHexH       = mhw::kMonsterIdentityHexH;
constexpr int kCrownSize  = mhw::kMonsterIdentityCrownSize;
constexpr int kTitleH     = mhw::kMonsterIdentityTitleH;
constexpr int kNmFont     = mhw::kMonsterIdentityNameFont;
// The panel's .szchip padding (panel_monster.cpp:81) and its
// enrage-tag row's extra height.
constexpr int kSzChipPadX = 5;

constexpr int kBarH       = 15;    // .bar height (panel_monster.cpp:82)
constexpr int kRageBarH   = 6;     // .bar.er visual text band (:83-85)
constexpr int kStamBarH   = 10;    // .bar.st — monster stamina (fatigue) (:86)
constexpr int kBarVPad    = 3;     // Vertical padding inside .bar (:89)
constexpr int kPulsePeriodMs = mhw::kMonsterEnragePulsePeriodMs;

// The bar text's font pair and inset. panel_monster.cpp uses 11px Medium
// for the HP row, 8px for the fatigue and rage rows, and an 8px inset on
// both sides. Measuring with anything else is how a window ends up one
// elided glyph short.
constexpr int kBarValueFontSize = 11;
constexpr int kStamFontSize     = 8;
constexpr int kRageFontSize     = 8;
constexpr int kBarTextInset     = 8;

// ---- Colour palette (panel_monster.cpp:104-112, 122) ----------------
constexpr int kEnrageR = 255, kEnrageG = 112, kEnrageB = 67;  // #ff7043
constexpr int kGoldR   = 255, kGoldG   = 193, kGoldB   = 7;   // #ffc107
constexpr int kHpHighR =  76, kHpHighG = 175, kHpHighB =  80; // green
constexpr int kHpMidR  = 251, kHpMidG  = 192, kHpMidB =  45;  // amber
constexpr int kHpLowR  = 244, kHpLowG  =  67, kHpLowB =  54;  // red
constexpr float kHpAmberPct = 0.50F;  // ≤50% flips to amber
constexpr int kErBar1R = 230, kErBar1G =  74, kErBar1B = 25;  // --c
constexpr int kErBar2R = 255, kErBar2G = 112, kErBar2B = 67;  // --c2
constexpr int kStamR   = 246, kStamG   = 165, kStamB   = 34;  // HunterPie Yellow

// The default width the demo's portrait and the panel's own width are
// measured from; NOT this window's content size (which is derived).
constexpr int kPanelWidth = 380;   // panel_monster.cpp:76
constexpr int kPanelPad   = 9;     // panel_monster.cpp:77

// Reader values reach these drawings twice sanitized: once in the model,
// once by the clamp they drive. A NaN or Inf that slips through produces
// a percentage like "-2147483648%" instead of a bar, because the cast of
// a non-finite float is platform-defined. Same helper as
// panel_monster.cpp:70.
inline float sanitizeQ10(float v) { return std::isfinite(v) ? v : 0.0F; }

// panel_monster.cpp:127-142, "hexPolygon" — the centred hexagon matching
// the CSS clip-path, copied verbatim apart from being reachable here.
QPainterPath hexPolygon(const QRectF &r)
{
    auto X = [&](double pct) { return r.x() + r.width()  * pct; };
    auto Y = [&](double pct) { return r.y() + r.height() * pct; };
    QPainterPath p;
    p.moveTo(X(0.50), Y(0.00));
    p.lineTo(X(0.93), Y(0.25));
    p.lineTo(X(0.93), Y(0.75));
    p.lineTo(X(0.50), Y(1.00));
    p.lineTo(X(0.07), Y(0.75));
    p.lineTo(X(0.07), Y(0.25));
    p.closeSubpath();
    return p;
}

// panel_monster.cpp:164-197, "drawHex". Copied verbatim.
void drawHex(QPainter &p, const QRectF &cell, const QString &iconPath)
{
    p.setRenderHint(QPainter::Antialiasing);
    const QPainterPath outer = hexPolygon(cell);

    // .hex filter: drop-shadow rgba(0,0,0,.6) 2px 6px
    QPainterPath shadow = outer.translated(0, 2);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 153));
    p.drawPath(shadow);

    // .pic inset:4 hex clip-path with <img> cover
    const QRectF picCell(cell.x() + 4, cell.y() + 4,
                         cell.width() - 8, cell.height() - 8);
    p.save();
    p.setClipPath(hexPolygon(picCell));
    p.setBrush(QColor(40, 44, 48));   // slightly brighter than panel body
    p.drawRect(picCell);

    const QPixmap icon = mhw::Icon::renderRect(
        iconPath,
        QSize(static_cast<int>(picCell.width()),
              static_cast<int>(picCell.height())));
    if (!icon.isNull())
        p.drawPixmap(picCell.toRect(), icon);
    p.restore();

    // Outer SVG polygon stroke #ff7043 1.6
    p.setPen(QPen(QColor(kEnrageR, kEnrageG, kEnrageB), 1.6));
    p.setBrush(Qt::NoBrush);
    p.drawPath(outer);
}

// panel_monster.cpp:200-217, "drawBarV". Copied verbatim.
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

// panel_monster.cpp:152-162, "healthColor". HunterPie V2 /
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

// The font the identity block's name row draws, measured the same way
// the panel measures it (panel_monster.cpp:1130-1133).
QFont nameFont()
{
    QFont f(QStringLiteral("Chakra Petch"), kNmFont, QFont::Bold);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

} // namespace

namespace mhw {

MonsterIdentityModel buildMonsterIdentityModel(const mhw::MonsterSnapshot &m)
{
    MonsterIdentityModel model;
    model.address   = m.address;
    model.hasData   = true;
    model.monsterId = m.id;
    model.game      = m.game;
    // Rise MonsterData.xml contains no display-name field or
    // localization key. Keep the established runtime-name / ID fallback
    // instead of inventing one — the panel's own rule (:1138-1140).
    model.name      = m.internalName.isEmpty()
                          ? QStringLiteral("Monster %1").arg(m.id)
                          : m.internalName;
    model.size      = m.size;
    model.enraged   = m.enraged;
    model.enrageMaxSeconds = m.enrageMaxSeconds;
    model.enrageSeconds    = m.enrageSeconds;

    // ── HP row (.bar.hp, panel_monster.cpp:1251-1272) ──
    model.health.known = m.maxHealth > 0.0F;
    model.health.pct   = m.maxHealth > 0.0F
                             ? std::clamp(m.health / m.maxHealth, 0.0F, 1.0F)
                             : 0.0F;
    model.health.valueText = model.health.known
        ? QStringLiteral("%1 / %2")
              .arg(mhw::groupNumber(static_cast<int>(m.health)))
              .arg(mhw::groupNumber(static_cast<int>(m.maxHealth)))
        : QString::fromLatin1(monsterIdentityBarPlaceholder());
    model.health.pctText   = model.health.known
        ? mhw::percentage(m.health, m.maxHealth)
        : QString::fromLatin1("--");

    // ── Stamina row (.bar.st, panel_monster.cpp:1283-1307) ──
    // The reader's stamina pair is the real fatigue meter (HunterPie
    // draws this gauge 10px tall under the HP bar). maxStamina == 0 is
    // the panel's own "the 0x320 chain did not resolve" condition, and
    // the same one here: a zero maximum draws an empty track with the
    // placeholder pair, never a full bar that claims a value nobody read.
    model.stamina.known = m.maxStamina > 0.0F;
    model.stamina.pct   = m.maxStamina > 0.0F
                              ? std::clamp(sanitizeQ10(m.stamina)
                                               / sanitizeQ10(m.maxStamina),
                                           0.0F, 1.0F)
                              : 0.0F;
    model.stamina.valueText = model.stamina.known
        ? QStringLiteral("%1 / %2")
              .arg(mhw::groupNumber(static_cast<int>(m.stamina)))
              .arg(mhw::groupNumber(static_cast<int>(m.maxStamina)))
        : QString::fromLatin1(monsterIdentityBarPlaceholder());

    // ── Enrage row (.bar.er, panel_monster.cpp:1314-1356) ──
    //
    // The panel's own two expressions, read field for field:
    //   ragePct = maxBuildup > 0 ? clamp(buildup / maxBuildup)
    //                            : (enraged ? 1.0 : 0.0)
    //   text     = (enraged && enrageSeconds > 0) ? "MAX" : "<pct>%"
    //
    // Note what that means for the readings: MAX is decided by the
    // TIMER, not by the fill. A monster enraged with no live seconds is
    // 100% filled and reads "100%", not "MAX".
    const bool showRage = sanitizeQ10(m.enrageMaxBuildup) > 0.0F
                          || m.enraged;
    if (sanitizeQ10(m.enrageMaxBuildup) > 0.0F) {
        model.pct = std::clamp(sanitizeQ10(m.enrageBuildup)
                                   / sanitizeQ10(m.enrageMaxBuildup),
                               0.0F, 1.0F);
    } else {
        // No buildup pair: the panel falls back to a binary on `enraged`,
        // which is the only fill it can honestly express.
        model.pct = m.enraged ? 1.0F : 0.0F;
    }
    model.pulsing = m.enraged && m.enrageSeconds > 0.0F;
    model.reading = showRage
        ? (model.pulsing ? MonsterEnrageReading::Max
                         : MonsterEnrageReading::Percent)
        : MonsterEnrageReading::Empty;

    return model;
}

} // namespace mhw

MonsterIdentityStripPanel::MonsterIdentityStripPanel(QWidget *parent,
                                                     int subWindowIndex)
    : Panel(QStringLiteral("monster-identity"), Corner::TopLeft, parent,
        subWindowIndex)
{
    // No chrome, no title row: this window is a fragment of the monster
    // panel, not a panel. The translucent body still comes from
    // Panel::paintEvent(), which is what keeps a fragment legible over
    // the game — see panel.h's v0.12.1 note on what the chrome switch
    // governs (the FRAME alone, never the surface).
    setChromeVisible(false);

    // Own window-title key (distinct from ui.monster_title, the panel's
    // own). The window draws no cap of its own, so the key is the one
    // main.cpp's applyLocale re-sets on a locale flip.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
}

void MonsterIdentityStripPanel::update(const mhw::MonsterSnapshot &m)
{
    // Same shape as MonsterPanel::update(): cache what paintPanel()
    // needs, schedule one repaint. ONE call resolves all four blocks
    // from the same snapshot, so the identity window can never show
    // one monster's portrait next to another's HP bar.
    model_ = mhw::buildMonsterIdentityModel(m);

    // Demo numbers are never live state (the rule every split window
    // applies): the first authoritative snapshot replaces them outright,
    // so an edit-mode preview cannot survive into a live session.
    demoData_ = false;

    triggerUpdate();
}

void MonsterIdentityStripPanel::clear()
{
    // No live monster: the reader's monster list is empty, or every entry
    // in it is dead (selectMonsterTarget answered -1). The cached model is
    // DROPPED rather than left standing, because it is the only state the
    // paint reads and it describes the monster that just died.
    //
    // Without this the window keeps the last quest's portrait, name and HP
    // fraction, hasContent() keeps answering true through the
    // identityInfoEnabled() arm (the mask did not change), and the main
    // loop keeps the surface mounted — the reported 「打完一把回到据点不清空」.
    // It is not enough to hide the window: the next quest's update() would
    // then have to overwrite every field, and a single unread field would
    // resurrect a stale one.
    //
    // The same assignment shape as the demo path: the model is a plain
    // struct, so `= {}` is its full reset and leaves hasData false, which
    // is exactly what hasContent() and paintPanel()'s defensive branch
    // already test.
    model_ = {};

    // The enrage pulse has nothing to count down for a monster that is
    // gone. An invalid clock answers phase 0 (see pulsePhase()), which is
    // the brightest stop of the bar — a stopped pulse, not a frozen one.
    phaseClock_.invalidate();

    triggerUpdate();
}

void MonsterIdentityStripPanel::retranslateUi()
{
    // Cached strings first, for the reason the pilot's retranslateUi()
    // states: main.cpp's applyLocale re-sets the title from the same
    // key, so the second set is deliberate redundancy. The 体力 label
    // and the demo monster name are re-read from the table at DRAW
    // time (not cached in the model), so there is nothing
    // locale-dependent to rebuild here.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
    triggerUpdate();
}

void MonsterIdentityStripPanel::setGameForDemo(mhw::GameId game)
{
    if (game_ == game)
        return;
    game_ = game;
    // Drop the one-shot demo seed so the next paint re-runs
    // setupDemoData() with the new game. The crown tables and the
    // capture colour are game-keyed, so a Rise preview that kept
    // World's seed would tint the wrong palette.
    resetDemoPrimed();
    triggerUpdate();
}

void MonsterIdentityStripPanel::setDemoGame(mhw::GameId game)
{
    // Delegate to the non-virtual the live overlay calls. The two names
    // exist because the two call sites need two different dispatch
    // mechanisms: main.cpp constructs this window as a concrete stack
    // object and calls setGameForDemo() directly; the Console's split
    // preview host holds one as std::unique_ptr<Panel> and can only
    // reach it virtually. Both land in the same body, so the preview and
    // the live overlay cannot disagree about which game the demo
    // represents — the disagreement the user reported as
    // 「Rise 预览显示衣装、World 预览显示翔虫」.
    setGameForDemo(game);
}

bool MonsterIdentityStripPanel::identityInfoEnabled() const
{
    return (sectionMask() & mhw::MonsterSection::Info) != 0;
}

bool MonsterIdentityStripPanel::hpEnabled() const
{
    return (sectionMask() & mhw::MonsterSection::Hp) != 0;
}

bool MonsterIdentityStripPanel::staminaEnabled() const
{
    return (sectionMask() & mhw::MonsterSection::Stamina) != 0;
}

bool MonsterIdentityStripPanel::enrageEnabled() const
{
    return (sectionMask() & mhw::MonsterSection::Enrage) != 0;
}

bool MonsterIdentityStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    // ANY block on with data keeps the window mounted — never "all of
    // them". This is the answer to the user's "close the enrage bar
    // without closing the monster": the window survives losing a block.
    if (!model_.hasData)
        return false;   // no snapshot ever arrived
    if (identityInfoEnabled())
        return true;
    if (hpEnabled() && model_.hasHealth())
        return true;
    if (staminaEnabled() && model_.hasStamina())
        return true;
    if (enrageEnabled() && model_.hasEnrage())
        return true;
    return false;
}

int MonsterIdentityStripPanel::infoBlockHeight() const
{
    if (!identityInfoEnabled())
        return 0;
    // panel_monster.cpp's kInfoH = kTitleH + kRowGap + kHexH, with the
    // row gap now the split's BETWEEN grade.
    return kTitleH + kBlockGap + kHexH;
}

int MonsterIdentityStripPanel::hpBlockHeight() const
{
    if (!hpEnabled())
        return 0;
    // panel_monster.cpp's hpH = kBarH + 2*kBarVPad. NO per-block
    // margins: the window has ONE outer inset (kMargin at each end of
    // contentHeight()), and the single-block windows' double-margin
    // arithmetic belonged to a window that held exactly one block.
    // Stacking four such heights here would reserve 3*2*11 = 66px of
    // dead vertical space.
    return kBarH + 2 * kBarVPad;
}

int MonsterIdentityStripPanel::staminaBlockHeight() const
{
    if (!staminaEnabled())
        return 0;
    // panel_monster.cpp's stamH = kStamBarH + 2*kBarVPad. See
    // hpBlockHeight() for why there is no margin term here.
    return kStamBarH + 2 * kBarVPad;
}

int MonsterIdentityStripPanel::enrageBlockHeight() const
{
    if (!enrageEnabled())
        return 0;
    // panel_monster.cpp's rageH = kRageBarH + 2*kBarVPad. See
    // hpBlockHeight() for why there is no margin term here.
    return kRageBarH + 2 * kBarVPad;
}

int MonsterIdentityStripPanel::identityTextWidth() const
{
    if (!identityInfoEnabled())
        return 0;

    // ---- One gateset, three consumers: width, reservation, draw. ------
    // The panel's identity row is a flex: hex on the left, then a right
    // column holding the name row (crown + name + [ID]) and the second
    // row (size chip + enrage tag). The window measures the SAME
    // strings and reserves the natural width — the reason a 380px
    // panel column can express it and a window must also.
    const QFontMetrics fm(nameFont());

    // Row 1: the name itself, plus the crown when there is one, plus
    // the right-aligned [ID n] reading. The crown only exists with a
    // size AND a threshold table entry, so it is measured from the
    // model's own fields rather than always assumed.
    int nameW = fm.horizontalAdvance(model_.name);
    if (model_.size > 0.0F && mhw::crownThresholdsFor(model_.game,
                                                      model_.monsterId))
        nameW += kCrownSize + 7;
    const QString ids = QStringLiteral("[ID %1]").arg(model_.monsterId);
    const int idsW = fm.horizontalAdvance(ids);

    // Row 2: the size chip (text + 2x padding) and the enrage tag,
    // which the panel right-aligns in a fixed 110px band.
    int chipW = 0;
    if (model_.size > 0.0F) {
        QFont szFont(QStringLiteral("Chakra Petch"), 9);
        chipW = QFontMetrics(szFont).horizontalAdvance(
                    mhw::sizeMultiplier(model_.size))
                + 2 * kSzChipPadX + 7;   // + the panel's 7px gap
    }
    const int enrageTagW = (model_.enraged && model_.pulsing)
        ? 110 : (model_.reading != mhw::MonsterEnrageReading::Empty ? 110 : 0);

    // hex + the panel's own 10px gap + the widest of the two rows.
    const int rowMax = std::max(nameW + idsW + 10,
                                chipW + enrageTagW);
    return kHexW + 10 + std::max(rowMax, 120);
}

int MonsterIdentityStripPanel::hpTextWidth() const
{
    if (!hpEnabled())
        return 0;
    // Content width, not a chrome width: the widest of the bar's two
    // texts, measured in the same font the paint sets, plus the bar's
    // 8px inset on both sides and the margin on both sides. The
    // "18,420 / 25,800" pair is what actually governs — a percentage
    // never exceeds "100.0%".
    const QFont valueFont(QStringLiteral("Chakra Petch"),
                          kBarValueFontSize, QFont::Medium);
    const QFontMetrics valueMetrics(valueFont);
    const int widest =
        valueMetrics.horizontalAdvance(model_.health.valueText)
        + valueMetrics.horizontalAdvance(model_.health.pctText);
    if (widest <= 0)
        return 2 * kMargin;
    return widest + 2 * kBarTextInset + 2 * kMargin;
}

int MonsterIdentityStripPanel::staminaTextWidth() const
{
    if (!staminaEnabled())
        return 0;
    // The 体力 label and the number pair, measured in the same font the
    // paint sets, with the bar's 8px inset separating them and the
    // margin on both sides. Measured from the strings the paint will
    // actually draw — the label from the table (so a locale switch
    // resizes the window honestly rather than clipping) and the numbers
    // from the model.
    const QFont stamFont(QStringLiteral("Chakra Petch"), kStamFontSize);
    const QFontMetrics stamMetrics(stamFont);
    const int widest =
        stamMetrics.horizontalAdvance(mh::tr("ui.monster_stamina"))
        + 2 * kBarTextInset
        + stamMetrics.horizontalAdvance(model_.stamina.valueText);
    if (widest <= 0)
        return 2 * kMargin;
    return widest + 2 * kMargin;
}

int MonsterIdentityStripPanel::contentWidth() const
{
    // The window is as wide as its WIDEST block. Each block measures
    // itself; the window takes the maximum so a turned-off block can
    // never leave the others squeezed into its leftover width.
    int w = 2 * kMargin;
    w = std::max(w, identityTextWidth());
    w = std::max(w, hpTextWidth());
    w = std::max(w, staminaTextWidth());
    // The enrage bar carries no text of its own wider than its track,
    // so it needs nothing beyond the shared floor.
    w = std::max(w, 2 * kMargin);
    return w;
}

int MonsterIdentityStripPanel::contentHeight() const
{
    // panel_monster.cpp:1072-1082's block table, one gap between
    // adjacent ENABLED blocks. A block that is off contributes no gap
    // either — that is what keeps turning 激怒 off from leaving a
    // 11px hole in the middle of the window.
    int total = 0;
    bool first = true;
    const auto addBlock = [&](int blockH) {
        if (blockH <= 0)
            return;
        if (!first)
            total += kBlockGap;
        total += blockH;
        first = false;
    };
    addBlock(infoBlockHeight());
    addBlock(hpBlockHeight());
    addBlock(staminaBlockHeight());
    addBlock(enrageBlockHeight());
    total += 2 * kMargin;
    // Never a 0-height surface: panel_sizing's own collapsed answer.
    return total > 0 ? total
                     : mhw::panel_sizing::gridEmptySize(kMargin).height;
}

double MonsterIdentityStripPanel::pulsePhase() const
{
    // A stopped clock answers 0, which the pulse cosine below turns into
    // the bar's brightest stop — the same value a freshly-started clock
    // answers. So a bar that is not counting down never flickers.
    if (!phaseClock_.isValid())
        return 0.0;
    const qint64 elapsed = phaseClock_.elapsed();
    return static_cast<double>(elapsed % kPulsePeriodMs) / kPulsePeriodMs;
}

void MonsterIdentityStripPanel::setupDemoData()
{
    // Edit-mode preview: the same monster MonsterPanel::setupDemoData()
    // seeds (火龙, 71%, 700/1000 stamina, enraged with 32s left), so the
    // preview and the panel agree on what a real monster looks like.
    // Numbers only — the 体力 label and the monster name are re-read
    // from the table at draw time — so there is nothing to re-seed on a
    // locale flip.
    mhw::MonsterSnapshot m;
    m.address          = 0xDEADBEEFULL;
    m.id               = 1;                       // 火龙
    m.game             = game_;
    m.internalName     = mh::tr("data.demo.monster_name");
    m.size             = 1.25F;                   // Gold crown
    m.maxHealth        = 25800.0F;
    m.health           = 18420.0F;                // 71%
    m.maxStamina       = 1000.0F;
    m.stamina          = 700.0F;
    m.enraged          = true;
    m.enrageMaxSeconds = 120.0F;
    m.enrageSeconds    = 88.0F;                   // 32s remaining
    m.enrageMaxBuildup = 100.0F;
    m.enrageBuildup    = 100.0F;
    model_ = mhw::buildMonsterIdentityModel(m);
    demoData_ = true;
}

void MonsterIdentityStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an
    // unmounted-but-alive window never renders a leftover sliver. The
    // live path never reaches here — the main loop asks
    // hasVisibleContent() first. This is the edit-preview / defensive
    // branch.
    if (!model_.hasData) {
        setContentSize(2 * kMargin, h);
        return;
    }

    // setContentSize has an idempotent guard inside Panel, so calling it
    // per repaint (and therefore per drag frame) is free.
    setContentSize(w, h);

    // Re-read the width the window actually got. setContentSize()
    // applied the user's minimum-width floor to its arguments, but `w`
    // is still the pre-floor content number — using it here is exactly
    // the bug the floor was added to fix: the window would widen and the
    // content would stay narrow, leaving empty background to the right.
    //
    // The window only ever grows, so the same max() the base class
    // applied reproduces it without asking the compositor anything.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE. drawV03Chrome() is deliberately not called — this
    // window is a fragment of the monster panel, and a panel frame
    // around a fragment is the shape the split exists to remove. The
    // translucent body still comes from Panel::paintEvent().
    const int innerW = drawnW - 2 * kMargin;
    const int innerX = kMargin;

    int y = kMargin;

    // ── 1+2. Info block (ptitle + hexwrap) ──────────────────────────
    if (identityInfoEnabled()) {
        // Panel title. The panel's own .ptitle is a "怪物 MONSTER"
        // stamps row with an "em\emNNN · address" meta on the right;
        // a fragment has no stamp of its own, so the identity window
        // keeps the panel's title text at the same size and spacing.
        QFont tFont(QStringLiteral("Chakra Petch"), 9, QFont::Bold);
        tFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
        tFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(tFont);
        p.setPen(QColor(245, 246, 247));
        const QRectF titleRect(innerX, y, innerW, kTitleH);
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                   mh::tr("ui.monster_header"));
        y += kTitleH + kBlockGap;

        // .hexwrap — panel_monster.cpp:1126, the same 48x54 hex.
        const QRectF hexCell(innerX, y, kHexW, kHexH);
        drawHex(p, hexCell, mhw::Icon::monsterPath(model_.monsterId,
                                                   model_.game));

        // .mtitle sits in the right column of the flex row.
        const QFont nmFont = nameFont();
        p.setFont(nmFont);
        p.setPen(QColor(245, 246, 247));
        const int nmY = y + 4;
        const int nmH = kNmFont + 2;
        int nmX = static_cast<int>(hexCell.right()) + 10;

        // .crownmini (15×15). Use only explicit metadata for the active
        // game. Missing Rise <Crowns> data (and missing individual
        // thresholds) disables the crown rather than borrowing a World
        // table/default. panel_monster.cpp:1148-1169, verbatim.
        const auto *thr = mhw::crownThresholdsFor(model_.game,
                                                  model_.monsterId);
        if (model_.size > 0.0F && thr) {
            QString crownPath;
            QColor crownTint;
            if (thr->at(2) > 0.0F && model_.size >= thr->at(2)) {
                crownPath = QStringLiteral(":/icons/crowns/crown_king.svg");
                crownTint = QColor(231, 197, 7);
            } else if (thr->at(1) > 0.0F && model_.size >= thr->at(1)) {
                crownPath = QStringLiteral(":/icons/crowns/crown_large.svg");
                crownTint = QColor(189, 189, 189);
            } else if (thr->at(0) > 0.0F && model_.size <= thr->at(0)) {
                crownPath = QStringLiteral(":/icons/crowns/crown_mini.svg");
                crownTint = QColor(33, 150, 243);
            }
            if (!crownPath.isEmpty()) {
                const QPixmap crown = mhw::Icon::render(crownPath,
                                                        kCrownSize, crownTint);
                if (!crown.isNull()) {
                    p.drawPixmap(nmX, nmY, crown);
                    nmX += kCrownSize + 7;
                }
            }
        }

        // .nm
        p.drawText(QRectF(nmX, nmY, innerX + innerW - nmX, nmH),
                   Qt::AlignLeft | Qt::AlignVCenter, model_.name);

        // .ids — 9px --t3, right-aligned within the mname-row.
        QFont idsFont(QStringLiteral("Chakra Petch"), 9);
        idsFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        idsFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(idsFont);
        p.setPen(QColor(146, 148, 149));
        const QString ids = QStringLiteral("[ID %1]").arg(model_.monsterId);
        const int idsW = QFontMetrics(idsFont).horizontalAdvance(ids);
        p.drawText(QRectF(innerX + innerW - idsW, nmY, idsW, nmH),
                   Qt::AlignLeft | Qt::AlignVCenter, ids);

        // .szchip + .enrage-tag on the second row of mname-row.
        const int r2Y = nmY + nmH + 4;
        int r2X = static_cast<int>(hexCell.right()) + 10;
        if (model_.size > 0.0F) {
            QFont szFont(QStringLiteral("Chakra Petch"), 9);
            szFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
            szFont.setStyleStrategy(QFont::PreferAntialias);
            const QString szTxt = mhw::sizeMultiplier(model_.size);
            const int szW = QFontMetrics(szFont).horizontalAdvance(szTxt)
                          + 2 * kSzChipPadX;
            const int szH = szFont.pointSize() + 4;
            const QRectF szRect(r2X, r2Y, szW, szH);
            p.setPen(QPen(QColor(kGoldR, kGoldG, kGoldB, 76), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(szRect, 1, 1);
            p.setPen(QColor(kGoldR, kGoldG, kGoldB));
            p.setFont(szFont);
            p.drawText(szRect.adjusted(kSzChipPadX, 0, -kSzChipPadX, 0),
                       Qt::AlignCenter, szTxt);
            r2X += szW + 7;
        }

        // .enrage-tag: 9px Chakra Bold #ff7043 (HTML v8 spec, not 11px),
        // margin-left:auto so it sits on the right edge of mname-row.
        // THE PANEL'S OWN CONDITION, WHICH THE WINDOW NARROWS ONE STEP
        // FURTHER: the tag only exists when the ENRAGE block is on, so
        // turning 激怒 off removes it together with the bar. That is the
        // user's 「比如激怒条」 request applied to every place the
        // enrage reading surfaces, not just to the meter.
        if (enrageEnabled()) {
            if (model_.enraged && model_.reading ==
                    mhw::MonsterEnrageReading::Max) {
                const int elapsed = static_cast<int>(
                    model_.enrageMaxSeconds - model_.enrageSeconds);
                const QString enTxt = mh::tr("ui.monster_enraged_secs")
                                          .arg(elapsed);
                QFont eFont(QStringLiteral("Chakra Petch"), 9, QFont::Bold);
                eFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
                eFont.setStyleStrategy(QFont::PreferAntialias);
                p.setFont(eFont);
                const QRectF eRect(innerX + innerW - 110, r2Y, 110, 14);
                const QColor glowCol(kEnrageR, kEnrageG, kEnrageB);
                QColor shadow = glowCol; shadow.setAlphaF(0.30);
                for (const QPointF &off : { QPointF(0, -1), QPointF(0, 1),
                                            QPointF(-1, 0), QPointF(1, 0) }) {
                    p.setPen(shadow);
                    p.drawText(eRect.translated(off),
                               Qt::AlignRight | Qt::AlignVCenter, enTxt);
                }
                p.setPen(glowCol);
                p.drawText(eRect, Qt::AlignRight | Qt::AlignVCenter, enTxt);
            } else if (model_.reading ==
                       mhw::MonsterEnrageReading::Percent) {
                const QString enTxt = mh::tr("ui.monster_enrage_pct")
                                          .arg(static_cast<int>(
                                              model_.pct * 100));
                QFont eFont(QStringLiteral("Chakra Petch"), 9, QFont::Bold);
                eFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
                eFont.setStyleStrategy(QFont::PreferAntialias);
                p.setFont(eFont);
                p.setPen(QColor(kGoldR, kGoldG, kGoldB));
                const QRectF eRect(innerX + innerW - 110, r2Y, 110, 14);
                p.drawText(eRect, Qt::AlignRight | Qt::AlignVCenter, enTxt);
            }
        }
        y += kHexH;
    }

    // ── 3. .bar.hp ─────────────────────────────────────────────────
    if (hpEnabled() && model_.hasHealth()) {
        y += kBlockGap;   // gap before HP (was hexwrap's trailing gap)
        const QColor hpC = healthColor(model_.health.pct, model_.game,
                                       model_.monsterId);
        const QRectF hpBarRect(innerX, y, innerW, kBarH + 2 * kBarVPad);
        drawBarV(p, hpBarRect, model_.health.pct, hpC.lighter(115), hpC);
        {
            QFont hpFont(QStringLiteral("Chakra Petch"), kBarValueFontSize,
                         QFont::Medium);
            hpFont.setStyleStrategy(QFont::PreferAntialias);
            p.setFont(hpFont);
            p.setPen(QColor(245, 246, 247));
            // Text band sits inside the bar with kBarVPad top/bottom so
            // the 11px glyph doesn't touch the bar edges.
            const QRectF hpTxtRect(innerX, y + kBarVPad, innerW, kBarH);
            p.drawText(hpTxtRect.adjusted(kBarTextInset, 0, -kBarTextInset, 0),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       model_.health.valueText);
            p.drawText(hpTxtRect.adjusted(kBarTextInset, 0, -kBarTextInset, 0),
                       Qt::AlignRight | Qt::AlignVCenter,
                       model_.health.pctText);
        }
        y += kBarH + 2 * kBarVPad;
    }

    // ── 3b. .bar.st (monster stamina / fatigue meter) ───────────────
    if (staminaEnabled() && model_.hasStamina()) {
        y += kBlockGap;
        const QColor stHi(kStamR, kStamG, kStamB);
        const QRectF barRect(innerX, y, innerW, kStamBarH + 2 * kBarVPad);
        drawBarV(p, barRect, model_.stamina.pct, stHi.lighter(115), stHi);

        QFont stamFont(QStringLiteral("Chakra Petch"), kStamFontSize);
        stamFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(stamFont);
        p.setPen(QColor(245, 246, 247));
        const QRectF textRect(innerX, y + kBarVPad, innerW, kStamBarH);
        // Left label, right numbers — the panel's .bar.st layout, verbatim.
        p.drawText(textRect.adjusted(kBarTextInset, 0, 0, 0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   mh::tr("ui.monster_stamina"));
        p.drawText(textRect.adjusted(0, 0, -kBarTextInset, 0),
                   Qt::AlignRight | Qt::AlignVCenter,
                   model_.stamina.valueText);
        y += kStamBarH + 2 * kBarVPad;
    }

    // ── 3c. .bar.er (enrage meter) ──────────────────────────────────
    if (enrageEnabled() && model_.hasEnrage()) {
        y += kBlockGap;

        // The pulse, verbatim from the panel's .bar.er block: scale the
        // red/orange fill's brightness between 0.89 and 1.0 with a
        // cosine. The clock is (re)started here rather than in update()
        // because the paint is where the phase is consumed and a bar
        // that is not counting down is never painted at all.
        if (model_.pulsing) {
            if (!phaseClock_.isValid())
                phaseClock_.start();
        } else if (phaseClock_.isValid()) {
            phaseClock_.invalidate();
        }
        const float pulse = 0.89F + 0.11F
            * static_cast<float>(std::cos(pulsePhase()
                                          * 2.0 * std::acos(-1.0)));

        QColor hi(kErBar1R, kErBar1G, kErBar1B);
        QColor lo(kErBar2R, kErBar2G, kErBar2B);
        const auto pulseChan = [pulse](int c) {
            return static_cast<int>(c * pulse);
        };
        hi.setRed(pulseChan(hi.red()));
        hi.setGreen(pulseChan(hi.green()));
        hi.setBlue(pulseChan(hi.blue()));
        lo.setRed(pulseChan(lo.red()));
        lo.setGreen(pulseChan(lo.green()));
        lo.setBlue(pulseChan(lo.blue()));

        const QRectF barRect(innerX, y, innerW, kRageBarH + 2 * kBarVPad);
        drawBarV(p, barRect, model_.pct, hi, lo);

        QFont rageFont(QStringLiteral("Chakra Petch"), kRageFontSize);
        rageFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(rageFont);
        const QRectF textRect(innerX, y + kBarVPad, innerW, kRageBarH);
        if (model_.reading == mhw::MonsterEnrageReading::Max) {
            // "MAX" is an untranslated token, exactly as the panel draws
            // it (panel_monster.cpp:1350-1355).
            p.setPen(QColor(kEnrageR, kEnrageG, kEnrageB));
            p.drawText(textRect.adjusted(0, 0, -kBarTextInset, 0),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QStringLiteral("MAX"));
        } else {
            p.setPen(QColor(245, 246, 247));
            p.drawText(textRect.adjusted(0, 0, -kBarTextInset, 0),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QStringLiteral("%1%").arg(
                           static_cast<int>(model_.pct * 100)));
        }
        y += kRageBarH + 2 * kBarVPad;
    }
}
