// SPDX-License-Identifier: Apache-2.0
//
// A.2 — the shared Mixed window-layout plan (window_layout_plan.h), and
// the Mixed half of the layout-mode chain it hangs off.
//
// WHAT IS UNDER TEST
// ------------------
//   1. RED SECTION — the seed v1.0.1 behaviour this lane exists to
//      change, written ONLY against APIs that already exist in the seed
//      so it COMPILES there and FAILS there as a real behaviour failure
//      (a missing symbol would only prove the tree is older than the
//      test, which proves nothing). Observed on the seed:
//          2 failures (mixed does not parse; conf `layout=mixed`
//          resolves back to the default Single).
//      After the implementation the same section is green — it is kept,
//      because it is the regression that says "mixed stopped parsing".
//
//   2. THE PLAN MODEL — parse / serialize / resolve / retainedMask /
//      detached, over the 11 rows of the three split tables.
//
//   3. TOPOLOGY RULES the overlay consumes: a mother panel's retained
//      mask and mount decision, and a sub-window's enable bit, in every
//      (mode x game x mask) combination — including the seed-equivalence
//      checks that pin Single/Split to exactly what playerPanelMaskFor /
//      monsterPanelMaskFor / damagePanelMaskFor answer today.
//
// WHY HEADER-ONLY + Qt6::Core: everything below is inline over QString /
// QByteArray over the pure tables in panel_sections.h. No QWidget, no
// reader, no config file on disk.
//
// MUTATION DISCIPLINE: each block names the one-line change that must
// make it FAIL.

#include "ui/viewmodel/console_layout_mode_codec.h"
#include "ui/window_layout_plan.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;
int checks   = 0;

void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  FAIL  %s\n", what);
    }
}

void checkQ(const QString &got, const QString &want, const char *what)
{
    ++checks;
    if (got != want) {
        ++failures;
        std::printf("  FAIL  %s: got \"%s\" want \"%s\"\n", what,
                    qPrintable(got), qPrintable(want));
    }
}

using Mode      = mhw::PlayerSection::PlayerLayoutMode;
using Plan      = mhw::WindowLayoutPlan;
using Source    = mhw::PlayerLayoutSource;
using Resolution = mhw::PlayerLayoutResolution;

// =====================================================================
// 1. RED — seed v1.0.1 counterexamples, existing API only.
// =====================================================================
// MUTATION: make parsePlayerLayoutMode() reject "mixed" (delete the
// arm) — both checks below fail again.
void test_red_mixedParsesOnTheSeed()
{
    Mode m = Mode::Single;
    check(mhw::parsePlayerLayoutMode(QStringLiteral("mixed"), m),
          "RED/now-green: --layout mixed must PARSE (seed v1.0.1 "
          "returned false and the run silently fell back to Single)");
    check(mhw::playerLayoutModeToString(m) == QStringLiteral("mixed"),
          "RED/now-green: the parsed mixed mode must spell back as "
          "\"mixed\" so serialize/argv/namespace round-trip");

    // Whitespace/case parity with "SPLIT"/" split ", asserted rather
    // than assumed: the codec comment promises the SAME accepted
    // spellings for conf and command line.
    Mode w = Mode::Single;
    check(mhw::parsePlayerLayoutMode(QStringLiteral(" mixed "), w)
              && w == Mode::Mixed,
          "RED/now-green: the mixed spelling tolerates surrounding "
          "space, exactly like split");

    // The chain, end to end: a conf row that says mixed must not come
    // back as the default Single.
    const Resolution r = mhw::resolveStartupLayoutMode(
        mhw::PlayerLayoutCliRequest{},
        mhw::parseConsoleLayoutRow(QStringLiteral("layout=mixed\n")));
    check(r.mode != Mode::Single,
          "RED/now-green: conf layout=mixed must not resolve back to "
          "the default Single");
    check(r.source == Source::Conf,
          "RED/now-green: conf layout=mixed reports source Conf");
}

// =====================================================================
// 2. THE PLAN MODEL
// =====================================================================
void test_parseAndSerialize()
{
    // Explicit empty string is a legal NONE, not an error.
    bool ok = false;
    const Plan none = mhw::parseWindowLayoutPlan(QString(), &ok);
    check(ok, "the empty string parses (explicit empty = none)");
    check(none == Plan(), "and it is the empty plan");
    checkQ(mhw::serializeWindowLayoutPlan(none), QString(),
           "serializing the empty plan is the empty string");

    // One key, then the canonical ORDER: table order, never QSet order.
    const Plan one = mhw::parseWindowLayoutPlan(
        QStringLiteral("player-bars"), &ok);
    check(ok, "one key parses");
    check(one.detached(mhw::kLayoutParentPlayer,
                       mhw::PlayerSplitSlot::Bars),
          "player-bars is detached after parsing it");
    checkQ(mhw::serializeWindowLayoutPlan(one),
           QStringLiteral("player-bars"),
           "serialize round-trips the single key");

    // Canonical order is TABLE order: a shuffled input re-emits in
    // table order (damage rows are player->monster->damage, and within
    // a table the row's own index).
    const Plan shuffled = mhw::parseWindowLayoutPlan(
        QStringLiteral("monster-conditions,damage-share,player-bars,"
                       "player-status"),
        &ok);
    check(ok, "a shuffled multi-key list parses");
    checkQ(mhw::serializeWindowLayoutPlan(shuffled),
           QStringLiteral("player-status,player-bars,damage-share,"
                          "monster-conditions"),
           "canonical output follows TABLE order (player rows 0..5, "
           "monster 0..1, damage 0..2), not input order and not "
           "alphabetical order");

    // Unknown key: rejected, never silently guessed.
    // (The out-flag, not a plan: parseWindowLayoutPlan's second argument
    // is `bool *ok` — see its declaration in ui/window_layout_plan.h.)
    bool junk = true;
    check(!mhw::parseWindowLayoutPlan(QStringLiteral("player-nope"),
                                      &junk)
               .detached(mhw::kLayoutParentPlayer,
                         mhw::PlayerSplitSlot::Status),
          "an unknown key does not detach any row");

    bool okUnknown = true;
    mhw::parseWindowLayoutPlan(QStringLiteral("player-nope"),
                               &okUnknown);
    check(!okUnknown, "unknown key reports ok=false");

    // Duplicate: rejected (the same key twice is a pile nobody
    // produced, and accepting it would let a later write silently
    // depend on scan order).
    bool okDup = true;
    mhw::parseWindowLayoutPlan(QStringLiteral("player-bars,player-bars"),
                               &okDup);
    check(!okDup, "a duplicated key reports ok=false");

    // Empty token inside the list ("a,,b") and a lone comma are
    // malformed, not "none".
    bool okEmptyToken = true;
    mhw::parseWindowLayoutPlan(QStringLiteral("player-bars,,player-info"),
                               &okEmptyToken);
    check(!okEmptyToken, "an empty token inside the list is invalid");

    // A whitespace-only string is NOT the explicit empty string: the
    // empty file row is "" and anything else must say what it means.
    bool okWs = true;
    mhw::parseWindowLayoutPlan(QStringLiteral("   "), &okWs);
    check(!okWs, "a whitespace-only list is invalid, not none");

    // setDetached on an unknown key returns false and does not
    // normalise it into a near-miss.
    Plan p;
    check(!p.setDetached(QStringLiteral("PLAYER-BARS"), true),
          "setDetached rejects an unknown spelling (no case folding)");
    check(p == Plan(), "and the plan is unchanged after the rejection");
    check(p.setDetached(QStringLiteral("damage-rows"), true),
          "setDetached accepts a registry key");
    check(p.detached(mhw::kLayoutParentDamage,
                     mhw::DamageSplitSlot::Rows),
          "and the row really is detached");
    check(p.setDetached(QStringLiteral("damage-rows"), false),
          "setDetached(off) on the same key succeeds");
    check(p == Plan(), "back to empty");
}

void test_defaultPlans()
{
    check(mhw::defaultWindowLayoutPlan(Mode::Single) == Plan(),
          "Single's default plan is NONE (every row merged)");
    const Plan split = mhw::defaultWindowLayoutPlan(Mode::Split);
    check(mhw::defaultWindowLayoutPlan(Mode::Split).detached(
              mhw::kLayoutParentPlayer, mhw::PlayerSplitSlot::Info),
          "Split's default plan detaches a player row");
    check(split.detached(mhw::kLayoutParentMonster,
                         mhw::MonsterSplitSlot::Identity),
          "Split's default plan detaches a monster row");
    check(split.detached(mhw::kLayoutParentDamage,
                         mhw::DamageSplitSlot::Rows),
          "Split's default plan detaches a damage row");
    // Mixed with no explicit configuration = ALL detached, the visible
    // safety fallback (a Mixed run with no keys must not silently
    // render the Single topology while claiming Mixed).
    check(mhw::defaultWindowLayoutPlan(Mode::Mixed) == split,
          "Mixed without explicit config falls back to ALL detached, "
          "i.e. the visible split, not to an empty plan");
}

void test_resolve()
{
    const QString confMixed =
        QStringLiteral("player=1ff\nlayout.windows.world=player-bars,"
                       "monster-conditions\n"
                       "layout.windows.rise=damage-share\n");
    const QString confNone = QStringLiteral("player=1ff\n");

    // Explicit CLI keys win, per game, whatever the mode says.
    const Plan cli = mhw::resolveWindowLayoutPlan(
        mhw::GameId::World, Mode::Mixed, confMixed,
        QStringLiteral("player-info"));
    checkQ(mhw::serializeWindowLayoutPlan(cli),
           QStringLiteral("player-info"),
           "explicit --detached-windows wins over the conf row");

    // Mixed reads ONLY its own game's row.
    const Plan world = mhw::resolveWindowLayoutPlan(
        mhw::GameId::World, Mode::Mixed, confMixed);
    checkQ(mhw::serializeWindowLayoutPlan(world),
           QStringLiteral("player-bars,monster-conditions"),
           "World/Mixed reads layout.windows.world=");
    const Plan rise = mhw::resolveWindowLayoutPlan(
        mhw::GameId::Rise, Mode::Mixed, confMixed);
    checkQ(mhw::serializeWindowLayoutPlan(rise),
           QStringLiteral("damage-share"),
           "Rise/Mixed reads layout.windows.rise=, not the world row");

    // Missing row for this game -> the visible safety fallback.
    const Plan missing = mhw::resolveWindowLayoutPlan(
        mhw::GameId::Rise, Mode::Mixed, confNone);
    check(missing == mhw::defaultWindowLayoutPlan(Mode::Mixed),
          "a Mixed run with no row for this game falls back to ALL "
          "detached (visible), never to an empty plan");

    // Unusable row (unknown key) -> fallback, never a guessed subset.
    const Plan garbage = mhw::resolveWindowLayoutPlan(
        mhw::GameId::World, Mode::Mixed,
        QStringLiteral("layout.windows.world=nope,nope2\n"));
    check(garbage == mhw::defaultWindowLayoutPlan(Mode::Mixed),
          "an unusable conf row falls back to the mode default "
          "instead of guessing keys");

    // The other game's row is untouched by a world resolution, and
    // vice versa: two games, two saved values.
    check(world.detached(mhw::kLayoutParentPlayer,
                         mhw::PlayerSplitSlot::Bars)
              && !world.detached(mhw::kLayoutParentDamage,
                                 mhw::DamageSplitSlot::Share),
          "World/Mixed ignores the rise row");

    // --layout single / split WITHOUT cliKeys is its quick preset and
    // is NOT polluted by the Mixed saved value.
    check(mhw::resolveWindowLayoutPlan(mhw::GameId::World, Mode::Single,
                                       confMixed)
              == Plan(),
          "--layout single ignores the saved Mixed row (no cliKeys)");
    check(mhw::resolveWindowLayoutPlan(mhw::GameId::World, Mode::Split,
                                       confMixed)
              == mhw::defaultWindowLayoutPlan(Mode::Split),
          "--layout split ignores the saved Mixed row (no cliKeys)");

    // Invalid CLI keys never take the run down and never guess: the
    // mode default applies.
    check(mhw::resolveWindowLayoutPlan(mhw::GameId::World, Mode::Mixed,
                                       confMixed,
                                       QStringLiteral("bogus"))
              == mhw::defaultWindowLayoutPlan(Mode::Mixed),
          "invalid explicit keys fall back to the mode default");
}

void test_serializeRow()
{
    const Plan p = mhw::parseWindowLayoutPlan(
        QStringLiteral("player-bars,monster-conditions"));
    const QByteArray worldRow =
        mhw::serializeWindowLayoutPlanRow(mhw::GameId::World, p);
    check(worldRow == QByteArray("layout.windows.world=player-bars,"
                                 "monster-conditions\n"),
          "the world row is one line with a trailing newline");

    // Empty plan still writes the row — "no keys" is a statement, its
    // absence is not.
    const QByteArray emptyWorld =
        mhw::serializeWindowLayoutPlanRow(mhw::GameId::World, Plan());
    check(emptyWorld == QByteArray("layout.windows.world=\n"),
          "an empty plan still writes its (empty) row, so 'all merged' "
          "survives a save");

    const QByteArray riseRow =
        mhw::serializeWindowLayoutPlanRow(mhw::GameId::Rise, p);
    check(riseRow.startsWith("layout.windows.rise="),
          "the rise row carries the rise key");
    check(worldRow != riseRow,
          "the two games' rows differ");
    check(mhw::layoutWindowsRowKey(mhw::GameId::World)
              != mhw::layoutWindowsRowKey(mhw::GameId::Rise),
          "and so do their keys");
}

// =====================================================================
// 3. TOPOLOGY RULES THE OVERLAY CONSUMES
// =====================================================================
void test_retainedMaskDisjointness()
{
    const uint32_t kAll = mhw::PlayerSection::kAll;

    // Single: the mother keeps the user's mask VERBATIM (the pixel
    // identity promise), and keeps the filter bits.
    const Plan none;
    check(none.retainedMask(mhw::kLayoutParentPlayer, kAll) == kAll,
          "an empty plan returns the original mask unchanged");

    // Split: every claim leaves, exactly like playerPanelMaskFor(Split).
    const Plan all = mhw::defaultWindowLayoutPlan(Mode::Split);
    check(all.retainedMask(mhw::kLayoutParentPlayer, kAll)
              == mhw::PlayerSection::playerPanelMaskFor(Mode::Split, kAll),
          "Split plan retained mask == seed playerPanelMaskFor(Split)");
    check(all.retainedMask(mhw::kLayoutParentMonster,
                           mhw::MonsterSection::kAll)
              == mhw::MonsterSection::monsterPanelMaskFor(
                     Mode::Split, mhw::MonsterSection::kAll),
          "Split plan retained mask == seed monsterPanelMaskFor(Split) "
          "(Tenderize is a filter and stays with the mother)");
    check(all.retainedMask(mhw::kLayoutParentDamage,
                           mhw::DamageSection::kAll)
              == mhw::DamageSection::damagePanelMaskFor(
                     Mode::Split, mhw::DamageSection::kAll),
          "Split plan retained mask == seed damagePanelMaskFor(Split) "
          "(OtherMembers is a filter and stays with the mother)");

    // Mixed, ONE row out: exactly that row's claim leaves, nothing
    // else. This is the "单项归并" contract.
    Plan oneRow;
    check(oneRow.setDetached(QStringLiteral("player-bars"), true),
          "detach player-bars");
    const uint32_t retained = oneRow.retainedMask(
        mhw::kLayoutParentPlayer, kAll);
    check((retained & mhw::PlayerSection::Bars) == 0,
          "the detached row's bit is GONE from the mother mask");
    check((retained & mhw::PlayerSection::Debuff)
              == mhw::PlayerSection::Debuff,
          "a merged row's bit stays with the mother");
    check(retained
              == (kAll & ~mhw::PlayerSection::Bars),
          "and NOTHING else changed — no second row was swallowed");

    // The mask handed to a detached sub-window is the plan's own
    // claim: mother and window can never draw the same bit.
    const uint32_t claim = mhw::kPlayerSplitTargets[
        mhw::PlayerSplitSlot::Bars].mask;
    check((retained & claim) == 0,
          "mother retained mask and the detached claim are DISJOINT");
    check((oneRow.retainedMask(mhw::kLayoutParentPlayer, kAll) | claim)
              == kAll,
          "mother + detached claims reassemble the original claim set");

    // OtherMembers / Tenderize are filters, never a row: a plan that
    // detaches every row still leaves them in the mother mask.
    const uint32_t monsterAll = mhw::MonsterSection::kAll;
    const uint32_t kept = all.retainedMask(mhw::kLayoutParentMonster,
                                           monsterAll);
    check((kept & mhw::MonsterSection::Tenderize)
              == mhw::MonsterSection::Tenderize,
          "Tenderize survives a full detach (real filter, not a shell)");
    const uint32_t dmgKept =
        all.retainedMask(mhw::kLayoutParentDamage,
                         mhw::DamageSection::kAll);
    check((dmgKept & mhw::DamageSection::OtherMembers)
              == mhw::DamageSection::OtherMembers,
          "OtherMembers survives a full detach (real filter)");
    check(mhw::DamageSection::damagePanelHasBlocks(dmgKept) == false,
          "and the filter-only mother answer hasBlocks == false, so "
          "the mount gate can refuse the empty shell");
    check(mhw::MonsterSection::monsterPanelHasBlocks(kept) == false,
          "same for the monster mother (the 18px empty frame defect)");
}

void test_motherMountRule()
{
    // The one rule main.cpp consumes for the three mother panels:
    // enabled = !disabledByFlag && (retained mask has a block ||
    // nothing was detached for this parent). The second arm is what
    // reproduces seed Single (a maskless single panel still enables)
    // while seed Split with a filter-only mask stays disabled.

    // Seed Single, any mask: enabled.
    const Plan none;
    check(mhw::planMotherPanelEnabled(none, mhw::kLayoutParentPlayer,
                                      /*disabledByFlag=*/false,
                                      /*maskHasBlocks=*/false),
          "Single + no blocks: enabled, exactly as the seed's "
          "`mode != Split || hasBlocks`");
    check(!mhw::planMotherPanelEnabled(none, mhw::kLayoutParentPlayer,
                                       /*disabledByFlag=*/true,
                                       /*maskHasBlocks=*/true),
          "--no-player still wins in Single");

    // Seed Split, filter-only retained mask: DISABLED (the empty
    // shell must not mount).
    const Plan all = mhw::defaultWindowLayoutPlan(Mode::Split);
    check(!mhw::planMotherPanelEnabled(all, mhw::kLayoutParentMonster,
                                       false, false),
          "Split + Tenderize-only retained mask: disabled (the "
          "18px-frame regression must not come back)");

    // Split, retained mask with a block (monster keeps Tenderize, but
    // with a block on): enabled.
    check(mhw::planMotherPanelEnabled(all, mhw::kLayoutParentMonster,
                                      false, true),
          "Split + a real block retained: the mother mounts");

    // Mixed: everything merged into one parent behaves like Single.
    Plan mergedNothing;
    check(mhw::planMotherPanelEnabled(mergedNothing,
                                      mhw::kLayoutParentDamage,
                                      false, false),
          "Mixed with no rows detached from damage: mother behaves "
          "like Single");

    // Mixed: rows detached AND the retained mask reduced to a filter
    // -> no shell.
    Plan detachedAll;
    for (const char *key : {"damage-share", "damage-chart",
                            "damage-rows"}) {
        if (!detachedAll.setDetached(QString::fromLatin1(key), true))
            check(false, "registry key must be settable");
    }
    check(!mhw::planMotherPanelEnabled(detachedAll,
                                       mhw::kLayoutParentDamage,
                                       false, false),
          "Mixed with every damage row detached and only "
          "OtherMembers left: no empty frame");

    // Mixed: SOME rows merged back -> the mother keeps their blocks
    // and mounts.
    Plan half;
    half.setDetached(QStringLiteral("damage-share"), true);
    check(half.retainedMask(mhw::kLayoutParentDamage,
                            mhw::DamageSection::kAll)
                  & mhw::DamageSection::Rows,
          "the merged Rows block stays with the mother");
    check(mhw::planMotherPanelEnabled(half, mhw::kLayoutParentDamage,
                                      false, true),
          "and the mother mounts to draw it");
}

void test_seedEquivalenceForPureModes()
{
    // The whole compatibility promise in one table: for Single and
    // Split, plan-driven mother masks EQUAL the seed's mode-driven
    // ones for every panel, and sub-window enable bits equal the
    // seed's `mode == Split`.
    const uint32_t masks[] = {0x0u,
                              mhw::PlayerSection::kAll,
                              mhw::PlayerSection::Debuff
                                  | mhw::PlayerSection::Bars};
    for (const uint32_t m : masks) {
        check(mhw::defaultWindowLayoutPlan(Mode::Single)
                      .retainedMask(mhw::kLayoutParentPlayer, m)
                  == mhw::PlayerSection::playerPanelMaskFor(
                         Mode::Single, m),
              "Single: plan mother mask == seed playerPanelMaskFor");
        check(mhw::defaultWindowLayoutPlan(Mode::Split)
                      .retainedMask(mhw::kLayoutParentPlayer, m)
                  == mhw::PlayerSection::playerPanelMaskFor(
                         Mode::Split, m),
              "Split: plan mother mask == seed playerPanelMaskFor");
    }
    const Plan split = mhw::defaultWindowLayoutPlan(Mode::Split);
    const Plan single;
    for (int row = 0; row < mhw::PlayerSplitSlot::kCount; ++row) {
        check(split.detached(mhw::kLayoutParentPlayer, row)
                  == /* seed: playerSplitOn = (mode == Split) */ true,
              "Split: every player sub-window is enabled");
        check(!single.detached(mhw::kLayoutParentPlayer, row),
              "Single: no player sub-window is enabled");
    }
}

void test_everyModeGameTopologyMatrix()
{
    // Logical positive AND negative for every (mode x game) topology:
    // at least one mother block and at least one detached window must
    // be true, and never the same bit twice.
    const Mode modes[] = {Mode::Single, Mode::Split, Mode::Mixed};
    const mhw::GameId games[] = {mhw::GameId::World, mhw::GameId::Rise};
    for (const Mode mode : modes) {
        for (const mhw::GameId game : games) {
            const Plan plan = mhw::resolveWindowLayoutPlan(
                game, mode, QString());
            // Negative: a Single plan detaches nothing anywhere.
            const bool anyDetached = [&] {
                for (int s = 0; s < mhw::kLayoutParentCount; ++s)
                    for (int r = 0;
                         r < mhw::layoutParentRowCount(s); ++r)
                        if (plan.detached(s, r))
                            return true;
                return false;
            }();
            check(anyDetached == (mode != Mode::Single),
                  "Single detaches nothing; Split/Mixed detach "
                  "something, in both games");

            // Disjointness per parent, whatever the mask.
            const uint32_t pm = mhw::PlayerSection::kAll;
            const uint32_t retained =
                plan.retainedMask(mhw::kLayoutParentPlayer, pm);
            uint32_t claims = 0;
            for (int r = 0; r < mhw::PlayerSplitSlot::kCount; ++r)
                if (plan.detached(mhw::kLayoutParentPlayer, r))
                    claims |= mhw::kPlayerSplitTargets[r].mask;
            check((retained & claims) == 0,
                  "no bit is drawn by both mother and window");
        }
    }
}

// =====================================================================
// 6. D5 — THE THREE *PanelMaskFor() AND THE PLAN ANSWER THE SAME THING
// =====================================================================
//
// The audit measured three DIFFERENT Mixed answers from three functions
// that are asked one question (000001ff / 0000007f / 00000008 for the
// three kAll masks, where the plan says 00000000 / 00000020 /
// 00000008): player and monster walked the Single arm, damage walked
// the Split arm. Two things are pinned here: the literal expectation
// for Mixed, and — the part that keeps the future honest — that in
// EVERY mode and for every mask, `*PanelMaskFor(mode, mask)` equals
// `defaultWindowLayoutPlan(mode).retainedMask(parent, mask)`, which is
// what main.cpp actually feeds the panels. If someone re-specialises one
// of the three for a fourth mode, this fails.
void test_d5ThreeMaskFunctionsAgreeWithThePlan()
{
    const uint32_t kP = mhw::PlayerSection::kAll;    // 0x000001ff
    const uint32_t kM = mhw::MonsterSection::kAll;   // 0x0000007f
    const uint32_t kD = mhw::DamageSection::kAll;    // 0x0000000f

    // (a) The audit's literal numbers for Mixed's DEFAULT plan (which is
    //     the all-detached fallback), written as literals so the check
    //     does not quietly follow a mutation of the table it reads.
    check(mhw::PlayerSection::playerPanelMaskFor(Mode::Mixed, kP) == 0x00000000u,
          "D5: playerPanelMaskFor(Mixed, kAll) == 00000000 (was 000001ff)");
    check(mhw::MonsterSection::monsterPanelMaskFor(Mode::Mixed, kM)
              == 0x00000020u,
          "D5: monsterPanelMaskFor(Mixed, kAll) == 00000020 (Tenderize "
          "stays; was 0000007f)");
    check(mhw::DamageSection::damagePanelMaskFor(Mode::Mixed, kD)
              == 0x00000008u,
          "D5: damagePanelMaskFor(Mixed, kAll) == 00000008 (OtherMembers "
          "stays; was 00000008 and stays)");

    // (b) The three now walk the SAME arm for Mixed: Split's.
    const uint32_t masks[] = {0x0u,
                              0xFFFFFFFFu,
                              0xDEADBEEFu,
                              kP,
                              mhw::PlayerSection::Debuff
                                  | mhw::PlayerSection::Bars,
                              1u};
    for (const uint32_t m : masks) {
        check(mhw::PlayerSection::playerPanelMaskFor(Mode::Mixed, m)
                  == mhw::PlayerSection::playerPanelMaskFor(Mode::Split, m),
              "D5: player Mixed answer == its Split answer (one arm, three "
              "functions)");
        check(mhw::MonsterSection::monsterPanelMaskFor(Mode::Mixed, m)
                  == mhw::MonsterSection::monsterPanelMaskFor(Mode::Split, m),
              "D5: monster Mixed answer == its Split answer");
        check(mhw::DamageSection::damagePanelMaskFor(Mode::Mixed, m)
                  == mhw::DamageSection::damagePanelMaskFor(Mode::Split, m),
              "D5: damage Mixed answer == its Split answer");
    }

    // (c) The real contract: for every mode, the plan-derived mask
    //     (what main.cpp hands the panel) and the mode-derived mask
    //     (what every other caller and every older test reads) are the
    //     same number — bit for bit.
    const Mode modes[] = {Mode::Single, Mode::Split, Mode::Mixed};
    for (const Mode mode : modes) {
        const Plan plan = mhw::defaultWindowLayoutPlan(mode);
        for (const uint32_t m : masks) {
            check(plan.retainedMask(mhw::kLayoutParentPlayer, m)
                      == mhw::PlayerSection::playerPanelMaskFor(mode, m),
                  "plan.retainedMask(player) == playerPanelMaskFor(mode) "
                  "in every mode");
            check(plan.retainedMask(mhw::kLayoutParentMonster, m)
                      == mhw::MonsterSection::monsterPanelMaskFor(mode, m),
                  "plan.retainedMask(monster) == monsterPanelMaskFor(mode) "
                  "in every mode");
            check(plan.retainedMask(mhw::kLayoutParentDamage, m)
                      == mhw::DamageSection::damagePanelMaskFor(mode, m),
                  "plan.retainedMask(damage) == damagePanelMaskFor(mode) "
                  "in every mode");
        }
    }
}

// =====================================================================
// 7. THE FULL MATRIX — (mode x game) x three parents, masks AND GATES
// =====================================================================
//
// What the overlay topology promises for every combination, asserted
// from the pieces the production block in main.cpp reads and NOTHING
// else (retainedMask, windowLayoutRowMask, planMotherPanelEnabled,
// detached), so a wiring mistake in main.cpp cannot make these pass by
// accident while its own copy of the rule is wrong — §8 checks the real
// binary against the same expectations.
void test_everyModeGameMaskUnionAndGates()
{
    const Mode modes[] = {Mode::Single, Mode::Split, Mode::Mixed};
    const mhw::GameId games[] = {mhw::GameId::World, mhw::GameId::Rise};

    struct ParentSpec {
        int slot;
        const char *name;
        uint32_t kAll;
        bool (*hasBlocks)(uint32_t);
        int rowCount;
    };
    const ParentSpec parents[] = {
        {mhw::kLayoutParentPlayer, "player", mhw::PlayerSection::kAll,
         [](uint32_t m) { return mhw::PlayerSection::playerPanelHasBlocks(m); },
         mhw::PlayerSection::PlayerSplitSlot::kCount},
        {mhw::kLayoutParentMonster, "monster", mhw::MonsterSection::kAll,
         [](uint32_t m) { return mhw::MonsterSection::monsterPanelHasBlocks(m); },
         mhw::MonsterSplitSlot::kCount},
        {mhw::kLayoutParentDamage, "damage", mhw::DamageSection::kAll,
         [](uint32_t m) { return mhw::DamageSection::damagePanelHasBlocks(m); },
         mhw::DamageSplitSlot::kCount},
    };
    const uint32_t maskChoices[] = {0u, 0xFFFFFFFFu, 1u, 0xFFu, 0xDEADBEEFu};

    for (const Mode mode : modes) {
        for (const mhw::GameId game : games) {
            const Plan plan = mhw::resolveWindowLayoutPlan(game, mode,
                                                           QString());


            for (const ParentSpec &par : parents) {
                for (const uint32_t mask : maskChoices) {
                    // The claims of the rows THIS parent detached.
                    uint32_t claims = 0;
                    for (int r = 0; r < par.rowCount; ++r)
                        if (plan.detached(par.slot, r))
                            claims |= mhw::windowLayoutRowMask(par.slot, r);

                    const uint32_t retained =
                        plan.retainedMask(par.slot, mask);

                    // (1) EXACT rule: user mask minus the detached claims.
                    check(retained == (mask & ~claims),
                          "retained == user mask & ~detached claims "
                          "(the D5 rule, in every mode x game x mask)");

                    // (2) Nothing is drawn twice: the mother shares no bit
                    //     with a detached window's own drawing mask.
                    check((retained & claims) == 0,
                          "mother mask and the detached claims are disjoint "
                          "(no block appears on two surfaces)");

                    // (3) Nothing is dropped: mother ∪ every detached
                    //     window's effective mask (claim & user mask) is
                    //     EXACTLY the user's mask, for every mask — not a
                    //     superset (a bit nobody asked for must not appear)
                    //     and not a subset (a bit must not vanish).
                    uint32_t drawn = retained;
                    for (int r = 0; r < par.rowCount; ++r)
                        if (plan.detached(par.slot, r))
                            drawn |= (mhw::windowLayoutRowMask(par.slot, r)
                                      & mask);
                    check(drawn == mask,
                          "mother ∪ detached windows == the user mask "
                          "exactly (union covers it, contributes nothing)");

                    // (4) The mount gate, spelled from the same pieces:
                    //     !flag && (blocks remain || nothing left this
                    //     parent). With no flag here, and using kAll as
                    //     the mask (the one every panel is shipped with).
                    const uint32_t retainedAll =
                        plan.retainedMask(par.slot, par.kAll);
                    const bool hasBlocks = par.hasBlocks(retainedAll);
                    bool anyDetachedFromParent = false;
                    for (int r = 0; r < par.rowCount; ++r)
                        anyDetachedFromParent =
                            anyDetachedFromParent
                            || plan.detached(par.slot, r);
                    const bool mounted = mhw::planMotherPanelEnabled(
                        plan, par.slot, /*disabledByFlag=*/false, hasBlocks);
                    check(mounted == (hasBlocks || !anyDetachedFromParent),
                          "mount gate == hasBlocks || nothing detached "
                          "(Single mounts, Split's filter-only mother does "
                          "not)");

                    // (5) THE LITERAL MATRIX for kAll, the shape a real
                    //     run has: Single keeps every block and mounts;
                    //     Split and Mixed's default hand all rows out, so
                    //     only the filter bit survives and NO mother
                    //     mounts an empty frame.
                    if (mask == maskChoices[1]) {   // 0xFFFFFFFF
                        const uint32_t wantMask =
                            mode == Mode::Single ? par.kAll
                            : (par.slot == mhw::kLayoutParentPlayer ? 0x00000000u
                               : par.slot == mhw::kLayoutParentMonster
                                   ? 0x00000020u
                                   : 0x00000008u);
                        const uint32_t got =
                            plan.retainedMask(par.slot, par.kAll);
                        check(got == wantMask,
                              "kAll literal matrix: single keeps kAll; "
                              "split/mixed keep 00000000 / 00000020 / "
                              "00000008");
                        if (mode == Mode::Single)
                            check(mounted,
                                  "Single: every mother with a block mounts");
                        else
                            check(!mounted,
                                  "split/mixed default: a mother left with "
                                  "only its filter bit does NOT mount");
                    }
                }
            }

            // (6) The ELEVEN enable gates, per mode with no CLI override:
            //     Single -> none, Split -> all, Mixed default -> all
            //     (Mixed's default plan is allDetached, and a Mixed run
            //     that wants fewer windows says so through the conf row
            //     or --detached-windows; §8 exercises those on the real
            //     binary).
            int enabled = 0;
            for (int i = 0; i < mhw::kWindowLayoutRowCount; ++i) {
                const mhw::WindowLayoutRow &row = mhw::kWindowLayoutRows[i];
                if (plan.detached(row.parentSlot, row.tableRow))
                    ++enabled;
            }
            const int wantEnabled = (mode == Mode::Single)
                ? 0 : mhw::kWindowLayoutRowCount;
            check(enabled == wantEnabled,
                  "window enable count: Single=0, Split/Mixed default=11");
        }
    }
}

// =====================================================================
// 8. THE DECISION CHAIN — CLI, conf row, game isolation, empty lists
// =====================================================================
void test_cliOverrideAndGameIsolation()
{
    const QString conf = QStringLiteral(
        "layout.windows.world=player-bars,monster-identity\n"
        "layout.windows.rise=player-info\n");

    auto detachedSet = [](const Plan &p) {
        QStringList keys = p.detachedKeys();
        keys.sort();
        return keys;
    };

    // (a) The conf row, per game. World reads World's row and Rise reads
    //     Rise's — one file, two answers, no leakage in either direction.
    const Plan world = mhw::resolveWindowLayoutPlan(
        mhw::GameId::World, Mode::Mixed, conf);
    const Plan rise = mhw::resolveWindowLayoutPlan(
        mhw::GameId::Rise, Mode::Mixed, conf);
    check(detachedSet(world)
              == QStringList({QStringLiteral("monster-identity"),
                              QStringLiteral("player-bars")}),
          "conf: World's Mixed run detaches exactly its own row's keys");
    check(detachedSet(rise)
              == QStringList({QStringLiteral("player-info")}),
          "conf: Rise's Mixed run detaches exactly ITS row — reading "
          "World never returns Rise's list");

    // (b) Single and Split never read the Mixed row, so a saved Mixed
    //     list cannot leak into `--layout single`.
    check(mhw::resolveWindowLayoutPlan(mhw::GameId::World, Mode::Single,
                                       conf).detachedKeys().isEmpty(),
          "Single ignores layout.windows.world entirely");
    check(mhw::resolveWindowLayoutPlan(mhw::GameId::World, Mode::Split,
                                       conf).detachedKeys().size()
              == mhw::kWindowLayoutRowCount,
          "Split ignores layout.windows.world entirely (all detached)");

    // (c) Explicit CLI wins, IN EVERY MODE.
    const std::optional<QString> oneKey =
        QStringLiteral("player-info");
    const Plan cliInSingle = mhw::resolveWindowLayoutPlan(
        mhw::GameId::World, Mode::Single, conf, oneKey);
    check(detachedSet(cliInSingle)
              == QStringList({QStringLiteral("player-info")}),
          "--detached-windows wins in SINGLE: exactly that key, no conf, "
          "no mode default");
    const Plan cliInMixed = mhw::resolveWindowLayoutPlan(
        mhw::GameId::World, Mode::Mixed, conf, oneKey);
    check(detachedSet(cliInMixed)
              == QStringList({QStringLiteral("player-info")}),
          "--detached-windows wins over the conf row in MIXED");

    // (d) An EXPLICIT EMPTY list means everything merged — the one way
    //     to say "no detached windows" that is not simply Single (it
    //     still names the mixed settings domain on the overlay side).
    const std::optional<QString> emptyKeys = QString();
    const Plan allMerged = mhw::resolveWindowLayoutPlan(
        mhw::GameId::World, Mode::Mixed, conf, emptyKeys);
    check(allMerged.detachedKeys().isEmpty(),
          "explicit --detached-windows= (empty) == every window merged");
    uint32_t drawsNothing = allMerged.retainedMask(mhw::kLayoutParentPlayer,
                                                   mhw::PlayerSection::kAll);
    check(drawsNothing == mhw::PlayerSection::kAll,
          "…and the mother then draws every block itself (no window "
          "stole any)");

    // (e) An unparseable list does NOT win: it falls back to the mode
    //     default rather than applying half of it (window_layout_plan.h's
    //     atomic-parse rule, which is what keeps a typo from half-splitting
    //     a run).
    const std::optional<QString> badKeys =
        QStringLiteral("player-bars,player-bars,not-a-key");
    const Plan bad = mhw::resolveWindowLayoutPlan(
        mhw::GameId::World, Mode::Mixed, conf, badKeys);
    check(bad.detachedKeys().size() == mhw::kWindowLayoutRowCount,
          "an invalid --detached-windows list does not win: the Mixed "
          "default topology is used instead");

    // (f) An explicit EMPTY conf row means all merged too, and an
    //     UNUSABLE row means the mode default (never a guessed subset).
    check(mhw::resolveWindowLayoutPlan(
              mhw::GameId::World, Mode::Mixed,
              QStringLiteral("layout.windows.world=\n"))
              .detachedKeys().isEmpty(),
          "an explicit empty conf row == every window merged");
    check(mhw::resolveWindowLayoutPlan(
              mhw::GameId::World, Mode::Mixed,
              QStringLiteral("layout.windows.world=nope\n"))
              .detachedKeys().size() == mhw::kWindowLayoutRowCount,
          "an unusable conf row falls back to the mode default, not to a "
          "guess");

    // (g) Game isolation WITH an explicit CLI list: the list is a
    //     topology, not a per-game value, so it applies to whichever
    //     game was asked for — and the two games still never mix their
    //     conf rows (asserted above). What must not happen is Rise
    //     answering World's conf row; re-asserted here with a mask so a
    //     future per-game CLI cannot hide behind the same assertion.
    const Plan riseNoCli = mhw::resolveWindowLayoutPlan(
        mhw::GameId::Rise, Mode::Mixed,
        QStringLiteral("layout.windows.world=player-bars\n"));
    check(riseNoCli.detachedKeys().size() == mhw::kWindowLayoutRowCount,
          "World's row cannot reach a Rise run (no CLI present)");
}

} // namespace

// =====================================================================
// 4'. B2 — an unusable conf row is REPORTED, once per distinct line.
// =====================================================================
//
// The defect: resolveWindowLayoutPlan() scans past a `layout.windows.*`
// row that does not parse (the right READ rule — the first usable row for
// the game wins) and said NOTHING, so a hand-edited line simply never
// took effect. This pins three things at once: the report exists, it
// quotes the user's own line, and the FREQUENCY is bounded (a resolver
// called repeatedly must not turn one typo into a log flood — the
// de-dup state in window_layout_plan_detail is documented as
// test-reachable precisely so this can be asserted from a clean slate).
//
// MUTATION: delete the reportUnusableConfRow(line) call in
// resolveWindowLayoutPlan() — (a) fails immediately.
namespace {

QStringList *warningSink = nullptr;

void captureWarnings(QtMsgType type, const QMessageLogContext &ctx,
                     const QString &msg)
{
    Q_UNUSED(type);
    Q_UNUSED(ctx);
    if (warningSink)
        warningSink->append(msg);
}

void resetConfRowReporting()
{
    mhw::window_layout_plan_detail::reportedUnusableConfRows.clear();
    mhw::window_layout_plan_detail::unusableConfRowsCapReported = false;
}

void test_unusableConfRowIsReportedOnce()
{
    resetConfRowReporting();

    QStringList warned;
    warningSink = &warned;
    const QtMessageHandler previous =
        qInstallMessageHandler(captureWarnings);

    const QString bad1 = QStringLiteral("layout.windows.world=not-a-key");
    const QString bad2 =
        QStringLiteral("layout.windows.world=player-bars,player-bars");
    const QString mixedConf = bad1 + QLatin1Char('\n') + bad2
        + QLatin1Char('\n')
        + QStringLiteral("layout.windows.world=player-bars\n");

    // (a) FIVE resolves of the same file: one warning per DISTINCT
    //     refused line, not one per call.
    for (int i = 0; i < 5; ++i)
        (void)mhw::resolveWindowLayoutPlan(mhw::GameId::World, Mode::Mixed,
                                           mixedConf);
    // The sixth call is also the one whose RESULT is asserted: scanning
    // past the two refused rows must still land on the usable one.
    const Plan resolved = mhw::resolveWindowLayoutPlan(
        mhw::GameId::World, Mode::Mixed, mixedConf);
    check(resolved.detachedKeys()
              == QStringList({QStringLiteral("player-bars")}),
          "B2: the read rule is unchanged — the usable row behind two "
          "refused ones still wins");
    check(warned.size() == 2,
          "B2: six resolves of one file produce exactly two reports — "
          "one per REFUSED line, de-duplicated across calls (a per-call "
          "warning would be 12)");
    check(warned.value(0).contains(bad1)
              && warned.value(1).contains(bad2),
          "B2: each report quotes the exact line the user wrote");
    check(warned.value(0).contains(QStringLiteral("Ignoring invalid conf row"))
              && warned.value(0).contains(QStringLiteral("NOT applied")),
          "B2: the wording says the line was NOT applied, not merely "
          "\"invalid\"");

    // (b) A file with only usable rows stays silent — reporting is about
    //     refused lines, not about announcing every read.
    const Plan usable = mhw::resolveWindowLayoutPlan(
        mhw::GameId::World, Mode::Mixed,
        QStringLiteral("layout.windows.world=player-bars\n"));
    Q_UNUSED(usable);
    check(warned.size() == 2,
          "B2: a fully usable conf produces no report at all");

    // (c) An invalid CLI list short-circuits the conf read entirely, so
    //     this function reports NOTHING for it — main.cpp owns that
    //     warning (it names the CLI option and the discarded conf row).
    const std::optional<QString> badCli =
        QStringLiteral("player-bars,not-a-key");
    const Plan fromBadCli = mhw::resolveWindowLayoutPlan(
        mhw::GameId::World, Mode::Mixed, mixedConf, badCli);
    check(fromBadCli.detachedKeys().size() == mhw::kWindowLayoutRowCount
              && warned.size() == 2,
          "B2: the CLI branch falls back without a conf-side report (one "
          "run, one story — main.cpp's warning covers it)");

    // (d) The BOUND. A conf that somehow carries 17 distinct refused
    //     lines reports the first kMaxReportedUnusableConfRows, announces
    //     the cap exactly once, and then stays quiet — a caller that
    //     re-reads an ever-changing file cannot flood the log.
    QStringList many;
    for (int i = 0; i < 17; ++i)
        many.append(QStringLiteral("layout.windows.world=not-a-key-%1")
                        .arg(i));
    const QString manyConf = many.join(QLatin1Char('\n'))
        + QLatin1Char('\n');
    for (int pass = 0; pass < 2; ++pass)
        (void)mhw::resolveWindowLayoutPlan(mhw::GameId::World, Mode::Mixed,
                                           manyConf);
    const int capMessages = [&] {
        int n = 0;
        for (const QString &m : warned)
            if (m.contains(QStringLiteral("further invalid layout.windows")))
                ++n;
        return n;
    }();
    check(mhw::window_layout_plan_detail::reportedUnusableConfRows.size()
              == mhw::window_layout_plan_detail::kMaxReportedUnusableConfRows,
          "B2: the de-dup list stops growing at its documented cap");
    check(capMessages == 1,
          "B2: the cap is announced exactly once across two passes "
          "(not once per call)");
    const int totalWarned = warned.size();
    (void)mhw::resolveWindowLayoutPlan(mhw::GameId::World, Mode::Mixed,
                                       manyConf);
    check(warned.size() == totalWarned,
          "B2: once capped, further calls emit nothing at all");

    qInstallMessageHandler(previous);
    warningSink = nullptr;
    resetConfRowReporting();   // leave the process state clean
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::printf("window_layout_plan_tests: the shared Mixed plan, its "
                "resolve chain and the topology rules\n");
    std::printf("================================================================\n");

    test_red_mixedParsesOnTheSeed();
    test_parseAndSerialize();
    test_defaultPlans();
    test_resolve();
    test_serializeRow();
    test_retainedMaskDisjointness();
    test_motherMountRule();
    test_seedEquivalenceForPureModes();
    test_everyModeGameTopologyMatrix();
    test_d5ThreeMaskFunctionsAgreeWithThePlan();
    test_everyModeGameMaskUnionAndGates();
    test_cliOverrideAndGameIsolation();
    test_unusableConfRowIsReportedOnce();   // B2: silent conf fallback

    std::printf("================================================================\n");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
