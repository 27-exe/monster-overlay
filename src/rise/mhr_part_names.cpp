// SPDX-License-Identifier: Apache-2.0
// AUTO-GENERATED - do not edit by hand.
// Regenerate with scripts/generate_rise_part_names.py:
//   python3 scripts/generate_rise_part_names.py --monster-data <MonsterData.xml>
//       --en-xml <en-us.xml> --zh-xml <zh-cn.xml> --cpp-out src/rise/mhr_part_names.cpp
//
// Sources (all three sha256-verified by the generator):
//   MonsterData.xml  HunterPie/Game/Rise/Data/MonsterData.xml
//     sha256 f5cd32a5ab481187dc356e8bd75e8df7618ecd95314db9cec4107567f17751b8
//     (/Monsters/Monster/Parts/Part — the part list every Rise monster
//      carries; partIndex is the part's Id attribute)
//   zh column        https://cdn.hunterpie.com/localization/zh-cn.xml
//     sha256 2a4b1bb318fc21a55c0b5b978e2d33cb8c2ea74457b34fcac88b9236c19a06db
//     /Strings/Monsters/Shared/Part[@Id] — the official Simplified Chinese part
//     names HunterPie's monster widget resolves.
//   en column        https://cdn.hunterpie.com/localization/en-us.xml
//     sha256 661d58b54bd1214ae0e9eff92fb4167b8aab54df0452eea238cb9574300030ea
//     /Strings/Monsters/Shared/Part[@Id] — the official English part names.
//   The two locale files are byte-identical to
//   HunterPie/localization@main/localization/<lang>.xml and to the hashes
//   published by https://api.hunterpie.com/v1/localization/checksum.
//   Extracted 2026-09-18. The generator asserts both locales resolve every
//   PART_* key reached from MonsterData.xml (598 rows, 79 monsters).
//
// PART_TO_BE_MAPPED is "Unknown" in BOTH locales (upstream placeholder);
// risePartName() keeps suppressing it so the overlay shows its safe
// "部位 N" / "Part N" fallback instead of the literal "Unknown".
//
// Locale selection (v0.9 i18n, WS-B): risePartName() / risePartDisplayName()
// return the en column while mhw::StringTable::instance().isEnglish() is true
// and the zh column otherwise; an empty locale (before any load()) reads as
// Chinese, so the pre-i18n behaviour is the default. risePartNameEn() exposes
// the en column explicitly (tests / tooling) and is locale-independent.

#include "rise/mhr_part_names.h"

#include "core/string_table.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

namespace mhw {
namespace {

struct RisePartName {
    int monsterId;
    int partIndex;
    const char *name;    // UTF-8, Simplified Chinese, verbatim from zh-cn.xml
    const char *nameEn;  // UTF-8, English, verbatim from en-us.xml
};

constexpr std::array<RisePartName, 598> kRisePartNames = {{
    { 0, 0, "头部", "Head" }, // PART_HEAD
    { 0, 1, "躯干", "Torso" }, // PART_TORSO
    { 0, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 0, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 0, 4, "左腿", "Left Leg" }, // PART_L_LEG
    { 0, 5, "右腿", "Right Leg" }, // PART_R_LEG
    { 0, 6, "尾巴", "Tail" }, // PART_TAIL
    { 1, 0, "头部", "Head" }, // PART_HEAD
    { 1, 1, "躯干", "Torso" }, // PART_TORSO
    { 1, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 1, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 1, 4, "左腿", "Left Leg" }, // PART_L_LEG
    { 1, 5, "右腿", "Right Leg" }, // PART_R_LEG
    { 1, 6, "尾巴", "Tail" }, // PART_TAIL
    { 2, 0, "躯干", "Torso" }, // PART_TORSO
    { 2, 1, "左翼", "Left Wing" }, // PART_L_WING
    { 2, 2, "右翼", "Right Wing" }, // PART_R_WING
    { 2, 3, "左腿", "Left Leg" }, // PART_L_LEG
    { 2, 4, "右腿", "Right Leg" }, // PART_R_LEG
    { 2, 5, "颈部", "Neck" }, // PART_NECK
    { 2, 6, "头部", "Head" }, // PART_HEAD
    { 2, 7, "尾巴", "Tail" }, // PART_TAIL
    { 3, 0, "躯干", "Torso" }, // PART_TORSO
    { 3, 1, "左翼", "Left Wing" }, // PART_L_WING
    { 3, 2, "右翼", "Right Wing" }, // PART_R_WING
    { 3, 3, "左腿", "Left Leg" }, // PART_L_LEG
    { 3, 4, "右腿", "Right Leg" }, // PART_R_LEG
    { 3, 5, "颈部", "Neck" }, // PART_NECK
    { 3, 6, "头部", "Head" }, // PART_HEAD
    { 3, 7, "尾巴", "Tail" }, // PART_TAIL
    { 4, 0, "头部", "Head" }, // PART_HEAD
    { 4, 1, "颈部", "Neck" }, // PART_NECK
    { 4, 2, "躯干", "Torso" }, // PART_TORSO
    { 4, 3, "左腿", "Left Leg" }, // PART_L_LEG
    { 4, 4, "右腿", "Right Leg" }, // PART_R_LEG
    { 4, 5, "左翼", "Left Wing" }, // PART_L_WING
    { 4, 6, "右翼", "Right Wing" }, // PART_R_WING
    { 4, 7, "尾巴", "Tail" }, // PART_TAIL
    { 5, 0, "背部", "Back" }, // PART_BACK
    { 5, 1, "左翼", "Left Wing" }, // PART_L_WING
    { 5, 2, "右翼", "Right Wing" }, // PART_R_WING
    { 5, 3, "左腿", "Left Leg" }, // PART_L_LEG
    { 5, 4, "右腿", "Right Leg" }, // PART_R_LEG
    { 5, 5, "头部", "Head" }, // PART_HEAD
    { 5, 6, "胸部", "Chest" }, // PART_CHEST
    { 5, 7, "尾巴", "Tail" }, // PART_TAIL
    { 6, 0, "头部", "Head" }, // PART_HEAD
    { 6, 1, "躯干", "Torso" }, // PART_TORSO
    { 6, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 6, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 6, 4, "左腿", "Left Leg" }, // PART_L_LEG
    { 6, 5, "右腿", "Right Leg" }, // PART_R_LEG
    { 6, 6, "尾巴", "Tail" }, // PART_TAIL
    { 7, 0, "头部", "Head" }, // PART_HEAD
    { 7, 1, "躯干", "Torso" }, // PART_TORSO
    { 7, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 7, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 7, 4, "左腿", "Left Leg" }, // PART_L_LEG
    { 7, 5, "右腿", "Right Leg" }, // PART_R_LEG
    { 7, 6, "尾巴", "Tail" }, // PART_TAIL
    { 8, 0, "头部", "Head" }, // PART_HEAD
    { 8, 1, "躯干", "Torso" }, // PART_TORSO
    { 8, 2, "左臂", "Left Arm" }, // PART_L_ARM
    { 8, 3, "右臂", "Right Arm" }, // PART_R_ARM
    { 8, 4, "左腿", "Left Leg" }, // PART_L_LEG
    { 8, 5, "右腿", "Right Leg" }, // PART_R_LEG
    { 8, 6, "尾巴", "Tail" }, // PART_TAIL
    { 9, 0, "头部", "Head" }, // PART_HEAD
    { 9, 1, "躯干", "Torso" }, // PART_TORSO
    { 9, 2, "左腿", "Left Leg" }, // PART_L_LEG
    { 9, 3, "右腿", "Right Leg" }, // PART_R_LEG
    { 9, 4, "翼", "Wings" }, // PART_WINGS
    { 9, 5, "尾巴", "Tail" }, // PART_TAIL
    { 10, 0, "头部", "Head" }, // PART_HEAD
    { 10, 1, "胸部", "Chest" }, // PART_CHEST
    { 10, 2, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 10, 3, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 10, 4, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 10, 5, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 10, 6, "尾巴", "Tail" }, // PART_TAIL
    { 10, 7, "翼", "Wings" }, // PART_WINGS
    { 11, 0, "头部", "Head" }, // PART_HEAD
    { 11, 1, "胸部", "Chest" }, // PART_CHEST
    { 11, 2, "前腿", "Forelegs" }, // PART_FORELEGS
    { 11, 3, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 11, 4, "翼", "Wings" }, // PART_WINGS
    { 11, 5, "尾巴", "Tail" }, // PART_TAIL
    { 12, 0, "头部", "Head" }, // PART_HEAD
    { 12, 1, "躯干", "Torso" }, // PART_TORSO
    { 12, 2, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 12, 3, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 12, 4, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 12, 5, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 12, 6, "尾巴", "Tail" }, // PART_TAIL
    { 13, 0, "头部", "Head" }, // PART_HEAD
    { 13, 1, "躯干", "Torso" }, // PART_TORSO
    { 13, 2, "左刃翼", "Left Cutwing" }, // PART_L_CUTWING
    { 13, 3, "尾巴", "Tail" }, // PART_TAIL
    { 13, 4, "前腿", "Forelegs" }, // PART_FORELEGS
    { 13, 5, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 13, 6, "右刃翼", "Right Cutwing" }, // PART_R_CUTWING
    { 13, 7, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 14, 0, "头部", "Head" }, // PART_HEAD
    { 14, 1, "躯干", "Torso" }, // PART_TORSO
    { 14, 2, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 14, 3, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 14, 4, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 14, 5, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 14, 6, "尾巴", "Tail" }, // PART_TAIL
    { 15, 0, "头部", "Head" }, // PART_HEAD
    { 15, 1, "躯干", "Torso" }, // PART_TORSO
    { 15, 2, "手臂", "Arms" }, // PART_ARMS
    { 15, 3, "左腿", "Left Leg" }, // PART_L_LEG
    { 15, 4, "右腿", "Right Leg" }, // PART_R_LEG
    { 15, 5, "尾巴", "Tail" }, // PART_TAIL
    { 15, 6, "头部(泥土)", "Head (Mud)" }, // PART_HEAD_MUD
    { 15, 7, "躯干(泥土)", "Torso (Mud)" }, // PART_TORSO_MUD
    { 15, 8, "手臂(泥土)", "Arms (Mud)" }, // PART_ARMS_MUD
    { 15, 9, "左腿(泥土)", "Left Leg (Mud)" }, // PART_L_LEG_MUD
    { 15, 10, "右腿(泥土)", "Right Leg (Mud)" }, // PART_R_LEG_MUD
    { 15, 11, "尾巴(泥土)", "Tail (Mud)" }, // PART_TAIL_MUD
    { 16, 0, "头部", "Head" }, // PART_HEAD
    { 16, 1, "海绵质", "Sponge" }, // PART_SPONGE
    { 16, 2, "躯干", "Torso" }, // PART_TORSO
    { 16, 3, "左腿", "Left Legs" }, // PART_L_LEGS
    { 16, 4, "右腿", "Right Legs" }, // PART_R_LEGS
    { 16, 5, "尾巴", "Tail" }, // PART_TAIL
    { 17, 0, "头部", "Head" }, // PART_HEAD
    { 17, 1, "身体", "Body" }, // PART_BODY
    { 17, 2, "尾巴", "Tail" }, // PART_TAIL
    { 18, 0, "头部", "Head" }, // PART_HEAD
    { 18, 1, "躯干", "Torso" }, // PART_TORSO
    { 18, 2, "背部", "Back" }, // PART_BACK
    { 18, 3, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 18, 4, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 18, 5, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 18, 6, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 18, 7, "尾巴", "Tail" }, // PART_TAIL
    { 19, 0, "头部", "Head" }, // PART_HEAD
    { 19, 1, "躯干", "Torso" }, // PART_TORSO
    { 19, 2, "背部", "Back" }, // PART_BACK
    { 19, 3, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 19, 4, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 19, 5, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 19, 6, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 19, 7, "尾巴", "Tail" }, // PART_TAIL
    { 20, 0, "头部", "Head" }, // PART_HEAD
    { 20, 1, "身体", "Body" }, // PART_BODY
    { 20, 2, "尾巴", "Tail" }, // PART_TAIL
    { 21, 0, "头部", "Head" }, // PART_HEAD
    { 21, 1, "上半身", "Upper Body" }, // PART_UPPER_BODY
    { 21, 2, "前腿", "Forelegs" }, // PART_FORELEGS
    { 21, 3, "臀部", "Rear" }, // PART_REAR
    { 21, 4, "下半身", "Lower Body" }, // PART_LOWER_BODY
    { 22, 0, "头部", "Head" }, // PART_HEAD
    { 22, 1, "上半身", "Upper Body" }, // PART_UPPER_BODY
    { 22, 2, "前腿", "Forelegs" }, // PART_FORELEGS
    { 22, 3, "臀部", "Rear" }, // PART_REAR
    { 22, 4, "下半身", "Lower Body" }, // PART_LOWER_BODY
    { 23, 0, "头部", "Head" }, // PART_HEAD
    { 23, 1, "上半身", "Upper Body" }, // PART_UPPER_BODY
    { 23, 2, "前腿", "Forelegs" }, // PART_FORELEGS
    { 23, 3, "臀部", "Rear" }, // PART_REAR
    { 23, 4, "下半身", "Lower Body" }, // PART_LOWER_BODY
    { 24, 0, "上背部", "Upper Back" }, // PART_UPPER_BACK
    { 24, 1, "头部", "Head" }, // PART_HEAD
    { 24, 2, "前腿", "Forelegs" }, // PART_FORELEGS
    { 24, 3, "下背部", "Lower Back" }, // PART_LOWER_BACK
    { 24, 4, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 24, 5, "滚动", "Rolling" }, // PART_ROLLING
    { 25, 0, "头部", "Head" }, // PART_HEAD
    { 25, 1, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 25, 2, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 25, 3, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 25, 4, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 25, 5, "躯干", "Torso" }, // PART_TORSO
    { 25, 6, "尾巴", "Tail" }, // PART_TAIL
    { 25, 7, "背鳍", "Dorsal Fin" }, // PART_DORSAL_FIN
    { 26, 0, "头部", "Head" }, // PART_HEAD
    { 26, 1, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 26, 2, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 26, 3, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 26, 4, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 26, 5, "躯干", "Torso" }, // PART_TORSO
    { 26, 6, "尾巴", "Tail" }, // PART_TAIL
    { 26, 7, "背鳍", "Dorsal Fin" }, // PART_DORSAL_FIN
    { 27, 0, "头部", "Head" }, // PART_HEAD
    { 27, 1, "躯干", "Torso" }, // PART_TORSO
    { 27, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 27, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 27, 4, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 27, 5, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 27, 6, "尾巴", "Tail" }, // PART_TAIL
    { 27, 7, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 27, 8, "胸部", "Chest" }, // PART_CHEST
    { 28, 0, "头部", "Head" }, // PART_HEAD
    { 28, 1, "躯干", "Torso" }, // PART_TORSO
    { 28, 2, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 28, 3, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 28, 4, "背部", "Back" }, // PART_BACK
    { 28, 5, "尾巴", "Tail" }, // PART_TAIL
    { 28, 6, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 29, 0, "头部", "Head" }, // PART_HEAD
    { 29, 1, "躯干", "Torso" }, // PART_TORSO
    { 29, 2, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 29, 3, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 29, 4, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 29, 5, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 29, 6, "尾巴", "Tail" }, // PART_TAIL
    { 30, 0, "头部", "Head" }, // PART_HEAD
    { 30, 1, "颈部", "Neck" }, // PART_NECK
    { 30, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 30, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 30, 4, "躯干", "Torso" }, // PART_TORSO
    { 30, 5, "尾巴", "Tail" }, // PART_TAIL
    { 30, 6, "腿", "Legs" }, // PART_LEGS
    { 31, 0, "头部", "Head" }, // PART_HEAD
    { 31, 1, "右臂", "Right Arm" }, // PART_R_ARM
    { 31, 2, "左臂", "Left Arm" }, // PART_L_ARM
    { 31, 3, "右腿", "Right Leg" }, // PART_R_LEG
    { 31, 4, "左腿", "Left Leg" }, // PART_L_LEG
    { 31, 5, "壳", "Shell" }, // PART_SHELL
    { 31, 6, "胸部", "Chest" }, // PART_CHEST
    { 31, 7, "尾巴", "Tail" }, // PART_TAIL
    { 32, 0, "躯干", "Torso" }, // PART_TORSO
    { 32, 1, "头部", "Head" }, // PART_HEAD
    { 32, 2, "???", "???" }, // PART_UNKNOWN
    { 32, 3, "左臂", "Left Arm" }, // PART_L_ARM
    { 32, 4, "右臂", "Right Arm" }, // PART_R_ARM
    { 32, 5, "左腿", "Left Leg" }, // PART_L_LEG
    { 32, 6, "右腿", "Right Leg" }, // PART_R_LEG
    { 32, 7, "尾巴", "Tail" }, // PART_TAIL
    { 33, 0, "头部", "Head" }, // PART_HEAD
    { 33, 1, "爪子", "Claw" }, // PART_CLAW
    { 33, 2, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 33, 3, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 33, 4, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 33, 5, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 33, 6, "腹部", "Abdomen" }, // PART_ABDOMEN
    { 33, 7, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 33, 8, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 33, 9, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 33, 10, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 33, 11, "腹部", "Abdomen" }, // PART_ABDOMEN
    { 33, 12, "胸部", "Chest" }, // PART_CHEST
    { 34, 0, "头部", "Head" }, // PART_HEAD
    { 34, 1, "躯干", "Torso" }, // PART_TORSO
    { 34, 2, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 34, 3, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 34, 4, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 34, 5, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 34, 6, "尾巴", "Tail" }, // PART_TAIL
    { 34, 7, "尾巴尖", "Tail Tip" }, // PART_TAIL_TIP
    { 34, 8, "泥土球", "Mud Ball" }, // PART_MUD_BALL
    { 35, 0, "头部", "Head" }, // PART_HEAD
    { 35, 1, "右臂", "Right Arm" }, // PART_R_ARM
    { 35, 2, "左臂", "Left Arm" }, // PART_L_ARM
    { 35, 3, "下躯干", "Lower Torso" }, // PART_LOWER_TORSO
    { 35, 4, "背部", "Back" }, // PART_BACK
    { 35, 5, "尾巴", "Tail" }, // PART_TAIL
    { 35, 6, "尾部风袋", "Tail Windsac" }, // PART_TAIL_WINDSAC
    { 35, 7, "胸部风袋", "Chest Windsac" }, // PART_CHEST_WINDSAC
    { 35, 8, "背部风袋", "Back Windsac" }, // PART_BACK_WINDSAC
    { 36, 0, "头部", "Head" }, // PART_HEAD
    { 36, 1, "背部", "Back" }, // PART_BACK
    { 36, 2, "左臂", "Left Arm" }, // PART_L_ARM
    { 36, 3, "左臂(冰)", "Left Arm (Ice)" }, // PART_L_ARM_ICE
    { 36, 4, "右臂", "Right Arm" }, // PART_R_ARM
    { 36, 5, "右臂(冰)", "Right Arm (Ice)" }, // PART_R_ARM_ICE
    { 36, 6, "左腿", "Left Leg" }, // PART_L_LEG
    { 36, 7, "右腿", "Right Leg" }, // PART_R_LEG
    { 36, 8, "躯干", "Torso" }, // PART_TORSO
    { 37, 0, "头部", "Head" }, // PART_HEAD
    { 37, 1, "身体", "Body" }, // PART_BODY
    { 37, 2, "手臂", "Arms" }, // PART_ARMS
    { 37, 3, "尾巴", "Tail" }, // PART_TAIL
    { 38, 0, "头部", "Head" }, // PART_HEAD
    { 38, 1, "后腿鱼鳍", "Back Leg Fins" }, // PART_B_LEG_FINS
    { 38, 2, "右臂", "Right Arm" }, // PART_R_ARM
    { 38, 3, "左臂", "Left Arm" }, // PART_L_ARM
    { 38, 4, "尾巴", "Tail" }, // PART_TAIL
    { 38, 5, "躯干", "Torso" }, // PART_TORSO
    { 38, 6, "背部", "Back" }, // PART_BACK
    { 38, 7, "雷电球", "Thunderballs" }, // PART_THUNDERBALLS
    { 38, 8, "胸部", "Chest" }, // PART_CHEST
    { 39, 0, "头部", "Head" }, // PART_HEAD
    { 39, 1, "后腿鱼鳍", "Back Leg Fins" }, // PART_B_LEG_FINS
    { 39, 2, "右臂", "Right Arm" }, // PART_R_ARM
    { 39, 3, "左臂", "Left Arm" }, // PART_L_ARM
    { 39, 4, "尾巴", "Tail" }, // PART_TAIL
    { 39, 5, "躯干", "Torso" }, // PART_TORSO
    { 39, 6, "背部", "Back" }, // PART_BACK
    { 39, 7, "雷电球", "Thunderballs" }, // PART_THUNDERBALLS
    { 39, 8, "胸部", "Chest" }, // PART_CHEST
    { 40, 0, "头部", "Head" }, // PART_HEAD
    { 40, 1, "躯干", "Torso" }, // PART_TORSO
    { 40, 2, "左腿", "Left Leg" }, // PART_L_LEG
    { 40, 3, "右腿", "Right Leg" }, // PART_R_LEG
    { 40, 4, "尾巴", "Tail" }, // PART_TAIL
    { 40, 5, "左翼", "Left Wing" }, // PART_L_WING
    { 40, 6, "右翼", "Right Wing" }, // PART_R_WING
    { 41, 0, "头部", "Head" }, // PART_HEAD
    { 41, 1, "躯干", "Torso" }, // PART_TORSO
    { 41, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 41, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 41, 4, "左腿", "Left Leg" }, // PART_L_LEG
    { 41, 5, "右腿", "Right Leg" }, // PART_R_LEG
    { 41, 6, "尾巴", "Tail" }, // PART_TAIL
    { 42, 0, "头部", "Head" }, // PART_HEAD
    { 42, 1, "手臂", "Arms" }, // PART_ARMS
    { 42, 2, "身体", "Body" }, // PART_BODY
    { 42, 3, "尾巴", "Tail" }, // PART_TAIL
    { 42, 4, "石头", "Rock" }, // PART_ROCK
    { 42, 5, "茶釜", "Pot" }, // PART_POT
    { 43, 0, "头部", "Head" }, // PART_HEAD
    { 43, 1, "躯干", "Torso" }, // PART_TORSO
    { 43, 2, "左腿", "Left Leg" }, // PART_L_LEG
    { 43, 3, "右腿", "Right Leg" }, // PART_R_LEG
    { 43, 4, "尾巴", "Tail" }, // PART_TAIL
    { 43, 5, "头部(泥土)", "Head (Mud)" }, // PART_HEAD_MUD
    { 43, 6, "躯干(泥土)", "Torso (Mud)" }, // PART_TORSO_MUD
    { 43, 7, "左腿(泥土)", "Left Leg (Mud)" }, // PART_L_LEG_MUD
    { 43, 8, "右腿(泥土)", "Right Leg (Mud)" }, // PART_R_LEG_MUD
    { 43, 9, "尾巴(泥土)", "Tail (Mud)" }, // PART_TAIL_MUD
    { 44, 0, "头部", "Head" }, // PART_HEAD
    { 44, 1, "躯干", "Torso" }, // PART_TORSO
    { 44, 2, "背部", "Back" }, // PART_BACK
    { 44, 3, "前腿", "Forelegs" }, // PART_FORELEGS
    { 44, 4, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 44, 5, "尾巴", "Tail" }, // PART_TAIL
    { 45, 0, "头部", "Head" }, // PART_HEAD
    { 45, 1, "躯干", "Torso" }, // PART_TORSO
    { 45, 2, "腿", "Legs" }, // PART_LEGS
    { 45, 3, "左翼", "Left Wing" }, // PART_L_WING
    { 45, 4, "右翼", "Right Wing" }, // PART_R_WING
    { 45, 5, "尾巴", "Tail" }, // PART_TAIL
    { 46, 0, "头部", "Head" }, // PART_HEAD
    { 46, 1, "躯干", "Torso" }, // PART_TORSO
    { 46, 2, "腿", "Legs" }, // PART_LEGS
    { 46, 3, "左翼", "Left Wing" }, // PART_L_WING
    { 46, 4, "右翼", "Right Wing" }, // PART_R_WING
    { 46, 5, "尾巴", "Tail" }, // PART_TAIL
    { 76, 0, "头部", "Head" }, // PART_HEAD
    { 76, 1, "身体", "Body" }, // PART_BODY
    { 76, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 76, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 76, 4, "左腿", "Left Leg" }, // PART_L_LEG
    { 76, 5, "右腿", "Right Leg" }, // PART_R_LEG
    { 76, 6, "尾巴", "Tail" }, // PART_TAIL
    { 77, 0, "身体", "Body" }, // PART_BODY
    { 77, 1, "左翼", "Left Wing" }, // PART_L_WING
    { 77, 2, "右翼", "Right Wing" }, // PART_R_WING
    { 77, 3, "左腿", "Left Leg" }, // PART_L_LEG
    { 77, 4, "右腿", "Right Leg" }, // PART_R_LEG
    { 77, 5, "颈部", "Neck" }, // PART_NECK
    { 77, 6, "头部", "Head" }, // PART_HEAD
    { 77, 7, "尾巴", "Tail" }, // PART_TAIL
    { 78, 0, "头部", "Head" }, // PART_HEAD
    { 78, 1, "身体", "Body" }, // PART_BODY
    { 78, 2, "壳", "Shell" }, // PART_SHELL
    { 78, 3, "左腿", "Left Leg" }, // PART_L_LEG
    { 78, 4, "右腿", "Right Leg" }, // PART_R_LEG
    { 78, 5, "左爪", "Left Claw" }, // PART_L_CLAW
    { 78, 6, "右爪", "Right Claw" }, // PART_R_CLAW
    { 78, 7, "前腿", "Forelegs" }, // PART_FORELEGS
    { 79, 0, "头部", "Head" }, // PART_HEAD
    { 79, 1, "身体", "Body" }, // PART_BODY
    { 79, 2, "壳", "Shell" }, // PART_SHELL
    { 79, 3, "左腿", "Left Leg" }, // PART_L_LEG
    { 79, 4, "右腿", "Right Leg" }, // PART_R_LEG
    { 79, 5, "左爪", "Left Claw" }, // PART_L_CLAW
    { 79, 6, "右爪", "Right Claw" }, // PART_R_CLAW
    { 79, 7, "前腿", "Forelegs" }, // PART_FORELEGS
    { 79, 8, "壳", "Shell" }, // PART_SHELL
    { 79, 9, "壳", "Shell" }, // PART_SHELL
    { 80, 0, "头部", "Head" }, // PART_HEAD
    { 80, 1, "躯干", "Torso" }, // PART_TORSO
    { 80, 2, "左臂", "Left Arm" }, // PART_L_ARM
    { 80, 3, "右臂", "Right Arm" }, // PART_R_ARM
    { 80, 4, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 80, 5, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 80, 6, "尾巴", "Tail" }, // PART_TAIL
    { 81, 0, "头部", "Head" }, // PART_HEAD
    { 81, 1, "身体", "Body" }, // PART_BODY
    { 81, 2, "左刃翼", "Left Cutwing" }, // PART_L_CUTWING
    { 81, 3, "尾巴", "Tail" }, // PART_TAIL
    { 81, 4, "前肢", "Foreleg" }, // PART_FORELEG
    { 81, 5, "后肢", "Hind Leg" }, // PART_H_LEG
    { 81, 6, "右刃翼", "Right Cutwing" }, // PART_R_CUTWING
    { 81, 7, "后肢", "Hind Leg" }, // PART_H_LEG
    { 82, 0, "头部", "Head" }, // PART_HEAD
    { 82, 1, "躯干", "Torso" }, // PART_TORSO
    { 82, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 82, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 82, 4, "前腿", "Forelegs" }, // PART_FORELEGS
    { 82, 5, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 82, 6, "尾巴", "Tail" }, // PART_TAIL
    { 82, 7, "触角", "Antenna" }, // PART_ANTENNA
    { 83, 0, "头部", "Head" }, // PART_HEAD
    { 83, 1, "躯干", "Torso" }, // PART_TORSO
    { 83, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 83, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 83, 4, "前腿", "Forelegs" }, // PART_FORELEGS
    { 83, 5, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 83, 6, "尾巴", "Tail" }, // PART_TAIL
    { 84, 0, "头部", "Head" }, // PART_HEAD
    { 84, 1, "躯干", "Torso" }, // PART_TORSO
    { 84, 2, "左腿", "Left Leg" }, // PART_L_LEG
    { 84, 3, "右腿", "Right Leg" }, // PART_R_LEG
    { 84, 4, "左翼", "Left Wing" }, // PART_L_WING
    { 84, 5, "右翼", "Right Wing" }, // PART_R_WING
    { 84, 6, "尾巴", "Tail" }, // PART_TAIL
    { 85, 0, "羽冠", "Crest" }, // PART_CREST
    { 85, 1, "躯干", "Torso" }, // PART_TORSO
    { 85, 2, "右翼", "Right Wing" }, // PART_R_WING
    { 85, 3, "左翼", "Left Wing" }, // PART_L_WING
    { 85, 4, "右腿", "Right Leg" }, // PART_R_LEG
    { 85, 5, "左腿", "Left Leg" }, // PART_L_LEG
    { 85, 6, "尾巴", "Tail" }, // PART_TAIL
    { 85, 7, "尾巴尖", "Tail Tip" }, // PART_TAIL_TIP
    { 86, 0, "头部", "Head" }, // PART_HEAD
    { 86, 1, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 86, 2, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 86, 3, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 86, 4, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 86, 5, "躯干", "Torso" }, // PART_TORSO
    { 86, 6, "尾巴", "Tail" }, // PART_TAIL
    { 86, 7, "背鳍", "Dorsal Fin" }, // PART_DORSAL_FIN
    { 87, 0, "头部", "Head" }, // PART_HEAD
    { 87, 1, "躯干", "Torso" }, // PART_TORSO
    { 87, 2, "右臂", "Right Arm" }, // PART_R_ARM
    { 87, 3, "左臂", "Left Arm" }, // PART_L_ARM
    { 87, 4, "背部", "Back" }, // PART_BACK
    { 87, 5, "尾巴", "Tail" }, // PART_TAIL
    { 87, 6, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 88, 0, "头部", "Head" }, // PART_HEAD
    { 88, 1, "身体", "Body" }, // PART_BODY
    { 88, 2, "右臂", "Right Arm" }, // PART_R_ARM
    { 88, 3, "左臂", "Left Arm" }, // PART_L_ARM
    { 88, 4, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 88, 5, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 88, 6, "尾巴", "Tail" }, // PART_TAIL
    { 89, 0, "躯干", "Torso" }, // PART_TORSO
    { 89, 1, "头部", "Head" }, // PART_HEAD
    { 89, 2, "颈部", "Neck" }, // PART_NECK
    { 89, 3, "左臂", "Left Arm" }, // PART_L_ARM
    { 89, 4, "右臂", "Right Arm" }, // PART_R_ARM
    { 89, 5, "左腿", "Left Leg" }, // PART_L_LEG
    { 89, 6, "右腿", "Right Leg" }, // PART_R_LEG
    { 89, 7, "尾巴", "Tail" }, // PART_TAIL
    { 90, 0, "头部", "Head" }, // PART_HEAD
    { 90, 1, "爪子", "Claw" }, // PART_CLAW
    { 90, 2, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 90, 3, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 90, 4, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 90, 5, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 90, 6, "腹部", "Abdomen" }, // PART_ABDOMEN
    { 90, 7, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 90, 8, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 90, 9, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 90, 10, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 90, 11, "腹部", "Abdomen" }, // PART_ABDOMEN
    { 90, 12, "胸部", "Chest" }, // PART_CHEST
    { 91, 0, "头部", "Head" }, // PART_HEAD
    { 91, 1, "身体", "Body" }, // PART_BODY
    { 91, 2, "左臂", "Left Arm" }, // PART_L_ARM
    { 91, 3, "右臂", "Right Arm" }, // PART_R_ARM
    { 91, 4, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 91, 5, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 91, 6, "尾巴", "Tail" }, // PART_TAIL
    { 91, 7, "尾巴尖", "Tail Tip" }, // PART_TAIL_TIP
    { 91, 8, "泥土球", "Mud Ball" }, // PART_MUD_BALL
    { 92, 0, "头部", "Head" }, // PART_HEAD
    { 92, 1, "身体", "Body" }, // PART_BODY
    { 92, 2, "腿", "Legs" }, // PART_LEGS
    { 92, 3, "左翼", "Left Wing" }, // PART_L_WING
    { 92, 4, "右翼", "Right Wing" }, // PART_R_WING
    { 92, 5, "尾巴", "Tail" }, // PART_TAIL
    { 93, 0, "头部", "Head" }, // PART_HEAD
    { 93, 1, "躯干", "Torso" }, // PART_TORSO
    { 93, 2, "左臂", "Left Arm" }, // PART_L_ARM
    { 93, 3, "右臂", "Right Arm" }, // PART_R_ARM
    { 93, 4, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 93, 5, "翼", "Wings" }, // PART_WINGS
    { 93, 6, "尾巴", "Tail" }, // PART_TAIL
    { 94, 0, "头部", "Head" }, // PART_HEAD
    { 94, 1, "身体", "Body" }, // PART_BODY
    { 94, 2, "左臂", "Left Arm" }, // PART_L_ARM
    { 94, 3, "右臂", "Right Arm" }, // PART_R_ARM
    { 94, 4, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 94, 5, "尾巴", "Tail" }, // PART_TAIL
    { 94, 6, "腹部", "Abdomen" }, // PART_ABDOMEN
    { 94, 7, "背部", "Back" }, // PART_BACK
    { 95, 0, "头部", "Head" }, // PART_HEAD
    { 95, 1, "头部", "Head" }, // PART_HEAD
    { 95, 2, "躯干", "Torso" }, // PART_TORSO
    { 95, 3, "左臂", "Left Arm" }, // PART_L_ARM
    { 95, 4, "右臂", "Right Arm" }, // PART_R_ARM
    { 95, 5, "后肢", "Hind Leg" }, // PART_H_LEG
    { 95, 6, "后肢", "Hind Leg" }, // PART_H_LEG
    { 95, 7, "尾巴", "Tail" }, // PART_TAIL
    { 96, 0, "头部", "Head" }, // PART_HEAD
    { 96, 1, "躯干", "Torso" }, // PART_TORSO
    { 96, 2, "翼爪", "Wingclaw" }, // PART_WING_CLAW
    { 96, 3, "翼爪", "Wingclaw" }, // PART_WING_CLAW
    { 96, 4, "翼爪", "Wingclaw" }, // PART_WING_CLAW
    { 96, 5, "翼爪", "Wingclaw" }, // PART_WING_CLAW
    { 96, 6, "前肢", "Foreleg" }, // PART_FORELEG
    { 96, 7, "前肢", "Foreleg" }, // PART_FORELEG
    { 96, 8, "前肢", "Foreleg" }, // PART_FORELEG
    { 96, 9, "前肢", "Foreleg" }, // PART_FORELEG
    { 96, 10, "后肢", "Hind Leg" }, // PART_H_LEG
    { 96, 11, "后肢", "Hind Leg" }, // PART_H_LEG
    { 96, 12, "尾巴", "Tail" }, // PART_TAIL
    { 96, 13, "头部", "Head" }, // PART_HEAD
    { 96, 14, "背部", "Back" }, // PART_BACK
    { 97, 0, "头部", "Head" }, // PART_HEAD
    { 97, 1, "躯干", "Torso" }, // PART_TORSO
    { 97, 2, "翼", "Wings" }, // PART_WINGS
    { 97, 3, "腿", "Legs" }, // PART_LEGS
    { 97, 4, "尾巴", "Tail" }, // PART_TAIL
    { 98, 0, "头部", "Head" }, // PART_HEAD
    { 98, 1, "躯干", "Torso" }, // PART_TORSO
    { 98, 2, "翼", "Wings" }, // PART_WINGS
    { 98, 3, "腿", "Legs" }, // PART_LEGS
    { 98, 4, "尾巴", "Tail" }, // PART_TAIL
    { 107, 0, "头部", "Head" }, // PART_HEAD
    { 107, 1, "躯干", "Torso" }, // PART_TORSO
    { 107, 2, "左腿", "Left Leg" }, // PART_L_LEG
    { 107, 3, "右腿", "Right Leg" }, // PART_R_LEG
    { 107, 4, "翼", "Wings" }, // PART_WINGS
    { 107, 5, "尾巴", "Tail" }, // PART_TAIL
    { 108, 0, "头部", "Head" }, // PART_HEAD
    { 108, 1, "胸部", "Chest" }, // PART_CHEST
    { 108, 2, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 108, 3, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 108, 4, "左后肢", "Left Hind Leg" }, // PART_L_H_LEG
    { 108, 5, "右后肢", "Right Hind Leg" }, // PART_R_H_LEG
    { 108, 6, "尾巴", "Tail" }, // PART_TAIL
    { 108, 7, "翼", "Wings" }, // PART_WINGS
    { 109, 0, "头部", "Head" }, // PART_HEAD
    { 109, 1, "胸部", "Chest" }, // PART_CHEST
    { 109, 2, "前腿", "Forelegs" }, // PART_FORELEGS
    { 109, 3, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 109, 4, "翼", "Wings" }, // PART_WINGS
    { 109, 5, "尾巴", "Tail" }, // PART_TAIL
    { 110, 0, "头部", "Head" }, // PART_HEAD
    { 110, 1, "躯干", "Torso" }, // PART_TORSO
    { 110, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 110, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 110, 4, "前腿", "Forelegs" }, // PART_FORELEGS
    { 110, 5, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 110, 6, "尾巴", "Tail" }, // PART_TAIL
    { 111, 0, "头部", "Head" }, // PART_HEAD
    { 111, 1, "躯干", "Torso" }, // PART_TORSO
    { 111, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 111, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 111, 4, "左前肢", "Left Foreleg" }, // PART_L_FORELEG
    { 111, 5, "右前肢", "Right Foreleg" }, // PART_R_FORELEG
    { 111, 6, "尾巴", "Tail" }, // PART_TAIL
    { 111, 7, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 111, 8, "胸部", "Chest" }, // PART_CHEST
    { 112, 0, "头部", "Head" }, // PART_HEAD
    { 112, 1, "躯干", "Torso" }, // PART_TORSO
    { 112, 2, "左臂", "Left Arm" }, // PART_L_ARM
    { 112, 3, "右臂", "Right Arm" }, // PART_R_ARM
    { 112, 4, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 112, 5, "翼", "Wings" }, // PART_WINGS
    { 112, 6, "尾巴", "Tail" }, // PART_TAIL
    { 113, 0, "头部", "Head" }, // PART_HEAD
    { 113, 1, "躯干", "Torso" }, // PART_TORSO
    { 113, 2, "左翼", "Left Wing" }, // PART_L_WING
    { 113, 3, "右翼", "Right Wing" }, // PART_R_WING
    { 113, 4, "前腿", "Forelegs" }, // PART_FORELEGS
    { 113, 5, "后腿", "Hind Legs" }, // PART_H_LEGS
    { 113, 6, "尾巴", "Tail" }, // PART_TAIL
    { 113, 7, "触角", "Antenna" }, // PART_ANTENNA
    { 114, 0, "头部", "Head" }, // PART_HEAD
    { 114, 1, "躯干", "Torso" }, // PART_TORSO
    { 114, 2, "前肢", "Foreleg" }, // PART_FORELEG
    { 114, 3, "腿", "Legs" }, // PART_LEGS
    { 114, 4, "翼", "Wings" }, // PART_WINGS
    { 114, 5, "尾巴", "Tail" }, // PART_TAIL
    { 114, 6, "Unknown", "Unknown" }, // PART_TO_BE_MAPPED
    { 114, 7, "Unknown", "Unknown" }, // PART_TO_BE_MAPPED
    { 114, 8, "Unknown", "Unknown" }, // PART_TO_BE_MAPPED
    { 114, 9, "Unknown", "Unknown" }, // PART_TO_BE_MAPPED
    { 114, 10, "Unknown", "Unknown" }, // PART_TO_BE_MAPPED
    { 114, 11, "Unknown", "Unknown" }, // PART_TO_BE_MAPPED
    { 115, 0, "头部", "Head" }, // PART_HEAD
    { 115, 1, "左臂", "Left Arm" }, // PART_L_ARM
    { 115, 2, "右臂", "Right Arm" }, // PART_R_ARM
    { 115, 3, "腹部", "Abdomen" }, // PART_ABDOMEN
    { 115, 4, "尾巴", "Tail" }, // PART_TAIL
    { 115, 5, "背部", "Back" }, // PART_BACK
    { 115, 6, "Unknown", "Unknown" }, // PART_TO_BE_MAPPED
    { 115, 7, "左腿", "Left Leg" }, // PART_L_LEG
    { 115, 8, "右腿", "Right Leg" }, // PART_R_LEG
}};

constexpr bool risePartNamesStrictlyIncreasing()
{
    for (std::size_t i = 1; i < kRisePartNames.size(); ++i) {
        const RisePartName &previous = kRisePartNames[i - 1];
        const RisePartName &current = kRisePartNames[i];
        if (previous.monsterId > current.monsterId
            || (previous.monsterId == current.monsterId
                && previous.partIndex >= current.partIndex))
            return false;
    }
    return true;
}

static_assert(risePartNamesStrictlyIncreasing());

constexpr bool risePartNamesCarryBothColumns()
{
    for (const RisePartName &entry : kRisePartNames) {
        if (entry.name == nullptr || entry.name[0] == '\0'
            || entry.nameEn == nullptr || entry.nameEn[0] == '\0')
            return false;
    }
    return true;
}
static_assert(risePartNamesCarryBothColumns(),
              "every Rise part row carries a zh and an en name");

const RisePartName *risePartNameEntry(int monsterId, int partIndex)
{
    const auto it = std::lower_bound(
        kRisePartNames.begin(), kRisePartNames.end(),
        std::pair{monsterId, partIndex},
        [](const RisePartName &entry, const std::pair<int, int> &key) {
            return entry.monsterId < key.first
                || (entry.monsterId == key.first && entry.partIndex < key.second);
        });
    if (it == kRisePartNames.end() || it->monsterId != monsterId
        || it->partIndex != partIndex)
        return nullptr;
    return &*it;
}

// HunterPie's tables intentionally leave PART_TO_BE_MAPPED as "Unknown" in
// both locales. Preserve the existing localized fallback instead of exposing
// that upstream placeholder in the overlay.
const char *risePartNameFromEntry(const RisePartName *entry, bool english)
{
    if (entry == nullptr)
        return nullptr;
    const char *name = english ? entry->nameEn : entry->name;
    if (std::strcmp(name, "Unknown") == 0)
        return nullptr;
    return name;
}

QString compactPartHealthValue(float value)
{
    if (!std::isfinite(value) || value < 0.0F)
        return QStringLiteral("--");
    const int rounded = static_cast<int>(std::lround(value));
    if (rounded < 1000)
        return QString::number(rounded);
    const float thousands = static_cast<float>(rounded) / 1000.0F;
    if (rounded % 1000 == 0 || thousands >= 10.0F)
        return QStringLiteral("%1k").arg(static_cast<int>(std::lround(thousands)));
    return QStringLiteral("%1k").arg(thousands, 0, 'f', 1);
}

} // namespace

const char *risePartName(int monsterId, int partIndex)
{
    return risePartNameFromEntry(risePartNameEntry(monsterId, partIndex),
                                 StringTable::instance().isEnglish());
}

const char *risePartNameEn(int monsterId, int partIndex)
{
    return risePartNameFromEntry(risePartNameEntry(monsterId, partIndex), true);
}

QString risePartDisplayName(int monsterId, int partIndex)
{
    const bool english = StringTable::instance().isEnglish();
    if (const char *name = risePartNameFromEntry(
            risePartNameEntry(monsterId, partIndex), english))
        return QString::fromUtf8(name);
    return english ? QStringLiteral("Part %1").arg(partIndex)
                   : QStringLiteral("部位 %1").arg(partIndex);
}

QString compactPartHealth(float current, float maximum)
{
    if (!std::isfinite(maximum) || maximum <= 0.0F)
        return QStringLiteral("--/--");
    return QStringLiteral("%1/%2")
        .arg(compactPartHealthValue(current))
        .arg(compactPartHealthValue(maximum));
}

PartHealthPair partHealthForDisplay(const PartSnapshot &part)
{
    if (part.partType == PartType::Flinch)
        return {part.flinch, part.maxFlinch};
    return {part.health, part.maxHealth};
}

} // namespace mhw
