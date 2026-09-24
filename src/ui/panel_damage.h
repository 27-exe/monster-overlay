#pragma once

#include "panel.h"
#include "core/game_snapshot.h"
#include "rise/rise_damage_reader.h"
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

    // Rise damage feed (via REFramework's sandboxed data directory). Converts the
    // REFramework snapshot into the internal chart history. The panel stays
    // hidden until the first valid snapshot arrives, exactly like the World
    // path — there is no "waiting" placeholder.
    void updateRiseDamage(const mhw::RiseDamageSnapshot &dmg);
    void updateRiseDamage(const mhw::RiseDamageSnapshot &dmg,
                          const mhw::RiseDamageDisplayOptions &options);

    // Controls which Rise actors can enter the main damage table. The main
    // panel intentionally supports hunters and companions only; pets have
    // their own presentation path.
    void setRiseDisplayOptions(const mhw::RiseDamageDisplayOptions &options);

    // i18n: window title + demo party labels are cached; the rest of the
    // panel reads tr() at the draw site. Called after a locale swap.
    void retranslateUi() override;

protected:
    void paintPanel(QPainter &p) override;
    void setupDemoData() override;
    // Rise 与 World 一视同仁：没有可用数据就整块不挂载。
    // 曾经这里写作 `hasData_ || riseMode_`，让 Rise 在拿到第一份快照前
    // 也强制可见，并在 paintPanel 里画一个 36px 的「等待伤害数据」占位块
    // —— 理由是“免得看起来像窗口坏了”。实际效果相反：玩家没装
    // REFramework Lua 时，那块占位会一直挂着，比隐藏更容易被当成故障。
    // 现在与 World 分支（main.cpp 的 `!party.isEmpty() || showAll`）以及
    // MonsterPanel 的 `hasData_` 口径一致。
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
    QVector<int>  rawDamage_;        // latest raw counter, used to detect a new hunt
    QVector<QString> names_;
    QVector<int> weaponIds_;
    QVector<int>  masterRanks_;
    QVector<int>  slots_;            // party slot (0-3) for color assignment
    QVector<bool> locals_;           // self flag (HunterPie name match)
    QVector<QString> riseKeys_;      // stable Rise row identity (never array index)
    mhw::RiseDamageDisplayOptions riseDisplayOptions_;
    int riseQuestEpoch_{0};
    bool hasRiseQuestEpoch_{false};
    QVector<bool> left_;             // true once a previously-seen player
                                     // disappears from snap.party (party
                                     // shrink, drop-out, kick). Their row
                                     // freezes at the last recorded damage
                                     // until the quest resets the panel.
                                     // Fixes the v0.5.x bug where a member
                                     // dropping out zeroed their total
                                     // damage mid-chart.
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