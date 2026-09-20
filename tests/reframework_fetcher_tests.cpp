// SPDX-License-Identifier: Apache-2.0

#include "core/reframework_fetcher.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>
#include <chrono>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, const QString &message)
{
    if (condition) {
        std::cout << "PASS: " << message.toStdString() << '\n';
    } else {
        std::cerr << "FAIL: " << message.toStdString() << '\n';
        ++failures;
    }
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

bool writeExecutable(const QString &path, const QByteArray &script)
{
    if (!writeFile(path, script))
        return false;
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                       | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                       | QFileDevice::ExeOther);
}

QByteArray validBsdtarScript()
{
    return QByteArrayLiteral(
        "#!/bin/sh\n"
        "set -eu\n"
        "case \"$1\" in\n"
        "  -tf)\n"
        "    printf '%s\\n' dinput8.dll openvr_api.dll openxr_loader.dll "
        "reframework_revision.txt DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR\n"
        "    ;;\n"
        "  -tvf)\n"
        "    printf '%s\\n' '-rw-r--r-- dinput8.dll' '-rw-r--r-- openvr_api.dll' "
        "'-rw-r--r-- openxr_loader.dll' '-rw-r--r-- reframework_revision.txt' "
        "'-rw-r--r-- DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR'\n"
        "    ;;\n"
        "  -xf)\n"
        "    test \"$3\" = -C\n"
        "    out=$4\n"
        "    mkdir -p \"$out\"\n"
        "    printf dinput8 > \"$out/dinput8.dll\"\n"
        "    printf openvr > \"$out/openvr_api.dll\"\n"
        "    printf openxr > \"$out/openxr_loader.dll\"\n"
        "    printf revision > \"$out/reframework_revision.txt\"\n"
        "    printf marker > \"$out/DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR\"\n"
        "    ;;\n"
        "  *) exit 91 ;;\n"
        "esac\n");
}

QByteArray copyingCurlScript()
{
    return QByteArrayLiteral(
        "#!/bin/sh\n"
        "set -eu\n"
        ": > \"$FAKE_CURL_LOG\"\n"
        "out=\n"
        "want_out=0\n"
        "for arg in \"$@\"; do\n"
        "  printf '[%s]\\n' \"$arg\" >> \"$FAKE_CURL_LOG\"\n"
        "  if test \"$want_out\" = 1; then out=$arg; want_out=0; fi\n"
        "  if test \"$arg\" = --output; then want_out=1; fi\n"
        "done\n"
        "test -n \"$out\"\n"
        "cp \"$FAKE_CURL_SOURCE\" \"$out\"\n");
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray malformedOrUnsafeBsdtarScript(const QByteArray &listing, const QByteArray &verbose)
{
    return QByteArrayLiteral("#!/bin/sh\nset -eu\ncase \"$1\" in\n")
        + QByteArrayLiteral("  -tf) printf '%s\\n' '") + listing + QByteArrayLiteral("' ;;\n")
        + QByteArrayLiteral("  -tvf) printf '%s\\n' '") + verbose + QByteArrayLiteral("' ;;\n")
        + QByteArrayLiteral("  -xf) exit 99 ;;\n  *) exit 91 ;;\nesac\n");
}

QByteArray cancellableBsdtarScript()
{
    return QByteArrayLiteral(
        "#!/bin/sh\n"
        "set -eu\n"
        "case \"$1\" in\n"
        "  -tf) printf '%s\\n' dinput8.dll openvr_api.dll openxr_loader.dll "
        "reframework_revision.txt DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR ;;\n"
        "  -tvf) printf '%s\\n' '-rw-r--r-- dinput8.dll' '-rw-r--r-- openvr_api.dll' "
        "'-rw-r--r-- openxr_loader.dll' '-rw-r--r-- reframework_revision.txt' "
        "'-rw-r--r-- DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR' ;;\n"
        "  -xf) exec sleep 10 ;;\n"
        "  *) exit 91 ;;\n"
        "esac\n");
}


mhw::ReFrameworkFetcher::Options fixtureOptions(const QByteArray &archive,
                                                 const QString &bsdtar)
{
    mhw::ReFrameworkFetcher::Options options;
    options.curlExecutable = QStringLiteral("/definitely/not/curl");
    options.bsdtarExecutable = bsdtar;
    options.expectedArchiveSize = archive.size();
    options.expectedArchiveSha256 =
        QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex();
    options.processTimeoutMs = 5000;
    return options;
}

void localArchiveIsPreferredAndLifetimeIsOwned()
{
    QTemporaryDir root;
    check(root.isValid(), QStringLiteral("local-success temporary root is valid"));

    const QByteArray archiveBytes("fixture-archive-v1");
    const QString archive = root.filePath(QStringLiteral("MHRISE.zip"));
    const QString bsdtar = root.filePath(QStringLiteral("fake-bsdtar"));
    check(writeFile(archive, archiveBytes), QStringLiteral("local-success archive fixture writes"));
    check(writeExecutable(bsdtar, validBsdtarScript()),
          QStringLiteral("local-success bsdtar fixture writes"));

    QList<mhw::ReFrameworkFetcher::State> states;
    auto result = mhw::ReFrameworkFetcher::prepareArchive(
        archive, root.filePath(QStringLiteral("jobs")), fixtureOptions(archiveBytes, bsdtar),
        [&](mhw::ReFrameworkFetcher::State state, const QString &) { states.append(state); });

    check(result.ok, QStringLiteral("valid local archive prepares successfully"));
    check(result.source == mhw::ReFrameworkFetcher::Source::LocalArchive,
          QStringLiteral("valid local archive is preferred over curl"));
    check(states.contains(mhw::ReFrameworkFetcher::State::InspectingArchive)
              && states.contains(mhw::ReFrameworkFetcher::State::ExtractingArchive)
              && !states.contains(mhw::ReFrameworkFetcher::State::DownloadingArchive),
          QStringLiteral("local success follows inspect/extract without download"));
    check(QFileInfo(result.stagingDir + QStringLiteral("/dinput8.dll")).isFile(),
          QStringLiteral("local success returns extracted dinput8.dll"));
    check(QFileInfo(result.stagingDir + QStringLiteral("/openvr_api.dll")).isFile()
              && QFileInfo(result.stagingDir + QStringLiteral("/openxr_loader.dll")).isFile()
              && QFileInfo(result.stagingDir + QStringLiteral("/reframework_revision.txt")).isFile()
              && QFileInfo(result.stagingDir
                           + QStringLiteral("/DELETE_OPENVR_API_DLL_IF_YOU_WANT_TO_USE_OPENXR"))
                     .isFile(),
          QStringLiteral("local success validates every manager-required core file"));

    const QString staging = result.stagingDir;
    check(QFileInfo(staging).isDir(), QStringLiteral("returned result keeps staging alive"));
    result = {};
    check(!QFileInfo::exists(staging),
          QStringLiteral("releasing returned result cleans its temporary staging tree"));
}

void badLocalArchiveFallsBackToPinnedCurlCommand()
{
    QTemporaryDir root;
    const QByteArray goodArchive("downloaded-fixture-archive");
    const QString localArchive = root.filePath(QStringLiteral("bad-local.zip"));
    const QString downloadSource = root.filePath(QStringLiteral("download-source.zip"));
    const QString curl = root.filePath(QStringLiteral("fake-curl"));
    const QString bsdtar = root.filePath(QStringLiteral("fake-bsdtar"));
    const QString curlLog = root.filePath(QStringLiteral("curl.args"));
    check(writeFile(localArchive, QByteArrayLiteral("bad")),
          QStringLiteral("fallback bad local fixture writes"));
    check(writeFile(downloadSource, goodArchive),
          QStringLiteral("fallback download source fixture writes"));
    check(writeExecutable(curl, copyingCurlScript()),
          QStringLiteral("fallback curl fixture writes"));
    check(writeExecutable(bsdtar, validBsdtarScript()),
          QStringLiteral("fallback bsdtar fixture writes"));

    const QByteArray oldSource = qgetenv("FAKE_CURL_SOURCE");
    const QByteArray oldLog = qgetenv("FAKE_CURL_LOG");
    const bool sourceWasSet = qEnvironmentVariableIsSet("FAKE_CURL_SOURCE");
    const bool logWasSet = qEnvironmentVariableIsSet("FAKE_CURL_LOG");
    qputenv("FAKE_CURL_SOURCE", downloadSource.toLocal8Bit());
    qputenv("FAKE_CURL_LOG", curlLog.toLocal8Bit());

    auto options = fixtureOptions(goodArchive, bsdtar);
    options.curlExecutable = curl;
    QList<mhw::ReFrameworkFetcher::State> states;
    const auto result = mhw::ReFrameworkFetcher::prepareArchive(
        localArchive, root.filePath(QStringLiteral("jobs")), options,
        [&](mhw::ReFrameworkFetcher::State state, const QString &) { states.append(state); });

    if (sourceWasSet) qputenv("FAKE_CURL_SOURCE", oldSource);
    else qunsetenv("FAKE_CURL_SOURCE");
    if (logWasSet) qputenv("FAKE_CURL_LOG", oldLog);
    else qunsetenv("FAKE_CURL_LOG");

    check(result.ok && result.source == mhw::ReFrameworkFetcher::Source::Download,
          QStringLiteral("bad local archive falls back to a verified download"));
    check(result.localArchiveDetail.contains(QStringLiteral("size mismatch"),
                                             Qt::CaseInsensitive),
          QStringLiteral("fallback result retains the local rejection reason"));
    check(states.contains(mhw::ReFrameworkFetcher::State::DownloadingArchive)
              && states.contains(mhw::ReFrameworkFetcher::State::VerifyingArchive),
          QStringLiteral("fallback reports download and verification states"));

    const QByteArray expectedArgs = QByteArrayLiteral(
        "[--fail]\n"
        "[--location]\n"
        "[--retry]\n"
        "[3]\n"
        "[--connect-timeout]\n"
        "[20]\n"
        "[--max-time]\n"
        "[300]\n"
        "[--output]\n")
        + QByteArrayLiteral("[") + result.archivePath.toLocal8Bit() + QByteArrayLiteral("]\n")
        + QByteArrayLiteral(
            "[https://github.com/praydog/REFramework/releases/download/v1.5.9.1/MHRISE.zip]\n");
    check(readFile(curlLog) == expectedArgs,
          QStringLiteral("curl receives the exact fixed retry/timeouts, output, and pinned URL arguments"));
}

void archiveIdentityRejectionAndMissingToolsAreErrors()
{
    QTemporaryDir root;
    const QByteArray good("expected-archive");
    const QString archive = root.filePath(QStringLiteral("local.zip"));
    const QString bsdtar = root.filePath(QStringLiteral("fake-bsdtar"));
    check(writeFile(archive, good), QStringLiteral("identity archive fixture writes"));
    check(writeExecutable(bsdtar, validBsdtarScript()),
          QStringLiteral("identity bsdtar fixture writes"));

    auto sizeOptions = fixtureOptions(good, bsdtar);
    sizeOptions.expectedArchiveSize += 1;
    sizeOptions.curlExecutable = QStringLiteral("/definitely/missing/curl");
    auto result = mhw::ReFrameworkFetcher::prepareArchive(
        archive, root.filePath(QStringLiteral("jobs-size")), sizeOptions);
    check(!result.ok && result.source == mhw::ReFrameworkFetcher::Source::Download
              && result.localArchiveDetail.contains(QStringLiteral("size mismatch"),
                                                    Qt::CaseInsensitive)
              && result.detail.contains(QStringLiteral("could not start"),
                                        Qt::CaseInsensitive),
          QStringLiteral("size mismatch chooses fallback and reports a missing curl"));

    auto hashOptions = fixtureOptions(good, bsdtar);
    hashOptions.expectedArchiveSha256.fill('0');
    hashOptions.curlExecutable = QStringLiteral("/definitely/missing/curl");
    result = mhw::ReFrameworkFetcher::prepareArchive(
        archive, root.filePath(QStringLiteral("jobs-hash")), hashOptions);
    check(!result.ok
              && result.localArchiveDetail.contains(QStringLiteral("SHA-256 mismatch"),
                                                    Qt::CaseInsensitive),
          QStringLiteral("SHA-256 mismatch rejects the local archive"));

    const QString curl = root.filePath(QStringLiteral("identity-curl"));
    const QString curlLog = root.filePath(QStringLiteral("identity-curl.log"));
    const QString badDownload = root.filePath(QStringLiteral("bad-download.zip"));
    check(writeFile(badDownload, QByteArrayLiteral("wrong-download")),
          QStringLiteral("bad download fixture writes"));
    check(writeExecutable(curl, copyingCurlScript()),
          QStringLiteral("identity curl fixture writes"));
    const QByteArray oldSource = qgetenv("FAKE_CURL_SOURCE");
    const QByteArray oldLog = qgetenv("FAKE_CURL_LOG");
    const bool sourceWasSet = qEnvironmentVariableIsSet("FAKE_CURL_SOURCE");
    const bool logWasSet = qEnvironmentVariableIsSet("FAKE_CURL_LOG");
    qputenv("FAKE_CURL_SOURCE", badDownload.toLocal8Bit());
    qputenv("FAKE_CURL_LOG", curlLog.toLocal8Bit());
    auto badDownloadOptions = fixtureOptions(good, bsdtar);
    badDownloadOptions.curlExecutable = curl;
    result = mhw::ReFrameworkFetcher::prepareArchive(
        root.filePath(QStringLiteral("missing-local.zip")),
        root.filePath(QStringLiteral("jobs-download-hash")), badDownloadOptions);
    if (sourceWasSet) qputenv("FAKE_CURL_SOURCE", oldSource);
    else qunsetenv("FAKE_CURL_SOURCE");
    if (logWasSet) qputenv("FAKE_CURL_LOG", oldLog);
    else qunsetenv("FAKE_CURL_LOG");
    check(!result.ok && result.source == mhw::ReFrameworkFetcher::Source::Download
              && result.detail.contains(QStringLiteral("Downloaded archive verification failed")),
          QStringLiteral("downloaded size/hash mismatch is rejected before extraction"));

    auto missingTar = fixtureOptions(good, QStringLiteral("/definitely/missing/bsdtar"));
    result = mhw::ReFrameworkFetcher::prepareArchive(
        archive, root.filePath(QStringLiteral("jobs-tar")), missingTar);
    check(!result.ok && result.source == mhw::ReFrameworkFetcher::Source::LocalArchive
              && result.detail.contains(QStringLiteral("could not start"),
                                        Qt::CaseInsensitive),
          QStringLiteral("missing bsdtar is a clean local-path failure"));
}

void unsafeArchiveEntriesAreRejectedBeforeExtraction()
{
    QTemporaryDir root;
    const QByteArray archiveBytes("unsafe-archive");
    const QString archive = root.filePath(QStringLiteral("local.zip"));
    check(writeFile(archive, archiveBytes), QStringLiteral("unsafe archive fixture writes"));

    struct UnsafeCase {
        QByteArray path;
        QByteArray verbose;
        QString description;
    };
    const QList<UnsafeCase> cases{
        {QByteArrayLiteral("../escape.dll"), QByteArrayLiteral("-rw-r--r-- ../escape.dll"),
         QStringLiteral("parent traversal")},
        {QByteArrayLiteral("/absolute.dll"), QByteArrayLiteral("-rw-r--r-- /absolute.dll"),
         QStringLiteral("absolute path")},
        {QByteArrayLiteral("link.dll"), QByteArrayLiteral("lrwxrwxrwx link.dll -> dinput8.dll"),
         QStringLiteral("symlink")},
        {QByteArrayLiteral("device"), QByteArrayLiteral("crw-r--r-- device"),
         QStringLiteral("special file")},
    };

    int index = 0;
    for (const UnsafeCase &unsafe : cases) {
        const QString bsdtar = root.filePath(QStringLiteral("unsafe-bsdtar-%1").arg(index));
        check(writeExecutable(bsdtar,
                              malformedOrUnsafeBsdtarScript(unsafe.path, unsafe.verbose)),
              QStringLiteral("%1 fixture writes").arg(unsafe.description));
        const auto result = mhw::ReFrameworkFetcher::prepareArchive(
            archive, root.filePath(QStringLiteral("jobs-unsafe-%1").arg(index)),
            fixtureOptions(archiveBytes, bsdtar));
        check(!result.ok
                  && (result.detail.contains(QStringLiteral("unsafe"), Qt::CaseInsensitive)
                      || result.detail.contains(QStringLiteral("absolute"), Qt::CaseInsensitive)
                      || result.detail.contains(QStringLiteral("special"), Qt::CaseInsensitive)
                      || result.detail.contains(QStringLiteral("link"), Qt::CaseInsensitive)),
              QStringLiteral("archive %1 is rejected before extraction").arg(unsafe.description));
        ++index;
    }
}

void asynchronousJobHasExplicitStateAndCancellationTransitions()
{
    QTemporaryDir root;
    const QByteArray archiveBytes("async-fixture-archive");
    const QString archive = root.filePath(QStringLiteral("local.zip"));
    const QString bsdtar = root.filePath(QStringLiteral("slow-bsdtar"));
    check(writeFile(archive, archiveBytes), QStringLiteral("async archive fixture writes"));
    check(writeExecutable(bsdtar, cancellableBsdtarScript()),
          QStringLiteral("async cancellable bsdtar fixture writes"));

    const auto options = fixtureOptions(archiveBytes, bsdtar);
    auto notStarted = mhw::ReFrameworkFetcher::createJob(
        archive, root.filePath(QStringLiteral("jobs")), options);
    check(notStarted->state() == mhw::ReFrameworkFetcher::State::Idle,
          QStringLiteral("new job starts in Idle state"));
    check(!notStarted->waitForFinished(0),
          QStringLiteral("waiting on an unstarted job reports a state error"));
    check(!notStarted->result().ok
              && notStarted->result().detail.contains(QStringLiteral("not been started")),
          QStringLiteral("unstarted job result explains the state error"));
    check(notStarted->start(), QStringLiteral("idle job starts exactly once"));
    check(!notStarted->start(), QStringLiteral("started job rejects a second start"));
    const auto cancelDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (notStarted->state() != mhw::ReFrameworkFetcher::State::ExtractingArchive
           && notStarted->state() != mhw::ReFrameworkFetcher::State::Ready
           && notStarted->state() != mhw::ReFrameworkFetcher::State::Failed
           && std::chrono::steady_clock::now() < cancelDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(notStarted->state() == mhw::ReFrameworkFetcher::State::ExtractingArchive,
          QStringLiteral("async job reaches the cancellable extraction state"));
    notStarted->cancel();
    check(notStarted->waitForFinished(5000), QStringLiteral("started job can be joined"));
    check(notStarted->state() == mhw::ReFrameworkFetcher::State::Cancelled,
          QStringLiteral("slow job can be cancelled into terminal state"));
    check(notStarted->result().cancelled, QStringLiteral("cancelled job marks its result"));

    auto cancelledBeforeStart = mhw::ReFrameworkFetcher::createJob(
        archive, root.filePath(QStringLiteral("jobs")), options);
    cancelledBeforeStart->cancel();
    check(cancelledBeforeStart->state() == mhw::ReFrameworkFetcher::State::Cancelled,
          QStringLiteral("cancel before start transitions directly to Cancelled"));
    check(!cancelledBeforeStart->start(),
          QStringLiteral("cancelled-before-start job cannot be started"));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    localArchiveIsPreferredAndLifetimeIsOwned();
    badLocalArchiveFallsBackToPinnedCurlCommand();
    archiveIdentityRejectionAndMissingToolsAreErrors();
    unsafeArchiveEntriesAreRejectedBeforeExtraction();
    asynchronousJobHasExplicitStateAndCancellationTransitions();

    if (failures == 0)
        std::cout << "ALL TESTS PASSED\n";
    else
        std::cerr << failures << " TESTS FAILED\n";
    return failures == 0 ? 0 : 1;
}
