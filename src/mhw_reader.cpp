// SPDX-License-Identifier: Apache-2.0
// Core offsets and structures are derived from HunterPie/HunterPie (Apache-2.0).

#include "mhw_reader.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QFileInfo>
#include <QDateTime>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/uio.h>
#include <unistd.h>

namespace mhw {
namespace {

constexpr std::size_t kPointerSize = sizeof(std::uintptr_t);

bool isSanePointer(std::uintptr_t value)
{
    return value >= 0x10000 && value < 0x0000800000000000ULL;
}

QString errnoMessage(const QString &operation)
{
    return QStringLiteral("%1: %2 (%3)")
        .arg(operation, QString::fromLocal8Bit(std::strerror(errno)))
        .arg(errno);
}

#pragma pack(push, 1)
struct MonsterEnrage {
    std::int64_t reference;
    std::int64_t unknown0;
    std::int32_t unknown1;
    std::int32_t active;
    float buildup;
    float damageDone;
    float unknown3;
    float duration;
    float maxDuration;
};

struct QuestData {
    std::int32_t maxDeaths;
    std::int32_t deaths;
};
#pragma pack(pop)
struct MonsterEnrageSimple { float duration; float maxDuration; };
static_assert(sizeof(MonsterEnrage) >= 40);
static_assert(sizeof(QuestData) == 8);

} // namespace

bool AddressMap::load(const QString &path, QString *error)
{
    addresses_.clear();
    offsets_.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("无法打开地址表 %1: %2").arg(path, file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    qsizetype lineNumber = 0;
    while (!stream.atEnd()) {
        ++lineNumber;
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        const auto comment = line.indexOf('#');
        if (comment >= 0)
            line = line.left(comment).trimmed();

        const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (tokens.size() < 3)
            continue;

        const QString type = tokens[0];
        const std::string key = tokens[1].toStdString();
        if (type == QStringLiteral("Address")) {
            bool ok = false;
            const qulonglong value = tokens[2].toULongLong(&ok, 0);
            if (!ok) {
                if (error)
                    *error = QStringLiteral("地址表第 %1 行不是合法地址").arg(lineNumber);
                return false;
            }
            addresses_[key] = static_cast<std::uintptr_t>(value);
        } else if (type == QStringLiteral("Offset")) {
            QString valueText = tokens.mid(2).join(QStringLiteral(" "));
            std::vector<std::uintptr_t> values;
            for (const QString &part : valueText.split(',', Qt::SkipEmptyParts)) {
                bool ok = false;
                const qulonglong value = part.trimmed().toULongLong(&ok, 0);
                if (!ok) {
                    if (error)
                        *error = QStringLiteral("地址表第 %1 行不是合法偏移链").arg(lineNumber);
                    return false;
                }
                values.push_back(static_cast<std::uintptr_t>(value));
            }
            offsets_[key] = std::move(values);
        }
    }

    if (addresses_.empty()) {
        if (error)
            *error = QStringLiteral("地址表中没有 Address 项");
        return false;
    }
    return true;
}

std::uintptr_t AddressMap::address(const QString &key) const
{
    const auto it = addresses_.find(key.toStdString());
    return it == addresses_.end() ? 0 : it->second;
}

const std::vector<std::uintptr_t> &AddressMap::offsets(const QString &key) const
{
    static const std::vector<std::uintptr_t> empty;
    const auto it = offsets_.find(key.toStdString());
    return it == offsets_.end() ? empty : it->second;
}

bool AddressMap::hasAddress(const QString &key) const
{
    return addresses_.contains(key.toStdString());
}

bool AddressMap::hasOffsets(const QString &key) const
{
    return offsets_.contains(key.toStdString());
}

ProcessMemory::~ProcessMemory()
{
    detach();
}

bool ProcessMemory::attach(qint64 pid, QString *error)
{
    Q_UNUSED(error);
    detach();
    pid_ = pid;
    // Yama ptrace_scope=1 forbids opening another session's /proc/<pid>/mem,
    // but it does NOT forbid process_vm_readv() because that path goes
    // through mm_access, not through ptrace_may_access. Hold no fd and
    // always use process_vm_readv.
    return true;
}

void ProcessMemory::detach()
{
    if (memFd_ >= 0)
        ::close(memFd_);
    memFd_ = -1;
    pid_ = -1;
}

bool ProcessMemory::attached() const
{
    return pid_ > 0;
}

qint64 ProcessMemory::pid() const
{
    return pid_;
}

std::uintptr_t ProcessMemory::imageBase(QString *error) const
{
    if (pid_ <= 0)
        return 0;

    // Use QByteArray rather than QTextStream: when the line contains a
    // non-ASCII path component (e.g. "Monster Hunter World" with a literal
    // space) the QTextStream decoder on a POSIX locale drops bytes and
    // misses the row.
    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid_));
    if (!maps.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 maps: %1").arg(maps.errorString());
        return 0;
    }

    const QByteArray raw = maps.readAll();
    std::uintptr_t fallback = 0;
    const QList<QByteArray> lines = raw.split('\n');
    for (const QByteArray &lineBytes : lines) {
        if (!lineBytes.toLower().contains("monsterhunterworld.exe"))
            continue;
        const QList<QByteArray> fields = lineBytes.split(' ');
        if (fields.size() < 3)
            continue;
        const QList<QByteArray> halves = fields[0].split('-');
        if (halves.size() != 2)
            continue;
        bool okStart = false;
        bool okOffset = false;
        const qulonglong start = halves[0].toULongLong(&okStart, 16);
        const qulonglong offset = fields[2].toULongLong(&okOffset, 16);
        if (!okStart || !okOffset)
            continue;
        if (fallback == 0)
            fallback = static_cast<std::uintptr_t>(start - offset);
        if (offset == 0)
            return static_cast<std::uintptr_t>(start);
    }

    if (fallback == 0 && error)
        *error = QStringLiteral("maps 中没有 MonsterHunterWorld.exe 映射");
    return fallback;
}

bool ProcessMemory::readBytes(std::uintptr_t address, void *destination, std::size_t size, QString *error) const
{
    if (!attached() || !isSanePointer(address) || destination == nullptr || size == 0) {
        if (error)
            *error = QStringLiteral("无效内存读取请求: 0x%1, %2 bytes")
                         .arg(static_cast<qulonglong>(address), 0, 16)
                         .arg(size);
        return false;
    }

    iovec local{destination, size};
    iovec remote{reinterpret_cast<void *>(address), size};
    const ssize_t result = ::process_vm_readv(static_cast<pid_t>(pid_), &local, 1, &remote, 1, 0);
    if (result == static_cast<ssize_t>(size))
        return true;

    if (error)
        *error = errnoMessage(QStringLiteral("process_vm_readv PID %1 @ 0x%2")
                                 .arg(pid_)
                                 .arg(static_cast<qulonglong>(address), 0, 16));
    return false;
}

MhwReader::MhwReader(QString mapPath)
    : mapPath_(std::move(mapPath))
{
    map_.load(mapPath_, &mapError_);
}

const QString &MhwReader::mapPath() const
{
    return mapPath_;
}

std::optional<qint64> MhwReader::findGamePid()
{
    static qint64 cachedPid = -1;
    static qint64 lastScanMs = 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (cachedPid > 0 && (nowMs - lastScanMs) < 5000) {
        // Verify cached PID is still alive
        if (QFile::exists(QStringLiteral("/proc/%1/maps").arg(cachedPid)))
            return cachedPid;
        cachedPid = -1; // stale, fall through to rescan
    }
    lastScanMs = nowMs;

    QDir proc(QStringLiteral("/proc"));
    const QFileInfoList entries = proc.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        bool numeric = false;
        const qint64 pid = entry.fileName().toLongLong(&numeric);
        if (!numeric || pid <= 0)
            continue;

        // The actual Wine PE process has MonsterHunterWorld.exe in its map set,
        // but the comm name is set to wine-preloader / wineserver so we cannot
        // rely on /proc/<pid>/comm. Match on /proc/<pid>/maps (any case) and
        // also on cmdline as a fallback.
        QFile maps(entry.filePath() + QStringLiteral("/maps"));
        if (maps.open(QIODevice::ReadOnly | QIODevice::Text)
            && maps.readAll().toLower().contains("monsterhunterworld.exe")) {
            cachedPid = pid;
            return cachedPid;
        }
    }
    cachedPid = -1;
    return std::nullopt;
}

std::uintptr_t MhwReader::followPointerChain(const ProcessMemory &memory,
                                             std::uintptr_t address,
                                             const std::vector<std::uintptr_t> &offsets,
                                             QString *error)
{
    for (const std::uintptr_t offset : offsets) {
        const auto next = memory.read<std::uintptr_t>(address, error);
        if (!next || !isSanePointer(*next))
            return 0;
        address = *next + offset;
    }
    return address;
}

bool MhwReader::ensureAttached(GameSnapshot &snapshot)
{
    if (!mapError_.isEmpty()) {
        snapshot.status = mapError_;
        return false;
    }

    const auto pid = findGamePid();
    if (!pid) {
        memory_.detach();
        imageBase_ = 0;
        snapshot.status = QStringLiteral("等待 MonsterHunterWorld.exe");
        return false;
    }

    if (!memory_.attached() || memory_.pid() != *pid) {
        QString error;
        if (!memory_.attach(*pid, &error)) {
            snapshot.pid = *pid;
            snapshot.status = QStringLiteral("已发现 PID %1，但 /proc/%1/mem 拒绝读取：%2。ptrace_scope=1 + 同用户非子进程 → 失败，需要 launcher/ptrace_proxy 关系。")
                                  .arg(*pid)
                                  .arg(error);
            return false;
        }
        imageBase_ = memory_.imageBase(&error);
        if (imageBase_ == 0) {
            memory_.detach();
            snapshot.status = error;
            return false;
        }
    }

    snapshot.attached = true;
    snapshot.pid = *pid;
    snapshot.imageBase = imageBase_;
    snapshot.status = QStringLiteral("MHW 已连接 · PID %1 · BASE 0x%2")
                          .arg(*pid)
                          .arg(static_cast<qulonglong>(imageBase_), 0, 16);
    return true;
}

std::uintptr_t MhwReader::absolute(const QString &key) const
{
    return imageBase_ + map_.address(key);
}

QString MhwReader::readUtf8(std::uintptr_t address, std::size_t maxLength) const
{
    std::vector<char> buffer(maxLength + 1, '\0');
    if (!memory_.readBytes(address, buffer.data(), maxLength, nullptr))
        return {};
    const auto end = std::find(buffer.begin(), buffer.end(), '\0');
    return QString::fromUtf8(buffer.data(), static_cast<qsizetype>(std::distance(buffer.begin(), end))).trimmed();
}

QString MhwReader::joinOffsets() const
{
    QStringList items;
    for (const auto v : map_.offsets(QStringLiteral("MONSTER_LIST_OFFSETS")))
        items << QString::number(v, 16);
    return items.join(QLatin1Char(','));
}

QVector<MonsterSnapshot> MhwReader::readMonsters(QString *error)
{
    QVector<MonsterSnapshot> result;

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
        if (fresh.size() == 128 && fresh == cachedArray_) {
            // Array unchanged - check HP from cache for each live slot
            for (const std::uintptr_t comp : cachedArray_) {
                if (comp < 0x10000 || comp >= 0x0000800000000000ULL) continue;
                auto cachedIt = monsterCache_.find(comp);
                if (cachedIt == monsterCache_.end()) continue;
                const std::uintptr_t monster = cachedIt->second.snapshot.address;
                const auto healthPtr = memory_.read<std::uintptr_t>(monster + 0x7670ULL);
                if (!healthPtr || !isSanePointer(*healthPtr)) continue;
                const auto hp = memory_.readArray<float>(*healthPtr + 0x60ULL, 2);
                if (hp.size() != 2) continue;
                MonsterSnapshot m = cachedIt->second.snapshot;
                m.maxHealth = hp[0];
                m.health = hp[1];
                cachedIt->second.maxHP = hp[0];
                result.push_back(m);
            }
            return result;
        }
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

        // Cache hit: name+maxHP unchanged -> reuse, only update curHP
        auto cachedIt = monsterCache_.find(comp);
        if (cachedIt != monsterCache_.end() &&
            cachedIt->second.maxHP == maxHP && !cachedIt->second.snapshot.internalName.isEmpty()) {
            MonsterSnapshot m = cachedIt->second.snapshot;
            m.health = curHP;
            result.push_back(m);
            continue;
        }

        // Cache miss: read name
        char nameBuf[64] = {0};
        const auto nameStruct = memory_.read<std::uintptr_t>(monster + 0x2A0ULL);
        if (nameStruct && *nameStruct >= 0x10000 && *nameStruct < 0x0000800000000000ULL) {
            memory_.readBytes(*nameStruct + 0xCULL, nameBuf, sizeof(nameBuf) - 1, nullptr);
        }
        QString displayName = QString::fromUtf8(nameBuf);
        if (displayName.isEmpty() || displayName.startsWith(QStringLiteral("em\\ems")))
            continue;
        if (displayName.startsWith(QStringLiteral("em\\em")))
            displayName = displayName.mid(5);

        // Capcom's em\* ID mapping changed in the 421810 build; old HunterPie
        // XML maps are stale. Apply our locally-verified table.
        static const QHash<QString, QString> kNameTable = {
            {QStringLiteral("057"), QStringLiteral("雷狼龙")},
            // Add more as the user verifies them in-game.
        };
        const auto it = kNameTable.find(displayName);
        if (it != kNameTable.end())
            displayName = *it;

        MonsterSnapshot m;
        m.address = monster;
        m.internalName = displayName;

        // HunterPie reads the ID at +0x12280. In the 421810 build this
        // returns a different number than the em\* string id (e.g. em057
        // -> id 94). We still record it for debugging but use the em\*
        // string as the source of truth for the display name.
        if (const auto id = memory_.read<std::int32_t>(monster + 0x12280ULL))
            m.id = *id;

        // HP: Monster + 0x7670 -> HealthPtr; HealthPtr + 0x60 -> [maxHP, curHP]
        m.maxHealth = maxHP;
        m.health = curHP;
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

PlayerSnapshot MhwReader::readPlayer(QString *error)
{
    PlayerSnapshot result;
    const std::uintptr_t hud = followPointerChain(memory_, absolute(QStringLiteral("EQUIPMENT_ADDRESS")),
                                                  map_.offsets(QStringLiteral("PLAYER_BASIC_INFORMATION_OFFSETS")), error);
    if (!hud)
        return result;
    const auto maxHealth = memory_.read<float>(hud + 0x60);
    const auto health = memory_.read<float>(hud + 0x64);
    const auto stamina = memory_.read<float>(hud + 0x12C);
    const auto maxStamina = memory_.read<float>(hud + 0x130);
    if (maxHealth && health && stamina && maxStamina) {
        result.maxHealth = *maxHealth;
        result.health = *health;
        result.stamina = *stamina;
        result.maxStamina = *maxStamina;
        result.valid = std::isfinite(result.health) && result.maxHealth > 0.0F && result.maxHealth < 10000.0F;
    }
    return result;
}

Zone MhwReader::readZone(QString *error)
{
    Zone result = Zone::Unknown;
    const std::uintptr_t zoneAddress = followPointerChain(memory_, absolute(QStringLiteral("ZONE_OFFSET")),
                                                         map_.offsets(QStringLiteral("ZoneOffsets")), error);
    if (!zoneAddress)
        return result;
    const auto value = memory_.read<std::int32_t>(zoneAddress, nullptr);
    if (!value)
        return result;
    const int intValue = static_cast<int>(*value);
    if (intValue < 0 || intValue > 1000)
        return result;
    return static_cast<Zone>(intValue);
}

QuestSnapshot MhwReader::readQuest(QString *error)
{
    QuestSnapshot result;
    const std::uintptr_t quest = followPointerChain(memory_, absolute(QStringLiteral("QUEST_DATA_ADDRESS")),
                                                    map_.offsets(QStringLiteral("QUEST_DATA_OFFSETS")), error);
    if (!quest)
        return result;

    if (const auto id = memory_.read<std::int32_t>(quest + 0x4C))
        result.id = *id;
    if (const auto stars = memory_.read<std::int32_t>(quest + 0x50))
        result.stars = *stars;
    if (const auto state = memory_.read<std::int32_t>(quest + 0x54))
        result.state = *state;
    if (const auto category = memory_.read<std::uint8_t>(quest + 0x7C))
        result.category = *category;

    const std::uintptr_t extra = followPointerChain(memory_, absolute(QStringLiteral("QUEST_DATA_ADDRESS")),
                                                    map_.offsets(QStringLiteral("QUEST_EXTRA_DATA_OFFSETS")), nullptr);
    if (extra) {
        if (const auto data = memory_.read<QuestData>(extra)) {
            result.maxDeaths = data->maxDeaths;
            result.deaths = data->deaths;
        }
    }

    const std::uintptr_t timer = followPointerChain(memory_, absolute(QStringLiteral("QUEST_DATA_ADDRESS")),
                                                    map_.offsets(QStringLiteral("QUEST_TIMER_OFFSETS")), nullptr);
    if (timer) {
        if (const auto ticks = memory_.read<std::uint64_t>(timer))
            result.timeLeftSeconds = static_cast<float>(*ticks) / 60.0F;
    }

    result.active = result.id > 0 && result.state == 2;
    return result;
}

QVector<PartyMemberSnapshot> MhwReader::readParty(QString *error)
{
    QVector<PartyMemberSnapshot> result;
    const std::uintptr_t party = followPointerChain(memory_, absolute(QStringLiteral("PARTY_ADDRESS")),
                                                    map_.offsets(QStringLiteral("PARTY_OFFSETS")), error);
    const std::uintptr_t damage = followPointerChain(memory_, absolute(QStringLiteral("DAMAGE_ADDRESS")),
                                                     map_.offsets(QStringLiteral("DAMAGE_OFFSETS")), nullptr);
    if (!party)
        return result;

    // MHWPartyMemberStructure is 0x58 bytes; its first field is the actual member address.
    constexpr std::size_t stride = 0x58;
    for (int index = 0; index < 4; ++index) {
        const auto memberAddress = memory_.read<std::uintptr_t>(party + static_cast<std::uintptr_t>(index) * stride);
        if (!memberAddress || !isSanePointer(*memberAddress))
            continue;
        const QString name = readUtf8(*memberAddress + 0x49, 32);
        if (name.isEmpty())
            continue;

        PartyMemberSnapshot member;
        member.name = name;
        if (const auto rank = memory_.read<std::int16_t>(*memberAddress + 0x70 + 0x2))
            member.masterRank = *rank;
        if (const auto weapon = memory_.read<std::uint8_t>(*memberAddress + 0x7C))
            member.weaponId = *weapon;
        if (damage) {
            if (const auto dealt = memory_.read<std::int32_t>(damage + static_cast<std::uintptr_t>(index) * 0x2A0))
                member.damage = *dealt;
        }
        result.push_back(member);
    }
    return result;
}

GameSnapshot MhwReader::poll()
{
    GameSnapshot snapshot;
    if (!ensureAttached(snapshot))
        return snapshot;

    snapshot.zone = readZone(nullptr);
    static int callCount = 0;
    if (++callCount % 10 == 0)
        qWarning("poll #%d: zone=%d", callCount, static_cast<int>(snapshot.zone));
    QString error;
    if (isHuntingZone(snapshot.zone)) {
        const auto t0 = std::chrono::steady_clock::now();
        snapshot.monsters = readMonsters(&error);
        const auto t1 = std::chrono::steady_clock::now();
        if (callCount % 10 == 0)
            qWarning("poll: monsters=%zu in %lldus", snapshot.monsters.size(),
                     std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    }
    snapshot.player = readPlayer(nullptr);
    snapshot.quest = readQuest(nullptr);
    snapshot.party = readParty(nullptr);
    if (!error.isEmpty() && snapshot.monsters.isEmpty())
        snapshot.status += QStringLiteral(" · 部分读取失败: %1").arg(error);
    return snapshot;
}

} // namespace mhw
