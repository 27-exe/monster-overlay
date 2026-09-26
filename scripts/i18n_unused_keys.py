#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Report i18n keys that nobody references — the cleanup half of the gate suite.

scripts/i18n_check_tr_keys.py answers the opposite question ("is every used key
defined?"). This one answers "is every defined key used?" and is meant to be
run before a key-deletion sweep, not as a merge gate.

Reference channels this script understands (all of them — that is why it can
find the dead keys the other two scripts cannot see):

 1. Any literal that appears in the code stream: this is deliberately coarse,
    but it is what covers `mh::tr("k")`, `trMessage("k")`, `table.tr("k")`,
    `trSet(w,"k")`, `trTip(w,"k")`, `trHook(w,"k")`, `trWindowTitle("k")`.
 2. Static arrays of keys in the code: `kKeys[]` (panel_sections.h),
    `kNameKeys[]` (hud_canvas.cpp) and the `kCorners[]` table. Their entries
    are plain string literals in the same code stream, so channel 1 covers
    them. Verified: the 47 keys the audit counted as "indirect" (section 4)
    all resolve through channels 1-3.
 3. `const char *stateKey = "console....."` style: the ternary/assignment
    chains in control_panel.cpp. Same channel (they are literals too).
 4. `QStringLiteral("mantle.%1").arg(id)`-style COMPOSED keys: no literal ever
    names `mantle.3`, so every `mantle.<n>` key is marked reachable (see
    `_COMPOSED`). Without this rule all 20 mantle ids would be reported dead.

# TODO: this is the current best known approximation. It intentionally reports
# a superset (may list keys that are actually reachable through indirection
# that this script has not modelled) — the false-positive cost is a needless
# inspection, while the false-negative cost is silently shipping a dead key.

Exit code:
  0 always by default. Dead keys are a documentation/cleanup signal, NOT a
    correctness gate, so a repo may legitimately carry dead keys while a
    rename is in flight and must not go red.
  1 only with --strict, for opt-in use in a dedicated cleanup branch or CI
    stage. Do not wire this into the default branch gate suite.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import re
import sys
from pathlib import Path

# Metadata fields that never belong to the lookup table. StringTable::load()
# skips these at the JSON top level (see core/string_table.cpp) so the runtime
# table and this script must agree.
SKIP_KEYS = {"_meta", "_comment"}
SOURCE_EXTS = (".cpp", ".cc", ".h", ".hpp", ".hh")

# --- C++ tokenizing -----------------------------------------------------------
#
# A naive `"..."` regex over raw text is WRONG. src/ui/panel_player.cpp line 674
# is a // comment containing an odd number of ASCII quotes
# ("stretched" ... "x"红斩特长一条") which pairs with the next real literal and
# swallows both — and the 21 ui.demo.* keys lived immediately after it. Every
# literal scan below therefore runs on CODE-ONLY text: comments and character
# literals are stripped first by strip_noncode().

STR_BODY = r'[^"\\\n]|\\.'
STRING_PATTERN = r'"((?:' + STR_BODY + r')*)"'
CHAR_PATTERN = r"'(?:[^'\\\n]|\\.)*'"

_STRING = re.compile(STRING_PATTERN)
_CHAR = re.compile(CHAR_PATTERN)

# "prefix.%N" — a key composed at runtime by QString("%1.%2").arg().
_COMPOSED = re.compile(r"^([A-Za-z][A-Za-z0-9_.]*)\.%[0-9]+$")


def strip_noncode(text: str) -> str:
    """Return `text` with // and block comments removed, literals preserved.

    Hand-written scan rather than a regex so that an unbalanced quote inside a
    comment cannot desynchronise the rest of the file.
    """
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c == '"':
            m = _STRING.match(text, i)
            if m:
                out.append(m.group(0))
                i = m.end()
                continue
            i += 1          # stray quote, treat as plain text
            continue
        if c == "'":
            m = _CHAR.match(text, i)
            if m:
                out.append("' '")       # keep shaped like a char literal
                i = m.end()
                continue
            i += 1          # digit separator / lone apostrophe
            continue
        if c == "\\" and i + 1 < n:
            out.append(" ")
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def iter_literals(code: str):
    """Yield the text of each string literal in CODE-ONLY text."""
    for m in _STRING.finditer(code):
        yield m.group(1)


# --- JSON side ----------------------------------------------------------------


def flatten(obj: dict, prefix: str = "") -> dict:
    """Nested object -> dot-path leaves, exactly like StringTable::load().

    `_meta` is skipped at ANY depth, not just the top level: `mantle._meta` and
    `abnormality._meta` are maintenance notes, and both existing gate scripts
    (i18n_key_parity.py, i18n_check_tr_keys.py) already exclude them. Keeping
    the three in agreement is what stops a key-count mismatch between them.
    """
    out = {}
    for key, value in obj.items():
        if key in SKIP_KEYS:
            continue
        path = f"{prefix}.{key}" if prefix else key
        if isinstance(value, dict):
            out.update(flatten(value, path))
        else:
            out[path] = value
    return out


def load_locale_files(i18n_root: Path) -> dict:
    """{(locale, filename): {key: value}} for every locale dir under the root."""
    out = {}
    for locale_dir in sorted(p for p in i18n_root.iterdir() if p.is_dir()):
        for jf in sorted(locale_dir.glob("*.json")):
            try:
                doc = json.loads(jf.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as exc:
                print(f"WARN: {jf}: {exc}", file=sys.stderr)
                continue
            if not isinstance(doc, dict):
                print(f"WARN: {jf}: top level is not an object", file=sys.stderr)
                continue
            out[(locale_dir.name, jf.name)] = flatten(doc)
    return out


# --- code side ----------------------------------------------------------------


def collect_used(paths) -> set:
    """Collect the set of string literals referenced by `paths`.

    Over-collects on purpose: the caller only reports keys that are BOTH in the
    JSON and unreferenced, so extra candidates are harmless.
    """
    used: set = set()
    for path in paths:
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for lit in iter_literals(text):
            if lit:
                used.add(lit)
    return used


def composed_prefixes(used: set) -> set:
    """Prefixes of `"prefix.%N"` templates found in the code (`mantle`)."""
    prefixes: set = set()
    for lit in used:
        m = _COMPOSED.match(lit)
        if m:
            prefixes.add(m.group(1))
    return prefixes


# --- report -------------------------------------------------------------------


def find_dead_keys(i18n_root: Path, scan_roots):
    """Return (dead_by_locale_file, reference_channels).

    dead[(locale, file)] = sorted [key, ...] with zero reference.
    """
    files = load_locale_files(i18n_root)
    if not files:
        return {}, {"error": f"no locale JSON found under {i18n_root}"}

    paths = []
    for root in scan_roots:
        root = Path(root)
        if not root.is_dir():
            print(f"WARN: scan root not found: {root}", file=sys.stderr)
            continue
        paths.extend(p for p in sorted(root.rglob("*"))
                     if p.suffix in SOURCE_EXTS)

    used = collect_used(paths)
    prefixes = composed_prefixes(used)

    dead = {}
    for (locale, fname), flat in sorted(files.items()):
        keys = [k for k in flat if not k.startswith("_")]
        dead[(locale, fname)] = sorted(
            k for k in keys
            if not _reachable(k, used, prefixes)
        )
    return dead, {"files": len(paths), "literals": len(used),
                  "composed_prefixes": sorted(prefixes)}


def _reachable(key: str, used: set, prefixes: set) -> bool:
    """True when a literal (directly, or through a composed prefix) refers."""
    if key in used:
        return True
    first = key.split(".", 1)[0]
    return first in prefixes


def main(argv=None) -> int:
    repo_root = Path(__file__).resolve().parents[1]
    ap = argparse.ArgumentParser(
        description="Report i18n keys that nobody references. "
                    "Exit code is 0 by default even when dead keys exist; "
                    "pass --strict to make dead keys fail.")
    ap.add_argument("--src", default=str(repo_root / "src"),
                    help="source tree to scan for references (default: src)")
    ap.add_argument("--tests", action="store_true",
                    help="also scan the repo's tests/ tree. Tests DO reference "
                         "keys (locale_sync_tests.cpp, snap_*.cpp), so without "
                         "this those keys are reported dead even though they "
                         "are part of the contract")
    ap.add_argument("--i18n-root", default=str(repo_root / "src/resources/i18n"),
                    help="locale directories to audit (default: "
                         "src/resources/i18n)")
    ap.add_argument("--strict", action="store_true",
                    help="exit 1 when any dead key is found (opt-in gate; "
                         "off by default because dead keys are a cleanup "
                         "signal, not a correctness failure)")
    ap.add_argument("--format", choices=("text", "json"), default="text",
                    help="json emits ONLY the machine-readable payload; "
                         "text emits the human report")
    ap.add_argument("--emit-dead-keys", metavar="OUT", default=None,
                    help="write the plain dead-key list to OUT (one JSON array) "
                         "for consumption by scripts/i18n_split_domains.py")
    args = ap.parse_args(argv)

    dead, channels = find_dead_keys(Path(args.i18n_root),
                                    [args.src] + ([repo_root / "tests"]
                                                  if args.tests else []))

    if "error" in channels:
        print(f"FAIL: {channels['error']}", file=sys.stderr)
        return 2

    unique = sorted({k for keys in dead.values() for k in keys})
    total = len(unique)

    if args.emit_dead_keys:
        Path(args.emit_dead_keys).write_text(
            json.dumps(unique, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
        print(f"wrote {total} dead key(s) to {args.emit_dead_keys}")
        return 0

    if args.format == "json":
        print(json.dumps(
            {"dead": {f"{loc}/{fn}": ks for (loc, fn), ks in dead.items()},
             "unique": unique,
             "channels": channels,
             "total_dead": total},
            ensure_ascii=False, indent=2))
        # JSON mode is machine-readable ONLY; the strict exit code still
        # applies so CI can parse the payload and still gate.
        if args.strict and total:
            return 1
        return 0

    for (locale, fname), keys in sorted(dead.items()):
        print(f"{locale}/{fname}: {len(keys)} dead key(s)")
    print(f"scanned {channels['files']} source file(s), "
          f"{channels['literals']} distinct literal(s), "
          f"{len(channels['composed_prefixes'])} composed prefix(es)")
    print(f"{total} unique dead key(s)")

    by_prefix: dict = {}
    for key in unique:
        by_prefix.setdefault(key.split(".", 1)[0], []).append(key)
    for prefix in sorted(by_prefix):
        keys = by_prefix[prefix]
        print(f"  {prefix}: {len(keys)}")
        for k in keys:
            print(f"    {k}")

    if args.strict and total:
        print("FAIL: dead keys found (--strict)", file=sys.stderr)
        return 1
    print("OK (report only; pass --strict to gate on this)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
