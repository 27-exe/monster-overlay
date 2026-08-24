# Architecture

> **v0.4 reader/layout overview** — see `docs/V0.4-STATUS.md` for the
> v0.4 status snapshot and the v0.5 design notes. **The v0.2 split plan
> in §"What goes where" below is now landed** — `src/monster/`,
> `src/player/`, `src/quest/`, `src/world/`, and `src/ui/panel_*.{h,cpp}`
> exist exactly as described. The reader at `src/mhw_reader.cpp` is
> an orchestrator over the per-domain readers.
>
> For the v0.4 control console architecture (monster-control subprocess,
> section masks, master visibility), see `docs/CONTROL_CONSOLE.md`.

---

# Architecture

## Goals (and how we get there)

1. **Pluggable readers.** `mhw_reader.cpp` is a top-level orchestrator
   that composes a per-domain reader per data source (monster / player
   / quest / world). New fields belong in the appropriate sub-reader,
   not in the orchestrator.
2. **Low coupling, high cohesion.** Each sub-reader has a single
   responsibility (one struct family). UI never touches memory; data
   never knows about UI.
3. **Stable data interface.** All UI consumes `mhw::GameSnapshot`,
   which is the only type that the UI layer imports from data.

In v0.1 we laid the directory skeleton and externalised strings. The
internal split is planned for v0.2 alongside the UI rebuild.

## Process model

```
┌─────────────────────────────────────────────────────┐
│  monster-overlay (Qt GUI, layer-shell, single-process)   │
│                                                      │
│  ┌────────────┐  ┌──────────────────────────────┐  │
│  │ OverlayWindow│ │ MhwReader (orchestrator)     │  │
│  │ (UI)        │  │                              │  │
│  └─────┬──────┘  │  ┌────────────┐ per-Tick:    │  │
│        │ mh::tr  │  │  GameSnapshot out          │  │
│        │ GameSnap│  │  ────────────              │  │
│        │ in      │  │  discover PID              │  │
│        │         │  │  follow chain:             │  │
│        │         │  │   EQUIPMENT_ADDRESS →      │  │
│        │         │  │   PLAYER_BASIC_OFFSETS →   │  │
│        │         │  │   Player struct            │  │
│        │         │  │  + ABNORMALITY_OFFSETS →   │  │
│        │         │  │   75-slot timer array      │  │
│        │         │  │  + EQUIPMENT_OFFSETS →     │  │
│        │         │  │   mantles + cooldowns      │  │
│        │         │  │  + MONSTER_LIST_ADDRESS →  │  │
│        │         │  │   Component[] → Monster    │  │
│        │         │  │   + part normal + severable│  │
│        │         │  │   + ailments + enrage     │  │
│        │         │  └────────────────────────────┘  │
│        │         │                                   │
│        │         │  ┌────────────────────────────┐  │
│        │         │  │ core/ProcessMemory         │  │
│        │         │  │  process_vm_readv()        │  │
│        │         │  │  + sane() pointer filter  │  │
│        │         │  └────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
              │                  │
              │ mh::tr("ui.*")  │ process_vm_readv(pid, ...)
              ▼                  ▼
   ┌───────────────────┐  ┌────────────────────┐
   │  QRC JSON         │  │  /proc/<mhw>/mem   │
   │  (string table)   │  │  (Wine Mapped)     │
   └───────────────────┘  └────────────────────┘
```

UI is **completely decoupled** from data. It calls `mh::tr("ui.xyz")`
to get a translated string and reads from `mhw::GameSnapshot`. The
data layer never knows that strings, panels, or a window exist.

## mh::tr vs QObject::tr

We need a translation lookup function in the overlay. Qt already
provides one: `QObject::tr(const char*, const char*, int)`. The catch
is that `QObject::tr` is a **virtual member function of every QObject
subclass**, and inside an `OverlayWindow` member function an
unqualified call to `tr(...)` is resolved by **Argument-Dependent
Lookup** on the implicit `this->QObject`. That routes the call to
`QObject::tr(const char*, const char*, int)`, which expects C
strings. Passing a `QString` either fails to compile or silently
returns the literal `const char*` from a default arg.

We avoid this by using a **non-conflicting namespace**:

```cpp
// src/overlay_window.cpp
namespace mh {
inline QString tr(const QString& key) {
    return mhw::StringTable::instance().tr(key);
}
}
// ...
void OverlayWindow::render(...) {
    context_->setText(mh::tr("ui.context_hunting").arg(zoneName));
}
```

The namespace prefix `mh::` is the disambiguator. A macro `tr(...)` or
unqualified `tr(...)` would lose this fight to `QObject::tr` again.
The cost of writing `mh::` at every call site is 4 characters — a
small price for the bug we no longer have to chase.

`mhw::StringTable` itself uses `QHash<QString, QString>` keyed by
dot-path strings (`"ui.zone.astera"`, `"mantle.13"`). The C++ side
sees a flat namespace; the JSON side sees a nested object that the
loader flattens on `load()`.

## Data flow on every tick (1 Hz)

1. Overlay timer fires.
2. `MhwReader::poll()` runs the chain above, producing a fresh
   `mhw::GameSnapshot`.
3. `OverlayWindow::render(snapshot)` is called.
4. The window calls `mh::tr("ui.x.y")` ~60 times to compose panel
   labels. Each call is a single `QHash::constFind` — sub-microsecond.
5. `setText()` updates the corresponding `QLabel`. Qt re-renders the
   layer-shell surface.

## Memory reading rules

- **Only `process_vm_readv`**, never `open("/proc/<pid>/mem")`. The
  former works across `ptrace_scope=1` between the overlay process
  and the proton-wrapped MHW; the latter returns `EPERM`.
- **`sane()` pointer filter** on every `read<T>(address)`: reject
  addresses outside `[0x10000, 0x8000_0000_0000)`. A bad pointer
  here is almost always a 4 GiB pread that we'd otherwise see as
  `EIO` with no clue.
- **No `WriteProcessMemory`**, no `ptrace(PTRACE_ATTACH)`. We never
  need to write into the game; the data we need is naturally written
  by the game itself (HP/部位/异常/任务 timer all update in-engine).
- **`/proc/<pid>/maps` with `QByteArray`, not `QTextStream`**: when
  the install path is `.../Monster Hunter World/MonsterHunterWorld.exe`
  (literal space, possible CJK in the user dir), `QTextStream` over
  a POSIX-locale `QIODevice::Text` silently drops the offending
  bytes, and the reader says "no MHW process found" while `grep`
  finds the line instantly.

## The Yama `ptrace_scope` caveat

`kernel.yama.ptrace_scope` is a per-system sysctl. On Arch defaults
to `1` ("only a parent process can ptrace"). Under this mode:

| API                          | Same UID, same session? | Same UID, different session? |
|------------------------------|--------------------------|--------------------------------|
| `open("/proc/<pid>/mem")`    | OK                       | EPERM (Yama)                    |
| `process_vm_readv(...)`      | OK                       | **OK** (mm_access only)        |

We run as a desktop process reading a Proton-wrapped game in a
different session. We use `process_vm_readv` and never `open(mem)`.
The monster-probe tools follow the same rule.

## What goes where (v0.2 split plan)

```
src/
├── core/
│   ├── process_memory.{h,cpp}     # /proc/<pid>/mem + sane() filter
│   ├── string_table.{h,cpp}       # ✅ done in v0.1
│   └── result.h                   # Result<T, ReadError> for read failures
│
├── address_map/
│   ├── address_map.{h,cpp}        # parser for HunterPie .map files
│   └── build_id_resolver.{h,cpp}  # auto-pick the .map matching the running MHW
│
├── monster/
│   ├── monster_types.h            # MonsterSnapshot, PartSnapshot, AilmentSnapshot
│   ├── monster_reader.{h,cpp}      # orchestrator: HP + parts + ailments + enrage
│   ├── monster_hp_reader.{h,cpp}   # total HP + enrage struct
│   ├── monster_part_reader.{h,cpp} # normal + severable tables
│   └── monster_ailment_reader.{h,cpp} # monster+0x1BC40 pointer chain
│
├── player/
│   ├── player_types.h
│   └── player_reader.{h,cpp}      # HP/ST + 75-slot abnormalities + mantles
│
├── quest/
│   ├── quest_types.h
│   └── quest_reader.{h,cpp}       # quest state, timer, deaths
│
├── world/
│   ├── zone_types.h
│   └── zone_reader.{h,cpp}        # zone id, isHunting/Peace predicates
│
└── ui/
    ├── overlay_window.{h,cpp}     # ✅ exists; will be split into 3 panels
    ├── formatters.{h,cpp}         # percentage(), seconds(), mm:ss()
    └── panel_*.{h,cpp}            # one per panel (v0.2)
```

The v0.1 reader (`mhw_reader.cpp` ~2000 lines) is the source for this
split. The split is mechanical: extract a struct + its reader into a
sibling pair, add `#include "monster/monster_types.h"`, delete the
inline struct from `mhw_reader.h`. The orchestrator becomes ~200
lines of `MonsterReader::read(snapshot)`, `PlayerReader::read(...)`,
etc.

## v0.2 scope (planned)

The user's stated v0.2 goals are:

1. **Player status panel** — re-organise the player line, add
   sharpness/coating/cooler timers (DAMAGE_ADDRESS adjacent slots),
   food buff icons, weapon-specific gauges.
2. **Monster HP panel** — split the current single monster block
   into a per-monster card with HP bar, enrage bar, severable
   indicator, ailment icons, part list as a grid rather than a
   vertical column.
3. **DPS / damage chart** — read the 4 玩家 damage fields at
   `DAMAGE_ADDRESS + DAMAGE_OFFSETS[i*0x2A0]`, sample at 1 Hz, plot
   as a sparkline. HunterPie uses MinHook'd `DealDamage`; on Linux we
   only get cumulative damage (the game writes it), but a 1-second
   windowed diff gives us per-second DPS without injecting.

Assets for v0.2: see `assets/icons/` and `assets/charts/`. The
content pipeline is "ship a default skin + a documented override
mechanism" — exactly how HunterPie does it (`HunterPie.UI/Assets/`).
We'll define that pipeline in v0.2 itself.

## What's intentionally NOT in this codebase

- **No DLL injection** (no `CreateRemoteThread`, no MinHook). Wine's
  D3D11 translation layer means the in-process hooks HunterPie uses
  don't exist on Linux. The data we need is already in the game's
  own memory; we don't need to hook anything to read it.
- **No WPF / WinForms / XAML.** All UI is Qt 6 with KDE layer-shell.
  WPF XAML porting is out of scope.
- **No Discord Rich Presence, telemetry, or cloud sync.** Pure
  read-only memory display.