#include "panel_monster.h"

#include "core/string_table.h"
#include "monster/monster_types.h"
#include "ui/formatters.h"
#include "ui/icon.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace {

constexpr int kPanelWidth = 380;
constexpr int kTotalBarH = 28;
constexpr int kMargin = 10;
constexpr int kRowGap = 6;
constexpr int kPartBarH = 14;   // per-part mini bar inside a cell
constexpr int kPartRowH = 32;    // each row (label + mini-bar + pct) for 3-cell layout
constexpr int kCellGap = 8;
constexpr int kCols = 3;

QColor healthColor(float pct)
{
    if (pct > 0.6F) return QColor(76, 175, 80);    // green
    if (pct > 0.25F) return QColor(255, 193, 7);    // amber
    return QColor(244, 67, 54);                      // red
}

void drawBar(QPainter &p, const QRectF &rect, float pct, const QColor &fill)
{
    const float clamped = std::clamp(pct, 0.0F, 1.0F);
    // Background
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 40, 40, 180));
    p.drawRoundedRect(rect, 3, 3);
    // Fill
    if (clamped > 0.001F) {
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(rect.x(), rect.y(),
                                 rect.width() * clamped, rect.height()), 3, 3);
    }
    // Border
    p.setPen(QPen(QColor(100, 100, 100, 120), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect, 3, 3);
}

} // namespace

namespace mh {
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

MonsterPanel::MonsterPanel(QWidget *parent)
    : Panel(QStringLiteral("monster"), Corner::TopRight, parent)
{
    setWindowTitle(mh::tr("ui.monster_title"));
}

void MonsterPanel::update(const mhw::MonsterSnapshot &m)
{
    monster_ = m;
    hasData_ = (m.id >= 0);
    canvas()->update();
}

void MonsterPanel::paintPanel(QPainter &p)
{
    if (!hasData_)
        return;

    p.setPen(Qt::white);
    p.setFont(QFont(QStringLiteral("Work Sans"), 10));

    // Panel size: title (22) + total bar + rows of 3 part cells.
    const int partCount = monster_.parts.size();
    const int partRows  = (partCount + kCols - 1) / kCols;
    const int totalH = kMargin + 22 + kRowGap + kTotalBarH + kRowGap
                     + partRows * kPartRowH + (partRows > 0 ? (partRows - 1) * kRowGap : 0)
                     + kMargin;
    setContentSize(kPanelWidth, totalH);

    int y = kMargin;

    // Row 1: Name + enrage
    QString name = monster_.internalName;
    if (name.isEmpty())
        name = QStringLiteral("Monster %1").arg(monster_.id);
    QString header = QStringLiteral("%1  [ID %2]").arg(name).arg(monster_.id);

    // TODO v0.2+: crown icon once MonsterSnapshot carries crown data.
    p.drawText(kMargin, y + 22 - 8, header);

    // Enrage timer (right-aligned). Show a clear label so the
    // player doesn't miss it.
    const QRectF enrageRect(kMargin, y, kPanelWidth - 2 * kMargin, 22);
    if (monster_.enraged && monster_.enrageSeconds > 0.0F) {
        // Show remaining enrage time (v0.1 behavior), not elapsed.
        const int remaining = static_cast<int>(
            monster_.enrageMaxSeconds > 0.0F
                ? std::max(0.0F, monster_.enrageMaxSeconds - monster_.enrageSeconds)
                : monster_.enrageSeconds);
        const QString enrageText = QStringLiteral("激怒: ! %1s").arg(remaining);
        p.setPen(QColor(255, 140, 60));  // orange, hard to miss
        p.setFont(QFont(QStringLiteral("Work Sans"), 9, QFont::Bold));
        p.drawText(enrageRect, Qt::AlignRight | Qt::AlignVCenter, enrageText);
    } else if (monster_.enrageMaxBuildup > 0.0F && monster_.enrageBuildup > 0.0F) {
        const int pct = static_cast<int>(
            std::clamp(monster_.enrageBuildup / monster_.enrageMaxBuildup * 100.0F,
                       0.0F, 100.0F));
        if (pct >= 1) {
            const QString angerText = mh::tr("ui.enrage_buildup").arg(pct);
            p.setPen(QColor(255, 190, 60));  // amber, pre-enrage
            p.setFont(QFont(QStringLiteral("Work Sans"), 9, QFont::Bold));
            p.drawText(enrageRect, Qt::AlignRight | Qt::AlignVCenter, angerText);
        }
    }

    y += 22 + kRowGap;

    // Row 2: Big total HP bar
    const float hpPct = (monster_.maxHealth > 0.0F)
                            ? monster_.health / monster_.maxHealth : 0.0F;
    drawBar(p, QRectF(kMargin, y, kPanelWidth - 2 * kMargin, kTotalBarH),
            hpPct, healthColor(hpPct));

    // HP text on bar
    p.setPen(Qt::white);
    p.setFont(QFont(QStringLiteral("Work Sans"), 9, QFont::Bold));
    const QString hpText = QStringLiteral("%1 / %2  %3")
                               .arg(static_cast<int>(monster_.health))
                               .arg(static_cast<int>(monster_.maxHealth))
                               .arg(mhw::percentage(monster_.health, monster_.maxHealth));
    p.drawText(QRectF(kMargin + 6, y, kPanelWidth - 2 * kMargin - 12, kTotalBarH),
               Qt::AlignLeft | Qt::AlignVCenter, hpText);
    y += kTotalBarH + kRowGap;

    // Rows 3+: parts arranged in a 3-column grid. In single-player
    // each cell shows a mini HP bar + percentage; in multiplayer the
    // game does not expose real HP, so cells show flinch/break
    // counters instead (matching v0.1 / HunterPie behavior).
    const int cellW = (kPanelWidth - 2 * kMargin - (kCols - 1) * kCellGap) / kCols;
    p.setFont(QFont(QStringLiteral("Work Sans"), 8));

    for (int row = 0; row < partRows; ++row) {
        const int cy = y + row * (kPartRowH + kRowGap);
        for (int col = 0; col < kCols; ++col) {
            const int idx = row * kCols + col;
            if (idx >= partCount) break;
            const auto &part = monster_.parts[idx];

            const int cx = kMargin + col * (cellW + kCellGap);

            // Short label: drop the tag suffix for compact display.
            QString partLabel = part.name;
            if (part.isSeverable)
                partLabel += QStringLiteral(" (斩)");
            else if (part.isBreakable)
                partLabel += QStringLiteral(" (破)");
            // Truncate to fit cell width.
            const QFontMetrics fm(p.font());
            if (fm.horizontalAdvance(partLabel) > cellW)
                partLabel = fm.elidedText(partLabel, Qt::ElideRight, cellW);

            // Top label
            p.setPen(QColor(210, 210, 210));
            p.drawText(QRectF(cx, cy, cellW, kPartBarH),
                       Qt::AlignLeft | Qt::AlignVCenter, partLabel);

            const QRectF barRect(cx, cy + kPartBarH, cellW, kPartBarH);
            if (multiplayer_ && (part.isBroken || part.counter > 0)) {
                // Multiplayer + this part has been broken: show counter.
                QString counterText;
                if (part.isSeverable && part.maxFlinch > 0.0F)
                    counterText = QStringLiteral("硬直 %1/%2")
                        .arg(static_cast<int>(part.flinch))
                        .arg(static_cast<int>(part.maxFlinch));
                else if (part.isBreakable && part.counter > 0)
                    counterText = QStringLiteral("破坏 %1").arg(part.counter);
                else
                    counterText = part.isBroken ? QStringLiteral("已破") : QStringLiteral("—");
                p.setPen(Qt::white);
                p.drawText(barRect, Qt::AlignCenter, counterText);
            } else {
                // Single-player OR multiplayer part not yet broken: HP bar.
                const float partPct = (part.maxHealth > 0.0F)
                    ? part.health / part.maxHealth : 0.0F;
                drawBar(p, barRect, partPct, healthColor(partPct));
                p.setPen(Qt::white);
                p.setFont(QFont(QStringLiteral("Work Sans"), 8, QFont::Bold));
                p.drawText(barRect, Qt::AlignCenter,
                           mhw::percentage(part.health, part.maxHealth));
                p.setFont(QFont(QStringLiteral("Work Sans"), 8));
            }
        }
    }
}

void MonsterPanel::paintDemo(QPainter &p)
{
    // Sample content shown in edit mode when no real monster data is
    // available, so the user can identify and position this panel.
    // Mirrors paintPanel's 3-column part layout.
    const int demoParts = 6;
    const int demoRows = (demoParts + kCols - 1) / kCols;
    const int totalH = kMargin + 22 + kRowGap + kTotalBarH + kRowGap
                     + demoRows * kPartRowH + (demoRows > 0 ? (demoRows - 1) * kRowGap : 0)
                     + kMargin;
    setContentSize(kPanelWidth, totalH);

    int y = kMargin;

    // Title
    p.setPen(QColor(120, 180, 255));
    p.setFont(QFont(QStringLiteral("Work Sans"), 10, QFont::Bold));
    p.drawText(kMargin, y + 14, QStringLiteral("怪物血量 (示例) — Monster"));
    y += 22 + kRowGap;

    // Big total HP bar
    drawBar(p, QRectF(kMargin, y, kPanelWidth - 2 * kMargin, kTotalBarH),
            0.65F, healthColor(0.65F));
    p.setPen(Qt::white);
    p.setFont(QFont(QStringLiteral("Work Sans"), 11, QFont::Bold));
    p.drawText(QRectF(kMargin + 6, y, kPanelWidth - 2 * kMargin - 12, kTotalBarH),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("示例怪物  65000/100000 65%"));
    y += kTotalBarH + kRowGap;

    // 6 sample parts in a 2x3 grid (matches typical monster: head,
    // body, tail, wing-l, wing-r, leg).
    const int cellW = (kPanelWidth - 2 * kMargin - (kCols - 1) * kCellGap) / kCols;
    const char *demoNames[] = {"头部", "身体", "尾巴", "左翼", "右翼", "左腿"};
    const float demoPct[] = {0.85F, 0.60F, 0.30F, 0.55F, 0.45F, 0.70F};
    p.setFont(QFont(QStringLiteral("Work Sans"), 8));

    for (int row = 0; row < demoRows; ++row) {
        const int cy = y + row * (kPartRowH + kRowGap);
        for (int col = 0; col < kCols; ++col) {
            const int idx = row * kCols + col;
            if (idx >= demoParts) break;
            const int cx = kMargin + col * (cellW + kCellGap);

            // Label
            p.setPen(QColor(210, 210, 210));
            p.drawText(QRectF(cx, cy, cellW, kPartBarH),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString::fromUtf8(demoNames[idx]));

            // Mini bar
            const QRectF barRect(cx, cy + kPartBarH, cellW, kPartBarH);
            drawBar(p, barRect, demoPct[idx], healthColor(demoPct[idx]));

            // Percent
            p.setPen(Qt::white);
            p.setFont(QFont(QStringLiteral("Work Sans"), 8, QFont::Bold));
            p.drawText(barRect, Qt::AlignCenter,
                       QStringLiteral("%1%").arg(static_cast<int>(demoPct[idx] * 100)));
            p.setFont(QFont(QStringLiteral("Work Sans"), 8));
        }
    }
}