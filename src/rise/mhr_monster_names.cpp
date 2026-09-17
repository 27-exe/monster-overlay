// SPDX-License-Identifier: Apache-2.0
//
// AUTO-GENERATED — do not edit by hand.
// Regenerate with v0.8.4-r18/monster-identity/tools/extract_rise_monster_names.py
//
// Source: HunterPie official localization "zh-cn.xml"
//   https://cdn.hunterpie.com/localization/zh-cn.xml
//   sha256 2a4b1bb318fc21a55c0b5b978e2d33cb8c2ea74457b34fcac88b9236c19a06db
//   (byte-identical to HunterPie/localization@main/localization/zh-cn.xml and to
//    the hash published by https://api.hunterpie.com/v1/localization/checksum;
//    fetch transcript: .../v0.8.4-r18/zone-names/evidence/localization/FETCH.md)
//   extracted 2026-09-17 from the path
//   /Strings/Monsters/Rise/Monster[@Id] (zh-cn.xml lines 981-1140,
//   the <Rise> child of the top-level <Monsters> block).
//
// 79 entries covering Id 0..115; the id gaps are upstream —
// HunterPie's zh-cn.xml carries no <Monster> entry for e.g. 47..75 / 99..106,
// so a miss here is a real "no localized name", not an extraction artefact.

#include "rise/mhr_monster_names.h"

#include <array>
#include <cstddef>

namespace mhw {
namespace {

struct RiseMonsterName {
    int id;
    const char *name;   // UTF-8, Simplified Chinese, verbatim from zh-cn.xml
};

// Sorted by id: riseMonsterName() binary-searches it.
constexpr std::array<RiseMonsterName, 79> kRiseMonsterNames = {{
    { 0, "雌火龙" },
    { 1, "霸主·雌火龙" },
    { 2, "火龙" },
    { 3, "霸主·火龙" },
    { 4, "奇怪龙" },
    { 5, "岩龙" },
    { 6, "角龙" },
    { 7, "霸主·角龙" },
    { 8, "金狮子" },
    { 9, "钢龙" },
    { 10, "霞龙" },
    { 11, "炎王龙" },
    { 12, "轰龙" },
    { 13, "迅龙" },
    { 14, "冰牙龙" },
    { 15, "土砂龙" },
    { 16, "水兽" },
    { 17, "眠狗龙王" },
    { 18, "雷狼龙" },
    { 19, "霸主·雷狼龙" },
    { 20, "毒狗龙王" },
    { 21, "青熊兽" },
    { 22, "霸主·青熊兽" },
    { 23, "白兔兽" },
    { 24, "赤甲兽" },
    { 25, "泡狐龙" },
    { 26, "霸主·泡狐龙" },
    { 27, "神秘红光天彗龙" },
    { 28, "怨虎龙" },
    { 29, "天狗兽" },
    { 30, "伞鸟" },
    { 31, "河童蛙" },
    { 32, "人鱼龙" },
    { 33, "妃蜘蛛" },
    { 34, "泥翁龙" },
    { 35, "风神龙" },
    { 36, "雪鬼兽" },
    { 37, "镰鼬龙王" },
    { 38, "雷神龙" },
    { 39, "百龙渊源雷神龙" },
    { 40, "蛮颚龙" },
    { 41, "毒妖鸟" },
    { 42, "骚鸟" },
    { 43, "泥鱼龙" },
    { 44, "飞雷龙" },
    { 45, "爆鳞龙" },
    { 46, "机关蛙" },
    { 76, "金火龙" },
    { 77, "银火龙" },
    { 78, "大名盾蟹" },
    { 79, "将军镰蟹" },
    { 80, "激昂金狮子" },
    { 81, "月迅龙" },
    { 82, "黑蚀龙" },
    { 83, "天廻龙" },
    { 84, "千刃龙" },
    { 85, "电龙" },
    { 86, "焰狐龙" },
    { 87, "嗟怨震天怨虎龙" },
    { 88, "绯天狗兽" },
    { 89, "冰人鱼龙" },
    { 90, "炽妃蜘蛛" },
    { 91, "熔翁龙" },
    { 92, "红莲爆鳞龙" },
    { 93, "爵银龙" },
    { 94, "冰狼龙" },
    { 95, "刚缠兽" },
    { 96, "冥渊龙" },
    { 97, "棘龙" },
    { 98, "棘茶龙" },
    { 107, "怪异克服钢龙" },
    { 108, "怪异克服霞龙" },
    { 109, "怪异克服炎王龙" },
    { 110, "怪异克服天廻龙" },
    { 111, "怪异克服天彗龙" },
    { 112, "原初形态爵银龙" },
    { 113, "混沌黑蚀龙" },
    { 114, "冰呪龙" },
    { 115, "岚龙" },
}};

// The lookup below is a binary search, so a regeneration that ever emits an
// unsorted or duplicated id must fail the build instead of silently returning
// wrong names at runtime.
constexpr bool riseMonsterNamesStrictlyIncreasing()
{
    for (std::size_t i = 1; i < kRiseMonsterNames.size(); ++i) {
        if (kRiseMonsterNames[i - 1].id >= kRiseMonsterNames[i].id)
            return false;
    }
    return true;
}
static_assert(riseMonsterNamesStrictlyIncreasing(),
              "riseMonsterName() binary search requires strictly increasing ids");

} // namespace

const char *riseMonsterName(int id)
{
    std::size_t lo = 0;
    std::size_t hi = kRiseMonsterNames.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (kRiseMonsterNames[mid].id < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < kRiseMonsterNames.size() && kRiseMonsterNames[lo].id == id)
        return kRiseMonsterNames[lo].name;
    return nullptr;
}

} // namespace mhw
