// SPDX-License-Identifier: Apache-2.0

#include "monster/monster_types.h"

#include <QCoreApplication>

#include <array>
#include <iostream>

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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // HunterPie Game/Rise/Data/MonsterData.xml, Monster Id=1:
    // <Crowns Mini="0.96" Silver="1.1" Gold="1.14" />.
    const auto *riseOne = mhw::crownThresholdsFor(mhw::GameId::Rise, 1);
    check(riseOne != nullptr, "Rise Id=1 has explicit crown data");
    check(riseOne && *riseOne == std::array<float, 3>{0.96F, 1.10F, 1.14F},
          "Rise Id=1 crowns match MonsterData.xml");

    const auto *worldOne = mhw::crownThresholdsFor(mhw::GameId::World, 1);
    check(worldOne && *worldOne == std::array<float, 3>{0.90F, 1.15F, 1.23F},
          "World Id=1 crown data remains independently selected");
    check(riseOne && worldOne && *riseOne != *worldOne,
          "Rise crown lookup does not reuse World data for a shared Id");

    // Rise Id=3 has no Gold attribute. Missing attributes must remain absent
    // rather than receiving World defaults.
    const auto *riseThree = mhw::crownThresholdsFor(mhw::GameId::Rise, 3);
    check(riseThree && *riseThree == std::array<float, 3>{0.97F, 1.17F, 0.0F},
          "partial Rise crown data preserves missing Gold as absent");
    check(mhw::crownThresholdsFor(mhw::GameId::Rise, 0) == nullptr,
          "Rise Id=0 without Crowns does not fall back to World crowns");

    // Rise XML has no Capture attribute. Its only capture-related field is
    // IsNotCapturable=true, so a capturable Rise monster has no static
    // threshold while Id=1 is explicitly non-capturable.
    check(mhw::captureThresholdFor(mhw::GameId::Rise, 1) == 0,
          "Rise IsNotCapturable=true exports capture-disabled metadata");
    check(!mhw::captureThresholdFor(mhw::GameId::Rise, 0).has_value(),
          "Rise missing Capture data does not fall back to World threshold");
    check(mhw::captureThresholdFor(mhw::GameId::World, 0) == 25,
          "World capture threshold remains independently selected");

    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
