// SPDX-License-Identifier: Apache-2.0
#pragma once

// Console text helpers — the pure, widget-free text dispatchers the
// control console reads through whenever it needs a localized string.
//
// This is a move, not a rewrite: the three bodies came 1:1 out of the
// anonymous namespace at the top of control_panel.cpp (HEAD 0a88737,
// lines 85-403) and are reproduced verbatim in the .cpp. Nothing was
// normalized, reordered or "improved" on the way in; a branch that looks
// wrong is a property of the original code and belongs to a change that
// owns it.
//
// Hard rule, same as ConsoleLayoutStore / DamageViewModel /
// MonsterViewModel / RiseReframeworkStatus: this header and its
// implementation carry NO widget dependency — only QtCore types plus
// the pure data headers that define GameId and the section tables. The
// View calls what these publish and paints the returned strings
// unchanged; nothing here knows a QWidget exists.
//
// Why three of the seven moved and four did not is a dependency fact,
// not a judgement: the other four (prepareDamagePreview, qssBase,
// iconKind, panelAccent) each read a live GUI object — DamagePanel,
// Panel's virtual demo hook, the SectionRow::Icon palette, uiTheme().
// They cannot be reached without constructing the console, so they stay
// with the View (see the note left at the remaining namespace in
// control_panel.cpp). These three are reachable from a plain logic test,
// which is the point of the split.
//
// Namespace: `mhw`, like every other thing this layer publishes that a
// test may reach — formatters.h, panel_sections.h, icon.h,
// screen_query.h, and the four ViewModels this joins. NOT `mh::`, which
// is the paint-path alias the panels and the console share on the View
// side and which console_text_helpers.cpp reproduces locally so
// consoleText()'s original body resolves unchanged.

#include <QString>

#include "monster/monster_types.h"

namespace mhw {

// A console.section.* label for (panel index, section bit index).
// Delegates to the dynamic panel_sections.h table.
[[nodiscard]] QString sectionLabel(int panel, int index);

// The display name of a game. One resolver so the auto-detect badge,
// the GAME column and the switching status line can never drift apart.
[[nodiscard]] QString gameName(GameId id);

// Resolve a console i18n key through the shared StringTable.
[[nodiscard]] QString consoleText(const QString &key);

} // namespace mhw
