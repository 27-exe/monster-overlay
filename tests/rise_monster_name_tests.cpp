// SPDX-License-Identifier: Apache-2.0
//
// v0.8.4-r19 monster-identity regression tests (offline, no game required):
//
//   1. mhw::riseMonsterName() — the official Rise monster name table
//      generated from HunterPie's zh-cn.xml (/Strings/Monsters/Rise/Monster).
//   2. mhw::riseMonsterSizeFromFactors() — the Rise crown ratio derived from
//      MHRSizeStructure, including the "1.00 is legitimate data, 0 means
//      not-read" contract that replaced the old fabricated 1.0F default.
//
// Every string is a verbatim copy of the localization file, so these
// assertions double as a re-extraction check: if the table is ever
// regenerated from a different file, the spot values below stop matching.

#include "monster/monster_types.h"
#include "rise/mhr_monster_names.h"
#include "rise/mhr_types.h"

#include <QCoreApplication>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition) {
        std::cout << "PASS: " << message << '\n';
    } else {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Compares against the exact UTF-8 bytes of the localization entry.
void checkName(int id, const char *expected, const char *message)
{
    const char *actual = mhw::riseMonsterName(id);
    check(actual != nullptr && std::string(actual) == expected, message);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ------------------------------------------------------------------
    // 1. Official localized names (zh-cn.xml, <Monsters><Rise>)
    // ------------------------------------------------------------------
    // Spotted values straight out of the localization file.
    checkName(1, "霸主·雌火龙", "Rise id 1 = 霸主·雌火龙 (Apex Rathian)");
    checkName(14, "冰牙龙", "Rise id 14 = 冰牙龙 (Barioth)");
    checkName(107, "怪异克服钢龙", "Rise id 107 = 怪异克服钢龙 (Risen Kushala Daora)");
    checkName(115, "岚龙", "Rise id 115 = 岚龙 (Amatsu)");

    // The monsters the live overlay panel actually met (r18 probe session:
    // ids 14 / 92 / 94).
    checkName(92, "红莲爆鳞龙", "Rise id 92 = 红莲爆鳞龙 (Seething Bazelgeuse)");
    checkName(94, "冰狼龙", "Rise id 94 = 冰狼龙 (Lunagaron)");

    // Boundaries of the two id blocks.
    checkName(0, "雌火龙", "Rise id 0 = 雌火龙 (Rathian)");
    checkName(46, "机关蛙", "Rise id 46 = 机关蛙 (the last entry before the gap)");
    checkName(76, "金火龙", "Rise id 76 = 金火龙 (first Sunbreak block entry)");
    checkName(98, "棘茶龙", "Rise id 98 = 棘茶龙 (the last entry before the last gap)");

    // Ids the localization file has no <Monster> entry for are misses, and a
    // miss must be nullptr (never "" and never a placeholder string).
    const int upstreamGaps[] = {47, 60, 75, 99, 106};
    for (const int id : upstreamGaps) {
        check(mhw::riseMonsterName(id) == nullptr,
              "id without a localization entry returns nullptr (gap)");
    }
    check(mhw::riseMonsterName(-1) == nullptr, "negative id returns nullptr");
    check(mhw::riseMonsterName(116) == nullptr, "id above the Rise range returns nullptr");
    check(mhw::riseMonsterName(100000) == nullptr, "absurd id returns nullptr");

    // Table-wide invariants: exactly 79 ids in 0..115 have a name, every name
    // is a non-empty string with no stray whitespace, and the lookup is
    // consistent across the whole domain (a broken binary search shows up as a
    // name that moves between calls).
    int named = 0;
    for (int id = 0; id <= 115; ++id) {
        const char *name = mhw::riseMonsterName(id);
        if (name == nullptr)
            continue;
        ++named;
        const std::string s(name);
        check(!s.empty(), "every known id maps to a non-empty name");
        check(s.find_first_of(" \t\r\n") == std::string::npos,
              "known names carry no stray whitespace");
        check(mhw::riseMonsterName(id) == name, "lookup is stable");
    }
    check(named == 79, "0..115 carries exactly the 79 upstream names");

    // ------------------------------------------------------------------
    // 2. Rise crown ratio (MHRSizeStructure product)
    // ------------------------------------------------------------------
    // ABI of the structure HunterPie reads at crownObject + 0x24.
    check(sizeof(mhw::MHRSizeStructure) == 8, "MHRSizeStructure is 8 bytes");
    check(offsetof(mhw::MHRSizeStructure, sizeMultiplier) == 0x00,
          "MHRSizeStructure.sizeMultiplier is at +0x00");
    check(offsetof(mhw::MHRSizeStructure, unkMultiplier) == 0x04,
          "MHRSizeStructure.unkMultiplier is at +0x04");
    check(mhw::kMhrSizeStructureOffset == 0x24ULL,
          "the crown structure sits at crownObject + 0x24");

    // The product itself (HunterPie MHRMonster.cs:508).
    check(mhw::riseMonsterSizeFromFactors(1.07F, 1.0F) == 1.07F,
          "1.07 x 1.0 -> 1.07 (large monster)");
    check(mhw::riseMonsterSizeFromFactors(0.95F, 1.0F) == 0.95F,
          "0.95 x 1.0 -> 0.95 (small monster)");
    check(mhw::riseMonsterSizeFromFactors(1.15F, 1.01F) > 1.16F,
          "both factors contribute to the ratio");

    // Regression for the reported symptom: a monster that really is at 100 %
    // must report 1.0 — 1.0 is data, not the "read failed" placeholder. The
    // placeholder is 0.
    check(mhw::riseMonsterSizeFromFactors(1.0F, 1.0F) == 1.0F,
          "a genuine 100% monster reads 1.0, not the unknown sentinel");
    check(mhw::MonsterSnapshot{}.size == 0.0F,
          "MonsterSnapshot.size defaults to 0 (unknown), not 1.0");

    // Unusable reads must never be reported as a size.
    check(mhw::riseMonsterSizeFromFactors(0.0F, 1.0F) == 0.0F,
          "a zero factor is rejected as not-read");
    check(mhw::riseMonsterSizeFromFactors(1.0F, 0.0F) == 0.0F,
          "a zero second factor is rejected as not-read");
    check(mhw::riseMonsterSizeFromFactors(-1.0F, 1.0F) == 0.0F,
          "a negative product is rejected as not-read");
    check(mhw::riseMonsterSizeFromFactors(std::nanf(""), 1.0F) == 0.0F,
          "NaN is rejected as not-read");
    check(mhw::riseMonsterSizeFromFactors(std::numeric_limits<float>::infinity(), 1.0F) == 0.0F,
          "Inf is rejected as not-read");
    check(mhw::riseMonsterSizeFromFactors(3.0F, 1.0F) == 0.0F,
          "an out-of-band (3x) ratio is rejected as a read artefact");
    check(mhw::riseMonsterSizeFromFactors(0.1F, 1.0F) == 0.0F,
          "an out-of-band (0.1x) ratio is rejected as a read artefact");
    // The band is inclusive on both ends and wide enough for every threshold
    // in MonsterData.xml (Mini 0.765 .. Gold 1.17).
    check(mhw::riseMonsterSizeFromFactors(0.5F, 1.0F) == 0.5F,
          "the lower band edge 0.5 is accepted");
    check(mhw::riseMonsterSizeFromFactors(1.5F, 1.0F) == 1.5F,
          "the upper band edge 1.5 is accepted");
    check(mhw::riseMonsterSizeFromFactors(0.765F, 1.0F) == 0.765F,
          "the smallest Mini crown in MonsterData.xml round-trips");
    check(mhw::riseMonsterSizeFromFactors(1.17F, 1.0F) == 1.17F,
          "the largest Gold crown in MonsterData.xml round-trips");

    // ------------------------------------------------------------------
    // 2b. Monster stamina pair (riseMonsterStaminaFromPair)
    // ------------------------------------------------------------------
    // The value behind the monster widget's stamina gauge — the real
    // "fatigue" meter (v0.8.4-r23 fatigue-semantics). The exhaust ailment
    // slot (the old "疲劳" card) is a different gauge entirely.
    check(sizeof(mhw::MHRStaminaStructure) == 40,
          "MHRStaminaStructure is 40 bytes");
    check(offsetof(mhw::MHRStaminaStructure, stamina) == 0x20,
          "MHRStaminaStructure.stamina is at +0x20");
    check(offsetof(mhw::MHRStaminaStructure, maxStamina) == 0x24,
          "MHRStaminaStructure.maxStamina is at +0x24");

    // Live pairs from the r23 probe session (fresh 1000/1000, dragged to 0,
    // and the 2000-pool monster).
    float stv = -1.0F, mxv = -1.0F;
    check(mhw::riseMonsterStaminaFromPair(1000.0F, 1000.0F, &stv, &mxv)
              && stv == 1000.0F && mxv == 1000.0F,
          "full stamina pair round-trips");
    check(mhw::riseMonsterStaminaFromPair(0.0F, 1000.0F, &stv, &mxv)
              && stv == 0.0F && mxv == 1000.0F,
          "zero stamina is real data (exhausted monster), not a failed read");
    check(mhw::riseMonsterStaminaFromPair(2000.0F, 2000.0F, &stv, &mxv)
              && stv == 2000.0F && mxv == 2000.0F,
          "the 2000-pool monster round-trips");

    // Unusable reads are rejected without touching the outputs.
    stv = -1.0F; mxv = -1.0F;
    check(!mhw::riseMonsterStaminaFromPair(std::nanf(""), 1000.0F, &stv, &mxv)
              && stv == -1.0F && mxv == -1.0F,
          "NaN stamina is rejected and leaves outputs untouched");
    check(!mhw::riseMonsterStaminaFromPair(700.0F, std::nanf(""), &stv, &mxv),
          "NaN max stamina is rejected");
    check(!mhw::riseMonsterStaminaFromPair(
              std::numeric_limits<float>::infinity(), 1000.0F, &stv, &mxv),
          "infinite stamina is rejected");
    check(!mhw::riseMonsterStaminaFromPair(-1.0F, 1000.0F, &stv, &mxv),
          "negative stamina is rejected");
    check(!mhw::riseMonsterStaminaFromPair(700.0F, 0.0F, &stv, &mxv),
          "zero max stamina is rejected (unread)");
    check(!mhw::riseMonsterStaminaFromPair(700.0F, -5.0F, &stv, &mxv),
          "negative max stamina is rejected");
    check(!mhw::riseMonsterStaminaFromPair(700.0F, 1.0e6F, &stv, &mxv),
          "an out-of-band max stamina (1e6) is rejected as a read artefact");

    // A slight overflow (two floats read a moment apart from a live
    // simulation) clamps to max instead of being discarded.
    check(mhw::riseMonsterStaminaFromPair(1050.0F, 1000.0F, &stv, &mxv)
              && stv == 1000.0F && mxv == 1000.0F,
          "a slight overflow clamps to max");

    // Unread default: 0/0 keeps the panel row hidden.
    check(mhw::MonsterSnapshot{}.maxStamina == 0.0F,
          "MonsterSnapshot.maxStamina defaults to 0 (row hidden until read)");

    // The crown thresholds the panel compares this ratio against must stay
    // available for the Rise ids the XML actually declares (33 of the 79
    // monsters carry <Crowns>; id 14 = Barioth is one of those without, which
    // is why the panel shows no crown icon for it regardless of size).
    const auto *crowns = mhw::crownThresholdsFor(mhw::GameId::Rise, 1);
    check(crowns != nullptr && (*crowns)[2] > 1.1F,
          "Rise id 1 keeps its <Crowns> metadata (gold threshold > 1.1)");
    check(mhw::crownThresholdsFor(mhw::GameId::Rise, 14) == nullptr,
          "Rise id 14 has no <Crowns> entry and must not inherit World data");

    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
