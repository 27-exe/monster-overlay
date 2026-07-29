#pragma once

#include "panel.h"
#include "core/game_snapshot.h"
#include <QPainter>
#include <QRectF>
#include <QVector>

// Damage statistics panel: per-player rows (name, MR, weapon icon,
// cumulative damage, DPS) + a line chart of damage accumulation
// and percentage share over time.
class DamagePanel : public Panel {
    Q_OBJECT
public:
    explicit DamagePanel(QWidget *parent = nullptr);

    void update(const mhw::GameSnapshot &snap);

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;
    bool hasContent() const override { return hasData_; }

private:
    struct Sample {
        int tick;
        QVector<int> damage;
    };

    QVector<Sample> history_;
    int tick_{0};
    QVector<int>  firstHitTick_;     // poll tick when this player first dealt damage
    QVector<int>  baselineDamage_;   // damage at first-hit tick
    QVector<QString> names_;
    QVector<int> weaponIds_;
    QVector<int>  masterRanks_;
    QVector<int>  slots_;            // party slot (0-3) for color assignment
    QVector<bool> locals_;           // self flag (HunterPie name match)
    bool hasData_{false};
    bool questEnded_{false};         // freeze after quest completes (Success/Completed/Failed)
    // HunterPie: real quest elapsed time = max(0, maxTimer - timeLeft).
    // We cache the last non-zero value so the title-row timer keeps
    // showing the correct "task complete" time after the freeze kicks
    // in (the in-game timer pointer is no longer valid in the
    // settlement screen).
    float lastElapsedSeconds_{0.0F};

    // paintPanel helpers — extracted to keep the main layout short.
    void drawChart(QPainter &p, const QRectF &chartRect);
    void drawShareBar(QPainter &p, const QRectF &barRect);
    int  computeDps(int playerIdx) const;
};