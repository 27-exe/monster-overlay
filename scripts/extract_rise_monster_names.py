#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# Generate src/rise/mhr_monster_names.cpp — the Rise monster-name table — in
# BOTH locales from HunterPie's official localization files.
#
# Adapted (WS-B, i18n) from the v0.8.4-r18 single-locale generator
# `v0.8.4-r18/monster-identity/tools/extract_rise_monster_names.py`:
#   * takes --en-xml AND --zh-xml instead of one positional file;
#   * verifies BOTH sha256 hashes against the hashes published by
#     https://api.hunterpie.com/v1/localization/checksum (fails on mismatch
#     unless --allow-unverified is passed);
#   * emits one row per Id carrying both columns `{ id, "<zh>", "<en>" }`
#     and the locale-aware lookup the overlay uses at run time.
#
# The zh column is byte-identical to what the previous single-locale
# generator produced, so regenerating this file never changes the Chinese
# side of the overlay (see ws-b/REPORT.md for the semantic diff against
# `git show HEAD:src/rise/mhr_monster_names.cpp`).
#
# Sources (both fetched 2026-09-17, byte-identical to
# HunterPie/localization@main/localization/<lang>.xml):
#   https://cdn.hunterpie.com/localization/zh-cn.xml
#   https://cdn.hunterpie.com/localization/en-us.xml
# Section: /Strings/Monsters/Rise/Monster[@Id] — the exact node HunterPie's
# MHRMonster.cs:48 resolves for a Rise monster's name.
#
# Usage:
#   python3 scripts/extract_rise_monster_names.py \
#       --en-xml hp-loc/en-us.xml --zh-xml hp-loc/zh-cn.xml \
#       --cpp-out src/rise/mhr_monster_names.cpp \
#       [--csv-out out.csv] [--json-out out.json] [--check] \
#       [--allow-unverified]
#
# --check compares the regenerated text against --cpp-out and exits 1 when it
# differs (CI / "is the checked-in table stale?" gate).

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

# sha256 of the official files, as published by
# https://api.hunterpie.com/v1/localization/checksum (fetched 2026-09-17).
EXPECTED_SHA256 = {
    "zh-cn.xml": "2a4b1bb318fc21a55c0b5b978e2d33cb8c2ea74457b34fcac88b9236c19a06db",
    "en-us.xml": "661d58b54bd1214ae0e9eff92fb4167b8aab54df0452eea238cb9574300030ea",
}

MONSTER_ENTRY = re.compile(
    r'<Monster\s+Id="(-?\d+)"\s+String="([^"]*)"\s*/>', re.S
)

EXPECTED_IDS = 79


def slice_rise_monsters(text: str) -> tuple[int, int]:
    """Return the 1-based [start, end] line range of <Monsters><Rise>."""
    lines = text.split("\n")
    monsters_start = next(
        (i for i, l in enumerate(lines, 1) if l.strip() == "<Monsters>"), None
    )
    if monsters_start is None:
        raise SystemExit("no <Monsters> block in file")
    rise_start = next(
        (i for i in range(monsters_start, len(lines) + 1)
         if lines[i - 1].strip() == "<Rise>"),
        None,
    )
    if rise_start is None:
        raise SystemExit("no <Rise> child of <Monsters>")
    rise_end = next(
        (i for i in range(rise_start, len(lines) + 1)
         if lines[i - 1].strip() == "</Rise>"),
        None,
    )
    if rise_end is None:
        raise SystemExit("unterminated <Rise> block")
    return rise_start, rise_end


def extract(path: Path, expected_sha: str, allow_unverified: bool) -> tuple[list[tuple[int, str]], tuple[int, int], str]:
    raw = path.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != expected_sha and not allow_unverified:
        raise SystemExit(
            f"{path}: sha256 {digest} does not match the published "
            f"{expected_sha}; pass --allow-unverified to override")
    text = raw.decode("utf-8")
    start, end = slice_rise_monsters(text)
    segment = "\n".join(text.split("\n")[start - 1:end])
    entries = [(int(i), s) for i, s in MONSTER_ENTRY.findall(segment)]
    ids = [i for i, _ in entries]
    if len(ids) != len(set(ids)):
        raise SystemExit(f"{path}: duplicate Id in the Rise monster section")
    if entries != sorted(entries):
        raise SystemExit(f"{path}: Rise monster section is not sorted by Id")
    if len(entries) != EXPECTED_IDS:
        raise SystemExit(
            f"{path}: {len(entries)} entries, expected {EXPECTED_IDS}")
    return entries, (start, end), digest


def cpp_escape(value: str) -> str:
    out = []
    for ch in value:
        if ch in '\\"':
            out.append("\\" + ch)
        else:
            out.append(ch)
    return "".join(out)


def cpp_table(rows: list[tuple[int, str, str]], zh_src: Path, zh_lines: tuple[int, int],
              zh_digest: str, en_src: Path, en_lines: tuple[int, int],
              en_digest: str) -> str:
    body = "\n".join(
        f'    {{ {i}, "{cpp_escape(zh)}", "{cpp_escape(en)}" }},' for i, zh, en in rows
    )
    count = len(rows)
    ids = [i for i, _, _ in rows]
    return f'''// SPDX-License-Identifier: Apache-2.0
//
// AUTO-GENERATED — do not edit by hand.
// Regenerate with scripts/extract_rise_monster_names.py
//   python3 scripts/extract_rise_monster_names.py --en-xml <en-us.xml> --zh-xml <zh-cn.xml> --cpp-out src/rise/mhr_monster_names.cpp
//
// Source: HunterPie official localization — BOTH columns verbatim.
//   zh column: zh-cn.xml  https://cdn.hunterpie.com/localization/zh-cn.xml
//     sha256 {zh_digest}
//     /Strings/Monsters/Rise/Monster[@Id] ({zh_src.name} lines {zh_lines[0]}-{zh_lines[1]})
//   en column: en-us.xml  https://cdn.hunterpie.com/localization/en-us.xml
//     sha256 {en_digest}
//     /Strings/Monsters/Rise/Monster[@Id] ({en_src.name} lines {en_lines[0]}-{en_lines[1]})
//   Both files are byte-identical to HunterPie/localization@main/localization/
//   <lang>.xml and to the hashes published by
//   https://api.hunterpie.com/v1/localization/checksum (fetch transcript:
//   .../v0.8.4-r18/zone-names/evidence/localization/FETCH.md).
//   Extracted 2026-09-18. The two locales carry the SAME {count} Ids — the
//   generator asserts the Id sets are equal, so every row has both columns.
//
// {count} entries covering Id {min(ids)}..{max(ids)}; the id gaps are upstream —
// HunterPie's localization carries no <Monster> entry for e.g. 47..75 / 99..106,
// so a miss here is a real "no localized name", not an extraction artefact.
//
// Locale selection (v0.9 i18n, WS-B): riseMonsterName() returns the en column
// while mhw::StringTable::instance().isEnglish() is true and the zh column
// otherwise; an empty locale (before any load()) reads as Chinese, so the
// pre-i18n behaviour is the default. riseMonsterNameEn() exposes the en column
// explicitly (tests / tooling) and is locale-independent.

#include "rise/mhr_monster_names.h"

#include "core/string_table.h"

#include <array>
#include <cstddef>

namespace mhw {{
namespace {{

struct RiseMonsterName {{
    int id;
    const char *name;    // UTF-8, Simplified Chinese, verbatim from zh-cn.xml
    const char *nameEn;  // UTF-8, English, verbatim from en-us.xml
}};

// Sorted by id: the lookup below binary-searches it.
constexpr std::array<RiseMonsterName, {count}> kRiseMonsterNames = {{{{
{body}
}}}};

// A regeneration that ever emits an unsorted or duplicated id must fail the
// build instead of silently returning wrong names at runtime.
constexpr bool riseMonsterNamesStrictlyIncreasing()
{{
    for (std::size_t i = 1; i < kRiseMonsterNames.size(); ++i) {{
        if (kRiseMonsterNames[i - 1].id >= kRiseMonsterNames[i].id)
            return false;
    }}
    return true;
}}
static_assert(riseMonsterNamesStrictlyIncreasing(),
              "riseMonsterName() binary search requires strictly increasing ids");

// Both locale columns must be present for every row: a regenerated table with
// an empty column would otherwise show blank names instead of failing.
constexpr bool riseMonsterNamesCarryBothColumns()
{{
    for (const RiseMonsterName &entry : kRiseMonsterNames) {{
        if (entry.name == nullptr || entry.name[0] == '\\0'
            || entry.nameEn == nullptr || entry.nameEn[0] == '\\0')
            return false;
    }}
    return true;
}}
static_assert(riseMonsterNamesCarryBothColumns(),
              "every Rise monster row carries a zh and an en name");

const RiseMonsterName *riseMonsterNameEntry(int id)
{{
    std::size_t lo = 0;
    std::size_t hi = kRiseMonsterNames.size();
    while (lo < hi) {{
        const std::size_t mid = lo + (hi - lo) / 2;
        if (kRiseMonsterNames[mid].id < id)
            lo = mid + 1;
        else
            hi = mid;
    }}
    if (lo < kRiseMonsterNames.size() && kRiseMonsterNames[lo].id == id)
        return &kRiseMonsterNames[lo];
    return nullptr;
}}

}} // namespace

const char *riseMonsterName(int id)
{{
    const RiseMonsterName *entry = riseMonsterNameEntry(id);
    if (entry == nullptr)
        return nullptr;
    return StringTable::instance().isEnglish() ? entry->nameEn : entry->name;
}}

const char *riseMonsterNameEn(int id)
{{
    const RiseMonsterName *entry = riseMonsterNameEntry(id);
    return entry == nullptr ? nullptr : entry->nameEn;
}}

}} // namespace mhw
'''


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--en-xml", type=Path, required=True)
    ap.add_argument("--zh-xml", type=Path, required=True)
    ap.add_argument("--cpp-out", type=Path)
    ap.add_argument("--csv-out", type=Path)
    ap.add_argument("--json-out", type=Path)
    ap.add_argument("--check", action="store_true",
                    help="fail when --cpp-out differs from the regenerated text")
    ap.add_argument("--allow-unverified", action="store_true",
                    help="downgrade a sha256 mismatch to a warning")
    args = ap.parse_args()

    zh_entries, zh_lines, zh_digest = extract(
        args.zh_xml, EXPECTED_SHA256["zh-cn.xml"], args.allow_unverified)
    en_entries, en_lines, en_digest = extract(
        args.en_xml, EXPECTED_SHA256["en-us.xml"], args.allow_unverified)

    zh_by_id = dict(zh_entries)
    en_by_id = dict(en_entries)
    if set(zh_by_id) != set(en_by_id):
        raise SystemExit(
            "locale Id sets differ: "
            f"zh-only {sorted(set(zh_by_id) - set(en_by_id))}, "
            f"en-only {sorted(set(en_by_id) - set(zh_by_id))}")

    rows = [(i, zh_by_id[i], en_by_id[i]) for i, _ in zh_entries]
    text = cpp_table(rows, args.zh_xml, zh_lines, zh_digest,
                     args.en_xml, en_lines, en_digest)

    if args.check:
        current = args.cpp_out.read_text(encoding="utf-8") if args.cpp_out else ""
        if current != text:
            print(f"OUT OF DATE: {args.cpp_out}")
            return 1
        print(f"up to date: {args.cpp_out}")
        return 0

    if args.cpp_out:
        args.cpp_out.write_text(text, encoding="utf-8")
    if args.csv_out:
        args.csv_out.write_text(
            "id,name_zh_cn,name_en_us\n"
            + "\n".join(f"{i},{zh},{en}" for i, zh, en in rows) + "\n",
            encoding="utf-8")
    if args.json_out:
        args.json_out.write_text(json.dumps({
            "source_files": {
                "zh-cn.xml": {"path": str(args.zh_xml), "sha256": zh_digest,
                              "lines": list(zh_lines)},
                "en-us.xml": {"path": str(args.en_xml), "sha256": en_digest,
                              "lines": list(en_lines)},
            },
            "section": "/Strings/Monsters/Rise/Monster[@Id]",
            "count": len(rows),
            "entries": [{"id": i, "name_zh_cn": zh, "name_en_us": en}
                        for i, zh, en in rows],
        }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print(f"{len(rows)} entries (zh+en), zh lines {zh_lines}, en lines {en_lines}")
    print(f"zh sha256 {zh_digest}")
    print(f"en sha256 {en_digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
