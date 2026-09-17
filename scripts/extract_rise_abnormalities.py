#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# Generate src/rise/mhr_abnormalities.cpp — the Rise player-abnormality
# schema table + the Rise monster-ailment slot labels — in BOTH locales.
#
# Adapted (WS-B, i18n) from the v0.8.4-r18 single-locale generator
# `v0.8.4-r18/abnormalities/tools/extract_rise_abnormalities.py`:
#   * takes --en-xml AND --zh-xml and sha256-verifies AbnormalityData.xml plus
#     both locale files (fails on mismatch unless --allow-unverified);
#   * emits the `nameEn` column next to the existing zh `name` column;
#   * adds the Rise ailment slot label table (MonsterData.xml <Ailments> slot
#     ids -> AILMENT_* keys -> both locales) and the locale-aware resolvers
#     riseAbnormalityDisplayName() / riseAilmentDisplayName().
#
# Inputs (read-only):
#   HunterPie/Game/Rise/Data/AbnormalityData.xml
#       the AbnormalityDefinition source AbnormalityRepository.Load() parses
#   HunterPie/Game/Rise/Data/MonsterData.xml
#       <Ailments> slot ids (Id attribute 0..16) -> AILMENT_* keys
#   HunterPie.Integrations/.../MonsterHunterRise/Entity/Enums/Player*
#       CommonConditions / DebuffConditions / ActionFlags bit values
#   zh-cn.xml / en-us.xml (HunterPie official localization)
#       <Abnormalities>/Abnormality[@Id = schema.Name] display names, and
#       <Ailments><Rise>/Ailment[@Id] for the monster-ailment slot labels
#   src/monster/part_schemas.cpp (--part-schemas)
#       the pre-i18n kRiseAilmentNames zh labels, copied verbatim so the
#       Chinese side of the overlay cannot change
#
# Only entries whose Category (defaulting to the group name, see
# XmlNodeToAbnormalityDefinitionMapper) is "Consumables" or "Debuffs" are
# emitted — the two lists MHRPlayer.GetConsumableAbnormalities /
# GetPlayerDebuffAbnormalities iterate.
#
# Usage:
#   python3 scripts/extract_rise_abnormalities.py \
#       --abnormality-data HunterPie/Game/Rise/Data/AbnormalityData.xml \
#       --monster-data HunterPie/Game/Rise/Data/MonsterData.xml \
#       --enums-dir HunterPie.Integrations/Datasources/MonsterHunterRise/Entity/Enums \
#       --en-xml hp-loc/en-us.xml --zh-xml hp-loc/zh-cn.xml \
#       --part-schemas src/monster/part_schemas.cpp \
#       --cpp-out src/rise/mhr_abnormalities.cpp [--check] [--allow-unverified]

from __future__ import annotations

import argparse
import hashlib
import re
import xml.etree.ElementTree as ET
from pathlib import Path

# sha256 of the inputs this table was extracted from (2026-09-18).
EXPECTED_SHA256 = {
    "AbnormalityData.xml": "537298c8322d665a0feed84e6c9871b7df936f60286b844ab24e9bbc852cda65",
    "zh-cn.xml": "2a4b1bb318fc21a55c0b5b978e2d33cb8c2ea74457b34fcac88b9236c19a06db",
    "en-us.xml": "661d58b54bd1214ae0e9eff92fb4167b8aab54df0452eea238cb9574300030ea",
}

CATEGORIES = ("Consumables", "Debuffs")
EXPECTED_CONSUMABLES = 69
EXPECTED_DEBUFFS = 28


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(path: Path, expected: str, allow_unverified: bool) -> str:
    digest = sha256(path)
    if digest != expected and not allow_unverified:
        raise SystemExit(
            f"{path}: sha256 {digest} does not match the recorded "
            f"{expected}; pass --allow-unverified to override")
    return digest


def parse_flag_enum(path: Path) -> dict[str, int]:
    """Bit values of a C# [Flags] enum (the 'Name = 1 << N' / 'Name = 0'
    forms the Rise condition enums use). Anything else fails loudly so a
    new enum shape can never be silently mis-parsed."""
    text = open(path, encoding="utf-8-sig").read()
    values: dict[str, int] = {}
    for line in text.splitlines():
        if line.lstrip().startswith("//"):
            continue
        match = re.match(r"^\s*(\w+)\s*=\s*([^,]+?)\s*,?\s*$", line)
        if match is None:
            continue
        name, rhs = match.group(1), match.group(2)
        if re.fullmatch(r"\d+", rhs):
            values[name] = int(rhs)   # e.g. "None = 0", "AttackUp = 1"
            continue
        shift = re.fullmatch(r"1(?:UL|LU|L|U)?\s*<<\s*(\d+)", rhs)
        if shift is None:
            raise SystemExit(f"{path}: cannot parse enum member {name} = {rhs!r}")
        values[name] = 1 << int(shift.group(1))
    return values


def build_id(raw_id: str, group: str) -> str:
    """XmlNodeToAbnormalityDefinitionMapper.BuildId."""
    return raw_id if raw_id.startswith("ABN_") else f"{group}_{raw_id}"


def parse_abnormality_names(path: Path, tag: str) -> dict[str, str]:
    """First match wins (HunterPie FindStringBy -> XmlNode.SelectSingleNode)."""
    root = ET.parse(path).getroot()
    names: dict[str, str] = {}
    for node in root.find("Abnormalities"):
        names.setdefault(node.attrib["Id"], node.attrib.get("String"))
    if not names:
        raise SystemExit(f"{path}: no <Abnormalities> entries ({tag})")
    return names


def parse_ailment_strings(path: Path, tag: str) -> dict[str, str]:
    """AILMENT_* key -> string, from /Strings/Ailments/Rise/Ailment."""
    root = ET.parse(path).getroot()
    names: dict[str, str] = {}
    for node in root.find("Ailments/Rise"):
        names.setdefault(node.attrib["Id"], node.attrib.get("String"))
    if not names:
        raise SystemExit(f"{path}: no <Ailments><Rise> entries ({tag})")
    return names


def parse_part_schema_ailments(path: Path) -> dict[int, str]:
    """The pre-i18n zh slot labels out of src/monster/part_schemas.cpp."""
    text = path.read_text(encoding="utf-8")
    block = re.search(r"kRiseAilmentNames\s*=\s*\{(.*?)\n\};", text, re.S)
    if block is None:
        raise SystemExit(f"{path}: kRiseAilmentNames table not found")
    rows = re.findall(r"\{\s*(\d+),\s*QStringLiteral\(\"([^\"]*)\"\)\s*\}", block.group(1))
    if not rows:
        raise SystemExit(f"{path}: kRiseAilmentNames table is empty")
    labels = {int(slot): value for slot, value in rows}
    if len(labels) != len(rows):
        raise SystemExit(f"{path}: duplicate slot id in kRiseAilmentNames")
    return labels


def c_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def hex_or_zero(value: str | None) -> int:
    if value is None:
        return 0
    try:
        return int(value, 16)
    except ValueError:
        return 0


def int_or_zero(value: str | None) -> int:
    if value is None:
        return 0
    try:
        return int(value)
    except ValueError:
        return 0


def flag_type_of(raw: str | None) -> str:
    # Enum.TryParse failure leaves the default value (None).
    return raw if raw in ("RiseCommon", "RiseDebuff", "RiseAction") else "None"


def collect(abnormality_data: Path, enum_dir: Path,
            zh_names: dict[str, str], en_names: dict[str, str]) -> dict[str, list[dict]]:
    flag_enums = {
        "RiseCommon": parse_flag_enum(enum_dir / "PlayerCommonConditions.cs"),
        "RiseDebuff": parse_flag_enum(enum_dir / "PlayerDebuffConditions.cs"),
        "RiseAction": parse_flag_enum(enum_dir / "PlayerActionFlags.cs"),
    }

    root = ET.parse(abnormality_data).getroot()
    rows: dict[str, list[dict]] = {"Consumables": [], "Debuffs": []}
    for group in root:
        for node in group:
            attrs = node.attrib
            category = attrs.get("Category", group.tag)
            if category not in CATEGORIES:
                continue
            flag_type = flag_type_of(attrs.get("FlagType"))
            flag_name = attrs.get("Flag", "") if flag_type != "None" else ""
            flag_bit = flag_enums.get(flag_type, {}).get(flag_name, 0)
            if flag_type != "None" and flag_name and flag_bit == 0:
                raise SystemExit(
                    f"flag {flag_name} of {attrs['Id']} not found in {flag_type}")
            name_key = attrs.get("Name", "ABNORMALITY_UNKNOWN")
            if name_key not in zh_names:
                raise SystemExit(f"zh-cn.xml has no name for {name_key}")
            if name_key not in en_names:
                raise SystemExit(f"en-us.xml has no name for {name_key}")
            rows[category].append({
                "id": build_id(attrs["Id"], group.tag),
                "nameKey": name_key,
                "name": zh_names[name_key],
                "nameEn": en_names[name_key],
                "group": group.tag,
                "flagName": flag_name,
                "flagType": flag_type,
                "kind": "Buff" if category == "Consumables" else "Debuff",
                "flag": flag_bit,
                "offset": hex_or_zero(attrs.get("Offset")),
                "dependsOn": hex_or_zero(attrs.get("DependsOn")),
                "withValue": int_or_zero(attrs.get("WithValue")),
                "maxBuildup": int_or_zero(attrs.get("MaxBuildup")),
                "maxTimer": int_or_zero(attrs.get("MaxTimer")),
                "isInfinite": attrs.get("IsInfinite", "False") == "True",
                "isInteger": attrs.get("IsInteger", "False") == "True",
                "isBuildup": attrs.get("IsBuildup", "False") == "True",
            })
    if len(rows["Consumables"]) != EXPECTED_CONSUMABLES \
            or len(rows["Debuffs"]) != EXPECTED_DEBUFFS:
        raise SystemExit(
            "unexpected category counts: "
            f"{len(rows['Consumables'])} consumables / {len(rows['Debuffs'])} debuffs "
            f"(expected {EXPECTED_CONSUMABLES} / {EXPECTED_DEBUFFS})")
    return rows


def collect_ailment_labels(monster_data: Path, zh_labels: dict[int, str],
                           zh_strings: dict[str, str],
                           en_strings: dict[str, str]) -> list[tuple[int, str, str, str]]:
    root = ET.parse(monster_data).getroot()
    labels: list[tuple[int, str, str, str]] = []
    for node in root.find("Ailments"):
        slot = int(node.attrib["Id"])
        if slot < 0:
            continue  # AILMENT_UNKNOWN — upstream's "no ailment" sentinel
        key = node.attrib["String"]
        if key not in en_strings:
            raise SystemExit(f"en-us.xml has no Ailments/Rise entry for {key}")
        if key not in zh_strings:
            raise SystemExit(f"zh-cn.xml has no Ailments/Rise entry for {key}")
        if slot not in zh_labels:
            raise SystemExit(
                f"src/monster/part_schemas.cpp kRiseAilmentNames has no slot {slot} ({key})")
        labels.append((slot, key, zh_labels[slot], en_strings[key]))
    labels.sort()
    if [slot for slot, _, _, _ in labels] != sorted(zh_labels):
        raise SystemExit(
            "MonsterData.xml <Ailments> slots and kRiseAilmentNames disagree: "
            f"xml {[slot for slot, _, _, _ in labels]} vs partschemas {sorted(zh_labels)}")
    return labels


def emit_row(row: dict) -> str:
    notes = []
    if row["flagType"] != "None":
        notes.append(f'{row["flagType"]}:{row["flagName"]}')
    if row["isInfinite"]:
        notes.append("infinite")
    if row["isBuildup"]:
        notes.append(f'buildup max={row["maxBuildup"]}')
    if row["maxTimer"]:
        notes.append(f'maxTimer={row["maxTimer"]}')
    note = ("  // " + ", ".join(notes)) if notes else ""
    return (
        f'    {{ {c_string(row["id"])}, {c_string(row["nameKey"])},\n'
        f'      {c_string(row["name"])}, {c_string(row["nameEn"])}, '
        f'{c_string(row["group"])},\n'
        f'      {c_string(row["flagName"])}, RiseAbnormalityFlagType::{row["flagType"]},\n'
        f'      RiseAbnormalityKind::{row["kind"]},\n'
        f'      0x{row["flag"]:016X}ULL, 0x{row["offset"]:03X}, 0x{row["dependsOn"]:03X},\n'
        f'      {row["withValue"]}, {row["maxBuildup"]}, {row["maxTimer"]}.0F,\n'
        f'      {"true" if row["isInfinite"] else "false"}, '
        f'{"true" if row["isInteger"] else "false"}, '
        f'{"true" if row["isBuildup"] else "false"} }},{note}\n')


def emit_ailment_table(labels: list[tuple[int, str, str, str]]) -> str:
    rows = "\n".join(
        f'    {{ {slot:2d}, {c_string(key)}, {c_string(zh)}, {c_string(en)} }},'
        for slot, key, zh, en in labels)
    return f'''
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
struct RiseAilmentLabel {{
    int slotId;           // <Ailment Id> in MonsterData.xml (0..16)
    const char *nameKey;  // AILMENT_* localization key
    const char *name;     // zh label shown by the overlay (pre-i18n value)
    const char *nameEn;   // en-us.xml Ailments/Rise value for nameKey
}};

// Sorted by slotId: riseAilmentLabel() binary-searches it.
constexpr std::array<RiseAilmentLabel, {len(labels)}> kRiseAilmentNames = {{{{
{rows}
}}}};

constexpr bool riseAilmentLabelsCarryBothColumns()
{{
    for (const RiseAilmentLabel &label : kRiseAilmentNames) {{
        if (label.nameKey == nullptr || label.nameKey[0] == '\\0'
            || label.name == nullptr || label.name[0] == '\\0'
            || label.nameEn == nullptr || label.nameEn[0] == '\\0')
            return false;
    }}
    return true;
}}
static_assert(riseAilmentLabelsCarryBothColumns(),
              "every Rise ailment label carries a zh and an en string");

const RiseAilmentLabel *riseAilmentLabel(int slotId)
{{
    std::size_t lo = 0;
    std::size_t hi = kRiseAilmentNames.size();
    while (lo < hi) {{
        const std::size_t mid = lo + (hi - lo) / 2;
        if (kRiseAilmentNames[mid].slotId < slotId)
            lo = mid + 1;
        else
            hi = mid;
    }}
    if (lo < kRiseAilmentNames.size() && kRiseAilmentNames[lo].slotId == slotId)
        return &kRiseAilmentNames[lo];
    return nullptr;
}}

}} // namespace
'''


def emit_tail() -> str:
    return '''
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
'''


def generate(abnormality_data: Path, enum_dir: Path, monster_data: Path,
             zh_xml: Path, en_xml: Path, part_schemas: Path,
             allow_unverified: bool) -> str:
    zh_names = parse_abnormality_names(zh_xml, "zh")
    en_names = parse_abnormality_names(en_xml, "en")
    rows = collect(abnormality_data, enum_dir, zh_names, en_names)
    consumables, debuffs = rows["Consumables"], rows["Debuffs"]
    labels = collect_ailment_labels(
        monster_data, parse_part_schema_ailments(part_schemas),
        parse_ailment_strings(zh_xml, "zh"), parse_ailment_strings(en_xml, "en"))

    out: list[str] = []
    out.append("// SPDX-License-Identifier: Apache-2.0\n//\n")
    out.append("// AUTO-GENERATED — do not edit by hand.\n")
    out.append("// Regenerate with scripts/extract_rise_abnormalities.py\n//\n")
    out.append("// Source: HunterPie v2\n")
    out.append("//   HunterPie/Game/Rise/Data/AbnormalityData.xml\n")
    out.append(f"//     sha256 {sha256(abnormality_data)}\n")
    out.append("//     <Abnormality> nodes whose Category attribute (defaulting to\n")
    out.append("//     the parent group) is \"Consumables\" or \"Debuffs\" — the two\n")
    out.append("//     lists MHRPlayer.GetConsumableAbnormalities / …Debuff…\n")
    out.append("//     iterate via AbnormalityRepository.FindAllAbnormalitiesBy.\n")
    out.append("//     Hunting-horn songs carry Category=\"Songs\" and are absent\n")
    out.append("//     by construction (MHRPlayer.GetSongs uses HH_ABNORMALITIES_OFFSETS).\n")
    out.append("//   Flag bits: MHRAbnormalityFlagTypeParser → PlayerCommonConditions /\n")
    out.append("//     PlayerDebuffConditions / PlayerActionFlags ([Flags] enums, hashed\n")
    out.append("//     below; the generator fails if a Flag member no longer parses).\n")
    for name in ("PlayerCommonConditions.cs", "PlayerDebuffConditions.cs",
                 "PlayerActionFlags.cs"):
        out.append(f"//       {name} sha256 {sha256(enum_dir / name)}\n")
    out.append("//   Names (both locales, verbatim):\n")
    out.append("//     zh column: zh-cn.xml <Abnormalities>/Abnormality[@Id = Name]\n")
    out.append(f"//       sha256 {sha256(zh_xml)}\n")
    out.append("//     en column: en-us.xml <Abnormalities>/Abnormality[@Id = Name]\n")
    out.append(f"//       sha256 {sha256(en_xml)}\n")
    out.append("//     (first match wins: HunterPie's FindStringBy uses SelectSingleNode;\n")
    out.append("//      the two locale files carry the same Id set — the generator\n")
    out.append("//      refuses to run otherwise.)\n")
    out.append("//   Ailment slot labels: HunterPie/Game/Rise/Data/MonsterData.xml\n")
    out.append("//     <Ailments> (slot -> AILMENT_* key) joined to the zh labels of\n")
    out.append("//     src/monster/part_schemas.cpp kRiseAilmentNames and the en-us.xml\n")
    out.append("//     <Ailments><Rise> strings; see the kRiseAilmentNames comment below.\n")
    out.append(f"//\n// {len(consumables)} consumables + {len(debuffs)} debuffs, in XML document\n")
    out.append("// order. Field parsing mirrors XmlNodeToAbnormalityDefinitionMapper\n")
    out.append("// (Offset/DependsOn hex, WithValue/MaxTimer/MaxBuildup decimal).\n")
    out.append("//\n")
    out.append("// Locale selection (v0.9 i18n, WS-B): riseAbnormalityDisplayName() and\n")
    out.append("// riseAilmentDisplayName() return the en column while\n")
    out.append("// mhw::StringTable::instance().isEnglish() is true and the zh column\n")
    out.append("// otherwise; an empty locale (before any load()) reads as Chinese, so\n")
    out.append("// the pre-i18n behaviour is the default.\n")
    out.append('\n#include "rise/mhr_abnormalities.h"\n\n')
    out.append('#include "core/string_table.h"\n\n')
    out.append("#include <array>\n#include <cstddef>\n#include <cstring>\n")
    out.append("\nnamespace mhw {\nnamespace {\n\n")

    out.append(f"constexpr RiseAbnormalitySchema kConsumables[{len(consumables)}] = {{\n")
    for row in consumables:
        out.append(emit_row(row))
    out.append("};\n\n")

    out.append(f"constexpr RiseAbnormalitySchema kDebuffs[{len(debuffs)}] = {{\n")
    for row in debuffs:
        out.append(emit_row(row))
    out.append("};\n\n")

    out.append("} // namespace\n\n")

    out.append("const RiseAbnormalitySchema *riseConsumableAbnormalities(std::size_t &count)\n{\n")
    out.append("    count = std::size(kConsumables);\n")
    out.append("    return kConsumables;\n}\n\n")
    out.append("const RiseAbnormalitySchema *riseDebuffAbnormalities(std::size_t &count)\n{\n")
    out.append("    count = std::size(kDebuffs);\n")
    out.append("    return kDebuffs;\n}\n")

    out.append("""
const RiseAbnormalitySchema *riseFindAbnormality(const char *id)
{
    if (id == nullptr || *id == '\\0')
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
        return QStringLiteral("\\u221E"); // ∞
    if (buildup) {
        const int value = static_cast<int>(timer);
        return maxBuildup > 0.0F
            ? QStringLiteral("%1/%2").arg(value).arg(static_cast<int>(maxBuildup))
            : QString::number(value);
    }
    return QStringLiteral("%1s").arg(static_cast<int>(timer));
}
""")

    out.append("\nnamespace {\n")
    out.append(emit_ailment_table(labels))
    out.append(emit_tail())
    out.append("\n} // namespace mhw\n")
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--abnormality-data", type=Path, required=True)
    ap.add_argument("--monster-data", type=Path, required=True)
    ap.add_argument("--enums-dir", type=Path, required=True)
    ap.add_argument("--en-xml", type=Path, required=True)
    ap.add_argument("--zh-xml", type=Path, required=True)
    ap.add_argument("--part-schemas", type=Path, required=True)
    ap.add_argument("--cpp-out", type=Path)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--allow-unverified", action="store_true")
    args = ap.parse_args()

    verify(args.abnormality_data, EXPECTED_SHA256["AbnormalityData.xml"],
           args.allow_unverified)
    verify(args.zh_xml, EXPECTED_SHA256["zh-cn.xml"], args.allow_unverified)
    verify(args.en_xml, EXPECTED_SHA256["en-us.xml"], args.allow_unverified)

    text = generate(args.abnormality_data, args.enums_dir, args.monster_data,
                    args.zh_xml, args.en_xml, args.part_schemas,
                    args.allow_unverified)

    if args.check:
        current = args.cpp_out.read_text(encoding="utf-8") if args.cpp_out else ""
        if current != text:
            print(f"OUT OF DATE: {args.cpp_out}")
            return 1
        print(f"up to date: {args.cpp_out}")
        return 0

    if args.cpp_out:
        args.cpp_out.write_text(text, encoding="utf-8")
    print(f"generated {text.count(chr(10)) + 1} lines -> {args.cpp_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
