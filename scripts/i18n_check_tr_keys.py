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

# Same literal shape, without the wrapping call — used to harvest the entries
# inside a static key table.
STR_LITERAL = re.compile(r'"([^"]+)"')

PATTERNS = [
    # mh::tr("k") / trMessage("k") / table.tr("k") / instance().tr("k")
    re.compile(r'(?:mh::tr|trMessage|table\.tr|instance\(\)\.tr)\(\s*' + STR),
    # trVm("k") — the view-model/table alias. Measured 17 call sites in src/,
    # so a typo in any of them used to reach all three gates green.
    re.compile(r'trVm\(\s*' + STR),
    # A key chosen by a ternary: tr(a ? "k1" : "k2"). The conditional sits
    # between the callee and the literal, so match the literal on either
    # branch instead of assuming we know the preceding token.
    re.compile(r'(?:mh::tr|trMessage|trVm|table\.tr|instance\(\)\.tr)'
               r'\((?:[^()"]*?[?][^()]*?)?\s*' + STR),
    # Keys pulled out of a static array or list:
    # trVm(kNameKeys[i]) / tr(QString::fromLatin1(kProbeKeys[i])). Only the
    # literal inside the array initialiser is matchable from the call site,
    # so the arrays themselves are enumerated by ARRAY_PATTERNS below.
    re.compile(r'tr(?:Set|Tip|Hook)\([^,()]+,\s*' + STR),
    # trWindowTitle("k")
    re.compile(r'trWindowTitle\(\s*' + STR),
]

# Static key tables: the literals live in the initialiser, and the call site
# only names the array. Without this, a typo inside kCorners[] is invisible to
# every gate. Matched against the whole file, not a call expression.
#
# Only tables whose name marks them as keys are enumerated. Matching every
# braced initialiser in the tree would also swallow path fragments, JSON
# field names and protocol tokens, which are formats, not translations --
# the check gate then fails on strings that were never meant to resolve.
ARRAY_NAME_HINT = re.compile(r'[Kk]eys?|Corners|Names?$', re.IGNORECASE)
ARRAY_PATTERNS = [
    # const char *const kNameKeys[] = { "a", "b" };
    # static const QVector<QString> kCorners = { "a", "b" };
    # Group 1 is the name, group 2 the brace body; ARRAY_NAME_HINT vets 1.
    # (No \K here: Python's re has no \K, unlike PCRE.)
    re.compile(r'\b(?:static\s+)?(?:const\s+)?'
               r'(?:QVector<QString>|QStringList|const\s+char\s*\*|const\s+QString)\s+'
               r'([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[\s*\d*\s*\])?\s*=\s*\{([^{}]*)\}'),
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
                # Static key tables hold their literals in the initialiser, so
                # the call site never shows them. Enumerate the table itself.
                for pat in ARRAY_PATTERNS:
                    for m in pat.finditer(src):
                        if not ARRAY_NAME_HINT.search(m.group(1)):
                            continue  # not a key table — formats don't translate
                        for lit in STR_LITERAL.finditer(m.group(2)):
                            used.setdefault(lit.group(1), set()).add(full)

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
