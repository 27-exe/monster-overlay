#include "panel_player.h"

#include "core/string_table.h"
#include "player/player_types.h"
#include "ui/formatters.h"
#include "ui/icon.h"

#include <QPainter>
#include <cmath>

using mhw::Icon;

namespace {

constexpr int kPanelW = 320;
constexpr int kBarH = 18;
constexpr int kRowGap = 6;
constexpr int kMargin = 10;
constexpr int kIconSize = 20;

QColor hpColor(float pct)
{
    if (pct > 0.6F) return QColor(76, 175, 80);
    if (pct > 0.25F) return QColor(255, 193, 7);
    return QColor(244, 67, 54);
}

QColor stColor(float pct)
{
    if (pct > 0.5F) return QColor(33, 150, 243);
    if (pct > 0.2F) return QColor(255, 152, 0);
    return QColor(244, 67, 54);
}

void drawBar(QPainter &p, const QRectF &rect, float pct, const QColor &fill)
{
    const float c = std::clamp(pct, 0.0F, 1.0F);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 40, 40, 180));
    p.drawRoundedRect(rect, 3, 3);
    if (c > 0.001F) {
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(rect.x(), rect.y(), rect.width() * c, rect.height()), 3, 3);
    }
    p.setPen(QPen(QColor(100, 100, 100, 120), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect, 3, 3);
}

} // namespace

namespace mh {
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

PlayerPanel::PlayerPanel(QWidget *parent)
    : Panel(QStringLiteral("player"), QPoint(20, 20), parent)
{
    setWindowTitle(mh::tr("ui.player_title"));
}

void PlayerPanel::update(const mhw::PlayerSnapshot &p)
{
    player_ = p;
    hasData_ = p.valid;
    canvas()->update();
}

void PlayerPanel::paintPanel(QPainter &p)
{
    if (!hasData_)
        return;

    p.setPen(Qt::white);
    p.setFont(QFont(QStringLiteral("Work Sans"), 10));

    const int mantleCount = (player_.mantleSlot0Id >= 0 ? 1 : 0)
                          + (player_.mantleSlot1Id >= 0 ? 1 : 0);
    const int totalH = kMargin + kBarH + kRowGap + kBarH + kRowGap
                     + (mantleCount > 0 ? kIconSize + kRowGap : 0) + kMargin;
    setContentSize(kPanelW, totalH);

    int y = kMargin;
    const float hpPct = (player_.maxHealth > 0.0F) ? player_.health / player_.maxHealth : 0.0F;
    const float stPct = (player_.maxStamina > 0.0F) ? player_.stamina / player_.maxStamina : 0.0F;

    // Row 1: HP bar
    drawBar(p, QRectF(kMargin, y, kPanelW - 2 * kMargin, kBarH), hpPct, hpColor(hpPct));
    p.setPen(Qt::white);
    p.setFont(QFont(QStringLiteral("Work Sans"), 9, QFont::Bold));
    p.drawText(QRectF(kMargin + 6, y, kPanelW - 2 * kMargin - 12, kBarH),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("HP %1/%2 %3")
                   .arg(static_cast<int>(player_.health))
                   .arg(static_cast<int>(player_.maxHealth))
                   .arg(mhw::percentage(player_.health, player_.maxHealth)));
    y += kBarH + kRowGap;

    // Row 2: ST bar
    drawBar(p, QRectF(kMargin, y, kPanelW - 2 * kMargin, kBarH), stPct, stColor(stPct));
    p.setPen(Qt::white);
    p.drawText(QRectF(kMargin + 6, y, kPanelW - 2 * kMargin - 12, kBarH),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("ST %1/%2 %3")
                   .arg(static_cast<int>(player_.stamina))
                   .arg(static_cast<int>(player_.maxStamina))
                   .arg(mhw::percentage(player_.stamina, player_.maxStamina)));
    y += kBarH + kRowGap;

    // Row 3: Weapon icon + mantle timers
    if (weaponId_ >= 0) {
        const QString wp = Icon::weaponPath(weaponId_, 1);
        const QPixmap pix = Icon::render(wp, kIconSize);
        if (!pix.isNull()) {
            p.drawPixmap(kMargin, y, pix);
        }
    }

    // Mantle icons
    int mx = kMargin + kIconSize + 8;
    auto drawMantle = [&](int id, float timer, float cooldown) {
        if (id < 0) return;
        const QString mp = Icon::mantlePath(id);
        const QPixmap pix = Icon::render(mp, kIconSize);
        if (!pix.isNull())
            p.drawPixmap(mx, y, pix);
        // Timer / cooldown text
        p.setFont(QFont(QStringLiteral("Work Sans"), 8));
        QString label;
        if (timer > 0.0F)
            label = mh::tr("ui.mantle_active").arg(mhw::mantleName(id)).arg(static_cast<int>(timer));
        else if (cooldown > 0.0F)
            label = mh::tr("ui.mantle_cooldown").arg(mhw::mantleName(id)).arg(static_cast<int>(cooldown));
        p.drawText(mx + kIconSize + 2, y + kIconSize - 3, label);
        mx += kIconSize + 8 + p.fontMetrics().horizontalAdvance(label) + 12;
    };
    drawMantle(player_.mantleSlot0Id, player_.mantleSlot0Timer, player_.mantleSlot0Cooldown);
    drawMantle(player_.mantleSlot1Id, player_.mantleSlot1Timer, player_.mantleSlot1Cooldown);
}