// SPDX-License-Identifier: Apache-2.0
// Probe the live monster's ailment pointer chain.
//
// HunterPie GetMonsterAilments() walks monster+0x1BC40 as a pointer
// array; each entry +0x148 is MHWMonsterAilmentStructure. We don't
// know which offsets are correct on 421810, so this binary dumps
// BOTH candidates:
//
//   Candidate A: array base at monster+0x1BC40, struct at *(ptr)+0x148
//   Candidate B: array base at monster+0x1BC40, struct at *(ptr)+0x00
//   Candidate C: array base at monster+0x1BC40, struct at *(ptr)+0x10
//   Candidate D: single struct INLINE at monster+0x1BE30 (this is
//                where MHWMonsterStatusStructure/enrage lives —
//                lets us verify whether ailments share the inline
//                layout or the pointer-chain one)
//
// For each candidate we print 16 bytes as Owner/IsActive/Id header +
// MaxDuration / Duration floats + their sanity. If sleep was just
// triggered (so the monster is asleep right now), one of these will
// have IsActive != 0 and Duration > 0.

#include "mhw_reader.h"

#include <QCoreApplication>
#include <QFile>

#include <cstdio>
#include <cinttypes>
#include <cstring>
#include <unistd.h>

namespace {

struct Candidate {
    const char *label;
    std::uintptr_t arrayBase;   // read pointer array from here
    std::uintptr_t structOff;   // add this to the dereferenced pointer
};

void dumpCandidate(mhw::ProcessMemory &mem, std::uintptr_t monster,
                   const Candidate &c, int maxElems)
{
    std::printf("\n=== %s ===\n", c.label);
    std::printf("  array @ monster+0x%lx\n", (unsigned long)c.arrayBase);

    for (int i = 0; i < maxElems; ++i) {
        const std::uintptr_t cursor = monster + c.arrayBase
                                    + std::uintptr_t(i) * sizeof(std::uintptr_t);
        const auto entry = mem.read<std::uintptr_t>(cursor, nullptr);
        if (!entry || *entry < 0x10000) {
            std::printf("  [%2d] no entry (0x%" PRIxPTR ")\n", i,
                        entry ? (qulonglong)*entry : 0ULL);
            continue;
        }
        const std::uintptr_t structAddr = *entry + c.structOff;

        // Read 0x80 bytes raw
        char raw[0x80] = {0};
        if (!mem.readBytes(structAddr, raw, sizeof(raw), nullptr)) {
            std::printf("  [%2d] @ 0x%" PRIxPTR "  read failed\n", i, (unsigned long)structAddr);
            continue;
        }

        std::int64_t owner = 0;
        std::int32_t active = 0, unk1 = 0, id = 0;
        std::memcpy(&owner, raw + 0x00, 8);
        std::memcpy(&active, raw + 0x08, 4);
        std::memcpy(&unk1, raw + 0x0C, 4);
        std::memcpy(&id,    raw + 0x10, 4);

        // Try common float positions for MaxDuration
        float f14 = 0, f18 = 0, f1C = 0, f34 = 0, f50 = 0, f5C = 0, f60 = 0, f64 = 0;
        std::memcpy(&f14, raw + 0x14, 4);
        std::memcpy(&f18, raw + 0x18, 4);
        std::memcpy(&f1C, raw + 0x1C, 4);
        std::memcpy(&f34, raw + 0x34, 4);
        std::memcpy(&f50, raw + 0x50, 4);
        std::memcpy(&f5C, raw + 0x5C, 4);
        std::memcpy(&f60, raw + 0x60, 4);
        std::memcpy(&f64, raw + 0x64, 4);

        std::int32_t counter = 0;
        std::memcpy(&counter, raw + 0x74, 4);

        const std::int64_t monsterI = static_cast<std::int64_t>(monster);
        const bool ownerOk = (owner == monsterI);

        std::printf("  [%2d] struct=0x%" PRIxPTR "  owner=%s  active=%d unk1=%d id=%d "
                    "counter=%d\n",
                    i, (unsigned long)structAddr,
                    ownerOk ? "OK" : "MISMATCH",
                    active, unk1, id, counter);
        std::printf("       floats  0x14=%6.2f  0x18=%6.2f  0x1C=%6.2f  0x34=%6.2f  0x50=%6.2f  0x5C=%6.2f  0x60=%6.2f  0x64=%6.2f\n",
                    f14, f18, f1C, f34, f50, f5C, f60, f64);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const auto pidOpt = mhw::MhwReader::findGamePid();
    if (!pidOpt) { std::printf("no MHW process\n"); return 1; }
    const qint64 pid = *pidOpt;
    std::printf("MHW pid=%lld\n", static_cast<long long>(pid));

    mhw::ProcessMemory mem;
    QString err;
    if (!mem.attach(pid, &err)) { std::printf("attach: %s\n", err.toStdString().c_str()); return 2; }

    const std::uintptr_t imageBase = mem.imageBase(nullptr);
    std::printf("imageBase=0x%" PRIxPTR "\n", imageBase);

    // Walk MonsterList -> Component -> Monster the same way readMonsters does
    const std::uintptr_t listAddr = imageBase + 0x0500CF40ULL;
    const auto headPtr = mem.read<std::uintptr_t>(listAddr, nullptr);
    if (!headPtr) { std::printf("MonsterList head read failed\n"); return 3; }
    const std::uintptr_t compsBase = *headPtr + 0x38ULL;
    const auto comps = mem.readArray<std::uintptr_t>(compsBase, 128, nullptr);
    if (comps.size() != 128) { std::printf("components read failed\n"); return 4; }

    // Find live monster (HP > 1000, name non-empty)
    std::uintptr_t monster = 0;
    int idx = -1;
    for (int i = 0; i < 128; ++i) {
        const std::uintptr_t comp = comps[i];
        if (comp < 0x10000 || comp >= 0x0000800000000000ULL) continue;
        const auto inner = mem.read<std::uintptr_t>(comp + 0x138ULL, nullptr);
        if (!inner || *inner < 0x10000) continue;
        const auto hpPtr = mem.read<std::uintptr_t>(*inner + 0x7670ULL, nullptr);
        if (!hpPtr) continue;
        if (*hpPtr < 0x10000 || *hpPtr >= 0x0000800000000000ULL) continue;
        const auto hp = mem.readArray<float>(*hpPtr + 0x60ULL, 2, nullptr);
        if (hp.size() != 2 || hp[0] <= 1000.0F) continue;
        monster = *inner;
        idx = i;
        break;
    }
    if (monster == 0) { std::printf("no live monster\n"); return 5; }

    // Print name
    char nameBuf[64] = {0};
    const auto nameStruct = mem.read<std::uintptr_t>(monster + 0x2A0ULL, nullptr);
    if (nameStruct && *nameStruct >= 0x10000)
        mem.readBytes(*nameStruct + 0xCULL, nameBuf, sizeof(nameBuf) - 1, nullptr);
    std::printf("live monster slot[%d] @ 0x%" PRIxPTR " em=\"%s\"\n",
                idx, monster, nameBuf);

    // Read schema Id for cross-check
    const auto id = mem.read<std::int32_t>(monster + 0x12280ULL, nullptr);
    std::printf("schemaId=%d\n", id ? *id : -1);

    const Candidate candidates[] = {
        {"A: array @ +0x1BC40, struct @ *(ptr)+0x148", 0x1BC40ULL, 0x148ULL},
        {"B: array @ +0x1BC40, struct @ *(ptr)+0x000", 0x1BC40ULL, 0x000ULL},
        {"C: array @ +0x1BC40, struct @ *(ptr)+0x010", 0x1BC40ULL, 0x010ULL},
        {"D: array @ +0x1BC40, struct @ *(ptr)+0x080", 0x1BC40ULL, 0x080ULL},
        {"E: array @ +0x1BC40, struct @ *(ptr)+0x0A0", 0x1BC40ULL, 0x0A0ULL},
        {"F: array @ +0x1BB40, struct @ *(ptr)+0x148", 0x1BB40ULL, 0x148ULL},
        {"G: array @ +0x1BC00, struct @ *(ptr)+0x148", 0x1BC00ULL, 0x148ULL},
        {"H: array @ +0x1BC80, struct @ *(ptr)+0x148", 0x1BC80ULL, 0x148ULL},
        {"I: array @ +0x1BC40, struct @ *(ptr)+0x0C0", 0x1BC40ULL, 0x0C0ULL},
    };
    for (const auto &c : candidates) {
        dumpCandidate(mem, monster, c, 8);
    }

    // Also dump the inline region around 0x1BE30 to check whether
    // ailments could be inline like enrage (no — enrage is its own struct)
    std::printf("\n=== inline region 0x1BE30..0x1BF30 ===\n");
    char raw[0x100] = {0};
    if (mem.readBytes(monster + 0x1BE30ULL, raw, sizeof(raw), nullptr)) {
        for (int off = 0; off < 0x100; off += 0x10) {
            std::printf("  +0x%04x:", 0x1BE30 + off);
            for (int j = 0; j < 0x10; j += 4) {
                std::int32_t v = 0;
                std::memcpy(&v, raw + off + j, 4);
                std::printf(" %08x", (unsigned)v);
            }
            std::printf("\n");
        }
    }

    std::printf("\ndone. Trigger sleep/paralysis on the monster, then re-run to see 'active=1 id=N'.\n");
    return 0;
}