#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Console i18n copy semantics check (before/after evidence script).

Usage:  python3 tests/copy_semantics_check.py <label>

Prints, for src/resources/i18n/{zh-CN,en-US}/console.json:
  * console-section key count per locale + key-set difference
  * per-key placeholder (%1 / {n} ...) comparison + a placeholder summary
  * which keys are referenced by C++ literals in src/ (NativeUI visible set)
  * banned ambiguous / prototype-era terms found in values
  * presence of the keys reserved for the new controls (item 5)

Exit code 1 when a hard invariant is violated (key-set mismatch, placeholder
mismatch, banned term in a NativeUI-visible value, missing reserved key).
"""

import json
import os
import re
import sys

WT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
I18N = os.path.join(WT, "src", "resources", "i18n")

# `--root DIR` points the loader at another tree (e.g. the pristine HEAD copy
# extracted for a before/after run). The src/ literal scan always uses this
# script's own repository: the C++ call sites, not the locale files, define
# what NativeUI shows.
_argv = sys.argv[1:]
if "--root" in _argv:
    _i = _argv.index("--root")
    _root = _argv[_i + 1]
    del _argv[_i:_i + 2]
    _candidate = os.path.join(_root, "src", "resources", "i18n")
    I18N = _candidate if os.path.isdir(_candidate) else I18N
sys.argv = [sys.argv[0]] + _argv


# Keys reserved for the controls a later change wires up (task item 5).
RESERVED = [
    "console.layout.mixed", "console.layout.group_title",
    "console.layout.detached", "console.layout.merged",
    "console.layout.merge_all", "console.layout.detach_all",
    "console.layout.custom_hint",
    "console.preview.fit", "console.preview.actual_size",
    "console.preview.zoom", "console.preview.size",
    "console.preview.height", "console.preview.hint",
    "console.preview.resize_tip",
    "console.edit.overlay_start", "console.edit.overlay_tip",
    "console.inspector.columns", "console.inspector.min_width",
    "console.inspector.min_height", "console.inspector.fixed_width",
    "console.inspector.fixed_height", "console.inspector.auto_size",
]

# Ambiguous / colloquial / prototype-era wording that must not survive in a
# string the Qt console (NativeUI) can render.  Checked in every value of
# both locales, which is a superset of the NativeUI-visible set.
BANNED = {
    # zh-CN additionally bans latin "demo" — mixed-language copy.
    "zh-CN": ["分开密度", "元数据", "舞台", "localStorage", "浏览器",
              "HTML", "原型", "demo", "Demo"],
    # en-US bans "demo" nowhere: "demo data" is the accurate English term.
    "en-US": ["Metadata", "metadata", "localStorage", "Browser", "browser",
              "HTML", "prototype", "Prototype", "stage", "Stage", "STAGE"],
}

PLACEHOLDER = re.compile(r"%\d+|\{[A-Za-z_][A-Za-z0-9_]*\}")
LITERAL = re.compile(r'"(console\.[A-Za-z0-9_.\-]+)"')


def flat(node, prefix="console"):
    out = {}
    for k, v in node.items():
        key = "%s.%s" % (prefix, k)
        if isinstance(v, dict):
            out.update(flat(v, key))
        else:
            out[key] = v
    return out


def load(locale):
    path = os.path.join(I18N, locale, "console.json")
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    return flat(data["console"])


def nativeui_visible_keys():
    """Keys the C++ sources reference as string literals (NativeUI)."""
    hits = set()
    src = os.path.join(WT, "src")
    for root, _dirs, files in os.walk(src):
        for name in files:
            if not name.endswith((".cpp", ".h", ".cc", ".hpp")):
                continue
            with open(os.path.join(root, name), encoding="utf-8",
                      errors="replace") as fh:
                hits.update(m.group(1) for m in LITERAL.finditer(fh.read()))
    return hits


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else "check"
    zh, en = load("zh-CN"), load("en-US")
    visible = nativeui_visible_keys() & set(zh)
    problems = []

    print("=== [%s] console.json ===" % label)
    print("locale key counts: zh-CN=%d  en-US=%d" % (len(zh), len(en)))
    only_zh = sorted(set(zh) - set(en))
    only_en = sorted(set(en) - set(zh))
    print("key set difference: only_zh=%s only_en=%s"
          % (only_zh or "{}", only_en or "{}"))
    if only_zh or only_en:
        problems.append("key sets differ between locales")

    ph_mismatch = [k for k in sorted(set(zh) & set(en))
                   if PLACEHOLDER.findall(str(zh[k]))
                   != PLACEHOLDER.findall(str(en[k]))]
    print("per-key placeholder mismatches: %s" % (ph_mismatch or "none"))
    if ph_mismatch:
        problems.append("placeholder mismatch: %s" % ph_mismatch)

    all_ph = {}
    for locale, table in (("zh-CN", zh), ("en-US", en)):
        for k, v in table.items():
            for tok in PLACEHOLDER.findall(str(v)):
                all_ph.setdefault(tok, set()).add(locale)
    with_ph = [k for k in zh if PLACEHOLDER.search(str(zh[k]))]
    print("placeholder summary: distinct=%s  keys carrying placeholders: "
          "zh-CN=%d en-US=%d"
          % (sorted(all_ph), len(with_ph),
             len([k for k in en if PLACEHOLDER.search(str(en[k]))])))
    print("placeholder signature (zh-CN): %s"
          % sorted({("%s=%s" % (k, ",".join(PLACEHOLDER.findall(str(zh[k])))))
                    for k in with_ph}))
    print("placeholder signature (en-US): %s"
          % sorted({("%s=%s" % (k, ",".join(PLACEHOLDER.findall(str(en[k])))))
                    for k in en if PLACEHOLDER.search(str(en[k]))}))

    # NativeUI-visible set (referenced by C++ literals, minus keys that do
    # not exist in the JSON at all).
    missing_visible = sorted(k for k in nativeui_visible_keys()
                             if k.startswith("console.")
                             and k not in zh)
    print("C++ literals with no JSON key: %s" % (missing_visible or "none"))
    print("NativeUI-visible keys: %d / %d" % (len(visible), len(zh)))

    hits = []
    for locale, table in (("zh-CN", zh), ("en-US", en)):
        for term in BANNED[locale]:
            for k in sorted(table):
                if term in str(table[k]):
                    hits.append((locale, k, term, k in visible))
    print("banned terms: %d hit(s)" % len(hits))
    for locale, k, term, vis in hits:
        print("   %s %s term=%r visible=%s"
              % (locale, k, term, vis))
    if any(vis for *_x, vis in hits):
        problems.append("banned term in a NativeUI-visible value")

    # A parsed value must never contain "\uXXXX" as literal text: that is the
    # double-escaped form the JSON shipped with (it rendered as \u201c on
    # screen), and it must never contain a raw dot-path (leaked key).
    escaped = [(loc, k) for loc, table in (("zh-CN", zh), ("en-US", en))
               for k, v in table.items() if re.search(r"\\u[0-9A-Fa-f]{4}", str(v))]
    leaked = [(loc, k) for loc, table in (("zh-CN", zh), ("en-US", en))
              for k, v in table.items() if "console." in str(v)]
    empty_vals = [(loc, k) for loc, table in (("zh-CN", zh), ("en-US", en))
                  for k, v in table.items() if not str(v).strip()]
    print("literal \\uXXXX leftovers: %s" % (escaped or "none"))
    print("values containing a raw dot-path: %s" % (leaked or "none"))
    print("empty values: %s" % (empty_vals or "none"))
    if escaped:
        problems.append("literal escape left in value: %s" % escaped)
    if leaked:
        problems.append("raw key path in a value: %s" % leaked)
    if empty_vals:
        problems.append("empty value: %s" % empty_vals)

    absent = [k for k in RESERVED if k not in zh or k not in en]
    empty = [k for k in RESERVED
             if k in zh and (not str(zh[k]).strip() or not str(en[k]).strip())]
    print("reserved keys present in both locales: %d/%d"
          % (len(RESERVED) - len(absent), len(RESERVED)))
    if absent:
        print("   missing: %s" % absent)
        problems.append("reserved keys missing: %s" % absent)
    if empty:
        print("   empty value: %s" % empty)
        problems.append("reserved keys with empty value: %s" % empty)

    print("RESULT: %s" % ("FAIL " + "; ".join(problems) if problems else "OK"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
