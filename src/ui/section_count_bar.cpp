#include "section_count_bar.h"
#include "ui_theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <algorithm>

SectionCountBar::SectionCountBar(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
}

void SectionCountBar::setAccent(const QColor &c)
{
    if (accent_ == c) return;
    accent_ = c;
    update();
}

void SectionCountBar::setRatio(qreal onOverTotal)
{
    const qreal r = std::clamp(onOverTotal, 0.0, 1.0);
    if (qFuzzyCompare(r, ratio_)) return;
    ratio_ = r;
    update();
}

void SectionCountBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Track: a 1px-inset rounded dark plate matching the section-row
    // tile family, so the bar reads as part of the same visual set.
    const QRectF track = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(track, 2.0, 2.0);
    p.setPen(Qt::NoPen);
    p.fillPath(path, uiTheme().bgTrack);

    // Fill: accent-coloured, width scaled by the ratio. A 4px track
    // leaves at least a rounded nub at 0% and full width at 100%.
    const qreal fillW = track.width() * ratio_;
    if (fillW > 1.0) {
        QPainterPath fp;
        fp.addRoundedRect(QRectF(track.left(), track.top(), fillW, track.height()),
                          2.0, 2.0);
        p.fillPath(fp, accent_);
    }
}
