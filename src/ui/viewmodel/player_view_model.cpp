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

    return {masterRank, name, weaponId, partyCount};
}

} // namespace mhw
