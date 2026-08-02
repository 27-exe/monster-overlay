# MHW Linux Overlay

A native-Linux overlay for **Monster Hunter: World** running under
Steam + GE-Proton. Reads the game's process memory directly via
`/proc/<pid>/mem` and renders a Qt-based HUD in a KDE Wayland
layer-shell surface.

## Status: v0.4

`v0.4` is the current stable product. The overlay binary
(`mhw-overlay`) and the control console (`mhw-control`) are both
in active use. Read `docs/V0.4-STATUS.md` for the full v0.4 status
snapshot.

### What shipped in v0.4

- **Master visibility** — `--no-player / --no-monster / --no-damage`
  CLI flags gate the whole panel independently of the per-section
  masks. A panel with all sections off still shows its title row
  (intended — that’s “enable panel, but every sub is off”;
  “disable the whole panel” is a different state and is what
  `--no-*` now means).
- **Per-section masks** — `--mask-player/--mask-monster/--mask-damage`
  accept hex32 bitmasks; default `0xFFFFFFFF` is all-on and matches
  the legacy behaviour.
- **Control console** (`mhw-control`) — a separate Qt GUI that owns
  a sub-process of `mhw-overlay`. The console window hides when the
  overlay spawns and re-shows on overlay exit (SIGINT / ESC / quit).
  Per-section toggles + master toggles are persisted to
  `~/.config/mhw-overlay/mhw-overlay.conf`.

Read `docs/V0.4-STATUS.md` for the full status snapshot.

## Quick start

```bash
# Build all targets (overlay + console + tests + probes)
cmake -B build
cmake --build build -j$(nproc)

# Run the overlay against a live MHW
./build/mhw-overlay --map data/MonsterHunterWorld.421810.map

# Run in edit mode (no MHW needed; position the 3 panels with keyboard)
./build/mhw-overlay --edit --poll 250
#   click a panel to focus it, then:
#     ←↑↓→   nudge 10 px  |  Shift + ←↑↓→   nudge 50 px
#     wheel               zoom 0.5× – 2×
#     Ctrl + S            persist position to ~/.config/mhw-linux-overlay/panels.ini
#     Space               toggle minimized (small letter block)
#     Esc                 graceful quit

# Run the control console (replaces the command line)
./build/mhw-control

# Tests
ctest --test-dir build --output-on-failure
```

The `421810.map` ships in `data/` and is resolved automatically
from `MHW_DEFAULT_MAP` (set by CMake) if `--map` is omitted.

## Requirements

- Arch Linux (or any distro with Qt 6.8+)
- Steam + GE-Proton 10-34 (or Proton 9+)
- A running `MonsterHunterWorld.exe` process (live mode reads it)
- KDE Plasma 6 on Wayland for the layer-shell surface
- Build deps: `qt6-base qt6-declarative qt6-wayland layer-shell-qt cmake ninja`

## What the overlay shows

```
┌─────────────────────────────────────┐
│  狩猎 · 古代树森林                    │  ← context (zone + quest state)
│  任务 66801 · ★6 · 剩余 41:37 · 猫车 0/3 │  ← quest
│  猎人  HP 172/200 (86.0%)  ST 130/150 │  ← player
│  武器  转身 60s · 不动 (冷却) 280s    │  ← mantles
│  A27exe  MR 214  伤害 12840           │  ← party damage
│  em\em100_00 [ID 100]                │  ← monster
│  HP  14280 / 20880   68.4%  🔥74s    │  ← total HP + enrage countdown
│    头部                              │  ← per-part
│    尾巴 (硬直 200/200  100.0%  mp)   │  ← multi: counter only
└─────────────────────────────────────┘
```

UI strings are externalised to `src/resources/i18n/<locale>.json`;
all text on the panel is i18n-driven and re-renders without rebuilding
when the JSON is updated.

## Repo layout

```text
src/
├── main.cpp                  # mhw-overlay entry
├── main_control.cpp          # mhw-control entry (control console)
├── mhw_reader.{h,cpp}        # orchestrator over the per-domain readers
├── core/                     # foundational types
│   └── string_table.{h,cpp}  # mhw::StringTable, QRC JSON loader
│
├── monster/                  # monsters/HP/parts/ailments/enrage
├── player/                   # HP/ST + abnormalities + mantles
├── quest/                    # quest state, timer, deaths
├── world/                    # zone, scene
└── ui/                       # panel widgets, control console, icons
    ├── panel.{h,cpp}         # base class (layer-shell anchor, edit mode, save/load config)
    ├── panel_player.{h,cpp}  # player panel (kConn/kQuest/kWeapon/...)
    ├── panel_monster.{h,cpp} # monster panel (kInfo/kHp/kEnrage/kAil/kParts)
    ├── panel_damage.{h,cpp}  # damage panel (kRows/kShare/kChart)
    ├── panel_sections.h      # per-panel section enum + names + displayNames
    ├── control_panel.{h,cpp} # mhw-control main window
    ├── toggle_chip.{h,cpp}   # iOS-style master toggle
    ├── section_row.{h,cpp}   # per-section row widget
    ├── formatters.{h,cpp}    # integer / time / percent formatting
    ├── icon.{h,cpp}          # SVG/PNG icon loader

assets/
├── icons/                    # per-monster / per-status icons
├── charts/                   # DPS chart textures
└── icons.qrc                 # Qt resource manifest

data/
└── MonsterHunterWorld.421810.map  # HunterPie legacy offset table

tests/                        # ctest targets — 4 unit + 7 diagnostic probes
├── reader_tests.cpp
├── string_table_tests.cpp
├── snap_player_demo.cpp        # offscreen render of player panel
├── snap_all_demo.cpp           # offscreen render of monster + damage panels
├── control_l2_smoke.cpp        # round-trip mhw-control mask persistence
├── mhw_probe.cpp                # generic reader probe
├── mhw_scan.cpp                 # find monster struct table in heap
├── mhw_probe_parts.cpp          # part table dump (normal + severable)
├── mhw_probe_ailments.cpp       # one-shot ailment probe
├── mhw_probe_ailments_watch.cpp # continuous ailment watch
├── mhw_probe_mantles.cpp        # mantle probe
└── mhw_probe_mantles_wide.cpp   # full region dump around EQUIPMENT_OFFSETS

scripts/                       # maintenance scripts
├── gen_schema.py              # regenerate src/monster/part_schemas.cpp

web/                           # web prototype (Ctrl-F/T/C web)
tools/                         # standalone CLI tools
```

## Build details

CMake targets:

| target                  | description                                            |
|-------------------------|--------------------------------------------------------|
| `mhw-overlay`           | the GUI binary; the thing you actually run             |
| `mhw-control`           | control console (launches `mhw-overlay` subprocess)   |
| `mhw-core`              | static lib: `mhw::StringTable` + utilities             |
| `mhw-reader-tests`      | schema integrity check (kPartSchemas has 72 entries)   |
| `mhw-core-tests`        | `mhw::StringTable` load / miss / fallback             |
| `mhw-control-l2-smoke`  | `mhw-control` mask persistence round-trip              |
| `mhw-snap-player-demo`  | offscreen render of player panel (PNG)                 |
| `mhw-snap-all-demo`     | offscreen render of monster + damage panels (PNG)      |
| `mhw-probe*`            | 7 diagnostic CLIs; skipped by ctest if MHW not running |

`ctest` runs the unit tests. The probe binaries are wired up but
`SKIP_RETURN_CODE`’d so they pass on a box that doesn’t have MHW running.

## CLI flags (mhw-overlay)

| flag                  | meaning                                              |
|-----------------------|------------------------------------------------------|
| `-m, --map <path>`    | HunterPie legacy map file (default: bundled)          |
| `--locale <code>`     | UI locale (default: `zh-CN`)                         |
| `--edit`              | edit mode (drag panels, ESC quits)                  |
| `--poll <ms>`         | polling interval (default: 250, range 30–5000)      |
| `--mask-player <hex32>` | player panel section mask (default: 0xFFFFFFFF)     |
| `--mask-monster <hex32>`| monster panel section mask (default: 0xFFFFFFFF)    |
| `--mask-damage <hex32>` | damage panel section mask (default: 0xFFFFFFFF)      |
| `--no-player`         | disable the player panel entirely                     |
| `--no-monster`        | disable the monster panel entirely                    |
| `--no-damage`         | disable the damage panel entirely                     |
| `-h, --help`          | show help                                            |
| `-v, --version`       | show version                                         |

## Edge cases / known restrictions

- **`SECCOMP` / `ptrace_scope`** — `/proc/<pid>/mem` reads need
  `CAP_SYS_PTRACE` or `kernel.yama.ptrace_scope = 0`. See
  `docs/ARCHITECTURE.md` for the full caveat.
- **Layer-shell re-render** — KDE occasionally reuses an old
  surface after mtime changes. If a panel looks stale, kill all
  `mhw-overlay` processes and relaunch.
- **Offscreen testing** — `mhw-snap-player-demo` and
  `mhw-snap-all-demo` write PNGs to file. They run under
  `QT_QPA_PLATFORM=offscreen` and don’t touch Wayland.

## License

The core offsets and struct layouts in `mhw_reader.cpp` are derived
from the HunterPie project (HunterPie/HunterPie, Apache-2.0). Original
offsets and layouts are credited in the file comments.

The Linux port glue (panel / control-panel widgets, core utilities,
build system, packaging) is also Apache-2.0. See `LICENSES/`.

## See also

- `docs/ARCHITECTURE.md` — how the reader is wired together, why we
  avoid the QObject::tr ADL trap, the Yama `ptrace_scope` caveat.
- `docs/V0.4-STATUS.md` — v0.4 status snapshot and CLI flag matrix.
- `docs/I18N.md` — adding a new locale, adding a new UI string.
- `docs/PROBE-TOOLS.md` — what each `mhw-probe-*` binary does and when
  to use which.
- `docs/ASSETS.md` — where icons/charts live and how to add new ones.
- `control-panel-v0.5-A.html` — the canonical HTML design mock
  for the v0.5 control console (sidebar + inspector + unified canvas).
  Visual fidelity work for the console happens against this file.
