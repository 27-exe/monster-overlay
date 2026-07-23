// SPDX-License-Identifier: Apache-2.0
// Watch the live monster's ailment pointer chain and dump 0x1BC40 each second.
//
// Walks MonsterList -> Component -> Monster the same way readMonsters does.
// For the first live monster (HP > 1000, name non-empty):
//   - reads pointer array at monster+0x1BC40 (8 entries)
//   - dumps the inline region 0x1BC00..0x1BE00 (0x200 bytes) hex
//   - dumps each pointer's struct at +0x148 (HunterPie candidate), +0x00,
//     +0x80, +0xA0 (other common struct offsets)
//   - writes to a log file path supplied on the command line so you can
//     scroll back and find the moment sleep/paralysis was active.
//
// Usage: ./build/mhw-probe-ailments-watch /tmp/ailments.log
//
// Stop with Ctrl-C; the log is appended on every iteration, so search
// backward through it for `active=1 id=N` once you've had a trigger.

#include "mhw_reader.h"

#include <QCoreApplication>

#include <cstdio>
#include <cinttypes>
#include <cstring>
#include <csignal>
#include <unistd.h>

namespace {

std::FILE *g_log = nullptr;
volatile std::sig_atomic_t g_stop = 0;

void onSigInt(int) { g_stop = 1; }

void hexDump(FILE *f, std::uintptr_t baseAddr, const char *raw, std::size_t size)
{
    for (std::size_t off = 0; off < size; off += 0x10) {
        std::fprintf(f, "  +0x%04lx:", (unsigned long)(baseAddr + off));
        for (std::size_t j = 0; j < 0x10 && off + j < size; j += 4) {
            std::int32_t v = 0;
            std::memcpy(&v, raw + off + j, 4);
            std::fprintf(f, " %08x", (unsigned)v);
        }
        std::fprintf(f, "\n");
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <log-file>\n", argv[0]);
        return 1;
    }
    std::signal(SIGINT, onSigInt);

    QCoreApplication app(argc, argv);

    g_log = std::fopen(argv[1], "w");
    if (!g_log) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::setvbuf(g_log, nullptr, _IOLBF, 0);

    auto pidOpt = mhw::MhwReader::findGamePid();
    if (!pidOpt) {
        std::fprintf(stderr, "no MHW process\n");
        // Exit 1: "MHW not running" — ctest's SKIP_RETURN_CODE for
        // diagnostic probes treats this as a soft-skip, not a failure.
        return 1;
    }
    std::int64_t pid = *pidOpt;
    std::fprintf(g_log, "MHW pid=%lld\n", (long long)pid);

    mhw::ProcessMemory mem;
    QString err;
    if (!mem.attach(pid, &err)) {
        std::fprintf(g_log, "attach failed: %s\n", err.toStdString().c_str());
        return 4;
    }

    const std::uintptr_t imageBase = mem.imageBase(nullptr);
    std::fprintf(g_log, "imageBase=0x%" PRIxPTR "\n\n", imageBase);

    int tick = 0;
    while (!g_stop) {
        // Re-resolve live monster each tick (zone changes, swaps, etc.)
        const std::uintptr_t listAddr = imageBase + 0x0500CF40ULL;
        const auto headPtr = mem.read<std::uintptr_t>(listAddr, nullptr);
        if (!headPtr) {
            std::fprintf(g_log, "tick %d: MonsterList head read failed\n", tick++);
            ::sleep(1);
            continue;
        }
        const std::uintptr_t compsBase = *headPtr + 0x38ULL;
        const auto comps = mem.readArray<std::uintptr_t>(compsBase, 128, nullptr);
        if (comps.size() != 128) {
            std::fprintf(g_log, "tick %d: components read failed\n", tick++);
            ::sleep(1);
            continue;
        }

        std::uintptr_t monster = 0;
        for (int i = 0; i < 128; ++i) {
            const std::uintptr_t comp = comps[i];
            if (comp < 0x10000 || comp >= 0x0000800000000000ULL) continue;
            const auto inner = mem.read<std::uintptr_t>(comp + 0x138ULL, nullptr);
            if (!inner || *inner < 0x10000) continue;
            const auto hpPtr = mem.read<std::uintptr_t>(*inner + 0x7670ULL, nullptr);
            if (!hpPtr || *hpPtr < 0x10000) continue;
            const auto hp = mem.readArray<float>(*hpPtr + 0x60ULL, 2, nullptr);
            if (hp.size() != 2 || hp[0] <= 1000.0F) continue;
            char nameBuf[64] = {0};
            const auto nameStruct = mem.read<std::uintptr_t>(*inner + 0x2A0ULL, nullptr);
            if (nameStruct && *nameStruct >= 0x10000)
                mem.readBytes(*nameStruct + 0xCULL, nameBuf, sizeof(nameBuf) - 1, nullptr);
            if (nameBuf[0] == 0) continue;
            monster = *inner;
            std::fprintf(g_log, "tick %d: live monster slot[%d] @ 0x%" PRIxPTR " em=\"%s\" hp=%.0f/%.0f\n",
                         tick, i, monster, nameBuf, hp[1], hp[0]);
            break;
        }
        if (monster == 0) {
            std::fprintf(g_log, "tick %d: no live monster\n", tick++);
            ::sleep(1);
            continue;
        }

        // Dump the inline region 0x1BC00..0x1BE00 (0x200 bytes) hex
        char rawBlock[0x200] = {0};
        if (mem.readBytes(monster + 0x1BC00ULL, rawBlock, sizeof(rawBlock), nullptr)) {
            std::fprintf(g_log, "  inline 0x1BC00..0x1BE00:\n");
            hexDump(g_log, 0x1BC00ULL, rawBlock, sizeof(rawBlock));
        }

        // Walk the pointer array at +0x1BC40 (8 entries)
        std::fprintf(g_log, "  ptr array @ +0x1BC40:\n");
        for (int i = 0; i < 8; ++i) {
            const std::uintptr_t cursor = monster + 0x1BC40ULL
                                        + std::uintptr_t(i) * sizeof(std::uintptr_t);
            const auto entry = mem.read<std::uintptr_t>(cursor, nullptr);
            if (!entry || *entry < 0x10000) {
                std::fprintf(g_log, "    [%d] no entry\n", i);
                continue;
            }
            std::fprintf(g_log, "    [%d] -> 0x%" PRIxPTR "\n", i, (qulonglong)*entry);

            // Try several struct offsets
            const std::uintptr_t structOffsets[] = {0x148ULL, 0x000ULL, 0x080ULL, 0x0A0ULL, 0x0C0ULL};
            for (std::uintptr_t soff : structOffsets) {
                char raw[0x80] = {0};
                const std::uintptr_t sAddr = *entry + soff;
                if (!mem.readBytes(sAddr, raw, sizeof(raw), nullptr)) continue;

                std::int64_t owner = 0;
                std::int32_t active = 0, unk1 = 0, id = 0;
                std::memcpy(&owner, raw + 0x00, 8);
                std::memcpy(&active, raw + 0x08, 4);
                std::memcpy(&unk1, raw + 0x0C, 4);
                std::memcpy(&id, raw + 0x10, 4);

                float f14 = 0, f18 = 0, f1C = 0, f34 = 0, f50 = 0, f5C = 0;
                std::memcpy(&f14, raw + 0x14, 4);
                std::memcpy(&f18, raw + 0x18, 4);
                std::memcpy(&f1C, raw + 0x1C, 4);
                std::memcpy(&f34, raw + 0x34, 4);
                std::memcpy(&f50, raw + 0x50, 4);
                std::memcpy(&f5C, raw + 0x5C, 4);

                std::int32_t counter = 0;
                std::memcpy(&counter, raw + 0x74, 4);

                const bool ownerOk = (owner == (std::int64_t)monster);
                const bool looksActive = (active != 0);
                const bool looksReasonable = (std::abs(f14) > 0.001F && std::abs(f14) < 600.0F);
                const int flags = (ownerOk ? 1 : 0) | (looksActive ? 2 : 0) | (looksReasonable ? 4 : 0);
                std::fprintf(g_log,
                             "      soff=0x%lx  %s  active=%d unk1=%d id=%d cnt=%d  "
                             "f14=%.2f f18=%.2f f1C=%.2f f34=%.2f f50=%.2f f5C=%.2f\n",
                             (unsigned long)soff,
                             (flags == 7) ? "★MATCH" : "    ",
                             active, unk1, id, counter,
                             f14, f18, f1C, f34, f50, f5C);
            }
        }
        std::fprintf(g_log, "\n");
        ++tick;
        ::sleep(1);
    }

    std::fclose(g_log);
    return 0;
}