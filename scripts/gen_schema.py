#!/usr/bin/env python3
"""Generate the kPartSchemas table for mhw_reader.cpp.

Reads HunterPie's MonsterData.xml + zh-cn.xml, emits a C++ fragment that
preserves each monster's part list (severable and breakable) so that
readMonsters() can pick the right memory table per part.

Usage:
    scripts/gen_schema.py                                  # default paths
    scripts/gen_schema.py path/to/MonsterData.xml zh-cn.xml > frag
    scripts/gen_schema.py > src/mhw_reader.schema.fragment # pipe into cpp

This requires HunterPie cloned locally — the default paths match the
mhw-linux-overlay skill layout at /tmp/HunterPie. To regenerate after a
HunterPie update:

    git -C /tmp/HunterPie pull
    python3 scripts/gen_schema.py \
        /tmp/HunterPie/HunterPie/Game/World/Data/MonsterData.xml \
        /tmp/HunterPie/Localization/localization/zh-cn.xml \
        > /tmp/schema.fragment

Then paste the fragment between BEGIN/END AUTO-GENERATED kPartSchemas
markers in src/mhw_reader.cpp.
"""
import sys

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

MONSTER_DATA = Path(sys.argv[1] if len(sys.argv) > 1
                    else "/tmp/HunterPie/HunterPie/Game/World/Data/MonsterData.xml")
ZH_CN = Path(sys.argv[2] if len(sys.argv) > 2
             else "/tmp/HunterPie/Localization/localization/zh-cn.xml")

root = ET.parse(ZH_CN).getroot()
part_name = {p.attrib["Id"]: p.attrib["String"]
             for p in root.findall("Part") if "Id" in p.attrib and "String" in p.attrib}

ALIAS = {
    "PART_L_LEG": "左腿", "PART_R_LEG": "右腿", "PART_L_LEGS": "左腿", "PART_R_LEGS": "右腿",
    "PART_L_ARM": "左臂", "PART_R_ARM": "右臂",
    "PART_L_WING": "左翼", "PART_R_WING": "右翼",
    "PART_L_CUTWING": "左翼", "PART_R_CUTWING": "右翼",
    "PART_BODY": "身体", "PART_TORSO": "身体",
    "PART_TAIL": "尾巴", "PART_HEAD": "头部", "PART_NECK": "颈部", "PART_BACK": "背部",
    "PART_LEGS": "腿", "PART_FORELEGS": "腿", "PART_ARMS": "手臂", "PART_WINGS": "翼",
    "PART_LOWER_BODY": "下躯干", "PART_UPPER_BODY": "上躯干", "PART_REAR": "臀部",
    "PART_UPPER_BACK": "上背部", "PART_LOWER_BACK": "下背部", "PART_DORSAL_FIN": "背鳍",
    "PART_H_LEGS": "后腿", "PART_SHELL": "壳",
    "PART_HORN": "角", "PART_HORNS": "角", "PART_THROAT": "喉咙",
    "PART_CHARGE": "充能角", "PART_HORN_CHARGE": "充能角",
    "PART_MUD_BALL": "泥球", "PART_REAR_POWER_UNIT": "尾部动力单元",
}

def cn(part_string):
    return ALIAS.get(part_string, part_name.get(part_string, part_string))


md = ET.parse(MONSTER_DATA).getroot()
monsters = {}
captures = {}
for mon in md.findall("./Monsters/Monster"):
    mid = int(mon.attrib["Id"])
    # Capture: Capcom-defined per-monster capture HP threshold (%).
    # HunterPie exposes it via MonsterData.xml; we mirror it so the
    # monster HP bar can switch to the "capturable" colour per game
    # mechanics (HR default 25, MR equivalent 10–15, 0 = uncapturable).
    cap_raw = mon.attrib.get("Capture", "0")
    captures[mid] = int(cap_raw)
    parts_block = mon.find("Parts")
    if parts_block is None:
        continue
    parts = []
    for p in parts_block.findall("Part"):
        breaks = [int(b.attrib["Threshold"]) for b in p.findall("Break") if "Threshold" in b.attrib]
        parts.append({
            "id": int(p.attrib["Id"]),
            "string": p.attrib.get("String", ""),
            "name": cn(p.attrib.get("String", "")),
            "is_severable": p.attrib.get("IsSeverable", "False").lower() == "true",
            "thresholds": ",".join(str(b) for b in breaks),
        })
    monsters[mid] = parts


# Capture-threshold table — used by MonsterPanel::paintPanel to switch the
# HP fill colour to "capturable" (red) at the right percentage.
print(f"const QHash<int, int> kMonsterCaptureThresholds = {{")
for mid in sorted(captures):
    print(f"    {{ {mid}, {captures[mid]} }},")
print("};")


print("// Auto-generated from MonsterData.xml + zh-cn.xml (HunterPie v2.14.0.461)")
print(f"// {len(monsters)} monsters (severable parts included)")
print("// Each entry preserves MonsterData.xml\'s per-monster local Id; severable")
print("// parts map to the 0x1D058+0x1FC8 table (stride 0x78) and normal parts")
print("// map to 0x1D058+0x40 (stride 0x1F8).")
for mid in sorted(monsters):
    parts = monsters[mid]
    print(f"    {{{mid}, {{")
    for p in parts:
        sev = "true" if p["is_severable"] else "false"
        name = p["name"].replace(chr(34), chr(92)+chr(34))
        print(f'        {{ {p["id"]}, {sev}, "{name}", "{p["thresholds"]}" }},')
    print("    }},")
