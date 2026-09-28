// SPDX-License-Identifier: Apache-2.0
#pragma once

// Rise console card — REFramework status-line derivation (MVVM extraction).
//
// ControlPanel::refreshRiseReframeworkStatus() used to build the readable
// status lines inline, in the middle of a method that also queries the Steam
// locator, toggles the card's visibility and enables/disables four buttons.
// The line-building half is pure: it reads a RiseReFrameworkManager::Status,
// the locator's game directory and the console's own operation flags, and
// returns the QStringList the label shows. Nothing in it reads or writes a
// GUI object — which is also why the REFramework state machine had zero
// automated coverage: it was only reachable through a private member of the
// console panel.
//
// Hard rule, same as DamageViewModel and MonsterViewModel: this header and
// its implementation carry NO GUI-layer dependency — only QtCore types plus
// the pure monster-core headers. The View calls riseReframeworkStatusLines()
// and paints the returned lines unchanged.
//
// The derivation moved here 1:1 from control_panel.cpp (HEAD 91901e3,
// refreshRiseReframeworkStatus()). Every i18n key, every branch condition and
// the line order are the original: nothing was normalized, reordered or
// "improved" on the way. A branch that looks wrong is recorded in
// T17-REPORT.md rather than fixed here.

#include "rise/reframework/rise_reframework_manager.h"

#include <QString>
#include <QStringList>

namespace mhw {

// Everything the derivation needs beyond the manager's own Status snapshot.
// `status.detail` is the installer's technical summary; it is handed through
// untouched and appended as the last, clearly-labelled line.
struct RiseReframeworkStatusInput {
    // Result of the Steam locator. Empty means "Steam has no Rise install".
    QString gameDir;
    // What the manager reported for `gameDir`.
    mhw::RiseReFrameworkManager::Status status;
    // mhw::detectGame().has_value() — independent of the manager.
    bool gameRunning{false};
    // Console-side lifecycle bookkeeping: a worker run in flight, and
    // whether that run has already reported a result for this card.
    bool operationPending{false};
    bool hasResult{false};
    bool resultOk{false};
    QString resultDetail;
};

// The readable status lines, in the exact order the console label joins them.
[[nodiscard]] QStringList riseReframeworkStatusLines(
    const RiseReframeworkStatusInput &input);

} // namespace mhw
