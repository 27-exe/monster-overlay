#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# Generate src/rise/data/mhr_part_names_data.cpp — the per-monster part-name
# TABLE — in BOTH locales from HunterPie's Rise data + official localization.
#
# The table and its two static_asserts are generated into the data translation
# unit; the LOOKUP layer (risePartNameEntry / risePartName / risePartNameEn /
# risePartDisplayName) is hand-written in src/rise/mhr_part_names.cpp, and
# struct RisePartName plus the extern declaration of kRisePartNames live in
# src/rise/mhr_part_names.h so the two TUs can share the table. Regenerating
# therefore rewrites only the data file; the logic file is maintained by hand.
#
# Adapted (WS-B, i18n) from the v0.8.4-r21 single-locale generator
# `v0.8.4-r21/parts-names-hp/tools/generate_rise_part_names.py`:
#   * takes --en-xml AND --zh-xml instead of one --localization file;
#   * verifies all three input sha256 hashes (MonsterData.xml + both locale
#     files) and fails on mismatch unless --allow-unverified is passed;
#   * emits one row per (monsterId, partIndex) carrying both columns
#     `{ monsterId, partIndex, "<zh>", "<en>" }`; the locale-aware selection
#     over those columns lives in src/rise/mhr_part_names.cpp.
#
# Join (unchanged from the original generator): every
# /Monsters/Monster/Parts/Part of HunterPie/Game/Rise/Data/MonsterData.xml,
# keyed by the part's String=PART_* attribute, resolved against
# /Strings/Monsters/Shared/Part[@Id] of each locale file. partIndex is the
# Part's Id attribute (the script asserts the Ids are dense 0..n-1 in document
# order, so document order and Id agree — same as the original generator).
# The zh column is byte-identical to what the previous single-locale generator
# produced (see ws-b/REPORT.md for the semantic diff against
# `git show HEAD:src/rise/mhr_part_names.cpp`).
#
# Usage:
#   python3 scripts/generate_rise_part_names.py \
#       --monster-data HunterPie/Game/Rise/Data/MonsterData.xml \
#       --en-xml hp-loc/en-us.xml --zh-xml hp-loc/zh-cn.xml \
#       --cpp-out src/rise/data/mhr_part_names_data.cpp [--check] [--allow-unverified]

from __future__ import annotations

import argparse
import hashlib
import xml.etree.ElementTree as ET
from pathlib import Path

# sha256 of the inputs this table was extracted from (2026-09-18).
EXPECTED_SHA256 = {
    "MonsterData.xml": "f5cd32a5ab481187dc356e8bd75e8df7618ecd95314db9cec4107567f17751b8",
    "zh-cn.xml": "2a4b1bb318fc21a55c0b5b978e2d33cb8c2ea74457b34fcac88b9236c19a06db",
    "en-us.xml": "661d58b54bd1214ae0e9eff92fb4167b8aab54df0452eea238cb9574300030ea",
}

EXPECTED_ROWS = 598


def verify(path: Path, expected: str, allow_unverified: bool) -> str:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != expected and not allow_unverified:
        raise SystemExit(
            f"{path}: sha256 {digest} does not match the recorded "
            f"{expected}; pass --allow-unverified to override")
    return digest


def part_strings(path: Path, tag: str) -> dict[str, str]:
    """Shared/Part Id -> String for one locale file."""
    root = ET.parse(path).getroot()
    table: dict[str, str] = {}
    for node in root.findall(".//Monsters/Shared/Part"):
        table.setdefault(node.attrib["Id"], node.attrib["String"])
    if not table:
        raise SystemExit(f"{path}: no /Monsters/Shared/Part entries ({tag})")
    return table


def cpp_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def collect_rows(monster_data: Path, zh_table: dict[str, str],
                 en_table: dict[str, str]) -> list[tuple[int, int, str, str, str]]:
    root = ET.parse(monster_data).getroot()
    rows: list[tuple[int, int, str, str, str]] = []
    seen: set[tuple[int, int]] = set()
    for monster in root.findall("./Monsters/Monster"):
        monster_id = int(monster.attrib["Id"])
        parts = monster.findall("./Parts/Part")
        for position, part in enumerate(parts):
            part_id = int(part.attrib["Id"])
            if part_id != position:
                raise SystemExit(
                    f"monster {monster_id}: Part Id {part_id} != document "
                    f"position {position}; the index semantics need review")
            key = part.attrib["String"]
            if key not in zh_table:
                raise SystemExit(f"missing zh-cn Shared/Part entry: {key}")
            if key not in en_table:
                raise SystemExit(f"missing en-us Shared/Part entry: {key}")
            row_key = (monster_id, part_id)
            if row_key in seen:
                raise SystemExit(f"duplicate monster/index row: {row_key}")
            seen.add(row_key)
            rows.append((monster_id, part_id, key, zh_table[key], en_table[key]))
    rows.sort()
    if len(rows) != EXPECTED_ROWS:
        raise SystemExit(
            f"unexpected Rise part row count: {len(rows)} (expected {EXPECTED_ROWS})")
    return rows


def cpp_file(rows: list[tuple[int, int, str, str, str]], monster_data: Path,
             monster_sha: str, en_xml: Path, en_sha: str,
             zh_xml: Path, zh_sha: str) -> str:
    body = "\n".join(
        f'    {{ {m}, {i}, "{cpp_escape(zh)}", "{cpp_escape(en)}" }}, // {key}'
        for m, i, key, zh, en in rows)
    return f'''// SPDX-License-Identifier: Apache-2.0
// AUTO-GENERATED - do not edit by hand.
// Regenerate with scripts/generate_rise_part_names.py:
//   python3 scripts/generate_rise_part_names.py --monster-data <MonsterData.xml>
//       --en-xml <en-us.xml> --zh-xml <zh-cn.xml> --cpp-out src/rise/data/mhr_part_names_data.cpp
//
// Sources (all three sha256-verified by the generator):
//   MonsterData.xml  HunterPie/Game/Rise/Data/MonsterData.xml
//     sha256 {monster_sha}
//     (/Monsters/Monster/Parts/Part — the part list every Rise monster
//      carries; partIndex is the part's Id attribute)
//   zh column        https://cdn.hunterpie.com/localization/zh-cn.xml
//     sha256 {zh_sha}
//     /Strings/Monsters/Shared/Part[@Id] — the official Simplified Chinese part
//     names HunterPie's monster widget resolves.
//   en column        https://cdn.hunterpie.com/localization/en-us.xml
//     sha256 {en_sha}
//     /Strings/Monsters/Shared/Part[@Id] — the official English part names.
//   The two locale files are byte-identical to
//   HunterPie/localization@main/localization/<lang>.xml and to the hashes
//   published by https://api.hunterpie.com/v1/localization/checksum.
//   Extracted 2026-09-18. The generator asserts both locales resolve every
//   PART_* key reached from MonsterData.xml ({len(rows)} rows, 79 monsters).
//
// DATA translation unit: struct RisePartName (in rise/mhr_part_names.h), the
// rows below, and the two static_asserts that police them. The lookups
// (risePartNameEntry's lower_bound binary search, risePartName / risePartNameEn
// / risePartDisplayName) live in src/rise/mhr_part_names.cpp and use the table
// through the `extern const` declaration in rise/mhr_part_names.h — hence the
// plain `namespace mhw` scope below, not an anonymous one: a constexpr array
// inside an anonymous namespace has internal linkage and no other TU could
// see it. `constexpr` here (rather than `extern const`) is what lets the
// static_asserts inspect the committed rows at compile time.
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

#include <array>
#include <cstddef>

namespace mhw {{

// AUTO-GENERATED TABLE - do not edit by hand.

constexpr std::array<RisePartName, {len(rows)}> kRisePartNames = {{{{
{body}
}}}};

constexpr bool risePartNamesStrictlyIncreasing()
{{
    for (std::size_t i = 1; i < kRisePartNames.size(); ++i) {{
        const RisePartName &previous = kRisePartNames[i - 1];
        const RisePartName &current = kRisePartNames[i];
        if (previous.monsterId > current.monsterId
            || (previous.monsterId == current.monsterId
                && previous.partIndex >= current.partIndex))
            return false;
    }}
    return true;
}}

static_assert(risePartNamesStrictlyIncreasing());

constexpr bool risePartNamesCarryBothColumns()
{{
    for (const RisePartName &entry : kRisePartNames) {{
        if (entry.name == nullptr || entry.name[0] == '\\0'
            || entry.nameEn == nullptr || entry.nameEn[0] == '\\0')
            return false;
    }}
    return true;
}}
static_assert(risePartNamesCarryBothColumns(),
              "every Rise part row carries a zh and an en name");

}} // namespace mhw
'''


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--monster-data", type=Path, required=True)
    ap.add_argument("--en-xml", type=Path, required=True)
    ap.add_argument("--zh-xml", type=Path, required=True)
    # The default is the DATA translation unit. The lookup layer
    # (src/rise/mhr_part_names.cpp) is hand-written, so --cpp-out can only
    # ever point at the file that owns the generated rows + static_asserts.
    ap.add_argument("--cpp-out", type=Path,
                    default=Path("src/rise/data/mhr_part_names_data.cpp"))
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--allow-unverified", action="store_true")
    args = ap.parse_args()

    monster_sha = verify(args.monster_data, EXPECTED_SHA256["MonsterData.xml"],
                         args.allow_unverified)
    zh_sha = verify(args.zh_xml, EXPECTED_SHA256["zh-cn.xml"],
                    args.allow_unverified)
    en_sha = verify(args.en_xml, EXPECTED_SHA256["en-us.xml"],
                    args.allow_unverified)

    rows = collect_rows(args.monster_data,
                        part_strings(args.zh_xml, "zh"),
                        part_strings(args.en_xml, "en"))
    text = cpp_file(rows, args.monster_data, monster_sha,
                    args.en_xml, en_sha, args.zh_xml, zh_sha)

    if args.check:
        current = args.cpp_out.read_text(encoding="utf-8") if args.cpp_out else ""
        if current != text:
            print(f"OUT OF DATE: {args.cpp_out}")
            return 1
        print(f"up to date: {args.cpp_out}")
        return 0

    if args.cpp_out:
        args.cpp_out.write_text(text, encoding="utf-8")
    print(f"generated {len(rows)} rows (zh+en)")
    print(f"MonsterData.xml sha256 {monster_sha}")
    print(f"zh-cn.xml sha256 {zh_sha}")
    print(f"en-us.xml sha256 {en_sha}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
