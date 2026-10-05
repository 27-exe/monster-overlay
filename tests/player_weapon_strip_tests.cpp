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

// QtCore AND QtGui, for the same reason tests/player_info_strip_tests.cpp
// uses them (measured, not assumed): the layout section below asks
// QFontMetrics for the identity row's real pitch, and QFontMetrics
// fatals — "QGuiApplication::font(): Must construct a QGuiApplication
// first" — in a QCoreApplication-only process. QtGui reaches this target
// through mhw-ui's own public dependency, so there is no CMake change
// and no second target.
#include <QCoreApplication>
#include <QGuiApplication>
#include <QFont>
#include <QFontMetrics>
#include <QString>

#include <cmath>
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
// `sharpness` is the gauge block, defaulted invalid so the pre-merge
// tests below read exactly the way they did before the gauge came back;
// the merge tests fill it in.
mhw::GameSnapshot snapshotOf(bool playerValid, const QString &name,
                             int weaponId, int masterRank,
                             const mhw::SharpnessSnapshot &sharpness = {},
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
    pl.sharpness  = sharpness;
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
    // QGuiApplication, not QCoreApplication: the layout section asks
    // QFontMetrics for real pitches, and that needs a QGuiApplication to
    // exist. The same choice tests/player_info_strip_tests.cpp:141-143
    // makes, for the same reason, in a target with the same libraries.
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(
        QStringLiteral("player-weapon-strip-tests"));

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
            /*sharpness=*/{}, /*withLocalPartyMember=*/true,
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
            /*sharpness=*/{}, /*withLocalPartyMember=*/true,
            QString(), -1, 0);
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

    // ====================================================================
    // 7. THE MERGE — the sharpness block travels IN the model now.
    //
    // This is the whole point of the change under test: the gauge left
    // PlayerWeaponStripPanel for a window of its own and then came back,
    // so the model that used to carry four identity fields carries a
    // fifth block. Pinning verbatim transit is what stops a later
    // "cleanup" from copying the block field-by-field and quietly
    // dropping thresholds[].
    // ====================================================================
    {
        mhw::SharpnessSnapshot shp;
        shp.valid        = true;
        shp.level        = 4;
        shp.currentHits  = 40;
        shp.maxHits      = 100;
        shp.thresholds[0] = 60;
        shp.thresholds[4] = 340;

        const mhw::GameSnapshot sharp = snapshotOf(
            true, QString::fromUtf8(kA), /*weaponId=*/3, 340,
            /*sharpness=*/shp);

        const mhw::WeaponIdentityRow row =
            mhw::buildWeaponIdentityModel(sharp);

        check(row.sharpness.valid,
              "merge: a live melee weapon's sharpness block reaches the "
              "model");
        check(row.sharpness.level == 4 && row.sharpness.maxHits == 100
                  && row.sharpness.currentHits == 40,
              "merge: the level and both hit counters survive verbatim");
        check(row.sharpness.thresholds[0] == 60
                  && row.sharpness.thresholds[4] == 340,
              "merge: the threshold array survives too — the field a "
              "field-by-field copy would have dropped");

        // And the inverse: a ranged weapon's invalid block must arrive
        // invalid, not be "fixed up" into something drawable. The gauge
        // painter's own guard is what then decides to draw nothing.
        const mhw::GameSnapshot ranged = snapshotOf(
            true, QString::fromUtf8(kA), /*weaponId=*/3, 340,
            /*sharpness=*/{});   // default-constructed = valid == false

        const mhw::WeaponIdentityRow bore =
            mhw::buildWeaponIdentityModel(ranged);
        check(!bore.sharpness.valid,
              "merge: a ranged weapon's invalid sharpness block arrives "
              "invalid");
        check(!mhw::weaponIdentitySharpnessDrawable(bore.sharpness),
              "merge: weaponIdentitySharpnessDrawable() refuses it, the "
              "same entry guard drawPlayerSharpnessBar() takes");
    }

    // ====================================================================
    // 8. THE FOUR COMBINATIONS OF hasContent() — the merge's real work.
    //
    // Before the merge this window answered "identity only", and the
    // gauge had its own window with its own hasContent(). Now one
    // window answers for both, and the answer must be the OR of what
    // either half was worth:
    //
    //   name?  weapon?  sharpness?  ->  window?
    //   yes    yes      yes            yes  (the default)
    //   yes    yes      NO (ranged)   yes  (the trio alone earns it)
    //   NO     NO       yes            yes  (THE NEW ONE — see below)
    //   NO     NO       NO             NO   (nothing to say)
    //
    // The third row is the change. It is the state the "icon + MR with
    // no gauge" report came out of: the identity fails to resolve while
    // the gauge reads fine, and a window that hid itself then would drop
    // the one thing that actually works.
    // ====================================================================
    {
        // (yes, yes) with no drawable sharpness — a ranged weapon. The
        // trio alone must keep the window mounted, which is exactly the
        // answer this window gave when it WAS the trio alone.
        const mhw::GameSnapshot ranged = snapshotOf(
            true, QString::fromUtf8(kA), /*weaponId=*/3, 340,
            /*sharpness=*/{});   // default-constructed = valid == false
        const mhw::WeaponIdentityRow named =
            mhw::buildWeaponIdentityModel(ranged);
        check(!named.isEmpty() && !named.sharpness.valid,
              "hasContent: a resolved hunter with a ranged weapon still "
              "has content (the trio earns the window alone)");

        // (no, no, yes) — identity unresolved, gauge live. THE NEW
        // COMBINATION, and the reason isEmpty() takes three fields
        // rather than two.
        mhw::SharpnessSnapshot live;
        live.valid = true;
        live.level = 1;
        const mhw::GameSnapshot gaugeOnly = snapshotOf(
            /*playerValid=*/false, QString(), /*weaponId=*/0,
            /*masterRank=*/0, /*sharpness=*/live);

        const mhw::WeaponIdentityRow gauge =
            mhw::buildWeaponIdentityModel(gaugeOnly);
        check(!gauge.hasName && gauge.weaponId == -1,
              "hasContent: the identity really is gone (name and id)");
        check(gauge.sharpness.valid
                  && mhw::weaponIdentitySharpnessDrawable(gauge.sharpness),
              "hasContent: but the gauge is live and drawable");
        check(!gauge.isEmpty(),
              "hasContent: a live gauge alone mounts the window — the "
              "merge's one semantic change, and the answer the gauge's "
              "own window used to give");

        // (no, no, no) — still nothing, and the window must not exist.
        // The default-constructed sharpness is the invalid one, so this
        // is really "no name, no weapon, ranged weapon".
        const mhw::GameSnapshot dead = snapshotOf(
            /*playerValid=*/false, QString(), /*weaponId=*/0,
            /*masterRank=*/0, /*sharpness=*/{});
        const mhw::WeaponIdentityRow empty =
            mhw::buildWeaponIdentityModel(dead);
        check(empty.isEmpty() && !empty.hasName
                  && empty.weaponId < 0 && !empty.sharpness.valid,
              "hasContent: no name, no weapon, no gauge is still the "
              "do-not-mount answer");
    }

    // ====================================================================
    // 9. weaponIdentitySharpnessDrawable() — the drawability predicate,
    // pinned on its own.
    //
    // It is the answer the width reservation, the height reservation and
    // hasContent() all read, so a wrong value here would silently
    // reserve a 174px block for a gauge that draws nothing. The ranges
    // are drawPlayerSharpnessBar()'s own entry guard; the broken-weapon
    // (-1) and invalid (7) levels are real live states.
    // ====================================================================
    {
        const mhw::SharpnessSnapshot broken = [] {
            mhw::SharpnessSnapshot s;
            s.valid = true; s.level = -1;   // broken: a real live state
            return s;
        }();
        const mhw::SharpnessSnapshot invalid = [] {
            mhw::SharpnessSnapshot s;
            s.valid = true; s.level = 7;    // invalid: the reader's nobody
            return s;
        }();
        const mhw::SharpnessSnapshot red = [] {
            mhw::SharpnessSnapshot s;
            s.valid = true; s.level = 0;    // the lowest real colour
            return s;
        }();
        const mhw::SharpnessSnapshot purple = [] {
            mhw::SharpnessSnapshot s;
            s.valid = true; s.level = 6;    // the highest real colour
            return s;
        }();
        const mhw::SharpnessSnapshot notValid = [] {
            mhw::SharpnessSnapshot s;
            s.valid = false; s.level = 3;   // range says yes, validity no
            return s;
        }();

        check(mhw::weaponIdentitySharpnessDrawable(red)
                  && mhw::weaponIdentitySharpnessDrawable(purple),
              "drawable: level 0 and level 6 are both drawable (the "
              "endpoints of the colour range)");
        check(!mhw::weaponIdentitySharpnessDrawable(broken),
              "drawable: a broken weapon (level -1) is not drawable");
        check(!mhw::weaponIdentitySharpnessDrawable(invalid),
              "drawable: an invalid level (7) is not drawable");
        check(!mhw::weaponIdentitySharpnessDrawable(notValid),
              "drawable: a valid-looking level with an invalid block is "
              "still not drawable — both halves of the guard must pass");
    }

    // ====================================================================
    // 10. THE PLACEHOLDER CONSTANT — one "--" for both columns, and
    //     the same one the panel spells inline.
    //
    // The panel uses the literal in two places; this window reads it out
    // of the header. The pin is that the header's answer IS the panel's,
    // because a window printing "n/a" beside a panel printing "--" is
    // exactly the kind of divergence nobody would report and everybody
    // would notice.
    // ====================================================================
    {
        check(mhw::weaponIdentityNoName() == QLatin1String("--"),
              "placeholder: the no-name fallback is the same \"--\" the "
              "panel uses at panel_player.cpp:977");
    }

    // ====================================================================
    // 11. THE SIZE ARITHMETIC — the layout, pinned as numbers.
    //
    // This is the half of the merge the user asked for ("拆分后的面板的
    // 布局可以考虑重构优化一下"), and the reason these functions moved into
    // the header instead of staying in the .cpp's anonymous namespace:
    // the width and the height are what the user sees, they are read by
    // three callers that must not disagree, and a target that links
    // QtCore-only can assert on them once they are reachable.
    //
    // What is pinned:
    //   * THE ROW HEIGHT'S max(). identityRowHeight() must be >= the
    //     slot, because the slot is drawn ON that row. A pitch of 21
    //     against a 26px slot is exactly the defect the first draft had.
    //   * THE SHARED BUDGET. The merged window is max(streams), not the
    //     sum — the whole compactness argument, quantified.
    //   * EVERY COMBINATION of the two gates. A stream that is off must
    //     not leave width or height behind it, which is the rule that
    //     makes a gauge-only window SHORTER than the panel's own prow.
    // ====================================================================
    {
        const int rowH = mhw::weapon_row::identityRowHeight();
        const int slotH = mhw::weapon_row::kWslot;
        const int gaugeW = mhw::weapon_row::gaugeStreamWidth();
        const int margin = mhw::weapon_row::kMargin;
        const int gap = mhw::weapon_row::kWeaponGap;
        const int gaugeRowH = mhw::weapon_row::kGaugeRowH;

        // --- the gauge block's width, the number the painter's own
        //     accessor reports: [130 + 14 + 30].
        check(gaugeW == 130 + 14 + 30,
              "layout: the gauge block is the width contract 174px "
              "(130 + 14 + 30), read through playerSharpnessBarTotalWidth");

        // --- the slot fits its own row. THE LOAD-BEARING max().
        check(rowH >= slotH,
              "layout: the identity row is at least as tall as the slot "
              "— a plain font pitch would let a 26px slot overflow it");
        check(rowH == 26,
              "layout: and with 12pt Chakra Petch's pitch (21) below the "
              "slot, the row is exactly the 26px prow");

        // --- height, per gate combination. Each stream pays only for
        //     itself: the identity-only window is SHORTER than the
        //     merged one, and the gauge-only window is shorter still.
        check(mhw::weapon_row::contentHeight(true, true)
                  == margin + gap + (rowH + mhw::weapon_row::kIdentityLineGap
                                     + gaugeRowH) + margin
                  && mhw::weapon_row::contentHeight(true, true) == 93,
              "layout: both streams on is 93px "
              "(11 + 9 + [26 + 6 + 30] + 11)");
        check(mhw::weapon_row::contentHeight(true, false)
                  == margin + gap + rowH + margin
                  && mhw::weapon_row::contentHeight(true, false) == 57,
              "layout: identity only is 57px — the panel's own prow "
              "height, no more");
        check(mhw::weapon_row::contentHeight(false, true)
                  == margin + gap + gaugeRowH + margin
                  && mhw::weapon_row::contentHeight(false, true) == 61,
              "layout: gauge only is 61px, and it is its OWN height "
              "rather than the merged window's");
        check(mhw::weapon_row::contentHeight(false, false) == 2 * margin
                  && mhw::weapon_row::contentHeight(false, false) == 22,
              "layout: nothing on is the collapsed 2*kMargin, alive but "
              "unmounted");

        // --- the two single-stream heights differ from each other, which
        //     is the property "no stream drags another's height with it".
        check(mhw::weapon_row::contentHeight(true, false)
                  != mhw::weapon_row::contentHeight(false, true),
              "layout: turning either stream off changes the height by a "
              "different amount — they are independent, not one switch");

        // --- width. The shared budget, quantified against the pair it
        //     replaced: the old weapon strip was 26+8+name+8+MR+22 = 135
        //     and the old gauge strip was 174+22 = 196, i.e. 331 added.
        const int demoNameW = [] {
            QFont f(QStringLiteral("Chakra Petch"), 12, QFont::Medium);
            f.setStyleStrategy(QFont::PreferAntialias);
            return QFontMetrics(f).horizontalAdvance(
                QString::fromUtf8("猎人"));
        }();
        const int demoMrW = [] {
            QFont f(QStringLiteral("Chakra Petch"), 9);
            f.setStyleStrategy(QFont::PreferAntialias);
            return QFontMetrics(f).horizontalAdvance(
                QStringLiteral("MR 247"));
        }();
        const int demoIdentW =
            mhw::weapon_row::identityStreamWidth(demoNameW, demoMrW);

        check(demoIdentW > 0 && demoIdentW < gaugeW,
              "layout: for the demo hunter the identity stream (" +
                    std::to_string(demoIdentW) + ") is narrower than the"
                    " gauge stream (" + std::to_string(gaugeW)
                    + "), so the gauge sets the window's width");
        check(mhw::weapon_row::contentWidth(demoIdentW, gaugeW)
                  == gaugeW + 2 * margin
                  && mhw::weapon_row::contentWidth(demoIdentW, gaugeW)
                         == 196,
              "layout: the merged window is therefore 196px, not the "
              "331px the two separate windows added up to (40% narrower"
              " for the same content)");

        // A name longer than the whole gauge block must win — the width
        // budget is a max(), never a clamp, so a long name grows the
        // window instead of eliding. That is the entire reason the
        // identity window existed.
        const int longNameW = demoIdentW + 200;
        check(mhw::weapon_row::contentWidth(longNameW, gaugeW)
                  > mhw::weapon_row::contentWidth(demoIdentW, gaugeW),
              "layout: a name wider than the gauge grows the window "
              "rather than being elided — the max() cannot clamp");

        // With a stream off, its width leaves the budget with it.
        check(mhw::weapon_row::contentWidth(demoIdentW, 0)
                  < mhw::weapon_row::contentWidth(demoIdentW, gaugeW),
              "layout: turning the gauge off drops its 174px from the "
              "width, not just from the paint");
        check(mhw::weapon_row::contentWidth(0, gaugeW) == gaugeW + 2*margin,
              "layout: turning the identity off leaves exactly the gauge "
              "block and the margin");
    }

    std::printf("\n%d checks, %d failure(s)\n%s\n",
                checks, failures,
                failures == 0 ? "ALL WEAPON-IDENTITY TESTS PASSED"
                              : "SOME WEAPON-IDENTITY TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
