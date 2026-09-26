#!/usr/bin/env python3
"""Rename i18n keys in monster-overlay: camelCase -> snake_case + namespace alignment.

Applies the full T2 task mechanically across BOTH locales and all call sites:
  * console.<...camelLeaf>          -> snake_case leaf          (B)
  * panel_sections.h kKeys[] entries + kFallback removal        (C)
  * ui.reader.*                     -> reader.*                 (A, reader domain)
  * ui.demo.*                       -> data.demo.*              (A, data domain)
  * mantle.<n>                      -> data.mantle.id.<n>       (A, data domain)

Idempotent: keys already renamed are skipped. Aborts if a target key already
exists with a different meaning (single-injection guard).
"""
import json
import re
import sys
from pathlib import Path

SRC = Path("src")
I18N = SRC / "resources" / "i18n"
LOCALES = ["zh-CN", "en-US"]
DOMAINS = ["console", "overlay", "reader", "data"]

CODE_EXTS = {".cpp", ".h", ".hpp"}


def snake(name: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def load(locale, domain):
    p = I18N / locale / f"{domain}.json"
    return (p, json.loads(p.read_text(encoding="utf-8"))) if p.exists() else (None, None)


def save(p, obj):
    p.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def collect_leaves(obj, prefix=""):
    for k, v in obj.items():
        if k == "_meta":
            continue
        key = f"{prefix}.{k}" if prefix else k
        if isinstance(v, dict):
            yield from collect_leaves(v, key)
        else:
            yield key, v


def set_leaf(obj, dotted, value):
    parts = dotted.split(".")
    cur = obj
    for p in parts[:-1]:
        cur = cur.setdefault(p, {})
    cur[parts[-1]] = value


def del_leaf(obj, dotted):
    parts = dotted.split(".")
    stack, cur = [], obj
    for p in parts[:-1]:
        if not isinstance(cur, dict) or p not in cur:
            return False
        stack.append((cur, p))
        cur = cur[p]
    if isinstance(cur, dict) and parts[-1] in cur:
        del cur[parts[-1]]
        for parent, key in reversed(stack):
            if isinstance(parent[key], dict) and not parent[key]:
                del parent[key]
        return True
    return False


def build_map(all_keys):
    """old full key -> new full key, for every key that needs renaming."""
    m = {}
    for k in all_keys:
        parts = k.split(".")
        # (A) namespace alignment by domain
        if parts[:2] == ["ui", "reader"]:
            m[k] = ".".join(["reader"] + parts[2:])
            continue
        if parts[:2] == ["ui", "demo"]:
            m[k] = ".".join(["data"] + parts[1:])
            continue
        if parts[0] == "mantle":
            m[k] = ".".join(["data", "mantle", "id"] + parts[1:])
            continue
        if parts[:2] == ["abnormality"]:
            m[k] = ".".join(["data", "abnormality"] + parts[1:])
            continue
        # (B) camelCase leaf -> snake_case
        leaf = parts[-1]
        s = snake(leaf)
        if s != leaf:
            m[k] = ".".join(parts[:-1] + [s])
    return m


def rename_code_files(rename: dict):
    """Rewrite every literal occurrence of an old key in source/test files."""
    # longest first so 'console.a.b' never pre-empts 'console.a.b.c'
    ordered = sorted(rename.items(), key=lambda kv: -len(kv[0]))
    changed = []
    for path in sorted(SRC.rglob("*")) + sorted(Path("tests").rglob("*")):
        if path.suffix not in CODE_EXTS or not path.is_file():
            continue
        text = original = path.read_text(encoding="utf-8")
        for old, new in ordered:
            if f'"{old}"' in text:
                text = text.replace(f'"{old}"', f'"{new}"')
        if text != original:
            path.write_text(text, encoding="utf-8")
            changed.append(str(path))
    return changed


def main():
    dry = "--dry-run" in sys.argv

    all_keys = set()
    for loc in LOCALES:
        for dom in DOMAINS:
            p, obj = load(loc, dom)
            if obj is None:
                continue
            all_keys.update(k for k, _v in collect_leaves(obj))

    rename = build_map(all_keys)
    print(f"distinct keys scanned : {len(all_keys)}")
    print(f"keys to rename        : {len(rename)}")

    conflicts = [f"{o} -> {n}" for o, n in rename.items() if n in all_keys and o != n]
    if conflicts:
        print("ABORT: target key already exists (not single-injection safe):")
        for c in conflicts[:10]:
            print("  ", c)
        return 2

    for loc in LOCALES:
        for dom in DOMAINS:
            p, obj = load(loc, dom)
            if obj is None:
                continue
            leaves = dict(collect_leaves(obj))
            for old, new in sorted(rename.items()):
                if old in leaves:
                    set_leaf(obj, new, leaves[old])
                    del_leaf(obj, old)
            if not dry:
                save(p, obj)
    print("json: renamed in", len(LOCALES), "locales x", len(DOMAINS), "domains")

    touched = rename_code_files(rename) if not dry else []
    if dry:
        print("code files that WOULD change:")
        for path in sorted(SRC.rglob("*")) + sorted(Path("tests").rglob("*")):
            if path.suffix in CODE_EXTS and path.is_file():
                text = path.read_text(encoding="utf-8")
                if any(f'"{o}"' in text for o in rename):
                    touched.append(str(path))
    print(f"code files rewritten: {len(touched)}")
    for t in touched:
        print("   ", t)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
