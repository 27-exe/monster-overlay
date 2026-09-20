// SPDX-License-Identifier: Apache-2.0

#include "core/reframework_fetcher.h"

#include "core/rise_reframework_manager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>

#include <chrono>
#include <utility>

namespace mhw {
namespace {

constexpr qsizetype kMaximumToolOutput = 16 * 1024 * 1024;

struct ProcessResult {
    bool ok{false};
    bool cancelled{false};
    QByteArray standardOutput;
    QByteArray standardError;
    QString detail;
};

QString toolErrorText(const QString &program, const QString &reason,
                      const QByteArray &standardError = {})
{
    QString detail = QStringLiteral("%1: %2").arg(program, reason);
    const QString stderrText = QString::fromLocal8Bit(standardError).trimmed();
    if (!stderrText.isEmpty())
        detail += QStringLiteral(" (%1)").arg(stderrText.left(1000));
    return detail;
}

ProcessResult runProcess(const QString &program, const QStringList &arguments,
                         int timeoutMs,
                         const ReFrameworkFetcher::CancelCheck &cancelled)
{
    ProcessResult result;
    if (program.trimmed().isEmpty()) {
        result.detail = QStringLiteral("Required executable path is empty.");
        return result;
    }

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    process.start();

    QElapsedTimer timer;
    timer.start();
    bool started = false;
    while (!started) {
        started = process.waitForStarted(25);
        if (started)
            break;
        if (cancelled && cancelled()) {
            process.kill();
            process.waitForFinished(1000);
            result.cancelled = true;
            result.detail = QStringLiteral("Operation cancelled while starting %1.").arg(program);
            return result;
        }
        if (process.state() == QProcess::NotRunning) {
            result.detail = toolErrorText(
                program, QStringLiteral("could not start: %1").arg(process.errorString()));
            return result;
        }
        if (timer.elapsed() >= 5000) {
            process.kill();
            process.waitForFinished(1000);
            result.detail = toolErrorText(program, QStringLiteral("start timed out"));
            return result;
        }
    }

    timer.restart();
    while (process.state() != QProcess::NotRunning) {
        if (cancelled && cancelled()) {
            process.kill();
            process.waitForFinished(1000);
            result.standardOutput += process.readAllStandardOutput();
            result.standardError += process.readAllStandardError();
            result.cancelled = true;
            result.detail = QStringLiteral("Operation cancelled; killed %1.").arg(program);
            return result;
        }

        process.waitForFinished(25);
        result.standardOutput += process.readAllStandardOutput();
        result.standardError += process.readAllStandardError();
        if (result.standardOutput.size() + result.standardError.size() > kMaximumToolOutput) {
            process.kill();
            process.waitForFinished(1000);
            result.detail = toolErrorText(program, QStringLiteral("produced excessive output"));
            return result;
        }
        if (timeoutMs > 0 && timer.elapsed() >= timeoutMs) {
            process.kill();
            process.waitForFinished(1000);
            result.detail = toolErrorText(program, QStringLiteral("timed out"),
                                          result.standardError);
            return result;
        }
    }

    result.standardOutput += process.readAllStandardOutput();
    result.standardError += process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.detail = toolErrorText(
            program,
            process.exitStatus() == QProcess::CrashExit
                ? QStringLiteral("crashed")
                : QStringLiteral("exited with code %1").arg(process.exitCode()),
            result.standardError);
        return result;
    }

    result.ok = true;
    return result;
}

QByteArray sha256(const QString &path, bool *ok, QString *detail)
{
    *ok = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *detail = QStringLiteral("Cannot open archive for SHA-256: %1").arg(file.errorString());
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (true) {
        const qint64 count = file.read(buffer.data(), buffer.size());
        if (count < 0) {
            *detail = QStringLiteral("Cannot read archive for SHA-256: %1").arg(file.errorString());
            return {};
        }
        if (count == 0)
            break;
        hash.addData(QByteArrayView(buffer.constData(), count));
    }
    *ok = true;
    return hash.result().toHex();
}

bool verifyArchive(const QString &path, const ReFrameworkFetcher::Options &options,
                   QString *detail)
{
    const bool customSize = options.expectedArchiveSize >= 0;
    const bool customHash = !options.expectedArchiveSha256.isEmpty();
    const QFileInfo pathInfo(path);
    if (pathInfo.isSymLink()) {
        *detail = QStringLiteral("Archive must be a regular file, not a symbolic link.");
        return false;
    }
    if (!customSize && !customHash)
        return RiseReFrameworkManager::verifyArchive(path, detail);
    if (customSize != customHash) {
        *detail = QStringLiteral("Archive expectation must provide both size and SHA-256.");
        return false;
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.isSymLink()) {
        *detail = QStringLiteral("Archive does not exist or is not a regular file.");
        return false;
    }
    if (info.size() != options.expectedArchiveSize) {
        *detail = QStringLiteral("Archive size mismatch: expected %1 bytes, got %2 bytes.")
                      .arg(options.expectedArchiveSize)
                      .arg(info.size());
        return false;
    }

    bool hashOk = false;
    const QByteArray actual = sha256(path, &hashOk, detail);
    if (!hashOk)
        return false;
    const QByteArray expected = options.expectedArchiveSha256.toLower();
    if (actual != expected) {
        *detail = QStringLiteral("Archive SHA-256 mismatch: expected %1, got %2.")
                      .arg(QString::fromLatin1(expected), QString::fromLatin1(actual));
        return false;
    }
    *detail = QStringLiteral("Archive size and SHA-256 match the configured identity.");
    return true;
}

QList<QByteArray> outputLines(const QByteArray &output, QString *detail)
{
    if (output.contains('\0')) {
        *detail = QStringLiteral("Archive listing contains a NUL byte.");
        return {};
    }
    QList<QByteArray> lines = output.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    for (QByteArray &line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty()) {
            *detail = QStringLiteral("Archive listing contains an empty entry.");
            return {};
        }
    }
    return lines;
}

bool safeRelativeArchivePath(QByteArray path, QString *detail,
                            QByteArray *normalizedPath = nullptr)
{
    while (path.endsWith('/'))
        path.chop(1);
    if (path.isEmpty() || path.startsWith('/') || path.startsWith('\\')
        || (path.size() >= 2
            && ((path.at(0) >= 'a' && path.at(0) <= 'z')
                || (path.at(0) >= 'A' && path.at(0) <= 'Z'))
            && path.at(1) == ':')
        || path.contains('\\')) {
        *detail = QStringLiteral("Archive contains an absolute or non-portable path: %1")
                      .arg(QString::fromLocal8Bit(path));
        return false;
    }

    QByteArray normalized;
    const QList<QByteArray> components = path.split('/');
    for (const QByteArray &component : components) {
        for (const char byte : component) {
            const unsigned char value = static_cast<unsigned char>(byte);
            if (value < 0x20 || value == 0x7f || byte == ':') {
                *detail = QStringLiteral("Archive path contains a control character or colon.");
                return false;
            }
        }
        if (component.isEmpty()) {
            *detail = QStringLiteral("Archive contains an unsafe relative path: %1")
                          .arg(QString::fromLocal8Bit(path));
            return false;
        }
        if (component == "..") {
            *detail = QStringLiteral("Archive contains an unsafe relative path: %1")
                          .arg(QString::fromLocal8Bit(path));
            return false;
        }
        if (component == ".")
            continue;
        if (!normalized.isEmpty())
            normalized += '/';
        normalized += component;
    }
    // A root-directory entry such as ./ is safe and is ignored by the
    // duplicate/path checks; it cannot escape the staging directory.
    if (normalizedPath)
        *normalizedPath = normalized;
    return true;
}

bool inspectArchive(const QString &bsdtar, const QString &archive, int timeoutMs,
                    const ReFrameworkFetcher::CancelCheck &cancelled,
                    bool *wasCancelled, QString *detail)
{
    const ProcessResult names = runProcess(bsdtar,
                                           {QStringLiteral("-tf"), archive},
                                           timeoutMs, cancelled);
    if (!names.ok) {
        *wasCancelled = names.cancelled;
        *detail = names.detail;
        return false;
    }
    QString parseError;
    const QList<QByteArray> paths = outputLines(names.standardOutput, &parseError);
    if (paths.isEmpty()) {
        *detail = parseError.isEmpty() ? QStringLiteral("Archive has no entries.") : parseError;
        return false;
    }

    QSet<QByteArray> seen;
    for (const QByteArray &path : paths) {
        QByteArray normalized;
        if (!safeRelativeArchivePath(path, detail, &normalized))
            return false;
        if (seen.contains(normalized)) {
            *detail = QStringLiteral("Archive contains duplicate entry: %1")
                          .arg(QString::fromLocal8Bit(normalized));
            return false;
        }
        seen.insert(normalized);
    }

    const ProcessResult verbose = runProcess(bsdtar,
                                             {QStringLiteral("-tvf"), archive},
                                             timeoutMs, cancelled);
    if (!verbose.ok) {
        *wasCancelled = verbose.cancelled;
        *detail = verbose.detail;
        return false;
    }
    const QList<QByteArray> modes = outputLines(verbose.standardOutput, &parseError);
    if (modes.size() != paths.size()) {
        *detail = parseError.isEmpty()
            ? QStringLiteral("Archive type listing does not match its path listing.")
            : parseError;
        return false;
    }
    for (qsizetype i = 0; i < modes.size(); ++i) {
        const char type = modes.at(i).isEmpty() ? '\0' : modes.at(i).at(0);
        if (type != '-' && type != 'd') {
            *detail = QStringLiteral("Archive entry is a link or special file: %1")
                          .arg(QString::fromLocal8Bit(paths.at(i)));
            return false;
        }
    }
    return true;
}

bool validateStagingTree(const QString &staging, QString *detail)
{
    QDirIterator iterator(staging, QDir::AllEntries | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString relative = QDir(staging).relativeFilePath(info.filePath());
        const QString portableRelative = QDir::fromNativeSeparators(relative);
        QString pathError;
        if (!safeRelativeArchivePath(portableRelative.toLocal8Bit(), &pathError)) {
            *detail = QStringLiteral("Extracted staging contains an unsafe path: %1")
                          .arg(pathError);
            return false;
        }
        if (info.isSymLink() || (!info.isFile() && !info.isDir())) {
            *detail = QStringLiteral("Extracted staging contains a link or special file: %1")
                          .arg(relative);
            return false;
        }
    }

    const QStringList required{
        QStringLiteral("dinput8.dll"),
        QStringLiteral("openvr_api.dll"),
        QStringLiteral("openxr_loader.dll"),
        QStringLiteral("reframework_revision.txt"),
        QStringLiteral("DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR"),
    };
    for (const QString &relative : required) {
        const QFileInfo info(QDir(staging).filePath(relative));
        if (!info.exists() || !info.isFile() || info.isSymLink()) {
            *detail = QStringLiteral("Extracted staging is missing regular file: %1").arg(relative);
            return false;
        }
    }
    return true;
}

void report(const ReFrameworkFetcher::ProgressCallback &progress,
            ReFrameworkFetcher::State state, const QString &detail)
{
    if (progress)
        progress(state, detail);
}

ReFrameworkFetcher::Result failure(ReFrameworkFetcher::Result result,
                                   const ReFrameworkFetcher::ProgressCallback &progress,
                                   const QString &detail, bool cancelled = false)
{
    result.ok = false;
    result.cancelled = cancelled;
    result.detail = detail;
    result.archivePath.clear();
    result.stagingDir.clear();
    result.workspace.reset();
    report(progress,
           cancelled ? ReFrameworkFetcher::State::Cancelled
                     : ReFrameworkFetcher::State::Failed,
           detail);
    return result;
}

} // namespace

ReFrameworkFetcher::Result ReFrameworkFetcher::prepareArchive(
    const QString &localArchive, const QString &tempRoot, const Options &options,
    ProgressCallback progress, CancelCheck cancelled)
{
    Result result;
    const bool hasCustomExpectedIdentity = options.expectedArchiveSize >= 0
        || !options.expectedArchiveSha256.isEmpty();
    if (hasCustomExpectedIdentity
        && (options.expectedArchiveSize < 0 || options.expectedArchiveSha256.isEmpty())) {
        return failure(result, progress,
                       QStringLiteral("Archive expectation must provide both size and SHA-256."));
    }
    report(progress, State::CheckingLocalArchive,
           QStringLiteral("Checking the caller-provided REFramework archive."));
    if (cancelled && cancelled())
        return failure(result, progress, QStringLiteral("Operation cancelled."), true);

    QString localDetail;
    const bool localValid = verifyArchive(localArchive, options, &localDetail);
    result.localArchiveDetail = localDetail;
    if (cancelled && cancelled())
        return failure(result, progress, QStringLiteral("Operation cancelled."), true);

    if (!localValid)
        report(progress, State::VerifyingArchive,
               QStringLiteral("Local archive failed verification; trying the pinned download."));

    const QString root = tempRoot.trimmed().isEmpty() ? QDir::tempPath() : tempRoot;
    if (!QDir().mkpath(root))
        return failure(result, progress,
                       QStringLiteral("Cannot create temporary root: %1").arg(root));
    auto workspace = std::make_shared<QTemporaryDir>(
        QDir(root).filePath(QStringLiteral("monster-overlay-reframework-XXXXXX")));
    if (!workspace->isValid())
        return failure(result, progress,
                       QStringLiteral("Cannot create temporary REFramework workspace in %1.")
                           .arg(root));

    const QString archive = QDir(workspace->path()).filePath(QStringLiteral("MHRISE.zip"));
    if (localValid) {
        result.source = Source::LocalArchive;
        if (!QFile::copy(localArchive, archive))
            return failure(result, progress,
                           QStringLiteral("Cannot snapshot the verified local archive."));
        QString snapshotDetail;
        if (!verifyArchive(archive, options, &snapshotDetail))
            return failure(result, progress,
                           QStringLiteral("Local archive changed while it was being snapshotted: %1")
                               .arg(snapshotDetail));
    } else {
        result.source = Source::Download;
        report(progress, State::DownloadingArchive,
               QStringLiteral("Local archive was rejected; downloading the pinned REFramework asset."));
        const ProcessResult downloaded = runProcess(
            options.curlExecutable,
            {QStringLiteral("--fail"),
             QStringLiteral("--location"),
             QStringLiteral("--retry"), QStringLiteral("3"),
             QStringLiteral("--connect-timeout"), QStringLiteral("20"),
             QStringLiteral("--max-time"), QStringLiteral("300"),
             QStringLiteral("--output"), archive,
             RiseReFrameworkManager::archiveUrl()},
            options.processTimeoutMs, cancelled);
        if (!downloaded.ok)
            return failure(result, progress, downloaded.detail, downloaded.cancelled);

        report(progress, State::VerifyingArchive,
               QStringLiteral("Verifying the downloaded archive size and SHA-256."));
        QString downloadDetail;
        if (!verifyArchive(archive, options, &downloadDetail))
            return failure(result, progress,
                           QStringLiteral("Downloaded archive verification failed: %1")
                               .arg(downloadDetail));
    }

    if (cancelled && cancelled())
        return failure(result, progress, QStringLiteral("Operation cancelled."), true);

    report(progress, State::InspectingArchive,
           QStringLiteral("Inspecting archive paths and entry types."));
    bool wasCancelled = false;
    QString detail;
    if (!inspectArchive(options.bsdtarExecutable, archive, options.processTimeoutMs,
                        cancelled, &wasCancelled, &detail)) {
        return failure(result, progress, detail, wasCancelled);
    }
    if (cancelled && cancelled())
        return failure(result, progress, QStringLiteral("Operation cancelled."), true);

    const QString staging = QDir(workspace->path()).filePath(QStringLiteral("staging"));
    if (!QDir().mkpath(staging))
        return failure(result, progress,
                       QStringLiteral("Cannot create archive staging directory."));

    report(progress, State::ExtractingArchive,
           QStringLiteral("Extracting the verified archive to temporary staging."));
    const ProcessResult extracted = runProcess(
        options.bsdtarExecutable,
        {QStringLiteral("-xf"), archive, QStringLiteral("-C"), staging},
        options.processTimeoutMs, cancelled);
    if (!extracted.ok)
        return failure(result, progress, extracted.detail, extracted.cancelled);
    if (cancelled && cancelled())
        return failure(result, progress, QStringLiteral("Operation cancelled."), true);
    if (!validateStagingTree(staging, &detail))
        return failure(result, progress, detail);

    result.ok = true;
    result.archivePath = archive;
    result.stagingDir = staging;
    result.workspace = std::move(workspace);
    result.detail = result.source == Source::LocalArchive
        ? QStringLiteral("Prepared the verified local REFramework archive.")
        : QStringLiteral("Downloaded and prepared the verified REFramework archive.");
    report(progress, State::Ready, result.detail);
    return result;
}

ReFrameworkFetcher::Result ReFrameworkFetcher::prepareArchive(
    const QString &localArchive, const QString &tempRoot,
    ProgressCallback progress, CancelCheck cancelled)
{
    return prepareArchive(localArchive, tempRoot, Options{}, std::move(progress),
                          std::move(cancelled));
}

ReFrameworkFetcher::Job::Job(QString localArchive, QString tempRoot, Options options,
                              ProgressCallback progress)
    : m_localArchive(std::move(localArchive)),
      m_tempRoot(std::move(tempRoot)),
      m_options(std::move(options)),
      m_progress(std::move(progress))
{
}

ReFrameworkFetcher::Job::~Job()
{
    cancel();
    if (m_thread.joinable())
        m_thread.join();
}

bool ReFrameworkFetcher::Job::terminal(State state)
{
    return state == State::Ready || state == State::Failed || state == State::Cancelled;
}

bool ReFrameworkFetcher::Job::start()
{
    {
        std::lock_guard lock(m_mutex);
        if (m_state != State::Idle)
            return false;
        m_state = State::CheckingLocalArchive;
        m_threadStarted = true;
    }
    m_thread = std::thread(&ReFrameworkFetcher::Job::run, this);
    return true;
}

void ReFrameworkFetcher::Job::cancel()
{
    std::lock_guard lock(m_mutex);
    if (terminal(m_state))
        return;
    m_cancelRequested = true;
    if (m_state == State::Idle && !m_threadStarted) {
        m_result.cancelled = true;
        m_result.detail = QStringLiteral("Operation was cancelled before it started.");
        m_state = State::Cancelled;
        m_finished.notify_all();
    } else {
        m_state = State::Cancelling;
    }
}

bool ReFrameworkFetcher::Job::waitForFinished(int timeoutMs)
{
    std::unique_lock lock(m_mutex);
    if (m_state == State::Idle) {
        m_result.detail = QStringLiteral("Job has not been started.");
        return false;
    }
    if (timeoutMs < 0) {
        m_finished.wait(lock, [this] { return terminal(m_state); });
        return true;
    }
    return m_finished.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                               [this] { return terminal(m_state); });
}

ReFrameworkFetcher::State ReFrameworkFetcher::Job::state() const
{
    std::lock_guard lock(m_mutex);
    return m_state;
}

ReFrameworkFetcher::Result ReFrameworkFetcher::Job::result() const
{
    std::lock_guard lock(m_mutex);
    return m_result;
}

void ReFrameworkFetcher::Job::run()
{
    const CancelCheck cancelled = [this]() {
        std::lock_guard lock(m_mutex);
        return m_cancelRequested;
    };
    const ProgressCallback progress = [this](State state, const QString &detail) {
        bool cancelled = false;
        {
            std::lock_guard lock(m_mutex);
            cancelled = m_cancelRequested;
            if (!terminal(m_state) && !cancelled)
                m_state = state;
        }
        if (m_progress) {
            const State callbackState =
                cancelled && state != State::Cancelled && state != State::Failed
                    ? State::Cancelling
                    : state;
            m_progress(callbackState, detail);
        }
    };

    Result prepared = ReFrameworkFetcher::prepareArchive(
        m_localArchive, m_tempRoot, m_options, progress, cancelled);
    {
        std::lock_guard lock(m_mutex);
        m_result = std::move(prepared);
        if (m_cancelRequested && !m_result.cancelled) {
            m_result.ok = false;
            m_result.cancelled = true;
            m_result.archivePath.clear();
            m_result.stagingDir.clear();
            m_result.workspace.reset();
            m_result.detail = QStringLiteral("Operation cancelled.");
        }
        if (m_result.cancelled || m_cancelRequested)
            m_state = State::Cancelled;
        else if (m_result.ok)
            m_state = State::Ready;
        else
            m_state = State::Failed;
    }
    m_finished.notify_all();
}

std::shared_ptr<ReFrameworkFetcher::Job> ReFrameworkFetcher::createJob(
    const QString &localArchive, const QString &tempRoot, const Options &options,
    ProgressCallback progress)
{
    return std::shared_ptr<Job>(new Job(localArchive, tempRoot, options, std::move(progress)));
}

std::shared_ptr<ReFrameworkFetcher::Job> ReFrameworkFetcher::createJob(
    const QString &localArchive, const QString &tempRoot, ProgressCallback progress)
{
    return createJob(localArchive, tempRoot, Options{}, std::move(progress));
}

} // namespace mhw
