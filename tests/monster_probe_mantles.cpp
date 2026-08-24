// Probe the player's equipment / mantle array.
//
// HunterPie MHWPlayer.GetMantlesData():
//   equippedIds = read<int>(equipmentBase + 0x34, 2)
//   timers      = read<float>(equipmentBase + 0xA8C, 40)   // [0..19]=current, [20..39]=max
//   cooldowns   = read<float>(equipmentBase + 0x99C, 40)   // [0..19]=current, [20..39]=max
//
// Dump all of these each second so we can see whether the equipped
// mantle ID changes and whether the timer / cooldown fields move.
//
// Usage: ./build/monster-probe-mantles [/tmp/log]

#include "mhw_reader.h"

#include <QCoreApplication>

#include <cstdio>
#include <cinttypes>
#include <cstring>
#include <csignal>
#include <unistd.h>

namespace {
FILE *g_log = nullptr;
volatile std::sig_atomic_t g_stop = 0;
void onSig(int) { g_stop = 1; }
}

int main(int argc, char **argv)
{
    std::signal(SIGINT, onSig);
    QCoreApplication app(argc, argv);

    const char *path = (argc >= 2) ? argv[1] : "/tmp/monster-probe-mantles.log";
    g_log = std::fopen(path, "w");
    if (!g_log) { std::fprintf(stderr, "open %s failed\n", path); return 1; }
    std::setvbuf(g_log, nullptr, _IOLBF, 0);

    auto pidOpt = mhw::MhwReader::findGamePid();
    if (!pidOpt) { std::fprintf(g_log, "no MHW\n"); return 1; }
    const qint64 pid = *pidOpt;
    std::fprintf(g_log, "pid=%lld\n", (long long)pid);

    mhw::ProcessMemory mem;
    QString err;
    if (!mem.attach(pid, &err)) { std::fprintf(g_log, "attach: %s\n", err.toStdString().c_str()); return 2; }

    const std::uintptr_t imageBase = mem.imageBase(nullptr);
    std::fprintf(g_log, "imageBase=0x%" PRIxPTR "\n", imageBase);

    int tick = 0;
    while (!g_stop) {
        // Resolve EQUIPMENT_ADDRESS -> EQUIPMENT_OFFSETS
        const std::uintptr_t equipAbs = imageBase + 0x050139A0ULL;
        const std::vector<std::uintptr_t> equipOff = {0x50ULL, 0x80ULL, 0x80ULL, 0x18ULL, 0x460ULL};
        const std::uintptr_t equipBase = mhw::MhwReader::followPointerChain(mem, equipAbs, equipOff, nullptr);
        std::fprintf(g_log, "tick %d: equipBase=0x%" PRIxPTR "\n", tick, equipBase);

        if (equipBase) {
            // Equipped mantle IDs (2 ints at +0x34)
            const auto ids = mem.readArray<std::int32_t>(equipBase + 0x34ULL, 2, nullptr);
            std::fprintf(g_log, "  equippedIds: ");
            for (auto v : ids) std::fprintf(g_log, "%d ", v);
            std::fprintf(g_log, "\n");

            // Cooldowns (40 floats at +0x99C)
            const auto cds = mem.readArray<float>(equipBase + 0x99CULL, 40, nullptr);
            std::fprintf(g_log, "  cooldowns[0..19] current:");
            if (cds.size() == 40) for (int i = 0; i < 20; ++i) std::fprintf(g_log, " %d:%.0fs", i, cds[i]);
            std::fprintf(g_log, "\n  cooldowns[20..39] max:   ");
            if (cds.size() == 40) for (int i = 20; i < 40; ++i) std::fprintf(g_log, " %d:%.0fs", i-20, cds[i]);
            std::fprintf(g_log, "\n");

            // Timers (40 floats at +0xA8C)
            const auto timers = mem.readArray<float>(equipBase + 0xA8CULL, 40, nullptr);
            std::fprintf(g_log, "  timers[0..19] current:   ");
            if (timers.size() == 40) for (int i = 0; i < 20; ++i) std::fprintf(g_log, " %d:%.0fs", i, timers[i]);
            std::fprintf(g_log, "\n  timers[20..39] max:      ");
            if (timers.size() == 40) for (int i = 20; i < 40; ++i) std::fprintf(g_log, " %d:%.0fs", i-20, timers[i]);
            std::fprintf(g_log, "\n");

            // Also dump a few nearby offsets in case the offsets are off
            char raw[0x100] = {0};
            if (mem.readBytes(equipBase + 0x99CULL, raw, sizeof(raw), nullptr)) {
                std::fprintf(g_log, "  raw +0x99C (256 bytes hex):\n");
                for (int i = 0; i < 0x100; i += 0x10) {
                    std::fprintf(g_log, "    +0x%03x:", 0x99C + i);
                    for (int j = 0; j < 0x10; j += 4) {
                        std::int32_t v = 0;
                        std::memcpy(&v, raw + i + j, 4);
                        std::fprintf(g_log, " %08x", (unsigned)v);
                    }
                    std::fprintf(g_log, "\n");
                }
            }
        }
        std::fprintf(g_log, "\n");
        ++tick;
        ::sleep(1);
    }
    std::fclose(g_log);
    return 0;
}