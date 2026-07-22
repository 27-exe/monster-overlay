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
#include <QRegularExpression>
#include <QString>
#include <QTextStream>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

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

    // Open /proc/<pid>/maps to find the PE base. This is read-only on the
    // caller's side and does not require ptrace access.
    const QString mapsPath = QStringLiteral("/proc/%1/maps").arg(pid);
    QFile maps(mapsPath);
    if (!maps.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cout << "cannot open " << mapsPath.toStdString() << ": " << maps.errorString().toStdString() << '\n';
        return 3;
    }
    std::uintptr_t base = 0;
    int matchLines = 0;
    QTextStream in(&maps);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (!line.toLower().contains("monsterhunterworld.exe"))
            continue;
        ++matchLines;
        const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (fields.size() < 3)
            continue;
        const QStringList range = fields[0].split('-');
        if (range.size() != 2)
            continue;
        bool okStart = false, okOffset = false;
        const qulonglong start = range[0].toULongLong(&okStart, 16);
        const qulonglong offset = fields[2].toULongLong(&okOffset, 16);
        if (!okStart || !okOffset)
            continue;
        if (offset == 0 && base == 0)
            base = static_cast<std::uintptr_t>(start);
    }
    if (matchLines == 0) {
        std::cout << "maps contains no MonsterHunterWorld.exe row\n";
        return 4;
    }
    if (base == 0) {
        std::cout << "maps contain MonsterHunterWorld.exe but no offset=0 row (PE base hidden by loader); try fallback\n";
        return 5;
    }
    std::cout << "imageBase = 0x" << std::hex << base << std::dec << " (" << matchLines << " matching rows)\n";

    // Try to read 32 bytes at the alleged MONSTER_LIST_ADDRESS = 0x0500CF40.
    const std::uintptr_t probe = base + 0x0500CF40ULL;
    char buffer[32] = {0};
    if (memory.readBytes(probe, buffer, sizeof(buffer), &err)) {
        std::cout << "read 32 bytes @ 0x" << std::hex << probe << " ok\n";
        for (size_t i = 0; i < sizeof(buffer); i += 8) {
            std::cout << "  +" << std::hex << i << ":";
            for (size_t j = 0; j < 8; ++j)
                std::cout << ' ' << std::hex << (static_cast<unsigned int>(static_cast<unsigned char>(buffer[i + j])));
            std::cout << std::dec << '\n';
        }
    } else {
        std::cout << "read 32 bytes @ 0x" << std::hex << probe << " failed: "
                  << err.toStdString() << '\n';
    }
    return 0;
}
