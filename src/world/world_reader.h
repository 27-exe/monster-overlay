#pragma once

// ProcessMemory, AddressMap and the four stateless process-memory helpers
// moved into monster-core (core/process_memory.h) so the Rise reader in this
// library no longer pulls World-only symbols. They are re-exported here for
// the World-side call sites, which spell them as MhwReader::staticMember.
// The MhwReader class below keeps only World-specific state.
#include "core/process_memory.h"

namespace mhw {

class MhwReader {
public:
    explicit MhwReader(QString mapPath,
                       QString exeName = QStringLiteral("monsterhunterworld.exe"));

    [[nodiscard]] GameSnapshot poll();
    [[nodiscard]] const QString &mapPath() const;

    // The four helpers below are
    //   findGamePid / selectLoadableMap /
    //   followPointerChain / followPointerChainOffsetThenDeref
    // in mhw namespace (core/process_memory.h). These thin static
    // delegating members exist purely so the existing World call sites
    // — main.cpp, the domain readers in this library, and the
    // probe/doctor tools under tests/ — keep their original spelling
    // without any behavioural change. The single implementation of each
    // lives in core/process_memory.cpp (monster-core).
    static std::optional<qint64> findGamePid(
        const QString &exeName = QStringLiteral("monsterhunterworld.exe"))
    {
        return mhw::findGamePid(exeName);
    }
    // Return the first candidate whose AddressMap parses. If none parses,
    // retain the first path so the caller can report an actionable error.
    [[nodiscard]] static QString selectLoadableMap(const QStringList &candidates)
    {
        return mhw::selectLoadableMap(candidates);
    }
    static std::uintptr_t followPointerChain(const ProcessMemory &memory,
                                             std::uintptr_t address,
                                             const std::vector<std::uintptr_t> &offsets,
                                             QString *error = nullptr)
    {
        return mhw::followPointerChain(memory, address, offsets, error);
    }
    // HunterPie ReadPtrAsync semantics: at each hop read the pointer stored at
    // address + offset. Keep it separate from followPointerChain (ReadAsync)
    // because Rise maps deliberately use both encodings.
    static std::uintptr_t followPointerChainOffsetThenDeref(
        const ProcessMemory &memory, std::uintptr_t address,
        const std::vector<std::uintptr_t> &offsets, QString *error = nullptr)
    {
        return mhw::followPointerChainOffsetThenDeref(memory, address, offsets, error);
    }

private:
    bool ensureAttached(GameSnapshot &snapshot);
    std::uintptr_t absolute(const QString &key) const;
    QString readUtf8(std::uintptr_t address, std::size_t maxLength) const;
    QString joinOffsets() const;
    void refreshPlayerIdentity(PlayerSnapshot &player);
    Zone readZone(QString *error);
    QVector<MonsterSnapshot> readMonsters(QString *error);
    void readMonsterAilments(MonsterSnapshot &monster);
    // v0.7.4: Tenderize is folded into PartSnapshot; this routine walks
    // the 10 in-memory TenderizeInfoStructure slots and writes each slot's
    // (Duration, MaxDuration) into every PartSnapshot whose PartSchema
    // declares the slot's PartId in its tenderizeIds list.
    void applyTenderizesToParts(MonsterSnapshot &monster);
    PlayerSnapshot readPlayer(QString *error);
    QVector<PartyMemberSnapshot> readParty(QString *error);
    // World session player count (HunterPie MHWPlayer.GetParty). 0 = solo /
    // not in a session, 1..4 = hunters in the current session. Companions
    // are not counted, which is why this replaces `party.size() > 1` as the
    // GameSnapshot::isMultiplayer signal.
    // Returns nullopt when the session structure could not be read (chain
    // miss, non-finite value, or a count outside [0,4]). 0 is a valid,
    // meaningful SOLO signal and is returned as such — the optional is what
    // keeps "read failed" and "the session is empty" distinguishable.
    [[nodiscard]] std::optional<int> readSessionPlayerCount(QString *error);
    QuestSnapshot readQuest(QString *error);
    // Sharpness — HunterPie MHWMeleeWeapon.GetWeaponSharpness. Returns
    // a zero-initialised snapshot when the equipped weapon is ranged
    // (bow, hbg, lbg) or the memory read fails. Force a fresh read on
    // every poll so the bar tracks sharpening / hitting in real time.
    SharpnessSnapshot readSharpness(int weaponId, QString *error);

    AddressMap map_;
    ProcessMemory memory_;
    QString mapPath_;
    QString exeName_;
    QString mapError_;
    std::uintptr_t imageBase_ = 0;
    std::uintptr_t monsterTableBase_ = 0;
    std::size_t monsterTableCount_ = 0;
    std::vector<HpCluster> hpClusters_;
    struct CachedMonster { MonsterSnapshot snapshot; float maxHP; };
    std::unordered_map<std::uintptr_t, CachedMonster> monsterCache_;
    std::vector<std::uintptr_t> cachedArray_;
    std::uintptr_t cachedArrayBase_ = 0;
    // Last manual target the player pinned on the map (zero if none / map
    // closed). Read once per poll from MHWMapMonsterSelectionStructure.
    std::uintptr_t manualTargetAddress_ = 0;
    // Last quest-pinned monster pointer. HunterPie reads both:
    //   quest target (cap quest / investigation mark)
    //   manual map pin (player opened map and pinned)
    // Each monster picks quest target first, falling back to manual
    // pin.  Address comes from MONSTER_QUEST_TARGET_ADDRESS →
    // MONSTER_QUEST_TARGET_OFFSETS → 0x48,0x1760,0x100.
    std::uintptr_t questTargetAddress_ = 0;
    // Sharpness cache: thresholds depend on the equipped weapon id,
    // so we re-read them only when the weapon changes. knocks the
    // ~7 pointer-chase + 7 short reads out of the per-tick critical
    // path during sustained combat.
    int  cachedSharpnessWeaponId_ = -1;
    int  cachedSharpnessThresholds_[7] = {0,0,0,0,0,0,0};
    bool cachedSharpnessThresholdsValid_ = false;
    // S1 (v0.7.5 audit): HunterPie MHWMeleeWeapon.MaximumSharpness needs
    // the MINIMUM_SHARPNESSES_ADDRESS table (8 ints, static in game
    // memory — HunterPie caches it with `??=`) and the weapon's MaxLevel
    // field to decide whether handicraft bonus applies. Cached here the
    // same way thresholds are.
    int  cachedMinimumSharpnesses_[8] = {0,0,0,0,0,0,0,0};
    bool cachedMinimumSharpnessesValid_ = false;
    // v0.10.8: last session player count that passed validation. Guards the
    // multiplayer part-card gate: a single failed/unaligned read must not
    // publish 0, because 0 is HunterPie's SOLO value and would restore every
    // fake full Health bar the gate exists to remove. Same failure mode (and
    // the same remedy) as the mantle CD cache above, which was added for the
    // same class of transient miss in 4-player sessions.
    int previousPlayerCount_ = 0;
    // HunterPie LockOn mode: LOCKON chain resolves a list node whose +0x950
    // contains the targeted monster's double-linked-list index.
    int lockOnTargetIndex_ = -1;
    // v0.7.5: mantle / equipment CD cache. The EQUIPMENT_ADDRESS and
    // ABNORMALITY_ADDRESS chains occasionally return 0 in multiplayer
    // (4-player sessions), causing the panel to flash the "no mantle
    // equipped" placeholder mid-combat for one tick at a time. We
    // stash the last successful read's mantle fields and replay them
    // when the chain fails, gated by a 5-second TTL so the panel
    // doesn't show stale data long after the player swapped gear.
    struct CachedMantles {
        // abnormality timers (line 100-122 of player_reader.cpp)
        float mantleHealthTimer       = 0.0F;
        float mantleHealthLargeTimer  = 0.0F;
        float mantleStaminaTimer      = 0.0F;
        float mantleStaminaLargeTimer = 0.0F;
        float mantleToolTimer         = 0.0F;
        float mantleToolLargeTimer    = 0.0F;
        float earplugTimer            = 0.0F;
        // equipment slot ids / timers / cooldowns (line 139-170)
        int   mantleSlot0Id           = -1;
        float mantleSlot0Timer        = 0.0F;
        float mantleSlot0Cooldown     = 0.0F;
        float mantleSlot0CooldownMax  = 270.0F;
        int   mantleSlot1Id           = -1;
        float mantleSlot1Timer        = 0.0F;
        float mantleSlot1Cooldown     = 0.0F;
        float mantleSlot1CooldownMax  = 270.0F;
    };
    CachedMantles cachedMantles_{};
    // Wall-clock of last successful mantle read, in ms since process start.
    // We use a counter rather than QDateTime so we don't need QDateTime in
    // the hot path; this header stays light-weight. Caller (poll loop)
    // bumps pollTicks_ each tick.
    qint64 mantlesCachedAtTick_ = -1;
    qint64 pollTick_ = 0;
    // TTL: how many polls a cached mantle read may live before we treat
    // it as stale. At ~4 Hz polling that's roughly 5 seconds.
    static constexpr qint64 kMantleCacheTtl = 20;
};

} // namespace mhw