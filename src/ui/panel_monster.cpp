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
constexpr int kPartBarH = 14;
constexpr int kRowGap = 6;
constexpr int kMargin = 10;
constexpr int kIconSize = 20;

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

    // Panel size: compute from content
    const int partCount = monster_.parts.size();
    const int totalH = kMargin + 22 + kRowGap + kTotalBarH + kRowGap
                     + partCount * (kPartBarH + kRowGap) + kMargin;
    setContentSize(kPanelWidth, totalH);

    int y = kMargin;

    // Row 1: Name + enrage
    QString name = monster_.internalName;
    if (name.isEmpty())
        name = QStringLiteral("Monster %1").arg(monster_.id);
    QString header = QStringLiteral("%1  [ID %2]").arg(name).arg(monster_.id);

    // TODO v0.2+: crown icon once MonsterSnapshot carries crown data.
    p.drawText(kMargin, y + kIconSize - 3, header);

    // Enrage timer (right-aligned)
    if (monster_.enraged && monster_.enrageSeconds > 0.0F) {
        const QString enrageText = QStringLiteral("\xF0\x9F\x94\xA5 %1s")
                                       .arg(static_cast<int>(monster_.enrageSeconds));
        const QRectF rect(kMargin, y, kPanelWidth - 2 * kMargin, kIconSize);
        p.drawText(rect, Qt::AlignRight | Qt::AlignVCenter, enrageText);
    } else if (monster_.enrageMaxBuildup > 0.0F && monster_.enrageBuildup > 0.0F) {
        const int pct = static_cast<int>(
            std::clamp(monster_.enrageBuildup / monster_.enrageMaxBuildup * 100.0F,
                       0.0F, 100.0F));
        if (pct >= 1) {
            const QString angerText = mh::tr("ui.enrage_buildup").arg(pct);
            const QRectF rect(kMargin, y, kPanelWidth - 2 * kMargin, kIconSize);
            p.drawText(rect, Qt::AlignRight | Qt::AlignVCenter, angerText);
        }
    }

    y += kIconSize + kRowGap;

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

    // Rows 3+: Per-part small bars
    p.setFont(QFont(QStringLiteral("Work Sans"), 8));
    for (const auto &part : monster_.parts) {
        const float partPct = (part.maxHealth > 0.0F) ? part.health / part.maxHealth : 0.0F;
        // Part name + small icon
        QString partLabel = part.name;
        if (part.isSeverable)
            partLabel += mh::tr("ui.part_severable_tag");
        else if (part.isBreakable)
            partLabel += mh::tr("ui.part_breakable_tag");

        // Small colored bar
        drawBar(p, QRectF(kMargin + 100, y, kPanelWidth - 2 * kMargin - 100, kPartBarH),
                partPct, healthColor(partPct));

        // Name on left
        p.setPen(QColor(200, 200, 200));
        p.drawText(QRectF(kMargin, y, 96, kPartBarH),
                   Qt::AlignLeft | Qt::AlignVCenter, partLabel);

        // Percentage on bar
        p.setPen(Qt::white);
        p.drawText(QRectF(kMargin + 104, y, kPanelWidth - 2 * kMargin - 108, kPartBarH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   mhw::percentage(part.health, part.maxHealth));

        y += kPartBarH + kRowGap;
    }
}

void MonsterPanel::paintDemo(QPainter &p)
{
    // Sample content shown in edit mode when no real monster data is
    // available, so the user can identify and position this panel.
    const int demoParts = 3;
    const int totalH = kMargin + 22 + kRowGap + kTotalBarH + kRowGap
                     + demoParts * (kPartBarH + kRowGap) + kMargin;
    setContentSize(kPanelWidth, totalH);

    int y = kMargin;

    // Title
    p.setPen(QColor(120, 180, 255));
    p.setFont(QFont(QStringLiteral("Work Sans"), 10, QFont::Bold));
    p.drawText(kMargin, y + 12, QStringLiteral("怪物血量 (示例) — Monster"));
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

    // Sample part bars
    p.setFont(QFont(QStringLiteral("Work Sans"), 8));
    const char *demoPartNames[] = {"头部", "尾巴", "左翼"};
    const float demoPartPct[] = {0.85F, 0.30F, 0.55F};
    for (int i = 0; i < demoParts; ++i) {
        drawBar(p, QRectF(kMargin + 100, y, kPanelWidth - 2 * kMargin - 100, kPartBarH),
                demoPartPct[i], healthColor(demoPartPct[i]));
        p.setPen(QColor(200, 200, 200));
        p.drawText(QRectF(kMargin, y, 96, kPartBarH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromUtf8(demoPartNames[i]));
        p.setPen(Qt::white);
        p.drawText(QRectF(kMargin + 104, y, kPanelWidth - 2 * kMargin - 108, kPartBarH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1%").arg(static_cast<int>(demoPartPct[i] * 100)));
        y += kPartBarH + kRowGap;
    }
}