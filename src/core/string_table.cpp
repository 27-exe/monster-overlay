#include "string_table.h"

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
    const QString path = QStringLiteral(":/i18n/%1.json").arg(locale);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Missing locale: caller (main.cpp) logs a warning and the
        // singleton continues serving empty entries — tr() returns the
        // key string, so the UI shows the dot-path of the missing key.
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    // Recursive flatten: any object becomes a dot-path key, any string
    // becomes a value. _meta and other metadata keys land as regular
    // entries; that's fine — they're not "ui.*" so no caller looks them up.
    QHash<QString, QString> flat;
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