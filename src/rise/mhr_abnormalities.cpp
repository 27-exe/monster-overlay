// SPDX-License-Identifier: Apache-2.0
//
// AUTO-GENERATED — do not edit by hand.
// Regenerate with scripts/extract_rise_abnormalities.py
//
// Source: HunterPie v2
//   HunterPie/Game/Rise/Data/AbnormalityData.xml
//     sha256 537298c8322d665a0feed84e6c9871b7df936f60286b844ab24e9bbc852cda65
//     <Abnormality> nodes whose Category attribute (defaulting to
//     the parent group) is "Consumables" or "Debuffs" — the two
//     lists MHRPlayer.GetConsumableAbnormalities / …Debuff…
//     iterate via AbnormalityRepository.FindAllAbnormalitiesBy.
//     Hunting-horn songs carry Category="Songs" and are absent
//     by construction (MHRPlayer.GetSongs uses HH_ABNORMALITIES_OFFSETS).
//   Flag bits: MHRAbnormalityFlagTypeParser → PlayerCommonConditions /
//     PlayerDebuffConditions / PlayerActionFlags ([Flags] enums, hashed
//     below; the generator fails if a Flag member no longer parses).
//       PlayerCommonConditions.cs sha256 6ebd38ba2127d710231ef915c6b6453a60e4999e6943e8341bb4aaed2e589355
//       PlayerDebuffConditions.cs sha256 f683f46ba1a79a1db07121d99d6a37f8c8a43c54bcfa04573108b50266d0b229
//       PlayerActionFlags.cs sha256 8f07190005c0a2e203003ac04c58cda3bf1e3b9a972b73d74a39b7364fb9a817
//   Names (both locales, verbatim):
//     zh column: zh-cn.xml <Abnormalities>/Abnormality[@Id = Name]
//       sha256 2a4b1bb318fc21a55c0b5b978e2d33cb8c2ea74457b34fcac88b9236c19a06db
//     en column: en-us.xml <Abnormalities>/Abnormality[@Id = Name]
//       sha256 661d58b54bd1214ae0e9eff92fb4167b8aab54df0452eea238cb9574300030ea
//     (first match wins: HunterPie's FindStringBy uses SelectSingleNode;
//      the two locale files carry the same Id set — the generator
//      refuses to run otherwise.)
//   Ailment slot labels: HunterPie/Game/Rise/Data/MonsterData.xml
//     <Ailments> (slot -> AILMENT_* key) joined to the zh labels of
//     src/monster/part_schemas.cpp kRiseAilmentNames and the en-us.xml
//     <Ailments><Rise> strings; see the kRiseAilmentNames comment below.
//
// 69 consumables + 28 debuffs, in XML document
// order. Field parsing mirrors XmlNodeToAbnormalityDefinitionMapper
// (Offset/DependsOn hex, WithValue/MaxTimer/MaxBuildup decimal).
//
// Locale selection (v0.9 i18n, WS-B): riseAbnormalityDisplayName() and
// riseAilmentDisplayName() return the en column while
// mhw::StringTable::instance().isEnglish() is true and the zh column
// otherwise; an empty locale (before any load()) reads as Chinese, so
// the pre-i18n behaviour is the default.

#include "rise/mhr_abnormalities.h"

#include "core/string_table.h"

#include <array>
#include <cstddef>
#include <cstring>

namespace mhw {
namespace {

constexpr RiseAbnormalitySchema kConsumables[69] = {
    { "ABN_DEMONDRUG", "ABNORMALITY_DEMONDRUG",
      "鬼人药", "Demondrug", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x000, 0x07C,
      5, 0, 0.0F,
      true, false, false },  // infinite
    { "ABN_MEGA_DEMONDRUG", "ABNORMALITY_MEGA_DEMONDRUG",
      "鬼人药·大", "Mega Demondrug", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x000, 0x07C,
      7, 0, 0.0F,
      true, false, false },  // infinite
    { "ABN_ARMORSKIN", "ABNORMALITY_ARMORSKIN",
      "硬化药", "Armorskin", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x000, 0x080,
      15, 0, 0.0F,
      true, false, false },  // infinite
    { "ABN_MEGA_ARMORSKIN", "ABNORMALITY_MEGA_ARMORSKIN",
      "硬化药·大", "Mega Armorskin", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x000, 0x080,
      25, 0, 0.0F,
      true, false, false },  // infinite
    { "ABN_MIGHT_SEED", "ABNORMALITY_MIGHT_SEED",
      "怪力种子", "Might Seed", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x090, 0x084,
      10, 0, 0.0F,
      false, false, false },
    { "ABN_BUTTERFLAME", "ABNORMALITY_BUTTERFLAME",
      "炎火蝶", "Butterflame", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x090, 0x084,
      25, 0, 0.0F,
      false, false, false },
    { "ABN_ADAMANT_SEED", "ABNORMALITY_ADAMANT_SEED",
      "忍耐种子", "Adamant Seed", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x094, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_CLOTHFLY", "ABNORMALITY_CLOTHFLY",
      "匹白蝶", "Clothfly", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x098, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_DASH_JUICE", "ABNORMALITY_DASH_JUICE",
      "强走药", "Dash Juice", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x09C, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_DEMON_POWDER", "ABNORMALITY_DEMON_POWDER",
      "鬼人粉尘", "Demon Powder", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0A8, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_HARDSHELL_POWDER", "ABNORMALITY_HARDSHELL_POWDER",
      "硬化粉尘", "Hardshell Powder", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0AC, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_ARC_SHOT_BRACE", "ABNORMALITY_ARC_SHOT_BRACE",
      "变更曲射【耐冲型】", "Arc Shot: Brace", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0B0, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_REDLAMPSQUID", "ABNORMALITY_REDLAMPSQUID",
      "红不知火乌贼", "Red Lampsquid", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0B4, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_YELLOWLAMPSQUID", "ABNORMALITY_YELLOWLAMPSQUID",
      "黄不知火乌贼", "Yellow Lampsquid", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0BC, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "Consumables_BC", "ABNORMALITY_CUTTERFLY",
      "碎网赤蜻", "Cutterfly", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0C4, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_ARC_SHOT_AFFINITY", "ABNORMALITY_ARC_SHOT_AFFINITY",
      "变更曲射【会心型】", "Arc Shot: Affinity", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0C8, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_STINKMINK", "ABNORMALITY_STINKMINK",
      "烟雪鼬", "Stinkmink", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0D4, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_IMMUNITY", "ABNORMALITY_IMMUNITY",
      "免疫", "Immunity", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0DC, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_GOURMET_FISH", "ABNORMALITY_GOURMET_FISH",
      "体力回复速度提升【熟鱼】", "Gourmet Fish", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0E4, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_NATURAL_HEALING", "ABNORMALITY_NATURAL_HEALING",
      "活力剂", "Natural Healing", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0E8, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_GO_FIGHT_WIN", "ABNORMALITY_GO_FIGHT_WIN",
      "应援舞蹈之技", "Go, Fight, Win", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0EC, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_DEMON_AMMO", "ABNORMALITY_DEMON_AMMO",
      "鬼人弹", "Demon Ammo", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0F0, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_ARMOR_AMMO", "ABNORMALITY_ARMOR_AMMO",
      "硬化弹", "Armor Ammo", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0F4, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "Consumables_F4", "ABNORMALITY_UNKNOWN",
      "Unknown", "Unknown", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0FC, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_POWER_DRUM", "ABNORMALITY_POWER_DRUM",
      "强化太鼓之技", "Power Drum", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x100, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_ROUSING_ROAR", "ABNORMALITY_ROUSING_ROAR",
      "强化咆哮之技", "Rousing Roar", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x104, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_BLEED_HEALING", "ABNORMALITY_BLEED_HEALING",
      "体力回复速度提升【裂伤治愈】", "Health Regeneration (Bleed)", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x180, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_FRENZY_IMMUNITY", "ABNORMALITY_FRENZY_IMMUNITY",
      "狂龙克服", "Frenzy Immunity", "Consumables",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x194, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_RUBY_WIREBUG", "ABNORMALITY_RUBY_WIREBUG",
      "变幻翔虫・红", "Ruby Wirebug", "Consumables",
      "RubyWirebug", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000000400000000ULL, 0x1A0, 0x000,
      0, 0, 0.0F,
      false, false, false },  // RiseCommon:RubyWirebug
    { "ABN_GOLD_WIREBUG", "ABNORMALITY_GOLD_WIREBUG",
      "变幻翔虫・金", "Gold Wirebug", "Consumables",
      "GoldWirebug", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000001000000000ULL, 0x1A0, 0x000,
      0, 0, 0.0F,
      false, false, false },  // RiseCommon:GoldWirebug
    { "ABN_HEALTH_REGEN", "ABNORMALITY_HEALTH_REGEN",
      "体力持续回复", "Health Regeneration", "Consumables",
      "HealthRegen", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000000000004000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:HealthRegen, infinite
    { "ABN_OFFENSIVE_GUARD", "ABNORMALITY_OFFENSIVE_GUARD",
      "攻击守势", "Offensive Guard", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x0F8, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_AFFINITY_SLIDING", "ABNORMALITY_AFFINITY_SLIDING",
      "滑走强化", "Affinity Sliding", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x114, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_WALLRUNNER_ATKUP", "ABNORMALITY_WALL_RUNNER",
      "墙面移动【攻】", "Wall Runner (Attack UP)", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x11C, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_COUNTERSTRIKE", "ABNORMALITY_COUNTERSTRIKE",
      "逆袭", "Counterstrike", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x120, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_KUSHALA_DAORA_SOUL", "ABNORMALITY_KUSHALA_DAORA_SOUL",
      "钢龙之魂", "Kushala Daora Soul", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x12C, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_CHAMELEOS_SOUL", "ABNORMALITY_CHAMELEOS_SOUL",
      "炎王龙之魂", "Chameleos Soul", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x134, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_COALESCENCE", "ABNORMALITY_COALESCENCE",
      "转祸为福", "Coalescence", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x168, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_CHAIN_CRIT", "ABNORMALITY_CHAIN_CRIT",
      "连击", "Chain Crit", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x16C, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_ADRENALINE_RUSH", "ABNORMALITY_ADRENALINE_RUSH",
      "巧击", "Adrenaline Rush", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x174, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_SPIRIBIRDS_CALL", "ABNORMALITY_SPIRIBIRDS_CALL",
      "提供", "Spiribird's Call", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x17C, 0x000,
      0, 0, 60.0F,
      false, false, false },  // maxTimer=60
    { "ABN_DERELICTION_S", "ABNORMALITY_DERELICTION_S",
      "伏魔耗命【小】", "Dereliction (S)", "Skills",
      "DerelictionS", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000020000000000ULL, 0x184, 0x000,
      0, 50, 0.0F,
      false, true, true },  // RiseCommon:DerelictionS, buildup max=50
    { "ABN_DERELICTION_M", "ABNORMALITY_DERELICTION_M",
      "伏魔耗命【中】", "Dereliction (M)", "Skills",
      "DerelictionM", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000040000000000ULL, 0x184, 0x000,
      0, 100, 0.0F,
      false, true, true },  // RiseCommon:DerelictionM, buildup max=100
    { "ABN_DERELICTION_L", "ABNORMALITY_DERELICTION_L",
      "伏魔耗命【大】", "Dereliction (L)", "Skills",
      "DerelictionL", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000080000000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:DerelictionL, infinite
    { "ABN_FURIOUS_BUILDUP", "ABNORMALITY_FURIOUS_BUILDUP",
      "激昂（怒气积攒）", "Furious (Fury Buildup)", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x188, 0x000,
      0, 100, 0.0F,
      false, true, true },  // buildup max=100
    { "ABN_FURIOUS_STAMINA_BUFF", "ABNORMALITY_FURIOUS_STAMINA_BUFF",
      "激昂（无限耐力）", "Furious (Infinite Stamina)", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x18C, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_GRINDER_S", "ABNORMALITY_GRINDER_S",
      "打磨术【锐】", "Grinder (S)", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x190, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_STATUS_TRIGGER", "ABNORMALITY_STATUS_TRIGGER",
      "状态异常必定累积", "Status Trigger", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x1AC, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_INTERPID_HEART", "ABNORMALITY_INTERPID_HEART",
      "刚心", "Interpid Heart (Buildup)", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x1B0, 0x000,
      0, 400, 0.0F,
      false, false, true },  // buildup max=400
    { "ABN_POWDERMANTLE_RED", "ABNORMALITY_POWDERMANTLE_RED",
      "粉尘绕【红】", "Powder Mantle (Red)", "Skills",
      "PowderMantleRed", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0400000000000000ULL, 0x1BC, 0x000,
      0, 0, 0.0F,
      false, false, false },  // RiseCommon:PowderMantleRed
    { "ABN_POWDERMANTLE_BLUE", "ABNORMALITY_POWDERMANTLE_BLUE",
      "粉尘绕【蓝】", "Powder Mantle (Blue)", "Skills",
      "PowderMantleBlue", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0800000000000000ULL, 0x1BC, 0x000,
      0, 0, 0.0F,
      false, false, false },  // RiseCommon:PowderMantleBlue
    { "ABN_AGITATOR", "ABNORMALITY_AGITATOR",
      "挑战者", "Agitator", "Skills",
      "Agitator", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000000040000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:Agitator, infinite
    { "ABN_DEFIANCE", "ABNORMALITY_DEFIANCE",
      "坚如磐石", "Defiance", "Skills",
      "Defiance", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000000100000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:Defiance, infinite
    { "ABN_HEROICS", "ABNORMALITY_HEROICS",
      "火场怪力", "Heroics", "Skills",
      "Heroics", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000000004000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:Heroics, infinite
    { "ABN_PEAK_PERFORMANCE", "ABNORMALITY_PEAK_PERFORMANCE",
      "无伤", "Peak Performance", "Skills",
      "PeakPerformance", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000000008000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:PeakPerformance, infinite
    { "ABN_DRAGONHEART", "ABNORMALITY_DRAGONHEART",
      "龙气活性", "Dragonheart", "Skills",
      "Dragonheart", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000000010000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:Dragonheart, infinite
    { "ABN_EMBOLDEN", "ABNORMALITY_EMBOLDEN",
      "嘲讽防御", "Embolden", "Skills",
      "Embolden", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000010000000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:Embolden, infinite
    { "ABN_STRIFE_S", "ABNORMALITY_STRIFE_S",
      "奋斗【小】", "Strife (S)", "Skills",
      "StrifeS", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x1000000000000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:StrifeS, infinite
    { "ABN_STRIFE_L", "ABNORMALITY_STRIFE_L",
      "奋斗【大】", "Strife (L)", "Skills",
      "StrifeL", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x2000000000000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:StrifeL, infinite
    { "ABN_BERSERK", "ABNORMALITY_BERSERK",
      "狂化", "Berserk", "Skills",
      "Berserk", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x4000000000000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:Berserk, infinite
    { "ABN_DRAGON_CONVERSION", "ABNORMALITY_DRAGON_CONVERSION",
      "龙气转换【属耐】", "Dragon Conversion (All Res. Up)", "Skills",
      "ResistUp", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x8000000000000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:ResistUp, infinite
    { "ABN_HEAVEN_SENT", "ABNORMALITY_HEAVEN_SENT",
      "天衣无缝", "Heaven-Sent", "Skills",
      "HeavenSentInvocation", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000000000040000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:HeavenSentInvocation, infinite
    { "ABN_MAXIMUM_MIGHT", "ABNORMALITY_MAXIMUM_MIGHT",
      "精神抖擞", "Maximum Might", "Skills",
      "MaximumMight", RiseAbnormalityFlagType::RiseAction,
      RiseAbnormalityKind::Buff,
      0x0000000000000020ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseAction:MaximumMight, infinite
    { "ABN_DANGO_BULKER", "ABNORMALITY_DANGO_BULKER",
      "团子健身术", "Dango Bulker", "Foods",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x090, 0x084,
      15, 0, 0.0F,
      false, false, false },
    { "ABN_DANGO_BOOSTER", "ABNORMALITY_DANGO_BOOSTER",
      "团子短期催眠术", "Dango Booster", "Foods",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x138, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_DANGO_GLUTTON", "ABNORMALITY_DANGO_GLUTTON",
      "团子饱腹术", "Dango Glutton", "Foods",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x144, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_DANGO_DEFENDER", "ABNORMALITY_DANGO_DEFENDER",
      "团子防御术", "Dango Defender", "Foods",
      "DangoDefender", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Buff,
      0x0000400000000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseCommon:DangoDefender, infinite
    { "ABN_DANGO_HUNTER", "ABNORMALITY_DANGO_HUNTER",
      "团子逃跑术", "Dango Hunter", "Foods",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x158, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_DANGO_CONNECTOR", "ABNORMALITY_DANGO_CONNECTOR",
      "团子牵绊术", "Dango Connector", "Foods",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Buff,
      0x0000000000000000ULL, 0x160, 0x000,
      0, 0, 0.0F,
      false, false, false },
};

constexpr RiseAbnormalitySchema kDebuffs[28] = {
    { "ABN_BLOOD", "ABNORMALITY_BLOOD",
      "劫血异常", "Bloodblight", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x7EC, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_PRE_SLEEP", "ABNORMALITY_PRE_SLEEP",
      "昏睡前夕", "Pre-Sleep", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x820, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_POISON", "ABNORMALITY_POISON",
      "中毒", "Poison", "Debuffs",
      "Poison", RiseAbnormalityFlagType::RiseDebuff,
      RiseAbnormalityKind::Debuff,
      0x0000000100000000ULL, 0x8A4, 0x000,
      0, 0, 0.0F,
      false, false, false },  // RiseDebuff:Poison
    { "ABN_VENOM", "ABNORMALITY_VENOM",
      "猛毒", "Venom", "Debuffs",
      "NoxiousPoison", RiseAbnormalityFlagType::RiseDebuff,
      RiseAbnormalityKind::Debuff,
      0x0000000200000000ULL, 0x8A4, 0x000,
      0, 0, 0.0F,
      false, false, false },  // RiseDebuff:NoxiousPoison
    { "ABN_STUN", "ABNORMALITY_STUN",
      "眩晕", "Stun", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8A8, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_SLEEP", "ABNORMALITY_SLEEP",
      "睡眠", "Sleep", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8AC, 0x810,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_PARALYSIS", "ABNORMALITY_PARALYSIS",
      "麻痹", "Paralysis", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8B0, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_DEF_DOWN", "ABNORMALITY_DEF_DOWN",
      "防御力下降", "Defense Down", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8BC, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_RES_DOWN", "ABNORMALITY_RES_DOWN",
      "全属性耐性值下降", "Resistance Down", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8C0, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_STENCH", "ABNORMALITY_STENCH",
      "无法使用部分道具", "Stench", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8C4, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_HELLFIRE", "ABNORMALITY_HELLFIRE",
      "鬼火异常", "Hellfireblight", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8C8, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_BLAST", "ABNORMALITY_BLAST",
      "爆炸异常", "Blastblight", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8CC, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_WEBBED", "ABNORMALITY_WEBBED",
      "拘束异常", "Webbed", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8D0, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_FIRE", "ABNORMALITY_FIRE",
      "火属性异常", "Fireblight", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8D4, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_WATER", "ABNORMALITY_WATER",
      "水属性异常", "Waterblight", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8D8, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_ICE", "ABNORMALITY_ICE",
      "冰属性异常", "Iceblight", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8DC, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_THUNDER", "ABNORMALITY_THUNDER",
      "雷属性异常", "Thunderblight", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8E0, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_DRAGON", "ABNORMALITY_DRAGON",
      "龙属性异常", "Dragonblight", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x8E4, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_BUBBLES", "ABNORMALITY_BUBBLES",
      "泡沫异常【小】", "Minor Bubbleblight", "Debuffs",
      "BubbleBlightS", RiseAbnormalityFlagType::RiseDebuff,
      RiseAbnormalityKind::Debuff,
      0x0000000000002000ULL, 0x8F0, 0x000,
      0, 0, 0.0F,
      false, false, false },  // RiseDebuff:BubbleBlightS
    { "ABN_BUBBLES_PLUS", "ABNORMALITY_BUBBLES_PLUS",
      "泡沫异常【大】", "Major Bubbleblight", "Debuffs",
      "BubbleBlightL", RiseAbnormalityFlagType::RiseDebuff,
      RiseAbnormalityKind::Debuff,
      0x0000020000000000ULL, 0x8F0, 0x000,
      0, 0, 0.0F,
      false, false, false },  // RiseDebuff:BubbleBlightL
    { "ABN_BLEED", "ABNORMALITY_BLEED",
      "裂伤", "Bleed", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x914, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_FRENZY_BUILDUP", "ABNORMALITY_FRENZY_BUILDUP",
      "狂龙症（增长中）", "The Frenzy (Buildup)", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x91C, 0x000,
      0, 120, 0.0F,
      false, false, true },  // buildup max=120
    { "ABN_FRENZY", "ABNORMALITY_FRENZY",
      "狂龙症", "The Frenzy", "Debuffs",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x92C, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_LEECHED", "ABNORMALITY_LEECHED",
      "吸血异常", "Leeched", "Debuffs",
      "Leeched", RiseAbnormalityFlagType::RiseDebuff,
      RiseAbnormalityKind::Debuff,
      0x0000100000000000ULL, 0x000, 0x000,
      0, 0, 0.0F,
      true, false, false },  // RiseDebuff:Leeched, infinite
    { "ABN_PROTECTIVE_POLISH", "ABNORMALITY_PROTECTIVE_POLISH",
      "刚刃研磨", "Protective Polish", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x1D8, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_LATENTPOWER", "ABNORMALITY_LATENTPOWER",
      "力量解放", "Latent Power", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x1DC, 0x000,
      0, 0, 0.0F,
      false, false, false },
    { "ABN_WINDMANTLE", "ABNORMALITY_WINDMANTLE",
      "风绕", "Wind Mantle", "Skills",
      "WindMantle", RiseAbnormalityFlagType::RiseCommon,
      RiseAbnormalityKind::Debuff,
      0x0200000000000000ULL, 0x79C, 0x000,
      0, 0, 15.0F,
      false, false, false },  // RiseCommon:WindMantle, maxTimer=15
    { "ABN_FROSTCRAFT", "ABNORMALITY_FROSTCRAFT_RISE",
      "寒气炼成", "Frostcraft", "Skills",
      "", RiseAbnormalityFlagType::None,
      RiseAbnormalityKind::Debuff,
      0x0000000000000000ULL, 0x7C8, 0x000,
      0, 100, 0.0F,
      false, false, true },  // buildup max=100
};

} // namespace

const RiseAbnormalitySchema *riseConsumableAbnormalities(std::size_t &count)
{
    count = std::size(kConsumables);
    return kConsumables;
}

const RiseAbnormalitySchema *riseDebuffAbnormalities(std::size_t &count)
{
    count = std::size(kDebuffs);
    return kDebuffs;
}

const RiseAbnormalitySchema *riseFindAbnormality(const char *id)
{
    if (id == nullptr || *id == '\0')
        return nullptr;

    std::size_t consumableCount = 0;
    std::size_t debuffCount = 0;
    const RiseAbnormalitySchema *tables[2] = {
        riseConsumableAbnormalities(consumableCount),
        riseDebuffAbnormalities(debuffCount),
    };
    const std::size_t counts[2] = { consumableCount, debuffCount };

    for (int t = 0; t < 2; ++t) {
        for (std::size_t i = 0; i < counts[t]; ++i) {
            if (std::strcmp(tables[t][i].id, id) == 0)
                return &tables[t][i];
        }
    }
    return nullptr;
}

QString riseAbnormalityTimerText(float timer, bool infinite, bool buildup,
                                 float maxBuildup)
{
    if (infinite)
        return QStringLiteral("\u221E"); // ∞
    if (buildup) {
        const int value = static_cast<int>(timer);
        return maxBuildup > 0.0F
            ? QStringLiteral("%1/%2").arg(value).arg(static_cast<int>(maxBuildup))
            : QString::number(value);
    }
    return QStringLiteral("%1s").arg(static_cast<int>(timer));
}

namespace {

// ---------------------------------------------------------------------------
// Rise monster-ailment slot labels (v0.9 i18n, WS-B).
//
// Slot ids are the <Ailment Id> attributes of HunterPie Game/Rise/Data/
// MonsterData.xml <Ailments> (0..16; the Id=-1 AILMENT_UNKNOWN row is
// upstream's "no ailment" sentinel and stays out of the table), and the
// nameKey is that row's String= AILMENT_* localization key.
//
//   en column: en-us.xml /Strings/Ailments/Rise/Ailment[@Id] — the same key.
//   zh column: the label the overlay has always shown for the slot — a
//     verbatim copy of kRiseAilmentNames in src/monster/part_schemas.cpp
//     (WS-A owns that table; the generator refuses to run when the slot sets
//     disagree). For slots 3/6/14/15 that label is deliberately shorter than
//     the raw zh-cn.xml string (闪光 vs 眩目, 疲劳 vs 虚弱, 捕获 vs 捕获用麻醉,
//     异臭 vs 异臭弹), which is why the zh column is copied rather than
//     re-extracted.
// ---------------------------------------------------------------------------
struct RiseAilmentLabel {
    int slotId;           // <Ailment Id> in MonsterData.xml (0..16)
    const char *nameKey;  // AILMENT_* localization key
    const char *name;     // zh label shown by the overlay (pre-i18n value)
    const char *nameEn;   // en-us.xml Ailments/Rise value for nameKey
};

// Sorted by slotId: riseAilmentLabel() binary-searches it.
constexpr std::array<RiseAilmentLabel, 17> kRiseAilmentNames = {{
    {  0, "AILMENT_PARALYSIS", "麻痹", "Paralysis" },
    {  1, "AILMENT_SLEEP", "睡眠", "Sleep" },
    {  2, "AILMENT_STUN", "眩晕", "Stun" },
    {  3, "AILMENT_FLASH", "闪光", "Flash" },
    {  4, "AILMENT_POISON", "毒", "Poison" },
    {  5, "AILMENT_BLAST", "爆破", "Blast" },
    {  6, "AILMENT_EXHAUST", "疲劳", "Exhaust" },
    {  7, "AILMENT_RIDE", "乘骑", "Ride" },
    {  8, "AILMENT_WATER", "水异常", "Waterblight" },
    {  9, "AILMENT_FIRE", "火异常", "Fireblight" },
    { 10, "AILMENT_ICE", "冰异常", "Iceblight" },
    { 11, "AILMENT_THUNDER", "雷异常", "Thunderblight" },
    { 12, "AILMENT_PITFALLTRAP", "落穴陷阱", "Pitfall Trap" },
    { 13, "AILMENT_SHOCKTRAP", "麻痹陷阱", "Shock Trap" },
    { 14, "AILMENT_TRANQUILIZE", "捕获", "Tranquilize" },
    { 15, "AILMENT_DUNG", "异臭", "Dung Bomb" },
    { 16, "AILMENT_STEELFANG", "钢龙毒", "Steel Fang" },
}};

constexpr bool riseAilmentLabelsCarryBothColumns()
{
    for (const RiseAilmentLabel &label : kRiseAilmentNames) {
        if (label.nameKey == nullptr || label.nameKey[0] == '\0'
            || label.name == nullptr || label.name[0] == '\0'
            || label.nameEn == nullptr || label.nameEn[0] == '\0')
            return false;
    }
    return true;
}
static_assert(riseAilmentLabelsCarryBothColumns(),
              "every Rise ailment label carries a zh and an en string");

const RiseAilmentLabel *riseAilmentLabel(int slotId)
{
    std::size_t lo = 0;
    std::size_t hi = kRiseAilmentNames.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (kRiseAilmentNames[mid].slotId < slotId)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < kRiseAilmentNames.size() && kRiseAilmentNames[lo].slotId == slotId)
        return &kRiseAilmentNames[lo];
    return nullptr;
}

} // namespace

// Locale-aware display name of one abnormality schema (v0.9 i18n, WS-B):
// the en-us.xml column while mhw::StringTable::instance().isEnglish() is
// true, the zh-cn.xml column otherwise. An empty locale reads as Chinese, so
// the pre-i18n behaviour is the default.
QString riseAbnormalityDisplayName(const RiseAbnormalitySchema &schema)
{
    return QString::fromUtf8(StringTable::instance().isEnglish() ? schema.nameEn
                                                                 : schema.name);
}

// English Rise monster-ailment name for a slot id (0..16); nullptr when the
// slot is outside the table. Locale-independent — see riseAilmentDisplayName()
// for the locale-aware pick.
const char *riseAilmentNameEn(int slotId)
{
    const RiseAilmentLabel *label = riseAilmentLabel(slotId);
    return label == nullptr ? nullptr : label->nameEn;
}

// Locale-aware Rise monster-ailment name. Unknown slots keep the reader's
// historical fallback text, translated: "异常N" / "Ailment N".
QString riseAilmentDisplayName(int slotId)
{
    const bool english = StringTable::instance().isEnglish();
    if (const RiseAilmentLabel *label = riseAilmentLabel(slotId))
        return QString::fromUtf8(english ? label->nameEn : label->name);
    return english ? QStringLiteral("Ailment %1").arg(slotId)
                   : QStringLiteral("异常%1").arg(slotId);
}

} // namespace mhw
