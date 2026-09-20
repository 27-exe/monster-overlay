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

} // namespace

int main()
{
    const std::filesystem::path root{MONSTER_SOURCE_DIR};
    const std::string mainSource = compact(readFile(root / "src/main.cpp"));
    const std::string readerSource =
        compact(readFile(root / "src/rise/rise_damage_reader.cpp"));
    const std::string cmakeSource = compact(readFile(root / "CMakeLists.txt"));
    const std::string_view overlayTarget =
        cmakeCall(cmakeSource, "add_executable(monster-overlay");
    const std::string_view coreTarget =
        cmakeCall(cmakeSource, "add_library(monster-core");

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
    checkContains(mainSource,
                  "petDamagePanel.setVisible(petDamagePanel.panelEnabled());",
                  "Rise keeps the enabled pets surface mounted while waiting for data");
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
    checkContains(overlayTarget, "src/ui/panel_pet_damage.h",
                  "product target tracks the fourth panel header");
    checkContains(overlayTarget, "src/ui/panel_pet_damage.cpp",
                  "product target compiles the fourth panel source");
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
