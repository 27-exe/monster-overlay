// SPDX-License-Identifier: Apache-2.0
// Source-level contract test for the Rise pet-damage CLI/panel wiring.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef MONSTER_SOURCE_DIR
#error "MONSTER_SOURCE_DIR must name the repository root"
#endif

namespace {

int failures = 0;

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "FAIL: cannot open " << path << '\n';
        ++failures;
        return {};
    }
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

// True when the '#' at `at` starts a C/C++ preprocessor directive
// (#include / #if / #else / #endif / #define / #pragma / …). Their bodies are
// live source text for every caller that scans a .cpp, so they must never be
// treated as a comment; a CMake '#' comment line never begins with one of these
// keywords, so the exclusion cannot swallow a real comment either.
bool isPreprocessorDirective(std::size_t at, std::string_view source)
{
    static constexpr std::string_view kDirectivePrefixes[]{
        "include", "import", "if", "ifdef", "ifndef", "else", "elif",
        "endif", "define", "undef", "pragma", "line", "warning", "error",
        "embed", "assert", "unassert",
    };
    // Bounded to avoid an O(n^2) scan on pathological input.
    const std::size_t maxKeyword = 16;
    const std::size_t limit = std::min(at + maxKeyword, source.size());
    for (const std::string_view prefix : kDirectivePrefixes) {
        if (at + prefix.size() > limit) continue;
        if (source.substr(at + 1, prefix.size()) == prefix
            && (at + 1 + prefix.size() == source.size()
                || !std::isalnum(static_cast<unsigned char>(
                       source[at + 1 + prefix.size()])))) {
            return true;
        }
    }
    return false;
}

std::string compact(std::string_view source)
{
    std::string out;
    out.reserve(source.size());
    bool inLineComment = false;
    bool inBlockComment = false;
    bool inString = false;
    bool escaped = false;

    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        const char next = i + 1 < source.size() ? source[i + 1] : '\0';
        if (inLineComment) {
            if (c == '\n') inLineComment = false;
            continue;
        }
        if (inBlockComment) {
            if (c == '*' && next == '/') {
                inBlockComment = false;
                ++i;
            }
            continue;
        }
        // CMake's comment marker has no C++ counterpart, and CMake comments may
        // carry parens (e.g. a note about StringTable::tr()), which would throw
        // off cmakeCall()'s paren-balanced target slicing if they survived into
        // the compressed text. Only a '#' at the start of a line (leading
        // whitespace allowed) begins a comment — but the same code path also
        // scans C++ sources, where a leading '#' is normally a preprocessor
        // directive whose text matters, so the directive families are excluded.
        if (!inString && (i == 0 || source[i - 1] == '\n')) {
            std::size_t j = i;
            while (j < source.size() && (source[j] == ' ' || source[j] == '\t')) ++j;
            if (j < source.size() && source[j] == '#' && !isPreprocessorDirective(j, source)) {
                inLineComment = true;
                continue;
            }
        }
        if (!inString && c == '/' && next == '/') {
            inLineComment = true;
            ++i;
            continue;
        }
        if (!inString && c == '/' && next == '*') {
            inBlockComment = true;
            ++i;
            continue;
        }
        if (c == '"' && !escaped) inString = !inString;
        escaped = inString && c == '\\' && !escaped;
        if (c != '\\') escaped = false;
        if (!inString && (c == ' ' || c == '\t' || c == '\r' || c == '\n'))
            continue;
        out.push_back(c);
    }
    return out;
}

std::string_view cmakeCall(std::string_view source, std::string_view start)
{
    const std::size_t begin = source.find(start);
    if (begin == std::string_view::npos) return {};

    int depth = 0;
    for (std::size_t i = begin; i < source.size(); ++i) {
        if (source[i] == '(') {
            ++depth;
        } else if (source[i] == ')' && --depth == 0) {
            return source.substr(begin, i - begin + 1);
        }
    }
    return {};
}

void checkContains(std::string_view haystack, std::string_view needle,
                   std::string_view description)
{
    if (haystack.find(needle) != std::string_view::npos) {
        std::cout << "PASS: " << description << '\n';
    } else {
        std::cerr << "FAIL: " << description << " (missing `" << needle << "`)\n";
        ++failures;
    }
}

// The inverse assertion, for guarding against a regression back to a rule we
// deliberately removed (e.g. a branch that force-shows a panel regardless of
// whether it has anything to draw).
// v1.0.0: the boolean sibling of checkContains(). The branch test this
// file now carries is not a substring question — it is "is this offset
// between these two offsets", which has no haystack to hand a
// checkContains call. Kept next to the other two so every assertion in
// this file reads the same way.
// v1.0.0: find the `} else {` that ACTUALLY closes the arm starting at
// `armStart`, by walking brace depth from the `if`'s opening brace.
//
// Why this exists instead of a substring search for "}else{": the Rise
// poll arm contains two NESTED else branches of its own — the else of
// `if (newestReader)` and the else of `if (feedError != ...)` — so the
// first "}else{" after `if (isRise)` is one of those, several hundred
// characters early. A substring test that anchored on it reported all
// nine drive positions missing while the code was correct, which is the
// worst kind of test: it fails on a fix and passes on the bug's absence
// for the wrong reason.
//
// The compaction in compact() strips comments and whitespace but not
// braces, so depth-walking is exact. Returns npos when the arm is not
// closed, which is itself a finding worth reporting.
std::size_t armElseOffset(std::string_view src, std::size_t armStart)
{
    const auto open = src.find('{', armStart);
    if (open == std::string_view::npos)
        return std::string_view::npos;

    int depth = 0;
    for (std::size_t i = open; i < src.size(); ++i) {
        if (src[i] == '{')
            ++depth;
        else if (src[i] == '}') {
            --depth;
            if (depth == 0) {
                // The arm's closing brace. What follows is the `else`.
                const auto after = src.find('{', i);
                if (after != std::string_view::npos) {
                    const auto tail = src.substr(i + 1, after - i - 1);
                    if (tail.find("else") != std::string_view::npos)
                        return after;
                }
                return std::string_view::npos;
            }
        }
    }
    return std::string_view::npos;
}

void checkTrue(bool condition, std::string_view description)
{
    if (condition)
        std::cout << "PASS: " << description << '\n';
    else
        std::cerr << "FAIL: " << description << '\n';
    if (!condition)
        ++failures;
}

void checkNotContains(std::string_view haystack, std::string_view needle,
                      std::string_view description)
{
    if (haystack.find(needle) == std::string_view::npos) {
        std::cout << "PASS: " << description << '\n';
    } else {
        std::cerr << "FAIL: " << description << " (unexpected `" << needle
                  << "`)\n";
        ++failures;
    }
}

} // namespace

int main()
{
    const std::filesystem::path root{MONSTER_SOURCE_DIR};
    const std::string mainSource = compact(readFile(root / "src/main.cpp"));
    const std::string readerSource =
        compact(readFile(root / "src/rise/rise_damage_reader.cpp"));
    const std::string cmakeSource = compact(readFile(root / "CMakeLists.txt"));
    const std::string_view coreTarget =
        cmakeCall(cmakeSource, "add_library(monster-core");
    const std::string_view uiTarget =
        cmakeCall(cmakeSource, "add_library(mhw-ui");
    const std::string_view riseTarget =
        cmakeCall(cmakeSource, "add_library(game-rise");

    checkContains(mainSource,
                  "QCommandLineOptionmaskPetsOption(QStringLiteral(\"mask-pets\")",
                  "--mask-pets is declared");
    checkContains(mainSource,
                  "QCommandLineOptionnoPetsOption(QStringLiteral(\"no-pets\")",
                  "--no-pets is declared");
    checkContains(mainSource,
                  "QCommandLineOptionoutputPetsOption(QStringLiteral(\"output-pets\")",
                  "--output-pets is declared");
    checkContains(mainSource, "parser.addOption(maskPetsOption);",
                  "--mask-pets is registered");
    checkContains(mainSource, "parser.addOption(noPetsOption);",
                  "--no-pets is registered");
    checkContains(mainSource, "parser.addOption(outputPetsOption);",
                  "--output-pets is registered");
    checkContains(mainSource, "visibleDamageParty(",
                  "World damage path applies OtherMembers filter");

    checkContains(mainSource, "#include\"ui/panel_pet_damage.h\"",
                  "main includes the fourth panel header");
    checkContains(mainSource, "petDamagePanel.updateRiseDamage(damageSnapshot);",
                  "main delivers Rise snapshots to the fourth panel");
    // v0.10.10: Rise follows the World rule instead of staying mounted with a
    // placeholder. The pets surface must not be shown just because the user
    // enabled it — it needs rows to draw — so it gates on the same public
    // content verdict the main panel uses. (mainSource is run through
    // compact(), which strips every whitespace character, so these needles
    // carry no newlines or indentation.)
    checkContains(mainSource,
                  "petDamagePanel.setVisible(petDamagePanel.panelEnabled()"
                  "&&petDamagePanel.hasVisibleContent());",
                  "Rise mounts the pets surface only when it has rows to draw");
    // The needle below used to be the whole call, which made this test a
    // spelling check: adding a THIRD condition to the gate (v0.16 added
    // damagePanelHasBlocks(), so a Split launch with every content block
    // stripped stops mounting the shell that used to sit on HDMI) broke
    // the assertion even though the semantics it guards — "never
    // force-show the panel without data" — still held.
    //
    // What is actually being protected is the HEAD of the gate, not its
    // tail. So assert the prefix, and separately assert the new condition
    // is present, which is a stronger statement than the old one: the
    // panel is now gated on content AND on having something left to draw.
    checkContains(mainSource,
                  "damagePanel.setVisible(damagePanel.panelEnabled()"
                  "&&damagePanel.hasVisibleContent()",
                  "Rise damage panel hides itself without data, like World");
    checkContains(mainSource,
                  "damagePanelHasBlocks(",
                  "and the damage panel also stops mounting once the split "
                  "mask leaves it no content block to draw (v0.16)");
    checkNotContains(mainSource,
                     "damagePanel.setVisible(true);",
                     "no branch force-shows the damage panel without data");
    checkContains(mainSource, "diagnosticReader->lastErrorText()",
                  "main surfaces the selected Rise feed failure reason");
    checkContains(mainSource, "rise damage feed: available",
                  "main reports recovery after a feed failure");
    checkContains(mainSource, "mhr_damage_a.json",
                  "main reads REFramework data slot A");
    checkContains(mainSource, "mhr_damage_b.json",
                  "main reads REFramework data slot B");
    checkContains(mainSource, "findRiseInstallDir()",
                  "main locates the portable Rise data directory at runtime");
    checkContains(readerSource, "O_NOFOLLOW",
                  "reader opens the feed without following path swaps to symlinks");
    checkContains(readerSource, "O_NONBLOCK",
                  "reader cannot block the UI loop on a swapped FIFO");
    checkContains(readerSource, "fstat(",
                  "reader validates the inode actually opened");
    checkContains(readerSource, "S_ISREG(",
                  "reader accepts only a regular opened inode");
    // v0.11.1: the fourth panel moved into the mhw-ui library, so assert the
    // library owns it rather than that the executable lists it directly.
    checkContains(uiTarget, "src/ui/panel_pet_damage.h",
                  "ui library tracks the fourth panel header");
    checkContains(uiTarget, "src/ui/panel_pet_damage.cpp",
                  "ui library compiles the fourth panel source");
    // S3 (library split): the Rise damage types moved OUT of monster-core and
    // into game-rise along with the rest of Rise. The point of the assertion
    // survives — the shared types must be tracked by exactly one library, so
    // mhw-ui and the executable consume them without recompiling them — but
    // the library it asks about changed. Asserting this against monster-core
    // would now be asserting the split has NOT happened.
    checkContains(riseTarget, "src/rise/rise_damage_types.h",
                  "game-rise target tracks the shared Rise damage types");
    checkNotContains(coreTarget, "src/rise/rise_damage_types.h",
                     "the kernel no longer carries a Rise-specific header");

    // v0.10.1: the companion (pets) surface has no World data source —
    // `mhw::enrichRiseDamageSnapshot()` returns immediately unless the game
    // is Rise (src/rise/rise_damage_roster.cpp:87), and the JSON feed itself
    // is a Lua producer protocol with no World counterpart.
    const std::string panelSource =
        compact(readFile(root / "src/ui/control_panel.cpp"));
    //
    // v0.13 separated what used to be one expression. The old spelling was
    // `setPanelEnabled(isRise && !parser.isSet(noPetsOption))`, which
    // bundled "does this game have the data?" with "did the user switch it
    // off?" — two questions that live on two different axes everywhere else
    // in Panel (sectionMask_ filters CONTENT, panelEnabled_ decides
    // MOUNTING). It also made a World run report the pets panel as
    // USER-disabled, so the console drew its "Rise only" placeholder for
    // the wrong reason.
    //
    // So the gate is now the user toggle alone, and "the game has no pets"
    // is expressed where it belongs: the main loop's
    // `panelEnabled() && hasVisibleContent()` collapses a World pets window
    // instead of mounting an empty frame. Asserting the toggle is NOT
    // conflated with the game is the invariant — a regression back to
    // `isRise &&` would fail here.
    checkContains(mainSource,
                  "setPanelEnabled(!parser.isSet(noPetsOption))",
                  "the pets panel's mount is gated on the USER toggle, not "
                  "on the game, so the console can tell a user-disabled "
                  "panel from a game without pets");
    checkNotContains(mainSource,
                     "setPanelEnabled(isRise&&!parser.isSet(noPetsOption))",
                     "the pets panel's mount no longer bundles the game into "
                     "the user toggle — that conflation is what made a World "
                     "run look user-disabled");
    checkContains(mainSource,
                  "petDamagePanel.setVisible(petDamagePanel.panelEnabled()",
                  "a World pets window is collapsed by hasVisibleContent(), "
                  "not left as an empty frame");
    checkContains(panelSource, "navButton->setVisible(petsAvailable)",
                  "console hides the pets rail card per game");
    checkContains(panelSource, "setPanelPresent(3,petsAvailable)",
                  "console removes the pets stage tile per game");

    // ====================================================================
    // v1.0.0 — RISE + SPLIT ACTUALLY DRIVES THE THREE DAMAGE WINDOWS.
    //
    // 「控制台显示 dps 脚本正常，但是游戏内没显示悬浮窗」.
    //
    // Why this needs more than checkContains: the World arm's drive already
    // existed, so "the three windows are fed somewhere in main.cpp" was
    // TRUE while the bug was live. What was missing was the branch — the
    // World arm's `if (damageSplitOn)` sat inside `} else {`, and Rise took
    // the `if (isRise)` path, so on Rise the windows were constructed,
    // masked and never fed. A substring test cannot see that; only an
    // offset comparison between the `if (isRise)` marker and its `} else {`
    // can, because in the compacted source those two strings ARE the
    // branch boundary.
    //
    // Each window is checked twice: fed AND mounted. Feeding without
    // mounting is the v0.15 failure this file's own comment describes
    // ("Constructing a window is not driving one"); mounting without
    // feeding is what the bug looked like from the outside.
    // ====================================================================
    {
        // SECOND occurrence, deliberately. main.cpp has an early
        // `if (isRise)` that only decides which damage readers to build,
        // and the poll-time one further down that carries the whole Rise /
        // World split. Anchoring on the first makes this test measure the
        // wrong arm — which is exactly what the first version of it did,
        // reporting all nine positions missing when the code was correct.
        //
        // So: take the LAST `if(isRise)`. If someone adds a third one after
        // it the test will say so rather than quietly pass, because the
        // boundary it then finds belongs to the newer arm.
        const auto riseArm = mainSource.rfind("if(isRise)");
        const auto worldArm = armElseOffset(mainSource, riseArm);
        checkTrue(riseArm != std::string::npos,
              "v1.0.0: main.cpp still has the if(isRise) arm to compare against");
        checkTrue(worldArm != std::string::npos,
              "v1.0.0: the Rise arm is brace-closed with an else after it "
              "(depth-walked, because the arm contains nested else "
              "branches a substring search would mistake for this one)");
        checkTrue(riseArm < worldArm,
              "v1.0.0: the Rise arm opens before the World arm");

        // Guard the anchor itself. This test reasons about "the poll-time
        // Rise arm", and it finds that arm with rfind — so a THIRD
        // `if (isRise)` appearing later would silently become the anchor
        // and the test would start measuring a different arm while still
        // reporting green. Count them instead.
        int riseArms = 0;
        for (auto at = mainSource.find("if(isRise)");
             at != std::string::npos;
             at = mainSource.find("if(isRise)", at + 1))
            ++riseArms;
        checkTrue(riseArms == 2,
              "v1.0.0: main.cpp has exactly two `if (isRise)` arms — the "
              "reader-construction one and the poll-time one this test "
              "anchors on. A third would move the rfind anchor without any "
              "check here failing loudly.");

        if (riseArm != std::string::npos && worldArm != std::string::npos) {
            // Inside = after if(isRise), before } else {. Outside the arm
            // is where the World drive lives, which is exactly where the
            // Rise drive must NOT be.
            //
            // rfind, NOT find: main.cpp has `setVisible(editMode)` for these
            // windows at construction (main.cpp:1413-1416), long before the
            // poll arm. A find-from-zero therefore lands on the CONSTRUCTION
            // call, reports "not in the Rise arm", and does so while the
            // poll-side call sits right there. Searching forward from the
            // arm start is the only thing that means "the poll drives it".
            const auto inRiseArm = [&](std::string_view needle) {
                const auto at = mainSource.find(needle, riseArm);
                return at != std::string::npos && at < worldArm;
            };

            checkTrue(inRiseArm("damageShareStripPanel.updateRiseDamage(damageSnapshot)"),
                  "v1.0.0: the share window is FED on the Rise arm — the "
                  "World-only drive is what left Rise + split with no "
                  "damage surface at all");
            checkTrue(inRiseArm("damageChartStripPanel.updateRiseDamage(damageSnapshot)"),
                  "v1.0.0: the chart window is fed on the Rise arm");
            checkTrue(inRiseArm("damageRowsStripPanel.updateRiseDamage(damageSnapshot)"),
                  "v1.0.0: the per-player rows window is fed on the Rise "
                  "arm — this is the window whose missing drive the v0.15 "
                  "comment predicted for World");

            checkTrue(inRiseArm("damageShareStripPanel.updateRiseDamage(feedLost)"),
                  "v1.0.0: a LOST feed clears the share window too, so "
                  "Rise does not keep the last quest's numbers on screen "
                  "after the producer goes quiet");
            checkTrue(inRiseArm("damageChartStripPanel.updateRiseDamage(feedLost)"),
                  "v1.0.0: a lost feed clears the chart window");
            checkTrue(inRiseArm("damageRowsStripPanel.updateRiseDamage(feedLost)"),
                  "v1.0.0: a lost feed clears the rows window");

            checkTrue(inRiseArm("damageShareStripPanel.setVisible("),
                  "v1.0.0: the share window is MOUNTED on the Rise arm — fed "
                  "but never setVisible is the same invisible bug");
            checkTrue(inRiseArm("damageChartStripPanel.setVisible("),
                  "v1.0.0: the chart window is mounted on the Rise arm");
            checkTrue(inRiseArm("damageRowsStripPanel.setVisible("),
                  "v1.0.0: the rows window is mounted on the Rise arm");

            // And the negative: the World drive must stay OUT of the Rise
            // arm, or a Rise launch would take the GameSnapshot arm's path.
            checkTrue(!inRiseArm("damageShareStripPanel.update(damageSnap)"),
                  "v1.0.0: the Rise arm does not also run the World "
                  "GameSnapshot drive — the two arms feed different data");
        }
    }

    if (failures == 0)
        std::cout << "\nrise-damage-wiring-tests: ALL PASSED\n";
    else
        std::cerr << "\nrise-damage-wiring-tests: " << failures << " FAILURE(S)\n";
    return failures == 0 ? 0 : 1;
}
