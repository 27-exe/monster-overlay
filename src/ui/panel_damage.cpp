#include "panel_damage.h"

#include "core/string_table.h"
#include "player/player_types.h"
#include "quest/quest_types.h"
#include "ui/formatters.h"
#include "ui/icon.h"
#include "ui/panel_sections.h"

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
constexpr int kMaxPlayers = 4;

// HunterPie default damage meter colors (PlayerConfigHelper.cs):
// slot 0 = #F24891 (pink), slot 1 = #50C5B7 (teal),
// slot 2 = #49CFF5 (sky blue), slot 3 = #FF8040 (orange).
// Self override = #A74FFF (purple) when isSelf.
const QColor kPlayerColors[] = {
    QColor(0xF2, 0x48, 0x91),  // slot 0 — pink
    QColor(0x50, 0xC5, 0xB7),  // slot 1 — teal
    QColor(0x49, 0xCF, 0xF5),  // slot 2 — sky blue
    QColor(0xFF, 0x80, 0x40),  // slot 3 — orange
};

// Returns the player colour for a row: purple for self, otherwise the
// slot's normal hue. Mirrors the HTML `style="--dc:#a74fff"` for self.
QColor colorForRow(int slot, bool isSelf)
{
    if (isSelf) return QColor(0xA7, 0x4F, 0xFF);  // --dc self
    return kPlayerColors[(slot % kMaxPlayers + kMaxPlayers) % kMaxPlayers];
}

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

namespace mh {
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

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
    if (vm.lastElapsedSeconds() > 0.0F) {
        const int mm = static_cast<int>(vm.lastElapsedSeconds() / 60);
        const int ss = static_cast<int>(vm.lastElapsedSeconds()) % 60;
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

    for (int i = 0; i < n; ++i) {
        if (!onRows) break;   // skip row drawing; y not advanced
        const int dmg = vm.history().isEmpty() ? 0 : vm.history().last().damage.value(i, 0);
        // The demo early-path sets kDemoDps[] below to match the user's
        // realistic MHW DPS range (~300 down to ~100). Real updates use
        // computeDps() from history_.
        int dps = vm.computeDps(i);
        if (vm.history().size() <= 8 && i < 4) {
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
        QPixmap wp;
        if (vm.weaponIds().value(i, -1) >= 0)
            wp = Icon::render(Icon::weaponPath(vm.weaponIds()[i], 1), kIconSize);
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
        drawShareBar(p, shareRect);
        y += kShareBarH + 4;
    }

    // --- Line chart ---
    if (onChart)
        drawChart(p, QRectF(kMargin, y, kPanelW - 2 * kMargin, kChartH));
}

void DamagePanel::drawShareBar(QPainter &p, const QRectF &barRect)
{
    const auto &vm = m_vm;
    const int n = vm.rowCount();
    if (n == 0) return;

    qint64 total = 0;
    QVector<int> dmgs(n, 0);
    if (!vm.history().isEmpty()) {
        const auto &last = vm.history().last();
        for (int i = 0; i < n && i < last.damage.size(); ++i) {
            dmgs[i] = last.damage[i];
            total += dmgs[i];
        }
    }
    if (total == 0) total = 1;

    // Invisible track — dark grey, no rounded corners.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(42, 45, 47));          // --line, same as panel chrome
    p.drawRect(barRect);

    // Four colored segments, flat abutted edges, no gaps, no text.
    int xCursor = static_cast<int>(barRect.x());
    const int trackRight = static_cast<int>(barRect.right());
    for (int i = 0; i < n; ++i) {
        if (dmgs[i] <= 0) continue;
        const int segW = std::max(1,
            static_cast<int>(barRect.width() * static_cast<double>(dmgs[i])
                              / static_cast<double>(total)));
        if (xCursor >= trackRight) break;
        const int segWidth = std::min(segW, trackRight - xCursor);
        const QRectF seg(xCursor, barRect.y(), segWidth, barRect.height());
        const QColor segColor = colorForRow(vm.partySlots().value(i, i),
                                             vm.locals().value(i, false));
        p.setBrush(segColor);
        p.drawRect(seg);
        xCursor += segWidth;
    }
}

void DamagePanel::drawChart(QPainter &p, const QRectF &chartRect)
{
    // HTML: .chart {background:rgba(30,30,30,.6); border:1px solid --line;}
    // 2026-07-27: dropped chart alpha 153 → 100 → 65 → 35 → 15 (~6%) to
    // match the translucent panel body and let the wallpaper show.
    p.setPen(QPen(QColor(42, 45, 47), 1));
    p.setBrush(QColor(30, 30, 30, 15));
    p.drawRoundedRect(chartRect, 4, 4);

    const auto &vm = m_vm;
    const int n = vm.rowCount();
    if (vm.history().size() < 2 || n == 0) {
        p.setPen(QColor(150, 150, 150));
        p.setFont(QFont(QStringLiteral("Chakra Petch"), 8));
        p.drawText(chartRect, Qt::AlignCenter, mh::tr("ui.damage_chart_waiting"));
        return;
    }

    // Compute global max across history.
    int maxDmg = 0;
    for (const auto &s : vm.history())
        for (int i = 0; i < s.damage.size() && i < n; ++i)
            maxDmg = std::max(maxDmg, s.damage[i]);
    if (maxDmg == 0) maxDmg = 1;

    const int firstTick = vm.history().first().tick;
    const int lastTick  = vm.history().last().tick;
    const int tickSpan  = lastTick - firstTick;
    if (tickSpan <= 0) return;

    // Y-axis area: leave room for the 24px label column on the left
    // (HTML: .chart .ylabel 24px wide, .chart svg starts at left:28).
    const QRectF plotRect(chartRect.left() + kChartLeft,
                          chartRect.top() + 2,
                          chartRect.width() - kChartLeft - 4,
                          chartRect.height() - 4);

    // y-grid (HTML: .chart .ygrid i.t/i.m, two faint horizontal lines).
    p.setPen(QPen(QColor(255, 255, 255, 10), 1));
    p.drawLine(plotRect.left(),  plotRect.top(),
               plotRect.right(), plotRect.top());
    p.drawLine(plotRect.left(),  plotRect.top() + plotRect.height() / 2,
               plotRect.right(), plotRect.top() + plotRect.height() / 2);

    // Y-axis labels (HTML: 400 / 200 / 0). Round the global max up
    // to a nice number for the top label so it reads as a real scale.
    auto roundUp = [](int v) -> int {
        int scale = 100;
        while (scale < v) scale *= 2;
        return std::max(scale, v);
    };
    const int yMax = roundUp(maxDmg);
    const int yMid = yMax / 2;
    QFont yFont(QStringLiteral("Chakra Petch"), 8);
    yFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(yFont);

    // Compact formatter for y-axis labels (HTML .ylabel 8px, --t4 #505355):
    //   < 1,000       → "123"  (integer)
    //   1k–9,999      → "1k"   (integer k, always no decimal — looks
    //                          tighter than "1.2k" and matches HTML v8
    //                          visual rhythm at typical quest lengths)
    //   10k–999,999   → "184k" (integer k)
    auto fmtCompact = [](int v) -> QString {
        if (v < 1000) return QString::number(v);
        // 1000..999 → 1k..999k (rounded to nearest 1k).
        const int k = (v + 500) / 1000;
        return QString::number(k) + QLatin1Char('k');
    };
    // Right-anchored inside yLabelRect so wider "184k" never clips.
    const QRectF yLabelRect(chartRect.left() + 2, chartRect.top(),
                            kChartLeft - 6, chartRect.height());
    QFontMetrics yFm(yFont);
    const int labelH = yFm.height();
    auto drawRightAligned = [&](const QString &s, qreal topY) {
        const int textW = yFm.horizontalAdvance(s);
        // Higher-contrast direct label: the chart stays visually clean and
        // transparent, while brighter text survives non-black scenes.
        p.setPen(QColor(220, 224, 226, 245));
        p.drawText(QRectF(yLabelRect.right() - textW, topY,
                          textW, labelH),
                   Qt::AlignLeft | Qt::AlignVCenter, s);
    };
    const int top    = static_cast<int>(chartRect.top()) + 2;
    const int mid    = static_cast<int>(chartRect.top())
                     + static_cast<int>(chartRect.height()) / 2 - labelH / 2;
    const int bottom = static_cast<int>(chartRect.bottom()) - labelH - 2;
    drawRightAligned(fmtCompact(yMax), top);
    drawRightAligned(fmtCompact(yMid), mid);
    drawRightAligned(QStringLiteral("0"), bottom);

    // Polyline per player (HTML: 4 polylines, no fill, stroke-width 1.6).
    // Colour must match the row's colour exactly: the row reads
    // slots_/locals_ at draw time; the chart does the same here so the
    // line and the row header are visually linked.
    for (int pi = 0; pi < n; ++pi) {
        const QColor c = colorForRow(vm.partySlots().value(pi, pi),
                                      vm.locals().value(pi, false));
        p.setPen(QPen(c, 1.6));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        bool first = true;
        for (const auto &s : vm.history()) {
            const float x = plotRect.left()
                + static_cast<float>(s.tick - firstTick) / tickSpan * plotRect.width();
            const float d = static_cast<float>(s.damage.value(pi, 0));
            const float y = plotRect.bottom() - (d / yMax) * plotRect.height();
            if (first) { path.moveTo(x, y); first = false; }
            else       { path.lineTo(x, y); }
        }
        p.drawPath(path);
    }

    // No elapsed-time label here: the previous '+1s' demo marker was
    // neither a real x-axis nor useful gameplay information, and it
    // collided with the top y-axis value on transparent backgrounds.
}

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
