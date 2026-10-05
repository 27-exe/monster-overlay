// SPDX-License-Identifier: Apache-2.0
//
// MonsterPartsStripPanel implementation.
//
// The drawing sub-routines and geometry tokens below are this file's own
// copies of the ones it names in panel_monster.cpp, written out in full
// on purpose: drawPc(), the pcHas* predicates and pcGaugeExtraH() live in
// that file's anonymous namespace, and hoisting them into a shared header
// would mean editing a file this change may not touch in a way that
// moves the pixels the 10 single-mode baselines depend on. The header
// records the trade in full; the rule here is the same one every v0.12
// split window follows — the copy is confined to the block being
// extracted, and every copied symbol names the line it mirrors.
//
// What is NOT copied is the state and the assembly: MonsterPartListBuilder
// is owned here (not duplicated) and it is the only producer of PcEntry,
// so the 15 s PartAutoHide window is identical between the panel and this
// window by construction.

#include "panel_monster_split_parts.h"

#include "core/game_tables.h"
#include "core/string_table.h"
#include "monster/monster_types.h"
#include "ui/formatters.h"
// MonsterSection::Parts / ::Tenderize — the mask axes, the same bits
// MonsterPanel reads (panel_monster.cpp:968 / :976).
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm (kSplitMargin, kSplitGapWithin) this window
// answers contentMargin() and builds its grid from.
#include "ui/panel_player_split_layout.h"
// The ONE assembler and the ONE height contract. The Geometry tokens
// below are this file's copies; the builder and PcEntry are not.
#include "ui/viewmodel/monster_view_model.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

// i18n lookup, defined the same way panel_monster.cpp:59-63 defines it.
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- .pgrid geometry — straight from panel_monster.cpp:293-306 --------
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
// ---- v0.7.4 PR C: the per-part tenderize strip (:305-311) ----
constexpr int kPcTnH      = 3;     // tenderize mini bar (only when active)
constexpr int kPcTnGap    = 2;     // gap between .pn and the tenderize strip
// The label sits in a kPcTnLabelH tall row; visually it occupies the gap
// between .pn and the strip (no extra kPcTnLabelGap needed).
constexpr int kPcTnLabelH = 9;     // "Ns" label height (matches kPcTagFont)
// The v0.10.3-r6 hard-stagger Row 1 gauge (:332-334).
constexpr int kPcFlinchH = 4;      // Row 1 gauge track height (.mini-like)
constexpr int kPcGaugeGap = 2;     // gap between the two gauge rows
// Whether the optional Row 1 flinch gauge is drawn at all
// (panel_monster.cpp:344). True in the panel, copied verbatim.
constexpr bool kPcDrawFlinchRow = true;

// The default width the demo's hex portrait and the panel's own width are
// measured from; NOT this window's content size (which is derived).
constexpr int kPanelWidth = 380;   // panel_monster.cpp:76
constexpr int kPanelPad   = 9;     // panel_monster.cpp:77

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

// Reader values reach these drawings twice sanitized: once in the model,
// once by the clamp they drive. Same helper as panel_monster.cpp:70.
inline float sanitizeQ10(float v) { return std::isfinite(v) ? v : 0.0F; }

// panel_monster.cpp:342-346 — the flinch gauge's presence test on the raw
// denominator. Kept separate from the PcEntry overload because
// pcGaugeExtraH() calls it before pcList exists in the panel's height
// pass; this window's copy is only ever handed a PcEntry, but the
// primitive is kept for the same reason and with the same name so the
// two read alike side by side.
inline bool pcHasFlinch(float flinchMax)
{
    return flinchMax > 0.0F;
}

// panel_monster.cpp:414, "pcHasPrimaryGauge". Why a flag and not a
// heuristic: on World the per-layer Health/MaxHealth is local-feedback
// data, so in a multiplayer session a non-host client's pair is frozen
// at whatever the last authoritative update left — usually full. Drawing
// that as HP shows a broken part at 90-100%. The session player count is
// what says so; this is the predicate the builder leaves on the entry.
inline bool pcHasPrimaryGauge(const PcEntry &e) { return e.hasPrimaryGauge; }

// panel_monster.cpp:429-437 — the Row 1 flinch gauge's presence test.
// A World NORMAL part receives a flinch layer that is exactly as stale as
// the Health pair on a non-host client, so it is suppressed under the
// same condition (through hasPrimaryGauge, which the builder sets).
inline bool pcHasFlinch(const PcEntry &e)
{
    if (!pcHasPrimaryGauge(e))
        return false;
    return pcHasFlinch(e.flinchMax);
}

// panel_monster.cpp:450-461, "pcPrimaryGaugeDebt" and "pcGaugeExtraH".
// THE ONE height calculator, copied verbatim: the rows this card does
// NOT spend on a primary-layer gauge, folded into one place so the
// reservation and the draw stay identical. v0.10.3 shipped an invisible
// tenderize strip because a draw gate and two height reservations read
// different fields; negative-adjust rather than conditional-add keeps
// one expression to inspect.
inline int pcPrimaryGaugeDebt(const PcEntry &e)
{
    return pcHasPrimaryGauge(e) ? 0 : (kPcMiniH + kPcValueH + kPcValueGap);
}

inline int pcGaugeExtraH(const PcEntry &e)
{
    int extra = 0;
    if (e.tenderizeDuration > 0.0F)
        extra += kPcTnExtra;
    if (kPcDrawFlinchRow && pcHasFlinch(e))
        extra += kPcFlinchExtra;
    extra -= pcPrimaryGaugeDebt(e);
    return extra;
}

// panel_monster.cpp:506-700, "drawPc". The .pc card: name row with its
// tag chip, the optional "Ns" tenderize strip, the value row, the
// optional Row 1 flinch gauge, and the .mini track with its coloured
// fill. Copied verbatim apart from being reachable from this translation
// unit; the body is identical because every branch in it is a contract
// with the reservation above it.
void drawPc(QPainter &p, const QRectF &cell, const PcEntry &e)
{
    p.setRenderHint(QPainter::Antialiasing);

    // ---- .pn — the part's name, with its tag chip on the right ----
    QFont pnFont(QStringLiteral("Chakra Petch"), kPcPnFont);
    pnFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(pnFont);
    p.setPen(QColor(200, 205, 208));                       // --t2
    const int pnY = static_cast<int>(cell.top()) + kPcPadY;
    const int pnH = kPcPnFont + 2;
    // "斩" / "破" chip, drawn first so the name's width can be measured
    // against what is left of the row.
    QString chipText;
    if (!e.tag.isEmpty())
        chipText = e.tag;
    const int tagW = chipText.isEmpty()
        ? 0
        : QFontMetrics(pnFont).horizontalAdvance(chipText) + 2 * kPcTagPadX;
    const int nameW = static_cast<int>(cell.width())
                      - 2 * kPcPadX - (chipText.isEmpty() ? 0 : tagW);
    p.drawText(QRectF(cell.x() + kPcPadX, pnY, nameW, pnH),
               Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(pnFont).elidedText(e.name, Qt::ElideRight, nameW));
    if (!chipText.isEmpty()) {
        QFont tagFont(QStringLiteral("Chakra Petch"), kPcTagFont, QFont::Bold);
        p.setFont(tagFont);
        p.setPen(e.tagKind == QStringLiteral("brk") ? QColor(244, 67, 54)
                                                    : QColor(255, 193, 7));
        const QRectF tagRect(cell.x() + kPcPadX + nameW - kPcTagPadX,
                             pnY, tagW, pnH);
        p.drawText(tagRect, Qt::AlignCenter, chipText);
    }

    // ---- v0.7.4 PR C: the per-part tenderize strip (only when active) --
    // The remaining-time gate is the same one pcGaugeExtraH() reserves
    // with, so a spent timer reserves no space and a live one is always
    // drawn into the height it paid for.
    int y = pnY + pnH + kPcPnGap;
    if (e.tenderizeDuration > 0.0F) {
        QFont tnFont(QStringLiteral("Chakra Petch"), kPcTagFont);
        p.setFont(tnFont);
        p.setPen(QColor(251, 192, 45));
        const int remaining =
            static_cast<int>(std::ceil(sanitizeQ10(e.tenderizeDuration)));
        const QRectF tnLabelRect(cell.x() + kPcPadX, y,
                                 cell.width() - 2 * kPcPadX, kPcTnLabelH);
        p.drawText(tnLabelRect, Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1s").arg(remaining));

        const float frac = e.tenderizeMaxDuration > 0.0F
            ? std::clamp(sanitizeQ10(e.tenderizeDuration)
                             / sanitizeQ10(e.tenderizeMaxDuration),
                         0.0F, 1.0F)
            : 0.0F;
        const QRectF tnRect(cell.x() + kPcPadX, y + kPcTnLabelH,
                            cell.width() - 2 * kPcPadX, kPcTnH);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(29, 32, 34));
        p.drawRect(tnRect);
        if (frac > 0.001F) {
            p.setBrush(QColor(255, 193, 7));
            p.drawRect(tnRect.x(), tnRect.y(),
                       tnRect.width() * frac, tnRect.height());
        }
        y += kPcTnExtra;
    }

    // ---- the value row (only when the primary gauge is drawn) --------
    // pcGaugeExtraH() gives these rows back when hasPrimaryGauge is
    // false, so skipping them here is the exact inverse of that debt.
    if (pcHasPrimaryGauge(e)) {
        QFont valueFont(QStringLiteral("Chakra Petch"), kPcValueFont);
        p.setFont(valueFont);
        p.setPen(QColor(150, 152, 154));                   // --t3
        const QRectF valueRect(cell.x() + kPcPadX, y,
                               cell.width() - 2 * kPcPadX, kPcValueH);
        p.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, e.value);

        // ---- Row 1: the optional hard-stagger (flinch) gauge --------
        if (kPcDrawFlinchRow && pcHasFlinch(e)) {
            const int flY = y + kPcValueH + kPcValueGap;
            const QRectF flTrack(cell.x() + kPcPadX, flY,
                                 cell.width() - 2 * kPcPadX, kPcFlinchH);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(10, 11, 12));
            p.drawRect(flTrack);
            const float flPct = std::clamp(e.flinchPct, 0.0F, 1.0F);
            if (flPct > 0.001F) {
                p.setBrush(QColor(kPcFlR, kPcFlG, kPcFlB));
                p.drawRect(flTrack.x(), flTrack.y(),
                           flTrack.width() * flPct, flTrack.height());
            }
            y += kPcFlinchExtra;
        }

        // ---- the .mini track, the card's bottom row ------------------
        const int miniY = static_cast<int>(cell.bottom()) - kPcPadY - kPcMiniH;
        const QRectF miniRect(cell.x() + kPcPadX, miniY,
                              cell.width() - 2 * kPcPadX, kPcMiniH);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(29, 32, 34));
        p.drawRect(miniRect);
        const float pct = std::clamp(e.pct, 0.0F, 1.0F);
        if (pct > 0.001F) {
            // partGaugeFill() (monster/monster_types.h) — same function
            // the panel reads, NOT a second copy of the colour rule.
            const mhw::PartGaugeFill fill = mhw::partGaugeFill(e.broken);
            p.setBrush(QColor(fill.r, fill.g, fill.b));
            p.drawRect(miniRect.x(), miniRect.y(),
                       miniRect.width() * pct, miniRect.height());
        }
    }
}

} // namespace

MonsterPartsStripPanel::MonsterPartsStripPanel(QWidget *parent)
    : Panel(QStringLiteral("monster-parts"), Corner::TopLeft, parent)
{
    // No chrome, no title row: a card grid is a fragment of the monster
    // panel, not a panel. The translucent body still comes from
    // Panel::paintEvent() — see panel.h's v0.12.1 note on what the
    // chrome switch governs (the FRAME alone, never the surface).
    setChromeVisible(false);

    // Own window-title key (distinct from ui.monster_title, the panel's
    // own). The grid shows no cap of its own, so the key is the one
    // main.cpp's applyLocale re-sets on a locale flip.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
}

QVector<mhw::PartSnapshot> MonsterPartsStripPanel::displayableParts(
    const QVector<mhw::PartSnapshot> &parts)
{
    // panel_monster.cpp:377-392, verbatim: severable/breakable only, with
    // the first-kFlinchFallbackParts fallback when a monster has nothing
    // but flinch bars.
    constexpr int kFlinchFallbackParts = 4;
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

void MonsterPartsStripPanel::update(const mhw::MonsterSnapshot &m)
{
    // The same identity-change reset the panel's update() issues
    // (panel_monster.cpp:790-796): a stale (sig, stampMs) from the
    // previous target would make the new monster's parts look
    // already-silent (hidden on their first paint) or already-fresh
    // (pinned for a spurious 15 s).
    if (m.address != model_.address) {
        partList_.resetAutoHide();
        model_.address = m.address;
    }

    // ONE clock sample for this whole paint — the panel's own comment on
    // hoisting the per-card QDateTime::currentMSecsSinceEpoch() syscall
    // out of the loop applies here for the same reason.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    // The Tenderize bit is applied INSIDE build(), in one place, so the
    // reservation and the render cannot disagree about whether the strip
    // is on. Same reading of the same bit the panel makes (:976).
    const bool onTenderize =
        (sectionMask() & mhw::MonsterSection::Tenderize) != 0;

    model_ = mhw::buildMonsterPartsModel(
        displayableParts(m.parts), nowMs, onTenderize, editMode(), m.game,
        multiplayer_, partList_);
    model_.address = m.address;
    game_ = m.game;

    // Demo cards are never live state: the first authoritative snapshot
    // replaces them outright, so an edit-mode preview cannot survive
    // into a live session.
    demoData_ = false;

    triggerUpdate();
}

void MonsterPartsStripPanel::setMultiplayer(bool on)
{
    multiplayer_ = on;
}
void MonsterPartsStripPanel::retranslateUi()
{
    // Cached strings first, for the reason the pilot's retranslateUi()
    // states: main.cpp's applyLocale re-sets the title from the same
    // key, so the second set is deliberate redundancy. The part names
    // arrive on the snapshot and are re-resolved by the reader, so there
    // is nothing locale-dependent cached here to rebuild.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
    triggerUpdate();
}

void MonsterPartsStripPanel::setGameForDemo(mhw::GameId game)
{
    if (game_ == game)
        return;
    game_ = game;
    // Drop the one-shot demo seed so the next paint re-runs
    // setupDemoData() with the new game's part names and layer pairs.
    resetDemoPrimed();
    triggerUpdate();
}

void MonsterPartsStripPanel::setupDemoData()
{
    // Edit-mode preview: the same six parts the panel's own demo seeds
    // (panel_monster.cpp:1489-1511), so the preview and the panel agree.
    // Names go through ui.demo.part.*, the same locale rule the panel's
    // demo follows.
    struct DPart { int idx; QString name;
                  float hp; float maxHp;
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
    for (const auto &dp : dparts) {
        mhw::PartSnapshot ps;
        ps.index       = dp.idx;
        ps.name        = dp.name;
        ps.health      = dp.hp;
        ps.maxHealth   = dp.maxHp;
        ps.flinch      = dp.hp;            // mirror for flinch bar pre-PR C
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

    // Edit mode, so the PartAutoHide silence window never hides a card:
    // build() takes editMode and pins every card exactly as it does for
    // the panel.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    model_ = mhw::buildMonsterPartsModel(
        displayableParts(parts), nowMs,
        (sectionMask() & mhw::MonsterSection::Tenderize) != 0, true, game_,
        multiplayer_, partList_);
    demoData_ = true;
}

bool MonsterPartsStripPanel::partsEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing MonsterSection::Parts means the
    // console's existing monster toggle drives this window for free.
    return (sectionMask() & mhw::MonsterSection::Parts) != 0;
}

bool MonsterPartsStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    // With the mask bit off, or with no parts at all, there is nothing
    // to show, so the window must not exist: the main loop's
    // hasVisibleContent() answers false and the layer-shell surface is
    // never mounted.
    if (!partsEnabled())
        return false;
    return !model_.isEmpty();
}

void MonsterPartsStripPanel::setCardsPerRow(int perRow)
{
    // Clamped to the range the width arithmetic can express. The lower
    // bound is 1 (a column of stacked cards); the upper is the panel's
    // own 3 plus three, because the whole point of a window over a panel
    // is that it is not stuck at the panel's column count.
    const int clamped = std::clamp(perRow, 1, kPcCols + 3);
    if (clamped == columns_)
        return;
    columns_ = clamped;
    triggerUpdate();
}

void MonsterPartsStripPanel::nudgeColumns(int delta)
{
    setCardsPerRow(columns_ + Panel::kColumnsStep * (delta > 0 ? 1 : -1));
}

int MonsterPartsStripPanel::naturalItemWidth() const
{
    // The panel's own .pgrid column width: (innerW - gap*(cols-1)) / cols
    // with innerW = kPanelWidth - 2*kPanelPad and its own kPcCols.
    // Carried as the NATURAL width because that is what a layout
    // compares against.
    const int innerW = kPanelWidth - 2 * kPanelPad;
    const int w = (innerW - kPcGap * (kPcCols - 1)) / kPcCols;
    return w > 0 ? w : 0;
}

int MonsterPartsStripPanel::itemGap() const
{
    return kPcGap;
}

int MonsterPartsStripPanel::rowPitch() const
{
    // The tallest card plus the gap — the height a row actually costs,
    // which is what a layout needs to reserve when it derives a wrap.
    int tallest = 0;
    for (int i = 0; i < model_.cards.size(); ++i)
        tallest = std::max(tallest, cardHeight(i));
    return tallest + kPcGap;
}

int MonsterPartsStripPanel::cardHeight(int index) const
{
    // kPcBaseCellH + pcGaugeExtraH(), the panel's own cellHeights[i]
    // (panel_monster.cpp:1041-1042) — one owner per term, both copied
    // verbatim from the lines this file names.
    return kPcBaseCellH + pcGaugeExtraH(model_.cards[index]);
}

int MonsterPartsStripPanel::gridBlockHeight() const
{
    const int count = model_.cards.size();
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
            rowMax = std::max(rowMax, cardHeight(c));
        sum += rowMax;
    }
    return sum + (rows - 1) * kPcGap;
}

int MonsterPartsStripPanel::slotWidth(int innerW) const
{
    // The exact inverse of contentWidth(): one card at this width fills
    // `innerW` with `columns_` columns. panel_sizing's own function, so
    // the two directions cannot drift.
    return mhw::panel_sizing::gridSlotWidth(innerW, columns_, kPcGap);
}

int MonsterPartsStripPanel::contentWidth() const
{
    const int count = model_.cards.size();
    if (count <= 0 || !partsEnabled())
        return 2 * mhw::split_layout::kSplitMargin;
    // What the grid's CELLS need: `columns_` cards of the natural width
    // plus (columns_-1) gaps. Through panel_sizing's gridContentWidth,
    // the same function the panels use.
    const int perRow = std::min(columns_, count);
    return mhw::panel_sizing::gridContentWidth(count, perRow,
                                               naturalItemWidth(), kPcGap)
           + 2 * mhw::split_layout::kSplitMargin;
}

int MonsterPartsStripPanel::contentHeight() const
{
    // With no cards the window answers the collapsed size the same way
    // every grid does (panel_sizing's gridEmptySize), never a 0-height
    // surface.
    if (model_.isEmpty())
        return mhw::panel_sizing::gridEmptySize(
                    mhw::split_layout::kSplitMargin).height;
    return gridBlockHeight() + 2 * mhw::split_layout::kSplitMargin;
}

void MonsterPartsStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an
    // unmounted-but-alive window never renders a leftover sliver. The
    // live path never reaches here — the main loop asks
    // hasVisibleContent() first.
    if (!partsEnabled() || model_.isEmpty()) {
        setContentSize(2 * mhw::split_layout::kSplitMargin, h);
        return;
    }

    setContentSize(w, h);

    // Re-read the width the window actually got: setContentSize()
    // applied the user's minimum-width floor, but `w` is still the
    // pre-floor content number.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE. drawV03Chrome() is deliberately not called — the
    // translucent body still comes from Panel::paintEvent().
    const int innerW = drawnW - 2 * mhw::split_layout::kSplitMargin;
    const int innerX = mhw::split_layout::kSplitMargin;

    const int cellW = slotWidth(innerW);
    // A budget too small to hold even the gaps has no room for a card;
    // answering 0 here rather than drawing a negative-width rect is what
    // keeps panel_sizing's gridOverflows() handshake honest.
    if (cellW <= 0)
        return;

    const int count = model_.cards.size();
    // The SAME two passes the reservation ran: per-card heights, then
    // per-row maxima, so each row is anchored to the row's top and every
    // card in it is drawn at the height it paid for.
    QVector<int> cellHeights(count, kPcBaseCellH);
    for (int i = 0; i < count; ++i)
        cellHeights[i] += pcGaugeExtraH(model_.cards[i]);
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

    int y = mhw::split_layout::kSplitMargin;
    for (int i = 0; i < count; ++i) {
        const int row = i / columns_;
        const int col = i % columns_;
        // y0 is the top of this row; cy is the top of this cell (cells
        // in the same row are anchored to the row's top).
        int rowY = y;
        for (int r = 0; r < row; ++r)
            rowY += rowHeights[r] + kPcGap;
        const int cx = innerX + col * (cellW + kPcGap);
        drawPc(p, QRectF(cx, rowY, cellW, cellHeights[i]),
               model_.cards[i]);
    }
}
