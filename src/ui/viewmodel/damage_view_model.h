#pragma once

// Damage statistics ViewModel — the MVVM pilot for this project.
//
// This class owns every piece of *state and pure logic* the damage panel
// used to keep as private members: the rolling chart history, the per-row
// identity vectors, the first-hit/baseline tracking, the quest-end freeze
// and the Rise quest epoch. It was moved out of DamagePanel 1:1 (no
// re-derivation, no rename) so the statistic that had zero automated
// coverage becomes reachable from a plain logic test.
//
// Hard rule: this header and its .cpp carry NO widget or painter
// dependency — only QtCore (QObject / QString / QVector / QHash). All
// drawing lives in panel_damage.cpp; the panel reads what this model
// publishes through the accessors below and paints it unchanged.

#include "core/game_snapshot.h"
#include "rise/rise_damage_types.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace mhw {

class DamageViewModel : public QObject {
    Q_OBJECT

public:
    // One chart sample. `tick` is the model's own poll counter, `damage`
    // is indexed by row (same index space as the identity vectors below).
    struct Sample {
        int tick{0};
        QVector<int> damage;
    };

    // Row seed for the edit-mode demo. The label is resolved by the View
    // (it is localized) and handed over as plain data, so the model never
    // has to reach for a string table.
    struct DemoRow {
        QString name;
        int weaponId{-1};
        int masterRank{0};
        int slot{-1};
    };

    explicit DamageViewModel(QObject *parent = nullptr);

    // ---- Input paths (bodies moved verbatim from the panel) ----

    // World feed: quest lifecycle, party shrink/drop-out carry-over,
    // first-hit baselines, the damage-counter rollback (new-hunt)
    // detector and the sample recorder.
    void updateWorld(const GameSnapshot &snap);

    // Rise feed (REFramework JSON): lifecycle action dispatch, quest-epoch
    // reset, the hunter/companion-only row filter, the key-based history
    // remap and the sample recorder. The two-overload form mirrors the
    // panel's original API (options are installed first, then applied).
    void updateRise(const RiseDamageSnapshot &dmg);
    void updateRise(const RiseDamageSnapshot &dmg,
                    const RiseDamageDisplayOptions &options);

    // Mirrors the panel's setter; the Rise row filter needs the option.
    void setRiseDisplayOptions(const RiseDamageDisplayOptions &options);

    // Edit-mode demo seed (was DamagePanel::setupDemoData's seeding half).
    // `elapsedSeconds` is the value the title-row timer shows.
    void seedDemoData(const QVector<DemoRow> &party, float elapsedSeconds);

    // ---- Output: read-only views of the former panel-private members ----

    // Row identity vectors — published as-is (same index space) instead of
    // being folded into a Row struct, so the paint code reads exactly the
    // values it read before the move.
    [[nodiscard]] int rowCount() const { return names_.size(); }
    [[nodiscard]] const QVector<QString> &names() const { return names_; }
    [[nodiscard]] const QVector<int> &weaponIds() const { return weaponIds_; }
    [[nodiscard]] const QVector<int> &masterRanks() const { return masterRanks_; }
    [[nodiscard]] const QVector<int> &partySlots() const { return slots_; }
    [[nodiscard]] const QVector<bool> &locals() const { return locals_; }
    [[nodiscard]] const QVector<bool> &left() const { return left_; }
    [[nodiscard]] const QVector<QString> &riseKeys() const { return riseKeys_; }

    [[nodiscard]] const QVector<Sample> &history() const { return history_; }
    [[nodiscard]] int  tick() const { return tick_; }
    [[nodiscard]] bool hasData() const { return hasData_; }
    [[nodiscard]] bool questEnded() const { return questEnded_; }
    [[nodiscard]] int  riseQuestEpoch() const { return riseQuestEpoch_; }
    [[nodiscard]] bool hasRiseQuestEpoch() const { return hasRiseQuestEpoch_; }

    // HunterPie: the real quest elapsed time, cached so the title-row
    // timer keeps the final value after the freeze.
    [[nodiscard]] float lastElapsedSeconds() const { return lastElapsedSeconds_; }

    // The panel's DPS formula: 250 ms poll → ×4 per second, measured from
    // this row's first-hit tick.
    [[nodiscard]] int computeDps(int playerIdx) const;

signals:
    // Emitted on every path that used to call the panel's repaint hook.
    // The View connects it to its repaint scheduling.
    void changed();

private:
    // --- the former DamagePanel private members (16 + 2) ---
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
    RiseDamageDisplayOptions riseDisplayOptions_;
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
    float lastElapsedSeconds_{0.0F};
};

} // namespace mhw
