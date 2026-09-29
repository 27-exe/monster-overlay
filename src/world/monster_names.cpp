// SPDX-License-Identifier: Apache-2.0
//
// World monster display names. This file holds the two id-key -> name tables
// and the one lookup that reads them, i.e. the DATA plus the six lines of
// LOGIC that consume it, in a single translation unit.
//
// Contents:
//   1. kWorldMonsterNames      zh column, 72 entries
//   2. kWorldMonsterNamesEn    en column, same 72 keys
//   3. monsterDisplayName()    picks the column, falls back to the raw key
//
// ---------------------------------------------------------------------------
// Why this is ONE file rather than the two Rise uses
// ---------------------------------------------------------------------------
// Rise keeps its equivalent tables in src/rise/data/mhr_monster_names_data.cpp
// and the lookup in src/rise/mhr_monster_names.cpp. That split is a
// REGENERATION boundary, not a style choice: scripts/extract_rise_monster_names.py
// rewrites ONLY the data file, sha256-verifying both HunterPie locale XMLs on the
// way, and `--check` re-verifies the row count against the header. A
// `--data-out`-only generator is the reason a separate data TU earns its keep.
//
// The World tables below have no such generator — nothing under scripts/
// mentions them (the four generator scripts are extract_rise_abnormalities,
// extract_rise_monster_names, generate_rise_part_names and gen_schema, all
// Rise or per-part). The zh column is the frozen pre-i18n literal that shipped
// in v0.8; the en column was transcribed by hand from HunterPie's en-us.xml
// during the v0.9 i18n work (WS-A). Copying Rise's two-file + extern +
// static_assert + --check machinery here would only add a hand-maintained
// declaration with nothing to regenerate it, so the honest equivalent of
// "align with Rise" is the property the split actually buys, not its shape:
//
//   the data lives at namespace scope in its own TU, not inside a function
//   body, so it is visible to the whole compile (and to any reader of the
//   file) instead of being reachable only as the lookup's private table.
//
// That property is what was missing: both tables used to be function-local
// `static const QHash` inside monster_reader.cpp's monsterDisplayName(), which
// is why the old WS-A comment had to complain "Kept at namespace scope so the
// reader tests can assert both columns" — the LOOKUP was hoisted, the DATA
// stayed trapped. monster_reader.cpp now carries reader logic only.
//
// ---------------------------------------------------------------------------
// Initialisation timing (the one thing that changes by moving them)
// ---------------------------------------------------------------------------
// A function-local `static` is a C++11 magic static: lazily constructed, on
// first call, thread-safe. At namespace scope here the two QHashes are
// constructed during static initialisation, before main(). Every real call
// site runs inside MhwReader::poll(), long after main() has started, so
// construction still precedes every use; the net effect is 144 heap
// allocations moving from "first frame with a monster on screen" to "process
// start", which is invisible next to the Qt/plugin initialisation already
// happening there. They keep internal linkage (`static`), so this TU exports
// nothing new: only monsterDisplayName()'s pre-existing declaration in
// monster/monster_types.h.
//
// ---------------------------------------------------------------------------
// Provenance (both columns are HunterPie/Localization)
// ---------------------------------------------------------------------------
//   https://github.com/HunterPie/Localization
// See the per-table comments below. Locale selection is
// mhw::StringTable::instance().isEnglish(); an unloaded locale reads as
// Chinese, which is exactly the pre-i18n behaviour, so the tables cannot
// change what a not-yet-loaded overlay renders.

#include "core/string_table.h"
#include "monster/monster_types.h"

#include <QHash>
#include <QString>

namespace mhw {

// HunterPie 421810 zh-cn.xml Id -> name. The 421810 build reads
// monster Id at monster+0x12280 and HunterPie's zh-cn.xml maps
// it to a Chinese name. Submodule:
//   https://github.com/HunterPie/Localization
// Note: the Id differs from the em* string (e.g. em057=雷狼龙
// in this build; Id 94=雷狼龙 in this table; in older builds
// 76=雷狼龙). em* is the source of truth, Id is just a
// cross-check.
static const QHash<QString, QString> kWorldMonsterNames = {
    // 72 entries from HunterPie/Localization zh-cn.xml (World section)
    // Source: https://github.com/HunterPie/Localization
    {QStringLiteral("000"), QStringLiteral("蛮颚龙")},
    {QStringLiteral("001"), QStringLiteral("火龙")},
    {QStringLiteral("004"), QStringLiteral("熔山龙")},
    {QStringLiteral("007"), QStringLiteral("大贼龙")},
    {QStringLiteral("009"), QStringLiteral("雌火龙")},
    {QStringLiteral("010"), QStringLiteral("樱火龙")},
    {QStringLiteral("011"), QStringLiteral("苍火龙")},
    {QStringLiteral("012"), QStringLiteral("角龙")},
    {QStringLiteral("013"), QStringLiteral("黑角龙")},
    {QStringLiteral("014"), QStringLiteral("麒麟")},
    {QStringLiteral("015"), QStringLiteral("贝希摩斯")},
    {QStringLiteral("016"), QStringLiteral("钢龙")},
    {QStringLiteral("017"), QStringLiteral("炎妃龙")},
    {QStringLiteral("018"), QStringLiteral("炎王龙")},
    {QStringLiteral("019"), QStringLiteral("熔岩龙")},
    {QStringLiteral("020"), QStringLiteral("恐暴龙")},
    {QStringLiteral("021"), QStringLiteral("土砂龙")},
    {QStringLiteral("022"), QStringLiteral("爆锤龙")},
    {QStringLiteral("023"), QStringLiteral("鹿首精")},
    {QStringLiteral("024"), QStringLiteral("毒妖鸟")},
    {QStringLiteral("025"), QStringLiteral("灭尽龙")},
    {QStringLiteral("026"), QStringLiteral("冥灯龙")},
    {QStringLiteral("027"), QStringLiteral("搔鸟")},
    {QStringLiteral("028"), QStringLiteral("眩鸟")},
    {QStringLiteral("029"), QStringLiteral("泥鱼龙")},
    {QStringLiteral("030"), QStringLiteral("飞雷龙")},
    {QStringLiteral("031"), QStringLiteral("浮空龙")},
    {QStringLiteral("032"), QStringLiteral("风漂龙")},
    {QStringLiteral("033"), QStringLiteral("大痹贼龙")},
    {QStringLiteral("034"), QStringLiteral("惨爪龙")},
    {QStringLiteral("035"), QStringLiteral("骨锤龙")},
    {QStringLiteral("036"), QStringLiteral("尸套龙")},
    {QStringLiteral("037"), QStringLiteral("岩贼龙")},
    {QStringLiteral("038"), QStringLiteral("绚辉龙")},
    {QStringLiteral("039"), QStringLiteral("爆鳞龙")},
    {QStringLiteral("051"), QStringLiteral("古代鹿首精")},
    {QStringLiteral("061"), QStringLiteral("轰龙")},
    {QStringLiteral("062"), QStringLiteral("迅龙")},
    {QStringLiteral("063"), QStringLiteral("冰牙龙")},
    {QStringLiteral("064"), QStringLiteral("惶怒恐暴龙")},
    {QStringLiteral("065"), QStringLiteral("碎龙")},
    {QStringLiteral("066"), QStringLiteral("斩龙")},
    {QStringLiteral("067"), QStringLiteral("硫斩龙")},
    {QStringLiteral("068"), QStringLiteral("雷颚龙")},
    {QStringLiteral("069"), QStringLiteral("水妖鸟")},
    {QStringLiteral("070"), QStringLiteral("歼世灭尽龙")},
    {QStringLiteral("071"), QStringLiteral("痹毒龙")},
    {QStringLiteral("072"), QStringLiteral("浮眠龙")},
    {QStringLiteral("073"), QStringLiteral("霜翼风漂龙")},
    {QStringLiteral("074"), QStringLiteral("凶爪龙")},
    {QStringLiteral("075"), QStringLiteral("雾瘴尸套龙")},
    {QStringLiteral("076"), QStringLiteral("红莲爆鳞龙")},
    {QStringLiteral("077"), QStringLiteral("冰鱼龙")},
    {QStringLiteral("078"), QStringLiteral("猛牛龙")},
    {QStringLiteral("079"), QStringLiteral("冰呪龙")},
    {QStringLiteral("080"), QStringLiteral("溟波龙")},
    {QStringLiteral("081"), QStringLiteral("天地煌啼龙")},
    {QStringLiteral("087"), QStringLiteral("煌黑龙")},
    {QStringLiteral("088"), QStringLiteral("金火龙")},
    {QStringLiteral("089"), QStringLiteral("银火龙")},
    {QStringLiteral("090"), QStringLiteral("黑狼鸟")},
    {QStringLiteral("091"), QStringLiteral("金狮子")},
    {QStringLiteral("092"), QStringLiteral("激昂金狮子")},
    {QStringLiteral("093"), QStringLiteral("黑轰龙")},
    {QStringLiteral("094"), QStringLiteral("雷狼龙")},
    {QStringLiteral("095"), QStringLiteral("狱狼龙")},
    {QStringLiteral("096"), QStringLiteral("猛爆碎龙")},
    {QStringLiteral("097"), QStringLiteral("冥赤龙")},
    {QStringLiteral("098"), QStringLiteral("木人桩")},
    {QStringLiteral("099"), QStringLiteral("战痕黑狼鸟")},
    {QStringLiteral("100"), QStringLiteral("霜刃冰牙龙")},
    {QStringLiteral("101"), QStringLiteral("黑龙")},
};

// v0.9 i18n (WS-A): English column for the same 72 keys. Source:
// HunterPie Localization en-us.xml <Monsters><World><Monster Id=N
// String="..."/> (sha256 661d58b5...), key = the zero-padded Id read
// at monster+0x12280 — identical to the zh column's key scheme. The
// zh table above is untouched; isEnglish() picks the column.
static const QHash<QString, QString> kWorldMonsterNamesEn = {
    {QStringLiteral("000"), QStringLiteral("Anjanath")},
    {QStringLiteral("001"), QStringLiteral("Rathalos")},
    {QStringLiteral("004"), QStringLiteral("Zorah Magdaros")},
    {QStringLiteral("007"), QStringLiteral("Great Jagras")},
    {QStringLiteral("009"), QStringLiteral("Rathian")},
    {QStringLiteral("010"), QStringLiteral("Pink Rathian")},
    {QStringLiteral("011"), QStringLiteral("Azure Rathalos")},
    {QStringLiteral("012"), QStringLiteral("Diablos")},
    {QStringLiteral("013"), QStringLiteral("Black Diablos")},
    {QStringLiteral("014"), QStringLiteral("Kirin")},
    {QStringLiteral("015"), QStringLiteral("Behemoth")},
    {QStringLiteral("016"), QStringLiteral("Kushala Daora")},
    {QStringLiteral("017"), QStringLiteral("Lunastra")},
    {QStringLiteral("018"), QStringLiteral("Teostra")},
    {QStringLiteral("019"), QStringLiteral("Lavasioth")},
    {QStringLiteral("020"), QStringLiteral("Deviljho")},
    {QStringLiteral("021"), QStringLiteral("Barroth")},
    {QStringLiteral("022"), QStringLiteral("Uragaan")},
    {QStringLiteral("023"), QStringLiteral("Leshen")},
    {QStringLiteral("024"), QStringLiteral("Pukei-Pukei")},
    {QStringLiteral("025"), QStringLiteral("Nergigante")},
    {QStringLiteral("026"), QStringLiteral("Xeno'jiiva")},
    {QStringLiteral("027"), QStringLiteral("Kulu-Ya-Ku")},
    {QStringLiteral("028"), QStringLiteral("Tzitzi-Ya-Ku")},
    {QStringLiteral("029"), QStringLiteral("Jyuratodus")},
    {QStringLiteral("030"), QStringLiteral("Tobi-Kadachi")},
    {QStringLiteral("031"), QStringLiteral("Paolumu")},
    {QStringLiteral("032"), QStringLiteral("Legiana")},
    {QStringLiteral("033"), QStringLiteral("Great Girros")},
    {QStringLiteral("034"), QStringLiteral("Odogaron")},
    {QStringLiteral("035"), QStringLiteral("Radobaan")},
    {QStringLiteral("036"), QStringLiteral("Vaal Hazak")},
    {QStringLiteral("037"), QStringLiteral("Dodogama")},
    {QStringLiteral("038"), QStringLiteral("Kulve Taroth")},
    {QStringLiteral("039"), QStringLiteral("Bazelgeuse")},
    {QStringLiteral("051"), QStringLiteral("Ancient Leshen")},
    {QStringLiteral("061"), QStringLiteral("Tigrex")},
    {QStringLiteral("062"), QStringLiteral("Nargacuga")},
    {QStringLiteral("063"), QStringLiteral("Barioth")},
    {QStringLiteral("064"), QStringLiteral("Savage Deviljho")},
    {QStringLiteral("065"), QStringLiteral("Brachydios")},
    {QStringLiteral("066"), QStringLiteral("Glavenus")},
    {QStringLiteral("067"), QStringLiteral("Acidic Glavenus")},
    {QStringLiteral("068"), QStringLiteral("Fulgur Anjanath")},
    {QStringLiteral("069"), QStringLiteral("Coral Pukei-Pukei")},
    {QStringLiteral("070"), QStringLiteral("Ruiner Nergigante")},
    {QStringLiteral("071"), QStringLiteral("Viper Tobi-Kadachi")},
    {QStringLiteral("072"), QStringLiteral("Nightshade Paolumu")},
    {QStringLiteral("073"), QStringLiteral("Shrieking Legiana")},
    {QStringLiteral("074"), QStringLiteral("Ebony Odogaron")},
    {QStringLiteral("075"), QStringLiteral("Blackveil Vaal Hazak")},
    {QStringLiteral("076"), QStringLiteral("Seething Bazelgeuse")},
    {QStringLiteral("077"), QStringLiteral("Beotodus")},
    {QStringLiteral("078"), QStringLiteral("Banbaro")},
    {QStringLiteral("079"), QStringLiteral("Velkhana")},
    {QStringLiteral("080"), QStringLiteral("Namielle")},
    {QStringLiteral("081"), QStringLiteral("Shara Ishvalda")},
    {QStringLiteral("087"), QStringLiteral("Alatreon")},
    {QStringLiteral("088"), QStringLiteral("Gold Rathian")},
    {QStringLiteral("089"), QStringLiteral("Silver Rathalos")},
    {QStringLiteral("090"), QStringLiteral("Yian Garuga")},
    {QStringLiteral("091"), QStringLiteral("Rajang")},
    {QStringLiteral("092"), QStringLiteral("Furious Rajang")},
    {QStringLiteral("093"), QStringLiteral("Brute Tigrex")},
    {QStringLiteral("094"), QStringLiteral("Zinogre")},
    {QStringLiteral("095"), QStringLiteral("Stygian Zinogre")},
    {QStringLiteral("096"), QStringLiteral("Raging Brachydios")},
    {QStringLiteral("097"), QStringLiteral("Safi'jiiva")},
    {QStringLiteral("098"), QStringLiteral("Training Dummy")},
    {QStringLiteral("099"), QStringLiteral("Scarred Yian Garuga")},
    {QStringLiteral("100"), QStringLiteral("Frostfang Barioth")},
    {QStringLiteral("101"), QStringLiteral("Fatalis")},
};

// v0.9 i18n (WS-A): World monster display name for the zero-padded
// 3-digit key the reader builds from monster+0x12280 ("000".."101").
// Both columns share the same 72 keys: the zh table is the frozen
// pre-i18n column, the en table is HunterPie's official en-us.xml
// <Monsters><World> column. An unknown key is returned unchanged, exactly
// as the pre-i18n lookup left the key string in place.
QString monsterDisplayName(const QString &idKey)
{
    const QHash<QString, QString> &table = StringTable::instance().isEnglish()
        ? kWorldMonsterNamesEn
        : kWorldMonsterNames;
    const auto it = table.find(idKey);
    return it == table.end() ? idKey : *it;
}

} // namespace mhw
