// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>

class QFrame;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;
class ControlPanel;

// Rise console card — the Rise-only REFramework management block in the
// inspector (v0.11 K4-A1).
//
// It used to be 6 members of ControlPanel plus 9 widget pointers bolted onto
// that class: the whole GUI-thread state machine that decides whether the
// install / remove-Lua / remove-REFramework / menu-state buttons are enabled,
// and the confirm-via-QMessageBox that turns a click into one of the three
// request signals. Everything that is not a request signal moved here 1:1.
//
// What deliberately did NOT move, and why:
//   * the three request signals and the finishRiseReframeworkOperation() slot
//     stay on ControlPanel. They are the integration seam — src/main_control.cpp
//     connects them to its own worker/fetcher and calls the slot back. Moving
//     them would change the integration surface instead of just moving code
//     out of a file.
//   * the 1500 ms refresh timer stays on ControlPanel. retranslateUi() and
//     switchGame() both call refresh() on their own, independent of the tick,
//     so the timer is not the card's lifecycle to own.
// This class owns the presentation, not the wiring.
//
// Shape: NOT a QObject and NOT a QWidget — a plain collaborator that builds
// widgets into the owner's layout and holds their pointers, like
// mhw::OverlayProcessController and the ViewModels. It needs no signal of its
// own because every signal it wants to emit belongs to its owner.
//
// The card reaches ControlPanel's private surface through an owner pointer:
// trSet()/trTip() to register its widgets with the console's i18n replay
// registry (retranslateUi() has no other way to find them), currentGame(),
// and QObject::receivers() to notice that no worker is connected. That is
// why ControlPanel befriends it rather than widening its own API to public
// for one caller. Note the direction of the include: this header only
// forward-declares ControlPanel, so rise_reframework_card.cpp includes
// control_panel.h and there is no cycle.
class RiseReframeworkCard {
public:
    explicit RiseReframeworkCard(ControlPanel *owner);

    // Builds the card into `inspectorLayout` (parented to `contentParent`)
    // and grabs every handle it needs to re-label itself later. Called from
    // ControlPanel::buildInspector() when it reaches the Rise page (idx 2),
    // exactly where the block used to be inlined.
    void build(QWidget *contentParent, QVBoxLayout *inspectorLayout);

    // Re-reads the filesystem, refreshes the status lines and re-enables every
    // button. Safe to call from the refresh timer, from retranslateUi() and
    // from switchGame() alike.
    void refresh();

    // The four user actions. Each confirms with a QMessageBox and then either
    // emits one of the owner's request signals or reports a failure inline
    // through the owner's result slot, exactly as before the split.
    void requestInstall();
    void requestRemoveLua();
    void requestMenuStateFix(bool restoreDefault);
    void requestRemoveFramework();

    // Where the owner's slot forwards the async worker's outcome. Kept
    // separate from refresh() because it is what completes the pending/result
    // bookkeeping refresh() then re-renders.
    void finishOperation(bool ok, const QString &detail);

private:
    ControlPanel *owner_ = nullptr;

    // Widget handles — the nine that used to hang off ControlPanel. The card
    // is their only reader.
    QFrame     *card_              = nullptr;
    QLabel     *statusLabel_       = nullptr;
    QPushButton *installButton_     = nullptr;
    QPushButton *removeLuaButton_   = nullptr;
    QLabel     *menuStateLabel_    = nullptr;
    QPushButton *menuStateFixButton_    = nullptr;
    QPushButton *menuStateRestoreButton_ = nullptr;
    QPushButton *removeFrameworkButton_ = nullptr;

    // Result of the last refresh()'s locator call: the Steam install dir.
    // Kept because every request path gates on it.
    QString gameDir_;

    // Console-side operation bookkeeping: at most one in-flight worker run,
    // plus the result of the last completed one. Mirrors the clock
    // ControlPanel used to keep for exactly this purpose.
    bool    operationPending_ = false;
    bool    hasResult_        = false;
    bool    resultOk_         = false;
    QString resultDetail_;
};
