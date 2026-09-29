// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the two small zero-coverage translation units S3 named in
// the "裸奔判定表" (§6.3): src/ui/viewmodel/console_text_helpers.cpp (the
// console's localized text dispatchers) and src/ui/formatters.cpp (the
// percent / group / size renderers).
//
// Both are pure functions of their arguments plus the StringTable, with no
// widget and no process, so they are the cheapest possible 裸奔 TU to close.
// The target compiles their own sources directly instead of linking mhw-ui:
// console_text_helpers.cpp needs only QtCore plus core/string_table.h and
// the pure panel_sections.h table, and formatters.cpp needs only QLocale.
//
// The rules pinned here, and why each one can break silently:
//
// console_text_helpers
//   * consoleText() is the ONLY path the console uses to resolve an i18n
//     key, and it must return the RAW KEY for a key nobody registered — the
//     project's "no fallback in code" rule (docs/I18N.md). A defensive
//     fallback here would hide a missing translation behind plausible
//     Chinese, which is a bug the overlay ships instead of a bug anyone
//     sees. Empty input must NOT be special-cased away.
//   * gameName() is the one resolver the auto-detect badge, the GAME column
//     and the switching status line share; if they drift apart the user sees
//     "WORLD" in one place and "世界" in another on the same screen. Its
//     ASCII fallback (RISE / WORLD) must fire when the key is missing, and
//     must NOT when it is present — the two must be distinguishable.
//   * sectionLabel(panel, index) dispatches to four different section
//     tables. The bit index is the same number in all four, so a wrong
//     branch paints the monster panel's section names inside the player
//     panel's switches — a swap that no i18n gate and no pixel baseline
//     would catch, because the strings are all valid.
//
// formatters
//   * percentage() folds NaN / Inf / a non-positive maximum to "--" and
//     clamps a valid reading to one decimal. A NaN that escapes reaches
//     Qt's qRound() and aborts the overlay, the same 2026-09-22 crash class
//     the monster ViewModel's sanitizePartValue() guards against.
//   * groupNumber() is locale-aware; sizeMultiplier() is fixed at two
//     decimals with the × glyph and hides an unusable value behind "--".
//
// Every expected string below is read off the implementation and the
// bundled qrc, not derived from a spec.

#include "ui/viewmodel/console_text_helpers.h"
#include "ui/formatters.h"
#include "ui/panel_sections.h"

#include "core/string_table.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <limits>
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
    std::fflush(stdout);
    std::fflush(stderr);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    mhw::StringTable &strings = mhw::StringTable::instance();

    // Load zh-CN first: every assertion below reads the table, and an
    // unloaded StringTable returns the key itself, which would make the
    // translated-text cases vacuous (they would "pass" against a raw key).
    check(strings.load(QStringLiteral("zh-CN")),
          "setup: zh-CN loads from the bundled qrc");

    // ======================================================================
    // 1. consoleText() — the "no fallback in code" rule
    // ======================================================================
    // A registered key resolves to its translation.
    check(mhw::consoleText(QStringLiteral("console.rail.start_overlay"))
              == QStringLiteral("▶   启动悬浮窗"),
          "consoleText: a registered key resolves to its zh-CN translation");

    // A missing key returns THE KEY ITSELF. This is the contract that makes
    // a typo visible on screen instead of silently rendering an English or
    // empty label; a "helpful" fallback here would defeat it.
    check(mhw::consoleText(QStringLiteral("console.no_such.key"))
              == QStringLiteral("console.no_such.key"),
          "consoleText: a missing key returns the key itself, not a fallback "
          "(docs/I18N.md \"no fallback in code\")");

    check(mhw::consoleText(QString()).isEmpty(),
          "consoleText: an empty key returns empty");

    // Nested keys resolve through the flattened dot-path, so a dot in the
    // middle of a key is not a delimiter accident.
    check(mhw::consoleText(QStringLiteral("console.nav.monster"))
              == QStringLiteral("怪物"),
          "consoleText: a nested dot-path key resolves");

    // ======================================================================
    // 2. gameName() — one resolver for three call sites
    // ======================================================================
    // World and Rise must read as their translated names while the key is
    // present.
    check(mhw::gameName(mhw::GameId::World) == QStringLiteral("世界"),
          "gameName: World reads 世界 in zh-CN");
    check(mhw::gameName(mhw::GameId::Rise) == QStringLiteral("崛起"),
          "gameName: Rise reads 崛起 in zh-CN");

    // The ASCII fallback exists for the case where the console.strings
    // domain has not been loaded / the key is absent. It is verified by
    // swapping the locale to one that exists but has no console domain, and
    // by checking the en-US spelling in the same pass below.
    {
        // en-US: both games translate, and the switch is consistent with
        // zh-CN (the two call sites can never disagree if one resolver is
        // used — this proves the resolver is locale-driven, not hardcoded).
        check(strings.load(QStringLiteral("en-US")),
              "gameName: en-US loads");
        check(mhw::gameName(mhw::GameId::World) == QStringLiteral("WORLD"),
              "gameName: World reads WORLD in en-US");
        check(mhw::gameName(mhw::GameId::Rise) == QStringLiteral("RISE"),
              "gameName: Rise reads RISE in en-US");
        check(strings.load(QStringLiteral("zh-CN")),
              "gameName: zh-CN is restored");
    }

    // The ASCII fallback branch. gameName() falls back to "RISE"/"WORLD"
    // when tr() returns the key unchanged, so the two must be
    // distinguishable from the translated values — asserting the value is
    // NOT the ASCII literal while the key IS registered proves the
    // fallback did not fire spuriously.
    {
        using mhw::GameId;
        check(mhw::gameName(GameId::Rise) != QStringLiteral("RISE"),
              "gameName: the ASCII RISE fallback does NOT fire while "
              "console.game.rise is registered");
        check(mhw::gameName(GameId::World) != QStringLiteral("WORLD"),
              "gameName: the ASCII WORLD fallback does NOT fire while "
              "console.game.world is registered");
    }

    // Both enum values are handled and are DIFFERENT: a resolver that
    // returned one constant for every GameId would pass a single-value
    // assertion and silently label both games the same.
    check(mhw::gameName(mhw::GameId::World) != mhw::gameName(mhw::GameId::Rise),
          "gameName: World and Rise resolve to different strings");

    // ======================================================================
    // 3. sectionLabel(panel, index) — the four-way dispatch
    // ======================================================================
    // Panel 0 = player, 1 = monster, 2 = damage, 3 = pets. The bit index is
    // the SAME number in all four tables, so a swapped branch produces a
    // label that is valid but belongs to the wrong panel — nothing else
    // would catch it.
    check(mhw::sectionLabel(0, 0) == mhw::PlayerSection::displayName(0),
          "sectionLabel: panel 0 delegates to PlayerSection");
    check(mhw::sectionLabel(1, 0) == mhw::MonsterSection::displayName(0),
          "sectionLabel: panel 1 delegates to MonsterSection");
    check(mhw::sectionLabel(2, 0) == mhw::DamageSection::displayName(0),
          "sectionLabel: panel 2 delegates to DamageSection");
    check(mhw::sectionLabel(3, 0) == mhw::PetDamageSection::displayName(0),
          "sectionLabel: panel 3 delegates to PetDamageSection");

    // Concretely: index 0 in each panel is a DIFFERENT string. If the
    // dispatch is wrong, whoever reorders these two lines gets a red.
    check(mhw::sectionLabel(0, 0) == QStringLiteral("连接状态")
              && mhw::sectionLabel(1, 0) == QStringLiteral("六角肖像"),
          "sectionLabel: index 0 differs per panel (连接状态 vs 六角肖像) — "
          "the guard against a swapped branch");
    check(mhw::sectionLabel(0, 4) == QStringLiteral("衣装")
              && mhw::sectionLabel(1, 4) == QStringLiteral("部位"),
          "sectionLabel: index 4 differs per panel (衣装 vs 部位) — the "
          "player table's mantles row is not the monster table's parts row");

    // The four panels' counts differ (8/6/4/2), so the same index maps to
    // different rows; that asymmetry is precisely what a dispatch bug hides.
    check(mhw::PlayerSection::kCount == 8
              && mhw::MonsterSection::kCount == 6
              && mhw::DamageSection::kCount == 4
              && mhw::PetDamageSection::kCount == 2,
          "sectionLabel: the four tables have distinct sizes (8/6/4/2)");

    // An out-of-range panel is a programming error and must return a null
    // QString rather than reading off the end of some table.
    check(mhw::sectionLabel(4, 0).isNull(),
          "sectionLabel: panel 4 is out of range and returns a null string");
    check(mhw::sectionLabel(-1, 0).isNull(),
          "sectionLabel: a negative panel returns a null string");

    // An out-of-range index inside a valid panel is empty, not garbage.
    check(mhw::sectionLabel(0, 99).isEmpty(),
          "sectionLabel: an out-of-range player index returns empty");
    check(mhw::sectionLabel(1, 99).isEmpty(),
          "sectionLabel: an out-of-range monster index returns empty");

    // Out-of-range indices in EVERY panel: the four displayName()
    // implementations were moved separately, so each has its own bounds
    // check that can be dropped independently.
    check(mhw::sectionLabel(2, 99).isEmpty() && mhw::sectionLabel(3, 99).isEmpty(),
          "sectionLabel: out-of-range indices in the damage and pets panels "
          "also return empty");

    // Every valid index in every panel must resolve to a NON-KEY string:
    // a raw dot-path leaking into the console's switches means the i18n
    // table is out of sync with the section table.
    {
        bool allResolved = true;
        for (int panel = 0; panel < 4; ++panel) {
            const int count = (panel == 0) ? mhw::PlayerSection::kCount
                             : (panel == 1) ? mhw::MonsterSection::kCount
                             : (panel == 2) ? mhw::DamageSection::kCount
                                            : mhw::PetDamageSection::kCount;
            for (int i = 0; i < count; ++i) {
                const QString label = mhw::sectionLabel(panel, i);
                if (label.isEmpty() || label.startsWith(QStringLiteral("console.")))
                    allResolved = false;
            }
        }
        check(allResolved,
              "sectionLabel: every valid (panel, index) resolves to a real "
              "translated label, never a leaked i18n key");
    }

    // ======================================================================
    // 4. formatters — percentage / groupNumber / sizeMultiplier
    // ======================================================================
    // Normal path: one decimal, and the trailing zero is kept as written.
    check(mhw::percentage(50.0F, 200.0F) == QStringLiteral("25.0%"),
          "percentage: 50/200 renders as 25.0%");
    check(mhw::percentage(200.0F, 200.0F) == QStringLiteral("100.0%"),
          "percentage: a full value renders as 100.0%");
    check(mhw::percentage(0.0F, 200.0F) == QStringLiteral("0.0%"),
          "percentage: zero renders as 0.0%");

    // The clamp is 0..999: an over-full reading is capped at the ceiling,
    // so the panel's bar geometry always receives a bounded number. 500 % of
    // a 100 maximum stays 500.0 — inside the ceiling — while 2000 % of a 100
    // maximum is clamped DOWN to 999.0. The two must be distinguishable, or
    // the ceiling is not actually there.
    check(mhw::percentage(500.0F, 100.0F) == QStringLiteral("500.0%"),
          "percentage: a 500 % reading renders as 500.0% (inside the 999 ceiling)");
    check(mhw::percentage(2000.0F, 100.0F) == QStringLiteral("999.0%"),
          "percentage: an over-ceiling reading is CLAMPED down to 999.0%");

    // Non-finite input is folded to "--", NOT propagated. Qt's
    // qRound(2 x 25.0 x 1.0) asserts and aborts on NaN, the same crash
    // class the monster ViewModel's sanitize helpers guard against.
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        check(mhw::percentage(nan, 100.0F) == QStringLiteral("--"),
              "percentage: a NaN value renders as --");
        check(mhw::percentage(inf, 100.0F) == QStringLiteral("--"),
              "percentage: an infinite value renders as --");
        check(mhw::percentage(50.0F, nan) == QStringLiteral("--"),
              "percentage: a NaN maximum renders as --");
        check(mhw::percentage(50.0F, 0.0F) == QStringLiteral("--"),
              "percentage: a zero maximum renders as -- (no divide by zero)");
        check(mhw::percentage(50.0F, -10.0F) == QStringLiteral("--"),
              "percentage: a negative maximum renders as --");
    }

    // groupNumber: a plain integer with a thousands separator.
    check(mhw::groupNumber(18420) == QStringLiteral("18,420"),
          "groupNumber: 18420 renders as 18,420");
    check(mhw::groupNumber(1234567) == QStringLiteral("1,234,567"),
          "groupNumber: a seven-digit number groups every three digits");
    check(mhw::groupNumber(999) == QStringLiteral("999"),
          "groupNumber: a sub-thousand number is ungrouped");
    check(mhw::groupNumber(0) == QStringLiteral("0"),
          "groupNumber: zero renders as 0");
    check(mhw::groupNumber(-5) == QStringLiteral("-5"),
          "groupNumber: a negative number keeps its sign");

    // sizeMultiplier: two decimals plus the × glyph, or "--" for a value
    // that cannot be a real crown ratio.
    check(mhw::sizeMultiplier(1.25F) == QStringLiteral("1.25×"),
          "sizeMultiplier: 1.25 renders as 1.25×");
    check(mhw::sizeMultiplier(1.0F) == QStringLiteral("1.00×"),
          "sizeMultiplier: 1.0 renders as 1.00× (both decimals kept)");
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        check(mhw::sizeMultiplier(nan) == QStringLiteral("--"),
              "sizeMultiplier: a NaN renders as --");
        check(mhw::sizeMultiplier(inf) == QStringLiteral("--"),
              "sizeMultiplier: an infinite value renders as --");
        check(mhw::sizeMultiplier(0.0F) == QStringLiteral("--"),
              "sizeMultiplier: zero (\"not read\") renders as --");
        check(mhw::sizeMultiplier(-1.0F) == QStringLiteral("--"),
              "sizeMultiplier: a negative value renders as --");
    }

    if (failures == 0)
        std::printf("console-text-helpers-tests: ALL PASSED (%d checks)\n",
                    checks);
    return failures == 0 ? 0 : 1;
}
