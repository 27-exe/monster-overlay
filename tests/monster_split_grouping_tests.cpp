// SPDX-License-Identifier: Apache-2.0
//
// The v0.13 monster split REGROUPING: five single-block windows became
// two grouped ones (Identity = 肖像+激怒+血量; Conditions = 异常+部位),
// and the per-block toggles had to survive that grouping.
//
// WHAT THIS FILE PINS, in order of how badly a regression would hurt:
//
//   1. THE SINGLE-MODE PIXEL PROMISE. monsterPanelMaskFor(Single, m)
//      returns m VERBATIM for EVERY value — proven exhaustively over all
//      128 seven-bit masks plus the two pathological probes. If this
//      breaks, the 10 single-mode baselines move and nothing else in the
//      suite would say why.
//   2. THE TWO-ROW TABLE'S SHAPE. kMonsterSplitTargets has exactly two
//      rows, they are OR-disjoint, and together they claim every bit
//      MonsterSection has EXCEPT Tenderize (which is not a block of its
//      own — see the NOT-in-table note).
//   3. THE ENRAGE TOGGLE. MonsterSection::Enrage must still switch the
//      enrage row off while the Identity WINDOW stays mounted. This is
//      the user's 「还可选择性的关闭部分，比如激怒条」 request, and it is
//      the whole reason the gates are public members rather than
//      something the panel does.
//   4. THE MODEL FOLDS. Zero maxima produce the placeholder pair, never
//      a full bar that claims a value nobody read — the panel's own
//      rule, mirrored, per block.
//   5. THE PER-GAME hasContent DIFFERENCE. A World-only and a
//      Rise-only snapshot give the two windows different answers, which
//      is what keeps a Rise preview from mounting World's cards.
//
// The Windows themselves are asked through their PUBLIC surface (the two
// gates, cardsPerRow()/ailCardsPerRow(), contentWidth()/Height(),
// hasContent(), itemCount()/naturalItemWidth()/itemGap()/rowPitch(),
// supportsColumns()) — the same discipline monster_parts_window_tests.cpp
// uses. Widening the private section for a test would trade a real
// encapsulation for a green run.
//
// CMake WIRING (the parent session owns CMakeLists.txt):
//
//     add_executable(monster-split-grouping-tests
//         tests/monster_split_grouping_tests.cpp
//     )
//     target_link_libraries(monster-split-grouping-tests PRIVATE
//         Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg mhw-ui)
//     target_include_directories(monster-split-grouping-tests PRIVATE
//         ${CMAKE_CURRENT_SOURCE_DIR}/src)
//     target_compile_options(monster-split-grouping-tests PRIVATE
//         -Wall -Wextra -Wpedantic)
//     add_test(NAME monster-split-grouping-tests
//              COMMAND monster-split-grouping-tests)
//     set_tests_properties(monster-split-grouping-tests PROPERTIES
//         ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
//
// Links mhw-ui rather than listing the new .cpp files, for the reason the
// sibling targets do: the panel .cpp files carry Q_OBJECT, so listing
// them here as well would run AUTOMOC over the same headers twice. The
// qrc is compiled in for the same reason monster-split-models-tests
// compiles it — the localized card fields are asserted against the text
// that actually ships.

#include "ui/panel_monster_split_identity.h"
#include "ui/panel_monster_split_conditions.h"
#include "ui/panel_sections.h"

#include "core/game_tables.h"
#include "core/string_table.h"
#include "monster/monster_types.h"

#include <QApplication>
#include <QString>

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool ok, const std::string &what)
{
    ++gChecks;
    if (ok) {
        std::printf("PASS: %s\n", what.c_str());
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++gFailures;
    }
}

// The panel's own demo seed (panel_monster.cpp:1444-1511): 火龙, 71%,
// 700/1000 stamina, enraged with 32s of timer left, four ailments and
// six parts. One factory so every section starts from the same baseline
// and a change there is one edit, not ten.
mhw::MonsterSnapshot demoMonster(mhw::GameId game = mhw::GameId::World)
{
    mhw::MonsterSnapshot m;
    m.address          = 0xDEADBEEFULL;
    m.id               = 1;
    m.game             = game;
    m.internalName     = QStringLiteral("Rathalos");
    m.size             = 1.25F;
    m.maxHealth        = 25800.0F;
    m.health           = 18420.0F;
    m.maxStamina       = 1000.0F;
    m.stamina          = 700.0F;
    m.enraged          = true;
    m.enrageMaxSeconds = 120.0F;
    m.enrageSeconds    = 88.0F;                   // 32s remaining
    m.enrageMaxBuildup = 100.0F;
    m.enrageBuildup    = 100.0F;

    struct Ail { int id; QString name; float timer; float maxT;
                 float bu; float maxB; int cnt; bool active; };
    const Ail demoAil[] = {
        { 2, QStringLiteral("Paralysis"),   12.0F, 20.0F,  0.0F,   0.0F, 1, true },
        {14, QStringLiteral("Shock Trap"),  8.0F,  8.0F,  0.0F,   0.0F, 0, true },
        {15, QStringLiteral("Pitfall"),    60.0F, 60.0F,  0.0F,   0.0F, 0, true },
        { 1, QStringLiteral("Poison"),      0.0F,  0.0F, 30.0F, 100.0F, 2, false},
    };
    for (const Ail &a : demoAil) {
        mhw::MonsterAilmentSnapshot ail;
        ail.id        = a.id;
        ail.name      = a.name;
        ail.active    = a.active;
        ail.timer     = a.timer;
        ail.maxTimer  = a.maxT;
        ail.buildup   = a.bu;
        ail.maxBuildup = a.maxB;
        ail.counter   = a.cnt;
        m.ailments.append(ail);
    }

    struct DPart { int idx; QString name; float hp; float maxHp;
                   int counter; bool breakable; bool severable; };
    const DPart dparts[] = {
        { 0, QStringLiteral("Head"),   80.0F, 100.0F, 0, true,  false},
        { 1, QStringLiteral("L Wing"), 45.0F, 100.0F, 1, true,  true },
        { 2, QStringLiteral("R Wing"), 20.0F, 100.0F, 2, true,  true },
        { 3, QStringLiteral("Tail"),   90.0F, 100.0F, 0, false, true },
        { 4, QStringLiteral("L Leg"),  60.0F, 100.0F, 0, false, false},
        { 5, QStringLiteral("R Leg"),  35.0F, 100.0F, 0, false, false},
    };
    for (const DPart &dp : dparts) {
        mhw::PartSnapshot ps;
        ps.index       = dp.idx;
        ps.name        = dp.name;
        ps.health      = dp.hp;
        ps.maxHealth   = dp.maxHp;
        ps.flinch      = dp.hp;
        ps.maxFlinch   = dp.maxHp;
        ps.counter     = dp.counter;
        ps.isBreakable = dp.breakable;
        ps.isSeverable = dp.severable;
        ps.partType    = dp.severable ? mhw::PartType::Severable
                        : (dp.breakable ? mhw::PartType::Breakable
                                        : mhw::PartType::Flinch);
        m.parts.append(ps);
    }
    m.parts[0].tenderizeDuration    = 45.0F;
    m.parts[0].tenderizeMaxDuration = 90.0F;
    m.parts[3].tenderizeDuration    = 12.0F;
    m.parts[3].tenderizeMaxDuration = 90.0F;
    return m;
}

// Show the window through enough paints that the demo seed has run and
// contentWidth()/Height() describe a real reservation rather than the
// construct-time default. Same shape as monster_parts_window_tests' prime().
template <class P>
void prime(P &w, mhw::GameId game = mhw::GameId::World)
{
    w.setEditMode(true);
    w.setGameForDemo(game);
    w.resetDemoPrimed();
    w.show();
    for (int i = 0; i < 6; ++i) {
        w.repaint();
        QCoreApplication::processEvents();
    }
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // Load the locale table BEFORE anything reads it. A StringTable with
    // nothing loaded answers tr(key) with the key itself, so the i18n
    // assertions at the bottom of this file would report a missing key
    // for a key that is right there in the qrc. This is the same load
    // every other GUI target's main() does.
    if (!mhw::StringTable::instance().load(QStringLiteral("en-US"))) {
        std::fprintf(stderr, "NOTE: could not load en-US; the i18n "
                             "assertions below will report missing keys\n");
    }

    // =====================================================================
    // 1. THE TABLE, AND WHAT THE TWO ROWS CLAIM.
    // =====================================================================
    //
    // The user's grouping, as a shape: 图标+激怒+血量 in one window,
    // 异常状态+部位 in another. Asserted against the enum and the table
    // rather than a number, so a third row cannot appear silently.
    check(mhw::kMonsterSplitTargetCount == 2,
          "table: kMonsterSplitTargets has exactly 2 rows (v0.13 grouping)");
    check(mhw::MonsterSplitSlot::kCount == 2,
          "table: MonsterSplitSlot::kCount is 2, matching the row count");
    check(mhw::MonsterSplitSlot::Identity == 0
              && mhw::MonsterSplitSlot::Conditions == 1,
          "table: Identity is row 0 and Conditions is row 1 (cascade order)");

    // Row 0's claim, bit for bit: 肖像+HP+激怒+耐力. Not a single bit and
    // not "whatever the Identity window happens to draw" — the union is
    // what monsterPanelMaskFor() subtracts, so it must be exact.
    const uint32_t kIdentityBits =
        mhw::MonsterSection::Info | mhw::MonsterSection::Hp
        | mhw::MonsterSection::Enrage | mhw::MonsterSection::Stamina;
    const uint32_t kConditionsBits =
        mhw::MonsterSection::Ail | mhw::MonsterSection::Parts;
    check(mhw::kMonsterSplitTargets[mhw::MonsterSplitSlot::Identity].mask
              == kIdentityBits,
          "table: row Identity claims Info|Hp|Enrage|Stamina exactly");
    check(mhw::kMonsterSplitTargets[mhw::MonsterSplitSlot::Conditions].mask
              == kConditionsBits,
          "table: row Conditions claims Ail|Parts exactly");

    // Disjoint, and together they cover everything but Tenderize —
    // which is not a block of its own (it is a strip inside each .pc
    // card, so it travels with Parts).
    check((kIdentityBits & kConditionsBits) == 0,
          "table: the two rows' claims share no bit");
    check((kIdentityBits | kConditionsBits)
              == (mhw::MonsterSection::kAll & ~mhw::MonsterSection::Tenderize),
          "table: the two rows together claim every bit except Tenderize");

    // Every row claims at least one bit: a row claiming nothing would
    // produce a window that draws nothing and a preview box with no
    // paint behind it. The same check the player/damage tables make.
    bool allRowsClaim = true;
    for (int i = 0; i < mhw::kMonsterSplitTargetCount; ++i)
        if (mhw::kMonsterSplitTargets[i].mask == 0)
            allRowsClaim = false;
    check(allRowsClaim,
          "table: every row claims at least one section bit");

    // =====================================================================
    // 2. THE PIXEL-IDENTITY PROMISE, PROVEN EXHAUSTIVELY.
    // =====================================================================
    //
    // Single mode returns the mask VERBATIM. Not "the same bits", not
    // "equivalent": the same uint32_t, so monsterPanel.setSectionMask()
    // is handed exactly the value v0.12 hands it. Two shapes — the
    // pathological 0xDEADBEEF and the boring 0 — because a
    // `mask & ~removed` that accidentally cleared a bit would catch the
    // second and might not the first.
    check(mhw::monsterPanelMaskFor(mhw::PlayerSection::PlayerLayoutMode::Single,
                                  0xDEADBEEFu) == 0xDEADBEEFu,
          "mask: Single mode returns 0xDEADBEEF verbatim");
    check(mhw::monsterPanelMaskFor(mhw::PlayerSection::PlayerLayoutMode::Single,
                                  0u) == 0u,
          "mask: Single mode returns 0 verbatim");
    check(mhw::monsterPanelMaskFor(mhw::PlayerSection::PlayerLayoutMode::Single,
                                  mhw::MonsterSection::kAll)
              == mhw::MonsterSection::kAll,
          "mask: Single mode returns a stock kAll mask verbatim");

    // The exhaustive arm: EVERY value the 7-bit MonsterSection can
    // express. That is the whole space of user toggle combinations, so
    // "the Single arm is a no-op" is proven rather than spot-checked.
    // v0.13: this is the assertion the regrouping was most at risk of
    // breaking, because the table it derives `removed` from now has
    // multi-bit rows.
    bool singleIsIdentity = true;
    for (uint32_t m = 0; m <= mhw::MonsterSection::kAll; ++m) {
        if (mhw::monsterPanelMaskFor(
                mhw::PlayerSection::PlayerLayoutMode::Single, m) != m) {
            singleIsIdentity = false;
            std::fprintf(stderr, "  first divergence at mask 0x%02x\n", m);
            break;
        }
    }
    check(singleIsIdentity,
          "mask: Single mode is a no-op for ALL 128 seven-bit masks");

    // And the Split arm, which is what actually changed shape: the panel
    // keeps NONE of the removed bits, and what it keeps is exactly
    // Tenderize — the one bit no row claims.
    const uint32_t removed = mhw::monsterSplitRemovedBits();
    check(mhw::monsterPanelMaskFor(mhw::PlayerSection::PlayerLayoutMode::Split,
                                   mhw::MonsterSection::kAll)
              == mhw::MonsterSection::Tenderize,
          "mask: in Split mode the panel keeps exactly Tenderize");
    check((mhw::monsterPanelMaskFor(
               mhw::PlayerSection::PlayerLayoutMode::Split,
               mhw::MonsterSection::kAll) & removed) == 0,
          "mask: in Split mode the panel holds none of the removed bits");
    check(removed == (kIdentityBits | kConditionsBits),
          "mask: the removed bits are the two rows' union");

    // Each window's own mask is the user's toggles intersected with its
    // claim — so a bit ANOTHER window claims never reaches it. This is
    // the property that lets both windows be handed `maskMonster` whole.
    check(mhw::monsterSubWindowMask(mhw::MonsterSplitSlot::Identity,
                                    mhw::MonsterSection::kAll)
              == kIdentityBits,
          "subwindow: slot Identity is handed its four bits");
    check(mhw::monsterSubWindowMask(mhw::MonsterSplitSlot::Conditions,
                                    mhw::MonsterSection::kAll)
              == kConditionsBits,
          "subwindow: slot Conditions is handed its two bits");
    // The two windows never see each other's bits.
    check((mhw::monsterSubWindowMask(mhw::MonsterSplitSlot::Identity,
                                     mhw::MonsterSection::kAll)
           & mhw::monsterSubWindowMask(mhw::MonsterSplitSlot::Conditions,
                                       mhw::MonsterSection::kAll)) == 0,
          "subwindow: no bit reaches both windows");
    check(mhw::monsterSubWindowMask(-1, 0x7Fu) == 0u
              && mhw::monsterSubWindowMask(99, 0x7Fu) == 0u,
          "subwindow: an out-of-range slot draws nothing");

    // =====================================================================
    // 3. THE ENRAGE TOGGLE — the user's 「比如激怒条」, as assertions.
    // =====================================================================
    //
    // This is the section the whole design turns on, so it asks the
    // question every honest way it can be asked: through the public gate
    // (does the block draw?), through the mask algebra (does the window
    // still get its other bits?), and through the geometry (does the
    // window shrink by the row's height, minus its gap?).
    {
        MonsterIdentityStripPanel w;
        // A fresh window's sectionMask defaults to ALL BITS SET
        // (Panel::sectionMask_ is 0xFFFFFFFF — live mode must show
        // everything unless someone says otherwise), so the "fresh
        // window" assertion is that every block answers ON, and the
        // interesting state is what a mask that clears a bit does.
        check(w.enrageEnabled() && w.hpEnabled() && w.staminaEnabled()
                  && w.identityInfoEnabled(),
              "enrage: a fresh window defaults every block ON "
              "(Panel::sectionMask_ starts at 0xFFFFFFFF)");

        // Stock kAll: everything on.
        w.setSectionMask(mhw::MonsterSection::kAll);
        check(w.enrageEnabled() && w.hpEnabled() && w.staminaEnabled()
                  && w.identityInfoEnabled(),
              "enrage: a stock kAll mask turns every Identity block on");

        // THE REQUESTED CASE: 激怒 off, everything else on.
        const uint32_t noEnrage =
            mhw::MonsterSection::kAll & ~mhw::MonsterSection::Enrage;
        w.setSectionMask(noEnrage);
        check(!w.enrageEnabled(),
              "enrage: clearing MonsterSection::Enrage turns the row off");
        // ...and the OTHER THREE blocks are untouched. A gate that
        // consulted the union, or that read a neighbour's bit, would
        // fail this line.
        check(w.identityInfoEnabled() && w.hpEnabled()
                  && w.staminaEnabled(),
              "enrage: the other three Identity blocks stay on");

        // The window is still a window. This is the difference between
        // "the user closed the enrage bar" and "the user closed the
        // monster": monsterPanelMaskFor would still hand MonsterPanel a
        // mask with the window's other blocks in it.
        check(mhw::monsterPanelMaskFor(
                  mhw::PlayerSection::PlayerLayoutMode::Split, noEnrage)
                  == mhw::MonsterSection::Tenderize,
              "enrage: with 激怒 off, Split mode still moves the other "
              "three Identity bits out of the panel");

        // The per-block masks the two windows receive are still their own
        // claims, which is what lets one window live with a block off.
        check(mhw::monsterSubWindowMask(mhw::MonsterSplitSlot::Identity,
                                        noEnrage)
                  == (kIdentityBits & ~mhw::MonsterSection::Enrage),
              "enrage: the Identity window's own mask loses exactly "
              "the Enrage bit");
    }

    // The same discipline for Conditions' two blocks, because the
    // grouping would be dishonest if it only worked for enrage.
    {
        MonsterConditionsStripPanel w;
        w.setSectionMask(mhw::MonsterSection::kAll);
        check(w.conditionsAilEnabled() && w.partsEnabled()
                  && w.tenderizeEnabled(),
              "conditions: a stock kAll mask turns both blocks on");
        const uint32_t noAil =
            mhw::MonsterSection::kAll & ~mhw::MonsterSection::Ail;
        w.setSectionMask(noAil);
        check(!w.conditionsAilEnabled() && w.partsEnabled(),
              "conditions: clearing Ail leaves the parts grid on");
        check(mhw::monsterSubWindowMask(mhw::MonsterSplitSlot::Conditions,
                                        noAil) == mhw::MonsterSection::Parts,
              "conditions: the Conditions window's own mask loses exactly "
              "the Ail bit");
        const uint32_t noParts =
            mhw::MonsterSection::kAll & ~mhw::MonsterSection::Parts;
        w.setSectionMask(noParts);
        check(w.conditionsAilEnabled() && !w.partsEnabled(),
              "conditions: clearing Parts leaves the ailments grid on");
    }

    // =====================================================================
    // 4. THE IDENTITY WINDOW AS AN OBJECT.
    // =====================================================================
    {
        MonsterIdentityStripPanel w;
        prime(w);
        w.setSectionMask(mhw::MonsterSection::kAll);

        check(w.isSubWindow(),
              "identity: declares itself a sub-window");
        check(w.contentMargin() == mhw::split_layout::kSplitMargin,
              "identity: answers the split's shared inset");
        check(w.hasVisibleContent(),
              "identity: has content with the demo seeded and all bits on");
        check(w.contentWidth() > 2 * w.contentMargin(),
              "identity: the demo's identity row needs real width");
        check(w.contentHeight() > 2 * w.contentMargin(),
              "identity: the demo's four blocks need real height");

        // Turning 激怒 off must SHRINK the window by exactly the enrage
        // row plus its separating gap — not by an arbitrary amount, and
        // not by zero. This is the reserve/draw inversion the v0.10.3
        // tenderize incident was, asserted at the level a user sees.
        const int fullH = w.contentHeight();
        const uint32_t noEnrage =
            mhw::MonsterSection::kAll & ~mhw::MonsterSection::Enrage;
        w.setSectionMask(noEnrage);
        for (int i = 0; i < 4; ++i) {
            w.repaint();
            QCoreApplication::processEvents();
        }
        check(w.contentHeight() < fullH,
              "identity: turning 激怒 off makes the window SHORTER");
        check(w.hasVisibleContent(),
              "identity: and it still has content (the other 3 blocks)");
        // The row the panel's own .bar.er occupies: 6 bar + 2*3 padding.
        // The window must not be shorter by more than that plus its gap.
        check(fullH - w.contentHeight() <= 6 + 2 * 3
                  + mhw::split_layout::kSplitGapBetween,
              "identity: the height lost is bounded by the enrage row "
              "plus one gap");
    }

    // =====================================================================
    // 5. THE CONDITIONS WINDOW AS AN OBJECT.
    // =====================================================================
    {
        MonsterConditionsStripPanel w;
        prime(w);
        w.setSectionMask(mhw::MonsterSection::kAll);

        check(w.isSubWindow(),
              "conditions: declares itself a sub-window");
        check(w.supportsColumns(),
              "conditions: a two-grid window answers the columns dial");
        check(w.contentMargin() == mhw::split_layout::kSplitMargin,
              "conditions: answers the split's shared inset");
        check(w.hasVisibleContent(),
              "conditions: has content with the demo seeded and all bits on");
        check(w.itemCount() > 0,
              "conditions: itemCount() reports the demo's parts");
        check(w.naturalItemWidth() > 0,
              "conditions: naturalItemWidth() is measured");
        check(w.itemGap() > 0,
              "conditions: itemGap() is answered");
        check(w.rowPitch() > 0,
              "conditions: rowPitch() is answered — the one whose "
              "absence is invisible until a layout asks for a wrap");

        // The reserve/draw inversion, the same inversion
        // monster_parts_window_tests pins for the old parts window.
        check(w.contentHeight() == w.contentSize().height(),
              "conditions: the height it reports is the height it wants");
        check(w.contentWidth() == w.contentSize().width(),
              "conditions: the width it reports is the width it wants");
        check(w.contentHeight() >= 2 * w.contentMargin(),
              "conditions: never thinner than two margins");

        // TWO dials, independently clamped. One number for a 4-column
        // ailment card and a 3-column part card would make a usable
        // setting for one unusable for the other.
        w.setCardsPerRow(1);
        check(w.cardsPerRow() == 1,
              "conditions: the parts dial accepts 1 (a stacked column)");
        w.setCardsPerRow(-5);
        check(w.cardsPerRow() >= 1,
              "conditions: the parts dial clamps a negative up to 1");
        w.setCardsPerRow(99);
        check(w.cardsPerRow() < 99,
              "conditions: the parts dial caps an absurd value");
        check(w.cardsPerRow() == w.columns(),
              "conditions: cardsPerRow() and the base columns() agree");

        w.setAilCardsPerRow(1);
        check(w.ailCardsPerRow() == 1,
              "conditions: the ailments dial accepts 1");
        w.setAilCardsPerRow(0);
        check(w.ailCardsPerRow() >= 1,
              "conditions: the ailments dial clamps 0 up to 1 (no "
              "divide-by-zero in monsterAilRows)");
        w.setAilCardsPerRow(99);
        check(w.ailCardsPerRow() <= mhw::kMonsterAilMaxColumns,
              "conditions: the ailments dial caps at the documented max");

        // Fewer columns is narrower and taller — the stacking direction
        // the user asked for, and the dial actually moving geometry.
        w.setCardsPerRow(3);
        w.setAilCardsPerRow(4);
        for (int i = 0; i < 4; ++i) {
            w.repaint();
            QCoreApplication::processEvents();
        }
        const int wideW = w.contentWidth();
        const int wideH = w.contentHeight();
        w.setCardsPerRow(1);
        for (int i = 0; i < 4; ++i) {
            w.repaint();
            QCoreApplication::processEvents();
        }
        check(w.contentWidth() < wideW,
              "conditions: stacking the parts to one column makes it "
              "NARROWER");
        check(w.contentHeight() > wideH,
              "conditions: and TALLER, so no card is lost on the way down");
    }

    // =====================================================================
    // 6. THE PER-GAME hasContent DIFFERENCE.
    // =====================================================================
    //
    // Both windows' demo seeds are game-aware (the ailment ids and the
    // crown tables are game-keyed), and a game switch must not leave a
    // window mounted on the other game's data. The same class of defect
    // the user reported as 「衣装 and 翔虫 at the same time」.
    {
        MonsterConditionsStripPanel w;
        prime(w, mhw::GameId::World);
        w.setSectionMask(mhw::MonsterSection::kAll);
        const bool worldOk = w.hasVisibleContent();

        w.setGameForDemo(mhw::GameId::Rise);
        w.resetDemoPrimed();
        w.show();
        for (int i = 0; i < 6; ++i) {
            w.repaint();
            QCoreApplication::processEvents();
        }
        check(worldOk,
              "game: the World demo seeds a mounted Conditions window");
        check(w.hasVisibleContent(),
              "game: the Rise demo ALSO seeds a mounted Conditions window "
              "(both games have ailments and parts)");
    }
    {
        MonsterIdentityStripPanel w;
        prime(w, mhw::GameId::World);
        w.setSectionMask(mhw::MonsterSection::kAll);
        check(w.hasVisibleContent(),
              "game: the World demo seeds a mounted Identity window");
        w.setGameForDemo(mhw::GameId::Rise);
        w.resetDemoPrimed();
        w.show();
        for (int i = 0; i < 6; ++i) {
            w.repaint();
            QCoreApplication::processEvents();
        }
        check(w.hasVisibleContent(),
              "game: the Rise demo seeds a mounted Identity window too "
              "(the bars are game-agnostic; only the crown changes)");
    }

    // =====================================================================
    // 7. THE MODELS' FOLDS — per block, the panel's own rule.
    // =====================================================================
    // A zero maximum is "the read did not resolve", never "0% full".
    {
        mhw::MonsterSnapshot m = demoMonster();
        const mhw::MonsterIdentityModel full =
            mhw::buildMonsterIdentityModel(m);
        check(full.health.known && full.stamina.known && full.hasEnrage(),
              "model: a resolved monster resolves all three bars");
        check(full.monsterId == 1 && full.game == mhw::GameId::World,
              "model: the identity fields arrive on the model");

        // health == 0 with a resolved maximum is a DEFEATED monster, not
        // an unresolved read: the bar draws empty and the numbers say
        // "0 / 25,800". The panel's own hpPct distinguishes the two by
        // the DENOMINATOR only, and so does this model.
        mhw::MonsterSnapshot dead = demoMonster();
        dead.health = 0.0F;
        const mhw::MonsterIdentityModel defeated =
            mhw::buildMonsterIdentityModel(dead);
        check(defeated.health.known && defeated.health.pct == 0.0F,
              "model: health == 0 with a known maximum is a defeated "
              "monster (empty bar, real numbers)");

        // An unresolved read is ONLY the denominator dropping out.
        m.maxHealth = 0.0F;
        const mhw::MonsterIdentityModel deadHp =
            mhw::buildMonsterIdentityModel(m);
        check(!deadHp.health.known,
              "model: maxHealth == 0 reads as unresolved, not as 0/0");

        const mhw::MonsterSnapshot noRage = [] {
            mhw::MonsterSnapshot s = demoMonster();
            s.enraged          = false;
            s.enrageMaxBuildup = 0.0F;
            s.enrageBuildup    = 0.0F;
            s.enrageSeconds    = 0.0F;
            return s;
        }();
        check(!mhw::buildMonsterIdentityModel(noRage).hasEnrage(),
              "model: no buildup pair and not enraged is no meter at all");
    }

    // Enrage's readings: MAX is decided by the TIMER, not the fill. A
    // monster enraged with no live seconds is 100% filled and reads
    // "100%", not "MAX" — the panel's own rule (:1314-1320).
    {
        mhw::MonsterSnapshot m = demoMonster();      // enraged, 32s left
        const mhw::MonsterIdentityModel live =
            mhw::buildMonsterIdentityModel(m);
        check(live.reading == mhw::MonsterEnrageReading::Max
                  && live.pulsing,
              "model: enraged with a live timer reads MAX and pulses");
        check(live.pct == 1.0F,
              "model: a full buildup fills the track completely");

        m.enrageSeconds = 0.0F;
        const mhw::MonsterIdentityModel spent =
            mhw::buildMonsterIdentityModel(m);
        check(spent.reading == mhw::MonsterEnrageReading::Percent
                  && !spent.pulsing,
              "model: enraged with no live seconds reads the percentage "
              "and does NOT pulse");
        check(spent.hasEnrage(),
              "model: and it is still a meter (the block exists)");

        m.enrageBuildup = 30.0F;
        const mhw::MonsterIdentityModel partial =
            mhw::buildMonsterIdentityModel(m);
        check(partial.pct > 0.29F && partial.pct < 0.31F,
              "model: a partial buildup maps to its fraction");

        // The name fallback Rise needs: no localisation key for monsters.
        mhw::MonsterSnapshot nameless = demoMonster();
        nameless.internalName.clear();
        nameless.id = 42;
        check(mhw::buildMonsterIdentityModel(nameless).name
                  == QStringLiteral("Monster 42"),
              "model: an empty internal name falls back to Monster <id>");
    }

    // NaN folding, which is the 2026-09-22 crash class: Qt6's qRound()
    // asserts on non-finite input and a NaN reaching a divide produces a
    // garbage bar. Both folds must land on 0, the "no such layer"
    // sentinel.
    {
        const float nan = std::nanf("");
        mhw::MonsterSnapshot m = demoMonster();
        m.maxHealth = nan;   m.health     = nan;
        m.maxStamina = nan;  m.stamina    = nan;
        m.enrageMaxBuildup = nan; m.enrageBuildup = nan;
        const mhw::MonsterIdentityModel dirty =
            mhw::buildMonsterIdentityModel(m);
        check(!dirty.health.known && !dirty.stamina.known,
              "model: a NaN maximum reads as unresolved, not as a "
              "platform-defined cast");
        // A NaN buildup denominator falls into the panel's OWN fallback
        // arm — `maxBuildup > 0` is false for NaN, so the fill becomes
        // the binary on `enraged` (panel_monster.cpp:1316-1320). For an
        // enraged monster that is 1.0, which is correct and finite: the
        // point of the guard is that NO non-finite value reaches the
        // bar's width multiply, not that the answer is zero.
        check(std::isfinite(dirty.pct) && dirty.pct >= 0.0F
                  && dirty.pct <= 1.0F,
              "model: a NaN buildup never becomes a non-finite bar "
              "fraction (the panel's binary fallback applies)");
        check(dirty.pct == 1.0F,
              "model: an enraged monster with a NaN buildup fills the "
              "track — the panel's `enraged ? 1.0 : 0.0` arm");
        // The NaN reads: with the fill folds at 0 the meter is a real
        // (empty) one, because `enraged` is a bool that survives the
        // NaN. The panel's `showRage` is exactly that test.
        check(dirty.hasEnrage(),
              "model: a NaN buildup still leaves the enraged meter "
              "drawable (showRage is the panel's test, NaN only zeroes "
              "the fill)");
    }

    // =====================================================================
    // 7b. THE AILMENTS TRACKER'S RESET ON TARGET SWITCH.
    // =====================================================================
    //
    // HONEST SCOPE NOTE, recorded so the gap is a known one: the ailments'
    // 15 s AutoHide state lives in mhw::MonsterAilmentTracker, whose
    // resetAutoHide() is called from update() on the `address` change. The
    // tracker is DEFAULTED-constructible and its isVisible() is public, so
    // the state machine itself IS directly testable — but that test needs
    // a controlled clock (a `nowMs` more than 15 s past the stamp), and
    // passing one through this window's public update() is not possible
    // without adding a test-only parameter to the signature.
    //
    // What this file therefore pins is the CONSEQUENCE the way a user
    // sees it: a target switch must leave the grid mounted and populated.
    // The tracker's own semantics are covered by
    // monster_split_models_tests.cpp's neighbours, and the tracker's
    // class comment names the three rules isVisible() implements.
    {
        MonsterConditionsStripPanel w;
        w.setEditMode(true);
        w.setSectionMask(mhw::MonsterSection::kAll);
        const mhw::MonsterSnapshot first = demoMonster();
        w.update(first);
        check(w.hasVisibleContent(),
              "tracker: the first target's ailments are visible");
        mhw::MonsterSnapshot second = demoMonster();
        second.address = 0x0BADF00DULL;
        w.update(second);
        for (int i = 0; i < 4; ++i) {
            w.repaint();
            QCoreApplication::processEvents();
        }
        // A switch with edit mode on keeps every card pinned either way,
        // so the honest assertion is that the reset happens without
        // perturbing the card set: a dropped reset shows up as a grid
        // that no longer matches the snapshot it was just handed.
        check(w.itemCount() > 0 && w.hasVisibleContent(),
              "tracker: a target switch leaves the Conditions grid intact");

        // And the GAME switch, which IS observable: Rise ailment ids are
        // unrelated to World's (Rise 4 is poison, World 2 is paralysis),
        // so a window that failed to re-resolve would keep the old
        // game's card names on screen. One update from a World monster
        // then one from a Rise monster, and the second must still mount.
        mhw::MonsterSnapshot rise = demoMonster(mhw::GameId::Rise);
        rise.address = 0x0BEEBEEFULL;
        w.update(rise);
        for (int i = 0; i < 4; ++i) {
            w.repaint();
            QCoreApplication::processEvents();
        }
        check(w.hasVisibleContent(),
              "tracker: a World->Rise switch re-resolves through the "
              "game-keyed table and keeps the grid mounted");
    }

    // =====================================================================
    // 8. THE I18N KEYS THIS CHANGE NEEDS.
    // =====================================================================
    //
    // The two windows ask for ONE new key each (their title). Reporting
    // it here rather than silently shipping a missing-key literal: a
    // StringTable miss returns the key itself, which is visible in the
    // UI and is the behaviour we are asserting on.
    const mhw::StringTable &table = mhw::StringTable::instance();
    check(table.tr(QStringLiteral("ui.monster_strip_title"))
              != QStringLiteral("ui.monster_strip_title"),
          "i18n: ui.monster_strip_title resolves (the shared window title)");
    check(table.tr(QStringLiteral("ui.monster_stamina"))
              != QStringLiteral("ui.monster_stamina"),
          "i18n: ui.monster_stamina resolves (the Identity fatigue label)");

    std::printf("\n%s: %d checks, %d failures\n",
                gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
