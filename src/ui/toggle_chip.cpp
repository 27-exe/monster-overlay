#include "toggle_chip.h"
#include "ui_theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>

// R3: iOS-style capsule toggle. Draws a 40×22 track (rounded rect) plus
// a 14px circle handle that slides between left (off) and right (on)
// positions. Track colour: off = #1d2022, on = #50c5b7 (teal — the same
// teal used by the logo dot and v0.4 ON state). Handle is #e7e8e9 with
// a subtle drop shadow painted as a darker outline behind it.
//
// Hover raises track brightness 10% so the control feels alive without
// the stylesheet dance that QCheckBox::indicator forces.

ToggleChip::ToggleChip(QWidget *parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void ToggleChip::setChecked(bool c) {
    if (on_ == c) return;
    on_ = c;
    update();
}

void ToggleChip::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal h = height();
    const qreal w = width();
    const qreal rad = h / 2.0;

    // ---- Track ----
    QColor track = on_ ? QColor(80, 197, 183)
                       : uiTheme().tileDark;
    if (hover_) {
        track = on_ ? QColor(110, 215, 200)
                    : uiTheme().tileHover;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(QRectF(0, 0, w, h), rad, rad);

    // ---- Handle ----
    const qreal hd = h - 6.0;     // 16
    const qreal margin = 3.0;
    const qreal xL = margin;
    const qreal xR = w - hd - margin;
    const qreal xc = on_ ? xR : xL;

    // Subtle shadow ring (a slightly darker ring behind the handle).
    p.setBrush(QColor(0, 0, 0, 60));
    p.drawEllipse(QPointF(xc + hd/2, h/2), hd/2 + 1, hd/2 + 1);

    p.setBrush(uiTheme().fg);
    p.drawEllipse(QPointF(xc + hd/2, h/2), hd/2, hd/2);
}

void ToggleChip::mousePressEvent(QMouseEvent *) {
    // No-op: we wait for release to toggle (avoids accidental drag-toggles).
}

void ToggleChip::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
        setChecked(!on_);
        emit stateChanged(on_ ? Qt::Checked : Qt::Unchecked);
    }
}

void ToggleChip::enterEvent(QEnterEvent *) {
    hover_ = true;
    update();
}

void ToggleChip::leaveEvent(QEvent *) {
    hover_ = false;
    update();
}