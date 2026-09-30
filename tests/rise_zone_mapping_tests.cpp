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
//
// These ids and names used to be WorldZone enumerators answered by
// world_types.cpp. They are Rise's vocabulary: rise/rise_zones.h owns them,
// and this test is where a move or a renumbering would have to show itself.

#include "rise/rise_zones.h"
#include "core/string_table.h"

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
    check(std::strcmp(mhw::riseZoneName(mhw::RiseZone::RiseLoc1), "废神社") == 0,
          "hid 1 (StageId 201) is 废神社 [in-game verified]");
    check(std::strcmp(mhw::riseZoneName(mhw::RiseZone::RiseLoc4), "冰封群岛") == 0,
          "hid 4 (StageId 204) is 冰封群岛 [in-game verified]");
    check(std::strcmp(mhw::riseZoneName(mhw::RiseZone::RiseLoc5), "熔岩洞") == 0,
          "hid 5 (StageId 205) is 熔岩洞");
    check(std::strcmp(mhw::riseZoneName(mhw::RiseZone::RiseLoc7), "翡叶要塞") == 0,
          "hid 7 (StageId 207, Red Stronghold) is 翡叶要塞");
    check(std::strcmp(mhw::riseZoneName(mhw::RiseZone::RiseLoc12), "密林") == 0,
          "hid 12 (StageId 212) is 密林");
    // Official-table holes keep the placeholder.
    check(std::strcmp(mhw::riseZoneName(mhw::RiseZone::RiseLoc0), "未知狩猎区") == 0,
          "hid 0 (StageId 200, no upstream entry) keeps the placeholder");

    // Guards against the pre-r18 shifted table regressing back in.
    check(std::strcmp(mhw::riseZoneName(mhw::RiseZone::RiseLoc1), "冰封群岛") != 0,
          "RiseLoc1 is no longer labelled Frost Islands (old shifted table)");
    check(std::strcmp(mhw::riseZoneName(mhw::RiseZone::RiseLoc7), "珊瑚宫殿") != 0,
          "RiseLoc7 is no longer labelled Coral Palace");

    // --- the ids the reader emits are the ones the table names -------------
    // computeZoneId() (src/rise/mhr_reader.cpp) rebases HuntingId into
    // 600..616 and villageId into 700..799; the enum values must stay equal
    // to those raw ids, or snapshot.zone.id would silently answer from the
    // wrong row. This is the invariant the split could have broken.
    check(static_cast<int>(mhw::RiseZone::RiseLoc0) == 600
              && static_cast<int>(mhw::RiseZone::RiseLoc1) == 601
              && static_cast<int>(mhw::RiseZone::RiseLoc16) == 616,
          "RiseLocN == HuntingId N + 600 (the reader's rebased id)");
    check(static_cast<int>(mhw::RiseZone::RiseTrainingRoom) == 705,
          "RiseTrainingRoom == villageId 5 + 700");

    // --- v0.9 i18n (WS-A): the English column of the same table -------------
    // Source: HunterPie en-us.xml Strings/Stages/Rise/Stage (same StageId
    // keys as the zh table above) and Strings/Stages/World for the World
    // ids. riseZoneName() keeps returning the frozen zh literals; only
    // riseZoneNameLocalized() follows the active locale.
    mhw::StringTable &strings = mhw::StringTable::instance();
    check(strings.load(QStringLiteral("en-US")), "en-US locale loads");
    check(strings.isEnglish(), "isEnglish() is true for en-US");
    check(std::strcmp(mhw::riseZoneNameLocalized(mhw::RiseZone::RiseLoc1), "Shrine Ruins") == 0,
          "hid 1 reads as Shrine Ruins in English");
    check(std::strcmp(mhw::riseZoneNameLocalized(mhw::RiseZone::RiseLoc7), "Red Stronghold") == 0,
          "hid 7 reads as Red Stronghold in English");
    check(std::strcmp(mhw::riseZoneNameLocalized(mhw::RiseZone::RiseLoc14), "Forlorn Arena") == 0,
          "hid 14 reads as Forlorn Arena in English");
    check(std::strcmp(mhw::riseZoneNameLocalized(mhw::RiseZone::RiseLoc0), "Unknown Hunting Zone") == 0,
          "hid 0 keeps the explicit English placeholder (no upstream entry)");
    check(std::strcmp(mhw::riseZoneNameLocalized(mhw::RiseZone::RiseTrainingRoom), "Training Area") == 0,
          "the rebased training-room village reads as Training Area");
    check(std::strcmp(mhw::riseZoneNameLocalized(static_cast<mhw::RiseZone>(703)), "Gathering Hub") == 0,
          "Rise village id 3 reads as Gathering Hub");
    // The locale-independent accessor is unaffected by the switch.
    check(std::strcmp(mhw::riseZoneName(mhw::RiseZone::RiseLoc1), "废神社") == 0,
          "riseZoneName() still returns the zh string while en-US is loaded");

    check(strings.load(QStringLiteral("zh-CN")), "zh-CN locale reloads");
    check(std::strcmp(mhw::riseZoneNameLocalized(mhw::RiseZone::RiseLoc1), "废神社") == 0,
          "after reloading zh-CN the label is 废神社 again");
    check(std::strcmp(mhw::riseZoneNameLocalized(mhw::RiseZone::RiseLoc0), "未知狩猎区") == 0,
          "after reloading zh-CN the placeholder is 未知狩猎区 again");
    check(std::strcmp(mhw::riseZoneNameEn(mhw::RiseZone::RiseLoc1), "Shrine Ruins") == 0,
          "riseZoneNameEn() stays the English accessor regardless of locale");

    // --- which ids are named, and which are hunting zones -------------------
    // hasRiseZoneName() is what the UI asks instead of comparing a label:
    // the 700..799 band is named for 0..7 and 11 only, so village ids 8/9/10
    // and anything outside both bands are unnamed — and a village must read
    // as a peace zone so the overlay does not show a damage feed in town.
    check(mhw::hasRiseZoneName(mhw::RiseZone::RiseTrainingRoom),
          "the training-room village has a name");
    check(!mhw::hasRiseZoneName(static_cast<mhw::RiseZone>(709)),
          "village id 9 has no table entry");
    check(mhw::isRiseHuntingZone(mhw::RiseZone::RiseLoc7),
          "RiseLoc7 is a hunting zone");
    check(mhw::isRiseHuntingZone(mhw::RiseZone::RiseTrainingRoom),
          "the training room counts as a hunting zone (HunterPie does)");
    check(!mhw::isRiseHuntingZone(static_cast<mhw::RiseZone>(703)),
          "the gathering hub is a peace zone");

    if (failures == 0) {
        std::printf("rise-zone-mapping-tests: ALL PASSED\n");
    }
    return failures == 0 ? 0 : 1;
}
