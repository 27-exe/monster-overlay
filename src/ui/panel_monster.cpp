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

    // Panel size: title (22) + total bar + status row + rows of 3 part cells.
    const int partCount = monster_.parts.size();
    const int partRows  = (partCount + kCols - 1) / kCols;

    // Collect visible statuses into a list for horizontal layout.
    struct StatusEntry {
        QString name;
        QColor color;
        float pct;
        QString text;
        int counter;
    };
    QVector<StatusEntry> statuses;

    // Enrage first
    const bool enrageVisible = (monster_.enraged && monster_.enrageSeconds > 0.0F)
                            || (monster_.enrageMaxBuildup > 0.0F && monster_.enrageBuildup > 0.0F);
    if (enrageVisible) {
        StatusEntry e;
        e.name = QStringLiteral("激怒");
        if (monster_.enraged && monster_.enrageSeconds > 0.0F) {
            const int remaining = static_cast<int>(
                monster_.enrageMaxSeconds > 0.0F
                    ? std::max(0.0F, monster_.enrageMaxSeconds - monster_.enrageSeconds)
                    : monster_.enrageSeconds);
            e.pct = (monster_.enrageMaxSeconds > 0.0F)
                ? std::clamp(static_cast<float>(remaining) / monster_.enrageMaxSeconds, 0.0F, 1.0F) : 0.0F;
            e.text = QStringLiteral("%1s").arg(remaining);
            e.color = QColor(255, 140, 60);
        } else {
            e.pct = std::clamp(monster_.enrageBuildup / monster_.enrageMaxBuildup, 0.0F, 1.0F);
            e.text = QStringLiteral("%1%").arg(static_cast<int>(e.pct * 100));
            e.color = QColor(255, 190, 60);
        }
        e.counter = 0;
        statuses.append(e);
    }

    // Ailments: only show if timer > 0, buildup > 0, or counter > 0
    for (const auto &ail : monster_.ailments) {
        if (ail.timer <= 0.0F && ail.buildup <= 0.0F && ail.counter <= 0)
            continue;
        StatusEntry e;
        e.name = ail.name;
        e.counter = ail.counter;
        switch (ail.id) {
        case 1:  e.color = QColor(156, 39, 176);  break; // 毒
        case 2:  e.color = QColor(255, 193, 7);   break; // 麻痹
        case 3:  e.color = QColor(33, 150, 243);  break; // 睡眠
        case 4:  e.color = QColor(244, 67, 54);   break; // 爆破
        case 5:  e.color = QColor(76, 175, 80);   break; // 毒(上位)
        default: e.color = QColor(158, 158, 158); break;
        }
        if (ail.active && ail.maxTimer > 0.0F) {
            e.pct = std::clamp(ail.timer / ail.maxTimer, 0.0F, 1.0F);
            e.text = QStringLiteral("%1s").arg(static_cast<int>(ail.timer));
        } else if (ail.maxBuildup > 0.0F) {
            e.pct = std::clamp(ail.buildup / ail.maxBuildup, 0.0F, 1.0F);
            e.text = QStringLiteral("%1%").arg(static_cast<int>(e.pct * 100));
        } else {
            e.pct = 0.0F;
            e.text.clear();
        }
        statuses.append(e);
    }

    const int statusCount = statuses.size();
    const int statusRowH = (statusCount > 0) ? (kPartBarH + 12 + kRowGap) : 0;

    const int totalH = kMargin + 22 + kRowGap + kTotalBarH + kRowGap
                     + statusRowH
                     + partRows * kPartRowH + (partRows > 0 ? (partRows - 1) * kRowGap : 0)
                     + kMargin;
    setContentSize(kPanelWidth, totalH);

    int y = kMargin;

    // Row 1: Name
    QString name = monster_.internalName;
    if (name.isEmpty())
        name = QStringLiteral("Monster %1").arg(monster_.id);
    QString header = QStringLiteral("%1  [ID %2]").arg(name).arg(monster_.id);
    p.drawText(kMargin, y + 22 - 8, header);

    y += 22 + kRowGap;

    // Row 2: Big total HP bar
    const float hpPct = (monster_.maxHealth > 0.0F)
                            ? monster_.health / monster_.maxHealth : 0.0F;
    drawBar(p, QRectF(kMargin, y, kPanelWidth - 2 * kMargin, kTotalBarH),
            hpPct, healthColor(hpPct));

    p.setPen(Qt::white);
    p.setFont(QFont(QStringLiteral("Work Sans"), 9, QFont::Bold));
    const QString hpText = QStringLiteral("%1 / %2  %3")
                               .arg(static_cast<int>(monster_.health))
                               .arg(static_cast<int>(monster_.maxHealth))
                               .arg(mhw::percentage(monster_.health, monster_.maxHealth));
    p.drawText(QRectF(kMargin + 6, y, kPanelWidth - 2 * kMargin - 12, kTotalBarH),
               Qt::AlignLeft | Qt::AlignVCenter, hpText);
    y += kTotalBarH + kRowGap;

    // Status row: all statuses in one horizontal row, auto-width
    if (statusCount > 0) {
        const int gap = 6;
        const int cellW = (kPanelWidth - 2 * kMargin - (statusCount - 1) * gap) / statusCount;
        p.setFont(QFont(QStringLiteral("Work Sans"), 7));

        for (int i = 0; i < statusCount; ++i) {
            const auto &s = statuses[i];
            const int cx = kMargin + i * (cellW + gap);

            // Name + counter on top
            QString label = s.name;
            if (s.counter > 0)
                label += QStringLiteral(" ×%1").arg(s.counter);
            p.setPen(QColor(200, 200, 200));
            p.drawText(QRectF(cx, y, cellW, 12),
                       Qt::AlignLeft | Qt::AlignVCenter, label);

            // Bar below
            const QRectF barRect(cx, y + 12, cellW, kPartBarH);
            drawBar(p, barRect, s.pct, s.color);
            p.setPen(Qt::white);
            p.setFont(QFont(QStringLiteral("Work Sans"), 7, QFont::Bold));
            p.drawText(barRect, Qt::AlignCenter, s.text);
            p.setFont(QFont(QStringLiteral("Work Sans"), 7));
        }
        y += statusRowH;
    }

    // Parts: 3-column grid
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
    const int demoParts = 6;
    const int demoRows = (demoParts + kCols - 1) / kCols;
    const int demoAilments = 3;  // 激怒 + 麻痹 active + 睡眠 buildup
    const int totalH = kMargin + 22 + kRowGap + kTotalBarH + kRowGap
                     + demoAilments * (kPartBarH + kRowGap)
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

    // Sample status row: horizontal, auto-width (mirrors paintPanel)
    struct { const char *name; QColor color; float pct; QString text; int counter; } demoAil[] = {
        {"激怒", QColor(255, 140, 60), 0.70F, QStringLiteral("42s"), 0},
        {"麻痹", QColor(255, 193, 7), 0.60F, QStringLiteral("12s"), 1},
        {"睡眠", QColor(33, 150, 243), 0.45F, QStringLiteral("45%"), 0},
    };
    const int demoStatusCount = 3;
    {
        const int gap = 6;
        const int sw = (kPanelWidth - 2 * kMargin - (demoStatusCount - 1) * gap) / demoStatusCount;
        p.setFont(QFont(QStringLiteral("Work Sans"), 7));
        for (int i = 0; i < demoStatusCount; ++i) {
            const auto &a = demoAil[i];
            const int cx = kMargin + i * (sw + gap);
            QString label = QString::fromUtf8(a.name);
            if (a.counter > 0)
                label += QStringLiteral(" ×%1").arg(a.counter);
            p.setPen(QColor(200, 200, 200));
            p.drawText(QRectF(cx, y, sw, 12),
                       Qt::AlignLeft | Qt::AlignVCenter, label);
            const QRectF barRect(cx, y + 12, sw, kPartBarH);
            drawBar(p, barRect, a.pct, a.color);
            p.setPen(Qt::white);
            p.setFont(QFont(QStringLiteral("Work Sans"), 7, QFont::Bold));
            p.drawText(barRect, Qt::AlignCenter, a.text);
            p.setFont(QFont(QStringLiteral("Work Sans"), 7));
        }
        y += kPartBarH + 12 + kRowGap;
    }

    // 6 sample parts in a 2x3 grid
    const int cellW = (kPanelWidth - 2 * kMargin - (kCols - 1) * kCellGap) / kCols;
    const char *demoNames[] = {"头部", "身体", "尾巴", "左翼", "右翼", "左腿"};
    const float demoPct[] = {0.85F, 0.60F, 0.30F, 0.55F, 0.45F, 0.70F};

    for (int row = 0; row < demoRows; ++row) {
        const int cy = y + row * (kPartRowH + kRowGap);
        for (int col = 0; col < kCols; ++col) {
            const int idx = row * kCols + col;
            if (idx >= demoParts) break;
            const int cx = kMargin + col * (cellW + kCellGap);

            p.setPen(QColor(210, 210, 210));
            p.drawText(QRectF(cx, cy, cellW, kPartBarH),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString::fromUtf8(demoNames[idx]));

            const QRectF barRect(cx, cy + kPartBarH, cellW, kPartBarH);
            drawBar(p, barRect, demoPct[idx], healthColor(demoPct[idx]));

            p.setPen(Qt::white);
            p.setFont(QFont(QStringLiteral("Work Sans"), 8, QFont::Bold));
            p.drawText(barRect, Qt::AlignCenter,
                       QStringLiteral("%1%").arg(static_cast<int>(demoPct[idx] * 100)));
            p.setFont(QFont(QStringLiteral("Work Sans"), 8));
        }
    }
}