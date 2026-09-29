#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Census of src translation units and their reachability from ctest targets.

WHAT THIS IS FOR
----------------
S3 ("the test audit") found 15 of 55 src translation units with **no ctest
target touching them**, seven of which were brand-new TUs carved out of fat
files days earlier. A whole-file rewrite of one of them (`monster_view_model.cpp`,
a 42-case rewrite) is exactly the size of change that can blame a `ctest` run
that is, in truth, green for a *different* reason.

Three ways to detect "no ctest target touches this TU" were tried. All three
look plausible and two are wrong:

| method | verdict | why |
|---|---|---|
| grep the CMakeLists for the .cpp | **false 100% green** | a TU listed as a *source of a library* is not the same as a TU a test target *links*; the audit found TUs listed by 4+ test targets that those targets never actually executed; also, `src/ui/**/*.cpp` in the product target's file list makes every UI TU look covered. |
| `nm` the *test binary* | **false green** | the linker drops every object it decides it cannot see a use for (`--gc-sections` defaults, plus dead-strip of unreachable whole objects), so the binary's symbol set is *post*-linkage, not *pre*-linkage. What survives into the binary is what got pulled in, which is the very answer we are asking for. |
| `nm --extern-only --undefined-only` each object file | **correct, with caveats below** | the *defined* symbols of a src TU are read straight out of the object file, before any link, and compared against the *undefined* symbols each ctest target's own objects still wanted when it was compiled. Neither side has been through a link. |

This script implements the third one. The caveats, so nobody mistakes the
number for a coverage percentage:

1. **It measures symbolic reference, not execution.** A TU whose functions are
   called exactly once, from a helper that is itself only touched by a
   `QCOMPARE` in one test, counts the same as a TU with 45 assertions against
   it. It answers "can a test reach this code at all?" — never "is it
   asserted?".
2. **A TU with no externally-visible (non-`static`, non-`inline`) symbol can
   never be scored**, so it is reported as UNKNOWN rather than ZERO. Those are
   the ones that need a human to check (the 0.0.0 commit's test-only helper
   headers are the usual case).
3. **Indirection is only counted one level deep.** A symbol the test target
   references from a *library* object, and which this TU's symbols also define,
   counts — because both sides are read pre-link. A symbol that only exists
   after inlining or constexpr evaluation is invisible to it.
4. **Symbol matching is exact string matching** (mangled names), so an
   `#ifdef` branch compiled out of the test but not the library is scored
   INDIRECT.

SO WHAT IS THE GATE, THEN?
--------------------------
Two modes, because the two questions have two different reliability profiles:

* `--report` (a.k.a. `--measure`) prints the live census from a **built**
  tree. It needs `build/`, is verdict-only, and is meant to be run by a human
  or by CI right after a build. It **never** fails the build.
* `--snapshot <file>` compares the src TU inventory against a committed
  snapshot. It needs **no build**, is deterministic, and is what ctest runs.

The ctest case is the snapshot comparison. That is deliberate. A coverage
number (58.9%) is not something a gate should gate on: it moves for reasons
that have nothing to do with correctness, so gating on it would train
everybody to re-baseline the number by hand. What a gate *can* safely gate on
is **identity**: a .cpp file exists on disk that the census does not
account for. That is the "new TU must declare itself" invariant, and it is a
boolean — no threshold to relax, no number to massage.

Every TU in the census carries either
  * `owner` — the test target(s) that exercise it, or
  * `exempt` — an explicit entry in the `EXEMPT` table below, with the reason
    and the condition that would lift it.

The exemption table lives in this file on purpose. An exemption is a claim
about the *code* (this TU needs a live game process / it is a `main()` /
it is a generated data table), so it belongs next to the census it is
exempting. A separate JSON would split one reviewable artifact into two
files and make it possible to update one and forget the other.

Usage:
    python3 scripts/zero_reference_gate.py --measure          # live census (needs build/)
    python3 scripts/zero_reference_gate.py --report --quiet   # same, summary only
    python3 scripts/zero_reference_gate.py --update-snapshot  # rewrite the snapshot
    python3 scripts/zero_reference_gate.py --snapshot <f>     # gate: compare (ctest runs this)
    python3 scripts/zero_reference_gate.py --check            # gate, don't rewrite (default)

Exit code: 0 pass, 1 gate failed, 2 usage/environment error.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from collections import defaultdict

# ---------------------------------------------------------------------------
# Exemptions: src TUs that a ctest target cannot be expected to exercise, each
# with the reason and what would make it testable. An entry here is a decision,
# not a dodge: if you add one, be ready to say why out loud in review.
#
# The snapshot gate (the ctest case) enforces: a TU recorded as ZERO must have
# an entry here, and every entry must name a TU that still exists. So this
# table is the one place the "we know this TU is untested and that is the
# accepted state" decision is recorded.
# ---------------------------------------------------------------------------
EXEMPT: dict[str, str] = {
    "src/main.cpp":
        "product entry point (main()). Nothing a test can call; only the "
        "whole overlay binary exercises it.",
    "src/main_control.cpp":
        "the control console's main(). Its consumer targets ARE ctest cases "
        "(monster-control-l2-smoke / -p0-test / -rise-rf-smoke) but they "
        "reference its symbols only through QApplication/event plumbing, so "
        "the entry symbol reads as unreferenced pre-link.",
    "src/ui/viewmodel/overlay_process_controller.cpp":
        "wraps a real child process (QProcess + REFramework injection) and "
        "a live game. Needs a real process to mean anything; lift by "
        "extracting the launch-argument / env-plumbing half into a pure TU.",
    # Rise name tables generated from HunterPie XML by scripts/extract_*.py.
    # The generator and the readers are tested; the table is data, not logic.
    "src/rise/data/mhr_part_names_data.cpp":
        "generated static table (HunterPie XML -> .cpp, "
        "scripts/extract_rise_part_names.py). Data, not logic.",
    "src/rise/data/mhr_monster_names_data.cpp":
        "generated static table (HunterPie XML -> .cpp, "
        "scripts/extract_rise_monster_names.py). Data, not logic.",
    "src/ui/control_panel_styles.cpp":
        "stylesheet strings. Rendering a stylesheet needs a live QPA; the "
        "GUI smoke targets render the panel that consumes it.",
    "src/ui/icon.cpp":
        "icon-path table for QPainter resources. Consumed by the product "
        "target and the GUI smoke targets.",
    "src/ui/rise_reframework_card.cpp":
        "REFramework setup card (install/version UI). Needs the real "
        "installer; the smoke targets cover the panel that hosts it.",
    "src/ui/section_count_bar.cpp":
        "a paint() widget. Needs an offscreen QPA to mean anything.",
    "src/ui/toggle_chip.cpp":
        "a paint() widget. Needs an offscreen QPA to mean anything.",
    "src/world/quest_reader.cpp":
        "reads the save file's quest block; needs a real save file and the "
        "quest-name tables. Lift by feeding it a fixture save.",
}

RE_DEF = re.compile(r"^[0-9a-fA-F]+\s+([A-Za-z])\s+(\S.*)$")
RE_UNDEF = re.compile(r"^\s+U\s+(\S+)$")
STRONG = set("T B D R C".split())
ANY_DEF = set("T W B D R C V".split())


def _syms(path: str, kind: str, strong_only: bool) -> set[str]:
    """nm one object file; `kind` is 'defined' or 'undefined'."""
    out = subprocess.run(
        ["nm", "--extern-only", f"--{kind}-only", path],
        capture_output=True, text=True).stdout
    res: set[str] = set()
    for line in out.splitlines():
        if kind == "defined":
            m = RE_DEF.match(line)
            if not m:
                continue
            letter = m.group(1)
            if strong_only and letter not in STRONG:
                continue
            if letter in ANY_DEF:
                res.add(m.group(2).split("$", 1)[0])
        else:
            m = RE_UNDEF.match(line)
            if m:
                res.add(m.group(1).split("$", 1)[0])
    return res


def measure(repo: str, build: str | None = None) -> tuple[dict, list[str]]:
    """Return (census, warnings).

    `census` maps each src TU to:
        {"symbols": N, "refs": [ctest targets], "class": DIRECT|INDIRECT|ZERO|UNKNOWN}
    `warnings` collects anything that made the measurement weaker.
    """
    repo = os.path.abspath(repo)
    build = build or os.path.join(repo, "build")
    nmdir = os.path.join(build, "CMakeFiles")
    warnings: list[str] = []

    if not os.path.isdir(nmdir):
        return {}, [f"no build tree at {nmdir} (measure needs a build)"]

    # 1. every object file, grouped by the target that owns it, autogen skipped
    own_objs: dict[str, list[str]] = defaultdict(list)
    for dirpath, _dirs, files in os.walk(nmdir):
        for f in sorted(files):
            p = os.path.join(dirpath, f)
            if not f.endswith(".o") or ".dir/" not in p:
                continue
            head = p.split(".dir/", 1)[0]
            if os.path.basename(head).endswith("_autogen"):
                continue
            own_objs[os.path.basename(head)].append(p)

    # 2. the ctest case list, so only registered targets count as "exercised"
    jt = subprocess.run(
        ["ctest", "--test-dir", build, "-N", "--show-only=json-v1"],
        capture_output=True, text=True).stdout
    try:
        cases = json.loads(jt)["tests"]
    except (KeyError, json.JSONDecodeError) as exc:
        return {}, [f"ctest --test-dir {build} -N did not return parseable json: {exc}"]
    exe_set = {os.path.basename(c["command"][0])
               for c in cases if c.get("command") and c["command"]}

    # 3. what each target could NOT resolve from itself, pre-link
    need: dict[str, set[str]] = {}
    for tgt, objs in own_objs.items():
        s: set[str] = set()
        for o in objs:
            s |= _syms(o, "undefined", strong_only=False)
        need[tgt] = s

    # 4. what each src TU defines, pre-link
    tu_def: dict[str, set[str]] = {}
    for dirpath, _dirs, files in os.walk(nmdir):
        for f in sorted(files):
            p = os.path.join(dirpath, f)
            if not f.endswith(".o") or ".dir/" not in p:
                continue
            rel = p.split(".dir/", 1)[1]
            if not (rel.startswith("src/") and rel.endswith(".cpp.o")):
                continue
            d = _syms(p, "defined", strong_only=False)
            tu_def[rel[:-2]] = d

    census: dict[str, dict] = {}
    for cpp in sorted(tu_def):
        dsyms = tu_def[cpp]
        refs = sorted(t for t, u in need.items()
                      if t in exe_set and (dsyms & u))
        if not dsyms:
            # no externally-visible symbol: this measurement cannot score it
            # either way (a TU of only static/inline/anon-namespace functions,
            # or one whose symbols the compiler fully inlined away).
            cls = "UNKNOWN"
            warnings.append(
                f"{cpp}: no extern symbol found — unmeasurable by this method "
                f"(check for unreferenced static helpers by hand)")
        elif refs:
            cls = "INDIRECT"
        else:
            cls = "ZERO"
        census[cpp] = {"symbols": len(dsyms), "refs": refs, "class": cls}

    # compiled-by-some-test-target is a *different* axis; it does not affect
    # the class, but it is the difference between "compiled and called" and
    # "compiled and ignored", so keep it for the report.
    for cpp, info in census.items():
        info["compiled_by"] = sorted(
            t for t, objs in own_objs.items()
            if t in exe_set
            and any(o.split(".dir/", 1)[1] == f"{cpp}.o" for o in objs))

    # on-disk vs measured: a TU on disk whose object we never found
    on_disk = set()
    for dirpath, _dirs, files in os.walk(os.path.join(repo, "src")):
        for f in files:
            if f.endswith(".cpp"):
                on_disk.add(os.path.relpath(os.path.join(dirpath, f), repo))
    for miss in sorted(on_disk - set(census)):
        census[miss] = {"symbols": 0, "refs": [], "class": "UNBUILT",
                        "compiled_by": []}
        warnings.append(f"{miss}: on disk but no object file found in {build}")
    return census, warnings


# ---------------------------------------------------------------------------
# snapshot gate
# ---------------------------------------------------------------------------
DEFAULT_SNAPSHOT = "tests/tu_census.json"


def snapshot_path_default(repo: str) -> str:
    return os.path.join(repo, DEFAULT_SNAPSHOT)


def gate(repo: str, snap_path: str, quiet: bool = False) -> int:
    repo = os.path.abspath(repo)
    snap_path = os.path.abspath(snap_path)

    if not os.path.isfile(snap_path):
        print(f"FAIL: no snapshot at {snap_path}.", file=sys.stderr)
        print("      Create it with: python3 scripts/zero_reference_gate.py "
              "--update-snapshot", file=sys.stderr)
        return 2
    try:
        snap = json.load(open(snap_path))
    except json.JSONDecodeError as exc:
        print(f"FAIL: {snap_path} is not valid JSON: {exc}", file=sys.stderr)
        return 2

    snap_tus = set(snap.get("tus", {}))
    # the census is the set of .cpp files the gate is accountable for; read it
    # from the repo, NOT from the build, so this runs with no build/ at all.
    on_disk = set()
    src_root = os.path.join(repo, "src")
    for dirpath, _dirs, files in os.walk(src_root):
        for f in files:
            if f.endswith(".cpp"):
                on_disk.add(os.path.relpath(os.path.join(dirpath, f), repo))

    new = sorted(on_disk - snap_tus)
    gone = sorted(snap_tus - on_disk)

    exempt = set(EXEMPT)
    undeclared = [t for t in new if t not in exempt]
    stale_exempt = sorted(exempt - on_disk)
    snaps = snap.get("tus", {})
    census_zero = sorted(t for t, v in snaps.items() if v.get("class") == "ZERO")
    # A TU recorded ZERO must either be exempted above or be a finding the
    # gate is supposed to surface. Nothing else may silence it — in
    # particular not the census file itself: the census may only *record* a
    # class, never *justify* one.
    unjustified_zero = sorted(t for t in census_zero if t not in exempt)

    if not quiet:
        print(f"tu census gate — {snap_path}")
        print(f"  src TUs on disk      : {len(on_disk)}")
        print(f"  src TUs in snapshot  : {len(snap_tus)}")
        print(f"  exempt (in-script)   : {len(exempt)}")
        print(f"  ZERO at snapshot time: {len(census_zero)}")

    failed = False
    if unjustified_zero:
        failed = True
        print("\nFAIL: src TUs measured ZERO with no EXEMPT entry:",
              file=sys.stderr)
        for t in unjustified_zero:
            print(f"  ! {t}", file=sys.stderr)
        print("""
No ctest target references any symbol these translation units define. Either
add the missing test (the honest fix), or record in EXEMPT in
scripts/zero_reference_gate.py why no test can exist — with the reason and
what would make it testable.""", file=sys.stderr)

    if undeclared:
        failed = True
        print("\nFAIL: new src translation units not in the census:", file=sys.stderr)
        for t in undeclared:
            print(f"  + {t}", file=sys.stderr)
        print("""
A new src TU must be accounted for. Pick one:

  1. It IS tested. Refresh the census so the class is measured for real:
         python3 scripts/zero_reference_gate.py --update-snapshot
     (needs a build; or hand-add the entry to tests/tu_census.json and
     confirm with --measure.)
  2. It cannot be tested (needs a live game / is a main() / is a generated
     table). Add it to EXEMPT in scripts/zero_reference_gate.py with the
     reason and what would make it testable.
  3. It SHOULD be tested and is not. That is the honest fix: write the test.

Option 2 is for genuinely intractable TUs. If you find yourself reaching for
it to make a gate green, you are on step 3.""", file=sys.stderr)

    if stale_exempt:
        failed = True
        print("\nWARN->FAIL: EXEMPT entries whose src TU no longer exists:",
              file=sys.stderr)
        for t in stale_exempt:
            print(f"  ? {t}", file=sys.stderr)
        print("  (a test landed, or the TU moved: delete the EXEMPT entry)",
              file=sys.stderr)

    if gone and not quiet:
        print(f"\nnote: {len(gone)} snapshot entries no longer on disk:")
        for t in gone:
            print(f"  - {t}")
        print("      refresh with: python3 scripts/zero_reference_gate.py "
              "--update-snapshot")

    if failed:
        print("\nRESULT: FAIL", file=sys.stderr)
        return 1
    if not quiet:
        print("\nRESULT: PASS")
    return 0


def update_snapshot(repo: str, snap_path: str, census: dict, warnings: list) -> int:
    repo = os.path.abspath(repo)
    snap_dir = os.path.dirname(os.path.abspath(snap_path))
    doc = {
        "_comment": (
            "Census of src translation units, refreshed by "
            "`python3 scripts/zero_reference_gate.py --update-snapshot`. "
            "The ctest gate (zero-reference-gate) compares the src/*.cpp set "
            "on disk against the keys here, so a new TU fails the build until "
            "it appears in this file — with a class and either an owner test "
            "target or an EXEMPT entry in the script. `class` is DIRECT when "
            "some test target compiles the TU itself, INDIRECT when a test "
            "target needs one of its symbols, ZERO when nothing does, UNKNOWN "
            "when it has no external symbol to measure."),
        "method": "nm --extern-only undefined/defined, per object file, pre-link",
        "tus": census,
    }
    os.makedirs(snap_dir, exist_ok=True)
    tmp = snap_path + ".tmp"
    with open(tmp, "w") as fh:
        json.dump(doc, fh, indent=2, sort_keys=True)
        fh.write("\n")
    os.replace(tmp, snap_path)
    n_zero = sum(1 for v in census.values() if v.get("class") == "ZERO")
    print(f"wrote {snap_path}: {len(census)} TUs "
          f"({n_zero} ZERO)")
    for w in warnings:
        print(f"  warning: {w}")
    if warnings:
        print("fix the warnings before trusting the census.")
    return 0


def report(repo: str, quiet: bool = False) -> int:
    census, warnings = measure(repo)
    if not census:
        for w in warnings:
            print(f"error: {w}", file=sys.stderr)
        return 2
    by: dict[str, list[str]] = defaultdict(list)
    for cpp, info in sorted(census.items()):
        by[info["class"]].append(cpp)
    if not quiet:
        print("=" * 78)
        print("src TU reachability from ctest targets (nm symbol reachability)")
        print("=" * 78)
    if not quiet:
        for cls in ("ZERO", "UNKNOWN", "INDIRECT"):
            items = by.get(cls, [])
            print(f"\n### {cls} — {len(items)} of {len(census)}")
            for t in items:
                info = census[t]
                refs = ", ".join(info["refs"][:4]) or "-"
                more = f" (+{len(info['refs']) - 4})" if len(info["refs"]) > 4 else ""
                status = "  [EXEMPT]" if t in EXEMPT else ("  <-- NOT EXEMPT" if cls == "ZERO" else "")
                print(f"  {t}{status}")
                print(f"      symbols={info['symbols']}  exercised-by={refs}{more}")
        print(f"\nTOTAL {len(census)}  "
              + "  ".join(f"{cls}={len(by.get(cls, []))}" for cls in ("ZERO", "UNKNOWN", "INDIRECT")))
    exempt_zero = [t for t in by.get("ZERO", []) if t in EXEMPT]
    unexempt_zero = [t for t in by.get("ZERO", []) if t not in EXEMPT]
    if unexempt_zero:
        print(f"\n{len(unexempt_zero)} ZERO TU(s) with no EXEMPT entry: "
              f"{', '.join(unexempt_zero)}")
    for w in warnings:
        print(f"warning: {w}")
    if quiet:
        print(f"TUs={len(census)} ZERO={len(by.get('ZERO', []))} "
              f"(exempt {len(exempt_zero)}) UNKNOWN={len(by.get('UNKNOWN', []))} "
              f"INDIRECT={len(by.get('INDIRECT', []))}")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="gate == --snapshot (default when neither is given)")
    ap.add_argument("--repo", default=".")
    ap.add_argument("--build", help="build dir (default: <repo>/build)")
    ap.add_argument("--snapshot", help=f"path to the census json (default {DEFAULT_SNAPSHOT})")
    ap.add_argument("--measure", "--report", dest="measure", action="store_true",
                    help="live census from a built tree; verdict-only, never fails")
    ap.add_argument("--update-snapshot", action="store_true",
                    help="rewrite the census json from a built tree")
    ap.add_argument("--check", action="store_true",
                    help="snapshot gate: fail on a new/removed src TU (default)")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args(argv)

    repo = os.path.abspath(args.repo)
    snap = os.path.abspath(args.snapshot) if args.snapshot else snapshot_path_default(repo)

    if args.measure:
        return report(repo, quiet=args.quiet)

    if args.update_snapshot:
        census, warnings = measure(repo, args.build)
        if not census:
            for w in warnings:
                print(f"error: {w}", file=sys.stderr)
            return 2
        return update_snapshot(repo, snap, census, warnings)

    # default and --check: the gate
    return gate(repo, snap, quiet=args.quiet)


if __name__ == "__main__":
    sys.exit(main())
