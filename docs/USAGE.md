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
console. The console spawns `monster-overlay` with the current switch state as
`--mask-*` / `--no-*` flags and hides itself; the panels appear over the game.

## 3. The control console

| Element | What it does |
|---|---|
| panel cards (player / monster / damage / pets) | master switch plus per-section switches; the preview updates as you toggle |
| `START` / `STOP` | starts / stops `monster-overlay` |
| preview canvas | click a panel to focus it; drag to move; arrow keys nudge 10 px (`Shift` = 50 px) |
| display selector | which output the panels are placed on |
| `中文 | EN` chip | switches the UI language; the overlay follows within ~1 s |
| theme chip / `--light` | light or dark console theme |

## 4. Positioning panels (edit mode)

```bash
./monster-overlay --edit        # no game required
```

| Input | Action |
|---|---|
| click a panel | focus it |
| `←` `↑` `↓` `→` | nudge 10 px (`Shift` = 50 px) — edit mode only |
| mouse wheel | scale 0.5× – 2× — edit mode only |
| `Ctrl+S` | persist to `~/.config/monster-overlay/panels.ini` — edit mode only |
| `Space` | collapse the panel to a small block — works in edit **and** live mode |
| `Esc` | quit the overlay and return focus to the console — works in edit **and** live mode |

`Space` and `Esc` are reachable from the running overlay too: when the console
spawns the overlay it hides itself, so the panels become the focused window and
`Space` / `Esc` work without entering `--edit`. Arrow keys and the wheel only
take effect in edit mode — the live overlay ignores them so it cannot drift
while you hunt.

## 5. Panel contents

| Panel | Shows |
|---|---|
| Player | name, HP / stamina, sharpness, weapon, wirebug (Rise), mantles (World), ailments, food skills |
| Monster | current target, HP and part damage, ailments, enrage / stamina (Rise), crown size |
| Damage | per-player damage and DPS; on Rise it is fed by the REFramework producer (§10) |
| Pets | pet damage grouped by owner — Rise only; in World mode the rail card, its options and the floating window are not shown at all |

## 6. Command line

### `monster-overlay`

| Flag | Meaning |
|---|---|
| `-m`, `--map <path>` | address-map file; overrides the runtime search order |
| `--game auto\|world\|rise` | which game to read (default `auto`) |
| `--poll <ms>` | polling interval, default 250 |
| `--locale <code>` | UI locale for this run (`zh-CN`, `en-US`) |
| `--edit` | edit mode (§4) |
| `--mask-player\|monster\|damage <hex32>` | per-section visibility; the console passes these |
| `--no-player\|no-monster\|no-damage` | hide a whole panel |
| `--version`, `--help` | |

### `monster-control`

| Flag | Meaning |
|---|---|
| `--locale <code>` | UI locale for this run |
| `--light` | light theme |
| `--snap <file>` | render the console to a PNG and exit |
| `--print-locale` | print the resolved locale and source, then exit |
| `--print-screen-info` | print the detected outputs, then exit |
| `--version` / `--help` | print the version / usage and exit (no window) |

### `monster-doctor`

| Flag | Meaning |
|---|---|
| `--world` / `--rise` | restrict every section to one title |
| `--out <file>` | report path (default `monster-doctor-<timestamp>.txt`) |
| `--seconds <n>` | length of the overlay status run, default 6 |
| `--no-overlay-run` | skip that run (offline machines) |

## 7. Files written

| Path | Written by | Contents |
|---|---|---|
| `~/.config/monster-overlay/monster-overlay.conf` | console | `player=` / `monster=` / `damage=` masks, plus `locale=` once you pick a language |
| `~/.config/monster-overlay/panels.ini` | overlay (`Ctrl+S` in edit mode) | panel positions and scale |
| `monster-doctor-<timestamp>.txt` | doctor | the diagnostic report |

## 8. Diagnostics

```bash
./monster-doctor            # ~15 s, read-only, writes the report next to the CWD
```

The report covers: where the address maps were found (search order and the
source that won), whether a game process is visible and at which image base, the
reader's verdict with the exact errno, the Proton build on the process's parent
chain, Steam launch options, and the overlay's own status output. Attach it to
an issue — paths under `~` print as `~`, and no environment variables, Steam
account data or game memory are collected.

## 9. Language resolution

`--locale` → the `locale=` row in the conf (written only when you press the
`中文 | EN` chip) → the detected system locale (`LC_ALL`, `LC_MESSAGES`, `LANG`,
then `LANGUAGE`; `zh*` → `zh-CN`, anything else including `C`/unset → `en-US`).
Saving panel switches never writes that row, so an untouched install keeps
following the desktop language.

## 10. Rise live damage (REFramework)

Rise does not expose live damage to memory reading; it is produced inside the
game by a small REFramework Lua script. REFramework 1.5.9.1 restricts Lua file
access to `reframework/data`, so the producer alternates two snapshots there
(`mhr_damage_a.json` / `mhr_damage_b.json`) and the overlay consumes the newest
valid sequence. The console manages that chain for you — select
the Rise game and open the **Damage** card:

| Card line | Meaning |
|---|---|
| `STEAM INSTALL: <dir>` | the Rise install was found; the status line also lists core / Lua / manifest state |
| `RISE INSTALL NOT FOUND…` | no Steam library contains `MonsterHunterRise.exe` |

| Button | Effect |
|---|---|
| `INSTALL REFRAMEWORK + LUA` | installs the pinned REFramework build plus the overlay Lua. The archive bundled in this package is used first (offline-capable); if it is missing, the pinned upstream release is downloaded and SHA-256 verified before anything is written |
| `REMOVE LUA` | removes only `reframework/autorun/mhr-overlay-damage.lua` |
| `REMOVE REFRAMEWORK + LUA` | removes the overlay Lua and every file recorded by the installer; an externally installed REFramework and other mods are left alone |

Rules the console enforces:

- close the game first — every card button is disabled while a Monster Hunter
  process is running;
- a hand-edited overlay Lua is never overwritten silently: remove it first,
  then install again;
- start (or restart) the game after installing so the Lua producer loads.

**Proton / Linux:** a normal local `dinput8.dll` install is loaded automatically
on many Proton setups. If `re2_framework_log.txt` is not created after starting
the game, add this to Steam Launch Options and retry:

```
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

When REFramework has attached, the log contains `ScriptRunner` and
`[mhr-overlay-damage] loaded`; the producer files then appear under
`reframework/data`.

### What counts, and whose pet is that?

Attribution follows the upstream (HunterPie) gates, verified live:

- only damage **to a large monster** counts — small monsters, objects, and
  damage dealt to players are filtered;
- the source must be a **player** (`0`) or **a pet** (`0x15`/`0x16`/`0x17`);
  monster-vs-monster brawls, terrain, the mounted monster's attacks during
  Riding, and anything else the game does not mark as a player/pet source are
  never credited to a hunter (deployables such as bombs count only when the
  game reports a player/pet as their source);
- pet rows are grouped **per owner** — no cat/dog split, matching upstream.

The raw pet streams are layout-dependent; live sessions (MHR 16.0.2.0)
settled the mapping:

| Layout | Streams | Meaning |
|---|---|---|
| solo (two buddies) | `0`, `4` | your two buddies |
| solo + followers | `0`, `4`, `5`, `6` | `5`/`6` = follower slot 0 / 1 buddy |
| multiplayer | `0`–`3`, one per hunter | each hunter's single buddy |

Your own buddies merge into one row (your name); a follower's buddy row reads
`<follower> · pet` so it cannot be mistaken for the follower's own damage row.
A stream that cannot be proven to belong to anyone stays an explicit generic
row — never guessed, never silently dropped.

## 11. Troubleshooting

| Symptom | Check |
|---|---|
| panels empty / `未连接` | run `monster-doctor`; the report names the cause |
| `读取被拒绝` / errno in the panel | the panel prints the two remedies (`setcap`, `sysctl`) |
| panels on the wrong screen | display selector in the console |
| Rise: damage panel stays empty | install the REFramework producer from the Damage card (§10); the game must be closed while installing |
| Rise: damage empty after a successful install | check `re2_framework_log.txt` for `[mhr-overlay-damage] loaded`, `hooked`, and publish errors; the current producer writes `reframework/data/mhr_damage_{a,b}.json` |
| stale `未连接` after a game restart | the reader re-detects within ~5 s; if it persists, re-run the panel check |
