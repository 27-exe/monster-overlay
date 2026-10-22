# Monster Overlay v1.1.5

**The console is a native window now.** The whole control console was
rebuilt in Qt6 — no more embedded HTML page, no more "click something and
hope the layout behind it updates". The sidebar lists the game and the panel
groups, the middle shows a display overview you can click into, and the
inspector on the right describes whatever you selected.

## What you get

- **Three window layouts.** *Single* (one window per panel), *Split* (every
  section group gets its own window) and *Mixed* — new, and the point of this
  release: you decide **per window** whether it stays separate or merges back
  into its main panel. The choice is stored per game × layout, so World and
  Rise never overwrite each other, and `--print-layout-plan` prints the
  resolved topology as JSON without touching a config file.
- **Layout editing that behaves.** Drag windows on the console board, or
  launch the real overlay and drag them over the game. Reset one group to the
  shipped default, reset everything, import a layout package, export the
  current one as text.
- **A preview that tells the truth.** The console draws the same panel code
  the overlay uses, with demo data — and it now mounts exactly the windows the
  overlay mounts for the game in force: ten windows under World (no 翔虫), ten
  under Rise (no 衣装). A reset repaints the board immediately instead of
  waiting for you to click something else.
- **Output ownership you can read.** The target-display picker names the panel
  a window follows, and the line under it prints the screen it actually
  resolves to right now.
- **Help and settings that match the code.** The help panel lists the real
  keyboard shortcuts (the zoom `−` / `+` / `1:1` are buttons, and the panel
  says so), and the Settings dialog carries appearance, language, the Rise
  REFramework section and About.

## Upgrade notes

- Configuration keeps working: `~/.config/monster-overlay/panels.ini` and
  `monster-overlay.conf` are read exactly as before, and the console writes
  them when it closes (or before it relaunches the overlay). There is no
  separate Save button any more — the write was already automatic.
- The bundled `monster-control` requires **Qt 6.12**; the overlay and
  `monster-doctor` as well. Arch and other rolling releases have it;
  Debian trixie and Ubuntu 24.04 do not.
- Rise's damage panel still needs the REFramework Lua producer. The console
  installs it from **Settings → Rise environment · REFramework & scripts**.

## Tarball

```
monster-overlay-v1.1.5-linux-x86_64.tar.gz
```

Binaries: `monster-overlay` (the HUD), `monster-control` (the console),
`monster-doctor` (a read-only diagnostic report when the overlay shows
nothing). See the packaged README for requirements and startup.
