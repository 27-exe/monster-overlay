#include "monster/world_severable_scan.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition) {
        std::cout << "PASS: " << message << '\n';
    } else {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct Slot {
    mhw::WorldPartReadStatus status;
    std::uint32_t index;
};

std::size_t walk(const std::vector<Slot> &slots, std::uint32_t wanted,
                 std::size_t *consulted)
{
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        *consulted = slot + 1;
        switch (mhw::severableScanAction(
            slots[slot].status, slots[slot].index, wanted)) {
        case mhw::SeverableScanAction::Continue:
            continue;
        case mhw::SeverableScanAction::Match:
            return slot;
        case mhw::SeverableScanAction::Stop:
            return slots.size();
        }
    }
    *consulted = slots.size();
    return slots.size();
}

} // namespace

int main()
{
    {
        const std::vector<Slot> slots = {
            {mhw::WorldPartReadStatus::Empty, 99},
            {mhw::WorldPartReadStatus::Valid, 7},
            {mhw::WorldPartReadStatus::Empty, 0},
            {mhw::WorldPartReadStatus::Valid, 0},
        };
        std::size_t consulted = 0;
        check(walk(slots, 0, &consulted) == 3,
              "empty and non-matching slots do not hide a later Index=0 tail");
        check(consulted == 4, "scan stops immediately after matching the tail");
    }

    {
        const std::vector<Slot> slots = {
            {mhw::WorldPartReadStatus::Empty, 0},
            {mhw::WorldPartReadStatus::Failure, 0},
            {mhw::WorldPartReadStatus::Valid, 0},
        };
        std::size_t consulted = 0;
        check(walk(slots, 0, &consulted) == slots.size(),
              "real memory-read failure terminates safely");
        check(consulted == 2, "scan never reads beyond a failed slot");
    }

    {
        std::vector<Slot> slots(32, {mhw::WorldPartReadStatus::Empty, 0});
        std::size_t consulted = 0;
        check(walk(slots, 0, &consulted) == slots.size(),
              "an all-empty table has no match");
        check(consulted == 32, "an all-empty table remains bounded to 32 slots");
    }

    check(mhw::severableScanAction(mhw::WorldPartReadStatus::Empty, 0, 0)
              == mhw::SeverableScanAction::Continue,
          "an empty wanted-index slot is not a false match");

    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
