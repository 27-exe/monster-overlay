#include "ui/viewmodel/monster_view_model.h"

// MonsterPanel ViewModel implementation — the bodies MOVED here from
// panel_monster.cpp. Only the access layer changed: the two private reads
// (`panel.multiplayer_`, `panel.monster_.game`, `panel.editMode()`) became
// parameters and `panel.partTrack_` became this class's own member
// `partTrack_`. Every other line is the original code, so the PartAutoHide
// semantics are unchanged by construction rather than by re-derivation.
//
// Namespace note: panel_monster.cpp kept `mh::tr()` in a namespace above its
// anonymous one specifically so buildPcList() could resolve the part-fallback
// / tag strings (buildPcList lived inside that anonymous namespace). That
// helper is still needed here, so it is declared again below. Nothing else
// from the anonymous namespace is used — the geometry/paint constants and the
// pcHas*/pcGaugeExtraH predicates all stay in panel_monster.cpp with the
// View.

#include "core/string_table.h"
#include "rise/mhr_part_names.h"

#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// Same definition as panel_monster.cpp's `mh::tr`, kept local because this
// translation unit is the builder that fills the localized card fields.
QString trVm(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}

// ---------------------------------------------------------------------------
// NaN/Inf guard — 2026-09-22 real-machine crash + garbage-bar fix.
//
// Reader values come from live game memory. A slot that is mid-teardown
// (target switch, multiplayer slot churn, part array realloc) can yield
// NaN or ±Inf for one tick. Two consequences, both observed on 2026-09-22:
//
//  1. Hard crash. Qt6's qRound() asserts on non-finite input
//     (qCheckedFPConversionToInteger → Q_ASSERT(!std::isnan(value))),
//     which calls qFatal() → SIGABRT. coredump backtrace:
//       qt_assert ← qRoundf ← MonsterPanel::paintPanel
//     Reproduced in a 3-player Rise hunt right after the target changed.
//
//  2. Garbage bars. NaN fails every comparison the intuitive way:
//     a non-finite denominator must never be treated as a usable layer;
//     and std::clamp(NaN, 0, 1) returns NaN, which propagates into the
//     bar width multiply. Symptom: blue bar present but length nonsense,
//     "bars don't match the monster's actual reactions".
//
// Non-finite is folded to 0.0F, which is PartSnapshot's existing
// "this part has no such layer" sentinel — no new state is introduced,
// and a card with a NaN layer simply reads as "no layer" for one tick
// instead of crashing or lying.
//
// The moral: any float that reaches a gate, a quantizer, a clamp or a
// divide in this file must come through one of these two first.
// ---------------------------------------------------------------------------
inline float sanitizePartValue(float v)
{
    return std::isfinite(v) ? v : 0.0F;
}

// Quantize for the AutoHide signature, with the same finite guarantee.
inline quint32 sanitizeQ10(float v)
{
    return std::isfinite(v) ? static_cast<quint32>(qRound(v * 10.0F)) : 0u;
}

} // namespace

namespace mhw {

struct PartDisplayLayers {
    mhw::PartHealthPair flinch;
    mhw::PartHealthPair primary;
    PcEntry::Layer primaryKind;
};

PartDisplayLayers partDisplayLayers(mhw::GameId game, const mhw::PartSnapshot &part)
{
    const mhw::PartHealthPair flinch{part.flinch, part.maxFlinch};
    // Readers normalize the selected Sever/Break pair into health/maxHealth.
    // World Severable is sourced from its severable table; World Breakable
    // from its threshold table. Rise selects its sever or break array by
    // PartType. Rise's additional breakHealth pair is retained, not painted.
    switch (game) {
    case mhw::GameId::Rise:
        if (part.partType == mhw::PartType::Severable)
            return {flinch, {part.health, part.maxHealth}, PcEntry::Layer::Sever};
        if (part.partType == mhw::PartType::Breakable)
            return {flinch, {part.health, part.maxHealth}, PcEntry::Layer::Break};
        return {flinch, flinch, PcEntry::Layer::Flinch};
    case mhw::GameId::World:
        if (part.partType == mhw::PartType::Severable)
            return {flinch, {part.health, part.maxHealth}, PcEntry::Layer::Sever};
        if (part.partType == mhw::PartType::Breakable)
            return {flinch, {part.health, part.maxHealth}, PcEntry::Layer::Break};
        return {flinch, flinch, PcEntry::Layer::Flinch};
    default:
        return {flinch, mhw::partHealthForDisplay(part), PcEntry::Layer::Flinch};
    }
}

QVector<PcEntry> MonsterPartListBuilder::build(
        const QVector<mhw::PartSnapshot> &shownParts, qint64 nowMs,
        bool onTenderize, bool editMode, mhw::GameId game, bool multiplayer)
{
    // v0.10.x-r2 PartAutoHide: same 15 s silence timeout as HunterPie's
    // MonsterPartViewModel (MonsterWidgetConfig.cs:131-139 default
    // `AutoHidePartsDelay = new(15, 300, 1, 1)`). The signature is the
    // eight PartSnapshot fields the player perceives as "the part
    // moved" — HP/maxHP (Severable / Breakable layer), Flinch/maxFlinch
    // (the alternate value-layer HunterPie's Row 3 Conditional selects
    // once a part is severed/broken), tenderizeDuration (the strip's
    // countdown), Counter (+0x18, the player's "破 N" feedback), and
    // the broken/severed flags. tenderizeMaxDuration is the sentinel
    // "slot has authored this part" gate used by the panel's strip draw,
    // not a per-tick value, so it deliberately does NOT participate —
    // including it would pin a card forever as soon as one tick
    // authored the slot (see v0.10.x-r3 merged-stackable patch).
    //
    // Quantize each float field at 0.1 (matches ailments' ailSig) so
    // float jitter doesn't keep the card from settling into the silent
    // state. Counter / bool fields are already integer-stable, no
    // quantize needed.
    constexpr qint64 kPartAutoHideMs = 15000;
    QVector<PcEntry> pcList;
    pcList.reserve(shownParts.size());
    for (const auto &p : shownParts) {
        // sanitizeQ10() folds NaN/Inf to 0 before qRound(). Without it a
        // single non-finite read aborts the process inside paintPanel()
        // (Qt6's qRound asserts on NaN) — see the helper comment above.
        const quint32 hpQ = sanitizeQ10(p.health);
        const quint32 mhQ = sanitizeQ10(p.maxHealth);
        const quint32 flQ = sanitizeQ10(p.flinch);
        const quint32 mfQ = sanitizeQ10(p.maxFlinch);
        const quint32 tdQ = sanitizeQ10(p.tenderizeDuration);
        const quint32 ctQ = static_cast<quint32>(p.counter);
        const quint32 brQ = p.isBroken       ? 1u : 0u;
        const quint32 svQ = p.isPartSevered  ? 1u : 0u;
        // Accumulate (do NOT XOR-fold). The previous fold
        //   (a<<32)^b ^ (c<<32)^d ^ ...
        // let two IDENTICAL pairs cancel: on a World normal part the reader
        // sets flinch/maxFlinch == health/maxHealth (monster_reader.cpp:746-753),
        // so the (hp,mh) and (fl,mf) terms were equal and cancelled to 0 —
        // and 0 is also the zero-initialized PartTrack default. That made
        // `partSig == 0` for 25 % of the World breakable grid whenever
        // Counter == 0, which both suppressed the card on its first paint and
        // let a *moving* bar hide. A multiply-accumulate fold cannot cancel.
        quint64 partSig = 0x9E3779B97F4A7C15ULL;
        auto mix = [&partSig](quint64 v) {
            partSig = (partSig ^ v) * 0x100000001B3ULL;
        };
        mix((static_cast<quint64>(hpQ) << 32) | mhQ);
        mix((static_cast<quint64>(flQ) << 32) | mfQ);
        mix((static_cast<quint64>(tdQ) << 32) | ctQ);
        mix((static_cast<quint64>(brQ) << 32) | svQ);
        // A real state can still hash to 0 by coincidence; force the low bit
        // so a genuine signature is never confusable with the zero-init slot.
        partSig |= 1ULL;
        // edit-mode demo: never auto-hide (mirrors ailments' `recent = true`
        // default). Non-host multiplayer can still proceed — the non-host
        // override below ("--/--" when HP is stale) is purely a
        // value-masking concern and is unrelated to the silence filter, so
        // the card falls out cleanly when the player's actual game-clock
        // signal goes quiet.
        bool recent = true;
        // v0.10.x-r2 fix: the guard used the RAW PartSnapshot::index, but the
        // three readers assign it on three different scales:
        //   World severable  1000 + s        (monster_reader.cpp:680)  >= 1000
        //   World normal     -1 - normalSlot (monster_reader.cpp:743)  < 0
        //   Rise             i               (mhr_reader.cpp:419)      0..15
        // Only the Rise scale lands inside [0, partTrack_.size()), so
        // AutoHide silently did nothing on World — the whole grid stayed
        // pinned forever. Normalize to a dense, stable slot key instead of
        // changing the reader's index semantics (that field is also used for
        // the "部位 N" fallback label and is intentionally signed to encode
        // which table a part came from).
        //
        // Key = (100 + schema row) for severable, (1000 + normal slot) for
        // normal, i for Rise. The three regions are disjoint:
        //   severable [100, 131)   Rise [0, 64)   normal [1000, 2024)
        // so no key can alias another even if a panel ever saw parts from
        // two games (it cannot — main.cpp:316 fixes the reader at startup).
        // A key outside [0, 2048) falls out of the guard below and leaves
        // `recent == true`, i.e. it degrades to "always visible", which is
        // the safe failure mode.
        const int slot = (p.index >= 1000)
            ? 100 + (p.index - 1000)       // World severable
            : (p.index < 0 ? 1000 - p.index // World normal: -1-n -> 1000+n
                           : p.index);     // Rise: already dense
        if (!editMode && slot >= 0
                       && slot < static_cast<int>(partTrack_.size())) {
            PartTrack &track = partTrack_[slot];
            if (track.sig != partSig) {
                track.sig     = partSig;
                track.stampMs = nowMs;
                recent = true;
            } else {
                recent = track.stampMs > 0
                      && (nowMs - track.stampMs) < kPartAutoHideMs;
            }
        }
        if (!recent)
            continue;

        PcEntry e;
        e.name = p.name.isEmpty()
            ? trVm(QStringLiteral("ui.monster_part_fallback")).arg(p.index)
            : p.name;
        e.counter = p.counter;
        e.broken = p.isBroken;
        e.severed = p.isPartSevered;
        switch (p.partType) {
        case mhw::PartType::Severable:
            e.tag = trVm(QStringLiteral("ui.monster_tag_sever"));
            e.tagKind = QStringLiteral("sev");
            break;
        case mhw::PartType::Breakable:
            e.tag = trVm(QStringLiteral("ui.monster_tag_break"));
            e.tagKind = QStringLiteral("brk");
            break;
        case mhw::PartType::Flinch:
            break;
        }
        // v0.7.4 PR C: per-part tenderize values feed the new strip
        // drawn inside each .pc card. The struct fields are 0 by default
        // (no active tenderize), so we only need to copy when nonzero.
        // v0.8.4-r23: gate on the 软化 section bit so the control-panel
        // toggle actually hides the strip (and the reservation stays in
        // lockstep with the drawn cards) — it is applied HERE, once, so
        // both heights see the identical condition.
        e.tenderizeDuration    = onTenderize ? p.tenderizeDuration : 0.0F;
        e.tenderizeMaxDuration = onTenderize ? p.tenderizeMaxDuration : 0.0F;
        // Row 1 hard-stagger gauge data.
        // Exactly the p.flinch / p.maxFlinch layer, un-decimated: the
        // caller (pcGaugeExtraH) needs the denominator to tell "no flinch
        // layer" (World body parts, 0/0) from "flinch layer, 0 % left".
        // v0.10.3-r6 (B1): breakPct/breakMax — Rise Severable parts carry
        // a break layer alongside the sever layer (PartSnapshot::
        // breakHealth / breakMaxHealth, commit 3584e32). A nonzero
        // breakMaxHealth is exactly "this Severable part also has a
        // breakable body"; 0 means no break layer, which happens for
        // World parts, Breakable parts (health/maxHealth already IS the
        // break layer) and Flinch parts (no break layer at all).
        // Sanitized copies of the four layer fields the gate / clamp /
        // divide path reads below. NaN compares false against zero, but
        // still must not reach std::clamp, division, or Qt's qRound.
        const float sFlinchMax = sanitizePartValue(p.maxFlinch);
        if (sFlinchMax > 0.0F) {
            e.flinchPct = std::clamp(sanitizePartValue(p.flinch)
                                     / sFlinchMax, 0.0F, 1.0F);
            e.flinchMax = sFlinchMax;
        }
        const float sBreakMax = sanitizePartValue(p.breakMaxHealth);
        if (sBreakMax > 0.0F) {
            e.breakPct = std::clamp(sanitizePartValue(p.breakHealth)
                                     / sBreakMax, 0.0F, 1.0F);
            e.breakMax = sBreakMax;
        }
        // v0.7.4 PR C: pick the right HP pair per PartType.
        //   - Severable: health/maxHealth carries Sever; World leaves
        //     Flinch untouched while Rise updates it each tick.
        //   - Breakable: Health/MaxHealth is the cumulative threshold
        //     progress (UpdateBreakableData); Flinch/MaxFlinch is the
        //     current layer's raw value (less useful on the main bar).
        //   - Flinch:    only Flinch/MaxFlinch is meaningful (no
        //     thresholds, not severable). This is the path that fixes
        //     the "脏数据" complaint — body/leg parts now show real
        //     flinch bar values instead of the broken double-filled
        //     health/flinch pair.
        const PartDisplayLayers layers = partDisplayLayers(game, p);
        const mhw::PartHealthPair hp = layers.primary;
        // v0.10.x-r3 UI-template alignment: Row 3 Conditional
        // (HunterPie MonsterPartTemplateSelector + BossMonsterSeverablePartView.xaml:101-134,
        //  BossMonsterBreakablePartView.xaml:124-157). The displayed value flips
        // from the primary layer (Sever / Health) to Flinch/MaxFlinch once the
        // part is severed or broken. Mapping:
        //   Severable + severed  → "Flinch/MaxFlinch"
        //   Severable + !severed → "Sever/MaxSever"   (= health/maxHealth)
        //   Breakable + broken   → "Flinch/MaxFlinch"
        //   Breakable + !broken  → "Health/MaxHealth" (= health/maxHealth)
        //   Flinch               → "Flinch/MaxFlinch"
        // The multiplayer non-host override below still takes precedence
        // (it forces "—" when the layer pair is stale), so this never
        // masks a frozen non-host value.
        // Sanitized on the way in: hp.current/maximum and the flinch pair
        // are all live memory reads, so any of them can be NaN/Inf on a
        // slot that is mid-teardown. compactPartHealth() and the
        // staleFullHp / flinchLive comparisons below would all happily
        // propagate NaN into the displayed value and the bar width.
        const float mHP = sanitizePartValue(hp.maximum);
        const float cHP = sanitizePartValue(hp.current);
        e.primaryLayer = layers.primaryKind;
        e.valueLayer = e.primaryLayer;
        const bool switchToFlinch =
            (p.partType == mhw::PartType::Severable && e.severed)
         || (p.partType == mhw::PartType::Breakable && e.broken);
        // HunterPie changes only the value row after sever/break; the
        // Sever/Health gauge continues to bind its original source.
        if (switchToFlinch) e.valueLayer = PcEntry::Layer::Flinch;
        e.value = e.valueLayer == PcEntry::Layer::Flinch
            ? mhw::compactPartHealth(sanitizePartValue(layers.flinch.current),
                                     sanitizePartValue(layers.flinch.maximum))
            : mhw::compactPartHealth(cHP, mHP);
        e.pct = mHP > 0.0F ? std::clamp(cHP / mHP, 0.0F, 1.0F) : 0.0F;
        // v0.10.8: on World in a multiplayer session the Health/MaxHealth
        // pair (and therefore the .mini gauge it feeds, plus the value row
        // that labels it) is stale local data — see pcHasPrimaryGauge(). The
        // whole gauge goes away rather than being masked to "--/--" over a
        // fake full bar, which is what v0.8.4-r23 did and what the user
        // reported as redundant.
        //
        // Deliberately Scoped: only World. Rise writes all six layers from
        // its own per-tick reads (mhr_reader.cpp), so its multiplayer part
        // data is real and stays rendered even in a session.
        //
        // The signals that survive are the ones the game actually replicates
        // to a non-host client: the Counter (+0x18) with the broken/severed
        // state in the tag chip, and the tenderize strip (an independent
        // table read locally, monster_reader.cpp:340). Flinch stays on Rise;
        // on World a Severable part is never given a flinch layer at all
        // (references/parity §2), so there is nothing extra to keep.
        e.hasPrimaryGauge = !(multiplayer
                              && game == mhw::GameId::World);
        pcList.append(e);
    }
    return pcList;
}

} // namespace mhw
