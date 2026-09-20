// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QByteArray>
#include <QString>
#include <QTemporaryDir>

#include <functional>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace mhw {

// QtCore-only acquisition worker for the pinned Monster Hunter Rise
// REFramework archive. prepareArchive() is synchronous by design and may be
// called from a worker thread; the Job facade below keeps UI callers off the
// blocking path.
class ReFrameworkFetcher {
public:
    enum class State {
        Idle,
        CheckingLocalArchive,
        DownloadingArchive,
        VerifyingArchive,
        InspectingArchive,
        ExtractingArchive,
        Cancelling,
        Ready,
        Failed,
        Cancelled,
    };

    enum class Source {
        None,
        LocalArchive,
        Download,
    };

    struct Options {
        QString curlExecutable{QStringLiteral("curl")};
        QString bsdtarExecutable{QStringLiteral("bsdtar")};
        int processTimeoutMs{330000};

        // Leave both unset to enforce RiseReFrameworkManager's pinned size and
        // SHA-256. A complete pair is injectable for deterministic unit tests;
        // the hash is the lowercase hexadecimal form.
        qint64 expectedArchiveSize{-1};
        QByteArray expectedArchiveSha256;
    };

    struct Result {
        bool ok{false};
        bool cancelled{false};
        Source source{Source::None};
        QString archivePath;
        QString stagingDir;
        QString detail;
        QString localArchiveDetail;

        // Owns archivePath/stagingDir. Copies of Result share this lifetime;
        // dropping the last copy removes the complete temporary workspace.
        std::shared_ptr<QTemporaryDir> workspace;
    };

    using ProgressCallback = std::function<void(State, const QString &)>;
    using CancelCheck = std::function<bool()>;

    class Job;

    [[nodiscard]] static Result prepareArchive(const QString &localArchive,
                                               const QString &tempRoot,
                                               const Options &options,
                                               ProgressCallback progress = {},
                                               CancelCheck cancelled = {});
    [[nodiscard]] static Result prepareArchive(const QString &localArchive,
                                               const QString &tempRoot,
                                               ProgressCallback progress,
                                               CancelCheck cancelled = {});

    [[nodiscard]] static std::shared_ptr<Job> createJob(
        const QString &localArchive, const QString &tempRoot, const Options &options,
        ProgressCallback progress = {});
    [[nodiscard]] static std::shared_ptr<Job> createJob(
        const QString &localArchive, const QString &tempRoot,
        ProgressCallback progress);
};

class ReFrameworkFetcher::Job {
public:
    ~Job();

    // Returns false when the job was already started, cancelled, or finished.
    [[nodiscard]] bool start();
    void cancel();

    // A negative timeout means wait indefinitely; zero performs a poll.
    // Calling this before start() returns false and leaves the job in Idle.
    [[nodiscard]] bool waitForFinished(int timeoutMs = -1);
    [[nodiscard]] State state() const;
    [[nodiscard]] Result result() const;

private:
    friend class ReFrameworkFetcher;

    Job(QString localArchive, QString tempRoot, Options options,
        ProgressCallback progress);
    Job(const Job &) = delete;
    Job &operator=(const Job &) = delete;

    void run();
    static bool terminal(State state);

    const QString m_localArchive;
    const QString m_tempRoot;
    const Options m_options;
    const ProgressCallback m_progress;

    mutable std::mutex m_mutex;
    std::condition_variable m_finished;
    std::thread m_thread;
    State m_state{State::Idle};
    Result m_result;
    bool m_cancelRequested{false};
    bool m_threadStarted{false};
};

} // namespace mhw
