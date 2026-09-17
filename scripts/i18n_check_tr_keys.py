#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Static audit of every tr-style key used by the sources.

Each key passed to mh::tr() / trMessage() / table.tr() / instance().tr() /
trSet() / trTip() / trHook() / trWindowTitle() must exist in the locale
directories. Complements scripts/i18n_key_parity.py (zh/en set equality)
with the opposite direction: *used keys must exist*. Catches typo'd keys
and keys referenced from C++ that never made it into the JSON.

Delivered by the WS-C i18n workstream (ws-c/tools/check_tr_keys.py);
promoted to scripts/ during integration, with two fixes:
  * handle QStringLiteral("key") / QLatin1String("key") wrappers at call
    sites (the original pattern only matched bare string literals and
    therefore under-reported);
  * audit the trSet/trTip/trHook/trWindowTitle widget-registry helpers,
    whose key is the second argument.

Usage:
    python3 scripts/i18n_check_tr_keys.py [--roots src tests] \
        [--locale-dir src/resources/i18n/zh-CN]

Exit code 0 = all used keys resolve; 1 = missing keys (printed).
"""
import argparse
import glob
import json
import os
import re
import sys

DEFAULT_ROOTS = ["src"]

STR = r'(?:QStringLiteral\(|QLatin1String\(|QString\()?\s*"([^"]+)"'

PATTERNS = [
    # mh::tr("k") / trMessage("k") / table.tr("k") / instance().tr("k")
    re.compile(r'(?:mh::tr|trMessage|table\.tr|instance\(\)\.tr)\(\s*' + STR),
    # trSet(w, "k") / trTip(w, "k") / trHook(w, "k") — key is arg 2. The
    # widget expression must not itself contain a comma at depth 0, which
    # holds for every current call site (plain identifiers / this->x).
    re.compile(r'tr(?:Set|Tip|Hook)\([^,()]+,\s*' + STR),
    # trWindowTitle("k")
    re.compile(r'trWindowTitle\(\s*' + STR),
]

# Documentation-only occurrences: core/string_table.h carries schema examples
# ("ui.buildup" -> "怒气 %1%") in its header comment. Those are prose, not
# call sites, so the file is excluded from the audit.
EXCLUDE_FILES = {"src/core/string_table.h"}


def flatten(obj, prefix=""):
    out = {}
    for k, v in obj.items():
        key = f"{prefix}.{k}" if prefix else k
        if isinstance(v, dict):
            out.update(flatten(v, key))
        else:
            out[key] = v
    return out


def load_locale_dir(path):
    flat = {}
    for p in sorted(glob.glob(os.path.join(path, "*.json"))):
        with open(p, encoding="utf-8") as f:
            flat.update(flatten(json.load(f)))
    return flat


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument("--roots", nargs="+", default=DEFAULT_ROOTS)
    ap.add_argument("--locale-dir",
                    default=os.path.join(repo_root, "src/resources/i18n/zh-CN"))
    args = ap.parse_args()

    known = {k for k in load_locale_dir(args.locale_dir)
             if "_meta" not in k.split(".")}
    # Every other shipped locale must define the same keys as zh-CN; check the
    # used-keys resolve against EACH locale so an en-only gap cannot hide.
    locale_dirs = sorted(
        d for d in glob.glob(os.path.join(repo_root, "src/resources/i18n/*"))
        if os.path.isdir(d))
    known_by_locale = {os.path.basename(d): load_locale_dir(d) for d in locale_dirs}

    used = {}
    for root in args.roots:
        for dirpath, _dirs, files in os.walk(os.path.join(repo_root, root)):
            for name in files:
                if not name.endswith((".cpp", ".h")):
                    continue
                full = os.path.relpath(os.path.join(dirpath, name), repo_root)
                if os.path.normpath(full) in {os.path.normpath(e) for e in EXCLUDE_FILES}:
                    continue
                src = open(os.path.join(repo_root, full), encoding="utf-8").read()
                for pat in PATTERNS:
                    for m in pat.finditer(src):
                        used.setdefault(m.group(1), set()).add(full)

    print(f"distinct tr-style keys used in {', '.join(args.roots)}: {len(used)}")
    print(f"keys defined in zh-CN: {len(known)}")

    failed = False
    for loc_name, flat in sorted(known_by_locale.items()):
        missing = {k: sorted(v) for k, v in used.items() if k not in flat}
        if missing:
            failed = True
            print(f"\nMISSING KEYS in {loc_name}:")
            for k in sorted(missing):
                print(f"  {k}  <- {', '.join(missing[k])}")

    if failed:
        return 1
    print("ALL USED KEYS RESOLVE (all locales)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
