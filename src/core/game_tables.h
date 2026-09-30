#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// game_tables.h — game-keyed lookup tables, owned by whoever registers them.
//
// WHY THIS EXISTS
// ---------------
// monster_types.h used to answer "what are this monster's crown thresholds?"
// with an inline function that picked a table by ternary:
//
//     const auto &thresholds =
//         game == GameId::Rise ? kRiseCrownThresholds : kCrownThresholds;
//
// That looks harmless and is not. It puts both games' data behind one
// `if`, inside a header that every layer includes. Three consequences, in
// increasing order of cost:
//
//   1. Adding a game meant editing a shared header. A third enum value could
//      not be handled without touching core, which is exactly the coupling
//      this refactor exists to remove.
//   2. The two branches are not even the same query. captureThresholdFor()
//      has World read a threshold table and Rise read a *not-capturable set*
//      and answer 0 — so "just add an arm" was never a mechanical change.
//   3. Both tables had to stay reachable from the same TU. Once the Rise
//      tables move into their own library (Stage S3), a ternary in a core
//      header would have to link both libraries, dragging World data and
//      Rise data back into every consumer. The architecture fight and the
//      linker were on the same side.
//
// So the tables are registered instead of referenced. A game owns its data
// and hands it over at startup; core asks by (game, id) and never names a
// specific game's table:
//
//     registerCrownThresholds(GameId::Rise, table);
//     registerCaptureRules(GameId::Rise, rules);
//     crownThresholds(GameId::Rise, 1);   // core, no game knowledge
//
// A game that is never started never registers, and its queries answer
// "no data" — which is honest. The old ternary answered a World threshold
// for an id it had never heard of, because the default arm was World.
//
// The cost is that registration must happen before the first query. See
// kRegistrationOrder note at the bottom; in the current wiring main.cpp
// constructs exactly one reader, and each reader's constructor registers
// its own tables, so the tables are live before the first poll().

#include "monster/monster_types.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <array>
#include <optional>

namespace mhw {

// How one game answers the two static-metadata questions. Default
// implementations answer "no data", which is what an unregistered game
// gets — deliberately not a World fallback, see the header comment.
//
// Named with a `Tables` suffix so they cannot collide with the accessor
// functions below; C++ would otherwise reject `crownThresholds(game)`
// against a type of the same name.
struct CrownTables {
    const QHash<int, std::array<float, 3>> *byMonster{nullptr};
};

struct CaptureTables {
    // Percentage HP at which the monster becomes capturable. Absent entry
    // means "the game has no static capture data for this monster".
    const QHash<int, int> *thresholdByMonster{nullptr};
    // Monsters the game explicitly marks non-capturable. For Rise this is
    // the only capture information MonsterData.xml carries, so a member
    // here answers 0 (capturable never) and a non-member answers "unknown"
    // — NOT "same as World". See the old captureThresholdFor() for the
    // semantics being preserved.
    const QSet<int> *notCapturable{nullptr};
};

// How a game colours one ailment: the status colour, and the trap icon when
// the ailment is a trap item. The two shipped games use UNRELATED id tables
// — Rise 4 is poison and World 2 is paralysis — so a colour keyed only by id
// would be wrong for one of them no matter which value it picked.
//
// The colour is a packed ARGB int, NOT a QColor. This header lives in
// monster-core, which links QtCore only; QColor is in QtGui, and naming it
// here would drag QtGui down into the kernel. The View converts with
// QColor::fromRgba at the point of painting, which is where a GUI type
// belongs.
struct AilmentStyle {
    // Packed 0xAARRGGBB. 0x9E9E9E alpha-ff is the neutral grey the old
    // switch used as its `default:` in both arms.
    quint32 statusColorRgba{0xFF9E9E9EU};
    QString trapIconPath; // empty for a non-trap ailment
};

// Registered per game. Absent entry = this game has no data, and
// ailmentStyle() answers the neutral grey rather than borrowing another
// game's palette — the old if/else defaulted to World's table, so an id no
// game knew about was painted with World colours.
AilmentStyle ailmentStyle(GameId game, int ailmentId);

// The two tables live in the data translation units that own them:
// registerWorldAilmentStyles() in src/monster/data/part_schemas_data.cpp,
// registerRiseAilmentStyles() in src/rise/part_schemas.cpp. The UI asks
// by (game, id) and never sees a switch.
void registerAilmentStyles(GameId game,
                           const QHash<int, AilmentStyle> &styles);

// Registered tables per game. Absent entry = no data for that game.
CrownTables crownTablesFor(GameId game);
CaptureTables captureTablesFor(GameId game);

// The two queries, now game-agnostic. Signatures are unchanged from the
// inline functions they replace, so call sites (panel_monster.cpp:1125,
// rise_monster_metadata_tests.cpp:32, rise_monster_name_tests.cpp:289)
// need no edit.
//
// nullptr / nullopt mean "this game has no static data for that monster".
// They never mean "fall back to another game".
const std::array<float, 3> *crownThresholdsFor(GameId game, int monsterId);
std::optional<int> captureThresholdFor(GameId game, int monsterId);

// Registration. Called from each game's reader constructor, before its
// first poll(). Calling twice for the same game replaces the previous
// registration, so a restarted reader does not accumulate state.
//
// `thresholdByMonster` may be null: Rise publishes no Capture=N percentages
// at all, and passing an empty table instead would make "no data" and "no
// monsters" indistinguishable at the call site. Likewise a game whose XML
// expresses non-capturability inside the threshold table (World, where
// Elder Dragons carry Capture=0) passes kNoNotCapturable.
inline const QSet<int> &kNoNotCapturable()
{
    static const QSet<int> empty;
    return empty;
}

void registerCrownThresholds(GameId game, const QHash<int, std::array<float, 3>> &table);
void registerCaptureRules(GameId game, const QHash<int, int> *thresholdByMonster,
                          const QSet<int> &notCapturable);

} // namespace mhw
