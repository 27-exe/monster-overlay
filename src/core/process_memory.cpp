// SPDX-License-Identifier: Apache-2.0
// Core offsets and structures are derived from HunterPie/HunterPie (Apache-2.0).

// The bodies below were moved verbatim from mhw_reader.cpp, which used to
// define them as MhwReader static members. Every function is stateless with
// respect to MhwReader: the state is either its parameters or per-instance
// member state (the PID lookup cache lives on the ProcessMemory that owns
// the attach it describes — see ProcessMemory::lookupPid).
#include "core/process_memory.h"

#include "core/string_table.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>
#include <cerrno>
#include <csignal>
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

// ---- PID lookup mechanics (shared by the member and the free lookup) -----
// These are plain functions in the anonymous namespace rather than members
// so both ProcessMemory::lookupPid() and the free findGamePid() can call
// them, and so the state is plainly data (a PidLookupState table) instead
// of hidden in function-local statics that a second caller could clobber.

bool processStillAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
    // kill(pid, 0) is the cheap liveness test: it only asks whether a
    // signal could be delivered, it never delivers one.
    //   ESRCH  -> the process is gone.
    //   EPERM  -> it exists but belongs to another user. For a reader that
    //             is the *good* case (the game is running; we simply cannot
    //             signal it), so it must not be reported as "not running".
    //   EINVAL -> unexpected for a plain 0 signal; treat as alive so a
    //             liveness probe never invents a "game gone".
    errno = 0;
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM || errno == EINVAL;
}

bool processMapsMention(qint64 pid, const QByteArray &exeNameBytes)
{
    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
    // No QIODevice::Text: the maps line for a Wine PE image can carry bytes
    // that QTextStream's decoder mangles, which is how the old imageBase()
    // parsing missed a name containing a literal space. Read raw and compare
    // lowercased.
    if (!maps.open(QIODevice::ReadOnly))
        return false;
    return maps.readAll().toLower().contains(exeNameBytes);
}

// The single /proc walk: first PID whose maps mention exeNameBytes.
std::optional<qint64> scanForPidInProcfs(const QByteArray &exeNameBytes)
{
    QDir proc(QStringLiteral("/proc"));
    const QFileInfoList entries = proc.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        bool numeric = false;
        const qint64 pid = entry.fileName().toLongLong(&numeric);
        if (!numeric || pid <= 0)
            continue;

        // The actual Wine PE process has the game .exe in its map set,
        // but the comm name is set to wine-preloader / wineserver so we cannot
        // rely on /proc/<pid>/comm. Match on /proc/<pid>/maps (any case).
        if (processMapsMention(pid, exeNameBytes))
            return pid;
    }
    return std::nullopt;
}

// The 5 s throttle that keeps a 60 Hz poll loop off a full /proc walk on
// every tick. Returns true when this entry is inside its window and the
// cached answer is still valid.
bool insideScanWindow(const PidLookupState &state)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    return state.lastScanMs != 0 && (nowMs - state.lastScanMs) < 5000;
}

} // namespace

ProcessMemory::~ProcessMemory()
{
    detach();
}

bool ProcessMemory::attach(qint64 pid, QString *error)
{
    // Attaching to a process that is already gone must fail here, not
    // several hundred bytes later. Otherwise ensureAttached() would
    // "successfully" attach a dead PID, report imageBase == 0 and leave the
    // reader attached to nothing — and (the old symptom) detach() from the
    // next poll of a *different* reader.
    if (pid <= 0) {
        if (error)
            *error = errnoMessage(QStringLiteral("attach to PID"), EINVAL);
        return false;
    }
    if (!processStillAlive(pid)) {
        if (error)
            *error = QStringLiteral("attach: PID %1 is not a running process").arg(pid);
        return false;
    }
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
    // The PID lookup cache describes which process this instance wants to
    // talk to. When the attach goes away, so does its answer — and it must
    // go with it, because that is the one moment the reader is certain the
    // target changed. Any other moment, leaving the entry alone lets the
    // next poll reuse a PID that is still alive instead of forcing a
    // rescan (or worse, a "not running" verdict inside the throttle).
    pidLookup_.clear();
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
    // The two precondition failures are separated so a caller can tell
    // "you are not attached" from "the read request itself was bad". They
    // used to share one reader.invalid_read_request string, which made
    // them indistinguishable at the call site: an unattached reader and a
    // zero-length read produced the same text.
    if (!attached()) {
        if (error)
            *error = QStringLiteral("readBytes: not attached");
        return false;
    }
    if (!isSanePointer(address) || destination == nullptr || size == 0) {
        if (error)
            *error = QStringLiteral("readBytes: invalid request — "
                                    "address 0x%1, %2 bytes")
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

// AddressMap: the .map address-table parser. Its declaration has been in
// core/process_memory.h since the header moved into monster-core, but these
// five bodies were left in src/world/world_reader.cpp — so libmonster-core
// had an UNDEFINED mhw::AddressMap::load() reference that only game-world
// could satisfy, and libgame-rise could not link without game-world in the
// link line. The file is a plain text parser (QFile / QTextStream /
// QStringConverter); it names no game and no World type, so it belongs in
// the kernel beside the declaration. Bodies below are moved VERBATIM: same
// statements, same order, same comments (only this explanatory block and the
// blank separators were added).

bool AddressMap::load(const QString &path, QString *error)
{
    addresses_.clear();
    offsets_.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = trMessage("reader.address_table_open_failed").arg(path, file.errorString());
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
                    *error = trMessage("reader.address_table_bad_address").arg(lineNumber);
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
                        *error = trMessage("reader.address_table_bad_chain").arg(lineNumber);
                    return false;
                }
                values.push_back(static_cast<std::uintptr_t>(value));
            }
            offsets_[key] = std::move(values);
        }
    }

    if (addresses_.empty()) {
        if (error)
            *error = trMessage("reader.address_table_no_address");
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

std::optional<qint64> ProcessMemory::lookupPid(const QString &exeName) const
{
    const QByteArray exeNameBytes = exeName.toLower().toUtf8();

    // A PID this instance is already attached to survives every throttle
    // window, provided the process is still alive and still the executable
    // that was asked for. This is the invariant that used to break: with one
    // process-wide entry, a lookup for a different executable evicted this
    // reader's PID and left it inside the 5 s window, which then answered
    // "no game running" for a process that was alive and healthy — the
    // caller detaches on that answer. On the instance nobody else can write
    // this entry.
    //
    // The maps check is what keeps this from answering for the wrong game:
    // a reader attached to one game must not report that PID when asked
    // about a different executable name, which is also how the old C2
    // (v0.7.5) guard stayed attached to a cache hit.
    if (pid_ > 0 && processStillAlive(pid_) && processMapsMention(pid_, exeNameBytes))
        return pid_;

    PidLookupState &state = pidLookup_[exeName.toLower()]; // default-constructs
    if (state.pid > 0 && processStillAlive(state.pid)
        && processMapsMention(state.pid, exeNameBytes))
        return state.pid;
    if (state.pid > 0)
        state.pid = -1; // dead, or a PID the kernel reused for something else

    // The 5 s negative cache is what keeps a poll loop at 60 Hz from
    // walking /proc on every tick. It is only reached for a target this
    // instance has never attached to and that is not live, so a running
    // game can never be throttled into "not running".
    if (insideScanWindow(state))
        return std::nullopt;

    // The C2 (v0.7.5) revalidation the old cache did on every hit is
    // preserved: scanForPidInProcfs only returns a PID whose
    // /proc/<pid>/maps currently names the executable, so a recycled PID
    // inside the window cannot answer for an unrelated process.
    state.lastScanMs = QDateTime::currentMSecsSinceEpoch();
    state.pid = scanForPidInProcfs(exeNameBytes).value_or(-1);
    if (state.pid <= 0)
        return std::nullopt;
    return state.pid;
}

std::optional<qint64> findGamePid(const QString &exeName)
{
    // Free entry point for call sites that hold no ProcessMemory yet (the
    // readers' poll loops, the probe/doctor tools). Its cache is a table
    // keyed by executable name, NOT the single entry it used to be:
    //
    //   The old shape was three statics — cachedPid, lastScanMs and
    //   cachedExeName — so a lookup for one executable *overwrote* the
    //   entry for the other. Two readers (one per game) therefore evicted
    //   each other's PID on every lookup, and the loser saw "not running"
    //   for an attached, live process.
    //
    // Keying by executable name keeps the throttle a lookup needs (a 60 Hz
    // poll loop must not walk /proc on every tick) without giving any
    // lookup the power to invalidate another one. Callers that DO hold the
    // ProcessMemory they are about to attach to should prefer
    // ProcessMemory::lookupPid(), which additionally cannot report their
    // own attached process as missing.
    static QHash<QString, PidLookupState> table;

    const QString key = exeName.toLower();
    const QByteArray exeNameBytes = key.toUtf8();
    PidLookupState &state = table[key];

    if (state.pid > 0 && processStillAlive(state.pid))
        return state.pid;
    if (state.pid > 0)
        state.pid = -1; // target died: forget it before scanning

    if (insideScanWindow(state))
        return std::nullopt;

    state.lastScanMs = QDateTime::currentMSecsSinceEpoch();
    state.pid = scanForPidInProcfs(exeNameBytes).value_or(-1);
    if (state.pid <= 0)
        return std::nullopt;
    return state.pid;
}

std::uintptr_t followPointerChain(const ProcessMemory &memory,
                                  std::uintptr_t address,
                                  const std::vector<std::uintptr_t> &offsets,
                                  QString *error)
{
    for (const std::uintptr_t offset : offsets) {
        // The overflow/out-of-range guard the sibling chain helper already
        // had. Offset values come from a .map table that the user can edit
        // by hand, and the dereference result comes from game memory: if
        // either is garbage, `address + offset` wraps and the next read goes
        // to a low pointer that is sane only because isSanePointer's floor
        // (0x10000) accepts it. One add away from everything is better than
        // a wrapped one.
        if (offset > std::numeric_limits<std::uintptr_t>::max() - address) {
            if (error)
                *error = QStringLiteral("pointer chain offset overflow at 0x%1")
                             .arg(static_cast<qulonglong>(address), 0, 16);
            return 0;
        }
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
