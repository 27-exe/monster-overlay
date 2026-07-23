// SPDX-License-Identifier: Apache-2.0

#include "mhw_reader.h"

#include <QCoreApplication>
#include <QTemporaryFile>

#include <array>
#include <cstdint>
#include <iostream>

namespace mhw {
// Exposed here for the schema sanity test below.
extern const QHash<int, QVector<PartSchema>> kPartSchemas;
}

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition) {
        std::cout << "PASS: " << message << '\n';
    } else {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryFile mapFile;
    check(mapFile.open(), "temporary map opens");
    const QByteArray map = R"MAP(
# fixture
Address ROOT 0x1234
Offset CHAIN 0x10,0x20,0x0
Address OTHER 0xCAFE # inline comment
)MAP";
    mapFile.write(map);
    mapFile.flush();

    mhw::AddressMap parsed;
    QString error;
    check(parsed.load(mapFile.fileName(), &error), "legacy map parses");
    check(parsed.address(QStringLiteral("ROOT")) == 0x1234, "address value parsed");
    check(parsed.address(QStringLiteral("OTHER")) == 0xCAFE, "inline comment ignored");
    check(parsed.offsets(QStringLiteral("CHAIN")).size() == 3, "offset chain length parsed");
    check(parsed.offsets(QStringLiteral("CHAIN"))[1] == 0x20, "offset chain value parsed");

    mhw::ProcessMemory ownMemory;
    check(ownMemory.attach(QCoreApplication::applicationPid(), &error), "reader opens own /proc/pid/mem");
    const std::uint64_t marker = 0x1122334455667788ULL;
    const auto observed = ownMemory.read<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&marker), &error);
    check(observed.has_value(), "process_vm_readv reads own process");
    check(observed && *observed == marker, "read value is exact");

    const std::uintptr_t markerAddress = reinterpret_cast<std::uintptr_t>(&marker);
    const std::uintptr_t levelTwo = markerAddress;
    const std::uintptr_t levelOne = reinterpret_cast<std::uintptr_t>(&levelTwo);
    const auto chained = mhw::MhwReader::followPointerChain(
        ownMemory,
        reinterpret_cast<std::uintptr_t>(&levelOne),
        {0, 0},
        &error);
    check(chained == markerAddress, "HunterPie pointer-chain semantics match");
    const auto chainedValue = ownMemory.read<std::uint64_t>(chained, &error);
    check(chainedValue && *chainedValue == marker, "pointer chain resolves readable target");

    check(ownMemory.imageBase(nullptr) == 0, "non-MHW fixture has no MHW image base");

    // Schema must include severable parts. Without them, multi-player quests
    // show 100% HP on severable parts (tails, horns, charges) because the
    // normal table is only populated on the host's client.
    check(mhw::kPartSchemas.size() == 72, "schema covers 72 monsters");
    int severableTotal = 0;
    for (auto it = mhw::kPartSchemas.cbegin(); it != mhw::kPartSchemas.cend(); ++it) {
        for (const auto &p : it.value()) {
            if (p.isSeverable) ++severableTotal;
        }
    }
    check(severableTotal == 120, "schema contains 120 severable parts (HunterPie MonsterData.xml)");
    // Tigrex (Id=94) has charge horn + breakable tail as severable.
    const auto tigrex = mhw::kPartSchemas.value(94);
    check(tigrex.size() >= 2, "Tigrex (94) schema has entries");
    int tigrexSev = 0;
    for (const auto &p : tigrex) if (p.isSeverable) ++tigrexSev;
    check(tigrexSev == 2, "Tigrex has 2 severable parts (PART_CHARGE + PART_TAIL)");

    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
