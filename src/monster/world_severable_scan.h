#pragma once

#include <cstdint>

namespace mhw {

// A World severable-table read must distinguish a readable live slot, a
// readable empty slot, and an unreadable address. HunterPie keeps scanning
// after empty/non-matching slots and stops only after an actual read failure.
enum class WorldPartReadStatus {
    Valid,
    Empty,
    Failure,
};

enum class SeverableScanAction {
    Continue,
    Match,
    Stop,
};

inline constexpr SeverableScanAction severableScanAction(
    WorldPartReadStatus status,
    std::uint32_t slotIndex,
    std::uint32_t wantedIndex)
{
    if (status == WorldPartReadStatus::Failure)
        return SeverableScanAction::Stop;
    if (status == WorldPartReadStatus::Valid && slotIndex == wantedIndex)
        return SeverableScanAction::Match;
    return SeverableScanAction::Continue;
}

} // namespace mhw
