#pragma once

#include "panel.h"
#include "core/game_snapshot.h"
#include "player/player_types.h"

// Player status panel: zone + quest + connection status + memory address,
// then HP / ST bars + weapon icon + mantle timers.
class PlayerPanel : public Panel {
    Q_OBJECT
public:
    explicit PlayerPanel(QWidget *parent = nullptr);

    // Player-only update (sets hasData_ from snapshot.valid).
    void update(const mhw::PlayerSnapshot &p);

    // Full snapshot — keeps zone/quest/connection info in sync. We
    // only care about the fields relevant to this panel, so call this
    // whenever the main loop pushes a fresh GameSnapshot.
    void update(const mhw::GameSnapshot &snap);

    void setWeaponId(int id) { weaponId_ = id; }

protected:
    void paintPanel(QPainter &p) override;
    void paintDemo(QPainter &p) override;
    bool hasContent() const override { return true; }

private:
    mhw::PlayerSnapshot player_;
    // Cached metadata from GameSnapshot so paintPanel doesn't need to
    // take a GameSnapshot every frame.
    mhw::Zone zone_{mhw::Zone::Unknown};
    mhw::QuestSnapshot quest_;
    bool attached_{false};
    qint64 pid_{-1};
    std::uintptr_t imageBase_{};
    QString status_;
    int weaponId_{-1};
    bool hasData_{false};
};