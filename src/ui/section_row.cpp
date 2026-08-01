#include "section_row.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

SectionRow::SectionRow(const QString &zh, const QString &key, QWidget *parent)
    : QWidget(parent), zhText_(zh), keyText_(key)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, false);

    auto *vl = new QHBoxLayout(this);
    vl->setContentsMargins(14, 6, 12, 6);
    vl->setSpacing(0);

    // The dot is painted in paintEvent so we can switch freely between
    // filled-circle (on, green) and hollow-ring (off, dim stroke).
    zh_ = new QLabel(zhText_);
    zh_->setStyleSheet(QStringLiteral(
        "color:#e7e8e9;font-family:'Noto Sans SC';font-size:12px;"
        "background:transparent;border:none;"));
    vl->addSpacing(16);   // dot zone (~dot radius 5 + gap)
    vl->addWidget(zh_, 1);

    // R4b: HTML mockup uses UPPERCASE keys (QUEST / WSLOT / GAUGE …).
    // upper() here keeps the data layer case-agnostic.
    key_ = new QLabel(keyText_.toUpper());
    key_->setStyleSheet(QStringLiteral(
            "color:#6f7375;font-family:'Chakra Petch';font-weight:600;"
            "font-size:10px;letter-spacing:1.5px;"
            "background:transparent;border:none;"));
    vl->addWidget(key_, 0);
    setLayout(vl);
}

void SectionRow::setChecked(bool c) {
    if (on_ == c) return;
    on_ = c;
    // R4b off-state dims the text (HTML mockup effect).
    const QString zhColour = on_ ? QStringLiteral("#e7e8e9")
                                 : QStringLiteral("#929495");
    zh_->setStyleSheet(QStringLiteral(
        "color:%1;font-family:'Noto Sans SC';font-size:12px;"
        "background:transparent;border:none;")
        .arg(zhColour));
    key_->setStyleSheet(QStringLiteral(
            "color:#6f7375;font-family:'Chakra Petch';font-weight:600;"
            "font-size:10px;letter-spacing:1.5px;"
            "background:transparent;border:none;"));
    update();
}

void SectionRow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // R4b: chip tile background — matches HTML's per-row rounded dark
    // plate. The plate sits 1px inset so adjacent tiles get a 2px gap.
    const QRectF tile(rect().adjusted(1, 1, -1, -1));
    QPainterPath path;
    path.addRoundedRect(tile, 4.0, 4.0);
    p.fillPath(path, QColor(22, 24, 26));   // #16181a
    p.setPen(QColor(38, 41, 43));          // #26292b hairline
    p.drawPath(path);

    // Dot at left, vertical-centre. On = filled teal, off = hollow ring.
    const QPointF c(13.0, height() / 2.0);
    const qreal r = 4.0;
    if (on_) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(80, 197, 183));   // #50c5b7 (same as logo dot)
        p.drawEllipse(c, r, r);
    } else {
        p.setPen(QPen(QColor(96, 100, 102), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, r, r);
    }
}

void SectionRow::mousePressEvent(QMouseEvent *) {}
void SectionRow::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
        setChecked(!on_);
        emit stateChanged(on_ ? Qt::Checked : Qt::Unchecked);
    }
}