#include "overlay_process_controller.h"

#include <QCoreApplication>
#include <QProcess>
#include <QTimer>

#include <signal.h>
#include <sys/types.h>

namespace mhw {

OverlayProcessController::OverlayProcessController(QObject *parent)
    : QObject(parent)
{
}

// v0.5 P1: cold start. The argv is built by the View (it reads its own
// mask checkboxes and output combos); this function appends the `--game`
// flag that used to be assembled here from currentGame_, spawns the child
// detached and, on success, arms the 250 ms liveness poll.
//
// The launch-refusal guard (a child is already alive) and the
// QProcess::startDetached() failure path are the original ones: both
// return without touching overlayPid_, so the console stays in the READY
// state and the caller's buttons stay disabled/enabled exactly as before.
bool OverlayProcessController::launch(const QStringList &argv, bool editMode)
{
    if (overlayPid_ != 0) {
        // Already running — refuse to launch a second copy. The user
        // can press ESC in the overlay to bring the console back, then
        // click again.
        return false;
    }

    QStringList args = argv;
    // Target game selected in the rail (setCurrentGame persists it). The
    // overlay would otherwise auto-detect, which can pick the wrong
    // process when both World and Rise are installed/running.
    args << QStringLiteral("--game=%1")
                .arg(currentGame_ == mhw::GameId::Rise
                         ? QStringLiteral("rise") : QStringLiteral("world"));

    // monster-overlay lives next to monster-control in the same build dir.
    const QString overlay = QCoreApplication::applicationDirPath()
                          + QStringLiteral("/monster-overlay");

    qint64 pid = 0;
    if (!QProcess::startDetached(overlay, args,
                                 QCoreApplication::applicationDirPath(),
                                 &pid)) {
        qWarning("monster-control: failed to launch %s", qPrintable(overlay));
        return false;
    }
    overlayPid_ = pid;
    lastArgv_ = args;
    lastEditMode_ = editMode;

    // Poll the PID. 250ms feels live but stays well under one paint frame
    // — the console re-shows within a quarter second of overlay death.
    startPollTimer();

    emit runningChanged(true, overlayPid_);
    emit launched(lastArgv_, lastEditMode_);
    return true;
}

// v0.5 P1: kill the overlay subprocess and let the poll observe the exit.
// Safe to call when no overlay is running.
void OverlayProcessController::stop()
{
    if (overlayPid_ == 0) return;
    if (overlayWatch_) overlayWatch_->stop();
    // SIGTERM = gentle. The overlay's own ESC handler will run
    // saveConfig() and quit cleanly. SIGKILL would skip that.
    kill(static_cast<pid_t>(overlayPid_), SIGTERM);
    // Don't zero overlayPid_ here — the 250ms PID-poll timer will
    // observe the exit and call handleExited() which does the
    // teardown. Setting it to 0 now would block a re-launch.
}

void OverlayProcessController::setCurrentGame(mhw::GameId game)
{
    if (game == currentGame_)
        return;

    const bool wasRunning = (overlayPid_ != 0);
    currentGame_ = game;

    if (!wasRunning)
        return;

    // v0.6 Phase 5: hot-swap — the user switched game while running, so
    // SIGTERM the child and relaunch with the new --game once the poll
    // observes the exit. stop() pauses the poll timer; the hot-swap needs
    // it alive to observe the exit and trigger the relaunch.
    pendingRestart_ = true;
    stop();
    if (overlayWatch_) overlayWatch_->start();
}

void OverlayProcessController::restartWithCurrentGame()
{
    // The View has already saved mask + appearance by the time this runs
    // (it does so in its `exited()` handler before calling us), so this
    // is a plain relaunch with the freshly-updated currentGame_.
    launch(lastArgv_, lastEditMode_);
}

// The private poll body. kill(pid, 0) is POSIX's "does this PID exist?" —
// no signal sent. ESRCH (any nonzero errno) means the process is gone.
void OverlayProcessController::startPollTimer()
{
    stopPollTimer();
    overlayWatch_ = new QTimer(this);
    overlayWatch_->setInterval(250);
    connect(overlayWatch_, &QTimer::timeout, this, [this]{
        if (overlayPid_ == 0) return;
        if (kill(static_cast<pid_t>(overlayPid_), 0) != 0) {
            handleExited();
        }
    });
    overlayWatch_->start();
}

void OverlayProcessController::stopPollTimer()
{
    if (!overlayWatch_)
        return;
    overlayWatch_->stop();
    overlayWatch_->deleteLater();
    overlayWatch_ = nullptr;
}

void OverlayProcessController::teardownChildState()
{
    overlayPid_ = 0;
    stopPollTimer();
}

// The former ControlPanel::onOverlayExited(): teardown of the child state
// (pid + poll timer) plus the hot-swap relaunch. Everything the View used
// to do around it (re-enabling buttons, badge text, show/raise) happens in
// the slots connected to `exited()`.
void OverlayProcessController::handleExited()
{
    teardownChildState();

    emit runningChanged(false, 0);
    emit exited();

    if (pendingRestart_) {
        pendingRestart_ = false;
        restartWithCurrentGame();
    }
}

} // namespace mhw
