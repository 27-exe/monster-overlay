// SPDX-License-Identifier: Apache-2.0
// Regression coverage for the official Rise stage-name table
// (HunterPie localization: Strings/Stages/Rise/Stage, zh-CN, v0.8.4-r18).
//
// StageId = HuntingId + 200; the official table has holes at 200/206/208/216
// (those keep the "未知狩猎区" placeholder). hid 1 and hid 4 are locked by
// in-game user verification; the others pin the official table against the
// pre-r18 community-navigation ordering that shifted every label from hid 4.
// Rampage-mode labelling (百龙夜行 vs the map name 翡叶要塞 for hid 7) is a
// quest-type display decision, not part of this map-name table.

#include "world/world_types.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition) {
        std::printf("PASS: %s\n", message);
    } else {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

} // namespace

int main()
{
    // Official table entries (StageId 201..215).
    check(std::strcmp(mhw::zoneName(mhw::Zone::RiseLoc1), "废神社") == 0,
          "hid 1 (StageId 201) is 废神社 [in-game verified]");
    check(std::strcmp(mhw::zoneName(mhw::Zone::RiseLoc4), "冰封群岛") == 0,
          "hid 4 (StageId 204) is 冰封群岛 [in-game verified]");
    check(std::strcmp(mhw::zoneName(mhw::Zone::RiseLoc5), "熔岩洞") == 0,
          "hid 5 (StageId 205) is 熔岩洞");
    check(std::strcmp(mhw::zoneName(mhw::Zone::RiseLoc7), "翡叶要塞") == 0,
          "hid 7 (StageId 207, Red Stronghold) is 翡叶要塞");
    check(std::strcmp(mhw::zoneName(mhw::Zone::RiseLoc12), "密林") == 0,
          "hid 12 (StageId 212) is 密林");
    // Official-table holes keep the placeholder.
    check(std::strcmp(mhw::zoneName(mhw::Zone::RiseLoc0), "未知狩猎区") == 0,
          "hid 0 (StageId 200, no upstream entry) keeps the placeholder");

    // Guards against the pre-r18 shifted table regressing back in.
    check(std::strcmp(mhw::zoneName(mhw::Zone::RiseLoc1), "冰封群岛") != 0,
          "RiseLoc1 is no longer labelled Frost Islands (old shifted table)");
    check(std::strcmp(mhw::zoneName(mhw::Zone::RiseLoc7), "珊瑚宫殿") != 0,
          "RiseLoc7 is no longer labelled Coral Palace");

    if (failures == 0) {
        std::printf("rise-zone-mapping-tests: ALL PASSED\n");
    }
    return failures == 0 ? 0 : 1;
}