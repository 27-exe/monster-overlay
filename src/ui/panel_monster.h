#pragma once

#include "panel.h"
#include "monster/monster_types.h"

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
// v0.10.3-r6 (B1) — .pc card data model.
//
// The per-part cards of the .pgrid used to be assembled by one long loop
// inside MonsterPanel::paintPanel(), while the PANEL HEIGHT RESERVATION and
// drawPc()'s own row stack each re-derived the few card fields they cared
// about. Three copies of the same "how tall is this card" question is how
// v0.10.3 shipped an invisible tenderize strip: the draw gate and the
// reservation gate disagreed by one field name.
//
// PcEntry is the one shape all three sites now agree on:
//   * buildPcList()  — the ONLY assembler (panel_monster.cpp). Every
//                      layout site calls it.
//   * pcGaugeExtraH()— the ONLY height calculator. Called by the panel
//                      height reservation, the .pgrid cell layout AND
//                      drawPc()'s internal row stack.
//   * drawPc()       — the ONLY painter. Pure function of (cell, entry).
//
// It used to live in panel_monster.cpp's anonymous namespace and moves here
// because MonsterPanel befriends buildPcList(), and a friend declaration
// must name the function's parameter and return types exactly.
// -----------------------------------------------------------------------------
struct PcEntry {
    QString name;          // 头 / 左翼 / 右翼 / 尾巴 / 左脚 / 右脚
    QString tag;           // empty / "破" / "斩"
    QString tagKind;       // "" / "brk" / "sev"
    int     counter{0};    // HunterPie raw counter
    bool    broken{false}; // true if part has been broken/severed at least once
    // v0.10.x-r3 UI-template alignment (HunterPie XAML
    // BossMonsterSeverablePartView.xaml:101-134 / Breakable 124-157):
    // drives the Row 3 conditional text -- Sever -> Flinch or Health -> Flinch.
    bool    severed{false}; // true once the part has been severed (tail/horn)
    enum class Layer { Flinch, Break, Sever };
    Layer primaryLayer{Layer::Break};
    Layer valueLayer{Layer::Break};
    float   pct{0.0F};     // primary gauge fraction, independent of valueLayer
    QString value;         // compact current/max for valueLayer, e.g. 34k/57k
    // v0.7.4 PR C: per-part tenderize. When tenderizeDuration > 0 the
    // card renders a small amber strip showing the remaining seconds
    // and a fill bar driven by duration / tenderizeMaxDuration.
    // The strip and its height both depend on remaining duration.
    float   tenderizeDuration{0.0F};
    float   tenderizeMaxDuration{0.0F};

    // v0.10.3-r6 (B1) — Row 1 hard-stagger gauge, HunterPie parity.
    // BossMonsterSeverablePartView.xaml:65-76 renders TWO gauges in one
    // .pc card: a Flinch gauge ABOVE the primary-layer (Sever) gauge.
    //   <ProgressBar ...Bind Flinch... ["<!-- Flinch -->"]
    //   <ProgressBar ...Bind Sever...  ["<!-- Sever -->"]
    // flinchPct holds the Row 1 fill fraction (0..1) and flinchMax the
    // denominator it was computed against.
    //
    // IMPORTANT — flinchMax is a PRESENCE flag, not a "0 % hard stagger"
    // signal, and the two must stay distinguishable:
    //   * flinchMax == 0  → no usable fill denominator. The optional
    //     row is omitted here, unlike HunterPie's fixed-height empty track.
    //   * flinchMax > 0 && flinchPct == 0 → the layer EXISTS and is
    //     currently empty/dealt. The row is still reserved and drawn.
    // Collapsing the two into "draw when pct > 0" is exactly the class of
    // bug that made the v0.10.3 tenderize strip invisible, so the presence
    // test elsewhere is `flinchMax > 0`.
    // DRAWN — kPcDrawFlinchRow (panel_monster.cpp) is true and drawPc()
    // paints this row above the value text; pcGaugeExtraH() reserves the
    // kPcFlinchExtra that pays for it. Keep the field pair named as-is.
    float   flinchPct{0.0F};
    float   flinchMax{0.0F};

    // v0.10.3-r6 (B1) — break gauge of a RISE Severable part.
    // BossMonsterSeverablePartView in HunterPie has no break gauge (World
    // concepts have none), but Rise's MHRPartStructure carries a break
    // layer alongside sever (PartSnapshot::breakHealth / breakMaxHealth,
    // landed in commit 3584e32 — DO NOT re-implement it, only read it).
    // breakPct is that layer's fill fraction (0..1) with breakMax as its
    // denominator. A NONZERO breakMax is exactly "this Severable part also
    // has a breakable body"; 0 means no break layer, which is the case for
    // World parts, for Breakable parts (health/maxHealth already IS the
    // break layer) and for Flinch parts (no break layer at all).
    // breakPct is meaningless unless breakMax > 0 (or, on a World part,
    // isBreakable) — test the denominator, never breakPct alone. Same
    // presence-vs-value split as flinchMax above.
    float   breakPct{0.0F};
    float   breakMax{0.0F};
};

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
    // v0.10.3-r6 (B1): buildPcList() — the .pc card aggregator — is defined
    // in panel_monster.cpp at global scope, right after the anonymous
    // namespace, so it stays a single definition shared by every layout
    // site and by drawPc(). It needs to read multiplayer_ and to advance
    // partTrack_ while it applies HunterPie's 15 s PartAutoHide — exactly
    // the member access the old inline loop in paintPanel() had. Declared
    // here (the definition with these exact parameter types lives in the
    // .cpp) so that access is granted explicitly rather than by widening
    // the fields.
    friend QVector<PcEntry> buildPcList(MonsterPanel &,
                                        const QVector<mhw::PartSnapshot> &,
                                        qint64, bool);

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
    //
    // v0.10.3-r6 (B1): advanced by buildPcList() (see the friend
    // declaration above), the single assembler both the panel height
    // reservation and the .pgrid render now call.
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
