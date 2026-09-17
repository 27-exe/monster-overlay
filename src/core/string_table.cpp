#include "string_table.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringLiteral>

#include <utility>

namespace mhw {

StringTable& StringTable::instance()
{
    static StringTable s;
    return s;
}

bool StringTable::load(const QString& locale)
{
    // Each locale is a directory of domain files (see the header for the
    // layout). Merge them into one flat table; a missing directory or an
    // all-malformed directory fails the load and leaves the previous
    // table untouched — tr() then keeps returning the old locale (or the
    // key strings when nothing has ever loaded).
    const QDir dir(QStringLiteral(":/i18n/%1").arg(locale));
    if (!dir.exists())
        return false;
    const QStringList files =
        dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);

    QHash<QString, QString> flat;
    int loadedFiles = 0;
    for (const QString& name : files) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QByteArray data = f.readAll();
        f.close();

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning("StringTable: %s is not valid JSON (%s); skipped",
                     qPrintable(f.fileName()), qPrintable(err.errorString()));
            continue;
        }

        // Recursive flatten: any object becomes a dot-path key, any string
        // becomes a value. _meta and other metadata keys land as regular
        // entries; that's fine — they're not looked up by callers.
        auto visit = [&](auto&& self, const QJsonObject &obj, const QString &prefix) -> void {
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                const QString key = prefix.isEmpty() ? it.key()
                                                     : prefix + QLatin1Char('.') + it.key();
                if (it.value().isObject())
                    self(self, it.value().toObject(), key);
                else if (it.value().isString())
                    flat.insert(key, it.value().toString());
            }
        };
        visit(visit, doc.object(), QString());
        ++loadedFiles;
    }
    if (loadedFiles == 0)
        return false;

    entries_ = std::move(flat);
    currentLocale_ = locale;
    return true;
}

QString StringTable::tr(const QString& key) const
{
    const auto it = entries_.constFind(key);
    if (it != entries_.cend())
        return it.value();
    // Missing key: return the key itself so it shows up visibly in the
    // UI (better diagnostic than blank during development). Caller will
    // see dot-paths in the overlay if a translation is missing.
    return key;
}

} // namespace mhw
