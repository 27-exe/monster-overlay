#pragma once

// Overlay subprocess lifecycle Controller — the process-management half of
// ControlPanel, extracted 1:1 from it (no re-derivation, no rename).
//
// ControlPanel grew to ~3000 lines mixing six responsibilities. This class
// takes exactly one of them: the lifecycle of the monster-overlay child
// process (launch / stop / game hot-swap / liveness poll) plus the state
// that drives it — overlayPid_, the 250 ms kill(pid,0) poll timer,
// pendingRestart_ and currentGame_ (the value passed as --game).
//
// Hard rule, mirroring DamageViewModel: this header and its .cpp carry NO
// widget or painter dependency. Only QtCore is used (QObject / QString /
// QStringList / QTimer). All drawing and widget styling lives in
// control_panel.cpp; the panel connects to the signals below and updates
// its UI in the slots. QProcess/QTimer are allowed because process
// management is the whole point of this type.

#include "core/game_snapshot.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <QtGlobal>

namespace mhw {

class OverlayProcessController : public QObject {
    Q_OBJECT

public:
    explicit OverlayProcessController(QObject *parent = nullptr);

    // ---- state accessors (the former ControlPanel private members) ----
    [[nodiscard]] qint64 overlayPid() const { return overlayPid_; }
    [[nodiscard]] bool   isRunning() const { return overlayPid_ != 0; }
    [[nodiscard]] mhw::GameId currentGame() const { return currentGame_; }

    // ---- actions (same semantics as the functions they replaced) ----

    // Cold start. `argv` is the argument list the caller (the View) built
    // from its own widget state, EXCEPT `--game`, which this controller
    // appends itself from currentGame_. Returns false when a launch was
    // refused because a child is already running, or when
    // QProcess::startDetached() failed — in both cases no process was
    // started and no state changed.
    bool launch(const QStringList &argv, bool editMode);

    // SIGTERM the child and let the poll observe the exit. Safe to call
    // when nothing is running.
    void stop();

    // Select the game that is passed to the next child as `--game`. When
    // the game changes while a child is alive, the child is killed and
    // relaunched once the poll observes the exit (hot-swap).
    void setCurrentGame(mhw::GameId game);

    // v0.6 Phase 5: relaunch used by the hot-swap path. The View saves
    // whatever it needs to save, then calls this.
    void restartWithCurrentGame();

signals:
    // The child's liveness flipped. The View re-styles the START/STOP
    // button, the status badge and the two launcher buttons here.
    void runningChanged(bool running, qint64 pid);

    // The child died (or was killed) and the poll observed the exit.
    // Emitted on every death, including the one a hot-swap relaunch is
    // about to follow, so the View's slot runs exactly as often as the
    // former onOverlayExited() did.
    void exited();

    // launch() succeeded and a child is now alive. Carries the argv (with
    // this controller's own --game appended) so the View can rebuild it
    // for a hot-swap relaunch.
    void launched(const QStringList &argv, bool editMode);

private:
    void startPollTimer();
    void stopPollTimer();
    void teardownChildState();
    // The former ControlPanel::onOverlayExited().
    void handleExited();

    qint64      overlayPid_ = 0;      // 0 = no child
    QTimer     *overlayWatch_ = nullptr;  // 250 ms kill(pid,0) liveness poll
    bool        pendingRestart_ = false;  // relaunch once the poll sees the exit
    mhw::GameId currentGame_ = mhw::GameId::World;  // drives --game
    QStringList lastArgv_;            // argv of the most recent successful launch
    bool        lastEditMode_ = false;
};

} // namespace mhw
