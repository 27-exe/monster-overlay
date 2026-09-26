#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Run the three i18n gates, or any one of them, with a single command.

WHEN TO RUN WHICH
-----------------
| script | question it answers | gate? | run it |
|---|---|---|---|
| `i18n_check_tr_keys.py` | does every key the code asks for EXIST? | **yes — always** | after adding/moving any key, and in CI |
| `i18n_key_parity.py` | do zh and en define the SAME keys? | **yes — always** | after editing any i18n JSON, and in CI |
| `i18n_unused_keys.py` | does any key nobody asks for still EXIST? | no — report only | during cleanup sweeps; `--strict` to gate |
| `i18n_split_domains.py` | (re)split the domain files | no — migration | once, from a clean worktree |

The three answer three different questions and all three are needed: a key that
exists in zh but not en (parity), a key the code wants but no JSON defines
(check_tr_keys), and a key no code wants at all (unused). The first two are
hard gates because a failure there is a bug the SHIPPING user sees. The third
is a report by default — a leftover key is invisible to the player, so gating
on it turns a cosmetic cleanup into a red build and teaches people to ignore
the gate. Flip it with `--strict` when you want it enforced (a good habit right
before a release, where the key set should be frozen).

Usage:
    python3 scripts/i18n_gate.py            # all three, default mode
    python3 scripts/i18n_gate.py --strict   # also fail on dead keys
    python3 scripts/i18n_gate.py parity     # one gate only
    python3 scripts/i18n_gate.py --list     # show the same table as above

Exit code: 0 when every requested gate passes, 1 on the first failure,
2 on a usage/environment error.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# name -> (relative path, extra argv builders, human description, default-gate?)
GATES = {
    "check": ("i18n_check_tr_keys.py", "used keys must resolve in EVERY locale"),
    "parity": ("i18n_key_parity.py", "zh/en key sets must be identical"),
    "unused": ("i18n_unused_keys.py", "report keys no one references"),
}
ORDER = ("check", "parity", "unused")


def run_one(name: str, strict: bool, quiet: bool) -> bool:
    script, description = GATES[name]
    path = HERE / script
    if not path.is_file():
        print(f"FAIL[{name}]: missing {script}", file=sys.stderr)
        return False
    argv = [sys.executable, str(path)]
    if strict:
        # `unused` honours --strict; the other two are always strict already,
        # so passing it there would be an unknown-flag error. Only `unused`
        # also needs --tests for a complete reference scan.
        if name == "unused":
            argv += ["--tests", "--strict"]
    elif name == "unused":
        argv += ["--tests"]
    print(f"\n=== {name}: {script} ({description}) ===")
    # quiet swallows the child's streams; on failure they are replayed below,
    # because the report is the only thing that explains WHY it failed.
    child_out = subprocess.PIPE if quiet else None
    child_err = subprocess.PIPE if quiet else None
    result = subprocess.run(argv, stdout=child_out, stderr=child_err, text=True)
    if quiet and result.returncode != 0:
        sys.stdout.write(result.stdout or "")
        sys.stderr.write(result.stderr or "")
    print(f"--- [{name}] exit={result.returncode} "
          f"{'PASS' if result.returncode == 0 else 'FAIL'}")
    return result.returncode == 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        epilog="gate names: " + ", ".join(ORDER))
    ap.add_argument("gates", nargs="*", choices=ORDER + ("all",),
                    help="which gate(s) to run (default: all)")
    ap.add_argument("--strict", action="store_true",
                    help="also fail when dead keys exist (unused defaults to a "
                         "report-only exit 0)")
    ap.add_argument("--list", action="store_true",
                    help="print the when-to-run-which table and exit")
    ap.add_argument("-q", "--quiet", action="store_true",
                    help="suppress per-gate stdout; keep the verdict lines")
    args = ap.parse_args(argv)

    if args.list:
        print(f"{'gate':<9} {'script':<26} gate by default?")
        for name in ORDER:
            script, description = GATES[name]
            gate = "no" if name == "unused" else ("yes" if not args.strict else "yes*")
            print(f"{name:<9} {script:<26} {gate}")
        print("\n* always; `--strict` additionally gates on `unused`")
        return 0

    selected = [g for g in (args.gates or ["all"]) if g != "all"] or list(ORDER)

    ok = True
    for name in selected:
        if not run_one(name, args.strict, args.quiet):
            ok = False

    print("\n" + ("ALL REQUESTED GATES PASSED" if ok else "SOME GATES FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
