// SPDX-License-Identifier: Apache-2.0
// Unit tests for the QtCore-only Steam game installation locator.

#include "core/steam_game_locator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *description)
{
    if (condition) {
        std::printf("PASS: %s\n", description);
    } else {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

bool writeFile(const QString &path, const QByteArray &contents = QByteArray())
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(contents) == contents.size();
}

QString addGame(const QString &library, quint64 appId, const QString &installDir,
                const QString &executableName, bool createExecutable = true)
{
    const QString gameDir =
        QDir(library + QStringLiteral("/steamapps/common")).filePath(installDir);
    if (!QDir().mkpath(gameDir))
        return {};
    const QByteArray manifest = QByteArray("\"AppState\"\n{\n  \"appid\" \"") +
                                QByteArray::number(appId) + QByteArray("\"\n  \"installdir\" \"") +
                                installDir.toUtf8() + QByteArray("\"\n}\n");
    if (!writeFile(QDir(library + QStringLiteral("/steamapps"))
                       .filePath(QStringLiteral("appmanifest_%1.acf").arg(appId)),
                   manifest)) {
        return {};
    }
    if (createExecutable && !writeFile(QDir(gameDir).filePath(executableName), QByteArray("exe"))) {
        return {};
    }
    return QFileInfo(gameDir).canonicalFilePath();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        std::fprintf(stderr, "FATAL: could not create temporary directory\n");
        return 2;
    }

    const QString root = temporaryDirectory.filePath(QStringLiteral("default Steam root"));
    const QString expected = addGame(root, 1446780, QStringLiteral("MonsterHunterRise"),
                                     QStringLiteral("MonsterHunterRise.exe"));
    check(!expected.isEmpty(), "fixture: default-library Rise install created");
    check(mhw::findSteamGameInstallDir(1446780, QStringLiteral("MonsterHunterRise.exe"), {root}) ==
              expected,
          "game is found in an explicit Steam root's default library");

    const QString configRoot = temporaryDirectory.filePath(QStringLiteral("config root"));
    const QString spacedLibrary =
        temporaryDirectory.filePath(QStringLiteral("extra library with spaces"));
    const QString spacedExpected =
        addGame(spacedLibrary, 1446780, QStringLiteral("Rise From Extra Library"),
                QStringLiteral("MonsterHunterRise.exe"));
    const QByteArray configVdf =
        QByteArray("\"libraryfolders\"\n{\n  \"1\"\n  {\n    \"path\" \"") +
        spacedLibrary.toUtf8() + QByteArray("\"\n  }\n}\n");
    check(writeFile(QDir(configRoot).filePath(QStringLiteral("config/libraryfolders.vdf")),
                    configVdf),
          "fixture: config/libraryfolders.vdf created");
    check(!spacedExpected.isEmpty(), "fixture: spaced extra-library game created");
    check(mhw::findSteamGameInstallDir(1446780, QStringLiteral("MonsterHunterRise.exe"),
                                       {configRoot}) == spacedExpected,
          "quoted path with spaces is found through config/libraryfolders.vdf");

    const QString steamappsRoot = temporaryDirectory.filePath(QStringLiteral("steamapps-vdf root"));
    const QString steamappsLibrary =
        temporaryDirectory.filePath(QStringLiteral("library from steamapps vdf"));
    const QString steamappsExpected =
        addGame(steamappsLibrary, 1446780, QStringLiteral("Rise Steamapps VDF"),
                QStringLiteral("MonsterHunterRise.exe"));
    const QByteArray steamappsVdf =
        QByteArray("\"libraryfolders\"\n{\n  \"2\"\n  {\n    \"path\" \"") +
        steamappsLibrary.toUtf8() + QByteArray("\"\n  }\n}\n");
    check(writeFile(QDir(steamappsRoot).filePath(QStringLiteral("steamapps/libraryfolders.vdf")),
                    steamappsVdf),
          "fixture: steamapps/libraryfolders.vdf created");
    check(mhw::findSteamGameInstallDir(1446780, QStringLiteral("MonsterHunterRise.exe"),
                                       {steamappsRoot}) == steamappsExpected,
          "extra library is found through steamapps/libraryfolders.vdf");

    const QString escapedRoot = temporaryDirectory.filePath(QStringLiteral("escaped root"));
    const QString escapedLibrary =
        temporaryDirectory.filePath(QStringLiteral("library\\with backslash"));
    const QString escapedExpected =
        addGame(escapedLibrary, 1446780, QStringLiteral("Rise Escaped Path"),
                QStringLiteral("MonsterHunterRise.exe"));
    const QString escapedAlias =
        temporaryDirectory.filePath(QStringLiteral("escaped library alias"));
    check(QFile::link(escapedLibrary, escapedAlias),
          "fixture: symlink alias for escaped library created");
    QString encodedEscapedLibrary = escapedLibrary;
    encodedEscapedLibrary.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    QString encodedEscapedAlias = escapedAlias;
    encodedEscapedAlias.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    const QString missingLibrary =
        temporaryDirectory.filePath(QStringLiteral("missing absolute library"));
    const QByteArray filteredConfigVdf =
        QByteArray("\"libraryfolders\"\n{\n") + QByteArray("  \"1\" { \"path\" \"") +
        encodedEscapedLibrary.toUtf8() + QByteArray("\" }\n") +
        QByteArray("  \"2\" { \"path\" \"relative/library\" }\n") +
        QByteArray("  \"3\" { \"path\" \"") + missingLibrary.toUtf8() + QByteArray("\" }\n}\n");
    const QByteArray duplicateSteamappsVdf =
        QByteArray("\"libraryfolders\"\n{\n") + QByteArray("  \"4\" { \"path\" \"") +
        encodedEscapedAlias.toUtf8() + QByteArray("\" }\n") + QByteArray("  \"5\" { \"path\" \"") +
        encodedEscapedLibrary.toUtf8() + QByteArray("\" }\n}\n");
    check(writeFile(QDir(escapedRoot).filePath(QStringLiteral("config/libraryfolders.vdf")),
                    filteredConfigVdf),
          "fixture: escaped/filtering config VDF created");
    check(writeFile(QDir(escapedRoot).filePath(QStringLiteral("steamapps/libraryfolders.vdf")),
                    duplicateSteamappsVdf),
          "fixture: duplicate steamapps VDF created");
    const QStringList filteredLibraries = mhw::steamLibraryPaths({escapedRoot, escapedRoot});
    check(filteredLibraries == QStringList{QFileInfo(escapedRoot).canonicalFilePath(),
                                           QFileInfo(escapedLibrary).canonicalFilePath()},
          "library paths are unescaped, canonicalized, filtered, and deduplicated");
    check(mhw::findSteamGameInstallDir(1446780, QStringLiteral("MonsterHunterRise.exe"),
                                       {escapedRoot}) == escapedExpected,
          "game is found through a VDF path containing an escaped backslash");

    const auto checkSingleDefaultRoot = [&](const QString &homeName, const QString &relativeRoot,
                                            const char *message) {
        const QString home = temporaryDirectory.filePath(homeName);
        const QString candidate = QDir(home).filePath(relativeRoot);
        check(QDir().mkpath(candidate), "fixture: individual default root created");
        check(mhw::defaultSteamRoots(home) == QStringList{QFileInfo(candidate).canonicalFilePath()},
              message);
    };
    checkSingleDefaultRoot(QStringLiteral("home-local-share"), QStringLiteral(".local/share/Steam"),
                           "default roots include ~/.local/share/Steam");
    checkSingleDefaultRoot(QStringLiteral("home-steam-root"), QStringLiteral(".steam/root"),
                           "default roots include ~/.steam/root");
    checkSingleDefaultRoot(QStringLiteral("home-steam-steam"), QStringLiteral(".steam/steam"),
                           "default roots include ~/.steam/steam");

    const QString linkedHome = temporaryDirectory.filePath(QStringLiteral("home-linked-defaults"));
    const QString linkedCanonicalRoot =
        QDir(linkedHome).filePath(QStringLiteral(".local/share/Steam"));
    const QString dotSteam = QDir(linkedHome).filePath(QStringLiteral(".steam"));
    check(QDir().mkpath(linkedCanonicalRoot) && QDir().mkpath(dotSteam),
          "fixture: linked default-root directories created");
    check(QFile::link(linkedCanonicalRoot, QDir(dotSteam).filePath(QStringLiteral("root"))) &&
              QFile::link(linkedCanonicalRoot, QDir(dotSteam).filePath(QStringLiteral("steam"))),
          "fixture: duplicate default-root symlinks created");
    check(mhw::defaultSteamRoots(linkedHome) ==
              QStringList{QFileInfo(linkedCanonicalRoot).canonicalFilePath()},
          "default roots canonicalize and deduplicate symlink aliases");
    check(mhw::defaultSteamRoots() == mhw::defaultSteamRoots(QDir::homePath()),
          "no-argument default roots use the current home directory");
    check(mhw::findRiseInstallDir({root}) == expected,
          "Rise convenience locator uses app 1446780 and MonsterHunterRise.exe");
    check(mhw::findSteamGameInstallDir(1446780, QStringLiteral("MonsterHunterRise.exe")) ==
              mhw::findSteamGameInstallDir(1446780, QStringLiteral("MonsterHunterRise.exe"),
                                           mhw::defaultSteamRoots()),
          "no-root game locator searches the default Steam roots");
    check(mhw::findRiseInstallDir() ==
              mhw::findSteamGameInstallDir(1446780, QStringLiteral("MonsterHunterRise.exe"),
                                           mhw::defaultSteamRoots()),
          "no-argument Rise locator searches the default Steam roots");

    const QString invalidRoot =
        temporaryDirectory.filePath(QStringLiteral("invalid manifest root"));
    check(QDir().mkpath(QDir(invalidRoot).filePath(QStringLiteral("steamapps/common"))),
          "fixture: invalid-manifest root created");
    const QString invalidGameDir =
        QDir(invalidRoot).filePath(QStringLiteral("steamapps/common/Broken"));
    check(writeFile(QDir(invalidGameDir).filePath(QStringLiteral("MonsterHunterRise.exe")),
                    QByteArray("exe")),
          "fixture: executable referenced by malformed manifest created");
    const QString invalidManifest =
        QDir(invalidRoot).filePath(QStringLiteral("steamapps/appmanifest_1446780.acf"));
    check(writeFile(invalidManifest, QByteArray("\"AppState\" { \"installdir\" \"Broken\"\n")),
          "fixture: malformed manifest created");
    check(mhw::findRiseInstallDir({invalidRoot}).isEmpty(),
          "malformed manifest returns no guessed path");

    const QString valueOnlyRoot =
        temporaryDirectory.filePath(QStringLiteral("key-value pairing root"));
    const QString valueOnlyGameDir =
        QDir(valueOnlyRoot).filePath(QStringLiteral("steamapps/common/NotAnInstallDir"));
    check(writeFile(QDir(valueOnlyGameDir).filePath(QStringLiteral("MonsterHunterRise.exe")),
                    QByteArray("exe")),
          "fixture: executable for key-value pairing check created");
    check(
        writeFile(QDir(valueOnlyRoot).filePath(QStringLiteral("steamapps/appmanifest_1446780.acf")),
                  QByteArray("\"AppState\" { \"note\" \"installdir\" "
                             "\"NotAnInstallDir\" \"unused\" }\n")),
        "fixture: manifest with installdir only as a value created");
    check(mhw::findRiseInstallDir({valueOnlyRoot}).isEmpty(),
          "a value named installdir is not misread as a key");

    const QString relativeRoot = temporaryDirectory.filePath(QStringLiteral("relative path root"));
    check(QDir().mkpath(QDir(relativeRoot).filePath(QStringLiteral("steamapps/common"))),
          "fixture: relative-path root created");
    check(writeFile(QDir(relativeRoot).filePath(QStringLiteral("config/libraryfolders.vdf")),
                    QByteArray("\"libraryfolders\" { \"1\" { \"path\" \"") +
                        QByteArray("relative/library\" } }\n")),
          "fixture: relative library path manifest created");
    check(mhw::steamLibraryPaths({relativeRoot}) ==
              QStringList{QFileInfo(relativeRoot).canonicalFilePath()},
          "relative library path is ignored");

    const QString traversalRoot =
        temporaryDirectory.filePath(QStringLiteral("traversal manifest root"));
    const QString outsideDirectory =
        temporaryDirectory.filePath(QStringLiteral("outside install directory"));
    check(writeFile(QDir(outsideDirectory).filePath(QStringLiteral("MonsterHunterRise.exe")),
                    QByteArray("exe")),
          "fixture: outside executable for traversal check created");
    check(
        writeFile(QDir(traversalRoot).filePath(QStringLiteral("steamapps/appmanifest_1446780.acf")),
                  QByteArray("\"AppState\" { \"installdir\" \"../outside install directory\" }\n")),
        "fixture: traversal manifest created");
    check(mhw::findRiseInstallDir({traversalRoot}).isEmpty(),
          "manifest installdir traversal returns empty");

    const QString noExeRoot =
        temporaryDirectory.filePath(QStringLiteral("missing executable root"));
    const QString noExeExpected = addGame(noExeRoot, 1446780, QStringLiteral("Rise Without Exe"),
                                          QStringLiteral("MonsterHunterRise.exe"), false);
    check(!noExeExpected.isEmpty(),
          "fixture: manifest and install directory without executable created");
    check(mhw::findRiseInstallDir({noExeRoot}).isEmpty(),
          "manifest without target executable returns empty");
    check(mhw::findSteamGameInstallDir(1446780, QStringLiteral("../MonsterHunterRise.exe"), {root})
              .isEmpty(),
          "unsafe executable path cannot escape the install directory");

    const QString duplicateRoot = temporaryDirectory.filePath(QStringLiteral("duplicate root"));
    const QString duplicateExpected =
        addGame(duplicateRoot, 1446780, QStringLiteral("Rise Duplicate Root"),
                QStringLiteral("MonsterHunterRise.exe"));
    check(mhw::steamLibraryPaths({duplicateRoot, duplicateRoot}) ==
              QStringList{QFileInfo(duplicateRoot).canonicalFilePath()},
          "duplicate explicit roots are collapsed");
    check(mhw::findRiseInstallDir({duplicateRoot, duplicateRoot}) == duplicateExpected,
          "duplicate explicit roots do not change the result");

    if (failures == 0)
        std::printf("\nsteam-game-locator-tests: ALL PASSED\n");
    else
        std::fprintf(stderr, "\nsteam-game-locator-tests: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
