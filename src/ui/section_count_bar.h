#pragma once

#include <QColor>
#include <QWidget>
#include <QtGlobal>

// v0.5 P2: a thin self-painted progress bar shown under the
// "N OF M SECTIONS VISIBLE" headline. It visualises the enabled
// section ratio (on / total) in the panel's accent colour, giving
// the inspector a denser at-a-glance readout than the text alone.
//
// Pure presentation: the caller owns the ratio and the colour, the
// bar just draws them. Matches the project's all-QPainter, no-qrc,
// no-svg discipline.
class SectionCountBar : public QWidget {
    Q_OBJECT
public:
    explicit SectionCountBar(QWidget *parent = nullptr);

    void setAccent(const QColor &c);
    void setRatio(qreal onOverTotal);   // clamped to [0, 1]

    QSize sizeHint() const override { return QSize(160, 4); }
    QSize minimumSizeHint() const override { return QSize(80, 4); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QColor accent_{80, 197, 183};
    qreal ratio_{1.0};
};
