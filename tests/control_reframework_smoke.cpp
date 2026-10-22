// Smoke test: the Rise-only REFramework management card in the console.
//
// WHAT THIS PINS DOWN (the runtime promises of the REFramework card):
//   * Rise mode: the card is built INSIDE THE SETTINGS DIALOG, visible,
//     and carries the three action buttons plus a non-empty status line;
//   * World mode: the same card and its buttons stay hidden (the
//     prototype hides its whole #rise-settings section for a non-Rise
//     game, reference.html:502);
//   * the request path is the REAL one — a click opens the card's own
//     confirmation box and, with no worker connected, the console reports
//     `console.reframework.no_worker` without touching a single file;
//   * construction/destruction with the 1.5 s status timer running does
//     not crash — the card's handles are QPointer now, so closing the
//     dialog must leave the tick reading "no card" instead of a freed
//     address (the crash this used to be);
//   * nothing outside the redirected XDG_CONFIG_HOME is ever written.
//
// ALSO COVERS (user-8, the rail status summary):
//   * Rise mode: the console's RAIL carries a read-only REFramework/Lua
//     summary (objectName riseReframeworkSide / riseReframeworkSideStatus)
//     with no action buttons in it — the entry points stay on the card;
//   * World mode: that same block is hidden, like the card itself;
//   * MUTATION: with HOME pointed at a QTemporaryDir, the summary reads
//     "not installed" for both the core and the Lua, then — after a
//     dinput8.dll and the byte-identical packaged Lua are placed into the
//     fake install — flips to "available"/"installed" on the card's own
//     1.5 s tick, without any manual refresh call. The probes therefore
//     never leave that temporary directory.
//
// WHY THE ASSERTIONS LIVE INSIDE openSettingsDialog()'s exec().
// The card used to be built into the Damage inspector page; v1.0.0 moved
// it to settings, which is the ONLY place reference.html has it
// (#rise-settings, reference.html:349). The settings dialog is a stack
// QDialog driven by a BLOCKING exec(), so the test schedules its body on
// a zero timer BEFORE clicking the settings button: the timer can only
// fire inside exec()'s event loop — i.e. exactly while the dialog is on
// screen — and closing the dialog there returns from exec(), from the
// click, and from the helper.
//
// Read-only against the real Steam tree: the locator and the manager
// status only inspect files. Nothing under the game directory is ever
// written, including by the button click above (the no-worker guard
// fires before any request signal is emitted).
// Set MONSTER_SMOKE_SNAP=<path.png> to additionally render the console
// (manual inspection).

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

#include "core/game_detector.h"
#include "rise/reframework/rise_reframework_manager.h"
#include "core/steam_game_locator.h"
#include "rise/rise_install_locator.h"
#include "core/string_table.h"
#include "ui/control_panel.h"
#include "ui/hud_canvas.h"

namespace {

int failures = 0;

void check(bool ok, const char *what, const QString &detail = QString())
{
    std::printf("%s: %s", ok ? "PASS" : "FAIL", what);
    if (!detail.isEmpty())
        std::printf(" -- %s", qPrintable(detail));
    std::printf("\n");
    if (!ok)
        ++failures;
}

// Clicks the rail's game button whose label matches `key` (same lookup the
// L2 smoke test uses: objectName "gameBtn" + localized text).
void clickGameButton(ControlPanel *cp, const QString &key)
{
    const QString want = mhw::StringTable::instance().tr(key);
    for (QPushButton *button : cp->findChildren<QPushButton *>()) {
        if (button->objectName() == QStringLiteral("gameBtn") && button->text() == want) {
            button->click();
            QCoreApplication::processEvents();
            return;
        }
    }
}

void seedGame(const char *game)
{
    QSettings settings;
    settings.setValue(QStringLiteral("game"), QString::fromLatin1(game));
    settings.sync();
}

// Opens the console's settings dialog and runs `body` while it is on
// screen, then closes it. See the file header for why the body is a
// timer callback rather than a straight call.
void withSettings(ControlPanel *cp, const std::function<void(QDialog *)> &body)
{
    QTimer::singleShot(0, cp, [cp, body]{
        QDialog *dialog = nullptr;
        for (QWidget *top : QApplication::topLevelWidgets()) {
            auto *candidate = qobject_cast<QDialog *>(top);
            if (candidate
                && candidate->objectName() == QStringLiteral("settingsDialog")) {
                dialog = candidate;
                break;
            }
        }
        if (!dialog) {
            std::printf("FAIL: settings dialog did not appear\n");
            ++failures;
            return;
        }
        body(dialog);
        dialog->close();
    });
    for (QPushButton *button : cp->findChildren<QPushButton *>()) {
        if (button->objectName() == QStringLiteral("settingsBtn")) {
            button->click();          // blocks in exec() until the timer closes it
            QCoreApplication::processEvents();
            return;
        }
    }
    std::printf("FAIL: settings button not found\n");
    ++failures;
}

// Clicks a card button while answering its confirmation box.
//
// QMessageBox::question runs a NESTED event loop, and a zero timer
// scheduled just before the click can only fire inside that loop — so
// the answer lands while the box is on screen, on the same thread, with
// nothing in between. Answering Yes is safe even with a real install on
// disk: the card emits one of ControlPanel's request signals, and with
// no worker connected the slot reports `no_worker` without touching the
// filesystem.
void clickWithBoxAnswer(QPushButton *button, bool accept)
{
    QTimer::singleShot(0, qApp, [accept]{
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            bool handled = false;
            for (QWidget *top : QApplication::topLevelWidgets()) {
                auto *box = qobject_cast<QMessageBox *>(top);
                if (!box || !box->isVisible())
                    continue;
                QAbstractButton *answer =
                    box->button(accept ? QMessageBox::Yes : QMessageBox::No);
                if (answer)
                    answer->click();
                else
                    box->close();
                handled = true;
                break;
            }
            if (handled)
                return;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
    });
    button->click();
}

// Waits `ms` of wall clock while pumping events — long enough for the
// console's 1.5 s REFramework status tick to run at least once with the
// dialog closed.
void pumpFor(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

struct CardHandles {
    QFrame *card = nullptr;
    QPushButton *install = nullptr;
    QPushButton *removeLua = nullptr;
    QPushButton *removeReframework = nullptr;
    QLabel *status = nullptr;
};

// The card lives in the settings dialog, so the search root is the DIALOG
// — not the console. (The dialog is a top-level window, not a child of
// the console, so a findChild() on the console would legitimately find
// nothing.)
CardHandles handles(QWidget *root)
{
    CardHandles h;
    h.card = root->findChild<QFrame *>(QStringLiteral("riseReframeworkCard"));
    h.install = root->findChild<QPushButton *>(QStringLiteral("installRiseReframeworkButton"));
    h.removeLua = root->findChild<QPushButton *>(QStringLiteral("removeRiseLuaButton"));
    h.removeReframework = root->findChild<QPushButton *>(QStringLiteral("removeRiseReframeworkButton"));
    h.status = root->findChild<QLabel *>(QStringLiteral("riseReframeworkStatus"));
    return h;
}

void writeSnapshotIfRequested(ControlPanel *cp)
{
    const QString path = qEnvironmentVariable("MONSTER_SMOKE_SNAP");
    if (path.isEmpty())
        return;
    const bool saved = cp->grab().save(path);
    std::printf("%s: snapshot %s\n", saved ? "INFO" : "FAIL", qPrintable(path));
    if (!saved)
        ++failures;
}

} // namespace

int main(int argc, char *argv[])
{
    // Keep every config write inside this sandbox and render offscreen; both
    // must be in place before the QApplication reads them.
    QTemporaryDir configRoot;
    qputenv("XDG_CONFIG_HOME", configRoot.path().toLocal8Bit());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationName(QStringLiteral("monster-control"));
    QApplication::setApplicationVersion(QStringLiteral("9.9.9-test"));

    // ---- Rise mode: card visible with all three actions -------------------
    seedGame("rise");
    {
        ControlPanel cp;
        cp.show();
        QCoreApplication::processEvents();

        // v1.0.0 (A): THE CARD IS NOT IN THE INSPECTOR ANY MORE. The old
        // Damage page is a hidden controller now, so nothing here drives
        // an inspector page at all: the settings entry is the user path.
        check(cp.findChild<QFrame *>(QStringLiteral("riseReframeworkCard")) == nullptr,
              "rise: no card is built into the console's own tree");

        // ---- user-8: the RAIL carries the read-only status summary ----------
        // Built by RiseReframeworkCard::buildSidebarSummary() into the rail's
        // scroll column, refreshed by the SAME 1.5 s tick as the card.
        auto *sideBlock =
            cp.findChild<QWidget *>(QStringLiteral("riseReframeworkSide"));
        check(sideBlock != nullptr, "rise: rail summary block exists");
        check(sideBlock && sideBlock->isVisible(), "rise: rail summary visible in Rise");
        auto *sideStatus = cp.findChild<QLabel *>(
            QStringLiteral("riseReframeworkSideStatus"));
        check(sideStatus != nullptr, "rise: rail summary label exists");
        check(sideStatus && !sideStatus->text().trimmed().isEmpty(),
              "rise: rail summary has text",
              sideStatus ? sideStatus->text() : QString());
        // Locale-independent wording checks: line 1 always names the product.
        check(sideStatus && sideStatus->text().contains(QStringLiteral("REFramework")),
              "rise: rail summary names REFramework",
              sideStatus ? sideStatus->text() : QString());
        // Line 2 (the Lua state) only exists once the locator found a
        // directory to look in — so the expectation follows the locator,
        // exactly like the enablement assertions below do.
        const bool riseDirFound = !mhw::findRiseInstallDir().isEmpty();
        check(sideStatus
                  && (sideStatus->text().contains(QStringLiteral("Lua")) == riseDirFound),
              "rise: rail summary names the Lua script exactly when a Rise install exists",
              QStringLiteral("found=%1 text=%2")
                  .arg(riseDirFound)
                  .arg(sideStatus ? sideStatus->text() : QString()));
        // Read-only contract: NOT ONE action button anywhere in the block —
        // the entry points stay on the settings card.
        check(sideBlock && sideBlock->findChildren<QPushButton *>().isEmpty(),
              "rise: rail summary carries no buttons");
        // Same section caption objectName as the rail's 游戏 / 面板 captions,
        // so it is registered with the console's i18n replay.
        check(sideBlock
                  && sideBlock->findChild<QLabel *>(QStringLiteral("sideLabel")) != nullptr,
              "rise: rail summary has the rail section caption");

        bool checked = false;
        withSettings(&cp, [&](QDialog *dialog){
            checked = true;
            const CardHandles h = handles(dialog);
            check(h.card != nullptr, "rise: card exists");
            check(h.card && h.card->isVisible(), "rise: card is visible");
            check(h.install != nullptr, "rise: install button exists");
            check(h.install && h.install->isVisible(), "rise: install button visible");
            check(h.install && !h.install->text().isEmpty(), "rise: install button labelled");
            check(h.removeLua && h.removeLua->isVisible(), "rise: remove-Lua button visible");
            check(h.removeReframework && h.removeReframework->isVisible(),
                  "rise: remove-Lua+REFramework button visible");
            check(h.status != nullptr, "rise: status label exists");
            check(h.status && !h.status->text().isEmpty(), "rise: status label has text",
                  h.status ? h.status->text() : QString());

            // Enablement contract follows the detected state: install/repair is
            // offered only when needed; destructive actions require an owned
            // target. This catches the real "managed core + removed Lua" state.
            const QString installDir = mhw::findRiseInstallDir();
            const bool canChange = !installDir.isEmpty() && !mhw::detectGame().has_value();
            const mhw::RiseReFrameworkManager manager(
                QCoreApplication::applicationDirPath());
            const auto rfStatus = manager.status(installDir);
            const bool usableCore =
                rfStatus.core == mhw::RiseReFrameworkManager::CoreState::Managed
                || rfStatus.core == mhw::RiseReFrameworkManager::CoreState::External;
            const bool ready = usableCore
                && rfStatus.lua == mhw::RiseReFrameworkManager::LuaState::Current
                && rfStatus.manifest
                       == mhw::RiseReFrameworkManager::ManifestState::Valid;
            const bool installSafe =
                rfStatus.core != mhw::RiseReFrameworkManager::CoreState::Conflict
                && rfStatus.manifest
                       != mhw::RiseReFrameworkManager::ManifestState::Invalid;
            check(h.install && h.install->isEnabled() == (canChange && installSafe && !ready),
                  "rise: install button follows availability",
                  QStringLiteral("enabled=%1 expected=%2")
                      .arg(h.install && h.install->isEnabled())
                      .arg(canChange && installSafe && !ready));
            check(h.removeLua
                      && h.removeLua->isEnabled()
                             == (canChange
                                 && rfStatus.lua
                                        != mhw::RiseReFrameworkManager::LuaState::Missing),
                  "rise: remove-Lua button follows availability");
            check(h.removeReframework
                      && h.removeReframework->isEnabled()
                             == (canChange
                                 && (rfStatus.manifest
                                         == mhw::RiseReFrameworkManager::ManifestState::Valid
                                     || rfStatus.lua
                                            != mhw::RiseReFrameworkManager::LuaState::Missing)),
                  "rise: remove-both button follows availability");
            if (!installDir.isEmpty() && h.status)
                check(h.status->text().contains(installDir),
                      "rise: status line names the located install directory");

            // ---- the worker guard, through the real button -----------------
            // No worker is connected in this test process, so a request that
            // gets as far as the confirmation box must come back as
            // `no_worker` — the console's own failure line — and must not
            // write anywhere. When the request is gated BEFORE any box (no
            // install dir found, or the game is running) the button is
            // disabled, the click does nothing, and the status keeps its
            // previous text.
            const QString noWorker =
                mhw::StringTable::instance().tr(
                    QStringLiteral("console.reframework.no_worker"));
            const QString before = h.status ? h.status->text() : QString();
            if (h.install && h.install->isEnabled()) {
                clickWithBoxAnswer(h.install, true);
                QCoreApplication::processEvents();
                check(h.status && h.status->text().contains(noWorker),
                      "rise: a request with no worker reports no_worker",
                      h.status ? h.status->text() : QString());
                check(!noWorker.isEmpty(),
                      "rise: the no_worker line is translated");
            } else {
                check(before == (h.status ? h.status->text() : QString()),
                      "rise: a gated request changes nothing");
            }
        });
        check(checked, "rise: settings dialog opened");

        // ---- lifecycle: the 1.5 s tick must survive a closed dialog --------
        // The dialog is destroyed when exec() returns; the console's
        // riseReframeworkRefreshTimer_ keeps running. With raw pointers
        // this was a use-after-free on the very next tick.
        withSettings(&cp, [](QDialog *){});
        pumpFor(2000);
        check(cp.findChild<QFrame *>(QStringLiteral("riseReframeworkCard")) == nullptr,
              "rise: after close+tick the console tree still has no card");

        // v0.10.1: companion surfaces are Rise-only.
        auto *navPets = cp.findChild<QFrame *>(QStringLiteral("navPets"));
        check(navPets != nullptr, "rise: pets rail card exists");
        check(navPets && navPets->isVisible(), "rise: pets rail card visible");
        auto *canvas = cp.findChild<HudCanvas *>();
        check(canvas && canvas->panelPresent(3), "rise: pets stage tile present");
        writeSnapshotIfRequested(&cp);

        // v0.10.1 hot-switch: with the pets inspector OPEN in Rise, clicking
        // World must remove the whole companion surface and fall the console
        // back to another inspector.
        {
            QKeyEvent petsKey(QEvent::KeyPress, Qt::Key_4, Qt::NoModifier);
            QCoreApplication::sendEvent(&cp, &petsKey);
            QCoreApplication::processEvents();
            auto *navPetsSel = cp.findChild<QFrame *>(QStringLiteral("navPets"));
            check(navPetsSel && navPetsSel->property("selected").toBool(),
                  "rise: hot-key 4 selects the pets inspector");

            clickGameButton(&cp, QStringLiteral("console.game.world"));
            auto *navPetsWorld = cp.findChild<QFrame *>(QStringLiteral("navPets"));
            auto *navDamage = cp.findChild<QFrame *>(QStringLiteral("navDamage"));
            auto *canvasWorld = cp.findChild<HudCanvas *>();
            check(navPetsWorld && !navPetsWorld->isVisible(),
                  "switch rise->world: pets rail card hidden");
            check(canvasWorld && !canvasWorld->panelPresent(3),
                  "switch rise->world: pets stage tile removed");
            check(navDamage && navDamage->property("selected").toBool(),
                  "switch rise->world: inspector falls back away from pets");
        }
    }

    // ---- World mode: the same card must stay hidden ----------------------
    seedGame("world");
    {
        ControlPanel cp;
        cp.show();
        QCoreApplication::processEvents();

        bool checked = false;
        withSettings(&cp, [&](QDialog *dialog){
            checked = true;
            // The prototype hides the whole #rise-settings section for a
            // non-Rise game; the card is still BUILT (there is exactly one
            // build site now) and refreshed into the hidden state, which is
            // the contract the old Damage page kept.
            const CardHandles h = handles(dialog);
            check(h.card != nullptr, "world: card exists");
            check(h.card && !h.card->isVisible(), "world: card hidden");
            check(h.install && !h.install->isVisible(), "world: install button hidden");
            check(h.removeLua && !h.removeLua->isVisible(), "world: remove-Lua button hidden");
            check(h.removeReframework && !h.removeReframework->isVisible(),
                  "world: remove-button hidden");
        });
        check(checked, "world: settings dialog opened");

        // ---- user-8: World must not show the REFramework summary ------------
        // The block is BUILT once in the constructor (there is one build
        // site, like the card) and gated on the game switch by refresh().
        auto *sideBlockWorld =
            cp.findChild<QWidget *>(QStringLiteral("riseReframeworkSide"));
        check(sideBlockWorld != nullptr,
              "world: rail summary block still exists in the tree");
        check(sideBlockWorld && !sideBlockWorld->isVisible(),
              "world: rail summary hidden in World");
        auto *sideStatusWorld = cp.findChild<QLabel *>(
            QStringLiteral("riseReframeworkSideStatus"));
        check(sideStatusWorld && !sideStatusWorld->isVisible(),
              "world: rail summary text hidden in World");

        // v0.10.1: selecting World removes the whole companion surface.
        auto *navPets = cp.findChild<QFrame *>(QStringLiteral("navPets"));
        check(navPets != nullptr, "world: pets rail card exists");
        check(navPets && !navPets->isVisible(), "world: pets rail card hidden");
        auto *canvas = cp.findChild<HudCanvas *>();
        check(canvas && !canvas->panelPresent(3), "world: pets stage tile removed");

        // Hot-key 4 must not reach the hidden Rise-only inspector in World.
        QKeyEvent damageKey(QEvent::KeyPress, Qt::Key_3, Qt::NoModifier);
        QCoreApplication::sendEvent(&cp, &damageKey);
        QCoreApplication::processEvents();
        QKeyEvent petsKey(QEvent::KeyPress, Qt::Key_4, Qt::NoModifier);
        QCoreApplication::sendEvent(&cp, &petsKey);
        QCoreApplication::processEvents();
        auto *navDamage = cp.findChild<QFrame *>(QStringLiteral("navDamage"));
        check(navDamage && navDamage->property("selected").toBool(),
              "world: hot-key 4 cannot select the pets inspector");
    }

    // ---- user-8: MUTATION — the rail summary must follow the filesystem -----
    //
    // HOME is pointed at a QTemporaryDir BEFORE the console below is built,
    // so findRiseInstallDir() (defaultSteamRoots → QDir::homePath()) can only
    // ever see the fake Steam library assembled here, and the menu-state read
    // (reframeworkConfigPaths → QDir::home()) only the fake home. The probes
    // are read-only: nothing outside this directory is written by this block,
    // not by the console and not by the 1.5 s tick.
    {
        QTemporaryDir fakeHome;
        check(fakeHome.isValid(), "mutate: sandbox home created");
        qputenv("HOME", fakeHome.path().toLocal8Bit());
        qunsetenv("STEAM_COMPAT_DATA_PATH");

        const QString steamRoot =
            fakeHome.path() + QStringLiteral("/.local/share/Steam");
        const QString gameDir =
            steamRoot + QStringLiteral("/steamapps/common/MonsterHunterRise");
        check(QDir().mkpath(gameDir), "mutate: fake install directory created");
        {
            QFile acf(steamRoot + QStringLiteral("/steamapps/appmanifest_1446780.acf"));
            check(acf.open(QIODevice::WriteOnly | QIODevice::Text),
                  "mutate: Steam manifest written");
            acf.write("\"AppState\"\n{\n\t\"installdir\"\t\t\"MonsterHunterRise\"\n}\n");
        }
        {
            QFile exe(QDir(gameDir).filePath(QStringLiteral("MonsterHunterRise.exe")));
            check(exe.open(QIODevice::WriteOnly), "mutate: game exe stub written");
            exe.write("MZ");
        }
        const QString located = mhw::findRiseInstallDir();
        check(!located.isEmpty() && located == QDir(gameDir).canonicalPath(),
              "mutate: the locator resolves ONLY the fake install",
              QStringLiteral("located=%1 canonical=%2")
                  .arg(located, QDir(gameDir).canonicalPath()));

        seedGame("rise");
        ControlPanel cp;
        cp.show();
        QCoreApplication::processEvents();

        const auto trKey = [](const char *key) {
            return mhw::StringTable::instance().tr(QString::fromLatin1(key));
        };
        const auto summaryText = [&cp]() -> QString {
            QLabel *label =
                cp.findChild<QLabel *>(QStringLiteral("riseReframeworkSideStatus"));
            return label ? label->text() : QString();
        };

        // State 1 — a valid install with NOTHING installed yet.
        const QString before = summaryText();
        const QStringList beforeLines = before.split(QLatin1Char('\n'));
        check(!before.isEmpty() && beforeLines.size() == 2,
              "mutate(1): exactly two lines with no core and no Lua", before);
        // Exact line equality (the label holds the composed string verbatim),
        // so the two lines cannot pass by matching each other's wording.
        const QString expectRf1 =
            trKey("console.reframework.side_rf")
                .arg(trKey("console.reframework.side_rf_missing"));
        const QString expectLua1 =
            trKey("console.reframework.side_lua")
                .arg(trKey("console.reframework.side_lua_missing"));
        check(beforeLines.value(0) == expectRf1,
              "mutate(1): REFramework line reads 'not installed'",
              QStringLiteral("got=[%1] want=[%2]").arg(beforeLines.value(0), expectRf1));
        check(beforeLines.value(1) == expectLua1,
              "mutate(1): Lua line reads 'not installed'",
              QStringLiteral("got=[%1] want=[%2]").arg(beforeLines.value(1), expectLua1));
        check(!before.contains(QStringLiteral("mhr-overlay-damage.lua")),
              "mutate(1): no script named while none is installed", before);
        check(!before.contains(trKey("console.reframework.menu_state_unknown")),
              "mutate(1): no menu line without a core to read it", before);

        // State 2 — put a core and the byte-identical packaged Lua on disk.
        {
            QFile core(QDir(gameDir).filePath(QStringLiteral("dinput8.dll")));
            check(core.open(QIODevice::WriteOnly), "mutate(2): core dll written");
            core.write("stub-core");
            const QString autorun =
                QDir(gameDir).filePath(QStringLiteral("reframework/autorun"));
            check(QDir().mkpath(autorun), "mutate(2): autorun directory created");
            const QString source =
                QCoreApplication::applicationDirPath()
                + QStringLiteral("/reframework/autorun/mhr-overlay-damage.lua");
            const QString target = autorun + QStringLiteral("/mhr-overlay-damage.lua");
            QFile::remove(target);
            check(QFile::copy(source, target),
                  "mutate(2): packaged Lua copied verbatim", source);
        }

        // NO manual refresh call anywhere: the console's own 1.5 s tick is the
        // only thing allowed to notice the new files (bounded wait for it).
        QString after = before;
        QStringList afterLines = beforeLines;
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < 6000) {
            pumpFor(400);
            after = summaryText();
            afterLines = after.split(QLatin1Char('\n'));
            if (afterLines.size() >= 3
                && afterLines.at(1).contains(
                    trKey("console.reframework.side_lua_installed")))
                break;
        }
        check(after != before,
              "mutate(2): the summary changed with the filesystem",
              QStringLiteral("before=[%1] after=[%2]").arg(before, after));
        check(afterLines.size() == 3,
              "mutate(2): three lines once a core exists", after);
        const QString expectRf2 =
            trKey("console.reframework.side_rf").arg(trKey("console.reframework.side_rf_ok"));
        const QString expectLua2 =
            trKey("console.reframework.side_lua")
                .arg(trKey("console.reframework.side_lua_installed"));
        check(afterLines.value(0) == expectRf2,
              "mutate(2): REFramework line reads 'available'",
              QStringLiteral("got=[%1] want=[%2]").arg(afterLines.value(0), expectRf2));
        check(afterLines.value(1) == expectLua2,
              "mutate(2): Lua line reads 'installed' and names the script",
              QStringLiteral("got=[%1] want=[%2]").arg(afterLines.value(1), expectLua2));
        check(afterLines.value(1).contains(QStringLiteral("mhr-overlay-damage.lua")),
              "mutate(2): the Lua line names the installed script", after);
        check(afterLines.value(2) == trKey("console.reframework.menu_state_unknown"),
              "mutate(2): the menu line appears once a core exists",
              QStringLiteral("got=[%1]").arg(afterLines.value(2)));
    }

    std::printf("%s\n", failures == 0 ? "RISE-RF SMOKE PASS" : "RISE-RF SMOKE FAIL");
    return failures == 0 ? 0 : 1;
}
