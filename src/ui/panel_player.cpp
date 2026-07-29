#include "panel_player.h"

#include "core/string_table.h"
#include "player/player_types.h"
#include "quest/quest_types.h"
#include "ui/formatters.h"
#include "ui/icon.h"
#include "world/world_types.h"

#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QFontMetrics>
#include <QLinearGradient>

#include <cmath>
#include <algorithm>

using mhw::Icon;

namespace {

// ---- Layout tokens (mirror HTML v8 .op.player spec) ----
constexpr int kPanelW   = 378;     // HTML: .op width:378px
constexpr int kMargin   = 11;     // HTML: .op padding:11/13 (use 11 for symmetric edges)
constexpr int kTitleH   = 14;     // HTML: .ptitle height (1 line)
constexpr int kQrowH    = 14;     // HTML: .qrow line height (clamp 8-10 font + padding)
constexpr int kBarH     = 15;     // HTML: .bar height:15px
constexpr int kStBarH   = 12;     // was 9 (HTML spec) — too tight for 8px glyphs;
                                     // raised slightly so the text sits comfortably
                                     // inside and aligns with the HP bar padding.
constexpr int kWslot    = 26;     // HTML: .wslot 26x26
constexpr int kMbW      = 66;     // HTML: .mb width:66px
constexpr int kMbH      = 60;     // .mb svg ~26px + cn + tm (tightened 2026-07-27)
constexpr int kPillH    = 18;     // HTML: .pill height (b + span ~9px + padding)
constexpr int kGapQrow  = 3;      // vertical between .qrow lines
constexpr int kGapBar   = 7;      // HTML: .bar margin-bottom:7px
constexpr int kGapSection = 9;    // HTML: .mantlerow/.debuffs margin-top:9 padding-top:9
constexpr int kMantleMaxSec = 120; // common mantle active duration, used
                                     // as the denominator for the
                                     // vertical progress strip.

// ---- Colour palette (HTML CSS variables) ----
constexpr int kHpGreen    = 76;  constexpr int kHpGreenG  = 175; constexpr int kHpGreenB  = 80;
constexpr int kHpAmber    = 255; constexpr int kHpAmberG  = 193; constexpr int kHpAmberB  = 7;
constexpr int kHpRed      = 244; constexpr int kHpRedG    = 67;  constexpr int kHpRedB    = 54;
constexpr int kStBlue     = 33;  constexpr int kStBlueG   = 150; constexpr int kStBlueB   = 243;
constexpr int kStOrange   = 255; constexpr int kStOrangeG = 152; constexpr int kStOrangeB = 0;

constexpr int kConnGreen  = 76;  constexpr int kConnGreenG = 175; constexpr int kConnGreenB = 80;
constexpr int kConnRed    = 244; constexpr int kConnRedG   = 67;  constexpr int kConnRedB   = 54;

// Mantle state borders.
constexpr int kMbActiveR = 76;   constexpr int kMbActiveG  = 175; constexpr int kMbActiveB  = 80;
constexpr int kMbCoolR   = 50;   constexpr int kMbCoolG    = 50;  constexpr int kMbCoolB    = 50;

// ---- Layout sub-routines ----

QColor hpColor(float pct)
{
    if (pct > 0.60F)  return QColor(kHpGreen,   kHpGreenG,   kHpGreenB);
    if (pct > 0.25F)  return QColor(kHpAmber,   kHpAmberG,   kHpAmberB);
    return                 QColor(kHpRed,     kHpRedG,     kHpRedB);
}

QColor stColor(float pct)
{
    if (pct > 0.50F)  return QColor(kStBlue,  kStBlueG,  kStBlueB);
    if (pct > 0.20F)  return QColor(kStOrange,kStOrangeG,kStOrangeB);
    return                 QColor(kHpRed,   kHpRedG,   kHpRedB);
}

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

QString zoneLabel(mhw::Zone z)
{
    if (z == mhw::Zone::Unknown)
        return QStringLiteral("未知区域");
    const QString name = QString::fromUtf8(mhw::zoneName(z));
    return name.isEmpty() ? QStringLiteral("区域%1").arg(static_cast<int>(z)) : name;
}

QString fmtMmSs(float seconds)
{
    if (seconds <= 0.0F)
        return QStringLiteral("--:--");
    const int total = static_cast<int>(seconds);
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0'));
}

// HTML .bar fill: 15px tall, rounded 3px, track deep, fill
// vertical gradient (top lighter, bottom darker). Matches HTML
// `--c-hi`→`--c` gradient semantics.
void drawBarV15(QPainter &p, const QRectF &rect, float pct,
                const QColor &fill, int radius = 3)
{
    const float c = std::clamp(pct, 0.0F, 1.0F);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    // Track: --bg-cell #1d2022
    p.setBrush(QColor(29, 32, 34));
    p.drawRoundedRect(rect, radius, radius);

    if (c > 0.001F) {
        const QRectF fillRect(rect.x(), rect.y(),
                              rect.width() * c, rect.height());
        QLinearGradient fg(fillRect.topLeft(), fillRect.bottomLeft());
        fg.setColorAt(0.0, fill.lighter(108));
        fg.setColorAt(0.5, fill);
        fg.setColorAt(1.0, fill.darker(118));
        p.setBrush(fg);
        p.drawRoundedRect(fillRect, radius, radius);
    }

    // 1px hairline border (HTML: --line #2a2d2f)
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(42, 45, 47), 1));
    p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

// 26x26 slot with 1px --line-hi border, dark recessed body.
void drawWslot(QPainter &p, const QRectF &slotRect, const QPixmap &icon)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(58, 61, 63), 1));
    p.setBrush(QColor(14, 16, 18));
    p.drawRoundedRect(slotRect, 3, 3);
    if (!icon.isNull()) {
        // Inset icon by 1px so the border stays visible.
        const QRectF iconRect(slotRect.x() + 1, slotRect.y() + 1,
                              slotRect.width() - 2, slotRect.height() - 2);
        p.drawPixmap(iconRect.toRect(), icon);
    }
}

// HTML .mb (mantle/debuff box): center icon + name + timer. State via border.
void drawMantleBox(QPainter &p, const QRectF &box, const QPixmap &icon,
                   const QString &name, const QString &timer,
                   bool active, float progress)
{
    // Mantle box: outer frame stays the HTML v8 size; we keep the layout
    // compact (icon 32 + label column + vertical progress strip).
    //   border 1px  | active = teal, cooling = grey
    //   layout:  [ icon 32 ] [ name + timer (stacked, centred) ] | [ vert progress 7px ]
    //   progress: active  = decreasing  (time remaining -> vertical fill)
    //              cooling = increasing  (cooldown filling back up)
    p.setRenderHint(QPainter::Antialiasing);
    const QColor border = active
        ? QColor(kMbActiveR, kMbActiveG, kMbActiveB)
        : QColor(kMbCoolR,   kMbCoolG,   kMbCoolB);
    p.setPen(QPen(border, 1));
    p.setBrush(QColor(22, 24, 26));
    p.drawRoundedRect(box, 3, 3);

    const qreal iconBox = 32.0;
    const QRectF iconRect(box.x() + 4,
                          box.y() + (box.height() - iconBox) / 2.0,
                          iconBox, iconBox);
    if (!icon.isNull())
        p.drawPixmap(iconRect.toRect(), icon);

    // Vertical progress strip on the right edge — bumped from 7px to 10px
    // so the timer state is obvious at a glance, especially during the
    // long cooldown tail of high-maxSec mantles (e.g. Rocksteady 270s).
    const qreal stripW = 10.0;
    const qreal padY = 3.0;
    const QRectF stripRect(box.right() - stripW - 4.0,
                          box.top() + padY,
                          stripW,
                          box.height() - 2 * padY);
    if (stripRect.width() > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(34, 36, 38));             // dark track
        p.drawRoundedRect(stripRect, 2.0, 2.0);
        const qreal clamped = std::clamp(progress, 0.0F, 1.0F);
        const qreal fillH = stripRect.height() * clamped;
        const QRectF fillRect(stripRect.x(),
                              stripRect.bottom() - fillH,
                              stripRect.width(),
                              fillH);
        QColor barColor = active
            ? QColor(110, 200, 230)                  // teal accent
            : QColor(80, 197, 183);                   // also teal during cool-up
        QLinearGradient grad(fillRect.bottomLeft(),
                             fillRect.topLeft());
        grad.setColorAt(0.0, barColor.darker(140));
        grad.setColorAt(1.0, barColor);
        p.setBrush(grad);
        p.drawRoundedRect(fillRect, 2.0, 2.0);
    }

    // Name + timer column (left of the vertical strip).  Two-line stack,
    // vertically centred as a block so the visual mass matches the icon.
    const qreal colRight = stripRect.x() - 4.0;
    const qreal colLeft  = iconRect.right() + 6;
    const qreal colW     = colRight - colLeft;

    const qreal lineGap  = 2.0;
    const qreal nameH    = 11.0;
    const qreal timerH   = 12.0;
    const qreal blockH   = nameH + lineGap + timerH;
    const qreal blockTop = box.y() + (box.height() - blockH) / 2.0;

    QFont cnFont(QStringLiteral("Chakra Petch"), 8);
    cnFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(cnFont);
    p.setPen(QColor(220, 222, 224));
    const QRectF cnRect(colLeft, blockTop,
                        colW, nameH);
    p.drawText(cnRect, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(cnFont).elidedText(name, Qt::ElideRight,
                                                static_cast<int>(colW)));

    QFont tmFont(QStringLiteral("Chakra Petch"), 9, QFont::Medium);
    tmFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(tmFont);
    p.setPen(active ? QColor(110, 200, 230) : QColor(150, 150, 150));
    const QRectF tmRect(colLeft, blockTop + nameH + lineGap,
                        colW, timerH);
    p.drawText(tmRect, Qt::AlignLeft | Qt::AlignVCenter, timer);
}

// HTML .pill: flex item with --pc left border, name on left, timer on right.
void drawPill(QPainter &p, const QRectF &box, const QColor &pc,
              const QString &name, const QString &timer)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(22, 24, 26));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(box, 2, 2);

    // 3px left accent stripe (HTML: border-left:3px solid var(--pc))
    const QRectF stripe(box.x(), box.y(), 3, box.height());
    p.setBrush(pc);
    p.drawRect(stripe);

    // 1px --line outline
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(42, 45, 47), 1));
    p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);

    QFont bFont(QStringLiteral("Chakra Petch"), 9, QFont::Medium);
    bFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(bFont);
    p.setPen(QColor(245, 246, 247));
    const QRectF nameRect(stripe.right() + 6, box.y(),
                          box.width() * 0.55F, box.height());
    p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(bFont).elidedText(name, Qt::ElideRight,
                                                static_cast<int>(nameRect.width())));

    QFont tFont(QStringLiteral("Chakra Petch"), 9);
    tFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(tFont);
    p.setPen(QColor(210, 214, 216));
    p.drawText(QRectF(box.x() + 4, box.y(),
                       box.width() - 8, box.height()),
               Qt::AlignRight | Qt::AlignVCenter, timer);
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
    player_ = p;
    hasData_ = p.valid;
    canvas()->update();
}

void PlayerPanel::update(const mhw::GameSnapshot &snap)
{
    player_    = snap.player;
    zone_      = snap.zone;
    quest_     = snap.quest;
    attached_  = snap.attached;
    pid_       = snap.pid;
    imageBase_ = snap.imageBase;
    status_    = snap.status;
    hasData_   = snap.player.valid;

    // MR / name / weapon all come from the local player struct
    // (PlayerSnapshot) directly.  In the gathering hub the party
    // array is empty, but the player struct still holds valid name,
    // MR and weaponId so we can show them.
    playerMR_   = snap.player.masterRank;
    playerName_ = snap.player.name;
    weaponId_   = snap.player.weaponId;
    partyCount_ = snap.party.size();

    // When party has a local=true snapshot it carries the same fields;
    // prefer those in case the player struct lags by a poll.
    for (const auto &m : snap.party) {
        if (m.local) {
            if (m.masterRank > 0) playerMR_   = m.masterRank;
            if (!m.name.isEmpty())  playerName_ = m.name;
            if (m.weaponId >= 0)    weaponId_   = m.weaponId;
            break;
        }
    }

    // If we are not attached to a real process, reset demo mirrors so
    // the panel doesn't keep stale data from a previous frame.
    if (!snap.player.valid) {
        if (weaponId_ == 0 && playerName_.isEmpty()) {
            // Nothing to show — player struct also returned nothing.
            weaponId_ = -1;
        }
    }

    QSet<int> activeOffsets;
    for (const auto &d : player_.debuffs) {
        activeOffsets.insert(d.offset);
        const auto it = debuffMaxTimers_.find(d.offset);
        if (it == debuffMaxTimers_.end() || d.timer > *it)
            debuffMaxTimers_[d.offset] = d.timer;
    }
    for (auto it = debuffMaxTimers_.begin(); it != debuffMaxTimers_.end(); ) {
        if (!activeOffsets.contains(it.key()))
            it = debuffMaxTimers_.erase(it);
        else
            ++it;
    }

    canvas()->update();
}

void PlayerPanel::paintPanel(QPainter &p)
{
    drawV03Chrome(p, Panel::Accent::Player);

    // Disconnected placeholder (HTML v8 keeps the chrome + a small
    // status block; no data rows render).
    if (!attached_) {
        const int totalH = kMargin + kTitleH + kQrowH + kMargin;
        setContentSize(kPanelW, totalH);
        const QRectF titleRect(kMargin, kMargin,
                               kPanelW - 2 * kMargin, kTitleH);
        QFont tFont(QStringLiteral("Chakra Petch"), 9, QFont::Medium);
        tFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
        tFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(tFont);
        p.setPen(QColor(255, 255, 255));
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("玩家 PLAYER"));
        p.setPen(QColor(110, 110, 110));
        QFont sFont(QStringLiteral("Chakra Petch"), 9);
        sFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(sFont);
        p.setPen(QColor(kConnRed, kConnRedG, kConnRedB));
        p.drawText(titleRect, Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("○ 未连接"));
        return;
    }

    if (!hasData_)
        return;

    const int mantleCount = (player_.mantleSlot0Id >= 0 ? 1 : 0)
                          + (player_.mantleSlot1Id >= 0 ? 1 : 0);
    const int debuffCount = player_.debuffs.size();

    // ---- Compute height up front ----
    int totalH = kMargin
                + kTitleH                              // title row
                + kQrowH * 4                           // 4 quest rows
                + kGapQrow * 3                         // gap between qrows
                + kGapSection                          // gap before prow
                + 26 + kGapBar                         // prow (wslot 26) + bar gap
                + kBarH + kGapBar                      // HP bar
                + kStBarH + kGapBar                    // ST bar
                + kMargin;
    if (mantleCount > 0) totalH += kGapSection + kMbH;
    if (debuffCount > 0) {
        constexpr int kPillsPerRow = 3;
        const int debuffRows = (debuffCount + kPillsPerRow - 1) / kPillsPerRow;
        totalH += kGapSection + debuffRows * kPillH + (debuffRows - 1) * 4;
    }
    setContentSize(kPanelW, totalH);

    int y = kMargin;
    const int innerLeft  = kMargin;
    const int innerRight = kPanelW - kMargin;
    const int innerW     = innerRight - innerLeft;

    // ---- Title row: "玩家 PLAYER" + right "self" tag (HTML v8) ----
    {
        QFont tFont(QStringLiteral("Chakra Petch"), 9, QFont::Medium);
        tFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
        tFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(tFont);
        p.setPen(QColor(245, 246, 247));
        const QRectF titleRect(innerLeft, y, innerW, kTitleH);
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("玩家 PLAYER"));
        p.setPen(QColor(110, 110, 110));
        QFont iFont(QStringLiteral("Chakra Petch"), 9);
        iFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(iFont);
        p.drawText(titleRect, Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("self"));
        y += kTitleH;
    }

    // ---- qblock: 4 quest rows (.qrow class) ----
    QFont qFontL(QStringLiteral("Chakra Petch"), 9);
    qFontL.setStyleStrategy(QFont::PreferAntialias);
    QFont qFontL10(QStringLiteral("Chakra Petch"), 10, QFont::Medium);
    qFontL10.setStyleStrategy(QFont::PreferAntialias);
    QFont qFontL12(QStringLiteral("Chakra Petch"), 12, QFont::Bold);
    qFontL12.setStyleStrategy(QFont::PreferAntialias);
    QFont qFontR(QStringLiteral("Chakra Petch"), 10, QFont::Medium);
    qFontR.setStyleStrategy(QFont::PreferAntialias);
    // Smaller 8px variant used for the meta line (连接状态/队伍计数).
    QFont qFontSmallL(QStringLiteral("Chakra Petch"), 8);
    qFontSmallL.setStyleStrategy(QFont::PreferAntialias);
    QFont qFontSmallR(QStringLiteral("Chakra Petch"), 8, QFont::Medium);
    qFontSmallR.setStyleStrategy(QFont::PreferAntialias);

    struct QrowStyle {
        const QFont  *leftFont;
        const QFont  *rightFont;
        QColor        leftCol;
        QColor        rightCol;
        bool          leftDot{false};
        QColor        dotCol;
    };
    auto drawQrow = [&p, &y, innerLeft, innerW, this](
                        const QString &left, const QString &right,
                        QrowStyle st) {
        const QRectF rect(innerLeft, y, innerW, kQrowH);
        p.setFont(*st.leftFont);
        p.setPen(st.leftCol);
        if (st.leftDot) {
            p.setBrush(st.dotCol);
            p.setPen(Qt::NoPen);
            const qreal r = 2.5;
            p.drawEllipse(QPointF(innerLeft + 5, y + kQrowH / 2.0), r, r);
            p.setPen(st.leftCol);
            p.setBrush(Qt::NoBrush);
            p.drawText(rect.adjusted(12, 0, 0, 0),
                       Qt::AlignLeft | Qt::AlignVCenter, left);
        } else {
            p.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, left);
        }
        p.setFont(*st.rightFont);
        p.setPen(st.rightCol);
        p.drawText(rect, Qt::AlignRight | Qt::AlignVCenter, right);
        y += kQrowH + kGapQrow;
    };

    // Row 1: 已连接 · PID 12345 · BASE 0x7FFE0000  + 4H 队伍
    {
        const QString mem = QStringLiteral("PID %1 · BASE 0x%2")
                                .arg(pid_ > 0 ? QString::number(pid_)
                                              : QStringLiteral("--"))
                                .arg(imageBase_
                                       ? QString::number(static_cast<qulonglong>(imageBase_), 16)
                                       : QStringLiteral("--"));
        const QString partyLabel = partyCount_ > 0
            ? QStringLiteral("%1人").arg(partyCount_)
            : QStringLiteral("--");
        drawQrow(QStringLiteral("已连接 · %1").arg(mem),
                     partyLabel,
                     QrowStyle{&qFontSmallL, &qFontSmallR,
                           QColor(120, 122, 124),
                           QColor(120, 122, 124),
                           true,
                           QColor(kConnGreen, kConnGreenG, kConnGreenB)});
    }

    // Row 2: 区域 · 任务  +  古代树森林#66801 ★6 · 进行中
    {
        QString zone = zoneLabel(zone_);
        if (zone.length() > 6) zone = zone.left(6) + QStringLiteral("…");
        const QString questTxt = quest_.active
            ? QStringLiteral("%1#%2 ★%3 · %4")
                  .arg(zone)
                  .arg(quest_.id)
                  .arg(quest_.stars)
                  .arg(questStateLabel(quest_.state))
            : QStringLiteral("%1 · 空闲").arg(zone);
        drawQrow(QStringLiteral("区域 · 任务"),
                 questTxt,
                 QrowStyle{&qFontL, &qFontL10,
                           QColor(146, 148, 149),          // --t3 label
                           QColor(255, 214, 107)});         // .qrow.zq .qr gold #ffd66b
    }

    // Row 3: 剩余  +  41:37
    {
        const QString timerTxt = quest_.active
            ? fmtMmSs(quest_.timeLeftSeconds)
            : QStringLiteral("--:--");
        drawQrow(QStringLiteral("剩余"),
                 timerTxt,
                 QrowStyle{&qFontL, &qFontL12,
                           QColor(146, 148, 149),
                           QColor(80, 197, 183)});          // .qrow.timer .qr teal #50c5b7
    }

    // Row 4: 猫车  +  0 / 3
    {
        const QString cart = quest_.active
            ? QStringLiteral("%1 / %2")
                  .arg(quest_.deaths).arg(quest_.maxDeaths)
            : QStringLiteral("0 / 3");
        drawQrow(QStringLiteral("猫车"),
                 cart,
                 QrowStyle{&qFontL, &qFontL10,
                           QColor(146, 148, 149),
                           QColor(255, 118, 118)});         // .qrow.cart .qr red #ff7676
    }

    y -= kGapQrow;          // last qrow has no trailing inter-row gap
    y += kGapSection;       // separator before prow (HTML .mantlerow padding-top:9)

    // ---- prow: weapon slot + name + MR ----
    {
        const int prowH = 26;             // .wslot height
        const QRectF slotRect(innerLeft, y, kWslot, prowH);
        const QString wp = weaponId_ >= 0
            ? Icon::weaponPath(weaponId_, 1) : QString();
        drawWslot(p, slotRect, Icon::render(wp, kWslot));

        QFont nFont(QStringLiteral("Chakra Petch"), 12, QFont::Medium);
        nFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(nFont);
        p.setPen(QColor(245, 246, 247));
        const QRectF nameRect(slotRect.right() + 8, y,
                              innerW - kWslot - 8, prowH);
        const QString nm = playerName_.isEmpty()
            ? QStringLiteral("--")   // no local player in party yet
            : playerName_;
        p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, nm);

        QFont mrFont(QStringLiteral("Chakra Petch"), 9);
        mrFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(mrFont);
        p.setPen(QColor(146, 148, 149));
        const int mr = playerMR_ > 0 ? playerMR_ : 0;
        const QString mrTxt = playerMR_ > 0
            ? QStringLiteral("MR %1").arg(mr)
            : QStringLiteral("MR --");
        p.drawText(nameRect, Qt::AlignRight | Qt::AlignVCenter, mrTxt);
        y += prowH;
    }

    y += kGapBar;

    // ---- HP bar ----
    const bool hpKnown = (player_.valid && player_.maxHealth > 0.0F);
    const float hpPct = hpKnown
        ? player_.health / player_.maxHealth : 0.0F;
    drawBarV15(p, QRectF(innerLeft, y, innerW, kBarH), hpPct, hpColor(hpPct));
    {
        QFont vFont(QStringLiteral("Chakra Petch"), 9, QFont::Medium);
        vFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(vFont);
        p.setPen(QColor(245, 246, 247));
        const QRectF barRect(innerLeft, y, innerW, kBarH);
        const QString leftText = hpKnown
            ? QStringLiteral("%1 / %2")
                  .arg(mhw::groupNumber(static_cast<int>(player_.health)))
                  .arg(mhw::groupNumber(static_cast<int>(player_.maxHealth)))
            : QStringLiteral("-- / --");
        const QString rightText = hpKnown
            ? mhw::percentage(player_.health, player_.maxHealth)
            : QStringLiteral("--%");
        p.drawText(barRect.adjusted(8, 0, -8, 0),
                   Qt::AlignLeft | Qt::AlignVCenter, leftText);
        p.drawText(barRect.adjusted(8, 0, -8, 0),
                   Qt::AlignRight | Qt::AlignVCenter, rightText);
    }
    y += kBarH + kGapBar;

    // ---- ST bar (9px tall — HTML .bar.st) ----
    const bool stKnown = hpKnown && player_.maxStamina > 0.0F;
    const float stPct = stKnown
        ? player_.stamina / player_.maxStamina : 0.0F;
    drawBarV15(p, QRectF(innerLeft, y, innerW, kStBarH), stPct, stColor(stPct));
    {
        QFont sFont(QStringLiteral("Chakra Petch"), 8);
        sFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(sFont);
        p.setPen(QColor(245, 246, 247));
        const QRectF barRect(innerLeft, y, innerW, kStBarH);
        const QString leftText = stKnown
            ? QStringLiteral("%1 / %2")
                  .arg(mhw::groupNumber(static_cast<int>(player_.stamina)))
                  .arg(mhw::groupNumber(static_cast<int>(player_.maxStamina)))
            : QStringLiteral("-- / --");
        const QString rightText = stKnown
            ? mhw::percentage(player_.stamina, player_.maxStamina)
            : QStringLiteral("--%");
        p.drawText(barRect.adjusted(8, 0, -8, 0),
                   Qt::AlignLeft | Qt::AlignVCenter, leftText);
        p.drawText(barRect.adjusted(8, 0, -8, 0),
                   Qt::AlignRight | Qt::AlignVCenter, rightText);
    }
    y += kStBarH + kGapBar;

    // ---- mantlerow: 2 mantle boxes (.mb grid) ----
    if (mantleCount > 0) {
        y += kGapSection - kGapBar;     // align with HTML separator
        const int totalW = innerW;
        const int gap    = 8;            // HTML gap:8 inside .mantlerow
        const int slotW  = (totalW - gap * (mantleCount - 1)) / mantleCount;
        const QRectF m0(innerLeft, y, slotW, kMbH);
        const QRectF m1(innerLeft + slotW + gap, y, slotW, kMbH);

        if (player_.mantleSlot0Id >= 0) {
            const bool active = (player_.mantleSlot0Timer > 0.0F);
            const QString name = mhw::mantleName(player_.mantleSlot0Id);
            const QString timer = active
                ? QStringLiteral("%1s").arg(static_cast<int>(player_.mantleSlot0Timer))
                : QStringLiteral("%1s")
                      .arg(static_cast<int>(player_.mantleSlot0Cooldown));
            const QString mp = Icon::mantlePath(player_.mantleSlot0Id);
            const float progress = active
                ? (player_.mantleSlot0Timer / static_cast<float>(kMantleMaxSec))
                : (1.0F - player_.mantleSlot0Cooldown
                       / player_.mantleSlot0CooldownMax);
            drawMantleBox(p, m0, Icon::render(mp, 32), name, timer, active,
                          std::clamp(progress, 0.0F, 1.0F));
        }
        if (player_.mantleSlot1Id >= 0) {
            const bool active = (player_.mantleSlot1Timer > 0.0F);
            const QString name = mhw::mantleName(player_.mantleSlot1Id);
            const QString timer = active
                ? QStringLiteral("%1s").arg(static_cast<int>(player_.mantleSlot1Timer))
                : QStringLiteral("%1s")
                      .arg(static_cast<int>(player_.mantleSlot1Cooldown));
            const QString mp = Icon::mantlePath(player_.mantleSlot1Id);
            const float progress1 = active
                ? (player_.mantleSlot1Timer / static_cast<float>(kMantleMaxSec))
                : (1.0F - player_.mantleSlot1Cooldown
                       / player_.mantleSlot1CooldownMax);
            drawMantleBox(p, m1, Icon::render(mp, 32), name, timer, active,
                          std::clamp(progress1, 0.0F, 1.0F));
        }
        y += kMbH;
    }

    // ---- debuffs: .pill row, max 3 per row, wraps to a second line ----
    if (debuffCount > 0) {
        y += kGapSection;
        const int totalW = innerW;
        const int pillGap = 5;
        constexpr int kPillsPerRow = 3;
        const int rows = (debuffCount + kPillsPerRow - 1) / kPillsPerRow;
        for (int i = 0; i < debuffCount; ++i) {
            const int rowIdx    = i / kPillsPerRow;
            const int colIdx    = i % kPillsPerRow;
            const int itemsInRow = std::min(kPillsPerRow, debuffCount - rowIdx * kPillsPerRow);
            const int slotW = (totalW - pillGap * (itemsInRow - 1)) / itemsInRow;
            const int cx    = innerLeft + colIdx * (slotW + pillGap);
            const QRectF pillRect(cx, y + rowIdx * (kPillH + 4), slotW, kPillH);
            const auto &d = player_.debuffs[i];
            // Pill accent colours per type. Demo: purple for 毒, orange for 爆破.
            const QString n = d.name.isEmpty() ? QStringLiteral("状态") : d.name;
            const QString t = QStringLiteral("%1s").arg(static_cast<int>(d.timer));
            QColor pc(167, 79, 255);     // default purple
            if (n.contains(QStringLiteral("爆破"))) pc = QColor(255, 87, 34);
            else if (n.contains(QStringLiteral("火"))) pc = QColor(255, 87, 34);
            else if (n.contains(QStringLiteral("防御"))) pc = QColor(255, 193, 7);
            else if (n.contains(QStringLiteral("眠")))   pc = QColor(120, 120, 220);
            else if (n.contains(QStringLiteral("麻")))   pc = QColor(180, 130, 220);
            drawPill(p, pillRect, pc, n, t);
        }
        y += rows * kPillH + (rows - 1) * 4;
    }
}

void PlayerPanel::setupDemoData()
{
    using namespace mhw;

    attached_ = true;
    pid_       = 12345;
    imageBase_ = 0x7FFE00000000ULL;
    zone_      = mhw::Zone::AncientForest;
    quest_     = {66801, 6, 2, 0, 0, 3, 2497.0F, true};
    status_    = QStringLiteral("示例 Demo");

    player_ = mhw::PlayerSnapshot{};
    player_.valid = true;
    player_.health = 132.0F;
    player_.maxHealth = 150.0F;
    player_.stamina = 93.0F;
    player_.maxStamina = 150.0F;
    player_.mantleSlot0Id = 0;     // Ghillie
    player_.mantleSlot0Timer = 42.0F;
    player_.mantleSlot1Id = 3;     // Rocksteady (cooling)
    player_.mantleSlot1Cooldown = 96.0F;
    weaponId_ = 0;                // Great Sword
    playerMR_ = 247;
    playerName_ = QStringLiteral("苍蓝星");   // demo local player name
    partyCount_ = 4;                         // demo party size
    {
        PlayerAbnormality d1;
        d1.offset = 0; d1.name = QStringLiteral("毒");
        d1.timer = 12.0F; d1.maxTimer = 60.0F;
        player_.debuffs.append(d1);
    }
    {
        PlayerAbnormality d2;
        d2.offset = 1; d2.name = QStringLiteral("爆破");
        d2.timer = 41.0F; d2.maxTimer = 60.0F;
        player_.debuffs.append(d2);
    }
    // Extra debuffs to demo the 3-per-row wrap into a second line.
    {
        PlayerAbnormality d3;
        d3.offset = 2; d3.name = QStringLiteral("麻");
        d3.timer = 17.0F; d3.maxTimer = 30.0F;
        player_.debuffs.append(d3);
    }
    {
        PlayerAbnormality d4;
        d4.offset = 3; d4.name = QStringLiteral("眠");
        d4.timer = 28.0F; d4.maxTimer = 45.0F;
        player_.debuffs.append(d4);
    }
    {
        PlayerAbnormality d5;
        d5.offset = 4; d5.name = QStringLiteral("防御DOWN");
        d5.timer = 60.0F; d5.maxTimer = 90.0F;
        player_.debuffs.append(d5);
    }

    hasData_ = true;
}