#include "panel_damage.h"

#include "core/game_snapshot.h"
#include "core/string_table.h"
#include "ui/formatters.h"
#include "ui/icon.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

using mhw::Icon;

namespace {

constexpr int kPanelW = 400;
constexpr int kMargin = 10;
constexpr int kRowH = 22;
constexpr int kChartH = 120;
constexpr int kIconSize = 18;
constexpr int kMaxSamples = 180; // 3 minutes at 1 Hz

const QColor kPlayerColors[] = {
    QColor(66, 165, 245),   // blue  — local player
    QColor(255, 167, 38),   // orange
    QColor(102, 187, 106),  // green
    QColor(171, 71, 188),   // purple
};
constexpr int kMaxPlayers = 4;

} // namespace

namespace mh {
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

DamagePanel::DamagePanel(QWidget *parent)
    : Panel(QStringLiteral("dps"), QPoint(1200, 400), parent)
{
    setWindowTitle(mh::tr("ui.damage_title"));
}

void DamagePanel::update(const mhw::GameSnapshot &snap)
{
    hasData_ = !snap.party.isEmpty();
    if (!hasData_) {
        history_.clear();
        canvas()->update();
        return;
    }

    // Update player metadata
    const int n = std::min(static_cast<int>(snap.party.size()), kMaxPlayers);
    names_.resize(n);
    weaponIds_.resize(n);
    masterRanks_.resize(n);
    for (int i = 0; i < n; ++i) {
        names_[i] = snap.party[i].name;
        weaponIds_[i] = snap.party[i].weaponId;
        masterRanks_[i] = snap.party[i].masterRank;
    }

    // Record sample
    Sample s;
    s.tick = tick_++;
    s.damage.resize(n);
    for (int i = 0; i < n; ++i)
        s.damage[i] = snap.party[i].damage;
    history_.append(s);
    if (history_.size() > kMaxSamples)
        history_.removeFirst();

    canvas()->update();
}

void DamagePanel::paintPanel(QPainter &p)
{
    if (!hasData_)
        return;

    p.setPen(Qt::white);
    p.setFont(QFont(QStringLiteral("Work Sans"), 9));

    const int n = names_.size();
    const int rowsH = n * kRowH;
    const int totalH = kMargin + rowsH + kMargin + kChartH + kMargin + 16;
    setContentSize(kPanelW, totalH);

    int y = kMargin;

    // --- Player rows ---
    for (int i = 0; i < n; ++i) {
        const int dmg = history_.isEmpty() ? 0 : history_.last().damage.value(i, 0);
        // DPS = damage delta over last 60 samples (1 min window)
        int dps = 0;
        if (history_.size() >= 2) {
            const int idx = std::max(0, static_cast<int>(history_.size()) - 60);
            const int elapsed = history_.last().tick - history_[idx].tick;
            if (elapsed > 0)
                dps = (dmg - history_[idx].damage.value(i, 0)) / elapsed;
        }

        // Weapon icon
        if (weaponIds_[i] >= 0) {
            const QPixmap wp = Icon::render(Icon::weaponPath(weaponIds_[i], 1), kIconSize);
            if (!wp.isNull())
                p.drawPixmap(kMargin, y + 1, wp);
        }

        // Name + MR
        p.setPen(kPlayerColors[i % kMaxPlayers]);
        p.setFont(QFont(QStringLiteral("Work Sans"), 9, QFont::Bold));
        p.drawText(kMargin + kIconSize + 4, y + kRowH - 5,
                   QStringLiteral("%1  MR%2").arg(names_[i]).arg(masterRanks_[i]));

        // Damage + DPS (right-aligned)
        p.setPen(QColor(220, 220, 220));
        p.setFont(QFont(QStringLiteral("Work Sans"), 8));
        const QString dmgStr = QStringLiteral("%1  DPS %2")
                                   .arg(dmg)
                                   .arg(dps);
        p.drawText(QRectF(kMargin, y, kPanelW - 2 * kMargin, kRowH),
                   Qt::AlignRight | Qt::AlignVCenter, dmgStr);
        y += kRowH;
    }

    y += kMargin;

    // --- Line chart: cumulative damage over time ---
    const QRectF chartRect(kMargin + 30, y, kPanelW - 2 * kMargin - 30, kChartH);

    // Background
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 30, 160));
    p.drawRoundedRect(chartRect, 4, 4);

    if (history_.size() < 2) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(chartRect, Qt::AlignCenter, mh::tr("ui.damage_waiting"));
        return;
    }

    // Find max damage for scaling
    int maxDmg = 1;
    for (const auto &s : history_)
        for (int d : s.damage)
            maxDmg = std::max(maxDmg, d);

    // Grid lines (4 horizontal)
    p.setPen(QPen(QColor(80, 80, 80, 100), 1, Qt::DotLine));
    for (int g = 1; g <= 4; ++g) {
        const float gy = chartRect.bottom() - chartRect.height() * g / 4.0F;
        p.drawLine(QPointF(chartRect.left(), gy), QPointF(chartRect.right(), gy));
        // Y-axis label
        p.setPen(QColor(150, 150, 150));
        p.setFont(QFont(QStringLiteral("Work Sans"), 6));
        p.drawText(QRectF(kMargin, gy - 8, 28, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1k").arg(maxDmg * g / 4000));
        p.setPen(QPen(QColor(80, 80, 80, 100), 1, Qt::DotLine));
    }

    // Draw one polyline per player
    const float xStep = chartRect.width() / static_cast<float>(kMaxSamples - 1);
    for (int i = 0; i < n; ++i) {
        QPainterPath path;
        bool first = true;
        for (int j = 0; j < history_.size(); ++j) {
            const float x = chartRect.left() + j * xStep;
            const float yv = chartRect.bottom()
                           - chartRect.height()
                                 * std::clamp(static_cast<float>(history_[j].damage.value(i, 0))
                                                  / static_cast<float>(maxDmg),
                                              0.0F, 1.0F);
            if (first) { path.moveTo(x, yv); first = false; }
            else path.lineTo(x, yv);
        }
        p.setPen(QPen(kPlayerColors[i % kMaxPlayers], 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    // --- Percentage share bar at the bottom ---
    y = chartRect.bottom() + 6;
    const int totalDmg = [&] {
        int sum = 0;
        if (!history_.isEmpty())
            for (int d : history_.last().damage) sum += d;
        return sum;
    }();

    if (totalDmg > 0) {
        float bx = kMargin;
        const float barW = kPanelW - 2 * kMargin;
        for (int i = 0; i < n; ++i) {
            const float share = static_cast<float>(history_.last().damage.value(i, 0))
                              / static_cast<float>(totalDmg);
            const float w = barW * share;
            p.setPen(Qt::NoPen);
            p.setBrush(kPlayerColors[i % kMaxPlayers]);
            p.drawRoundedRect(QRectF(bx, y, w, 8), 2, 2);
            // Percentage label
            p.setPen(Qt::white);
            p.setFont(QFont(QStringLiteral("Work Sans"), 6));
            p.drawText(QRectF(bx, y - 1, w, 10), Qt::AlignCenter,
                       QStringLiteral("%1%").arg(static_cast<int>(share * 100)));
            bx += w;
        }
    }
}