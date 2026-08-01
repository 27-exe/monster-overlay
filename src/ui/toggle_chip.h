#pragma once

#include <QWidget>
#include <QString>

// iOS-style capsule toggle used for the "面板启用" master switch.
// Self-paints a 40x22 track + a 18px circle handle. Emits stateChanged(int)
// to mirror QCheckBox's signal so existing code doesn't change.
class ToggleChip : public QWidget {
    Q_OBJECT
public:
    explicit ToggleChip(QWidget *parent = nullptr);
    bool isChecked() const { return on_; }
    void setChecked(bool c);

    QSize sizeHint() const override { return QSize(40, 22); }
    QSize minimumSizeHint() const override { return QSize(40, 22); }

signals:
    void stateChanged(int state);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    bool on_{true};
    bool hover_{false};
};