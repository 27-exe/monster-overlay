// SPDX-License-Identifier: Apache-2.0
//
// MonsterConditionsStripPanel implementation. Every drawing sub-routine
// below is this file's own small copy of the one it names in
// panel_monster.cpp, written out in full on purpose: drawSc() and
// drawPc() live in that file's anonymous namespace and are unreachable
// from here, and hoisting them into a shared header would mean editing a
// file this change may not touch in a way that moves the pixels the 10
// single-mode baselines depend on. The header records the trade in full;
// the rule here is the same one every v0.12/v0.13 split window follows —
// the copy is confined to the blocks being extracted, and every copied
// symbol names the line it mirrors.
//
// What is NOT copied is the STATE and the ASSEMBLY: MonsterPartListBuilder
// is owned here (not duplicated) and it is the only producer of PcEntry,
// so the 15 s PartAutoHide window is identical between the panel and this
// window by construction. The ailments' AutoHide table and
// buildMonsterAilModel() are INHERITED from ui/panel_monster_split_ail.h
// (declared) and panel_monster_split_ail.cpp (defined) — spelled once for
// the whole split, so one window turning an ailment card off and one
// window turning it on cannot both be right.

#include "panel_monster_split_conditions.h"

#include "core/game_tables.h"
#include "core/string_table.h"
#include "monster/monster_types.h"
// MonsterSection::Ail / ::Parts / ::Tenderize — the mask axes, the same
// bits MonsterPanel reads (panel_monster.cpp :967 / :968 / :976).
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm (kSplitMargin, the gap grades) this window
// answers contentMargin() and builds its grids from.
#include "ui/panel_player_split_layout.h"
// The ailment colour registry (game-keyed) and the part schemas.
#include "ui/icon.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPixmap>
#include <QPainter>

#include <algorithm>
#include <cmath>

// i18n lookup, defined the same way panel_monster.cpp:59-63 defines it.
// The card names arrive already translated on the snapshot, and the
// demo's own labels go through ui.demo.ail.* / ui.demo.part.*, the same
// locale rule the panel's demo follows. The lookup is here so a future
// label goes through the table rather than an inline zh literal.
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- Layout tokens ---------------------------------------------------
constexpr int kMargin  = mhw::split_layout::kSplitMargin;
// The gap between the two grids is the split's BETWEEN grade, not the
// panel's kRowGap = 9 — that is the whole point of a window ring.
constexpr int kBlockGap = mhw::split_layout::kSplitGapBetween;

// ---- .srow geometry — panel_monster.cpp:94-101 ------------------------
constexpr int kScGap      = 5;    // .srow gap (panel_monster.cpp:95)
constexpr int kScPadX     = 6;    // .sc padding (:96)
constexpr int kScPadY     = 4;    // :97
constexpr int kScTopFont  = 9;    // .sc .top (:98)
constexpr int kScTmFont   = 9;    // .sc .tm (:99)
constexpr int kScMiniH    = 5;    // .sc .mini height (:100)
constexpr int kScIconSize = 11;   // .sc .top svg (:101)

// The cell height the panel's two sites agree on: kScPadY + kScTopFont +
// 4 + kScMiniH + kScPadY. Spelled out as the sum of the tokens above
// plus the same 4 the panel's `kScCellH` uses, so the window's row
// pitch and the panel's cannot drift apart independently.
constexpr int kScCellH = kScPadY + kScTopFont + 4 + kScMiniH + kScPadY;

// The natural width the panel's .srow column resolves to. Derived from
// the panel's own width, NOT from this window's content size (which is
// itself derived): the NATURAL width is what a layout compares against
// before the user's dial widens it.
constexpr int kPanelWidth = 380;   // panel_monster.cpp:76
constexpr int kPanelPad   = 9;     // panel_monster.cpp:77

// ---- .pgrid geometry — panel_monster.cpp:293-306 ----------------------
constexpr int kPcCols      = 3;     // .pgrid grid-template-columns
constexpr int kPcGap       = 5;     // .pgrid gap
constexpr int kPcPadX      = 6;     // .pc padding 4 6
constexpr int kPcPadY      = 4;
constexpr int kPcPnFont    = 9;     // .pc .pn font-size
constexpr int kPcValueFont = 8;     // current/max HP row
constexpr int kPcValueH    = kPcValueFont + 2;
constexpr int kPcValueGap  = 2;
constexpr int kPcTagFont   = 8;     // .pc .tag font-size
constexpr int kPcTagPadX   = 4;     // .pc .tag padding 0 4
constexpr int kPcMiniH     = 4;     // .pc .mini height:4
constexpr int kPcPnGap     = 3;     // .pc .pn margin-bottom
// ---- v0.7.4 PR C: the per-part tenderize strip (:305-311) ----
constexpr int kPcTnH       = 3;     // tenderize mini bar (only when active)
constexpr int kPcTnGap     = 2;     // gap between .pn and the tenderize strip
constexpr int kPcTnLabelH  = 9;     // "Ns" label height (matches kPcTagFont)
// The v0.10.3-r6 hard-stagger Row 1 gauge (:332-334).
constexpr int kPcFlinchH   = 4;     // Row 1 gauge track height (.mini-like)
constexpr int kPcGaugeGap  = 2;     // gap between the two gauge rows
constexpr bool kPcDrawFlinchRow = true;

// Derived, exactly as panel_monster.cpp:318-334 derives them.
constexpr int kPcBaseCellH = kPcPadY + kPcPnFont + 2 + kPcPnGap
                           + kPcValueH + kPcValueGap
                           + kPcMiniH + kPcPadY;
constexpr int kPcTnExtra      = kPcTnLabelH + kPcTnH + kPcTnGap;
constexpr int kPcFlinchExtra  = kPcFlinchH + kPcGaugeGap;

// ---- colour palette (panel_monster.cpp:340-342) ----
constexpr int kPcFlR = 0x4B;
constexpr int kPcFlG = 0x8E;
constexpr int kPcFlB = 0xEE;

// The panel's fallback count when a monster has nothing but flinch
// bars (panel_monster.cpp:375).
constexpr int kFlinchFallbackParts = 4;

// The ailments' 15 s change-based AutoHide. The tracker class itself
// lives in the header (it is a public part of this window's contract),
// and this is the panel's own value.
constexpr qint64 kAilAutoHideMs = 15000;

// Reader values reach these drawings twice sanitized: once in the model,
// once by the clamp they drive. A NaN or Inf that slips through produces
// a percentage like "-2147483648%" instead of a bar, because the cast of
// a non-finite float is platform-defined. Same helper as
// panel_monster.cpp:70.
inline float sanitizeQ10(float v) { return std::isfinite(v) ? v : 0.0F; }

// panel_monster.cpp:229-284, "drawSc" — the .sc card: background +
// state border, optional svg icon, the elided name, the right-aligned
// timer text, and the 5px mini track with its coloured fill. Copied
// verbatim apart from being reachable from this translation unit, and
// apart from the name column's reserved width, which this window
// measures from its own card width rather than the panel's.
void drawSc(QPainter &p, const QRectF &cell, const mhw::MonsterAilCard &e)
{
    p.setRenderHint(QPainter::Antialiasing);

    // 1) Card background + border. .sc[data-state=active] uses --sc,
    // otherwise --line.
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

// panel_monster.cpp:342-346 — the flinch gauge's presence test on the
// raw denominator. Kept separate from the PcEntry overload because
// pcGaugeExtraH() calls it before pcList exists in the panel's height
// pass; this window's copy is only ever handed a PcEntry, but the
// primitive is kept for the same reason and with the same name so the
// two read alike side by side.
inline bool pcHasFlinch(float flinchMax)
{
    return flinchMax > 0.0F;
}

// panel_monster.cpp:414, "pcHasPrimaryGauge".
inline bool pcHasPrimaryGauge(const PcEntry &e) { return e.hasPrimaryGauge; }

// panel_monster.cpp:435, the PcEntry overload of pcHasFlinch.
inline bool pcHasFlinch(const PcEntry &e)
{
    if (!pcHasPrimaryGauge(e))
        return false;
    return pcHasFlinch(e.flinchMax);
}

// panel_monster.cpp:458-461, "pcPrimaryGaugeDebt".
inline int pcPrimaryGaugeDebt(const PcEntry &e)
{
    return pcHasPrimaryGauge(e) ? 0 : (kPcMiniH + kPcValueH + kPcValueGap);
}

// panel_monster.cpp:463-477, "pcGaugeExtraH" — the ONE height
// calculator. Every reservation site and the draw site call the same
// function on the same list, which is the v0.10.3 lesson.
inline int pcGaugeExtraH(const PcEntry &e)
{
    int extra = 0;
    // The remaining-time gate matches the strip painter and HunterPie's
    // Tenderize visibility binding; a spent timer reserves no space.
    if (e.tenderizeDuration > 0.0F)
        extra += kPcTnExtra;
    if (kPcDrawFlinchRow && pcHasFlinch(e))
        extra += kPcFlinchExtra;
    extra -= pcPrimaryGaugeDebt(e);
    return extra;
}

// panel_monster.cpp:490+, "pcHasTenderize" — the ONE predicate for
// "this .pc card carries a tenderize strip", shared verbatim by the draw
// gate and both height reservations.
inline bool pcHasTenderize(const PcEntry &e)
{
    return e.tenderizeDuration > 0.0F;
}

// panel_monster.cpp:496-732, "drawPc" — the .pc card. Copied verbatim
// apart from being reachable from this translation unit. The layout is
// bottom-anchored (.mini, value row, flinch, tenderize) exactly as the
// panel's is, so a card in this window occupies the same pixels a card
// in the panel does.
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
    // draw gate below read the SAME value. Rise has NO Counter field at
    // all, so a "破 0 / 斩 0" chip is pure noise there — suppressed at 0,
    // and the part name gets the full width back.
    const QString chipText = (!e.tag.isEmpty() && e.counter > 0)
        ? QStringLiteral("%1 %2").arg(e.tag).arg(e.counter)
        : QString();
    const int nameW = std::max(0, static_cast<int>(cell.width())
        - 2 * kPcPadX - (chipText.isEmpty() ? 0 : 36));
    p.drawText(QRectF(static_cast<qreal>(static_cast<int>(cell.x()) + kPcPadX),
                      pnY, nameW, pnH),
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

    // v0.10.8: the primary-layer gauge and its numeric value row are only
    // present when we have live data for them (see pcHasPrimaryGauge). On
    // World in a multiplayer session both go: no track, no fill, no label.
    const int miniY = static_cast<int>(cell.bottom())
                      - kPcPadY - kPcMiniH;
    const QRectF miniRect(static_cast<qreal>(static_cast<int>(cell.x())
                                             + kPcPadX),
                          miniY, cell.width() - 2 * kPcPadX, kPcMiniH);
    if (pcHasPrimaryGauge(e)) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(10, 11, 12));          // #0a0b0c
        p.drawRect(miniRect);
        const float clamped = std::clamp(e.pct, 0.0F, 1.0F);
        if (clamped > 0.001F) {
            // v0.10.10 — the fill has two states, not two per-tag hues.
            // HunterPie's part brush switches to Part.Broken.Foreground
            // (#71717A) on IsPartBroken/IsPartSevered, for either kind.
            const mhw::PartGaugeFill fillMhw = mhw::partGaugeFill(e.broken);
            p.setBrush(QColor(fillMhw.r, fillMhw.g, fillMhw.b));
            p.drawRect(miniRect.x(), miniY,
                       miniRect.width() * clamped, miniRect.height());
        }
    }

    // Position the value above only the tenderize strip. The Flinch row
    // stacks above the value, while pcGaugeExtraH reserves both rows.
    const int tenderizeHeight = pcHasTenderize(e) ? kPcTnExtra : 0;
    QFont valueFont(QStringLiteral("Chakra Petch"), kPcValueFont,
                    QFont::Medium);
    valueFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(valueFont);
    p.setPen(QColor(150, 154, 158));
    const QString layerName = e.valueLayer == PcEntry::Layer::Flinch
        ? QStringLiteral("Flinch") : e.valueLayer == PcEntry::Layer::Sever
        ? QStringLiteral("Sever") : QStringLiteral("Break");
    // The value row only exists to label the primary gauge; without the
    // gauge it is a dangling "Sever 2750/2750" over empty space.
    const int valueY = pcHasPrimaryGauge(e)
        ? (miniY - kPcValueGap - tenderizeHeight - kPcValueH)
        : (pnY + pnH + kPcValueGap + tenderizeHeight);
    if (pcHasPrimaryGauge(e)) {
        p.drawText(QRectF(static_cast<qreal>(static_cast<int>(cell.x())
                                             + kPcPadX),
                          valueY, cell.width() - 2 * kPcPadX, kPcValueH),
                   Qt::AlignRight | Qt::AlignVCenter,
                   layerName + QStringLiteral(" ") + e.value);
    }

    // Flinch gauge sits above the value row with kPcGaugeGap; the primary
    // gauge remains anchored at the bottom. Height and paint share the gate.
    if (kPcDrawFlinchRow && pcHasFlinch(e)) {
        const int flTop = valueY - kPcGaugeGap - kPcFlinchH;
        const QRectF flRect(static_cast<qreal>(static_cast<int>(cell.x())
                                               + kPcPadX),
                            flTop, cell.width() - 2 * kPcPadX, kPcFlinchH);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(10, 11, 12));       // #0a0b0c, same as .mini
        p.drawRect(flRect);
        // flinchMax > 0 is guaranteed by pcHasFlinch(), so the division
        // is safe; the guard keeps the intent explicit.
        const float flPct = std::clamp(e.flinchPct, 0.0F, 1.0F);
        if (flPct > 0.001F) {
            // HunterPie does NOT swap this gauge's brush on break state —
            // it stays Part.Default.Foreground (blue).
            p.setBrush(QColor(kPcFlR, kPcFlG, kPcFlB));   // #4B8EEE
            p.drawRect(flRect.x(), flRect.y(),
                       flRect.width() * flPct, flRect.height());
        }
    }

    // v0.7.4 PR C: per-part tenderize strip. Drawn ABOVE .mini and below
    // .pn (the caller reserves the extra kPcTnLabelH + kPcTnH + kPcTnGap
    // when this card has an active tenderize). Amber palette matches the
    // .sev tag.
    if (pcHasTenderize(e)) {
        const QColor amber(246, 165, 34);    // #f6a522
        // Strip + label layout: stack from .mini top going up.
        const int barY = static_cast<int>(miniRect.top())
                         - kPcTnGap - kPcTnH;
        const int labelY = barY - kPcTnLabelH;
        const QRectF tnRect(static_cast<qreal>(static_cast<int>(cell.x())
                                               + kPcPadX),
                            barY, cell.width() - 2 * kPcPadX, kPcTnH);
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
                   // tenderizeDuration already stores REMAINING seconds;
                   // round UP so a 0.4s tail reads "1s" rather than a
                   // sticky "0s".
                   QStringLiteral("%1s").arg(
                       static_cast<int>(std::ceil(e.tenderizeDuration))));
        // Track.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(10, 11, 12));
        p.drawRect(tnRect);
        // Fill. NOTE: this `tenderizeMaxDuration > 0.0F` is a DIVISOR
        // guard, not a visibility gate — the strip above is already
        // gated by pcHasTenderize(e).
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

} // namespace

MonsterConditionsStripPanel::MonsterConditionsStripPanel(
    QWidget *parent, int subWindowIndex)
    : Panel(QStringLiteral("monster-conditions"), Corner::TopLeft, parent,
        subWindowIndex)
{
    // No chrome, no title row: this window is a fragment of the monster
    // panel, not a panel. The translucent body still comes from
    // Panel::paintEvent(), which is what keeps a fragment legible over
    // the game — see panel.h's v0.12.1 note on what the chrome switch
    // governs (the FRAME alone, never the surface).
    setChromeVisible(false);

    // Own window-title key (distinct from ui.monster_title, the panel's
    // own). The grids show no caps of their own, so the key is the one
    // main.cpp's applyLocale re-sets on a locale flip.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
}

void MonsterConditionsStripPanel::update(const mhw::MonsterSnapshot &m)
{
    // The same identity-change reset the panel's update() issues on BOTH
    // of its trackers (panel_monster.cpp:790-796): switching target
    // replaces the whole snapshot, and a stale (sig, stampMs) from the
    // previous monster would judge the new monster's card
    // "already-silent" (hidden on their first paint) or
    // "already-fresh" (pinned for a spurious 15 s).
    if (m.address != ail_.address) {
        partList_.resetAutoHide();
        ailTracker_.resetAutoHide();
    }
    // `address` is refreshed on every tick and is the same key the panel
    // uses, so this fires exactly once per target switch.
    ail_.address    = m.address;
    parts_.address  = m.address;

    // ONE clock sample for this whole paint, exactly like the panel's
    // `const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();`
    // (panel_monster.cpp:887) — so every card in one frame is compared
    // against one timestamp instead of N.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // Both grids from the SAME snapshot, so the ailments and the parts
    // can never describe two different monsters.
    ail_ = mhw::buildMonsterAilModel(m.ailments, m.game, editMode(), nowMs,
                                     ailTracker_);
    ail_.address = m.address;

    // The Tenderize bit is applied INSIDE build(), in one place, so the
    // reservation and the render cannot disagree about whether the strip
    // is on. Same reading of the same bit the panel makes (:976).
    const bool onTenderize = tenderizeEnabled();
    parts_ = mhw::buildMonsterPartsModel(
        displayableParts(m.parts), nowMs, onTenderize, editMode(), m.game,
        multiplayer_, partList_);
    parts_.address = m.address;
    game_ = m.game;

    // Demo cards are never live state: the first authoritative snapshot
    // replaces them outright, so an edit-mode preview cannot survive
    // into a live session.
    demoData_ = false;

    triggerUpdate();
}

void MonsterConditionsStripPanel::setMultiplayer(bool on)
{
    multiplayer_ = on;
}

void MonsterConditionsStripPanel::retranslateUi()
{
    // Cached strings first, for the reason the pilot's retranslateUi()
    // states: main.cpp's applyLocale re-sets the title from the same
    // key, so the second set is deliberate redundancy. The card names
    // arrive on the snapshot and are re-resolved by the reader, so there
    // is nothing locale-dependent cached here to rebuild.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
    triggerUpdate();
}

void MonsterConditionsStripPanel::setGameForDemo(mhw::GameId game)
{
    if (game_ == game)
        return;
    game_ = game;
    // Drop the one-shot demo seed so the next paint re-runs
    // setupDemoData() with the new game's part names and layer pairs.
    resetDemoPrimed();
    triggerUpdate();
}

bool MonsterConditionsStripPanel::conditionsAilEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing MonsterSection::Ail means the
    // console's existing monster-section toggle drives this block for
    // free — and, unlike the five-window split, it does so INSIDE a
    // window that also carries the parts grid.
    return (sectionMask() & mhw::MonsterSection::Ail) != 0;
}

bool MonsterConditionsStripPanel::partsEnabled() const
{
    // Reusing MonsterSection::Parts, the same bit MonsterPanel reads for
    // this block (panel_monster.cpp:968).
    return (sectionMask() & mhw::MonsterSection::Parts) != 0;
}

bool MonsterConditionsStripPanel::tenderizeEnabled() const
{
    // MonsterSection::Tenderize (panel_monster.cpp:976). This has NO
    // block of its own — it is a strip INSIDE each .pc card since
    // v0.7.4 PR C — so it is read where build() is called rather than
    // gating a row of its own.
    return (sectionMask() & mhw::MonsterSection::Tenderize) != 0;
}

bool MonsterConditionsStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    // EITHER block on with data keeps the window mounted — never "both".
    // This is the answer to the user's "close one part without closing
    // the other": the window survives losing a block.
    if (conditionsAilEnabled() && !ail_.isEmpty())
        return true;
    if (partsEnabled() && !parts_.isEmpty())
        return true;
    return false;
}

void MonsterConditionsStripPanel::setCardsPerRow(int perRow)
{
    // Clamped to the range the width arithmetic can express. The lower
    // bound is 1 (a column of stacked cards); the upper is the panel's
    // own 3 plus three, because the whole point of a window over a
    // panel is that it is not stuck at the panel's column count.
    const int clamped = std::clamp(perRow, 1, kPcCols + 3);
    if (clamped == columns_)
        return;
    columns_ = clamped;
    triggerUpdate();
}

void MonsterConditionsStripPanel::setAilCardsPerRow(int perRow)
{
    // Clamped to the range the width arithmetic can express: 0 would
    // make monsterAilRows() divide by zero and contentWidth() answer a
    // 0-wide grid, and above 6 the window is wider than the panel it
    // was carved out of.
    const int clamped = std::clamp(perRow, mhw::kMonsterAilMinColumns,
                                   mhw::kMonsterAilMaxColumns);
    if (clamped == ailColumns_)
        return;
    ailColumns_ = clamped;
    triggerUpdate();
}

void MonsterConditionsStripPanel::nudgeColumns(int delta)
{
    // The base class's own dial moves the PARTS grid, matching the
    // panel's own .pgrid key handling.
    setCardsPerRow(columns_ + Panel::kColumnsStep * (delta > 0 ? 1 : -1));
}

int MonsterConditionsStripPanel::naturalItemWidth() const
{
    // The panel's own .pgrid column width: (innerW - gap*(cols-1)) / cols
    // with innerW = kPanelWidth - 2*kPanelPad and its own kPcCols.
    // Carried as the NATURAL width because that is what a layout
    // compares against.
    const int innerW = kPanelWidth - 2 * kPanelPad;
    const int w = (innerW - kPcGap * (kPcCols - 1)) / kPcCols;
    return w > 0 ? w : 0;
}

int MonsterConditionsStripPanel::itemGap() const
{
    return kPcGap;
}

int MonsterConditionsStripPanel::rowPitch() const
{
    // The tallest card plus the gap — the height a row actually costs,
    // which is what a layout needs to reserve when it derives a wrap.
    int tallest = 0;
    for (int i = 0; i < parts_.cards.size(); ++i)
        tallest = std::max(tallest, partCardHeight(i));
    return tallest + kPcGap;
}

int MonsterConditionsStripPanel::partCardHeight(int index) const
{
    // kPcBaseCellH + pcGaugeExtraH(), the panel's own cellHeights[i]
    // (panel_monster.cpp:1041-1042) — one owner per term, both copied
    // verbatim from the lines this file names.
    return kPcBaseCellH + pcGaugeExtraH(parts_.cards[index]);
}

QVector<mhw::PartSnapshot> MonsterConditionsStripPanel::displayableParts(
    const QVector<mhw::PartSnapshot> &parts)
{
    // panel_monster.cpp:377-392, verbatim: severable/breakable only, with
    // the first-kFlinchFallbackParts fallback when a monster has nothing
    // but flinch bars.
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

int MonsterConditionsStripPanel::ailGridBlockHeight() const
{
    // The grid's own height, not including the outer margins: rows of
    // cards with the gap between adjacent pairs. Through panel_sizing's
    // gridContentHeight so the arithmetic is the same one every panel
    // uses.
    const int rows = mhw::monsterAilRows(ail_.cards.size(), ailColumns_);
    return mhw::panel_sizing::gridContentHeight(rows, kScCellH, kScGap);
}

int MonsterConditionsStripPanel::ailBlockHeight() const
{
    if (!conditionsAilEnabled() || ail_.isEmpty())
        return 0;
    // The grid's own height. NO per-block margins: the window has ONE
    // outer inset (kMargin at each end of contentHeight()) and the
    // single-block windows' double-margin arithmetic belonged to a
    // window that held exactly one block.
    return ailGridBlockHeight();
}

int MonsterConditionsStripPanel::ailContentWidth() const
{
    const int count = ail_.cards.size();
    if (count <= 0 || !conditionsAilEnabled())
        return 2 * kMargin;
    // What the grid's CELLS need: `ailColumns_` cards of the natural
    // width plus (ailColumns_-1) gaps. Through panel_sizing's
    // gridContentWidth, the same function the panels use, so a
    // reservation made here and a slot width computed with
    // gridSlotWidth are exact inverses.
    const int perRow = std::min(ailColumns_, count);
    const int innerW = kPanelWidth - 2 * kPanelPad;
    const int natural = (innerW - kScGap * (perRow - 1)) / perRow;
    return mhw::panel_sizing::gridContentWidth(count, perRow,
                                               natural, kScGap)
           + 2 * kMargin;
}

int MonsterConditionsStripPanel::ailSlotWidth(int innerW) const
{
    // The exact inverse of what ailContentWidth() reserves: one card at
    // this width fills `innerW` with `ailColumns_` columns. panel_sizing's
    // own function, so the two directions cannot drift.
    return mhw::panel_sizing::gridSlotWidth(innerW, ailColumns_, kScGap);
}

int MonsterConditionsStripPanel::partsGridBlockHeight() const
{
    const int count = parts_.cards.size();
    if (count <= 0)
        return 0;
    // Per-row maximum cell height keeps cells aligned; per-card height
    // gives each card the room it actually needs. Same two loops the
    // panel's reservation runs (:1041-1058) and its render runs again
    // (:1391-1408) — identical arithmetic in all three sites.
    const int rows = (count + columns_ - 1) / columns_;
    int sum = 0;
    for (int r = 0; r < rows; ++r) {
        const int start = r * columns_;
        const int end   = std::min(start + columns_, count);
        int rowMax = 0;
        for (int c = start; c < end; ++c)
            rowMax = std::max(rowMax, partCardHeight(c));
        sum += rowMax;
    }
    return sum + (rows - 1) * kPcGap;
}

int MonsterConditionsStripPanel::partsBlockHeight() const
{
    if (!partsEnabled() || parts_.isEmpty())
        return 0;
    // See ailBlockHeight() for why there is no margin term here.
    return partsGridBlockHeight();
}

int MonsterConditionsStripPanel::partsContentWidth() const
{
    const int count = parts_.cards.size();
    if (count <= 0 || !partsEnabled())
        return 2 * kMargin;
    // What the grid's CELLS need: `columns_` cards of the natural width
    // plus (columns_-1) gaps. Through panel_sizing's gridContentWidth,
    // the same function the panels use.
    const int perRow = std::min(columns_, count);
    return mhw::panel_sizing::gridContentWidth(count, perRow,
                                               naturalItemWidth(), kPcGap)
           + 2 * kMargin;
}

int MonsterConditionsStripPanel::partsSlotWidth(int innerW) const
{
    // The exact inverse of what partsContentWidth() reserves.
    return mhw::panel_sizing::gridSlotWidth(innerW, columns_, kPcGap);
}

int MonsterConditionsStripPanel::contentWidth() const
{
    // The window is as wide as its WIDEST block. Each grid measures
    // itself; the window takes the maximum so a turned-off block can
    // never leave the other squeezed into its leftover width.
    return std::max(ailContentWidth(), partsContentWidth());
}

int MonsterConditionsStripPanel::contentHeight() const
{
    // The panel's two-block stack (:1077 and :1081), one gap between
    // adjacent ENABLED blocks. A block that is off contributes no gap
    // either — that is what keeps turning 异常状态 off from leaving an
    // 11px hole above the parts grid.
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
    addBlock(ailBlockHeight());
    addBlock(partsBlockHeight());
    total += 2 * kMargin;
    // Never a 0-height surface: panel_sizing's own collapsed answer.
    return total > 0 ? total
                     : mhw::panel_sizing::gridEmptySize(kMargin).height;
}

void MonsterConditionsStripPanel::setupDemoData()
{
    // Edit-mode preview: the SAME four ailment cards and six parts the
    // panel's own demo seeds (panel_monster.cpp:1475-1533), so the
    // preview and the panel agree on what a real hunt looks like.
    //
    //   - 麻痹:        active, timer counts down (12s remaining)
    //   - 麻痹陷阱:    active, timer counts down (8s remaining)
    //   - 落穴:        active, timer counts down (60s remaining)
    //   - 毒:          not active, but has 30% build-up and 2 triggers
    //
    // The ids are the World ones (the same ones the panel's demo uses),
    // so the palette resolves through the same game-keyed table.
    struct Ail { int id; QString name; float timer; float maxT;
                 float bu; float maxB; int cnt; bool active; };
    const Ail demoAil[] = {
        { 2, mh::tr("data.demo.ail.paralysis"), 12.0F, 20.0F,  0.0F,   0.0F, 1, true },
        {14, mh::tr("data.demo.ail.shock_trap"),  8.0F,  8.0F,  0.0F,   0.0F, 0, true },
        {15, mh::tr("data.demo.ail.pitfall"),   60.0F, 60.0F,  0.0F,   0.0F, 0, true },
        { 1, mh::tr("data.demo.ail.poison"),     0.0F,  0.0F, 30.0F, 100.0F, 2, false},
    };
    QVector<mhw::MonsterAilmentSnapshot> ailments;
    for (const Ail &a : demoAil) {
        mhw::MonsterAilmentSnapshot ail;
        ail.id        = a.id;
        ail.name      = a.name;
        ail.active    = a.active;
        ail.timer     = a.timer;
        ail.maxTimer  = a.maxT;
        ail.buildup   = a.bu;
        ail.maxBuildup = a.maxB;
        ail.counter   = a.cnt;
        ailments.append(ail);
    }

    // Demo parts — head/左右翼/尾巴/左右脚, mirrors HTML v8 .pgrid.
    // Names go through ui.demo.part.*, the same locale rule the panel's
    // demo follows.
    struct DPart { int idx; QString name; float hp; float maxHp;
                   int counter; bool breakable; bool severable; };
    const DPart dparts[] = {
        { 0, mh::tr("data.demo.part.head"),  80.0F, 100.0F, 0, true,  false},
        { 1, mh::tr("data.demo.part.l_wing"), 45.0F, 100.0F, 1, true,  true },
        { 2, mh::tr("data.demo.part.r_wing"), 20.0F, 100.0F, 2, true,  true },
        { 3, mh::tr("data.demo.part.tail"),  90.0F, 100.0F, 0, false, true },
        { 4, mh::tr("data.demo.part.l_leg"),  60.0F, 100.0F, 0, false, false},
        { 5, mh::tr("data.demo.part.r_leg"),  35.0F, 100.0F, 0, false, false},
    };
    QVector<mhw::PartSnapshot> parts;
    for (const DPart &dp : dparts) {
        mhw::PartSnapshot ps;
        ps.index       = dp.idx;
        ps.name        = dp.name;
        ps.health      = dp.hp;
        ps.maxHealth   = dp.maxHp;
        ps.flinch      = dp.hp;            // mirror for flinch bar
        ps.maxFlinch   = dp.maxHp;
        ps.counter     = dp.counter;
        ps.isBreakable = dp.breakable;
        ps.isSeverable = dp.severable;
        ps.partType    = dp.severable ? mhw::PartType::Severable
                        : (dp.breakable ? mhw::PartType::Breakable
                                        : mhw::PartType::Flinch);
        parts.append(ps);
    }

    // v0.7.4 PR B + C: tenderize is per-part. Two demo parts stamp an
    // active strip so the grid lights up in the console preview — the
    // same two the panel stamps (head 45s/90s, tail 12s/90s), which is
    // what keeps the preview and the panel's .pgrid the same picture.
    Q_ASSERT(parts.size() >= 4);
    parts[0].tenderizeDuration    = 45.0F;
    parts[0].tenderizeMaxDuration = 90.0F;
    parts[3].tenderizeDuration    = 12.0F;
    parts[3].tenderizeMaxDuration = 90.0F;

    // Edit mode, so both state machines pin every card exactly as they
    // do for the panel: the ailments' `recent = true` default and the
    // PartAutoHide's editMode parameter.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    ail_ = mhw::buildMonsterAilModel(ailments, game_, true, nowMs,
                                     ailTracker_);
    ail_.address = 0xDEADBEEFULL;
    parts_ = mhw::buildMonsterPartsModel(
        displayableParts(parts), nowMs, tenderizeEnabled(), true, game_,
        multiplayer_, partList_);
    parts_.address = 0xDEADBEEFULL;
    demoData_ = true;
}

void MonsterConditionsStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an
    // unmounted-but-alive window never renders a leftover sliver. The
    // live path never reaches here — the main loop asks
    // hasVisibleContent() first. This is the edit-preview / defensive
    // branch.
    if (!conditionsAilEnabled() && !partsEnabled()) {
        setContentSize(2 * kMargin, h);
        return;
    }

    setContentSize(w, h);

    // Re-read the width the window actually got: setContentSize()
    // applied the user's minimum-width floor, but `w` is still the
    // pre-floor content number. The window only ever grows, so the same
    // max() the base class applied reproduces it without asking the
    // compositor anything.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE. drawV03Chrome() is deliberately not called — the
    // translucent body still comes from Panel::paintEvent().
    const int innerW = drawnW - 2 * kMargin;
    const int innerX = kMargin;

    int y = kMargin;

    // ── .srow — the ailments grid ────────────────────────────────────
    if (conditionsAilEnabled() && !ail_.isEmpty()) {
        const int cellW = ailSlotWidth(innerW);
        // A budget too small to hold even the gaps has no room for a
        // card. panel_sizing's gridOverflows() handshake exists for
        // exactly this case, and answering 0 here rather than drawing a
        // negative-width rect is what keeps it honest.
        if (cellW > 0) {
            const int perRow = ailColumns_ > 0 ? ailColumns_ : 1;
            for (int i = 0; i < ail_.cards.size(); ++i) {
                const int row = i / perRow;
                const int col = i % perRow;
                if (col == 0 && row > 0)
                    y += kScCellH + kScGap;
                const int cx = innerX + col * (cellW + kScGap);
                drawSc(p, QRectF(cx, y, cellW, kScCellH), ail_.cards[i]);
            }
        }
        y += ailGridBlockHeight();
    }

    // ── .pgrid — the parts grid ──────────────────────────────────────
    if (partsEnabled() && !parts_.isEmpty()) {
        y += kBlockGap;
        const int cellW = partsSlotWidth(innerW);
        if (cellW > 0) {
            const int count = parts_.cards.size();
            // The SAME two passes the reservation ran: per-card heights,
            // then per-row maxima, so each row is anchored to the row's
            // top and every card in it is drawn at the height it paid
            // for.
            QVector<int> cellHeights(count, kPcBaseCellH);
            for (int i = 0; i < count; ++i)
                cellHeights[i] += pcGaugeExtraH(parts_.cards[i]);
            const int rows = (count + columns_ - 1) / columns_;
            QVector<int> rowHeights(rows, 0);
            for (int r = 0; r < rows; ++r) {
                const int start = r * columns_;
                const int end   = std::min(start + columns_, count);
                int rowMax = 0;
                for (int c = start; c < end; ++c)
                    rowMax = std::max(rowMax, cellHeights[c]);
                rowHeights[r] = rowMax;
            }

            for (int i = 0; i < count; ++i) {
                const int row = i / columns_;
                const int col = i % columns_;
                // y0 is the top of this row; cy is the top of this cell
                // (cells in the same row are anchored to the row's top).
                int rowY = y;
                for (int r = 0; r < row; ++r)
                    rowY += rowHeights[r] + kPcGap;
                const int cx = innerX + col * (cellW + kPcGap);
                drawPc(p, QRectF(cx, rowY, cellW, cellHeights[i]),
                       parts_.cards[i]);
            }
        }
    }
}
