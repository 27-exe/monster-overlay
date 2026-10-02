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

    const bool epochChanged = mhw::riseDamageQuestEpochChanged(
        hasRiseQuestEpoch_, riseQuestEpoch_, dmg);
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
    } else if (atLobby) {
        // A lobby frame that never had a frozen quest to clear. The
        // branch above already returns when questEnded_ is set, so
        // reaching here means there is nothing to carry — and falling
        // through would violate the recording path's own precondition
        // ("we're guaranteed !questEnded_ && inQuest", below), since
        // inQuest is false on a lobby frame.
        //
        // This is the "stay in camp and keep exploring" case: the quest
        // struct is wiped outside a hunting zone (world_reader.cpp clears
        // quest before party), so there is a real frame where the state
        // reads ≤1 while the roster is still populated. Relaying that
        // roster would seed names_ with hunters who are in no quest, and
        // they survive into the next one because the only identity-clearing
        // path (inQuest && questEnded_) needs questEnded_, which the branch
        // above just reset. The hub path repairs the panel only because the
        // roster happens to be empty there, so this early return is what
        // makes "keep exploring" behave like "return to the hub".
        hasData_ = false;
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
    // Strategy: never shrink the visible party mid-quest, and never let a
    // row change occupant. A row index is a STABLE identity per player for
    // as long as that player stays in the session; the row's data is what
    // moves when the compact party array renumbers. Drop-outs only flip a
    // `left_` flag (see below); rejoins with the same name clear the flag
    // and resume normal tracking. The panel resets `left_` to all-false on
    // every new quest (3-state lifecycle above already handles that path).
    const int liveN = n;

    // Map: name → index in this tick's compact live party.
    QHash<QString, int> liveByName;
    liveByName.reserve(liveN * 2);
    for (int i = 0; i < liveN; ++i)
        liveByName.insert(snap.party[i].name, i);

    // --- 置换表（live 下标 → 布局行号）---
    //
    // Identity is the NAME, never the array index and never
    // PartyMemberSnapshot::slot. `readParty` walks the 4 roster slots and
    // `continue`s past the empty ones, so `snap.party` is a COMPACT array:
    // a member dropping out of slot 2 of 4 renumbers everyone after them.
    // The only identity that survives that compaction — and the rejoin that
    // follows — is the string. (`slot` is derived from the same roster index
    // and would drift with it; `entityIndex` is never filled on the World
    // path.)
    //
    // So a row must keep the position it already occupies. Live members are
    // *permuted* into the rows their names own, a dropped player's row keeps
    // their frozen data where it is, and no other row moves. The old
    // "carry-over rows live at indices [liveN, prevSeen)" scheme encoded a
    // different (positional) assumption: index i maps to snap.party[i] for
    // i < liveN, and whoever dropped was assumed to sit at the tail. That is
    // what let "Me" — live index 2 after "Three" dropped from a 4-member
    // party — be read into Three's row, and my own row to be frozen as if
    // *I* were the drop-out.
    //
    // 每个 live 成员按名字去认自己已有的那一行。同 tick 内名字唯一，所以这
    // 张表是双射，认行的先后与 QHash 迭代序无关。
    QVector<int> rowOfLive(liveN, -1);
    QVector<bool> rowTaken(names_.size(), false);
    for (int live = 0; live < liveN; ++live) {
        const QString &name = snap.party[live].name;
        for (int row = 0; row < names_.size(); ++row) {
            if (rowTaken[row])
                continue;
            if (names_.value(row) != name)
                continue;
            rowOfLive[live] = row;
            rowTaken[row] = true;
            break;
        }
    }

    // 布局行数：旧的「有名字的行」全部保留（这就是 oldNamed），没有旧行的
    // 新成员顺延排在它们后面。掉线者本来就有行，不新增。
    // 注意种子不能写成 max(names_.size(), liveN)：size() 算的是旧行数（含
    // 空洞），而 liveN 是本 tick 的活人数；新行的起始位置只由「旧行里真正
    // 有名字的行数」决定。第一帧 names_ 为空时旧 named 是 0，3 个人就该排
    // 0/1/2，布局 3 行 —— 拿 liveN 当种子会凭空多出一倍空行。
    int oldNamed = 0;
    for (int row = 0; row < names_.size(); ++row)
        if (!names_.value(row).isEmpty())
            ++oldNamed;
    int firstFree = oldNamed;
    for (int live = 0; live < liveN; ++live) {
        if (rowOfLive[live] >= 0)
            continue;
        rowOfLive[live] = firstFree++;
    }
    const int layoutN = firstFree;

    // --- resize 提到一切写入之前 ---
    //
    // 旧代码先写 firstHitTick_[i] 才 resize：当 history_ 已被清空（party
    // 全空那一 tick、或新 quest 重置）而 firstHitTick_ 还是 size 0 时，这里
    // 的 row 可以是 1 —— QList::operator[] 带 Q_ASSERT，Debug 构建直接
    // abort（gdb 实测复现）。所有 per-row 向量先抬到 layoutN。
    if (firstHitTick_.size() < layoutN)
        firstHitTick_.resize(layoutN);
    if (baselineDamage_.size() < layoutN)
        baselineDamage_.resize(layoutN);
    if (rawDamage_.size() < layoutN)
        rawDamage_.resize(layoutN);
    if (names_.size() < layoutN)
        names_.resize(layoutN);
    if (weaponIds_.size() < layoutN)
        weaponIds_.resize(layoutN);
    if (masterRanks_.size() < layoutN)
        masterRanks_.resize(layoutN);
    if (slots_.size() < layoutN)
        slots_.resize(layoutN);
    if (locals_.size() < layoutN)
        locals_.resize(layoutN);
    if (left_.size() < layoutN)
        left_.resize(layoutN);

    // --- 按名单差集置 left_ ---
    //
    // 取代旧的 `liveN < prevSeen` 位置判定：那个判定隐含"掉线的永远在队尾"，
    // 掉在中间的玩家会让后面所有人的行一起错位。这里按名字说话 —— 没有 live
    // 成员认领、而自己本来有名字的旧行，就是掉线者。
    for (int row = 0; row < layoutN; ++row) {
        if (row >= names_.size() || names_.value(row).isEmpty())
            continue;
        if (rowTaken[row])
            continue;                 // 这一行现在有人在 → 不是掉线者
        left_[row] = true;
    }

    // --- rejoin：名字回到 live 队伍 → 清除冻结标记并强制重取基线 ---
    for (int row = 0; row < layoutN; ++row) {
        if (row >= names_.size() || names_.value(row).isEmpty())
            continue;
        // 这行原本冻结、而它的名字又出现在 live 队伍里 = 重连。
        if (left_.value(row, false) && liveByName.contains(names_[row])) {
            left_[row] = false;
            // Force rebaseline so post-rejoin damage isn't blended with
            // pre-disconnect damage — matches the "playerChanged"
            // semantics in the per-row loop below.
            firstHitTick_[row] = 0;
            baselineDamage_[row] = 0;
        }
    }

    // --- 9 个 per-player 向量 + history_ 的 damage 列：按名搬运 ---
    //
    // 只有真的发生置换时才动手（绝大多数 tick 是恒等映射）。搬运漏了
    // history_ 会让折线图串行：原本属于 row 2 的历史点会被当成 row 3 的。
    bool permuted = false;
    for (int live = 0; live < liveN; ++live)
        if (rowOfLive[live] != live)
            permuted = true;
    if (layoutN != names_.size() || layoutN != firstHitTick_.size())
        permuted = true;

    if (permuted) {
        // 反向表：旧行号 → 新行号。
        //   - live 成员的名字认领自己那一行 → dest == row（数据跟着行走）；
        //   - 没被认领的旧行（掉线者）原地不动 → dest == row；
        //   - 两者都不占的空洞 → -1（该行本来就没数据）。
        // 也就是说「行号不变、数据跟着行号走」就是这次置换的语义：变的只是
        // 哪一列开始对应哪个 snap.party[live]，不变的是行本身。
        QVector<int> destOfRow(names_.size(), -1);
        for (int live = 0; live < liveN; ++live) {
            const int row = rowOfLive[live];
            if (row >= 0 && row < names_.size())
                destOfRow[row] = row;
        }
        for (int row = 0; row < names_.size(); ++row) {
            if (destOfRow[row] >= 0)
                continue;                       // 已被 live 成员认领
            if (names_.value(row).isEmpty())
                continue;                       // 空洞行
            destOfRow[row] = row;               // 掉线者：原地冻结
        }

        QVector<QString> nextNames(layoutN);
        QVector<int> nextWeaponIds(layoutN, -1);
        QVector<int> nextMasterRanks(layoutN, 0);
        QVector<int> nextSlots(layoutN, -1);
        QVector<bool> nextLocals(layoutN, false);
        QVector<int> nextFirstHit(layoutN, 0);
        QVector<int> nextBaseline(layoutN, 0);
        QVector<int> nextRaw(layoutN, 0);
        QVector<bool> nextLeft(layoutN, true);   // 默认冻结，live 行再翻回 false

        // 1) 旧行整体搬到自己的新行号：名字/武器/MR/伤害/第一击/基线/原始
        //    计数器全部原样带过去。live 行稍后被下面第 2 步覆写成新鲜值，
        //    所以这里搬谁都不会出错。
        for (int row = 0; row < names_.size(); ++row) {
            const int dest = destOfRow[row];
            if (dest < 0 || dest >= layoutN)
                continue;
            if (names_.value(row).isEmpty())
                continue;
            nextNames[dest]       = names_.value(row);
            nextWeaponIds[dest]   = weaponIds_.value(row, -1);
            nextMasterRanks[dest] = masterRanks_.value(row, 0);
            nextSlots[dest]       = slots_.value(row, -1);
            nextLocals[dest]      = locals_.value(row, false);
            nextLeft[dest]        = left_.value(row, false);
            if (row < firstHitTick_.size())
                nextFirstHit[dest] = firstHitTick_.value(row);
            if (row < baselineDamage_.size())
                nextBaseline[dest] = baselineDamage_.value(row);
            if (row < rawDamage_.size())
                nextRaw[dest]     = rawDamage_.value(row);
        }

        // 2) live 成员的行覆写为新鲜快照值。first/baseline 保留迁移过来的值
        //    —— rejoin 通行已经把它清过 0，其它行则是该玩家自己的历史状态，
        //    「换行号」这件事本身不该重置任何人的第一击基线。
        for (int live = 0; live < liveN; ++live) {
            const int row = rowOfLive[live];
            if (row < 0 || row >= layoutN)
                continue;
            nextNames[row]       = snap.party[live].name;
            nextWeaponIds[row]   = snap.party[live].weaponId;
            nextMasterRanks[row] = snap.party[live].masterRank;
            nextSlots[row]       = snap.party[live].slot;
            nextLocals[row]      = snap.party[live].local;
            nextLeft[row]        = false;   // 在场的人不可能是冻结行
        }

        // 3) history_ 逐 sample 换列：旧 damage [row] 搬到 [destOfRow[row]]。
        //    冻结行 dest == row 所以那一列纹丝不动；live 行跟着自己的历史
        //    一起走，折线图不会串行。
        for (Sample &sample : history_) {
            QVector<int> remapped(layoutN, 0);
            for (int row = 0; row < names_.size() && row < sample.damage.size(); ++row) {
                const int dest = destOfRow[row];
                if (dest >= 0 && dest < layoutN)
                    remapped[dest] = sample.damage.value(row, 0);
            }
            sample.damage = std::move(remapped);
        }

        names_          = std::move(nextNames);
        weaponIds_      = std::move(nextWeaponIds);
        masterRanks_    = std::move(nextMasterRanks);
        slots_          = std::move(nextSlots);
        locals_         = std::move(nextLocals);
        firstHitTick_   = std::move(nextFirstHit);
        baselineDamage_ = std::move(nextBaseline);
        rawDamage_      = std::move(nextRaw);
        left_           = std::move(nextLeft);
    }

    // 行号空间到此为止就是 layoutN：上面所有 per-row 向量都已先 resize 到
    // 它（早于任何写入），置换搬运后又被整体换成 layoutN 长的向量。不需要
    // 在循环里再补 resize。

    for (int i = 0; i < layoutN; ++i) {
        // Live 行取数：先按 live 下标读 snap.party，再按置换表落到行 i。
        // 冻结行（掉线者的行）保留搬运后的旧值。
        const int liveOfRow = rowOfLive.lastIndexOf(i);
        const bool isCarryOver = (liveOfRow < 0);
        const QString previousName = names_.value(i);
        const int previousWeaponId = weaponIds_.value(i, -1);

        // Live slot: pull fresh data from snap.party. Carry-over slot: keep
        // the frozen name/weapon/etc, the player is no longer in the live
        // party array.
        if (!isCarryOver) {
            names_[i]       = snap.party[liveOfRow].name;
            weaponIds_[i]   = snap.party[liveOfRow].weaponId;
            masterRanks_[i] = snap.party[liveOfRow].masterRank;
            slots_[i]       = snap.party[liveOfRow].slot;
            locals_[i]      = snap.party[liveOfRow].local;
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
            && (previousWeaponId != snap.party[liveOfRow].weaponId
             || previousName     != snap.party[liveOfRow].name);
        if (playerChanged) {
            firstHitTick_[i] = 0;
            baselineDamage_[i] = 0;
        }
        if (!isCarryOver && firstHitTick_[i] == 0
            && snap.party[liveOfRow].damage > 0) {
            firstHitTick_[i] = tick_;
            baselineDamage_[i] = snap.party[liveOfRow].damage;
        }
    }

    // Record sample
    Sample s;
    s.tick = tick_++;
    s.damage.resize(layoutN);
    for (int i = 0; i < layoutN; ++i) {
        const int liveOfRow = rowOfLive.lastIndexOf(i);
        const bool isCarryOver = (liveOfRow < 0);
        if (isCarryOver) {
            // Frozen row — preserve the last recorded cumulative
            // damage. Don't touch rawDamage_ either, so the
            // damageCounterReset detector above stays stable across
            // drops (the engine zeroes the live counter which is no
            // longer indexed by us at this slot anyway).
            s.damage[i] = history_.isEmpty() ? 0 : history_.last().damage.value(i, 0);
            continue;
        }
        const int raw = static_cast<int>(snap.party[liveOfRow].damage);
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
        for (int live = 0; live < liveN; ++live) {
            const int row = rowOfLive[live];
            if (rawDamage_.value(row, 0) > 0) {
                comparedActiveCounter = true;
                if (snap.party[live].damage < rawDamage_[row]) {
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
            firstHitTick_.fill(0, names_.size());
            baselineDamage_.fill(0, names_.size());
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
