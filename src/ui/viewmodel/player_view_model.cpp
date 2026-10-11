// SPDX-License-Identifier: Apache-2.0
#include "ui/viewmodel/player_view_model.h"

// PlayerPanel ViewModel implementation — the three blocks MOVED here
// out of PlayerPanel::update(const GameSnapshot &). Only the access
// layer changed: the members they wrote (`playerMR_`, `playerName_`,
// `weaponId_`, `partyCount_`) became locals of this function that are
// returned as PlayerIdentity, and the panel copies them into its own
// members. Not one condition, comparison, guard or order was touched.
//
// Hard rule, same as DamageViewModel / MonsterViewModel: no widget or
// painter dependency, only QtCore plus the pure data headers.

namespace mhw {

PlayerViewModel::PlayerIdentity PlayerViewModel::resolveIdentity(
    const GameSnapshot &snap) const
{
    // MR / name / weapon all come from the local player struct
    // (PlayerSnapshot) directly.  In the gathering hub the party
    // array is empty, but the player struct still holds valid name,
    // MR and weaponId so we can show them.
    int     masterRank = snap.player.masterRank;
    QString name       = snap.player.name;
    int     weaponId    = snap.player.weaponId;
    int     partyCount = snap.party.size();

    // When party has a local=true snapshot it carries the same fields;
    // prefer those in case the player struct lags by a poll.
    for (const auto &m : snap.party) {
        if (m.local) {
            if (m.masterRank > 0) masterRank = m.masterRank;
            if (!m.name.isEmpty())  name      = m.name;
            if (m.weaponId >= 0)    weaponId  = m.weaponId;
            break;
        }
    }

    // If we are not attached to a real process, reset demo mirrors so
    // the panel doesn't keep stale data from a previous frame.
    if (!snap.player.valid) {
        if (weaponId == 0 && name.isEmpty()) {
            // Nothing to show — player struct also returned nothing.
            weaponId = -1;
        }
    }

    // v1.0.0 note (recorded here after trying it the other way): the
    // 「title screen shows my name and MR」 fix is NOT this function's job.
    //
    // Tried: an early `if (snap.player.maxHealth <= 0) return empty;` guard
    // here, on the reasoning that name / masterRank / weaponId all live in
    // the save header and so answer before the player is in a quest. It
    // broke 53 of 68 checks in player-view-model-tests and was wrong on the
    // merits too: this is a pure identity transform, and its contract is
    // "what did the snapshot say about the hunter", not "is the hunter
    // interesting yet". A snapshot that carries a name and no HP is a
    // coherent thing to describe.
    //
    // The window that MOUNTS is the thing that has to decide, and it does,
    // in PlayerWeaponStripPanel::hasContent(). Same predicate, correct
    // layer — a transform asked to refuse valid data cannot be tested, and
    // cannot be reused.

    return {masterRank, name, weaponId, partyCount};
}

} // namespace mhw
