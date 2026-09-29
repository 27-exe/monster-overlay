// SPDX-License-Identifier: Apache-2.0
// Core offsets and structures are derived from HunterPie/HunterPie (Apache-2.0).

// MhrReader::readQuest(), lifted out of src/rise/mhr_reader.cpp so the
// largest parser in the Rise reader gets its own translation unit.
// The function body is unchanged.

#include "core/process_memory.h"
#include "rise/mhr_reader.h"

#include <algorithm>
#include <cstdint>

namespace mhw {

QuestSnapshot MhrReader::readQuest(QString *error)
{
    QuestSnapshot result;
    const std::uintptr_t questStruct = followPointerChain(
        memory_, absolute(QStringLiteral("QUEST_ADDRESS")),
        map_.offsets(QStringLiteral("QUEST_OFFSETS")), error);
    if (!questStruct)
        return result;

    // v0.8.4-r7 restore-reader (BUG #2): at +0x170 HunterPie exposes
    // TimeElapsed, NOT TimeLeft. The real TimeLimit lives at +0x178.
    // Populate all three timer fields consistently with World.
    if (const auto elapsed = memory_.read<float>(questStruct + 0x170ULL))
        result.elapsedSeconds = *elapsed;
    if (const auto limit = memory_.read<float>(questStruct + 0x178ULL))
        result.maxTimerSeconds = *limit;
    // Derive the countdown that panel_player.cpp displays; clamp to
    // [0, max] so a one-frame jitter never overflows.
    if (result.maxTimerSeconds > 0.0F) {
        result.timeLeftSeconds = std::max(
            0.0F,
            result.maxTimerSeconds - result.elapsedSeconds);
        if (result.timeLeftSeconds > result.maxTimerSeconds)
            result.timeLeftSeconds = result.maxTimerSeconds;
    }

    if (const auto status = memory_.read<std::int32_t>(questStruct + 0x110ULL))
        result.state = *status;
    // v0.8.4-r7 restore-reader (BUG #3): category / deaths / maxDeaths
    // from MHRQuestStructure; id / stars / rank from QuestDataPointer
    // (+0x118) -> Normal or Anomaly (Sunbreak) data structure.
    if (const auto category = memory_.read<std::int32_t>(questStruct + 0x120ULL))
        result.category = *category;
    if (const auto maxDeaths = memory_.read<std::int32_t>(questStruct + 0x15CULL))
        result.maxDeaths = *maxDeaths;
    if (const auto deaths = memory_.read<std::int32_t>(questStruct + 0x160ULL))
        result.deaths = *deaths;

    const auto qdp = memory_.read<std::uintptr_t>(questStruct + 0x118ULL);
    if (qdp && isSanePointer(*qdp)) {
        const std::uintptr_t questData = *qdp;
        const auto normalPtr =
            memory_.read<std::uintptr_t>(questData + 0x10ULL);
        const auto anomalyPtr =
            memory_.read<std::uintptr_t>(questData + 0x28ULL);

        // Prefer the Normal pointer; fall back to Anomaly (Sunbreak).
        if (normalPtr && isSanePointer(*normalPtr)) {
            if (const auto id = memory_.read<std::int32_t>(*normalPtr + 0x10ULL))
                result.id = *id;
            if (const auto stars = memory_.read<std::int32_t>(*normalPtr + 0x24ULL))
                result.stars = riseNormalQuestStars(*stars);
            if (const auto rank = memory_.read<std::int32_t>(*normalPtr + 0x28ULL))
                result.rank = *rank;
        } else if (anomalyPtr && isSanePointer(*anomalyPtr)) {
            if (const auto id = memory_.read<std::int32_t>(*anomalyPtr + 0x14ULL))
                result.id = *id;
            // Anomaly quests have no Stars field; HunterPie surfaces
            // `Level` as Stars so the panel shows the MR level.
            if (const auto level = memory_.read<std::int32_t>(*anomalyPtr + 0x18ULL))
                result.stars = *level;
            result.isAnomaly = true;
        }
    }

    // Match HunterPie MHRGame.GetQuest: InQuest and a positive id are not
    // sufficient when MHRQuestStructure.Type has no supported ToQuestType map.
    result.active = isRiseQuestActive(result.id, result.state, result.category);
    return result;
}

} // namespace mhw
