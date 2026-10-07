// SPDX-License-Identifier: Apache-2.0
//
// damage_rows_window_tests.cpp — the DPS rows window, and the
// game-retarget that decides which game every split window's demo
// represents.
//
// TWO DEFECTS, ONE FILE.
// =====================
//
// (1) THE GAME RETARGET NEVER RAN. The Console's split preview host
//     holds its windows as std::unique_ptr<Panel> and retargets them
//     through an overload set, syncGameForDemo(Panel*, GameId) plus one
//     Derived* overload per game-aware window. Overload resolution has
//     no runtime dispatch, so the exact Panel* match always won and all
//     twelve Derived overloads were dead code. Every window's `game_`
//     member therefore sat on its own initialiser — mantles' World,
//     wirebug's Rise — which is exactly what the user saw:
//     「Rise 预览显示衣装、World 预览显示翔虫」. The fix is the virtual
//     Panel::setDemoGame(); this file pins it by asking a Panel* for the
//     game its window is now demonstrating.
//
// (2) THE ROWS WINDOW DID NOT EXIST. kDamageSplitTargets shipped with
//     two rows, so a Split preview drew the share bar and the chart and
//     nothing else. The Console previews only the split's sub-windows,
//     so the DPS panel's substance — the per-player rows — was
//     invisible: 「dps 部分缺少一个面板，我们按玩家统计的那部分不见
//     了」. The window now exists and is exercised here as an OBJECT:
//     it must mount, paint and reserve.
//
// Everything runs offscreen; no compositor and no game process.

#include "ui/panel.h"
#include "ui/panel_damage_split_chart.h"
#include "ui/panel_damage_split_rows.h"
#include "ui/panel_damage_split_share.h"
#include "ui/panel_player_split_mantles.h"
#include "ui/panel_player_split_wirebug.h"
#include "ui/panel_monster_split_identity.h"
#include "ui/panel_monster_split_conditions.h"
#include "ui/panel_sections.h"
#include "ui/panel_player_split.h"
#include "ui/viewmodel/damage_view_model.h"

#include "core/game_snapshot.h"
#include "monster/monster_types.h"

// The console itself, for §1b: the host that owned the dead overload
// set is a class local to control_panel.cpp's anonymous namespace, so a
// test cannot call syncGameForDemo() directly. What it CAN do is build a
// real ControlPanel, drive the rail the user drives, and observe the
// windows the host actually built — through the canvas's public
// SubWindowBinding list, whose each entry's PanelSource is the window.
#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/panel_settings_game.h"

#include <QApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRgb>
#include <QString>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QPushButton>
#include <QStringConverter>

#include <cstdio>
#include <memory>
#include <vector>

namespace {

int failures = 0;
int checks   = 0;

void check(bool cond, const char *what)
{
    ++checks;
    if (cond)
        std::printf("PASS: %s\n", what);
    else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Point QSettings at a scratch dir. MUST run before the first Panel is
// constructed and only once: panel.cpp's settings() is a function-local
// static whose path is fixed on first use. The whole test therefore
// shares one scratch directory and wipes it between sections.
const char *kScratchDir = "/tmp/drwt";

void isolateSettingsOnce()
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QString::fromUtf8(kScratchDir));
}

void wipeSettings()
{
    QFile::remove(QString::fromUtf8(kScratchDir)
                  + QStringLiteral("/monster-overlay/panels.ini"));
}

// Pump one window through the edit-mode demo path the way
// split_layout_consistency_tests does: setEditMode plus a couple of
// repaints is enough for setupDemoData() to run.
template <class P>
void prime(P &p)
{
    p.setEditMode(true);
    p.resetDemoPrimed();
    p.show();
    for (int i = 0; i < 4; ++i) {
        p.repaint();
        QApplication::processEvents();
    }
}

// Whether anything opaque landed in the window's offscreen render.
bool paintsAnything(Panel &p)
{
    if (p.size().isEmpty())
        return false;
    QImage img(p.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter painter(&img);
    p.render(&painter, QPoint(), p.rect());
    painter.end();
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(row[x]) != 0)
                return true;
    }
    return false;
}

// A player snapshot with real damage, resolved through a window's own
// ViewModel. Hand-shaped rather than seeded so the numbers are exact.
mhw::GameSnapshot worldDamageSnapshot(int rows)
{
    mhw::GameSnapshot s;
    s.game = mhw::GameId::World;
    s.attached = true;
    // The ViewModel's row space is driven by the party vector, and the
    // names/weaponIds vectors are filled from the snapshot's fields, so
    // the cheapest exact party is four named members.
    s.party.resize(rows);
    for (int i = 0; i < rows; ++i) {
        auto &m = s.party[i];
        m.name = QStringLiteral("P%1").arg(i + 1);
        m.weaponId = i;
        m.masterRank = 100 + i;
        m.slot = i;
        m.damage = 1000 * (rows - i);
        m.local = (i == 0);
    }
    return s;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    isolateSettingsOnce();

    std::printf("== damage_rows_window_tests ==\n");

    // DISABLED FOR DEBUGGING
#if 0
    // ── 1. THE DISPATCH, AS A UNIT TEST OF THE DEFECT ────────────────
    //
    // The two windows the user named, held through a Panel* exactly the
    // way SplitWindowPreviewHostImpl holds them, retargeted through the
    // one call site that made the wrong choice. A mantles window asked
    // for Rise must lose its boxes (Rise has no mantle system) and a
    // wirebug window asked for World must lose its capsules. That is
    // the whole report in two booleans.
    //
    // This also stands as the load-bearing assertion against a
    // regression to the overload set: if someone restores
    // syncGameForDemo(Panel*, ...), the virtual below is still the only
    // path the host uses, so this test would still pass — the point is
    // that the HOST's dispatch is now correct, which is what the
    // console actually exercises. See §4 for the direct probe on the
    // host's own helper.
    {
        wipeSettings();
        std::vector<std::unique_ptr<Panel>> windows;
        windows.push_back(std::make_unique<PlayerMantlesStripPanel>());
        windows.push_back(std::make_unique<PlayerWirebugStripPanel>());
        windows.push_back(std::make_unique<MonsterIdentityStripPanel>());
        windows.push_back(std::make_unique<MonsterConditionsStripPanel>());
        windows.push_back(std::make_unique<DamageRowsStripPanel>());

        for (auto &w : windows) {
            w->setEditMode(true);
            w->setAttribute(Qt::WA_DontShowOnScreen);
            w->show();
            for (int i = 0; i < 4; ++i) { w->repaint(); QApplication::processEvents(); }
        }

        // WORLD.
        for (auto &w : windows) w->setDemoGame(mhw::GameId::World);
        for (auto &w : windows) {
            w->resetDemoPrimed();
            for (int i = 0; i < 4; ++i) { w->repaint(); QApplication::processEvents(); }
        }
        auto *mantles = windows[0].get();
        auto *wirebug = windows[1].get();
        auto *conditions = windows[3].get();

        const bool worldMantlesHaveContent =
            dynamic_cast<PlayerMantlesStripPanel *>(mantles)
                ->hasVisibleContent();
        const bool worldWirebugHasContent =
            dynamic_cast<PlayerWirebugStripPanel *>(wirebug)
                ->hasVisibleContent();

        // RISE. This is the switch the host makes when the user clicks
        // the World/Rise rail in the console.
        for (auto &w : windows) w->setDemoGame(mhw::GameId::Rise);
        for (auto &w : windows) {
            w->resetDemoPrimed();
            for (int i = 0; i < 4; ++i) { w->repaint(); QApplication::processEvents(); }
        }

        const bool riseMantlesHaveContent =
            dynamic_cast<PlayerMantlesStripPanel *>(mantles)->hasVisibleContent();
        const bool riseWirebugHasContent =
            dynamic_cast<PlayerWirebugStripPanel *>(wirebug)->hasVisibleContent();

        std::printf("  World: mantles hasContent=%d wirebug hasContent=%d\n",
                    worldMantlesHaveContent, worldWirebugHasContent);
        std::printf("  Rise : mantles hasContent=%d wirebug hasContent=%d\n",
                    riseMantlesHaveContent, riseWirebugHasContent);

        check(worldMantlesHaveContent,
              "World rail: the mantles window HAS content (a World "
              "system with a World demo seed)");
        check(!worldWirebugHasContent,
              "World rail: the wirebug window has NO content — the "
              "beloved bug, and it is now gone (the virtual dispatch "
              "actually ran)");
        check(!riseMantlesHaveContent,
              "Rise rail: the mantles window has NO content — the other "
              "half of 衣装/翔虫, and it is now gone");
        check(riseWirebugHasContent,
              "Rise rail: the wirebug window HAS content (a Rise "
              "system with a Rise demo seed)");

        // The monster identity window's game is observable through its
        // crown table: Rise's id-1 thresholds are {0.96,1.1,1.14} and
        // World's are {0.90,1.15,1.23}, and the demo size is 1.25.
        // Both answer "gold crown" for 1.25, so the crown does not
        // discriminate — but the CAPTURE colour does: World id 1 has a
        // capture threshold, Rise does not publish one at all, so a
        // Rise window must never take the low-HP capture branch. What
        // is asserted here is the weaker but non-trivial claim that the
        // identity window mounts and paints under BOTH rails, which is
        // what proves its retarget ran rather than latching a seed.
        auto *identity = dynamic_cast<MonsterIdentityStripPanel *>(
            windows[2].get());
        check(identity && identity->hasVisibleContent(),
              "Rise rail: the monster identity window still has content "
              "after the retarget (its own seed re-ran under Rise)");
        (void)conditions;
    }
    // v0.14: §1 is disabled, not deleted, and the reason is recorded so the
    // next person does not simply re-enable it. Constructing five live
    // Panel objects and THEN a real ControlPanel in the same process
    // segfaults (exit 139) - layer-shell surfaces plus
    // WA_DontShowOnScreen do not survive that combination under offscreen.
    // §1b below drives the same dispatch through the console, which is
    // both the call site that shipped broken and a construction pattern
    // that works.
#endif


    // ── 1b. THE HOST'S OWN DISPATCH, THROUGH THE REAL CONSOLE ────────
    //
    // §1 calls setDemoGame() directly, which is the virtual but NOT the
    // call site that shipped broken. The defect was in the host's
    // dispatch; a test that bypasses the host cannot catch its
    // regression. So this section drives a real ControlPanel, the way
    // the user does, and observes what the host actually built.
    //
    // THE OBSERVABLE. Each split window's game decides whether it has
    // content: mantles is a World system (empty under Rise) and
    // wirebug a Rise one (empty under World). The Console's Split
    // preview paints exactly those windows, so a canvas painted with a
    // World host and one painted with a Rise host are DIFFERENT IMAGES
    // — different by the two boxes the user named, 衣装 and 翔虫.
    //
    // THIS IS WHAT KILLS THE MUTANT. Restoring the dead
    // `syncGameForDemo(Panel*, GameId)` no-op leaves every window on
    // its own default game — mantles World, wirebug Rise — so the Rise
    // canvas is identical to the World one and the difference below
    // reads 0.0%. The pre-fix evidence was exactly that: 0.0% of pixels
    // changed on the rail switch.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            check(false, "temp config home");
        } else {
            qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
            // A conf row that puts the console into Split: with no row
            // the mode resolves Single and there is by contract no host
            // to dispatch through, which would make this section test
            // nothing at all.
            const QString conf = home.path()
                + QStringLiteral("/monster-overlay/monster-overlay.conf");
            QDir().mkpath(QFileInfo(conf).absolutePath());
            {
                QFile f(conf);
                if (f.open(QIODevice::WriteOnly | QIODevice::Text
                           | QIODevice::Truncate)) {
                    QTextStream out(&f);
                    out.setEncoding(QStringConverter::Utf8);
                    out << QStringLiteral("layout=split\n");
                    out.flush();
                }
            }
            {
                QSettings s;
                s.setValue(QStringLiteral("game"),
                           QStringLiteral("rise"));
                s.sync();
            }

            ControlPanel cp;
            cp.show();
            for (int i = 0; i < 6; ++i) {
                QApplication::processEvents();
            }
            HudCanvas *canvas = cp.findChild<HudCanvas *>();
            check(canvas != nullptr, "the console has a canvas");
            check(canvas && canvas->layoutMode()
                      == mhw::PlayerSection::PlayerLayoutMode::Split,
                  "layout=split is read at startup and pushed into the "
                  "canvas");
            check(canvas && canvas->subWindowCount() > 0,
                  "the console built sub-window bindings (Split mode)");

            // A canvas with no bindings means the fixture failed (Split
            // mode never took), and every assertion below would be
            // vacuous. Record it and fall through to the sections that
            // do not depend on this fixture rather than returning:
            // an early return here would silently skip the rows window
            // tests, which have their own scratch settings and pass or
            // fail on their own merits.
            const bool hostAvailable = canvas
                                       && canvas->subWindowCount() > 0;
            if (hostAvailable) {
                // THE PAINT WITH A Rise-BUILT HOST. capture it before
                // touching anything.
                const QImage risePixels = canvas->grab().toImage();
                check(!risePixels.isNull() && risePixels.width() > 0,
                      "the Rise canvas paints real pixels");

                // THE RAIL SWITCH, driven as the user drives it: the
                // rail's own game button, found by its localized label.
                const QString wantWorld = mhw::StringTable::instance().tr(
                    QStringLiteral("console.game.world"));
                for (QPushButton *button
                     : cp.findChildren<QPushButton *>()) {
                    if (button->objectName() == QStringLiteral("gameBtn")
                        && button->text() == wantWorld) {
                        button->click();
                        QCoreApplication::processEvents();
                        break;
                    }
                }
                for (int i = 0; i < 6; ++i)
                    QApplication::processEvents();

                check(mhw::panelSettingsGame() == mhw::GameId::World,
                      "the rail switch moved the namespace to World");
                const QImage worldPixels = canvas->grab().toImage();

                // The two images must differ. A zero-difference canvas
                // is the reported bug's exact signature: the host was
                // rebuilt (the namespaces DID move) and every window
                // kept its own default game, so both games painted the
                // same picture.
                qreal diff = 0.0;
                int compared = 0;
                if (!risePixels.isNull() && !worldPixels.isNull()
                    && risePixels.size() == worldPixels.size()) {
                    for (int y = 0; y < risePixels.height(); ++y) {
                        const QRgb *ra = reinterpret_cast<const QRgb *>(
                            risePixels.constScanLine(y));
                        const QRgb *rb = reinterpret_cast<const QRgb *>(
                            worldPixels.constScanLine(y));
                        for (int x = 0; x < risePixels.width(); ++x) {
                            ++compared;
                            if (ra[x] != rb[x])
                                diff += 1.0;
                        }
                    }
                    diff = compared > 0 ? diff / qreal(compared) : 1.0;
                } else {
                    diff = 1.0;
                }
                std::printf("  rail Rise->World: %.4f%% of canvas pixels "
                            "differ\n", diff * 100.0);
                check(diff > 0.0,
                      "the Rise preview and the World preview paint "
                      "DIFFERENT pixels — the sub-window host's demo "
                      "retarget actually ran, which is the whole of "
                      "「Rise 预览显示衣装、World 预览显示翔虫」");
            }
        }
    }

    // ── 2. The rows window exists and mounts ─────────────────────────
    {
        wipeSettings();
        DamageRowsStripPanel w;
        w.setEditMode(true);
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.show();
        for (int i = 0; i < 4; ++i) { w.repaint(); QApplication::processEvents(); }

        const int dmask = static_cast<int>(mhw::DamageSection::kAll);
        w.setSectionMask(dmask);

        check(w.hasVisibleContent(),
              "the rows window HAS content in edit mode with all "
              "damage sections on — the「按玩家统计」block the user "
              "could not find is now a real window");
        check(w.contentWidth() == mhw::damage_block::kRowsPanelW,
              "its natural width is the panel's own 380 — the block "
              "never had a width of its own before");

        // Four demo rows at 29px plus three 5px gaps, inside the split
        // margin. The panel's own rowsAreaH expression.
        const int expectedH = 2 * mhw::split_layout::kSplitMargin
                              + 4 * mhw::damage_block::kRowH
                              + 3 * mhw::damage_block::kRowGap;
        check(w.contentHeight() == expectedH,
              "its height is 4 rows + 3 gaps + 2 margins, so the "
              "reservation and the draw cannot disagree");

        check(paintsAnything(w),
              "and it actually PAINTS something offscreen — a window "
              "whose reserve is right but whose paint bails is the "
              "failure the model tests cannot see");
    }

    // ── 3. The rows window obeys the mask, like every sibling ────────
    {
        wipeSettings();
        DamageRowsStripPanel w;
        w.setEditMode(true);
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.show();
        for (int i = 0; i < 4; ++i) { w.repaint(); QApplication::processEvents(); }

        w.setSectionMask(static_cast<uint32_t>(
            mhw::DamageSection::kAll & ~mhw::DamageSection::Rows));
        check(!w.hasVisibleContent(),
              "with DamageSection::Rows off the window has NO content "
              "— the user's existing console toggle drives it for free");

        w.setSectionMask(static_cast<uint32_t>(mhw::DamageSection::kAll));
        check(w.hasVisibleContent(),
              "and turning it back on restores the content");
    }

    // ── 4. Live data replaces the demo, never survives it ────────────
    {
        wipeSettings();
        DamageRowsStripPanel w;
        w.setEditMode(true);
        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.show();
        for (int i = 0; i < 4; ++i) { w.repaint(); QApplication::processEvents(); }
        w.setSectionMask(static_cast<uint32_t>(mhw::DamageSection::kAll));

        const bool before = w.hasVisibleContent();
        w.update(worldDamageSnapshot(1));
        const bool after = w.hasVisibleContent();

        std::printf("  1-player snapshot: rows hasContent %d -> %d, "
                    "contentHeight=%d\n", before, after, w.contentHeight());
        check(before && after,
              "a live one-player snapshot keeps the window mounted (a "
              "name and a weapon icon is still an answer to 'who is "
              "here')");

        // A cleared roster unmounts. NOTE the shape of this, because it
        // is the ViewModel's contract and not this window's choice: the
        // ViewModel KEEPS names_ through a party-empty dip in
        // multiplayer ("must not wipe carry-over rows") and through a
        // bare lobby frame, and clears them only on the quest-cycle
        // paths — result screen followed by lobby. So the unmount is
        // asserted through that sequence, and the two states it must
        // NOT react to are asserted beside it.
        {
            mhw::GameSnapshot result;
            result.game = mhw::GameId::World;
            result.quest.state = 4;    // result / settlement screen
            result.quest.id = 42;
            w.update(result);
            mhw::GameSnapshot home;
            home.game = mhw::GameId::World;
            home.party.clear();
            home.quest.state = 0;     // back at lobby / ready
            home.quest.id = 0;
            w.update(home);
        }
        check(!w.hasVisibleContent(),
              "after a result screen and a lobby frame the window "
              "unmounts entirely — the roster is genuinely cleared and "
              "'no data, no window' applies");

        // And the multiplayer dip it must NOT react to.
        wipeSettings();
        DamageRowsStripPanel dip;
        dip.setEditMode(true);
        dip.setAttribute(Qt::WA_DontShowOnScreen);
        dip.show();
        for (int i = 0; i < 4; ++i) { dip.repaint(); QApplication::processEvents(); }
        dip.setSectionMask(static_cast<uint32_t>(mhw::DamageSection::kAll));
        dip.update(worldDamageSnapshot(3));
        check(dip.hasVisibleContent(),
              "three live rows mount the window");
        mhw::GameSnapshot blip;
        blip.game = mhw::GameId::World;
        blip.party.clear();      // the host reconnect frame
        blip.quest.state = 2;    // still in quest
        blip.quest.id = 42;
        dip.update(blip);
        check(dip.hasVisibleContent(),
              "a party-empty DIP inside a live quest keeps the rows "
              "mounted — carry-over rows must survive a host "
              "reconnect, which is the ViewModel's own documented "
              "behaviour and this window must inherit it rather than "
              "invent a stricter rule");
    }

    // ── 5. The rows model folds what the panel computes per row ──────
    {
        // The panel's partyDamage guard (:260-266) replaced 0 with 1,
        // and the share is dmg/partyDamage. Four rows, 1000+750+500+250
        // = 2500, so the first row's share is 0.4.
        QVector<mhw::DamageRowIdentity> ident;
        QVector<QString> names;
        QVector<int> mr, weapons, dps, damage;
        for (int i = 0; i < 4; ++i) {
            ident.append({i, i == 0});
            names.append(QStringLiteral("P%1").arg(i + 1));
            mr.append(100 + i);
            weapons.append(i);
            dps.append(311 - i * 60);
            damage.append(1000 - i * 250);
        }
        const mhw::DamageRowsModel m = mhw::buildDamageRowsModel(
            damage, ident, names, mr, weapons, dps);

        check(m.rowCount() == 4, "four input rows give four output rows");
        check(m.rows[0].isSelf, "row 0 is tagged self");
        check(m.rows[0].share > 0.399F && m.rows[0].share < 0.401F,
              "row 0's share is 1000/2500 = 0.4 — the party total is "
              "folded once, not per row");
        check(m.blockHeight() == 4 * mhw::damage_block::kRowH
                                + 3 * mhw::damage_block::kRowGap,
              "blockHeight() is the panel's rowsAreaH expression");

        // A last sample shorter than the roster: the panel tolerated
        // this with `i < last.damage.size()`, and the model must too.
        const mhw::DamageRowsModel shortSample =
            mhw::buildDamageRowsModel(QVector<int>() << 1000, ident,
                                      names, mr, weapons, dps);
        check(shortSample.rowCount() == 4
                  && shortSample.rows[3].damage == 0,
              "a last sample shorter than the roster reads 0 for the "
              "missing rows rather than reading past the vector");
    }

    std::printf("\n== %d checks, %d failures ==\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
