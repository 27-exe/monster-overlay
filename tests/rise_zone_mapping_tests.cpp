// SPDX-License-Identifier: Apache-2.0
// Regression coverage for the HunterPie-confirmed Rise StageId 207 state.

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
    const char *name = mhw::zoneName(mhw::Zone::RiseLoc7);

    check(std::strcmp(name, "珊瑚宫殿") != 0,
          "RiseLoc7 is no longer labelled Coral Palace");
    check(std::strcmp(name, "百龙夜行") == 0,
          "RiseLoc7 is labelled the HunterPie-confirmed Rampage state");

    if (failures == 0) {
        std::printf("rise-zone-mapping-tests: ALL PASSED\n");
    }
    return failures == 0 ? 0 : 1;
}
