#pragma once

#include "panel.h"
#include "core/game_snapshot.h"
#include "rise/rise_damage_reader.h"
#include "ui/viewmodel/damage_view_model.h"

#include <QPainter>
#include <QRectF>
#include <QVector>

// Damage statistics panel: per-player rows (name, MR, weapon icon,
// cumulative damage, DPS) + a line chart of damage accumulation
// and percentage share over time.
//
// MVVM (v0.11 pilot): every statistic this panel used to keep as a private
// member now lives in mhw::DamageViewModel (m_vm). The panel is a pure
// consumer: update*/setRiseDisplayOptions() forward to the VM and schedule
// one repaint, and paintPanel()/drawChart()/drawShareBar() read the VM's
// read-only views. The ViewModel carries no widget or painter dependency,
// so the statistic itself is now testable without a QApplication.
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
    bool hasContent() const override { return m_vm.hasData(); }

private:
    // paintPanel helpers — extracted to keep the main layout short.
    void drawChart(QPainter &p, const QRectF &chartRect);
    void drawShareBar(QPainter &p, const QRectF &barRect);

    mhw::DamageViewModel m_vm;
};
