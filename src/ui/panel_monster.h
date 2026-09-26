#pragma once

#include "panel.h"
#include "monster/monster_types.h"
#include "ui/viewmodel/monster_view_model.h"

#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <array>

// HunterPie-style monster HP panel:
//   - One big total-HP progress bar
//   - Small per-part progress bars below
//   - Enrage timer, part names

// -----------------------------------------------------------------------------
// v0.11 (T16) — .pc card data model, now in the ViewModel.
//
// PcEntry (the one shape every layout site agrees on), its ONLY producer
// (mhw::MonsterPartListBuilder::build(), formerly the free function
// buildPcList()) and the PartAutoHide table that builder advances (formerly
// MonsterPanel::partTrack_) all live in ui/viewmodel/monster_view_model.h.
//
// The .pc cards of the .pgrid used to be assembled by one long loop inside
// MonsterPanel::paintPanel(), while the PANEL HEIGHT RESERVATION and
// drawPc()'s own row stack each re-derived the few card fields they cared
// about. Three copies of the same "how tall is this card" question is how
// v0.10.3 shipped an invisible tenderize strip: the draw gate and the
// reservation gate disagreed by one field name.
//
// The three sites still agree on one shape, it just no longer needs this
// header (and with it the whole QWidget layer) to describe it:
//   * MonsterPartListBuilder::build() — the ONLY assembler. Every layout
//                                      site calls it.
//   * pcGaugeExtraH()— the ONLY height calculator. Called by the panel
//                      height reservation, the .pgrid cell layout AND
//                      drawPc()'s internal row stack.
//   * drawPc()       — the ONLY painter. Pure function of (cell, entry).
//
// PcEntry lived HERE because MonsterPanel befriended buildPcList() and a
// friend declaration must name the function's parameter and return types
// exactly. That friend is gone (T16): the builder now takes the two panel
// members it used to borrow — game and multiplayer — plus editMode() as
// plain parameters, and owns partTrack_ itself. So the model needs no
// QWidget and this panel needs no friend.
// -----------------------------------------------------------------------------

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
    // now computes sin() alpha for the enrage label.
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

    // v0.11 (T16): the PartAutoHide table is GONE from this class. It now
    // belongs to mhw::MonsterPartListBuilder (m_partList), which advances it
    // while it builds the .pc list, and update() calls
    // m_partList.resetAutoHide() exactly where it used to do
    // `partTrack_ = {}`. The rationale for the slot normalization, the
    // 2048-entry size and the reset-on-identity-change all moved with the
    // state — see ui/viewmodel/monster_view_model.h.

    // The one stateless-or-stateful dependency the panel still keeps: the
    // assembler of the .pc grid. It is a member (not a local in
    // paintPanel()) precisely BECAUSE it carries the 15 s PartAutoHide
    // state across frames — a fresh builder per paint would re-pin every
    // card on every frame.
    mhw::MonsterPartListBuilder m_partList;

    mhw::MonsterSnapshot monster_;
    bool hasData_{false};
    bool multiplayer_{false};
    QTimer pulseTimer_;
    QElapsedTimer phaseClock_;
};
