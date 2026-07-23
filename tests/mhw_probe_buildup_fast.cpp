// Probe enrage buildup at 4Hz while in a hunt.
// Captures the buildup value before/after a "clutch claw" hit so we can see
// whether Buildup smoothly increases by 30% (HunterPie behavior) or jumps
// directly to max (current Linux port behavior).
//
// Logs to /tmp/buildup-fast.log

#include "mhw_reader.h"

#include <QCoreApplication>

#include <cstdio>
#include <cinttypes>
#include <cstring>
#include <unistd.h>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    auto pidOpt = mhw::MhwReader::findGamePid();
    if (!pidOpt) { std::printf("no MHW\n"); return 1; }
    const qint64 pid = *pidOpt;

    mhw::ProcessMemory mem;
    QString err;
    if (!mem.attach(pid, &err)) { std::printf("attach: %s\n", err.toStdString().c_str()); return 2; }

    const std::uintptr_t imageBase = mem.imageBase(nullptr);
    const std::uintptr_t listAddr = imageBase + 0x0500CF40ULL;
    const auto headPtr = mem.read<std::uintptr_t>(listAddr, nullptr);
    if (!headPtr) { std::printf("no head\n"); return 3; }
    const std::uintptr_t compsBase = *headPtr + 0x38ULL;
    const auto comps = mem.readArray<std::uintptr_t>(compsBase, 128, nullptr);

    std::uintptr_t monster = 0;
    for (int i = 0; i < 128; ++i) {
        const std::uintptr_t comp = comps[i];
        if (comp < 0x10000) continue;
        const auto inner = mem.read<std::uintptr_t>(comp + 0x138ULL, nullptr);
        if (!inner || *inner < 0x10000) continue;
        const auto hpPtr = mem.read<std::uintptr_t>(*inner + 0x7670ULL, nullptr);
        if (!hpPtr || !(*hpPtr >= 0x10000)) continue;
        const auto hp = mem.readArray<float>(*hpPtr + 0x60ULL, 2, nullptr);
        if (hp.size() != 2 || hp[0] <= 1000.0F) continue;
        monster = *inner;
        std::printf("monitoring monster=0x%lx hp=%.0f/%.0f\n", (unsigned long)monster, hp[1], hp[0]);
        break;
    }
    if (!monster) return 4;

    // Read enrage struct at 250ms intervals for 60 seconds
    std::printf("logging to /tmp/buildup-fast.log, 4 Hz, 60s. Try clutch-claw hits during this window.\n");
    FILE *log = std::fopen("/tmp/buildup-fast.log", "w");
    if (!log) return 5;

    for (int i = 0; i < 240; ++i) {
        float bu = 0, mbu = 0, dur = 0, mdur = 0;
        std::memcpy(&bu,   *(mem.read<float>(monster + 0x1BE30ULL + 0x18ULL, nullptr).operator->(), 0);  // ugly
    }

    return 0;
}