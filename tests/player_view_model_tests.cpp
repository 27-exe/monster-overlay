// SPDX-License-Identifier: Apache-2.0
// Unit tests for mhw::PlayerViewModel::resolveIdentity — the three blocks
// that used to sit inline in PlayerPanel::update(const GameSnapshot &):
// the initial mirroring of (masterRank, name, weaponId, partyCount) from
// the player struct, the party-override loop that prefers the local party
// member's fields when the player struct lags a poll, and the
// not-attached fallback that resets weaponId to -1.
//
// The contract this test pins down is deliberately narrow and literal,
// because every line of it is a guard that only shows up as a wrong
// number on screen:
//
//   * the party override is per-field, not per-member — a local member
//     whose masterRank is 0 (or name is empty, or weaponId is -1) leaves
//     exactly that one field on the player struct's value;
//   * the loop `break`s on the first local member. A roster with more
//     than one local-tagged entry must keep overriding from the FIRST
//     one only — the case a `break` regression silently changes, and the
//     one nothing else in the suite would notice;
//   * a member is only eligible when `local` is true, so a remote member
//     that happens to sit ahead of the local one is skipped, not used;
//   * the !valid fallback fires only when BOTH weaponId == 0 and the name
//     is empty, i.e. when there is genuinely nothing to show, and it is
//     evaluated AFTER the override, so an override that lands on weaponId
//     0 still gets the -1.
//
// The type carries no widget dependency, so no QApplication and no
// QT_QPA_PLATFORM=offscreen is required.

#include "ui/viewmodel/player_view_model.h"

#include "core/game_snapshot.h"
#include "player/player_types.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;
int checks   = 0;

// PASS lines go to stdout, FAIL lines to stderr, and the two streams are
// not interleaved line-buffered when a suite is read from a pipe — the
// FAIL can surface in the middle of a long PASS label. Flushing both at
// the end of every check keeps the report readable.
// The cases below hand a string literal here, which converts to
// std::string implicitly.
void check(bool cond, const std::string &what)

{
    ++checks;
    if (cond) {
        std::printf("PASS: %s\n", what.c_str());
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
    std::fflush(stdout);
    std::fflush(stderr);
}

// A case can fail more than one of the four fields, so the identity checks
// share a single label: printing it four times per case made the log
// unreadable and the failing line impossible to pick out.
std::string checkLabel(const std::string &caseName, const char *field)
{
    return caseName + " [" + field + "]";
}

// A party member with only the fields resolveIdentity() reads. The struct
// defaults (weaponId -1, masterRank 0, local false, slot -1) mean a member
// built here must set every field it wants non-default, so a test can
// never accidentally pass a value it never stated.
mhw::PartyMemberSnapshot member(const QString &name, int masterRank,
                                int weaponId, bool local, int slot)
{
    mhw::PartyMemberSnapshot value;
    value.name       = name;
    value.masterRank = masterRank;
    value.weaponId   = weaponId;
    value.local      = local;
    value.slot       = slot;
    return value;
}

// The player struct the panel reads directly (gathering-hub case: the
// party array is empty but the player struct already holds everything).
mhw::GameSnapshot playerSnapshot(const QString &name, int masterRank,
                                 int weaponId, bool valid)
{
    mhw::GameSnapshot snap;
    snap.player.name       = name;
    snap.player.masterRank = masterRank;
    snap.player.weaponId   = weaponId;
    snap.player.valid      = valid;
    return snap;
}

void checkIdentity(const mhw::PlayerViewModel::PlayerIdentity &id,
                   int masterRank, const QString &name, int weaponId,
                   int partyCount, const std::string &what)
{
    check(id.masterRank == masterRank,
          checkLabel(what, "masterRank"));
    check(id.name == name,          checkLabel(what, "name"));
    check(id.weaponId == weaponId,  checkLabel(what, "weaponId"));
    check(id.partyCount == partyCount,
          checkLabel(what, "partyCount"));
}

} // namespace

int main()
{
    const mhw::PlayerViewModel vm;

    // ------------------------------------------------------------------ base
    //
    // The player struct is the source of truth when the party array has
    // nothing to say: in the gathering hub PlayerPanel still has to show
    // MR, name, weapon and a party count of 0.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 200, 7, true);
        checkIdentity(vm.resolveIdentity(snap), 200,
                      QStringLiteral("Hunter"), 7, 0,
                      "base: MR / name / weaponId / partyCount are taken "
                      "straight from the player struct with an empty party");
    }

    // partyCount counts EVERY roster entry, local or not — it is the size
    // of the array, not the number of local members.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 200, 7, true);
        snap.party.append(member(QStringLiteral("Remote"), 50, 2, false, 0));
        snap.party.append(member(QStringLiteral("Remote2"), 60, 3, false, 1));
        checkIdentity(vm.resolveIdentity(snap), 200,
                      QStringLiteral("Hunter"), 7, 2,
                      "base: a party of non-local members still counts "
                      "toward partyCount but overrides nothing");
    }

    // --------------------------------------------------------------- override
    //
    // The whole reason the loop exists: the player struct can lag a poll,
    // and the local member's fields are the fresher ones. All three fields
    // travel together here because a real local member has all three.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 200, 7, true);
        snap.party.append(member(QStringLiteral("Local"), 250, 9, true, 0));
        checkIdentity(vm.resolveIdentity(snap), 250,
                      QStringLiteral("Local"), 9, 1,
                      "override: the local member's MR / name / weaponId "
                      "replace the player struct's, partyCount is 1");
    }

    // A remote member sitting AHEAD of the local one must be skipped, not
    // used: removing `if (m.local)` would take this member's fields and
    // then break, silently showing another hunter's identity.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 200, 7, true);
        snap.party.append(member(QStringLiteral("Remote"), 100, 2, false, 0));
        snap.party.append(member(QStringLiteral("Local"), 250, 9, true, 1));
        checkIdentity(vm.resolveIdentity(snap), 250,
                      QStringLiteral("Local"), 9, 2,
                      "override: a non-local member ahead of the local one "
                      "is skipped, the local member still wins");
    }

    // ------------------------------------------------- per-field guard: MR
    //
    // masterRank > 0 only. 0 is the struct default for both PlayerSnapshot
    // and PartyMemberSnapshot, so a local member that has not had its MR
    // filled yet must not wipe the player struct's 200 down to 0. The
    // name and weaponId in this case DO override, which is what proves
    // the guard is per-field rather than a bail-out on the whole member.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 200, 7, true);
        snap.party.append(member(QStringLiteral("Local"), 0, 9, true, 0));
        checkIdentity(vm.resolveIdentity(snap), 200,
                      QStringLiteral("Local"), 9, 1,
                      "guard: masterRank 0 on the local member does NOT "
                      "override, but its name and weaponId still do");
    }

    // ---------------------------------------------- per-field guard: name
    //
    // A non-empty name only: empty means "not read yet", and an empty
    // name would blank the panel's name label.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 200, 7, true);
        snap.party.append(member(QString(), 250, 9, true, 0));
        checkIdentity(vm.resolveIdentity(snap), 250,
                      QStringLiteral("Hunter"), 9, 1,
                      "guard: an empty name on the local member does NOT "
                      "override, but its MR and weaponId still do");
    }

    // -------------------------------------------- per-field guard: weapon
    //
    // weaponId >= 0 only. -1 is the not-equipped value both structs
    // default to, so it must not overwrite a real weapon id.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 200, 7, true);
        snap.party.append(member(QStringLiteral("Local"), 250, -1, true, 0));
        checkIdentity(vm.resolveIdentity(snap), 250,
                      QStringLiteral("Local"), 7, 1,
                      "guard: weaponId -1 on the local member does NOT "
                      "override, but its MR and name still do");
    }

    // All three guards failing at once leaves the player struct untouched
    // while still counting the member.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 200, 7, true);
        snap.party.append(member(QString(), 0, -1, true, 0));
        checkIdentity(vm.resolveIdentity(snap), 200,
                      QStringLiteral("Hunter"), 7, 1,
                      "guard: a local member with none of its fields filled "
                      "changes nothing but partyCount");
    }

    // ----------------------------------------------------------------- break
    //
    // THE case this suite exists for: two local-tagged members with
    // different, fully-populated fields. Only the first one may apply.
    // Deleting the `break;` makes every field below wrong at once.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 200, 7, true);
        snap.party.append(member(QStringLiteral("First"), 250, 9, true, 0));
        snap.party.append(member(QStringLiteral("Second"), 300, 11, true, 1));
        checkIdentity(vm.resolveIdentity(snap), 250,
                      QStringLiteral("First"), 9, 2,
                      "break: with two local members only the FIRST one "
                      "applies, the second is not reached");
    }

    // Three local members, and the first one carries nothing usable
    // (MR 0, empty name, weapon -1). The guards make this case
    // indistinguishable from "no local member at all" — but WITHOUT the
    // break the second member's real values would land, so it catches a
    // dropped `break;` even when the first local entry is a blank one,
    // which is the realistic roster a reader produces mid-poll.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 200, 7, true);
        snap.party.append(member(QString(), 0, -1, true, 0));
        snap.party.append(member(QStringLiteral("Third"), 444, 5, true, 1));
        snap.party.append(member(QStringLiteral("Fourth"), 555, 6, true, 2));
        checkIdentity(vm.resolveIdentity(snap), 200,
                      QStringLiteral("Hunter"), 7, 3,
                      "break: a blank first local member blocks the later "
                      "local members, not just itself");
    }

    // The blank-first local member also protects the fallback below: the
    // break must happen before any later member can hand weaponId a 0.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 200, 7, false);
        snap.party.append(member(QString(), 0, -1, true, 0));
        snap.party.append(member(QStringLiteral("Third"), 444, 0, true, 1));
        checkIdentity(vm.resolveIdentity(snap), 200,
                      QStringLiteral("Hunter"), 7, 2,
                      "break: a later local member's weaponId 0 cannot "
                      "reach the fallback through a broken break");
    }

    // ------------------------------------------------------- no local member
    //
    // Every member remote: the loop finds nothing and the player struct
    // is the only source. This is the case a missing `if (m.local)` guard
    // would turn into an override by the first roster entry.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 200, 7, true);
        snap.party.append(member(QStringLiteral("Remote"), 100, 2, false, 0));
        snap.party.append(member(QStringLiteral("Remote2"), 300, 11, false, 1));
        checkIdentity(vm.resolveIdentity(snap), 200,
                      QStringLiteral("Hunter"), 7, 2,
                      "no local member: a party of remote members overrides "
                      "nothing");
    }

    // ------------------------------------------------------- !valid fallback
    //
    // Not attached to a real process and both mirrors empty: the panel
    // would otherwise keep the previous frame's stale weapon on screen,
    // so weaponId is reset to the not-equipped value. MR and the name are
    // NOT touched — only weaponId carries the -1.
    {
        mhw::GameSnapshot snap = playerSnapshot(QString(), 0, 0, false);
        checkIdentity(vm.resolveIdentity(snap), 0, QString(), -1, 0,
                      "fallback: weaponId 0 with an empty name and "
                      "!valid resets weaponId to -1");
    }

    // The fallback is evaluated AFTER the override, so a local member that
    // lands on weaponId 0 (it passes the >= 0 guard) is what the fallback
    // then sees — but only if the name is empty too, which here it is in
    // both the player struct and the member. The ordering is part of the
    // contract: swap the two blocks and this case returns 0, not -1.
    {
        mhw::GameSnapshot snap = playerSnapshot(QString(), 200, 7, false);
        snap.party.append(member(QString(), 0, 0, true, 0));
        checkIdentity(vm.resolveIdentity(snap), 200,
                      QString(), -1, 1,
                      "fallback: an override that lands on weaponId 0 with "
                      "an empty name still resolves to -1");
    }

    // ---------------------------------------------- fallback preconditions
    //
    // weaponId != 0 is enough to keep the value, even with !valid and an
    // empty name: a weapon was read, so there is something to show.
    {
        mhw::GameSnapshot snap = playerSnapshot(QString(), 0, 4, false);
        checkIdentity(vm.resolveIdentity(snap), 0, QString(), 4, 0,
                      "fallback: weaponId 4 is NOT reset to -1 even when "
                      "!valid and the name is empty");
    }

    // A non-empty name is enough on its own, even with weaponId == 0.
    {
        mhw::GameSnapshot snap = playerSnapshot(
            QStringLiteral("Hunter"), 0, 0, false);
        checkIdentity(vm.resolveIdentity(snap), 0,
                      QStringLiteral("Hunter"), 0, 0,
                      "fallback: weaponId 0 is NOT reset to -1 when the "
                      "name is non-empty");
    }

    // The !valid guard itself: an ATTACHED player struct with weaponId 0
    // and an empty name is left alone — the fallback is about the
    // not-attached case, not about empty titles.
    {
        mhw::GameSnapshot snap = playerSnapshot(QString(), 0, 0, true);
        checkIdentity(vm.resolveIdentity(snap), 0, QString(), 0, 0,
                      "fallback: an attached snapshot with weaponId 0 and an "
                      "empty name keeps weaponId 0");
    }

    std::printf("%s: %d failure(s) out of %d check(s)\n",
                failures == 0 ? "OK" : "FAILED", failures, checks);
    return failures == 0 ? 0 : 1;
}
