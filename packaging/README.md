# Monster Overlay v1.1.5

Linux-native HUD overlay for **Monster Hunter: World** and **Monster Hunter
Rise** — player/monster/damage panels drawn on top of the game (Wayland
layer-shell), reading the game's memory from outside the process.

## 1. Requirements

- x86_64 Linux, Wayland session with a layer-shell compositor (KDE Plasma,
  Hyprland, niri, sway, …). **X11 is not supported.**
- **Steam + GE-Proton 10-34** (recommended; the Proton 11 family also works)
- **Qt 6.12** (Core/Gui/Widgets/Svg), **glibc 2.34+**, and `layer-shell-qt`.
  The C++ runtime must provide **CXXABI_1.3.15** (libstdc++ from GCC 14 or newer).
  Older Qt or libstdc++ versions fail at load time with missing symbols.
  `./install-deps.sh` installs runtime dependencies on Arch only.

Reading the game's memory normally works as-is: Steam launches the game inside
a user namespace owned by you, which satisfies the kernel's ptrace check. If a
read is ever denied, the overlay says so, and the two remedies are
`sudo setcap cap_sys_ptrace+ep ./monster-overlay` (per binary, lost when the
file is replaced) or `sudo sysctl kernel.yama.ptrace_scope=0` (temporary).

**World needs nothing else.** Rise's damage panel additionally needs the
REFramework Lua producer shipped in `reframework/autorun/` — the console
installs it for you (see §2).

## 2. Start it

```bash
tar -xzf monster-overlay-v1.1.5-linux-x86_64.tar.gz
cd monster-overlay-v1.1.5-linux-x86_64
./install-deps.sh      # one-time: Qt 6 + layer-shell-qt
./monster-control      # control console
```

Start the game first (past the main menu), then press **START OVERLAY** in the
console's top bar. `./install.sh` additionally copies the binaries to
`~/.local/bin` and the maps to `~/.local/share/monster-overlay/data`.

The UI follows your desktop language on first run (English; Chinese when the
system locale is `zh*`). Change it under **Settings → Language**; the saved
choice takes precedence on later launches.

### The console

A native window: a sidebar with the game and the panel groups, a display
overview in the middle, and a three-column inspector for whatever you click.

- **Layouts.** *Single* draws each panel as one window, *Split* gives every
  section group its own window, *Mixed* lets you decide window by window
  whether it stays separate or merges back into its main panel. The choice is
  stored per game × layout, so the two games never overwrite each other.
- **Edit layout** moves windows on the console board; **Edit in overlay**
  launches the real overlay so you can drag them over the game.
- **Reset Layout** restores the focused group to the shipped default, **Reset
  all** covers both games and all three layouts (it asks first), **Import
  Layout** applies a layout package, **Export layout** writes the current one
  out as text. Changes are written when the console closes — there is no
  separate Save button.
- The preview is demo content: the console never reads game memory. Only the
  process detection is real.

### Rise damage panel (REFramework)

Open **Settings → Rise environment · REFramework & scripts**. The section shows
the install state and installs the Lua producer for you. Rise does not expose
live damage to memory reading, so the producer samples it inside the game and
writes to `reframework/data/`; the overlay consumes the newest snapshot.

The **Fix menu popup** button stops REFramework's menu from re-opening on
every launch — it flips exactly one setting and keeps a `.pre-monster-overlay`
copy you can restore by hand. **Restore default** puts it back.

## 3. Something is wrong? Run `monster-doctor`

```bash
./monster-doctor       # writes ./monster-doctor-<timestamp>.txt
```

One command, read-only, no privileges, ~15 s. The report records where the
address maps were found, whether a game process is visible (and at which image
base), the reader's verdict with the exact errno, which Proton build the
running game uses, and what the overlay itself prints on stderr. Attach the
file to a GitHub issue or discussion.

Nothing private is collected: paths under `~` print as `~`, and no environment
variables, Steam account data, game memory contents or other processes'
command lines are included.
