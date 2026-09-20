// SPDX-License-Identifier: Apache-2.0
//
// Console-side bridge for REFramework install/removal work.
//
// The console UI (ControlPanel) only emits request signals; this bridge runs
// the actual work OFF the GUI thread - archive preparation with the bundled
// local archive first and the pinned upstream download as fallback,
// installation into the game directory, and removal of only the managed
// files - then reports the outcome back on the GUI thread.
//
// Nothing in this file is imported by monster-overlay: the overlay stays an
// external, read-only consumer of the game process.

#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <thread>

class RiseReFrameworkBridge : public QObject {
    Q_OBJECT

public:
    explicit RiseReFrameworkBridge(QString applicationDir, QObject *parent = nullptr);
    ~RiseReFrameworkBridge() override;

signals:
    // Emitted on the GUI thread after every operation; connected to
    // ControlPanel::finishRiseReframeworkOperation().
    void finished(bool ok, const QString &detail);

public slots:
    // GUI-thread entry points connected to the ControlPanel request signals.
    void install(const QString &gameDir);
    void removeLua(const QString &gameDir);
    void removeReframework(const QString &gameDir);

private:
    // Runs on the worker thread; returns overall success and fills detail.
    // `cancel` is set by the destructor when the console exits mid-operation.
    using Operation = std::function<bool(std::atomic<bool> &cancel, QString *detail)>;

    void start(QString label, Operation operation);
    void joinWorker();

    const QString m_applicationDir;
    std::thread m_worker;
    std::atomic<bool> m_busy{false};
    std::atomic<bool> m_cancel{false};
};
