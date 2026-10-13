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
#include <QElapsedTimer>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QString>
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

    std::printf("%s\n", failures == 0 ? "RISE-RF SMOKE PASS" : "RISE-RF SMOKE FAIL");
    return failures == 0 ? 0 : 1;
}
