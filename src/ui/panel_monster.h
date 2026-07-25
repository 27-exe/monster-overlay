#pragma once

#include "panel.h"
#include "monster/monster_types.h"

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
    void paintDemo(QPainter &p) override;
    bool hasContent() const override { return hasData_; }

private:
    mhw::MonsterSnapshot monster_;
    bool hasData_{false};
    bool multiplayer_{false};
};