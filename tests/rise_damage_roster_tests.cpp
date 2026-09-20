// SPDX-License-Identifier: Apache-2.0

#include "core/game_snapshot.h"
#include "rise/rise_damage_roster.h"

#include <QVector>

#include <cstdio>
#include <utility>

namespace {

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,      \
                         #condition);                                          \
            return false;                                                      \
        }                                                                      \
    } while (false)

mhw::PartyMemberSnapshot rosterMember(QString name, int entityIndex,
                                      int displaySlot, bool local,
                                      mhw::PartyMemberKind kind)
{
    mhw::PartyMemberSnapshot member;
    member.name = std::move(name);
    member.entityIndex = entityIndex;
    member.slot = displaySlot;
    member.local = local;
    member.kind = kind;
    return member;
}

mhw::RiseDamageActor damageActor(QString key, mhw::RiseDamageActorKind kind,
                                 int entityIndex, int ownerEntityIndex,
                                 int displaySlot, bool local, QString name,
                                 QString ownerName = {})
{
    mhw::RiseDamageActor actor;
    actor.key = std::move(key);
    actor.kind = kind;
    actor.entityIndex = entityIndex;
    actor.ownerEntityIndex = ownerEntityIndex;
    actor.displaySlot = displaySlot;
    actor.local = local;
    actor.name = std::move(name);
    actor.ownerName = std::move(ownerName);
    actor.total = 1234;
    return actor;
}

bool joinsPlayersAndCompanionsByEntityIndex()
{
    mhw::GameSnapshot game;
    game.game = mhw::GameId::Rise;
    game.party = {
        rosterMember(QStringLiteral("Remote"), 0, 3, false,
                     mhw::PartyMemberKind::Player),
        rosterMember(QStringLiteral("Self"), 2, 0, true,
                     mhw::PartyMemberKind::Player),
        rosterMember(QStringLiteral("Fiorayne"), 4, 1, false,
                     mhw::PartyMemberKind::Companion),
    };

    mhw::RiseDamageSnapshot damage;
    damage.actors = {
        damageActor(QStringLiteral("player:0"),
                    mhw::RiseDamageActorKind::Unknown,
                    0, -1, 88, true, QStringLiteral("producer remote")),
        damageActor(QStringLiteral("player:2"),
                    mhw::RiseDamageActorKind::Player,
                    2, -1, 77, false, QStringLiteral("producer self")),
        damageActor(QStringLiteral("player:4"),
                    mhw::RiseDamageActorKind::Player,
                    4, -1, 66, true, QStringLiteral("producer follower")),
    };

    mhw::enrichRiseDamageSnapshot(damage, game);

    CHECK(damage.actors[0].kind == mhw::RiseDamageActorKind::Player);
    CHECK(damage.actors[0].displaySlot == 3);
    CHECK(!damage.actors[0].local);
    CHECK(damage.actors[0].name == QStringLiteral("Remote"));

    // The local hunter is entity 2 in this fixture. Entity zero must not win
    // simply because the producer historically treated it as local.
    CHECK(damage.actors[1].displaySlot == 0);
    CHECK(damage.actors[1].local);
    CHECK(damage.actors[1].name == QStringLiteral("Self"));

    CHECK(damage.actors[2].kind == mhw::RiseDamageActorKind::Companion);
    CHECK(damage.actors[2].displaySlot == 1);
    CHECK(!damage.actors[2].local);
    CHECK(damage.actors[2].name == QStringLiteral("Fiorayne"));
    return true;
}

bool joinsPetToOwnerWithoutInventingSpecies()
{
    mhw::GameSnapshot game;
    game.game = mhw::GameId::Rise;
    game.party = {
        rosterMember(QStringLiteral("Self"), 2, 0, true,
                     mhw::PartyMemberKind::Player),
    };

    mhw::RiseDamageSnapshot damage;
    damage.actors = {
        damageActor(QStringLiteral("pet:2"), mhw::RiseDamageActorKind::Pet,
                    7, 2, 91, false, QStringLiteral(""),
                    QStringLiteral("producer owner")),
    };

    mhw::enrichRiseDamageSnapshot(damage, game);

    const auto &pet = damage.actors[0];
    CHECK(pet.kind == mhw::RiseDamageActorKind::Pet);
    CHECK(pet.displaySlot == 0);
    CHECK(pet.local);
    CHECK(pet.ownerName == QStringLiteral("Self"));
    // Pet events are one owner aggregate. The owner is a truthful display
    // identity; assigning Palico/Palamute or an individual pet name is not.
    CHECK(pet.name == QStringLiteral("Self"));
    return true;
}

bool preservesProducerDataWhenRosterCannotJoin()
{
    mhw::GameSnapshot game;
    game.game = mhw::GameId::Rise;
    game.party = {
        rosterMember(QStringLiteral("Known"), 1, 1, false,
                     mhw::PartyMemberKind::Player),
    };

    const mhw::RiseDamageActor unmatchedPlayer = damageActor(
        QStringLiteral("player:0"), mhw::RiseDamageActorKind::Player,
        0, -1, 9, false, QStringLiteral("Producer zero"));
    const mhw::RiseDamageActor unmatchedPet = damageActor(
        QStringLiteral("pet:3"), mhw::RiseDamageActorKind::Palico,
        8, 3, 8, true, QStringLiteral("Producer pet"),
        QStringLiteral("Producer owner"));

    mhw::RiseDamageSnapshot damage;
    damage.actors = {unmatchedPlayer, unmatchedPet};
    mhw::enrichRiseDamageSnapshot(damage, game);

    CHECK(damage.actors[0].kind == unmatchedPlayer.kind);
    CHECK(damage.actors[0].displaySlot == unmatchedPlayer.displaySlot);
    CHECK(damage.actors[0].local == unmatchedPlayer.local);
    CHECK(damage.actors[0].name == unmatchedPlayer.name);
    CHECK(!damage.actors[0].local); // never promote missing entity zero

    CHECK(damage.actors[1].kind == unmatchedPet.kind);
    CHECK(damage.actors[1].displaySlot == unmatchedPet.displaySlot);
    CHECK(damage.actors[1].local == unmatchedPet.local);
    CHECK(damage.actors[1].name == unmatchedPet.name);
    CHECK(damage.actors[1].ownerName == unmatchedPet.ownerName);
    return true;
}

bool refusesAmbiguousRosterIdentity()
{
    mhw::GameSnapshot game;
    game.game = mhw::GameId::Rise;
    game.party = {
        rosterMember(QStringLiteral("First"), 1, 0, true,
                     mhw::PartyMemberKind::Player),
        rosterMember(QStringLiteral("Second"), 1, 1, false,
                     mhw::PartyMemberKind::Player),
    };

    const mhw::RiseDamageActor producer = damageActor(
        QStringLiteral("player:1"), mhw::RiseDamageActorKind::Unknown,
        1, -1, 7, false, QStringLiteral("Producer"));
    mhw::RiseDamageSnapshot damage;
    damage.actors = {producer};

    mhw::enrichRiseDamageSnapshot(damage, game);
    CHECK(damage.actors[0].kind == producer.kind);
    CHECK(damage.actors[0].displaySlot == producer.displaySlot);
    CHECK(damage.actors[0].name == producer.name);
    return true;
}

} // namespace

int main()
{
    if (!joinsPlayersAndCompanionsByEntityIndex())
        return 1;
    if (!joinsPetToOwnerWithoutInventingSpecies())
        return 1;
    if (!preservesProducerDataWhenRosterCannotJoin())
        return 1;
    if (!refusesAmbiguousRosterIdentity())
        return 1;

    std::fprintf(stderr, "PASS Rise damage roster enrichment\n");
    return 0;
}
