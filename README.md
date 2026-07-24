# MHW Linux Overlay

A native-Linux overlay for **Monster Hunter: World** running under
Steam + GE-Proton. Reads the game's process memory directly via
`/proc/<pid>/mem` and renders a Qt-based HUD in a KDE Wayland
layer-shell surface.

> **Status: v0.2.** UI rebuilt as 3 independent Wayland layer-shell
> panels (player status / monster HP / party damage), positioned by
> keyboard nudge (arrow keys) and anchored to screen corners. Idle CPU
> ≈ 2 % (was 99 % in v0.1). Live-data path unchanged from v0.1.
> See `docs/V0.2-STATUS.md` for the full status snapshot and the
> Wayland drag-feedback-loop writeup that drove the keyboard-nudge
> design.

> **Next: v0.3** — live-game validation + multi-monster stacking +
> DPS chart (was previously planned under v0.2).

---

## Quick start

```bash
# Build
cmake -B build
cmake --build build -j$(nproc)

# Run (live, connects to a running MHW)
./build/mhw-overlay

# Run in edit mode (no MHW needed; position the 3 panels with keyboard)
./build/mhw-overlay --edit --poll 250
#   click a panel to focus it, then:
#     ←↑↓→   nudge 10 px  |  Shift + ←↑↓→   nudge 50 px
#     wheel               zoom 0.5× – 2×
#     Ctrl + S            persist position to ~/.config/mhw-linux-overlay/panels.ini

# Run in demo mode (no MHW needed, shows mock data)
./build/mhw-overlay --demo

# Run with a specific .map file (HunterPie legacy offset table)
./build/mhw-overlay --map path/to/MonsterHunterWorld.421810.map

# Use a different UI language
./build/mhw-overlay --locale en-US

# Tests
ctest --test-dir build --output-on-failure
```

The `421810.map` is bundled in `data/`. It's resolved automatically
from `MHW_DEFAULT_MAP` (set by CMake) if `--map` is omitted.

## Requirements

- Arch Linux (or any distro with Qt 6.8+)
- Steam + GE-Proton 10-34 (or Proton 9+)
- A running `MonsterHunterWorld.exe` process (live mode requires it)
- KDE Plasma 6 on Wayland for the layer-shell surface
- Build deps: `qt6-base qt6-declarative qt6-wayland layer-shell-qt cmake ninja`

## What the overlay shows

```
┌─────────────────────────────────────┐
│  狩猎 · 古代树森林                    │  ← context (zone + quest state)
│  DEMO · KDE Wayland layer-shell...   │  ← status / MHW state
│  任务 66801 · ★6 · 剩余 41:37 · 猫车 0/3 │  ← quest
│  猎人  HP 172/200 (86.0%)  ST 130/150 │  ← player
│  装备  转身 60s · 不动 (冷却) 280s    │  ← mantles (active or cooldown)
│  A27exe  MR 214  伤害 12840           │  ← party damage
│  em\em100_00 [ID 100]                │  ← monster
│  HP  14280 / 20880   68.4%  🔥74s    │  ← total HP + enrage countdown
│    头部                              │  ← per-part
│      硬直 3105/3105  100.0%           │
│    尾巴
│      硬直 200/200   100.0%  (mp)      │  ← multi: counter only
└─────────────────────────────────────┘
```

UI strings are externalised to `src/resources/i18n/<locale>.json`;
all text on the panel is i18n-driven and re-renders without rebuilding
when the JSON is updated.

## Repo layout

```
src/
├── main.cpp                  # entry: --map / --demo / --locale
├── overlay_window.{h,cpp}    # Qt UI, layer-shell, render loop
├── mhw_reader.{h,cpp}        # top-level orchestrator (TODO: split in v0.2)
│
├── core/                     # foundational types and utilities
│   └── string_table.{h,cpp}  # mhw::StringTable, QRC JSON loader
│
├── resources/                # Qt resources (compiled into the binary)
│   ├── resources.qrc         # qrc manifest
│   ├── i18n/                # UI strings (zh-CN.json, en-US.json …)
│   ├── monsters/             # HunterPie XML-derived tables (parts, ailments)
│   └── quests/               # quest-state translations (v0.2)
│
├── address_map/              # v0.2: extracted from mhw_reader.h
├── monster/                  # v0.2: HP + parts + ailments + enrage
├── player/                   # v0.2: HP/ST + 75-slot abnormalities + mantles
├── quest/                    # v0.2: quest state, timer, deaths
├── world/                    # v0.2: zone, scene
└── ui/                       # v0.2: panel widgets, formatters

assets/                        # image assets (v0.2)
├── icons/                     # per-monster / per-status icons
└── charts/                    # DPS chart textures

data/
└── MonsterHunterWorld.421810.map  # HunterPie legacy offset table

tests/                        # ctest targets — 1 unit + 7 diagnostic probes
├── reader_tests.cpp
├── string_table_tests.cpp
├── mhw_probe.cpp                # generic reader probe
├── mhw_scan.cpp                 # find monster struct table in heap
├── mhw_probe_parts.cpp          # part table dump (normal + severable)
├── mhw_probe_ailments.cpp       # one-shot ailment probe
├── mhw_probe_ailments_watch.cpp # continuous ailment watch
├── mhw_probe_mantles.cpp        # mantle probe
├── mhw_probe_mantles_wide.cpp   # full region dump around EQUIPMENT_OFFSETS
└── mhw_probe_buildup_fast.cpp  # (legacy, not built)

docs/
├── ARCHITECTURE.md
├── I18N.md
└── PROBE-TOOLS.md
```

## Build details

CMake targets:

| target           | description                                            |
|------------------|--------------------------------------------------------|
| `mhw-overlay`    | the GUI binary; the thing you actually run             |
| `mhw-core`       | static lib: `mhw::StringTable` + future core utilities |
| `mhw-reader-tests` | schema integrity check (kPartSchemas has 72 entries)  |
| `mhw-core-tests` | `mhw::StringTable` load / miss / fallback             |
| `mhw-probe*`     | 7 diagnostic CLIs; skipped by ctest if MHW not running |

`ctest` runs the unit tests. The probe binaries are wired up but
SKIP_RETURN_CODE'd so they pass on a box that doesn't have MHW running.

## License

The core offsets and struct layouts in `mhw_reader.cpp` are derived
from the HunterPie project (HunterPie/HunterPie, Apache-2.0). Original
offsets and layouts are credited in the file comments.

The Linux port glue (`overlay_window.cpp`, `core/string_table.cpp`,
build system, packaging) is also Apache-2.0.

## See also

- `docs/ARCHITECTURE.md` — how the reader is wired together, why we
  avoid the QObject::tr ADL trap, the Yama `ptrace_scope` caveat.
- `docs/I18N.md` — adding a new locale, adding a new UI string.
- `docs/PROBE-TOOLS.md` — what each `mhw-probe-*` binary does and when
  to use which.