#pragma once

#include <QColor>
#include <QWidget>

class QLabel;
class QVariantAnimation;

// R4 sub-switch: a row with three slots:
//   [icon]  <chinese label>          <grey english key>
// Click anywhere on the row to toggle. On = panel-accent icon (filled)
// + bright text; off = dim grey icon (hollow) + muted text.
//
// v0.5 P2: the plain teal/grey dot is replaced by a small per-section
// geometric glyph (drawn, not an svg/png — matches the all-QPainter
// discipline). The on/off transition is eased over 200ms; the logical
// checked state (isChecked / stateChanged) stays immediate.
class SectionRow : public QWidget {
    Q_OBJECT
public:
    // Which glyph to draw. None keeps the legacy plain dot so any
    // caller that doesn't supply a kind renders exactly as before.
    enum Icon {
        IconNone = -1,
        // Player panel
        IconConn, IconQuest, IconWeapon, IconBars, IconMantles, IconDebuff,
        // Monster panel
        IconInfo, IconHp, IconEnrage, IconAil, IconParts,
        // Damage panel
        IconRows, IconShare, IconChart,
    };

    explicit SectionRow(const QString &zh, const QString &key,
                        int icon = IconNone, QWidget *parent = nullptr);

    bool isChecked() const { return on_; }
    void setChecked(bool c);

    // Panel accent used for the icon's on-state colour (purple/orange/teal).
    void setAccent(const QColor &c);

    QSize sizeHint() const override { return QSize(160, 24); }
    QSize minimumSizeHint() const override { return QSize(120, 24); }

signals:
    void stateChanged(int state);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    void drawIcon(QPainter &p, const QRectF &box) const;

    QLabel *zh_ = nullptr;
    QLabel *key_ = nullptr;
    QString zhText_;
    QString keyText_;
    int icon_{IconNone};
    QColor accent_{80, 197, 183};   // teal default
    bool on_{true};

    // v0.5 P2: eased on/off. onT_ runs 0(off)..1(on); driven by anim_.
    qreal onT_{1.0};
    QVariantAnimation *anim_ = nullptr;
};
