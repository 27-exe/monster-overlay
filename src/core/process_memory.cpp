// SPDX-License-Identifier: Apache-2.0
// Core offsets and structures are derived from HunterPie/HunterPie (Apache-2.0).

// The bodies below were moved verbatim from mhw_reader.cpp, which used to
// define them as MhwReader static members. Every function is stateless with
// respect to MhwReader: the state is either its parameters or a
// function-local static (the PID cache), so only the qualifying prefix was
// rewritten. No condition, order, boundary or comment changed.
#include "core/process_memory.h"

#include "core/string_table.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QRegularExpression>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/uio.h>
#include <unistd.h>

namespace mhw {
namespace {

// i18n status strings. diagnostics are read through the StringTable at the
// point of production (not cached), so a locale flip shows up in the next
// snapshot the overlay pulls — see core/string_table.h / docs/I18N.md.
inline QString trMessage(const QString &key) { return StringTable::instance().tr(key); }

QString errnoMessage(const QString &operation, int errorCode)
{
    return QStringLiteral("%1: %2 (%3)")
        .arg(operation, QString::fromLocal8Bit(std::strerror(errorCode)))
        .arg(errorCode);
}

} // namespace

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
            *error = trMessage("reader.maps_read_failed").arg(maps.errorString());
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
        *error = trMessage("reader.maps_no_mapping").arg(exeName);
    return fallback;
}

bool ProcessMemory::readBytes(std::uintptr_t address, void *destination, std::size_t size, QString *error) const
{
    if (!attached() || !isSanePointer(address) || destination == nullptr || size == 0) {
        if (error)
            *error = trMessage("reader.invalid_read_request")
                         .arg(static_cast<qulonglong>(address), 0, 16)
                         .arg(size);
        return false;
    }

    iovec local{destination, size};
    iovec remote{reinterpret_cast<void *>(address), size};
    const ssize_t result = ::process_vm_readv(static_cast<pid_t>(pid_), &local, 1, &remote, 1, 0);
    // Save errno before any formatting: tr()/QString work can clobber it,
    // and a partial read reports fewer bytes than requested *without*
    // setting errno at all (v0.9.1 audit).
    const int savedErrno = errno;
    if (result == static_cast<ssize_t>(size))
        return true;

    if (error) {
        const QString what = QStringLiteral("process_vm_readv PID %1 @ 0x%2")
                                 .arg(pid_)
                                 .arg(static_cast<qulonglong>(address), 0, 16);
        if (result > 0)
            *error = what + trMessage(QStringLiteral("reader.partial_read_failed"))
                                 .arg(static_cast<qlonglong>(result));
        else
            *error = errnoMessage(what, savedErrno);
    }
    return false;
}


QString selectLoadableMap(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        AddressMap map;
        if (map.load(candidate))
            return candidate;
    }
    return candidates.isEmpty() ? QString() : candidates.first();
}


std::optional<qint64> findGamePid(const QString &exeName)
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


std::uintptr_t followPointerChain(const ProcessMemory &memory,
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

std::uintptr_t followPointerChainOffsetThenDeref(
    const ProcessMemory &memory, std::uintptr_t address,
    const std::vector<std::uintptr_t> &offsets, QString *error)
{
    for (const std::uintptr_t offset : offsets) {
        if (address > std::numeric_limits<std::uintptr_t>::max() - offset) {
            if (error)
                *error = QStringLiteral("pointer chain offset overflow");
            return 0;
        }
        const auto next = memory.read<std::uintptr_t>(address + offset, error);
        if (!next || !isSanePointer(*next))
            return 0;
        address = *next;
    }
    return address;
}


} // namespace mhw
