"""Balance-check every HTML label in a .dot file and report offenders.

Usage: python3 check_dot.py <file.dot> [file2.dot ...]
Exits 1 if any label=< ... > region is unbalanced.

Known limitation: a label whose <TABLE> spans several source lines is counted
per line, so a multi-line note always reports as unbalanced.  The authoritative
test is `dot -Tsvg` succeeding; this script only catches the one-line cases.
"""
from __future__ import annotations

import re
import sys


def check(path: str) -> int:
    bad = 0
    for lineno, line in enumerate(open(path, encoding="utf-8").read().split("\n"), 1):
        # only inspect the HTML-label region, so braces in C++ text don't count
        start = line.find("label=<")
        if start < 0:
            continue
        rest = line[start + len("label=<"):]
        # an HTML label runs to the last '>' on the line
        end = rest.rfind(">")
        if end < 0:
            continue
        html = rest[:end]
        o, c = html.count("<"), html.count(">")
        if o != c:
            bad += 1
            print(f"{path}:{lineno}: unbalanced <{o} >{c} :: {line[:80]}")
    return bad


if __name__ == "__main__":
    total = 0
    for f in sys.argv[1:]:
        total += check(f)
    print("OK — labels balanced" if not total else f"{total} unbalanced label(s)")
    sys.exit(1 if total else 0)
