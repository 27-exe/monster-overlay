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

struct WorldSeverableSlotLayout {
    std::uintptr_t payloadAddress{};
    std::uintptr_t nextAddress{};
};

inline constexpr WorldSeverableSlotLayout worldSeverableSlotLayout(
    std::uintptr_t slotAddress,
    std::int32_t prefixWord)
{
    const std::uintptr_t payload = prefixWord <= 0xA0
        ? slotAddress + 0x8ULL
        : slotAddress;
    return {payload, payload + 0x78ULL};
}

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
