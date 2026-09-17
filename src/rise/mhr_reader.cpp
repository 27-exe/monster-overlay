// SPDX-License-Identifier: Apache-2.0
// Core offsets and structures are derived from HunterPie/HunterPie (Apache-2.0).

#include "core/string_table.h"
#include "rise/mhr_reader.h"
#include "rise/mhr_abnormalities.h"
#include "rise/mhr_monster_names.h"
#include "rise/mhr_part_names.h"

#include <QDir>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace mhw {

namespace {
constexpr std::uintptr_t kPointerSize = sizeof(std::uintptr_t);

// i18n (integration): locale-aware status strings — the snapshot status is
// rendered verbatim by the player panel, so it must flip with the UI locale.
// Mirrors mhw_reader.cpp's trMessage pattern.
inline QString trMessage(const QString &key) { return StringTable::instance().tr(key); }

// v0.8.4-r7 restore-reader: zone translation. The returned int is
// cast into mhw::Zone in poll(). World zones occupy 1xx/3xx/4xx/5xx
// so 2xx is unsafe; the project instead maps:
//   type  0     -> MainMenu       (-1)
//   type  3     -> CharSelection  (199)
//   type  4     -> Village        (villageId + 700)
//   type 12     -> LoadingScreen  (-2)
//   type 5..11  -> HuntingZone    (huntingId + 600)
// (Rise uses HuntingId 0..16, so +600 puts us in 600..616 where the
// Zone enum reserves RiseLoc0..RiseLoc16, overlap-free with World.)
// Returns -1 for any unknown stage.type so poll() stays on
// Zone::MainMenu rather than fabricating a hunting zone.
inline int computeZoneId(const MHRStageStructure &stage)
{
    switch (stage.type) {
    case 0:  return -1;             // MainMenu
    case 3:  return 199;            // CharSelection
    case 4:  return stage.villageId + 700;
    case 12: return -2;             // LoadingScreen
    default:
        if (stage.type >= 5 && stage.type <= 11)
            return stage.huntingId + 600;
        return -1;                  // unknown — fall back to MainMenu
    }
}

// v0.8.4-r7 restore-reader: per-weapon sharpness struct layout
// (HunterPie MHRSharpnessStructure: int Level, int Hits, int MaxHits).
// Defined locally because no other translation unit needs it.
struct MHRSharpnessStructure {
    std::int32_t level;
    std::int32_t hits;
    std::int32_t maxHits;
};
static_assert(sizeof(MHRSharpnessStructure) == 12);
} // namespace

// v0.8 alignment: translate the Rise memory WeaponType byte (int at
// WEAPON_ADDRESS + 0x8C) into the HunterPie Core Weapon enum value that
// downstream consumers (Icon::weaponPath()'s kDirs[], readSharpness()'s
// threshold table, isMeleeWeapon()'s 0..10 range) expect. Mirrors
// HunterPie's MHRiseUtils.ToWeaponId() — see header for the full table.
// Out-of-range mem values (incl. None=0xFF) yield -1 so downstream
// isMeleeWeapon() / Icon::weaponPath() treat them as invalid.
int riseWeaponTypeToCore(int memoryIdx)
{
    static constexpr int kRiseMemToCore[14] = {
        /*  0 GreatSword    */  0,
        /*  1 SwitchAxe     */  8,
        /*  2 LongSword     */  3,
        /*  3 LightBowgun   */ 13,
        /*  4 HeavyBowgun   */ 12,
        /*  5 Hammer        */  4,
        /*  6 GunLance      */  7,
        /*  7 Lance         */  6,
        /*  8 SwordAndShield*/  1,
        /*  9 DualBlades    */  2,
        /* 10 HuntingHorn   */  5,
        /* 11 ChargeBlade   */  9,
        /* 12 InsectGlaive  */ 10,
        /* 13 Bow           */ 11,
    };
    if (memoryIdx < 0 || memoryIdx > 13) return -1;
    return kRiseMemToCore[memoryIdx];
}

MhrReader::MhrReader(QString mapPath)
    : mapPath_(std::move(mapPath))
{
    map_.load(mapPath_, &mapError_);
}

const QString &MhrReader::mapPath() const
{
    return mapPath_;
}

std::optional<qint64> MhrReader::findRisePid()
{
    return MhwReader::findGamePid(QStringLiteral("monsterhunterrise.exe"));
}

QString MhrReader::findBestMap(const QString &dataDir)
{
    const QRegularExpression re(
        QStringLiteral("^MonsterHunterRise\\.(\\d+)\\.(\\d+)\\.(\\d+)\\.(\\d+)\\.map$"));

    struct Candidate {
        QString path;
        std::array<int, 4> version{};
    };
    QVector<Candidate> candidates;
    const QDir dir(dataDir);
    const QStringList entries =
        dir.entryList({QStringLiteral("MonsterHunterRise.*.map")}, QDir::Files);
    for (const QString &name : entries) {
        const QRegularExpressionMatch match = re.match(name);
        if (!match.hasMatch())
            continue;
        candidates.push_back({dir.absoluteFilePath(name),
                              {match.captured(1).toInt(), match.captured(2).toInt(),
                               match.captured(3).toInt(), match.captured(4).toInt()}});
    }
    if (candidates.isEmpty())
        return {};

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.version > b.version; });

    const auto pid = findRisePid();
    if (!pid)
        return candidates.first().path;

    ProcessMemory memory;
    if (!memory.attach(*pid))
        return candidates.first().path;
    const std::uintptr_t imageBase =
        memory.imageBase(nullptr, QStringLiteral("monsterhunterrise.exe"));
    if (imageBase == 0)
        return candidates.first().path;

    for (const Candidate &candidate : candidates) {
        AddressMap map;
        if (!map.load(candidate.path) || !map.hasAddress(QStringLiteral("MONSTERS_ADDRESS")))
            continue;
        const std::uintptr_t base = MhwReader::followPointerChain(
            memory, imageBase + map.address(QStringLiteral("MONSTERS_ADDRESS")),
            map.offsets(QStringLiteral("MONSTER_LIST_OFFSETS")), nullptr);
        if (!base)
            continue;

        const auto countOpt = memory.read<std::int32_t>(
            base + kRiseMonoArrayLengthOffset);
        const int count = riseMonsterListCount(countOpt);
        for (int i = 0; i < count; ++i) {
            const auto monsterAddress = riseMonsterListElementAddress(base, i);
            if (!monsterAddress)
                continue;
            const auto monsterOpt = memory.read<std::uintptr_t>(*monsterAddress);
            if (!monsterOpt || !isSanePointer(*monsterOpt))
                continue;
            const auto id = memory.read<std::int32_t>(*monsterOpt + 0x2D4ULL);
            if (!hasRiseMonsterId(id))
                continue;
            const std::uintptr_t component = MhwReader::followPointerChainOffsetThenDeref(
                memory, *monsterOpt,
                map.offsets(QStringLiteral("MONSTER_HEALTH_COMPONENT_OFFSETS")), nullptr);
            if (!component)
                continue;
            const auto maxHealth = memory.read<float>(component + 0x18ULL);
            const std::uintptr_t encoded = MhwReader::followPointerChainOffsetThenDeref(
                memory, component,
                map.offsets(QStringLiteral("MONSTER_HEALTH_COMPONENT_ENCODED_OFFSETS")), nullptr);
            if (!encoded)
                continue;
            const auto hp = memory.read<float>(encoded + 0x18ULL);
            if (isRiseMapProbeMonster(id, maxHealth, hp))
                return candidate.path;
        }
    }
    return candidates.first().path;
}

std::uintptr_t MhrReader::absolute(const QString &key) const
{
    return imageBase_ + map_.address(key);
}

bool MhrReader::ensureAttached(GameSnapshot &snapshot)
{
    if (!mapError_.isEmpty()) {
        snapshot.status = mapError_;
        return false;
    }

    const auto pid = findRisePid();
    if (!pid) {
        memory_.detach();
        imageBase_ = 0;
        snapshot.status = trMessage(QStringLiteral("ui.reader.rise_waiting"));
        return false;
    }

    if (!memory_.attached() || memory_.pid() != *pid) {
        QString error;
        if (!memory_.attach(*pid, &error)) {
            snapshot.pid = *pid;
            snapshot.status = trMessage(QStringLiteral("ui.reader.rise_attach_failed"))
                                  .arg(*pid).arg(error);
            return false;
        }
        imageBase_ = memory_.imageBase(&error, QStringLiteral("monsterhunterrise.exe"));
        if (imageBase_ == 0) {
            memory_.detach();
            snapshot.status = error;
            return false;
        }
    }

    snapshot.attached = true;
    snapshot.pid = *pid;
    snapshot.imageBase = imageBase_;
    snapshot.status = trMessage(QStringLiteral("ui.reader.rise_connected"))
                          .arg(*pid)
                          .arg(static_cast<qulonglong>(imageBase_), 0, 16);
    return true;
}

QString MhrReader::readUtf16(std::uintptr_t address, int length) const
{
    if (length <= 0)
        return {};
    std::vector<char16_t> buffer(static_cast<std::size_t>(length));
    if (!memory_.readBytes(address, buffer.data(),
                           sizeof(char16_t) * static_cast<std::size_t>(length), nullptr))
        return {};
    return QString::fromUtf16(buffer.data(), length).trimmed();
}

MhrReader::StageInfo MhrReader::readZone(QString *error)
{
    StageInfo info;
    const std::uintptr_t stageBase = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("STAGE_ADDRESS")),
        map_.offsets(QStringLiteral("STAGE_OFFSETS")), error);
    if (!stageBase)
        return info;

    const auto stage = memory_.read<MHRStageStructure>(stageBase + 0x60ULL);
    if (!stage)
        return info;
    info.stage = *stage;
    info.inHuntingZone = isRiseHuntingZone(*stage);
    return info;
}

std::uintptr_t MhrReader::readLockOnTarget() const
{
    const std::uintptr_t typeAddr = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("LOCKON_ADDRESS")),
        map_.offsets(QStringLiteral("LOCKON_CAMERA_STYLE_OFFSETS")), nullptr);
    if (!typeAddr)
        return 0;
    const auto type = memory_.read<std::int32_t>(typeAddr);
    if (!type || *type < 0)
        return 0;

    const std::uintptr_t stylePtr = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("LOCKON_ADDRESS")),
        map_.offsets(QStringLiteral("LOCKON_OFFSETS")), nullptr);
    if (!stylePtr)
        return 0;

    const auto slotAddr = riseLockOnSlotEntryAddress(stylePtr, *type);
    if (!slotAddr)
        return 0;
    const auto entry = memory_.read<std::uintptr_t>(*slotAddr);
    if (!entry || !isSanePointer(*entry))
        return 0;
    const auto target = memory_.read<std::uintptr_t>(*entry + 0x78ULL);
    if (!target || !isSanePointer(*target))
        return 0;
    return *target;
}

void MhrReader::readMonsterParts(std::uintptr_t monster, MonsterSnapshot &snapshot)
{
    const std::uintptr_t flinchArr = MhwReader::followPointerChainOffsetThenDeref(
        memory_, monster,
        map_.offsets(QStringLiteral("MONSTER_FLINCH_HEALTH_COMPONENT_OFFSETS")), nullptr);
    const std::uintptr_t breakArr = MhwReader::followPointerChainOffsetThenDeref(
        memory_, monster,
        map_.offsets(QStringLiteral("MONSTER_BREAK_HEALTH_COMPONENT_OFFSETS")), nullptr);
    const std::uintptr_t severArr = MhwReader::followPointerChainOffsetThenDeref(
        memory_, monster,
        map_.offsets(QStringLiteral("MONSTER_SEVER_HEALTH_COMPONENT_OFFSETS")), nullptr);
    if (!flinchArr || !breakArr || !severArr)
        return;

    const auto flinchCount = memory_.read<std::int32_t>(flinchArr + 0x1CULL);
    const auto breakCount = memory_.read<std::int32_t>(breakArr + 0x1CULL);
    const auto severCount = memory_.read<std::int32_t>(severArr + 0x1CULL);
    if (!flinchCount || !breakCount || !severCount)
        return;
    if (*flinchCount != *breakCount || *breakCount != *severCount)
        return;

    const int count = *flinchCount;
    if (count <= 0 || count > 64)
        return;

    auto partValue = [&](std::uintptr_t arr, int idx, float &cur, float &max) -> bool {
        const auto partOpt = memory_.read<std::uintptr_t>(
            arr + 0x20ULL + static_cast<std::uintptr_t>(idx) * kPointerSize);
        if (!partOpt || !isSanePointer(*partOpt))
            return false;
        const std::uintptr_t part = *partOpt;
        const auto maxV = memory_.read<float>(part + 0x18ULL);
        const std::uintptr_t encoded = MhwReader::followPointerChainOffsetThenDeref(
            memory_, part,
            map_.offsets(QStringLiteral("MONSTER_HEALTH_COMPONENT_ENCODED_OFFSETS")), nullptr);
        if (!encoded)
            return false;
        const auto curV = memory_.read<float>(encoded + 0x18ULL);
        if (!maxV || !curV)
            return false;
        max = *maxV;
        cur = *curV;
        return true;
    };

    for (int i = 0; i < count; ++i) {
        float flinchCur = 0.0F, flinchMax = 0.0F;
        float breakCur = 0.0F, breakMax = 0.0F;
        float severCur = 0.0F, severMax = 0.0F;
        const bool hasFlinch = partValue(flinchArr, i, flinchCur, flinchMax);
        const bool hasBreak = partValue(breakArr, i, breakCur, breakMax);
        const bool hasSever = partValue(severArr, i, severCur, severMax);
        if (!hasFlinch && !hasBreak && !hasSever)
            continue;

        PartSnapshot part;
        part.index = i;
        part.flinch = flinchCur;
        part.maxFlinch = flinchMax;
        part.isBreakable = breakMax > 0.0F;
        part.isSeverable = severMax > 0.0F;
        part.partType = risePartType(part.isSeverable, part.isBreakable);
        part.name = risePartDisplayName(snapshot.id, i);
        switch (part.partType) {
        case PartType::Severable:
            part.health = severCur;
            part.maxHealth = severMax;
            break;
        case PartType::Breakable:
            part.health = breakCur;
            part.maxHealth = breakMax;
            break;
        case PartType::Flinch:
            part.health = flinchCur;
            part.maxHealth = flinchMax;
            break;
        }
        part.isBroken = (part.maxHealth > 0.0F && part.health <= 0.0F);
        snapshot.parts.push_back(part);
    }
}

void MhrReader::readMonsterTenderizes(std::uintptr_t /*monster*/,
                                      MonsterSnapshot &snapshot)
{
    // v0.8.4 E1: documented no-op. The Rise 16.0.2.0 monster component
    // does not expose a verified Clutch Claw / wound / tenderize table
    // — HunterPie's MHRise / MHRPartStructure declares Tenderize +
    // MaxTenderize on MHRMonsterPart but never assigns them, and the
    // upstream struct itself omits both fields. See
    //   - HunterPie.Integrations/.../MonsterHunterRise/Entity/Enemy/
    //     MHRMonsterPart.cs  (Tenderize setter never called)
    //   - HunterPie.Integrations/.../MonsterHunterRise/Definitions/
    //     MHRPartStructure.cs (struct has no Tenderize field)
    // and the B3 REPORT (v0.8.4-r2/adversarial-monster/REPORT.md)
    // §E1 — Tenderize 缺失. The 10-slot MHWTenderizeInfoStructure at
    // monster+0x1C458 used by World is NOT a valid offset for Rise;
    // reading 640 bytes at an unverified base would silently fabricate
    // data, which is worse than a missing strip.
    //
    // The right behaviour is: zero every part's tenderize fields, so any
    // stale value from a previous read (or a future in-place probe that
    // forgets to reset) cannot leak into the per-part tenderize strip
    // in panel_monster.cpp. The World reader's applyTenderizesToParts
    // applies the same zero-reset before walking slots; we mirror it
    // here for behavioural consistency. Until a Cheat Engine / FFXIV
    // Memoria-style probe locates the real Rise table, this stays the
    // only honest implementation.
    for (PartSnapshot &p : snapshot.parts) {
        p.tenderizeDuration    = 0.0F;
        p.tenderizeMaxDuration = 0.0F;
    }
    // TODO(E1): once a verified Rise tenderize / wound offset is found,
    // walk it here. Likely candidates to probe first (in a real Rise
    // session with the panel paused):
    //   - the same 0x1C458 region as World (cheap to try; will read
    //     640 bytes of plausible-looking floats if not gated);
    //   - monster+0x438 region (already used by readMonsterQurio for
    //     qurio state, so the surrounding 0x438..0x4A0 area is the
    //     most likely place for wound data);
    //   - the MHREnrageStructure analogue (enrage is the only other
    //     per-monster state struct with a timer+max pair, so wound
    //     timers may sit next to it).
    // Until then: panel_monster.cpp:642 `if (tenderizeDuration > 0.0F)`
    // remains false for every Rise part, the .pc tenderize strip stays
    // hidden, and no false-positive data is shown.
}

void MhrReader::readMonsterAilments(std::uintptr_t monster, MonsterSnapshot &snapshot)
{
    const std::uintptr_t base = MhwReader::followPointerChainOffsetThenDeref(
        memory_, monster,
        map_.offsets(QStringLiteral("MONSTER_AILMENTS_OFFSETS")), nullptr);
    if (!base)
        return;

    // HunterPie's MHRiseUtils.ReadArraySafeAsync<nint>() treats this as a
    // size-prefixed array: the element count is at +0x1C and the pointers
    // begin at +0x20. The previous fixed 17-slot walk started at `base`,
    // so it read the array header as slot 0 and could dereference entries
    // beyond the live list. Keep the same safety cap as HunterPie.
    constexpr int kAilmentCount = 17;
    const auto countOpt = memory_.read<std::int32_t>(base + 0x1CULL);
    if (!countOpt || *countOpt <= 0)
        return;
    const int count = std::min(*countOpt, kAilmentCount);

    for (int i = 0; i < count; ++i) {
        const auto aOpt = memory_.read<std::uintptr_t>(
            base + 0x20ULL + static_cast<std::uintptr_t>(i) * kPointerSize);
        if (!aOpt || !isSanePointer(*aOpt))
            continue;
        const std::uintptr_t a = *aOpt;

        MonsterAilmentSnapshot ail;
        ail.id = i;

        if (const auto counterPtr = memory_.read<std::uintptr_t>(a + mhr_ailment::kCounterPtr)) {
            if (isSanePointer(*counterPtr)) {
                if (const auto c = memory_.read<std::int32_t>(*counterPtr + 0x20ULL))
                    ail.counter = *c;
            }
        }
        if (const auto buildUpPtr = memory_.read<std::uintptr_t>(a + mhr_ailment::kBuildUpPtr)) {
            if (isSanePointer(*buildUpPtr)) {
                if (const auto b = memory_.read<float>(*buildUpPtr + 0x20ULL))
                    ail.buildup = *b;
            }
        }
        if (const auto maxBuildUpPtr = memory_.read<std::uintptr_t>(a + mhr_ailment::kMaxBuildUpPtr)) {
            if (isSanePointer(*maxBuildUpPtr)) {
                if (const auto mb = memory_.read<float>(*maxBuildUpPtr + 0x20ULL))
                    ail.maxBuildup = *mb;
            }
        }
        if (const auto mt = memory_.read<float>(a + mhr_ailment::kMaxTimer))
            ail.maxTimer = *mt;
        if (const auto t = memory_.read<float>(a + mhr_ailment::kTimer))
            ail.timer = *t;

        if (ail.counter <= 0 && ail.timer <= 0.0F && ail.buildup <= 0.0F
            && ail.maxBuildup <= 0.0F && ail.maxTimer <= 0.0F)
            continue;

        ail.active = ail.timer > 0.0F;
        // v0.9 i18n (WS-B): locale-aware slot label — en-us.xml while the
        // StringTable is on an English locale, otherwise the pre-i18n zh
        // label (the table lives in rise/mhr_abnormalities.cpp and is
        // verified against src/monster/part_schemas.cpp by its generator).
        ail.name = riseAilmentDisplayName(i);
        snapshot.ailments.push_back(ail);
    }
}

void MhrReader::readMonsterQurio(std::uintptr_t monster, MonsterSnapshot &snapshot)
{
    const auto qurioDataOpt = memory_.read<std::uintptr_t>(monster + 0x438ULL);
    if (!qurioDataOpt || !isSanePointer(*qurioDataOpt))
        return;
    const std::uintptr_t qurioData = *qurioDataOpt;

    if (const auto state = memory_.read<std::uint16_t>(qurioData + 0x12ULL))
        snapshot.qurioActive = (*state == 2);

    if (const auto threshold = memory_.read<MHRQurioThresholdStructure>(qurioData + 0x14ULL)) {
        snapshot.qurioMaxThreshold = threshold->maxThreshold;
        snapshot.qurioThreshold = threshold->threshold;
    }

    const std::uintptr_t partArrayBase = MhwReader::followPointerChainOffsetThenDeref(
        memory_, monster,
        map_.offsets(QStringLiteral("MONSTER_QURIO_HEALTH_COMPONENT_OFFSETS")), nullptr);
    if (!partArrayBase)
        return;

    const auto countOpt = memory_.read<std::int32_t>(partArrayBase + 0x1CULL);
    if (!countOpt)
        return;
    const int count = *countOpt;
    if (count <= 0 || count > 64)
        return;

    for (int i = 0; i < count; ++i) {
        const auto partOpt = memory_.read<std::uintptr_t>(
            partArrayBase + 0x20ULL + static_cast<std::uintptr_t>(i) * kPointerSize);
        if (!partOpt || !isSanePointer(*partOpt))
            continue;
        const std::uintptr_t part = *partOpt;

        MonsterSnapshot::QurioPart qpart;
        if (const auto active = memory_.read<std::uint8_t>(part + 0x10ULL))
            qpart.active = *active != 0;
        if (const auto maxHealth = memory_.read<float>(part + 0x38ULL))
            qpart.maxHealth = *maxHealth;
        if (const auto healthPtr = memory_.read<std::uintptr_t>(part + 0x18ULL)) {
            if (isSanePointer(*healthPtr)) {
                const std::uintptr_t encoded = MhwReader::followPointerChainOffsetThenDeref(
                    memory_, *healthPtr,
                    map_.offsets(QStringLiteral("MONSTER_HEALTH_COMPONENT_ENCODED_OFFSETS")), nullptr);
                if (encoded) {
                    if (const auto cur = memory_.read<float>(encoded + 0x18ULL))
                        qpart.health = *cur;
                }
            }
        }
        snapshot.qurioParts.push_back(qpart);
    }
}

QVector<MonsterSnapshot> MhrReader::readMonsters(QString *error)
{
    QVector<MonsterSnapshot> result;
    const std::uintptr_t base = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("MONSTERS_ADDRESS")),
        map_.offsets(QStringLiteral("MONSTER_LIST_OFFSETS")), error);
    if (!base)
        return result;

    const std::uintptr_t lockOnTarget = readLockOnTarget();

    const auto countOpt = memory_.read<std::int32_t>(base + kRiseMonoArrayLengthOffset);
    const int count = riseMonsterListCount(countOpt);
    for (int i = 0; i < count; ++i) {
        const auto monsterAddress = riseMonsterListElementAddress(base, i);
        if (!monsterAddress)
            continue;
        const auto monsterOpt = memory_.read<std::uintptr_t>(*monsterAddress);
        if (!monsterOpt || !isSanePointer(*monsterOpt))
            continue;
        const std::uintptr_t monster = *monsterOpt;

        const auto idOpt = memory_.read<std::int32_t>(monster + 0x2D4ULL);
        if (!hasRiseMonsterId(idOpt))
            continue;

        MonsterSnapshot snapshot;
        snapshot.address = monster;
        snapshot.id = *idOpt;
        snapshot.game = GameId::Rise;
        // v0.8.4-r19 monster-identity: official localized name. The id read
        // above is the same schema Id HunterPie resolves as
        // `//Strings/Monsters/Rise/Monster[@Id='{Id}']` (MHRMonster.cs:46-48);
        // the table is generated verbatim from HunterPie's zh-cn.xml and
        // en-us.xml, and riseMonsterName() picks the column for the active
        // locale (v0.9 i18n, WS-B). Ids the localization files have no entry
        // for (the upstream gaps 47..75 / 99..106) keep the numeric
        // placeholder.
        if (const char *localizedName = riseMonsterName(*idOpt))
            snapshot.internalName = QString::fromUtf8(localizedName);
        else
            snapshot.internalName = QStringLiteral("Monster #%1").arg(*idOpt);

        const std::uintptr_t healthComponent = MhwReader::followPointerChainOffsetThenDeref(
            memory_, monster,
            map_.offsets(QStringLiteral("MONSTER_HEALTH_COMPONENT_OFFSETS")), nullptr);
        if (healthComponent) {
            if (const auto maxHP = memory_.read<float>(healthComponent + 0x18ULL))
                snapshot.maxHealth = *maxHP;
            const std::uintptr_t encoded = MhwReader::followPointerChainOffsetThenDeref(
                memory_, healthComponent,
                map_.offsets(QStringLiteral("MONSTER_HEALTH_COMPONENT_ENCODED_OFFSETS")), nullptr);
            if (encoded) {
                if (const auto curHP = memory_.read<float>(encoded + 0x18ULL))
                    snapshot.health = *curHP;
            }
        }

        // Rise crown / body-size (HunterPie MHRMonster.GetMonsterCrown,
        // MHRMonster.cs:501-517): ReadPtrAsync(monster, MONSTER_CROWN_OFFSETS)
        // -> MHRSizeStructure at that object + kMhrSizeStructureOffset -> the
        // product of the two floats, which is the crown ratio compared against
        // MonsterData.xml <Crowns> (kRiseCrownThresholds) and printed by the
        // panel's crown icons / size chip.
        //
        // v0.8.4-r19 monster-identity: the product is validated
        // (riseMonsterSizeFromFactors) so an unreadable or absurd read leaves
        // snapshot.size at its 0.0F "unknown" default instead of the 1.0F
        // placeholder that made every monster display a dead `1.00x`.
        const std::uintptr_t sizeBase = MhwReader::followPointerChainOffsetThenDeref(
            memory_, monster,
            map_.offsets(QStringLiteral("MONSTER_CROWN_OFFSETS")), nullptr);
        if (sizeBase) {
            if (const auto sizeStruct =
                    memory_.read<MHRSizeStructure>(sizeBase + kMhrSizeStructureOffset)) {
                snapshot.size = riseMonsterSizeFromFactors(sizeStruct->sizeMultiplier,
                                                           sizeStruct->unkMultiplier);
            }
        }

        const std::uintptr_t enrageAddr = MhwReader::followPointerChainOffsetThenDeref(
            memory_, monster,
            map_.offsets(QStringLiteral("MONSTER_ENRAGE_OFFSETS")), nullptr);
        if (enrageAddr) {
            if (const auto enrage = memory_.read<MHREnrageStructure>(enrageAddr)) {
                snapshot.enraged = enrage->timer > 0.0F;
                snapshot.enrageSeconds = enrage->maxTimer - enrage->timer;
                snapshot.enrageMaxSeconds = enrage->maxTimer;
                snapshot.enrageBuildup = enrage->buildup;
                snapshot.enrageMaxBuildup = enrage->maxBuildup;
            }
        }

        // v0.8.4-r23 fatigue-semantics: monster stamina ("fatigue" meter).
        // HunterPie MHRMonster.GetMonsterStamina resolves
        // MONSTER_STAMINA_OFFSETS (a single 0x320 hop) and reads the
        // MHRStaminaStructure pair {Stamina @+0x20, MaxStamina @+0x24}.
        // Dragging stamina to zero is exactly the drooling / exhausted
        // state the player sees in-game; the exhaust ailment (slot 6) is a
        // separate 減気 gauge and must not be mistaken for this value.
        // riseMonsterStaminaFromPair keeps the honest-zero convention: a
        // failed or absurd read leaves the snapshot at 0/0 and the panel
        // hides the stamina row (same contract as riseMonsterSizeFromFactors).
        const std::uintptr_t staminaAddr = MhwReader::followPointerChainOffsetThenDeref(
            memory_, monster,
            map_.offsets(QStringLiteral("MONSTER_STAMINA_OFFSETS")), nullptr);
        if (staminaAddr) {
            if (const auto staminaStruct =
                    memory_.read<MHRStaminaStructure>(staminaAddr)) {
                riseMonsterStaminaFromPair(staminaStruct->stamina,
                                           staminaStruct->maxStamina,
                                           &snapshot.stamina,
                                           &snapshot.maxStamina);
            }
        }

        snapshot.isLockOnTarget = (lockOnTarget != 0 && monster == lockOnTarget);
        snapshot.isManualTargeted = snapshot.isLockOnTarget;
        snapshot.isManuallyTargeted = snapshot.isLockOnTarget;

        readMonsterParts(monster, snapshot);
        // v0.8.4 E1: wire up the documented no-op tenderize reader so
        // the per-tick lifecycle is identical to World. The method
        // zeros every part's tenderize fields (mirroring the World's
        // applyTenderizesToParts pre-reset) and is the single, obvious
        // place to drop a verified offset once one is found. See the
        // B3 REPORT §E1 for the full evidence and probe plan.
        readMonsterTenderizes(monster, snapshot);
        readMonsterAilments(monster, snapshot);
        readMonsterQurio(monster, snapshot);

        result.push_back(snapshot);
    }
    return result;
}

PlayerSnapshot MhrReader::readPlayer(QString *error)
{
    PlayerSnapshot result;

    std::uintptr_t charNamePtr = 0;
    const std::uintptr_t savePtr = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("CHARACTER_ADDRESS")),
        map_.offsets(QStringLiteral("CHARACTER_OFFSETS")), error);
    if (savePtr) {
        const auto namePtrOpt = memory_.read<std::uintptr_t>(savePtr + 0x0ULL);
        if (namePtrOpt && isSanePointer(*namePtrOpt)) {
            charNamePtr = *namePtrOpt;
            const auto len = memory_.read<std::int32_t>(charNamePtr + 0x10ULL);
            if (len && *len > 0 && *len < 128)
                result.name = readUtf16(charNamePtr + 0x14ULL, *len);
        }
    }

    const std::uintptr_t saveBase = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("SAVE_ADDRESS")),
        map_.offsets(QStringLiteral("SAVE_OFFSETS")), nullptr);
    // HunterPie compares every candidate's name pointer against the current
    // character pointer.  A missing/zero pointer is never a match; otherwise a
    // transient read failure could attribute the first save slot's rank to the
    // active character.
    if (saveBase && charNamePtr != 0) {
        for (int slot = 0; slot < 3; ++slot) {
            // Mirrors HunterPie MHRPlayer.cs:302 exactly:
            //   levelOffsets = { (_saveSlotId * 8) + 0x20, 0x18 }
            // The first offset is consumed by followPointerChain (deref
            // saveBase, then add 0x20+slot*8). The second is a direct
            // add into the resolved slot object (the level struct lives
            // there at +0x18, not behind another pointer).
            const std::uintptr_t slotEntryAddr = MhwReader::followPointerChain(
                memory_, saveBase, {0x20ULL + static_cast<std::uintptr_t>(slot) * kPointerSize}, nullptr);
            if (!slotEntryAddr)
                continue;
            const auto slotPtrOpt = memory_.read<std::uintptr_t>(slotEntryAddr);
            if (!slotPtrOpt || !isSanePointer(*slotPtrOpt))
                continue;
            const std::uintptr_t slotPtr = *slotPtrOpt;

            const auto slotNamePtr = memory_.read<std::uintptr_t>(slotPtr + 0x10ULL);
            if (!isRiseSaveSlotNameMatch(charNamePtr, slotNamePtr))
                continue;

            // slotPtr + 0x18 IS the MHRPlayerLevelStructure; no further deref.
            const std::uintptr_t levelAddr = slotPtr + 0x18ULL;
            const auto level = memory_.read<MHRPlayerLevelStructure>(levelAddr);
            if (!level)
                continue;
            result.highRank = level->highRank;
            result.masterRank = level->masterRank;
            break;
        }
    }

    const std::uintptr_t weaponAddr = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("WEAPON_ADDRESS")),
        map_.offsets(QStringLiteral("WEAPON_OFFSETS")), nullptr);
    if (weaponAddr) {
        if (const auto wp = memory_.read<std::int32_t>(weaponAddr + 0x8CULL))
            // Translate Rise memory WeaponType -> Core Weapon so
            // downstream Icon::weaponPath() / readSharpness() see Core order.
            result.weaponId = riseWeaponTypeToCore(*wp);
    }

    // v0.8.4-r7 fix-hud-layout: read HP / stamina from the player HUD
    // struct. UI_ADDRESS + PLAYER_HUD_OFFSETS resolves to an
    // MHRPlayerHudStructure (Pack=1, 0x34 bytes; Health@0x00, MaxHealth@0x08,
    // Stamina@0x28, MaxStamina@0x2C — see mhr_types.h). Skip silently on
    // read failure — fields stay zero and the panel shows 0/0 until the
    // chain resolves (same UX as before the fix). Mirrors HunterPie
    // MHRPlayer.cs:736-744. The previous v0.8 alignment placed
    // health@0x10 (16-byte synthesised pad) which is structurally wrong:
    // health would read the bottom half of an engine pointer and
    // maxHealth would land on memory that happens to be ~1.7e8 in
    // village hubs (final-audit §5.1, 反证 #1).
    const std::uintptr_t hudPtr = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("UI_ADDRESS")),
        map_.offsets(QStringLiteral("PLAYER_HUD_OFFSETS")), nullptr);
    if (hudPtr) {
        const auto hud = memory_.read<MHRPlayerHudStructure>(hudPtr);
        if (hud) {
            result.health     = hud->health;
            result.maxHealth  = hud->maxHealth;
            result.stamina    = hud->stamina;
            result.maxStamina = hud->maxStamina;
        }
    }

    // v0.7.1: wirebug (翔虫) state. Reads up to kRiseWirebugSlotCap (4)
    // source slots: the default plus any environment- or skill-granted
    // extras; only slots admitted by the default/environment/skill count
    // partition are exposed. Empty when the hunter hasn't unlocked
    // wirebug gathering yet or the offsets fail to resolve.
    readWirebugs(result, nullptr);

    // v0.8.4-r7 restore-reader (BUG #5): sharpness. Only meaningful for
    // melee weapons; readSharpness returns an invalid snapshot for bow /
    // lbg / hbg.
    result.sharpness = readSharpness(result.weaponId, nullptr);

    result.valid = isRisePlayerValid(result.name, result.health, result.maxHealth,
                                     result.stamina, result.maxStamina);
    return result;
}

// Rise wirebug entries use a Mono nint[] object. Its header length is at +0x1C
// and the pointer payload begins at +0x20, like HunterPie's ReadArraySafeAsync.
// Counts only identify valid partitions; they do not determine a contiguous
// iteration range.
void MhrReader::readWirebugs(PlayerSnapshot &snapshot, QString *error)
{
    const std::uintptr_t countBase = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("ABNORMALITIES_ADDRESS")),
        map_.offsets(QStringLiteral("WIREBUG_COUNT_OFFSETS")), error);
    if (!countBase)
        return;
    const auto count = memory_.read<MHRWirebugCountStructure>(countBase);
    if (!count || !isRiseWirebugCountSane(*count))
        return;

    const std::uintptr_t arrayPtr = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("ABNORMALITIES_ADDRESS")),
        map_.offsets(QStringLiteral("WIREBUG_DATA_OFFSETS")), nullptr);
    if (!isSanePointer(arrayPtr))
        return;
    const auto length = memory_.read<std::int32_t>(arrayPtr + kRiseMonoArrayLengthOffset);
    if (!length)
        return;
    const int slotCount = riseWirebugSlotsToRead(*count, *length);
    if (slotCount <= 0)
        return;

    for (int i = 0; i < slotCount; ++i) {
        const RiseWirebugType type = riseWirebugType(*count, i);
        if (type == RiseWirebugType::None)
            continue;

        const auto elementAddress = riseWirebugElementAddress(arrayPtr, i);
        if (!elementAddress)
            continue;
        const auto wbAddr = memory_.read<std::uintptr_t>(*elementAddress);
        if (!wbAddr || !isSanePointer(*wbAddr))
            continue;
        const auto wb = memory_.read<MHRWirebugStructure>(*wbAddr);
        if (!wb)
            continue;

        WirebugSnapshot snap;
        snap.slot = i;
        snap.isAvailable = true;
        snap.isTemporary = isRiseWirebugTemporary(type);

        std::optional<float> temporaryTimer;
        if (isRiseWirebugTemporary(type)) {
            const RiseWirebugExtraDataSource extraSource = riseWirebugExtraDataSource(type);
            const QString extraKey = extraSource == RiseWirebugExtraDataSource::Environment
                ? QStringLiteral("WIREBUG_EXTRA_DATA_OFFSETS")
                : QStringLiteral("WIREBUG_EXTRA_DATA_FROM_SKILL_OFFSETS");
            const std::uintptr_t extrasBase = MhwReader::followPointerChain(
                memory_, absolute(QStringLiteral("ABNORMALITIES_ADDRESS")),
                map_.offsets(extraKey), nullptr);
            if (extrasBase) {
                if (const auto extras = memory_.read<MHRWirebugExtrasStructure>(extrasBase))
                    temporaryTimer = extras->timer;
            }
        }
        const RiseWirebugSnapshotData data = riseWirebugSnapshotData(
            wb->cooldown, wb->maxCooldown, wb->extraCooldown, temporaryTimer);
        snap.cooldown = data.cooldown;
        snap.maxCooldown = data.maxCooldown;
        snap.timer = data.timer;
        snap.maxTimer = data.maxTimer;
        snapshot.wirebugs.push_back(snap);
    }
}

namespace {

// One category blob of the abnormalities structure — the shared body of
// MHRPlayer.GetConsumableAbnormalities (MHRPlayer.cs:452-493) and
// GetPlayerDebuffAbnormalities (:512-553).
//
// Every gate is applied before its reads so a schema that cannot be active
// costs nothing: flag gate (pure) -> DependsOn sub-id -> value slot. A read
// that fails skips the entry: upstream's failed read yields default(0), which
// for a timer is indistinguishable from "not active", and fabricating an
// active entry from a failed read would be worse than omitting it.
void appendRiseAbnormalities(const ProcessMemory &memory,
                             const RiseAbnormalitySchema *schemas,
                             std::size_t schemaCount,
                             std::uintptr_t blobBase,
                             const RiseAbnormalityConditions &conditions,
                             PlayerSnapshot &snapshot)
{
    if (blobBase == 0)
        return;

    for (std::size_t i = 0; i < schemaCount; ++i) {
        const RiseAbnormalitySchema &schema = schemas[i];

        if (!riseAbnormalityFlagSatisfied(schema, conditions))
            continue;

        int subId = 0;
        if (schema.dependsOn != 0) {
            const auto value = memory.read<std::int32_t>(
                blobBase + static_cast<std::uintptr_t>(schema.dependsOn));
            if (!value)
                continue;  // cannot validate WithValue -> not published
            subId = *value;
        }
        if (subId != schema.withValue)
            continue;

        float rawValue = 0.0F;
        bool valueRead = true;
        if (!schema.isInfinite) {
            const auto slot = memory.read<MHRAbnormalityValue>(
                blobBase + static_cast<std::uintptr_t>(schema.offset));
            valueRead = slot.has_value();
            if (slot) {
                rawValue = schema.isInteger
                    ? static_cast<float>(slot->integer)
                    : slot->timer;
            }
        }

        const RiseAbnormalityEvaluation evaluation = riseEvaluateAbnormality(
            schema, conditions, subId, rawValue, valueRead);
        if (!evaluation.active)
            continue;

        PlayerAbnormalitySnapshot entry;
        entry.id = QString::fromUtf8(schema.id);
        // v0.9 i18n (WS-B): locale-aware schema name (zh/en columns of the
        // generated table).
        entry.name = riseAbnormalityDisplayName(schema);
        entry.timer = evaluation.timer;
        entry.kind = schema.kind == RiseAbnormalityKind::Debuff
            ? AbnormalityKind::Debuff
            : AbnormalityKind::Buff;
        entry.isBuildup = schema.isBuildup;
        entry.isInfinite = schema.isInfinite;
        entry.maxTimer = schema.isBuildup
            ? static_cast<float>(schema.maxBuildup)
            : schema.maxTimer;
        snapshot.abnormalities.push_back(entry);
    }
}

} // namespace

// ===================================================================
// readAbnormalities — Rise consumable buffs + debuffs.
//
// Mirrors HunterPie
//   MHRPlayer.GetPlayerAbnormalitiesCleanup  (MHRPlayer.cs:395-405)
//   MHRPlayer.GetConsumableAbnormalities     (MHRPlayer.cs:436-494)
//   MHRPlayer.GetPlayerDebuffAbnormalities   (MHRPlayer.cs:497-554)
//   MHRPlayer.GetPlayerConditions            (MHRPlayer.cs:862-886)
//
// Mem path (all pre-resolved in data/MonsterHunterRise.16.0.2.0.map):
//   ABNORMALITIES_ADDRESS      + CONS_ABNORMALITIES_OFFSETS    consumable blob
//   ABNORMALITIES_ADDRESS      + DEBUFF_ABNORMALITIES_OFFSETS  debuff blob
//   LOCAL_PLAYER_DATA_ADDRESS  + PLAYER_CONDITION_OFFSETS      condition object
//       -> CommonConditions ulong @ +0x10, DebuffConditions ulong @ +0x38
//   LOCAL_PLAYER_DATA_ADDRESS  + PLAYER_ACTIONFLAG_OFFSETS     action-flag object
//       -> uint @ +0x20
//
// The per-schema body (flag gate, WithValue sub-id, IsInfinite, raw/60,
// MaxTimer inversion) lives in rise/mhr_abnormalities.h so it is unit
// testable without a game process. The snapshot is rebuilt every poll, so
// the upstream "clear" paths are expressed as an empty vector.
// ===================================================================
void MhrReader::readAbnormalities(PlayerSnapshot &snapshot, bool inHuntingZone,
                                  QString *error)
{
    if (!inHuntingZone)
        return;

    const std::uintptr_t consumableBase = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("ABNORMALITIES_ADDRESS")),
        map_.offsets(QStringLiteral("CONS_ABNORMALITIES_OFFSETS")), error);
    const std::uintptr_t debuffBase = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("ABNORMALITIES_ADDRESS")),
        map_.offsets(QStringLiteral("DEBUFF_ABNORMALITIES_OFFSETS")), nullptr);
    if (consumableBase == 0 && debuffBase == 0)
        return;

    // GetPlayerConditions: a chain that fails to resolve leaves the words at
    // 0, which is what HunterPie keeps after reporting CommonConditions.None
    // / DebuffConditions.None (and what a fresh field starts at).
    RiseAbnormalityConditions conditions;
    const std::uintptr_t conditionPtr = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("LOCAL_PLAYER_DATA_ADDRESS")),
        map_.offsets(QStringLiteral("PLAYER_CONDITION_OFFSETS")), nullptr);
    if (conditionPtr) {
        if (const auto common = memory_.read<std::uint64_t>(
                conditionPtr + kMhrCommonConditionsOffset))
            conditions.common = *common;
        if (const auto debuff = memory_.read<std::uint64_t>(
                conditionPtr + kMhrDebuffConditionsOffset))
            conditions.debuff = *debuff;
    }
    const std::uintptr_t actionPtr = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("LOCAL_PLAYER_DATA_ADDRESS")),
        map_.offsets(QStringLiteral("PLAYER_ACTIONFLAG_OFFSETS")), nullptr);
    if (actionPtr) {
        if (const auto action = memory_.read<std::uint32_t>(
                actionPtr + kMhrActionFlagOffset))
            conditions.action = *action;
    }

    // Hoist the accessor calls OUT of the appendRiseAbnormalities()
    // argument list: they write the count through the reference, and the
    // evaluation order of function arguments is unspecified. GCC
    // (x86-64, every opt level) evaluates arguments right-to-left, so the
    // plain `consumableCount` argument was evaluated BEFORE the accessor
    // wrote it — the callee always received 0 and iterated zero schemas,
    // leaving the snapshot permanently empty even with active buffs
    // (live-confirmed 2026-09-17; assembler trace in the v0.8.4-r22
    // status-reader report). Keep these as separate statements so the
    // count is written before anything reads it.
    std::size_t consumableCount = 0;
    const RiseAbnormalitySchema *consumableSchemas =
        riseConsumableAbnormalities(consumableCount);
    std::size_t debuffCount = 0;
    const RiseAbnormalitySchema *debuffSchemas =
        riseDebuffAbnormalities(debuffCount);
    appendRiseAbnormalities(memory_, consumableSchemas, consumableCount,
                            consumableBase, conditions, snapshot);
    appendRiseAbnormalities(memory_, debuffSchemas, debuffCount, debuffBase,
                            conditions, snapshot);
}

QuestSnapshot MhrReader::readQuest(QString *error)
{
    QuestSnapshot result;
    const std::uintptr_t questStruct = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("QUEST_ADDRESS")),
        map_.offsets(QStringLiteral("QUEST_OFFSETS")), error);
    if (!questStruct)
        return result;

    // v0.8.4-r7 restore-reader (BUG #2): at +0x170 HunterPie exposes
    // TimeElapsed, NOT TimeLeft. The real TimeLimit lives at +0x178.
    // Populate all three timer fields consistently with World.
    if (const auto elapsed = memory_.read<float>(questStruct + 0x170ULL))
        result.elapsedSeconds = *elapsed;
    if (const auto limit = memory_.read<float>(questStruct + 0x178ULL))
        result.maxTimerSeconds = *limit;
    // Derive the countdown that panel_player.cpp displays; clamp to
    // [0, max] so a one-frame jitter never overflows.
    if (result.maxTimerSeconds > 0.0F) {
        result.timeLeftSeconds = std::max(
            0.0F,
            result.maxTimerSeconds - result.elapsedSeconds);
        if (result.timeLeftSeconds > result.maxTimerSeconds)
            result.timeLeftSeconds = result.maxTimerSeconds;
    }

    if (const auto status = memory_.read<std::int32_t>(questStruct + 0x110ULL))
        result.state = *status;
    // v0.8.4-r7 restore-reader (BUG #3): category / deaths / maxDeaths
    // from MHRQuestStructure; id / stars / rank from QuestDataPointer
    // (+0x118) -> Normal or Anomaly (Sunbreak) data structure.
    if (const auto category = memory_.read<std::int32_t>(questStruct + 0x120ULL))
        result.category = *category;
    if (const auto maxDeaths = memory_.read<std::int32_t>(questStruct + 0x15CULL))
        result.maxDeaths = *maxDeaths;
    if (const auto deaths = memory_.read<std::int32_t>(questStruct + 0x160ULL))
        result.deaths = *deaths;

    const auto qdp = memory_.read<std::uintptr_t>(questStruct + 0x118ULL);
    if (qdp && isSanePointer(*qdp)) {
        const std::uintptr_t questData = *qdp;
        const auto normalPtr =
            memory_.read<std::uintptr_t>(questData + 0x10ULL);
        const auto anomalyPtr =
            memory_.read<std::uintptr_t>(questData + 0x28ULL);

        // Prefer the Normal pointer; fall back to Anomaly (Sunbreak).
        if (normalPtr && isSanePointer(*normalPtr)) {
            if (const auto id = memory_.read<std::int32_t>(*normalPtr + 0x10ULL))
                result.id = *id;
            if (const auto stars = memory_.read<std::int32_t>(*normalPtr + 0x24ULL))
                result.stars = riseNormalQuestStars(*stars);
            if (const auto rank = memory_.read<std::int32_t>(*normalPtr + 0x28ULL))
                result.rank = *rank;
        } else if (anomalyPtr && isSanePointer(*anomalyPtr)) {
            if (const auto id = memory_.read<std::int32_t>(*anomalyPtr + 0x14ULL))
                result.id = *id;
            // Anomaly quests have no Stars field; HunterPie surfaces
            // `Level` as Stars so the panel shows the MR level.
            if (const auto level = memory_.read<std::int32_t>(*anomalyPtr + 0x18ULL))
                result.stars = *level;
            result.isAnomaly = true;
        }
    }

    // Match HunterPie MHRGame.GetQuest: InQuest and a positive id are not
    // sufficient when MHRQuestStructure.Type has no supported ToQuestType map.
    result.active = isRiseQuestActive(result.id, result.state, result.category);
    return result;
}

GameSnapshot MhrReader::poll()
{
    GameSnapshot snapshot;
    snapshot.game = GameId::Rise;
    if (!ensureAttached(snapshot))
        return snapshot;

    QString error;
    const StageInfo stageInfo = readZone(nullptr);
    snapshot.zone = static_cast<Zone>(computeZoneId(stageInfo.stage));

    if (stageInfo.inHuntingZone)
        snapshot.monsters = readMonsters(&error);

    snapshot.player = readPlayer(nullptr);

    // v0.8.4-r18 player-abnormalities: consumable buffs + debuffs. Rise-only
    // (World keeps its own reader); the cleanup path empties them outside a
    // hunting zone, so the zone flag is threaded through from readZone().
    readAbnormalities(snapshot.player, stageInfo.inHuntingZone, nullptr);

    snapshot.quest = stageInfo.inHuntingZone ? readQuest(nullptr) : QuestSnapshot{};

    snapshot.party = {};
    snapshot.isMultiplayer = false;

    if (!error.isEmpty() && snapshot.monsters.isEmpty())
        snapshot.status += trMessage(QStringLiteral("ui.reader.partial_read_failed")).arg(error);
    return snapshot;
}

// ===================================================================
// readSharpness — HunterPie MHRMeleeWeapon.GetWeaponSharpness
//
// Reads the local player's weapon sharpness. Returns a zero-initialised
// snapshot (i.e. valid=false) when:
//   - the equipped weapon is ranged (Bow / HBG / LBG), which have no
//     sharpness bar in Rise
//   - the memory read fails (game not running, address not mapped)
//   - the in-game level field is Broken (-1) or Invalid (>6)
//
// Mem path (all pre-resolved in data/MonsterHunterRise.16.0.2.0.map):
//   SHARPNESS_ADDRESS + SHARPNESS_OFFSETS
//     MHRSharpnessStructure { int Level, int Hits, int MaxHits }
//   SHARPNESS_ADDRESS + SHARPNESS_ARRAY_OFFSETS
//     int[] thresholds        (7 per-level upper bounds, summed into
//                              cumulative thresholds per HunterPie's
//                              CalculateThresholds)
//
// The threshold array is cached by the Mono int[] object address. Two
// weapons of the same type can have distinct arrays, so weaponId alone
// is not a valid cache key.
// ===================================================================
SharpnessSnapshot MhrReader::readSharpness(int weaponId, QString *error)
{
    SharpnessSnapshot result;

    // HunterPie only runs MHRMeleeWeapon.GetWeaponSharpness() for a known
    // melee weapon.  Do this before any sharpness read so Weapon.None (-1)
    // cannot render data retained at SHARPNESS_ADDRESS.
    if (!isRiseMeleeWeaponId(weaponId))
        return result;

    // 1. Read the live sharpness state. Same as HunterPie: only a valid
    //    in-range level can produce a visible gauge.
    const std::uintptr_t sharpPtr = MhwReader::followPointerChain(
        memory_,
        absolute(QStringLiteral("SHARPNESS_ADDRESS")),
        map_.offsets(QStringLiteral("SHARPNESS_OFFSETS")),
        error);
    if (!sharpPtr) {
        cachedSharpnessThresholdsValid_ = false;
        cachedSharpnessArrayPtr_ = 0;
        return result;
    }

    const auto sharp = memory_.read<MHRSharpnessStructure>(sharpPtr);
    if (!sharp || sharp->level < 0 || sharp->level > 6)
        return result;
    result.level = sharp->level;
    result.currentHits = sharp->hits;
    result.maxHits = sharp->maxHits;

    // 3. The resolved address is the Mono int[] object header. Its length
    //    is at +0x1C and elements begin at +0x20 (MHRiseUtils.ReadArrayAsync).
    const std::uintptr_t arrayPtr = MhwReader::followPointerChain(
        memory_,
        absolute(QStringLiteral("SHARPNESS_ADDRESS")),
        map_.offsets(QStringLiteral("SHARPNESS_ARRAY_OFFSETS")),
        nullptr);
    if (!isSanePointer(arrayPtr)) {
        cachedSharpnessThresholdsValid_ = false;
        cachedSharpnessArrayPtr_ = 0;
        return result;
    }

    if (riseSharpnessCacheNeedsRefresh(cachedSharpnessArrayPtr_,
                                       cachedSharpnessThresholdsValid_, arrayPtr)) {
        const auto length = memory_.read<std::int32_t>(arrayPtr + 0x1CULL);
        if (!length || *length < 1 || *length > 7) {
            cachedSharpnessThresholdsValid_ = false;
            cachedSharpnessArrayPtr_ = 0;
            return result;
        }

        const auto raw = memory_.readArray<std::int32_t>(
            arrayPtr + 0x20ULL, static_cast<std::size_t>(*length));
        std::array<int, 7> thresholds{};
        if (raw.size() != static_cast<std::size_t>(*length)
            || !riseBuildSharpnessThresholds(raw, &thresholds)) {
            cachedSharpnessThresholdsValid_ = false;
            cachedSharpnessArrayPtr_ = 0;
            return result;
        }

        cachedSharpnessThresholds_ = thresholds;
        cachedSharpnessArrayPtr_ = arrayPtr;
        cachedSharpnessThresholdsValid_ = true;
    }

    for (int i = 0; i < 7; ++i)
        result.thresholds[i] = cachedSharpnessThresholds_[static_cast<std::size_t>(i)];

    // threshold = end of previous-level segment (or 0 at Red).
    result.threshold = (result.level <= 0)
        ? 0
        : result.thresholds[result.level - 1];
    result.valid = true;
    return result;
}

} // namespace mhw
