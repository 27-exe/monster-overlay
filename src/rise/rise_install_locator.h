#pragma once

// SPDX-License-Identifier: Apache-2.0
//
// Rise install-directory lookup.
//
// Rise's Steam appId (1446780) and executable name lived in a
// findRiseInstallDir() wrapper inside src/core/steam_game_locator.{h,cpp} —
// the kernel's Steam helper header. Two consequences: core named one game's
// identity, and adding a second Steam-delivered game meant editing core
// rather than adding a column beside its own code.
//
// findSteamGameInstallDir(appId, exeName, roots) in that header is already
// the generic helper; these thin wrappers only supply Rise's two facts.

#include <QString>
#include <QStringList>

namespace mhw {

// Steam app id for Monster Hunter Rise.
inline constexpr quint64 kRiseSteamAppId = 1446780;

// Locate Rise's install directory. Empty when Steam has no such app, or the
// app is installed but its executable is absent (a common state for a
// Proton-prefix game on a fresh prefix).
[[nodiscard]] QString findRiseInstallDir(const QStringList &steamRoots);

// Same, over the default Steam roots.
[[nodiscard]] QString findRiseInstallDir();

} // namespace mhw
