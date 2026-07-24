#pragma once

#include "panel.h"
#include "player/player_types.h"

// Player status panel: HP, ST bars + weapon icon + mantle timers.
class PlayerPanel : public Panel {
    Q_OBJECT
public:
    explicit PlayerPanel(QWidget *parent = nullptr);

    void update(const mhw::PlayerSnapshot &p);
    void setWeaponId(int id) { weaponId_ = id; }

protected:
    void paintPanel(QPainter &p) override;

private:
    mhw::PlayerSnapshot player_;
    int weaponId_{-1};
    bool hasData_{false};
};