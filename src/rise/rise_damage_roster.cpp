// SPDX-License-Identifier: Apache-2.0

#include "rise/rise_damage_roster.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace mhw {

namespace {

using RosterLookup = std::unordered_map<int, const PartyMemberSnapshot *>;

RosterLookup buildRosterLookup(const QVector<PartyMemberSnapshot> &roster)
{
    RosterLookup lookup;
    std::unordered_set<int> ambiguous;
    lookup.reserve(static_cast<std::size_t>(roster.size()));

    for (const PartyMemberSnapshot &member : roster) {
        const int entityIndex = member.effectiveEntityIndex();
        if (entityIndex < 0 || ambiguous.contains(entityIndex))
            continue;

        const auto [position, inserted] = lookup.emplace(entityIndex, &member);
        if (!inserted) {
            lookup.erase(position);
            ambiguous.insert(entityIndex);
        }
    }
    return lookup;
}

const PartyMemberSnapshot *findRosterMember(const RosterLookup &lookup,
                                             int entityIndex)
{
    if (entityIndex < 0)
        return nullptr;
    const auto found = lookup.find(entityIndex);
    return found == lookup.end() ? nullptr : found->second;
}

RiseDamageActorKind damageKind(PartyMemberKind kind)
{
    return kind == PartyMemberKind::Companion
        ? RiseDamageActorKind::Companion
        : RiseDamageActorKind::Player;
}

void applyMemberIdentity(RiseDamageActor &actor,
                         const PartyMemberSnapshot &member)
{
    actor.kind = damageKind(member.kind);
    actor.displaySlot = member.slot;
    actor.local = member.local;
    actor.weaponId = member.weaponId;
    actor.masterRank = member.masterRank;
    if (!member.name.isEmpty())
        actor.name = member.name;
}

void applyPetOwnerIdentity(RiseDamageActor &actor,
                           const PartyMemberSnapshot &owner,
                           bool plainName = true)
{
    actor.displaySlot = owner.slot;
    actor.local = owner.local;
    if (owner.name.isEmpty())
        return;

    actor.ownerName = owner.name;
    // The Lua producer aggregates every pet source for one owner into one actor.
    // The owner name is therefore the honest row label: it identifies the
    // aggregate without pretending the source was specifically a cat or dog.
    // Follower buddy rows keep the owner name plus the panel fallback label
    // ("Fiorayne · pet") so they never read as the follower's own damage row.
    if (plainName)
        actor.name = owner.name;
}

} // namespace

void enrichRiseDamageSnapshot(RiseDamageSnapshot &damage,
                              const GameSnapshot &game)
{
    if (game.game != GameId::Rise || game.party.isEmpty())
        return;

    const RosterLookup roster = buildRosterLookup(game.party);
    if (roster.empty())
        return;

    // Solo and follower sessions have exactly one human hunter. Live sessions
    // prove the owner-less pet streams belong to that human (raw entity 4:
    // second buddy) or to the followers (raw 5/6: follower-slot 0/1 buddies).
    // With two or more humans the streams are not provably ours; leave the
    // producers untouched then.
    const PartyMemberSnapshot *soleHuman = nullptr;
    int humanCount = 0;
    for (const PartyMemberSnapshot &member : game.party) {
        if (member.kind != PartyMemberKind::Player)
            continue;
        ++humanCount;
        soleHuman = &member;
    }
    if (humanCount != 1 || !soleHuman->local)
        soleHuman = nullptr;

    QVector<const PartyMemberSnapshot *> companions;
    for (const PartyMemberSnapshot &member : game.party) {
        if (member.kind == PartyMemberKind::Companion)
            companions.append(&member);
    }
    std::sort(companions.begin(), companions.end(),
              [](const PartyMemberSnapshot *left,
                 const PartyMemberSnapshot *right) {
                  return left->effectiveEntityIndex()
                      < right->effectiveEntityIndex();
              });
    const bool followerLayout = soleHuman != nullptr && !companions.isEmpty();

    for (RiseDamageActor &actor : damage.actors) {
        if (isRisePetKind(actor.kind)) {
            if (const PartyMemberSnapshot *owner =
                    findRosterMember(roster, actor.ownerEntityIndex)) {
                applyPetOwnerIdentity(actor, *owner);
                continue;
            }
            if (actor.ownerEntityIndex < 0) {
                const int rawIndex = actor.entityIndex;
                if (rawIndex == 4 && soleHuman != nullptr) {
                    // The local hunter's second buddy.
                    actor.ownerEntityIndex =
                        soleHuman->effectiveEntityIndex();
                    applyPetOwnerIdentity(actor, *soleHuman);
                } else if (followerLayout && rawIndex >= 5 && rawIndex <= 6) {
                    const int position = rawIndex - 5;
                    if (position < companions.size()) {
                        const PartyMemberSnapshot *companion =
                            companions[position];
                        actor.ownerEntityIndex =
                            companion->effectiveEntityIndex();
                        applyPetOwnerIdentity(actor, *companion,
                                              /*plainName=*/false);
                    }
                } else if (!followerLayout && rawIndex >= 5 && rawIndex <= 9) {
                    // Sessions without measured follower layouts (e.g.
                    // teammate parties) keep the historical owner=id-5 join.
                    if (const PartyMemberSnapshot *owner =
                            findRosterMember(roster, rawIndex - 5)) {
                        actor.ownerEntityIndex = rawIndex - 5;
                        applyPetOwnerIdentity(actor, *owner);
                    }
                }
            }
            continue;
        }

        // Any non-pet damage actor with an unambiguous roster entity is a
        // player/follower, even when the producer could not classify its kind.
        if (const PartyMemberSnapshot *member =
                findRosterMember(roster, actor.entityIndex)) {
            applyMemberIdentity(actor, *member);
        }
    }
}

} // namespace mhw
