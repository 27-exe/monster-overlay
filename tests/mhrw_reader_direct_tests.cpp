// SPDX-License-Identifier: Apache-2.0
//
// Direct unit tests for MhwReader's three zero-coverage monster readers.
//
// The v0.11.0 audit found that `MhwReader::readMonsterAilments`,
// `MhwReader::applyTenderizesToParts` and `MhwReader::readMonsters` were never
// reached by CI: they are private members that reach a live
// MonsterHunterWorld.exe through ProcessMemory, and the only tools that ever
// exercised them (the monster-probe* binaries under tests/) are
// EXCLUDE_FROM_ALL and not ctest entries. The Rise side already ran this class
// of code through a fake image (tests/mhr_reader_direct_tests.cpp); this file
// is the World counterpart, using the same mechanism.
//
// The technique — no production seam, no src/ change:
//
//   1. `-fno-access-control`. The private members are still the ones the real
//      translation units compiled; the flag only relaxes access checking in
//      THIS translation unit, so nothing shipped changes and the ABI is
//      untouched.
//   2. A fake World image. One big PROT_NONE reservation whose RW prefix ends
//      exactly at a PROT_NONE guard page. The reader's own map drives it and
//      imageBase_ points at the reservation, so every pointer the reader
//      resolves lands in the RW region and reads succeed; a read that would run
//      past the image lands in the guard page and fails.
//
// The three required paths — normal value / out-of-range index / real read
// failure (including a genuine PARTIAL read into the guard page) — therefore
// go through the same process_vm_readv code path as production, not a stub.
//
// No real game process is involved: every read is against our own pid, and
// findGamePid() is never reached because the private readers are called
// directly instead of through poll()/ensureAttached().
//
// ---------------------------------------------------------------------------
// World vs Rise — the differences this file has to respect
// ---------------------------------------------------------------------------
// The two readers look alike but the World one has three behaviours that
// needed their own treatment (see the section headers below):
//
//   * readMonsters walks the MonsterList with FOUR lambdas capturing
//     memory_/ps (readPartStruct / applyBreakable / the severable scan and
//     its prefix-word branch), and the severable scan `break`s out of a loop
//     nested inside the per-monster loop. The Rise harness unwinds one chain
//     per public entry point; the World one has to drive a whole component
//     tree, so the map-driven setup lives in its own helpers below.
//   * World deliberately does NOT tag PartyMemberKind on the roster it builds
//     (v0.10.8): isMultiplayer comes from the session structure instead.
//     There is nothing to assert about the roster, so this file does not touch
//     readParty.
//   * AddressMap is implemented in src/world/world_reader.cpp, which lives in
//     the mhw-reader library (monster-core's Rise reader must not pull it in),
//     so this target links mhw-reader even though the TU under test is
//     src/world/monster_reader.cpp.
// ---------------------------------------------------------------------------

#include "core/process_memory.h"
#include "core/string_table.h"
#include "monster/monster_types.h"
#include "world/world_reader.h"
#include "world/world_severable_scan.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

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

// --------------------------------------------------------------------------
// The fake World image.
//
// 272 MiB holds every address in data/MonsterHunterWorld.421810.map (the
// largest is 0x051C53F0) plus a high scratch window for the object trees the
// readers dereference into. Only pages actually written get committed, so the
// resident cost stays in the KiB range.
// --------------------------------------------------------------------------
constexpr std::size_t kImageBytes = static_cast<std::size_t>(0x11000000);

// Scratch objects live in a high window of the image, spaced 0x10000 apart so
// no two of them can overlap: a monster struct alone spans ~0x1C500 (ailments
// at +0x1BC40, tenderizes at +0x1C458), and a part table holds 128 tiers at
// 0x1F8 stride. The 4 KiB stride the Rise harness uses is not enough here.
constexpr std::uintptr_t kScratchWindow = 0x10800000ULL;
constexpr std::uintptr_t kObjectStride = 0x10000ULL;

class FakeWorldImage {
public:
    FakeWorldImage()
    {
        const long page = sysconf(_SC_PAGESIZE);
        reserveBytes_ = kImageBytes + static_cast<std::size_t>(page);
        reserve_ = ::mmap(nullptr, reserveBytes_, PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (reserve_ == MAP_FAILED)
            return;
        // RW for the image. The trailing page stays PROT_NONE and becomes the
        // guard that turns overruns into failed reads.
        if (::mprotect(reserve_, kImageBytes, PROT_READ | PROT_WRITE) != 0) {
            ::munmap(reserve_, reserveBytes_);
            reserve_ = nullptr;
        }
    }

    ~FakeWorldImage()
    {
        if (reserve_)
            ::munmap(reserve_, reserveBytes_);
    }

    FakeWorldImage(const FakeWorldImage &) = delete;
    FakeWorldImage &operator=(const FakeWorldImage &) = delete;

    [[nodiscard]] bool valid() const { return reserve_ != nullptr; }

    // imageBase_ for the reader. The map's `Address` values are already
    // absolute, so a zero base offset reproduces them exactly.
    [[nodiscard]] std::uintptr_t imageBase() const
    {
        return reinterpret_cast<std::uintptr_t>(reserve_);
    }

    // Scratch object #index, always the first 0x400 bytes of its own page, so
    // every field offset the readers add stays inside that object.
    [[nodiscard]] std::uintptr_t object(std::size_t index) const
    {
        return imageBase() + kScratchWindow + static_cast<std::uintptr_t>(index)
               * kObjectStride + 0x400;
    }

    // Scratch object #index at an explicit offset into its page.
    [[nodiscard]] std::uintptr_t objectAt(std::size_t index, std::uintptr_t slot) const
    {
        return imageBase() + kScratchWindow + static_cast<std::uintptr_t>(index)
               * kObjectStride + slot;
    }

    // First address INSIDE the guard page: any read starting here fails.
    [[nodiscard]] std::uintptr_t guardAddress() const
    {
        return imageBase() + kImageBytes;
    }

    // The struct address such that a 4-byte read at `offset` crosses into the
    // guard page: the first 2 bytes copy, then EFAULT, so process_vm_readv
    // returns a PARTIAL count. That is the only way ProcessMemory::readBytes()
    // can produce `reader.partial_read_failed` — the branch a failed (as
    // opposed to merely absent) read takes.
    [[nodiscard]] std::uintptr_t straddlingStruct(std::uintptr_t offset) const
    {
        return guardAddress() - offset - 2;
    }

    template <typename T>
    void put(std::uintptr_t address, const T &value)
    {
        std::memcpy(reinterpret_cast<void *>(address), &value, sizeof(T));
    }

    void putPointer(std::uintptr_t address, std::uintptr_t target)
    {
        put<std::uintptr_t>(address, target);
    }

    // Raw byte store: the part / tenderize tables are read as bytes and
    // memcpy'd out at non-zero offsets, so writing a struct would not lay the
    // fields out the way the reader expects.
    void putBytes(std::uintptr_t address, const void *data, std::size_t size)
    {
        std::memcpy(reinterpret_cast<void *>(address), data, size);
    }

    void putF32(std::uintptr_t address, float value)
    {
        std::memcpy(reinterpret_cast<void *>(address), &value, sizeof(value));
    }

    void putU32(std::uintptr_t address, std::uint32_t value)
    {
        std::memcpy(reinterpret_cast<void *>(address), &value, sizeof(value));
    }

    std::int32_t readInt(std::uintptr_t address) const
    {
        std::int32_t value = 0;
        iovec local{&value, sizeof(value)};
        iovec remote{reinterpret_cast<void *>(address), sizeof(value)};
        (void)::process_vm_readv(static_cast<pid_t>(::getpid()), &local, 1, &remote, 1, 0);
        return value;
    }

private:
    void *reserve_{nullptr};
    std::size_t reserveBytes_{0};
};

// --------------------------------------------------------------------------
// The fixture map.
//
// The shipped World map is used verbatim. The Rise harness has to rewrite its
// two SHARPNESS chains because they share an anchor and would clobber each
// other; World has no such collision for the chains this file drives —
// MONSTER_LIST_OFFSETS is a single 0x38 hop off its own anchor, and
// LOCKEDON_MONSTER_INDEX_OFFSETS walks its own. So the file stays
// byte-identical and the assertions cover what the shipped reader really
// resolves in production.
// --------------------------------------------------------------------------
QString shippedMapPath()
{
    return QDir(QStringLiteral(MONSTER_SOURCE_DIR))
        .absoluteFilePath(QStringLiteral("data/MonsterHunterWorld.421810.map"));
}

bool installFixtureMap(QTemporaryDir &dir, QString *pathOut)
{
    QFile in(shippedMapPath());
    if (!in.open(QIODevice::ReadOnly)) {
        std::cerr << "FAIL: cannot read the shipped World map at "
                  << qPrintable(shippedMapPath()) << '\n';
        return false;
    }
    const QByteArray shipped = in.readAll();
    in.close();
    if (shipped.isEmpty()) {
        std::cerr << "FAIL: the shipped World map is empty\n";
        return false;
    }
    if (!dir.isValid()) {
        std::cerr << "FAIL: temporary directory is unavailable\n";
        return false;
    }

    const QString target = dir.filePath(QStringLiteral("fixture.map"));
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || out.write(shipped) != shipped.size() || !out.flush()) {
        std::cerr << "FAIL: cannot stage the fixture map\n";
        return false;
    }
    *pathOut = target;
    return true;
}

// The fixture is byte-identical, so every contract the harness relies on is
// checked against the SHIPPED file. An upstream key rename or an offset-chain
// change fails here instead of silently degrading the scenarios below into
// "no monster found".
void checkShippedMapContracts()
{
    mhw::AddressMap shipped;
    QString error;
    check(shipped.load(shippedMapPath(), &error),
          "the shipped World map parses (guards a corrupted data/ fixture)");

    const bool keysPresent
        = shipped.hasAddress(QStringLiteral("MONSTER_LIST_ADDRESS"))
          && shipped.hasOffsets(QStringLiteral("MONSTER_LIST_OFFSETS"))
          && shipped.hasAddress(QStringLiteral("LOCKON_ADDRESS"))
          && shipped.hasOffsets(QStringLiteral("LOCKEDON_MONSTER_INDEX_OFFSETS"))
          && shipped.hasAddress(QStringLiteral("MONSTER_MANUAL_TARGET_ADDRESS"))
          && shipped.hasAddress(QStringLiteral("MONSTER_QUEST_TARGET_ADDRESS"))
          && shipped.hasOffsets(QStringLiteral("MONSTER_QUEST_TARGET_OFFSETS"));
    check(keysPresent, "the shipped map defines every key the tests need");

    // The MonsterList head deref: MONSTER_LIST_ADDRESS -> *(head) + 0x38, so
    // the chain is a single hop of exactly 0x38.
    const std::vector<std::uintptr_t> list = shipped.offsets(
        QStringLiteral("MONSTER_LIST_OFFSETS"));
    check(list.size() == 1 && list.front() == 0x38,
          "the shipped MONSTER_LIST chain is a single 0x38 hop");

    // Every anchor this file points into the fake image must fit inside it.
    const bool anchorsFit
        = shipped.address(QStringLiteral("MONSTER_LIST_ADDRESS")) < kImageBytes
          && shipped.address(QStringLiteral("LOCKON_ADDRESS")) < kImageBytes
          && shipped.address(QStringLiteral("MONSTER_MANUAL_TARGET_ADDRESS")) < kImageBytes
          && shipped.address(QStringLiteral("MONSTER_QUEST_TARGET_ADDRESS")) < kImageBytes;
    check(anchorsFit, "every chain anchor the tests use fits inside the fake image");
}

// --------------------------------------------------------------------------
// A real MhwReader wired onto the fake image.
// --------------------------------------------------------------------------
struct ReaderHarness {
    mhw::MhwReader reader;
    FakeWorldImage image;
    std::size_t nextObject = 1;

    // Where the harness last put the MonsterList's Component*[128] array, so a
    // scenario can rewrite one component's Monster* pointer without re-arming
    // the whole list.
    std::uintptr_t componentArrayBase = 0;
    // Where it last put each component cell, indexed by slot.
    std::vector<std::uintptr_t> componentCells;

    explicit ReaderHarness(const QString &mapPath)
        : reader(mapPath)
    {
        // Attach to our OWN pid: process_vm_readv on self always works, and
        // findGamePid() is never reached because these tests call the private
        // readers directly instead of going through poll()/ensureAttached().
        reader.memory_.attach(QCoreApplication::applicationPid(), nullptr);
        reader.imageBase_ = image.imageBase();
    }

    [[nodiscard]] std::uintptr_t freshObject()
    {
        return image.object(nextObject++);
    }

    [[nodiscard]] std::uintptr_t anchor(const char *addressKey) const
    {
        return reader.imageBase_ + reader.map_.address(QLatin1String(addressKey));
    }

    [[nodiscard]] std::uintptr_t monster()
    {
        return freshObject();
    }

    // Arms readMonsters' MonsterList with the given monsters, laid out exactly
    // as production walks them:
    //
    //   MONSTER_LIST_ADDRESS --read--> head
    //   head + 0x38          --read--> Component*[128]   (one readArray)
    //   component[i] + 0x138 --read--> Monster*
    //
    // `count` may be smaller than monsters.size() to emulate a truncated
    // array. The array is always 128 readable slots, so a component whose
    // pointer is 0 simply contributes nothing.
    void setupMonsterList(const std::vector<std::uintptr_t> &monsters,
                          std::size_t count)
    {
        // The reader reads MONSTER_LIST_ADDRESS as a POINTER and starts the
        // Component*[] at that pointer + 0x38. So the anchor cell holds a
        // value 0x38 below the array base, and the array base itself is never
        // dereferenced as a pointer.
        const std::uintptr_t arrayBase = freshObject();
        image.putPointer(anchor("MONSTER_LIST_ADDRESS"), arrayBase - 0x38);

        componentArrayBase = arrayBase;
        componentCells.clear();
        std::vector<std::uintptr_t> componentPointers(128, 0);
        for (std::size_t i = 0; i < count && i < monsters.size(); ++i) {
            const std::uintptr_t comp = freshObject();
            image.putPointer(comp + 0x138, monsters[i]);
            componentPointers[i] = comp;
            componentCells.push_back(comp);
        }
        image.putBytes(arrayBase, componentPointers.data(),
                       componentPointers.size() * sizeof(std::uintptr_t));
    }

    // Re-points component slot `index` at a new Monster*.
    void relocateComponent(std::size_t index, std::uintptr_t monsterPtr)
    {
        if (index < componentCells.size())
            image.putPointer(componentCells[index] + 0x138, monsterPtr);
    }

    // MonsterList head deliberately null/stale: the reader's first guard.
    void clearMonsterListHead()
    {
        image.putPointer(anchor("MONSTER_LIST_ADDRESS"), 0);
    }

    // Drop the reader's cross-tick caches so each scenario starts from the
    // state a first poll after a quest start would see. Without this, a
    // scenario that reuses the same MonsterList array base gets the cached
    // 128-slot array, and one that reuses a component address gets the cached
    // monster snapshot (which skips the name filter entirely).
    void resetReaderCaches()
    {
        reader.cachedArray_.clear();
        reader.cachedArrayBase_ = 0;
        reader.monsterCache_.clear();
        reader.manualTargetAddress_ = 0;
        reader.questTargetAddress_ = 0;
        reader.lockOnTargetIndex_ = -1;
    }

    // Walks LOCKEDON_MONSTER_INDEX_OFFSETS so the node the reader lands on is
    // ours, then writes `index` at its +0x950. Returns the node address.
    //
    // followPointerChain semantics: addr_1 = *(anchor) + offsets[0],
    // addr_{i+1} = *(addr_i) + offsets[i]. So the cell at addr_i stores the
    // value V_i with V_i + offsets[i] == addr_{i+1}.
    std::uintptr_t setupLockOnIndex(std::int32_t index)
    {
        const std::vector<std::uintptr_t> offsets = reader.map_.offsets(
            QStringLiteral("LOCKEDON_MONSTER_INDEX_OFFSETS"));
        const std::uintptr_t anchorCell = anchor("LOCKON_ADDRESS");
        const std::uintptr_t anchorPointer = freshObject();
        image.putPointer(anchorCell, anchorPointer);

        if (offsets.empty())
            return 0;
        std::uintptr_t cell = anchorPointer + offsets.front();
        for (std::size_t i = 1; i < offsets.size(); ++i) {
            const std::uintptr_t nextCell = (i + 1 == offsets.size())
                                            ? freshObject()
                                            : freshObject();
            // *(cell) + offsets[i] == nextCell
            image.putPointer(cell, nextCell - offsets[i]);
            cell = nextCell;
        }
        image.put<std::int32_t>(cell + 0x950, index);
        return cell;
    }
};

// The minimum a monster needs to appear in the snapshot at all. Anything less
// and the loop `continue`s before it reaches the fields being asserted, which
// would make the test pass for the wrong reason.
struct MonsterFields {
    int schemaId = 0;                // HunterPie schema Id, monster+0x12280
    const char *name = "em000";      // C string at (+0x2A0 ->) +0xC
    float maxHealth = 100.0F;        // healthPtr[0], at healthPtr + 0x60
    float health = 40.0F;            // healthPtr[1]
    float sizeMultiplier = 1.0F;     // +0x184
    float sizeModifier = 1.0F;       // +0x7730
    int doubleLinkedListIndex = 0;   // +0x1228C
};

// Writes a monster that readMonsters will accept, and returns its Monster*
// base address.
std::uintptr_t plantMonster(ReaderHarness &h, const MonsterFields &f)
{
    const std::uintptr_t monster = h.monster();

    // Name: a pointer struct at monster+0x2A0 holds the name at +0xC.
    const std::uintptr_t nameStruct = h.freshObject();
    char nameBuf[32] = {0};
    std::memcpy(nameBuf, f.name, std::strlen(f.name));
    h.image.putBytes(nameStruct + 0xC, nameBuf, sizeof(nameBuf));
    h.image.putPointer(monster + 0x2A0, nameStruct);

    // HP: monster+0x7670 -> healthPtr; healthPtr+0x60 -> [max, cur].
    const std::uintptr_t healthPtr = h.freshObject();
    h.image.putF32(healthPtr + 0x60, f.maxHealth);
    h.image.putF32(healthPtr + 0x64, f.health);
    h.image.putPointer(monster + 0x7670, healthPtr);

    h.image.put<std::int32_t>(monster + 0x12280, f.schemaId);
    h.image.put<std::int32_t>(monster + 0x1228C, f.doubleLinkedListIndex);
    h.image.putF32(monster + 0x184, f.sizeMultiplier);
    h.image.putF32(monster + 0x7730, f.sizeModifier);
    return monster;
}

// MHWMonsterPartStructure layout (verified 421810), as readPartStruct decodes
// it: +0x0C MaxHealth, +0x10 Health, +0x18 Counter, +0x20 ExtraMaxHealth,
// +0x24 ExtraHealth, +0x6C Index.
void writePartStruct(FakeWorldImage &image, std::uintptr_t addr,
                     float maxHealth, float health,
                     float extraMaxHealth, float extraHealth,
                     int counter, std::uint32_t index)
{
    constexpr int kSize = 0x78;
    std::vector<char> raw(kSize, 0);
    std::memcpy(raw.data() + 0x0C, &maxHealth, 4);
    std::memcpy(raw.data() + 0x10, &health, 4);
    std::memcpy(raw.data() + 0x18, &counter, 4);
    std::memcpy(raw.data() + 0x20, &extraMaxHealth, 4);
    std::memcpy(raw.data() + 0x24, &extraHealth, 4);
    std::memcpy(raw.data() + 0x6C, &index, 4);
    image.putBytes(addr, raw.data(), raw.size());
}

// Plants the part table at monster+0x1D058 and returns the partPtr it points
// at, so a caller can also aim the severable table's base at partPtr + 0x1FC8.
std::uintptr_t plantPartTable(ReaderHarness &h, std::uintptr_t monster,
                              float maxHealth, float health, int counter,
                              std::size_t tiers)
{
    const std::uintptr_t partPtr = h.freshObject();
    const std::uintptr_t normalBase = partPtr + 0x40ULL;   // normal table base
    for (std::size_t i = 0; i < tiers; ++i) {
        const std::uintptr_t slot = normalBase + i * 0x1F8ULL;   // 0x1F8 stride
        writePartStruct(h.image, slot, maxHealth, health,
                        maxHealth, health, counter,
                        static_cast<std::uint32_t>(100 + i));
    }
    h.image.putPointer(monster + 0x1D058, partPtr);
    return partPtr;
}

// MHWTenderizeInfoStructure layout (HunterPie): +0x08 Duration(f32),
// +0x0C MaxDuration(f32), +0x30 PartId(u32, 0xFFFFFFFF = empty slot).
constexpr std::uintptr_t kTenderizeOffset = 0x1C458ULL;
constexpr int kTenderizeSlotSize = 64;

void writeTenderizeSlot(FakeWorldImage &image, std::uintptr_t base, int slot,
                        float duration, float maxDuration, std::uint32_t partId)
{
    const std::uintptr_t p = base + static_cast<std::uintptr_t>(slot) * kTenderizeSlotSize;
    image.putF32(p + 0x08, duration);
    image.putF32(p + 0x0C, maxDuration);
    image.putU32(p + 0x30, partId);
}

// MHWMonsterAilmentStructure layout (HunterPie), read at
// (array element) + 0x148: Owner +0x00, IsActive +0x08, Unk1 +0x0C,
// Id +0x10, MaxDuration +0x14, Buildup +0x30, MaxBuildup +0x40,
// Duration +0x70, Counter +0x78.
//
// The reader does `structAddr = *current + 0x148`, so a planted struct puts
// the pointer in the array cell and the payload 0x148 bytes into its own
// object, exactly as HunterPie's stride works.
struct AilmentFields {
    std::int64_t owner = 0;   // must equal monster.address to be accepted
    std::int32_t active = 0;
    std::int32_t id = 0;
    float maxDuration = 0.0F;
    float buildup = 0.0F;
    float maxBuildup = 0.0F;
    float duration = 0.0F;
    std::int32_t counter = 0;
};

constexpr std::uintptr_t kAilmentStructShift = 0x148ULL;

// Writes one ailment slot: the pointer at `arrayCell` and the struct it
// dereferences to.
void writeAilmentSlot(ReaderHarness &h, std::uintptr_t arrayCell,
                      const AilmentFields &f)
{
    const std::uintptr_t structBase = h.freshObject();
    h.image.putPointer(arrayCell, structBase);
    const std::uintptr_t s = structBase + kAilmentStructShift;
    h.image.put<std::int64_t>(s + 0x00, f.owner);
    h.image.put<std::int32_t>(s + 0x08, f.active);
    h.image.put<std::int32_t>(s + 0x10, f.id);
    h.image.putF32(s + 0x14, f.maxDuration);
    h.image.putF32(s + 0x30, f.buildup);
    h.image.putF32(s + 0x40, f.maxBuildup);
    h.image.putF32(s + 0x70, f.duration);
    h.image.put<std::int32_t>(s + 0x78, f.counter);
}

// MonsterSnapshot pre-loaded with N parts in schema order, which is the shape
// applyTenderizesToParts expects (it writes monster.parts[s] at the schema
// index s).
mhw::MonsterSnapshot parkedParts(std::size_t count, int schemaId)
{
    mhw::MonsterSnapshot m;
    m.id = schemaId;
    m.parts.resize(static_cast<int>(count));
    for (std::size_t i = 0; i < count; ++i) {
        m.parts[static_cast<int>(i)].index = static_cast<int>(i);
        m.parts[static_cast<int>(i)].name = QStringLiteral("Part[%1]").arg(i);
    }
    return m;
}

// ==========================================================================
// readMonsterAilments
//
// Paths: a normal live slot / stale owner / unknown-id filter / the active
// derivation from Duration alone / read failure (PARTIAL into the guard page).
// ==========================================================================
void testReadMonsterAilments()
{
    QTemporaryDir dir;
    QString mapPath;
    check(installFixtureMap(dir, &mapPath), "fixture map is staged");
    if (!dir.isValid())
        return;

    ReaderHarness h(mapPath);
    check(h.image.valid(), "the fake World image is mapped");
    if (!h.image.valid())
        return;

    // A private member function pointer, reached through -fno-access-control.
    void (mhw::MhwReader::*readAilments)(mhw::MonsterSnapshot &) =
        &mhw::MhwReader::readMonsterAilments;

    // ---- path 1: one live slot, and every field it decodes ---------------
    {
        mhw::MonsterSnapshot monster = parkedParts(1, 0);
        monster.address = h.monster();
        const std::uintptr_t ailBase = monster.address + 0x1BC40ULL;

        AilmentFields live;
        live.owner = static_cast<std::int64_t>(monster.address);
        live.active = 0;        // IsActive is deliberately 0 (see below)
        live.id = 2;             // paralysis
        live.maxDuration = 90.0F;
        live.buildup = 30.0F;
        live.maxBuildup = 60.0F;
        live.duration = 45.0F;
        live.counter = 3;
        writeAilmentSlot(h, ailBase, live);
        // Slot 1 stays null so the 32-slot loop stops after the first.
        h.image.putPointer(ailBase + 8, 0);

        (h.reader.*readAilments)(monster);
        check(monster.ailments.size() == 1,
              "one live ailment slot produces exactly one snapshot");
        if (monster.ailments.size() == 1) {
            const mhw::MonsterAilmentSnapshot &a = monster.ailments.front();
            check(a.id == 2, "the ailment id is decoded from +0x10");
            check(a.name == QStringLiteral("麻痹"),
                  "id 2 resolves to the localized Chinese name");
            // v0.8.4-r23: active is derived from Duration > 0, NOT IsActive.
            check(a.active,
                  "Duration > 0 marks the ailment active even with IsActive = 0");
            check(a.timer == 45.0F && a.maxTimer == 90.0F,
                  "timer/maxTimer come from Duration/MaxDuration");
            check(a.buildup == 30.0F && a.maxBuildup == 60.0F,
                  "buildup/maxBuildup come from +0x30/+0x40");
            check(a.counter == 3, "the counter is decoded from +0x78");
        }
    }

    // ---- path 2: a stale Owner is skipped, not fatal ---------------------
    {
        mhw::MonsterSnapshot monster = parkedParts(1, 0);
        monster.address = h.monster();
        const std::uintptr_t ailBase = monster.address + 0x1BC40ULL;

        AilmentFields stale;
        stale.owner = static_cast<std::int64_t>(monster.address) + 0x1000;
        stale.id = 2;
        stale.duration = 30.0F;
        writeAilmentSlot(h, ailBase, stale);

        // Slot 1 is ours and carries a different id.
        AilmentFields live;
        live.owner = static_cast<std::int64_t>(monster.address);
        live.id = 7;
        live.duration = 12.0F;
        writeAilmentSlot(h, ailBase + 8, live);
        // Slot 2 stays null so the scan ends there.

        (h.reader.*readAilments)(monster);
        check(monster.ailments.size() == 1 && monster.ailments.front().id == 7,
              "a mismatched Owner skips its slot but does not kill the loop");
    }

    // ---- path 3: the repository filter ---------------------------------
    {
        mhw::MonsterSnapshot monster = parkedParts(1, 0);
        monster.address = h.monster();
        const std::uintptr_t ailBase = monster.address + 0x1BC40ULL;

        AilmentFields bad;
        bad.owner = static_cast<std::int64_t>(monster.address);
        // Id 13 is one of World MonsterData.xml's non-AILMENT ids that
        // HunterPie flags IsUnknown and skips.
        bad.id = 13;
        bad.duration = 99.0F;
        writeAilmentSlot(h, ailBase, bad);

        AilmentFields good;
        good.owner = static_cast<std::int64_t>(monster.address);
        good.id = 4;
        good.duration = 5.0F;
        writeAilmentSlot(h, ailBase + 8, good);

        (h.reader.*readAilments)(monster);
        check(monster.ailments.size() == 1
                  && monster.ailments.front().id == 4,
              "HunterPie's unknown ailment id 13 is filtered out");
    }

    // ---- path 4: the English column resolves through StringTable ---------
    {
        // This is the one place the file crosses into the i18n surface. The
        // reader picks its column from StringTable::isEnglish(); with no
        // locale loaded that is false and the zh column answers.
        mhw::StringTable &strings = mhw::StringTable::instance();
        const bool wasEnglish = strings.isEnglish();
        const QString previousLocale = strings.currentLocale();

        mhw::MonsterSnapshot monster = parkedParts(1, 0);
        monster.address = h.monster();
        const std::uintptr_t ailBase = monster.address + 0x1BC40ULL;
        AilmentFields ail;
        ail.owner = static_cast<std::int64_t>(monster.address);
        // Id 12 is absent from the zh table (fallback 异常12) but present in
        // kAilmentNamesEn.
        ail.id = 12;
        ail.duration = 1.0F;
        writeAilmentSlot(h, ailBase, ail);

        (h.reader.*readAilments)(monster);
        // Id 12 is absent from kAilmentNames, so the zh path falls back to
        // "异常12" — the documented behaviour, and exactly why the English
        // column was added.
        check(monster.ailments.size() == 1
                  && monster.ailments.front().name == QStringLiteral("异常12"),
              "with no locale loaded, id 12 falls back to the Chinese default");

        check(strings.load(QStringLiteral("en-US")),
              "en-US locale loads from the qrc");
        (h.reader.*readAilments)(monster);
        check(monster.ailments.size() == 1
                  && monster.ailments.front().name == QStringLiteral("Dung Bomb"),
              "id 12 resolves to Dung Bomb through the English column");

        check(strings.load(previousLocale.isEmpty()
                               ? QStringLiteral("zh-CN")
                               : previousLocale),
              "the test restores the previous locale");
        check(strings.isEnglish() == wasEnglish,
              "the locale switch leaves no residue");
    }

    // ---- path 5: a real PARTIAL read into the guard page -----------------
    {
        // The reader reads the struct's HEADER (16 bytes at
        // *current + 0x148) in one readBytes call. Point the slot at a
        // struct base whose header read straddles the guard: the Owner/Id
        // fields partly copy, then process_vm_readv returns a partial count
        // and readBytes reports false. The reader must break out of the loop
        // instead of publishing a struct whose remaining fields never
        // arrived.
        mhw::MonsterSnapshot monster = parkedParts(1, 0);
        monster.address = h.monster();
        const std::uintptr_t ailBase = monster.address + 0x1BC40ULL;
        h.image.putPointer(ailBase,
                           h.image.straddlingStruct(0x0) - kAilmentStructShift);
        h.image.putPointer(ailBase + 8, 0);

        (h.reader.*readAilments)(monster);
        check(monster.ailments.empty(),
              "a partial ailment-struct read publishes nothing");
    }

    // ---- path 6: a base pointer that is null/stale ---------------------
    {
        mhw::MonsterSnapshot monster = parkedParts(1, 0);
        monster.address = h.monster();
        const std::uintptr_t ailBase = monster.address + 0x1BC40ULL;
        // The `<= 1` guard: a monster with no ailment table at all.
        h.image.putPointer(ailBase, 1);
        (h.reader.*readAilments)(monster);
        check(monster.ailments.empty(),
              "an ailment base of 1 (no table) yields no ailments");
    }
}

// ==========================================================================
// applyTenderizesToParts
//
// Paths: a live slot writes remaining/max onto the matching part; the slot's
// PartId maps by PartSchema.tenderizeIds; an expired slot leaves the part at
// 0/0; an unknown schema id maps nothing.
// ==========================================================================
void testApplyTenderizesToParts()
{
    QTemporaryDir dir;
    QString mapPath;
    check(installFixtureMap(dir, &mapPath), "fixture map is staged");
    if (!dir.isValid())
        return;

    ReaderHarness h(mapPath);
    check(h.image.valid(), "the fake World image is mapped");
    if (!h.image.valid())
        return;

    void (mhw::MhwReader::*applyTenderizes)(mhw::MonsterSnapshot &) =
        &mhw::MhwReader::applyTenderizesToParts;

    // Monster 0 (Great Jagras) has 7 parts. Its schema's tenderizeIds map
    // slots onto specific parts: index 2 (Head) claims {0x0, 0x4}, index 3
    // (Body) {0x1, 0x5}, index 4 (Left Leg) {0x2}, index 5 (Right Leg)
    // {0x6}, index 6 (Tail) {0x3, 0x7}. So slot PartId 4 must land on
    // part index 2.
    constexpr int kJagrasSchemaId = 0;

    // ---- path 1: a live slot writes remaining and max -------------------
    {
        mhw::MonsterSnapshot monster = parkedParts(7, kJagrasSchemaId);
        monster.address = h.monster();
        writeTenderizeSlot(h.image, monster.address + kTenderizeOffset, 0,
                           30.0F /* duration */, 60.0F /* maxDuration */,
                           0x4 /* PartId -> schema part index 2 (Head) */);

        (h.reader.*applyTenderizes)(monster);
        check(monster.parts[2].tenderizeDuration == 30.0F
                  && monster.parts[2].tenderizeMaxDuration == 60.0F,
              "a live slot writes remaining = max - duration onto the matching part");
        check(monster.parts[0].tenderizeDuration == 0.0F
                  && monster.parts[0].tenderizeMaxDuration == 0.0F,
              "every other part of the same monster stays at 0/0");
    }

    // ---- path 2: the slot table is zero-cleared first --------------------
    {
        mhw::MonsterSnapshot monster = parkedParts(7, kJagrasSchemaId);
        monster.address = h.monster();
        // A stale value from a previous tick whose slot has since expired
        // must clear, not persist (the v0.10.x sentinel regression).
        monster.parts[2].tenderizeDuration = 45.0F;
        monster.parts[2].tenderizeMaxDuration = 60.0F;
        // Every slot is the empty sentinel.
        for (int s = 0; s < 10; ++s)
            writeTenderizeSlot(h.image, monster.address + kTenderizeOffset, s,
                               0.0F, 0.0F, 0xFFFFFFFFu);

        (h.reader.*applyTenderizes)(monster);
        check(monster.parts[2].tenderizeDuration == 0.0F
                  && monster.parts[2].tenderizeMaxDuration == 0.0F,
              "an expired slot clears the part instead of pinning a stale max");
    }

    // ---- path 3: duration <= 0 is skipped, not clamped negative ----------
    {
        mhw::MonsterSnapshot monster = parkedParts(7, kJagrasSchemaId);
        monster.address = h.monster();
        // Slot 0: the PartId matches part 2 but the duration has run to an
        // impossible 0 / negative, so the slot must be skipped entirely.
        writeTenderizeSlot(h.image, monster.address + kTenderizeOffset, 0,
                           0.0F /* duration -> expired */, 60.0F, 0x4);
        // Slot 1 is a valid one on the SAME part, to prove the loop still
        // runs after the skip.
        writeTenderizeSlot(h.image, monster.address + kTenderizeOffset, 1,
                           20.0F, 50.0F, 0x4);

        (h.reader.*applyTenderizes)(monster);
        check(monster.parts[2].tenderizeDuration == 30.0F
                  && monster.parts[2].tenderizeMaxDuration == 50.0F,
              "a zero-duration slot is skipped and the valid one still lands");
    }

    // ---- path 4: an unknown schema id maps nothing ----------------------
    {
        mhw::MonsterSnapshot monster = parkedParts(7, 0xFFFF);   // no schema
        monster.address = h.monster();
        writeTenderizeSlot(h.image, monster.address + kTenderizeOffset, 0,
                           30.0F, 60.0F, 0x4);

        (h.reader.*applyTenderizes)(monster);
        bool allZero = true;
        for (const mhw::PartSnapshot &p : monster.parts) {
            if (p.tenderizeDuration != 0.0F || p.tenderizeMaxDuration != 0.0F)
                allZero = false;
        }
        check(allZero,
              "a monster with no PartSchema maps no slot (HunterPie UnknownDefinition)");
    }

    // ---- path 5: a real PARTIAL read into the guard page -----------------
    {
        // Place the 640-byte slot table so the single readBytes for the whole
        // table returns a partial count. The parts must be left as the
        // function cleared them — never with a stale max.
        mhw::MonsterSnapshot monster = parkedParts(7, kJagrasSchemaId);
        monster.address = h.image.straddlingStruct(kTenderizeOffset);
        monster.parts[2].tenderizeDuration = 45.0F;
        monster.parts[2].tenderizeMaxDuration = 60.0F;

        (h.reader.*applyTenderizes)(monster);
        check(monster.parts[2].tenderizeDuration == 0.0F
                  && monster.parts[2].tenderizeMaxDuration == 0.0F,
              "a partial slot-table read into the guard page clears the parts");
    }
}

// ==========================================================================
// readMonsters
//
// The main body: the MonsterList guard, the deref chain, the component
// de-dup, the schema dispatch, the name filter and the lock-on target flag.
// ==========================================================================
void testReadMonsters()
{
    QTemporaryDir dir;
    QString mapPath;
    check(installFixtureMap(dir, &mapPath), "fixture map is staged");
    if (!dir.isValid())
        return;

    ReaderHarness h(mapPath);
    check(h.image.valid(), "the fake World image is mapped");
    if (!h.image.valid())
        return;

    // ---- path 1: a full, readable monster ---------------------------------
    {
        h.resetReaderCaches();
        MonsterFields f;
        f.schemaId = 0;                       // Great Jagras, 7 parts
        f.maxHealth = 500.0F;
        f.health = 250.0F;
        f.sizeMultiplier = 1.0F;
        f.sizeModifier = 1.0F;
        const std::uintptr_t monster = plantMonster(h, f);
        // Monster 0's parts 0 (Throat) and 1 (Tail) are severable; 2..6 are
        // normal. Three live normal tiers therefore produce parts 2..4.
        plantPartTable(h, monster, 200.0F, 100.0F, 1, 3);

        h.setupMonsterList({monster}, 1);
        const QVector<mhw::MonsterSnapshot> out = h.reader.readMonsters(nullptr);

        check(out.size() == 1, "one component in the list yields one snapshot");
        if (out.size() == 1) {
            const mhw::MonsterSnapshot &m = out.front();
            check(m.address == monster, "the snapshot records the Monster* address");
            check(m.id == 0, "the schema Id is read from +0x12280");
            check(m.internalName == QStringLiteral("蛮颚龙"),
                  "the em000 name maps to the localized monster name for id 000");
            check(m.maxHealth == 500.0F && m.health == 250.0F,
                  "maxHealth/health come from the healthPtr +0x60 pair");
            check(m.size == 1.0F,
                  "size is sizeMultiplier / sizeModifier, rounded");
            // Severable parts 0/1 need a severable table, which this
            // scenario leaves empty, so only the three normal parts appear.
            check(m.parts.size() == 3,
                  "three live normal tiers produce three normal parts");
            for (const mhw::PartSnapshot &p : m.parts) {
                check(!p.isSeverable, "no part is severable without a table entry");
                break;
            }
            // Monster 0's schema rows, in order: 0 Throat (severable),
            // 1 Tail (severable), 2 Head (thresholds "5"), 3 Body (no
            // thresholds -> Flinch), 4 Left Leg (thresholds "3"). With three
            // live normal tiers planted, the emitted parts are rows 2/3/4.
            //
            // Row 2 (Head) with counter 1: nextThreshold 5, so
            //   maxHealth = 5 * 200 = 1000, health = (5-1-1)*200 + 100 = 700
            // Row 4 (Left Leg) with counter 1: nextThreshold 3, so
            //   maxHealth = 3 * 200 = 600, health = (3-1-1)*200 + 100 = 300
            const mhw::PartSnapshot *head = nullptr;
            const mhw::PartSnapshot *leg = nullptr;
            const mhw::PartSnapshot *body = nullptr;
            for (const mhw::PartSnapshot &p : m.parts) {
                if (p.firstThreshold == 5)
                    head = &p;
                else if (p.firstThreshold == 3)
                    leg = &p;
                else if (p.name == QStringLiteral("身体"))
                    body = &p;
            }
            check(head != nullptr,
                  "a threshold-bearing part dispatches to Breakable");
            if (head) {
                check(head->maxHealth == 1000.0F,
                      "Breakable maxHealth is nextThreshold * per-layer maxHealth");
                check(head->health == 700.0F,
                      "Breakable health is (next - counter - 1) * per-layer + live");
                check(head->name == QStringLiteral("头部"),
                      "the part name resolves through partDisplayName");
            }
            check(leg != nullptr,
                  "a second threshold-bearing part also computes its cap-room");
            check(body != nullptr && body->partType == mhw::PartType::Flinch,
                  "a part with an empty thresholds string stays a Flinch part");
        }
    }

    // ---- path 2: the MonsterList head guard ------------------------------
    {
        h.resetReaderCaches();
        MonsterFields f;
        const std::uintptr_t monster = plantMonster(h, f);
        h.setupMonsterList({monster}, 1);

        // Without a component, the head itself is the only thing to break.
        h.clearMonsterListHead();
        QString error;
        const QVector<mhw::MonsterSnapshot> out = h.reader.readMonsters(&error);
        check(out.isEmpty(),
              "a null MonsterList head publishes nothing");
        check(!error.isEmpty() && error.contains(QStringLiteral("MonsterList head")),
              "a null MonsterList head reports why it published nothing");
    }

    // ---- path 3: an out-of-range / stale component pointer ---------------
    {
        h.resetReaderCaches();
        MonsterFields f;
        const std::uintptr_t monster = plantMonster(h, f);
        h.setupMonsterList({monster}, 1);

        // First prove the same list publishes the monster.
        check(h.reader.readMonsters(nullptr).size() == 1,
              "the component is readable before the pointer is corrupted");

        // Point the component's Monster* at 1 — below the isSanePointer floor
        // the reader applies to every dereference. This is the out-of-range
        // index path: a live but unmapped slot, not a failed read.
        h.relocateComponent(0, 1);
        check(h.reader.readMonsters(nullptr).isEmpty(),
              "an out-of-range component pointer is skipped, not chased");
    }

    // ---- path 4: the name filter ------------------------------------------
    {
        // The reader skips any monster whose name starts with the runtime
        // prefix "em\ems" — ONE backslash, as the MSH MonsterList spells the
        // multi-part variant "em000\ems001". Plant that spelling.
        MonsterFields skip;
        skip.name = "em\\ems001";
        skip.schemaId = 0;
        const std::uintptr_t skipped = plantMonster(h, skip);
        h.setupMonsterList({skipped}, 1);
        h.resetReaderCaches();
        check(h.reader.readMonsters(nullptr).isEmpty(),
              "an em\\ems multi-part name is skipped");

        h.resetReaderCaches();
        MonsterFields plain;
        plain.name = "em000";
        plain.schemaId = 0;
        const std::uintptr_t plainMonster = plantMonster(h, plain);
        h.setupMonsterList({plainMonster}, 1);
        check(h.reader.readMonsters(nullptr).size() == 1,
              "the plain em000 name of the same monster is published");
    }

    // ---- path 5: a null name ----------------------------------------------
    {
        // nameBuf[0] == 0 -> the reader skips the monster.
        h.resetReaderCaches();
        MonsterFields f;
        f.name = "";
        const std::uintptr_t monster = plantMonster(h, f);
        h.setupMonsterList({monster}, 1);
        check(h.reader.readMonsters(nullptr).isEmpty(),
              "a monster with no name is skipped");
    }

    // ---- path 6: the lock-on target flag ----------------------------------
    {
        // LOCKON_ADDRESS -> LOCKEDON_MONSTER_INDEX_OFFSETS -> +0x950 index,
        // compared against each monster's +0x1228C value.
        h.resetReaderCaches();
        MonsterFields f;
        f.schemaId = 0;
        f.doubleLinkedListIndex = 3;
        f.name = "em007";
        const std::uintptr_t monster = plantMonster(h, f);

        h.resetReaderCaches();
        MonsterFields other;
        other.schemaId = 1;
        other.doubleLinkedListIndex = 9;
        other.name = "em001";
        const std::uintptr_t otherMonster = plantMonster(h, other);

        h.setupMonsterList({monster, otherMonster}, 2);
        check(h.setupLockOnIndex(3) != 0,
              "the LOCKON chain resolves inside the fake image");

        const QVector<mhw::MonsterSnapshot> out = h.reader.readMonsters(nullptr);
        check(out.size() == 2, "two components in the list yield two snapshots");
        const mhw::MonsterSnapshot *target = nullptr;
        const mhw::MonsterSnapshot *bystander = nullptr;
        for (const mhw::MonsterSnapshot &m : out) {
            if (m.isLockOnTarget)
                target = &m;
            else
                bystander = &m;
        }
        check(target != nullptr && target->doubleLinkedListIndex == 3,
              "the monster whose +0x1228C equals the lock-on index is flagged");
        check(bystander != nullptr && !bystander->isLockOnTarget,
              "a monster with a different index is not flagged");
    }

    // ---- path 7: a real PARTIAL read --------------------------------------
    {
        h.resetReaderCaches();
        MonsterFields f;
        f.maxHealth = 100.0F;
        f.health = 80.0F;
        const std::uintptr_t monster = plantMonster(h, f);
        h.setupMonsterList({monster}, 1);

        // Straddle the Id read at +0x12280: 2 of 4 bytes copy, then EFAULT.
        // The Id read fails, so hunterId stays -1 while the LOWER-offset name
        // read still lands. What must not happen is a published snapshot whose
        // id claims a value the read never produced.
        const std::uintptr_t straddle = h.image.straddlingStruct(0x12280);
        h.relocateComponent(0, straddle);

        const QVector<mhw::MonsterSnapshot> out = h.reader.readMonsters(nullptr);
        check(out.size() <= 1,
              "a straddling monster read yields at most one snapshot");
        if (out.size() == 1) {
            check(out.front().id == -1,
                  "a partial schema-Id read leaves the id at its -1 default");
        }
    }

    // ---- path 8: maxHealth <= 0 is dropped --------------------------------
    {
        h.resetReaderCaches();
        MonsterFields f;
        f.maxHealth = 0.0F;    // not a live monster
        f.health = 0.0F;
        const std::uintptr_t dead = plantMonster(h, f);

        h.resetReaderCaches();
        MonsterFields live;
        live.maxHealth = 10.0F;
        live.health = 5.0F;
        live.name = "em001";
        live.schemaId = 1;
        const std::uintptr_t alive = plantMonster(h, live);

        h.setupMonsterList({dead, alive}, 2);
        const QVector<mhw::MonsterSnapshot> out = h.reader.readMonsters(nullptr);
        check(out.size() == 1,
              "a monster with maxHealth <= 0 is not published");
        if (out.size() == 1)
            check(out.front().maxHealth == 10.0F,
                  "the surviving monster carries the live health pair");
    }

    // ---- path 9: the severable scan ---------------------------------------
    {
        // Monster 0's parts 0 (Throat) and 1 (Tail) are severable, so the
        // reader jumps into the partPtr + 0x1FC8 table and matches by the
        // payload's live Index. worldSeverableSlotLayout() sends the payload
        // to slot+8 when the prefix word is <= 0xA0.
        h.resetReaderCaches();
        MonsterFields f;
        f.schemaId = 0;
        f.maxHealth = 400.0F;
        f.health = 200.0F;
        f.name = "em007";
        const std::uintptr_t monster = plantMonster(h, f);
        const std::uintptr_t partPtr = plantPartTable(h, monster, 100.0F, 50.0F, 0, 3);

        const std::uintptr_t sevBase = partPtr + 0x1FC8ULL;
        // worldSeverableSlotLayout() returns {payload, payload + 0x78}, so the
        // NEXT slot's prefix word sits at payload + 0x78 — not at slot + 0x78.
        //
        // Monster 0's severable rows, in schema order:
        //   row 0 = Throat, ps.id 1
        //   row 1 = Tail,   ps.id 0
        // so slot 0 must carry Index 1 and slot 1 Index 0.
        //
        // Slot 0: prefix word 0 (<= 0xA0 -> payload at slot + 8), Index 1.
        h.image.put<std::int32_t>(sevBase, 0);
        writePartStruct(h.image, sevBase + 8, 120.0F, 80.0F, 0.0F, 0.0F, 2, 1);
        // Slot 1 lands at payload0 + 0x78 = sevBase + 0x80, Index 0.
        const std::uintptr_t sevSlot1 = sevBase + 8 + 0x78;
        h.image.put<std::int32_t>(sevSlot1, 0);
        writePartStruct(h.image, sevSlot1 + 8, 90.0F, 90.0F, 0.0F, 0.0F, 0, 0);
        // Slot 2 lands at payload1 + 0x78 and stays zeroed: maxHealth 0, so the
        // scan reads Empty and keeps going for its full 32-slot budget.

        h.setupMonsterList({monster}, 1);
        const QVector<mhw::MonsterSnapshot> out = h.reader.readMonsters(nullptr);
        check(out.size() == 1, "a severable monster yields one snapshot");
        if (out.size() == 1) {
            int severableCount = 0, normalCount = 0;
            bool sawTail = false, sawThroat = false;
            for (const mhw::PartSnapshot &p : out.front().parts) {
                if (p.isSeverable) {
                    ++severableCount;
                    if (p.name == QStringLiteral("尾巴"))
                        sawTail = true;
                    if (p.name == QStringLiteral("喉咙"))
                        sawThroat = true;
                } else {
                    ++normalCount;
                }
            }
            check(severableCount == 2,
                  "both severable schema rows matched a severable-table slot");
            check(sawTail && sawThroat,
                  "the severable parts resolve to their schema names");
            check(normalCount == 3,
                  "the non-statePart normal rows still produce their own parts");
            // Slot 0 (Index 1) is the THROAT row and carries Counter 2, so
            // HunterPie's `Breaks > 0` clause marks it severed.
            for (const mhw::PartSnapshot &p : out.front().parts) {
                if (p.name == QStringLiteral("喉咙") && p.isSeverable) {
                    check(p.counter == 2 && p.isBroken,
                          "a severable part with counter > 0 is marked broken");
                    check(std::fabs(p.health - 80.0F) < 1e-4F
                              && std::fabs(p.maxHealth - 120.0F) < 1e-4F,
                          "the severable layer binds Health/MaxHealth verbatim");
                    break;
                }
            }
            // Slot 1 (Index 0) is the TAIL row at full HP with Counter 0, so
            // it is NOT severed even though the equality clause holds.
            for (const mhw::PartSnapshot &p : out.front().parts) {
                if (p.name == QStringLiteral("尾巴") && p.isSeverable) {
                    check(!p.isBroken,
                          "a full-HP severable part with counter 0 is not severed");
                    check(std::fabs(p.health - 90.0F) < 1e-4F
                              && std::fabs(p.maxHealth - 90.0F) < 1e-4F,
                          "the tail severable layer also binds Health/MaxHealth verbatim");
                    break;
                }
            }
        }
    }

    // ---- path 10: the severable scan stops on a real read failure ----------
    {
        // Same layout, but the severable table is placed so its first payload
        // read straddles the guard page. severableScanAction() returns Stop,
        // the scan breaks, and NEITHER severable part can be emitted.
        h.resetReaderCaches();
        MonsterFields f;
        f.schemaId = 0;
        f.maxHealth = 400.0F;
        f.health = 200.0F;
        f.name = "em007";
        const std::uintptr_t monster = plantMonster(h, f);
        // The severable base must be the straddling address; the normal table
        // can stay inside the image so the normal parts still read.
        (void)plantPartTable(h, monster, 100.0F, 50.0F, 0, 3);
        // Re-point the severable base by planting a partPtr whose +0x1FC8
        // lands in the guard page. straddlingStruct(0x1FC8) puts a 0x78-byte
        // read at that offset across the boundary.
        const std::uintptr_t badPartPtr = h.image.straddlingStruct(0x1FC8)
                                          - 0x1FC8 + 0x1FC8;   // base + 0x1FC8 == guard - 2
        h.image.putPointer(monster + 0x1D058, badPartPtr - 0x1FC8);

        h.setupMonsterList({monster}, 1);
        const QVector<mhw::MonsterSnapshot> out = h.reader.readMonsters(nullptr);
        check(out.size() == 1, "the monster itself survives the scan failure");
        if (out.size() == 1) {
            bool anySeverable = false;
            for (const mhw::PartSnapshot &p : out.front().parts) {
                if (p.isSeverable)
                    anySeverable = true;
            }
            check(!anySeverable,
                  "a severed read in the severable table emits no severable part");
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    checkShippedMapContracts();
    testReadMonsterAilments();
    testApplyTenderizesToParts();
    testReadMonsters();

    if (failures == 0) {
        std::cout << "ALL MHW READER DIRECT TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
