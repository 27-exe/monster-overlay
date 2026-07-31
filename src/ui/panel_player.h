#pragma once

#include "panel.h"
#include "core/game_snapshot.h"
#include "player/player_types.h"

#include <QHash>

// Player status panel: zone + quest + connection status + memory address,
// then HP / ST bars + weapon icon + mantle timers + debuff bars.
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
    void setupDemoData() override;
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
    int  playerMR_{0};                 // mirrored from party slot 0 MR
    QString playerName_;                // mirrored from party slot 0 name
    int     partyCount_{0};             // mirrored from party snapshot size
    // Track max timer per debuff offset for progress bar scaling.
    QHash<int, float> debuffMaxTimers_;
    // Sharpness — mirrored from PlayerSnapshot.sharpness on every
    // poll. valid=false means the equipped weapon is ranged (or the
    // memory read failed) and the panel should hide the bar.
    mhw::SharpnessSnapshot sharpness_;
};