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
| panel cards (player / monster / damage) | master switch plus per-section switches; the preview updates as you toggle |
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
| Damage | per-player damage and DPS — World only; Rise does not expose live damage |

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

## 10. Troubleshooting

| Symptom | Check |
|---|---|
| panels empty / `未连接` | run `monster-doctor`; the report names the cause |
| `读取被拒绝` / errno in the panel | the panel prints the two remedies (`setcap`, `sysctl`) |
| panels on the wrong screen | display selector in the console |
| Rise: no damage panel | by design — Rise does not expose live damage |
| stale `未连接` after a game restart | the reader re-detects within ~5 s; if it persists, re-run the panel check |
