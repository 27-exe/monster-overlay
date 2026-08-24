# Control Console (monster-control)

The control console is a standalone Qt GUI for managing the
overlay without editing the command line. It owns the overlay
process and persists its toggle state.

## Binary

- **Source**: [`src/main_control.cpp`](../../src/main_control.cpp)
- **Entry**: `QApplication` + `QStringTable::load("zh-CN")` + new
  `ControlPanel` + `app.exec()`.
- **Self-test** (no display required): `--snap <path>` writes the
  full console window to a PNG and exits. Run with
  `QT_QPA_PLATFORM=offscreen` to capture without a real compositor.

```bash
# Run the console
./build/monster-control

# Take a screenshot of the console (no display needed)
QT_QPA_PLATFORM=offscreen ./build/monster-control --snap /tmp/console.png
```

## Architecture

```
ControlPanel (QMainWindow)
├── logo row (top, full width)
│   ├── "MHW · OVERLAY CONTROL" + version subtitle
│   └── READY / RUNNING badge + PREVIEW mark
├── body (3-column in v0.4; 2-column with sidebar in v0.5)
│   ├── LEFT — switch column (mask toggles per panel)
│   │   ├── PLAYER  ┐
│   │   ├── MONSTER │  one `buildGroup()` per panel
│   │   └── DAMAGE  ┘
│   │       ├── letter badge (P/M/D)
│   │       ├── panel title + sub
│   │       ├── master toggle (iOS-style capsule)
│   │       └── sub-row grid (2-column, one row per sub)
│   │
│   └── RIGHT — preview column (3 panel pixmaps)
│       ├── PLAYER preview tile
│       ├── MONSTER preview tile
│       └── DAMAGE preview tile
│
├── Hairline divider
│
└── EDIT MODE block
    ├── "ENTER EDIT" → spawn monster-overlay --edit
    └── "START"      → spawn monster-overlay (live)
```

## State flow

1. **Launch**: `ControlPanel` ctor calls `loadMaskFromDisk()` to
   restore toggle state from `~/.config/monster-overlay/monster-overlay.conf`.
   Defaults to all-on if the file is missing.
2. **Toggle**: any switch in a group calls `rebuildAndRender(idx)`,
   which:
   - Computes the panel mask from `master` + `subs` (a closed master
     yields mask = 0).
   - Calls `panel->setSectionMask(mask)` and updates the preview
     tile.
3. **Persist**: `closeEvent` and `~ControlPanel` both call
   `saveMaskToDisk()` — same 3-line `key=value` format used at
   load time.
4. **Launch overlay**: START or ENTER EDIT click:
   - Re-saves the mask (in case the user flipped toggles without
     closing the console).
   - Builds `argv` from the current mask.
   - `QProcess::startDetached(overlay, args, workingDir, &pid)`.
   - Hides the console window.
   - Starts a 250 ms QTimer that polls `kill(pid, 0)` to detect
     overlay exit.
5. **Overlay exit**: `onOverlayExited()` flips the status badge
   back to READY, stops the QTimer, deletes it, re-enables the
   START/EDIT buttons, and `show()`s the console.

## Persistence format

`~/.config/monster-overlay/monster-overlay.conf` (XDG path via
`QStandardPaths::GenericConfigLocation`, honours `XDG_CONFIG_HOME`):

```text
player=3f
monster=1f
damage=7
```

Three lines, each `key=<hex32>`. Malformed lines are silently
ignored on load; the file is rewritten atomically on save.

## Smoke test

`monster-control-l2-smoke` exercises the persistence round-trip:

1. Sets `XDG_CONFIG_HOME` to a temp dir.
2. Builds a `ControlPanel` and flips `player sub 2` and
   `monster sub 4` off via `findChildren<SectionRow*>()`.
3. Lets the dtor save to disk.
4. Re-opens a `ControlPanel` and verifies the row states match.

Run: `QT_QPA_PLATFORM=offscreen ./build/monster-control-l2-smoke`.
Expected output ends with `PASS`.

## Renderer delegation

The right-column preview tiles use the same `QPainter` code paths
as the live overlay. The three panels are real instances:

```cpp
// Each is a top-level QMainWindow in the live overlay. When the
// console hosts them for preview, they're rendered with
// WA_DontShowOnScreen + show() so the paint path runs without
// mapping a wayland surface.
player_  = new PlayerPanel();
monster_ = new MonsterPanel();
damage_  = new DamagePanel();
for (Panel *p : {player_, monster_, damage_}) {
    p->setAttribute(Qt::WA_DontShowOnScreen);
    p->setEditMode(true);   // seeds setupDemoData() on first paint
    p->show();
}
```

`ControlPanel::renderPreview(p)` then:

1. Calls `p->repaint()` (first paint seeds demo data and runs
   `setupDemoData()`).
2. Reads `p->contentSize()` (the synchronous logical size).
3. `p->resize(nat)` + `p->repaint()` second paint so the
   backing store matches the natural geometry.
4. Allocates a `QPixmap` of the right size, `p->render()`s into
   it, and returns the pixmap for the preview tile.

## Edge cases and limitations

- **Long-running overlay** — the console PID-poller keeps running
  forever if the overlay is launched externally. There is no
  "I’m not the parent" detection; if you started `monster-overlay`
  by hand and the console is closed, the console still tracks it.
  This is acceptable; the console is a launcher, not a service.
- **PID reuse** — the 250 ms poll uses `kill(pid, 0)`. If the OS
  recycles the PID while the overlay is still alive, the console
  will incorrectly think the overlay has exited. The interval
  is short enough that this is unlikely to cause data loss.
- **Mask persistence vs `--no-*`** — the persisted config does
  **not** store master visibility. Master visibility is a
  `--no-*` CLI flag that defaults to off. To launch a disabled
  panel, close the console after toggling the master switch off
  in the launcher block (the START/EDIT buttons), and the next
  launch will pick that up via mask = 0 — but the panel will
  still be visible (empty). True disable is via CLI only.

## v0.5 design notes

The current v0.4 console layout is a 2-column list-and-preview
grid. The v0.5 target (per `monster-overlay-concept .html`) is a
3-column layout:

```
[Sidebar 220px]  [Hero 1fr]            [Preview 1fr]
Panels + Actions  当前 panel master+sub  3 panel 真实渲染
```

A `SidePanelCard` widget prototype exists
(src/ui/sidebar_card.{h,cpp}) but is not wired in. The next
iteration should focus on `V5.3 layout only` (3-column
restructure) and stop there before attempting polish.
