// SPDX-License-Identifier: Apache-2.0

#include "rise/rise_damage_roster.h"

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
    if (!member.name.isEmpty())
        actor.name = member.name;
}

void applyPetOwnerIdentity(RiseDamageActor &actor,
                           const PartyMemberSnapshot &owner)
{
    actor.displaySlot = owner.slot;
    actor.local = owner.local;
    if (owner.name.isEmpty())
        return;

    actor.ownerName = owner.name;
    // The Lua producer aggregates every pet source for one owner into one actor.
    // The owner name is therefore the honest row label: it identifies the
    // aggregate without pretending the source was specifically a cat or dog.
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

    for (RiseDamageActor &actor : damage.actors) {
        if (isRisePetKind(actor.kind)) {
            if (const PartyMemberSnapshot *owner =
                    findRosterMember(roster, actor.ownerEntityIndex)) {
                applyPetOwnerIdentity(actor, *owner);
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
