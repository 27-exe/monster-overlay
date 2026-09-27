#pragma once

// Player-panel display metrics — the pure, Qt-only conversions the panel
// applies on its display path.
//
// These used to live in the anonymous namespace at the top of
// panel_player.cpp, behind a test-only conditional-compilation guard, so the
// Core-only reader test target could textually include the 1557-line Qt View
// .cpp just to reach two helpers. That guard silently excluded everything
// after it from the test target — code appended below the guard neither
// compiled nor failed, and the blind spot grew as the file grew. The helpers
// now live here instead: a plain inline header the test target can include
// directly, with no conditional-compilation window at all.
// `staminaDisplayValue()` and `wirebugSlotLabel()` are inline (not static)
// so every translation unit gets the same definition.

#include <QString>

#include "rise/mhr_reader.h"

// v0.8.4-r18 stamina-units-r2: the Rise HUD's raw Stamina / MaxStamina are in
// thirtieths of the bar's human-scale domain, so the panel divides by 30.
// Live user measurements (2026-09-17) falsified the earlier /10 pass of this
// fix with an exact 3x overshoot: 240 in-game stamina rendered as "720" and
// 100 rendered as "300" — both are the raw value (7200 / 3000) over 10, while
// raw / 30 gives back the value the game itself shows (7200/30 = 240,
// 3000/30 = 100). A raw probe in the same session read stamina = 3000 /
// maxStamina = 3000 / maxExtendableStamina = 4500, which at /30 is the
// observed 100 / 100 / 150 bar.
//
// This overturns the /10 conclusion recorded in the v0.8.4-r1/symptom4 README
// ("1 s of stamina = 10 raw units"), which that round then implemented. The
// raw magnitudes it sampled (90 / 930 / 1500 / 3000) are simply bar value x30:
// raw 930 = bar 31, raw 1500 = bar 50, raw 3000 = bar 100, raw 4500 = bar 150,
// raw 7200 = bar 240. Test vectors in tests/reader_tests.cpp pin these.
//
// HunterPie is consistent with the same x30 raw ratio: its numeric widget
// takes these raw fields unscaled (MHRPlayer.cs:771-776 ->
// PlayerHudView.xaml:284-302) and its PETALACE_STAMINA_MULTIPLIER = 30 is a
// petalace-point -> raw factor. Do NOT scale by any of its constants; divide
// raw by kRiseStaminaUnitScale only.
//
// World is NOT in this domain — its HUD stamina is already the bar value
// (player_reader.cpp reads hud+0x12C/0x130, ~150 max) — hence the GameId
// gate, which also keeps the two readers' snapshot semantics untouched.
// Health is never scaled, in either game (raw HP is already the bar value).
//
// This is a display-only conversion: PlayerSnapshot keeps raw values because
// the validity bounds (isRisePlayerValid: maxStamina <= 10000) and the
// diagnostic probes are raw-domain. Never write the result back into a
// snapshot. Kept in the shared helper block so the Core-only reader test
// target can pin the scaling without pulling in the Qt widgets panel.
constexpr float kRiseStaminaUnitScale = 30.0F;

inline float staminaDisplayValue(mhw::GameId game, float rawStamina)
{
    return game == mhw::GameId::Rise
        ? rawStamina / kRiseStaminaUnitScale
        : rawStamina;
}

// Reader snapshots retain their original Mono-array slot even when `None`
// entries are omitted from QVector. Never derive this label from vector index.
//
// i18n: the label pair is a parameter (defaults = the historical zh
// literals) instead of an inline tr(), because this helper lives in the
// shared block that the Core-only reader-test target compiles without any
// StringTable behind it. The live panel passes mh::tr("ui.wirebug_slot") /
// mh::tr("ui.wirebug_slot_n") in at the call site.
inline QString wirebugSlotLabel(int slot,
                                const QString &plain = QStringLiteral("翔虫"),
                                const QString &numbered = QStringLiteral("翔虫·%1"))
{
    if (slot == 0)
        return plain;
    if (slot > 0 && slot < mhw::kRiseWirebugSlotCap)
        return numbered.arg(slot + 1);
    // Corrupt slots have no trustworthy identity. Keep the generic label
    // rather than inventing a number or indexing any source-slot storage.
    return plain;
}
