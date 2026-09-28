// SPDX-License-Identifier: Apache-2.0

// Rise REFramework management card — extracted from src/ui/control_panel.cpp
// (v0.11 K4-A1).
//
// This is a move, not a rewrite. Everything below came 1:1 out of
// ControlPanel's 6 member functions plus the `if (idx == 2)` block inside
// buildInspector(): every i18n key, every branch condition, every enablement
// expression and every objectName. The only edits on the way in are the
// mechanical ones the move forces — ControlPanel becomes `owner_`,
// `riseReframeworkCard_` becomes `card_`, `refreshRiseReframeworkStatus()`
// becomes `refresh()`, and the three `emit` sites are qualified with
// `owner_->` because the signals belong to the panel, not to this file (see
// the header for why that split was chosen).
//
// What stays behind in ControlPanel and why is written in
// rise_reframework_card.h: the request signals, the result slot, and the
// 1500 ms timer. They are the integration seam retranslateUi()/switchGame()
// reach through, so moving them would change the console's API rather than
// its file layout.

#include "ui/rise_reframework_card.h"

#include "control_panel.h"

#include "core/game_detector.h"
#include "core/steam_game_locator.h"
#include "core/string_table.h"
#include "rise/reframework/rise_reframework_manager.h"
#include "ui/viewmodel/rise_reframework_status.h"

#include <QCoreApplication>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QObject>

namespace mh {
// v0.9 i18n: local alias for the shared StringTable. Identical definition to
// panel_player.cpp / panel_monster.cpp / panel_damage.cpp and to the console
// itself (inline → ODR-safe), so the card and the panel it belongs to resolve
// their copy through one path.
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

RiseReframeworkCard::RiseReframeworkCard(ControlPanel *owner)
    : owner_(owner)
{
}

void RiseReframeworkCard::build(QWidget *contentParent, QVBoxLayout *inspectorLayout)
{
    auto *card = new QFrame(contentParent);
    card->setObjectName(QStringLiteral("riseReframeworkCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(7);

    auto *cardTitle = new QLabel();
    owner_->trSet(cardTitle, QStringLiteral("console.reframework.title"));
    cardTitle->setObjectName(QStringLiteral("riseReframeworkTitle"));
    cardLayout->addWidget(cardTitle);

    auto *cardSubtitle = new QLabel();
    owner_->trSet(cardSubtitle, QStringLiteral("console.reframework.subtitle"));
    cardSubtitle->setObjectName(QStringLiteral("riseReframeworkSubtitle"));
    cardSubtitle->setWordWrap(true);
    cardLayout->addWidget(cardSubtitle);

    auto *status = new QLabel();
    owner_->trSet(status, QStringLiteral("console.reframework.not_found"));
    status->setObjectName(QStringLiteral("riseReframeworkStatus"));
    status->setWordWrap(true);
    statusLabel_ = status;
    cardLayout->addWidget(status);

    auto *actions = new QVBoxLayout();
    actions->setSpacing(5);
    auto *install = new QPushButton();
    owner_->trSet(install, QStringLiteral("console.reframework.install"));
    install->setObjectName(QStringLiteral("installRiseReframeworkButton"));
    install->setCursor(Qt::PointingHandCursor);
    QObject::connect(install, &QPushButton::clicked, owner_,
            [this]{ requestInstall(); });
    actions->addWidget(install);
    installButton_ = install;

    auto *removeLua = new QPushButton();
    owner_->trSet(removeLua, QStringLiteral("console.reframework.remove_lua"));
    removeLua->setObjectName(QStringLiteral("removeRiseLuaButton"));
    removeLua->setCursor(Qt::PointingHandCursor);
    QObject::connect(removeLua, &QPushButton::clicked, owner_,
            [this]{ requestRemoveLua(); });
    actions->addWidget(removeLua);
    removeLuaButton_ = removeLua;

    // v0.10.10: menu-state fix. Deliberately NOT part of install — it is a
    // user-config overlay on a file REFramework shares with ~80 other
    // settings, so it stays an explicit click. Both directions are offered
    // because the point is to give the player control, not to force one
    // behaviour; the label under them reflects what the files say now.
    auto *menuHeader = new QLabel();
    owner_->trSet(menuHeader, QStringLiteral("console.reframework.menu_state_header"));
    menuHeader->setObjectName(QStringLiteral("riseReframeworkSubtitle"));
    menuHeader->setWordWrap(true);
    actions->addWidget(menuHeader);

    auto *menuState = new QLabel();
    menuState->setObjectName(QStringLiteral("riseReframeworkStatus"));
    menuState->setWordWrap(true);
    actions->addWidget(menuState);
    menuStateLabel_ = menuState;

    auto *menuFix = new QPushButton();
    owner_->trSet(menuFix, QStringLiteral("console.reframework.fix_menu_state"));
    menuFix->setObjectName(QStringLiteral("fixMenuStateButton"));
    menuFix->setCursor(Qt::PointingHandCursor);
    // trTip, not setToolTip: only trTip registers the widget with the
    // locale switcher, so a plain setToolTip would freeze it in whatever
    // language the panel happened to be built in.
    owner_->trTip(menuFix, QStringLiteral("console.reframework.fix_menu_state_tip"));
    QObject::connect(menuFix, &QPushButton::clicked, owner_,
            [this]{ requestMenuStateFix(false); });
    actions->addWidget(menuFix);
    menuStateFixButton_ = menuFix;

    auto *menuRestore = new QPushButton();
    owner_->trSet(menuRestore,
                  QStringLiteral("console.reframework.restore_menu_state"));
    menuRestore->setObjectName(QStringLiteral("restoreMenuStateButton"));
    menuRestore->setCursor(Qt::PointingHandCursor);
    owner_->trTip(menuRestore,
                  QStringLiteral("console.reframework.restore_menu_state_tip"));
    QObject::connect(menuRestore, &QPushButton::clicked, owner_,
            [this]{ requestMenuStateFix(true); });
    actions->addWidget(menuRestore);
    menuStateRestoreButton_ = menuRestore;

    auto *removeReframework = new QPushButton();
    owner_->trSet(removeReframework,
                  QStringLiteral("console.reframework.remove_reframework"));
    removeReframework->setObjectName(
        QStringLiteral("removeRiseReframeworkButton"));
    removeReframework->setCursor(Qt::PointingHandCursor);
    QObject::connect(removeReframework, &QPushButton::clicked, owner_,
            [this]{ requestRemoveFramework(); });
    actions->addWidget(removeReframework);
    removeFrameworkButton_ = removeReframework;

    cardLayout->addLayout(actions);
    card_ = card;
    card_->setVisible(owner_->currentGame() == mhw::GameId::Rise);
    inspectorLayout->addWidget(card_);
    inspectorLayout->addSpacing(12);
}

void RiseReframeworkCard::refresh()
{
    if (!card_)
        return;

    const bool riseSelected = owner_->currentGame() == mhw::GameId::Rise;
    card_->setVisible(riseSelected);
    if (!riseSelected)
        return;

    // The locator is the single source of truth for the Steam install. The
    // manager is still queried for an empty path so every refresh exercises
    // the same validation/status path; no mutation happens in this method.
    gameDir_ = mhw::findRiseInstallDir();
    const mhw::RiseReFrameworkManager manager(QCoreApplication::applicationDirPath());
    const mhw::RiseReFrameworkManager::Status status = manager.status(gameDir_);
    const bool gameRunning = mhw::detectGame().has_value();

    // The readable status lines are derived by the widget-free view model
    // (src/ui/viewmodel/rise_reframework_status.cpp), which carries the
    // derivation rules and their i18n keys so the REFramework state machine
    // is reachable from a plain logic test. The query above, the console's
    // operation flags and the game-process check are the model's whole input;
    // the join and the write below are still this card's job.
    mhw::RiseReframeworkStatusInput statusInput;
    statusInput.gameDir = gameDir_;
    statusInput.status = status;
    statusInput.gameRunning = gameRunning;
    statusInput.operationPending = operationPending_;
    statusInput.hasResult = hasResult_;
    statusInput.resultOk = resultOk_;
    statusInput.resultDetail = resultDetail_;
    const QStringList lines = mhw::riseReframeworkStatusLines(statusInput);
    statusLabel_->setText(lines.join(QLatin1Char('\n')));

    // A missing/invalid locator result is not actionable. A running game or
    // an in-flight worker is also a hard safety gate. Destructive actions are
    // enabled only when there is actually something owned to remove.
    const bool canChange = status.gameDirValid && !gameRunning
                           && !operationPending_;
    const bool usableCore =
        status.core == mhw::RiseReFrameworkManager::CoreState::Managed
        || status.core == mhw::RiseReFrameworkManager::CoreState::External;
    const bool ready = usableCore
                       && status.lua == mhw::RiseReFrameworkManager::LuaState::Current
                       && status.manifest == mhw::RiseReFrameworkManager::ManifestState::Valid;
    const bool installSafe = status.core != mhw::RiseReFrameworkManager::CoreState::Conflict
                             && status.manifest
                                    != mhw::RiseReFrameworkManager::ManifestState::Invalid;
    installButton_->setEnabled(canChange && installSafe && !ready);
    removeLuaButton_->setEnabled(
        canChange && status.lua != mhw::RiseReFrameworkManager::LuaState::Missing);
    removeFrameworkButton_->setEnabled(
        canChange
        && (status.manifest == mhw::RiseReFrameworkManager::ManifestState::Valid
            || status.lua != mhw::RiseReFrameworkManager::LuaState::Missing));
    // The menu-state fix edits REFramework's own user config. The overlay Lua
    // may legitimately be gone (it is ours to remove), and the config file
    // itself survives — but the REFramework CORE is the thing that happens to
    // read that file. With it uninstalled the key is an orphan line nothing
    // parses, so editing it would be a no-op dressed up as a fix. Gate on the
    // core, exactly like the removal buttons do.
    menuStateFixButton_->setEnabled(canChange && usableCore);

    // Report what the files actually say, and label the two actions by the
    // state they lead to rather than by fixed text — the point is that the
    // player can see which one is in effect right now.
    if (menuStateLabel_) {
        const mhw::ReFrameworkMenuStateReport menuReport =
            mhw::queryReFrameworkMenuState(gameDir_);
        const char *stateKey =
            menuReport.state == mhw::ReFrameworkMenuState::Overridden
                ? "console.reframework.menu_state_overridden"
                : menuReport.state == mhw::ReFrameworkMenuState::Default
                    ? "console.reframework.menu_state_default"
                    : "console.reframework.menu_state_unknown";
        const QString keyText = mh::tr(QString::fromLatin1(stateKey));
        // With the core gone the key is real but inert — say so rather than
        // leaving the player to read an "active" state that nothing consumes.
        const QString note =
            usableCore ? QString()
                       : QStringLiteral("\n")
                         + mh::tr(QStringLiteral(
                             "console.reframework.menu_state_no_core"));
        // Only name the paths when there is more than one: a single game-dir
        // file needs no listing, while several mean the fallback locations are
        // in play and the player should know which ones were touched.
        QString pathText;
        if (menuReport.paths.size() > 1) {
            pathText = QStringLiteral("\n")
                       + mh::tr(QStringLiteral("console.reframework.menu_state_paths"))
                             .arg(menuReport.paths.size())
                       + QStringLiteral("\n")
                       + menuReport.paths.join(QStringLiteral("\n"));
        }
        menuStateLabel_->setText(keyText + note + pathText);
        menuStateRestoreButton_->setEnabled(
            canChange && usableCore
            && menuReport.state != mhw::ReFrameworkMenuState::Default);
    }
}

void RiseReframeworkCard::requestInstall()
{
    if (owner_->currentGame() != mhw::GameId::Rise || gameDir_.isEmpty()
        || operationPending_ || mhw::detectGame().has_value()) {
        refresh();
        return;
    }
    const auto answer = QMessageBox::question(
        owner_,
        mh::tr(QStringLiteral("console.reframework.confirm_title")),
        mh::tr(QStringLiteral("console.reframework.confirm_install")).arg(gameDir_),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    operationPending_ = true;
    hasResult_ = false;
    resultDetail_.clear();
    refresh();
    if (owner_->receivers(SIGNAL(installRiseReframeworkRequested(QString))) == 0) {
        owner_->finishRiseReframeworkOperation(
            false, mh::tr(QStringLiteral("console.reframework.no_worker")));
        return;
    }
    emit owner_->installRiseReframeworkRequested(gameDir_);
}

void RiseReframeworkCard::requestRemoveLua()
{
    if (owner_->currentGame() != mhw::GameId::Rise || gameDir_.isEmpty()
        || operationPending_ || mhw::detectGame().has_value()) {
        refresh();
        return;
    }
    const auto answer = QMessageBox::question(
        owner_,
        mh::tr(QStringLiteral("console.reframework.confirm_title")),
        mh::tr(QStringLiteral("console.reframework.confirm_remove_lua")).arg(gameDir_),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    operationPending_ = true;
    hasResult_ = false;
    resultDetail_.clear();
    refresh();
    if (owner_->receivers(SIGNAL(removeRiseLuaRequested(QString))) == 0) {
        owner_->finishRiseReframeworkOperation(
            false, mh::tr(QStringLiteral("console.reframework.no_worker")));
        return;
    }
    emit owner_->removeRiseLuaRequested(gameDir_);
}

void RiseReframeworkCard::requestMenuStateFix(bool restoreDefault)
{
    // Same core gate as the button: without a REFramework core there is
    // nothing to read the key we would write, so the action is meaningless.
    const mhw::RiseReFrameworkManager manager(
        QCoreApplication::applicationDirPath());
    const mhw::RiseReFrameworkManager::Status rfStatus =
        manager.status(mhw::findRiseInstallDir());
    const bool rfCorePresent =
        rfStatus.core == mhw::RiseReFrameworkManager::CoreState::Managed
        || rfStatus.core == mhw::RiseReFrameworkManager::CoreState::External;

    if (owner_->currentGame() != mhw::GameId::Rise || gameDir_.isEmpty()
        || operationPending_ || !rfCorePresent
        || mhw::detectGame().has_value()) {
        refresh();
        return;
    }

    // Every place REFramework might read its config from — the game dir plus
    // the %APPDATA% fallback it switches to when the game dir is unreachable.
    const QStringList configPaths =
        mhw::reframeworkConfigPaths(gameDir_);
    const QString pathList = configPaths.join(QStringLiteral("\n"));
    const char *confirmKey = restoreDefault
        ? "console.reframework.confirm_restore_menu_state"
        : "console.reframework.confirm_fix_menu_state";
    const auto answer = QMessageBox::question(
        owner_,
        mh::tr(QStringLiteral("console.reframework.confirm_title")),
        mh::tr(QString::fromLatin1(confirmKey)).arg(pathList),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    // One key, a handful of files. Nothing is downloaded and no core file is
    // touched, so this runs inline — but it still routes through the shared
    // pending/result state so the card reads the same as install/removal.
    operationPending_ = true;
    hasResult_ = false;
    resultDetail_.clear();
    refresh();

    mhw::ReFrameworkMenuStateReport report;
    if (!mhw::applyReFrameworkMenuStateFix(gameDir_, restoreDefault, &report)) {
        owner_->finishRiseReframeworkOperation(
            false, mh::tr(QStringLiteral("console.reframework.fix_menu_state_failed")));
        return;
    }
    owner_->finishRiseReframeworkOperation(true, QString());
}

void RiseReframeworkCard::requestRemoveFramework()
{
    if (owner_->currentGame() != mhw::GameId::Rise || gameDir_.isEmpty()
        || operationPending_ || mhw::detectGame().has_value()) {
        refresh();
        return;
    }
    const auto answer = QMessageBox::question(
        owner_,
        mh::tr(QStringLiteral("console.reframework.confirm_title")),
        mh::tr(QStringLiteral("console.reframework.confirm_remove_reframework"))
            .arg(gameDir_),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    operationPending_ = true;
    hasResult_ = false;
    resultDetail_.clear();
    refresh();
    if (owner_->receivers(SIGNAL(removeRiseReframeworkRequested(QString))) == 0) {
        owner_->finishRiseReframeworkOperation(
            false, mh::tr(QStringLiteral("console.reframework.no_worker")));
        return;
    }
    emit owner_->removeRiseReframeworkRequested(gameDir_);
}

void RiseReframeworkCard::finishOperation(bool ok, const QString &detail)
{
    operationPending_ = false;
    hasResult_ = true;
    resultOk_ = ok;
    resultDetail_ = detail;
    refresh();
}
