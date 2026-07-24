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

private:
    struct Sample {
        int tick;
        QVector<int> damage;
    };

    QVector<Sample> history_;
    int tick_{0};
    QVector<QString> names_;
    QVector<int> weaponIds_;
    QVector<int> masterRanks_;
    bool hasData_{false};
};