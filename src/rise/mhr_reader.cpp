// SPDX-License-Identifier: Apache-2.0
// Core offsets and structures are derived from HunterPie/HunterPie (Apache-2.0).

#include "rise/mhr_reader.h"

#include <QDir>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace mhw {

namespace {
constexpr std::uintptr_t kPointerSize = sizeof(std::uintptr_t);
} // namespace

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

        constexpr int kMaxMonsters = 5;
        for (int i = 0; i < kMaxMonsters; ++i) {
            const auto monsterOpt = memory.read<std::uintptr_t>(
                base + static_cast<std::uintptr_t>(i) * kPointerSize);
            if (!monsterOpt || !isSanePointer(*monsterOpt))
                continue;
            const std::uintptr_t component = MhwReader::followPointerChain(
                memory, *monsterOpt,
                map.offsets(QStringLiteral("MONSTER_HEALTH_COMPONENT_OFFSETS")), nullptr);
            if (!component)
                continue;
            const std::uintptr_t encoded = MhwReader::followPointerChain(
                memory, component,
                map.offsets(QStringLiteral("MONSTER_HEALTH_COMPONENT_ENCODED_OFFSETS")), nullptr);
            if (!encoded)
                continue;
            const auto hp = memory.read<float>(encoded + 0x18ULL);
            if (hp && *hp > 0.0F)
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
        snapshot.status = QStringLiteral("等待 MonsterHunterRise.exe");
        return false;
    }

    if (!memory_.attached() || memory_.pid() != *pid) {
        QString error;
        if (!memory_.attach(*pid, &error)) {
            snapshot.pid = *pid;
            snapshot.status = QStringLiteral("已发现 PID %1，但无法附加：%2").arg(*pid).arg(error);
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
    snapshot.status = QStringLiteral("MHR 已连接 · PID %1 · BASE 0x%2")
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
    info.inHuntingZone = (stage->type >= 5 && stage->type <= 11);
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

    const std::uintptr_t slotAddr =
        stylePtr + static_cast<std::uintptr_t>(*type) * 8ULL;
    const auto target = memory_.read<std::uintptr_t>(slotAddr + 0x78ULL);
    if (!target || !isSanePointer(*target))
        return 0;
    return *target;
}

void MhrReader::readMonsterParts(std::uintptr_t monster, MonsterSnapshot &snapshot)
{
    const std::uintptr_t flinchArr = MhwReader::followPointerChain(
        memory_, monster,
        map_.offsets(QStringLiteral("MONSTER_FLINCH_HEALTH_COMPONENT_OFFSETS")), nullptr);
    const std::uintptr_t breakArr = MhwReader::followPointerChain(
        memory_, monster,
        map_.offsets(QStringLiteral("MONSTER_BREAK_HEALTH_COMPONENT_OFFSETS")), nullptr);
    const std::uintptr_t severArr = MhwReader::followPointerChain(
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
        const std::uintptr_t encoded = MhwReader::followPointerChain(
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
        if (part.isBreakable) {
            part.health = breakCur;
            part.maxHealth = breakMax;
        } else if (part.isSeverable) {
            part.health = severCur;
            part.maxHealth = severMax;
        } else {
            part.health = flinchCur;
            part.maxHealth = flinchMax;
        }
        part.isBroken = (part.maxHealth > 0.0F && part.health <= 0.0F);
        snapshot.parts.push_back(part);
    }
}

void MhrReader::readMonsterAilments(std::uintptr_t monster, MonsterSnapshot &snapshot)
{
    const std::uintptr_t base = MhwReader::followPointerChain(
        memory_, monster,
        map_.offsets(QStringLiteral("MONSTER_AILMENTS_OFFSETS")), nullptr);
    if (!base)
        return;

    constexpr int kAilmentCount = 17;
    for (int i = 0; i < kAilmentCount; ++i) {
        const auto aOpt = memory_.read<std::uintptr_t>(
            base + static_cast<std::uintptr_t>(i) * kPointerSize);
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
        ail.name = kRiseAilmentNames.value(i, QStringLiteral("异常%1").arg(i));
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

    const std::uintptr_t partArrayBase = MhwReader::followPointerChain(
        memory_, monster, {0x438ULL, 0x38ULL}, nullptr);
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
                const std::uintptr_t encoded = MhwReader::followPointerChain(
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

    constexpr int kMaxMonsters = 5;
    for (int i = 0; i < kMaxMonsters; ++i) {
        const auto monsterOpt = memory_.read<std::uintptr_t>(
            base + static_cast<std::uintptr_t>(i) * kPointerSize);
        if (!monsterOpt || !isSanePointer(*monsterOpt))
            continue;
        const std::uintptr_t monster = *monsterOpt;

        const auto idOpt = memory_.read<std::int32_t>(monster + 0x2D4ULL);
        if (!idOpt || *idOpt == 0)
            continue;

        MonsterSnapshot snapshot;
        snapshot.address = monster;
        snapshot.id = *idOpt;
        snapshot.game = GameId::Rise;
        snapshot.internalName = QStringLiteral("Monster #%1").arg(*idOpt);

        const std::uintptr_t healthComponent = MhwReader::followPointerChain(
            memory_, monster,
            map_.offsets(QStringLiteral("MONSTER_HEALTH_COMPONENT_OFFSETS")), nullptr);
        if (healthComponent) {
            if (const auto maxHP = memory_.read<float>(healthComponent + 0x18ULL))
                snapshot.maxHealth = *maxHP;
            const std::uintptr_t encoded = MhwReader::followPointerChain(
                memory_, healthComponent,
                map_.offsets(QStringLiteral("MONSTER_HEALTH_COMPONENT_ENCODED_OFFSETS")), nullptr);
            if (encoded) {
                if (const auto curHP = memory_.read<float>(encoded + 0x18ULL))
                    snapshot.health = *curHP;
            }
        }

        const std::uintptr_t sizeBase = MhwReader::followPointerChain(
            memory_, monster,
            map_.offsets(QStringLiteral("MONSTER_CROWN_OFFSETS")), nullptr);
        if (sizeBase) {
            if (const auto sizeStruct = memory_.read<MHRSizeStructure>(sizeBase + 0x24ULL))
                snapshot.size = sizeStruct->sizeMultiplier * sizeStruct->unkMultiplier;
        }

        const std::uintptr_t enrageAddr = MhwReader::followPointerChain(
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

        snapshot.isLockOnTarget = (lockOnTarget != 0 && monster == lockOnTarget);
        snapshot.isManualTargeted = snapshot.isLockOnTarget;
        snapshot.isManuallyTargeted = snapshot.isLockOnTarget;

        readMonsterParts(monster, snapshot);
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
    if (saveBase) {
        for (int slot = 0; slot < 3; ++slot) {
            const auto slotPtrOpt = memory_.read<std::uintptr_t>(
                saveBase + static_cast<std::uintptr_t>(slot) * kPointerSize);
            if (!slotPtrOpt || !isSanePointer(*slotPtrOpt))
                continue;
            const std::uintptr_t slotPtr = *slotPtrOpt;

            const auto slotNamePtr = memory_.read<std::uintptr_t>(slotPtr + 0x0ULL);
            if (charNamePtr != 0 && slotNamePtr && *slotNamePtr != charNamePtr)
                continue;

            const std::uintptr_t levelAddr = MhwReader::followPointerChain(
                memory_, slotPtr, {0x20ULL, 0x18ULL}, nullptr);
            if (!levelAddr)
                continue;
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
            result.weaponId = *wp;
    }

    // v0.7.1: wirebug (翔虫) state. Reads up to 3 wirebugs: the default
    // plus any environment- or skill-granted extras. Empty when the
    // hunter hasn't unlocked wirebug gathering yet or the offsets fail
    // to resolve.
    readWirebugs(result, nullptr);

    result.valid = !result.name.isEmpty();
    return result;
}

// v0.7.1: read up to 3 wirebugs. The count comes from
// ABNORMALITIES_ADDRESS + WIREBUG_COUNT_OFFSETS (3 ints), and the per-
// wirebug pointers come from ABNORMALITIES_ADDRESS + WIREBUG_DATA_OFFSETS
// (an array of 3 qint64 pointers). Each wirebug struct carries its own
// cooldown + maxCooldown + extraCooldown floats; environment / skill
// wirebugs additionally read their bonus timer from
// WIREBUG_EXTRA_DATA_OFFSETS (MHRWirebugExtrasStructure).
//
// The blocked state (IceBlight / Wind Mantle / etc.) is read once from
// UI_ADDRESS + IS_WIREBUG_BLOCKED_OFFSETS. The task spec says we treat
// any non-zero value as Blocked for now (no per-cause classification).
void MhrReader::readWirebugs(PlayerSnapshot &snapshot, QString *error)
{
    // Step 1: count = max(0, default + environment + skill). Clamp at 3
    // because the game never has more than 3 wirebugs active.
    const std::uintptr_t countBase = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("ABNORMALITIES_ADDRESS")),
        map_.offsets(QStringLiteral("WIREBUG_COUNT_OFFSETS")), error);
    if (!countBase)
        return;
    const auto countStruct = memory_.read<MHRWirebugCountStructure>(countBase);
    if (!countStruct)
        return;
    const int count = std::max(0, std::min(3,
        countStruct->default_ + countStruct->environment + countStruct->skill));
    if (count <= 0)
        return;

    // Step 2: pointer array at ABNORMALITIES_ADDRESS + WIREBUG_DATA_OFFSETS.
    const std::uintptr_t arrayPtr = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("ABNORMALITIES_ADDRESS")),
        map_.offsets(QStringLiteral("WIREBUG_DATA_OFFSETS")), nullptr);
    if (!arrayPtr)
        return;
    const auto ptrs = memory_.readArray<std::int64_t>(arrayPtr, 3);
    if (ptrs.size() != 3)
        return;

    // Step 3: optional extras pointer (env / skill wirebug timer).
    const std::uintptr_t extrasBase = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("ABNORMALITIES_ADDRESS")),
        map_.offsets(QStringLiteral("WIREBUG_EXTRA_DATA_OFFSETS")), nullptr);

    for (int i = 0; i < count; ++i) {
        const std::uintptr_t wbAddr = static_cast<std::uintptr_t>(ptrs[i]);
        if (!isSanePointer(wbAddr))
            continue;
        const auto wb = memory_.read<MHRWirebugStructure>(wbAddr);
        if (!wb)
            continue;

        WirebugSnapshot snap;
        snap.slot         = i;
        snap.isAvailable  = true;
        // The first slot is always the default; environment / skill are
        // pushed onto slots 1+ only when the count breakdown says so.
        snap.isTemporary  = (i > 0) && (countStruct->environment > 0
                                       || countStruct->skill > 0)
                                       && (i >= countStruct->default_);
        snap.cooldown     = std::isfinite(wb->cooldown)     ? wb->cooldown     : 0.0F;
        snap.maxCooldown  = std::isfinite(wb->maxCooldown)
                            && wb->maxCooldown > 0.0F ? wb->maxCooldown : 30.0F;
        snap.timer        = 0.0F;
        snap.maxTimer     = 0.0F;
        // Env / skill wirebug bonus timer (only on slot >= default count).
        if (snap.isTemporary && extrasBase) {
            const auto extras = memory_.read<MHRWirebugExtrasStructure>(extrasBase);
            if (extras) {
                snap.timer    = std::isfinite(extras->timer) ? extras->timer : 0.0F;
                snap.maxTimer = snap.timer > 0.0F ? snap.timer : 1.0F;
            }
        }
        snapshot.wirebugs.push_back(snap);
    }
}

QuestSnapshot MhrReader::readQuest(QString *error)
{
    QuestSnapshot result;
    const std::uintptr_t questStruct = MhwReader::followPointerChain(
        memory_, absolute(QStringLiteral("QUEST_ADDRESS")),
        map_.offsets(QStringLiteral("QUEST_OFFSETS")), error);
    if (!questStruct)
        return result;

    if (const auto timer = memory_.read<float>(questStruct + 0x170ULL))
        result.timeLeftSeconds = *timer;
    if (const auto status = memory_.read<std::int32_t>(questStruct + 0x110ULL))
        result.state = *status;
    result.active = (result.state != 0);
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
    snapshot.zone = stageInfo.inHuntingZone ? Zone::Unknown : Zone::MainMenu;

    if (stageInfo.inHuntingZone)
        snapshot.monsters = readMonsters(&error);

    snapshot.player = readPlayer(nullptr);

    snapshot.quest = stageInfo.inHuntingZone ? readQuest(nullptr) : QuestSnapshot{};

    snapshot.party = {};
    snapshot.isMultiplayer = false;

    if (!error.isEmpty() && snapshot.monsters.isEmpty())
        snapshot.status += QStringLiteral(" · 部分读取失败: %1").arg(error);
    return snapshot;
}

} // namespace mhw
