// SPDX-License-Identifier: Apache-2.0
#include "rise/rise_damage_reader.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>
#include <limits>
#include <utility>

namespace mhw {
namespace {

constexpr qint64 kFreshnessWindowMs = 5000;

bool parseOptionalString(const QJsonObject &object, const QString &key, QString *result)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
        return true;
    if (!value.isString())
        return false;
    *result = value.toString();
    return true;
}

bool parseBool(const QJsonObject &object, const QString &key,
               bool required, bool *result)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
        return !required;
    if (!value.isBool())
        return false;
    *result = value.toBool();
    return true;
}

bool parseInt(const QJsonObject &object, const QString &key,
              bool required, int *result)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
        return !required;
    if (!value.isDouble())
        return false;

    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < static_cast<double>(std::numeric_limits<int>::min())
        || number > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }

    *result = static_cast<int>(number);
    return true;
}

bool parseNonNegativeQint64(const QJsonObject &object, const QString &key,
                            bool required, qint64 *result)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
        return !required;
    if (!value.isDouble())
        return false;

    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0)
        return false;

    // static_cast from a floating-point value outside the destination range is
    // undefined. Compare first; qint64::max rounds to 2^63 as a double, so >=
    // is intentional and also catches that rounded boundary.
    if (number >= static_cast<double>(std::numeric_limits<qint64>::max())) {
        *result = std::numeric_limits<qint64>::max();
    } else {
        *result = static_cast<qint64>(number);
    }
    return true;
}

bool parseNonNegativeQuint64(const QJsonObject &object, const QString &key,
                             bool required, quint64 *result)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
        return !required;
    if (!value.isDouble())
        return false;

    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0)
        return false;

    if (number >= static_cast<double>(std::numeric_limits<quint64>::max())) {
        *result = std::numeric_limits<quint64>::max();
    } else {
        *result = static_cast<quint64>(number);
    }
    return true;
}

RiseDamageActorKind actorKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("player"))
        return RiseDamageActorKind::Player;
    if (kind == QStringLiteral("companion"))
        return RiseDamageActorKind::Companion;
    if (kind == QStringLiteral("pet"))
        return RiseDamageActorKind::Pet;
    if (kind == QStringLiteral("palico"))
        return RiseDamageActorKind::Palico;
    if (kind == QStringLiteral("palamute"))
        return RiseDamageActorKind::Palamute;
    return RiseDamageActorKind::Unknown;
}

QString actorKindKey(RiseDamageActorKind kind)
{
    switch (kind) {
    case RiseDamageActorKind::Player:
        return QStringLiteral("player");
    case RiseDamageActorKind::Companion:
        return QStringLiteral("companion");
    case RiseDamageActorKind::Pet:
        return QStringLiteral("pet");
    case RiseDamageActorKind::Palico:
        return QStringLiteral("palico");
    case RiseDamageActorKind::Palamute:
        return QStringLiteral("palamute");
    case RiseDamageActorKind::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString fallbackActorKey(const RiseDamageActor &actor)
{
    const QString kind = actorKindKey(actor.kind);
    if (actor.entityIndex >= 0) {
        return QStringLiteral("fallback:%1:entity:%2")
            .arg(kind)
            .arg(actor.entityIndex);
    }

    QByteArray identity;
    identity.reserve(128);
    identity += kind.toUtf8();
    identity += '\0';
    identity += QByteArray::number(actor.displaySlot);
    identity += '\0';
    identity += QByteArray::number(actor.ownerEntityIndex);
    identity += '\0';
    identity += QByteArray::number(actor.sourceTypeRaw);
    identity += '\0';
    identity += actor.name.toUtf8();
    identity += '\0';
    identity += actor.ownerName.toUtf8();
    const QByteArray digest = QCryptographicHash::hash(
        identity, QCryptographicHash::Sha256).toHex();
    return QStringLiteral("fallback:%1:sha256:%2")
        .arg(kind, QString::fromLatin1(digest));
}

bool parseV2Actor(const QJsonObject &object, RiseDamageActor *actor)
{
    QString kind;
    if (!parseOptionalString(object, QStringLiteral("key"), &actor->key)
        || !parseOptionalString(object, QStringLiteral("kind"), &kind)
        || !parseInt(object, QStringLiteral("entity_index"), false,
                     &actor->entityIndex)
        || !parseInt(object, QStringLiteral("display_slot"), false,
                     &actor->displaySlot)
        || !parseInt(object, QStringLiteral("owner_entity_index"), false,
                     &actor->ownerEntityIndex)
        || !parseInt(object, QStringLiteral("source_type_raw"), false,
                     &actor->sourceTypeRaw)
        || !parseOptionalString(object, QStringLiteral("name"), &actor->name)
        || !parseOptionalString(object, QStringLiteral("owner_name"),
                                &actor->ownerName)
        || !parseNonNegativeQint64(object, QStringLiteral("total"), false,
                                   &actor->total)
        || !parseNonNegativeQint64(object, QStringLiteral("physical"), false,
                                   &actor->physical)
        || !parseNonNegativeQint64(object, QStringLiteral("elemental"), false,
                                   &actor->elemental)
        || !parseNonNegativeQuint64(object, QStringLiteral("hits"), false,
                                    &actor->hits)
        || !parseBool(object, QStringLiteral("is_local"), false, &actor->local)
        || !parseBool(object, QStringLiteral("present"), false, &actor->present)
        || !parseBool(object, QStringLiteral("connected"), false,
                      &actor->connected)) {
        return false;
    }

    actor->kind = actorKindFromString(kind);
    if (actor->key.isEmpty())
        actor->key = fallbackActorKey(*actor);
    return true;
}

bool appendFirstForKey(RiseDamageActor actor, QSet<QString> *keys,
                       QVector<RiseDamageActor> *actors)
{
    if (keys->contains(actor.key))
        return true;
    keys->insert(actor.key);
    actors->append(std::move(actor));
    return true;
}

bool parseV2(const QJsonObject &root, RiseDamageSnapshot *snapshot)
{
    if (!parseNonNegativeQint64(root, QStringLiteral("timestamp_ms"), true,
                                &snapshot->timestampMs)
        || !parseNonNegativeQuint64(root, QStringLiteral("seq"), true,
                                    &snapshot->sequence)) {
        return false;
    }

    const QJsonValue questValue = root.value(QStringLiteral("quest"));
    if (!questValue.isObject())
        return false;
    const QJsonObject quest = questValue.toObject();
    if (!parseBool(quest, QStringLiteral("active"), true,
                   &snapshot->questActive)
        || !parseInt(quest, QStringLiteral("state"), true,
                     &snapshot->questState)
        || !parseInt(quest, QStringLiteral("epoch"), true,
                     &snapshot->questEpoch)
        || !parseBool(quest, QStringLiteral("state_valid"), false,
                      &snapshot->questStateValid)
        || !parseBool(quest, QStringLiteral("collection_paused"), false,
                      &snapshot->collectionPaused)
        || !parseBool(quest, QStringLiteral("training"), false,
                      &snapshot->training)) {
        return false;
    }

    const QJsonValue entitiesValue = root.value(QStringLiteral("entities"));
    if (!entitiesValue.isArray())
        return false;

    QSet<QString> keys;
    for (const QJsonValue &value : entitiesValue.toArray()) {
        if (!value.isObject())
            return false;
        RiseDamageActor actor;
        if (!parseV2Actor(value.toObject(), &actor))
            return false;
        appendFirstForKey(std::move(actor), &keys, &snapshot->actors);
    }
    return true;
}

bool parseV1(const QJsonObject &root, RiseDamageSnapshot *snapshot)
{
    qint64 timestampSeconds = 0;
    if (!parseNonNegativeQint64(root, QStringLiteral("timestamp"), true,
                                &timestampSeconds)
        || !parseBool(root, QStringLiteral("quest_active"), true,
                      &snapshot->questActive)) {
        return false;
    }

    if (timestampSeconds > std::numeric_limits<qint64>::max() / 1000) {
        snapshot->timestampMs = std::numeric_limits<qint64>::max();
    } else {
        snapshot->timestampMs = timestampSeconds * 1000;
    }
    snapshot->questState = snapshot->questActive ? 2 : 0;

    const QJsonValue playersValue = root.value(QStringLiteral("players"));
    if (!playersValue.isArray())
        return false;

    QSet<QString> keys;
    for (const QJsonValue &value : playersValue.toArray()) {
        if (!value.isObject())
            return false;

        const QJsonObject object = value.toObject();
        RiseDamageActor actor;
        actor.kind = RiseDamageActorKind::Player;
        actor.entityIndex = 0;
        actor.displaySlot = 0;
        if (!parseInt(object, QStringLiteral("slot"), false,
                      &actor.entityIndex)
            || !parseOptionalString(object, QStringLiteral("name"), &actor.name)
            || !parseNonNegativeQint64(object, QStringLiteral("total"), false,
                                       &actor.total)
            || !parseNonNegativeQint64(object, QStringLiteral("physical"), false,
                                       &actor.physical)
            || !parseNonNegativeQint64(object, QStringLiteral("elemental"), false,
                                       &actor.elemental)
            || !parseNonNegativeQuint64(object, QStringLiteral("hits"), false,
                                        &actor.hits)
            || !parseBool(object, QStringLiteral("is_local"), false,
                          &actor.local)) {
            return false;
        }
        actor.displaySlot = actor.entityIndex;
        actor.key = fallbackActorKey(actor);
        appendFirstForKey(std::move(actor), &keys, &snapshot->actors);
    }
    return true;
}

bool isFresh(qint64 timestampMs)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    return timestampMs >= now - kFreshnessWindowMs
        && timestampMs <= now + kFreshnessWindowMs;
}

} // namespace

RiseDamageReader::RiseDamageReader(QString path)
    : path_(std::move(path))
{
}

bool RiseDamageReader::update()
{
    snapshot_ = {};

    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject root = document.object();
    int version = 0;
    if (!parseInt(root, QStringLiteral("version"), true, &version))
        return false;

    RiseDamageSnapshot parsed;
    const bool supported = version == 1 ? parseV1(root, &parsed)
                         : version == 2 ? parseV2(root, &parsed)
                                        : false;
    if (!supported || !isFresh(parsed.timestampMs))
        return false;

    parsed.valid = true;
    snapshot_ = std::move(parsed);
    return true;
}

} // namespace mhw
