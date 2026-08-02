#include "mhw_reader.h"
#include <QFile>
#include <QIODevice>
#include <QSet>
#include <cmath>

namespace mhw {

void MhwReader::readMonsterAilments(MonsterSnapshot &monster)
{
    // HunterPie GetMonsterAilments(): monster+0x1BC40 → pointer array,
    // each element +0x148 → MHWMonsterAilmentStructure.
    const std::uintptr_t ailBase = monster.address + 0x1BC40ULL;
    auto ailPtr = memory_.read<std::uintptr_t>(ailBase);
    if (!ailPtr || *ailPtr <= 1) return;

    std::uintptr_t cursor = ailBase;
    for (int i = 0; i < 32; ++i) {
        auto current = memory_.read<std::uintptr_t>(cursor);
        if (!current || *current <= 1) break;
        const std::uintptr_t structAddr = *current + 0x148ULL;

        // Read MHWMonsterAilmentStructure — offsets verified against
        // HunterPie MHWMonsterAilmentStructure.cs (sequential layout):
        //   +0x00 Owner(i64) +0x08 IsActive(i32) +0x0C Unk1 +0x10 Id(i32)
        //   +0x14 MaxDuration(f32)
        //   +0x30 Buildup(f32)
        //   +0x40 MaxBuildup(f32)
        //   +0x70 Duration(f32)
        //   +0x78 Counter(i32)
        struct { std::int64_t owner; std::int32_t active; std::int32_t unk1; std::int32_t id; } header{};
        if (!memory_.readBytes(structAddr, &header, sizeof(header), nullptr)) break;

        // HunterPie does NOT gate on Owner during discovery — it looks up
        // Id in the repository and skips unknowns. A mismatched Owner
        // (stale slot) should skip this entry, not kill the whole loop.
        if (header.owner != static_cast<std::int64_t>(monster.address)) {
            cursor += sizeof(std::uintptr_t);
            continue;
        }

        const auto maxDur = memory_.read<float>(structAddr + 0x14ULL);
        const auto buildup = memory_.read<float>(structAddr + 0x30ULL);
        const auto maxBuildup = memory_.read<float>(structAddr + 0x40ULL);
        const auto duration = memory_.read<float>(structAddr + 0x70ULL);
        const auto counter = memory_.read<std::int32_t>(structAddr + 0x78ULL);

        MonsterAilmentSnapshot ail;
        ail.id = header.id;
        ail.name = kAilmentNames.value(header.id, QStringLiteral("异常%1").arg(header.id));
        ail.active = (header.active != 0) && (duration && *duration > 0.0F);
        ail.maxTimer = maxDur ? *maxDur : 0.0F;
        // HunterPie: Timer = Duration (already countdown). Display remaining.
        ail.timer = duration ? *duration : 0.0F;
        ail.buildup = buildup ? *buildup : 0.0F;
        ail.maxBuildup = maxBuildup ? *maxBuildup : 0.0F;
        ail.counter = counter ? *counter : 0;
        monster.ailments.push_back(ail);

        cursor += sizeof(std::uintptr_t);
    }
}



QVector<MonsterSnapshot> MhwReader::readMonsters(QString *error)
{
    QVector<MonsterSnapshot> result;

    // HunterPie default mode is LockOn (MHWMonster.GetLockedOnMonster):
    // LOCKON_ADDRESS -> LOCKEDON_MONSTER_INDEX_OFFSETS, then read +0x950.
    lockOnTargetIndex_ = -1;
    const std::uintptr_t lockOnAddress = absolute(QStringLiteral("LOCKON_ADDRESS"));
    if (lockOnAddress) {
        const auto lockNode = followPointerChain(
            memory_, lockOnAddress,
            map_.offsets(QStringLiteral("LOCKEDON_MONSTER_INDEX_OFFSETS")), nullptr);
        if (lockNode >= 0x10000) {
            if (const auto index = memory_.read<std::int32_t>(lockNode + 0x950ULL))
                lockOnTargetIndex_ = *index;
        }
    }

    // Refresh the manual-target pointer once per poll. When the player
    // pins a monster on the in-game map (or the quest itself marks one),
    // MHWMapMonsterSelectionStructure.SelectedMonster points at that
    // monster's component address. We mirror HunterPie's "ManualTarget ==
    // Self" semantics: prefer the manually-pinned monster, fall back to
    // largest maxHealth when nothing is pinned.
    const std::uintptr_t manualTargetAddr = absolute(QStringLiteral(
        "MONSTER_MANUAL_TARGET_ADDRESS"));
    if (manualTargetAddr) {
        const auto selPtr = memory_.read<std::uintptr_t>(manualTargetAddr);
        // Offset 0x148 inside MHWMapMonsterSelectionStructure holds the
        // SelectedMonster pointer. Treat a stale / unmapped value as
        // "no manual target" rather than chasing it as a real pointer.
        if (selPtr && *selPtr >= 0x10000) {
            // HunterPie only treats the selection as "manual target"
            // when both MapInsectsRef (0x128) and GuiRadarRef (0x160)
            // are set — i.e. the player actually opened the map and
            // pinned the monster there. Without those checks the
            // pointer at 0x148 holds the last targeted monster even
            // when the map UI is closed, which causes the panel to
            // silently switch after every quest.
            const auto mapInsectsRef = memory_.read<std::uintptr_t>(
                *selPtr + 0x128ULL);
            const auto guiRadarRef = memory_.read<std::uintptr_t>(
                *selPtr + 0x160ULL);
            const bool uiOpen = mapInsectsRef && *mapInsectsRef != 0
                             && guiRadarRef && *guiRadarRef != 0;
            if (uiOpen) {
                const auto sel = memory_.read<std::uintptr_t>(
                    *selPtr + 0x148ULL);
                manualTargetAddress_ = (sel && *sel >= 0x10000) ? *sel : 0;
            } else {
                manualTargetAddress_ = 0;
            }
        } else {
            manualTargetAddress_ = 0;
        }
    }

    // HunterPie also reads the quest-pinned monster (capture quest,
    // investigation, etc.) and prefers it over the manual map pin.
    // MONSTER_QUEST_TARGET_ADDRESS → MONSTER_QUEST_TARGET_OFFSETS
    // (0x48,0x1760,0x100) is just a pointer deref chain ending in the
    // monster's component address.
    const std::uintptr_t questTargetAddr = absolute(QStringLiteral(
        "MONSTER_QUEST_TARGET_ADDRESS"));
    if (questTargetAddr) {
        const auto questPtr = followPointerChain(memory_, questTargetAddr,
            map_.offsets(QStringLiteral("MONSTER_QUEST_TARGET_OFFSETS")),
            nullptr);
        questTargetAddress_ = (questPtr >= 0x10000) ? questPtr : 0;
    }

    // With mhw_fix.so, the MonsterList chain is now functional.
    // Follow HunterPie's path: MONSTER_LIST_ADDRESS -> deref -> +0x38 -> Component*[].
    const std::uintptr_t listAddr = absolute(QStringLiteral("MONSTER_LIST_ADDRESS"));
    const auto headPtr = memory_.read<std::uintptr_t>(listAddr);
    if (!headPtr || *headPtr < 0x10000) {
        if (error) *error = QStringLiteral("MonsterList head is null/stale");
        return result;
    }
    // Cache the 128-slot Component* array: if it hasn't changed, reuse.
    const std::uintptr_t arrayBase = *headPtr + 0x38ULL;
    std::vector<std::uintptr_t> components;
    if (cachedArray_.size() == 128 && cachedArrayBase_ == arrayBase) {
        // Still cheap to re-read 1024 bytes once per tick to detect spawns,
        // but if identical to cache, skip per-component work below.
        const std::vector<std::uintptr_t> fresh = memory_.readArray<std::uintptr_t>(arrayBase, 128, error);
        components = fresh;
        cachedArray_ = fresh;
    } else {
        const std::vector<std::uintptr_t> fresh = memory_.readArray<std::uintptr_t>(arrayBase, 128, error);
        if (fresh.size() != 128) return result;
        components = fresh;
        cachedArray_ = fresh;
        cachedArrayBase_ = arrayBase;
    }

    // Cache monster struct addresses across ticks; only re-read HP if it
    // changes. The MonsterList array is static after the quest starts, so
    // we only do the heavy per-component dereferencing when a new
    // component appears (or disappears) in the array.
    QSet<std::uintptr_t> seenComponents;
    for (const std::uintptr_t comp : components) {
        if (comp < 0x10000 || comp >= 0x0000800000000000ULL)
            continue;
        seenComponents.insert(comp);
    }

    // Drop monsters that despawned.
    for (auto it = monsterCache_.begin(); it != monsterCache_.end(); ) {
        if (!seenComponents.contains(it->first))
            it = monsterCache_.erase(it);
        else
            ++it;
    }

    for (const std::uintptr_t comp : seenComponents) {
        // Component + 0x138 -> Monster*
        const auto innerPtr = memory_.read<std::uintptr_t>(comp + 0x138ULL);
        if (!innerPtr || *innerPtr < 0x10000 || *innerPtr >= 0x0000800000000000ULL)
            continue;
        const std::uintptr_t monster = *innerPtr;

        // HP: Monster + 0x7670 -> HealthPtr; HealthPtr + 0x60 -> [maxHP, curHP]
        const auto healthPtr = memory_.read<std::uintptr_t>(monster + 0x7670ULL);
        if (!healthPtr || !isSanePointer(*healthPtr)) continue;
        const auto hp = memory_.readArray<float>(*healthPtr + 0x60ULL, 2);
        if (hp.size() != 2) continue;
        const float maxHP = hp[0];
        const float curHP = hp[1];
        if (maxHP <= 0.0F) continue;

        // Enrage: MHWMonsterStatusStructure at monster+0x1BE30
        float enrageDuration = 0.0F, enrageMaxDuration = 0.0F;
        float enrageBuildup = 0.0F, enrageMaxBuildup = 0.0F;
        bool isEnraged = false;
        // Enrage: MHWMonsterStatusStructure INLINE at monster+0x1BE30
        // +0x14 IsActive, +0x18 Buildup, +0x1C DamageDone,
        // +0x24 Duration, +0x28 MaxDuration, +0x3C MaxBuildup
        if (const auto dur = memory_.read<float>(monster + 0x1BE30ULL + 0x24ULL)) {
            enrageDuration = *dur;
            if (const auto maxDur = memory_.read<float>(monster + 0x1BE30ULL + 0x28ULL))
                enrageMaxDuration = *maxDur;
            if (const auto active = memory_.read<int>(monster + 0x1BE30ULL + 0x14ULL))
                isEnraged = (*active != 0) && (enrageDuration > 0.0F);
        }
        // Buildup: independent of enrage state. When not enraged, shows
        // anger accumulation. When enraged, buildup resets to 0.
        // HunterPie MHWMonsterStatusStructure layout: IsActive=0x14, Buildup=0x18,
        // Duration=0x24, MaxDuration=0x28, MaxBuildup=0x3C.
        if (const auto bu = memory_.read<float>(monster + 0x1BE30ULL + 0x18ULL))
            enrageBuildup = *bu;
        if (const auto mbu = memory_.read<float>(monster + 0x1BE30ULL + 0x3CULL))
            enrageMaxBuildup = *mbu;

        // Resolve cache entry first (used for both part name lookup and cache
        // hit decision below).
        auto cachedIt = monsterCache_.find(comp);

        int hunterId = -1;
        if (const auto id = memory_.read<std::int32_t>(monster + 0x12280ULL))
            hunterId = *id;
        // Read body parts (always; HP changes on damage). Names are cached.
        const auto partPtr = memory_.read<std::uintptr_t>(monster + 0x1D058ULL);
        QVector<PartSnapshot> parts;
        // No per-part name cache: the name is recomputed every tick from
        // the fresh threshold suffix, so the displayed "0/2破" or
        // "1/2破" stays in sync with the live counter. An earlier version
        // cached names by part index; that caused the suffix to lag
        // when the counter ticked up.
        if (partPtr && isSanePointer(*partPtr)) {
            const std::uintptr_t normalAddr    = *partPtr + 0x40ULL;
            const std::uintptr_t severableBase = *partPtr + 0x1FC8ULL;
            const QVector<PartSchema> &schema = kPartSchemas.value(hunterId);

            // Helper: read a single MHWMonsterPartStructure (0x78 bytes) at addr.
            // Returns true if MaxHealth > 0 (valid slot).
            // Layout (verified 421810):
            //   +0x0C float MaxHealth
            //   +0x10 float Health        (per-layer current)
            //   +0x18 int   Counter
            //   +0x20 float ExtraMaxHealth
            //   +0x24 float ExtraHealth
            //   +0x6C uint  Index
            // HunterPie's MHWMonsterPart.Update() then dispatches by part type:
            //   Severable  -> MaxSever = data.MaxHealth;   Sever   = data.Health
            //   Flinch     -> MaxFlinch = data.MaxHealth;  Flinch  = data.Health
            //   Breakable  -> MaxFlinch = data.MaxHealth;  Flinch  = data.Health
            //                 (and threshold math on top for the cumulative HP)
            // In the Wine build the per-layer Health field is updated locally
            // for Flinch (stagger accumulator runs on the client). For
            // Breakable per-layer Health the client only sees the local hit
            // feedback; the cap-room (how much damage is still needed to
            // break the next layer) is what we expose as Health/MaxHealth
            // when thresholds remain, and as the layer's raw value after the
            // last threshold.
            auto readPartStruct = [&](std::uintptr_t addr, float &mhp, float &chp,
                                      float &emhp, float &ehp, int &counter,
                                      std::uint32_t &index) -> bool {
                std::vector<char> raw(0x78, 0);
                if (!memory_.readBytes(addr, raw.data(), 0x78, nullptr)) return false;
                std::memcpy(&mhp, raw.data() + 0x0C, 4);
                std::memcpy(&chp, raw.data() + 0x10, 4);
                std::memcpy(&emhp, raw.data() + 0x20, 4);
                std::memcpy(&ehp, raw.data() + 0x24, 4);
                std::memcpy(&counter, raw.data() + 0x18, 4);
                std::memcpy(&index, raw.data() + 0x6C, 4);
                return mhp > 0.0F;
            };

            // HunterPie UpdateBreakableData, copied semantically:
            // nextThreshold is the first configured threshold strictly greater
            // than raw Counter. Note: Counter is NOT a major-break count on
            // this build (Teostra head: one increment per small flinch), so
            // this scaled pair remains internal compatibility data only; the
            // UI must not label it "N/M破".
            auto applyBreakable = [&](PartSnapshot &p, const PartSchema &ps, float mhp, float chp) {
                if (!ps.thresholds[0]) return;

                int nextThreshold = 0;
                const char *t = ps.thresholds;
                while (*t) {
                    int threshold = 0;
                    while (*t >= '0' && *t <= '9') {
                        threshold = threshold * 10 + (*t - '0');
                        ++t;
                    }
                    if (threshold > p.counter) {
                        nextThreshold = threshold;
                        break;
                    }
                    while (*t && *t != ',') ++t;
                    if (*t == ',') ++t;
                }

                if (nextThreshold > 0) {
                    p.firstThreshold = nextThreshold;
                    p.maxHealth = nextThreshold * mhp;
                    p.health = std::max(0, nextThreshold - p.counter - 1) * mhp + chp;
                    if (p.health > p.maxHealth) p.health = p.maxHealth;
                } else {
                    // HunterPie: all configured thresholds have been crossed.
                    p.maxHealth = mhp;
                    p.health = mhp;
                }
            };

            // Iterate schema entries in source order, dispatching to the right
            // memory table for each part. Severable parts match the schema's
            // Id field against the live Index in the 0x1FC8 table (HunterPie
            // GetMonsterParts.cs:373-395). Normal parts use 0x1F8 stride from
            // 0x40 base, in normal-table order.
            //
            // The severable table is scanned from base each tick — at ~3.5 KB
            // (32 slots × 0x78) it's cheap enough that caching the cursor
            // across schema rows isn't worth the consistency risk when a
            // part breaks during the quest.
            int normalSlotIdx = 0;
            const std::uintptr_t sevEnd = severableBase + 0x78ULL * 32;
            for (int s = 0; s < schema.size(); ++s) {
                const PartSchema &ps = schema[s];

                // HunterPie behaviour: skip state-specific parts (PART_*_ROCK,
                // _MUD, _ICE, _SNOW, _GOLD, _GLOWING, _SPIDERS, etc.) — these
                // are tracked internally to drive body-part state
                // transitions (e.g. 煌啼龙 stone-shedding, 煌黑龙 gold
                // shell, 雪人防冰雪, 冰呪龍冰甲) but are NOT displayed on
                // the overlay. Also skip "PART_UNKNOWN" sentinels.
                // Rationale: kPartSchemas is auto-generated from the raw
                // HunterPie XML which contains these enum-named entries
                // verbatim. Showing them confuses users — they want to see
                // "头部/身体/尾巴", not "PART_HEAD_ROCK".
                if (QString::fromUtf8(ps.name).startsWith(QStringLiteral("PART_"))) {
                    continue;
                }

                if (ps.isSeverable) {
                    std::uintptr_t addr = severableBase;
                    for (int scan = 0; scan < 32 && addr < sevEnd; ++scan) {
                        // Sentinel check on the first 4 bytes (int32).
                        if (const auto pad = memory_.read<std::int32_t>(addr, nullptr)) {
                            if (*pad <= 0xA0) { addr += 0x8ULL; continue; }
                        }
                        float mhp = 0, chp = 0, emhp = 0, ehp = 0;
                        int counter = 0;
                        std::uint32_t index = 0;
                        if (!readPartStruct(addr, mhp, chp, emhp, ehp, counter, index))
                            break;
                        if (static_cast<int>(index) == ps.id) {
                            // Match — use schema position s as stable key.
                            PartSnapshot p;
                            p.index = 1000 + s; // positive key = severable
                            // Severable layer is bound to data.Health /
                            // data.MaxHealth verbatim (HunterPie
                            // UpdateSeverableData, no threshold math).
                            p.health = chp;
                            p.maxHealth = mhp;
                            p.flinch = chp;
                            p.maxFlinch = mhp;
                            p.extraHealth = ehp;
                            p.extraMaxHealth = emhp;
                            p.counter = counter;
                            p.isSeverable = true;
                            p.isBreakable = ps.thresholds[0] != '\0';
                            // Severed = the game cuts this part off and
                            // em* goes to a "destroyed" string; in practice
                            // for the tail / horn the part just stops ticking
                            // and the layer HP is no longer updated.
                            p.isBroken = counter > 0
                                      || (p.maxHealth > 0.0F && p.health <= 0.0F);
                            // Raw Counter is the observed small-flinch count;
                            // major-break count is not yet decoded, so keep the
                            // localized base name free of a false "N/M破" suffix.
                            const QString pname = QString::fromUtf8(ps.name);
                            // pname is static but recomputed every tick to
                            // avoid stale per-part name cache state.
                            p.name = pname.isEmpty()
                                ? QStringLiteral("Part[%1]").arg(s)
                                : pname;
                            parts.push_back(p);
                            break;
                        }
                        addr += 0x78ULL;
                    }
                } else {
                    // Normal table: stride 0x1F8 from base 0x40.
                    const std::uintptr_t addr = normalAddr + std::uintptr_t(normalSlotIdx) * 0x1F8ULL;
                    float mhp = 0, chp = 0, emhp = 0, ehp = 0;
                    int counter = 0;
                    std::uint32_t index = 0;
                    if (!readPartStruct(addr, mhp, chp, emhp, ehp, counter, index))
                        continue;
                    if (mhp <= 0.0F) continue; // empty slot
                    PartSnapshot p;
                    p.index = -1 - normalSlotIdx; // negative key = normal
                    // Flinch is always data.Health / data.MaxHealth
                    // (HunterPie UpdateFlinchData / UpdateBreakableData,
                    // first half — MaxFlinch is set unconditionally).
                    p.flinch = chp;
                    p.maxFlinch = mhp;
                    // Default MaxHealth to the raw per-layer value; parts
                    // with BreakThresholds get it overwritten by
                    // applyBreakable below (cumulative threshold math).
                    // Without this fallback, non-breakable parts end up
                    // with maxHealth == 0 and the UI shows "削 --".
                    p.maxHealth = mhp;
                    p.health = chp;
                    p.extraHealth = ehp;
                    p.extraMaxHealth = emhp;
                    p.counter = counter;
                    p.isSeverable = false;
                    p.isBreakable = ps.thresholds[0] != '\0';
                    // applyBreakable fills Health/MaxHealth + firstThreshold
                    // for parts that have BreakThresholds; it leaves
                    // p.health/p.maxHealth untouched for no-threshold parts
                    // (body, legs, non-severable tail on Tigrex) — which
                    // is why the default above is required.
                    applyBreakable(p, ps, mhp, chp);
                    // HunterPie's IsBroken formula (MonsterPartContextHandler.cs:120):
                    //   MaxHealth <= 0
                    // || (Health == MaxHealth && (Breaks > 0 || Flinch != MaxFlinch))
                    //
                    // Translated: a part is "broken" if its layer HP is full
                    // AND either (a) it has been broken before (Counter > 0)
                    // or (b) flinch is still accumulating on the current layer
                    // (Flinch < MaxFlinch — i.e. the part has not been reset
                    // back to a clean layer). Counter > 0 alone is not enough:
                    // it just means a previous tier was crossed, not that the
                    // current one is broken. The previous overlay version used
                    // `p.counter > 0` and showed "破 1" / "✖" the moment any
                    // tier was crossed even though the part had been reset and
                    // was a clean full-HP part again.
                    const bool flinchNotFull = std::fabs(p.flinch - p.maxFlinch) > 1e-4F;
                    p.isBroken = p.maxHealth <= 0.0F
                              || (std::fabs(p.health - p.maxHealth) <= 1e-4F
                                  && (p.counter > 0 || flinchNotFull));
                    // Raw Counter is the observed small-flinch count;
                    // major-break count is not yet decoded, so do not derive
                    // an "N/M破" suffix from this field.
                    const QString pname = QString::fromUtf8(ps.name);
                    // Always use the localized base name. It is deliberately
                    // not decorated with live Counter-derived text.
                    p.name = pname.isEmpty()
                        ? QStringLiteral("Part[%1]").arg(normalSlotIdx)
                        : pname;
                    parts.push_back(p);
                    ++normalSlotIdx;
                }
            }
        }

        // Cache hit: name+maxHP unchanged -> reuse, only update curHP+parts
        if (cachedIt != monsterCache_.end() &&
            cachedIt->second.maxHP == maxHP && !cachedIt->second.snapshot.internalName.isEmpty()) {
            MonsterSnapshot m = cachedIt->second.snapshot;
            m.health = curHP;
            m.parts = parts;
            m.enraged = isEnraged;
            m.enrageSeconds = enrageDuration;
            m.enrageMaxSeconds = enrageMaxDuration;
            m.enrageBuildup = enrageBuildup;
            m.enrageMaxBuildup = enrageMaxBuildup;
            if (const auto index = memory_.read<std::int32_t>(monster + 0x1228CULL))
                m.doubleLinkedListIndex = *index;
            m.isLockOnTarget = lockOnTargetIndex_ >= 0
                            && m.doubleLinkedListIndex == lockOnTargetIndex_;
            // Re-evaluate manual target every tick: the cache hit path
            // skips the rest of monster construction so we must apply
            // it here too.
            // HunterPie: quest target wins over manual pin. We surface three
            // flags so main.cpp can pick the right priority:
            //   isManualTargeted — player pinned on the map
            //   isQuestTargeted  — capture / investigation quest
            //   isManuallyTargeted — OR of the above (legacy compat)
            m.isManualTargeted  = (comp == manualTargetAddress_);
            m.isQuestTargeted   = (comp == questTargetAddress_);
            m.isManuallyTargeted = m.isManualTargeted || m.isQuestTargeted;
            result.push_back(m);
            monsterCache_[comp] = {m, maxHP};
            continue;
        }

        // Cache miss: read name
        char nameBuf[64] = {0};
        const auto nameStruct = memory_.read<std::uintptr_t>(monster + 0x2A0ULL);
        if (nameStruct && *nameStruct >= 0x10000 && *nameStruct < 0x0000800000000000ULL) {
            memory_.readBytes(*nameStruct + 0xCULL, nameBuf, sizeof(nameBuf) - 1, nullptr);
        }
        if (nameBuf[0] == 0) continue; // skip monsters with no name
        const QString rawEm = QString::fromUtf8(nameBuf);
        if (rawEm.startsWith(QStringLiteral("em\\ems"))) continue;
                QString displayName = QStringLiteral("%1").arg(hunterId, 3, 10, QLatin1Char('0'));


        // HunterPie 421810 zh-cn.xml Id -> name. The 421810 build reads
        // monster Id at monster+0x12280 and HunterPie's zh-cn.xml maps
        // it to a Chinese name. Submodule:
        //   https://github.com/HunterPie/Localization
        // Note: the Id differs from the em\* string (e.g. em057=雷狼龙
        // in this build; Id 94=雷狼龙 in this table; in older builds
        // 76=雷狼龙). em\* is the source of truth, Id is just a
        // cross-check.
        static const QHash<QString, QString> kNameTable = {
            // 72 entries from HunterPie/Localization zh-cn.xml (World section)
            // Source: https://github.com/HunterPie/Localization
            {QStringLiteral("000"), QStringLiteral("蛮颚龙")},
            {QStringLiteral("001"), QStringLiteral("火龙")},
            {QStringLiteral("004"), QStringLiteral("熔山龙")},
            {QStringLiteral("007"), QStringLiteral("大贼龙")},
            {QStringLiteral("009"), QStringLiteral("雌火龙")},
            {QStringLiteral("010"), QStringLiteral("樱火龙")},
            {QStringLiteral("011"), QStringLiteral("苍火龙")},
            {QStringLiteral("012"), QStringLiteral("角龙")},
            {QStringLiteral("013"), QStringLiteral("黑角龙")},
            {QStringLiteral("014"), QStringLiteral("麒麟")},
            {QStringLiteral("015"), QStringLiteral("贝希摩斯")},
            {QStringLiteral("016"), QStringLiteral("钢龙")},
            {QStringLiteral("017"), QStringLiteral("炎妃龙")},
            {QStringLiteral("018"), QStringLiteral("炎王龙")},
            {QStringLiteral("019"), QStringLiteral("熔岩龙")},
            {QStringLiteral("020"), QStringLiteral("恐暴龙")},
            {QStringLiteral("021"), QStringLiteral("土砂龙")},
            {QStringLiteral("022"), QStringLiteral("爆锤龙")},
            {QStringLiteral("023"), QStringLiteral("鹿首精")},
            {QStringLiteral("024"), QStringLiteral("毒妖鸟")},
            {QStringLiteral("025"), QStringLiteral("灭尽龙")},
            {QStringLiteral("026"), QStringLiteral("冥灯龙")},
            {QStringLiteral("027"), QStringLiteral("搔鸟")},
            {QStringLiteral("028"), QStringLiteral("眩鸟")},
            {QStringLiteral("029"), QStringLiteral("泥鱼龙")},
            {QStringLiteral("030"), QStringLiteral("飞雷龙")},
            {QStringLiteral("031"), QStringLiteral("浮空龙")},
            {QStringLiteral("032"), QStringLiteral("风漂龙")},
            {QStringLiteral("033"), QStringLiteral("大痹贼龙")},
            {QStringLiteral("034"), QStringLiteral("惨爪龙")},
            {QStringLiteral("035"), QStringLiteral("骨锤龙")},
            {QStringLiteral("036"), QStringLiteral("尸套龙")},
            {QStringLiteral("037"), QStringLiteral("岩贼龙")},
            {QStringLiteral("038"), QStringLiteral("绚辉龙")},
            {QStringLiteral("039"), QStringLiteral("爆鳞龙")},
            {QStringLiteral("051"), QStringLiteral("古代鹿首精")},
            {QStringLiteral("061"), QStringLiteral("轰龙")},
            {QStringLiteral("062"), QStringLiteral("迅龙")},
            {QStringLiteral("063"), QStringLiteral("冰牙龙")},
            {QStringLiteral("064"), QStringLiteral("惶怒恐暴龙")},
            {QStringLiteral("065"), QStringLiteral("碎龙")},
            {QStringLiteral("066"), QStringLiteral("斩龙")},
            {QStringLiteral("067"), QStringLiteral("硫斩龙")},
            {QStringLiteral("068"), QStringLiteral("雷颚龙")},
            {QStringLiteral("069"), QStringLiteral("水妖鸟")},
            {QStringLiteral("070"), QStringLiteral("歼世灭尽龙")},
            {QStringLiteral("071"), QStringLiteral("痹毒龙")},
            {QStringLiteral("072"), QStringLiteral("浮眠龙")},
            {QStringLiteral("073"), QStringLiteral("霜翼风漂龙")},
            {QStringLiteral("074"), QStringLiteral("凶爪龙")},
            {QStringLiteral("075"), QStringLiteral("雾瘴尸套龙")},
            {QStringLiteral("076"), QStringLiteral("红莲爆鳞龙")},
            {QStringLiteral("077"), QStringLiteral("冰鱼龙")},
            {QStringLiteral("078"), QStringLiteral("猛牛龙")},
            {QStringLiteral("079"), QStringLiteral("冰呪龙")},
            {QStringLiteral("080"), QStringLiteral("溟波龙")},
            {QStringLiteral("081"), QStringLiteral("天地煌啼龙")},
            {QStringLiteral("087"), QStringLiteral("煌黑龙")},
            {QStringLiteral("088"), QStringLiteral("金火龙")},
            {QStringLiteral("089"), QStringLiteral("银火龙")},
            {QStringLiteral("090"), QStringLiteral("黑狼鸟")},
            {QStringLiteral("091"), QStringLiteral("金狮子")},
            {QStringLiteral("092"), QStringLiteral("激昂金狮子")},
            {QStringLiteral("093"), QStringLiteral("黑轰龙")},
            {QStringLiteral("094"), QStringLiteral("雷狼龙")},
            {QStringLiteral("095"), QStringLiteral("狱狼龙")},
            {QStringLiteral("096"), QStringLiteral("猛爆碎龙")},
            {QStringLiteral("097"), QStringLiteral("冥赤龙")},
            {QStringLiteral("098"), QStringLiteral("木人桩")},
            {QStringLiteral("099"), QStringLiteral("战痕黑狼鸟")},
            {QStringLiteral("100"), QStringLiteral("霜刃冰牙龙")},
            {QStringLiteral("101"), QStringLiteral("黑龙")},
        };
        const auto it = kNameTable.find(displayName);
        if (it != kNameTable.end())
            displayName = *it;


        MonsterSnapshot m;
        m.address = monster;
        m.internalName = displayName;

        // HunterPie reads the schema Id at +0x12280; 0x1228C is the slot index (0..N-1) in MonsterList, not the schema Id.
        if (const auto id = memory_.read<std::int32_t>(monster + 0x12280ULL))
            m.id = hunterId;

        // HP: Monster + 0x7670 -> HealthPtr; HealthPtr + 0x60 -> [maxHP, curHP]
        m.maxHealth = maxHP;
        m.health = curHP;

        // Size (HunterPie GetMonsterCrownData):
        //   sizeModifier = +0x7730 (sanity: <=0 or >=2 -> 1)
        //   displayed size multiplier = sizeMultiplier(+0x184) / sizeModifier
        if (const auto sizeMul = memory_.read<float>(monster + 0x184ULL)) {
            float sizeMod = 1.0F;
            if (const auto raw = memory_.read<float>(monster + 0x7730ULL))
                if (*raw > 0.0F && *raw < 2.0F) sizeMod = *raw;
            if (*sizeMul > 0.0F)
                m.size = std::round(*sizeMul / sizeMod * 100.0F) / 100.0F;
        }

        m.enraged = isEnraged;
        m.enrageSeconds = enrageDuration;
        m.enrageMaxSeconds = enrageMaxDuration;
        m.enrageBuildup = enrageBuildup;
        m.enrageMaxBuildup = enrageMaxBuildup;
        if (const auto index = memory_.read<std::int32_t>(monster + 0x1228CULL))
            m.doubleLinkedListIndex = *index;
        m.isLockOnTarget = lockOnTargetIndex_ >= 0
                        && m.doubleLinkedListIndex == lockOnTargetIndex_;
        // Mark this monster as the player's manual target. The component
        // address we got from the list matches the one stored in
        // MHWMapMonsterSelectionStructure.SelectedMonster.
        // HunterPie: quest target wins over manual pin. We surface three
            // flags so main.cpp can pick the right priority:
            //   isManualTargeted — player pinned on the map
            //   isQuestTargeted  — capture / investigation quest
            //   isManuallyTargeted — OR of the above (legacy compat)
            m.isManualTargeted  = (comp == manualTargetAddress_);
            m.isQuestTargeted   = (comp == questTargetAddress_);
            m.isManuallyTargeted = m.isManualTargeted || m.isQuestTargeted;
        m.parts = parts;
        readMonsterAilments(m);
        monsterCache_[comp] = {m, maxHP};
        result.push_back(m);
    }
    return result;
}

void MhwReader::discoverMonsterTable()
{
    // Phase 1: find the name table by scanning rw-p regions for "em\\em001".
    const QString mapsPath = QStringLiteral("/proc/%1/maps").arg(memory_.pid());
    QFile mf(mapsPath);
    if (!mf.open(QIODevice::ReadOnly)) return;
    const QByteArray mapsData = mf.readAll();
    constexpr std::size_t kChunk = 0x400000;
    std::vector<char> buf(kChunk);

    for (const QByteArray &line : mapsData.split('\n')) {
        if (!line.contains("rw-p") && !line.contains("rw-s")) continue;
        const auto fields = line.split(' ');
        if (fields.size() < 2) continue;
        const auto range = fields[0].split('-');
        if (range.size() != 2) continue;
        bool okS = false, okE = false;
        const qulonglong rs = range[0].toULongLong(&okS, 16);
        const qulonglong re = range[1].toULongLong(&okE, 16);
        if (!okS || !okE || (re - rs) < 8ULL * 1024 * 1024) continue;

        for (std::uintptr_t addr = static_cast<std::uintptr_t>(rs);
             addr < static_cast<std::uintptr_t>(re); addr += kChunk) {
            const std::size_t want = std::min(kChunk, static_cast<std::size_t>(re - addr));
            if (!memory_.readBytes(addr, buf.data(), want, nullptr)) continue;
            for (std::size_t i = 0; i + 10 < want; ++i) {
                if (buf[i] != 'e' || buf[i+1] != 'm' || buf[i+2] != '\\'
                    || buf[i+3] != 'e' || buf[i+4] != 'm'
                    || buf[i+5] != '0' || buf[i+6] != '0' || buf[i+7] != '1')
                    continue;
                if (i < 0x2A0) continue;
                const std::uintptr_t strAddr = addr + i;
                std::uintptr_t firstBase = strAddr - 0x2A0ULL;
                // Walk back to table start
                for (int step = 1; step < 80; ++step) {
                    char check[8] = {0};
                    if (!memory_.readBytes(firstBase - 0x130ULL + 0x2A0ULL, check, 7, nullptr))
                        break;
                    if (check[0] == 'e' && check[1] == 'm' && check[2] == '\\'
                        && check[3] == 'e' && check[4] == 'm')
                        firstBase -= 0x130ULL;
                    else break;
                }
                std::size_t count = 0;
                for (std::size_t j = 0; j < 128; ++j) {
                    char check[8] = {0};
                    if (!memory_.readBytes(firstBase + j * 0x130ULL + 0x2A0ULL, check, 7, nullptr))
                        break;
                    if (check[0] != 'e' || check[1] != 'm' || check[2] != '\\'
                        || check[3] != 'e' || check[4] != 'm')
                        break;
                    ++count;
                }
                if (count < 35) continue;
                monsterTableBase_ = firstBase;
                monsterTableCount_ = count;
                qWarning("name table @ 0x%llx (%zu entries)",
                         static_cast<unsigned long long>(firstBase), count);

                // Phase 2: scan backwards for HP clusters [maxHP,curHP,?,0]
                hpClusters_.clear();
                struct Candidate { std::uintptr_t addr; float max; float cur; };
                std::vector<Candidate> cand;
                for (std::uintptr_t hpAddr = firstBase; hpAddr > firstBase - 0x40000ULL; hpAddr -= 16) {
                    const auto v = memory_.readArray<float>(hpAddr, 4);
                    if (v.size() < 4) continue;
                    float f0 = v[0], f1 = v[1], f2 = v[2], f3 = v[3];
                    if (f0 < 1000.0F || f1 < 0.0F || f1 > f0) continue;
                    if (f3 != 0.0F) continue; // padding marker
                    (void)f2;
                    cand.push_back({hpAddr, f0, f1});
                }
                // Group by similar maxHP (within 5%), require ≥2 entries
                hpClusters_.clear();
                struct C { std::uintptr_t addr; float max; float cur; int count; };
                std::vector<C> grp;
                for (const auto &c : cand) {
                    bool found = false;
                    for (auto &g : grp) {
                        if (std::abs(c.max - g.max) / g.max < 0.05F) {
                            g.count++;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        grp.push_back({c.addr, c.max, c.cur, 1});
                }
                for (const auto &g : grp) {
                    if (g.count >= 2 && g.max > 5000.0F) {
                        hpClusters_.push_back({g.addr, g.max});
                    }
                }
                // Sort by maxHP desc, keep top 8
                std::sort(hpClusters_.begin(), hpClusters_.end(),
                          [](const HpCluster &a, const HpCluster &b) { return a.maxHealth > b.maxHealth; });
                if (hpClusters_.size() > 8)
                    hpClusters_.resize(8);
                qWarning("HP clusters: %zu (total candidates: %zu)",
                         hpClusters_.size(), cand.size());
                return;
            }
        }
    }
}


} // namespace mhw