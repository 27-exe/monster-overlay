#include "panel_player.h"

#include "core/string_table.h"
#include "player/player_types.h"
#include "quest/quest_types.h"
#include "ui/formatters.h"
#include "ui/icon.h"
#include "world/world_types.h"

#include <QPainter>
#include <cmath>

using mhw::Icon;

namespace {

constexpr int kPanelW = 320;
constexpr int kBarH = 16;
constexpr int kRowGap = 5;
constexpr int kMargin = 8;
constexpr int kIconSize = 18;
constexpr int kHeaderH = 14;

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

QColor statusColor(bool ok) { return ok ? QColor(76, 175, 80) : QColor(244, 67, 54); }

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

// Format a quest-state int (per HunterPie quest table) into a short
// human-readable status. 0 means "no quest / idle".
QString questStateLabel(int state)
{
    switch (state) {
    case 0:  return QStringLiteral("空闲");
    case 1:  return QStringLiteral("接单");
    case 2:  return QStringLiteral("进行中");
    case 3:  return QStringLiteral("结束");
    case 4:  return QStringLiteral("失败");
    case 5:  return QStringLiteral("已放弃");
    default: return QStringLiteral("状态%1").arg(state);
    }
}

// Format zone (TopRight-corner label).
QString zoneLabel(mhw::Zone z)
{
    if (z == mhw::Zone::Unknown)
        return QStringLiteral("未知区域");
    const QString name = QString::fromUtf8(mhw::zoneName(z));
    return name.isEmpty() ? QStringLiteral("区域%1").arg(static_cast<int>(z)) : name;
}

// Format remaining quest time mm:ss.
QString fmtMmSs(float seconds)
{
    if (seconds <= 0.0F)
        return QStringLiteral("--:--");
    const int total = static_cast<int>(seconds);
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0'));
}

} // namespace

namespace mh {
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

PlayerPanel::PlayerPanel(QWidget *parent)
    : Panel(QStringLiteral("player"), Corner::TopLeft, parent)
{
    setWindowTitle(mh::tr("ui.player_title"));
}

void PlayerPanel::update(const mhw::PlayerSnapshot &p)
{
    // Legacy single-snapshot update — keeps hasData_ in sync.
    player_ = p;
    hasData_ = p.valid;
    canvas()->update();
}

void PlayerPanel::update(const mhw::GameSnapshot &snap)
{
    // Full snapshot — pull everything this panel needs.
    player_    = snap.player;
    zone_      = snap.zone;
    quest_     = snap.quest;
    attached_  = snap.attached;
    pid_       = snap.pid;
    imageBase_ = snap.imageBase;
    status_    = snap.status;
    hasData_   = snap.player.valid;
    canvas()->update();
}

void PlayerPanel::paintPanel(QPainter &p)
{
    // Disconnected: show a small "not connected" placeholder (v0.1
    // behaviour). No HP/ST bars — only title, status indicator, and
    // the reader's status message if it has one.
    if (!attached_) {
        const int totalH = kMargin + kHeaderH + kRowGap + kHeaderH + kMargin;
        setContentSize(kPanelW, totalH);
        int y = kMargin;

        p.setPen(QColor(120, 180, 255));
        p.setFont(QFont(QStringLiteral("Work Sans"), 10, QFont::Bold));
        p.drawText(kMargin, y + kHeaderH - 2,
                   QStringLiteral("猎人状态 — Player"));
        p.setPen(QColor(180, 180, 180));
        p.setFont(QFont(QStringLiteral("Work Sans"), 8));
        const QRectF titleRect(kMargin, y, kPanelW - 2 * kMargin, kHeaderH);
        p.drawText(titleRect, Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("○ 未连接"));
        y += kHeaderH + kRowGap;

        p.setPen(QColor(220, 220, 220));
        p.setFont(QFont(QStringLiteral("Work Sans"), 9));
        const QString hint = status_.isEmpty()
            ? QStringLiteral("启动 Monster Hunter: World 以连接")
            : status_;
        p.drawText(QRectF(kMargin, y, kPanelW - 2 * kMargin, kHeaderH),
                   Qt::AlignLeft | Qt::AlignVCenter, hint);
        return;
    }

    if (!hasData_)
        return;

    p.setFont(QFont(QStringLiteral("Work Sans"), 9));

    // Compute height up-front so paintDemo/paintPanel use the same layout.
    const int mantleCount = (player_.mantleSlot0Id >= 0 ? 1 : 0)
                          + (player_.mantleSlot1Id >= 0 ? 1 : 0);
    const int totalH =
          kMargin
        + kHeaderH                                  // connection row
        + kRowGap
        + kHeaderH                                  // zone + quest row
        + kRowGap
        + kHeaderH                                  // quest sub-row (stars / timer / deaths)
        + kRowGap
        + kBarH + kRowGap                           // HP
        + kBarH + kRowGap                           // ST
        + (mantleCount > 0 ? kIconSize + kRowGap : 0)  // weapon + mantles
        + kMargin;
    setContentSize(kPanelW, totalH);

    int y = kMargin;

    // Row 1: connection status + memory base.
    p.setPen(statusColor(attached_));
    p.setFont(QFont(QStringLiteral("Work Sans"), 9, QFont::Bold));
    const QString conn = attached_
        ? QStringLiteral("● 已连接")
        : QStringLiteral("○ 未连接");
    p.drawText(kMargin, y + kHeaderH - 2, conn);
    p.setPen(QColor(180, 180, 180));
    p.setFont(QFont(QStringLiteral("Work Sans"), 8));
    const QString mem = QStringLiteral("PID %1 · BASE 0x%2")
                            .arg(pid_ > 0 ? QString::number(pid_) : QStringLiteral("--"))
                            .arg(imageBase_ ? QString::number(static_cast<qulonglong>(imageBase_), 16) : QStringLiteral("--"));
    const QRectF connRect(kMargin, y, kPanelW - 2 * kMargin, kHeaderH);
    p.drawText(connRect, Qt::AlignRight | Qt::AlignVCenter, mem);
    y += kHeaderH + kRowGap;

    // Row 2: zone + quest summary.
    p.setPen(QColor(120, 180, 255));
    p.setFont(QFont(QStringLiteral("Work Sans"), 9, QFont::Bold));
    p.drawText(kMargin, y + kHeaderH - 2,
               QStringLiteral("区域: %1").arg(zoneLabel(zone_)));
    p.setPen(quest_.active ? QColor(255, 200, 80) : QColor(150, 150, 150));
    p.setFont(QFont(QStringLiteral("Work Sans"), 9));
    const QRectF zoneRect(kMargin, y, kPanelW - 2 * kMargin, kHeaderH);
    const QString questTxt = quest_.active
        ? QStringLiteral("任务 #%1 ★%2 · %3")
              .arg(quest_.id)
              .arg(quest_.stars)
              .arg(questStateLabel(quest_.state))
        : QStringLiteral("任务: 空闲");
    p.drawText(zoneRect, Qt::AlignRight | Qt::AlignVCenter, questTxt);
    y += kHeaderH + kRowGap;

    // Row 3: quest detail — timer / deaths (only when quest active).
    if (quest_.active) {
        p.setPen(QColor(220, 220, 220));
        p.setFont(QFont(QStringLiteral("Work Sans"), 8));
        const QString detail = QStringLiteral("剩余 %1 · 猫车 %2/%3")
                                   .arg(fmtMmSs(quest_.timeLeftSeconds))
                                   .arg(quest_.deaths)
                                   .arg(quest_.maxDeaths);
        p.drawText(kMargin, y + kHeaderH - 2, detail);
    } else if (!status_.isEmpty()) {
        // No quest active but reader reports a status (waiting / error).
        p.setPen(QColor(180, 180, 180));
        p.setFont(QFont(QStringLiteral("Work Sans"), 8));
        p.drawText(kMargin, y + kHeaderH - 2, status_);
    }
    y += kHeaderH + kRowGap;

    // Row 4: HP bar
    const float hpPct = (player_.maxHealth > 0.0F) ? player_.health / player_.maxHealth : 0.0F;
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

    // Row 5: ST bar
    const float stPct = (player_.maxStamina > 0.0F) ? player_.stamina / player_.maxStamina : 0.0F;
    drawBar(p, QRectF(kMargin, y, kPanelW - 2 * kMargin, kBarH), stPct, stColor(stPct));
    p.setPen(Qt::white);
    p.drawText(QRectF(kMargin + 6, y, kPanelW - 2 * kMargin - 12, kBarH),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("ST %1/%2 %3")
                   .arg(static_cast<int>(player_.stamina))
                   .arg(static_cast<int>(player_.maxStamina))
                   .arg(mhw::percentage(player_.stamina, player_.maxStamina)));
    y += kBarH + kRowGap;

    // Row 6: Weapon icon + mantle timers
    if (weaponId_ >= 0) {
        const QString wp = Icon::weaponPath(weaponId_, 1);
        const QPixmap pix = Icon::render(wp, kIconSize);
        if (!pix.isNull())
            p.drawPixmap(kMargin, y, pix);
    }

    int mx = kMargin + kIconSize + 8;
    auto drawMantle = [&](int id, float timer, float cooldown) {
        if (id < 0) return;
        const QString mp = Icon::mantlePath(id);
        const QPixmap pix = Icon::render(mp, kIconSize);
        if (!pix.isNull())
            p.drawPixmap(mx, y, pix);
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

void PlayerPanel::paintDemo(QPainter &p)
{
    // Sample content shown in edit mode. Mirrors paintPanel layout so
    // the user can see roughly what the panel will look like in-game.
    p.setFont(QFont(QStringLiteral("Work Sans"), 9));

    const int totalH =
          kMargin
        + kHeaderH + kRowGap
        + kHeaderH + kRowGap
        + kHeaderH + kRowGap
        + kBarH + kRowGap
        + kBarH + kRowGap
        + kIconSize
        + kMargin;
    setContentSize(kPanelW, totalH);

    int y = kMargin;

    // Connection row
    p.setPen(QColor(120, 180, 255));
    p.setFont(QFont(QStringLiteral("Work Sans"), 10, QFont::Bold));
    p.drawText(kMargin, y + kHeaderH - 2, QStringLiteral("猎人状态 (示例) — Player"));
    p.setPen(QColor(180, 180, 180));
    p.setFont(QFont(QStringLiteral("Work Sans"), 8));
    const QRectF connRect(kMargin, y, kPanelW - 2 * kMargin, kHeaderH);
    p.drawText(connRect, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("PID -- · BASE 0x--"));
    y += kHeaderH + kRowGap;

    // Zone + quest row
    p.setPen(QColor(120, 180, 255));
    p.setFont(QFont(QStringLiteral("Work Sans"), 9, QFont::Bold));
    p.drawText(kMargin, y + kHeaderH - 2, QStringLiteral("区域: 古代树森林"));
    p.setPen(QColor(255, 200, 80));
    p.setFont(QFont(QStringLiteral("Work Sans"), 9));
    const QRectF zr(kMargin, y, kPanelW - 2 * kMargin, kHeaderH);
    p.drawText(zr, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("任务 #66801 ★6 · 进行中"));
    y += kHeaderH + kRowGap;

    // Quest detail row
    p.setPen(QColor(220, 220, 220));
    p.setFont(QFont(QStringLiteral("Work Sans"), 8));
    p.drawText(kMargin, y + kHeaderH - 2, QStringLiteral("剩余 41:37 · 猫车 0/3"));
    y += kHeaderH + kRowGap;

    // HP bar
    p.setPen(Qt::white);
    p.setFont(QFont(QStringLiteral("Work Sans"), 9, QFont::Bold));
    drawBar(p, QRectF(kMargin, y, kPanelW - 2 * kMargin, kBarH), 0.78F, hpColor(0.78F));
    p.drawText(QRectF(kMargin + 6, y, kPanelW - 2 * kMargin - 12, kBarH),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("HP 117/150 78%"));
    y += kBarH + kRowGap;

    // ST bar
    drawBar(p, QRectF(kMargin, y, kPanelW - 2 * kMargin, kBarH), 0.60F, stColor(0.60F));
    p.drawText(QRectF(kMargin + 6, y, kPanelW - 2 * kMargin - 12, kBarH),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("ST 90/150 60%"));
    y += kBarH + kRowGap;

    // Mantle placeholders
    p.setPen(QColor(180, 180, 180));
    p.setFont(QFont(QStringLiteral("Work Sans"), 8));
    p.drawText(kMargin, y + kIconSize - 3, QStringLiteral("[武器] [衣装A 60s] [衣装B 冷却 280s]"));
}