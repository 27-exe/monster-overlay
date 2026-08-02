// SPDX-License-Identifier: Apache-2.0
#include "rise/rise_damage_reader.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <ctime>

namespace mhw {

RiseDamageReader::RiseDamageReader(QString path)
    : path_(std::move(path))
{
}

bool RiseDamageReader::update()
{
    snapshot_.valid = false;

    QFile file(path_);
    if (!file.exists())
        return false;

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();

    // Version gate
    if (root.value(QStringLiteral("version")).toInt() != 1)
        return false;

    // Staleness check: discard if older than 5 seconds
    const qint64 ts = static_cast<qint64>(root.value(QStringLiteral("timestamp")).toDouble());
    const qint64 now = static_cast<qint64>(std::time(nullptr));
    if (now - ts > 5)
        return false;

    snapshot_.timestamp = ts;
    snapshot_.questActive = root.value(QStringLiteral("quest_active")).toBool();
    snapshot_.players.clear();

    const QJsonArray players = root.value(QStringLiteral("players")).toArray();
    for (const QJsonValue &v : players) {
        const QJsonObject obj = v.toObject();
        RiseDamageEntry entry;
        entry.slot      = obj.value(QStringLiteral("slot")).toInt();
        entry.name      = obj.value(QStringLiteral("name")).toString();
        entry.total     = static_cast<float>(obj.value(QStringLiteral("total")).toDouble());
        entry.physical  = static_cast<float>(obj.value(QStringLiteral("physical")).toDouble());
        entry.elemental = static_cast<float>(obj.value(QStringLiteral("elemental")).toDouble());
        entry.hits      = obj.value(QStringLiteral("hits")).toInt();
        entry.isLocal   = obj.value(QStringLiteral("is_local")).toBool();
        snapshot_.players.append(entry);
    }

    snapshot_.valid = true;
    return true;
}

} // namespace mhw
