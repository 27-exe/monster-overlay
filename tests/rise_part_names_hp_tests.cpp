// SPDX-License-Identifier: Apache-2.0
// Offline regression coverage for Rise official part names and part HP display.

#include "rise/mhr_part_names.h"

#include <QCoreApplication>

#include <cmath>
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

void checkName(int monsterId, int partIndex, const char *expected, const char *message)
{
    const char *actual = mhw::risePartName(monsterId, partIndex);
    check(actual != nullptr && std::strcmp(actual, expected) == 0, message);
}

void checkPair(const mhw::PartHealthPair &actual, float current, float maximum,
               const char *message)
{
    check(std::fabs(actual.current - current) < 0.001F
              && std::fabs(actual.maximum - maximum) < 0.001F,
          message);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Exact entries mechanically joined from Rise MonsterData.xml and the
    // official zh-cn.xml Shared/Part localization table.
    checkName(14, 0, "头部", "id 14 Icefang head is 官方中文 头部");
    checkName(14, 2, "左前肢", "id 14 part 2 is 官方中文 左前肢");
    checkName(14, 6, "尾巴", "id 14 tail is 官方中文 尾巴");
    checkName(92, 0, "头部", "id 92 Seething Bazelgeuse head is 官方中文 头部");
    checkName(92, 1, "身体", "id 92 body is 官方中文 身体");
    checkName(92, 3, "左翼", "id 92 left wing is 官方中文 左翼");
    checkName(94, 0, "头部", "id 94 Lunagaron head is 官方中文 头部");
    checkName(94, 4, "后腿", "id 94 hind legs is 官方中文 后腿");
    checkName(94, 7, "背部", "id 94 back is 官方中文 背部");

    check(mhw::risePartName(14, 7) == nullptr,
          "known monster out-of-range part index is a table miss");
    check(mhw::risePartName(999, 0) == nullptr,
          "unknown monster is a table miss");
    check(mhw::risePartName(114, 6) == nullptr,
          "upstream PART_TO_BE_MAPPED placeholder is not exposed as a name");
    check(mhw::risePartDisplayName(114, 6) == QStringLiteral("部位 6"),
          "upstream PART_TO_BE_MAPPED falls back to 部位 N");
    check(mhw::risePartDisplayName(14, 7) == QStringLiteral("部位 7"),
          "known monster out-of-range part falls back to 部位 N");
    check(mhw::risePartDisplayName(999, 0) == QStringLiteral("部位 0"),
          "unknown monster falls back to 部位 N");

    check(mhw::compactPartHealth(34000.0F, 57000.0F) == QStringLiteral("34k/57k"),
          "whole-k part HP is compact");
    check(mhw::compactPartHealth(1500.0F, 9900.0F) == QStringLiteral("1.5k/9.9k"),
          "sub-10k part HP retains one decimal");
    check(mhw::compactPartHealth(999.0F, 1000.0F) == QStringLiteral("999/1k"),
          "sub-thousand current HP remains readable");
    check(mhw::compactPartHealth(NAN, 57000.0F) == QStringLiteral("--/57k"),
          "invalid current HP is explicit");
    check(mhw::compactPartHealth(1.0F, 0.0F) == QStringLiteral("--/--"),
          "invalid maximum HP suppresses the pair");

    mhw::PartSnapshot sever;
    sever.partType = mhw::PartType::Severable;
    sever.health = 34000.0F;
    sever.maxHealth = 57000.0F;
    sever.flinch = 11.0F;
    sever.maxFlinch = 22.0F;
    checkPair(mhw::partHealthForDisplay(sever), 34000.0F, 57000.0F,
              "severable display uses Sever/MaxSever values stored in health pair");

    mhw::PartSnapshot breaking;
    breaking.partType = mhw::PartType::Breakable;
    breaking.health = 1250.0F;
    breaking.maxHealth = 3500.0F;
    breaking.flinch = 77.0F;
    breaking.maxFlinch = 88.0F;
    checkPair(mhw::partHealthForDisplay(breaking), 1250.0F, 3500.0F,
              "breakable display uses Health/MaxHealth rather than flinch");

    mhw::PartSnapshot flinch;
    flinch.partType = mhw::PartType::Flinch;
    flinch.health = 1.0F;
    flinch.maxHealth = 2.0F;
    flinch.flinch = 12.0F;
    flinch.maxFlinch = 24.0F;
    checkPair(mhw::partHealthForDisplay(flinch), 12.0F, 24.0F,
              "generic display preserves flinch fallback for non-Rise callers");

    if (failures == 0)
        std::printf("rise-part-names-hp-tests: ALL PASSED\n");
    return failures == 0 ? 0 : 1;
}
