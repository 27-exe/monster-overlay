#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include "core/string_table.h"
#include "monster/monster_types.h"
#include "ui/control_panel.h"
#include "ui/panel.h"
#include "ui/panel_settings_game.h"

#include <cstdio>

// v0.15: THE TEST THE v0.14 SEED FIX COULD NOT BE TESTED WITH.
//
// The seed bug this round chased — the console showed World's mantles
// under Rise and Rise's wirebugs under World — had a root cause worth
// stating precisely: the host called an OVERLOAD SET resolved at compile
// time against a Panel*, so the no-op base overload always won and the
// twelve typed ones never ran.
//
// The previous round's attempt to pin that failed, and the failure is the
// reason this file exists. It drove a real console through a rail click
// and compared the Rise canvas with the World canvas. That catches the
// NAMESPACE half - it asserts `panelSettingsGame()` moved and the canvas
// changed - and it cannot catch the SEED half, because a canvas differs
// from the GEOMETRY rebuild alone: every window re-reads [game/mode/key]
// and landed somewhere new whether or not its game_ moved. Empirical
// proof: gutting PlayerMantlesStripPanel::setDemoGame() so it was exactly
// the no-op the dead overloads always were, and both console suites stayed
// green.
//
// The second attempt used hasVisibleContent() - mantles are a World system
// and empty under Rise, so it reads the seed through the content. That is
// not safe either: a rail switch destroys the preview host and builds a
// new one, so any handle taken before the switch is dangling, and a handle
// taken after reaches Qt state the new window has not realised. gdb put
// that in QTabBar::sizeHint() under Panel::hasVisibleContent().
//
// So the observable is the seed itself - Panel::demoGame() - reached
// afresh after every switch. No Qt state, no window handle that outlives
// its host, and the cause rather than a symptom.

namespace
{

int checks = 0;
int failures = 0;

void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  FAIL  %s\n", what);
    } else {
        std::printf("PASS: %s\n", what);
    }
}

void writeConfLayout(const QString &mode, const QString &home)
{
    // No `.config` in this path: monster-overlay.conf is read from
    // $XDG_CONFIG_HOME/monster-overlay/, not from $XDG_CONFIG_HOME/.config
    // /... Writing it under `.config` leaves the reader finding nothing,
    // the mode resolving to Single, and every assertion below silently
    // vacuous.
    const QString path = home
                         + QStringLiteral("/monster-overlay/"
                                          "monster-overlay.conf");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << QStringLiteral("layout=%1\n").arg(mode);
        out.flush();
    }
}

void clickGameBtn(ControlPanel &cp, const QString &label)
{
    for (QPushButton *button : cp.findChildren<QPushButton *>()) {
        if (button->objectName() == QStringLiteral("gameBtn")
            && button->text() == label) {
            button->click();
            for (int i = 0; i < 8; ++i)
                QCoreApplication::processEvents();
            return;
        }
    }
}

// Re-read the seed from the CURRENT host. A rail click rebuilds the host,
// so anything captured before one is a use-after-free.
mhw::GameId seedOf(ControlPanel &cp, const char *key, bool &found)
{
    return cp.splitPreviewDemoGame(QString::fromLatin1(key), &found);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QTemporaryDir home;
    Q_ASSERT(home.isValid());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    QDir().mkpath(home.filePath(QStringLiteral("monster-overlay")));

    {
        QSettings s;
        s.setValue(QStringLiteral("game"), QStringLiteral("rise"));
        s.sync();
    }
    // Split mode, or there is no host and nothing to dispatch through.
    writeConfLayout(QStringLiteral("split"), home.path());

    ControlPanel cp;
    cp.show();
    for (int i = 0; i < 8; ++i)
        QCoreApplication::processEvents();

    bool found = false;
    // v1.1.5: the tracked window is player-bars, not player-mantles. The
    // mantles window used to be the sharpest probe (World initialiser,
    // read under a Rise rail), but a Rise host no longer BUILDS it at all
    // — the game filter removed the very surface the dead dispatch used
    // to corrupt. player-bars is the same virtual dispatch on a window
    // BOTH games mount, with its own per-game demo (raw stamina for Rise,
    // bar-domain for World), so a dead setDemoGame() still answers World
    // under a Rise rail and the assertion still dies exactly as before.
    const mhw::GameId asBuiltRise = seedOf(cp, "player-bars", found);
    check(found, "the console built a player-bars split-preview window");
    if (!found) {
        std::printf("\n== %d checks, %d failures ==\n", checks, failures);
        return failures == 0 ? 0 : 1;
    }
    check(asBuiltRise == mhw::GameId::Rise,
          "as built under Rise, the bars window is SEEDED FOR RISE — the "
          "host dispatched the rail's game into the window's own member");

    // ---- THE SWITCH THAT A DEAD DISPATCH CANNOT SURVIVE -----------------
    clickGameBtn(cp, mhw::StringTable::instance().tr(
                         QStringLiteral("console.game.world")));
    check(mhw::panelSettingsGame() == mhw::GameId::World,
          "the rail switch moved the NAMESPACE to World");

    const mhw::GameId afterWorld = seedOf(cp, "player-bars", found);
    check(found && afterWorld == mhw::GameId::World,
          "and moved the SEED to World too — this is the assertion that "
          "dies if setDemoGame() is ever a no-op again, which is exactly "
          "what the twelve dead overloads were");

    // ---- AND BACK, so it is not a one-way transition ---------------------
    clickGameBtn(cp, mhw::StringTable::instance().tr(
                         QStringLiteral("console.game.rise")));
    const mhw::GameId afterRise = seedOf(cp, "player-bars", found);
    check(found && afterRise == mhw::GameId::Rise,
          "switching back re-seeds for Rise — the dispatch is not a latched "
          "flag and the two directions are the same code");

    // ---- A WINDOW WHOSE DEMO DOES NOT VARY BY GAME ------------------------
    // The three damage windows inherit the no-op base and the base's World
    // answer. A test that read "World" and called it "seeded for Rise"
    // would pass on a host that dispatched nothing, so the check is that a
    // game-agnostic window is UNCHANGED across a switch - not that it
    // followed one.
    const mhw::GameId damageBefore = seedOf(cp, "damage-rows", found);
    check(found,
          "the console also built the damage-rows window (the per-player "
          "roster, which the live overlay now mounts too)");
    clickGameBtn(cp, mhw::StringTable::instance().tr(
                         QStringLiteral("console.game.world")));
    const mhw::GameId damageAfter = seedOf(cp, "damage-rows", found);
    check(found && damageBefore == damageAfter,
          "a game-agnostic window answers the same on both rails — so a "
          "test reading it cannot mistake 'never dispatched' for "
          "'dispatched to World'");

    std::printf("\n== %d checks, %d failures ==\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
