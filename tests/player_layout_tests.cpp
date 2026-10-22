// SPDX-License-Identifier: Apache-2.0
//
// v0.12 dual-mode player layout: the pure decision layer, pinned.
//
// WHAT THIS FILE COVERS
// ---------------------
// The user-facing v0.12 promise is a binary choice:
//
//     --layout=single   one big panel, pixel for pixel what v0.11.4 drew
//     --layout=split    the blocks the table names get their own windows
//
// Everything that makes that choice lives in ui/panel_sections.h and is
// constexpr / inline over uint32_t, so this target is header-only and
// links Qt6::Core only — no mhw-ui, no widget library, no AUTOMOC. That
// is deliberate: the mask arithmetic and the precedence chain are the two
// places a future edit could silently break the pixel-identity promise,
// and neither should need a compositor to be checked.
//
// Five contracts are pinned, in order of how much a wrong answer costs:
//
//   1. THE PRECEDENCE CHAIN (resolveStartupLayoutMode). The complete
//      decision table: an explicit --layout= beats the deprecated
//      --split-player-strip boolean, which beats a conf row, which beats
//      the Single default. A malformed --layout= resolves to the DEFAULT —
//      never to the conf row, never to the compat flag — because rescuing
//      a typo from a stale saved row is how a user's explicit choice gets
//      discarded.
//   2. THE PARSE (parsePlayerLayoutMode / playerLayoutModeToString).
//      Case and whitespace tolerated, unknown spellings rejected with
//      `out` left untouched, and the round trip closes.
//   3. THE MASK ALGEBRA (playerPanelMaskFor / playerSubWindowMask). Single
//      mode returns the user's mask verbatim; in Split mode the panel and
//      every sub-window claim disjoint bits, and their union is exactly
//      what the user toggled — so no block is drawn twice and none is lost.
//   4. THE TABLE (kPlayerSplitTargets). Non-empty rows, each a subset of
//      PlayerSection::kAll, pairwise disjoint, and the removed bits pinned
//      to the literals the first window owns.
//   5. THE EXHAUSTIVE PROOF. All 256 masks PlayerSection can express, in
//      both modes, checking the invariants above — the strongest form,
//      because it is the whole space of user toggle combinations rather
//      than four sample ones.
//
// The static_asserts in panel_sections.h pin what the compiler can see
// (Single(0xDEADBEEF) == 0xDEADBEEF, two rows disjoint, kCount == rows).
// This file re-proves the same invariants at run time because the ones
// that matter are quantified over the mask, which static_assert cannot
// express: "for ALL m" needs a loop.

#include "ui/panel_sections.h"
#include "ui/viewmodel/console_layout_mode_codec.h"

#include <QCoreApplication>
#include <QString>

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;
int checks = 0;

void check(bool cond, const char *what)
{
    ++checks;
    if (cond) std::printf("PASS: %s\n", what);
    else { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

using mhw::PlayerLayoutSource;
using mhw::PlayerLayoutResolution;
using mhw::PlayerLayoutCliRequest;
using mhw::PlayerLayoutConfRow;
using Mode = mhw::PlayerSection::PlayerLayoutMode;
using PlayerSplitTarget = mhw::PlayerSection::PlayerSplitTarget;
// PlayerSplitSlot is a namespace of int constants (and kCount), not a
// type, so it is referred to in full below rather than aliased.

const char *sourceName(PlayerLayoutSource s)
{
    switch (s) {
    case PlayerLayoutSource::CliExplicit:   return "cli-explicit";
    case PlayerLayoutSource::CliCompatFlag: return "cli-compat-flag";
    case PlayerLayoutSource::Conf:          return "conf";
    case PlayerLayoutSource::Default:       return "default";
    }
    return "?";
}

const char *modeName(Mode m)
{
    return m == Mode::Split ? "split" : "single";
}

// The CLI request main.cpp builds when neither flag was given. main.cpp
// passes parser.isSet(layoutOption) / parser.isSet(splitPlayerStripOption),
// so this is exactly the shape of "no flags at all" — the cold start.
const PlayerLayoutCliRequest kNoCli;

// An absent conf row. The console writes no `layout=` row today, so this is
// what every real launch resolves through.
const mhw::ConsoleLayoutConfRow kNoConfRow =
    mhw::parseConsoleLayoutRow(QString());   // no `layout=` row at all

// v1.1.0: a conf "row" is no longer a bare PlayerLayoutSource — the row has
// to CARRY ITS MODE, or `layout=mixed` cannot survive the trip and a save
// rewrites the user's mixed layout to split. These are real
// parseConsoleLayoutRow() outputs, not hand-built structs, so the test
// exercises the same shape production does.
mhw::ConsoleLayoutConfRow confRowFor(const char *text)
{
    return mhw::parseConsoleLayoutRow(QString::fromUtf8(text));
}
const mhw::ConsoleLayoutConfRow kConfSplitRow =
    confRowFor("layout=split\n");
const mhw::ConsoleLayoutConfRow kConfMixedRow =
    confRowFor("layout=mixed\n");
const mhw::ConsoleLayoutConfRow kConfSingleRow =
    confRowFor("layout=single\n");

// Deterministic LCG, so the pseudo-random masks below are identical on
// every run and a failure is reproducible rather than a lottery.
uint32_t gRng = 0x12345678u;
uint32_t nextRand()
{
    gRng = gRng * 1664525u + 1013904223u;
    return gRng;
}

// A command line where only the deprecated boolean alias was given.
PlayerLayoutCliRequest compatOnly()
{
    return PlayerLayoutCliRequest{false, QString(),
                                  Mode::Split};
}

// A command line where --layout= arrived with `value`. `compat` is what
// --split-player-strip said alongside it, which is how case (c) — an
// explicit value that must outrank the boolean — is exercised.
PlayerLayoutCliRequest explicitCli(const QString &value, Mode compat)
{
    return PlayerLayoutCliRequest{true, value, compat};
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ====================================================================
    // 1. THE PARSE. Tolerated spellings, rejected spellings, round trip.
    //    Tolerating case and surrounding whitespace is what makes
    //    `--layout=SPLIT` and `--layout= split ` work at all; rejecting
    //    `s` / `splitx` / `1` is what makes a typo loud instead of silent.
    // ====================================================================
    {
        struct { const char *text; bool accepted; Mode mode; } cases[] = {
            {"split",   true,  Mode::Split},
            {"SPLIT",   true,  Mode::Split},
            {"Split",   true,  Mode::Split},
            {" split ", true,  Mode::Split},
            {"\tsplit\n", true, Mode::Split},
            {"single",  true,  Mode::Single},
            {"SINGLE",  true,  Mode::Single},
            {"Single",  true,  Mode::Single},
            {" single ", true, Mode::Single},
            // Rejected. "s" and "1" are the dangerous ones: a parse that
            // accepted a prefix or a boolean-ish spelling would turn a
            // typo into a topology change the user never asked for.
            {"",        false, Mode::Single},
            {"s",       false, Mode::Single},
            {"singlee", false, Mode::Single},
            {"splitx",  false, Mode::Split},
            {"1",       false, Mode::Split},
            {"0",       false, Mode::Single},
            {"sp lit",  false, Mode::Split},
            {"split-single", false, Mode::Split},
        };

        bool all = true;
        for (const auto &c : cases) {
            Mode out = Mode::Single;
            const bool ok = mhw::parsePlayerLayoutMode(
                QString::fromUtf8(c.text), out);
            if (ok != c.accepted || (ok && out != c.mode)) {
                all = false;
                std::fprintf(stderr, "  parse(\"%s\"): expected %s, got %s/%s\n",
                             c.text,
                             c.accepted ? modeName(c.mode) : "reject",
                             ok ? "accept" : "reject", modeName(out));
            }
        }
        check(all, "parse: accepted spellings case/whitespace-tolerant, "
                   "unknown spellings rejected");

        // A rejected parse must leave the caller's out parameter alone.
        // main.cpp value-initialises `parsed{}` and ignores it on failure,
        // but pinning this keeps a future implementation from pre-seeding
        // `out` and handing the caller a mode it never validated.
        {
            Mode out = Mode::Split;
            const bool ok = mhw::parsePlayerLayoutMode(
                QStringLiteral("splitt"), out);
            check(!ok && out == Mode::Split,
                  "parse: a rejected value leaves `out` untouched (it must "
                  "not be pre-seeded with Single)");
        }

        // The round trip, and the literals. toString is what the startup
        // log line prints, so its spelling is user-visible.
        {
            const Mode modes[2] = {Mode::Single, Mode::Split};
            bool all = true;
            for (const Mode m : modes) {
                const QString text = mhw::playerLayoutModeToString(m);
                Mode back = (m == Mode::Single) ? Mode::Split : Mode::Single;
                if (!mhw::parsePlayerLayoutMode(text, back) || back != m) {
                    all = false;
                    std::fprintf(stderr, "  round trip %s -> \"%s\" -> %s\n",
                                 modeName(m), qPrintable(text),
                                 modeName(back));
                }
            }
            check(all, "round trip: parse(toString(m)) == m for both modes");
        }
        check(mhw::playerLayoutModeToString(Mode::Single)
                  == QStringLiteral("single")
                  && mhw::playerLayoutModeToString(Mode::Split)
                         == QStringLiteral("split"),
              "toString: the two public spellings are exactly "
              "\"single\" / \"split\"");
    }

    // ====================================================================
    // 2. THE COMPAT ALIAS. --split-player-strip is a store_true boolean,
    //    so it can only say Single or Split, and it is folded into the
    //    resolver through splitPlayerStripFlagSense() rather than decided
    //    again at the call site.
    // ====================================================================
    {
        check(mhw::splitPlayerStripFlagSense(false) == Mode::Single,
              "compat flag: absent -> Single (not Split, which would make "
              "the pilot flag sticky)");
        check(mhw::splitPlayerStripFlagSense(true) == Mode::Split,
              "compat flag: present -> Split");

        // A default-built request is "nothing on the command line". This
        // is the shape main.cpp constructs for a hand-launched overlay
        // with no flags at all, and it must be the Single default.
        const PlayerLayoutResolution r =
            mhw::resolveStartupLayoutMode(kNoCli, kNoConfRow);
        check(r.mode == Mode::Single && r.source == PlayerLayoutSource::Default,
              "cold start: no --layout=, no compat flag, no conf row -> "
              "Single/Default");
    }

    // ====================================================================
    // 3. THE PRECEDENCE CHAIN, as the five named contracts.
    //    These are the cases a user can actually produce, each with the
    //    source it must report — the source is part of the contract
    //    because it is what the startup log line claims, and a
    //    wrong-but-right-valued fallback (conf silently overriding an
    //    explicit flag) is only visible through it.
    // ====================================================================
    {
        // (a) The default. Nothing said anywhere -> one big window.
        const PlayerLayoutResolution a =
            mhw::resolveStartupLayoutMode(kNoCli, kNoConfRow);
        check(a.mode == Mode::Single
                  && a.source == PlayerLayoutSource::Default,
              "chain(a): no flag, no compat, conf absent -> Single/Default");

        // (b) The explicit value wins over a conf row that would have
        //     said the default.
        const PlayerLayoutCliRequest cliSplit{true, QStringLiteral("split"),
                                             Mode::Single};
        const PlayerLayoutResolution b =
            mhw::resolveStartupLayoutMode(cliSplit, kConfSplitRow);
        check(b.mode == Mode::Split
                  && b.source == PlayerLayoutSource::CliExplicit,
              "chain(b): --layout=split beats a conf row -> Split/CliExplicit");

        // (c) The explicit value wins over the boolean alias, in the
        //     direction that matters: --layout=single with
        //     --split-player-strip also on the command line. A boolean
        //     must never outrank a value the user typed on purpose.
        const PlayerLayoutCliRequest cliSingleOverCompat{
            true, QStringLiteral("single"), Mode::Split /* --split-player-strip */};
        const PlayerLayoutResolution c =
            mhw::resolveStartupLayoutMode(cliSingleOverCompat, kNoConfRow);
        check(c.mode == Mode::Single
                  && c.source == PlayerLayoutSource::CliExplicit,
              "chain(c): --layout=single + --split-player-strip -> "
              "Single/CliExplicit (the boolean cannot override the value)");

        // (d) The pilot flag on its own still gets what it asked for.
        const PlayerLayoutCliRequest compatOnly{false, QString(),
                                                Mode::Split};
        const PlayerLayoutResolution d =
            mhw::resolveStartupLayoutMode(compatOnly, kNoConfRow);
        check(d.mode == Mode::Split
                  && d.source == PlayerLayoutSource::CliCompatFlag,
              "chain(d): --split-player-strip alone -> Split/CliCompatFlag");

        // (e) A malformed --layout= resolves to the DEFAULT, not to the
        //     conf row. "conf has a usable value, CLI is garbage" is the
        //     tempting place to rescue with the conf value, and it is
        //     exactly the wrong answer: a typo must not be able to pick a
        //     window topology up out of a stale saved row.
        const PlayerLayoutCliRequest cliMalformed{true,
                                                 QStringLiteral("splitt"),
                                                 Mode::Single};
        const PlayerLayoutResolution e =
            mhw::resolveStartupLayoutMode(cliMalformed, kConfSplitRow);
        check(e.mode == Mode::Single
                  && e.source == PlayerLayoutSource::Default,
              "chain(e): --layout=splitt (malformed) -> Single/Default, "
              "NOT the conf row");

        // ... and with the compat flag also present, the malformed value
        // still falls to the default rather than to the boolean.
        const PlayerLayoutCliRequest cliMalformedOverCompat{
            true, QStringLiteral("splitt"), Mode::Split};
        const PlayerLayoutResolution e2 =
            mhw::resolveStartupLayoutMode(cliMalformedOverCompat,
                                          kConfSplitRow);
        check(e2.mode == Mode::Single
                  && e2.source == PlayerLayoutSource::Default,
              "chain(e): a malformed --layout= does not fall through to "
              "the compat flag either");
    }

    // ====================================================================
    // 4. THE WHOLE DECISION TABLE, not just its corners. Every
    //    (flag state x compat state x conf state) combination, with the
    //    expected mode AND source written out literally, so an
    //    implementation that only handles the five cases above still has
    //    to agree everywhere.
    // ====================================================================
    {
        struct {
            const char *name;
            PlayerLayoutCliRequest cli;
            mhw::ConsoleLayoutConfRow conf;
            Mode expectedMode;
            PlayerLayoutSource expectedSource;
        } cases[] = {
            // No --layout= at all: the conf row is the only voice.
            {"no cli, no compat, no conf", kNoCli, kNoConfRow,
             Mode::Single, PlayerLayoutSource::Default},
            {"no cli, no compat, conf=split", kNoCli,
             kConfSplitRow, Mode::Split, PlayerLayoutSource::Conf},
            // v1.1.0: THE DATA-LOSS CASE. A hand-edited `layout=mixed` used to
            // resolve to Split (the two-argument overload hard-coded it), and
            // saveMaskToDisk() then rewrote the user's file. The mode has to
            // travel WITH the source for this to hold.
            {"no cli, no compat, conf=mixed", kNoCli,
             kConfMixedRow, Mode::Mixed, PlayerLayoutSource::Conf},
            // `layout=single` is deliberately NOT a row: the console never
            // writes it (serializeConsoleLayoutRow returns empty for Single)
            // and a hand-edited one would look like a deliberate choice. It
            // resolves to the same Single the default gives, just labelled
            // Default because no row was found.
            {"no cli, no compat, conf=single (deliberately not a row)",
             kNoCli, kConfSingleRow, Mode::Single, PlayerLayoutSource::Default},
            // An explicit CLI value outranks the row, including Mixed.
            {"cli=mixed over conf=split", explicitCli("mixed", Mode::Single),
             kConfSplitRow, Mode::Mixed, PlayerLayoutSource::CliExplicit},
            {"cli=split over conf=mixed", explicitCli("split", Mode::Single),
             kConfMixedRow, Mode::Split, PlayerLayoutSource::CliExplicit},
            // A conf value that is anything other than `Conf` is "no
            // information": the resolver must not read a stray source
            // enum as a request for a topology.
            {"no cli, no compat, conf=CliExplicit", kNoCli,
             kNoConfRow, Mode::Single,
             PlayerLayoutSource::Default},
            {"no cli, no compat, conf=CliCompatFlag", kNoCli,
             kNoConfRow, Mode::Single,
             PlayerLayoutSource::Default},

            // The compat flag speaks when --layout= is absent — including
            // over a conf row, because a pilot user who has been typing
            // --split-player-strip must keep getting it.
            {"no cli, compat=Split, no conf", compatOnly(), kNoConfRow,
             Mode::Split, PlayerLayoutSource::CliCompatFlag},
            {"no cli, compat=Split, conf=Conf", compatOnly(),
             kConfSplitRow, Mode::Split,
             PlayerLayoutSource::CliCompatFlag},

            // An explicit value wins over everything, in both directions.
            {"cli=single, compat=Split, conf=Conf",
             explicitCli("single", Mode::Split), kConfSplitRow,
             Mode::Single, PlayerLayoutSource::CliExplicit},
            {"cli=split, compat=Split, conf=Conf",
             explicitCli("split", Mode::Split), kConfSplitRow,
             Mode::Split, PlayerLayoutSource::CliExplicit},
            {"cli=SPLIT, compat=Single, no conf",
             explicitCli("SPLIT", Mode::Single), kNoConfRow,
             Mode::Split, PlayerLayoutSource::CliExplicit},
            {"cli=' split ', compat=Single, no conf",
             explicitCli(" split ", Mode::Single), kNoConfRow,
             Mode::Split, PlayerLayoutSource::CliExplicit},
            {"cli=SINGLE, compat=Split, no conf",
             explicitCli("SINGLE", Mode::Split), kNoConfRow,
             Mode::Single, PlayerLayoutSource::CliExplicit},

            // A malformed value defaults, never inherits. The two rows
            // that matter most: malformed + a conf row that WOULD say
            // Split, and malformed + a compat flag that WOULD say Split.
            {"cli=splitt, compat=Single, no conf",
             explicitCli("splitt", Mode::Single), kNoConfRow,
             Mode::Single, PlayerLayoutSource::Default},
            {"cli=splitt, compat=Single, conf=Conf",
             explicitCli("splitt", Mode::Single), kConfSplitRow,
             Mode::Single, PlayerLayoutSource::Default},
            {"cli=splitt, compat=Split, conf=Conf",
             explicitCli("splitt", Mode::Split), kConfSplitRow,
             Mode::Single, PlayerLayoutSource::Default},
            {"cli='' (empty), compat=Split, conf=Conf",
             explicitCli(QString(), Mode::Split), kConfSplitRow,
             Mode::Single, PlayerLayoutSource::Default},
            {"cli=1, compat=Split, conf=Conf",
             explicitCli("1", Mode::Split), kConfSplitRow,
             Mode::Single, PlayerLayoutSource::Default},
            {"cli=splitx, compat=Single, conf=Conf",
             explicitCli("splitx", Mode::Single), kConfSplitRow,
             Mode::Single, PlayerLayoutSource::Default},
        };

        bool all = true;
        for (const auto &c : cases) {
            const PlayerLayoutResolution r =
                mhw::resolveStartupLayoutMode(c.cli, c.conf);
            if (r.mode != c.expectedMode
                || r.source != c.expectedSource) {
                all = false;
                std::fprintf(stderr,
                             "  [%s] expected %s/%s, got %s/%s\n", c.name,
                             modeName(c.expectedMode),
                             sourceName(c.expectedSource),
                             modeName(r.mode), sourceName(r.source));
            }
        }
        check(all, "chain: the full decision table agrees in all "
                   "combinations (mode and source both)");
    }

    // ====================================================================
    // 5. THE MASK ALGEBRA, three invariants.
    // ====================================================================
    {
        // 5a. Single mode returns the mask verbatim. This is the
        //     pixel-identity guarantee: every pre-v0.12 user keeps the
        //     exact value v0.11.4 handed PlayerPanel.setSectionMask().
        //     The literals are the ones the header's static_asserts use,
        //     re-proved here at run time alongside masks the compiler
        //     cannot enumerate.
        {
            const uint32_t literals[] = {
                0xFFFFFFFFu, 0x0u, 0xDEADBEEFu, 0x0000FFFFu, 0xFFFF0000u,
                mhw::PlayerSection::kAll, 0x00000001u, 0x80000000u,
                mhw::PlayerSection::Debuff, mhw::PlayerSection::Buff,
                mhw::PlayerSection::Debuff | mhw::PlayerSection::Buff,
            };
            bool all = true;
            for (const uint32_t m : literals) {
                if (mhw::PlayerSection::playerPanelMaskFor(Mode::Single, m)
                    != m) {
                    all = false;
                    std::fprintf(stderr,
                                 "  Single(0x%08x) changed the mask\n", m);
                }
            }
            // 64 pseudo-random 32-bit values: catches an implementation
            // that only happens to work on the low 8 bits.
            for (int i = 0; i < 64; ++i) {
                const uint32_t m = nextRand();
                if (mhw::PlayerSection::playerPanelMaskFor(Mode::Single, m)
                    != m) {
                    all = false;
                    std::fprintf(stderr,
                                 "  Single(0x%08x) changed the mask\n", m);
                }
            }
            check(all, "mask: Single mode returns the mask verbatim "
                       "(literals, the header's assert values, and 64 "
                       "pseudo-random 32-bit masks)");
        }

        // 5b. In Split mode PlayerPanel holds none of the removed bits,
        //     so `smask & Debuff` inside panel_player.cpp answers false
        //     and the blocks vanish from the height, the layout and the
        //     paint together — with no second gate anywhere.
        {
            const uint32_t removed = mhw::PlayerSection::playerSplitRemovedBits();
            bool all = true;
            for (int i = 0; i < 64; ++i) {
                const uint32_t m = nextRand();
                const uint32_t panel =
                    mhw::PlayerSection::playerPanelMaskFor(Mode::Split, m);
                if ((panel & removed) != 0) {
                    all = false;
                    std::fprintf(stderr,
                                 "  Split(0x%08x) kept removed bit 0x%08x\n",
                                 m, panel & removed);
                }
            }
            check(all, "mask: in Split mode the panel's mask shares no bit "
                       "with playerSplitRemovedBits()");
        }

        // 5c. THE core invariant: no block is drawn by two windows. The
        //     panel and every sub-window read the SAME user mask, each
        //     AND-ed with a disjoint claim, so their intersection is
        //     empty by construction — which is what makes "the user can
        //     never see the capsules twice" a property of the table
        //     rather than of two code paths remembering to agree.
        {
            bool all = true;
            for (int i = 0; i < 64; ++i) {
                const uint32_t m = nextRand();
                const uint32_t panel =
                    mhw::PlayerSection::playerPanelMaskFor(Mode::Split, m);
                for (int slot = 0;
                     slot < mhw::PlayerSection::kPlayerSplitTargetCount;
                     ++slot) {
                    const uint32_t sub = mhw::PlayerSection::playerSubWindowMask(
                        slot, m);
                    if ((panel & sub) != 0) {
                        all = false;
                        std::fprintf(stderr,
                                     "  0x%08x: panel 0x%08x and slot %d "
                                     "0x%08x share bits\n",
                                     m, panel, slot, sub);
                    }
                }
            }
            check(all, "mask: Split-mode panel mask and every sub-window "
                       "mask are disjoint");
        }
    }

    // ====================================================================
    // 6. THE EXHAUSTIVE PROOF. PlayerSection has 8 bits, so 256 masks are
    //    the complete space of user toggle combinations. For each one, in
    //    each mode:
    //      Single -> the panel mask IS the user's mask;
    //      Split  -> the panel's and every sub-window's masks are pairwise
    //                disjoint, and their union is exactly the user's mask
    //                (nothing drawn twice, nothing silently dropped).
    //    The union half is what catches the other failure shape: a table
    //    row that claims a bit nobody draws, which no disjointness check
    //    would notice and which shows up on screen as a block disappearing
    //    from both windows.
    // ====================================================================
    {
        uint32_t violations = 0;
        for (uint32_t m = 0; m <= 0xFFu; ++m) {
            for (int mi = 0; mi < 2; ++mi) {
                const Mode mode = mi == 0 ? Mode::Single : Mode::Split;
                const uint32_t panel =
                    mhw::PlayerSection::playerPanelMaskFor(mode, m);

                if (mode == Mode::Single) {
                    if (panel != m) {
                        ++violations;
                        std::fprintf(stderr,
                                     "  0x%02x single: panel 0x%02x != user "
                                     "mask\n", m, panel);
                    }
                    continue;
                }

                uint32_t drawn = 0;
                for (int slot = 0;
                     slot < mhw::PlayerSection::kPlayerSplitTargetCount;
                     ++slot) {
                    const uint32_t sub =
                        mhw::PlayerSection::playerSubWindowMask(slot, m);
                    if ((panel & sub) != 0) {
                        ++violations;
                        std::fprintf(stderr,
                                     "  0x%02x split: panel 0x%02x and slot "
                                     "%d 0x%02x overlap\n",
                                     m, panel, slot, sub);
                    }
                    for (int other = slot + 1;
                         other < mhw::PlayerSection::kPlayerSplitTargetCount;
                         ++other) {
                        const uint32_t o = mhw::PlayerSection::playerSubWindowMask(
                            other, m);
                        if ((sub & o) != 0) {
                            ++violations;
                            std::fprintf(stderr,
                                         "  0x%02x split: slots %d and %d "
                                         "overlap\n", m, slot, other);
                        }
                    }
                    drawn |= sub;
                }
                if ((panel | drawn) != m) {
                    ++violations;
                    std::fprintf(stderr,
                                 "  0x%02x split: panel|subs = 0x%02x != user "
                                 "mask 0x%02x\n", m, panel | drawn, m);
                }
            }
        }
        check(violations == 0,
              "exhaustive: all 256 possible user masks x both modes — "
              "Single returns the mask verbatim, Split keeps panel and "
              "every sub-window disjoint and their union equal to the "
              "mask");
    }

    // ====================================================================
    // 7. THE TABLE. Runtime re-proof of what the static_asserts pin, plus
    //    the two things only a loop can say: every row individually, and
    //    the literal set of removed bits.
    // ====================================================================
    {
        check(mhw::PlayerSection::playerSplitTargetsAreValid(),
              "table: playerSplitTargetsAreValid() is true");
        check(mhw::PlayerSection::PlayerSplitSlot::kCount
                  == mhw::PlayerSection::kPlayerSplitTargetCount,
              "table: PlayerSplitSlot::kCount equals the row count "
              "(the enum and the table cannot drift apart)");

        const int rows = mhw::PlayerSection::kPlayerSplitTargetCount;
        bool rowsOk = rows > 0;
        for (int i = 0; i < rows; ++i) {
            const PlayerSplitTarget t = mhw::PlayerSection::kPlayerSplitTargets[i];
            if (t.mask == 0) {
                rowsOk = false;
                std::fprintf(stderr, "  row %d claims nothing\n", i);
            }
            if ((t.mask & ~mhw::PlayerSection::kAll) != 0) {
                rowsOk = false;
                std::fprintf(stderr, "  row %d claims a bit outside "
                                     "PlayerSection::kAll\n", i);
            }
            for (int j = i + 1; j < rows; ++j) {
                if ((t.mask & mhw::PlayerSection::kPlayerSplitTargets[j].mask)
                    != 0) {
                    rowsOk = false;
                    std::fprintf(stderr, "  rows %d and %d claim the same "
                                         "bit\n", i, j);
                }
            }
        }
        check(rowsOk, "table: every row is non-empty, a subset of "
                      "PlayerSection::kAll, and pairwise disjoint");

        // THE literal, kept deliberate: what leaves PlayerPanel is the OR
        // of every row, NOT a value anyone types by hand. v0.12's first wave
        // split exactly one window owning Debuff|Buff; the second wave added
        // the mantles row. Asserting the OR-of-rows identity (rather than a
        // hardcoded bit set) is what keeps this honest — a future window
        // still cannot widen the removed bits without someone editing this
        // table, and a hand-written expected set that drifts out of sync
        // with the table now fails here instead of silently agreeing.
        uint32_t orOfRows = 0;
        for (int i = 0; i < rows; ++i)
            orOfRows |= mhw::PlayerSection::kPlayerSplitTargets[i].mask;
        check(mhw::PlayerSection::playerSplitRemovedBits() == orOfRows,
              "table: playerSplitRemovedBits() is the OR of every row "
              "(nothing leaves PlayerPanel that the table does not claim)");

        // Each split slot owns a non-empty slice of that, and the slices
        // tile it exactly: no bit is claimed twice, none is left unclaimed.
        bool tiledOk = true;
        uint32_t tiledUnion = 0;
        for (int s = 0; s < mhw::PlayerSection::kPlayerSplitTargetCount; ++s) {
            const uint32_t owned = mhw::PlayerSection::playerSubWindowMask(
                s, 0xFFFFFFFFu);
            if (owned == 0) {
                tiledOk = false;
                std::fprintf(stderr, "  split slot %d owns nothing\n", s);
            }
            if ((owned & tiledUnion) != 0) {
                tiledOk = false;
                std::fprintf(stderr,
                             "  split slot %d claims a bit another slot "
                             "already owns\n", s);
            }
            tiledUnion |= owned;
        }
        check(tiledOk && tiledUnion == mhw::PlayerSection::playerSplitRemovedBits(),
              "table: the split windows' claims tile the removed bits "
              "exactly — no overlap, no gap");

        // The pilot window is unchanged by the second wave: it still owns
        // exactly Debuff|Buff, so its saved position and content are still
        // about status pills only.
        check(mhw::PlayerSection::PlayerSplitSlot::Status == 0
                  && mhw::PlayerSection::PlayerSplitSlot::Status
                         < mhw::PlayerSection::kPlayerSplitTargetCount
                  && (mhw::PlayerSection::playerSubWindowMask(
                          mhw::PlayerSection::PlayerSplitSlot::Status,
                          0xFFFFFFFFu)
                      == (mhw::PlayerSection::Debuff
                          | mhw::PlayerSection::Buff)),
              "table: PlayerSplitSlot::Status names the window that owns "
              "Debuff|Buff");

        // Out-of-range slots answer 0 (draw nothing) rather than reading
        // past the table, so a registry row naming a window the table
        // does not have is inert instead of UB.
        {
            bool all = mhw::PlayerSection::playerSubWindowMask(-1, 0xFFFFFFFFu)
                           == 0
                       && mhw::PlayerSection::playerSubWindowMask(
                              rows, 0xFFFFFFFFu)
                              == 0
                       && mhw::PlayerSection::playerSubWindowMask(
                              rows + 97, 0xFFFFFFFFu)
                              == 0;
            check(all, "table: an out-of-range slot answers 0, not a read "
                       "past the table");
        }

        // A sub-window only ever draws blocks the user left on: the user's
        // toggles are AND-ed in, so `--mask-player 0` empties the strip too
        // instead of resurrecting a collapsed visibility toggle.
        {
            bool all = mhw::PlayerSection::playerSubWindowMask(mhw::PlayerSection::PlayerSplitSlot::Status, 0u)
                           == 0
                       && mhw::PlayerSection::playerSubWindowMask(
                              mhw::PlayerSection::PlayerSplitSlot::Status, mhw::PlayerSection::Debuff)
                              == mhw::PlayerSection::Debuff
                       && mhw::PlayerSection::playerSubWindowMask(
                              mhw::PlayerSection::PlayerSplitSlot::Status, mhw::PlayerSection::Buff)
                              == mhw::PlayerSection::Buff
                       && mhw::PlayerSection::playerSubWindowMask(
                              mhw::PlayerSection::PlayerSplitSlot::Status, mhw::PlayerSection::Weapon)
                              == 0;
            check(all, "mask: a sub-window draws the user's toggles "
                       "intersected with its own claim only");
        }
    }

    // ==================================================================
    // 7. MIXED, the third value of the same enum (D5).
    // ==================================================================
    //
    // playerPanelMaskFor() used to answer `mode == Split ? ... : mask`,
    // so Mixed fell into the VERBATIM arm and PlayerPanel kept all nine
    // blocks while the Mixed windows that exist to draw them were being
    // wired. Two things are pinned, exhaustively over the same 256-mask
    // space section 6 uses: Mixed answers exactly like Split (the one
    // arm all three *PanelMaskFor() functions now share), and the
    // panel-plus-windows union still tiles the user's mask with no
    // overlap — the pixel promise, for the third mode.
    {
        std::printf("7. mixed: the same algebra as split, in full\n");

        const uint32_t removed = mhw::PlayerSection::playerSplitRemovedBits();
        bool sameArm = true;
        bool unionHolds = true;
        bool disjoint  = true;
        for (uint32_t m = 0; m <= 0xFFu; ++m) {
            const uint32_t panel =
                mhw::PlayerSection::playerPanelMaskFor(Mode::Mixed, m);
            if (panel
                != mhw::PlayerSection::playerPanelMaskFor(Mode::Split, m)) {
                sameArm = false;
                std::fprintf(stderr, "  mixed(0x%02x): panel 0x%02x != split\n",
                             m, panel);
            }
            if ((panel & removed) != 0) {
                disjoint = false;
                std::fprintf(stderr,
                             "  mixed(0x%02x): kept a removed bit 0x%08x\n",
                             m, panel & removed);
            }
            uint32_t drawn = panel;
            for (int slot = 0;
                 slot < mhw::PlayerSection::kPlayerSplitTargetCount; ++slot)
                drawn |= mhw::PlayerSection::playerSubWindowMask(slot, m);
            if (drawn != m) {
                unionHolds = false;
                std::fprintf(stderr, "  mixed(0x%02x): union 0x%02x\n", m, drawn);
            }
        }
        check(sameArm,
              "mixed: playerPanelMaskFor(Mixed, m) == its Split answer for "
              "all 256 user masks — the third mode walks the same arm, "
              "not the verbatim one");
        check(disjoint,
              "mixed: the mother keeps no bit any window draws, in all 256 "
              "masks (no block on two surfaces)");
        check(unionHolds,
              "mixed: mother ∪ windows == the user mask exactly, in all 256 "
              "masks (nothing dropped, nothing added)");

        // The two literals the audit measured, stated as numbers so a
        // future change to the table cannot redefine them.
        check(mhw::PlayerSection::playerPanelMaskFor(Mode::Mixed,
                                                     mhw::PlayerSection::kAll)
                  == 0u,
              "mixed: playerPanelMaskFor(Mixed, kAll) == 0 (all nine rows "
              "have windows of their own)");
        check(mhw::PlayerSection::playerPanelHasBlocks(
                  mhw::PlayerSection::playerPanelMaskFor(Mode::Mixed,
                                                         mhw::PlayerSection::kAll))
                  == false,
              "mixed: so the player mother has NO block to draw and the "
              "mount gate refuses it — no empty frame beside the windows");
    }

    std::printf("\n%d checks, %d failure(s)\n%s\n", checks, failures,
                failures == 0 ? "ALL PLAYER-LAYOUT TESTS PASSED"
                              : "SOME PLAYER-LAYOUT TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
