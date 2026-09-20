#include "core/steam_game_locator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVector>

namespace mhw {
namespace {

enum class VdfTokenKind : quint8 {
    String,
    Separator,
};

struct VdfToken {
    VdfTokenKind kind{VdfTokenKind::Separator};
    QString value;
};

bool tokenizeVdf(const QByteArray &data, QVector<VdfToken> *tokens)
{
    tokens->clear();
    const QString text = QString::fromUtf8(data);
    qsizetype position = 0;
    int braceDepth = 0;

    while (position < text.size()) {
        const QChar current = text.at(position);
        if (current.isSpace()) {
            ++position;
            continue;
        }

        if (current == u'/' && position + 1 < text.size() && text.at(position + 1) == u'/') {
            position += 2;
            while (position < text.size() && text.at(position) != u'\n')
                ++position;
            continue;
        }

        if (current == u'{') {
            ++braceDepth;
            tokens->append({VdfTokenKind::Separator, {}});
            ++position;
            continue;
        }
        if (current == u'}') {
            if (braceDepth == 0)
                return false;
            --braceDepth;
            tokens->append({VdfTokenKind::Separator, {}});
            ++position;
            continue;
        }

        if (current != u'"') {
            tokens->append({VdfTokenKind::Separator, {}});
            while (position < text.size() && !text.at(position).isSpace() &&
                   text.at(position) != u'{' && text.at(position) != u'}') {
                ++position;
            }
            continue;
        }

        ++position;
        QString value;
        bool closed = false;
        while (position < text.size()) {
            const QChar character = text.at(position++);
            if (character == u'"') {
                closed = true;
                break;
            }
            if (character != u'\\') {
                value.append(character);
                continue;
            }
            if (position == text.size())
                return false;

            const QChar escaped = text.at(position++);
            if (escaped == u'\\' || escaped == u'"')
                value.append(escaped);
            else if (escaped == u'n')
                value.append(u'\n');
            else if (escaped == u'r')
                value.append(u'\r');
            else if (escaped == u't')
                value.append(u'\t');
            else {
                // Valve paths normally escape backslashes as "\\\\". Keep
                // unknown escape sequences verbatim rather than losing data.
                value.append(u'\\');
                value.append(escaped);
            }
        }
        if (!closed)
            return false;
        tokens->append({VdfTokenKind::String, value});
    }

    return braceDepth == 0;
}

QStringList quotedValues(const QString &filePath, QStringView key)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QVector<VdfToken> tokens;
    if (!tokenizeVdf(file.readAll(), &tokens))
        return {};

    QStringList values;
    for (qsizetype index = 0; index < tokens.size();) {
        if (tokens.at(index).kind != VdfTokenKind::String) {
            ++index;
            continue;
        }
        if (index + 1 >= tokens.size() || tokens.at(index + 1).kind != VdfTokenKind::String) {
            ++index;
            continue;
        }
        if (tokens.at(index).value.compare(key, Qt::CaseInsensitive) == 0)
            values.append(tokens.at(index + 1).value);
        index += 2;
    }
    return values;
}

void appendExistingCanonicalDirectory(QStringList *paths, const QString &path)
{
    if (!QDir::isAbsolutePath(path))
        return;

    const QFileInfo info(QDir::cleanPath(path));
    if (!info.exists() || !info.isDir())
        return;

    const QString canonicalPath = info.canonicalFilePath();
    if (!canonicalPath.isEmpty() && !paths->contains(canonicalPath))
        paths->append(canonicalPath);
}

bool isSafeRelativePath(const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path))
        return false;
    const QString cleanPath = QDir::cleanPath(path);
    return cleanPath != QStringLiteral("..") && !cleanPath.startsWith(QStringLiteral("../"));
}

} // namespace

QStringList defaultSteamRoots(const QString &homeDirectory)
{
    QStringList roots;
    const QStringList candidates = {
        QDir(homeDirectory).filePath(QStringLiteral(".local/share/Steam")),
        QDir(homeDirectory).filePath(QStringLiteral(".steam/root")),
        QDir(homeDirectory).filePath(QStringLiteral(".steam/steam")),
    };
    for (const QString &candidate : candidates)
        appendExistingCanonicalDirectory(&roots, candidate);
    return roots;
}

QStringList defaultSteamRoots()
{
    return defaultSteamRoots(QDir::homePath());
}

QStringList steamLibraryPaths(const QStringList &steamRoots)
{
    QStringList libraries;
    for (const QString &rawRoot : steamRoots) {
        QStringList rootOnly;
        appendExistingCanonicalDirectory(&rootOnly, rawRoot);
        if (rootOnly.isEmpty())
            continue;

        const QString root = rootOnly.constFirst();
        appendExistingCanonicalDirectory(&libraries, root);
        const QStringList vdfPaths = {
            QDir(root).filePath(QStringLiteral("config/libraryfolders.vdf")),
            QDir(root).filePath(QStringLiteral("steamapps/libraryfolders.vdf")),
        };
        for (const QString &vdfPath : vdfPaths) {
            const QStringList declaredPaths = quotedValues(vdfPath, QStringLiteral("path"));
            for (const QString &declaredPath : declaredPaths)
                appendExistingCanonicalDirectory(&libraries, declaredPath);
        }
    }
    return libraries;
}

QString findSteamGameInstallDir(quint64 appId, const QString &executableName,
                                const QStringList &steamRoots)
{
    if (!isSafeRelativePath(executableName))
        return {};

    const QString manifestName = QStringLiteral("appmanifest_%1.acf").arg(appId);
    for (const QString &library : steamLibraryPaths(steamRoots)) {
        const QString manifestPath =
            QDir(library + QStringLiteral("/steamapps")).filePath(manifestName);
        const QStringList installDirs = quotedValues(manifestPath, QStringLiteral("installdir"));
        for (const QString &declaredInstallDir : installDirs) {
            if (!isSafeRelativePath(declaredInstallDir))
                continue;

            const QString installDir = QDir(library + QStringLiteral("/steamapps/common"))
                                           .filePath(QDir::cleanPath(declaredInstallDir));
            if (!QFileInfo(QDir(installDir).filePath(executableName)).isFile())
                continue;

            const QString canonicalPath = QFileInfo(installDir).canonicalFilePath();
            if (!canonicalPath.isEmpty())
                return canonicalPath;
        }
    }

    return {};
}

QString findRiseInstallDir(const QStringList &steamRoots)
{
    return findSteamGameInstallDir(1446780, QStringLiteral("MonsterHunterRise.exe"), steamRoots);
}

QString findRiseInstallDir()
{
    return findRiseInstallDir(defaultSteamRoots());
}

} // namespace mhw
