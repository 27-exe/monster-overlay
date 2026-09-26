#include "damage_view_model.h"

// See the header: this file holds the *moved* DamagePanel statistics
// bodies. Only `canvas()->update()` calls were replaced by `emit
// changed()`. Nothing else was re-derived or renamed.

#include <QHash>
#include <QSet>

#include <algorithm>
#include <limits>
#include <utility>

namespace {

// Carried over verbatim from panel_damage.cpp's anonymous namespace: the
// history ring's length and the reader-side party cap. They are statistics
// parameters, so they belong next to the statistics.
constexpr int kMaxSamples = 900;
constexpr int kMaxPlayers = 4;

} // namespace

namespace mhw {

DamageViewModel::DamageViewModel(QObject *parent)
    : QObject(parent)
{
}

void DamageViewModel::updateRise(const RiseDamageSnapshot &dmg)
{
    auto clearRiseState = [this] {
        history_.clear();
        tick_ = 0;
        firstHitTick_.clear();
        baselineDamage_.clear();
        rawDamage_.clear();
        names_.clear();
        weaponIds_.clear();
        masterRanks_.clear();
        slots_.clear();
        locals_.clear();
        left_.clear();
        riseKeys_.clear();
        lastElapsedSeconds_ = 0.0F;
        hasData_ = false;
        questEnded_ = false;
    };

    const mhw::RiseDamageLifecycleAction action =
        mhw::riseDamageLifecycleAction(dmg);
    if (action == mhw::RiseDamageLifecycleAction::Keep) {
        emit changed();
        return;
    }
    if (action == mhw::RiseDamageLifecycleAction::Clear) {
        clearRiseState();
        hasRiseQuestEpoch_ = false;
        emit changed();
        return;
    }

    const bool epochChanged = hasRiseQuestEpoch_
                           && dmg.questEpoch != riseQuestEpoch_;
    riseQuestEpoch_ = dmg.questEpoch;
    hasRiseQuestEpoch_ = true;

    if (epochChanged) {
        // Epoch is the authoritative hunt identity for v2. Reset even when
        // the producer does not expose an inactive frame between two hunts.
        clearRiseState();
        riseQuestEpoch_ = dmg.questEpoch;
        hasRiseQuestEpoch_ = true;
    }

    if (action == mhw::RiseDamageLifecycleAction::Freeze) {
        if (hasData_)
            questEnded_ = true;
        emit changed();
        return;
    }

    if (questEnded_) {
        questEnded_ = false;
        history_.clear();
        tick_ = 0;
        firstHitTick_.clear();
        baselineDamage_.clear();
        rawDamage_.clear();
        lastElapsedSeconds_ = 0.0F;
    }

    // The main damage table intentionally contains hunters and NPC
    // companions only. Pet/Palico/Palamute rows belong to their own display,
    // while Unknown is never promoted into a user-facing row. Disabling
    // "other members" is stricter still: only the local Player survives;
    // even a locally-owned Companion is an other member for this option.
    QVector<const mhw::RiseDamageActor *> actors;
    actors.reserve(dmg.actors.size());
    QSet<QString> seenKeys;
    for (const auto &actor : dmg.actors) {
        const bool supportedKind = actor.kind == mhw::RiseDamageActorKind::Player
                                || actor.kind == mhw::RiseDamageActorKind::Companion;
        if (!supportedKind || actor.key.isEmpty() || seenKeys.contains(actor.key))
            continue;
        if (!riseDisplayOptions_.showOtherMembers
            && !(actor.kind == mhw::RiseDamageActorKind::Player && actor.local)) {
            continue;
        }
        seenKeys.insert(actor.key);
        actors.append(&actor);
    }

    std::sort(actors.begin(), actors.end(),
              [](const mhw::RiseDamageActor *lhs,
                 const mhw::RiseDamageActor *rhs) {
        if (lhs->local != rhs->local)
            return lhs->local > rhs->local;
        if (lhs->displaySlot != rhs->displaySlot)
            return lhs->displaySlot < rhs->displaySlot;
        return lhs->key < rhs->key;
    });

    if (actors.isEmpty()) {
        // Preserve the startup placeholder until a supported actor has ever
        // arrived. Once rows existed, however, an authoritative empty/filter
        // result must remove them rather than leave disallowed stale members.
        if (!riseKeys_.isEmpty()) {
            history_.clear();
            tick_ = 0;
            firstHitTick_.clear();
            baselineDamage_.clear();
            rawDamage_.clear();
            names_.clear();
            weaponIds_.clear();
            masterRanks_.clear();
            slots_.clear();
            locals_.clear();
            left_.clear();
            riseKeys_.clear();
            hasData_ = false;
        }
        emit changed();
        return;
    }

    hasData_ = true;

    const int n = actors.size();

    // Every positional vector and every historical sample is remapped through
    // actor.key before the sorted order is installed. Thus a producer may
    // reorder its JSON array (or a display slot may change) without assigning
    // one actor another actor's chart, baseline, or DPS history.
    QHash<QString, int> oldIndexByKey;
    oldIndexByKey.reserve(riseKeys_.size() * 2);
    for (int i = 0; i < riseKeys_.size(); ++i)
        oldIndexByKey.insert(riseKeys_[i], i);

    QVector<int> oldIndexes(n, -1);
    QVector<QString> nextKeys(n);
    QVector<QString> nextNames(n);
    QVector<int> nextWeaponIds(n, -1);
    QVector<int> nextMasterRanks(n, 0);
    QVector<int> nextSlots(n, -1);
    QVector<bool> nextLocals(n, false);
    QVector<int> nextFirstHitTicks(n, 0);
    QVector<int> nextBaselines(n, 0);
    QVector<int> nextRawDamage(n, 0);

    for (int i = 0; i < n; ++i) {
        const auto &actor = *actors[i];
        nextKeys[i] = actor.key;
        nextNames[i] = actor.name;
        nextWeaponIds[i] = actor.weaponId;
        nextMasterRanks[i] = actor.masterRank;
        nextSlots[i] = actor.displaySlot;
        nextLocals[i] = actor.local;

        const int oldIndex = oldIndexByKey.value(actor.key, -1);
        oldIndexes[i] = oldIndex;
        if (oldIndex >= 0) {
            nextFirstHitTicks[i] = firstHitTick_.value(oldIndex, 0);
            nextBaselines[i] = baselineDamage_.value(oldIndex, 0);
            nextRawDamage[i] = rawDamage_.value(oldIndex, 0);
        }

        const int total = actor.total >= std::numeric_limits<int>::max()
            ? std::numeric_limits<int>::max()
            : static_cast<int>(actor.total);
        if (nextFirstHitTicks[i] == 0 && total > 0) {
            nextFirstHitTicks[i] = tick_;
            nextBaselines[i] = total;
        }
    }

    for (Sample &sample : history_) {
        const QVector<int> oldDamage = sample.damage;
        sample.damage.fill(0, n);
        for (int i = 0; i < n; ++i) {
            if (oldIndexes[i] >= 0)
                sample.damage[i] = oldDamage.value(oldIndexes[i], 0);
        }
    }

    riseKeys_ = std::move(nextKeys);
    names_ = std::move(nextNames);
    weaponIds_ = std::move(nextWeaponIds);
    masterRanks_ = std::move(nextMasterRanks);
    slots_ = std::move(nextSlots);
    locals_ = std::move(nextLocals);
    firstHitTick_ = std::move(nextFirstHitTicks);
    baselineDamage_ = std::move(nextBaselines);
    rawDamage_ = std::move(nextRawDamage);
    left_.fill(false, n);

    Sample s;
    s.tick = tick_++;
    s.damage.resize(n);
    for (int i = 0; i < n; ++i) {
        const qint64 total = actors[i]->total;
        s.damage[i] = total >= std::numeric_limits<int>::max()
            ? std::numeric_limits<int>::max()
            : static_cast<int>(total);
        rawDamage_[i] = s.damage[i];
    }
    history_.append(s);
    if (history_.size() > kMaxSamples)
        history_.removeFirst();

    emit changed();
}

void DamageViewModel::updateRise(const RiseDamageSnapshot &dmg,
                                 const RiseDamageDisplayOptions &options)
{
    riseDisplayOptions_ = options;
    updateRise(dmg);
}

void DamageViewModel::setRiseDisplayOptions(
    const RiseDamageDisplayOptions &options)
{
    riseDisplayOptions_ = options;
    emit changed();
}

void DamageViewModel::updateWorld(const GameSnapshot &snap)
{
    // HunterPie: capture the real quest elapsed time before any
    // quest-end early-return so the title-row timer stays correct
    // after the freeze kicks in. The in-game timer pointer is
    // typically invalid in the settlement screen.
    if (snap.quest.maxTimerSeconds > 0.0F)
        lastElapsedSeconds_ = snap.quest.elapsedSeconds;

    // 3-state quest lifecycle keyed off snap.quest.state.
    //
    //   state == 2 (InQuest)  → record samples, DPS live
    //   state 3/4/5/6/7       → FREEZE: 60s settlement / abandon screen
    //                            where zone is still a hunting zone but
    //                            damage counters must NOT advance
    //   state ≤ 1             → CLEAR: back at lobby / mission select,
    //                            wipe chart and reset baselines
    //
    // The previous inHuntingZone()-only gate (v0.5.x) ran into the
    // 60-second settlement window: zone stays a hunting zone, so we
    // kept appending samples and ticking DPS for a full minute after
    // the quest had already finished. Authoritative reference: project
    // L2 page mhw-hunterpie-dps-algorithm.md.
    const int qstate = snap.quest.state;
    const bool inQuest = (qstate == 2) && (snap.quest.id > 0);
    const bool inResultScreen = (qstate >= 3 && qstate <= 7);
    const bool atLobby = (qstate <= 1);

    if (inQuest && questEnded_) {
        // New quest started while the old one was frozen → full reset.
        // All row identity vectors must clear too, otherwise the drop-out
        // carry-over block below would mistake leftover names from the
        // previous quest for a still-present row in this one.
        questEnded_ = false;
        history_.clear();
        tick_ = 0;
        firstHitTick_.clear();
        baselineDamage_.clear();
        rawDamage_.clear();
        lastElapsedSeconds_ = 0.0F;
        names_.clear();
        weaponIds_.clear();
        masterRanks_.clear();
        slots_.clear();
        locals_.clear();
        left_.clear();
    } else if (inResultScreen && !questEnded_) {
        // Quest finished (Success/Completed/Failed/Abandon/Quit) —
        // freeze immediately. Keep the chart and per-row damage
        // visible; stop appending new samples.
        questEnded_ = true;
        emit changed();
        return;
    } else if (questEnded_ && atLobby) {
        // Back at lobby / Ready state — fully clear and unhide.
        questEnded_ = false;
        history_.clear();
        tick_ = 0;
        firstHitTick_.clear();
        baselineDamage_.clear();
        rawDamage_.clear();
        lastElapsedSeconds_ = 0.0F;
        names_.clear();
        weaponIds_.clear();
        masterRanks_.clear();
        slots_.clear();
        locals_.clear();
        left_.clear();
        hasData_ = false;
        emit changed();
        return;
    } else if (questEnded_) {
        // Still in the 60s settlement screen — keep frozen data
        // visible; do NOT record, do NOT bump tick_. The party
        // pointer may have shrunk if a member disconnected, so we
        // intentionally don't touch the chart and let the per-row
        // "left" markers handle the visual gap (see readParty).
        emit changed();
        return;
    }

    // Below this point we're guaranteed !questEnded_ && inQuest, so we
    // can safely record new samples. If party is empty the engine
    // simply has no one to track (single-player / pre-quest), but we
    // still want the placeholder off-screen until first damage.
    hasData_ = !snap.party.isEmpty();
    if (!hasData_) {
        history_.clear();
        tick_ = 0;
        firstHitTick_.clear();
        baselineDamage_.clear();
        rawDamage_.clear();
        // Keep names_/left_ in place: a brief party-empty dip in
        // multiplayer (lobby reassembly, host reconnect) must not wipe
        // carry-over rows. The next tick where party.size() > 0 will
        // re-evaluate.
        emit changed();
        return;
    }

    const int n = std::min(static_cast<int>(snap.party.size()), kMaxPlayers);

    // MHW keeps party damage counters alive across the result screen and can
    // repopulate the party before the zone transition is observable. Treat a
    // counter rollback as the authoritative new-hunt boundary; otherwise the
    // old chart/ticks continue and DPS is divided by multiple hunts' time.
    //
    // The carry-over drop-out detection (below) can retire a row whose
    // rawDamage_ carries over a non-zero value from the pre-drop era. That
    // rejoin tick must NOT trip this detector — the counter legitimately
    // "rolls back" when a returning member's engine-side slot starts
    // accumulating damage from 0 again. To avoid that false-positive we
    // *defer* the reset check until after the carry-over rebaseline has
    // already aligned rawDamage_[i] for i < liveN. See the deferred block
    // at the end of this function.
    const bool deferDamageCounterReset = rawDamage_.size() == n
                                     && !history_.isEmpty();

    // --- Drop-out / party-shrink handling ---
    //
    // v0.5.x bug: when a non-host member dropped out mid-quest, the engine
    // zeroed their damage counter (DAMAGE_ADDRESS + index*0x2A0 → 0).
    // The overlay happily reflected that as "this player did 0 damage",
    // wiping their cumulative total and their entire row in the chart.
    //
    // Strategy: never shrink the visible party mid-quest. The layout
    // size `n` is the max of (live party size, number of previously-seen
    // names that are still !left_). Drop-outs move into a `left_` flag;
    // rejoins with the same name clear the flag and resume normal
    // tracking. The panel resets `left_` to all-false on every new
    // quest (3-state lifecycle above already handles that path).
    const int liveN = n;
    int prevSeen = 0;
    for (int i = 0; i < names_.size(); ++i)
        if (!names_.value(i).isEmpty()) ++prevSeen;

    // `n` already holds liveN. Bump it up to absorb rows for players
    // who used to be present but dropped this tick. Existing rows are
    // preserved via their index; their `left_` flag flips below.
    if (liveN < prevSeen) {
        // We can't simply extend n past the live party because the
        // row indices below are positional — index i maps to
        // snap.party[i] for i < liveN, and to a frozen history row for
        // i >= liveN. Track a "carry-over" list of frozen names keyed
        // by their original snap-party index (0..3), so the loop can
        // process them after the live block.
        for (int i = liveN; i < prevSeen; ++i) {
            if (left_.value(i, false))      continue;
            if (names_.value(i).isEmpty())  continue;
            if (i >= left_.size()) left_.resize(i + 1);
            left_[i] = true;
        }
    }
    // Final layout size: max of live party + previously-seen players
    // (the frozen carry-over rows live at indices [liveN, prevSeen)).
    const int layoutN = std::max(liveN, prevSeen);

    // `left_` index parity: the flag at index i corresponds to row i,
    // which equals snap.party[i] when i < liveN, and a frozen carry-over
    // row when i >= liveN. The per-player loop below handles both.
    left_.resize(layoutN);

    // Map: name → live party index (-1 if absent from this tick's snap).
    QHash<QString, int> liveByName;
    liveByName.reserve(liveN * 2);
    for (int i = 0; i < liveN; ++i)
        liveByName.insert(snap.party[i].name, i);

    // Per-name rejoin detection: if a name reappears in the live party
    // after being marked `left_`, the corresponding row's flag clears
    // here so the loop can rebaseline it as a fresh player.
    for (int i = 0; i < layoutN; ++i) {
        if (!left_.value(i, false))     continue;
        if (names_.value(i).isEmpty())  continue;
        if (liveByName.contains(names_[i])) {
            left_[i] = false;
            // Force rebaseline so post-rejoin damage isn't blended
            // with pre-disconnect damage — this also matches the
            // "playerChanged" semantics in the original loop.
            firstHitTick_[i] = 0;
            baselineDamage_[i] = 0;
        }
    }

    names_.resize(layoutN);
    weaponIds_.resize(layoutN);
    masterRanks_.resize(layoutN);
    slots_.resize(layoutN);
    locals_.resize(layoutN);

    // Per-player first-hit tracking. Resize on party-size change.
    firstHitTick_.resize(layoutN);
    baselineDamage_.resize(layoutN);
    rawDamage_.resize(layoutN);

    for (int i = 0; i < layoutN; ++i) {
        const bool isCarryOver = (i >= liveN);
        const QString previousName = names_.value(i);
        const int previousWeaponId = weaponIds_.value(i, -1);

        // Live slot: pull fresh data from snap.party. Carry-over
        // slot (i >= liveN): keep the frozen name/weapon/etc, the
        // player is no longer in the live party array.
        if (!isCarryOver) {
            names_[i]       = snap.party[i].name;
            weaponIds_[i]   = snap.party[i].weaponId;
            masterRanks_[i] = snap.party[i].masterRank;
            slots_[i]       = snap.party[i].slot;
            locals_[i]      = snap.party[i].local;
        } else {
            // Make sure the row renders even if resize left a hole.
            if (names_.value(i).isEmpty()) names_[i] = QString();
            if (slots_.value(i, -1) < 0)   slots_[i] = i;  // stable color
            if (!locals_.value(i, false))  locals_[i] = false;
        }

        // HunterPie: baseline captured when THIS player first deals damage.
        // Reset the baseline if the player joined fresh (slot/signature
        // changed) so we don't blend pre-join damage with post-join.
        const bool playerChanged = !isCarryOver
            && firstHitTick_[i] != 0
            && (previousWeaponId != snap.party[i].weaponId
             || previousName     != snap.party[i].name);
        if (playerChanged) {
            firstHitTick_[i] = 0;
            baselineDamage_[i] = 0;
        }
        if (!isCarryOver && firstHitTick_[i] == 0 && snap.party[i].damage > 0) {
            firstHitTick_[i] = tick_;
            baselineDamage_[i] = snap.party[i].damage;
        }
    }

    // Record sample
    Sample s;
    s.tick = tick_++;
    s.damage.resize(layoutN);
    for (int i = 0; i < layoutN; ++i) {
        const bool isCarryOver = (i >= liveN);
        if (isCarryOver) {
            // Frozen row — preserve the last recorded cumulative
            // damage. Don't touch rawDamage_ either, so the
            // damageCounterReset detector above stays stable across
            // drops (the engine zeroes the live counter which is no
            // longer indexed by us at this slot anyway).
            s.damage[i] = history_.isEmpty() ? 0 : history_.last().damage.value(i, 0);
            continue;
        }
        const int raw = static_cast<int>(snap.party[i].damage);
        if (firstHitTick_[i] > 0) {
            if (raw >= baselineDamage_[i]) {
                s.damage[i] = raw - baselineDamage_[i];
            } else {
                // raw dropped below baseline — memory reset
                // (quest cleared, party updated, etc). Rebaseline
                // so the next samples start fresh at 0 instead of
                // producing a giant negative spike in the chart.
                baselineDamage_[i] = raw;
                s.damage[i] = 0;
            }
        } else {
            s.damage[i] = 0;
        }
        rawDamage_[i] = raw;
    }
    history_.append(s);
    if (history_.size() > kMaxSamples)
        history_.removeFirst();

    // Deferred damageCounterReset check (v0.7.5 carry-over fix).
    //
    // We delayed the original reset detector past the carry-over block so
    // rawDamage_[i] for rejoin rows (i < liveN, formerly carry-over at
    // index >= prev-liveN) is aligned to the engine's live counter before
    // we compare. If a rollback survives that alignment across ALL live
    // members, it's a genuine new hunt boundary and we reset the chart.
    if (deferDamageCounterReset) {
        bool rollback = false;
        bool comparedActiveCounter = false;
        for (int i = 0; i < liveN; ++i) {
            if (rawDamage_[i] > 0) {
                comparedActiveCounter = true;
                if (snap.party[i].damage < rawDamage_[i]) {
                    rollback = true;
                    break;
                }
            }
        }
        if (rollback && comparedActiveCounter) {
            // Genuine new-hunt boundary: clear the chart but keep identity
            // rows (so the first sample of the new hunt still maps by slot).
            history_.clear();
            tick_ = 0;
            firstHitTick_.fill(0, liveN);
            baselineDamage_.fill(0, liveN);
            lastElapsedSeconds_ = snap.quest.elapsedSeconds;
        }
    }

    emit changed();
}

void DamageViewModel::seedDemoData(const QVector<DemoRow> &party,
                                   float elapsedSeconds)
{
    // Edit-mode demo: seed mock party identity (used for label rows
    // and per-row weapon icons) plus a synthetic 8-sample cumulative
    // damage history per player — enough for the line chart to show
    // visible curves and DPS to be non-zero. Sets private fields
    // directly to avoid the per-tick work of updateWorld().
    constexpr int kDemoPlayers = 4;
    // MHW realistic: 总伤害 ≤999,999 (6 位+逗号), DPS ≤999.
    const int kFinalDmg[kDemoPlayers] = {184220, 96240, 71030, 40510};

    names_.clear();       weaponIds_.clear();
    masterRanks_.clear(); slots_.clear();
    firstHitTick_.clear(); baselineDamage_.clear(); rawDamage_.clear();
    history_.clear();     tick_ = 0;
    left_.clear();        // v0.7.5: drop-out carry-over flag must
                           // reset alongside the identity rows. If a
                           // future change re-enables setupDemoData
                           // in a fresh live-mode path, missing this
                           // would cause the drop-out branch to flag
                           // demo players as "previously seen" and
                           // re-create the v0.5.x phantom-row bug.
    const int seeded = std::min<int>(party.size(), kDemoPlayers);
    for (int i = 0; i < seeded; ++i) {
        names_.append(party[i].name);
        weaponIds_.append(party[i].weaponId);
        masterRanks_.append(party[i].masterRank);
        slots_.append(party[i].slot);
        firstHitTick_.append(1);  // all started hitting on tick 1
        baselineDamage_.append(0);
    }
    // 8 samples at 0..7 ticks, growing monotonic curve (matches HunterPie).
    for (int t = 0; t < 8; ++t) {
        Sample s;
        s.tick = tick_++;
        s.damage.resize(kDemoPlayers);
        const float f = static_cast<float>(t) / 7.0F;  // 0..1
        for (int i = 0; i < kDemoPlayers; ++i) {
            // super-linear growth so the chart has a visible curve.
            const int d = static_cast<int>(kFinalDmg[i] * (0.10F + 0.90F * f * f));
            s.damage[i] = std::max(0, d - baselineDamage_.value(i, 0));
        }
        history_.append(s);
    }
    // Seed the title-row quest timer with a representative value so
    // the new "任务计时 mm:ss" is visible in demo / edit mode (no
    // real game running → no snap.quest data).
    lastElapsedSeconds_ = elapsedSeconds;
    hasData_ = true;

    emit changed();
}

int DamageViewModel::computeDps(int playerIdx) const
{
    if (history_.isEmpty() || playerIdx < 0) return 0;
    if (playerIdx >= firstHitTick_.size()) return 0;
    if (firstHitTick_[playerIdx] <= 0 || tick_ <= firstHitTick_[playerIdx])
        return 0;
    const int dmg = history_.last().damage.value(playerIdx, 0);
    const int elapsedTicks = history_.last().tick - firstHitTick_[playerIdx];
    if (elapsedTicks <= 0) return 0;
    const qint64 dps = static_cast<qint64>(dmg) * 4 / elapsedTicks;
    return dps >= std::numeric_limits<int>::max()
        ? std::numeric_limits<int>::max()
        : static_cast<int>(dps);       // 250ms poll → ×4 per second
}

} // namespace mhw
