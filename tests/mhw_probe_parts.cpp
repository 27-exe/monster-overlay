// SPDX-License-Identifier: Apache-2.0
// Deep probe: for the live monster in MHW, read BOTH the normal
// (monster+0x1D058+0x40) and severable (monster+0x1D058+0x1FC8)
// part tables, dump raw bytes and decoded values. Use this to verify
// whether the multi-player part data lives in the severable table.
//
// When run with --watch (or no args), it samples the first 6 normal +
// 6 severable slots every second for 8 ticks. Printed output is a
// matrix of `flinch/health (index)` per second — flinch should be
// visibly counting up; health should be stuck at 100% in multiplayer.
//
// Build target added by CMakeLists.

#include "mhw_reader.h"

#include <QCoreApplication>
#include <QFile>

#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <cstdlib>
#include <ctime>
#include <string>
#include <unistd.h>

namespace {

constexpr std::uintptr_t kPartPtrOffset = 0x1D058;
constexpr std::uintptr_t kNormalBase    = 0x40;
constexpr std::uintptr_t kNormalStride  = 0x1F8;
constexpr std::uintptr_t kSeverableBase = 0x1FC8;
constexpr std::uintptr_t kSeverableStride = 0x78;

struct PartSlot {
    float maxHealth;
    float health;
    float extraMaxHealth;
    float extraHealth;
    int   counter;
    std::uint32_t index;
};

bool decodeSlot(const char *raw, PartSlot &out)
{
    std::memcpy(&out.maxHealth, raw + 0x0C, 4);
    std::memcpy(&out.health,    raw + 0x10, 4);
    std::memcpy(&out.extraMaxHealth, raw + 0x20, 4);
    std::memcpy(&out.extraHealth,    raw + 0x24, 4);
    std::memcpy(&out.counter,   raw + 0x18, 4);
    std::memcpy(&out.index,     raw + 0x6C, 4);
    return out.maxHealth > 0.0F;
}

void dumpTable(mhw::ProcessMemory &mem, std::uintptr_t baseAddr,
               std::uintptr_t stride, int nSlots, const char *label)
{
    std::printf("\n=== %s @ 0x%lx (stride 0x%lx, %d slots) ===\n",
                label, (unsigned long)baseAddr, (unsigned long)stride, nSlots);
    int valid = 0;
    for (int i = 0; i < nSlots; ++i) {
        const std::uintptr_t addr = baseAddr + std::uintptr_t(i) * stride;
        char raw[0x78] = {0};
        if (!mem.readBytes(addr, raw, sizeof(raw), nullptr)) {
            std::printf("  slot[%2d] @ 0x%lx  read failed\n", i, (unsigned long)addr);
            continue;
        }
        // sentinel int32 (first 4 bytes)
        std::int32_t head = 0;
        std::memcpy(&head, raw, 4);
        PartSlot p{};
        const bool ok = decodeSlot(raw, p);
        if (ok) ++valid;
        std::printf("  slot[%2d] @ 0x%lx  head=0x%08x  idx=%2u  mhp=%9.1f  hp=%9.1f  cnt=%d  emhp=%9.1f  ehp=%9.1f  %s\n",
                    i, (unsigned long)addr, (unsigned)head, p.index, p.maxHealth, p.health,
                    p.counter, p.extraMaxHealth, p.extraHealth,
                    ok ? "VALID" : "(empty)");
    }
    std::printf("  -> %d / %d slots with MaxHealth > 0\n", valid, nSlots);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const auto pidOpt = mhw::MhwReader::findGamePid();
    if (!pidOpt) {
        std::printf("no MHW process found\n");
        return 1;
    }
    const qint64 pid = *pidOpt;
    std::printf("MHW pid=%lld\n", static_cast<long long>(pid));

    mhw::ProcessMemory mem;
    QString err;
    if (!mem.attach(pid, &err)) {
        std::printf("attach failed: %s\n", err.toStdString().c_str());
        return 2;
    }

    const std::uintptr_t imageBase = mem.imageBase(nullptr);
    std::printf("imageBase=0x%" PRIxPTR "\n", imageBase);

    // Walk the MonsterList the same way MhwReader::readMonsters does.
    const std::uintptr_t listAddr = imageBase + 0x0500CF40ULL;
    const auto headPtr = mem.read<std::uintptr_t>(listAddr);
    if (!headPtr) { std::printf("listAddr read failed\n"); return 3; }
    const std::uintptr_t componentsBase = *headPtr + 0x38ULL;
    const auto comps = mem.readArray<std::uintptr_t>(componentsBase, 128, nullptr);
    if (comps.size() != 128) { std::printf("component read failed\n"); return 4; }

    // Find the live monster with HP > 0 (skip stubs / dead).
    std::uintptr_t monsterAddr = 0;
    int foundIdx = -1;
    for (int i = 0; i < 128; ++i) {
        const std::uintptr_t comp = comps[i];
        if (comp < 0x10000 || comp >= 0x0000800000000000ULL) continue;
        const auto inner = mem.read<std::uintptr_t>(comp + 0x138ULL);
        if (!inner || *inner < 0x10000 || *inner >= 0x0000800000000000ULL) continue;
        const std::uintptr_t m = *inner;
        const auto hpPtr = mem.read<std::uintptr_t>(m + 0x7670ULL);
        if (!hpPtr || !(*hpPtr >= 0x10000 && *hpPtr < 0x0000800000000000ULL)) continue;
        const auto hp = mem.readArray<float>(*hpPtr + 0x60ULL, 2, nullptr);
        if (hp.size() == 2 && hp[0] > 1000.0F && hp[1] > 0.0F && hp[1] < hp[0]) {
            // Read em* name to confirm it's a real monster
            char name[64] = {0};
            const auto nameStruct = mem.read<std::uintptr_t>(m + 0x2A0ULL);
            if (nameStruct && *nameStruct >= 0x10000 && *nameStruct < 0x0000800000000000ULL) {
                mem.readBytes(*nameStruct + 0xCULL, name, sizeof(name) - 1, nullptr);
            }
            std::printf("live monster slot[%d] inner=0x%" PRIxPTR " em=\"%s\" hp=%.1f/%.1f\n",
                        i, m, name, hp[1], hp[0]);
            monsterAddr = m;
            foundIdx = i;
            break;
        }
    }
    if (monsterAddr == 0) {
        std::printf("no live monster with HP found\n");
        return 5;
    }

    // Get Id
    const auto id = mem.read<std::int32_t>(monsterAddr + 0x12280ULL);
    std::printf("hunterId=%d\n", id ? *id : -1);

    // Read part pointer
    const auto partPtr = mem.read<std::uintptr_t>(monsterAddr + kPartPtrOffset);
    if (!partPtr || !(*partPtr >= 0x10000 && *partPtr < 0x0000800000000000ULL)) {
        std::printf("partPtr read failed\n");
        return 6;
    }
    const std::uintptr_t partBase = *partPtr;
    std::printf("\npartPtr=0x%" PRIxPTR "  (normal @ +0x%" PRIxPTR " = 0x%" PRIxPTR
                ",  severable @ +0x%" PRIxPTR " = 0x%" PRIxPTR ")\n",
                partBase, kNormalBase, partBase + kNormalBase,
                kSeverableBase, partBase + kSeverableBase);

    // Dump the normal table (16 slots × 0x1F8 stride — read 0x78 bytes each).
    dumpTable(mem, partBase + kNormalBase, kNormalStride, 16, "NORMAL TABLE (0x40)");

    // Dump the severable table (32 slots × 0x78 stride).
    dumpTable(mem, partBase + kSeverableBase, kSeverableStride, 32, "SEVERABLE TABLE (0x1FC8)");

    // Watch mode: sample first 6 normal + 6 severable slots every second
    // for 8 ticks. Useful for proving whether the Flinch field updates
    // locally in multiplayer (the Health field is frozen on the client
    // per mhw-parts-hp-frozen-on-client-2026-07-23).
    bool watch = (argc < 2);
    if (argc >= 2) {
        const std::string a = argv[1];
        if (a == "--watch") watch = true;
        else if (a == "--no-watch") watch = false;
    }
    if (watch) {
        std::printf("\n=== WATCH MODE: sampling 1 Hz for 8s ===\n");
        std::printf("Format: `flinch/health (idx, cnt)` per slot, per second.\n");
        std::printf("Watch the 'flinch' value — it should move; 'health' may be stuck at mhp.\n\n");
        for (int t = 0; t < 8; ++t) {
            std::printf("t=%ds  ", t);
            // First 6 normal slots
            for (int s = 0; s < 6; ++s) {
                char raw[0x78] = {0};
                const std::uintptr_t addr = partBase + kNormalBase
                                          + std::uintptr_t(s) * kNormalStride;
                if (!mem.readBytes(addr, raw, sizeof(raw), nullptr)) {
                    std::printf("  N%d:read-err", s);
                    continue;
                }
                PartSlot p{};
                const bool ok = decodeSlot(raw, p);
                if (!ok) { std::printf("  N%d:----", s); continue; }
                std::printf("  N%d:%.0f/%.0f(idx=%u,cnt=%d)",
                            s, p.health, p.maxHealth, p.index, p.counter);
            }
            // First 6 severable slots
            for (int s = 0; s < 6; ++s) {
                char raw[0x78] = {0};
                std::uintptr_t addr = partBase + kSeverableBase
                                    + std::uintptr_t(s) * kSeverableStride;
                // Skip sentinel
                if (const auto pad = mem.read<std::int32_t>(addr, nullptr)) {
                    if (pad && *pad <= 0xA0) addr += 0x8;
                }
                if (!mem.readBytes(addr, raw, sizeof(raw), nullptr)) {
                    std::printf("  S%d:read-err", s);
                    continue;
                }
                PartSlot p{};
                const bool ok = decodeSlot(raw, p);
                if (!ok) { std::printf("  S%d:----", s); continue; }
                std::printf("  S%d:%.0f/%.0f(idx=%u,cnt=%d)",
                            s, p.health, p.maxHealth, p.index, p.counter);
            }
            std::printf("\n");
            std::fflush(stdout);
            ::sleep(1);
        }
    }

    std::printf("\ndone.\n");
    return 0;
}