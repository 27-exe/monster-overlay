#pragma once

#include "mhw_reader.h"

#include <QMainWindow>

class QLabel;
class QVBoxLayout;
class QTimer;

class OverlayWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit OverlayWindow(QString mapPath, bool demoMode = false, QWidget *parent = nullptr);

private slots:
    void refresh();

private:
    void setupLayerShell();
    void render(const mhw::GameSnapshot &snapshot);
    void renderDemo();
    QLabel *makeLabel(const QString &text, int pointSize, int weight = 500);
    QLabel *makeContextLabel();

    mhw::MhwReader reader_;
    bool demoMode_{};
    QWidget *container_{};
    QVBoxLayout *layout_{};
    QLabel *context_{};
    QLabel *status_{};
    QLabel *quest_{};
    QLabel *player_{};
    QLabel *party_{};
    QLabel *monsters_{};
    QLabel *abnormalities_{};
    QLabel *equipment_{};
    QTimer *timer_{};
    mhw::Zone lastZone_{mhw::Zone::Unknown};
};
