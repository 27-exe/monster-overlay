# Usage Guide

`monster-overlay` (the panels), `monster-control` (the console) and
`monster-doctor` (diagnostics). Requirements and installation: see
[../README.md](../README.md).

## 1. Roles

| Binary | Role |
|---|---|
| `monster-overlay` | draws the layer-shell panels and reads the game process |
| `monster-control` | console: starts the overlay, switches panels/sections, positions them, picks the display and the language |
| `monster-doctor` | one-shot, read-only diagnostic report (see §8) |

## 2. First run

```bash
sudo ./install-deps.sh      # one-time on Arch: Qt 6 + layer-shell-qt
./monster-control           # opens the console
```

Start the game first and get past the main menu, then press **START** in the
console. The console saves the switch state, spawns `monster-overlay` with the
current masks as `--mask-*` / `--no-*` flags plus `--game=<world|rise>`, and
hides itself; the panels appear over the game. When the overlay exits (its own
`Esc`), the console re-shows. A second START while one is running is refused
rather than spawning a second overlay.

## 3. The control console

| Element | What it does |
|---|---|
| panel cards (player / monster / damage / pets) | master switch plus per-section switches; the preview updates as you toggle |
| `START` / `STOP` | starts / stops `monster-overlay`; the console hides while it runs and returns when it exits |
| `ENTER EDIT` | same as START, but with `--edit` |
| preview canvas | click to select a panel, drag to move it, arrow keys nudge 10 px (Shift = 50 px); `Ctrl`+wheel zooms the canvas itself, not the panels |
| display selector | which output the panels are placed on |
| `中文 | EN` chip | switches the UI language; the overlay follows within ~1 s |
| theme chip | light or dark console theme |
| `WORLD` / `RISE` buttons | which title the overlay is started against; also hot-swaps a running overlay's target game |
| detect badge | scans `/proc` every 5 s and names the running Monster Hunter process (green = matches the selection, amber = mismatch, grey = none) |
| `1` `2` `3` `4` | select the player / monster / damage / pets card |

The pets rail card, its inspector page and its preview tile exist only in Rise
mode — World has no data source for them, so selecting World removes them
rather than showing an empty card.

## 4. Positioning panels (edit mode)

```bash
./monster-overlay --edit        # no game required
```

| Input | Action |
|---|---|
| `←` `↑` `↓` `→` | nudge 10 px (`Shift` = 50 px) — edit mode only |
| mouse wheel | scale 0.5× – 3× — edit mode only |
| `Space` | collapse the panel to a small block — works in edit **and** live mode |
| `Esc` | quit the overlay and return focus to the console — works in edit **and** live mode |

Clicking a panel is what gives it keyboard focus (layer-shell interactivity goes
None → OnDemand and the compositor is asked to activate the surface). In the
overlay the arrow keys are the only repositioning control; dragging exists only
in the console's preview canvas.

**There is no `Ctrl+S`.** Position is persisted the moment it changes: every
nudge and wheel step writes `~/.config/monster-overlay/panels.ini` immediately, so
a SIGTERM or crash cannot lose it. Leaving edit mode (`Esc`, or quitting while
`--edit` is on) writes all four panels again; that is the only point where the
whole layout is flushed at once. In live mode the arrow keys and the wheel are
ignored, so the panels cannot drift while you hunt.

## 5. Panel contents

| Panel | Shows |
|---|---|
| Player | name, HP / stamina, sharpness, weapon, wirebug (Rise), mantles (World), ailments, food skills |
| Monster | current target, HP and part damage, ailments, enrage / stamina (Rise), crown size |
| Damage | per-player damage and DPS; on Rise it is fed by the REFramework producer (§10) |
| Pets | pet damage grouped by owner — Rise only; in World mode the rail card, its options and the floating window are not shown at all |

Two things in the Damage panel are worth knowing before you read its rows.

**Somebody leaving does not re-order anyone.** Every hunter keeps the row they
claimed for the whole hunt. In World mode, a hunter who drops out mid-quest
keeps their row on the panel: their name, damage and DPS stop where they were —
only the numbers stop moving, and the row itself is not greyed out or marked in
any other way. The hunters still in the quest stay on their own rows and keep
counting, so nobody's numbers end up labelled with somebody else's name. A
rejoin under the same name resumes that row from a fresh baseline. (Rise rows
are rebuilt each frame from what the producer reports.)

**A grey `?` in a row's weapon slot means "no weapon data for this row"** — the
overlay could not resolve that hunter's weapon. It is not a broken or missing
icon, and it is not a placeholder while the panel loads. Some party make-ups (a
host plus three hunters plus an ally, for example) hit this. Treat it as a
signal to look into that row, not as a picture that failed to appear.

## 6. Command line

### `monster-overlay`

| Flag | Meaning |
|---|---|
| `-m`, `--map <path>` | address-map file; overrides the runtime search order |
| `--game auto\|world\|rise` | which game to read (default `auto`) |
| `--poll <ms>` | polling interval, default 250 |
| `--locale <code>` | UI locale for this run (`zh-CN`, `en-US`) |
| `--layout single\|split` | window topology: one big player panel (`single`, the default) or the player debuff/buff capsules in their own window (`split`) — see §6.1 below |
| `--edit` | edit mode (§4) |
| `--mask-player\|monster\|damage\|pets <hex32>` | per-section visibility; the console passes these |
| `--no-player\|no-monster\|no-damage\|no-pets` | hide a whole panel |
| `--output-player\|monster\|damage\|pets <name>` | pin one panel to a `QScreen` name (empty = follow the OS primary); the console passes these |
| `--version`, `--help` | |

`--game auto` scans `/proc` for a running Monster Hunter process; `--game`
`world` / `rise` forces one reader instead. `--no-pets` only matters in Rise:
World never mounts the pet-damage surface regardless of the flag.

### 6.1 Two layouts: one panel or several small ones

The player window topology is a two-way switch, and v0.12 is where the two
sides got their own name:

| Mode | What you get |
|---|---|
| `--layout single` | **one big panel** holding every player block. The default, and pixel-for-pixel what every version before v0.12 drew |
| `--layout split` | the player **debuff / buff capsules move into their own window**, separately positionable, scalable and hideable |

```bash
./monster-overlay --layout split     # capsules get their own window
./monster-overlay                    # same as --layout single: one panel
```

Both modes keep every block you toggled on. In `split` the capsules simply
leave the player panel and appear as a second window — they are drawn by
exactly one of the two windows, never both, so nothing duplicates and nothing
is dropped. Each window is positioned on its own during edit mode (§4) and
keeps its own position, scale, opacity, output and visibility in
`panels.ini`, under the settings key `player-status`.

Two things that follow from the switch:

- **`single` has no second window at all.** The split window is never
  constructed and `panels.ini` gains no new section, so a user who never
  types `--layout=` keeps the exact file layout they had before.
- **`--split-player-strip` is a deprecated alias for `--layout=split`.**
  It is a plain on/off boolean, so it can only turn the split *on*, never
  back off against an explicit `--layout single`. If both are given, the
  explicit value wins. Existing pilot scripts keep working unchanged.

The precedence, in the order it stops mattering:

1. `--layout=<single|split>` — an explicit value wins over everything else.
   Case and surrounding spaces are tolerated (`SPLIT`, ` split `), but an
   unrecognised value such as `splitt` is **not** read as `single`: it is
   reported as invalid and falls back to the plain default.
2. `--split-player-strip` — the deprecated alias; it only speaks when
   `--layout=` is absent.
3. A saved setting (none is written by the console today) — a hand-mangled
   row is never allowed to pick a window topology you did not ask for.
4. The default: one big window.

Everything above is a startup decision. The console still passes the same
`--mask-player` value in both modes; only the window topology differs.

### `monster-control`

| Flag | Meaning |
|---|---|
| `--locale <code>` | UI locale for this run |
| `--light` | light theme |
| `--snap <file>` | render the console to a PNG and exit |
| `--print-locale` | print the resolved locale and source, then exit |
| `--print-screen-info` | print the detected outputs, then exit |
| `--version` / `--help`, `-v` / `-h` | print the version / usage and exit (no window) |

### `monster-doctor`

| Flag | Meaning |
|---|---|
| `--world` / `--rise` | restrict every section to one title |
| `--out <file>` | report path (default `./monster-doctor-<timestamp>.txt`) |
| `--seconds <n>` | length of the overlay status run, default 6, accepted 0–60 |
| `--no-overlay-run` | skip that run (offline machines) |
| `--version` | print the version and exit |

## 7. Files written

| Path | Written by | Contents |
|---|---|---|
| `~/.config/monster-overlay/monster-overlay.conf` | console | `player=` / `monster=` / `damage=` / `pets=` masks (hex32), plus `locale=` once you pick a language; unrelated lines are preserved |
| `~/.config/monster-overlay/panels.ini` | overlay | per-panel geometry: one group per panel (`player`, `monster`, `dps`, `pet-damage`, and in `--layout split` the player strip as `player-status`), each with `ml`/`mt`/`mr`/`mb` margins, `scale`, `opacity`, `bgAlpha`, `outputName`, `visible` |
| `~/monster-doctor-<timestamp>.txt` | doctor | the diagnostic report |

The console also keeps its own window geometry, window state, splitter sizes and
canvas zoom in Qt's default settings file under the `monster-control` identity;
the overlay reads none of those.

The overlay never writes the conf file — it only reads `locale=` at startup and
polls the file's mtime while running, so switching the console's language chip
reaches a live overlay within about a second without any IPC.

## 8. Diagnostics

```bash
./monster-doctor            # under a second offline, writes the report to ./monster-doctor-<timestamp>.txt
```

The report is a flat text file with a section per question, in this order:
host, binaries (versions of the three executables), address maps (the search
directory list the overlay uses, marked with which of them hold a World / Rise
`.map`, plus the dev-tree fallback), game processes, the read verdict — the one
that decides whether the panels can work at all, with the exact errno and the
remedy when a read was denied — the game runtime (Proton build, container,
Steam launch options), an overlay status run (`--seconds 6` by default, so add
a few seconds to the offline figures), and a privacy note.

Attach it to an issue — paths under `~` print as `~`, nothing is attached to,
written to or changed, and no environment variables, Steam account data or game
memory are collected.

## 9. Language resolution

`--locale` → the `locale=` row in the conf (written only when you press the
`中文 | EN` chip) → the detected system locale (`LC_ALL`, `LC_MESSAGES`, `LANG`,
then `LANGUAGE`; `zh*` → `zh-CN`, anything else including `C`/unset → `en-US`).
Saving panel switches never writes that row, so an untouched install keeps
following the desktop language.

The overlay re-reads the conf's mtime on its poll loop and reloads in place when
the row changes — no restart. The console applies a chip switch to its own
registered widgets immediately.

## 10. Rise live damage (REFramework)

Rise does not expose live damage to memory reading; it is produced inside the
game by a small REFramework Lua script. REFramework 1.5.9.1 restricts Lua file
access to `reframework/data`, so the producer alternates two snapshots there
(`mhr_damage_a.json` / `mhr_damage_b.json`) and the overlay consumes the newest
valid sequence. The console manages that chain for you — select the Rise game and
open the **REFramework** card on the rail (in World mode the card is not shown).

The card header shows where the overlay thinks Rise is installed:

| Header line | Meaning |
|---|---|
| `STEAM INSTALL: <dir>` | `MonsterHunterRise.exe` was located in a Steam library |
| `RISE INSTALL NOT FOUND IN STEAM LIBRARIES` | no library holds the game; the buttons stay disabled |
| `RISE INSTALL IS NOT VALID: <dir>` | a path was found but is not usable |

Then one state line, in this order of precedence:

| State line | Meaning |
|---|---|
| `STATUS: REFRAMEWORK NOT INSTALLED` | the core is missing (the header line above still shows the install dir) |
| `STATUS: INSTALLATION INCOMPLETE — REINSTALL RECOMMENDED` | the manifest is invalid, or the Lua differs from the bundled copy |
| `STATUS: LUA NOT INSTALLED (no damage tracking)` | the core is fine, the producer is missing |
| `STATUS: READY` | both installed and matching |

Two further lines can appear at any time: `CLOSE THE GAME BEFORE CHANGING
REFRAMEWORK FILES` while a Monster Hunter process is running, and the result of
the last operation (success/failure with its detail).

| Button | Effect |
|---|---|
| `INSTALL REFRAMEWORK + LUA` | asks once, then installs the pinned REFramework build plus the overlay Lua. The archive bundled in this package is used first (offline-capable); if it is missing, the pinned upstream release is downloaded and SHA-256 verified before anything is written |
| `REMOVE LUA` | removes only the overlay's managed Lua |
| `REMOVE REFRAMEWORK + LUA` | removes the overlay Lua plus every file recorded in the installer's manifest; an externally installed REFramework core is left alone and the status line says so |
| `Fix menu popup` / `Restore default (popup every launch)` | sets REFramework's `RememberMenuState` to true / false, writing it to every detected config location and keeping the first copy of each file as `.pre-monster-overlay` |

Rules the console enforces, each reported on the card:

- close the game before installing or removing anything;
- a hand-edited overlay Lua is **never** overwritten — the install stops with
  "the Lua differs from the bundled copy, remove it first" (the remove button
  clears it, the install button then works);
- if the managed manifest is invalid, the install refuses rather than removing
  files it does not recognise;
- a second operation while one is running is refused, not queued on top.

**After installing, start or restart the game** so the Lua producer loads. When
it has attached, `reframework_revision.txt` and the two producer files under
`reframework/data/` appear next to `dinput8.dll` in the game directory.

### What counts, and whose pet is that?

The Lua producer classifies every damage event by attacker kind
(Player / Companion / Pet / Palico / Palamute / Unknown). The overlay then
decides what a row means, using the party roster the reader already has:

- a **Companion** (another hunter's character) or a pet whose owner is another
  hunter only shows when `other players` is enabled — they are not your damage;
- your own pets show when `local pets` is enabled;
- the producer already aggregates *all* of one owner's pets into a single
  actor, so pet rows are **per owner** — there is no cat/dog split to make, and
  the row is labelled with the owner's name rather than the animal's;
- a follower's buddy row keeps the pet label (`<follower> · pet`) so it cannot
  be read as that follower's own damage row;
- `Unknown` attackers are never shown.

Owner attribution is resolved in this order, in `src/rise/rise_damage_roster.cpp`:

1. the actor's own `ownerEntityIndex`, matched against the roster;
2. owner-less pet streams, resolved from the raw entity index only in layouts
   that have actually been measured live — **`4` = the local hunter's second
   buddy**, `5`/`6` = follower slot 0 / 1's buddy when the party has a follower
   layout;
3. `5`–`9` with **no** measured follower layout fall back to the historical
   `owner = id − 5` join (teammate parties);
4. anything still unresolved stays an explicit generic row — never guessed,
   never silently dropped.

The conditions that gate this are deliberately narrow: step 2 only applies when
the roster has exactly one human hunter **and** that hunter is local, and
step 3 only when there is no follower layout. A stream that cannot be proven to
belong to a named hunter keeps its producer label instead.

## 11. Troubleshooting

| Symptom | Check |
|---|---|
| panels empty / disconnected | run `monster-doctor`; its `read verdict` section names the cause |
| memory read denied | the panel prints the two remedies verbatim (`setcap cap_sys_ptrace+ep` on the overlay binary, or temporarily `sysctl kernel.yama.ptrace_scope=0`) |
| panels on the wrong screen | the display selector in the console; or `--output-*` on the command line |
| Rise: damage panel stays empty | install the REFramework producer from the REFramework card (§10); the game must be closed while installing |
| Rise: damage empty after a successful install | check `re2_framework_log.txt` for the producer's `loaded` and `hooked` lines, then look for `mhr_damage_{a,b}.json` under `reframework/data/` |
| a hand-edited Lua blocks the install | the card refuses to overwrite it; remove the Lua first, then install again |
| wrong game read | `--game world` / `--game rise` forces one reader; default `auto` scans `/proc` |
| damage panel shows more rows than the hunters in the hunt | every hunter claims one row for the whole hunt and keeps it; a hunter who left mid-quest keeps their frozen row. Rows from a previous group only survive if the panel never saw the hunt end — return to the hub (or restart the game) and the panel rebuilds its rows at the next quest. If it repeats, report the make-up of the hunt |

## 12. Keyboard summary

**Edit mode** (`--edit`, or the console's layout mode): arrows nudge 10 px
(`Shift` = 50 px), mouse wheel scales 0.5×–3×.

**Live and edit**: `Space` collapses a focused panel, `Esc` quits the overlay
and returns focus to the console.

**Console**: `1`/`2`/`3`/`4` select the player / monster / damage / pets card,
`Ctrl`+wheel zooms the preview canvas, `←→↑↓` move the selected panel in the
canvas (10 px, `Shift` = 50 px).

### 12.1 What each window supports

Before the split, one panel meant one key set and the answer was memorised.
Now a window can be one of seventeen, and the honest answer to "which keys
do I have on THIS window?" is identical everywhere except the one row that
matters: the **columns dial**, `[` and `]`.

Everything below is a capability of the window, not of the moment. Whether a
press does anything *right now* is a separate question — the five edit-mode
capabilities are gated on edit mode (§4), while `Space` and `Esc` work in
live mode too.

| Window | Move | Scale | Fold | Quit | Width | Height | Columns |
|---|---|---|---|---|---|---|---|
| Player | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| Monster | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| Damage | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| Pets | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| Status strip | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Bars | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| Mantles | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Wirebug | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Weapon | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| Info | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| 怪物血条 Monster HP | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| 怪物耐力 Monster Stamina | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| 怪物怒气 Monster Enrage | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| 怪物异常 Monster Ailments | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| 怪物部位 Monster Parts | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| 伤害占比 Damage Share | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |
| 伤害折线 Damage Chart | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | — |

> The player panel's sub-windows only exist under `--layout split` (or when
> the console selects 分块); so do the monster and damage ones. In Single
> mode only the first four rows exist, and the behaviour is pixel-identical
> to v0.11.

**Move** — `←` `↑` `↓` `→` nudge the margins 10 px, `Shift` = 50 px. Edit mode
only. Wayland layer-shell cannot place a window at an arbitrary coordinate,
so a position is the anchor corner plus margins and these keys are the offset.

**Scale** — mouse wheel, 0.5× to 3×. Edit mode only, and persisted
immediately (see the no-`Ctrl+S` note in §4).

**Fold** — `Space` collapses the window to a 32×32 block; press again to
restore. Works in edit **and** live mode, because a window hiding under the
action is the case folding exists for.

**Quit** — `Esc` saves the window's position and quits the overlay. Also
edit and live. A window that is unpinned but still mounted keeps the key: the
dispatch is on the base class, not on the live mount.

**Width / Height** — `-` and `=` dial the minimum width, `Shift`+`-` /
`Shift`+`=` the minimum height, 16 px per press. Edit mode only.
`Ctrl` is a *different quantity on the same two faces*, not a mode:
`Ctrl`+`-` / `Ctrl`+`=` dial the SET size (a lock that makes the window
exactly that wide whatever the content measures) instead of the floor. The
first press starts one step from the window's current width rather than from
zero, and dialling below one step clears the lock rather than parking a
0-width window.

**Columns** — `[` and `]` re-flow a wrapped row: `]` spreads the items into
one flat row, `[` narrows it until at 1 the items stack vertically. Edit mode
only. Only the rows that have something to re-flow answer it — the status
strip's pills (1–6 per row), the two mantle boxes (1–2), the
recovering-wirebug capsules (1–4, the Rise slot cap being the ceiling), the
monster ailment cards (1–6) and the monster part cards (1–3). This is the
dial 「无法通过调整来调整成竖排排列」 asked for.

The monster HP, stamina and enrage bars do NOT answer it: each is one bar
with no axis to re-flow. The damage share bar and line chart are the same.

Two things that follow from the table:

- **A key is never half-supported.** On a window whose row says `—`, `[` and
  `]` do nothing at all rather than doing something partly: the dispatch asks
  `supportsColumns()` and an unclaimed key falls through to Qt, which is the
  same destination it had before the base class owned the keys at all.
- **The other seven are answered identically by every window.** That is what
  unification bought. It used to be seven pass-through contracts, each of
  which had to remember to forward unclaimed keys, and the status strip was
  the last one still carrying its own `keyPressEvent`. Since v0.12 all
  seventeen windows share one dispatcher.
- **Every window answers the other seven the same way.** That is the
  unification. The answer used to be seven copies of a passthrough contract,
  each of which had to remember to forward the keys it did not claim, and the
  status strip was the last one still carrying its own `keyPressEvent`.

This list is queried, not believed. `mhw::panelCapabilities()` derives it from
the same public hooks `Panel::keyPressEvent` dispatches through, and
`tests/panel_capabilities_tests.cpp` asserts the table above matches the
derivation — including a row check against both language copies of this page.
A future window that forgets to answer `supportsColumns()` fails that test
rather than shipping a dead key with no documentation.
