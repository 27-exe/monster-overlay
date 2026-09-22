#include "panel_monster.h"

#include "core/string_table.h"
#include "monster/monster_types.h"
#include "rise/mhr_part_names.h"
#include "ui/formatters.h"
#include "ui/icon.h"
#include "ui/panel_sections.h"

#include <QColor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTimer>
#include <array>
#include <cmath>
#include <utility>

// =============================================================================
//
// MonsterPanel — a 1:1 rewrite of the .op.monster block in
// monster-overlay-concept (v8) HTML (assets/monster-overlay-concept 副本.html).
//
// HTML field order, from top to bottom:
//   .ptitle               9px Chakra Bold, letter-spacing 2.5
//                         <i> 8px --t3, right-aligned meta
//   .hexwrap              flex, gap:10
//     .hex                48×54, polygon stroke #ff7043 1.6, drop-shadow
//       .pic              inset:4 hex clip-path, <img> cover
//     .mtitle
//       .mname-row        flex, gap:7
//         .crownmini      15×15 SVG, tinted by data-crown (gold|silver|mini)
//         .nm             15px Chakra Bold letter-spacing 1
//         .ids            9px Chakra letter-spacing 1
//         .szchip         9px Chakra letter-spacing 1, border #ffc107 1px
//         .enrage-tag     11px Chakra Bold letter-spacing 1, color #ff7043,
//                         margin-left:auto, pulse animation
//   .bar.hp               height:15, fill green→amber→red,
//                         .txt 11px Chakra Medium, "18420 / 25800" + "71%"
//   .bar.er               height:9, fill #e64a19→#ff7043,
//                         .txt 8px, "怒气" + "MAX" / "%",
//                         erpulse 1.6s on the fill when MAX
//   .srow                 grid 4 cols, gap:5, margin 9 0
//     .sc                 background --cell, border 1px, padding 4 6 5
//     .sc[data-state=…]   border-color var(--sc)
//     .sc .top            flex gap:4 9px --t2 svg 11×11
//     .sc .mini           height:5 background #0a0b0c, <i> fill --sc
//     .sc .tm             position absolute right:5 top:4,
//                         9px Chakra Bold color --sc
//
// =============================================================================

namespace mh {
// v0.10.3-r6 (B1): moved ABOVE the anonymous namespace. buildPcList() —
// the .pc card aggregator — needs to resolve the part-fallback / tag
// strings while it fills entries, and it lives inside that namespace. The
// helper itself is unchanged.
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

namespace {

// ---- Geometry — straight from the HTML v8 stylesheet ----------------------
constexpr int kPanelWidth = 380;
constexpr int kPanelPad   = 9;     // .op padding:9
constexpr int kRowGap     = 9;     // gap between sections
constexpr int kTitleH     = 14;    // .ptitle visual row height
constexpr int kHexW       = 48;    // .hex width
constexpr int kHexH       = 54;    // .hex height
constexpr int kCrownSize  = 15;    // .crownmini
constexpr int kSzChipPadX = 5;     // .szchip padding 1 5
constexpr int kBarH       = 15;    // .bar height
constexpr int kRageBarH   = 6;     // .bar.er visual text band; small strip
                                    // with the MAX/% reading centred in it.
                                    // (Text glyph caps sit ~6px tall at 8pt.)
constexpr int kStamBarH   = 10;    // .bar.st — monster stamina (fatigue)
                                    // meter. HunterPie draws this gauge
                                    // 10px tall directly under the HP bar.
constexpr int kBarVPad    = 3;     // Vertical padding inside .bar so the
                                    // glyph doesn't sit flush against the
                                    // bar edges. Visual height = kBarH +
                                    // 2*kBarVPad for HP, kRageBarH + 2*kBarVPad
                                    // for the rage bar.
constexpr int kScCols     = 4;     // .srow grid-template-columns
constexpr int kScGap      = 5;     // .srow gap
constexpr int kScPadX     = 6;     // .sc padding
constexpr int kScPadY     = 4;
constexpr int kScTopFont  = 9;     // .sc .top
constexpr int kScTmFont   = 9;     // .sc .tm
constexpr int kScMiniH    = 5;     // .sc .mini
constexpr int kScIconSize = 11;    // .sc .top svg

// ---- Palette — straight from --c / --sc / --enrage / --tN ----------------
constexpr int kEnrageR = 255, kEnrageG = 112, kEnrageB = 67;   // #ff7043
constexpr int kGoldR   = 255, kGoldG   = 193, kGoldB   = 7;    // #ffc107
constexpr int kHpHighR =  76, kHpHighG = 175, kHpHighB =  80; // green:  HunterPie #4CAF50
constexpr int kHpMidR  = 251, kHpMidG  = 192, kHpMidB  =  45; // amber:  HunterPie #FBC02D
constexpr int kHpLowR  = 244, kHpLowG  =  67, kHpLowB  =  54; // red:    HunterPie #F44336
constexpr float kHpAmberPct   = 0.50F;  // ≤50% flips to amber
constexpr int kErBar1R = 230, kErBar1G =  74, kErBar1B = 25;  // --c
constexpr int kErBar2R = 255, kErBar2G = 112, kErBar2B = 67;  // --c2
constexpr int kStamR   = 246, kStamG   = 165, kStamB   =  34; // stamina:
                                    // HunterPie Yellow #F6A522, Scheme.xaml

constexpr int kPulsePeriodMs = 1600;  // .erpulse 1.6s

// Centred hexagon matching the CSS clip-path:
//   polygon(50% 0, 93% 25%, 93% 75%, 50% 100%, 7% 75%, 7% 25%)
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

// HunterPie V2 / MonsterData.xml capture mechanic:
//   > 50%       → green       (full health)
//   capture<HP≤50% → amber    (limping soon)
//   ≤ capture%   → red        (capturable per game data)
//
// Rise MonsterData.xml has no Capture=N percentage. Its known
// IsNotCapturable=true entries and all unknown Rise entries therefore stay
// green/amber; they must never inherit a World threshold.
QColor healthColor(float pct, mhw::GameId game, int monsterId)
{
    if (pct > kHpAmberPct)
        return QColor(kHpHighR, kHpHighG, kHpHighB);

    const auto cap = mhw::captureThresholdFor(game, monsterId);
    if (cap && *cap > 0 && pct <= static_cast<float>(*cap) / 100.0F)
        return QColor(kHpLowR, kHpLowG, kHpLowB);

    return QColor(kHpMidR, kHpMidG, kHpMidB);
}

// Draw the .hex block: SVG polygon stroke + drop shadow + inner .pic clipped
// to the inset hex with the monster portrait painted using object-fit: cover.
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

// Draw a .bar track + gradient fill + optional centered text span.
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

// .sc — status card cell.
struct ScEntry {
    QString name;
    QString iconPath;     // optional /icons/Traps/item_id_*.svg
    QColor  sc;           // --sc (border + tm + mini fill colour)
    float   pct{0.0F};    // mini progress 0..1
    QString tm;           // right-aligned timer text ("12s", "8s", "45%", "×2")
    bool    active{false};
};

void drawSc(QPainter &p, const QRectF &cell, const ScEntry &e)
{
    p.setRenderHint(QPainter::Antialiasing);

    // 1) Card background + border. .sc[data-state=active] uses --sc, otherwise --line.
    const QColor borderCol = e.active ? e.sc : QColor(42, 45, 47);
    p.setPen(QPen(borderCol, 1));
    p.setBrush(QColor(29, 32, 34));          // --cell
    p.drawRoundedRect(cell, 2, 2);

    // 2) .top: optional svg + name, baseline-aligned.
    const int topY     = static_cast<int>(cell.top()) + kScPadY;
    const int topRectH = kScTopFont + 2;
    const int iconSize = kScIconSize;
    const int iconX    = static_cast<int>(cell.x()) + kScPadX;
    const int iconY    = topY + (topRectH - iconSize) / 2;
    if (!e.iconPath.isEmpty()) {
        const QPixmap icon = mhw::Icon::render(e.iconPath, iconSize);
        if (!icon.isNull())
            p.drawPixmap(iconX, iconY, icon);
    }

    QFont nmFont(QStringLiteral("Chakra Petch"), kScTopFont);
    nmFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(nmFont);
    p.setPen(e.active ? e.sc : QColor(200, 205, 208));   // --t2
    const int nameX = iconX + (e.iconPath.isEmpty() ? 0 : iconSize + 4);
    const int nameRight = static_cast<int>(cell.right()) - kScPadX - 36;
    p.drawText(QRectF(nameX, topY, nameRight - nameX, topRectH),
               Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(nmFont).elidedText(
                   e.name, Qt::ElideRight, nameRight - nameX));

    // 3) .tm: 9px Chakra Bold, color --sc, right:5 top:4 inside the cell.
    QFont tmFont(QStringLiteral("Chakra Petch"), kScTmFont, QFont::Bold);
    tmFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(tmFont);
    p.setPen(e.active ? e.sc : QColor(140, 142, 144));
    p.drawText(QRectF(static_cast<int>(cell.right()) - kScPadX - 32,
                      topY, 32, topRectH),
               Qt::AlignRight | Qt::AlignVCenter, e.tm);

    // 4) .mini: 5px track + coloured fill, anchored to the bottom padding.
    const int miniY = static_cast<int>(cell.bottom()) - kScPadY - kScMiniH;
    const QRectF miniRect(cell.x() + kScPadX, miniY,
                          cell.width() - 2 * kScPadX, kScMiniH);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(10, 11, 12));
    p.drawRect(miniRect);
    const float clamped = std::clamp(e.pct, 0.0F, 1.0F);
    if (clamped > 0.001F) {
        p.setBrush(e.sc);
        p.drawRect(miniRect.x(), miniRect.y(),
                   miniRect.width() * clamped, miniRect.height());
    }
}

// ---- .pgrid — part grid (3 columns). HTML .pc/.pn/.tag/.mini layout ----
// v0.7.4 PR C: cell height is now conditional — a tenderize mini bar is
// inserted between .pn and .mini whenever the part has an active
// Clutch Claw tenderize (PartSnapshot.tenderizeDuration > 0). The cell
// height calculation lives inside paintPanel() because it depends on
// runtime data; the constant below is the *base* cell height without
// a tenderize strip.
constexpr int kPcCols     = 3;     // .pgrid grid-template-columns
constexpr int kPcGap      = 5;     // .pgrid gap
constexpr int kPcPadX     = 6;     // .pc padding 4 6
constexpr int kPcPadY     = 4;
constexpr int kPcPnFont   = 9;     // .pc .pn font-size
constexpr int kPcValueFont = 8;    // current/max HP row
constexpr int kPcValueH    = kPcValueFont + 2;
constexpr int kPcValueGap  = 2;
constexpr int kPcTagFont  = 8;     // .pc .tag font-size
constexpr int kPcTagPadX  = 4;     // .pc .tag padding 0 4
constexpr int kPcMiniH    = 4;     // .pc .mini height:4
constexpr int kPcPnGap    = 3;     // .pc .pn margin-bottom
constexpr int kPcTnH      = 3;     // tenderize mini bar (only when active)
constexpr int kPcTnGap    = 2;     // gap between .pn and the tenderize strip
// v0.7.4 PR C follow-up: when a tenderize strip is drawn we also render
// the "Ns" countdown label IMMEDIATELY above the strip (compact layout).
// The label sits in a kPcTnLabelH tall row; visually it occupies the
// gap between .pn and the strip (no extra kPcTnLabelGap needed).
constexpr int kPcTnLabelH = 9;     // "Ns" label height (matches kPcTagFont)

// v0.10.3-r6 (B1) — one definition of the .pc card's vertical budget.
// These three values used to be written out (twice) ad hoc inside
// paintPanel(); they now live here next to the rest of the kPc* geometry
// so the height sites and the draw site cannot drift apart again.
// base = .pn row + (value row + gap) + .mini row + top/bottom padding.
constexpr int kPcBaseCellH = kPcPadY + kPcPnFont + 2 + kPcPnGap
                           + kPcValueH + kPcValueGap
                           + kPcMiniH + kPcPadY;
// S3 follow-up: label height is included in the kPcTnGap budget, i.e. the
// strip+label sandwich occupies kPcTnLabelH + kPcTnH + kPcTnGap vertical
// real estate (label sits in what used to be the .pn→.mini gap, so it adds
// kPcTnLabelH + kPcTnH rather than just kPcTnH).
constexpr int kPcTnExtra = kPcTnLabelH + kPcTnH + kPcTnGap;

// v0.10.3-r6 (B1) dual-gauge: Row 1 of a .pc card. HunterPie's
// BossMonsterSeverablePartView.xaml:66-76 renders TWO gauges in the same
// card ("<!-- Flinch -->" then "<!-- Sever -->"), i.e. the hard-stagger
// bar sits ABOVE the primary-layer .mini; both are rendered. The
// growth rule is shared by layout and paint.
constexpr int kPcFlinchH = 4;      // Row 1 gauge track height (.mini-like)
constexpr int kPcGaugeGap = 2;     // gap between the two gauge rows
constexpr int kPcFlinchExtra = kPcFlinchH + kPcGaugeGap;
// B2: HunterPie Part.Default.Foreground == Blue == #4B8EEE
// (Themes/Base.xaml:380-381 → Themes/Colors/Scheme.xaml:99). HunterPie
// does NOT recolour this first gauge on break state — only the SECOND
// (Sever/Health) gauge swaps to Broken.Foreground — so there is no
// broken/severed branch in the Row 1 paint.
constexpr int kPcFlR = 0x4B;
constexpr int kPcFlG = 0x8E;
constexpr int kPcFlB = 0xEE;
// Keep the optional Flinch row and its height reservation on one gate.
constexpr bool kPcDrawFlinchRow = true;
// B2: the ONE place that decides whether a .pc card carries a Row 1
// flinch gauge. History lesson (v0.10.3 tenderize bug: the draw gate and
// two height reservations used different field names, so the strip never
// appeared): the gate and every reservation must be the SAME expression.
// The float overload is the primitive; the PcEntry overload is what
// drawPc() and the .pgrid pass call. The pcAreaH pass runs before pcList
// exists, so it calls the float overload with the raw
// shownParts[i].maxFlinch — all three sites funnel through here.
//
// `flinchMax == 0` is an absent denominator, not "0 % flinch". A layer
// present but empty is `flinchMax > 0 && flinchPct == 0`, and it
// must still reserve and draw — collapsing the two is exactly how the
// tenderize strip went invisible.
inline bool pcHasFlinch(float flinchMax) { return std::isfinite(flinchMax) && flinchMax > 0.0F; }
inline bool pcHasFlinch(const PcEntry &e)
{
    return pcHasFlinch(e.flinchMax);
}

// ---------------------------------------------------------------------------
// NaN/Inf guard — 2026-09-22 real-machine crash + garbage-bar fix.
//
// Reader values come from live game memory. A slot that is mid-teardown
// (target switch, multiplayer slot churn, part array realloc) can yield
// NaN or ±Inf for one tick. Two consequences, both observed on 2026-09-22:
//
//  1. Hard crash. Qt6's qRound() asserts on non-finite input
//     (qCheckedFPConversionToInteger → Q_ASSERT(!std::isnan(value))),
//     which calls qFatal() → SIGABRT. coredump backtrace:
//       qt_assert ← qRoundf ← MonsterPanel::paintPanel
//     Reproduced in a 3-player Rise hunt right after the target changed.
//
//  2. Garbage bars. NaN fails every comparison the intuitive way:
//     a non-finite denominator must never be treated as a usable layer;
//     and std::clamp(NaN, 0, 1) returns NaN, which propagates into the
//     bar width multiply. Symptom: blue bar present but length nonsense,
//     "bars don't match the monster's actual reactions".
//
// Non-finite is folded to 0.0F, which is PartSnapshot's existing
// "this part has no such layer" sentinel — no new state is introduced,
// and a card with a NaN layer simply reads as "no layer" for one tick
// instead of crashing or lying.
//
// The moral: any float that reaches a gate, a quantizer, a clamp or a
// divide in this file must come through one of these two first.
// ---------------------------------------------------------------------------
inline float sanitizePartValue(float v)
{
    return std::isfinite(v) ? v : 0.0F;
}

struct PartDisplayLayers {
    mhw::PartHealthPair flinch;
    mhw::PartHealthPair primary;
    PcEntry::Layer primaryKind;
};

PartDisplayLayers partDisplayLayers(mhw::GameId game, const mhw::PartSnapshot &part)
{
    const mhw::PartHealthPair flinch{part.flinch, part.maxFlinch};
    // Readers normalize the selected Sever/Break pair into health/maxHealth.
    // World Severable is sourced from its severable table; World Breakable
    // from its threshold table. Rise selects its sever or break array by
    // PartType. Rise's additional breakHealth pair is retained, not painted.
    switch (game) {
    case mhw::GameId::Rise:
        if (part.partType == mhw::PartType::Severable)
            return {flinch, {part.health, part.maxHealth}, PcEntry::Layer::Sever};
        if (part.partType == mhw::PartType::Breakable)
            return {flinch, {part.health, part.maxHealth}, PcEntry::Layer::Break};
        return {flinch, flinch, PcEntry::Layer::Flinch};
    case mhw::GameId::World:
        if (part.partType == mhw::PartType::Severable)
            return {flinch, {part.health, part.maxHealth}, PcEntry::Layer::Sever};
        if (part.partType == mhw::PartType::Breakable)
            return {flinch, {part.health, part.maxHealth}, PcEntry::Layer::Break};
        return {flinch, flinch, PcEntry::Layer::Flinch};
    default:
        return {flinch, mhw::partHealthForDisplay(part), PcEntry::Layer::Flinch};
    }
}

// Quantize for the AutoHide signature, with the same finite guarantee.
inline quint32 sanitizeQ10(float v)
{
    return std::isfinite(v) ? static_cast<quint32>(qRound(v * 10.0F)) : 0u;
}

// v0.8.4-r18 parts-display-filter: the .pgrid only displays parts the
// player can actually act on — severable (可切断) or breakable (可破坏).
// A live Rise read (reported monster id=14) collected 16 parts; every one
// of them was rendered as a card ("部位 0"…"部位 15"), so the grid was
// dominated by flinch-only body parts the player never interacts with
// ("部位是不是太多了？只显示有用的比较好").
//
// Rule: keep partType != Flinch. Fallback: when that leaves nothing
// (a monster whose entire part table is flinch-only, e.g. malformed /
// not-yet-decoded schema), show the first kFlinchFallbackParts parts in
// their original order so the section never collapses to an empty box.
constexpr int kFlinchFallbackParts = 4;

QVector<mhw::PartSnapshot> displayableParts(const QVector<mhw::PartSnapshot> &parts)
{
    QVector<mhw::PartSnapshot> shown;
    shown.reserve(parts.size());
    for (const mhw::PartSnapshot &p : parts) {
        if (p.partType != mhw::PartType::Flinch)
            shown.append(p);
    }
    if (shown.isEmpty()) {
        const int n = std::min(kFlinchFallbackParts,
                               static_cast<int>(parts.size()));
        for (int i = 0; i < n; ++i)
            shown.append(parts[i]);
    }
    return shown;
}

// v0.10.3-r6 (B1): PcEntry moved to panel_monster.h (comment there
// explains why). Do NOT re-declare it here.

// v0.10.3-r6 (B1) — THE ONE definition of how tall a .pc card is.
// Everything that lays the parts grid out MUST call this: the panel
// height reservation inside paintPanel() and the .pgrid render below it
// both fold its result into the shared cell height. v0.10.3's tenderize
// strip went invisible when its draw gate and height gate disagreed.
//
// Returns the height ADDED to kPcBaseCellH by the optional rows: the
// tenderize strip and the Row 1 flinch gauge. Both are gated by the same
// predicates their painters use, so the reserved height and the drawn
// height can never diverge.
inline int pcGaugeExtraH(const PcEntry &e)
{
    int extra = 0;
    // The remaining-time gate matches the strip painter and HunterPie's
    // Tenderize visibility binding; a spent timer reserves no space.
    if (e.tenderizeDuration > 0.0F)
        extra += kPcTnExtra;
    // The optional Flinch gauge uses the same gate as drawPc().
    if (kPcDrawFlinchRow && pcHasFlinch(e))
        extra += kPcFlinchExtra;
    return extra;
}

// One predicate for "this .pc card carries a tenderize strip", shared
// verbatim by the draw gate and BOTH height reservations. v0.10.x shipped
// an invisible strip because the draw gate read tenderizeMaxDuration while
// the reservations read tenderizeDuration; a second refactor pinned it as
// a "0s" empty track by gating on the max-duration SENTINEL. HunterPie
// binds visibility to the REMAINING time (MonsterPartContextHandler.cs:109
// computes MaxTenderize - Tenderize, consumed at BossMonsterPartView.xaml:94
// via NumberToBooleanConverter, i.e. value != 0.0f). Our reader already
// stores remaining seconds in tenderizeDuration (monster_reader.cpp:374-375),
// so "> 0" is the same gate.
//
// DO NOT "simplify" this by folding the two overloads or by inlining the
// comparison back at a call site: v0.10.x regressed exactly that way — see
// the "0s" empty-track bug report and the invisible-strip regression in
// 48b05f3. All five call sites (tenderizeHeight reservation in drawPc,
// the strip draw in drawPc, and the two cellHeights reservations) must
// resolve to this one function.
inline bool pcHasTenderize(const PcEntry &e)
{
    return e.tenderizeDuration > 0.0F;
}

inline bool pcHasTenderize(const mhw::PartSnapshot &p)
{
    return p.tenderizeDuration > 0.0F;
}

void drawPc(QPainter &p, const QRectF &cell, const PcEntry &e)
{
    // .pc — card bg + border, padding 4 6.
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(42, 45, 47), 1));   // --line
    p.setBrush(QColor(29, 32, 34));          // --cell
    p.drawRoundedRect(cell, 2, 2);

    // .pn — name + tag chip (justify-content:space-between).
    QFont pnFont(QStringLiteral("Chakra Petch"), kPcPnFont);
    pnFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(pnFont);
    p.setPen(QColor(200, 205, 208));         // --t2
    const int pnY = static_cast<int>(cell.top()) + kPcPadY;
    const int pnH = kPcPnFont + 2;
    // Chip text decided up front so nameW's width reservation and the
    // draw gate below read the SAME value. Pre-2026-09-22 the chip was
    // unconditional (`破 0` / `斩 0` were drawn), mirroring v0.8.4-r23's
    // "show the raw counter unconditionally" fix for World non-host.
    // 2026-09-22 carve-out: Rise has NO Counter field at all
    // (MHRPartStructure carries only Health/Sever/Flinch pairs), so
    // HunterPie's Rise "Breaks" is 0 by construction and a "破 0 / 斩 0"
    // chip is pure noise — exactly what the user reported. Suppress the
    // chip at 0 and give the part name the full width back.
    // World's counter comes from the real +0x18 slot, so its chip
    // appears the moment the field is nonzero (unchanged behaviour).
    const QString chipText = (!e.tag.isEmpty() && e.counter > 0)
        ? QStringLiteral("%1 %2").arg(e.tag).arg(e.counter)
        : QString();
    const int nameW = std::max(0, static_cast<int>(cell.width())
        - 2 * kPcPadX - (chipText.isEmpty() ? 0 : 36));
    p.drawText(QRectF(cell.x() + kPcPadX, pnY, nameW, pnH),
               Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(pnFont).elidedText(e.name, Qt::ElideRight, nameW));

    if (!chipText.isEmpty()) {
        // HunterPie's chip (BossMonsterBreakablePartView.xaml /
        // BossMonsterSeverablePartView.xaml bind "Breaks" =
        // MHWMonsterPart.Count ← data.Counter). On a non-host World
        // client this count is the one per-part signal the game keeps in
        // sync, so it must stay visible whenever it is nonzero.
        QFont tagFont(QStringLiteral("Chakra Petch"),
                      kPcTagFont, QFont::Bold);
        tagFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(tagFont);
        const QFontMetrics tFm(tagFont);
        const int tagW = tFm.horizontalAdvance(chipText) + 2 * kPcTagPadX;
        const int tagH = tFm.height() + 2;
        const int tagX = static_cast<int>(cell.right())
                        - kPcPadX - tagW;
        const int tagY = pnY + (pnH - tagH) / 2;
        // Tag colours per HTML v8 .tag.brk / .tag.sev.
        QColor tagBg, tagFg;
        if (e.tagKind == QLatin1String("brk")) {
            tagBg = QColor(244, 17, 98);    // #f41162
            tagFg = QColor(255, 255, 255);
        } else if (e.tagKind == QLatin1String("sev")) {
            tagBg = QColor(246, 165, 34);   // #f6a522
            tagFg = QColor(0, 0, 0);
        } else {
            tagBg = QColor(64, 64, 64);
            tagFg = QColor(255, 255, 255);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(tagBg);
        p.drawRoundedRect(QRectF(tagX, tagY, tagW, tagH), 1.5, 1.5);
        p.setPen(tagFg);
        p.drawText(QRectF(tagX, tagY, tagW, tagH),
                   Qt::AlignCenter, chipText);
    }

    // .mini: 4px track + #78909c fill, anchored to the bottom padding.
    const int miniY = static_cast<int>(cell.bottom())
                      - kPcPadY - kPcMiniH;
    const QRectF miniRect(cell.x() + kPcPadX, miniY,
                          cell.width() - 2 * kPcPadX, kPcMiniH);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(10, 11, 12));          // #0a0b0c
    p.drawRect(miniRect);
    const float clamped = std::clamp(e.pct, 0.0F, 1.0F);
    if (clamped > 0.001F) {
        // v0.8.4-r23: broken/severed parts paint the fill in the tag
        // palette (brk pink / sev amber) — HunterPie swaps its gauge
        // brush to Broken.Foreground on IsPartBroken/IsPartSevered.
        // This keeps the state visible even when the HP layer itself is
        // unreadable on a non-host client.
        QColor fill = QColor(120, 144, 156); // #78909c default
        if (e.broken) {
            fill = (e.tagKind == QLatin1String("sev"))
                ? QColor(246, 165, 34)   // #f6a522
                : QColor(244, 17, 98);   // #f41162
        }
        p.setBrush(fill);
        p.drawRect(miniRect.x(), miniY,
                   miniRect.width() * clamped, miniRect.height());
    }

    // Position the value above only the tenderize strip. The Flinch row
    // stacks above the value, while pcGaugeExtraH reserves both rows.
    const int tenderizeHeight = pcHasTenderize(e) ? kPcTnExtra : 0;
    QFont valueFont(QStringLiteral("Chakra Petch"), kPcValueFont, QFont::Medium);
    valueFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(valueFont);
    p.setPen(QColor(150, 154, 158));
    const QString layerName = e.valueLayer == PcEntry::Layer::Flinch
        ? QStringLiteral("Flinch") : e.valueLayer == PcEntry::Layer::Sever
        ? QStringLiteral("Sever") : QStringLiteral("Break");
    const int valueY = miniY - kPcValueGap - tenderizeHeight - kPcValueH;
    p.drawText(QRectF(cell.x() + kPcPadX,
                      valueY,
                      cell.width() - 2 * kPcPadX, kPcValueH),
               Qt::AlignRight | Qt::AlignVCenter,
               layerName + QStringLiteral(" ") + e.value);

    // Flinch gauge sits above the value row with kPcGaugeGap; the primary
    // gauge remains anchored at the bottom. Height and paint share the gate.
    if (kPcDrawFlinchRow && pcHasFlinch(e)) {
        const int flTop = valueY - kPcGaugeGap - kPcFlinchH;
        const QRectF flRect(cell.x() + kPcPadX, flTop,
                            cell.width() - 2 * kPcPadX, kPcFlinchH);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(10, 11, 12));       // #0a0b0c, same as .mini
        p.drawRect(flRect);
        // flinchMax > 0 is guaranteed by pcHasFlinch(), so the division
        // is safe; the guard keeps the intent explicit.
        const float flPct = std::clamp(e.flinchPct, 0.0F, 1.0F);
        if (flPct > 0.001F) {
            // HunterPie does NOT swap this gauge's brush on break state —
            // it stays Part.Default.Foreground (blue). Only the SECOND
            // gauge changes colour, so no broken/severed branch here.
            p.setBrush(QColor(kPcFlR, kPcFlG, kPcFlB));   // #4B8EEE
            p.drawRect(flRect.x(), flRect.y(),
                       flRect.width() * flPct, flRect.height());
        }
    }

    // v0.7.4 PR C: per-part tenderize strip. Drawn ABOVE .mini and below
    // .pn (the caller reserves the extra kPcTnLabelH + kPcTnH + kPcTnGap
    // when this card has an active tenderize). Amber palette matches
    // the .sev tag and the deleted standalone .tsc section. Layout
    // (compact variant — S3 follow-up):
    //   .pn row
    //   ↓ kPcTnGap
    //   "Ns" label (right-aligned, kPcTnLabelH tall)
    //   ↓ (no extra gap — label sits visually glued to the bar)
    //   amber fill bar (kPcTnH tall)
    //   ↓ kPcTnGap
    //   .mini row
    if (pcHasTenderize(e)) {
        const QColor amber(246, 165, 34);    // #f6a522
        // Strip + label layout: stack from .mini top going up.
        const int barY = static_cast<int>(miniRect.top())
                         - kPcTnGap - kPcTnH;
        const int labelY = barY - kPcTnLabelH;
        const QRectF tnRect(cell.x() + kPcPadX, barY,
                            cell.width() - 2 * kPcPadX, kPcTnH);
        // "Ns" label — right-aligned, sits IMMEDIATELY above the bar.
        QFont tnFont(QStringLiteral("Chakra Petch"), kPcTagFont, QFont::Bold);
        tnFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(tnFont);
        p.setPen(amber);
        const QFontMetrics tnFm(tnFont);
        const int labelW = tnFm.horizontalAdvance(QStringLiteral("00s")) + 4;
        const int labelH = kPcTnLabelH;
        p.drawText(QRectF(tnRect.right() - labelW, labelY, labelW, labelH),
                   Qt::AlignRight | Qt::AlignVCenter,
                   // v0.7.5: tenderizeDuration already stores REMAINING
                   // seconds (reader converts max-duration); round UP so a
                   // 0.4s tail reads "1s" rather than a sticky "0s".
                   QStringLiteral("%1s").arg(
                       static_cast<int>(std::ceil(e.tenderizeDuration))));
        // Track.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(10, 11, 12));
        p.drawRect(tnRect);
        // Fill.
        // NOTE: this `tenderizeMaxDuration > 0.0F` is a DIVISOR guard, not
        // a visibility gate — the strip above is already gated by
        // pcHasTenderize(e). It stays here on purpose so a remaining
        // duration that ever arrives without its matching total renders
        // an empty track instead of dividing by zero. Do NOT fold it into
        // pcHasTenderize() and do NOT delete it; the visibility decision
        // lives in the single helper, this one only keeps the ratio finite.
        const float tnPct = (e.tenderizeMaxDuration > 0.0F)
            ? std::clamp(e.tenderizeDuration / e.tenderizeMaxDuration,
                         0.0F, 1.0F)
            : 0.0F;
        if (tnPct > 0.001F) {
            p.setBrush(amber);
            p.drawRect(tnRect.x(), tnRect.y(),
                       tnRect.width() * tnPct, tnRect.height());
        }
    }
}

// v0.7.4 PR C: deleted the standalone .tsc section (drawTenderize +
// TzEntry + kTz* constants). Tenderize is now drawn inside each .pc card
// as a small amber strip; see drawPc() above.

} // namespace

// v0.10.3-r6 (B1) — the ONE builder that turns the snapshot's part rows
// into drawable .pc card entries. The three height sites (panel height
// reservation, .pgrid cell layout, drawPc's own row stack) plus anything a
// follow-up task adds must all consume buildPcList()'s output; none of them
// may re-derive a card's contents. Two .pgrid draws with a different
// autohide clock is exactly how the v0.10.3 tenderize defaults drifted.
//
// `panel` is used only for the two paint-time inputs that are genuinely
// panel state and not snapshot data:
//   * editMode() — v0.10.x-r2 PartAutoHide never hides a card in edit mode
//     (mirrors the ailments' `recent = true` default), so the demo /
//     control-panel preview keeps every card pinned.
//   * partTrack_ — the 15 s change-based visibility table (MUTATED here;
//     see the long comment on MonsterPanel::partTrack_ in panel_monster.h).
// Both are read/written exactly as the old inline loop did, so the
// resulting list is byte-for-byte the same set of cards as before the
// extraction — this is a refactor, not a behaviour change.
//
// `onTenderize` is the control-panel 软化/TENDERIZE section bit. It is
// applied HERE, in one place, so the reservation and the render can never
// disagree about whether the strip is on (the v0.10.3 incident).
// Defined at global scope on purpose: MonsterPanel befriends exactly this
// signature (panel_monster.h), and a friend declaration only grants access
// to that ONE entity, so its scope and parameter list must match the
// declaration character for character. Everything it needs from the panel
// (partTrack_, multiplayer_, editMode() via Panel) is handed to it through
// the MonsterPanel& parameter.
QVector<PcEntry> buildPcList(MonsterPanel &panel,
                             const QVector<mhw::PartSnapshot> &shownParts,
                             qint64 nowMs, bool onTenderize)
{
    // v0.10.x-r2 PartAutoHide: same 15 s silence timeout as HunterPie's
    // MonsterPartViewModel (MonsterWidgetConfig.cs:131-139 default
    // `AutoHidePartsDelay = new(15, 300, 1, 1)`). The signature is the
    // eight PartSnapshot fields the player perceives as "the part
    // moved" — HP/maxHP (Severable / Breakable layer), Flinch/maxFlinch
    // (the alternate value-layer HunterPie's Row 3 Conditional selects
    // once a part is severed/broken), tenderizeDuration (the strip's
    // countdown), Counter (+0x18, the player's "破 N" feedback), and
    // the broken/severed flags. tenderizeMaxDuration is the sentinel
    // "slot has authored this part" gate used by the panel's strip draw,
    // not a per-tick value, so it deliberately does NOT participate —
    // including it would pin a card forever as soon as one tick
    // authored the slot (see v0.10.x-r3 merged-stackable patch).
    //
    // Quantize each float field at 0.1 (matches ailments' ailSig) so
    // float jitter doesn't keep the card from settling into the silent
    // state. Counter / bool fields are already integer-stable, no
    // quantize needed.
    constexpr qint64 kPartAutoHideMs = 15000;
    QVector<PcEntry> pcList;
    pcList.reserve(shownParts.size());
    for (const auto &p : shownParts) {
        // sanitizeQ10() folds NaN/Inf to 0 before qRound(). Without it a
        // single non-finite read aborts the process inside paintPanel()
        // (Qt6's qRound asserts on NaN) — see the helper comment above.
        const quint32 hpQ = sanitizeQ10(p.health);
        const quint32 mhQ = sanitizeQ10(p.maxHealth);
        const quint32 flQ = sanitizeQ10(p.flinch);
        const quint32 mfQ = sanitizeQ10(p.maxFlinch);
        const quint32 tdQ = sanitizeQ10(p.tenderizeDuration);
        const quint32 ctQ = static_cast<quint32>(p.counter);
        const quint32 brQ = p.isBroken       ? 1u : 0u;
        const quint32 svQ = p.isPartSevered  ? 1u : 0u;
        // Accumulate (do NOT XOR-fold). The previous fold
        //   (a<<32)^b ^ (c<<32)^d ^ ...
        // let two IDENTICAL pairs cancel: on a World normal part the reader
        // sets flinch/maxFlinch == health/maxHealth (monster_reader.cpp:746-753),
        // so the (hp,mh) and (fl,mf) terms were equal and cancelled to 0 —
        // and 0 is also the zero-initialized PartTrack default. That made
        // `partSig == 0` for 25 % of the World breakable grid whenever
        // Counter == 0, which both suppressed the card on its first paint and
        // let a *moving* bar hide. A multiply-accumulate fold cannot cancel.
        quint64 partSig = 0x9E3779B97F4A7C15ULL;
        auto mix = [&partSig](quint64 v) {
            partSig = (partSig ^ v) * 0x100000001B3ULL;
        };
        mix((static_cast<quint64>(hpQ) << 32) | mhQ);
        mix((static_cast<quint64>(flQ) << 32) | mfQ);
        mix((static_cast<quint64>(tdQ) << 32) | ctQ);
        mix((static_cast<quint64>(brQ) << 32) | svQ);
        // A real state can still hash to 0 by coincidence; force the low bit
        // so a genuine signature is never confusable with the zero-init slot.
        partSig |= 1ULL;
        // edit-mode demo: never auto-hide (mirrors ailments' `recent = true`
        // default). Non-host multiplayer can still proceed — the non-host
        // override below ("--/--" when HP is stale) is purely a
        // value-masking concern and is unrelated to the silence filter, so
        // the card falls out cleanly when the player's actual game-clock
        // signal goes quiet.
        bool recent = true;
        // v0.10.x-r2 fix: the guard used the RAW PartSnapshot::index, but the
        // three readers assign it on three different scales:
        //   World severable  1000 + s        (monster_reader.cpp:680)  >= 1000
        //   World normal     -1 - normalSlot (monster_reader.cpp:743)  < 0
        //   Rise             i               (mhr_reader.cpp:419)      0..15
        // Only the Rise scale lands inside [0, partTrack_.size()), so
        // AutoHide silently did nothing on World — the whole grid stayed
        // pinned forever. Normalize to a dense, stable slot key instead of
        // changing the reader's index semantics (that field is also used for
        // the "部位 N" fallback label and is intentionally signed to encode
        // which table a part came from).
        //
        // Key = (100 + schema row) for severable, (1000 + normal slot) for
        // normal, i for Rise. The three regions are disjoint:
        //   severable [100, 131)   Rise [0, 64)   normal [1000, 2024)
        // so no key can alias another even if a panel ever saw parts from
        // two games (it cannot — main.cpp:316 fixes the reader at startup).
        // A key outside [0, 2048) falls out of the guard below and leaves
        // `recent == true`, i.e. it degrades to "always visible", which is
        // the safe failure mode.
        const int slot = (p.index >= 1000)
            ? 100 + (p.index - 1000)       // World severable
            : (p.index < 0 ? 1000 - p.index // World normal: -1-n -> 1000+n
                           : p.index);     // Rise: already dense
        if (!panel.editMode() && slot >= 0
                       && slot < static_cast<int>(panel.partTrack_.size())) {
            MonsterPanel::PartTrack &track = panel.partTrack_[slot];
            if (track.sig != partSig) {
                track.sig     = partSig;
                track.stampMs = nowMs;
                recent = true;
            } else {
                recent = track.stampMs > 0
                      && (nowMs - track.stampMs) < kPartAutoHideMs;
            }
        }
        if (!recent)
            continue;

        PcEntry e;
        e.name = p.name.isEmpty()
            ? mh::tr("ui.monster_part_fallback").arg(p.index)
            : p.name;
        e.counter = p.counter;
        e.broken = p.isBroken;
        e.severed = p.isPartSevered;
        switch (p.partType) {
        case mhw::PartType::Severable:
            e.tag = mh::tr("ui.monster_tag_sever");
            e.tagKind = QStringLiteral("sev");
            break;
        case mhw::PartType::Breakable:
            e.tag = mh::tr("ui.monster_tag_break");
            e.tagKind = QStringLiteral("brk");
            break;
        case mhw::PartType::Flinch:
            break;
        }
        // v0.7.4 PR C: per-part tenderize values feed the new strip
        // drawn inside each .pc card. The struct fields are 0 by default
        // (no active tenderize), so we only need to copy when nonzero.
        // v0.8.4-r23: gate on the 软化 section bit so the control-panel
        // toggle actually hides the strip (and the reservation stays in
        // lockstep with the drawn cards) — it is applied HERE, once, so
        // both heights see the identical condition.
        e.tenderizeDuration    = onTenderize ? p.tenderizeDuration : 0.0F;
        e.tenderizeMaxDuration = onTenderize ? p.tenderizeMaxDuration : 0.0F;
        // Row 1 hard-stagger gauge data.
        // Exactly the p.flinch / p.maxFlinch layer, un-decimated: the
        // caller (pcGaugeExtraH) needs the denominator to tell "no flinch
        // layer" (World body parts, 0/0) from "flinch layer, 0 % left".
        // v0.10.3-r6 (B1): breakPct/breakMax — Rise Severable parts carry
        // a break layer alongside the sever layer (PartSnapshot::
        // breakHealth / breakMaxHealth, commit 3584e32). A nonzero
        // breakMaxHealth is exactly "this Severable part also has a
        // breakable body"; 0 means no break layer, which happens for
        // World parts, Breakable parts (health/maxHealth already IS the
        // break layer) and Flinch parts (no break layer at all).
        // Sanitized copies of the four layer fields the gate / clamp /
        // divide path reads below. NaN compares false against zero, but
        // still must not reach std::clamp, division, or Qt's qRound.
        const float sFlinchMax = sanitizePartValue(p.maxFlinch);
        if (sFlinchMax > 0.0F) {
            e.flinchPct = std::clamp(sanitizePartValue(p.flinch)
                                     / sFlinchMax, 0.0F, 1.0F);
            e.flinchMax = sFlinchMax;
        }
        const float sBreakMax = sanitizePartValue(p.breakMaxHealth);
        if (sBreakMax > 0.0F) {
            e.breakPct = std::clamp(sanitizePartValue(p.breakHealth)
                                     / sBreakMax, 0.0F, 1.0F);
            e.breakMax = sBreakMax;
        }
        // v0.7.4 PR C: pick the right HP pair per PartType.
        //   - Severable: health/maxHealth carries Sever; World leaves
        //     Flinch untouched while Rise updates it each tick.
        //   - Breakable: Health/MaxHealth is the cumulative threshold
        //     progress (UpdateBreakableData); Flinch/MaxFlinch is the
        //     current layer's raw value (less useful on the main bar).
        //   - Flinch:    only Flinch/MaxFlinch is meaningful (no
        //     thresholds, not severable). This is the path that fixes
        //     the "脏数据" complaint — body/leg parts now show real
        //     flinch bar values instead of the broken double-filled
        //     health/flinch pair.
        const PartDisplayLayers layers = partDisplayLayers(panel.monster_.game, p);
        const mhw::PartHealthPair hp = layers.primary;
        // v0.10.x-r3 UI-template alignment: Row 3 Conditional
        // (HunterPie MonsterPartTemplateSelector + BossMonsterSeverablePartView.xaml:101-134,
        //  BossMonsterBreakablePartView.xaml:124-157). The displayed value flips
        // from the primary layer (Sever / Health) to Flinch/MaxFlinch once the
        // part is severed or broken. Mapping:
        //   Severable + severed  → "Flinch/MaxFlinch"
        //   Severable + !severed → "Sever/MaxSever"   (= health/maxHealth)
        //   Breakable + broken   → "Flinch/MaxFlinch"
        //   Breakable + !broken  → "Health/MaxHealth" (= health/maxHealth)
        //   Flinch               → "Flinch/MaxFlinch"
        // The multiplayer non-host override below still takes precedence
        // (it forces "—" when the layer pair is stale), so this never
        // masks a frozen non-host value.
        // Sanitized on the way in: hp.current/maximum and the flinch pair
        // are all live memory reads, so any of them can be NaN/Inf on a
        // slot that is mid-teardown. compactPartHealth() and the
        // staleFullHp / flinchLive comparisons below would all happily
        // propagate NaN into the displayed value and the bar width.
        const float mHP = sanitizePartValue(hp.maximum);
        const float cHP = sanitizePartValue(hp.current);
        e.primaryLayer = layers.primaryKind;
        e.valueLayer = e.primaryLayer;
        const bool switchToFlinch =
            (p.partType == mhw::PartType::Severable && e.severed)
         || (p.partType == mhw::PartType::Breakable && e.broken);
        // HunterPie changes only the value row after sever/break; the
        // Sever/Health gauge continues to bind its original source.
        if (switchToFlinch) e.valueLayer = PcEntry::Layer::Flinch;
        e.value = e.valueLayer == PcEntry::Layer::Flinch
            ? mhw::compactPartHealth(sanitizePartValue(layers.flinch.current),
                                     sanitizePartValue(layers.flinch.maximum))
            : mhw::compactPartHealth(cHP, mHP);
        e.pct = mHP > 0.0F ? std::clamp(cHP / mHP, 0.0F, 1.0F) : 0.0F;
        if (panel.multiplayer_) {
            // v0.8.4-r23 non-host readability: on a non-host client the
            // Health/MaxHealth layer pair is not replicated by the game
            // (mhw-parts-hp-frozen-on-client-2026-07-23) — it stays at
            // its last authoritative value, usually full, even while
            // teammates break the part. Never draw a stuck-at-full pair
            // as if it were live HP: a broken part would show a 90-100%
            // bar and an untouched one "--/--" with the chip stripped
            // (the old gate also cleared tag/tagKind, which is why the
            // grid "几乎不显示" in multiplayer).
            //
            // The signals that ARE live on a client (and that HunterPie
            // keeps rendering in the same situation):
            //   * Flinch/MaxFlinch — locally simulated stagger layer.
            //     Once it has moved it can carry the value row; the
            //     primary gauge keeps its own source binding.
            //   * Counter (+0x18) and the broken/severed state — break
            //     events are replicated; the tag chip keeps them
            //     visible unconditionally.
            const bool staleFullHp = mHP > 0.0F && cHP >= mHP;
            if (staleFullHp) {
                // sFlinchMax is already sanitized; sanitize the current
                // half too so a NaN flinch can't fake "still live".
                const float sFlinch = sanitizePartValue(p.flinch);
                const bool flinchLive =
                    sFlinchMax > 0.0F && sFlinch + 1.0e-4F < sFlinchMax;
                if (flinchLive) {
                    e.value = mhw::compactPartHealth(sFlinch, sFlinchMax);
                    e.valueLayer = PcEntry::Layer::Flinch;
                } else {
                    e.value = QStringLiteral("--/--");
                }
                // NOTE: the tag chip is deliberately NOT cleared here —
                // the counter/state is exactly the readable signal for
                // non-host members.
            }
        }
        pcList.append(e);
    }
    return pcList;
}

MonsterPanel::MonsterPanel(QWidget *parent)
    : Panel(QStringLiteral("monster"), Corner::TopRight, parent)
{
    setWindowTitle(mh::tr("ui.monster_title"));

    pulseTimer_.setInterval(80);
    connect(&pulseTimer_, &QTimer::timeout, this, &MonsterPanel::onEnragePulseTick);
}

void MonsterPanel::retranslateUi()
{
    // Only cached strings need re-querying: the title (ctor) and the demo
    // seed labels (monster name / ailments / part names), rebuilt on the
    // next paint via resetDemoPrimed(). Live data is untouched — the
    // reader refreshes it, and paint-time tr() follows the locale.
    setWindowTitle(mh::tr("ui.monster_title"));
    if (editMode())
        resetDemoPrimed();
    triggerUpdate();
}

void MonsterPanel::update(const mhw::MonsterSnapshot &m)
{
    // v0.10.x-r2 fix (B1): the AutoHide table is keyed by a part slot, not
    // by a monster. When the player switches target the whole snapshot is
    // replaced, and a stale (sig, stampMs) pair from the previous target
    // would make the new monster's parts look already-silent (hidden on
    // their first paint) or already-fresh (pinned for a spurious 15 s).
    // Clear it on the identity change. `address` is refreshed on every tick
    // (monster_reader.cpp:854 / mhr_reader.cpp:667) and is the same key the
    // panel already prints in its meta row, so this fires exactly once per
    // target switch. Note this deliberately does NOT clear `ailTrack_`:
    // ailment cards are gated on a live timer and HunterPie keeps the same
    // per-cell ViewModel lifetime, so the ailment residue is invisible.
    if (m.address != monster_.address)
        partTrack_ = {};
    monster_ = m;
    hasData_ = (m.id >= 0);

    const bool enraged = m.enraged && m.enrageSeconds > 0.0F;
    if (enraged) {
        if (!pulseTimer_.isActive()) {
            phaseClock_.restart();
            pulseTimer_.start();
        }
    } else {
        if (pulseTimer_.isActive())
            pulseTimer_.stop();
    }

    canvas()->update();
}

void MonsterPanel::onEnragePulseTick()
{
    const qint64 elapsed = phaseClock_.elapsed();
    enragePhase_ = static_cast<double>(elapsed % kPulsePeriodMs) / kPulsePeriodMs;
    canvas()->update();
}

void MonsterPanel::paintPanel(QPainter &p)
{
    // v0.8.x + symptom7 fix: empty-state placeholder.
    //
    // drawV03Chrome reads logicalSize_ for its rect (panel.cpp:545),
    // so setContentSize() MUST run before drawV03Chrome. The v0.8.x
    // patch moved drawV03Chrome to the top of paintPanel but did NOT
    // add a setContentSize() call on the empty-state branch.
    // logicalSize_ stayed at its default QSize() = 0×0, so the rounded
    // background drew at zero size and was clipped away — leaving
    // only the placeholder text visible against the layer-shell
    // surface. Symptom7: "任务内怪物面板只显示 '等待进入任务' 连背景
    // 半透明框都没有".
    //
    // UX symmetry with the damage Rise empty state (panel_damage.cpp
    // paintPanel): title row + kRowGap + message block, padded top
    // and bottom with kPanelPad. Same kPanelWidth so the corner
    // anchor doesn't jump when quest data arrives.
    if (!hasData_) {
        constexpr int kPlaceholderH = 28;
        const int totalH = kPanelPad + kTitleH + kRowGap + kPlaceholderH + kPanelPad;
        setContentSize(kPanelWidth, totalH);

        drawV03Chrome(p, Panel::Accent::Monster);

        // Title row — same "怪物 MONSTER" treatment as the real panel
        // (panel.h:108 vs damage empty state at panel_damage.cpp:539)
        // so the placeholder is recognisably the same panel.
        QFont hdrFont(QStringLiteral("Chakra Petch"), 9, QFont::Bold);
        hdrFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
        hdrFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(hdrFont);
        p.setPen(QColor(245, 246, 247));
        const QRectF titleRect(kPanelPad, kPanelPad,
                               kPanelWidth - 2 * kPanelPad, kTitleH);
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                   mh::tr("ui.monster_header"));

        // Centered message in logical coordinates. canvas()->rect()
        // returns DEVICE pixels and the scaled painter (paintEvent
        // applies p.scale(scale_, scale_) before paintPanel runs) would
        // map a 320×120 device rect to 640×240 logical, well outside
        // the surface. Logical rect avoids that entirely.
        QFont msgFont(QStringLiteral("Chakra Petch"), 10);
        msgFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(msgFont);
        p.setPen(QColor(150, 150, 150));
        const QRectF msgRect(kPanelPad, kPanelPad + kTitleH + kRowGap,
                             kPanelWidth - 2 * kPanelPad, kPlaceholderH);
        p.drawText(msgRect, Qt::AlignCenter,
                   mh::tr("ui.monster_waiting_quest"));
        return;
    }

    // ---- Build status card entries from ailments (mirrors .sc ordering) ----
    QVector<ScEntry> scList;
    // v0.8.4-r23: HunterPie's card visibility is *change*-based, not
    // value-based. MonsterAilmentViewModel inherits AutoVisibilityViewModel:
    // every Timer / BuildUp / IsTimerActive change restarts a per-cell timer
    // (AutoVisibilityViewModel.cs:61-68) and the cell hides once that timer
    // elapses with no further change; MonsterWidgetConfig.AutoHideAilmentsDelay
    // defaults to 15 s (MonsterWidgetConfig.cs:153 `new(15, 300, 1, 1)`).
    // The old "buildup > 0" gate pinned a card for as long as the game kept
    // a stale build-up value — World build-up decays slowly once nothing is
    // being applied — which is what turned the section into a wall of
    // frozen cards. Edit mode keeps the demo cards pinned so the control
    // console preview and screenshots still show every card.
    constexpr qint64 kAilAutoHideMs = 15000;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (const auto &a : monster_.ailments) {
        // HunterPie ignores build-up without a known threshold
        // (MonsterAilmentContextHandler.cs:89-96 `if (e.MaxBuildUp <= 0) return;`),
        // so a (buildup > 0, maxBuildup <= 0) pair is not renderable
        // progress — it used to draw a blank card (pct 0, empty text).
        const bool timerRunning = a.timer > 0.0F;
        const bool buildupKnown = a.buildup > 0.0F && a.maxBuildup > 0.0F;

        // Quantized (timer, buildup) signature at 0.1 resolution: coarse
        // enough to ignore float noise, fine enough to catch the changes
        // HunterPie reacts to. Counter changes deliberately do NOT refresh
        // the card — HunterPie's OnCounterUpdate is a plain SetValue
        // (MonsterAilmentContextHandler.cs:87), so a historical counter
        // alone can never pin a card.
        const quint64 ailSig =
            (static_cast<quint64>(static_cast<quint32>(qRound(a.timer * 10.0F))) << 32)
            | static_cast<quint32>(qRound(a.buildup * 10.0F));
        bool recent = true;   // edit-mode demo: never auto-hide
        if (!editMode() && a.id >= 0 && a.id < static_cast<int>(ailTrack_.size())) {
            AilTrack &track = ailTrack_[a.id];
            if (track.sig != ailSig) {
                track.sig = ailSig;
                track.stampMs = nowMs;
            }
            recent = track.stampMs > 0 && (nowMs - track.stampMs) < kAilAutoHideMs;
        }
        if (!timerRunning && !recent)
            continue;
        ScEntry e;
        e.name = a.name;
        // --sc + icon path are looked up by ailment id; trap items keep
        // their distinctive yellow/green from HunterPie. World and Rise
        // use UNRELATED id tables, so branch on the snapshot's game.
        if (monster_.game == mhw::GameId::Rise) {
            switch (a.id) {
            case  0: e.sc = QColor(246, 165,  34); break; // 麻痹
            case  1: e.sc = QColor(140, 195,  74); break; // 睡眠
            case  2: e.sc = QColor(255, 193,   7); break; // 眩晕
            case  3: e.sc = QColor(255, 255, 255); break; // 闪光
            case  4: e.sc = QColor(167,  79, 255); break; // 毒
            case  5: e.sc = QColor(255,  87,  34); break; // 爆破
            case  7: e.sc = QColor( 76, 175,  80); break; // 乘骑
            case 12: e.sc = QColor(139, 195,  74);
                     e.iconPath = QStringLiteral(":/icons/Traps/item_id_80.svg"); break; // 落穴
            case 13: e.sc = QColor(255, 235,  59);
                     e.iconPath = QStringLiteral(":/icons/Traps/item_id_81.svg"); break; // 麻痹陷阱
            default: e.sc = QColor(158, 158, 158); break;
            }
        } else switch (a.id) {
        case  1: e.sc = QColor(167,  79, 255); e.iconPath = QString(); break; // 毒
        case  2: e.sc = QColor(246, 165,  34); e.iconPath = QString(); break; // 麻痹
        case  3: e.sc = QColor(140, 195,  74); e.iconPath = QString(); break; // 睡眠
        case  4: e.sc = QColor(255,  87,  34); e.iconPath = QString(); break; // 爆破
        case  5: e.sc = QColor( 76, 175,  80); e.iconPath = QString(); break; // 乘骑
        case 14: e.sc = QColor(255, 235,  59);
                 e.iconPath = QStringLiteral(":/icons/Traps/item_id_81.svg"); break; // 麻痹陷阱
        case 15: e.sc = QColor(139, 195,  74);
                 e.iconPath = QStringLiteral(":/icons/Traps/item_id_80.svg"); break; // 落穴
        case 16: e.sc = QColor(102, 187, 106); e.iconPath = QString();          break; // 藤蔓陷阱
        default: e.sc = QColor(158, 158, 158); e.iconPath = QString();          break;
        }
        // The card is "active" the moment the monster is being affected:
        //   - the ailment has triggered (timer counting down), or
        //   - the build-up is in progress (mini bar moving).
        e.active = timerRunning || buildupKnown;

        // Mini progress + timer text. Order matches the HTML examples:
        //   active + maxTimer     → "Ns" (countdown)
        //   else + maxBuildup     → "N%" (buildup fill)
        if (timerRunning && a.maxTimer > 0.0F) {
            // HunterPie exposes the remaining seconds directly; round UP
            // so a 0.4s tail still shows "1s" instead of a sticky "0s"
            // (the card disappears the moment timer hits 0 anyway).
            e.pct = std::clamp(a.timer / a.maxTimer, 0.0F, 1.0F);
            e.tm  = QStringLiteral("%1s").arg(static_cast<int>(std::ceil(a.timer)));
        } else if (buildupKnown) {
            e.pct = std::clamp(a.buildup / a.maxBuildup, 0.0F, 1.0F);
            e.tm  = QStringLiteral("%1%").arg(static_cast<int>(e.pct * 100));
        } else {
            e.pct = 0.0F;
            e.tm  = QString();
        }
        scList.append(e);
    }
    const int scCount = scList.size();

    // ---- Section mask (ui/panel_sections.h) ----
    const uint32_t smask = sectionMask();
    const bool onInfo     = smask & mhw::MonsterSection::Info;
    const bool onHp       = smask & mhw::MonsterSection::Hp;
    const bool onEnrage   = smask & mhw::MonsterSection::Enrage;
    const bool onAil      = smask & mhw::MonsterSection::Ail;
    const bool onParts    = smask & mhw::MonsterSection::Parts;
    // v0.8.4-r23: the 软化/TENDERIZE control-panel toggle gates the
    // per-part tenderize strip again. v0.7.4 PR C dropped the standalone
    // .tsc section and left `onTenderize` computed-but-unused, which
    // turned the control-panel switch into a silent no-op. The strip is
    // data-driven AND mask-gated: the height reservation (below) and the
    // pcList fill must use the same condition or the panel height and
    // the drawn cards drift apart.
    const bool onTenderize = smask & mhw::MonsterSection::Tenderize;

    // v0.8.4-r23 fatigue-semantics: monster stamina row. The reader only
    // fills the pair when the 0x320 chain resolved (0/0 = unknown → row
    // hidden). Follows the HP toggle until a dedicated section bit exists.
    const bool stamDrawn = onHp && monster_.maxStamina > 0.0F;

    const bool showRage = monster_.enrageMaxBuildup > 0.0F || monster_.enraged;
    const bool rageDrawn = onEnrage && showRage;

    // Block-table height (see panel_sections.h MonsterSection). gapBefore
    // model: the original mixed post-gaps (title/hex/hp/rage) and pre-gaps
    // (ail/parts); we fold each post-gap into the next block's pre-gap so
    // disabling a block drops its spacing cleanly. rage's conditional
    // post-gap is folded into ail's conditional pre-gap (the +rageDrawn
    // term) so the fully-on rage→ail 2-row gap is preserved bit-exact.
    constexpr int kInfoH = kTitleH + kRowGap + kHexH;
    const int hpH        = kBarH + 2 * kBarVPad;
    const int stamH      = kStamBarH + 2 * kBarVPad;
    const int rageH      = kRageBarH + 2 * kBarVPad;
    int scAreaH = 0;
    if (scCount > 0) {
        const int scRows = (scCount + kScCols - 1) / kScCols;
        constexpr int kScCellH = kScPadY + kScTopFont + 4 + kScMiniH + kScPadY;
        scAreaH = scRows * kScCellH + (scRows - 1) * kScGap;
    }
    // v0.8.4-r18 parts-display-filter: resolve the parts the .pgrid will
    // actually render (severable/breakable only, with a first-4 fallback
    // when a monster has nothing but flinch bars). Resolved ONCE, before
    // the height reservation, so the reservation below and the .pgrid
    // render further down consume the exact same list — any divergence
    // would misalign rows and clip the panel.
    const QVector<mhw::PartSnapshot> shownParts =
        displayableParts(monster_.parts);

    // v0.10.3-r6 (B1): ONE buildPcList() call per paint, cached here and
    // reused by BOTH the height reservation below and the .pgrid render
    // further down.
    //
    // Before this refactor the reservation iterated raw shownParts[] while
    // the render iterated the AutoHide-filtered pcList, so the two had a
    // latent mismatch: a part suppressed by HunterPie's 15 s silence filter
    // still had its cell height reserved (harmless, because it only
    // over-reserved). Both now read the identical list AND the identical
    // heights, so there is nothing left that can disagree.
    //
    // It is called exactly ONCE rather than once per site, because
    // buildPcList() MUTATES partTrack_: a second call inside the same paint
    // would see each slot's sig already refreshed and would compute a
    // different `recent` (and potentially a different list) than the caller
    // that runs first. One call, one list, cached in pcList.
    const QVector<PcEntry> pcList = buildPcList(
        *this, shownParts, nowMs, onTenderize);
    const int pcCount = pcList.size();

    // v0.7.4 PR C: cell height is dynamic — any part with an active
    // Clutch Claw tenderize (PartSnapshot.tenderizeDuration > 0) inserts a
    // kPcTnH+kPcTnGap strip between .pn and .mini. We compute per-cell
    // heights up front so the .pgrid rows stay aligned with the tallest
    // cell in each row. pcGaugeExtraH() is the single source of truth for
    // that optional-row height, and the .pgrid loop below calls the same
    // function on the same list — so reserved height == drawn height,
    // always.
    int pcAreaH = 0;
    if (pcCount > 0) {
        QVector<int> cellHeights(pcCount, kPcBaseCellH);
        for (int i = 0; i < pcCount; ++i)
            cellHeights[i] += pcGaugeExtraH(pcList[i]);
        // Per-row max height → row height. Sum of (rowHeights + gaps).
        const int pcRows = (pcCount + kPcCols - 1) / kPcCols;
        int sum = 0;
        for (int r = 0; r < pcRows; ++r) {
            int rowMax = 0;
            const int start = r * kPcCols;
            const int end   = std::min(start + kPcCols, pcCount);
            for (int c = start; c < end; ++c)
                rowMax = std::max(rowMax, cellHeights[c]);
            sum += rowMax;
        }
        pcAreaH = sum + (pcRows - 1) * kPcGap;
    }

    int totalH = kPanelPad;
    if (onInfo)                  totalH += kInfoH;
    if (onHp)                    totalH += kRowGap + hpH;
    if (stamDrawn)               totalH += kRowGap + stamH;
    if (rageDrawn)               totalH += kRowGap + rageH;
    if (onAil && scCount > 0)    totalH += kRowGap + (rageDrawn ? kRowGap : 0) + scAreaH;
    // v0.7.4 PR C: per-part tenderize strip is rendered inside each
    // .pc card; the cell height for it is reserved in pcAreaH above.
    // No separate totalH delta is needed here.
    if (onParts && pcCount > 0)  totalH += kRowGap + pcAreaH;
    totalH += kPanelPad;
    setContentSize(kPanelWidth, totalH);

    // v0.8.4-r18 panel-bg fix: the full-data path lost its chrome call in
    // the v0.8.x refactor (drawV03Chrome only survived in the empty-state
    // branch above), so a panel that actually had a monster rendered no
    // dark #0c0e10 background / accent stripe — "预览状态下丢失深色背景".
    // Order matters: setContentSize() must run first because
    // Panel::drawV03Chrome derives its rect from logicalSize_ (the
    // logicalSize_.width() read inside drawV03Chrome, src/ui/panel.cpp),
    // and the chrome must be painted before the content rows so rows land
    // on top of it.
    drawV03Chrome(p, Panel::Accent::Monster);

    const int innerLeft  = kPanelPad;
    const int innerRight = kPanelWidth - kPanelPad;
    const int innerW     = innerRight - innerLeft;
    int y = kPanelPad;

    // ---- 1+2. Info block (ptitle + hexwrap) ----
    if (onInfo) {
    {
        QFont tFont(QStringLiteral("Chakra Petch"), 9, QFont::Bold);
        tFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
        tFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(tFont);
        p.setPen(QColor(245, 246, 247));
        const QRectF titleRect(innerLeft, y, innerW, kTitleH);
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                   mh::tr("ui.monster_header"));
        // right <i> 8px --t3
        QFont iFont(QStringLiteral("Chakra Petch"), 8);
        iFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        iFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(iFont);
        p.setPen(QColor(110, 112, 114));
        const QString meta = QStringLiteral("em\\em%1 \u00b7 %2")
            .arg(monster_.id, 3, 10, QChar('0'))
            .arg(static_cast<qulonglong>(monster_.address), 0, 16);
        p.drawText(titleRect, Qt::AlignRight | Qt::AlignVCenter, meta);
        y += kTitleH + kRowGap;
    }

    // ---- 2. .hexwrap ----
    const QRectF hexCell(innerLeft, y, kHexW, kHexH);
    drawHex(p, hexCell, mhw::Icon::monsterPath(monster_.id, monster_.game));

    // .mtitle sits in the right column of the flex row.
    constexpr int kNmFont = 15;
    QFont nmFont(QStringLiteral("Chakra Petch"), kNmFont, QFont::Bold);
    nmFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    nmFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(nmFont);
    p.setPen(QColor(245, 246, 247));
    // Rise MonsterData.xml contains no display-name field or localization key.
    // Keep the established runtime-name / ID fallback instead of inventing one.
    const QString nm = monster_.internalName.isEmpty()
        ? QStringLiteral("Monster %1").arg(monster_.id)
        : monster_.internalName;
    const int nmY = y + 4;
    const int nmH = kNmFont + 2;
    int nmX = static_cast<int>(hexCell.right()) + 10;

    // .crownmini (15×15). Use only explicit metadata for the active game.
    // Missing Rise <Crowns> data (and missing individual thresholds) disables
    // the crown rather than borrowing a World table/default.
    const auto *thr = mhw::crownThresholdsFor(monster_.game, monster_.id);
    if (monster_.size > 0.0F && thr) {
        QString crownPath;
        QColor crownTint;
        if (thr->at(2) > 0.0F && monster_.size >= thr->at(2)) {
            crownPath = QStringLiteral(":/icons/crowns/crown_king.svg");
            crownTint = QColor(231, 197, 7);
        } else if (thr->at(1) > 0.0F && monster_.size >= thr->at(1)) {
            crownPath = QStringLiteral(":/icons/crowns/crown_large.svg");
            crownTint = QColor(189, 189, 189);
        } else if (thr->at(0) > 0.0F && monster_.size <= thr->at(0)) {
            crownPath = QStringLiteral(":/icons/crowns/crown_mini.svg");
            crownTint = QColor(33, 150, 243);
        }
        if (!crownPath.isEmpty()) {
            const QPixmap crown = mhw::Icon::render(crownPath, kCrownSize, crownTint);
            if (!crown.isNull()) {
                p.drawPixmap(nmX, nmY, crown);
                nmX += kCrownSize + 7;
            }
        }
    }

    // .nm
    p.drawText(QRectF(nmX, nmY, innerRight - nmX, nmH),
               Qt::AlignLeft | Qt::AlignVCenter, nm);

    // .ids — 9px --t3, right-aligned within the mname-row.
    QFont idsFont(QStringLiteral("Chakra Petch"), 9);
    idsFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    idsFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(idsFont);
    p.setPen(QColor(146, 148, 149));
    const QString ids = QStringLiteral("[ID %1]").arg(monster_.id);
    const int idsW = QFontMetrics(idsFont).horizontalAdvance(ids);
    p.drawText(QRectF(innerRight - idsW, nmY, idsW, nmH),
               Qt::AlignLeft | Qt::AlignVCenter, ids);

    // .szchip + .enrage-tag on the second row of mname-row.
    const int r2Y = nmY + nmH + 4;
    int r2X = static_cast<int>(hexCell.right()) + 10;
    if (monster_.size > 0.0F) {
        QFont szFont(QStringLiteral("Chakra Petch"), 9);
        szFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        szFont.setStyleStrategy(QFont::PreferAntialias);
        const QString szTxt = mhw::sizeMultiplier(monster_.size);
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
    // text-shadow rgba(255,112,67,.55) but kept subtle in Qt (0.30)
    // so it reads as a gentle glow, not a heavy bloom.
    if (monster_.enraged && monster_.enrageSeconds > 0.0F) {
        const int elapsed = static_cast<int>(
            monster_.enrageMaxSeconds - monster_.enrageSeconds);
        const QString enTxt = mh::tr("ui.monster_enraged_secs").arg(elapsed);
        QFont eFont(QStringLiteral("Chakra Petch"), 9, QFont::Bold);
        eFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        eFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(eFont);
        const QRectF eRect(innerRight - 110, r2Y, 110, 14);
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
    } else if (monster_.enrageMaxBuildup > 0.0F) {
        const float pct = std::clamp(
            monster_.enrageBuildup / monster_.enrageMaxBuildup,
            0.0F, 1.0F);
        const QString enTxt = mh::tr("ui.monster_enrage_pct")
            .arg(static_cast<int>(pct * 100));
        QFont eFont(QStringLiteral("Chakra Petch"), 9, QFont::Bold);
        eFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        eFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(eFont);
        p.setPen(QColor(kGoldR, kGoldG, kGoldB));
        const QRectF eRect(innerRight - 110, r2Y, 110, 14);
        p.drawText(eRect, Qt::AlignRight | Qt::AlignVCenter, enTxt);
    }
    y += kHexH;
    } // end Info

    // ---- 3. .bar.hp ----
    if (onHp) {
        y += kRowGap;   // gap before HP (was hexwrap's trailing gap)
    const float hpPct = (monster_.maxHealth > 0.0F)
        ? monster_.health / monster_.maxHealth : 0.0F;
    const QColor hpC = healthColor(hpPct, monster_.game, monster_.id);
    const QRectF hpBarRect(innerLeft, y, innerW, kBarH + 2 * kBarVPad);
    drawBarV(p, hpBarRect, hpPct, hpC.lighter(115), hpC);
    {
        QFont hpFont(QStringLiteral("Chakra Petch"), 11, QFont::Medium);
        hpFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(hpFont);
        p.setPen(QColor(245, 246, 247));
        // Text band sits inside the bar with kBarVPad top/bottom so the
        // 11px glyph doesn't touch the bar edges.
        const QRectF hpTxtRect(innerLeft, y + kBarVPad,
                               innerW, kBarH);
        p.drawText(hpTxtRect.adjusted(8, 0, -8, 0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1 / %2")
                       .arg(mhw::groupNumber(static_cast<int>(monster_.health)))
                       .arg(mhw::groupNumber(static_cast<int>(monster_.maxHealth))));
        p.drawText(hpTxtRect.adjusted(8, 0, -8, 0),
                            Qt::AlignRight | Qt::AlignVCenter,
                            mhw::percentage(monster_.health, monster_.maxHealth));
            }
            y += kBarH + 2 * kBarVPad;
    } // end Hp

    // ---- 3b. .bar.st (monster stamina / fatigue meter) ----
    // v0.8.4-r23 fatigue-semantics: the reader's stamina pair is the real
    // fatigue meter (HunterPie draws this gauge 10px tall under the HP
    // bar). The drooling / exhausted state the player sees in-game is
    // stamina == 0; the exhaust ailment card (slot 6) is a separate 減気
    // gauge and never reaches 100 % at that moment.
    if (stamDrawn) {
        y += kRowGap;
        const float stPct = std::clamp(
            monster_.stamina / monster_.maxStamina, 0.0F, 1.0F);
        const QColor stHi(kStamR, kStamG, kStamB);
        const QRectF stBarRect(innerLeft, y, innerW,
                               kStamBarH + 2 * kBarVPad);
        drawBarV(p, stBarRect, stPct, stHi.lighter(115), stHi);
        QFont stFont(QStringLiteral("Chakra Petch"), 8);
        stFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(stFont);
        const QRectF stRect(innerLeft, y + kBarVPad, innerW, kStamBarH);
        p.setPen(QColor(245, 246, 247));
        p.drawText(stRect.adjusted(8, 0, 0, 0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   mh::tr("ui.monster_stamina"));
        p.setPen(QColor(245, 246, 247));
        p.drawText(stRect.adjusted(0, 0, -8, 0),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1 / %2")
                       .arg(mhw::groupNumber(
                                static_cast<int>(monster_.stamina)))
                       .arg(mhw::groupNumber(
                                static_cast<int>(monster_.maxStamina))));
        y += kStamBarH + 2 * kBarVPad;
    }

    // ---- 4. .bar.er (enrage meter) ----
    // Compact: no left label, just the value at the right. The HP bar
    // already names the fight so this strip stays out of the way.
    if (rageDrawn) {
        y += kRowGap;   // gap before enrage (was HP's trailing gap)
        const float ragePct = (monster_.enrageMaxBuildup > 0.0F)
            ? std::clamp(monster_.enrageBuildup / monster_.enrageMaxBuildup,
                         0.0F, 1.0F)
            : (monster_.enraged ? 1.0F : 0.0F);
        // Pulse: scale the red/orange fill brightness between 0.78 and 1.0.
        const float pulse = 0.89F + 0.11F
            * static_cast<float>(std::cos(enragePhase_ * 2.0 * std::acos(-1.0)));
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
        // Smaller overall — 14px tall (8 + 2*3) so it reads as a meter,
        // not a second HP bar.
        const QRectF rgBarRect(innerLeft, y, innerW,
                                kRageBarH + 2 * kBarVPad);
        drawBarV(p, rgBarRect, ragePct, hi, lo);
        QFont rgFont(QStringLiteral("Chakra Petch"), 8);
        rgFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(rgFont);
        const QRectF rgRect(innerLeft, y + kBarVPad,
                            innerW, kRageBarH);
        // Right-aligned value only. Text colour switches with the bar.
        if (monster_.enraged && monster_.enrageSeconds > 0.0F) {
            p.setPen(QColor(kEnrageR, kEnrageG, kEnrageB));
            p.drawText(rgRect.adjusted(0, 0, -8, 0),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QStringLiteral("MAX"));
        } else {
            p.setPen(QColor(245, 246, 247));
            p.drawText(rgRect.adjusted(0, 0, -8, 0),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QStringLiteral("%1%").arg(static_cast<int>(ragePct * 100)));
        }
        y += kRageBarH + 2 * kBarVPad;
    }

    // ---- 5. .srow ----
    if (onAil && scCount > 0) {
        y += kRowGap + (rageDrawn ? kRowGap : 0);
        constexpr int kScCellH = kScPadY + kScTopFont + 4 + kScMiniH + kScPadY;
        const int cellW = (innerW - kScGap * (kScCols - 1)) / kScCols;
        const int scRows = (scCount + kScCols - 1) / kScCols;
        for (int i = 0; i < scCount; ++i) {
            const int row = i / kScCols;
            const int col = i % kScCols;
            const int cy = y + row * (kScCellH + kScGap);
            const int cx = innerLeft + col * (cellW + kScGap);
            drawSc(p, QRectF(cx, cy, cellW, kScCellH), scList[i]);
        }
        y += scRows * kScCellH + (scRows - 1) * kScGap;
    }

    // ---- 6. .pgrid (HTML .pc cards: 头/翼/尾/脚) ----
    // Solo / host: per-part HP + counter.
    // Non-host client: the Health layer is not replicated to the client
    // (mhw-parts-hp-frozen-on-client-2026-07-23), so a stuck-at-full
    // pair is never drawn as HP. The card falls back to the signals
    // that ARE live on a client — Flinch/MaxFlinch (local stagger
    // layer) for the bar + value, and the Counter (+0x18) + broken /
    // severed state in the tag chip, which is no longer cleared
    // (v0.8.4-r23; the old gate blanked the chip, which is why the
    // grid "几乎不显示" in multiplayer).
    // (v0.7.4 PR B: totalPct removed — was never consumed in either
    // solo or multiplayer path.)
    // v0.10.x-r2 fix (B5): hoisted out of the loop. `constexpr` inside the
    // body was harmless, but the per-iteration
    // QDateTime::currentMSecsSinceEpoch() was a syscall per card per paint.
    // Reuse the timestamp the ailments path already samples once above
    // (panel_monster.cpp:624) so a single paint compares every card against
    // ONE clock.
    // v0.10.3-r6 (B1): pcList was built ONCE, above the height reservation
    // (see the "ONE buildPcList() call per paint" comment there), because
    // the builder advances partTrack_ and must not run twice per paint.
    // The AutoHide window it applies (kPartAutoHideMs, 15 s) now lives with
    // the builder — see buildPcList()'s own comment block — so the silence
    // filter and the list it filters can never be edited apart.
    if (onParts && !pcList.isEmpty()) {
        y += kRowGap;
        // Same shared constants (now defined once, next to the kPc*
        // geometry at the top of this file) and the same pcGaugeExtraH()
        // the panel height reservation above called. Keeping the render
        // loop word-for-word identical to the reservation loop is the
        // whole point of this extraction: v0.10.3's tenderize strip went
        // permanently invisible precisely because the two copies
        // disagreed on the gate while both looked locally correct.
        QVector<int> cellHeights(pcList.size(), kPcBaseCellH);
        for (int i = 0; i < pcList.size(); ++i)
            cellHeights[i] += pcGaugeExtraH(pcList[i]);
        const int cellW = (innerW - kPcGap * (kPcCols - 1)) / kPcCols;
        const int pcRows = (pcList.size() + kPcCols - 1) / kPcCols;
        // Per-row max height keeps cells aligned; per-cell height gives
        // each card the room it actually needs.
        QVector<int> rowHeights(pcRows, 0);
        for (int r = 0; r < pcRows; ++r) {
            const int start = r * kPcCols;
            const int end   = std::min(start + kPcCols,
                                       static_cast<int>(pcList.size()));
            int rowMax = 0;
            for (int c = start; c < end; ++c)
                rowMax = std::max(rowMax, cellHeights[c]);
            rowHeights[r] = rowMax;
        }
        for (int i = 0; i < pcList.size(); ++i) {
            const int row = i / kPcCols;
            const int col = i % kPcCols;
            // y0 is the top of this row; cy is the top of this cell
            // (cells in the same row are anchored to the row's top).
            int rowY = y;
            for (int r = 0; r < row; ++r)
                rowY += rowHeights[r] + kPcGap;
            const int cx = innerLeft + col * (cellW + kPcGap);
            drawPc(p, QRectF(cx, rowY, cellW, cellHeights[i]),
                   pcList[i]);
        }
        int rowSum = 0;
        for (int r = 0; r < pcRows; ++r) rowSum += rowHeights[r];
        y += rowSum + (pcRows - 1) * kPcGap;
    }
}

void MonsterPanel::setupDemoData()
{
    using namespace mhw;
    MonsterSnapshot m;
    m.address = 0xDEADBEEFULL;
    m.id = 1;                                 // 火龙
    m.internalName = mh::tr("ui.demo.monster_name");
    m.size = 1.25F;                           // Gold
    m.maxHealth = 25800.0F;
    m.health = 18420.0F;                      // 71%
    m.maxStamina = 1000.0F;
    m.stamina = 700.0F;
    m.enraged = true;
    m.enrageMaxSeconds = 120.0F;
    m.enrageSeconds = 88.0F;                  // 32s remaining
    m.enrageMaxBuildup = 100.0F;
    m.enrageBuildup = 100.0F;

    // Mirrors the four cards in the HTML demo so visual regression is easy.
    //   - 麻痹:        active, timer counts down (12s remaining)
    //   - 麻痹陷阱:    active, timer counts down (8s remaining)
    //   - 落穴:        active, timer counts down (60s remaining) — but HTML
    //                 uses "45%" because they chose buildup for the trap.
    //                 Both interpretations are valid; the timer path matches
    //                 HunterPie's live data.
    //   - 毒:          not active, but has 30% build-up and 2 prior triggers.
    // i18n: demo labels come from ui.demo.ail.* / ui.demo.part.* so the
    // edit-mode preview follows the locale (docs/I18N.md: the demo is the
    // regression baseline and now tracks the loaded locale too).
    struct Ail { int id; QString name; float timer; float maxT;
                float bu; float maxB; int cnt; bool active; };
    const Ail demoAil[] = {
        { 2, mh::tr("ui.demo.ail.paralysis"), 12.0F, 20.0F,  0.0F,   0.0F, 1, true },
        {14, mh::tr("ui.demo.ail.shock_trap"), 8.0F,  8.0F,  0.0F,   0.0F, 0, true },
        {15, mh::tr("ui.demo.ail.pitfall"),   60.0F, 60.0F,  0.0F,   0.0F, 0, true },
        { 1, mh::tr("ui.demo.ail.poison"),     0.0F,  0.0F, 30.0F, 100.0F, 2, false},
    };
    for (const auto &a : demoAil) {
        MonsterAilmentSnapshot ail;
        ail.id = a.id;
        ail.name = a.name;
        ail.active = a.active;
        ail.timer = a.timer;
        ail.maxTimer = a.maxT;
        ail.buildup = a.bu;
        ail.maxBuildup = a.maxB;
        ail.counter = a.cnt;
        m.ailments.append(ail);
    }

    // Demo parts — head/左右翼/尾巴/左右脚, mirrors HTML v8 .pgrid.
    // Names go through ui.demo.part.* (same locale rule as the ailments).
    struct DPart { int idx; QString name;
                   float hp; float maxHp;
                   int counter; bool breakable; bool severable; };
    const DPart dparts[] = {
        { 0, mh::tr("ui.demo.part.head"),  80.0F, 100.0F, 0, true,  false},
        { 1, mh::tr("ui.demo.part.l_wing"), 45.0F, 100.0F, 1, true,  true },
        { 2, mh::tr("ui.demo.part.r_wing"), 20.0F, 100.0F, 2, true,  true },
        { 3, mh::tr("ui.demo.part.tail"),  90.0F, 100.0F, 0, false, true },
        { 4, mh::tr("ui.demo.part.l_leg"), 60.0F, 100.0F, 0, false, false},
        { 5, mh::tr("ui.demo.part.r_leg"), 35.0F, 100.0F, 0, false, false},
    };
    for (const auto &dp : dparts) {
        PartSnapshot ps;
        ps.index = dp.idx;
        ps.name = dp.name;
        ps.health = dp.hp;
        ps.maxHealth = dp.maxHp;
        ps.flinch = dp.hp;            // mirror for flinch bar pre-PR C
        ps.maxFlinch = dp.maxHp;
        ps.counter = dp.counter;
        ps.isBreakable = dp.breakable;
        ps.isSeverable = dp.severable;
        ps.partType = dp.severable ? mhw::PartType::Severable
                    : (dp.breakable ? mhw::PartType::Breakable
                                    : mhw::PartType::Flinch);
        m.parts.append(ps);
    }

    // v0.7.4 PR B + PR C: tenderize is now per-part (HunterPie model).
    // Stamp two demo parts with active tenderize so the .pc strip lights up
    // in the control panel. 头 (idx 0) — 45s / 90s; 尾巴 (idx 3) — 12s / 90s.
    // Demo schemas above always emit >= 4 parts (头/左翼/右翼/尾巴/左脚/右脚
    // for 火龙 id=1), so the assert only fires on a malformed demo set.
    Q_ASSERT(m.parts.size() >= 4);
    m.parts[0].tenderizeDuration    = 45.0F;
    m.parts[0].tenderizeMaxDuration = 90.0F;
    m.parts[3].tenderizeDuration    = 12.0F;
    m.parts[3].tenderizeMaxDuration = 90.0F;

    monster_ = m;
    hasData_ = true;

    if (m.enraged && m.enrageSeconds > 0.0F && !pulseTimer_.isActive()) {
        phaseClock_.restart();
        pulseTimer_.start();
    }
}