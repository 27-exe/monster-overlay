// SPDX-License-Identifier: Apache-2.0
// Address discovery for MHW 421810 on Wine/Proton.
//
// The HunterPie map file we ship assumes a specific .bss layout for the
// monster component list, but on GE-Proton 10-34 the .bss offsets drift.
// Strategy: scan the executable image heap for the "em\em" string
// signature. Every big monster's name lives at +0x2A0 of its component
// struct and starts with "em\em" (e.g. "em\em100_00"). The list of live
// monsters is an inline array of pointers near 0x0500CF40 in the
// original build, but the array header we actually want contains the
// live component addresses themselves, not the 0x138 indirection.
//
// Plan: scan the entire [imageBase, imageBase+0x10000000) image range in
// 1 MB chunks. In each chunk, search for 8-byte aligned 0x40-aligned
// pointers that point at addresses holding "em\em" at +0x2A0. We also
// search for "em\em" directly to identify candidate component structs.
// For each candidate component, print pointer + name + id + HP. This is
// enough to deduce the live monster list location and the array base.

#include "mhw_reader.h"

#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <cstring>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <vector>

namespace {

constexpr std::size_t kScanBytes = 0x10000000; // 256 MB
constexpr std::size_t kChunkBytes = 0x100000;   // 1 MB
constexpr std::uintptr_t kNameOffset = 0x2A0;
constexpr std::uintptr_t kIdOffset = 0x12280;
constexpr std::uintptr_t kHealthPtr = 0x7670;
constexpr std::uintptr_t kHealthSlot = 0x60;
constexpr std::uintptr_t kInnerPtr = 0x138;

bool isBigMonsterName(const char *name)
{
    // "em\em*" is the big-monster family; "em\ems*" is small monsters / palico
    // handlers that HunterPie explicitly filters out of the big-monster list.
    return std::strstr(name, "em\\em") != nullptr
        && std::strstr(name, "em\\ems") == nullptr;
}

bool containsEmEm(const mhw::ProcessMemory &memory, std::uintptr_t candidate, QString *err)
{
    char buf[16] = {0};
    if (!memory.readBytes(candidate + kNameOffset, buf, sizeof(buf) - 1, err))
        return false;
    return isBigMonsterName(buf);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const auto pidOpt = mhw::MhwReader::findGamePid();
    if (!pidOpt) {
        std::cout << "no MHW process found\n";
        return 1;
    }
    const qint64 pid = *pidOpt;
    mhw::ProcessMemory memory;
    QString err;
    if (!memory.attach(pid, &err)) {
        std::cout << "attach failed: " << err.toStdString() << '\n';
        return 2;
    }

    // image base. We collect all readable mappings inside the PE virtual
    // address range (0x140000000 .. 0x14FFFFFFF on this build) so we
    // include the .text/.rdata/.bss anonymous rows the loader placed
    // after the 4 KB PE header row. HunterPie uses an internal scan
    // service to do exactly this on Windows; we do the same by hand here.
    QFile maps(QStringLiteral("/proc/%1/maps").arg(pid));
    if (!maps.open(QIODevice::ReadOnly)) {
        std::cout << "cannot open maps\n";
        return 3;
    }
    const QByteArray raw = maps.readAll();
    std::uintptr_t base = 0;
    std::uintptr_t baseEnd = 0;
    std::uintptr_t rangeStart = 0;
    std::uintptr_t rangeEnd = 0;
    for (const QByteArray &lineBytes : raw.split('\n')) {
        const QList<QByteArray> fields = lineBytes.split(' ');
        if (fields.size() < 3)
            continue;
        const QList<QByteArray> halves = fields[0].split('-');
        if (halves.size() != 2)
            continue;
        bool okStart = false, okEnd = false;
        const qulonglong start = halves[0].toULongLong(&okStart, 16);
        const qulonglong end = halves[1].toULongLong(&okEnd, 16);
        if (!okStart || !okEnd)
            continue;
        const std::uintptr_t startAddr = static_cast<std::uintptr_t>(start);
        const std::uintptr_t endAddr = static_cast<std::uintptr_t>(end);
        // Restrict to the PE image region (0x140000000..0x14FFFFFFF).
        if (startAddr < 0x140000000ULL || startAddr >= 0x150000000ULL)
            continue;
        if (rangeStart == 0 || startAddr < rangeStart)
            rangeStart = startAddr;
        if (endAddr > rangeEnd)
            rangeEnd = endAddr;
        // The PE header row carries "MonsterHunterWorld.exe" with offset 0.
        const QList<QByteArray> tail = fields.mid(3);
        bool hasMonster = false;
        for (const QByteArray &f : tail) {
            if (f.toLower().contains("monsterhunterworld.exe")) {
                hasMonster = true;
                break;
            }
        }
        if (hasMonster) {
            bool okOffset = false;
            const qulonglong off = fields[2].toULongLong(&okOffset, 16);
            if (okOffset && off == 0)
                base = startAddr;
        }
    }
    if (rangeStart == 0 || rangeEnd == 0) {
        std::cout << "no PE image rows found\n";
        return 4;
    }
    if (base == 0)
        base = rangeStart;
    baseEnd = rangeEnd;
    std::cout << "PE base 0x" << std::hex << base << " - 0x" << baseEnd
              << " (" << std::dec << ((baseEnd - base) / 1024 / 1024) << " MB; max scan 0x"
              << std::hex << (base + kScanBytes) << ")\n" << std::dec;

    // The candidate from the .rdata string scan was a red herring; the
    // first followPointerChain output from mhw-overlay was 0x27b97798
    // (heap allocation), with slot 0 = 0x81b62a40 (empty name). The
    // mhw-probe found deref(0x0500CF40) = 0x27b97760 directly, so
    // probe the heap monster struct at 0x81b62a40 with a generous set
    // of field candidates and print floats near 184784 / 19868.
    const std::uintptr_t target = 0x81b62a40ULL;
    if (target >= 0x10000 && target < 0x0000800000000000ULL) {
        char nameBuf[64] = {0};
        memory.readBytes(target + kNameOffset, nameBuf, sizeof(nameBuf) - 1, nullptr);
        std::cout << "\n[probe] target=0x" << std::hex << target
                  << " name=\"" << nameBuf << "\"\n" << std::dec;
        const std::uintptr_t offsets[] = {
            0x60, 0x64, 0x12c, 0x130, 0x138, 0x160, 0x184,
            0x2A0, 0x7670, 0x7670 + 0x60,
            0x61A0, 0x61A8, 0x61B0, 0x61B8,
            0x12A00, 0x12A60, 0x12A80,
            0x12280, 0x1BE30, 0x1C0F0,
            0x6D0, 0x700, 0x720
        };
        for (const std::uintptr_t off : offsets) {
            char raw[16] = {0};
            if (!memory.readBytes(target + off, raw, sizeof(raw) - 1, nullptr)) {
                std::printf("    [+0x%lx] read failed\n", off);
                continue;
            }
            const auto p0 = *reinterpret_cast<std::uintptr_t *>(raw);
            float f0 = 0.0F, f1 = 0.0F;
            std::memcpy(&f0, raw, 4);
            std::memcpy(&f1, raw + 4, 4);
            std::printf("    [+0x%lx] ptr=0x%016lx  f0=%.6g  f1=%.6g  raw=", off, p0, f0, f1);
            for (int i = 0; i < 16; ++i)
                std::printf("%02x ", static_cast<unsigned char>(raw[i]));
            std::printf("\n");
        }
    } else {
        std::cout << "target 0x" << std::hex << target << " not a valid userspace address\n";
    }

    // Strategy A: scan the entire image (or until the highest
    // MonsterHunterWorld.exe mapping) for "em\em" substrings, then verify
    // each as a candidate monster struct.
    std::vector<char> buffer(kChunkBytes);
    int nameHits = 0;
    int verified = 0;
    int bigMonster = 0;
    const std::uintptr_t scanEnd = std::min<std::uintptr_t>(base + kScanBytes, baseEnd);
    for (std::uintptr_t baseAddr = base; baseAddr < scanEnd; baseAddr += kChunkBytes) {
        const std::size_t want = std::min<std::size_t>(kChunkBytes, scanEnd - baseAddr);
        if (!memory.readBytes(baseAddr, buffer.data(), want, &err)) {
            std::cout << "chunk @ 0x" << std::hex << baseAddr << " read failed: "
                      << err.toStdString() << std::dec << '\n';
            err.clear();
            continue;
        }
        for (std::size_t i = 0; i + 16 < want; ++i) {
            if (buffer[i] != 'e' || buffer[i + 1] != 'm' || buffer[i + 2] != '\\' || buffer[i + 3] != 'e' || buffer[i + 4] != 'm')
                continue;
            ++nameHits;
            const std::uintptr_t candidate = baseAddr + i - kNameOffset;
            if (candidate < base || candidate > baseEnd)
                continue;
            // Health pointer must be readable and inside our region.
            const auto hpPtr = memory.read<std::uintptr_t>(candidate + kHealthPtr, nullptr);
            if (!hpPtr || !(*hpPtr >= 0x10000 && *hpPtr < 0x0000800000000000ULL))
                continue;
            const auto hp = memory.readArray<float>(*hpPtr + kHealthSlot, 2, nullptr);
            if (hp.size() != 2)
                continue;
            if (hp[0] <= 0.0F || hp[1] < 0.0F || hp[1] > hp[0])
                continue;
            char nameBuf[64] = {0};
            if (!memory.readBytes(candidate + kNameOffset, nameBuf, sizeof(nameBuf) - 1, nullptr))
                continue;
            if (std::strstr(nameBuf, "em\\ems") != nullptr)
                continue;
            ++verified;
            const auto id = memory.read<std::int32_t>(candidate + kIdOffset, nullptr);
            std::cout << "candidate component @ 0x" << std::hex << candidate
                      << " name=\"" << nameBuf << "\""
                      << " id=" << std::dec << (id ? *id : -1)
                      << " hp=" << hp[1] << "/" << hp[0];
            if (hp[0] > 1000.0F)
                ++bigMonster;
            std::cout << '\n';
        }
    }
    std::cout << "\nscanned " << (scanEnd - base) / 1024 / 1024 << " MB; "
              << "nameHits=" << nameHits
              << " verified=" << verified
              << " bigMonster=" << bigMonster << '\n';

    // Probe the canonical HunterPie 0x0500CF40 + 0x38 location for comparison
    // and to extract the big-monster count.
    {
        const std::uintptr_t listAddr = base + 0x0500CF40ULL;
        const std::uintptr_t countPtr = listAddr + 0x8;
        if (const auto count = memory.read<std::int32_t>(countPtr, nullptr)) {
            std::cout << "\nhunter-pie list@0x0500CF40: big-monster count=" << *count << '\n';
        }
    }
    return 0;
}
