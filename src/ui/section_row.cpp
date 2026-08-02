#include "section_row.h"
#include "ui_theme.h"

#include <cmath>
#include <QColor>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QVariantAnimation>

namespace {
// Linear colour lerp for the on/off icon transition.
QColor lerpColor(const QColor &a, const QColor &b, qreal t)
{
    auto mix = [t](int x, int y) { return int(x + (y - x) * t); };
    return QColor(mix(a.red(),   b.red()),
                  mix(a.green(), b.green()),
                  mix(a.blue(),  b.blue()));
}
} // namespace

SectionRow::SectionRow(const QString &zh, const QString &key,
                       int icon, QWidget *parent)
    : QWidget(parent), zhText_(zh), keyText_(key), icon_(icon)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, false);

    auto *vl = new QHBoxLayout(this);
    vl->setContentsMargins(14, 6, 12, 6);
    vl->setSpacing(0);

    // The icon is painted in paintEvent (geometric glyph). Reserve the
    // leading zone for it.
    zh_ = new QLabel(zhText_);
    vl->addSpacing(16);   // icon zone
    vl->addWidget(zh_, 1);

    // R4b: HTML mockup uses UPPERCASE keys (QUEST / WSLOT / GAUGE …).
    key_ = new QLabel(keyText_.toUpper());
    vl->addWidget(key_, 0);
    setLayout(vl);
    refreshTheme();
}

void SectionRow::setAccent(const QColor &c)
{
    accent_ = c;
    update();
}

void SectionRow::refreshTheme()
{
    if (!zh_ || !key_)
        return;
    const QString zhColour = on_ ? uiTheme().fg.name()
                                 : uiTheme().fgMuted.name();
    zh_->setStyleSheet(QStringLiteral(
        "color:%1;font-family:'Noto Sans SC';font-size:12px;"
        "background:transparent;border:none;").arg(zhColour));
    key_->setStyleSheet(QStringLiteral(
        "color:%1;font-family:'Chakra Petch';font-weight:600;"
        "font-size:10px;letter-spacing:1.5px;"
        "background:transparent;border:none;").arg(uiTheme().fgMuted.name()));
    update();
}

void SectionRow::setChecked(bool c) {
    if (on_ == c) return;
    on_ = c;
    // Inline child styles must be regenerated with the current theme.
    refreshTheme();

    // v0.5 P2: ease the icon's fill/colour over 200ms.
    if (!anim_) {
        anim_ = new QVariantAnimation(this);
        anim_->setDuration(200);
        anim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &v){ onT_ = v.toDouble(); update(); });
    }
    anim_->stop();
    anim_->setStartValue(onT_);
    anim_->setEndValue(on_ ? 1.0 : 0.0);
    anim_->start();
}

void SectionRow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // R4b: chip tile background — per-row rounded dark plate, 1px inset.
    const QRectF tile(rect().adjusted(1, 1, -1, -1));
    QPainterPath path;
    path.addRoundedRect(tile, 4.0, 4.0);
    p.fillPath(path, uiTheme().tileDark);
    p.setPen(uiTheme().tileHairline);
    p.drawPath(path);

    // Icon at left, vertically centred.
    const QRectF box(6.0, height() / 2.0 - 7.0, 14.0, 14.0);
    drawIcon(p, box);
}

void SectionRow::drawIcon(QPainter &p, const QRectF &box) const
{
    // on/off easing: colour lerps grey→accent, fill alpha lerps 0→~140,
    // so off = hollow grey glyph, on = filled accent glyph.
    const QColor grey = uiTheme().fgMuted;
    const QColor col = lerpColor(grey, accent_, onT_);
    p.setPen(QPen(col, 1.4));
    const int fillAlpha = int(onT_ * 140);
    QColor fill = accent_;
    fill.setAlpha(fillAlpha);
    p.setBrush(fillAlpha > 0 ? QBrush(fill) : Qt::NoBrush);

    const qreal x = box.x(), y = box.y(), w = box.width(), h = box.height();
    const QPointF c = box.center();

    switch (icon_) {
    case IconConn: {   // signal arcs
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < 3; ++i) {
            const qreal r = 2.5 + i * 3.2;
            p.drawArc(QRectF(c.x() - r, c.y() - r, r * 2, r * 2),
                      30 * 16, 120 * 16);
        }
        p.setPen(Qt::NoPen); p.setBrush(col);
        p.drawEllipse(c, 1.6, 1.6);
        break;
    }
    case IconQuest: {  // flag on a pole
        p.drawLine(QPointF(x + 3, y + 1), QPointF(x + 3, y + h - 1));
        QPainterPath fp; fp.moveTo(x + 3, y + 1);
        fp.lineTo(x + w - 1, y + 4); fp.lineTo(x + 3, y + 7); fp.closeSubpath();
        p.drawPath(fp);
        break;
    }
    case IconWeapon: { // sword (diagonal blade + crossguard)
        p.drawLine(QPointF(x + 2, y + h - 2), QPointF(x + w - 2, y + 2));
        p.drawLine(QPointF(x + w - 6, y + 2), QPointF(x + w - 2, y + 6));
        p.drawLine(QPointF(x + 3, y + h - 6), QPointF(x + 7, y + h - 2));
        break;
    }
    case IconBars: {   // two stacked bars (HP/ST)
        p.setBrush(fillAlpha > 0 ? QBrush(fill) : Qt::NoBrush);
        p.drawRoundedRect(QRectF(x + 1, y + 2, w - 2, 4), 1.5, 1.5);
        p.drawRoundedRect(QRectF(x + 1, y + h - 6, w - 2, 4), 1.5, 1.5);
        break;
    }
    case IconMantles: { // two side-by-side tiles (衣装两格)
        p.drawRoundedRect(QRectF(x + 1, y + 2, (w - 4) / 2, h - 4), 1.5, 1.5);
        p.drawRoundedRect(QRectF(x + w - 1 - (w - 4) / 2, y + 2, (w - 4) / 2, h - 4), 1.5, 1.5);
        break;
    }
    case IconDebuff: { // capsule (pill)
        p.drawRoundedRect(QRectF(x + 1, y + h/2 - 2.5, w - 2, 5), 2.5, 2.5);
        p.drawLine(QPointF(x + w/2, y + h/2 - 2.5), QPointF(x + w/2, y + h/2 + 2.5));
        break;
    }
    case IconInfo: {   // hexagon (六角肖像)
        QPainterPath hp; const qreal cx = c.x(), cy = c.y(), r = w/2 - 1;
        for (int i = 0; i < 6; ++i) {
            const qreal a = (60.0 * i - 90.0) * M_PI / 180.0;
            const QPointF pt(cx + r * std::cos(a), cy + r * std::sin(a));
            i ? hp.lineTo(pt) : hp.moveTo(pt);
        }
        hp.closeSubpath(); p.drawPath(hp);
        break;
    }
    case IconHp: {     // heart
        QPainterPath hp; const qreal s = w / 2 - 1;
        hp.moveTo(c.x(), c.y() + s * 0.7);
        hp.cubicTo(c.x() - s * 1.4, c.y() - s * 0.4, c.x() - s * 0.4, c.y() - s, c.x(), c.y() - s * 0.3);
        hp.cubicTo(c.x() + s * 0.4, c.y() - s, c.x() + s * 1.4, c.y() - s * 0.4, c.x(), c.y() + s * 0.7);
        p.drawPath(hp);
        break;
    }
    case IconEnrage: { // flame
        QPainterPath fp;
        fp.moveTo(c.x(), y + 1);
        fp.cubicTo(c.x() + w*0.45, y + h*0.35, c.x() + w*0.25, y + h*0.5, c.x() + w*0.3, y + h*0.7);
        fp.cubicTo(c.x() + w*0.32, y + h, c.x() - w*0.35, y + h, c.x() - w*0.28, y + h*0.62);
        fp.cubicTo(c.x() - w*0.4, y + h*0.45, c.x() - w*0.1, y + h*0.3, c.x(), y + 1);
        p.drawPath(fp);
        break;
    }
    case IconAil: {    // diamond capsule (异常)
        QPainterPath dp; dp.moveTo(c.x(), y + 1); dp.lineTo(x + w - 1, c.y());
        dp.lineTo(c.x(), y + h - 1); dp.lineTo(x + 1, c.y()); dp.closeSubpath();
        p.drawPath(dp);
        break;
    }
    case IconParts: {  // 2x2 grid
        const qreal g = (w - 6) / 2;
        p.drawRoundedRect(QRectF(x + 1, y + 1, g, g), 1.2, 1.2);
        p.drawRoundedRect(QRectF(x + 1 + g + 2, y + 1, g, g), 1.2, 1.2);
        p.drawRoundedRect(QRectF(x + 1, y + 1 + g + 2, g, g), 1.2, 1.2);
        p.drawRoundedRect(QRectF(x + 1 + g + 2, y + 1 + g + 2, g, g), 1.2, 1.2);
        break;
    }
    case IconRows: {   // three horizontal lines
        for (int i = 0; i < 3; ++i) {
            const qreal ly = y + 3 + i * (h - 6) / 2.0;
            p.drawLine(QPointF(x + 2, ly), QPointF(x + w - 2, ly));
        }
        break;
    }
    case IconShare: {  // stacked horizontal bar
        p.setBrush(fillAlpha > 0 ? QBrush(fill) : Qt::NoBrush);
        p.drawRoundedRect(QRectF(x + 1, c.y() - 2.5, (w - 2) * 0.6, 5), 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(x + 1 + (w - 2) * 0.6, c.y() - 2.5, (w - 2) * 0.4, 5), 1.5, 1.5);
        break;
    }
    case IconChart: {  // line chart going up
        QPainterPath cp; cp.moveTo(x + 1, y + h - 2);
        cp.lineTo(x + w*0.4, y + h*0.5); cp.lineTo(x + w*0.6, y + h*0.65);
        cp.lineTo(x + w - 1, y + 2); p.drawPath(cp);
        break;
    }
    default: {         // IconNone → legacy plain dot
        const QPointF dc(13.0, height() / 2.0);
        const qreal r = 4.0;
        if (on_) {
            p.setPen(Qt::NoPen); p.setBrush(col); p.drawEllipse(dc, r, r);
        } else {
            p.setPen(QPen(col, 1.2)); p.setBrush(Qt::NoBrush); p.drawEllipse(dc, r, r);
        }
        break;
    }
    }
}

void SectionRow::mousePressEvent(QMouseEvent *) {}
void SectionRow::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
        setChecked(!on_);
        emit stateChanged(on_ ? Qt::Checked : Qt::Unchecked);
    }
}
