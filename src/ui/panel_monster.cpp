#include "panel_monster.h"

#include "core/string_table.h"
#include "monster/monster_types.h"
#include "ui/formatters.h"
#include "ui/icon.h"
#include "ui/panel_sections.h"

#include <QColor>
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
// mhw-overlay-concept (v8) HTML (assets/mhw-overlay-concept 副本.html).
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
//   amber≤capture≤50% → amber  (limping soon)
//   ≤ capture%  → red         (capturable per game data; 0 = never)
//
// `capturePct` is read from MonsterData.xml <Monster Capture=N>; each
// monster has its own threshold (火龙=20, 飞雷龙=20, 黑轰=25, 银火=15,
// 绚辉龙=10, …) so the red colour appears at the exact moment the
// in-game capture flag flips.
QColor healthColor(float pct, int monsterId)
{
    int cap = mhw::kMonsterCaptureThresholds.value(monsterId, 0);
    // 0 means uncapturable (Elder Dragons / 黑死病); keep amber/green
    // for all values, never drop to red.
    const float capturePct = (cap > 0)
        ? static_cast<float>(cap) / 100.0F
        : 1.0F;
    if (pct > kHpAmberPct)   return QColor(kHpHighR, kHpHighG, kHpHighB);
    if (pct > capturePct)    return QColor(kHpMidR,  kHpMidG,  kHpMidB);
    return                        QColor(kHpLowR,  kHpLowG,  kHpLowB);
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

struct PcEntry {
    QString name;          // 头 / 左翼 / 右翼 / 尾巴 / 左脚 / 右脚
    QString tag;           // empty / "破" / "斩"
    QString tagKind;       // "" / "brk" / "sev"
    int     counter{0};    // HunterPie raw counter
    bool    broken{false}; // true if part has been broken/severed at least once
    float   pct{0.0F};     // 0..1 (solo: per-part HP, multiplayer: monster total HP)
    // v0.7.4 PR C: per-part tenderize. When tenderizeDuration > 0 the
    // card renders a small amber strip showing the remaining seconds
    // and a fill bar driven by duration / tenderizeMaxDuration.
    float   tenderizeDuration{0.0F};
    float   tenderizeMaxDuration{0.0F};
};

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
    p.drawText(QRectF(cell.x() + kPcPadX, pnY,
                      cell.width() / 2 - kPcPadX, pnH),
               Qt::AlignLeft | Qt::AlignVCenter, e.name);

    if (!e.tag.isEmpty()) {
        // When the part has been broken at least once we suffix the
        // chip with the counter so the row communicates "broken N
        // times" rather than just "broken" (HunterPie V2 doesn't show
        // the counter in the chip, but for non-host members that's
        // the only signal we have about the part's state).
        const QString tagText = e.counter > 0
            ? QStringLiteral("%1 %2").arg(e.tag).arg(e.counter)
            : e.tag;
        QFont tagFont(QStringLiteral("Chakra Petch"),
                      kPcTagFont, QFont::Bold);
        tagFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(tagFont);
        const QFontMetrics tFm(tagFont);
        const int tagW = tFm.horizontalAdvance(tagText) + 2 * kPcTagPadX;
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
                   Qt::AlignCenter, tagText);
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
        p.setBrush(QColor(120, 144, 156));   // #78909c
        p.drawRect(miniRect.x(), miniY,
                   miniRect.width() * clamped, miniRect.height());
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
    if (e.tenderizeDuration > 0.0F) {
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

namespace mh {
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

MonsterPanel::MonsterPanel(QWidget *parent)
    : Panel(QStringLiteral("monster"), Corner::TopRight, parent)
{
    setWindowTitle(mh::tr("ui.monster_title"));

    pulseTimer_.setInterval(80);
    connect(&pulseTimer_, &QTimer::timeout, this, &MonsterPanel::onEnragePulseTick);
}

void MonsterPanel::update(const mhw::MonsterSnapshot &m)
{
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
    if (!hasData_)
        return;

    drawV03Chrome(p, Panel::Accent::Monster);

    // ---- Build status card entries from ailments (mirrors .sc ordering) ----
    QVector<ScEntry> scList;
    for (const auto &a : monster_.ailments) {
        // Hide cards that are not actually affecting the monster RIGHT
        // NOW. Previously `counter > 0` kept the card alive forever —
        // once an ailment ever triggered (e.g. drool flinch) it stayed
        // on the panel at "0s" for the whole fight. HunterPie's
        // auto-hide semantics: hide when no timer is running and no
        // build-up is accumulating; the historical counter alone must
        // not pin the card.
        const bool timerRunning = a.active && a.timer > 0.0F;
        const bool buildingUp  = a.buildup > 0.0F;
        if (!timerRunning && !buildingUp)
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
        e.active = timerRunning || buildingUp;

        // Mini progress + timer text. Order matches the HTML examples:
        //   active + maxTimer     → "Ns" (countdown)
        //   else + maxBuildup     → "N%" (buildup fill)
        if (timerRunning && a.maxTimer > 0.0F) {
            // HunterPie exposes the remaining seconds directly; round UP
            // so a 0.4s tail still shows "1s" instead of a sticky "0s"
            // (the card disappears the moment timer hits 0 anyway).
            e.pct = std::clamp(a.timer / a.maxTimer, 0.0F, 1.0F);
            e.tm  = QStringLiteral("%1s").arg(static_cast<int>(std::ceil(a.timer)));
        } else if (a.maxBuildup > 0.0F) {
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
    // v0.7.4 PR C: per-part tenderize strip is always drawn when active,
    // regardless of the section toggle. Kept for backwards compat with
    // MonsterSection::Tenderize (still respected via MonsterPanel config
    // — see panel_sections.h); the on/off behaviour now lives on the
    // data side, not the layout side.
    const bool onTenderize = smask & mhw::MonsterSection::Tenderize;
    (void)onTenderize;

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
    const int rageH      = kRageBarH + 2 * kBarVPad;
    int scAreaH = 0;
    if (scCount > 0) {
        const int scRows = (scCount + kScCols - 1) / kScCols;
        constexpr int kScCellH = kScPadY + kScTopFont + 4 + kScMiniH + kScPadY;
        scAreaH = scRows * kScCellH + (scRows - 1) * kScGap;
    }
    int pcAreaH = 0;
    const int pcCount = monster_.parts.size();
    if (pcCount > 0) {
        // v0.7.4 PR C: cell height is dynamic — any part with an active
        // Clutch Claw tenderize (PartSnapshot.tenderizeDuration > 0)
        // inserts a kPcTnH+kPcTnGap strip between .pn and .mini. We
        // compute per-cell heights up front so the .pgrid rows stay
        // aligned with the tallest cell in each row.
        const int pcRows = (pcCount + kPcCols - 1) / kPcCols;
        constexpr int kPcBaseCellH = kPcPadY + kPcPnFont + 2 + kPcPnGap
                                    + kPcMiniH + kPcPadY;
        // S3 follow-up: label height is included in the kPcTnGap budget,
        // i.e. the strip+label sandwich occupies kPcTnLabelH + kPcTnH +
        // kPcTnGap vertical real estate (label sits in what used to be
        // the .pn→.mini gap, so it adds kPcTnLabelH + kPcTnH rather than
        // just kPcTnH).
        const int kPcTnExtra = kPcTnLabelH + kPcTnH + kPcTnGap;
        QVector<int> cellHeights(pcCount, kPcBaseCellH);
        for (int i = 0; i < pcCount; ++i) {
            if (monster_.parts[i].tenderizeDuration > 0.0F)
                cellHeights[i] += kPcTnExtra;
        }
        // Per-row max height → row height. Sum of (rowHeights + gaps).
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
    if (rageDrawn)               totalH += kRowGap + rageH;
    if (onAil && scCount > 0)    totalH += kRowGap + (rageDrawn ? kRowGap : 0) + scAreaH;
    // v0.7.4 PR C: per-part tenderize strip is rendered inside each
    // .pc card; the cell height for it is reserved in pcAreaH above.
    // No separate totalH delta is needed here.
    if (onParts && pcCount > 0)  totalH += kRowGap + pcAreaH;
    totalH += kPanelPad;
    setContentSize(kPanelWidth, totalH);

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
                   QStringLiteral("怪物 MONSTER"));
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
    drawHex(p, hexCell, mhw::Icon::monsterPath(monster_.id));

    // .mtitle sits in the right column of the flex row.
    constexpr int kNmFont = 15;
    QFont nmFont(QStringLiteral("Chakra Petch"), kNmFont, QFont::Bold);
    nmFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    nmFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(nmFont);
    p.setPen(QColor(245, 246, 247));
    const QString nm = monster_.internalName.isEmpty()
        ? QStringLiteral("Monster %1").arg(monster_.id)
        : monster_.internalName;
    const int nmY = y + 4;
    const int nmH = kNmFont + 2;
    int nmX = static_cast<int>(hexCell.right()) + 10;

    // .crownmini (15×15). Thresholds and tint colours come from HunterPie.
    constexpr std::array<float, 3> kDefaultCrown = {0.90F, 1.15F, 1.23F};
    const auto thr = mhw::kCrownThresholds.value(monster_.id, kDefaultCrown);
    if (monster_.size > 0.0F && thr[2] > 0.0F) {
        QString crownPath;
        QColor  crownTint;
        if      (monster_.size >= thr[2]) { crownPath = QStringLiteral(":/icons/crowns/crown_king.svg");  crownTint = QColor(231, 197,   7); }
        else if (monster_.size >= thr[1]) { crownPath = QStringLiteral(":/icons/crowns/crown_large.svg"); crownTint = QColor(189, 189, 189); }
        else if (monster_.size <= thr[0]) { crownPath = QStringLiteral(":/icons/crowns/crown_mini.svg");  crownTint = QColor( 33, 150, 243); }
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
        const QString enTxt = QStringLiteral("激怒 %1s").arg(elapsed);
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
        const QString enTxt = QStringLiteral("激怒 %1%")
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
    const QColor hpC = healthColor(hpPct, monster_.id);
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
    // Solo: per-part HP + counter.
    // Multiplayer: part HP cannot be read, so the mini bar carries the
    // total monster HP percentage and the tag chip shows the cumulative
    // break / sever count once a part has been touched.
    // (v0.7.4 PR B: totalPct removed — was never consumed in either
    // solo or multiplayer path; multiplayer stale-HP gating happens via
    // p.health/p.maxHealth on each entry below.)
    QVector<PcEntry> pcList;
    pcList.reserve(monster_.parts.size());
    for (const auto &p : monster_.parts) {
        PcEntry e;
        e.name = p.name.isEmpty()
            ? QStringLiteral("部位 %1").arg(p.index)
            : p.name;
        e.counter = p.counter;
        e.broken = (p.counter > 0)
                   || (p.maxHealth > 0.0F && p.health <= 0.0F);
        if (p.isBreakable && p.counter > 0)
            { e.tag = QStringLiteral("破"); e.tagKind = QStringLiteral("brk"); }
        else if (p.isSeverable && p.counter > 0)
            { e.tag = QStringLiteral("斩"); e.tagKind = QStringLiteral("sev"); }
        else if (p.isBreakable)
            { e.tag = QStringLiteral("破"); e.tagKind = QStringLiteral("brk"); }
        else if (p.isSeverable)
            { e.tag = QStringLiteral("斩"); e.tagKind = QStringLiteral("sev"); }
        // v0.7.4 PR C: per-part tenderize values feed the new strip
        // drawn inside each .pc card. The struct fields are 0 by default
        // (no active tenderize), so we only need to copy when nonzero.
        e.tenderizeDuration    = p.tenderizeDuration;
        e.tenderizeMaxDuration = p.tenderizeMaxDuration;
        // v0.7.4 PR C: pick the right HP pair per PartType.
        //   - Severable: only Health/MaxHealth is meaningful (HunterPie
        //     UpdateSeverableData leaves Flinch untouched).
        //   - Breakable: Health/MaxHealth is the cumulative threshold
        //     progress (UpdateBreakableData); Flinch/MaxFlinch is the
        //     current layer's raw value (less useful on the main bar).
        //   - Flinch:    only Flinch/MaxFlinch is meaningful (no
        //     thresholds, not severable). This is the path that fixes
        //     the "脏数据" complaint — body/leg parts now show real
        //     flinch bar values instead of the broken double-filled
        //     health/flinch pair.
        const float mHP = (p.partType == mhw::PartType::Flinch)
                          ? p.maxFlinch : p.maxHealth;
        const float cHP = (p.partType == mhw::PartType::Flinch)
                          ? p.flinch   : p.health;
        if (multiplayer_) {
            const bool staleFullHp =
                mHP > 0.0F && cHP >= mHP
                && p.counter == 0 && !p.isBroken;
            if (staleFullHp) {
                e.pct = 0.0F;
                e.tag.clear();
                e.tagKind.clear();
            } else {
                e.pct = (mHP > 0.0F)
                    ? std::clamp(cHP / mHP, 0.0F, 1.0F)
                    : 0.0F;
            }
        } else {
            e.pct = (mHP > 0.0F)
                ? std::clamp(cHP / mHP, 0.0F, 1.0F)
                : 0.0F;
        }
        pcList.append(e);
    }
    if (onParts && !pcList.isEmpty()) {
        y += kRowGap;
        constexpr int kPcBaseCellH = kPcPadY + kPcPnFont + 2 + kPcPnGap
                                    + kPcMiniH + kPcPadY;
        // S3 follow-up: matches the reservation formula above (label +
        // bar + gap stacked between .pn and .mini).
        const int kPcTnExtra = kPcTnLabelH + kPcTnH + kPcTnGap;
        // Reuse the per-cell heights from above (where pcAreaH was
        // computed) so the .pgrid render stays aligned with the panel
        // height reservation. We recompute here because pcList is built
        // from monster_.parts and the heights are 1:1.
        QVector<int> cellHeights(pcList.size(), kPcBaseCellH);
        for (int i = 0; i < pcList.size(); ++i) {
            if (pcList[i].tenderizeDuration > 0.0F)
                cellHeights[i] += kPcTnExtra;
        }
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
    m.internalName = QStringLiteral("火龙");
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
    struct Ail { int id; const char *name; float timer; float maxT;
                float bu; float maxB; int cnt; bool active; };
    Ail demoAil[] = {
        { 2, "\u9ebb\u75f9",   12.0F, 20.0F,   0.0F,   0.0F, 1, true },
        {14, "\u9ebb\u75f9\u9677\u9631",  8.0F,  8.0F,   0.0F,   0.0F, 0, true },
        {15, "\u843d\u7a74",  60.0F, 60.0F,   0.0F,   0.0F, 0, true },
        { 1, "\u6bd2",         0.0F,  0.0F,  30.0F, 100.0F, 2, false},
    };
    for (const auto &a : demoAil) {
        MonsterAilmentSnapshot ail;
        ail.id = a.id;
        ail.name = QString::fromUtf8(a.name);
        ail.active = a.active;
        ail.timer = a.timer;
        ail.maxTimer = a.maxT;
        ail.buildup = a.bu;
        ail.maxBuildup = a.maxB;
        ail.counter = a.cnt;
        m.ailments.append(ail);
    }

    // Demo parts — head/左右翼/尾巴/左右脚, mirrors HTML v8 .pgrid.
    struct DPart { int idx; const char *name;
                   float hp; float maxHp;
                   int counter; bool breakable; bool severable; };
    const DPart dparts[] = {
        { 0, "头",   80.0F, 100.0F, 0, true,  false},
        { 1, "左翼", 45.0F, 100.0F, 1, true,  true },
        { 2, "右翼", 20.0F, 100.0F, 2, true,  true },
        { 3, "尾巴", 90.0F, 100.0F, 0, false, true },
        { 4, "左脚", 60.0F, 100.0F, 0, false, false},
        { 5, "右脚", 35.0F, 100.0F, 0, false, false},
    };
    for (const auto &dp : dparts) {
        PartSnapshot ps;
        ps.index = dp.idx;
        ps.name = QString::fromUtf8(dp.name);
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