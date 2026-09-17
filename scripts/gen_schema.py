#!/usr/bin/env python3
"""Regenerate the kPartSchemas table in src/monster/part_schemas.cpp.

Columns produced per part, per monster:

    { <id>, <isSeverable>, "<zh name>", "<en name>", <statePart>,
      "<break thresholds>", {<tenderizeIds>..., 0xFFFFFFFFu}, <tenderizeCount>u },

* structural columns (id / IsSeverable / Break thresholds / TenderizeIds)
  come from HunterPie  HunterPie/Game/World/Data/MonsterData.xml
  <Monsters><Monster><Parts><Part>;
* the zh name column is FROZEN for normal rows: taken verbatim from the
  checked-in table (the pre-i18n literals). Never re-derived — v0.9 policy:
  zh strings must stay byte-identical to the pre-i18n release;
* statePart (v0.9 placeholder cleanup) marks the part-table entries the
  reader must NOT map (state-transition parts, PART_UNKNOWN sentinels,
  exotic enum entries). It freezes the pre-cleanup reader skip set
  (`name.startsWith("PART_")` back then) as explicit data, so those
  entries could be given real display names without moving a single
  normal-table slot index. Their zh comes from zh-cn.xml
  Monsters/Shared/Part — the legacy generator's broken Part lookup left
  the raw enum id in place; STATE_ZH_FALLBACK covers the one upstream gap;
* the en name column is from HunterPie en-us.xml <Monsters><Shared><Part
  Id="PART_*" String="..."/> keyed by the part's String attribute in
  MonsterData.xml. Strings absent from the shared table (today:
  PART_BODY_MUD only) fall back to a documented de-underscored label built
  from the enum name, flagged in the output header.

The generator *extends* the frozen table instead of replacing it: `--check`
regenerates and asserts that everything is identical to the checked-in text.

Usage:
    scripts/gen_schema.py [MonsterData.xml] [zh-cn.xml] [en-us.xml]
    scripts/gen_schema.py --cpp src/monster/part_schemas.cpp --in-place
    scripts/gen_schema.py --check            # CI-style drift check
"""
import argparse
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

REPO = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_CPP = REPO / "src/monster/part_schemas.cpp"
DEFAULT_HP = pathlib.Path("/tmp/HunterPie")

ENTRY_RE = re.compile(
    r'^\s*\{\s*(\d+),\s*(true|false),\s*"((?:[^"\\]|\\.)*)",\s*'
    r'"((?:[^"\\]|\\.)*)",\s*'
    r'(?:(true|false),\s*)?'          # optional v0.9 statePart column
    r'"([^"]*)"\s*'
    r'(?:,\s*\{([^}]*)\}\s*,\s*(\d+)u\s*)?\}\s*,?\s*$')
MONSTER_RE = re.compile(r'^\s*\{(\d+),\s*\{\s*$')
BEGIN = "extern const QHash<int, QVector<PartSchema>> kPartSchemas = {"
END = "// END AUTO-GENERATED kPartSchemas"


def _tenderize_ints(entries):
    """['0x0u', '0xFFFFFFFFu'] -> {0} (sentinel dropped)."""
    out = set()
    for raw in entries:
        value = int(raw.rstrip("u"), 0)
        if value != 0xFFFFFFFF:
            out.add(value)
    return out


# Enum-name fallback for part Strings with no shared-table entry.
TOKEN_FIX = {"L": "Left", "R": "Right", "MUD": "Mud", "SNOW": "Snow",
             "ICE": "Ice", "GOLD": "Gold", "ROCK": "Rock"}

# zh fallback for statePart entries absent from zh-cn.xml Monsters/Shared.
# PART_BODY_MUD is the only one (upstream carries no entry in either
# locale); spelling follows its siblings' *_MUD pattern (ASCII parens,
# e.g. 头部(泥土), 手臂(泥土)).
STATE_ZH_FALLBACK = {"PART_BODY_MUD": "身体(泥土)"}

# Pre-existing, deliberately preserved drift between the checked-in table and
# MonsterData.xml. Alatreon (87) has TWO parts with Id=3 (the severable
# PART_EXPLOSION_WEAKENING and PART_HEAD); the historical Id-keyed generator
# copied PART_HEAD's TenderizeIds="0,5" onto the severable entry as well.
# v0.9 keeps every structured field byte-identical, so the generator accepts
# this row instead of refusing to run.
KNOWN_TENDERIZE_DRIFT = {
    (87, 2): "TenderizeIds 0,5 borrowed from the same-Id PART_HEAD entry",
}


def parse_checked_in(text):
    """-> (header_lines, {monsterId: [entry, ...]}, trailer_lines).

    Accepts both the pre-cleanup 5-field format (no statePart column) and
    the v0.9 6-field format. When the column is absent, statePart is
    derived from the raw name prefix — the exact rule the reader used
    before the cleanup, which keeps the migration one-way and lossless.
    """
    begin = text.index(BEGIN)
    end = text.index(END)
    body = text[begin:end].splitlines()
    header, cur, out = [], None, {}
    for line in body[1:]:
        m = MONSTER_RE.match(line)
        if m:
            cur = int(m.group(1))
            out[cur] = []
            continue
        m = ENTRY_RE.match(line)
        if m and cur is not None:
            sp_raw = m.group(5)
            tids = [t.strip() for t in (m.group(7) or "").split(",") if t.strip()]
            out[cur].append({
                "id": int(m.group(1)),
                "severable": m.group(2) == "true",
                "name": m.group(3),
                "nameEn": m.group(4),
                "statePart": (sp_raw == "true") if sp_raw is not None
                             else m.group(3).startswith("PART_"),
                "thresholds": m.group(6),
                "tenderizeIds": tids,
                "tenderizeCount": int(m.group(8)) if m.group(8) else 0,
            })
    return header, out, text[end:]


def parse_localization(path, root_tag):
    root = ET.parse(path).getroot()
    node = root.find(root_tag)
    if node is None:
        raise SystemExit(f"{path}: missing <{root_tag}>")
    return {e.get("Id"): e.get("String") for e in node}


def parse_monster_parts(path):
    """-> {monsterId: [{id, string, isSeverable, thresholds, tenderizeIds}]}"""
    root = ET.parse(path).getroot()
    out = {}
    for mon in root.findall("./Monsters/Monster"):
        parts = []
        for p in mon.findall("./Parts/Part"):
            ths = [b.get("Threshold") for b in p.findall("Break") if b.get("Threshold")]
            tids = [t for t in (p.get("TenderizeIds") or "").split(",") if t]
            parts.append({
                "id": int(p.get("Id")),
                "string": p.get("String", ""),
                "severable": p.get("IsSeverable", "False").lower() == "true",
                "thresholds": ths,
                "tenderizeIds": tids,
            })
        if parts:
            out[int(mon.get("Id"))] = parts
    return out


def fallback_en(part_string):
    tokens = part_string.removeprefix("PART_").split("_")
    words = [TOKEN_FIX.get(t, t.capitalize()) for t in tokens]
    if tokens and tokens[-1] == "MUD" and len(tokens) > 1:
        return f"{' '.join(words[:-1])} (Mud)"
    return " ".join(words)


def cxx_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def build(checked_in, xml_parts, part_en, part_zh):
    """-> (block_text, stats)

    Field order per part is PartSchema's: id, isSeverable, name (zh),
    nameEn, statePart, thresholds, [tenderizeIds, tenderizeCount].
    statePart sits BEFORE the std::array member on purpose: gcc-15's brace
    elision fails to convert an aggregate whose nested std::array
    initializer precedes the last member ("could not convert
    '<brace-enclosed initializer list>' to 'QList<PartSchema>'"), and the
    generator owns that order.
    """
    stats = {"parts": 0, "en_from_xml": 0, "en_derived": [], "state_parts": 0,
             "state_resolved": [], "state_zh_derived": [], "known_drift": []}
    problems = []
    lines = [BEGIN]
    for mid in sorted(checked_in):
        entries = checked_in[mid]
        xml = xml_parts.get(mid)
        if xml is None:
            problems.append(f"monster {mid}: absent from MonsterData.xml")
            continue
        if len(xml) != len(entries):
            problems.append(f"monster {mid}: {len(entries)} checked-in parts "
                            f"vs {len(xml)} in MonsterData.xml")
            continue
        lines.append(f"    {{{mid}, {{")
        for i, (e, x) in enumerate(zip(entries, xml)):
            if e["id"] != x["id"] or e["severable"] != x["severable"]:
                problems.append(f"monster {mid} part #{i}: id/IsSeverable drift")
            if [t for t in e["thresholds"].split(",") if t] != x["thresholds"]:
                problems.append(f"monster {mid} part #{i}: Break threads drift")
            if _tenderize_ints(e["tenderizeIds"]) != _tenderize_ints(x["tenderizeIds"]):
                if (mid, i) in KNOWN_TENDERIZE_DRIFT:
                    stats["known_drift"].append((mid, i, KNOWN_TENDERIZE_DRIFT[(mid, i)]))
                else:
                    problems.append(f"monster {mid} part #{i}: TenderizeIds drift")
            en = part_en.get(x["string"])
            if en is None:
                en = fallback_en(x["string"])
                stats["en_derived"].append((mid, i, x["string"], en))
            else:
                stats["en_from_xml"] += 1
            if e["statePart"]:
                zh = part_zh.get(x["string"])
                if zh is None:
                    zh = STATE_ZH_FALLBACK.get(x["string"])
                    if zh is not None:
                        stats["state_zh_derived"].append((mid, i, x["string"], zh))
                if zh is None:
                    problems.append(f"monster {mid} part #{i}: state part "
                                    f"{x['string']} has no official zh name")
                    zh = e["name"]
                elif not e["name"].startswith("PART_") and e["name"] != zh:
                    problems.append(f"monster {mid} part #{i}: zh drift for "
                                    f"{x['string']} ({e['name']!r} != {zh!r})")
                stats["state_parts"] += 1
                stats["state_resolved"].append((mid, i, x["string"], zh))
            else:
                zh = e["name"]  # frozen pre-i18n literal
            stats["parts"] += 1
            fields = [f"{e['id']}", "true" if e["severable"] else "false",
                      f'"{zh}"', f'"{cxx_escape(en)}"',
                      "true" if e["statePart"] else "false",
                      f'"{e["thresholds"]}"']
            if e["tenderizeIds"]:
                ids = list(e["tenderizeIds"])
                ids = [t if t.endswith("u") else t + "u" for t in ids]
                fields.append("{" + ", ".join(ids) + "}")
                fields.append(f'{e["tenderizeCount"]}u')
            lines.append("        { " + ", ".join(fields) + " },")
        lines.append("    }},")
    lines.append("};")
    if problems:
        raise SystemExit("refusing to emit — checked-in table drifted from "
                         "HunterPie data:\n  " + "\n  ".join(problems))
    return "\n".join(lines) + "\n", stats


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("monster_data", nargs="?",
                    default=str(DEFAULT_HP / "HunterPie/Game/World/Data/MonsterData.xml"))
    ap.add_argument("zh_cn", nargs="?", default=str(DEFAULT_HP / "Localization/localization/zh-cn.xml"))
    ap.add_argument("en_us", nargs="?", default=str(DEFAULT_HP / "Localization/localization/en-us.xml"))
    ap.add_argument("--cpp", default=str(DEFAULT_CPP))
    ap.add_argument("--in-place", action="store_true",
                    help="rewrite the kPartSchemas block inside --cpp")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 when the checked-in block is not the generator's output")
    args = ap.parse_args()

    cpp = pathlib.Path(args.cpp)
    text = cpp.read_text(encoding="utf-8")
    header, checked_in, _ = parse_checked_in(text)
    xml_parts = parse_monster_parts(args.monster_data)
    part_en = parse_localization(args.en_us, "Monsters/Shared")
    part_zh = parse_localization(args.zh_cn, "Monsters/Shared")

    block, stats = build(checked_in, xml_parts, part_en, part_zh)
    zh_missing = sorted(s for s in part_en
                        if s not in part_zh and s.startswith("PART_"))
    print(f"// parts: {stats['parts']}  en from en-us.xml: {stats['en_from_xml']}  "
          f"en derived: {len(stats['en_derived'])}  state parts: {stats['state_parts']}",
          file=sys.stderr)
    for row in stats["state_resolved"]:
        print(f"//   state part  monster {row[0]:>3} #{row[1]:<2} {row[2]:<34} -> {row[3]}",
              file=sys.stderr)
    for row in stats["state_zh_derived"]:
        print(f"//   state part zh DERIVED (no upstream entry): "
              f"monster {row[0]} #{row[1]} {row[2]} -> {row[3]}", file=sys.stderr)
    for row in stats["en_derived"]:
        print(f"//   derived en  monster {row[0]} part #{row[1]} {row[2]} -> {row[3]}",
              file=sys.stderr)
    if zh_missing:
        print(f"//   note: {len(zh_missing)} shared en keys have no zh-cn sibling",
              file=sys.stderr)

    if args.check:
        start, end = text.index(BEGIN), text.index(END)
        if text[start:end] == block:
            print("gen_schema.py: kPartSchemas is up to date")
            return 0
        print("gen_schema.py: kPartSchemas differs from generated output", file=sys.stderr)
        return 1

    if args.in_place:
        start, end = text.index(BEGIN), text.index(END)
        cpp.write_text(text[:start] + block + text[end:], encoding="utf-8")
        print(f"rewrote {cpp} ({stats['parts']} parts)", file=sys.stderr)
    else:
        print(block + END, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
