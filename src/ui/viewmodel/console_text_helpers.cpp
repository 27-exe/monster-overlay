// SPDX-License-Identifier: Apache-2.0
#include "ui/viewmodel/console_text_helpers.h"

// Console text helper implementations — bodies moved 1:1 out of the
// anonymous namespace at the top of control_panel.cpp (HEAD 0a88737,
// lines 85-403). Every line below the namespace opener is the original
// text, byte for byte; the diff evidence is in F1-REPORT.md.

#include "core/game_capabilities.h"
#include "core/string_table.h"
#include "ui/panel_sections.h"

#include <QString>

// The panels' paint-path alias, reproduced verbatim so consoleText()'s
// original body (`return mh::tr(key);`) resolves here exactly as it did
// in control_panel.cpp without the View's `namespace mh` block above it.
// Same definition as panel_player.cpp / panel_monster.cpp /
// panel_damage.cpp / control_panel.cpp (inline -> ODR-safe), see
// docs/I18N.md for the "no fallback in code" rule.
namespace mh {
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

namespace mhw {

QString consoleText(const QString &key)
{
    return mh::tr(key);
}

// ---- v0.9 i18n helpers ---------------------------------------------------
// Game display name (WORLD/RISE). One resolver so the auto-detect badge,
// the GAME column and the switching status line can never drift apart.
//
// S4: this was `id == Rise ? rise_key : world_key`, a two-way binary with no
// arm for a third game, so one would have rendered as "WORLD". The mapping
// now lives in core/game_capabilities.h as a per-game table.
QString gameName(mhw::GameId id)
{
    return mhw::gameDisplayName(id);
}

// Section-switch display label for (panel, bit index) — delegates to the
// now-dynamic panel_sections.h table (console.section.*).
QString sectionLabel(int panel, int index)
{
    if (panel == 0) return mhw::PlayerSection::displayName(index);
    if (panel == 1) return mhw::MonsterSection::displayName(index);
    if (panel == 2) return mhw::DamageSection::displayName(index);
    if (panel == 3) return mhw::PetDamageSection::displayName(index);
    return {};
}

} // namespace mhw
