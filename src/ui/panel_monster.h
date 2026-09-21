#pragma once

#include "panel.h"
#include "monster/monster_types.h"

#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <array>

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

    // i18n: window title / demo labels are cached; everything else is read
    // from tr() at the draw site. Called after a locale swap (see panel.h).
    void retranslateUi() override;

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

    // v0.8.4-r23: HunterPie-style ailment auto-hide tracker. One slot per
    // World ailment id (0..24); MonsterAilmentSnapshot.id is the same id
    // HunterPie's MonsterAilmentRepository keys on. `sig` is the quantized
    // (timer, buildup) signature, `stampMs` the wall-clock millisecond at
    // which it last changed (see paintPanel()'s kAilAutoHideMs).
    struct AilTrack {
        quint64 sig{0};
        qint64  stampMs{0};
    };
    std::array<AilTrack, 32> ailTrack_{};

    // v0.10.x-r2 PartAutoHide: same shape as AilTrack, slot keyed by the
    // normalized PartSnapshot.index (see kPartAutoHideMs + the eight-field
    // pack inside paintPanel()).
    //
    // v0.10.x-r2 fix: the raw index is NOT a dense 0..N key — the readers
    // assign it on three different scales (World severable 1000+s, World
    // normal -1-n, Rise i). paintPanel() normalizes it to three disjoint
    // regions:
    //   severable: 100 + s           [100, 131)
    //   normal:    1000 + slotIdx    [1000, 2024)
    //   Rise:      i                 [0, 64)
    // so the table needs 2048 entries. The extra ~1900 entries are 30 KB and
    // only the slots actually observed are ever touched, so there is no
    // per-tick scan cost.
    //
    // v0.10.x-r2 fix (B1): reset on monster swap. A stale (sig, stampMs)
    // carried over from a previous target made a new monster's part look
    // "already silent" (hidden on its first paint) or "already fresh"
    // (pinned for a spurious 15 s). update() clears the table whenever the
    // snapshot's address changes — the address is the identity the rest of
    // the panel already keys on (panel_monster.cpp:831) and it is refreshed
    // on every tick (monster_reader.cpp:854 / mhr_reader.cpp:667).
    struct PartTrack {
        quint64 sig{0};
        qint64  stampMs{0};
    };
    std::array<PartTrack, 2048> partTrack_{};

    mhw::MonsterSnapshot monster_;
    bool hasData_{false};
    bool multiplayer_{false};
    QTimer pulseTimer_;
    QElapsedTimer phaseClock_;
};