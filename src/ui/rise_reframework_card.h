// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QString>

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

    // Builds the READ-ONLY status summary into `scrollLayout` (parented to
    // `sidebarParent`) — the rail's scroll column, one section below the
    // game switch and the panel list. Called from ControlPanel's constructor
    // at the rail build site; it happens once, because unlike the card this
    // block does not live in a dialog that is built on open.
    //
    // Read-only BY CONTRACT: labels only, no QPushButton may appear here.
    // The install / remove entry points live on the card in the settings
    // dialog, and a second copy in the rail would be exactly the duplicate
    // entry point the console is busy removing elsewhere. refresh() is the
    // only writer of its text, so it follows the same 1500 ms tick, the
    // same retranslateUi() and the same switchGame() as the card.
    void buildSidebarSummary(QWidget *sidebarParent, QVBoxLayout *scrollLayout);

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
    //
    // QPOINTER, AND THE WHOLE REASON IS openSettingsDialog(). The settings
    // dialog is a STACK QDialog (control_panel.cpp: openSettingsDialog)
    // that outlives build() but not exec(): the card is built into it,
    // exec() returns, the dialog's destructor runs and every handle here
    // becomes a dangling raw pointer — while riseReframeworkRefreshTimer_
    // keeps calling refresh() every 1500 ms. The old `if (!card_) return`
    // was a null check against a freed address, i.e. no check at all.
    //
    // QPointer watches the QObject: when the dialog deletes its children
    // the pointer self-clears, and refresh()'s card_ test becomes a real
    // "is the card still on screen" question. Every other handle is a
    // CHILD of card_ (the card's own layout owns them), so card_.isNull()
    // is also the proof that they are gone; they are QPointer too so a
    // future re-parent cannot silently resurrect the raw-pointer bug.
    QPointer<QFrame>     card_;
    QPointer<QLabel>     statusLabel_;
    QPointer<QPushButton> installButton_;
    QPointer<QPushButton> removeLuaButton_;
    QPointer<QLabel>     menuStateLabel_;
    QPointer<QPushButton> menuStateFixButton_;
    QPointer<QPushButton> menuStateRestoreButton_;
    QPointer<QPushButton> removeFrameworkButton_;

    // Rail (sidebar) summary handles — built once by buildSidebarSummary()
    // into ControlPanel's rail scroll column. QPointer for the same reason as
    // above: they are plain widgets in the console tree, but refresh() runs
    // from a timer that outlives anything a future refactor might delete.
    QPointer<QWidget> sideBlock_;        // title + summary container
    QPointer<QLabel>  sideStatusLabel_;  // the composed status text

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
