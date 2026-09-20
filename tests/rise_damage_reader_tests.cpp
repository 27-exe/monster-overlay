// SPDX-License-Identifier: Apache-2.0

#include "rise/rise_damage_reader.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <limits>

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition) {
        std::cout << "PASS: " << message << '\n';
    } else {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool writeFixture(const QString &path, const QByteArray &json)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    return file.write(json) == json.size() && file.flush();
}

QByteArray v2Document(qint64 timestampMs,
                      const QByteArray &entities,
                      const QByteArray &quest = R"({"active":true,"state":2,"epoch":7,"state_valid":false,"collection_paused":true,"training":true})",
                      const QByteArray &sequence = "42")
{
    return QByteArray("{\"version\":2,\"timestamp_ms\":")
        + QByteArray::number(timestampMs)
        + ",\"seq\":" + sequence
        + ",\"quest\":" + quest
        + ",\"entities\":" + entities + '}';
}

const mhw::RiseDamageActor *actorByName(const mhw::RiseDamageSnapshot &snapshot,
                                        const QString &name)
{
    for (const auto &actor : snapshot.actors) {
        if (actor.name == name)
            return &actor;
    }
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    check(temp.isValid(), "temporary directory is available");
    if (!temp.isValid())
        return 2;

    const QString path = temp.filePath(QStringLiteral("mhr_damage.json"));
    mhw::RiseDamageReader reader(path);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QByteArray allKinds = R"([
        {"key":"player:7","kind":"player","entity_index":7,"display_slot":0,
         "owner_entity_index":-1,"source_type_raw":1,"name":"Hunter",
         "owner_name":"","total":1234,"physical":1000,"elemental":234,
         "hits":17,"is_local":true,"present":false,"connected":false},
        {"key":"companion:8","kind":"companion","entity_index":8,"display_slot":1,
         "owner_entity_index":7,"source_type_raw":2,"name":"Fiorayne",
         "owner_name":"Hunter","total":300,"physical":250,"elemental":50,
         "hits":4,"is_local":false,"present":true,"connected":true},
        {"key":"pet:9","kind":"pet","name":"Buddy","total":10},
        {"key":"palico:10","kind":"palico","name":"Cat","total":11},
        {"key":"palamute:11","kind":"palamute","name":"Dog","total":12},
        {"key":"unknown:12","kind":"something-new","name":"Mystery","total":13}
    ])";
    check(writeFixture(path, v2Document(now, allKinds)), "v2 fixture writes");
    check(reader.update(), "v2 document parses");

    const auto &v2 = reader.snapshot();
    check(v2.valid && v2.timestampMs == now && v2.sequence == 42,
          "v2 root timestamp and sequence map exactly");
    check(v2.questActive && v2.questState == 2 && v2.questEpoch == 7
              && !v2.questStateValid && v2.collectionPaused && v2.training,
          "v2 quest activity, validity, pause, and training flags map exactly");
    check(v2.actors.size() == 6, "v2 retains all actor kinds for downstream filtering");
    if (v2.actors.size() == 6) {
        const auto &player = v2.actors[0];
        check(player.key == QStringLiteral("player:7")
                  && player.kind == mhw::RiseDamageActorKind::Player
                  && player.entityIndex == 7 && player.displaySlot == 0
                  && player.ownerEntityIndex == -1 && player.sourceTypeRaw == 1,
              "v2 player identity fields map exactly");
        check(player.name == QStringLiteral("Hunter") && player.ownerName.isEmpty()
                  && player.total == 1234 && player.physical == 1000
                  && player.elemental == 234 && player.hits == 17,
              "v2 player text and damage fields map exactly");
        check(player.local && !player.present && !player.connected,
              "v2 player flags map exactly");
        check(v2.actors[1].kind == mhw::RiseDamageActorKind::Companion
                  && v2.actors[2].kind == mhw::RiseDamageActorKind::Pet
                  && v2.actors[3].kind == mhw::RiseDamageActorKind::Palico
                  && v2.actors[4].kind == mhw::RiseDamageActorKind::Palamute
                  && v2.actors[5].kind == mhw::RiseDamageActorKind::Unknown,
              "all documented kind strings map without guessing unknown values");
    }

    const qint64 nowSeconds = QDateTime::currentSecsSinceEpoch();
    const QByteArray v1 = QByteArray("{\"version\":1,\"timestamp\":")
        + QByteArray::number(nowSeconds)
        + R"(,"quest_active":true,"players":[
            {"slot":2,"name":"Legacy Hunter","total":101,"physical":80,
             "elemental":21,"hits":6,"is_local":true}
          ]})";
    check(writeFixture(path, v1), "v1 fixture writes");
    check(reader.update(), "legacy v1 document still parses");
    const auto &legacy = reader.snapshot();
    check(legacy.valid && legacy.timestampMs == nowSeconds * 1000,
          "v1 seconds are converted to milliseconds");
    check(legacy.questActive && legacy.actors.size() == 1,
          "v1 quest and player list convert to the shared snapshot");
    if (legacy.actors.size() == 1) {
        const auto &player = legacy.actors[0];
        check(player.kind == mhw::RiseDamageActorKind::Player
                  && player.entityIndex == 2 && player.displaySlot == 2,
              "v1 entries become Player actors with stable slot identity");
        check(!player.key.isEmpty() && player.name == QStringLiteral("Legacy Hunter")
                  && player.total == 101 && player.physical == 80
                  && player.elemental == 21 && player.hits == 6 && player.local,
              "v1 player values and generated key survive conversion");
    }

    check(writeFixture(path,
                       v2Document(QDateTime::currentMSecsSinceEpoch() - 6000, "[]")),
          "stale fixture writes");
    check(!reader.update() && !reader.snapshot().valid,
          "documents older than five seconds are rejected");
    check(writeFixture(path,
                       v2Document(QDateTime::currentMSecsSinceEpoch() + 6000, "[]")),
          "future fixture writes");
    check(!reader.update() && !reader.snapshot().valid,
          "documents more than five seconds in the future are rejected");

    const QByteArray huge = R"([
        {"key":"huge","kind":"player","total":1e100,
         "physical":1e99,"elemental":1e98,"hits":1e100}
    ])";
    check(writeFixture(path, v2Document(QDateTime::currentMSecsSinceEpoch(), huge)),
          "large-number fixture writes");
    check(reader.update(), "finite non-negative large values parse");
    if (reader.snapshot().actors.size() == 1) {
        const auto &actor = reader.snapshot().actors[0];
        check(actor.total == std::numeric_limits<qint64>::max()
                  && actor.physical == std::numeric_limits<qint64>::max()
                  && actor.elemental == std::numeric_limits<qint64>::max(),
              "damage values clamp to qint64");
        check(actor.hits == std::numeric_limits<quint64>::max(),
              "hit count clamps to quint64");
    } else {
        check(false, "large-number snapshot contains its actor");
    }

    for (const QByteArray &badEntity : {
             QByteArray(R"([{"key":"bad-total","kind":"player","total":-1}])"),
             QByteArray(R"([{"key":"bad-physical","kind":"player","physical":-1}])"),
             QByteArray(R"([{"key":"bad-elemental","kind":"player","elemental":-1}])"),
             QByteArray(R"([{"key":"bad-hits","kind":"player","hits":-1}])"),
             QByteArray(R"([{"key":"bad-type","kind":"player","total":"12"}])")}) {
        check(writeFixture(path,
                           v2Document(QDateTime::currentMSecsSinceEpoch(), badEntity)),
              "invalid numeric fixture writes");
        check(!reader.update() && !reader.snapshot().valid,
              "negative or non-numeric counters reject the whole snapshot");
    }
    check(writeFixture(path,
                       v2Document(QDateTime::currentMSecsSinceEpoch(),
                                  R"([{"key":"overflow","kind":"player","total":1e309}])")),
          "non-finite-number fixture writes");
    check(!reader.update(), "non-finite JSON numbers are rejected");

    const QByteArray noKey = R"([
        {"kind":"companion","entity_index":9,"display_slot":1,
         "owner_entity_index":4,"source_type_raw":22,
         "name":"Utsushi","owner_name":"Hunter","total":55}
    ])";
    check(writeFixture(path, v2Document(QDateTime::currentMSecsSinceEpoch(), noKey)),
          "missing-key fixture writes");
    check(reader.update(), "missing actor key receives a fallback");
    const auto *firstFallbackActor = actorByName(reader.snapshot(), QStringLiteral("Utsushi"));
    const QString fallbackKey = firstFallbackActor ? firstFallbackActor->key : QString{};
    check(!fallbackKey.isEmpty(), "generated actor fallback key is non-empty");

    const QByteArray reorderedNoKey = R"([
        {"key":"other","kind":"player","entity_index":1,"name":"Other"},
        {"kind":"companion","entity_index":9,"display_slot":1,
         "owner_entity_index":4,"source_type_raw":22,
         "name":"Utsushi","owner_name":"Hunter","total":56}
    ])";
    check(writeFixture(path,
                       v2Document(QDateTime::currentMSecsSinceEpoch(), reorderedNoKey)),
          "reordered missing-key fixture writes");
    check(reader.update(), "reordered missing-key document parses");
    const auto *secondFallbackActor = actorByName(reader.snapshot(), QStringLiteral("Utsushi"));
    check(secondFallbackActor && secondFallbackActor->key == fallbackKey,
          "fallback key is stable across JSON array reordering");

    const QByteArray distinctMissingKeys = R"([
        {"kind":"pet","display_slot":0,"owner_entity_index":7,
         "source_type_raw":31,"name":"Buddy A","owner_name":"Hunter"},
        {"kind":"pet","display_slot":0,"owner_entity_index":7,
         "source_type_raw":32,"name":"Buddy B","owner_name":"Hunter"},
        {"kind":"pet","display_slot":1,"owner_entity_index":7,
         "source_type_raw":31,"name":"Buddy A","owner_name":"Hunter"}
    ])";
    check(writeFixture(path,
                       v2Document(QDateTime::currentMSecsSinceEpoch(),
                                  distinctMissingKeys)),
          "distinct missing-key fixture writes");
    check(reader.update(), "distinct missing-key actors parse");
    check(reader.snapshot().actors.size() == 3
              && reader.snapshot().actors[0].key
                     != reader.snapshot().actors[1].key
              && reader.snapshot().actors[0].key
                     != reader.snapshot().actors[2].key
              && reader.snapshot().actors[1].key
                     != reader.snapshot().actors[2].key,
          "fallback keys distinguish source and slot identity");

    const QByteArray duplicates = R"([
        {"key":"duplicate","kind":"player","name":"First","total":10},
        {"key":"duplicate","kind":"player","name":"Second","total":99}
    ])";
    check(writeFixture(path,
                       v2Document(QDateTime::currentMSecsSinceEpoch(), duplicates)),
          "duplicate-key fixture writes");
    check(reader.update(), "duplicate keys do not invalidate an otherwise valid snapshot");
    check(reader.snapshot().actors.size() == 1
              && reader.snapshot().actors[0].name == QStringLiteral("First")
              && reader.snapshot().actors[0].total == 10,
          "duplicate keys deterministically keep the first entity");

    const QByteArray unsupported = QByteArray("{\"version\":3,\"timestamp_ms\":")
        + QByteArray::number(QDateTime::currentMSecsSinceEpoch()) + '}';
    check(writeFixture(path, unsupported), "unsupported-version fixture writes");
    check(!reader.update(), "unsupported protocol versions are rejected");

    check(writeFixture(path,
                       QByteArray("{\"version\":2,\"timestamp_ms\":")
                           + QByteArray::number(QDateTime::currentMSecsSinceEpoch())
                           + R"(,"seq":1,"entities":[]})"),
          "missing-quest fixture writes");
    check(!reader.update(), "v2 quest object is required");
    check(writeFixture(path,
                       v2Document(QDateTime::currentMSecsSinceEpoch(), "{}")),
          "non-array entities fixture writes");
    check(!reader.update(), "v2 entities must be an array");

    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
