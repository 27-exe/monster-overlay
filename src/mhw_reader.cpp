// SPDX-License-Identifier: Apache-2.0
// Core offsets and structures are derived from HunterPie/HunterPie (Apache-2.0).

#include "mhw_reader.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextStream>

#include <array>
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
#pragma pack(pop)
struct MonsterEnrageSimple { float duration; float maxDuration; };
static_assert(sizeof(MonsterEnrage) >= 40);

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

std::uintptr_t ProcessMemory::imageBase(QString *error, const QString &exeName) const
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

    const QByteArray exeNameBytes = exeName.toLower().toUtf8();
    const QByteArray raw = maps.readAll();
    std::uintptr_t fallback = 0;
    const QList<QByteArray> lines = raw.split('\n');
    for (const QByteArray &lineBytes : lines) {
        if (!lineBytes.toLower().contains(exeNameBytes))
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
        *error = QStringLiteral("maps 中没有 %1 映射").arg(exeName);
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

MhwReader::MhwReader(QString mapPath, QString exeName)
    : mapPath_(std::move(mapPath)), exeName_(std::move(exeName))
{
    map_.load(mapPath_, &mapError_);
}

const QString &MhwReader::mapPath() const
{
    return mapPath_;
}

std::optional<qint64> MhwReader::findGamePid(const QString &exeName)
{
    static qint64 cachedPid = -1;
    static qint64 lastScanMs = 0;
    static QString cachedExeName;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // The cache is keyed by exeName: switching between World and Rise must
    // invalidate a previously cached PID for the other game.
    if (cachedExeName != exeName) {
        cachedPid = -1;
        lastScanMs = 0;
        cachedExeName = exeName;
    }

    const QByteArray exeNameBytes = exeName.toLower().toUtf8();

    // Cache hit (found): skip rescan for 5 s.
    // C2 (v0.7.5 audit): the old check only verified that
    // /proc/<pid>/maps EXISTS. If the game exited and the kernel reused
    // the PID within the 5 s cache window, we would keep reading an
    // unrelated process with the stale imageBase_ — silently wrong
    // values instead of a clean "not attached". Re-verify that the
    // cached PID's maps still contain the game exe; a single maps read
    // is cheap compared to the full /proc scan we're skipping.
    if (cachedPid > 0 && (nowMs - lastScanMs) < 5000) {
        QFile maps(QStringLiteral("/proc/%1/maps").arg(cachedPid));
        if (maps.open(QIODevice::ReadOnly | QIODevice::Text)
            && maps.readAll().toLower().contains(exeNameBytes))
            return cachedPid;
        cachedPid = -1;  // stale PID: fall through to a full rescan
    }
    // Cache hit (not found): skip rescan for 5 s as well.
    // Without this, a poll loop in edit mode fires 60 times/sec
    // and every call walks /proc → 100 % CPU.
    if (cachedPid <= 0 && (nowMs - lastScanMs) < 5000)
        return std::nullopt;

    lastScanMs = nowMs;

    QDir proc(QStringLiteral("/proc"));
    const QFileInfoList entries = proc.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        bool numeric = false;
        const qint64 pid = entry.fileName().toLongLong(&numeric);
        if (!numeric || pid <= 0)
            continue;

        // The actual Wine PE process has the game .exe in its map set,
        // but the comm name is set to wine-preloader / wineserver so we cannot
        // rely on /proc/<pid>/comm. Match on /proc/<pid>/maps (any case) and
        // also on cmdline as a fallback.
        QFile maps(entry.filePath() + QStringLiteral("/maps"));
        if (maps.open(QIODevice::ReadOnly | QIODevice::Text)
            && maps.readAll().toLower().contains(exeNameBytes)) {
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

    const auto pid = findGamePid(exeName_);
    if (!pid) {
        memory_.detach();
        imageBase_ = 0;
        snapshot.status = QStringLiteral("等待 %1").arg(exeName_);
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
        imageBase_ = memory_.imageBase(&error, exeName_);
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

GameSnapshot MhwReader::poll()
{
    GameSnapshot snapshot;
    if (!ensureAttached(snapshot))
        return snapshot;

    // v0.7.5: bump the per-instance poll counter so readPlayer()'s
    // mantle cache can compute ages in kMantleCacheTtl increments.
    ++pollTick_;
    snapshot.zone = readZone(nullptr);
    static Zone lastZone = Zone::Unknown;
    if (snapshot.zone != lastZone) {
        cachedArray_.clear();
        monsterCache_.clear();
        cachedArrayBase_ = 0;
        lastZone = snapshot.zone;
    }
    static int callCount = 0;
    if (++callCount % 10 == 0)
        qDebug("poll #%d: zone=%d", callCount, static_cast<int>(snapshot.zone));
    QString error;
    if (isHuntingZone(snapshot.zone)) {
        const auto t0 = std::chrono::steady_clock::now();
        snapshot.monsters = readMonsters(&error);
        const auto t1 = std::chrono::steady_clock::now();
        if (callCount % 10 == 0)
            qDebug("poll: monsters=%lld in %lldus",
                     static_cast<long long>(snapshot.monsters.size()),
                     static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()));
    }
    snapshot.player = readPlayer(nullptr);
    // HunterPie: persistent identity (name, MR) comes from the save
    // header, which is valid whenever the player is logged in (zone !=
    // MainMenu). readPlayer() handles HP/ST/mantle; this fills the
    // identity fields that player_reader no longer puts in PlayerSnapshot.
    refreshPlayerIdentity(snapshot.player);
    // Sharpness — only emitted for melee weapons (0..10). Ranged
    // weapons (bow/hbg/lbg) keep valid=false so the panel can hide
    // the bar entirely.
    snapshot.player.sharpness = readSharpness(snapshot.player.weaponId, nullptr);
    snapshot.quest = readQuest(nullptr);
    // Clear stale quest data when we're in a non-hunting zone (e.g.
    // gathering hub). The quest struct in memory can retain the
    // previous quest's state/id/maxDeaths after returning to town.
    if (!isHuntingZone(snapshot.zone))
        snapshot.quest = {};

    // readParty always probes 4 slots; stale names in memory linger
    // after leaving a quest, which would keep the damage panel
    // pinned to the now-meaningless last-quest data. Force-clear
    // party whenever we're not in a hunting zone (where party
    // damage is actually meaningful).
    snapshot.party = isHuntingZone(snapshot.zone) ? readParty(nullptr)
                                                   : QVector<PartyMemberSnapshot>{};
    snapshot.isMultiplayer = (snapshot.party.size() > 1);
    if (!error.isEmpty() && snapshot.monsters.isEmpty())
        snapshot.status += QStringLiteral(" · 部分读取失败: %1").arg(error);
    return snapshot;
}

} // namespace mhw
