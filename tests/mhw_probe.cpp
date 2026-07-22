// SPDX-License-Identifier: Apache-2.0
// Quick diagnostic tool: locate the MHW PE, attempt to read its image base
// and a few bytes from the alleged MONSTER_LIST_ADDRESS. Prints errno and
// the raw buffer when a read fails. Use this to disambiguate
//   - permission (EPERM / EACCES via process_vm_readv or /proc/<pid>/mem)
//   - wrong image base (EIO / EFAULT)
//   - wrong map (no MonsterHunterWorld.exe row in /proc/<pid>/maps)

#include "mhw_reader.h"

#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <cstring>
#include <iostream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const auto pidOpt = mhw::MhwReader::findGamePid();
    if (!pidOpt) {
        std::cout << "no MHW process found (no /proc/*/maps contains MonsterHunterWorld.exe)\n";
        return 1;
    }
    const qint64 pid = *pidOpt;
    std::cout << "found MHW PE pid=" << pid << '\n';

    mhw::ProcessMemory memory;
    QString err;
    if (!memory.attach(pid, &err)) {
        std::cout << "attach failed: " << err.toStdString() << '\n';
        return 2;
    }

    // Find PE image base.
    const QString mapsPath = QStringLiteral("/proc/%1/maps").arg(pid);
    QFile maps(mapsPath);
    if (!maps.open(QIODevice::ReadOnly)) {
        std::cout << "cannot open " << mapsPath.toStdString() << ": " << maps.errorString().toStdString() << '\n';
        return 3;
    }
    const QByteArray raw = maps.readAll();
    std::uintptr_t base = 0;
    int matchLines = 0;
    const QList<QByteArray> lines = raw.split('\n');
    for (const QByteArray &lineBytes : lines) {
        if (!lineBytes.toLower().contains("monsterhunterworld.exe"))
            continue;
        ++matchLines;
        const QList<QByteArray> fields = lineBytes.split(' ');
        if (fields.size() < 3)
            continue;
        const QList<QByteArray> halves = fields[0].split('-');
        if (halves.size() != 2)
            continue;
        bool okStart = false, okOffset = false;
        const qulonglong start = halves[0].toULongLong(&okStart, 16);
        const qulonglong off = fields[2].toULongLong(&okOffset, 16);
        if (!okStart || !okOffset)
            continue;
        if (off == 0 && base == 0)
            base = static_cast<std::uintptr_t>(start);
    }
    if (base == 0) {
        std::cout << "no offset=0 row for MonsterHunterWorld.exe\n";
        return 4;
    }
    std::cout << "imageBase = 0x" << std::hex << base << std::dec << " (" << matchLines << " matching rows)\n";

    // Walk the monster list the same way MhwReader::readMonsters does so we
    // can see exactly where the chain breaks if anything is off.
    constexpr std::uintptr_t kMonsterListAbs = 0x0500CF40ULL;
    constexpr std::size_t kMonsterListStride = 0x38;
    constexpr std::size_t kMaxSlots = 128;
    constexpr std::uintptr_t kMonsterInnerPtr = 0x138;
    constexpr std::uintptr_t kMonsterNameOffset = 0x2A0;
    constexpr std::uintptr_t kMonsterIdOffset = 0x12280;
    constexpr std::uintptr_t kMonsterHealthPtr = 0x7670;
    constexpr std::uintptr_t kMonsterHealthSlot = 0x60;

    const std::uintptr_t componentsBase = base + kMonsterListAbs;
    std::cout << "\n[1] componentsBase 0x" << std::hex << componentsBase << std::dec << " (read 128 pointers)\n";
    const auto componentPointers = memory.readArray<std::uintptr_t>(componentsBase, kMaxSlots, &err);
    if (componentPointers.empty()) {
        std::cout << "    read failed: " << err.toStdString() << '\n';
    } else {
        int live = 0;
        for (std::size_t i = 0; i < componentPointers.size(); ++i) {
            const std::uintptr_t ptr = componentPointers[i];
            if (ptr >= 0x10000 && ptr < 0x0000800000000000ULL)
                ++live;
        }
        std::cout << "    live pointers (sane range): " << live << "/" << componentPointers.size() << '\n';
        for (std::size_t i = 0; i < std::min<std::size_t>(8, componentPointers.size()); ++i) {
            std::cout << "    slot[" << i << "]=0x" << std::hex << componentPointers[i] << std::dec << '\n';
        }
    }

    // For the first 3 live slots, follow the +0x138 pointer, read the 0x2A0 string
    // and the 0x12280 id, and report whether the name starts with "em\\em".
    std::cout << "\n[2] probe first 3 live components for monster name + id + HP\n";
    int monsterCount = 0;
    for (std::size_t i = 0; i < componentPointers.size() && monsterCount < 3; ++i) {
        const std::uintptr_t component = componentPointers[i];
        if (!(component >= 0x10000 && component < 0x0000800000000000ULL))
            continue;
        const auto inner = memory.read<std::uintptr_t>(component + kMonsterInnerPtr, &err);
        if (!inner || !(*inner >= 0x10000 && *inner < 0x0000800000000000ULL)) {
            std::cout << "    slot[" << i << "] inner=invalid (err=" << err.toStdString() << ")\n";
            continue;
        }
        char nameBuf[64] = {0};
        if (!memory.readBytes(*inner + kMonsterNameOffset, nameBuf, sizeof(nameBuf) - 1, &err)) {
            std::cout << "    slot[" << i << "] name read failed: " << err.toStdString() << '\n';
            continue;
        }
        const auto id = memory.read<std::int32_t>(*inner + kMonsterIdOffset, nullptr);
        const auto healthPtr = memory.read<std::uintptr_t>(*inner + kMonsterHealthPtr, nullptr);
        float health[2] = {0.0F, 0.0F};
        if (healthPtr && *healthPtr >= 0x10000 && *healthPtr < 0x0000800000000000ULL) {
            const auto h = memory.readArray<float>(*healthPtr + kMonsterHealthSlot, 2, nullptr);
            if (h.size() == 2) {
                health[0] = h[0];
                health[1] = h[1];
            }
        }
        std::cout << "    slot[" << i << "] inner=0x" << std::hex << *inner
                  << " name=\"" << nameBuf << "\""
                  << " id=" << std::dec << (id ? *id : -1)
                  << " hp=" << health[1] << "/" << health[0]
                  << '\n';
        if (std::strstr(nameBuf, "em\\em") != nullptr)
            ++monsterCount;
    }
    if (monsterCount == 0) {
        std::cout << "    no components produced an \"em\\em*\" name; either we are not in a hunt zone,\n"
                  << "    or the pointer chain (component +0x138 -> monster +0x2A0) is broken.\n";
    }
    return 0;
}
