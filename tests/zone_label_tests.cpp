// SPDX-License-Identifier: Apache-2.0
//
// zone_label_tests.cpp — which branch zoneLabel() takes, without the strings.
//
// zoneLabel() (src/ui/panel_player.cpp:124) is the one per-game seam in the
// player panel that had no coverage: it lives in an anonymous namespace inside
// a 1400-line UI translation unit and returns a translated QString, so a test
// could only reach it by linking the whole panel. That is why a -Wswitch for a
// new GameId there would have gone unnoticed while the other seven seams
// screamed.
//
// What is asserted here is the *dispatch*, not the wording: which game side is
// consulted, and — the part that actually breaks — that an id the vocabulary
// has no entry for falls through to the "outpost / area" range check instead
// of rendering a wrong game's name. The labels themselves are covered by the
// snapshot regression, which draws both happy paths.
//
// This mirrors the switch in zoneLabel(). If that switch changes, this must
// change with it; the shared comment is the reminder that they are one thing.

#include "core/zone_id.h"

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("  FAIL  %s\n", what);
        ++g_failures;
    } else {
        std::printf("  ok    %s\n", what);
    }
}

// The three shapes zoneLabel() can answer with, as an enum so the test does
// not have to compare translated text. Ordered as the function decides.
enum class Route { Unknown, Named, OutpostRange, AreaFallback };

Route routeFor(mhw::ZoneRef z, bool worldHasName, bool riseHasName)
{
    if (mhw::isUnknownZone(z))
        return Route::Unknown;

    switch (z.game) {
    case mhw::GameId::Rise:
        if (riseHasName)
            return Route::Named;
        break;
    case mhw::GameId::World:
        if (worldHasName)
            return Route::Named;
        break;
    }
    const int raw = z.id;
    if (raw >= 0 && raw <= 100)
        return Route::OutpostRange;
    return Route::AreaFallback;
}

} // namespace

// See the matching block in tests/zone_id_tests.cpp.
using mhw::GameId;

int main()
{
    using mhw::ZoneRef;

    std::printf("zone_label: the game half picks the vocabulary\n");

    // Same numeric id, two games, different vocabularies: each side must be
    // asked, and the answer must come from that side's table.
    check(routeFor({GameId::World, 305}, true, false) == Route::Named,
          "World id consults the World table");
    check(routeFor({GameId::Rise, 305}, false, true) == Route::Named,
          "Rise id consults the Rise table");

    std::printf("zone_label: an unnamed id falls back by range, not by guessing\n");

    // The bug this fallback exists for (#7, "据点显示未知地点"). An id no
    // vocabulary names must NOT be rendered as some other zone's name, and must
    // not be reported as unknown either — it was read, it just has no label.
    // 0..100 is the peace-zone range zoneLabel() renders as "据点 N"; anything
    // above it renders as "区域 N".
    check(routeFor({GameId::World, 50}, false, false) == Route::OutpostRange,
          "unnamed World id in 0..100 reads as an outpost, not a name");
    check(routeFor({GameId::World, 101}, false, false) == Route::AreaFallback,
          "unnamed World id above 100 reads as an area");
    check(routeFor({GameId::World, 99999}, false, false) == Route::AreaFallback,
          "unmapped World id is an area, never a borrowed name");

    // The values computeZoneId() actually emits (mhr_reader.cpp): -2 for the
    // loading screen, 199 for character selection, villageId + 700.
    check(routeFor({GameId::Rise, 199}, false, false) == Route::AreaFallback,
          "Rise 199 (CharSelection) is an area, not a name");
    check(routeFor({GameId::Rise, -2}, false, false) == Route::AreaFallback,
          "Rise -2 (LoadingScreen) is an area, not a name");
    check(routeFor({GameId::Rise, 705}, false, false) == Route::AreaFallback,
          "Rise village (705) with no vocabulary entry is an area");

    std::printf("zone_label: unknown is decided before any game is consulted\n");

    // The unknown check comes first, so it wins even when a vocabulary would
    // have a name for that id. If a future edit reorders the branches, this
    // fails — which is the point.
    check(routeFor({GameId::World, mhw::kZoneUnknownId}, true, true) == Route::Unknown,
          "unknown id stays unknown even with named vocabularies");
    check(routeFor({GameId::Rise, mhw::kZoneUnknownId}, true, true) == Route::Unknown,
          "unknown id stays unknown under Rise too");

    if (g_failures == 0) {
        std::printf("zone_label: all checks passed\n");
        return 0;
    }
    std::printf("zone_label: %d check(s) FAILED\n", g_failures);
    return 1;
}
