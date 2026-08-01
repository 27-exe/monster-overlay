#pragma once

#include <QWidget>

class QLabel;

// R4 sub-switch: a row with three slots:
//   [●]  <chinese label>          <grey english key>
// Click anywhere on the row to toggle. On = orange dot (#ff8040) +
// bright text; off = hollow grey ring + muted text + 50% opacity on the
// row. Owns no signal slot pairs of its own: callers wire click+state
// just like QCheckBox would.
class SectionRow : public QWidget {
    Q_OBJECT
public:
    explicit SectionRow(const QString &zh, const QString &key,
                        QWidget *parent = nullptr);

    bool isChecked() const { return on_; }
    void setChecked(bool c);

    QSize sizeHint() const override { return QSize(160, 24); }
    QSize minimumSizeHint() const override { return QSize(120, 24); }

signals:
    void stateChanged(int state);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    QLabel *zh_ = nullptr;
    QLabel *key_ = nullptr;
    QString zhText_;
    QString keyText_;
    bool on_{true};
};