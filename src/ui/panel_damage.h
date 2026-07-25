#pragma once

#include "panel.h"
#include "core/game_snapshot.h"
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
    void paintDemo(QPainter &p) override;
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
    QVector<int> masterRanks_;
    bool hasData_{false};
    bool questEnded_{false};         // freeze after quest completes (Success/Completed/Failed)
};