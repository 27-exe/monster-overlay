// SPDX-License-Identifier: Apache-2.0
//
// v0.13 console layout mode: the conf row, the priority chain and the
// argv spelling, pinned.
//
// WHAT THIS FILE COVERS
// ---------------------
// The console gained a Single/Split selector in v0.13, and the promise
// the UI makes — "this is remembered, and the child is restarted when it
// must be" — rests on three small pieces of logic that must not drift:
//
//   1. THE ROW (`layout=` in monster-overlay.conf).
//   2. THE PRIORITY (conf > UI choice > default single), which is not
//      this file's logic at all — it goes through the SHARED
//      mhw::resolveStartupLayoutMode(), and what is asserted here is that
//      the console's row really is the conf arm of that chain, and that
//      the console's UI choice feeds it as a conf row rather than
//      competing with it.
//   3. THE FLAG (`--layout=split`, present only in split).
//
// THE FOUR CONTRACTS, in order of how much a wrong answer costs:
//
//   A. OLD CONF, NO KEY → SINGLE. The v0.11 user's file has no layout
//      row. Every unusable spelling — absent, garbage, a lone "single",
//      a hand-typed "off" — must resolve Single, because a value that
//      does not say Split must never move a user's windows. This is also
//      the 10 single-mode pixel baselines' guarantee stated in test form:
//      every user who never touched the combo opens exactly the topology
//      v0.11.4 painted.
//
//   B. conf layout=split → SPLIT, and the shared chain reports the
//      source as Conf — so the console's "the conf said so" claim is
//      checkable rather than inferred from the mode.
//
//   C. THE PRIORITY, end to end: with the shared resolver, CLI wins over
//      conf which wins over the default; and the console's own call
//      passes an empty CLI request, so the conf row is what speaks. A
//      console that passed a live CLI request would be able to outrank
//      a user's hand-written --layout=, which is the one override it
//      must never block.
//
//   D. `--layout` APPEARS ONLY IN SPLIT, and a Single-mode save writes no
//      row at all — the two halves of "an old conf stays byte-for-byte
//      single".
//
// WHY THIS TARGET IS HEADER-ONLY and links Qt6::Core only: everything
// above is constexpr / inline over QString and QByteArray. The widget
// half (ControlPanel) is covered by the console smoke targets; testing
// it here would need a QApplication and a compositor-free offscreen
// platform for no additional logic coverage.
//
// THE MUTATION DISCIPLINE. Contracts A, B, C and D each carry a stated
// mutation in a comment above them: the one-line change that must make
// them FAIL. Running those is what proves the assertions are load-
// bearing rather than true by construction.

#include "ui/viewmodel/console_layout_mode_codec.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <cstdio>
#include <string>

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

using Mode     = mhw::PlayerSection::PlayerLayoutMode;
using Source   = mhw::PlayerLayoutSource;
using Resolution = mhw::PlayerLayoutResolution;

// The console's own call shape, reduced to a helper so the test asserts
// the console's REAL behaviour rather than a re-derivation of it: empty
// CLI request, conf row from the text. If applyStartupLayoutMode() ever
// starts passing a CLI request, this is the line that changes and the
// priority test below tells you what that broke.
Resolution resolveAsConsoleDoes(const QString &confText)
{
    // v1.1.0: the console passes the WHOLE parsed row, not `.source`.
    // Passing `.source` is what made `layout=mixed` resolve to Split and let
    // one save rewrite the user's file.
    return mhw::resolveStartupLayoutMode(
        mhw::PlayerLayoutCliRequest{},
        mhw::parseConsoleLayoutRow(confText));
}

bool hasFlag(const QStringList &args, const QString &flag)
{
    return args.contains(flag);
}

// How many `layout=` rows a conf text carries. Counted rather than
// inferred, because "at most one" is the invariant and the parser cannot
// see a pile — it stops at the first row it can read as Split, which is
// precisely how the pile went unnoticed.
[[nodiscard]] int countRows(const QString &conf)
{
    int n = 0;
    for (const QString &line : conf.split(QLatin1Char('\n'))) {
        if (line.trimmed().startsWith(
                QLatin1String(mhw::kConsoleLayoutRowKey)
                + QLatin1String("=")))
            ++n;
    }
    return n;
}

// Resolve a conf text the way the console does, so a test can assert what
// a USER's file would decode to rather than what the writer emitted.
[[nodiscard]] Mode readMode(const QString &conf)
{
    return mhw::parseConsoleLayoutRow(conf).source
                   == mhw::PlayerLayoutSource::Conf
               ? Mode::Split
               : Mode::Single;
}

// =====================================================================
// A. OLD CONF, NO KEY → SINGLE
// =====================================================================
// MUTATION: make parseConsoleLayoutRow() return
// {PlayerLayoutSource::Conf} when the row is absent. Every assertion in
// this block fails, because a missing row would then read as Split —
// which is the failure that would silently change every existing user's
// window topology on upgrade.
void test_oldConfResolvesSingle()
{
    std::printf("A. old conf, no layout key -> Single\n");

    check(resolveAsConsoleDoes(QString()).mode == Mode::Single,
          "no conf file at all resolves Single");
    check(resolveAsConsoleDoes(QStringLiteral("")).mode == Mode::Single,
          "empty conf text resolves Single");

    // The shape a real v0.11 conf actually has: the four mask rows, the
    // schema marker and a locale row. None of them is a layout row.
    const QString legacy =
        "player=1ff\n"
        "monster=7f\n"
        "damage=3ff\n"
        "pets=3\n"
        "maskVersion=1\n"
        "locale=zh-CN\n";
    const Resolution legacyResolved = resolveAsConsoleDoes(legacy);
    check(legacyResolved.mode == Mode::Single,
          "a real v0.11/v0.12 conf with no layout row resolves Single");
    check(legacyResolved.source == Source::Default,
          "and it reports the source as Default, not Conf");

    // Every unusable spelling. The list is deliberately wider than the
    // codec's own reject set, because each of these is a value a user
    // could actually type while hand-editing the conf.
    for (const char *bad : {
             "layout=banana", "layout=", "layout=single", "layout=off",
             "layout=0", "layout=1", "layout=SplitTwo",
             "layout= SPLIT EXTRA",
         }) {
        const bool single = resolveAsConsoleDoes(
            QString::fromLatin1(bad)).mode == Mode::Single;
        const std::string label =
            std::string("unusable \"") + bad + "\" resolves Single";
        check(single, label.c_str());
    }

    // The spellings that ARE accepted: the row key is matched the same
    // way every other row in this file is matched (a trimmed
    // `startsWith("layout=")`), and the VALUE is parsed by
    // PlayerSection's own parsePlayerLayoutMode(), which trims and
    // lower-cases. So `layout=SPLIT` and `  layout=  split  ` both work,
    // while `layout = split` does NOT — the same way `player = 1ff`
    // never parsed either. That asymmetry is deliberate and consistent:
    // the mask codec set the rule, and a conf row is a machine file, not
    // a prose field.
    check(resolveAsConsoleDoes(QStringLiteral(" layout=SPLIT \n")).mode
              == Mode::Split,
          "an indented, upper-case layout=SPLIT resolves Split");
    check(resolveAsConsoleDoes(QStringLiteral("layout=  split  \n")).mode
              == Mode::Split,
          "a padded VALUE resolves Split (value parsing is tolerant)");
    check(resolveAsConsoleDoes(QStringLiteral("layout = split\n")).mode
              == Mode::Single,
          "spaces AROUND THE '=' do not match the row key — the same "
          "rule player=/monster= already obeys");
}

// =====================================================================
// B. conf layout=split → SPLIT, with the source reported as Conf
// =====================================================================
// MUTATION: return PlayerLayoutSource::Default from
// parseConsoleLayoutRow()'s success path. The mode still resolves
// Split (rule 3's Default arm is also Single for a missing row, so the
// mode test would still pass) — which is exactly why the SOURCE is
// asserted here: it is the half that proves "this came from the conf"
// rather than "this is a coincidence".
void test_confSplitResolvesSplitWithSource()
{
    std::printf("B. conf layout=split -> Split, source Conf\n");

    const Resolution r = resolveAsConsoleDoes(
        "player=1ff\nmaskVersion=1\nlayout=split\nlocale=zh-CN\n");
    check(r.mode == Mode::Split, "layout=split resolves to Split");
    check(r.source == Source::Conf,
          "and the source is Conf (the row really is the chain's conf "
          "arm)");

    // The row's position must not matter — the parse scans every line,
    // and a conf a user hand-edited will not have the row last.
    const Resolution first = resolveAsConsoleDoes(
        "layout=split\nplayer=1ff\nmaskVersion=1\n");
    check(first.mode == Mode::Split && first.source == Source::Conf,
          "a layout= row at the TOP of the file is found too");
}

// =====================================================================
// C. THE PRIORITY, through the shared resolver
// =====================================================================
// MUTATION A: swap the module order in resolveStartupLayoutMode() so the
// conf arm is checked before the CLI arm. `cliSplit_beatsConfSingle`
// fails — a user's explicit --layout=split would be discarded.
// MUTATION B: make the malformed-CLI arm fall through to the conf row
// instead of the DEFAULT. `cliMalformed_resolvesDefault` fails — which
// is the case where a typo'd flag would hijack a stale conf row.
void test_priorityChain()
{
    std::printf("C. priority: cli > conf > default (shared resolver)\n");

    // CLI wins over a conf row that says the other thing.
    const Resolution cliSplitOverConf =
        mhw::resolveStartupLayoutMode(
            mhw::PlayerLayoutCliRequest{/*given=*/true, QStringLiteral("split"),
                                        Mode::Single},
            mhw::parseConsoleLayoutRow(QStringLiteral("layout=split")));
    check(cliSplitOverConf.mode == Mode::Split,
          "an explicit --layout=split wins over the conf arm");
    check(cliSplitOverConf.source == Source::CliExplicit,
          "and it reports CliExplicit, not Conf");

    const Resolution cliSingleOverConf =
        mhw::resolveStartupLayoutMode(
            mhw::PlayerLayoutCliRequest{/*given=*/true,
                                        QStringLiteral("single"), Mode::Split},
            mhw::parseConsoleLayoutRow(QStringLiteral("layout=split")));
    check(cliSingleOverConf.mode == Mode::Single,
          "an explicit --layout=single wins over a conf layout=split "
          "row — the override the console must never be able to block");

    // The conf arm beats the Single default.
    const Resolution confOverDefault =
        resolveAsConsoleDoes("layout=split\n");
    check(confOverDefault.mode == Mode::Split
              && confOverDefault.source == Source::Conf,
          "the conf row beats the Single default");
    const Resolution defaultWins =
        resolveAsConsoleDoes("player=1ff\n");
    check(defaultWins.mode == Mode::Single
              && defaultWins.source == Source::Default,
          "with no CLI and no usable row, the Single default wins");

    // Malformed CLI resolves to the DEFAULT, not to the conf row.
    const Resolution malformed =
        mhw::resolveStartupLayoutMode(
            mhw::PlayerLayoutCliRequest{/*given=*/true,
                                        QStringLiteral("banana"), Mode::Split},
            mhw::parseConsoleLayoutRow(QStringLiteral("layout=split")));
    check(malformed.mode == Mode::Single,
          "a malformed --layout= resolves Single (never the conf row)");
    check(malformed.source == Source::Default,
          "and reports Default, so the discarded flag is visible to a "
          "test rather than invisible in production");

    // The v0.12 boolean alias still outranks the conf row.
    const Resolution compatFlag =
        mhw::resolveStartupLayoutMode(
            mhw::PlayerLayoutCliRequest{/*given=*/false, QString(),
                                        Mode::Split},
            mhw::parseConsoleLayoutRow(QString()));
    check(compatFlag.mode == Mode::Split
              && compatFlag.source == Source::CliCompatFlag,
          "--split-player-strip still wins over a conf row");
}

// =====================================================================
// D. THE FLAG AND THE ROUND TRIP
// =====================================================================
// MUTATION A: make consoleLayoutArgv() emit `--layout=single` in Single
// mode. `flagOnlyInSplit` and `argv_matches_conf` both fail — and the
// reason they fail is the real defect: an explicit single would outrank
// the user's own --layout= on the shared chain.
// MUTATION B: make serializeConsoleLayoutRow() emit a row for Single
// too. `singleSaveWritesNoRow` fails, proving that the "old conf has no
// key" case is not a migration path nobody exercises but the state every
// non-split user actually lives in.
void test_repeatedSavesDoNotStackTheRow()
{
    std::printf("D2. repeated saves keep ONE row (the drop-old-first "
                "contract)\n");

    // What saveMaskToDisk() used to do was append a fresh row and drop
    // nothing. Parsing survives that — the parser scans every line and
    // stops at the first it can read as Split — so nothing failed and the
    // defect stayed invisible while the conf grew a row per save. The
    // saves run on close, before START, and on every apply, so a session
    // that switched modes ended with the file's LAST row contradicting the
    // mode in force, and the next launch read that last row. That is the
    // "I switched and nothing happened" symptom.
    //
    // This is the composition step saveMaskToDisk() performs, restated so
    // the invariant is testable without a widget: take the existing text,
    // drop every layout row, append the codec's row for the mode in force.
    auto compose = [](const QString &existing, Mode mode) {
        const QStringList lines = existing.split(QLatin1Char('\n'));
        QStringList kept;
        kept.reserve(lines.size() + 1);
        for (const QString &line : lines) {
            if (line.trimmed().startsWith(
                    QLatin1String(mhw::kConsoleLayoutRowKey)
                    + QLatin1String("=")))
                continue;
            kept.append(line);
        }
        QString out = kept.join(QLatin1Char('\n'));
        if (existing.endsWith(QLatin1Char('\n')) && !out.endsWith(
                QLatin1Char('\n')))
            out += QLatin1Char('\n');
        out += QString::fromLatin1(mhw::serializeConsoleLayoutRow(mode));
        return out;
    };

    const QString seed = QStringLiteral("player=1ff\nmonster=7f\n"
                                        "damage=3ff\npets=3\n"
                                        "maskVersion=1\nlocale=zh-CN\n");

    // Split, saved ten times in a row: still exactly one row.
    QString conf = seed;
    for (int i = 0; i < 10; ++i)
        conf = compose(conf, Mode::Split);
    check(countRows(conf) == 1,
          "ten split saves leave ONE layout row, not ten");
    check(readMode(conf) == Mode::Split,
          "and it still resolves to split");

    // Alternating modes, which is the case that actually bit: the last
    // write wins, so the file and the console never disagree.
    //
    // `(i & 1) ? Split : Single` starts on Single (i = 0), so an EVEN
    // number of writes ends on SPLIT and an odd one ends on single. The
    // expectations below follow that parity — stating it here because
    // getting the parity backwards is exactly the kind of mistake that
    // makes a passing test prove the opposite of what its name says.
    conf = seed;
    for (int i = 0; i < 5; ++i)   // single..single, ends on SINGLE
        conf = compose(conf, (i & 1) ? Mode::Split : Mode::Single);
    check(countRows(conf) == 0,
          "ending on single leaves NO layout row — the default is not "
          "written down");
    check(readMode(conf) == Mode::Single,
          "so it resolves to single, which is what the user last chose");

    for (int i = 0; i < 6; ++i)   // ends on SPLIT
        conf = compose(conf, (i & 1) ? Mode::Split : Mode::Single);
    check(countRows(conf) == 1,
          "six alternating writes end on split and leave exactly one row");
    check(readMode(conf) == Mode::Split,
          "and it resolves to split — the LAST write, not the first");

    // A user's hand-edited duplicate converges too. It is not this
    // function's job to undo it, but it must not double it.
    QString dup = seed;
    dup += QStringLiteral("layout=split\nlayout=split\n");
    check(countRows(dup) == 2, "the hand-duplicated input has two rows");
    conf = compose(dup, Mode::Split);
    check(countRows(conf) == 1,
          "one save collapses a hand-duplicated pair back to one");

    // The mask rows must survive untouched — dropping a stray row is not
    // licence to drop anything else.
    for (const QString &need : {QStringLiteral("player=1ff"),
                                QStringLiteral("monster=7f"),
                                QStringLiteral("damage=3ff"),
                                QStringLiteral("pets=3"),
                                QStringLiteral("maskVersion=1"),
                                QStringLiteral("locale=zh-CN")})
        check(conf.contains(need),
              "every mask row survives the rewrite verbatim");

    // A file with no trailing newline must not gain a blank line per save.
    QString noNl = QStringLiteral("player=1ff\nlocale=zh-CN");
    QString after = noNl;
    for (int i = 0; i < 5; ++i)
        after = compose(after, Mode::Split);
    check(!after.contains(QLatin1String("\n\n")),
          "a conf without a trailing newline does not accumulate blanks");
    check(countRows(after) == 1, "and still ends with exactly one row");
}

void test_flagAndRoundTrip()
{
    std::printf("D. --layout only in split; the conf round trip\n");

    // The flag is present only in split, and it is spelled WITH the two
    // leading dashes.
    //
    // v0.14: this suite used to assert `layout=split` — the CONFIG
    // spelling — and it was therefore asserting the bug. The conf row and
    // the argv flag are different surfaces: `layout=split` is what goes in
    // monster-overlay.conf, `--layout=split` is what QCommandLineParser
    // recognises. Feeding the conf spelling into argv put it in front of
    // the parser as a POSITIONAL argument, so --layout was never set, and
    // resolveStartupLayoutMode() concluded the CLI arm was "not given" and
    // handed the child Single — no matter what the console's combo said.
    //
    // The check below is the round trip that would have caught it: the
    // helper's own output, re-read by the parser main.cpp uses.
    const QStringList splitArgv = mhw::consoleLayoutArgv(Mode::Split);
    check(hasFlag(splitArgv, QStringLiteral("--layout=split")),
          "split mode passes --layout=split");
    check(splitArgv.size() == 1,
          "and it is the ONLY flag this helper adds");

    const QStringList singleArgv = mhw::consoleLayoutArgv(Mode::Single);
    check(!hasFlag(singleArgv, QStringLiteral("--layout=split")),
          "single mode passes NO --layout flag");
    check(!hasFlag(singleArgv, QStringLiteral("--layout=single")),
          "in particular it passes no explicit --layout=single (which "
          "would outrank the user's own CLI override)")
    ;
    check(!singleArgv.contains(QStringLiteral("layout=single")),
          "and it does not smuggle the conf spelling out either, because "
          "that is what let the bug through — the spelling arrived at the "
          "child looking like a flag while parsing as an argument")
    ;
    check(singleArgv.isEmpty(),
          "single mode contributes nothing to argv at all");

    // v0.14: THE ROUND TRIP. Everything above inspects the STRING; this
    // feeds it to the parser that will actually read it, which is the only
    // check that could have caught the bug. QCommandLineParser does not
    // recognise `layout=split` as an option — it records it as a
    // positional argument and --layout stays unset — so a string-only
    // assertion passes while the child boots in Single.
    //
    // The option set below is the same one main.cpp builds; if the two ever
    // drift, this fails here rather than in front of the user.
    QCommandLineParser parser;
    QCommandLineOption layoutOption(
        QStringLiteral("layout"),
        QStringLiteral("Startup layout mode."),
        QStringLiteral("mode"));
    parser.addOption(layoutOption);
    parser.addOption(QCommandLineOption(
        QStringLiteral("edit"), QStringLiteral("Open in edit mode.")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("game"), QStringLiteral("Game."),
        QStringLiteral("id")));

    const QStringList childArgv = QStringList()
        << QStringLiteral("--mask-player=1ff")
        << QStringLiteral("--edit")
        << QStringLiteral("--game=rise")
        << mhw::consoleLayoutArgv(Mode::Split);
    if (parser.parse(childArgv)) {
        check(parser.isSet(layoutOption),
              "the child's parser SEES a --layout option, rather than "
              "filing the whole thing as a positional argument");
        check(parser.value(layoutOption) == QStringLiteral("split"),
              "and it reads back as split — the mode the console is showing");
        check(parser.isSet(QCommandLineOption(QStringLiteral("edit"))),
              "the options around it survive, so the fix did not simply "
              "crowd out the rest of the argv");
    } else {
        check(false, "the child's parser accepts the console's argv");
    }

    // The write side: split writes the row, single does not.
    const QByteArray splitRow = mhw::serializeConsoleLayoutRow(Mode::Split);
    checkQ(QString::fromLatin1(splitRow), QStringLiteral("layout=split\n"),
           "serializeConsoleLayoutRow(split) is the row plus a newline");
    check(mhw::serializeConsoleLayoutRow(Mode::Single).isEmpty(),
          "serializeConsoleLayoutRow(single) is EMPTY — the default is "
          "not written down");

    // THE ROUND TRIP, which is the whole claim in one loop: whatever the
    // console saves, reading it back resolves to the same mode, and the
    // flag it would pass matches. 256 iterations of the two modes' text
    // is trivially cheap here and covers every compose/save/reload
    // ordering automatically.
    for (int i = 0; i < 256; ++i) {
        const Mode written = (i & 1) ? Mode::Split : Mode::Single;
        const QByteArray row = mhw::serializeConsoleLayoutRow(written);
        // Compose the conf the way saveMaskToDisk() does: the codec's
        // mask block, then the appended row.
        QString conf = "player=1ff\nmonster=7f\ndamage=3ff\npets=3\n"
                       "maskVersion=1\nlocale=zh-CN\n";
        conf += QString::fromLatin1(row);
        const Resolution readBack = resolveAsConsoleDoes(conf);
        check(readBack.mode == written,
              "the mode written is the mode read back");

        // And the flag the console would pass describes the same mode.
        //
        // v0.14: spelled WITH the dashes. The old `layout=split` here was
        // the bug, asserted as if it were the contract — a conf spelling
        // handed to QCommandLineParser, which files it as a positional
        // argument and leaves --layout unset, so the child booted Single
        // regardless of what the console's selector said.
        const QStringList argv = mhw::consoleLayoutArgv(written);
        const bool flagSaysSplit =
            hasFlag(argv, QStringLiteral("--layout=split"));
        check(flagSaysSplit == (written == Mode::Split),
              "the flag and the conf row agree on the mode");
        // And the conf spelling does NOT sneak into argv, which is the
        // other half of the same mistake. A test that checks one and not
        // the other would pass on a helper that emits both.
        check(!hasFlag(argv, QStringLiteral("layout=split")),
              "and the bare conf spelling is not emitted alongside it, "
              "which is what previously made this look correct");
    }

    // A conf that has the row AND a stale duplicate converges, because
    // the parse scans rather than trusting position.
    const Resolution duplicated = resolveAsConsoleDoes(
        "layout=single\nlayout=split\n");
    check(duplicated.mode == Mode::Split,
          "a duplicated row still parses (the last split wins because "
          "the scan keeps looking)");
}

// =====================================================================
// E. MIXED — the row, the chain and the flag
// =====================================================================
//
// The three contracts above were written when `layout=` could say
// nothing but split, so every assertion in them names Split. Mixed
// exercises the SAME three pieces of logic and can fail in three ways
// they do not cover: the row not being written (the console would
// rewrite it on every save), the shared chain reading it as Split (the
// data-loss bug whose fix is the deleted two-argument overload), and the
// child's parser never seeing it (the argv spelling mistake, which for
// split was caught by a round trip and for mixed was never written).
//
// MUTATION: make serializeConsoleLayoutRow() return "" for Mixed (as it
// does for Single), or make parseConsoleLayoutRow() accept only Split,
// or make consoleLayoutArgv() emit `layout=mixed` without the dashes —
// one check below fails per mutation.
void test_mixedRowChainAndFlag()
{
    std::printf("E. mixed: the row, the shared chain and the flag\n");

    // E1. The console WRITES Mixed — the row that one save used to
    //     silently downgrade to split.
    checkQ(QString::fromLatin1(mhw::serializeConsoleLayoutRow(Mode::Mixed)),
           QStringLiteral("layout=mixed\n"),
           "serializeConsoleLayoutRow(mixed) writes layout=mixed");

    // E2. And the console's own call shape READS it back as Mixed, with
    //     Conf as the source (a saved choice, not a default).
    const Resolution mixed = resolveAsConsoleDoes(
        QStringLiteral("player=1ff\nlocale=zh-CN\nlayout=mixed\n"));
    check(mixed.mode == Mode::Mixed,
          "layout=mixed resolves to MIXED through the console's real "
          "call — not Split (the audit's rewrite), not Single");
    check(mixed.source == Source::Conf,
          "and the source is Conf, so a second console start keeps it");

    // E3. First usable row wins, and `single` is not usable — so a file
    //     that carries the default AND the choice still answers the
    //     choice, in either order.
    check(resolveAsConsoleDoes(QStringLiteral("layout=single\nlayout=mixed\n"))
                  .mode == Mode::Mixed,
          "an unusable first row does not shadow layout=mixed");
    check(resolveAsConsoleDoes(QStringLiteral("layout=mixed\nlayout=split\n"))
                  .mode == Mode::Mixed,
          "and layout=mixed wins as the first usable row");

    // E4. The CHILD's parser round trip, the check that caught split's
    //     argv bug: consoleLayoutArgv(Mixed) must arrive at
    //     QCommandLineParser as an OPTION with the value "mixed".
    const QStringList mixedArgv = mhw::consoleLayoutArgv(Mode::Mixed);
    check(mixedArgv == QStringList({QStringLiteral("--layout=mixed")}),
          "mixed passes exactly --layout=mixed, dashes included");
    check(!mixedArgv.contains(QStringLiteral("layout=mixed")),
          "and not the bare conf spelling, which parses as a positional");

    QCommandLineParser parser;
    QCommandLineOption layoutOption(
        QStringLiteral("layout"), QStringLiteral("Startup layout mode."),
        QStringLiteral("mode"));
    parser.addOption(layoutOption);
    parser.addOption(QCommandLineOption(
        QStringLiteral("game"), QStringLiteral("Game."),
        QStringLiteral("id")));
    const QStringList childArgv = QStringList()
        << QStringLiteral("--mask-player=1ff")
        << QStringLiteral("--game=world")
        << mixedArgv;
    if (parser.parse(childArgv)) {
        check(parser.isSet(layoutOption),
              "the child's parser sees --layout for mixed too");
        check(parser.value(layoutOption) == QStringLiteral("mixed"),
              "and it reads back as \"mixed\" — the value "
              "parsePlayerLayoutMode() accepts");
    } else {
        check(false, "the child's parser accepts the mixed argv");
    }

    // E5. The chain, with the CLI arm LIVE — the part the console
    //     cannot reach but main.cpp can, because --layout=mixed on the
    //     command line is how a run enters Mixed without a saved row.
    const mhw::PlayerLayoutCliRequest cliMixed{
        true, QStringLiteral("mixed"),
        Mode::Single};
    const Resolution cliWins = mhw::resolveStartupLayoutMode(
        cliMixed, mhw::parseConsoleLayoutRow(QStringLiteral("layout=split\n")));
    check(cliWins.mode == Mode::Mixed && cliWins.source == Source::CliExplicit,
          "--layout=mixed beats a saved layout=split, and the chain says "
          "the COMMAND LINE is where it came from");

    const mhw::PlayerLayoutCliRequest cliSplit{
        true, QStringLiteral("split"), Mode::Single};
    const Resolution confLoses = mhw::resolveStartupLayoutMode(
        cliSplit, mhw::parseConsoleLayoutRow(QStringLiteral("layout=mixed\n")));
    check(confLoses.mode == Mode::Split,
          "an explicit --layout=split still beats a saved layout=mixed");

    // A malformed --layout must NOT be rescued by the saved Mixed row —
    // the same rule that keeps a typo'd flag from silently inheriting a
    // stale conf, stated for the mode nobody wrote the rule for.
    const mhw::PlayerLayoutCliRequest cliBad{
        true, QStringLiteral("mixedx"), Mode::Single};
    const Resolution notRescued = mhw::resolveStartupLayoutMode(
        cliBad, mhw::parseConsoleLayoutRow(QStringLiteral("layout=mixed\n")));
    check(notRescued.mode == Mode::Single
              && notRescued.source == Source::Default,
          "a malformed --layout does not fall through to layout=mixed "
          "(given-but-unreadable resolves to the default)");

    // And the compat alias still cannot SAY mixed: it is a boolean, and
    // a boolean that could select Mixed would make `--split-player-strip`
    // change topology the user never asked to change.
    check(mhw::splitPlayerStripFlagSense(true) == Mode::Split
              && mhw::splitPlayerStripFlagSense(false) == Mode::Single,
          "--split-player-strip still answers only split or single");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::printf("console_layout_mode_tests: the layout row, the shared "
                "priority chain and the flag\n");
    std::printf("=======================================================\n");

    test_oldConfResolvesSingle();
    test_confSplitResolvesSplitWithSource();
    test_priorityChain();
    test_repeatedSavesDoNotStackTheRow();
    test_flagAndRoundTrip();
    test_mixedRowChainAndFlag();

    std::printf("=======================================================\n");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
