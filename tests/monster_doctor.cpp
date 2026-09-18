// SPDX-License-Identifier: Apache-2.0
//
// monster-doctor — one-shot, read-only diagnostic bundle for "the overlay
// shows nothing / says 未连接" reports.
//
// Run it, attach the file it writes, and a maintainer can see: which data
// files were found (and from where), whether a game process was detected,
// what the reader's own verdict is (attach + image base + a real read), which
// Proton build the running game uses, what the kernel says about ptrace, and
// what the overlay itself prints on stderr.
//
// Design rules (all three matter for a tool strangers will run):
//   * read-only: never ptrace-attach, never write into another process, no
//     writes outside the report file, no privileges required;
//   * reproducible: every line prints the raw fact behind it (path, /proc
//     value, errno) so the maintainer can re-derive the verdict;
//   * privacy: the home directory is collapsed to `~`, and the tool
//     deliberately does not collect environment variables, Steam account
//     data, other processes' command lines, or any byte of game memory.
//
// The detection, map-resolution and read paths are the *product's own* code
// (mhw::defaultDataSearchDirs / mhw::resolveDataFile / MhwReader::findGamePid
// / AddressMap / ProcessMemory), so a report cannot disagree with the overlay
// about what the overlay would do.

#include "core/map_paths.h"
#include "mhw_reader.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <sys/utsname.h>
#include <unistd.h>

#include <cstring>
#include <vector>

namespace {

QString g_home;
// --world / --rise: restrict every section to one title (empty = both).
QString g_onlyGame;
QStringList g_report;
QStringList g_summary;

QString redact(const QString &text)
{
    if (g_home.isEmpty())
        return text;
    QString out = text;
    out.replace(g_home, QStringLiteral("~"));
    return out;
}

void say(const QString &line = QString())
{
    g_report << redact(line);
}

// Terminal progress: the report file only appears at the end, so a silent
// 15 s run looks like a hang. Each section announces itself on stdout as it
// starts (the report file itself stays clean).
void progress(const QString &label, int index, int total)
{
    QTextStream out(stdout);
    out << QStringLiteral("[%1/%2] %3 … ").arg(index).arg(total).arg(label);
    out.flush();
}

void progressDone(const QString &detail = QStringLiteral("done"))
{
    QTextStream out(stdout);
    out << detail << '\n';
    out.flush();
}

void head(const QString &title)
{
    say();
    const int pad = static_cast<int>(qMax<qsizetype>(0, 58 - title.size()));
    say(QStringLiteral("== %1 %2").arg(title, QString(pad, u'=')));
}

QString fail(const QString &what, const QString &why)
{
    return QStringLiteral("%1: FAILED (%2)").arg(what, why);
}

// ---------------------------------------------------------------- utilities

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    // Raw bytes: a POSIX-locale text decode drops non-ASCII path bytes (the
    // same trap the reader avoids with /proc/<pid>/maps).
    return QString::fromUtf8(file.readAll());
}

QString firstLine(const QString &text)
{
    return text.section(u'\n', 0, 0).trimmed();
}

QString osReleaseField(const QString &field)
{
    const QString text = readText(QStringLiteral("/etc/os-release"));
    for (const QString &line : text.split(u'\n')) {
        const int eq = line.indexOf(u'=');
        if (eq <= 0 || line.left(eq) != field)
            continue;
        QString value = line.mid(eq + 1).trimmed();
        if (value.startsWith(u'"') && value.endsWith(u'"') && value.size() >= 2)
            value = value.mid(1, value.size() - 2);
        return value;
    }
    return QString();
}

QString procStatus(const QString &pid, const QString &key)
{
    const QString text = readText(QStringLiteral("/proc/%1/status").arg(pid));
    const QRegularExpression re(QStringLiteral("^%1:\\s*(.*)$").arg(QRegularExpression::escape(key)),
                                QRegularExpression::MultilineOption);
    const auto match = re.match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QString envState(const char *name)
{
    const QByteArray value = qgetenv(name);
    return value.isEmpty() ? QStringLiteral("unset") : QStringLiteral("set");
}

QString sha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("<unreadable: %1>").arg(file.errorString());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QStringLiteral("<read error>");
    return QString::fromLatin1(hash.result().toHex());
}

QString errnoText(int code)
{
    return QStringLiteral("%1 (%2)").arg(QString::fromLocal8Bit(std::strerror(code))).arg(code);
}

QString symlinkTarget(const QString &path)
{
    // readlink(2) directly: QFile::symLinkTarget() reports the link *and* its
    // target in some cases, which made the ns/user lines unreadable.
    char buffer[4096] = {};
    const ssize_t length =
        ::readlink(path.toLocal8Bit().constData(), buffer, sizeof(buffer) - 1);
    if (length <= 0)
        return QStringLiteral("<unreadable>");
    return QString::fromLocal8Bit(buffer, static_cast<int>(length));
}

// First "KEY = number" line of the [Addresses] section of a HunterPie map.
QString firstAddressKey(const QString &mapText)
{
    bool inAddresses = false;
    static const QRegularExpression keyRe(QStringLiteral("^([A-Za-z_][A-Za-z0-9_.:]*)\\s*=\\s*[0-9]+"));
    for (const QString &line : mapText.split(u'\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(u'[')) {
            inAddresses = trimmed.compare(QStringLiteral("[Addresses]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!inAddresses)
            continue;
        const auto match = keyRe.match(trimmed);
        if (match.hasMatch())
            return match.captured(1);
    }
    return QString();
}

QStringList runCapture(const QString &program, const QStringList &args, int timeoutMs)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, args);
    if (!process.waitForStarted(2000))
        return {QStringLiteral("<%1 could not start>").arg(program)};
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return {QStringLiteral("<%1 timed out>").arg(program)};
    }
    return QString::fromUtf8(process.readAll()).split(u'\n', Qt::SkipEmptyParts);
}

bool processNamed(const QString &name)
{
    const QStringList entries = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool numeric = false;
        entry.toLongLong(&numeric);
        if (!numeric)
            continue;
        if (firstLine(readText(QStringLiteral("/proc/%1/comm").arg(entry))) == name)
            return true;
    }
    return false;
}

// Extract `"key" { ... }` with brace-depth tracking. Steam's app blocks nest
// sub-blocks ("cloud", "MountedConfig"), so a brace-free regex matches
// nothing — and a real `PROTON_ENABLE_WAYLAND=1 %command%` silently degrades
// to "<none>", which is exactly how this tool shipped its first version.
QString vdfBlock(const QString &text, const QString &key)
{
    const QRegularExpression keyRe(
        QStringLiteral("\"%1\"\\s*[{]").arg(QRegularExpression::escape(key)));
    const auto match = keyRe.match(text);
    if (!match.hasMatch())
        return {};
    const int start = static_cast<int>(match.capturedEnd()) - 1;   // at '{'
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (int i = start; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == u'\\') {
                escaped = true;
            } else if (c == u'"') {
                inString = false;
            }
            continue;
        }
        if (c == u'"') {
            inString = true;
        } else if (c == u'{') {
            ++depth;
        } else if (c == u'}') {
            if (--depth == 0)
                return text.mid(start, i - start + 1);
        }
    }
    return {};
}

// Launch options are the third party in "overlay cannot read the game"
// reports (gamescope / dll overrides change the process tree and the
// container). Only the two app rows are read; the account directory name
// never reaches the report.
struct LaunchOptions {
    QString value;        // the option, "<none>", or empty when unknown
    int accountIndex = 0; // 1-based position of the account dir (never the ID)
    QString note;
};

LaunchOptions launchOptionsFor(const QString &appId)
{
    const QString userdata = QStringLiteral("%1/.steam/root/userdata").arg(g_home);
    const QStringList accounts = QDir(userdata).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (accounts.isEmpty())
        return {QString(), 0, QStringLiteral("<no Steam userdata>")};
    for (int index = 0; index < accounts.size(); ++index) {
        const QString vdf = readText(QStringLiteral("%1/%2/config/localconfig.vdf")
                                         .arg(userdata, accounts.at(index)));
        if (vdf.isEmpty())
            continue;
        const QString block = vdfBlock(vdf, appId);
        if (block.isEmpty())
            continue;   // this account never launched the app
        const auto option = QRegularExpression(QStringLiteral("\"LaunchOptions\"\\s*\"([^\"]*)\""))
                                .match(block);
        const QString value = option.hasMatch() ? option.captured(1).trimmed() : QString();
        return {value.isEmpty() ? QStringLiteral("<none>") : value, index + 1, QString()};
    }
    return {QStringLiteral("<no entry for this appid>"), 0, QString()};
}


bool isAncestorOf(qint64 pid)
{
    const qint64 self = QCoreApplication::applicationPid();
    for (int hop = 0; hop < 64; ++hop) {
        if (pid == self)
            return true;
        const QString stat = readText(QStringLiteral("/proc/%1/stat").arg(pid));
        const int close = stat.lastIndexOf(u')');
        if (close < 0)
            return false;
        const QStringList fields = stat.mid(close + 2).split(u' ', Qt::SkipEmptyParts);
        if (fields.size() < 2)
            return false;
        bool ok = false;
        const qint64 parent = fields.at(1).toLongLong(&ok);
        if (!ok || parent <= 0)
            return false;
        pid = parent;
    }
    return false;
}

struct MapRow {
    QString range;
    QString perms;
    QString offset;
    QString path;

    [[nodiscard]] QString start() const { return range.section(u'-', 0, 0); }
};

std::vector<MapRow> mappingsFor(qint64 pid, const QString &exePatternRaw)
{
    std::vector<MapRow> rows;
    const QString text = readText(QStringLiteral("/proc/%1/maps").arg(pid));
    if (text.isEmpty())
        return rows;
    // Same criterion as the readers: case-insensitive substring match on the
    // mapping path, no assumption about the directory it lives in.
    const QString needle = exePatternRaw.toLower();
    for (const QString &line : text.split(u'\n')) {
        if (!line.toLower().contains(needle))
            continue;
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 6)
            continue;
        // parts: range perms offset dev inode path…
        MapRow row;
        row.range = parts.at(0);
        row.perms = parts.at(1);
        row.offset = parts.at(2);
        row.path = parts.mid(5).join(u' ');
        rows.push_back(row);
    }
    return rows;
}

struct GameTarget {
    QString title;      // "world" / "rise"
    QString exe;        // lowercase image basename
    qint64 pid = -1;
    std::vector<MapRow> rows;
    int matchCount = 0;
    QStringList allPids;
};

void sectionToolAndHost()
{
    struct utsname uts {};
    QString kernel = QStringLiteral("<uname failed>");
    if (::uname(&uts) == 0) {
        kernel = QStringLiteral("%1 %2 %3").arg(QString::fromLocal8Bit(uts.sysname),
                                                QString::fromLocal8Bit(uts.release),
                                                QString::fromLocal8Bit(uts.machine));
    }
    head(QStringLiteral("host"));
    say(QStringLiteral("monster-doctor report — %1")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    say(QStringLiteral("kernel        : %1").arg(kernel));
    say(QStringLiteral("os-release    : %1 %2")
            .arg(osReleaseField(QStringLiteral("PRETTY_NAME")),
                 osReleaseField(QStringLiteral("VERSION_ID"))));
    say(QStringLiteral("session       : XDG_SESSION_TYPE=%1  WAYLAND_DISPLAY=%2  DISPLAY=%3")
            .arg(qEnvironmentVariable("XDG_SESSION_TYPE", QStringLiteral("<unset>")),
                 envState("WAYLAND_DISPLAY"), envState("DISPLAY")));
    say(QStringLiteral("desktop       : %1")
            .arg(qEnvironmentVariable("XDG_CURRENT_DESKTOP", QStringLiteral("<unset>"))));
    say(QStringLiteral("locale        : LANG=%1  LC_ALL=%2")
            .arg(qEnvironmentVariable("LANG", QStringLiteral("<unset>")),
                 qEnvironmentVariable("LC_ALL", QStringLiteral("<unset>"))));
    say(QStringLiteral("yama          : /proc/sys/kernel/yama/ptrace_scope = %1")
            .arg(firstLine(readText(QStringLiteral("/proc/sys/kernel/yama/ptrace_scope")))));
    say(QStringLiteral("this process  : pid=%1 uid=%2 euid=%3 CapEff=%4 (root=%5)")
            .arg(QCoreApplication::applicationPid())
            .arg(::getuid())
            .arg(::geteuid())
            .arg(procStatus(QString::number(QCoreApplication::applicationPid()),
                            QStringLiteral("CapEff")),
                 ::geteuid() == 0 ? QStringLiteral("yes — please rerun as your desktop user")
                                  : QStringLiteral("no")));
}

void sectionBinaries()
{
    head(QStringLiteral("binaries"));
    const QString dir = QCoreApplication::applicationDirPath();
    say(QStringLiteral("app dir       : %1").arg(dir));
    const QStringList names{QStringLiteral("monster-overlay"), QStringLiteral("monster-control"),
                            QStringLiteral("monster-doctor")};
    bool getcapAvailable = !runCapture(QStringLiteral("getcap"), {QStringLiteral("-v")}, 4000).isEmpty();
    if (!getcapAvailable)
        say(QStringLiteral("getcap        : not installed (capabilities shown from /proc only)"));
    for (const QString &name : names) {
        const QString path = dir + u'/' + name;
        const QFileInfo info(path);
        if (!info.exists()) {
            say(QStringLiteral("%1: missing").arg(name));
            continue;
        }
        say(QStringLiteral("%1: %2 bytes, mtime %3")
                .arg(name)
                .arg(info.size())
                .arg(info.lastModified().toString(Qt::ISODate)));
        say(QStringLiteral("  sha256      : %1").arg(sha256(path)));
        if (getcapAvailable) {
            const QStringList caps = runCapture(QStringLiteral("getcap"), {path}, 4000);
            say(QStringLiteral("  getcap      : %1")
                    .arg(caps.join(QStringLiteral("; ")).trimmed().isEmpty()
                             ? QStringLiteral("<none>")
                             : caps.join(QStringLiteral("; "))));
        }
    }
    const QStringList overlayVersion = runCapture(dir + QStringLiteral("/monster-overlay"),
                                                   {QStringLiteral("--version")}, 4000);
    for (const QString &line : overlayVersion) {
        // Qt/Gtk banner noise arrives on the same channel; keep the line that
        // actually names the build.
        if (line.contains(QStringLiteral("monster-overlay")) && !line.contains(QStringLiteral("WARNING"))) {
            say(QStringLiteral("overlay version: %1").arg(line.trimmed()));
            break;
        }
    }
    say(QStringLiteral("note          : caps are lost when the file is replaced; ignored on nosuid mounts"));
}

void sectionDataFiles()
{
    head(QStringLiteral("address maps"));
    say(QStringLiteral("# .map = address table for one game build (name -> RVA/offset)"));
    const QStringList dirs = mhw::defaultDataSearchDirs();
    say(QStringLiteral("this tool's dir : %1").arg(QCoreApplication::applicationDirPath()));
    say(QStringLiteral("search order (the overlay uses this exact list, in this order):"));
    for (int i = 0; i < dirs.size(); ++i) {
        const QDir dir(dirs.at(i));
        const bool exists = dir.exists();
        const bool world = exists && !dir.entryList({QStringLiteral("MonsterHunterWorld.*.map")},
                                                    QDir::Files).isEmpty();
        const bool rise = exists && !dir.entryList({QStringLiteral("MonsterHunterRise.*.map")},
                                                   QDir::Files).isEmpty();
        say(QStringLiteral("  %1. %2  [dir=%3 world=%4 rise=%5]%6")
                .arg(i + 1)
                .arg(dirs.at(i), exists ? QStringLiteral("yes") : QStringLiteral("no"),
                     world ? QStringLiteral("yes") : QStringLiteral("no"),
                     rise ? QStringLiteral("yes") : QStringLiteral("no"),
                     i == 0 ? QStringLiteral("  <- next to this binary (how the release ships)")
                            : QString()));
    }

    say(QStringLiteral("compile-time  : world=%1 rise=%2 (development fallback only)")
            .arg(QString::fromUtf8(MHW_DEFAULT_MAP), QString::fromUtf8(MHR_DEFAULT_MAP)));

    const struct {
        const char *title;
        const char *file;
        const char *fallback;
        const char *key;
    } maps[] = {
        {"world", "MonsterHunterWorld.421810.map", MHW_DEFAULT_MAP, "PLAYER_ADDRESS"},
        {"rise", "MonsterHunterRise.16.0.2.0.map", MHR_DEFAULT_MAP, "STAGE_ADDRESS"},
    };
    for (const auto &entry : maps) {
        if (!g_onlyGame.isEmpty() && QString::fromUtf8(entry.title) != g_onlyGame)
            continue;
        const QString resolved = mhw::resolveDataFile(QString(), QString::fromUtf8(entry.file),
                                                      dirs, QString::fromUtf8(entry.fallback));
        const QFileInfo info(resolved);
        QString source = QStringLiteral("not found in any search dir");
        if (!resolved.isEmpty()) {
            for (int i = 0; i < dirs.size(); ++i) {
                if (resolved.startsWith(dirs.at(i) + u'/')) {
                    source = (i == 0 ? QStringLiteral("search dir 1 (next to this binary)")
                                     : QStringLiteral("search dir %1").arg(i + 1));
                    break;
                }
            }
            if (resolved == QString::fromUtf8(entry.fallback))
                source = QStringLiteral("compile-time fallback (build machine / CTest only)");
        }
        say();
        say(QStringLiteral("%1 map       : %2").arg(QString::fromUtf8(entry.title), resolved));
        say(QStringLiteral("  source      : %1").arg(source));
        say(QStringLiteral("  exists      : %1").arg(info.exists() ? QStringLiteral("yes")
                                                                   : QStringLiteral("NO")));
        if (!info.exists())
            continue;
        say(QStringLiteral("  size        : %1 bytes, mtime %2")
                .arg(info.size())
                .arg(info.lastModified().toString(Qt::ISODate)));
        say(QStringLiteral("  sha256      : %1").arg(sha256(resolved)));
        mhw::AddressMap map;
        QString error;
        const bool ok = map.load(resolved, &error);
        say(QStringLiteral("  AddressMap  : %1")
                .arg(ok ? QStringLiteral("loaded") : fail(QStringLiteral("load"), error)));
        if (ok) {
            // Probe a key the file itself declares — an invented key name
            // would print a scary "missing" for a perfectly good map.
            const QString selfKey = firstAddressKey(readText(resolved));
            if (!selfKey.isEmpty())
                say(QStringLiteral("  key probe   : %1 (first key in file) present=%2")
                        .arg(selfKey, map.hasAddress(selfKey) ? QStringLiteral("yes")
                                                              : QStringLiteral("NO")));
        }
        g_summary << QStringLiteral("%1 map: %2%3")
                         .arg(QString::fromUtf8(entry.title), ok ? QStringLiteral("ok") : QStringLiteral("FAILED"),
                              resolved.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(resolved));
    }
}

std::vector<GameTarget> detectTargets()
{
    std::vector<GameTarget> targets;
    for (const auto &pair : {std::pair<const char *, const char *>{"world", "monsterhunterworld.exe"},
                             {"rise", "monsterhunterrise.exe"}}) {
        GameTarget target;
        target.title = QString::fromLatin1(pair.first);
        target.exe = QString::fromLatin1(pair.second);
        targets.push_back(target);
    }

    QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QStringList pids;
    for (const QString &entry : entries) {
        bool ok = false;
        entry.toLongLong(&ok);
        if (ok)
            pids << entry;
    }
    for (GameTarget &target : targets) {
        for (const QString &pidText : pids) {
            const std::vector<MapRow> rows = mappingsFor(pidText.toLongLong(), target.exe);
            if (rows.empty())
                continue;
            ++target.matchCount;
            target.allPids << pidText;
            if (target.pid < 0) {
                target.pid = pidText.toLongLong();
                target.rows = rows;
            }
        }
    }
    return targets;
}

void sectionGames(const std::vector<GameTarget> &targets)
{
    head(QStringLiteral("game processes"));
    bool any = false;
    for (const GameTarget &target : targets) {
        say(QStringLiteral("[%1] %2 — %3 process(es) mapped this image")
                .arg(target.title, target.exe)
                .arg(target.matchCount));
        if (target.matchCount == 0)
            continue;
        any = true;
        if (target.matchCount > 1)
            say(QStringLiteral("  WARNING: more than one match (pids: %1) — the reader takes the first")
                    .arg(target.allPids.join(u',')));
        const qint64 pid = target.pid;
        QString base;
        for (const MapRow &row : target.rows) {
            if (row.offset == QLatin1String("00000000") && row.perms.contains(u'r')) {
                base = QStringLiteral("0x") + row.start();
                break;
            }
        }
        say(QStringLiteral("  pid         : %1  comm=%2")
                .arg(pid)
                .arg(firstLine(readText(QStringLiteral("/proc/%1/comm").arg(pid)))));
        say(QStringLiteral("  image base  : %1  (rule: first offset==0 readable mapping of this image)")
                .arg(base.isEmpty() ? QStringLiteral("<not found>") : base));
        say(QStringLiteral("  image path  : %1").arg(target.rows.front().path));
        say(QStringLiteral("  mapping rows: %1").arg(target.rows.size()));
        say(QStringLiteral("  uid/nspid   : %1 / %2")
                .arg(procStatus(QString::number(pid), QStringLiteral("Uid")),
                     procStatus(QString::number(pid), QStringLiteral("NSpid"))));
        say(QStringLiteral("  cmdline     : %1")
                .arg(readText(QStringLiteral("/proc/%1/cmdline").arg(pid)).split(QChar(u'\0')).join(u' ').trimmed()));
        say(QStringLiteral("  reader would pick: pid %1 (MhwReader::findGamePid)")
                .arg(mhw::MhwReader::findGamePid(target.exe).value_or(-1)));
        g_summary << QStringLiteral("%1 process: pid %2, base %3").arg(target.title).arg(pid).arg(base);
    }
    if (!any) {
        say(QStringLiteral("no Monster Hunter process running"));
        say(QStringLiteral("(start the game and rerun for the read verdict + Proton fingerprint)"));
        g_summary << QStringLiteral("no game process running");
    }
}

void sectionReadVerdict(const std::vector<GameTarget> &targets)
{
    head(QStringLiteral("read verdict (this is what the overlay needs)"));
    say(QStringLiteral("own ns/user  : %1").arg(symlinkTarget(QStringLiteral("/proc/self/ns/user"))));
    say(QStringLiteral("CapEff(self) : %1")
            .arg(procStatus(QString::number(QCoreApplication::applicationPid()),
                            QStringLiteral("CapEff"))));
    for (const GameTarget &target : targets) {
        if (target.pid < 0)
            continue;
        const qint64 pid = target.pid;
        say();
        say(QStringLiteral("[%1] pid %2").arg(target.title).arg(pid));
        say(QStringLiteral("  target ns/user: %1")
                .arg(symlinkTarget(QStringLiteral("/proc/%1/ns/user").arg(pid))));
        const QString uidMap = readText(QStringLiteral("/proc/%1/uid_map").arg(pid)).replace(u'\n', u' ').trimmed();
        say(QStringLiteral("  uid_map      : %1").arg(uidMap));
        say(QStringLiteral("  descendant of this tool? %1")
                .arg(isAncestorOf(pid) ? QStringLiteral("yes") : QStringLiteral("no")));

        mhw::ProcessMemory memory;
        QString error;
        if (!memory.attach(pid, &error)) {
            say(QStringLiteral("  attach       : %1").arg(fail(QStringLiteral("attach"), error)));
            g_summary << QStringLiteral("%1 read: attach FAILED").arg(target.title);
            continue;
        }
        const std::uintptr_t base = memory.imageBase(&error, target.exe);
        if (base == 0) {
            say(QStringLiteral("  imageBase    : %1")
                    .arg(fail(QStringLiteral("resolve"), error))); 
            memory.detach();
            g_summary << QStringLiteral("%1 read: imageBase FAILED").arg(target.title);
            continue;
        }
        say(QStringLiteral("  imageBase    : 0x%1").arg(base, 0, 16));
        unsigned char probe[8] = {};
        const bool readOk = memory.readBytes(base, probe, sizeof(probe), &error);
        QString head8;
        for (unsigned char byte : probe)
            head8 += QStringLiteral("%1 ").arg(byte, 2, 16, QLatin1Char('0'));
        say(QStringLiteral("  8-byte read  : %1")
                .arg(readOk ? QStringLiteral("OK (%1)").arg(head8.trimmed())
                            : fail(QStringLiteral("read"), error)));
        memory.detach();
        g_summary << QStringLiteral("%1 read: %2")
                         .arg(target.title, readOk ? QStringLiteral("ok") : QStringLiteral("FAILED"));

        // Independent second opinion on the same address, so a report can tell
        // "the reader is wrong" apart from "the kernel denies us".
        QFile mem(QStringLiteral("/proc/%1/mem").arg(pid));
        if (mem.open(QIODevice::ReadOnly)) {
            if (mem.seek(static_cast<qint64>(base))) {
                const QByteArray bytes = mem.read(8);
                say(QStringLiteral("  /proc/pid/mem: %1").arg(bytes.size() == 8
                        ? QStringLiteral("OK (%1 bytes)").arg(bytes.size())
                        : fail(QStringLiteral("pread"), QStringLiteral("short read"))));
            } else {
                say(QStringLiteral("  /proc/pid/mem: seek failed (%1)").arg(mem.errorString()));
            }
        } else {
            say(QStringLiteral("  /proc/pid/mem: %1")
                    .arg(fail(QStringLiteral("open"),
                              errnoText(static_cast<int>(mem.error())))));
        }
        if (!readOk) {
            // The capability belongs on the overlay, not on this report tool.
            say(QStringLiteral("  remedy       : sudo setcap cap_sys_ptrace+ep %1/monster-overlay")
                    .arg(QCoreApplication::applicationDirPath()));
            say(QStringLiteral("                 or temporarily: sudo sysctl kernel.yama.ptrace_scope=0"));
        }
    }
}

void sectionRuntime(const std::vector<GameTarget> &targets)
{
    head(QStringLiteral("game runtime (Proton / container)"));
    for (const GameTarget &target : targets) {
        if (target.pid < 0)
            continue;
        say();
        say(QStringLiteral("[%1] pid %2").arg(target.title).arg(target.pid));
        say(QStringLiteral("  container root: %1")
                .arg(symlinkTarget(QStringLiteral("/proc/%1/root").arg(target.pid))));
        QString tool;
        qint64 hop = target.pid;
        for (int depth = 0; depth < 16 && hop > 1; ++depth) {
            const QString cmdline = readText(QStringLiteral("/proc/%1/cmdline").arg(hop)).split(QChar(u'\0')).join(u' ');
            const QRegularExpression re(QStringLiteral("compatibilitytools\\.d/([^/\\s]+)"));
            const auto match = re.match(cmdline);
            if (match.hasMatch()) {
                tool = match.captured(1);
                break;
            }
            bool ok = false;
            const QString stat = readText(QStringLiteral("/proc/%1/stat").arg(hop));
            const int close = stat.lastIndexOf(u')');
            if (close < 0)
                break;
            const QStringList fields = stat.mid(close + 2).split(u' ', Qt::SkipEmptyParts);
            if (fields.size() < 2)
                break;
            hop = fields.at(1).toLongLong(&ok);
            if (!ok)
                break;
        }
        if (tool.isEmpty()) {
            say(QStringLiteral("  compat tool  : <not found on the parent chain — not a Proton run?>"));
        } else {
            const QString root = QStringLiteral("%1/.steam/root").arg(g_home);
            const QString toolDir = QStringLiteral("%1/compatibilitytools.d/%2").arg(root, tool);
            const QString version = firstLine(readText(toolDir + QStringLiteral("/version")));
            say(QStringLiteral("  compat tool  : %1").arg(tool));
            say(QStringLiteral("  tool version : %1").arg(version.isEmpty() ? QStringLiteral("<none>") : version));
        }
        const QString appId = target.title == QLatin1String("rise") ? QStringLiteral("1446780")
                                                                    : QStringLiteral("582010");
        const QString compatVersion =
            firstLine(readText(QStringLiteral("%1/.steam/root/steamapps/compatdata/%2/version").arg(g_home, appId)));
        if (!compatVersion.isEmpty())
            say(QStringLiteral("  compatdata   : appid %1 → %2").arg(appId, compatVersion));
    }
    const QString configVdf = QStringLiteral("%1/.steam/root/config/config.vdf").arg(g_home);
    const QString vdf = readText(configVdf);
    say();
    say(QStringLiteral("forced compat tool (config.vdf):"));
    for (const QString &appId : {QStringLiteral("582010"), QStringLiteral("1446780")}) {
        if (!g_onlyGame.isEmpty()
            && (appId == QLatin1String("582010")) == (g_onlyGame == QLatin1String("rise")))
            continue;   // --world / --rise narrows this list too
        const QRegularExpression re(
            QStringLiteral("\"%1\"\\s*\\{[^}]*?\"name\"\\s*\"([^\"]*)\"").arg(appId));
        const auto match = re.match(vdf);
        say(QStringLiteral("  %1 : %2").arg(appId, match.hasMatch() ? match.captured(1)
                                                                    : QStringLiteral("<no per-app override>")));
    }
    say(QStringLiteral("steam client  : %1")
            .arg(processNamed(QStringLiteral("steam")) ? QStringLiteral("running")
                                                      : QStringLiteral("not running")));
    say(QStringLiteral("launch options (only these rows are read; the account dir is never printed):"));
    for (const QString &appId : {QStringLiteral("582010"), QStringLiteral("1446780")}) {
        const QString game = appId == QLatin1String("582010") ? QStringLiteral("World")
                                                              : QStringLiteral("Rise");
        if (!g_onlyGame.isEmpty() && game.toLower() != g_onlyGame)
            continue;
        const LaunchOptions option = launchOptionsFor(appId);
        QString suffix = option.note;
        if (suffix.isEmpty() && option.accountIndex > 0)
            suffix = QStringLiteral("   [account #%1]").arg(option.accountIndex);
        say(QStringLiteral("  %1 (%2) : %3%4")
                .arg(appId, game,
                     option.value.isEmpty() ? QStringLiteral("<unknown>") : option.value, suffix));
    }
    const QDir toolsDir(QStringLiteral("%1/.steam/root/compatibilitytools.d").arg(g_home));
    const QStringList tools = toolsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    say(QStringLiteral("installed compatibility tools: %1")
            .arg(tools.isEmpty() ? QStringLiteral("<none>") : tools.join(QStringLiteral(", "))));
}

void sectionOverlayRun(const std::vector<GameTarget> &targets, int seconds, bool enabled)
{
    head(QStringLiteral("overlay run (panels disabled — no window appears)"));
    if (!enabled) {
        say(QStringLiteral("skipped (--no-overlay-run)"));
        return;
    }
    const QString overlay = QCoreApplication::applicationDirPath() + QStringLiteral("/monster-overlay");
    if (!QFileInfo::exists(overlay)) {
        say(QStringLiteral("monster-overlay not found next to this tool (%1)").arg(overlay));
        return;
    }
    QStringList games;
    for (const GameTarget &target : targets) {
        if (target.pid >= 0)
            games << target.title;
    }
    if (games.isEmpty())
        games << (g_onlyGame.isEmpty() ? QStringLiteral("auto") : g_onlyGame);
    for (const QString &game : games) {
        say();
        say(QStringLiteral("[%1] running %2 for %3s …").arg(game, overlay).arg(seconds));
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(overlay, {QStringLiteral("--game"), game, QStringLiteral("--poll"),
                                QStringLiteral("250"), QStringLiteral("--no-player"),
                                QStringLiteral("--no-monster"), QStringLiteral("--no-damage")});
        if (!process.waitForStarted(3000)) {
            say(QStringLiteral("  could not start: %1").arg(process.errorString()));
            continue;
        }
        if (!process.waitForFinished(seconds * 1000)) {
            process.terminate();
            if (!process.waitForFinished(2000))
                process.kill();
            process.waitForFinished(1000);
        }
        const QStringList lines = QString::fromUtf8(process.readAll()).split(u'\n', Qt::SkipEmptyParts);
        QStringList statusLines;
        for (const QString &line : lines) {
            if (line.contains(QStringLiteral("reader status:")))
                statusLines << line.section(QStringLiteral("reader status:"), 1).trimmed();
        }
        if (statusLines.isEmpty()) {
            say(QStringLiteral("  no reader-status line was printed (unexpected — please send this report)"));
            for (const QString &line : lines.mid(0, 10))
                say(QStringLiteral("  raw: %1").arg(line));
        } else {
            for (const QString &line : statusLines)
                say(QStringLiteral("  reader status: %1").arg(line));
            g_summary << QStringLiteral("%1 overlay status: %2").arg(game, statusLines.last());
        }
    }
}

void sectionPrivacy()
{
    head(QStringLiteral("privacy"));
    say(QStringLiteral("read-only: nothing was attached to, written to or changed"));
    say(QStringLiteral("not collected: env vars, Steam account/ID, game memory, other cmdlines, your files"));
    say(QStringLiteral("paths under ~ print as ~"));
}

int parseSeconds(const QStringList &args, int fallback)
{
    const int index = args.indexOf(QStringLiteral("--seconds"));
    if (index >= 0 && index + 1 < args.size()) {
        bool ok = false;
        const int value = args.at(index + 1).toInt(&ok);
        if (ok && value >= 0 && value <= 60)
            return value;
    }
    return fallback;
}

QString parseOut(const QStringList &args)
{
    const int index = args.indexOf(QStringLiteral("--out"));
    if (index >= 0 && index + 1 < args.size())
        return args.at(index + 1);
    return QDir::current().filePath(
        QStringLiteral("monster-doctor-%1.txt")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("monster-doctor"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    const QStringList args = QCoreApplication::arguments();

    if (args.contains(QStringLiteral("--help")) || args.contains(QStringLiteral("-h"))) {
        QTextStream out(stdout);
        out << "monster-doctor — diagnostic report for \"the overlay shows nothing\"\n\n"
            << "Usage: monster-doctor [--world|--rise] [--out FILE] [--seconds N] [--no-overlay-run]\n\n"
            << "  --world|--rise     only inspect one game\n"
            << "  --out FILE         report path (default ./monster-doctor-<timestamp>.txt)\n"
            << "  --seconds N        overlay status run length (default 6)\n"
            << "  --no-overlay-run   skip that step (offline)\n\n"
            << "Read-only, no privileges. Attach the report file to your bug report.\n";
        return 0;
    }
    if (args.contains(QStringLiteral("--version"))) {
        QTextStream(stdout) << "monster-doctor "
                            << QCoreApplication::applicationVersion() << '\n';
        return 0;
    }

    // --world / --rise narrow every section to one title (useful when only one
    // game is installed, or when comparing two Proton configurations).
    if (args.contains(QStringLiteral("--world")) && !args.contains(QStringLiteral("--rise")))
        g_onlyGame = QStringLiteral("world");
    else if (args.contains(QStringLiteral("--rise")) && !args.contains(QStringLiteral("--world")))
        g_onlyGame = QStringLiteral("rise");

    g_home = QDir::homePath();
    const int seconds = parseSeconds(args, 6);
    const bool overlayRun = !args.contains(QStringLiteral("--no-overlay-run"));
    const QString outPath = parseOut(args);

    QTextStream(stdout) << "monster-doctor " << QCoreApplication::applicationVersion()
                        << " — collecting a diagnostic report (read-only, no window, ~15 s)\n";

    int step = 0;
    const int kSteps = 8;
    progress(QStringLiteral("host facts"), ++step, kSteps);
    sectionToolAndHost();
    progressDone();
    progress(QStringLiteral("shipped binaries (hashing)"), ++step, kSteps);
    sectionBinaries();
    progressDone();
    progress(QStringLiteral("address maps"), ++step, kSteps);
    sectionDataFiles();
    progressDone();
    progress(QStringLiteral("game processes"), ++step, kSteps);
    std::vector<GameTarget> targets = detectTargets();
    if (!g_onlyGame.isEmpty()) {
        std::vector<GameTarget> keep;
        for (const GameTarget &target : targets) {
            if (target.title == g_onlyGame)
                keep.push_back(target);
        }
        targets = keep;
    }
    sectionGames(targets);
    progressDone();
    progress(QStringLiteral("read verdict"), ++step, kSteps);
    sectionReadVerdict(targets);
    progressDone();
    progress(QStringLiteral("game runtime / Proton"), ++step, kSteps);
    sectionRuntime(targets);
    progressDone();
    progress(QStringLiteral("overlay run"), ++step, kSteps);
    sectionOverlayRun(targets, seconds, overlayRun);
    progressDone(overlayRun ? QStringLiteral("done") : QStringLiteral("skipped"));
    progress(QStringLiteral("privacy notes"), ++step, kSteps);
    sectionPrivacy();
    progressDone();

    head(QStringLiteral("summary"));
    for (const QString &line : g_summary)
        say(QStringLiteral("  %1").arg(line));

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream(stderr) << "monster-doctor: cannot write " << outPath << ": "
                            << file.errorString() << '\n';
        return 1;
    }
    QTextStream stream(&file);
    for (const QString &line : g_report)
        stream << line << '\n';
    stream.flush();
    file.close();

    QTextStream out(stdout);
    out << "report written to: " << outPath << '\n'
        << "Attach that file to your report (read-only, nothing private).\n";
    return 0;
}
