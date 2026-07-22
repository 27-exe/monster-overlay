// SPDX-License-Identifier: Apache-2.0
// Core offsets and structures are derived from HunterPie/HunterPie (Apache-2.0).

#include "mhw_reader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>
#include <cerrno>
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

    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid_));
    if (!maps.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("无法读取 maps: %1").arg(maps.errorString());
        return 0;
    }

    QTextStream stream(&maps);
    std::uintptr_t fallback = 0;
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (!line.contains(QStringLiteral("MonsterHunterWorld.exe"), Qt::CaseInsensitive))
            continue;
        const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (fields.size() < 3)
            continue;
        bool okStart = false;
        bool okOffset = false;
        const QStringList range = fields[0].split('-');
        if (range.size() != 2)
            continue;
        const qulonglong start = range[0].toULongLong(&okStart, 16);
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
    QDir proc(QStringLiteral("/proc"));
    const QFileInfoList entries = proc.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::optional<qint64> fallback;
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
            && maps.readAll().toLower().contains("monsterhunterworld.exe"))
            return pid;

        QFile comm(entry.filePath() + QStringLiteral("/comm"));
        if (comm.open(QIODevice::ReadOnly)) {
            const QByteArray raw = comm.readAll().trimmed().toLower();
            if (raw.contains("monsterhunterw"))
                fallback = pid;
        }

        if (!fallback) {
            QFile cmdline(entry.filePath() + QStringLiteral("/cmdline"));
            if (cmdline.open(QIODevice::ReadOnly)) {
                const QByteArray raw = cmdline.readAll().toLower();
                if (raw.contains("monsterhunterworld.exe"))
                    fallback = pid;
            }
        }
    }
    return fallback;
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

QVector<MonsterSnapshot> MhwReader::readMonsters(QString *error)
{
    QVector<MonsterSnapshot> result;
    const std::uintptr_t components = followPointerChain(memory_, absolute(QStringLiteral("MONSTER_LIST_ADDRESS")),
                                                         map_.offsets(QStringLiteral("MONSTER_LIST_OFFSETS")), error);
    if (!components)
        return result;

    const auto componentPointers = memory_.readArray<std::uintptr_t>(components, 128, error);
    for (const std::uintptr_t component : componentPointers) {
        if (!isSanePointer(component))
            continue;
        const auto monsterAddress = memory_.read<std::uintptr_t>(component + 0x138, nullptr);
        if (!monsterAddress || !isSanePointer(*monsterAddress))
            continue;
        const QString internalName = readUtf8(*monsterAddress + 0x2A0, 64);
        if (!internalName.startsWith(QStringLiteral("em\\em")) || internalName.startsWith(QStringLiteral("em\\ems")))
            continue;

        MonsterSnapshot monster;
        monster.address = *monsterAddress;
        monster.internalName = internalName;
        if (const auto id = memory_.read<std::int32_t>(*monsterAddress + 0x12280))
            monster.id = *id;
        if (const auto healthPtr = memory_.read<std::uintptr_t>(*monsterAddress + 0x7670);
            healthPtr && isSanePointer(*healthPtr)) {
            const auto health = memory_.readArray<float>(*healthPtr + 0x60, 2);
            if (health.size() == 2) {
                monster.maxHealth = health[0];
                monster.health = health[1];
            }
        }
        const auto stamina = memory_.readArray<float>(*monsterAddress + 0x1C0F0, 2);
        if (stamina.size() == 2) {
            monster.stamina = stamina[0];
            monster.maxStamina = stamina[1];
        }
        if (const auto enrage = memory_.read<MonsterEnrage>(*monsterAddress + 0x1BE30)) {
            monster.enrageSeconds = enrage->duration;
            monster.enrageMaxSeconds = enrage->maxDuration;
            monster.enraged = enrage->duration > 0.0F;
        }
        result.push_back(monster);
    }
    return result;
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
    QString error;
    snapshot.monsters = isHuntingZone(snapshot.zone) ? readMonsters(&error) : QVector<MonsterSnapshot>{};
    snapshot.player = readPlayer(nullptr);
    snapshot.quest = readQuest(nullptr);
    snapshot.party = readParty(nullptr);
    if (!error.isEmpty() && snapshot.monsters.isEmpty())
        snapshot.status += QStringLiteral(" · 部分读取失败: %1").arg(error);
    return snapshot;
}

} // namespace mhw
