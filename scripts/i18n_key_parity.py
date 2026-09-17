#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""i18n key-parity check for the monster-overlay locale directories.

Every locale is a DIRECTORY of domain files under src/resources/i18n/<locale>/
(see src/core/string_table.cpp). This script flattens each locale the same way
the C++ loader does (nested object -> dot-path leaf, "_meta" keys skipped) and
asserts that the key sets are equal, per file and overall.

Delivered by the WS-C i18n workstream (ws-c/tools/i18n_key_parity.py);
promoted to scripts/ during integration.

Usage:
    python3 scripts/i18n_key_parity.py [--root src/resources/i18n] \
        [--locales zh-CN en-US] [--json OUT]

Exit code 0 = parity, 1 = mismatch (missing/extra keys are printed).
"""

import argparse
import json
import sys
from pathlib import Path

SKIP_KEYS = {"_meta"}


def flatten(obj, prefix=""):
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


def load_locale(locale_dir):
    files = sorted(p for p in locale_dir.glob("*.json"))
    per_file = {}
    merged = {}
    for path in files:
        with path.open(encoding="utf-8") as fh:
            doc = json.load(fh)
        if not isinstance(doc, dict):
            raise SystemExit(f"{path}: top level is not an object")
        flat = flatten(doc)
        per_file[path.name] = flat
        merged.update(flat)
    return files, per_file, merged


def main():
    # Default root resolves against the repo (script lives in <repo>/scripts/).
    repo_root = Path(__file__).resolve().parents[1]
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(repo_root / "src/resources/i18n"))
    ap.add_argument("--locales", nargs="+", default=["zh-CN", "en-US"])
    ap.add_argument("--json", dest="json_out", default=None)
    args = ap.parse_args()

    root = Path(args.root)
    if not root.is_dir():
        print(f"FAIL: locale root not found: {root}")
        return 2

    data = {}
    for loc in args.locales:
        d = root / loc
        if not d.is_dir():
            print(f"FAIL: locale directory missing: {d}")
            return 2
        files, per_file, merged = load_locale(d)
        data[loc] = {"files": files, "per_file": per_file, "merged": merged}
        print(f"{loc}: {len(files)} file(s), {len(merged)} keys")

    base = args.locales[0]
    failures = 0
    report = {}

    # Per-file parity (same file must exist everywhere with equal keys).
    base_files = {p.name for p in data[base]["files"]}
    for loc in args.locales[1:]:
        files = {p.name for p in data[loc]["files"]}
        missing = sorted(base_files - files)
        extra = sorted(files - base_files)
        if missing or extra:
            failures += 1
            print(f"FAIL [{loc}]: file-set mismatch; missing={missing} extra={extra}")

    # Key parity per file + merged.
    for name in sorted(base_files):
        sets = {loc: set(data[loc]["per_file"].get(name, {})) for loc in args.locales}
        ref = sets[base]
        for loc in args.locales[1:]:
            miss = sorted(ref - sets[loc])
            extra = sorted(sets[loc] - ref)
            if miss or extra:
                failures += 1
                print(f"FAIL [{loc}/{name}]: missing={len(miss)} extra={len(extra)}")
                for k in miss[:20]:
                    print(f"    missing: {k}")
                for k in extra[:20]:
                    print(f"    extra:   {k}")
            report.setdefault(name, {})[loc] = {
                "missing": len(miss), "extra": len(extra)}

    # Empty values are almost always a merge accident — flag, don't fail.
    for loc in args.locales:
        empties = sorted(k for k, v in data[loc]["merged"].items()
                         if isinstance(v, str) and not v.strip())
        if empties:
            print(f"warn [{loc}]: {len(empties)} empty value(s): {empties[:10]}")

    if args.json_out:
        import json as _json
        Path(args.json_out).write_text(_json.dumps(report, indent=2), encoding="utf-8")

    if failures:
        print(f"\nPARITY FAILED ({failures} problem group(s))")
        return 1
    print("\nPARITY OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
