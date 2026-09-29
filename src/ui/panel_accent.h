#pragma once

#include <QColor>

// Single source of truth for the overlay panels' identity accent, indexed
// by the fixed slot order shared with mhw::kPanelCount / PanelSource
// (0 Player, 1 Monster, 2 Damage, 3 Pets).
//
// WHY THIS FILE EXISTS
// --------------------
// The palette used to be spelled out in three places that had drifted
// apart: Panel::accentColor() (the overlay's real paint path),
// HudCanvas's preview constants, and the tile stripe in
// ControlPanel::renderPreview(). The two console-side copies were
// written without consulting the paint path, so the damage and pets
// panels previewed as blue #40a9ff / green #67d69d while the live
// overlay drew teal #50c5b7 for both — the preview misrepresented the
// very thing it exists to show. Everything reads this one table now.
//
// PRODUCT DECISION (v0.11, owner: user): panel colours are FIXED and do
// NOT follow the console theme. uiTheme() drives the console's own
// chrome (surfaces, QSS, rail, chips) and must never be read here — a
// HUD floating on top of the game must look identical in every theme.
//
// The values below are what Panel::accentColor() returns, and the
// committed pixel baselines in tests/baseline/ pin them. Changing a
// channel here changes the overlay's rendered pixels, so the
// snapshot-regression gate is the authority on any edit.

namespace mhw::panel_accent {

// Component triplets rather than QColor objects: QColor's accessors and
// operator== are not constexpr, whereas this layout lets a wrong channel
// be a compile error instead of a silently mis-coloured pixel.
struct Rgb { int r, g, b; };

constexpr Rgb kPlayer  {167,  79, 255};   // #a74fff  HTML: --accent-purple
constexpr Rgb kMonster {255, 112,  67};   // #ff7043  HTML: --enrage orange
constexpr Rgb kDamage  { 80, 197, 183};   // #50c5b7  HTML: --accent-teal

// The pets panel has no accent of its own: DamagePanel and PetDamagePanel
// both draw their chrome through Panel::Accent::Damage, so slot 3 must
// share that hue. The separate name documents that the slot exists
// without inventing a fourth colour the overlay would never paint.
constexpr Rgb kPets = kDamage;

static_assert(kPlayer.r == 167 && kPlayer.g == 79 && kPlayer.b == 255,
              "player chrome is the HTML --accent-purple token");
static_assert(kMonster.r == 255 && kMonster.g == 112 && kMonster.b == 67,
              "monster chrome is the HTML --enrage orange token");
static_assert(kDamage.r == 80 && kDamage.g == 197 && kDamage.b == 183,
              "damage/pets chrome is the HTML --accent-teal token");
static_assert(kPets.r == kDamage.r && kPets.g == kDamage.g
                  && kPets.b == kDamage.b,
              "the pets panel shares the damage chrome");

// Slot lookup for the console-side callers that only have an index.
// Out-of-range slots fall back to the damage accent, mirroring
// Panel::accentColor()'s default arm.
[[nodiscard]] inline QColor forSlot(int slot)
{
    const Rgb c = (slot == 0) ? kPlayer
                : (slot == 1) ? kMonster
                : (slot == 3) ? kPets
                              : kDamage;   // slot 2 + anything unknown
    return QColor(c.r, c.g, c.b);
}

} // namespace mhw::panel_accent
