// SPDX-License-Identifier: Apache-2.0
// Regression tests for the live overlay's startup mask priority chain.
//
// WHAT WAS BROKEN (v0.11.4, fixed in v0.12)
// ------------------------------------------
// `parseMask` in src/main.cpp answered "no --mask-* flag -> 0xFFFFFFFF".
// That single `if` made every mask the overlay ever used come from the
// command line, so the four rows the control console writes into
// ~/.config/monster-overlay/monster-overlay.conf
//
//     player=<hex32>
//     monster=<hex32>
//     damage=<hex32>
//     pets=<hex32>
//
// reached a live overlay ONLY through the flags the console appends when
// it spawns the child process. Launching ./monster-overlay by hand — which
// is what README.md and docs/USAGE.md tell the user to do — silently
// discarded every saved toggle. Users saw a full-hide/partial-show panel
// with no way to explain it, because the console UI showed the opposite
// state from what the overlay drew.
//
// WHY THIS TEST FILE EXISTS
// --------------------------
// The decision itself was the bug, and it lived in a lambda inside main()
// so no test could reach it. The fixed chain is now a pure function,
// mhw::resolveStartupPanelMask (ui/viewmodel/panel_mask_fallback.h), and this file
// pins its three rules:
//
//   1. an explicit, parseable CLI value wins (this is the pre-existing
//      behaviour — the console-spawned path — and it must NOT be broken
//      by the new fallback; this is the rule most at risk of regression,
//      because the tempting implementation of "read the conf" is one
//      that also overrides an explicit flag)
//   2. no CLI flag, conf row present and well-formed -> the conf value
//   3. no CLI flag, conf row absent or malformed -> 0xFFFFFFFF (everything
//      visible), which is what the old lambda returned
//
// Rule 3 has a second half that is easy to get wrong: a MALFORMED conf
// row must degrade to the default, never to 0 and never to a partially
// parsed value, or a hand-edited conf typo would blank a panel.
//
// The parse half (mhw::parsePanelMasks) is covered exhaustively by
// panel_mask_codec_tests.cpp; this file covers the PRIORITY decision and
// exercises the parse only through the real codec, so the two cannot
// disagree about what "conf row present" means.

#include "ui/viewmodel/panel_mask_fallback.h"

#include "ui/viewmodel/panel_mask_codec.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QStringConverter>

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;
void check(bool cond, const char *what)
{
    if (cond) std::printf("PASS: %s\n", what);
    else { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

// The literal defaults the old lambda used, spelled out so a change to
// them is a visible diff in an expected value rather than a silent
// behaviour shift.
constexpr uint32_t kEverythingVisible = 0xFFFFFFFFu;

using mhw::PanelMaskCliRequest;
using mhw::PanelMaskConfRow;
using mhw::PanelMaskSource;
using mhw::PanelMaskSlot;

// Absent CLI flag: nothing was asked for on the command line.
const PanelMaskCliRequest kNoCli;

// A CLI flag with a value that parsed.
PanelMaskCliRequest cliGiven(uint32_t value)
{
    return PanelMaskCliRequest{true, true, value};
}

// A CLI flag that was given but failed to parse — what main.cpp builds
// after the qWarning() in its per-slot parse lambda.
const PanelMaskCliRequest kCliMalformed{true, false, kEverythingVisible};

// Conf row that was present and well-formed.
PanelMaskConfRow confRow(uint32_t value)
{
    return PanelMaskConfRow{true, value};
}

// Conf row that was absent or malformed.
const PanelMaskConfRow kNoConfRow;

const char *sourceName(PanelMaskSource s)
{
    switch (s) {
    case PanelMaskSource::CliExplicit: return "cli";
    case PanelMaskSource::Conf:        return "conf";
    case PanelMaskSource::DefaultAll:  return "default";
    }
    return "?";
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const PanelMaskSlot kAllSlots[4] = {
        PanelMaskSlot::Player,
        PanelMaskSlot::Monster,
        PanelMaskSlot::Damage,
        PanelMaskSlot::Pets,
    };
    const char *const kSlotNames[4] = {"player", "monster", "damage", "pets"};

    // ================================================================
    // 1. RULE 1 — an explicit CLI value wins over everything.
    //    This is the regression guard for the console-spawned path: the
    //    console appends --mask-* to the child argv, and that behaviour
    //    must survive the new fallback. The dangerous implementations are
    //    "conf also applies" (a blended or OR'd mask) and "conf wins",
    //    which these cases catch because every expected value is the CLI
    //    value exactly.
    // ================================================================
    {
        // CLI 0xfb over conf 0x2f: the conf row must be ignored, and the
        // source must read as CLI so a log line cannot claim "conf".
        const mhw::PanelMaskResolution r = mhw::resolveStartupPanelMask(
            cliGiven(0xfbu), confRow(0x2fu), PanelMaskSlot::Player);
        check(r.value == 0xfbu,
              "rule1: explicit CLI value is used verbatim (not OR'd with conf)");
        check(r.source == PanelMaskSource::CliExplicit,
              "rule1: source reports CLI-explicit, not conf");

        // CLI 0 beats a conf row that says "everything visible". Zero is
        // a legitimate mask (the user hid the panel) and must not be
        // mistaken for "unset".
        const mhw::PanelMaskResolution zero = mhw::resolveStartupPanelMask(
            cliGiven(0u), confRow(kEverythingVisible),
            PanelMaskSlot::Monster);
        check(zero.value == 0u,
              "rule1: CLI 0 (panel hidden) is honoured, not skipped as unset");
        check(zero.source == PanelMaskSource::CliExplicit,
              "rule1: CLI 0 still reports CLI-explicit");

        // Every slot honours the rule independently — the fallback is
        // per-mask, so a fix that only wired player must fail here.
        bool allSlots = true;
        for (int i = 0; i < 4; ++i) {
            const mhw::PanelMaskResolution r = mhw::resolveStartupPanelMask(
                cliGiven(0x11u * static_cast<uint32_t>(i + 1)),
                confRow(kEverythingVisible), kAllSlots[i]);
            if (r.value != 0x11u * static_cast<uint32_t>(i + 1)
                || r.source != PanelMaskSource::CliExplicit) {
                allSlots = false;
                std::fprintf(stderr, "  slot %s got %s/%u\n", kSlotNames[i],
                             sourceName(r.source), r.value);
            }
        }
        check(allSlots, "rule1: CLI wins on all four slots independently");

        // A CLI value that was given but MALFORMED must NOT fall through
        // to the conf row. That is the subtle case: "conf has a usable
        // value, CLI is garbage" is a tempting place to rescue with the
        // conf value, but the pre-v0.12 behaviour was to warn and use the
        // default, and a console that passed a corrupt flag would
        // otherwise be silently overridden by a stale conf row.
        const mhw::PanelMaskResolution bad = mhw::resolveStartupPanelMask(
            kCliMalformed, confRow(0x2fu), PanelMaskSlot::Damage);
        check(bad.value == kEverythingVisible,
              "rule1: malformed CLI value falls back to the default, "
              "NOT to the conf row");
        check(bad.source == PanelMaskSource::DefaultAll,
              "rule1: malformed CLI value reports the default source");

        // ... and with no conf at all, the same malformed CLI gives the
        // same default, which is exactly the old lambda's answer.
        const mhw::PanelMaskResolution badNoConf =
            mhw::resolveStartupPanelMask(kCliMalformed, kNoConfRow,
                                         PanelMaskSlot::Player);
        check(badNoConf.value == kEverythingVisible,
              "rule1: malformed CLI with no conf still gives the default");
    }

    // ================================================================
    // 2. RULE 2 — no CLI flag, conf row present: the conf value is used.
    //    This IS the v0.12 fix. Each case drives the REAL codec from a
    //    literal conf text so "what the conf says" and "what the overlay
    //    applies" are checked end to end rather than assumed.
    // ================================================================
    {
        // Expected value per slot, plus the source that must be reported.
        // The source is part of the contract: it is what a startup log
        // line says, and it is how a wrong-but-right-valued fallback (the
        // conf silently overriding an explicit CLI flag) becomes visible.
        struct {
            const char *name;
            const char *confText;
            uint32_t expected[4];
            PanelMaskSource expectedSource[4];
        } cases[] = {
            // The console's canonical four-row save.
            {"four rows",
             "player=fb\nmonster=2f\ndamage=7\npets=1\n",
             {0xfbu, 0x2fu, 0x7u, 0x1u},
             {PanelMaskSource::Conf, PanelMaskSource::Conf,
              PanelMaskSource::Conf, PanelMaskSource::Conf}},
            // Users hid entire panels by clearing rows. Zero is a value,
            // not an absence.
            {"rows cleared to zero",
             "player=0\nmonster=0\ndamage=0\npets=0\n",
             {0u, 0u, 0u, 0u},
             {PanelMaskSource::Conf, PanelMaskSource::Conf,
              PanelMaskSource::Conf, PanelMaskSource::Conf}},
            // An older three-row config (pre-pets). The pets row is
            // ABSENT, so pets resolves through the default — there is no
            // conf row to read and it must not be read as 0. Damage keeps
            // the codec's legacy migration, which ORs OtherMembers (0x8)
            // into a pre-pets damage row so old configs show teammates.
            {"pre-pets three rows",
             "player=1\nmonster=2\ndamage=3\n",
             {0x1u, 0x2u, 0x3u | 0x8u, kEverythingVisible},
             {PanelMaskSource::Conf, PanelMaskSource::Conf,
              PanelMaskSource::Conf, PanelMaskSource::DefaultAll}},
            // The conf also carries locale= and comments; those rows must
            // not disturb any mask.
            {"conf with locale and comments",
             "# hand-edited\nlocale=zh-CN\nplayer=e0\nmonster=3f\n"
             "damage=7\npets=2\n",
             {0xe0u, 0x3fu, 0x7u, 0x2u},
             {PanelMaskSource::Conf, PanelMaskSource::Conf,
              PanelMaskSource::Conf, PanelMaskSource::Conf}},
            // Duplicate rows converge to the last valid one, exactly as the
            // codec's own contract says; the fallback must inherit that
            // behaviour rather than re-deciding it.
            {"duplicate rows, last wins",
             "player=1\nplayer=zz\nplayer=7e\n",
             {0x7eu, kEverythingVisible, kEverythingVisible,
              kEverythingVisible},
             {PanelMaskSource::Conf, PanelMaskSource::DefaultAll,
              PanelMaskSource::DefaultAll, PanelMaskSource::DefaultAll}},
        };

        bool all = true;
        for (const auto &c : cases) {
            // Drive the real codec, the way main.cpp does.
            const mhw::PanelMaskSet parsed =
                mhw::parsePanelMasks(QString::fromUtf8(c.confText));
            const mhw::PanelMaskConfRow conf[4] = {
                {parsed.playerValid, parsed.player},
                {parsed.monsterValid, parsed.monster},
                {parsed.damageValid, parsed.damage},
                {parsed.petsValid, parsed.pets},
            };
            for (int i = 0; i < 4; ++i) {
                const mhw::PanelMaskResolution r =
                    mhw::resolveStartupPanelMask(kNoCli, conf[i], kAllSlots[i]);
                if (r.value != c.expected[i]
                    || r.source != c.expectedSource[i]) {
                    all = false;
                    std::fprintf(stderr,
                                 "  [%s] %s: expected 0x%x/%s, got 0x%x/%s\n",
                                 c.name, kSlotNames[i], c.expected[i],
                                 sourceName(c.expectedSource[i]), r.value,
                                 sourceName(r.source));
                }
            }
        }
        check(all, "rule2: no CLI + conf rows present -> conf values (and "
                   "the conf source) for all four slots");
    }

    // The end-to-end case that reproduces the ACTUAL user-visible bug,
    // through a real file on disk so nothing about the read path is
    // assumed either.
    {
        QTemporaryDir tmp;
        if (!tmp.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        const QString path =
            tmp.filePath(QStringLiteral("monster-overlay.conf"));

        // Exactly what the control console writes: four masks the user
        // switched off, plus the locale row the console owns.
        {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                check(false, "bug repro: temp conf not writable");
                return 1;
            }
            f.write(QByteArrayLiteral(
                "player=7e\nmonster=1a\ndamage=5\npets=1\nlocale=en-US\n"));
            f.close();
        }

        // main.cpp's read: the same conf-read helper main.cpp calls, on the
        // same real path. (A double read is deliberately avoided — the
        // helper IS the read path.)
        const QString text = mhw::readPanelMaskConfText(path);
        if (text.isEmpty()) {
            check(false, "bug repro: temp conf read back empty");
            return 1;
        }

        const mhw::PanelMaskSet parsed = mhw::parsePanelMasks(text);

        // The pre-fix overlay produced 0xFFFFFFFF for all four here.
        const mhw::PanelMaskConfRow conf[4] = {
            {parsed.playerValid, parsed.player},
            {parsed.monsterValid, parsed.monster},
            {parsed.damageValid, parsed.damage},
            {parsed.petsValid, parsed.pets},
        };
        const uint32_t expected[4] = {0x7eu, 0x1au, 0x5u, 0x1u};

        bool fixed = true;
        for (int i = 0; i < 4; ++i) {
            const mhw::PanelMaskResolution r = mhw::resolveStartupPanelMask(
                kNoCli, conf[i], kAllSlots[i]);
            if (r.value != expected[i] || r.source != PanelMaskSource::Conf) {
                fixed = false;
                std::fprintf(stderr, "  %s: expected conf/0x%x, got %s/0x%x\n",
                             kSlotNames[i], expected[i], sourceName(r.source),
                             r.value);
            }
        }
        check(fixed, "bug repro (through a real conf file): a hand-launched "
                     "overlay now applies the saved masks instead of four "
                     "0xFFFFFFFF defaults");
    }

    // ================================================================
    // 3. RULE 3 — no CLI flag, no usable conf row: 0xFFFFFFFF.
    //    A missing file, an empty file, a conf with other keys and a
    //    malformed mask row all land here. None of them may yield 0
    //    (which would blank a panel) and none may yield a partially
    //    parsed value.
    // ================================================================
    {
        struct {
            const char *name;
            const char *confText;
            uint32_t expectedPlayer;
        } cases[] = {
            {"empty conf", "", kEverythingVisible},
            {"conf with only unrelated rows", "locale=zh-CN\nzoom=2.0\n",
             kEverythingVisible},
            {"malformed mask value (non-hex)", "player=zz\n",
             kEverythingVisible},
            {"undeclared and malformed mask (nothing else)",
             "player=not-a-number\nmonster=7\n", kEverythingVisible},
            {"mask row present but empty value", "player=\n",
             kEverythingVisible},
            // A localized-only conf is the first-run case: no mask has
            // ever been saved, and the overlay must open fully visible.
            {"first-run conf", "locale=zh-CN\n", kEverythingVisible},
        };

        bool all = true;
        for (const auto &c : cases) {
            const mhw::PanelMaskSet parsed =
                mhw::parsePanelMasks(QString::fromUtf8(c.confText));
            const mhw::PanelMaskResolution r = mhw::resolveStartupPanelMask(
                kNoCli,
                PanelMaskConfRow{parsed.playerValid, parsed.player},
                PanelMaskSlot::Player);
            if (r.value != c.expectedPlayer
                || r.source != PanelMaskSource::DefaultAll) {
                all = false;
                std::fprintf(stderr, "  [%s] expected default/0x%x, got %s/0x%x\n",
                             c.name, c.expectedPlayer, sourceName(r.source),
                             r.value);
            }
        }
        check(all, "rule3: no CLI + missing/malformed conf row -> "
                   "everything-visible default");

        // The same rule must hold for the other three slots, driven
        // through a conf that simply lacks their rows.
        const mhw::PanelMaskSet parsed =
            mhw::parsePanelMasks(QStringLiteral("locale=zh-CN\n"));
        bool everySlotDefaults = true;
        const mhw::PanelMaskConfRow rows[4] = {
            {parsed.playerValid, parsed.player},
            {parsed.monsterValid, parsed.monster},
            {parsed.damageValid, parsed.damage},
            {parsed.petsValid, parsed.pets},
        };
        for (int i = 0; i < 4; ++i) {
            const mhw::PanelMaskResolution r =
                mhw::resolveStartupPanelMask(kNoCli, rows[i], kAllSlots[i]);
            if (r.value != kEverythingVisible
                || r.source != PanelMaskSource::DefaultAll) {
                everySlotDefaults = false;
                std::fprintf(stderr, "  slot %s got %s/0x%x\n", kSlotNames[i],
                             sourceName(r.source), r.value);
            }
        }
        check(everySlotDefaults,
              "rule3: every slot defaults to everything-visible");

        // A conf file that does not exist at all. This is the cold-start
        // path (no console ever ran) and it must be indistinguishable
        // from an empty one.
        {
            QTemporaryDir tmp;
            if (!tmp.isValid()) {
                std::fprintf(stderr, "FATAL: no temp dir\n");
                return 2;
            }
            const QString missing =
                tmp.filePath(QStringLiteral("does-not-exist.conf"));
            const QString text = mhw::readPanelMaskConfText(missing);
            const mhw::PanelMaskSet p = mhw::parsePanelMasks(text);
            const mhw::PanelMaskResolution r = mhw::resolveStartupPanelMask(
                kNoCli, PanelMaskConfRow{p.playerValid, p.player},
                PanelMaskSlot::Player);
            check(text.isEmpty() && r.value == kEverythingVisible
                      && r.source == PanelMaskSource::DefaultAll,
                  "rule3: a nonexistent conf reads empty and resolves to the "
                  "default");
        }
    }

    // ================================================================
    // 4. THE CHAIN, NOT JUST ITS EDGES — a 2x3 matrix so an
    //    implementation that handles only the four corners above still
    //    has to agree on the whole decision table.
    // ================================================================
    {
        struct {
            const char *name;
            PanelMaskCliRequest cli;
            PanelMaskConfRow conf;
            uint32_t expected;
            PanelMaskSource expectedSource;
        } cases[] = {
            {"cli ok + conf ok", cliGiven(0x11u), confRow(0x22u), 0x11u,
             PanelMaskSource::CliExplicit},
            {"cli ok + conf absent", cliGiven(0x11u), kNoConfRow, 0x11u,
             PanelMaskSource::CliExplicit},
            {"cli ok + conf zero", cliGiven(0x11u), confRow(0u), 0x11u,
             PanelMaskSource::CliExplicit},
            {"cli absent + conf ok", kNoCli, confRow(0x22u), 0x22u,
             PanelMaskSource::Conf},
            {"cli absent + conf absent", kNoCli, kNoConfRow,
             kEverythingVisible, PanelMaskSource::DefaultAll},
            {"cli absent + conf zero", kNoCli, confRow(0u), 0u,
             PanelMaskSource::Conf},
            {"cli malformed + conf ok", kCliMalformed, confRow(0x22u),
             kEverythingVisible, PanelMaskSource::DefaultAll},
            {"cli malformed + conf absent", kCliMalformed, kNoConfRow,
             kEverythingVisible, PanelMaskSource::DefaultAll},
        };

        bool all = true;
        for (const auto &c : cases) {
            const mhw::PanelMaskResolution r = mhw::resolveStartupPanelMask(
                c.cli, c.conf, PanelMaskSlot::Player);
            if (r.value != c.expected || r.source != c.expectedSource) {
                all = false;
                std::fprintf(stderr,
                             "  [%s] expected %s/0x%x, got %s/0x%x\n",
                             c.name, sourceName(c.expectedSource), c.expected,
                             sourceName(r.source), r.value);
            }
        }
        check(all, "chain: all 8 (cli state x conf state) combinations agree");

        // The matrix above must hold identically for every slot, proving
        // the decision is genuinely per-mask and not a single shared
        // value that happens to be right for player.
        bool slotsAgree = true;
        for (const auto &c : cases) {
            for (int i = 0; i < 4; ++i) {
                const mhw::PanelMaskResolution r =
                    mhw::resolveStartupPanelMask(c.cli, c.conf, kAllSlots[i]);
                if (r.value != c.expected || r.source != c.expectedSource) {
                    slotsAgree = false;
                    std::fprintf(stderr, "  [%s] slot %s diverged\n", c.name,
                                 kSlotNames[i]);
                }
            }
        }
        check(slotsAgree, "chain: the decision table is identical on all four "
                          "slots");
    }

    // ================================================================
    // 5. WHAT THE DEFAULT IS — pin the literal, so widening or
    //    narrowing the "everything visible" default is a deliberate
    //    change to this file rather than a silent one.
    // ================================================================
    {
        const mhw::PanelMaskResolution r = mhw::resolveStartupPanelMask(
            kNoCli, kNoConfRow, PanelMaskSlot::Player);
        check(r.value == 0xFFFFFFFFu && r.source == PanelMaskSource::DefaultAll,
              "default: the no-information default is exactly 0xFFFFFFFF "
              "(pre-v0.12 behaviour preserved)");
    }

    std::printf("\n%s\n", failures == 0 ? "ALL MASK-FALLBACK TESTS PASSED"
                                        : "SOME MASK-FALLBACK TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
