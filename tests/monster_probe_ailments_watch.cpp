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
// Usage: ./build/monster-probe-ailments-watch /tmp/ailments.log
//
// Stop with Ctrl-C; the log is appended on every iteration, so search
// backward through it for `active=1 id=N` once you've had a trigger.

#include "mhw_reader.h"

#include <QCoreApplication>
#include <QDateTime>

#ifndef MHW_DEFAULT_MAP
#define MHW_DEFAULT_MAP "data/MonsterHunterWorld.421810.map"
#endif

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

    // v0.8.4-r23: the sharpness dump below mirrors MhwReader::readSharpness,
    // so it needs the same address map the reader loads.
    mhw::AddressMap map;
    QString mapErr;
    if (!map.load(QStringLiteral(MHW_DEFAULT_MAP), &mapErr)) {
        std::fprintf(g_log, "map load failed: %s (run from the repo root)\n",
                     mapErr.toStdString().c_str());
        return 5;
    }

    int tick = 0;
    while (!g_stop) {
        // ---- v0.8.4-r23: player sharpness raw dump ----
        // Prints the exact inputs behind the panel's sharpness bar every tick:
        // weapon id, raw counter +0x20F8, level field +0x20FC, MaxLevel
        // +0x1D10, the 7 shorts of the weapon sharpness array, the level the
        // upstream derivation (MHWGameUtils.GetCurrentSharpness) picks, and
        // what MhwReader::readSharpness would publish. Used to find out why
        // a purple weapon renders blue after the r23 derivation patch.
        {
            const std::uintptr_t weaponBase =
                imageBase + map.address(QStringLiteral("WEAPON_ADDRESS"));
            const std::uintptr_t sharpPtr = mhw::MhwReader::followPointerChain(
                mem, weaponBase,
                map.offsets(QStringLiteral("WEAPON_SHARPNESS_OFFSETS")), nullptr);

            std::fprintf(g_log, "[sharpness] %s weaponBase=0x%lx sharpPtr=0x%lx\n",
                         qPrintable(QDateTime::currentDateTime().toString(
                                 QStringLiteral("yyyy-MM-dd HH:mm:ss"))),
                         (unsigned long)weaponBase, (unsigned long)sharpPtr);
            if (sharpPtr) {
                const auto raw   = mem.read<std::int32_t>(sharpPtr + 0x20F8ULL, nullptr);
                const auto field = mem.read<std::int32_t>(sharpPtr + 0x20FCULL, nullptr);
                const auto maxLv = mem.read<std::int32_t>(sharpPtr + 0x1D10ULL, nullptr);

                int weaponId = -1;
                const std::uintptr_t idPtr = mhw::MhwReader::followPointerChain(
                    mem, weaponBase,
                    map.offsets(QStringLiteral("WEAPON_ID_OFFSETS")), nullptr);
                if (idPtr) {
                    if (const auto w = mem.read<std::int32_t>(idPtr, nullptr))
                        weaponId = *w;
                }

                std::int16_t th[7] = {0};
                bool haveTh = false;
                // r23: WEAPON_ID_OFFSETS yields the weapon-DATA table row
                // (hundreds), not the 0..13 type enum — index with it raw.
                if (weaponId >= 0 && weaponId <= 8191) {
                    const std::uintptr_t dataPtr = mhw::MhwReader::followPointerChain(
                        mem,
                        imageBase + map.address(QStringLiteral("WEAPON_DATA_ADDRESS")),
                        map.offsets(QStringLiteral("WEAPON_DATA_OFFSETS")),
                        nullptr);
                    if (dataPtr) {
                        const std::uintptr_t arrayPtr = mhw::MhwReader::followPointerChain(
                            mem, dataPtr,
                            {static_cast<std::uintptr_t>(weaponId) * 8ULL, 0xCULL},
                            nullptr);
                        if (arrayPtr) {
                            const auto shorts =
                                mem.readArray<std::int16_t>(arrayPtr, 7, nullptr);
                            if (shorts.size() == 7) {
                                std::memcpy(th, shorts.data(), 7 * sizeof(std::int16_t));
                                haveTh = true;
                            }
                        }
                    }
                }

                int derived = -1;
                if (raw && haveTh) {
                    for (int i = 6; i > 0; --i) {
                        if (th[i] == 0)
                            continue;
                        if (*raw > th[i - 1] && *raw <= th[i]) {
                            derived = i;
                            break;
                        }
                    }
                }
                const int ourLevel = (derived >= 0) ? derived : (field ? *field : -1);
                const int ourThr =
                    (ourLevel >= 1 && ourLevel <= 6) ? th[ourLevel - 1] : 0;
                const int ourRemaining =
                    (raw && ourLevel >= 1) ? (*raw - ourThr) : -1;

                std::fprintf(g_log, "  weaponId=%d raw=%d field=%d maxLevel=%d haveTh=%d\n",
                             weaponId, raw ? *raw : -424242, field ? *field : -424242,
                             maxLv ? *maxLv : -424242, haveTh ? 1 : 0);
                if (haveTh)
                    std::fprintf(g_log, "  th_short[7]=%d %d %d %d %d %d %d\n",
                                 th[0], th[1], th[2], th[3], th[4], th[5], th[6]);
                std::fprintf(g_log,
                             "  derived=%d ourLevel=%d ourThreshold=%d ourRemaining=%d\n",
                             derived, ourLevel, ourThr, ourRemaining);

                char win[0x20] = {0};
                if (mem.readBytes(sharpPtr + 0x20E0ULL, win, sizeof(win), nullptr)) {
                    std::fprintf(g_log, "  win 0x20E0..0x2100:\n");
                    hexDump(g_log, 0x20E0ULL, win, sizeof(win));
                }
                char win2[0x20] = {0};
                if (mem.readBytes(sharpPtr + 0x1D00ULL, win2, sizeof(win2), nullptr)) {
                    std::fprintf(g_log, "  win 0x1D00..0x1D20:\n");
                    hexDump(g_log, 0x1D00ULL, win2, sizeof(win2));
                }
            }
        }

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

        // v0.8.4-r23: World monster stamina (MHWMonster.GetMonsterStaminaData
        // reads _address + 0x1C0F0, {Stamina, MaxStamina}). Correlate with
        // the id-23 CLAWFLINCH gauge for the "100% but no flinch" report.
        if (const auto stam = mem.readArray<float>(monster + 0x1C0F0ULL, 2, nullptr);
            stam.size() == 2) {
            std::fprintf(g_log, "  stamina=%.4f maxStamina=%.4f\n", stam[0], stam[1]);
        }

        // Dump the inline region 0x1BC00..0x1BE00 (0x200 bytes) hex
        char rawBlock[0x200] = {0};
        if (mem.readBytes(monster + 0x1BC00ULL, rawBlock, sizeof(rawBlock), nullptr)) {
            std::fprintf(g_log, "  inline 0x1BC00..0x1BE00:\n");
            hexDump(g_log, 0x1BC00ULL, rawBlock, sizeof(rawBlock));
        }

        // v0.8.4-r23: 32 entries, matching the reader's own cap. 8 was
        // too few — ids 14/15/16/22/23 sit past slot 7 and were never
        // captured by the old loop.
        std::fprintf(g_log, "  ptr array @ +0x1BC40:\n");
        for (int i = 0; i < 32; ++i) {
            const std::uintptr_t cursor = monster + 0x1BC40ULL
                                        + std::uintptr_t(i) * sizeof(std::uintptr_t);
            const auto entry = mem.read<std::uintptr_t>(cursor, nullptr);
            if (!entry || *entry < 0x10000) {
                std::fprintf(g_log, "    [%d] no entry\n", i);
                continue;
            }
            std::fprintf(g_log, "    [%d] -> 0x%" PRIxPTR "\n", i, (unsigned long)*entry);

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

                float f14 = 0, f18 = 0, f1C = 0, f30 = 0, f34 = 0, f40 = 0,
                      f50 = 0, f5C = 0, f70 = 0;
                std::memcpy(&f14, raw + 0x14, 4);
                std::memcpy(&f18, raw + 0x18, 4);
                std::memcpy(&f1C, raw + 0x1C, 4);
                // v0.8.4-r23: the fields the reader consumes.
                std::memcpy(&f30, raw + 0x30, 4);
                std::memcpy(&f40, raw + 0x40, 4);
                std::memcpy(&f34, raw + 0x34, 4);
                std::memcpy(&f50, raw + 0x50, 4);
                std::memcpy(&f5C, raw + 0x5C, 4);
                std::memcpy(&f70, raw + 0x70, 4);

                std::int32_t counter = 0;
                // v0.8.4-r23: Counter is at +0x78 (old +0x74 was Unk25).
                std::memcpy(&counter, raw + 0x78, 4);

                const bool ownerOk = (owner == (std::int64_t)monster);
                const bool looksActive = (active != 0);
                const bool looksReasonable = (std::abs(f14) > 0.001F && std::abs(f14) < 600.0F);
                const int flags = (ownerOk ? 1 : 0) | (looksActive ? 2 : 0) | (looksReasonable ? 4 : 0);
                std::fprintf(g_log,
                             "      soff=0x%lx  %s  active=%d unk1=%d id=%d cnt=%d  "
                             "maxDur=%.2f buildup=%.2f maxBuildup=%.2f duration=%.2f  "
                             "f18=%.2f f1C=%.2f f34=%.2f f50=%.2f f5C=%.2f\n",
                             (unsigned long)soff,
                             (flags == 7) ? "★MATCH" : "    ",
                             active, unk1, id, counter,
                             f14, f30, f40, f70,
                             f18, f1C, f34, f50, f5C);
            }
        }
        std::fprintf(g_log, "\n");
        ++tick;
        ::sleep(1);
    }

    std::fclose(g_log);
    return 0;
}