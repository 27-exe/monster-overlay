#pragma once

// MonsterPanel ViewModel — the part-gauge (`.pc` card) builder.
//
// This is the second MVVM extraction after DamageViewModel, and it targets
// the one place where the panel still needed a `friend` declaration:
// buildPcList(), the sole assembler of the .pc card grid.
//
// What lives here:
//   * PcEntry   — the one shape every layout site agrees on. It moved here
//                 from panel_monster.h (it used to live in panel_monster.cpp's
//                 anonymous namespace) because the builder that fills it is
//                 QWidget-free and panel_monster.h is not.
//   * MonsterPartListBuilder — HunterPie's MonsterPartViewModel semantics:
//                 the eight-field change signature, the 15 s PartAutoHide
//                 silence window, the slot-key normalization and the
//                 per-entry value/layer selection.
//   * PartTrack — the 15 s change-based visibility table. This is the state
//                 that used to be MonsterPanel::partTrack_, i.e. the whole
//                 reason the panel had to befriend buildPcList(). It moved
//                 out verbatim: same 2048-entry array, same (sig, stampMs)
//                 shape, same zero initializer, same reset-on-address-change
//                 call issued from MonsterPanel::update().
//
// Hard rule, same as DamageViewModel: this header and its .cpp carry NO
// widget or painter dependency. It includes only QtCore types plus the pure
// monster data headers (monster/monster_types.h). Everything drawable is
// returned as plain data in a QVector<PcEntry> and the panel paints that
// unchanged. That is what makes the PartAutoHide state machine — which had
// zero automated coverage because it was only reachable through a private
// member of a QWidget — reachable from a plain logic test.

#include "monster/monster_types.h"

#include <QString>
#include <QVector>

#include <array>
#include <QtGlobal>

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
//   * MonsterPartListBuilder::build() — the ONLY assembler. Every layout
//                                      site calls it.
//   * pcGaugeExtraH()                 — the ONLY height calculator. Called
//                                      by the panel height reservation, the
//                                      .pgrid cell layout AND drawPc()'s
//                                      internal row stack.
//   * drawPc()                        — the ONLY painter. Pure function of
//                                      (cell, entry).
//
// It lived in panel_monster.cpp's anonymous namespace, then in
// panel_monster.h (because MonsterPanel befriended buildPcList() and a friend
// declaration must name the function's parameter and return types exactly).
// It now lives here, next to the builder that is its only producer, so the
// assembler can be extracted without dragging panel_monster.h — and with it
// the whole QWidget layer — into the model.
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

    // v0.10.8: whether this card carries the primary-layer gauge (.mini bar
    // plus its numeric value row). False only on World in a multiplayer
    // session, where the Health/MaxHealth pair is stale local data that we
    // choose not to render. Defaults to true so every existing single-player
    // path is unchanged. Drawn and height-reserved through the SAME
    // predicate (pcHasPrimaryGauge / pcGaugeExtraH, panel_monster.cpp).
    bool    hasPrimaryGauge{true};
};

namespace mhw {

// The ONE builder that turns the snapshot's part rows into drawable .pc card
// entries. The three height sites (panel height reservation, .pgrid cell
// layout, drawPc's own row stack) plus anything a follow-up task adds must
// all consume build()'s output; none of them may re-derive a card's contents.
// Two .pgrid draws with a different autohide clock is exactly how the
// v0.10.3 tenderize defaults drifted.
//
// It used to be the free function buildPcList(MonsterPanel&, ...) in
// panel_monster.cpp, which MonsterPanel had to befriend because it read
// multiplayer_ and advanced partTrack_. Both of those inputs are now plain
// parameters plus this class's own state, so nothing anywhere needs friend
// access any more.
//
// NOT A PURE FUNCTION — and deliberately so: build() MUTATES partTrack_ to
// advance HunterPie's 15 s PartAutoHide. The caller must keep the "ONE call
// per paint" discipline it always had (see the comment in
// MonsterPanel::paintPanel); the panel resolves the list once, before the
// height reservation, and feeds that one list to both the reservation and
// the .pgrid render.
class MonsterPartListBuilder {
public:
    // v0.10.x-r2 PartAutoHide slot record. `sig` is the quantized
    // (eight-field) signature, `stampMs` the wall-clock millisecond at
    // which it last changed.
    struct PartTrack {
        quint64 sig{0};
        qint64  stampMs{0};
    };

    // v0.10.x-r2 fix: the raw index is NOT a dense 0..N key — the readers
    // assign it on three different scales (World severable 1000+s, World
    // normal -1-n, Rise i). build() normalizes it to three disjoint
    // regions:
    //   severable: 100 + s           [100, 131)
    //   normal:    1000 + slotIdx    [1000, 2024)
    //   Rise:      i                 [0, 64)
    // so the table needs 2048 entries. The extra ~1900 entries are 30 KB and
    // only the slots actually observed are ever touched, so there is no
    // per-tick scan cost.
    static constexpr int kTrackSize = 2048;

    // Assemble the .pc cards for one paint.
    //
    // Parameters, in the same order and with the same meaning as the old
    // buildPcList() free function, with the two former MonsterPanel private
    // reads spelled out:
    //   shownParts — the display-filtered parts (displayableParts(), caller
    //                side; the builder never re-filters).
    //   nowMs      — the paint clock stamp, sampled once per paint so every
    //                card in this frame is compared against ONE clock.
    //   onTenderize — the control-panel 软化/TENDERIZE section bit. Applied
    //                HERE, in one place, so the reservation and the render
    //                can never disagree about whether the strip is on (the
    //                v0.10.3 incident).
    //   editMode   — MonsterPanel::editMode() (Panel::editMode()).
    //                PartAutoHide never hides a card in edit mode (mirrors
    //                the ailments' `recent = true` default), so the demo /
    //                control-panel preview keeps every card pinned.
    //   game       — MonsterPanel::monster_.game (was panel.monster_.
    //                game). Selects the per-PartType layer pairs.
    //   multiplayer — MonsterPanel::multiplayer_ (was panel.multiplayer_).
    //                Suppresses the primary gauge on World in a session.
    //
    // Mutable: this advances partTrack_. See the class comment.
    QVector<PcEntry> build(const QVector<mhw::PartSnapshot> &shownParts,
                           qint64 nowMs, bool onTenderize,
                           bool editMode, mhw::GameId game, bool multiplayer);

    // v0.10.x-r2 fix (B1): clear the table. A stale (sig, stampMs) carried
    // over from a previous target made a new monster's part look "already
    // silent" (hidden on its first paint) or "already fresh" (pinned for a
    // spurious 15 s). MonsterPanel::update() calls this on the monster
    // identity change, exactly where it used to do `partTrack_ = {}`.
    void resetAutoHide() { partTrack_ = {}; }

private:
    // --- the former MonsterPanel private member (moved out verbatim) ---
    std::array<PartTrack, kTrackSize> partTrack_;
};

} // namespace mhw
