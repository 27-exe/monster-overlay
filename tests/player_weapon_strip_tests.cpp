// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the weapon identity trio's pure core:
// mhw::buildWeaponIdentityModel (src/ui/panel_player_split_weapon.h/.cpp).
//
// The window this feeds (PlayerWeaponStripPanel) draws the icon, the
// player name and the MR badge that used to sit inside PlayerPanel's
// weapon row. Its "should this window exist at all" answer, its width
// reservation and its demo seed all read the same model, so the model's
// decisions are the ones worth pinning — they are the only part of the
// extraction that no pixel baseline can see.
//
// What is pinned here:
//   * THE THIN WRAPPER IS THE POINT. buildWeaponIdentityModel delegates
//     to PlayerViewModel::resolveIdentity(), which is where the party
//     override, the player-struct fallback and the not-attached reset
//     live. This test proves the delegation is wired (a party member's
//     name wins, a stale one does not survive an invalid player struct)
//     rather than re-implementing those rules — a second implementation
//     here would be a second thing to keep in sync, and the panel and
//     the window would drift apart about who the local player is.
//   * hasName IS NOT "name is non-empty" AS SEEN FROM OUTSIDE. The
//     ViewModel returns an empty name for two very different states
//     (no local player vs. reader not attached), and the panel's own
//     "--" placeholder collapses that distinction. This window cannot:
//     hasContent() reads hasName to decide whether to mount a surface,
//     and isEmpty() combines it with weaponId, so the flag is tested
//     against both states as well as against an empty-but-present name.
//   * THE TWO PLACEHOLDER-AND-A-BIT SHAPES. masterRank is passed through
//     un-clamped and un-touched; the window's own mrText() decides
//     "MR --" for 0 and below, exactly as the panel does. Pinning the
//     raw passthrough is what stops a later change here from inventing a
//     placeholder one side shows and the other does not.

#include "ui/panel_player_split_weapon.h"

#include "player/player_types.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>
#include <string>

namespace {

int failures = 0;
int checks   = 0;

void check(bool cond, const std::string &what)
{
    ++checks;
    if (cond) {
        std::printf("PASS: %s\n", what.c_str());
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// One snapshot with a local player in it. `partyLocal` drives the
// ViewModel's own override arm, which is the half of the merge this
// window inherits by delegating rather than reimplementing.
mhw::GameSnapshot snapshotOf(bool playerValid, const QString &name,
                             int weaponId, int masterRank,
                             bool withLocalPartyMember = false,
                             const QString &partyName = QString(),
                             int partyWeaponId = -1, int partyRank = 0)
{
    mhw::GameSnapshot snap;
    snap.game = mhw::GameId::World;

    mhw::PlayerSnapshot pl;
    pl.valid      = playerValid;
    pl.name       = name;
    pl.weaponId   = weaponId;
    pl.masterRank = masterRank;
    snap.player   = pl;

    if (withLocalPartyMember) {
        mhw::PartyMemberSnapshot m;
        m.local     = true;
        m.name      = partyName;
        m.weaponId  = partyWeaponId;
        m.masterRank = partyRank;
        snap.party.append(m);
    }
    return snap;
}

const char *kA = "LocalHunter";
const char *kB = "PartyLeader";
const char *kC = "灰白掠夺者";

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ====================================================================
    // 1. THE HAPPY PATH — every field the trio needs, in one call.
    // ====================================================================
    {
        const mhw::GameSnapshot snap = snapshotOf(
            true, QString::fromUtf8(kA), 3, 340);
        const mhw::WeaponIdentityRow row =
            mhw::buildWeaponIdentityModel(snap);

        check(row.weaponId == 3,
              "identity: the weapon id comes straight from the player struct");
        check(row.masterRank == 340,
              "identity: the master rank comes straight from the player struct");
        check(row.name == QString::fromUtf8(kA),
              "identity: the player name comes straight from the player struct");
        check(row.hasName,
              "identity: a non-empty name sets hasName");
        check(!row.isEmpty(),
              "identity: a resolved hunter means the window has content");
    }

    // ====================================================================
    // 2. THE DELEGATION — the party override arm, inherited for free.
    //
    // This is the arm that would silently break if this window ever
    // grew its own copy of the identity merge: the panel's local player
    // would then disagree with the window's whenever the player struct
    // lags a poll and the party member ahead of it is fresher.
    // ====================================================================
    {
        const mhw::GameSnapshot snap = snapshotOf(
            true, QString::fromUtf8(kA), 3, 340,
            /*withLocalPartyMember=*/true,
            QString::fromUtf8(kB), /*partyWeaponId=*/7, /*partyRank=*/999);
        const mhw::WeaponIdentityRow row =
            mhw::buildWeaponIdentityModel(snap);

        check(row.name == QString::fromUtf8(kB),
              "delegation: a local party member's name overrides the "
              "player struct's");
        check(row.weaponId == 7,
              "delegation: a local party member's weapon id overrides too");
        check(row.masterRank == 999,
              "delegation: a local party member's MR overrides too");

        // The ViewModel's own guard: an empty party name must NOT
        // overwrite a good one. Delegation keeps that rule; a local
        // reimplementation would have to remember it.
        const mhw::GameSnapshot blankParty = snapshotOf(
            true, QString::fromUtf8(kA), 3, 340,
            /*withLocalPartyMember=*/true, QString(), -1, 0);
        const mhw::WeaponIdentityRow kept =
            mhw::buildWeaponIdentityModel(blankParty);
        check(kept.name == QString::fromUtf8(kA)
                  && kept.weaponId == 3 && kept.masterRank == 340,
              "delegation: an empty party name / sentinel id / zero rank "
              "leaves the player struct's values standing");
    }

    // ====================================================================
    // 3. THE NOT-ATTACHED BRANCH — where hasName earns its keep.
    //
    // resolveIdentity() resets weaponId to -1 when the player struct is
    // invalid and nothing resolved. The panel never notices (it has a
    // whole "not connected" placeholder to draw instead); this window
    // must not mount a surface at all, so hasContent() reads the flag.
    // ====================================================================
    {
        const mhw::GameSnapshot dead = snapshotOf(
            /*playerValid=*/false, QString(), /*weaponId=*/0,
            /*masterRank=*/0);
        const mhw::WeaponIdentityRow row =
            mhw::buildWeaponIdentityModel(dead);

        check(row.weaponId == -1,
              "not-attached: an invalid player struct collapses the weapon "
              "id to -1");
        check(!row.hasName,
              "not-attached: an invalid player struct leaves hasName false");
        check(row.isEmpty(),
              "not-attached: no name and no weapon is the window's "
              "do-not-mount answer");

        // The other half: nothing resolved, but the reader IS attached.
        // Same isEmpty() answer, and that is deliberate — an icon beside
        // "--" beside "MR --" is three placeholders with nothing behind
        // them.
        const mhw::GameSnapshot attachedEmpty = snapshotOf(
            /*playerValid=*/true, QString(), /*weaponId=*/-1,
            /*masterRank=*/0);
        const mhw::WeaponIdentityRow live =
            mhw::buildWeaponIdentityModel(attachedEmpty);
        check(!live.hasName && live.isEmpty(),
              "not-attached: an attached-but-empty player also answers "
              "empty");
    }

    // ====================================================================
    // 4. THE PARTIAL-HAVE CASES — the two that keep the window mounted.
    //
    // A name with no weapon (the icon slot is drawn but empty) and a
    // weapon with no name both have something to say, so neither is
    // empty. The panel's placeholder path is what makes these
    // indistinguishable in the row; the window cannot afford that.
    // ====================================================================
    {
        const mhw::GameSnapshot noWeapon = snapshotOf(
            true, QString::fromUtf8(kA), /*weaponId=*/-1, 340);
        const mhw::WeaponIdentityRow named =
            mhw::buildWeaponIdentityModel(noWeapon);
        check(named.hasName && !named.isEmpty(),
              "partial: a name with no resolved weapon still mounts the "
              "window");
        check(named.weaponId == -1,
              "partial: the unresolved weapon id is preserved, not "
              "clamped to 0");

        const mhw::GameSnapshot noName = snapshotOf(
            true, QString(), /*weaponId=*/5, /*masterRank=*/0);
        const mhw::WeaponIdentityRow armed =
            mhw::buildWeaponIdentityModel(noName);
        check(!armed.hasName && armed.weaponId == 5 && !armed.isEmpty(),
              "partial: a weapon with no name still mounts the window");
    }

    // ====================================================================
    // 5. WHAT THIS FUNCTION DELIBERATELY DOES NOT DECIDE.
    //
    // The placeholder strings ("MR --", "--") and the width arithmetic
    // are paint-side concerns, and the model must not pre-empt them:
    // a rank of 0 or below travels through untouched so the painter can
    // make the same decision the panel makes, in one place, once.
    // ====================================================================
    {
        const mhw::GameSnapshot zero = snapshotOf(
            true, QString::fromUtf8(kA), 3, /*masterRank=*/0);
        const mhw::WeaponIdentityRow row =
            mhw::buildWeaponIdentityModel(zero);
        check(row.masterRank == 0,
              "passthrough: MR 0 is passed through, not replaced by a "
              "placeholder here");
        check(row.hasName,
              "passthrough: a name stays present alongside an "
              "unresolved MR");

        const mhw::GameSnapshot negative = snapshotOf(
            true, QString::fromUtf8(kA), 3, /*masterRank=*/-5);
        check(mhw::buildWeaponIdentityModel(negative).masterRank == -5,
              "passthrough: a negative MR is passed through too — the "
              "painter owns 'MR --'");
    }

    // ====================================================================
    // 6. THE WIDE-NAME CASE — no elision, no truncation.
    //
    // The whole point of carving the trio out: the panel left the name a
    // 49px band and elided it, while the window measures it. The model
    // must hand the painter the name intact for any width, including one
    // with no Latin in it at all.
    // ====================================================================
    {
        const mhw::GameSnapshot wide = snapshotOf(
            true, QString::fromUtf8(kC), 12, 999);
        const mhw::WeaponIdentityRow row =
            mhw::buildWeaponIdentityModel(wide);
        check(row.name == QString::fromUtf8(kC) && row.hasName,
              "width: a long non-Latin name survives the model "
              "un-elided");
        check(!row.name.contains(QStringLiteral("…"))
                  && !row.name.endsWith(QStringLiteral("...")),
              "width: the model never adds an ellipsis of its own");
    }

    std::printf("\n%d checks, %d failure(s)\n%s\n",
                checks, failures,
                failures == 0 ? "ALL WEAPON-IDENTITY TESTS PASSED"
                              : "SOME WEAPON-IDENTITY TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
