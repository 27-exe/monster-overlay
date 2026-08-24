# Probe tools

Seven CLI binaries built alongside the overlay. Each one reads a
slice of the live game's memory and prints a dump. They're the
"if you don't trust the overlay, run this and see for yourself"
tools.

All of them require the game to be running. If it isn't, they
exit with code 1 (most) or 3 (`monster-probe-ailments` /
`monster-probe-ailments-watch`) and print a one-line "no MHW process"
diagnostic.

`ctest` registers all seven with `SKIP_RETURN_CODE 1;3` so the
test suite stays green on a machine without MHW.

---

## When to use which

| You're investigating...                  | Use                                            |
|------------------------------------------|------------------------------------------------|
| Basic connectivity (PID, image base)    | `monster-probe`                                    |
| Whether `.map` is right (monster found?) | `monster-probe`                                    |
| Where the monster struct table lives     | `monster-scan`                                     |
| Per-part HP reading (normal/severable)   | `monster-probe-parts [--watch]`                    |
| Monster ailment structure offset         | `monster-probe-ailments` (1-shot)                  |
| Catching a short-lived ailment trigger   | `monster-probe-ailments-watch <log>`               |
| Mantle equipped ID + timers + cooldowns  | `monster-probe-mantles`                            |
| Mantle offsets (unexplained)             | `monster-probe-mantles-wide`                       |
| Enrage buildup field (legacy)            | `monster-probe-buildup-fast` (not built by CMake)  |

---

## `monster-probe`

**What it does**: locate the MHW PE process, derive the image base
from `/proc/<pid>/maps`, attempt a 32-byte read at the offset that
the `.map` file calls `MONSTER_LIST_ADDRESS`. Prints errno and the
raw buffer when a read fails.

**When to use it first**: when nothing else works. If this can't
read 32 bytes from the alleged monster list address, no other probe
will either — the problem is upstream (PID resolution, image base
derivation, or the .map is for the wrong build).

```bash
./build/monster-probe
# expected output:
#   MHW pid=1614174
#   imageBase=0x140000000
#   (no output = read succeeded silently; bad reads are flagged with errno)
```

## `monster-scan`

**What it does**: scans the executable's heap range for the
`em\em<N>` string signature. Every big monster's component struct
has its name at `+0x2A0` starting with `em\em` (e.g. `em\em100_00`).
The scan is O(range) but bounded — typical 421810+ builds find 70+
big monsters in a few seconds.

**When to use it**: when the overlay reports "no monsters" or
"wrong monster names" but `monster-probe` says the image base is fine.
Either the `.map`'s `MONSTER_LIST_ADDRESS` is wrong for your build,
or the offsets drift. `monster-scan` finds the actual location.

```bash
./build/monster-scan
# emits 70+ lines like:
#   em\em094_00  -> 0x...
```

The found addresses let you patch `.map` manually, or compare
against a known-good build of HunterPie's `monster_table_address`
in the same `MonsterHunterWorld.<buildid>.map` from upstream.

## `monster-probe-parts`

**What it does**: walks the monster list the same way the overlay
does, finds the live monster (HP > 1000, name non-empty), reads
`partPtr = *(monster + 0x1D058)`, then dumps 16 entries from the
**normal** table at `partPtr + 0x40` (stride `0x1F8`) and 32 from
the **severable** table at `partPtr + 0x1FC8` (stride `0x78`).
Each slot prints its raw header bytes + decoded `MaxHealth`,
`Health`, `Counter`, `Index`, `ExtraMaxHealth`, `ExtraHealth`.

**With no args** (or `--watch`): samples first 6 normal + 6
severable slots every second for 8 ticks. This is the canonical
"is the field actually live, or is the overlay's display frozen"
test — flinch should tick up, HP should drop, Counter should
increment on a part break.

```bash
./build/monster-probe-parts              # one-shot dump
./build/monster-probe-parts --watch      # 1Hz × 8 ticks
```

## `monster-probe-ailments`

**What it does**: tries **9 candidate struct offsets** in one
run, so you can see which one yields real data. The candidates are
`+0x148` (HunterPie's), `+0x00`, `+0x80`, `+0xA0`, `+0xC0`,
`+0x0A0` against a base that varies around `monster+0x1BC40`.

**When to use it**: when the overlay's ailment section is empty
or always shows garbage. This is the **last-resort diagnostic for
ailment offsets**.

```bash
./build/monster-probe-ailments
```

Look for the line that says `IsActive=1` with a sensible-looking
`Duration` (a number that ticks down). The offset column tells
you which struct offset to use.

## `monster-probe-ailments-watch`

**What it does**: same as `monster-probe-ailments` but samples every
**second** for as long as you let it run, appending to a log file.
**Use this** when you need to catch a short-lived trigger
(sleep, paralysis, etc.) that you can't time manually with a
one-shot probe.

```bash
./build/monster-probe-ailments-watch /tmp/ailments.log
# Ctrl-C to stop
# afterwards:
grep -E 'IsActive=1' /tmp/ailments.log
```

The log is line-oriented and grep-friendly. Search for the ailment
ID you care about (e.g. `id=3` for sleep, per
`HunterPie/Game/World/Data/MonsterData.xml` Ailments section).

## `monster-probe-mantles`

**What it does**: reads the 4-byte `equippedIds` at
`equipmentBase + 0x34`, the 40-float `timers` at
`equipmentBase + 0xA8C` (current, max), and the 40-float
`cooldowns` at `equipmentBase + 0x99C` (current, max). 1 Hz
forever. Same idea as `monster-probe-ailments-watch` for the
mantle subsystem.

```bash
./build/monster-probe-mantles
# look for non-zero entries in slots 0..7 (mantles) and 8..19 (other)
```

The **enum → label** mapping is in `HunterPie.Core/Game/Enums/
SpecializedToolType.cs` (in upstream HunterPie). For reference,
ID=3 is `RocksteadyMantle` (the "immovable" mantle, 90 s active
/ 360 s cooldown in 421810).

## `monster-probe-mantles-wide`

**What it does**: dumps a 0x300-byte window from
`equipmentBase + 0x900` to `equipmentBase + 0xC00`. This is the
"the HunterPie offsets don't apply to my build" diagnostic —
the cooldowns + timers arrays should be in this range regardless.

**When to use it**: when `monster-probe-mantles` returns all-zero
cooldowns, the array is somewhere else and this dump tells you
where (look for sequences of plausible cooldown floats — typically
120..360 — in a row of 20).

```bash
./build/monster-probe-mantles-wide
```

## `monster-probe-buildup-fast` (legacy, not built)

**Status**: not in the current CMake build. Captures enrage
buildup at 4 Hz for tuning the polling frequency. Replaced by the
log of the live overlay itself; keep this around if you want to
debug polling-rate artifacts without the overlay's UI overhead.

---

## The pattern

The general workflow is:

1. **`monster-probe`** — does the basic plumbing work?
2. **`monster-scan`** — does the .map file point at real data?
3. **`monster-probe-parts` / `monster-probe-mantles` / `monster-probe-ailments`** —
   per-subsystem field validity.
4. **`<probe>-watch`** variant — capture time-bounded events.
5. **`monster-probe-*-wide`** — last-resort "field is somewhere else"
   diagnosis.

Each probe exits non-zero on failure so you can `ctest` them and
the suite stays green. The probes **never modify** the game
process; they're all read-only.