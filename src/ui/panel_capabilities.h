#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// The keyboard capability list — a question put to a window, not a claim
// made about it.
//
// WHAT THIS ANSWERS: for ANY Panel, "which edit-mode keys does this
// window actually react to?" Asked once, answered as a plain struct, and
// answered the way the code behaves rather than the way a comment hopes
// it behaves.
//
// WHY IT IS DERIVED, NOT DECLARED.
//
// Panel already carries the honest answers: supportsColumns() is "would
// the [ ] dial do anything", and every other capability here is a
// property of the base class itself — Space folds, Esc quits, the arrows
// nudge margins, the wheel scales, - /= dial the floor and their Ctrl
// forms dial the lock. Panel::keyPressEvent (src/ui/panel.cpp:639) is
// the ONE dispatch site for all of them, so its behaviour is the spec.
// Adding a virtual capability() to Panel would give the answer a second
// home where it could disagree with the dispatch, and a capability list
// that is allowed to lie is worse than none at all.
//
// So this file READS Panel's existing public hooks and reports what they
// imply. It adds no virtual to Panel, declares no data, patches no
// panel, and touches no pixel: it is a pure function of what Panel
// already answers, so it cannot move a baseline.
//
// WHAT IT IS FOR. The user's report was 「这些窗口支持的快捷键各不相同，统一
// 一下描述与行为，至少要能说清楚什么窗口支持什么行为」. The half of that
// nobody could answer was "which keys do I have on THIS window". The
// matrix in docs/USAGE.md §12 and docs/USAGE.zh-CN.md §12 is generated
// from this list, and tests/panel_capabilities_tests.cpp pins it against
// the dispatch — so the next window that forgets supportsColumns() fails
// a test instead of shipping an undocumented dead key.
//
// ONLY WHAT IT CAN KNOW. Everything here is a capability of the WINDOW,
// not of the current session: whether the wheel scales anything right
// now is separately gated on editMode(), which is runtime state rather
// than a property of the window. A capability being true means "press
// this key and this window will react"; it does not mean "this press
// will do something in this instant".

#include "panel.h"

class Panel;

namespace mhw {

struct PanelCapabilities {
    // ── the two every window has ──
    //
    // Panel::keyPressEvent answers them BEFORE the edit-mode bail-out,
    // so they are live in live mode too: Space captures the size into
    // normalSize_, Esc calls saveConfig() then quits the application.
    bool fold{true};
    bool quit{true};

    // ── the four edit-mode-only ones ──
    //
    // Every one of these sits AFTER `if (!editMode_) return
    // QMainWindow::keyPressEvent(e);` in Panel::keyPressEvent, so a
    // stray keystroke in-game cannot reposition or resize an overlay
    // over the action. Identical for every window — the point of this
    // struct is that SAYING so is now possible, not that the flags
    // differ.
    bool move{true};    // arrows: nudge margins 10 px (Shift = 50 px)
    bool scale{true};   // wheel: zoom 0.5× – 3×, persisted immediately
    bool resizeW{true}; // - / = : the min-width floor, 16 px per press
    bool resizeH{true}; // Shift+- / Shift+= : the min-height floor
                        //
                        // Not a separate flag because it is not a
                        // separate capability: Ctrl + the same pair
                        // dials the SET size (the lock) instead of the
                        // floor — Panel::nudgeLockWidth/Height. Both
                        // axes have both halves on the same two faces.

    // ── the one that is NOT uniform ──
    //
    // Read straight off Panel::supportsColumns(). This is the only
    // capability a window can decline, and declining is the default:
    // only a window with a wrapped row of items to re-flow has a column
    // axis to dial. For the four base panels and the strips that are not
    // a row (bars, weapon, info) the keys reach
    // QMainWindow::keyPressEvent() unclaimed, exactly as they did before
    // the keys existed on the base class at all.
    bool columns{false}; // [ / ] : one item per row (vertical) vs many (flat)
};

// The derivation, deliberately in ONE place.
//
// A free function rather than a Panel member, so the base class stays as
// it is. IF A CAPABILITY IS EVER ADDED HERE, it must come from something
// Panel already answers. There is deliberately no bitmask, no virtual,
// and nothing a subclass can set: the whole value of this file is that it
// cannot be out of date by construction, and an override-able field would
// undo exactly that.
//
// `inline` because this is a header by design. A .cpp here would have to
// be registered in CMakeLists.txt (and in tests/tu_census.json, which
// covers src/*.cpp but is blind to headers) before it could be used at
// all; an inline body needs neither and therefore cannot break the
// build the day it is added.
[[nodiscard]] inline PanelCapabilities panelCapabilities(const Panel &p)
{
    PanelCapabilities caps;
    // The ONLY read that is not "the base class does this for everyone".
    caps.columns = p.supportsColumns();
    return caps;
}

// ── notation, not prose ──────────────────────────────────────────────────
//
// The literal key faces a capability lives on, for a tooltip or a probe
// line. NOT for a translated sentence: `[` `]` `-` `=` are notation and
// survive every locale unchanged, whereas "dial the min-width floor" is
// prose and MUST go through mh::tr(). Returning the glyphs here keeps
// that boundary explicit — whoever adds a user-visible sentence takes it
// from an i18n key, not from here.
[[nodiscard]] inline QString panelCapabilityKeys(bool fold,
                                                 bool quit,
                                                 bool move,
                                                 bool scale,
                                                 bool resizeW,
                                                 bool resizeH,
                                                 bool columns)
{
    QString keys;
    if (move)
        keys += QStringLiteral("↑↓←→ ");
    if (scale)
        keys += QStringLiteral("wheel ");
    if (fold)
        keys += QStringLiteral("Space ");
    if (quit)
        keys += QStringLiteral("Esc ");
    if (resizeW)
        keys += QStringLiteral("- = ");
    if (resizeH)
        keys += QStringLiteral("⇧- ⇧= ");
    if (columns)
        keys += QStringLiteral("[ ] ");
    return keys.trimmed();
}

} // namespace mhw
