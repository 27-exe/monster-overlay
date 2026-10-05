#include "panel_damage.h"

#include "core/string_table.h"
#include "player/player_types.h"
#include "quest/quest_types.h"
#include "ui/formatters.h"
#include "ui/icon.h"
#include "ui/panel_sections.h"

// v0.9 i18n: local alias for the shared StringTable. Identical definition
// to the other panels (inline → ODR-safe across translation units). Kept
// HERE rather than in panel_damage_blocks.h: the shared header's drawing
// code uses damage_blocks::tr, and a header that spelled `mh::tr` would
// collide with the copy control_panel.cpp already has.
namespace mh {
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <limits>
#include <utility>

using mhw::Icon;

namespace {

// Per-row layout constants — match HTML v8 design spec.
// HTML .drow flex: wslot(24) | name(flex) | mr(auto) | dmg(~58) | dpsLabel(22) | dpsVal(~28)
// MHW damage tops out at ~999,999 (6 digits) and DPS at ~999 (3 digits),
// so we sized each column to its max real-world width.
constexpr int kRowH     = 29;     // HTML: .drow height:29px
constexpr int kRowGap   = 5;      // HTML: .drow margin-bottom:5px
constexpr int kIconSize = 24;     // HTML: .drow .wslot 24×24
constexpr int kNameMaxW = 150;    // widened to fit longer CJK names;
                                   // old value 100 was clipping 队友B_长昵称…
constexpr int kMrW      = 48;     // "MR 247" — widened from 40 so the
                                   // leading 'M' never overlaps the
                                   // truncated name's ellipsis
constexpr int kDmgW     = 68;     // "999,999" — 6 digits + comma
constexpr int kDpsLblW  = 24;     // "DPS"
constexpr int kDpsValW  = 36;     // "999" — widened from 32 to keep
                                   // right edge aligned across rows
constexpr int kColGap   = 6;      // HTML: .drow gap:7px (rounded for pixel grid)

// Panel-level layout tokens.
constexpr int kPanelW    = 380;
constexpr int kMargin    = 11;
constexpr int kChartH    = 140;   // HTML: .chart height:140px
constexpr int kChartLeft = 36;    // widened from 28 so 3-char "184k" labels at 8px don't clip; HTML .ylabel 24px + ~12px padding
constexpr int kShareBarH = 10;    // clean single-line bar (was 16)

constexpr int kMaxSamples = 900;

// The per-row palette and colourForRow() moved to
// ui/panel_damage_blocks.h in the v0.12 third wave: the share bar and
// the chart resolve a row's colour too, and they are shared with the
// split sub-windows, so one spelling now serves the row's contribution
// bar, the share segment and the chart polyline alike.
using mhw::damage_block::colorForRow;

// Draws the 1px accent stripe + dark cell behind the dps value when
// the row is self. HTML: .drow[data-self="true"] gets a purple
// 1px border + inset box-shadow that reads as a thin glowing frame.
void drawSelfFrame(QPainter &p, const QRectF &row, const QColor &accent)
{
    p.setPen(QPen(accent, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(row.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    // Inner glow: 1px inset border at very low alpha.
    QColor glow = accent;
    glow.setAlphaF(0.30);
    p.setPen(QPen(glow, 1));
    p.drawRoundedRect(row.adjusted(2.0, 2.0, -2.0, -2.0), 2, 2);
}

// Draws the weapon icon slot (HTML: 24×24 .wslot with var(--cell-hi)
// background + var(--line-hi) border + 18×18 icon centred inside).
// `active` is unused for damage rows but kept for parity with player
// panel slot helper.
void drawIconSlot(QPainter &p, int x, int y, const QPixmap &icon)
{
    constexpr int slotSz = kIconSize + 2;
    const QRectF slot(x, y, slotSz, slotSz);
    p.setPen(QPen(QColor(58, 61, 63), 1));          // --line-hi
    p.setBrush(QColor(31, 34, 37));                  // --cell-hi
    p.drawRoundedRect(slot, 3, 3);
    if (!icon.isNull())
        p.drawPixmap(x + 1, y + 1, icon);
}

// Draws the contribution background bar (HTML: .drow .contrib
// `background:linear-gradient(90deg,var(--dc),transparent)`
// opacity:.16; width = % of max damage). Sits behind the row's text.
void drawContribBar(QPainter &p, const QRectF &row, float pct,
                    const QColor &dc)
{
    // Damage row = dark translucent track + coloured progress fill.
    // The unfilled right side remains deep black; the filled area fades
    // left-to-right from the player's colour into a restrained tint.
    // This is the HTML v8 .drow/.contrib idea expressed as an actual
    // progress track instead of a full-row haze.
    const float clamped = std::clamp(pct, 0.0F, 1.0F);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(9, 11, 13, 185));            // deep row track
    p.drawRoundedRect(row, 3.0, 3.0);

    if (clamped <= 0.0F)
        return;

    const QRectF fill(row.left(), row.top(), row.width() * clamped, row.height());
    // Do not cut sharply at the damage endpoint. Extend a small tail into
    // the unfilled track and fade it to transparent; the solid part still
    // ends at `filledWidth`, while the eye gets a natural transition.
    const double filledWidth = fill.width();
    const double fadeWidth = std::min(28.0, std::max(0.0, row.width() - filledWidth));
    const QRectF colourExtent(row.left(), row.top(),
                              filledWidth + fadeWidth, row.height());
    QLinearGradient gradient(colourExtent.topLeft(), colourExtent.topRight());
    QColor left = dc;
    left.setAlpha(125);
    QColor atProgress = dc;
    atProgress.setAlpha(fadeWidth > 0.0 ? 70 : 35);
    const double progressStop = colourExtent.width() > 0.0
        ? filledWidth / colourExtent.width() : 1.0;
    gradient.setColorAt(0.0, left);
    gradient.setColorAt(progressStop, atProgress);
    if (fadeWidth > 0.0) {
        QColor tail = dc;
        tail.setAlpha(0);
        gradient.setColorAt(1.0, tail);
    }
    p.setBrush(gradient);
    p.drawRoundedRect(colourExtent, 3.0, 3.0);
}

} // namespace

// i18n lookup. Moved into ui/panel_damage_blocks.h in the v0.12 third
// wave, because the share bar and the chart it was only used by are now
// shared with the split sub-windows. See panel_damage_blocks.h for why
// the drawing code was hoisted rather than duplicated.

DamagePanel::DamagePanel(QWidget *parent)
    : Panel(QStringLiteral("dps"), Corner::TopRight, parent),
      m_vm(this)
{
    // The ViewModel's "data changed" signal replaces the five
    // canvas()->update() calls the statistic used to make itself.
    connect(&m_vm, &mhw::DamageViewModel::changed,
            this, [this] { QWidget::update(); });
    setWindowTitle(mh::tr("ui.damage_title"));
}

void DamagePanel::retranslateUi()
{
    // Cached strings only: title (ctor) + the demo party names seeded by
    // setupDemoData(). Live rows come from the snapshot, whose names are
    // re-read from the reader every poll.
    setWindowTitle(mh::tr("ui.damage_title"));
    if (editMode())
        resetDemoPrimed();
    triggerUpdate();
}

void DamagePanel::setRiseDisplayOptions(
    const mhw::RiseDamageDisplayOptions &options)
{
    m_vm.setRiseDisplayOptions(options);
}

void DamagePanel::updateRiseDamage(
    const mhw::RiseDamageSnapshot &dmg,
    const mhw::RiseDamageDisplayOptions &options)
{
    m_vm.updateRise(dmg, options);
}

void DamagePanel::updateRiseDamage(const mhw::RiseDamageSnapshot &dmg)
{
    m_vm.updateRise(dmg);
}

void DamagePanel::update(const mhw::GameSnapshot &snap)
{
    m_vm.updateWorld(snap);
}

void DamagePanel::paintPanel(QPainter &p)
{
    // 没有可用数据时整块不画：主循环依据 hasVisibleContent() 决定是否挂载，
    // Rise 与 World 同一口径。这里保留早退，防止编辑/预览路径直接调用
    // paintPanel 时画出空框。
    const auto &vm = m_vm;
    if (!vm.hasData())
        return;

    drawV03Chrome(p, Panel::Accent::Damage);

    const int n = vm.rowCount();
    // ---- Section mask (ui/panel_sections.h) ----
    const uint32_t smask   = sectionMask();
    const bool onRows     = smask & mhw::DamageSection::Rows;
    const bool onShare    = smask & mhw::DamageSection::Share;
    const bool onChart    = smask & mhw::DamageSection::Chart;

    // HTML layout: title row + POLL chip row + N×29px drow + chart 140px
    // + the 9px margin-top before the chart.
    const int rowsAreaH = onRows ? (n * kRowH + (n > 0 ? (n - 1) * kRowGap : 0)) : 0;
    const int totalH = kMargin + 14                  // title row
                      + 9                          // gap below title
                      + rowsAreaH
                      + (onShare && n > 0 ? 9 : 0)            // gap above share bar
                      + (onShare && n > 0 ? kShareBarH - 2 : 0)// share bar itself
                      + (onShare && n > 0 ? 4 : 0)            // gap before chart
                      + (onChart ? kChartH : 0)
                      + kMargin;
    setContentSize(kPanelW, totalH);

    // --- Title row: "伤害统计 DAMAGE" + (optional) quest timer right ---
    p.setPen(QColor(255, 255, 255));
    QFont hdrFont(QStringLiteral("Chakra Petch"), 9, QFont::Bold);
    hdrFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    p.setFont(hdrFont);
    const QRectF titleRect(kMargin, kMargin, kPanelW - 2 * kMargin, 14);
    p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
               mh::tr("ui.damage_header"));
    // Right-aligned quest timer (HTML spec: <i>任务计时 06:41</i>)
    // The elapsed value crosses a reader boundary, so a non-finite float
    // is possible: `Inf > 0` passes the gate and then casts to int as
    // platform-defined garbage (typically INT_MIN). One truncation feeds
    // both digits — the previous code truncated twice (once inside /60,
    // once before % 60) and could disagree with itself on negatives.
    const float elapsedSeconds = vm.lastElapsedSeconds();
    if (std::isfinite(elapsedSeconds) && elapsedSeconds > 0.0F) {
        const int totalSeconds = static_cast<int>(elapsedSeconds);
        const int mm = totalSeconds / 60;
        const int ss = totalSeconds % 60;
        p.setFont(QFont(QStringLiteral("Chakra Petch"), 8));
        p.setPen(QColor(150, 150, 150));
        p.drawText(titleRect, Qt::AlignRight | Qt::AlignVCenter,
                   mh::tr("ui.damage_quest_timer")
                       .arg(mm, 2, 10, QChar('0'))
                       .arg(ss, 2, 10, QChar('0')));
    }

    // --- Player rows ---
    int y = kMargin + 14 + 9;     // below title + gap

    // HunterPie V2 contribution rule: each row width is its share of
    // total party damage, so all player contributions sum to 100%.
    // This deliberately differs from the chart's max-DPS axis below.
    qint64 partyDamage = 0;
    if (!vm.history().isEmpty()) {
        for (int i = 0; i < n; ++i)
            partyDamage += vm.history().last().damage.value(i, 0);
    }
    if (partyDamage == 0)
        partyDamage = 1;

    // The whole row loop only runs when Rows is on. When it is off the
    // layout must still advance past the reserved strip (which the
    // height model zeroes, see the rowsAreaH computation above), so the
    // share bar and the chart land exactly where they would have sat
    // with rows present. Previously a bare `break` left y sitting right
    // under the title, and with Rows and Share both off the chart
    // overwrote the title row's right-hand side.
    if (onRows) {
        for (int i = 0; i < n; ++i) {
            const int dmg = vm.history().isEmpty() ? 0 : vm.history().last().damage.value(i, 0);
        // The demo early-path sets kDemoDps[] below to match the user's
        // realistic MHW DPS range (~300 down to ~100). Real updates use
        // computeDps() from history_.
        //
        // The gate is editMode(), NOT the history length: a real hunt
        // also has 8 or fewer samples during its first two seconds
        // (0.25 s per poll), which used to show 311/240/177/101 on the
        // live rows of every fresh quest.
        int dps = vm.computeDps(i);
        if (editMode() && vm.history().size() <= 8 && i < 4) {
            static const int kDemoDps[4] = {311, 240, 177, 101};
            dps = kDemoDps[i];
        }

        const QRectF row(kMargin, y, kPanelW - 2 * kMargin, kRowH);

        // Cell background (HTML: --cell #1d2022) + border (--line).
        p.setPen(QPen(QColor(42, 45, 47), 1));        // --line
        p.setBrush(QColor(29, 32, 34));                // --bg-cell
        p.drawRoundedRect(row, 3, 3);

        const QColor dc = colorForRow(vm.partySlots().value(i, i),
                                  vm.locals().value(i, false));
        const bool isSelf = vm.locals().value(i, false);   // HunterPie name match

        // Contribution gradient bar behind everything else.
        drawContribBar(p, row,
                       static_cast<float>(dmg) / static_cast<float>(partyDamage), dc);

        // Weapon icon slot.
        // An unresolved weaponId (<0 or >13) is rendered as the explicit
        // unknown-weapon placeholder instead of an empty slot. The
        // blank-slot bug this replaces: a Rise ally row whose weaponId
        // never resolved drew a featureless dark box, indistinguishable
        // from "the icon failed to load" and from "this row is still
        // loading". Now the user sees a grey "?" and knows the panel
        // has no weapon data for this row.
        QPixmap wp;
        const int weaponId = vm.weaponIds().value(i, -1);
        if (weaponId >= 0 && weaponId <= 13)
            wp = Icon::render(Icon::weaponPath(weaponId, 1), kIconSize);
        else
            wp = Icon::renderUnknownWeapon(kIconSize);
        drawIconSlot(p, static_cast<int>(row.left()) + 4,
                     static_cast<int>(row.top()) + 3, wp);

        // --- Column layout (left-to-right, matches HTML flex order) ---
        // icon | name | mr | dmg | dpsLbl | dpsVal
        // All columns are right-anchored from row.right() so they
        // stay aligned no matter how wide the player names get.
        const int rLeft   = static_cast<int>(row.left());
        const int rRight  = static_cast<int>(row.right());
        const int iconX   = rLeft + 4;
        const int nameX   = iconX + kIconSize + kColGap;       // icon | name
        const int dpsValR = rRight - 4;
        const int dpsLblR = dpsValR - kDpsValW - 2;            // dpsVal | dpsLbl
        const int dmgR    = dpsLblR - kDpsLblW - kColGap;       // dmg | dpsLbl
        const int mrR     = dmgR - kDmgW - kColGap;              // mr | dmg
        // name area runs from nameX up to mrR (with colGap on each side).
        const int nameW = mrR - kColGap - nameX;
        // Clamp name area to kNameMaxW so long names get ellipsized
        // instead of pushing into MR/dmg columns.
        const int nameClipW = std::min(nameW, kNameMaxW);
        const int nameDrawX = std::min(nameX, mrR - kColGap - nameClipW);

        // Weapon icon (drawn before text columns so columns position around
        // the icon). drawIconSlot already rendered wp at iconX in the
        // layout block above; nothing more to do here.

        // Name (HTML: .drow .nm 11px Medium, --dc). Elipsized at ~8 CJK
        // chars to fit the column without overlapping MR.
        // Medium weight here matches the design intent — Bold in Qt 6
        // on Linux falls back to the next-heavier face when the
        // requested weight isn't installed, which reads as "too chunky".
        p.setPen(dc);
        QFont nmFont(QStringLiteral("Chakra Petch"), 11, QFont::DemiBold);
        nmFont.setStyleStrategy(QFont::PreferAntialias);
        nmFont.setLetterSpacing(QFont::AbsoluteSpacing, 0);
        p.setFont(nmFont);
        const QFontMetrics nmFm(nmFont);
        const QString rawName = vm.names().value(i);
        const QString nameShown = nmFm.elidedText(rawName, Qt::ElideRight, nameClipW);
        p.drawText(QRectF(nameDrawX, row.top(), nameClipW, row.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, nameShown);

        // MR (HTML: .mr 9px --t3).
        p.setPen(QColor(90, 93, 95));                 // --t3
        QFont mrFont(QStringLiteral("Chakra Petch"), 9);
        mrFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(mrFont);
        const QString mrStr = QStringLiteral("MR %1").arg(vm.masterRanks().value(i));
        const QFontMetrics mrFm(mrFont);
        // Right-align within its kMrW column.
        const int mrW = std::min(mrFm.horizontalAdvance(mrStr), kMrW);
        p.drawText(QRectF(mrR - mrW, row.top(), mrW, row.height()),
                   Qt::AlignRight | Qt::AlignVCenter, mrStr);

        // Total damage (HTML: .drow .dmg 11px Medium, --t1).
        const QString dmgStr = mhw::groupNumber(dmg);
        p.setPen(QColor(245, 246, 247));              // --t1
        QFont dmgFont(QStringLiteral("Chakra Petch"), 11, QFont::DemiBold);
        dmgFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(dmgFont);
        const QFontMetrics dmgFm(dmgFont);
        const int dmgW = std::min(dmgFm.horizontalAdvance(dmgStr), kDmgW);
        p.drawText(QRectF(dmgR - dmgW, row.top(), dmgW, row.height()),
                   Qt::AlignRight | Qt::AlignVCenter, dmgStr);

        // DPS value (HTML: .drow .dps b --t1, 11px Medium) — right-aligned
        // in its kDpsValW column.
        p.setPen(QColor(245, 246, 247));
        p.setFont(dmgFont);
        const QFontMetrics dpsFm(dmgFont);
        const QString dpsStr = QString::number(dps);
        const int dpsValW = std::min(dpsFm.horizontalAdvance(dpsStr), kDpsValW);
        p.drawText(QRectF(dpsValR - dpsValW, row.top(), dpsValW, row.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, dpsStr);

        // DPS label (HTML: .drow .dps 9px --t3) — placed LEFT of the value.
        p.setPen(QColor(90, 93, 95));                 // --t3
        QFont dpsFont(QStringLiteral("Chakra Petch"), 9);
        dpsFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(dpsFont);
        const QFontMetrics lblFm(dpsFont);
        const int lblW = std::min(lblFm.horizontalAdvance(QStringLiteral("DPS")), kDpsLblW);
        p.drawText(QRectF(dpsLblR - lblW, row.top(), lblW, row.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("DPS"));

        // Self row: purple inset border + glow.
        if (isSelf)
            drawSelfFrame(p, row, dc);

        y += kRowH + kRowGap;
        }
    }
    // Trim the trailing inter-row gap after the last row so the running
    // y matches the height reservation above (rowsAreaH counts n-1 gaps,
    // not n). Same class of bug PR C fixed in panel_monster.
    if (onRows && n > 0)
        y -= kRowGap;

    // --- Share bar (clean single-row, full-width, 4 colour segments) ---
    // Gap above shared bar: 9px (HTML .chart margin), share bar 8px,
    // gap before chart: 6px.
    if (onShare && n > 0) {
        y += 9;
        const QRectF shareRect(kMargin, y, kPanelW - 2 * kMargin, kShareBarH - 2);
        // v0.12 third wave: the bar's own body is shared with
        // DamageShareStripPanel (ui/panel_damage_blocks.h). The model
        // and the draw are one call apart instead of a private member
        // reading this class's own m_vm, so the panel and the window
        // can never diverge about which segment is which colour.
        drawDamageShareBar(p, shareRect, buildDamageShareModel(vm));
        y += kShareBarH + 4;
    }

    // --- Line chart ---
    if (onChart)
        drawDamageChart(p, QRectF(kMargin, y, kPanelW - 2 * kMargin, kChartH),
                        buildDamageChartModel(vm));
}

// The share bar's and the chart's bodies moved to ui/panel_damage_blocks.h
// in the v0.12 third wave, shared verbatim with DamageShareStripPanel and
// DamageChartStripPanel. They were the two blocks of this panel whose
// painting had already been extracted into one-caller member functions
// with no access to any other member, which is exactly the shape that can
// be hoisted without a body edit — see that header's comment for the
// line-by-line equivalence and the reason a hoist rather than a copy.

void DamagePanel::setupDemoData()
{
    // Edit-mode demo: the identity rows and the synthetic 8-sample history
    // live in the ViewModel now (DamageViewModel::seedDemoData). The View
    // only resolves the localized labels — the model receives plain data so
    // it never needs a string table. The damage values, the tick spacing and
    // the seeded quest timer are unchanged from the pre-MVVM version, so the
    // demo rendering is byte-identical.
    QVector<mhw::DamageViewModel::DemoRow> demoParty;
    // NOT static: the demo labels are localized, so the table must be
    // rebuilt on every seed (a function-local static would freeze the first
    // locale for the process lifetime — see DamagePanel::retranslateUi).
    demoParty.append({QStringLiteral("A27exe"), 0, 247, 0});
    demoParty.append({mh::tr("data.demo.party.a"), 1, 500, 1});
    demoParty.append({mh::tr("data.demo.party.b"), 12, 300, 2});
    demoParty.append({mh::tr("data.demo.party.c"), 4, 250, 3});
    // 6:51 (matches HTML mockup) — seeds the title-row quest timer in
    // edit mode, where there is no real game to read snap.quest from.
    m_vm.seedDemoData(demoParty, 411.0F);
}
