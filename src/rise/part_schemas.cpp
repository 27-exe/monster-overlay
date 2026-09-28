#include "monster/monster_types.h"
#include <QHash>
#include <QSet>

namespace mhw {

// v0.7.5: Rise ailment SLOT ids (positional, 0-16) from HunterPie
// Game/Rise/Data/MonsterData.xml <Ailments>. NOTE the ordering differs
// from World's kAilmentNames (World 1=poison; Rise 4=poison).
extern const QHash<int, QString> kRiseAilmentNames = {
    { 0, QStringLiteral("麻痹")},
    { 1, QStringLiteral("睡眠")},
    { 2, QStringLiteral("眩晕")},
    { 3, QStringLiteral("闪光")},
    { 4, QStringLiteral("毒")},
    { 5, QStringLiteral("爆破")},
    { 6, QStringLiteral("疲劳")},
    { 7, QStringLiteral("乘骑")},
    { 8, QStringLiteral("水异常")},
    { 9, QStringLiteral("火异常")},
    {10, QStringLiteral("冰异常")},
    {11, QStringLiteral("雷异常")},
    {12, QStringLiteral("落穴陷阱")},
    {13, QStringLiteral("麻痹陷阱")},
    {14, QStringLiteral("捕获")},
    {15, QStringLiteral("异臭")},
    {16, QStringLiteral("钢龙毒")},
};

// Generated from HunterPie Game/Rise/Data/MonsterData.xml <Crowns>.
// This table deliberately includes only explicit Rise <Crowns> entries; no
// World default is substituted for a missing Rise element or attribute.
extern const QHash<int, std::array<float, 3>> kRiseCrownThresholds = {
    {1, {0.96F, 1.1F, 1.14F}},
    {3, {0.97F, 1.17F, 0.0F}},
    {4, {0.0F, 1.09F, 1.14F}},
    {7, {0.97F, 1.17F, 0.0F}},
    {8, {0.97F, 1.17F, 0.0F}},
    {9, {0.97F, 1.17F, 0.0F}},
    {10, {0.97F, 1.17F, 0.0F}},
    {11, {0.97F, 1.17F, 0.0F}},
    {19, {0.97F, 1.17F, 0.0F}},
    {22, {0.96F, 1.03F, 1.05F}},
    {26, {0.96F, 1.1F, 1.14F}},
    {27, {0.0F, 1.11F, 1.17F}},
    {29, {0.0F, 1.09F, 1.14F}},
    {30, {0.765F, 0.9775F, 1.0455F}},
    {76, {0.97F, 1.17F, 0.0F}},
    {77, {0.97F, 1.17F, 0.0F}},
    {80, {0.97F, 1.17F, 0.0F}},
    {81, {0.97F, 1.17F, 0.0F}},
    {82, {0.0F, 1.11F, 1.17F}},
    {83, {0.0F, 1.11F, 1.17F}},
    {86, {0.97F, 1.17F, 0.0F}},
    {87, {0.0F, 1.11F, 1.17F}},
    {88, {0.0F, 1.09F, 1.14F}},
    {93, {0.0F, 1.11F, 1.17F}},
    {95, {0.0F, 1.11F, 1.17F}},
    {107, {0.97F, 1.17F, 0.0F}},
    {108, {0.97F, 1.17F, 0.0F}},
    {109, {0.97F, 1.17F, 0.0F}},
    {110, {0.0F, 1.11F, 1.17F}},
    {111, {0.0F, 1.11F, 1.17F}},
    {112, {0.0F, 1.11F, 1.17F}},
    {113, {0.0F, 1.11F, 1.17F}},
    {114, {0.97F, 1.15F, 0.0F}},
};

// Generated from HunterPie Game/Rise/Data/MonsterData.xml
// <Monster IsNotCapturable="true">. Rise provides no Capture=N percentage.
extern const QSet<int> kRiseNotCapturableMonsterIds = {
    1,
    3,
    7,
    9,
    10,
    11,
    19,
    22,
    26,
    27,
    35,
    38,
    39,
    46,
    83,
    93,
    96,
    111,
    112,
    114,
    115,

};

} // namespace mhw
