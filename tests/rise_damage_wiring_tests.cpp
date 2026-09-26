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
    checkContains(mainSource,
                  "damagePanel.setVisible(damagePanel.panelEnabled()"
                  "&&damagePanel.hasVisibleContent());",
                  "Rise damage panel hides itself without data, like World");
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
    checkContains(coreTarget, "src/rise/rise_damage_types.h",
                  "core target tracks the shared Rise damage types");

    // v0.10.1: the companion (pets) surface is Rise-only end to end.
    const std::string panelSource =
        compact(readFile(root / "src/ui/control_panel.cpp"));
    checkContains(mainSource,
                  "setPanelEnabled(isRise&&!parser.isSet(noPetsOption))",
                  "World never mounts the Rise-only pet surface");
    checkContains(panelSource, "navButton->setVisible(petsAvailable)",
                  "console hides the pets rail card per game");
    checkContains(panelSource, "setPanelPresent(3,petsAvailable)",
                  "console removes the pets stage tile per game");

    if (failures == 0)
        std::cout << "\nrise-damage-wiring-tests: ALL PASSED\n";
    else
        std::cerr << "\nrise-damage-wiring-tests: " << failures << " FAILURE(S)\n";
    return failures == 0 ? 0 : 1;
}
