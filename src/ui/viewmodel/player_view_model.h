// SPDX-License-Identifier: Apache-2.0
#pragma once

// PlayerPanel ViewModel — the player-identity resolver.
//
// The third MVVM extraction after DamageViewModel / MonsterViewModel. It
// targets the three blocks that used to sit inline in
// PlayerPanel::update(const GameSnapshot &): the initial mirroring of
// (masterRank, name, weaponId, partyCount) from the player struct, the
// party-override loop that prefers the local party member's fields when
// the player struct lags a poll, and the not-attached fallback that resets
// weaponId to -1 when there is genuinely nothing to show.
//
// Those three blocks are a pure data transformation: GameSnapshot in,
// four values out. They carried no widget dependency except by the
// company they kept — they lived in a 1541-line file whose own includes
// drag in QPainter, QPainterPath, QFontMetrics and QLinearGradient, which
// is why a decision this visible at every poll had zero automated
// coverage. Moving them here makes them reachable from a plain logic test.
//
// Hard rule, same as DamageViewModel / MonsterViewModel: this header and
// its .cpp carry NO widget or painter dependency. It includes only QtCore
// (QString) plus the pure data headers that define GameSnapshot and
// PlayerSnapshot. Nothing here knows a QWidget, a QPainter or a canvas
// exists.
//
// Every condition, every guard, every comparison and every order is the
// original: this class is a move, not a rewrite. In particular the
// party loop still `break`s on the first `local` member, so a roster with
// several local-tagged entries keeps overriding from the first one only,
// exactly as before.

#include "core/game_snapshot.h"
#include "player/player_types.h"

#include <QString>

namespace mhw {

class PlayerViewModel {
public:
    // The four values the player panel mirrors from a snapshot. They were
    // the members PlayerPanel::playerMR_ / playerName_ / weaponId_ /
    // partyCount_ and they are still read by paintPanel() — the panel
    // keeps owning them, this type is only the shape resolveIdentity()
    // hands back so the caller has one obvious place to copy from.
    struct PlayerIdentity {
        int     masterRank;
        QString name;
        int     weaponId;
        int     partyCount;
    };

    // Resolve the local player's identity from `snap`. Pure: no members
    // are read or written, so it is const and safe to call from a test
    // with a hand-built snapshot.
    [[nodiscard]] PlayerIdentity resolveIdentity(
        const mhw::GameSnapshot &snap) const;
};

} // namespace mhw
