#pragma once

#include "panel.h"
#include "monster/monster_types.h"

#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

// HunterPie-style monster HP panel:
//   - One big total-HP progress bar
//   - Small per-part progress bars below
//   - Enrage timer, part names
class MonsterPanel : public Panel {
    Q_OBJECT
public:
    explicit MonsterPanel(QWidget *parent = nullptr);

    void update(const mhw::MonsterSnapshot &m);
    void setMultiplayer(bool on) { multiplayer_ = on; }

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;
    bool hasContent() const override { return hasData_; }

private slots:
    // Drives the enrage text pulse when the monster is actually
    // enraged. Auto-stops when the timer expires — keeps live mode
    // from burning frames when nothing is happening.
    void onEnragePulseTick();

private:
    // Pulse phase in [0,1), refreshed every kPulsePeriodMs. paint()
    // reads this to compute sin() alpha for the enrage label.
    double enragePhase_{0.0};

    mhw::MonsterSnapshot monster_;
    bool hasData_{false};
    bool multiplayer_{false};
    QTimer pulseTimer_;
    QElapsedTimer phaseClock_;
};