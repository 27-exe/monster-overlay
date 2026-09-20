// Smoke test: the Rise-only REFramework management card in the console.
//
// What this pins down (the runtime promises of the Damage-inspector card):
//   * Rise mode: the card is built, visible, and carries the three action
//     buttons plus a non-empty status line;
//   * World mode: the same card and its buttons stay hidden;
//   * construction/destruction with the 1.5 s status timer running does not
//     crash or write anything outside the redirected XDG_CONFIG_HOME.
//
// Read-only against the real Steam tree: the locator and the manager status
// only inspect files. Nothing under the game directory is ever written.
// Set MONSTER_SMOKE_SNAP=<path.png> to additionally render the console with
// the card scrolled into view (manual inspection).

#include <QApplication>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTemporaryDir>
#include <QString>

#include <cstdio>

#include "core/game_detector.h"
#include "core/rise_reframework_manager.h"
#include "core/steam_game_locator.h"
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

// The ctor selects inspector idx 0 (Player); key "3" selects idx 2 (Damage),
// the page that owns the Rise REFramework card.
void selectDamageInspector(ControlPanel *cp)
{
    QKeyEvent event(QEvent::KeyPress, Qt::Key_3, Qt::NoModifier);
    QCoreApplication::sendEvent(cp, &event);
    QCoreApplication::processEvents();
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

struct CardHandles {
    QFrame *card = nullptr;
    QPushButton *install = nullptr;
    QPushButton *removeLua = nullptr;
    QPushButton *removeReframework = nullptr;
    QLabel *status = nullptr;
};

CardHandles handles(ControlPanel *cp)
{
    CardHandles h;
    h.card = cp->findChild<QFrame *>(QStringLiteral("riseReframeworkCard"));
    h.install = cp->findChild<QPushButton *>(QStringLiteral("installRiseReframeworkButton"));
    h.removeLua = cp->findChild<QPushButton *>(QStringLiteral("removeRiseLuaButton"));
    h.removeReframework = cp->findChild<QPushButton *>(QStringLiteral("removeRiseReframeworkButton"));
    h.status = cp->findChild<QLabel *>(QStringLiteral("riseReframeworkStatus"));
    return h;
}

void writeSnapshotIfRequested(ControlPanel *cp, QFrame *card)
{
    const QString path = qEnvironmentVariable("MONSTER_SMOKE_SNAP");
    if (path.isEmpty() || card == nullptr)
        return;

    // The card sits at the bottom of the Damage inspector's scroll area; make
    // sure it is inside the viewport before the grab.
    for (QScrollArea *scroll : cp->findChildren<QScrollArea *>()) {
        if (scroll->isAncestorOf(card)) {
            scroll->ensureWidgetVisible(card);
            QCoreApplication::processEvents();
            break;
        }
    }
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
        selectDamageInspector(&cp);

        const CardHandles h = handles(&cp);
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

        // v0.10.1: companion surfaces are Rise-only.
        auto *navPets = cp.findChild<QFrame *>(QStringLiteral("navPets"));
        check(navPets != nullptr, "rise: pets rail card exists");
        check(navPets && navPets->isVisible(), "rise: pets rail card visible");
        auto *canvas = cp.findChild<HudCanvas *>();
        check(canvas && canvas->panelPresent(3), "rise: pets stage tile present");
        writeSnapshotIfRequested(&cp, h.card);

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
        selectDamageInspector(&cp);

        const CardHandles h = handles(&cp);
        check(h.card != nullptr, "world: card exists");
        check(h.card && !h.card->isVisible(), "world: card hidden");
        check(h.install && !h.install->isVisible(), "world: install button hidden");
        check(h.removeLua && !h.removeLua->isVisible(), "world: remove-Lua button hidden");
        check(h.removeReframework && !h.removeReframework->isVisible(),
              "world: remove-button hidden");

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
