#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace mhw {

struct QuestSnapshot {
    int id{};
    int stars{};
    int state{};
    int category{};
    int deaths{};
    int maxDeaths{};
    float timeLeftSeconds{};
    float maxTimerSeconds{};   // HunterPie: snapped quest max (seconds)
    float elapsedSeconds{};    // HunterPie: max(0, max - left)
    bool active{};
};

// HunterPie read path for the in-game quest timer:
//   QUEST_DATA_ADDRESS + QUEST_TIMER_OFFSETS
//     +0x00 uint64  ticks remaining  (decoded by MHWCrypto.LiterallyWhyCapcom)
//     +0x10 uint32  raw max ticks    (snapped to MaxQuestTimers)
//
// We don't reproduce the legacy byte-wise crypto here because
// build 421810 already uses LiterallyWhyCapcom (division by 60).
// Source: HunterPie.Integrations/MHWGame.cs:104-145, MHWGameUtils.cs:12
// and HunterPie.Core/Extensions/UIntExtensions.cs (ApproximateHigh).
//
// Both helpers below take and return values in seconds (post-division),
// matching how the UI presents the timer and how the damage panel
// consumes the elapsed time.
inline float questElapsedSeconds(float maxTimerSeconds, float timeLeftSeconds)
{
    return std::max(0.0F, maxTimerSeconds - timeLeftSeconds);
}

inline float questMaxTimerSeconds(std::uint32_t rawMax)
{
    // HunterPie MaxQuestTimers in raw seconds; ApproximateHigh snaps up
    // to the smallest entry that is >= rawMax.
    static const std::array<std::uint32_t, 5> kSteps = {
        54000, 72000, 108000, 126000, 180000
    };
    for (const std::uint32_t step : kSteps) {
        if (step >= rawMax)
            return static_cast<float>(step) / 60.0F;
    }
    // Above the highest known step: fall back to the raw value (would
    // only happen with unsupported quest types).
    return static_cast<float>(rawMax) / 60.0F;
}

} // namespace mhw
