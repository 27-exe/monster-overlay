// SPDX-License-Identifier: Apache-2.0
#include "rise/rise_install_locator.h"

#include "core/steam_game_locator.h"

namespace mhw {

QString findRiseInstallDir(const QStringList &steamRoots)
{
    return findSteamGameInstallDir(kRiseSteamAppId,
                                   QStringLiteral("MonsterHunterRise.exe"),
                                   steamRoots);
}

QString findRiseInstallDir()
{
    return findRiseInstallDir(defaultSteamRoots());
}

} // namespace mhw
