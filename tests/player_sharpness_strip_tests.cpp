// SPDX-License-Identifier: Apache-2.0
//
// v0.12 — the sharpness sub-window's two testable surfaces.
//
// The window's paint path is a delegation, not a reimplementation: it
// calls drawPlayerSharpnessBar() so the gauge drawn inside the window and
// the gauge drawn inside PlayerPanel are literally the same code, which is
// the reason the migration cost almost nothing. That reuse also means
// there is no per-window model function to test, and the two things worth
// pinning are the width contract and the mask gate.
//
//   * playerSharpnessBarTotalWidth() is the window's width authority. The
//     window is right-anchored — it has no name column to dodge, so it
//     reports a width and the draw call receives the right edge — which
//     makes this the one number that decides whether the gauge fits.
//   * The Sharpness bit's migration is what keeps the gauge visible for
//     anyone who started on a build that predates the bit. It lives in
//     panel_sections.h and is exercised here because the symptom it
//     prevents is a window that mounts and draws nothing.

#include "ui/panel_player_split_sharpness.h"

#include "ui/panel_sections.h"
#include "ui/panel_player_sharpness.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

int failures = 0;
int checks   = 0;

void check(bool cond, const std::string &what)
{
    ++checks;
    if (cond) {
        std::printf("PASS: %s\n", what.c_str());
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

} // namespace

int main()
{
    // ── 1. THE WIDTH CONTRACT ───────────────────────────────────────
    // A window sized from a number that changed under it would either clip
    // the seven-segment badge or leave a gap to its right, and the second
    // is the one that reads as a bug to a user.
    {
        const int w = playerSharpnessBarTotalWidth();
        check(w > 0,
              "width: the bar reports a positive total width, so the "
              "right-anchored window has something to anchor to");
        check(w == playerSharpnessBarTotalWidth(),
              "width: the total width is a constant of the design, not a "
              "measurement — two calls cannot disagree");
    }

    // ── 2. THE MIGRATION (a pre-bit mask must still show the gauge) ──
        //
        // The failure this pins is the one a user actually hit: v0.12 added
        // Sharpness = 1u << 8, and every mask persisted before that had bit 8
        // clear because the bit did not exist. Read literally, the gauge is
        // off — while the Weapon bit in the same mask is set, so the identity
        // row kept drawing. "Only the icon and MR, no sharpness bar" is
        // exactly what that combination produces.
        //
        // ── THE AMBIGUITY THIS MIGRATION CANNOT RESOLVE ──────────────────
        //   v0.11 all-on, bit 8 absent        ->  mask 0x0ff
        //   v0.12 all-on, gauge turned OFF    ->  mask 0x0ff
        //
        // Those are DIFFERENT intents that produce the SAME stored value, so
        // no function of the value alone can tell them apart. The consequence
        // is concrete: a user who opens the v0.12 console and turns the
        // sharpness row off gets the gauge back on the next start. Migrating
        // on every read makes "off" unreachable for one upgrade cycle.
        //
        // This is a product decision, not a code one, so it is recorded here
        // rather than decided in the implementation. The options are (a) accept
        // a one-time auto-enable, (b) write a schema version into the conf
        // file so the migration runs exactly once, (c) no migration at all and
        // let the user open the section themselves.
    {
        namespace PS = mhw::PlayerSection;
        constexpr uint32_t kW = PS::Weapon;
        constexpr uint32_t kS = PS::Sharpness;

        // A pre-v0.12 all-on mask: bit 8 never written, Weapon on.
        constexpr uint32_t kOldAll = 0xFFu;
        check((kOldAll & kS) == 0,
              "migration: the premise holds — a pre-bit mask really does "
              "have Sharpness clear, which is why the bar disappeared");
        check((kOldAll & kW) != 0,
              "migration: and the same mask still has Weapon set, which is "
              "why the identity row kept drawing beside nothing");
        check(PS::migrateOldSharpnessBit(kOldAll) == (kOldAll | kS),
              "migration: an all-on pre-bit mask gains Sharpness, so the "
              "gauge comes back for everyone who never turned it off");

        // Weapon off in the old schema meant "no weapon row AND no
        // gauge" — that was the only way to switch the gauge off, because
        // they shared one bit. It must stay off.
        constexpr uint32_t kOldNoWeapon = 0xFFu & ~kW;
        check(PS::migrateOldSharpnessBit(kOldNoWeapon)
                  == kOldNoWeapon,
              "migration: a pre-bit mask with Weapon off is left alone — "
              "turning the weapon row off was how the gauge was turned off");

        // KNOWN IMPOSSIBLE, not yet decided — see the two below.
        //
        // A mask written by v0.12 with the gauge turned OFF is numerically
        // identical to a mask written by v0.11 with the bit absent:
        //
        //     v0.11 all-on, no bit 8   ->  0x0ff
        //     v0.12 all-on, gauge off   ->  0x0ff
        //
        // so no function of the stored value can tell them apart, and the
        // migration necessarily re-enables a gauge the user turned off.
        // Whether that is acceptable (one-time auto-enable) or must be
        // fixed (a schema version in the conf file) is a product decision,
        // not a code one — so it is recorded here rather than asserted.
        //
        // constexpr uint32_t kExplicitOff = (kOldAll | kS) & ~kS;
        // check(PS::migrateOldSharpnessBit(kExplicitOff) == kExplicitOff,
        //       "migration: an explicit Sharpness-off survives");

        // All off is a decision, not an absence.
        check(PS::migrateOldSharpnessBit(0u) == 0u,
              "migration: an all-off mask stays all off");

        // Idempotent, because the value is read on every startup and a
        // second application must not be a different answer.
        const uint32_t once = PS::migrateOldSharpnessBit(kOldAll);
        check(PS::migrateOldSharpnessBit(once) == once,
              "migration: applying it twice is a fixed point");

        // Sweep the whole old schema: the only permitted change is turning
        // Sharpness on, and only when Weapon is already on.
        // The two properties that ARE decidable from the stored value.
        //
        // "No bit is cleared" must be tested as (out & in) == in — i.e. the
        // output still carries everything the input carried. The obvious-
        // looking `out & ~in` is the WRONG test for this: it measures what
        // was ADDED, and the whole point of the migration is to add exactly
        // one bit, so that is non-zero by construction and would fail
        // forever while the code was correct.
        bool noBitCleared = true;
        bool onlySharpnessAdded = true;
        bool onlyWhenWeaponOn = true;
        for (uint32_t m = 0; m < 0x100u; ++m) {
            const uint32_t out = PS::migrateOldSharpnessBit(m);
            if ((out & m) != m)
                noBitCleared = false;           // dropped a bit the input had
            if ((out & ~m) != kS && out != m)
                onlySharpnessAdded = false;     // added something besides Sharpness
            if ((out & kS) && !(m & kW))
                onlyWhenWeaponOn = false;       // raised it with Weapon off
        }
        check(noBitCleared,
              "migration: across all 256 pre-bit values not one bit is ever "
              "cleared — the migration can only add Sharpness, never take a "
              "section away");
        check(onlySharpnessAdded,
              "migration: and what it adds is Sharpness alone, never a "
              "second section");
        check(onlyWhenWeaponOn,
              "migration: and it is only raised when Weapon was already "
              "on, which is the pre-bit rule for 'the gauge was on'");
    }

    // ── 3. THE SPLIT TABLE'S CLAIM ─────────────────────────────────
    // The bar left PlayerPanel, so the panel's mask in Split loses it —
    // and loses nothing else, which the table's own static_asserts cover
    // but which is worth one runtime statement here.
    {
        namespace PS = mhw::PlayerSection;
        const uint32_t split =
            PS::playerPanelMaskFor(PS::PlayerLayoutMode::Split, PS::kAll);
        check((split & PS::Sharpness) == 0,
              "split: PlayerPanel no longer owns Sharpness in Split — the "
              "bar belongs to the window");
        check(split == 0u,
              "split: with every section in a window of its own, "
              "PlayerPanel's derived mask is empty, which is what lets "
              "main.cpp keep the panel from mounting an empty shell");
        check(PS::playerPanelMaskFor(PS::PlayerLayoutMode::Single, PS::kAll)
                  == PS::kAll,
              "split: Single still resolves the mask verbatim, so the "
              "single-mode pixel baselines do not move");
    }

    std::printf("\n%d checks, %d failure(s)\n%s\n", checks, failures,
                failures == 0 ? "ALL PLAYER-SHARPNESS-STRIP TESTS PASSED"
                              : "SOME PLAYER-SHARPNESS-STRIP TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
