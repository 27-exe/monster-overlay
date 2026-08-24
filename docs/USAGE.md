# Usage Guide — `monster-overlay` & `monster-control`

Everything in this document has been verified against the `v0.7.5` build
(`ac60017`). Source of truth: `src/ui/panel.cpp`, `src/ui/hud_canvas.cpp`,
`src/ui/control_panel.cpp`, `src/main.cpp`.

---

## 1. Two binaries, two roles

| Binary | Job | Visual style |
|--------|-----|--------------|
| `monster-overlay` | The HUD itself. Three layer-shell surfaces anchored to corners. | Translucent dark, role-themed (player=teal, monster=violet, damage=pink). |
| `monster-control` | The control console. Spawns `monster-overlay` as a child process and owns its lifecycle. | A native Qt `QMainWindow` with a left rail (`WORLD` / `RISE` / `DETECTED`) and a central preview canvas. |

You don't have to use `monster-control` — `monster-overlay` accepts every
setting as a CLI flag — but the console is the easiest way to flip
sections on/off and reposition the panels without restarting.

---

## 2. First run checklist

1. **Game is running and on a quest.** Live mode needs
   `MonsterHunterWorld.exe` (or Rise) — the reader scans
   `/proc/<pid>/maps` and attaches to the game's base address.
2. **Console shows `WORLD` detected.** The rail badge updates every
   five seconds; if it just says `WORLD` (no `DETECTED` marker) you need
   to grant the `ptrace` capability (`install.sh` does not do this):
   ```bash
   sudo setcap cap_sys_ptrace+ep ./monster-overlay
   ```
3. **Click START OVERLAY.** The console window hides; the three panels
   appear on top of the game.
4. **Press `Esc` over any panel** (or quit the game) — the console
   re-appears in the position you last left it.

---

## 3. The control console (`monster-control`)

The console is a single window with three regions stacked vertically
when the stage is expanded, side-by-side when collapsed:

```text
+-----------------------------------------------------+
| ◯ MHW OVERLAY    CONTROL CONSOLE · 0.5   ● READY    |
+----------+--------------------------+--------------+
| GAME     |    [expandable stage]   |  SELECTED    |
| ◉ WORLD  |    preview canvas       |  OBJECT      |
| ○ RISE   |    (HUD preview)        |              |
|          |                          |  P / M / D   |
| DETECTED |                          |  toggles +   |
| WORLD    |                          |  sub-rows    |
+----------+--------------------------+--------------+
| ↑↓ select / Click to focus    ←→↑↓ MOVE    ZOOM ×1.0|
+-----------------------------------------------------+
```

### 3.1 Buttons and shortcuts

| Where | What | Action |
|-------|------|--------|
| Left rail | `WORLD` / `RISE` radio | Pick which game's address map is active (`.map` resolves to the bundled file). |
| Left rail | Auto-detected badge | Refreshes every 5 s. Shows `WORLD` / `RISE` / `--` if not found. |
| Center top | `▾ HIDE STAGE` button | Collapses the preview canvas. The HUD objects list and "START OVERLAY" stay reachable. |
| Center top | `▴ SHOW STAGE` button | Re-opens it. |
| Center top | `▶ START OVERLAY` button | Spawns `monster-overlay` as a child process; console hides. |
| Center top | `■ STOP OVERLAY` button | Gracefully terminates the overlay (`SIGINT`), console re-appears. |
| Right column | `P` / `M` / `D` switch (master) | Disable the whole player / monster / damage panel. Independent of sub-row toggles. |
| Right column | sub-rows (Conn / Quest / Weapon / Bars / Mantles / Debuff, …) | Per-section visibility. Bitmask layout in `panel_sections.h`. |
| Right column | `ZOOM ×1.0` display | Read-only; click the preview canvas, scroll wheel to change. Range 0.5× – 2.0×. |
| Right column | `SAFE AREA` / `GRID` / `LIGHT` (preview chrome) | Decoration toggles inside the preview canvas. |
| Footer hint | `←→↑↓ MOVE` | Drag the selected panel with the keyboard (10 px per press, **Shift = 50 px**). |

### 3.2 The preview canvas

The preview canvas is a `HudCanvas` (not a `Panel`). It mirrors the
three real panels in size and position; every interaction has the
**same effect** as it would on a live overlay:

- **Click a panel** — selects it (cyan highlight). Each row in the
  inspector on the right now shows what that panel exposes.
- **Drag the selected panel** — the panel moves with your cursor in
  logical screen pixels. Drop where you want, the live overlay updates
  on the next save (default: on quit).
- **Arrow keys over the canvas** — 10 px nudge.
- **Shift + arrow keys** — 50 px nudge (use it to make big sweeps).
- **Wheel** — zoom the preview 0.5×–2.0× (visual only; not persisted).
- **Position is persisted** to `~/.config/monster-overlay/panels.ini`
  on quit. There is no "save" button — `Esc` or the `STOP OVERLAY`
  button writes the file.

> Tip: when the stage is **hidden** (`HIDE STAGE`), the preview canvas
> is collapsed but the inspector on the right stays reachable — useful
> when you're tweaking toggles for a clean screenshot.

---

## 4. Edit-mode interaction in `monster-overlay` (no game)

```bash
./build/monster-overlay --edit
```

Edit mode runs without a live game: every panel renders demo data so
you can reposition it. The **same binding table applies** as in
`monster-control`'s preview canvas, plus these that are specific to a
focused panel window:

| Key / gesture | Action |
|---------------|--------|
| `←` `→` `↑` `↓` | Nudge the focused panel by 10 px. |
| `Shift + ←` `→` `↑` `↓` | Nudge by 50 px. |
| `Ctrl + S` | Persist the current panel positions to `~/.config/monster-overlay/panels.ini` immediately (otherwise they persist on quit). |
| `Mouse wheel` (over the panel) | Scale panel content 0.5× – 2.0×. |
| `Space` | Toggle **minimize** — collapses the panel to a 32×32 letter chip. Hit Space again to restore. |
| `Esc` | Graceful quit — saves all current positions, then exits. |
| `Click` + drag | Drag the panel around. |

### 4.1 Demo-data behavior (and the trap)

Edit mode uses **`setupDemoData()`** to seed 4–8 fake snapshots so
the panels have something to render. The fixed bug from earlier
versions was a live-mode bleed:

- v0.6.x: the seeded names / damage numbers could leak into live mode
  for the very first frame if the overlay was already running.
- **v0.7.5 (current)**: `setupDemoData()` is gated by `editMode_` and
  the carry-over row is no longer sticky once a real quest starts.

In short: start the overlay at any time — the demo data will not
contaminate your live hunt.

---

## 5. The three panels

Every panel is independent — toggling one does not affect the others —
and the read chain is identical to HunterPie v2's (see
`mhw_reader.cpp` for the offset-by-offset correspondence).

### 5.1 Player panel (`P`)

| Section | Default | What shows |
|---------|:-------:|------------|
| Conn | on | Connected / disconnected status of the local game instance. |
| Quest | on | Slot # / zone / timer / cat-car count / MR rank. |
| Weapon | on | Icon + sharpness color gradient (white/purple/.../red). |
| Bars | on | HP and ST bars with numeric `current / max` and percentage. |
| Mantles | on (World only) | The two mantle slots, name + remaining CD countdown. |
| Wirebug | on (Rise only) | Rise's wirebug state — hidden on World. |
| Debuff | on | Buff / blight / consumable icons (Might, Adamant Seed, etc.). |

MR rank uses the green/purple tier color from `mhw::palette`.

### 5.2 Monster panel (`M`)

| Section | Default | What shows |
|---------|:-------:|------------|
| Info | on | Monster icon + name + ID + level + threat state ("Apex"). |
| HP | on | Total HP bar, current / max, percentage. |
| Enrage | on | Enrage / sleep countdown (red when high, faded when not active). |
| Ail | on | Per-ailment timers (poison / sleep / para / stun / blast / mount). |
| Parts | on | Per-part HP bars (head / body / wings / legs / tail). Severable parts use the broken-icon glyph when broken. |

The `Ail/Part` subsystem uses **two separate schema files** —
`src/resources/monsters/ailments.json` (HunterPie 2.14.0.461 data) and
`parts.json` — generated from HunterPie `MonsterData.xml`. See
`scripts/gen_schema.py` for the regeneration pipeline.

### 5.3 Damage panel (`D`)

| Section | Default | What shows |
|---------|:-------:|------------|
| Rows | on | Party list, sorted by DPS descending, with per-row percent bar. |
| Share | on | Contribution share (each hunter's percentage of total damage). |
| Chart | on | Time-series chart of DPS over the last ~30 s. |

The chart's grain texture (`assets/charts/alligator_noise_512x512.jpg`)
keeps the rendering stable across back-pressure events (a re-attach
after a Steam overlay popup doesn't blank the curve).

---

## 6. CLI flags (monster-overlay)

```text
  -m, --map <path>           HunterPie legacy map file (default: bundled
                             — MHW on Linux, Rise if --game rise)
  --game <world|rise>        Shortcut: select the bundled .map by name.
                             Equivalent to --map data/MonsterHunter<…>.map.
  --locale <code>            UI locale (default: zh-CN).
  --edit                     Edit mode (no game needed).
  --poll <ms>                Polling interval (default: 250, range 30–5000).
  --mask-player <hex32>      Section mask for player panel (default: FFFFFFFF).
  --mask-monster <hex32>     Section mask for monster panel.
  --mask-damage <hex32>      Section mask for damage panel.
  --no-player / --no-monster / --no-damage
                             Disable the whole panel entirely.
  -h, --help                 Show help.
  -v, --version              Show version (currently prints 0.7.5).
```

### 6.1 Mask vs `--no-*`

There are two related but **different** flags, both useful:

- **`--no-<panel>`** hides the whole panel. The layer-shell surface is
  not even mapped; the panel contributes zero pixels and zero height to
  the layout. Independent of the section mask.
- **`--mask-<panel> <hex32>`** leaves the panel visible (you still see
  its title row) but toggles individual sections inside.

The bit layout per panel lives in `src/ui/panel_sections.h`. To
figure out the correct hex value for your combination, use the
console — it writes the current mask to
`~/.config/monster-overlay/monster-overlay.conf` and the next launch reads
from there.

---

## 7. Persistence files

| File | Written by | Read by |
|------|------------|---------|
| `~/.config/monster-overlay/monster-overlay.conf` | `monster-control` on mask change + on quit | `monster-overlay` on startup |
| `~/.config/monster-overlay/panels.ini` | `monster-overlay` on Ctrl-S / on quit | `monster-overlay` on startup (panel positions + scales) |
| `~/.cache/monster-overlay/` | `monster-reader` (snapshot dump for bug reports) | you, when filing an issue |

Delete any of these to start fresh — there's no schema-version check.

---

## 8. Hidden behavior worth knowing

- **`taskbar / skip-dock`** — every layer-shell surface is configured
  with `Qt::Tool` window flag, so the overlay doesn't show up in your
  compositor's task list / dock. That means quitting the game without
  pressing `Esc` leaves the overlay surface stacked: kill it with
  `pkill monster-overlay` (or `kill <pid>` from the console title bar).
- **`ptrace_scope`** — on hardened distros you may need
  `sudo setcap cap_sys_ptrace+ep ./monster-overlay`. See
  `docs/ARCHITECTURE.md` for the full write-up.
- **Layout in MQ<2560×1080** — at narrow widths the three panel
  anchors may overlap; bump the screen height to ≥1080 (or scale each
  panel down via the console wheel).
- **Re-attaching to a saved PID** — Steam sometimes keeps the old PID
  alive in `~/.local/share/Steam/userdata/`. The reader scans
  `/proc/<pid>/comm` to confirm it's actually `MonsterHunterWorld.exe`
  before attaching; if you see `--` in the rail badge forever, the
  pid file is stale and a Steam re-launch fixes it.
