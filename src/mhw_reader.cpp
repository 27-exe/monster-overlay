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

// Auto-generated from MonsterData.xml + zh-cn.xml (HunterPie v2.14.0.461)
// 72 monsters; each schema entry carries an IsSeverable flag so that
// readMonsters can dispatch to either the normal table (monster+0x1D058+0x40,
// stride 0x1F8) or the severable table (monster+0x1D058+0x1FC8, stride 0x78).
// Without the severable table, multi-player quests display 100% HP on parts
// that the host only populates on the local player's screen.
// PartSchema + kPartSchemas extern are declared in mhw_reader.h so that
// tests can validate the table.
// BEGIN AUTO-GENERATED kPartSchemas
const QHash<int, QVector<PartSchema>> kPartSchemas = {
    {0, {
        { 1, true, "喉咙", "" },
        { 0, true, "尾巴", "1" },
        { 2, false, "头部", "5" },
        { 3, false, "身体", "" },
        { 4, false, "左腿", "3" },
        { 5, false, "右腿", "3" },
        { 6, false, "尾巴", "" },
    }},
    {1, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "2" },
        { 2, false, "身体", "1" },
        { 3, false, "左翼", "1" },
        { 4, false, "右翼", "1" },
        { 5, false, "左腿", "" },
        { 6, false, "右腿", "" },
        { 7, false, "尾巴", "" },
    }},
    {4, {
        { 0, true, "PART_REPEL", "" },
        { 1, false, "头部", "1" },
        { 2, false, "身体", "" },
        { 3, false, "PART_CHEST", "1" },
        { 4, false, "左臂", "" },
        { 5, false, "右臂", "" },
        { 6, false, "左腿", "" },
        { 7, false, "右腿", "" },
        { 8, false, "尾巴", "" },
        { 9, false, "壳", "" },
        { 10, false, "PART_EXHAUST_ORGAN_CENTRAL", "1" },
        { 11, false, "PART_EXHAUST_ORGAN_HEAD", "1" },
        { 12, false, "PART_EXHAUST_ORGAN_CRATER", "1" },
        { 13, false, "PART_EXHAUST_ORGAN_REAR", "1" },
        { 14, false, "PART_WEAK_L_SHELL", "1" },
        { 15, false, "PART_WEAK_R_SHELL", "1" },
    }},
    {7, {
        { 0, false, "头部", "2" },
        { 1, false, "身体", "" },
        { 2, false, "手臂", "1" },
        { 3, false, "腿", "" },
        { 4, false, "尾巴", "" },
        { 5, false, "PART_ABDOMEN", "3" },
    }},
    {9, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "2" },
        { 2, false, "身体", "1" },
        { 3, false, "左翼", "1" },
        { 4, false, "右翼", "1" },
        { 5, false, "左腿", "" },
        { 6, false, "右腿", "" },
        { 7, false, "尾巴", "" },
    }},
    {10, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "2" },
        { 2, false, "身体", "1" },
        { 3, false, "左翼", "1" },
        { 4, false, "右翼", "1" },
        { 5, false, "左腿", "" },
        { 6, false, "右腿", "" },
        { 7, false, "尾巴", "" },
    }},
    {11, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "2" },
        { 2, false, "身体", "1" },
        { 3, false, "左翼", "1" },
        { 4, false, "右翼", "1" },
        { 5, false, "左腿", "" },
        { 6, false, "右腿", "" },
        { 7, false, "尾巴", "" },
    }},
    {12, {
        { 0, true, "角", "1,2" },
        { 1, true, "尾巴", "1" },
        { 2, false, "头部", "" },
        { 3, false, "身体", "1" },
        { 4, false, "左翼", "" },
        { 5, false, "右翼", "" },
        { 6, false, "左腿", "" },
        { 7, false, "右腿", "" },
        { 8, false, "尾巴", "" },
    }},
    {13, {
        { 0, true, "角", "1,2" },
        { 1, true, "尾巴", "1" },
        { 2, false, "头部", "" },
        { 3, false, "身体", "1" },
        { 4, false, "左翼", "" },
        { 5, false, "右翼", "" },
        { 6, false, "左腿", "" },
        { 7, false, "右腿", "" },
        { 8, false, "尾巴", "" },
    }},
    {14, {
        { 0, false, "角", "1" },
        { 1, false, "手臂", "" },
        { 2, false, "腿", "" },
    }},
    {15, {
        { 0, true, "尾巴", "1" },
        { 1, false, "角", "2,3" },
        { 2, false, "头部", "" },
        { 3, false, "身体", "" },
        { 4, false, "左臂", "3" },
        { 5, false, "右臂", "3" },
        { 6, false, "左腿", "" },
        { 7, false, "右腿", "" },
        { 8, false, "尾巴", "" },
    }},
    {16, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "1" },
        { 2, false, "身体", "" },
        { 3, false, "尾巴", "" },
        { 4, false, "PART_L_LIMBS", "" },
        { 5, false, "PART_R_LIMBS", "" },
        { 6, false, "翼", "2" },
    }},
    {17, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "1" },
        { 2, false, "身体", "" },
        { 3, false, "PART_LIMBS", "" },
        { 4, false, "左翼", "2" },
        { 5, false, "右翼", "2" },
        { 6, false, "尾巴", "" },
    }},
    {18, {
        { 0, true, "尾巴", "1" },
        { 1, true, "角", "" },
        { 2, false, "头部", "1" },
        { 3, false, "身体", "" },
        { 4, false, "手臂", "" },
        { 5, false, "腿", "" },
        { 6, false, "翼", "1" },
        { 7, false, "尾巴", "" },
    }},
    {19, {
        { 0, false, "头部", "6" },
        { 1, false, "身体", "2" },
        { 2, false, "PART_ABDOMEN", "" },
        { 3, false, "左腿", "4" },
        { 4, false, "右腿", "4" },
        { 5, false, "尾巴", "4" },
    }},
    {20, {
        { 0, true, "尾巴", "1" },
        { 1, true, "PART_BIG_FLINCH", "" },
        { 2, false, "头部", "3" },
        { 3, false, "身体", "" },
        { 4, false, "PART_CHEST", "5" },
        { 5, false, "臀部", "" },
        { 6, false, "手臂", "" },
        { 7, false, "左腿", "" },
        { 8, false, "右腿", "" },
        { 9, false, "尾巴", "" },
    }},
    {21, {
        { 0, true, "头部", "1" },
        { 1, true, "尾巴", "1" },
        { 2, false, "头部", "" },
        { 3, false, "PART_HEAD_MUD", "1" },
        { 4, false, "身体", "" },
        { 5, false, "PART_BODY_MUD", "1" },
        { 6, false, "手臂", "1" },
        { 7, false, "PART_ARMS_MUD", "1" },
        { 8, false, "左腿", "1" },
        { 9, false, "PART_L_LEG_MUD", "1" },
        { 10, false, "右腿", "1" },
        { 11, false, "PART_R_LEG_MUD", "1" },
        { 12, false, "尾巴", "" },
        { 13, false, "PART_TAIL_MUD", "1" },
    }},
    {22, {
        { 0, true, "尾巴", "1" },
        { 1, false, "PART_JAW", "1" },
        { 2, false, "头部", "" },
        { 3, false, "身体", "1" },
        { 4, false, "手臂", "" },
        { 5, false, "左腿", "" },
        { 6, false, "右腿", "" },
        { 7, false, "尾巴", "1" },
    }},
    {23, {
        { 0, true, "PART_BODY_LEGS", "1" },
        { 1, false, "PART_ANTLERS", "1,2" },
        { 2, false, "身体", "" },
        { 3, false, "腿", "" },
        { 4, false, "手臂", "" },
        { 5, false, "PART_UNKNOWN", "" },
    }},
    {24, {
        { 0, true, "头部", "" },
        { 1, true, "尾巴", "1" },
        { 2, false, "头部", "2" },
        { 3, false, "身体", "1" },
        { 4, false, "左翼", "1" },
        { 5, false, "右翼", "1" },
        { 6, false, "左腿", "" },
        { 7, false, "右腿", "" },
        { 8, false, "尾巴", "" },
    }},
    {25, {
        { 0, true, "角", "2,3" },
        { 1, true, "尾巴", "1" },
        { 2, true, "PART_UNKNOWN", "" },
        { 3, false, "角", "" },
        { 4, false, "头部", "" },
        { 5, false, "身体", "" },
        { 6, false, "左臂", "" },
        { 7, false, "右臂", "" },
        { 8, false, "左腿", "" },
        { 9, false, "右腿", "" },
        { 10, false, "左翼", "" },
        { 11, false, "右翼", "" },
        { 12, false, "尾巴", "" },
    }},
    {26, {
        { 0, true, "尾巴", "1" },
        { 1, true, "PART_UNKNOWN", "" },
        { 2, false, "头部", "2" },
        { 3, false, "颈部", "" },
        { 4, false, "背部", "" },
        { 5, false, "PART_CHEST", "" },
        { 6, false, "左臂", "2" },
        { 7, false, "右臂", "2" },
        { 8, false, "左腿", "" },
        { 9, false, "右腿", "" },
        { 10, false, "翼", "2" },
        { 11, false, "尾巴", "" },
    }},
    {27, {
        { 0, false, "头部", "1" },
        { 1, false, "手臂", "1" },
        { 2, false, "腿", "" },
        { 3, false, "尾巴", "" },
        { 4, false, "PART_ROCK", "" },
    }},
    {28, {
        { 0, false, "头部", "2" },
        { 1, false, "头部", "" },
        { 2, false, "手臂", "" },
        { 3, false, "腿", "" },
        { 4, false, "尾巴", "" },
    }},
    {29, {
        { 0, false, "头部", "4" },
        { 1, false, "身体", "1" },
        { 2, false, "左腿", "2" },
        { 3, false, "右腿", "2" },
        { 4, false, "尾巴", "3" },
        { 5, false, "PART_HEAD_MUD", "1" },
        { 6, false, "PART_TORSO_MUD", "3" },
        { 7, false, "PART_L_LEG_MUD", "2" },
        { 8, false, "PART_R_LEG_MUD", "2" },
        { 9, false, "PART_TAIL_MUD", "1" },
    }},
    {30, {
        { 0, false, "头部", "2" },
        { 1, false, "身体", "" },
        { 2, false, "PART_MANE", "1" },
        { 3, false, "手臂", "1" },
        { 4, false, "腿", "" },
        { 5, false, "尾巴", "4" },
    }},
    {31, {
        { 0, true, "PART_BALLOON", "1" },
        { 1, false, "头部", "" },
        { 2, false, "PART_BALLOON", "" },
        { 3, false, "身体", "1" },
        { 4, false, "左腿", "" },
        { 5, false, "右腿", "" },
        { 6, false, "左翼", "1" },
        { 7, false, "右翼", "1" },
        { 8, false, "尾巴", "1" },
    }},
    {32, {
        { 0, false, "头部", "2" },
        { 1, false, "身体", "1" },
        { 2, false, "左翼", "1" },
        { 3, false, "右翼", "1" },
        { 4, false, "左腿", "" },
        { 5, false, "右腿", "" },
        { 6, false, "尾巴", "1" },
    }},
    {33, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "2" },
        { 2, false, "身体", "" },
        { 3, false, "手臂", "1" },
        { 4, false, "腿", "" },
        { 5, false, "尾巴", "" },
    }},
    {34, {
        { 0, true, "PART_COUNTERATTACK", "" },
        { 1, true, "尾巴", "1" },
        { 2, false, "头部", "1,2" },
        { 3, false, "身体", "" },
        { 4, false, "手臂", "1" },
        { 5, false, "腿", "1" },
        { 6, false, "尾巴", "1" },
    }},
    {35, {
        { 0, true, "尾巴", "1" },
        { 1, true, "PART_UNKNOWN", "" },
        { 2, true, "PART_UNKNOWN", "" },
        { 3, true, "PART_UNKNOWN", "" },
        { 4, true, "PART_UNKNOWN", "" },
        { 5, false, "头部", "" },
        { 6, false, "身体", "" },
        { 7, false, "左腿", "" },
        { 8, false, "右腿", "" },
        { 9, false, "尾巴", "" },
        { 10, false, "PART_JAW", "1,2" },
        { 11, false, "背部", "1,2" },
        { 12, false, "PART_L_BONE", "1,2" },
        { 13, false, "PART_R_BONE", "1,2" },
    }},
    {36, {
        { 0, true, "PART_EMISSIONS", "" },
        { 1, true, "尾巴", "1" },
        { 2, false, "头部", "2" },
        { 3, false, "背部", "" },
        { 4, false, "PART_CHEST", "1" },
        { 5, false, "尾巴", "" },
        { 6, false, "左臂", "1" },
        { 7, false, "右臂", "1" },
        { 8, false, "左腿", "" },
        { 9, false, "右腿", "" },
        { 10, false, "翼", "" },
    }},
    {37, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "5" },
        { 2, false, "身体", "" },
        { 3, false, "手臂", "1" },
        { 4, false, "腿", "" },
        { 5, false, "尾巴", "" },
    }},
    {38, {
        { 1, true, "角", "1" },
        { 0, true, "PART_HORNS_2", "1" },
        { 2, false, "角", "1" },
        { 3, false, "PART_CHEST", "" },
        { 4, false, "身体", "" },
        { 5, false, "PART_L_LIMBS", "" },
        { 6, false, "PART_R_LIMBS", "" },
        { 7, false, "尾巴", "1" },
        { 8, false, "PART_HORNS_GOLD", "1" },
        { 9, false, "PART_MANE_GOLD", "" },
        { 10, false, "PART_L_CHEST_GOLD", "1" },
        { 11, false, "PART_R_CHEST_GOLD", "1" },
        { 12, false, "PART_L_ARM_GOLD", "1" },
        { 13, false, "PART_R_ARM_GOLD", "1" },
        { 14, false, "PART_L_LEG_GOLD", "1" },
        { 15, false, "PART_R_LEG_GOLD", "1" },
        { 16, false, "PART_L_TAIL_GOLD", "1" },
        { 17, false, "PART_R_TAIL_GOLD", "1" },
    }},
    {39, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "2" },
        { 2, false, "身体", "1" },
        { 3, false, "腿", "" },
        { 4, false, "左翼", "1" },
        { 5, false, "右翼", "1" },
        { 6, false, "尾巴", "" },
    }},
    {51, {
        { 0, true, "PART_BODY_LEGS", "1" },
        { 1, false, "PART_ANTLERS", "1,2" },
        { 2, false, "身体", "" },
        { 3, false, "腿", "" },
        { 4, false, "手臂", "" },
        { 5, false, "PART_UNKNOWN", "" },
    }},
    {61, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "1" },
        { 2, false, "身体", "" },
        { 3, false, "左臂", "2" },
        { 4, false, "右臂", "2" },
        { 5, false, "左腿", "" },
        { 6, false, "右腿", "" },
        { 7, false, "尾巴", "" },
    }},
    {62, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "4" },
        { 2, false, "身体", "" },
        { 3, false, "左臂", "1" },
        { 4, false, "右臂", "1" },
        { 5, false, "左腿", "" },
        { 6, false, "右腿", "" },
        { 7, false, "尾巴", "" },
    }},
    {63, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "3" },
        { 2, false, "身体", "" },
        { 3, false, "左臂", "1" },
        { 4, false, "右臂", "1" },
        { 5, false, "左腿", "" },
        { 6, false, "右腿", "" },
        { 7, false, "尾巴", "" },
    }},
    {64, {
        { 0, true, "尾巴", "1" },
        { 1, true, "PART_BIG_FLINCH", "" },
        { 2, true, "PART_COUNTERATTACK", "" },
        { 3, false, "头部", "2" },
        { 4, false, "身体", "" },
        { 5, false, "PART_CHEST", "2" },
        { 6, false, "臀部", "" },
        { 7, false, "手臂", "" },
        { 8, false, "左腿", "" },
        { 9, false, "右腿", "" },
        { 10, false, "尾巴", "" },
    }},
    {65, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "4" },
        { 2, false, "身体", "" },
        { 3, false, "左臂", "2" },
        { 4, false, "右臂", "2" },
        { 5, false, "左腿", "" },
        { 6, false, "右腿", "" },
        { 7, false, "尾巴", "" },
    }},
    {66, {
        { 1, true, "喉咙", "" },
        { 0, true, "尾巴", "1" },
        { 2, false, "头部", "1,2" },
        { 3, false, "PART_FIN", "1" },
        { 4, false, "身体", "" },
        { 5, false, "手臂", "1" },
        { 6, false, "左腿", "" },
        { 7, false, "右腿", "" },
        { 8, false, "尾巴", "1" },
    }},
    {67, {
        { 1, true, "喉咙", "" },
        { 0, true, "尾巴", "1" },
        { 2, true, "PART_UNKNOWN", "" },
        { 3, false, "头部", "1,2" },
        { 4, false, "PART_FIN", "1" },
        { 5, false, "身体", "" },
        { 6, false, "手臂", "1" },
        { 7, false, "左腿", "" },
        { 8, false, "右腿", "" },
        { 9, false, "尾巴", "1" },
    }},
    {68, {
        { 1, true, "喉咙", "" },
        { 0, true, "尾巴", "1" },
        { 2, false, "头部", "5" },
        { 3, false, "身体", "" },
        { 4, false, "左腿", "3" },
        { 5, false, "右腿", "3" },
        { 6, false, "尾巴", "" },
    }},
    {69, {
        { 2, true, "头部", "" },
        { 0, true, "尾巴", "1" },
        { 1, true, "PART_INFLATED_TAIL", "" },
        { 3, false, "头部", "2" },
        { 4, false, "身体", "1" },
        { 5, false, "左翼", "1" },
        { 6, false, "右翼", "1" },
        { 7, false, "左腿", "" },
        { 8, false, "右腿", "" },
        { 9, false, "尾巴", "" },
    }},
    {70, {
        { 0, true, "角", "2,4" },
        { 1, true, "尾巴", "1" },
        { 2, true, "PART_UNKNOWN", "" },
        { 3, true, "PART_SILVER_SPIKES_HEAD", "1" },
        { 4, true, "PART_SILVER_SPIKES_L_ARM", "1" },
        { 5, true, "PART_SILVER_SPIKES_R_ARM", "1" },
        { 6, true, "PART_SILVER_SPIKES_L_WING", "1" },
        { 7, true, "PART_SILVER_SPIKES_R_WING", "1" },
        { 8, false, "角", "" },
        { 9, false, "头部", "" },
        { 10, false, "身体", "" },
        { 11, false, "左臂", "" },
        { 12, false, "右臂", "" },
        { 13, false, "左腿", "" },
        { 14, false, "右腿", "" },
        { 15, false, "左翼", "" },
        { 16, false, "右翼", "" },
        { 17, false, "尾巴", "" },
    }},
    {71, {
        { 0, false, "头部", "2" },
        { 1, false, "身体", "" },
        { 2, false, "PART_MANE", "1" },
        { 3, false, "手臂", "1" },
        { 4, false, "腿", "" },
        { 5, false, "尾巴", "1" },
    }},
    {72, {
        { 0, true, "PART_BALLOON", "1" },
        { 1, false, "头部", "" },
        { 2, false, "PART_BALLOON", "" },
        { 3, false, "身体", "1" },
        { 4, false, "左腿", "" },
        { 5, false, "右腿", "" },
        { 6, false, "左翼", "1" },
        { 7, false, "右翼", "1" },
        { 8, false, "尾巴", "1" },
    }},
    {73, {
        { 0, true, "PART_SKY_FALL", "" },
        { 1, false, "头部", "2" },
        { 2, false, "身体", "1" },
        { 3, false, "左翼", "1" },
        { 4, false, "右翼", "1" },
        { 5, false, "腿", "" },
        { 6, false, "尾巴", "1" },
    }},
    {74, {
        { 0, true, "PART_COUNTERATTACK", "" },
        { 2, true, "头部", "1,2" },
        { 1, true, "尾巴", "1" },
        { 3, false, "头部", "" },
        { 4, false, "身体", "" },
        { 5, false, "手臂", "1" },
        { 6, false, "腿", "1" },
        { 7, false, "尾巴", "1" },
    }},
    {75, {
        { 1, true, "尾巴", "1" },
        { 0, true, "PART_EMISSIONS", "" },
        { 2, false, "头部", "3" },
        { 3, false, "背部", "" },
        { 4, false, "PART_CHEST", "1" },
        { 5, false, "尾巴", "" },
        { 6, false, "左臂", "1" },
        { 7, false, "右臂", "1" },
        { 8, false, "左腿", "" },
        { 9, false, "右腿", "" },
        { 10, false, "翼", "" },
        { 11, false, "PART_UNKNOWN", "" },
        { 12, false, "PART_UNKNOWN", "" },
        { 13, false, "PART_UNKNOWN", "" },
        { 14, false, "PART_UNKNOWN", "" },
        { 15, false, "PART_UNKNOWN", "" },
    }},
    {76, {
        { 1, true, "PART_GLOWING_HEAD", "" },
        { 0, true, "尾巴", "1" },
        { 2, true, "PART_GLOWING_TAIL", "" },
        { 3, false, "头部", "2" },
        { 4, false, "身体", "1" },
        { 5, false, "腿", "" },
        { 6, false, "左翼", "1" },
        { 7, false, "右翼", "1" },
        { 8, false, "尾巴", "" },
    }},
    {77, {
        { 0, true, "PART_EMERGE_SNOW_HEAD", "" },
        { 1, true, "PART_EMERGE_SNOW_BODY", "" },
        { 2, true, "PART_EMERGE_SNOW_TAIL", "" },
        { 3, false, "头部", "2" },
        { 4, false, "PART_HEAD_SNOW", "1" },
        { 5, false, "身体", "1" },
        { 6, false, "PART_BODY_SNOW", "1" },
        { 7, false, "腿", "2" },
        { 8, false, "尾巴", "1" },
        { 9, false, "PART_TAIL_SNOW", "1" },
    }},
    {78, {
        { 0, true, "角", "1,2" },
        { 1, true, "尾巴", "1" },
        { 2, true, "PART_UNKNOWN", "" },
        { 3, false, "角", "" },
        { 4, false, "身体", "" },
        { 5, false, "左腿", "1" },
        { 6, false, "右腿", "1" },
        { 7, false, "尾巴", "" },
    }},
    {79, {
        { 0, true, "尾巴", "1" },
        { 1, true, "PART_SKY_FALL", "" },
        { 2, false, "头部", "2" },
        { 3, false, "PART_HEAD_ICE", "" },
        { 4, false, "身体", "" },
        { 5, false, "PART_BODY_ICE", "" },
        { 6, false, "翼", "2" },
        { 7, false, "PART_WINGS_ICE", "" },
        { 8, false, "手臂", "" },
        { 9, false, "PART_ARMS_ICE", "" },
        { 10, false, "腿", "" },
        { 11, false, "尾巴", "" },
    }},
    {80, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "5" },
        { 2, false, "背部", "" },
        { 3, false, "尾巴", "" },
        { 4, false, "手臂", "2" },
        { 5, false, "腿", "" },
        { 6, false, "翼", "2" },
    }},
    {81, {
        { 0, true, "PART_HEAD_ROCK", "1,2,3,4" },
        { 1, true, "PART_HEAD_ROCK", "1,2,3,4" },
        { 2, true, "PART_CHEST_ROCK", "1,2,3,4,5,6" },
        { 4, true, "PART_UNKNOWN", "" },
        { 3, true, "头部", "1" },
        { 5, false, "PART_L_NECK_ROCK", "" },
        { 6, false, "PART_R_NECK_ROCK", "" },
        { 7, false, "PART_HEAD_ROCK", "" },
        { 8, false, "PART_TAIL_ROCK", "" },
        { 9, false, "PART_L_WING_ROCK", "1" },
        { 10, false, "PART_R_WING_ROCK", "1" },
        { 11, false, "PART_L_ARM_ROCK", "1,2" },
        { 12, false, "PART_R_ARM_ROCK", "1,2" },
        { 13, false, "腿", "" },
        { 14, false, "头部", "1" },
        { 15, false, "身体", "" },
        { 16, false, "左翼", "" },
        { 17, false, "右翼", "" },
        { 18, false, "左臂", "1" },
        { 19, false, "右臂", "1" },
        { 20, false, "尾巴", "" },
    }},
    {87, {
        { 0, true, "角", "1,2" },
        { 1, true, "尾巴", "1" },
        { 3, true, "PART_EXPLOSION_WEAKENING", "3" },
        { 3, false, "头部", "" },
        { 4, false, "PART_CHEST", "" },
        { 5, false, "翼", "2" },
        { 6, false, "手臂", "" },
        { 7, false, "腿", "" },
        { 8, false, "尾巴", "" },
    }},
    {88, {
        { 1, true, "PART_BIG_FLINCH", "" },
        { 0, true, "尾巴", "1" },
        { 2, false, "头部", "2" },
        { 3, false, "身体", "1" },
        { 4, false, "左翼", "1" },
        { 5, false, "右翼", "1" },
        { 6, false, "左腿", "" },
        { 7, false, "右腿", "" },
        { 8, false, "尾巴", "" },
    }},
    {89, {
        { 1, true, "PART_BIG_FLINCH", "" },
        { 0, true, "尾巴", "1" },
        { 2, false, "头部", "2" },
        { 3, false, "身体", "1" },
        { 4, false, "左翼", "1" },
        { 5, false, "右翼", "1" },
        { 6, false, "左腿", "" },
        { 7, false, "右腿", "" },
        { 8, false, "尾巴", "" },
    }},
    {90, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "2,4" },
        { 2, false, "身体", "1" },
        { 3, false, "左腿", "" },
        { 4, false, "右腿", "" },
        { 5, false, "左翼", "1" },
        { 6, false, "右翼", "1" },
        { 7, false, "尾巴", "" },
    }},
    {91, {
        { 0, true, "角", "1" },
        { 2, true, "尾巴", "1" },
        { 1, true, "PART_HORNS_2", "1" },
        { 3, true, "PART_RAGE", "" },
        { 4, false, "头部", "" },
        { 5, false, "身体", "" },
        { 6, false, "左臂", "1" },
        { 7, false, "右臂", "1" },
        { 8, false, "左腿", "" },
        { 9, false, "右腿", "" },
        { 10, false, "尾巴", "" },
    }},
    {92, {
        { 0, true, "角", "1" },
        { 2, true, "尾巴", "" },
        { 1, true, "PART_HORNS_2", "1" },
        { 3, true, "PART_RAGE", "" },
        { 4, false, "头部", "" },
        { 5, false, "身体", "" },
        { 6, false, "左臂", "1" },
        { 7, false, "右臂", "1" },
        { 8, false, "左腿", "" },
        { 9, false, "右腿", "" },
        { 10, false, "尾巴", "" },
    }},
    {93, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "1" },
        { 2, false, "身体", "" },
        { 3, false, "左臂", "2" },
        { 4, false, "右臂", "2" },
        { 5, false, "左腿", "" },
        { 6, false, "右腿", "" },
        { 7, false, "尾巴", "" },
    }},
    {94, {
        { 1, true, "充能角", "" },
        { 0, true, "尾巴", "1" },
        { 2, false, "头部", "2,4" },
        { 3, false, "身体", "" },
        { 4, false, "背部", "1" },
        { 5, false, "手臂", "2" },
        { 6, false, "腿", "" },
        { 7, false, "尾巴", "" },
    }},
    {95, {
        { 1, true, "充能角", "" },
        { 0, true, "尾巴", "1" },
        { 2, false, "头部", "2,4" },
        { 3, false, "身体", "" },
        { 4, false, "背部", "1" },
        { 5, false, "手臂", "3" },
        { 6, false, "腿", "" },
        { 7, false, "尾巴", "" },
    }},
    {96, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "2" },
        { 2, false, "身体", "" },
        { 3, false, "背部", "" },
        { 4, false, "左臂", "2" },
        { 5, false, "右臂", "2" },
        { 6, false, "左腿", "" },
        { 7, false, "右腿", "" },
        { 8, false, "尾巴", "" },
        { 9, false, "PART_TAIL_TIP", "" },
    }},
    {97, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "5,10" },
        { 2, false, "身体", "" },
        { 3, false, "PART_ABDOMEN", "" },
        { 4, false, "背部", "1" },
        { 5, false, "PART_CHEST", "1" },
        { 6, false, "左臂", "7" },
        { 7, false, "右臂", "7" },
        { 8, false, "左腿", "5" },
        { 9, false, "右腿", "5" },
        { 10, false, "左翼", "3" },
        { 11, false, "右翼", "3" },
        { 12, false, "尾巴", "" },
    }},
    {98, {
        { 0, false, "PART_UNKNOWN", "" },
        { 1, false, "PART_UNKNOWN", "" },
        { 2, false, "PART_UNKNOWN", "" },
    }},
    {99, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "2,4" },
        { 2, false, "身体", "1" },
        { 3, false, "左腿", "" },
        { 4, false, "右腿", "" },
        { 5, false, "左翼", "1" },
        { 6, false, "右翼", "1" },
        { 7, false, "尾巴", "" },
    }},
    {100, {
        { 0, true, "尾巴", "1" },
        { 1, false, "头部", "1,2" },
        { 2, false, "身体", "" },
        { 3, false, "左臂", "2" },
        { 4, false, "右臂", "2" },
        { 5, false, "左腿", "" },
        { 6, false, "右腿", "" },
        { 7, false, "尾巴", "" },
    }},
    {101, {
        { 0, true, "PART_KNOCKDOWN", "" },
        { 1, false, "头部", "3,6" },
        { 2, false, "颈部", "" },
        { 3, false, "PART_CHEST", "2" },
        { 4, false, "身体", "" },
        { 5, false, "左臂", "" },
        { 6, false, "右臂", "" },
        { 7, false, "左腿", "" },
        { 8, false, "右腿", "" },
        { 9, false, "左翼", "1" },
        { 10, false, "右翼", "1" },
        { 11, false, "尾巴", "" },
    }},
};
// END AUTO-GENERATED kPartSchemas



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
        bool isEnraged = false;
        // Enrage: MHWMonsterStatusStructure INLINE at monster+0x1BE30
        // +0x14 IsActive, +0x24 Duration, +0x28 MaxDuration
        if (const auto dur = memory_.read<float>(monster + 0x1BE30ULL + 0x24ULL)) {
            enrageDuration = *dur;
            if (const auto maxDur = memory_.read<float>(monster + 0x1BE30ULL + 0x28ULL))
                enrageMaxDuration = *maxDur;
            if (const auto active = memory_.read<int>(monster + 0x1BE30ULL + 0x14ULL))
            isEnraged = (enrageDuration > 0.0F);
        }

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

            // Apply break-threshold scaling to a freshly-read part.
            // HunterPie UpdateBreakableData:
            //   MaxHealth = firstTh * mhp
            //   Health   = (max(0, firstTh - Counter - 1) * mhp) + data.Health
            auto applyBreakable = [&](PartSnapshot &p, const PartSchema &ps, float mhp, float chp) {
                if (!ps.thresholds[0]) return;
                int firstTh = 0;
                const char *t = ps.thresholds;
                while (*t >= '0' && *t <= '9') { firstTh = firstTh * 10 + (*t - '0'); ++t; }
                if (firstTh <= 0) return;
                p.firstThreshold = firstTh;
                if (p.counter < firstTh) {
                    p.maxHealth = firstTh * mhp;
                    p.health = (firstTh - p.counter - 1) * mhp + chp;
                    if (p.health > p.maxHealth) p.health = p.maxHealth;
                } else {
                    // Already broken past — show actual layer
                    p.maxHealth = mhp;
                    p.health = chp;
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
                            QString thSuffix;
                            if (p.firstThreshold > 0)
                                thSuffix = QStringLiteral(" (%1/%2破)").arg(p.counter).arg(p.firstThreshold);
                            const QString pname = QString::fromUtf8(ps.name) + thSuffix;
                            // pname already encodes the threshold suffix
                            // for this tick, so we accept it even when a
                            // cached name exists. Otherwise we'd show a
                            // stale "0/2破" for a part whose counter just
                            // ticked up.
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
                    // HunterPie's IsBroken:
                    //   MaxHealth <= 0
                    // || (Health == MaxHealth && (Breaks > 0 || Flinch != MaxFlinch))
                    // In solo on the local client Health == MaxHealth is the
                    // steady state for an unhit part, so "Breaks > 0" is the
                    // meaningful test. The Flinch != MaxFlinch arm is for
                    // parts still receiving flinch damage after a break.
                    p.isBroken = p.maxHealth <= 0.0F
                              || (p.counter > 0);
                    QString thSuffix;
                    if (p.firstThreshold > 0)
                        thSuffix = QStringLiteral(" (%1/%2破)").arg(p.counter).arg(p.firstThreshold);
                    const QString pname = QString::fromUtf8(ps.name) + thSuffix;
                    // Always use the freshly-computed pname (see severable
                    // branch comment for why cached names were stale).
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
        m.enraged = isEnraged;
        m.enrageSeconds = enrageDuration;
        m.enrageMaxSeconds = enrageMaxDuration;
        m.parts = parts;
            static int dc=0; if(++dc % 10 == 1) std::fprintf(stderr,"[multi] #%d parts=%zu hp=%d p0=%.0f/%.0f\n", dc, parts.size(), (int)curHP, parts.isEmpty()?0.f:parts[0].health, parts.isEmpty()?0.f:parts[0].maxHealth);
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
    static Zone lastZone = Zone::Unknown;
    if (snapshot.zone != lastZone) {
        cachedArray_.clear();
        monsterCache_.clear();
        cachedArrayBase_ = 0;
        lastZone = snapshot.zone;
    }
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
    snapshot.isMultiplayer = (snapshot.party.size() > 1);
    if (!error.isEmpty() && snapshot.monsters.isEmpty())
        snapshot.status += QStringLiteral(" · 部分读取失败: %1").arg(error);
    return snapshot;
}

} // namespace mhw
