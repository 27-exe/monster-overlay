#pragma once

// v1.0.0 fidelity port: THE CONSOLE'S WINDOW MODEL, AS ITS OWN TU.
//
// WHAT THIS IS. The prototype's board is driven by one object —
// `state.scopes[game+mode].windows[]`, where each entry names the panel
// it belongs to, its position in that panel's display, its scale /
// opacity / visibility, the sections it shows and the display it is
// aimed at. Everything the console does — the canvas's bindings, the
// inspector's three columns, the export payload, the eye toggles — is a
// read or a write over that list.
//
// The Qt console had no equivalent. It had four mother Panels (one per
// panel slot, addressing `[game/mode/<key>]`) and, in Split, fourteen
// sub-window Panels owned by a host private to control_panel.cpp. Which
// is a fine representation of the WINDOWS, and no representation at all
// of the LIST: nothing could answer "which windows live on DP-1 right
// now", "what is the selected window", or "give me the JSON for this
// scope" without the console re-deriving it from four different
// objects. That is the gap the prototype's inspector assumes is closed.
//
// WHY A SEPARATE FILE. control_panel.cpp is 6,600 lines and already
// carries the toolbar, the sidebar, four inspectors and the split host.
// Adding the window model to it would make it the one place a change to
// "how a window is described" lands — and the description is needed by
// three different surfaces. A header + a small TU keeps the model
// independently testable and keeps ControlPanel's diff to the call
// sites.
//
// WHAT THIS IS NOT.
//
//   * NOT an owner. Every Panel* here is borrowed from the console's
//     ctl_[] array or from SplitWindowPreviewHost. The model is a VIEW
//     over live objects, and a row that names a Panel which has since
//     been rebuilt is dropped rather than cached.
//   * NOT a second settings store. Position / scale / opacity / output
//     all live where they always lived, in the Panel itself and in
//     panels.ini. Nothing here writes to disk.
//   * NOT a controller. It has no signal and no slot; the console wires
//     the widgets to the setters below and re-reads the model.
//
// THE COORDINATE CONTRACT, stated once because every accessor below
// depends on it: a window's x/y is the LOGICAL offset inside its own
// target display, which is
//
//     own outputName  →  the parent panel's outputName  →  the primary
//
// resolved in that order (HudCanvas::windowOutputName spells the same
// chain, and the prototype's `follow` entry is the "inherit" spelling
// of it). There is no global desktop coordinate anywhere in this
// struct, because panels.ini has never stored one and the export
// contract says so in as many words.
//
// TWO AXES, NOT ONE. `panelEnabled` (Panel::panelEnabled()) is the
// master gate for a whole panel — it decides whether the panel mounts
// at all. `visible` is the per-WINDOW switch the prototype draws, and
// it is a separate fact: a split window can be hidden while its parent
// panel still mounts the other five. Qt has a persisted key for the
// first and, until this port, nothing for the second; see
// WindowState::visiblePersisted for how that is stored.

#include <QMargins>
#include <QString>
#include <QVector>
#include <cstdint>

class Panel;
class QScreen;

#include "ui/screen_query.h"

namespace mhw {

// Which of the four panel slots a window belongs to. Mirrors the fixed
// slot order everywhere else (hud_canvas's parentSlot, ctl_[]).
[[nodiscard]] inline bool isFidelityPanelIndex(int idx)
{
    return idx >= 0 && idx < 4;
}

// One window as the inspector describes it.
//
// A row is either a SPLIT sub-window (subIndex >= 0, a live index into
// the host's window list) or a MOTHER panel (subIndex < 0, one per
// enabled panel slot). Both are the same window to the user — the
// prototype's WIN_META table mixes them in one list — so both carry
// the same fields and the discriminator is only needed to reach the
// Panel.
struct WindowState {
    // Identity.
    int panelIndex = -1;     // 0 player / 1 monster / 2 damage / 3 pets
    int subIndex = -1;       // >= 0: a split sub-window; < 0: mother
    QString key;             // stable settings key ("player", "player-status", …)
    QString title;           // display name, localized by the caller

    // Geometry, in the window's own target display's logical space.
    QMargins margins;
    qreal scale = 1.0;
    qreal opacity = 1.0;

    // Screen. `outputOwn` is the window's OWN outputName — empty when
    // the window has not been given one (the chain default, which is
    // the prototype's `follow`), and a real name once it has; a split
    // sub-window may carry one and the console writes it there
    // (see ControlPanel::applySelectedWindowOutput). `outputResolved`
    // is the effective target after the own → parent → primary chain,
    // and is what "which display is this on" means.
    QString outputOwn;
    QString outputResolved;
    QString primaryOutput;    // the primary's name, for the chain's tail

    // The two visibility axes (see the file header).
    bool panelEnabled = true;     // the whole panel's master gate
    bool visiblePersisted = true; // this window's own switch, from panels.ini

    // What the window shows. v1.0.0 (C): `sectionMask` is the window's
    // own live mask — for a mother panel the panel's mask AND its
    // game-filtered schema, for a split window its table row's claim AND
    // the panel's live mask. `sectionsOn` / `sectionsTotal` count BITS
    // OF THAT SAME SCHEMA (panel_sections.h kAll and the k<Panel>Split
    // targets tables), so `2/3` means "two of the three blocks this
    // window can draw are switched on" and never a placeholder 1/1.
    uint32_t sectionMask = 0xFFFFFFFFu;
    int sectionsOn = 0;
    int sectionsTotal = 0;
};

// The whole list for one scope, in the order the prototype lists them:
// by panel, then by that panel's split-table row. The index of a row in
// this vector IS the index the canvas's sub-window bindings use for the
// split rows, which is what makes "the inspector selected row 4" and
// "the canvas highlighted row 4" the same statement.
//
// Built by ControlPanel::buildWindowStates() — this header deliberately
// knows nothing about the console's members, so the model can be
// reasoned about (and asserted on) without a QApplication.
using WindowList = QVector<WindowState>;

// ─── the per-window visibility key ────────────────────────────────────
//
// The prototype stores `windows[].visible` per window; panels.ini had
// no key for it, and inventing one is the whole change.
//
// THE KEY. `winVisible` under the window's own [game/mode/<key>] group
// — the same group every other appearance key uses, so it travels with
// the scope pair for free (a switch in one of the four domains never
// moves the other three).
//
// ONE POLARITY, BOTH HALVES: THE STORED VALUE MEANS VISIBLE, and the
// default is true. `on == false` writes `winVisible=false`; `on ==
// true` REMOVES the row, so a console that never touches the switch
// never adds a row to anyone's file — the same "no spurious key" rule
// saveConfig() holds itself to — and a window nobody ever hid answers
// "visible" from the default alone. readWindowVisible() is therefore
// `value(key, true)` with NO negation: the first spelling of it negated
// the stored false back to "visible", so a window the user had just
// hidden read back as shown and the inspector checkbox never stuck.
// A group that has never carried the key answers true, so an existing
// install sees every window exactly as it was.
[[nodiscard]] QString windowVisibleSettingsKey();

// Read one window's own switch out of panels.ini. `scope` is the
// [game/mode/<key>] group name the caller already resolved; the read
// is deliberately done by the caller so this function needs no
// QSettings include and no knowledge of the scope globals.
[[nodiscard]] bool readWindowVisible(const QString &scope, const QString &key);

// Write one window's switch. `on == true` writes nothing (absent means
// visible); `on == false` writes `winVisible=false`.
void writeWindowVisible(const QString &scope, const QString &key, bool on);

// ─── v1.0.0 integration (stage A): the #window-count predicate ─────────
//
// reference.html:476 sets `#window-count` to `visibleWindows().length`,
// and visibleWindows() (reference.html:442) is the prototype's own three
// questions about a window. This is that predicate, in the model's own
// vocabulary, so the stage head and a test can ask the SAME question:
//
//   panelEnabled   the panel master (prototype: `panel.visible`)
//   visiblePersisted   this window's switch (prototype: `w.visible`)
//   outputResolved == screenName   the display the board is drawing
//
// WHAT IS NOT IN HERE, AND WHY:
//
//   * GAME CAPABILITY. The prototype's `defs()` only lists the windows
//     the scope has, so a World build simply has no pets row to filter.
//     Same here: buildWindowStates() never emits it, so a caller that
//     loops over the list gets the capability filter for free.
//   * THE SECTION MASK. A window with every section off is still a
//     window on the board — drawn as the `.hidden-window` placeholder —
//     because reference.html:443 `isShown()` (does it have anything to
//     SHOW) is a separate question from `visibleWindows()` (is it on
//     the board). Gating the count on the mask would make the chip
//     disagree with the paint the moment the user switched a panel off.
//   * panelEnabled is read here but NOT pushed into the canvas's
//     visibility setters (ControlPanel::pushWindowVisibility): the two
//     are different axes with different consumers, and this function
//     exists precisely so neither has to borrow the other's spelling.
//
// `screenName` is compared exactly, the way resolveOutput()===state.display
// compares exactly. An empty screen name therefore matches only a window
// that resolved to empty, which is the same answer the JS gives.
[[nodiscard]] bool windowOnDisplay(const WindowState &w,
                                   const QString &screenName);

// ─── the effective target display ─────────────────────────────────────
//
// own → parent → primary, spelled as one function so the canvas's
// filter, the inspector's readout and the export payload all resolve a
// window the same way. `screens` is screen_query::listOutputs()'s
// answer (empty is fine: an empty list yields an empty primary and the
// chain stops at the parent, which is the honest answer for a machine
// with no outputs Qt can see).
[[nodiscard]] QString resolveWindowOutput(const QString &own,
                                          const QString &parentOutput,
                                          const QString &primaryOutput);

// The label the prototype prints for a display: "DP-1 · 2560×1440",
// or the bare name when the size is unknown. Built from the screen
// list rather than from a second table, so a rotated output shows the
// rotated logical size Qt actually reports.
[[nodiscard]] QString displayLabel(const QString &outputName,
                                   const QList<screen_query::OutputInfo> &screens);

} // namespace mhw
