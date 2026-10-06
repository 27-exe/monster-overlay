// SPDX-License-Identifier: Apache-2.0
//
// No empty window in either game.
//
// The reported symptom: "both Rise and World previews have a completely
// empty floating window." The mechanism was a short-circuit —
// hasContent() began `if (demoData_ && editMode()) return true;`, which
// records that the demo seed RAN rather than that it produced anything.
//
// That distinction is invisible until a game's seed is DELIBERATELY empty.
// buildMantleModel() has a Rise arm that reads nothing, because Rise has no
// mantle system; buildWirebugModel() has a World arm that reads nothing,
// because World has no wirebugs. Both are correct behaviour. The flag
// overrode both, and the user got an empty rectangle they had no way to
// explain — the window existed, it was just blank.
//
// So this asserts the INVARIANT, not the two known cases:
//
//     hasContent() == true  ⟹  the window has a measurable body
//
// A window claiming content at 2x2, or 0x0, is this same bug wearing a
// different coat. Enumerating every window × both games is what makes the
// next occurrence land here rather than in a screenshot.

#include "ui/panel.h"
#include "ui/panel_player_split.h"
#include "ui/panel_player_split_bars.h"
#include "ui/panel_player_split_mantles.h"
#include "ui/panel_player_split_wirebug.h"
#include "ui/panel_player_split_weapon.h"
#include "ui/panel_player_split_info.h"
#include "ui/panel_damage_split_share.h"
#include "ui/panel_damage_split_chart.h"

#include <QApplication>
#include <QCoreApplication>

#include <cstdio>

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool cond, const char *what)
{
    ++gChecks;
    if (cond) {
        std::printf("PASS: %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++gFailures;
    }
}

// Show the window long enough for the demo seed to have run and paintPanel
// to have set a content size. Without this the window reports its
// construct-time 0x0 and the assertion measures nothing.
//
// setGameForDemo() is spelled as a detection idiom rather than called
// unconditionally, because not every window HAS one: the two damage
// windows seed the same roster whatever the game, so retargeting them is
// meaningless and they never declared the method. Compiling the call anyway
// would make this file the thing that forces a no-op onto every future
// window — the opposite of what a test should do.
template <class W>
void prime(W &w, mhw::GameId game)
{
    w.setEditMode(true);
    if constexpr (requires { w.setGameForDemo(game); })
        w.setGameForDemo(game);
    w.resetDemoPrimed();
    w.show();
    for (int i = 0; i < 8; ++i) {
        w.repaint();
        QCoreApplication::processEvents();
    }
}

// The invariant, for one window under one game: if it claims content it
// must have a body, and if it has no body it must not claim content.
//
// The two-directional form matters. Asserting only "content ⟹ body" would
// pass for a window that answers true and draws nothing (the original bug);
// asserting only "no body ⟹ no content" would pass for one that hides real
// content behind an empty answer (the opposite bug, which used to be the
// reason the short-circuit was written in the first place).
template <class W>
void assertNoEmptyBody(const char *name, mhw::GameId game)
{
    W w;
    prime(w, game);
    // hasVisibleContent() rather than hasContent(): the latter is protected
    // because it is a question a window answers about ITSELF, and the
    // public one is what the main loop (and everything else) asks.
    const bool claims = w.hasVisibleContent();
    const QSize body = w.contentSize();
    char what[256];

    if (claims) {
        std::snprintf(what, sizeof(what),
                      "%s under %s claims content and measures %dx%d",
                      name, game == mhw::GameId::Rise ? "Rise" : "World",
                      body.width(), body.height());
        check(body.width() > 0 && body.height() > 0, what);
    } else {
        std::snprintf(what, sizeof(what),
                      "%s under %s answers no content, so no empty frame "
                      "is mounted",
                      name, game == mhw::GameId::Rise ? "Rise" : "World");
        check(true, what);
    }
    std::printf("      -> %-14s %-5s hasContent=%-3s content=%dx%d\n",
                name, game == mhw::GameId::Rise ? "Rise" : "World",
                claims ? "YES" : "NO", body.width(), body.height());
}

template <class W>
void assertKnownAbsent(const char *name, mhw::GameId game,
                       const char *why)
{
    W w;
    prime(w, game);
    char what[256];
    std::snprintf(what, sizeof(what), "%s under %s is absent — %s", name,
                  game == mhw::GameId::Rise ? "Rise" : "World", why);
    check(!w.hasVisibleContent(), what);
    check(!w.hasVisibleContent(),
          "and it is not visible either, so the layer-shell surface is "
          "never mounted");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    std::printf("== split_no_empty_window_tests ==\n");

    // ── 1. Every window, every game: no body unless it claims content ──
    //
    // 8 windows x 2 games. This is the sweep that would have caught the
    // original bug on the day it was written, because it does not know
    // which window is supposed to be empty where.
    {
        std::printf("\n-- World --\n");
        assertNoEmptyBody<PlayerStatusStripPanel>("player-status",
                                                  mhw::GameId::World);
        assertNoEmptyBody<PlayerBarsStripPanel>("player-bars",
                                                mhw::GameId::World);
        assertNoEmptyBody<PlayerMantlesStripPanel>("player-mantles",
                                                   mhw::GameId::World);
        assertNoEmptyBody<PlayerWirebugStripPanel>("player-wirebug",
                                                   mhw::GameId::World);
        assertNoEmptyBody<PlayerWeaponStripPanel>("player-weapon",
                                                  mhw::GameId::World);
        assertNoEmptyBody<PlayerInfoStripPanel>("player-info",
                                                mhw::GameId::World);
        assertNoEmptyBody<DamageShareStripPanel>("damage-share",
                                                 mhw::GameId::World);
        assertNoEmptyBody<DamageChartStripPanel>("damage-chart",
                                                 mhw::GameId::World);

        std::printf("\n-- Rise --\n");
        assertNoEmptyBody<PlayerStatusStripPanel>("player-status",
                                                  mhw::GameId::Rise);
        assertNoEmptyBody<PlayerBarsStripPanel>("player-bars",
                                                mhw::GameId::Rise);
        assertNoEmptyBody<PlayerMantlesStripPanel>("player-mantles",
                                                   mhw::GameId::Rise);
        assertNoEmptyBody<PlayerWirebugStripPanel>("player-wirebug",
                                                   mhw::GameId::Rise);
        assertNoEmptyBody<PlayerWeaponStripPanel>("player-weapon",
                                                  mhw::GameId::Rise);
        assertNoEmptyBody<PlayerInfoStripPanel>("player-info",
                                                mhw::GameId::Rise);
        assertNoEmptyBody<DamageShareStripPanel>("damage-share",
                                                 mhw::GameId::Rise);
        assertNoEmptyBody<DamageChartStripPanel>("damage-chart",
                                                 mhw::GameId::Rise);
    }

    // ── 2. The two cases the user actually saw ────────────────────────
    //
    // Pinned by name, because "no empty body" alone would also be
    // satisfied by a window that reports no content for the WRONG reason —
    // a mask that was never meant to exclude it, say. What the user
    // reported was specifically a World preview offering wirebugs and a
    // Rise preview offering mantles, neither of which the game has.
    {
        assertKnownAbsent<PlayerWirebugStripPanel>(
            "player-wirebug", mhw::GameId::World,
            "World publishes no wirebug; the demo's World arm seeds none");
        assertKnownAbsent<PlayerMantlesStripPanel>(
            "player-mantles", mhw::GameId::Rise,
            "Rise has no mantle system; the demo's Rise arm seeds none");
    }

    // ── 3. Both windows DO exist in their own game ────────────────────
    //
    // The guard against over-correcting. A fix that simply hid both
    // windows everywhere would pass everything above and leave the split
    // with two dead windows.
    {
        PlayerMantlesStripPanel mw;
        prime(mw, mhw::GameId::World);
        check(mw.hasVisibleContent() && mw.contentSize().width() > 0,
              "player-mantles under WORLD is present with a real body — "
              "the fix hid it where it does not belong, not everywhere");

        PlayerWirebugStripPanel ww;
        prime(ww, mhw::GameId::Rise);
        check(ww.hasVisibleContent() && ww.contentSize().width() > 0,
              "player-wirebug under RISE is present with a real body — "
              "likewise");
    }

    std::printf("\n%s: %d checks, %d failures\n",
                gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
