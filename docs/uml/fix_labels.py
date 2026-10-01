"""Repair HTML labels in a .dot file.

Two failure modes seen while authoring these diagrams:
  1. `label=TEXT<BR/>...` — TEXT contains HTML tags but the label was opened
     with `=` instead of `=<`.  Fix: insert the `<`.
  2. `label=<...<BR/>, fillcolor=` — the label string is never closed.  Fix:
     insert `>` immediately before `, fillcolor=` / `, shape=` / `, color=`
     / `, width=` / `, height=` / `, style=` / `, fontcolor=` / `, label=`
     / `;` end-of-statement, when the HTML is not already closed.

Writes <file>.fixed and prints the edits it made.
"""
from __future__ import annotations

import re
import sys

ATTR = r"(?:fillcolor|color|shape|style|fontcolor|fontsize|width|height|penwidth|group|peripheries|label|fontname|comment)"


def close_label(text: str) -> str:
    """Insert a missing '>' before the attribute list when needed."""
    m = re.search(r"label=<(?P<html>.*)", text)
    if not m:
        return text
    html = m.group("html")
    # find the last attribute boundary
    bm = re.search(r">\s*(?=,|\]|;)\s*$", html)
    if bm:
        return text  # already closed with a lone '>'
    # try appending '>' just before the first `, attr=` after the last tag
    tail = re.search(r"(,\s*%s\s*=.*)$" % ATTR, html)
    if not tail:
        return text
    head = html[: tail.start()]
    if head.rstrip().endswith("/>"):
        return text[: m.start("html") + tail.start()] + ">" + text[m.start("html") + tail.start():]
    return text


def open_label(text: str) -> str:
    if re.search(r"label=(?![<\"])", text) and "<" in text.split("label=", 1)[1]:
        return text.replace("label=", "label=<", 1)
    return text


def main() -> int:
    total = 0
    for path in sys.argv[1:]:
        out = []
        for lineno, line in enumerate(open(path, encoding="utf-8").read().split("\n"), 1):
            new = open_label(line)
            new = close_label(new)
            if new != line:
                total += 1
                print(f"{path}:{lineno}:")
                print(f"  - {line[:130]}")
                print(f"  + {new[:130]}")
            out.append(new)
        open(path + ".fixed", "w", encoding="utf-8").write("\n".join(out))
    print(f"{total} line(s) changed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
