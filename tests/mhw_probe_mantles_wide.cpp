// Search for the equipped mantle ID within a wider region around the
// expected address. We don't know if +0x34 is right on this build; dump
// a wider window and let us grep visually.

#include "mhw_reader.h"

#include <QCoreApplication>

#include <cstdio>
#include <cinttypes>
#include <cmath>
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
    const std::uintptr_t equipAbs = imageBase + 0x050139A0ULL;
    const std::vector<std::uintptr_t> equipOff = {0x50ULL, 0x80ULL, 0x80ULL, 0x18ULL, 0x460ULL};
    const std::uintptr_t equipBase = mhw::MhwReader::followPointerChain(mem, equipAbs, equipOff, nullptr);
    std::printf("equipBase=0x%" PRIxPTR "\n", equipBase);

    if (!equipBase) return 3;

    // Dump 0x300 bytes starting at -0x40 and ending at +0x2C0 from equipBase.
    // Equipped mantle IDs are 2 ints (4 bytes each). HunterPie says +0x34,
    // but it could be elsewhere on this Wine/Proton build.
    constexpr std::uintptr_t kStart = 0x900;     // start offset relative to equipBase (the cooldowns array lives at +0x99C)
    constexpr std::size_t kLen = 0x300;        // dump length

    char raw[kLen] = {0};
    if (!mem.readBytes(equipBase + kStart, raw, kLen, nullptr)) {
        std::printf("read failed\n");
        return 4;
    }

    std::printf("\n=== equipBase + 0x900..0xC00 (timer/cooldown region) ===\n");
    std::printf("Looking for non-zero float timers in the +0xA8C area (timer arrays).\n\n");

    // Print every 4 bytes as float (since this is the timers region)
    int plausibles = 0;
    for (std::size_t off = 0; off < kLen; off += 4) {
        float v = 0;
        std::memcpy(&v, raw + off, 4);
        const bool plausibleTimer = (std::isfinite(v) && v > 0.0F && v < 1000.0F);
        if (plausibleTimer) {
            std::printf("  +0x%03zx  %8.2f  <-- plausible timer\n", off + kStart, v);
            ++plausibles;
        }
    }
    std::printf("\nFound %d plausible timer values in this region.\n", plausibles);
    std::printf("(Expected: equipped mantle's timer should be > 0 here)\n");

    // Also dump the +0x34 area which HunterPie claims holds the equipped IDs
    char raw2[0x80] = {0};
    if (mem.readBytes(equipBase + 0x00ULL, raw2, sizeof(raw2), nullptr)) {
        std::printf("\n=== equipBase + 0x00..0x80 (where HunterPie says mantle IDs live) ===\n");
        for (std::size_t off = 0; off < sizeof(raw2); off += 4) {
            std::int32_t v = 0;
            std::memcpy(&v, raw2 + off, 4);
            std::printf("  +0x%03zx  %10d  0x%08x\n", off, v, (unsigned)v);
        }
    }

    return 0;
}