// SPDX-License-Identifier: Apache-2.0
//
// Direct unit tests for MhrReader's zero-coverage memory readers.
//
// Six functions never saw CI coverage (readSharpness / readQuest /
// readPlayer / readParty / readMonsters / poll) because they are private
// members that reach a live MonsterHunterRise.exe through ProcessMemory, and
// the only tool that ever ran them was rise-probe, which CMake marks
// EXCLUDE_FROM_ALL (CMakeLists.txt) — so it is not one of the ctest entries
// and never was. That is precisely why splitting
// src/rise/mhr_reader.cpp could not be verified by any gate: the new
// translation units would have been exercised by nothing.
//
// The technique — no production seam, no src/ change:
//
//   1. `-fno-access-control`. The private members are still the ones the real
//      monster-core translation unit compiled; the flag only relaxes access
//      checking in THIS translation unit, so nothing shipped changes and the
//      ABI is untouched.
//   2. A fake Rise image. One big PROT_NONE reservation whose first 272 MiB
//      are mprotect'd RW, with a PROT_NONE guard page immediately after. The
//      reader's own map drives it and imageBase_ points at the reservation,
//      so every pointer the reader resolves lands in the RW region and reads
//      succeed; a read that would run past the image lands in the guard page
//      and fails.
//
// The three required paths — normal value / out-of-range index / read failure
// (including a genuine PARTIAL read) — therefore go through the same
// process_vm_readv code path as production, not a stub.
//
// No real game process is involved: every read is against our own pid, and
// findRisePid() is never reached because the private readers are called
// directly instead of through poll()/ensureAttached().

#include "core/process_memory.h"
#include "rise/mhr_reader.h"
#include "rise/mhr_types.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QList>
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
// The fake Rise image.
//
// 272 MiB holds every address in data/MonsterHunterRise.16.0.2.0.map (the
// largest is 0x0F6FD040) plus a high scratch window for the objects the
// offset chains dereference into. Only pages actually written get committed,
// so the resident cost stays in the KiB range.
// --------------------------------------------------------------------------
constexpr std::size_t kImageBytes = static_cast<std::size_t>(0x11000000);

// Scratch objects live in a high window of the image, spaced well past every
// field offset the readers touch, so no two of them can overlap.
constexpr std::uintptr_t kScratchWindow = 0x10000000ULL;
constexpr std::uintptr_t kObjectStride = 0x1000ULL;

class FakeRiseImage {
public:
    FakeRiseImage()
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

    ~FakeRiseImage()
    {
        if (reserve_)
            ::munmap(reserve_, reserveBytes_);
    }

    FakeRiseImage(const FakeRiseImage &) = delete;
    FakeRiseImage &operator=(const FakeRiseImage &) = delete;

    [[nodiscard]] bool valid() const { return reserve_ != nullptr; }

    // imageBase_ for the reader. The map's `Address` values are already
    // absolute, so a zero base offset reproduces them exactly.
    [[nodiscard]] std::uintptr_t imageBase() const
    {
        return reinterpret_cast<std::uintptr_t>(reserve_);
    }

    // Scratch object #index at byte `slot` inside its page. A wide stride
    // between the object base and the slot keeps every hop's cell in a
    // distinct page, so writes can never clobber each other.
    [[nodiscard]] std::uintptr_t object(std::size_t index, std::uintptr_t slot) const
    {
        return imageBase() + kScratchWindow + static_cast<std::uintptr_t>(index)
               * kObjectStride + slot;
    }

    // The default slot: mid-page, with room for a 0x800-byte struct above it.
    [[nodiscard]] std::uintptr_t object(std::size_t index) const
    {
        return object(index, kObjectStride / 2);
    }

    // First address INSIDE the guard page: any read starting here fails.
    [[nodiscard]] std::uintptr_t guardAddress() const
    {
        return imageBase() + kImageBytes;
    }

    // The struct address such that the read at `offset` crosses into the guard
    // page: the first 2 of 4 bytes copy, then EFAULT, so process_vm_readv
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

    int32_t readInt(std::uintptr_t address) const
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
// The shipped map points BOTH SHARPNESS chains at SHARPNESS_ADDRESS with a
// shared leading 0xE0 hop, so after the first hop they read the SAME cell:
// setting one chain's pointer would clobber the other's and readSharpness —
// which needs both chains in a single call — could never be set up. So the
// fixture is the shipped map with ONLY those two Offset lines rewritten to
// diverging first hops. Everything else is copied verbatim; the pristine
// shipped file is loaded separately below to assert its keys and chain shapes
// are still what this harness expects, so a key rename or removal fails the
// suite instead of silently drifting the fixture.
// --------------------------------------------------------------------------
const char *const kSharpnessOffsetsLine = "Offset SHARPNESS_OFFSETS";
const char *const kSharpnessArrayOffsetsLine = "Offset SHARPNESS_ARRAY_OFFSETS";

bool installFixtureMap(QTemporaryDir &dir, QString *pathOut)
{
    const QString source = QDir(QStringLiteral(MONSTER_SOURCE_DIR))
                               .absoluteFilePath(QStringLiteral(
                                   "data/MonsterHunterRise.16.0.2.0.map"));
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        std::cerr << "FAIL: cannot read the shipped Rise map at "
                  << qPrintable(source) << '\n';
        return false;
    }
    const QByteArray shipped = in.readAll();
    in.close();
    if (shipped.isEmpty()) {
        std::cerr << "FAIL: the shipped Rise map is empty\n";
        return false;
    }
    if (!dir.isValid()) {
        std::cerr << "FAIL: temporary directory is unavailable\n";
        return false;
    }

    // Replace the two SHARPNESS `Offset` lines wholesale, keeping every hop
    // after the first byte-for-byte.
    const QList<QByteArray> lines = shipped.split('\n');
    QList<QByteArray> fixtureLines;
    QString rewrittenError;
    int rewritten = 0;
    for (QByteArray line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (!trimmed.startsWith(kSharpnessOffsetsLine)
            && !trimmed.startsWith(kSharpnessArrayOffsetsLine)) {
            fixtureLines.append(line);
            continue;
        }
        const bool isSharp = trimmed.startsWith(kSharpnessOffsetsLine);
        // Use the offical key length rather than a hand-counted constant:
        // SHARPNESS_OFFSETS is 17 chars, SHARPNESS_ARRAY_OFFSETS is 23, so
        // counting them by hand got this wrong once already.
        const QByteArray key = isSharp ? QByteArray("SHARPNESS_OFFSETS")
                                       : QByteArray("SHARPNESS_ARRAY_OFFSETS");
        const QByteArray firstHopText = isSharp ? QByteArray("0x100")
                                                : QByteArray("0x200");
        const QList<QByteArray> hops = trimmed.split(',');
        if (hops.isEmpty()) {
            rewrittenError = QStringLiteral("malformed SHARPNESS chain line");
            break;
        }
        // Keep everything up to and including the key, drop the rest of the
        // first hop, and append the diverging first hop.
        const QByteArray firstSegment = hops.first();
        const int keyAt = firstSegment.indexOf(key);
        if (keyAt < 0) {
            rewrittenError = QStringLiteral("SHARPNESS chain lost its key");
            break;
        }
        QByteArray rebuilt = firstSegment.left(keyAt + key.size());
        rebuilt += ' ';
        rebuilt += firstHopText;
        for (int i = 1; i < hops.size(); ++i)
            rebuilt += ',' + hops.at(i);
        fixtureLines.append(rebuilt);
        ++rewritten;
    }
    if (rewritten != 2) {
        std::cerr << "FAIL: expected to rewrite two SHARPNESS chains, rewrote "
                  << rewritten << " (" << qPrintable(rewrittenError) << ")\n";
        return false;
    }

    const QString target = dir.filePath(QStringLiteral("fixture.map"));
    QFile out(target);
    const QByteArray fixture = fixtureLines.join('\n');
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || out.write(fixture) != fixture.size() || !out.flush()) {
        std::cerr << "FAIL: cannot stage the fixture map\n";
        return false;
    }
    *pathOut = target;
    return true;
}

// The fixture rewrite above is the one place this suite departs from the
// shipped data, so verify it actually parses into the chains harness expects.
// Without this check a botched rewrite silently degrades into "empty offsets"
// and every sharpness test would fail in a way nobody could diagnose.
void checkFixtureMapContracts(const QString &fixturePath)
{
    mhw::AddressMap fixture;
    QString error;
    check(fixture.load(fixturePath, &error), "the fixture map parses");

    const std::vector<std::uintptr_t> sharp = fixture.offsets(
        QStringLiteral("SHARPNESS_OFFSETS"));
    const std::vector<std::uintptr_t> array = fixture.offsets(
        QStringLiteral("SHARPNESS_ARRAY_OFFSETS"));
    check(sharp.size() == 2, "the fixture SHARPNESS chain is still 2 hops");
    check(array.size() == 5, "the fixture SHARPNESS_ARRAY chain is still 5 hops");
    check(!sharp.empty() && !array.empty() && sharp.front() != array.front(),
          "the fixture's two SHARPNESS chains have diverging first hops");
    // The remaining hops must be the shipped ones.
    check(sharp.size() == 2 && sharp.back() == 0x1E8,
          "the fixture keeps the shipped SHARPNESS second hop");
    check(array.size() == 5 && array[1] == 0x4D8 && array[2] == 0xE0,
          "the fixture keeps the shipped SHARPNESS_ARRAY later hops");
    check(fixture.hasAddress(QStringLiteral("QUEST_ADDRESS"))
              && fixture.hasOffsets(QStringLiteral("QUEST_OFFSETS")),
          "the fixture keeps the shipped QUEST keys");
}

// Asserts the pristine shipped map still contains the keys and chain shapes
// this harness depends on, so the single-key rewriting above cannot hide an
// upstream rename or an offset change.
void checkShippedMapContracts()
{
    const QString source = QDir(QStringLiteral(MONSTER_SOURCE_DIR))
                               .absoluteFilePath(QStringLiteral(
                                   "data/MonsterHunterRise.16.0.2.0.map"));
    mhw::AddressMap shipped;
    QString error;
    check(shipped.load(source, &error),
          "the shipped Rise map parses (guards a corrupted data/ fixture)");

    bool allPresent = shipped.hasAddress(QStringLiteral("SHARPNESS_ADDRESS"))
                      && shipped.hasOffsets(QStringLiteral("SHARPNESS_OFFSETS"))
                      && shipped.hasOffsets(QStringLiteral("SHARPNESS_ARRAY_OFFSETS"))
                      && shipped.hasAddress(QStringLiteral("QUEST_ADDRESS"))
                      && shipped.hasOffsets(QStringLiteral("QUEST_OFFSETS"));
    check(allPresent, "the shipped map defines every key the tests need");

    check(shipped.offsets(QStringLiteral("SHARPNESS_OFFSETS")).size() == 2,
          "the shipped SHARPNESS chain is still 2 hops");
    check(shipped.offsets(QStringLiteral("SHARPNESS_ARRAY_OFFSETS")).size() == 5,
          "the shipped SHARPNESS_ARRAY chain is still 5 hops");
    check(shipped.offsets(QStringLiteral("QUEST_OFFSETS")).size() == 1
              && shipped.offsets(QStringLiteral("QUEST_OFFSETS")).front() == 0,
          "the shipped QUEST chain is still a single zero hop");
}

// Mirrors mhr_reader.cpp's file-local MHRSharpnessStructure (not exported, so
// the test has to spell it out).
struct SharpnessStruct {
    std::int32_t level;
    std::int32_t hits;
    std::int32_t maxHits;
};
static_assert(sizeof(SharpnessStruct) == 12);

// --------------------------------------------------------------------------
// A real MhrReader wired onto the fake image.
// --------------------------------------------------------------------------
struct ReaderHarness {
    mhw::MhrReader reader;
    FakeRiseImage image;
    std::size_t nextObject = 1;

    explicit ReaderHarness(const QString &mapPath)
        : reader(mapPath)
    {
        // Attach to our OWN pid: process_vm_readv on self always works, and
        // findRisePid() is never reached because these tests call the private
        // readers directly instead of going through poll()/ensureAttached().
        reader.memory_.attach(QCoreApplication::applicationPid(), nullptr);
        reader.imageBase_ = image.imageBase();
    }

    [[nodiscard]] std::uintptr_t freshObject()
    {
        // A wide stride so one chain's cells can never overlap another's:
        // every pointer the reader dereferences lives exactly kObjectStride
        // bytes into its own object, and a chain reads at most ~16 hops, so
        // even the last hop of one chain stays inside a page the next hop's
        // writes cannot reach.
        return image.object(nextObject++, kObjectStride - 0x800);
    }

    [[nodiscard]] std::uintptr_t anchor(const char *addressKey) const
    {
        return reader.imageBase_ + reader.map_.address(QLatin1String(addressKey));
    }

    // Lays out the chain named by `addressKey`/`offsetsKey` so the reader
    // resolves it to `finalTarget`, and returns that resolved address.
    //
    // followPointerChain() walks exactly one step per offset:
    //     addr_1 = *(anchor)                    + offsets[0]
    //     addr_2 = *(addr_1)                    + offsets[1]
    //     ...  addr_n = *(addr_{n-1})           + offsets[n-1]  == finalTarget
    // So the cell at addr_i stores V_i with V_i + offsets[i] == addr_{i+1}.
    //
    // Both SHARPNESS chains dereference the SAME anchor and therefore read the
    // same V_0. Their rewritten first hops differ (0x100 vs 0x200), so from
    // addr_1 on they are on separate cells and cannot clobber each other —
    // which is why `anchorPointer` is a parameter the caller sets once for the
    // whole pair.
    std::uintptr_t setupChain(const char *addressKey, const char *offsetsKey,
                              std::uintptr_t anchorPointer,
                              std::uintptr_t finalTarget)
    {
        const std::vector<std::uintptr_t> offsets =
            reader.map_.offsets(QLatin1String(offsetsKey));
        if (offsets.empty())
            return finalTarget;

        // addr_0 is the anchor cell; the value stored there is shared by
        // every chain using this address key.
        const std::uintptr_t anchorCell = anchor(addressKey);
        image.putPointer(anchorCell, anchorPointer);
        std::uintptr_t cell = anchorPointer + offsets[0];

        for (std::size_t i = 1; i < offsets.size(); ++i) {
            const std::uintptr_t nextCell = (i + 1 == offsets.size())
                                            ? finalTarget
                                            : freshObject();
            // *(cell) + offsets[i] == nextCell
            image.putPointer(cell, nextCell - offsets[i]);
            cell = nextCell;
        }
        return cell;
    }

    // Only the SHARPNESS chains share an anchor in the shipped map, so the
    // pointer written there must agree between them.
    void setupSharpnessPair(std::uintptr_t *sharpOut, std::uintptr_t *arrayOut)
    {
        const std::uintptr_t anchorPointer = freshObject();
        *sharpOut = setupChain("SHARPNESS_ADDRESS", "SHARPNESS_OFFSETS",
                              anchorPointer, freshObject());
        *arrayOut = setupChain("SHARPNESS_ADDRESS", "SHARPNESS_ARRAY_OFFSETS",
                              anchorPointer, freshObject());
    }

    // readSharpness keeps a threshold cache keyed on the array pointer, so
    // each scenario needs a clean one; the cache is a private member.
    void invalidateSharpnessCache()
    {
        reader.cachedSharpnessArrayPtr_ = 0;
        reader.cachedSharpnessThresholdsValid_ = false;
        reader.cachedSharpnessThresholds_.fill(0);
    }
};

// ==========================================================================
// readSharpness — the priority target.
//
// Paths: a normal readable gauge / the range guards / read failures,
// including a genuine PARTIAL read into the guard page.
// ==========================================================================
void testReadSharpness()
{
    QTemporaryDir dir;
    QString mapPath;
    check(installFixtureMap(dir, &mapPath), "fixture map is staged");
    if (!dir.isValid())
        return;

    checkFixtureMapContracts(mapPath);
    ReaderHarness h(mapPath);
    check(h.image.valid(), "the fake Rise image is mapped");
    if (!h.image.valid())
        return;

    // ---- path 1: a normal, readable gauge --------------------------------
    {
        h.invalidateSharpnessCache();
        std::uintptr_t sharp = 0;
        std::uintptr_t array = 0;
        h.setupSharpnessPair(&sharp, &array);
        check(sharp != array, "the two SHARPNESS chains resolve to distinct objects");
        check(sharp > h.image.imageBase() && array > h.image.imageBase(),
              "both SHARPNESS chains resolve inside the fake image");

        SharpnessStruct value{};
        value.level = 3;    // Yellow
        value.hits = 40;
        value.maxHits = 60;
        h.image.put<SharpnessStruct>(sharp, value);

        // The Mono int[] header: length at +0x1C, elements from +0x20.
        h.image.put<std::int32_t>(array + 0x1C, 7);
        const std::array<std::int32_t, 7> raw{{10, 10, 20, 30, 30, 30, 30}};
        for (int i = 0; i < 7; ++i)
            h.image.put<std::int32_t>(array + 0x20 + 4 * i, raw[i]);

        const mhw::SharpnessSnapshot snapshot = h.reader.readSharpness(0, nullptr);
        check(snapshot.valid, "a melee weapon with a readable array yields a valid gauge");
        check(snapshot.level == 3, "the gauge level matches the memory value");
        check(snapshot.currentHits == 40 && snapshot.maxHits == 60,
              "the gauge hits match the memory value");
        // Cumulative sums: 10, 20, 40, 70, 100, 130, 160.
        check(snapshot.thresholds[0] == 10 && snapshot.thresholds[1] == 20
              && snapshot.thresholds[2] == 40 && snapshot.thresholds[3] == 70,
              "cumulative thresholds follow riseBuildSharpnessThresholds");
        check(snapshot.threshold == snapshot.thresholds[2],
              "the gauge threshold is the previous colour's upper bound");

        // The cache is keyed on the array pointer: a changed level must be
        // visible on the very next poll without rebuilding the thresholds.
        h.image.put<std::int32_t>(sharp, 5);
        const mhw::SharpnessSnapshot again = h.reader.readSharpness(0, nullptr);
        check(again.valid && again.level == 5,
              "a level change is visible while the threshold cache is reused");
        check(again.thresholds[3] == 70,
              "the reused cache keeps the original cumulative thresholds");
    }

    // ---- path 2: the out-of-range guards ---------------------------------
    {
        h.invalidateSharpnessCache();
        std::uintptr_t sharp = 0;
        std::uintptr_t array = 0;
        h.setupSharpnessPair(&sharp, &array);
        h.image.put<std::int32_t>(array + 0x1C, 7);

        h.image.put<std::int32_t>(sharp, -1);   // Broken
        check(!h.reader.readSharpness(0, nullptr).valid,
              "level -1 (Broken) is rejected");

        h.image.put<std::int32_t>(sharp, 7);    // > 6
        const mhw::SharpnessSnapshot tooHigh = h.reader.readSharpness(0, nullptr);
        // SharpnessSnapshot::level starts at -1, so an out-of-range level
        // must leave it exactly there rather than echoing the memory value.
        check(!tooHigh.valid && tooHigh.level == -1,
              "level 7 is rejected without publishing a stale level");

        // Level 6 (White) is the HIGHEST valid colour and must still pass.
        // Without this case `level > 6` and `level >= 6` are
        // indistinguishable, so the boundary itself would go untested.
        h.image.put<std::int32_t>(sharp, 6);
        check(h.reader.readSharpness(0, nullptr).valid,
              "level 6 (White) is the highest accepted colour");
        check(h.reader.readSharpness(0, nullptr).level == 6,
              "level 6 is published verbatim");
        // And the gauge's threshold is the LAST cumulative entry, not
        // thresholds[6] (which would be one past the valid range).
        check(h.reader.readSharpness(0, nullptr).threshold
                  == h.reader.cachedSharpnessThresholds_[5],
              "level 6 resolves its threshold from the last colour slot");

        h.image.put<std::int32_t>(sharp, 2);
        check(h.reader.readSharpness(0, nullptr).valid,
              "a back-in-range level recovers after a rejected one");

        // The weapon guard runs before any read, even on perfect memory.
        const mhw::SharpnessSnapshot bow = h.reader.readSharpness(11, nullptr);
        check(!bow.valid && bow.level == -1,
              "a ranged weapon never reads a sharpness gauge");
        const mhw::SharpnessSnapshot none = h.reader.readSharpness(-1, nullptr);
        check(!none.valid && none.level == -1,
              "Weapon.None never reads a sharpness gauge");
    }

    // ---- path 3: read failures -------------------------------------------
    {
        // (a) The array chain stops resolving: the anchor cell is zeroed, so
        //     followPointerChain returns 0 before any array read.
        h.invalidateSharpnessCache();
        std::uintptr_t sharp = 0;
        std::uintptr_t array = 0;
        h.setupSharpnessPair(&sharp, &array);
        SharpnessStruct value{};
        value.level = 4;
        value.hits = 1;
        value.maxHits = 30;
        h.image.put<SharpnessStruct>(sharp, value);
        h.image.putPointer(h.anchor("SHARPNESS_ADDRESS"), 0);
        const mhw::SharpnessSnapshot noArray = h.reader.readSharpness(0, nullptr);
        check(!noArray.valid,
              "an unresolvable array chain leaves the gauge invalid");

        // (b) A genuine PARTIAL read: the array's length read at +0x1C starts
        //     2 bytes before the guard page, so process_vm_readv copies 2 of 4
        //     bytes and readBytes() reports reader.partial_read_failed.
        //     Both chains must share one anchor value for their first hop,
        //     then diverge into their own cells.
        h.invalidateSharpnessCache();
        std::uintptr_t sharpStraddle = 0;
        {
            const std::uintptr_t sharedAnchor = h.freshObject();
            sharpStraddle = h.setupChain(
                "SHARPNESS_ADDRESS", "SHARPNESS_OFFSETS", sharedAnchor,
                h.freshObject());
            h.setupChain(
                "SHARPNESS_ADDRESS", "SHARPNESS_ARRAY_OFFSETS", sharedAnchor,
                h.image.straddlingStruct(0x1C));
        }
        h.image.put<std::int32_t>(sharpStraddle, 1);
        {
            const mhw::SharpnessSnapshot partial =
                h.reader.readSharpness(0, nullptr);
            check(!partial.valid,
                  "a partial array-length read leaves the gauge invalid");
            check(!h.reader.cachedSharpnessThresholdsValid_,
                  "a failed array read invalidates the threshold cache");
        }

        // (c) A raw segment above the 10000-hit cap is rejected.
        h.invalidateSharpnessCache();
        h.setupSharpnessPair(&sharp, &array);
        h.image.put<std::int32_t>(sharp, 0);
        h.image.put<std::int32_t>(array + 0x1C, 7);
        h.image.put<std::int32_t>(array + 0x20, 10001);   // > kMaxSegmentHits
        const mhw::SharpnessSnapshot badSegment =
            h.reader.readSharpness(0, nullptr);
        check(!badSegment.valid,
              "a segment above the 10000-hit cap leaves the gauge invalid");
        check(!h.reader.cachedSharpnessThresholdsValid_,
              "a rejected segment invalidates the threshold cache");

        // (d) A declared array length outside [1, 7] is rejected.
        h.invalidateSharpnessCache();
        h.setupSharpnessPair(&sharp, &array);
        h.image.put<std::int32_t>(sharp, 3);
        h.image.put<std::int32_t>(array + 0x1C, 8);   // > 7
        check(!h.reader.readSharpness(0, nullptr).valid,
              "a declared array length above the 7-colour cap is rejected");
    }
}

// ==========================================================================
// readQuest — the second target.
//
// QUEST_OFFSETS is a single zero hop, so the quest struct is the value the
// anchor cell points at. Every field the reader touches is a small positive
// offset from there, so one scratch object holds the whole struct.
// ==========================================================================
void testReadQuest()
{
    QTemporaryDir dir;
    QString mapPath;
    check(installFixtureMap(dir, &mapPath), "fixture map is staged");
    if (!dir.isValid())
        return;

    ReaderHarness h(mapPath);
    if (!h.image.valid())
        return;

    // QUEST_OFFSETS is one hop of 0, so the anchor holds the struct pointer.
    const std::uintptr_t questAnchor = h.anchor("QUEST_ADDRESS");

    // ---- path 1: a normal quest with a readable Normal data pointer ------
    {
        const std::uintptr_t quest = h.freshObject();
        const std::uintptr_t questData = h.freshObject();
        const std::uintptr_t normal = h.freshObject();
        h.image.putPointer(questAnchor, quest);

        h.image.put<std::int32_t>(quest + 0x110, mhw::kRiseQuestStateInQuest);   // state
        h.image.put<std::int32_t>(quest + 0x120, 1);                        // Category
        h.image.put<std::int32_t>(quest + 0x15C, 3);                        // maxDeaths
        h.image.put<std::int32_t>(quest + 0x160, 1);                        // deaths
        h.image.put<float>(quest + 0x170, 600.0F);                          // elapsed
        h.image.put<float>(quest + 0x178, 1800.0F);                         // limit
        h.image.putPointer(quest + 0x118, questData);
        h.image.putPointer(questData + 0x10, normal);
        h.image.put<std::int32_t>(normal + 0x10, 42);                       // id
        h.image.put<std::int32_t>(normal + 0x24, 4);                        // stars
        h.image.put<std::int32_t>(normal + 0x28, 3);                        // rank

        const mhw::QuestSnapshot snapshot = h.reader.readQuest(nullptr);
        check(snapshot.state == mhw::kRiseQuestStateInQuest, "the quest state is read");
        check(snapshot.category == 1, "the quest category is read");
        check(snapshot.maxDeaths == 3 && snapshot.deaths == 1,
              "deaths and maxDeaths are read");
        check(snapshot.elapsedSeconds == 600.0F && snapshot.maxTimerSeconds == 1800.0F,
              "both timer fields are read from their documented offsets");
        check(snapshot.timeLeftSeconds == 1200.0F,
              "the countdown is derived as limit - elapsed");
        check(snapshot.id == 42, "the normal quest id is read");
        check(snapshot.stars == 5, "zero-based stars are surfaced as stars + 1");
        check(snapshot.rank == 3, "the quest rank is read");
        check(!snapshot.isAnomaly, "a normal quest is not flagged as anomaly");
        check(snapshot.active, "a positive id with a supported category is active");

        // A negative raw stars value clamps to 0 rather than displaying -1.
        h.image.put<std::int32_t>(normal + 0x24, -1);
        check(h.reader.readQuest(nullptr).stars == 0,
              "negative raw stars clamp to 0");
        // An INT_MAX raw value must not overflow.
        h.image.put<std::int32_t>(normal + 0x24, std::numeric_limits<int>::max());
        check(h.reader.readQuest(nullptr).stars == std::numeric_limits<int>::max(),
              "an INT_MAX raw stars value does not overflow");
    }

    // ---- path 2: the out-of-range / unsupported guards -------------------
    {
        const std::uintptr_t quest = h.freshObject();
        const std::uintptr_t questData = h.freshObject();
        const std::uintptr_t normal = h.freshObject();
        h.image.putPointer(questAnchor, quest);
        h.image.put<std::int32_t>(quest + 0x110, mhw::kRiseQuestStateInQuest);
        h.image.put<std::int32_t>(normal + 0x10, 42);
        h.image.putPointer(quest + 0x118, questData);
        h.image.putPointer(questData + 0x10, normal);

        // A category HunterPie's ToQuestType() does not map.
        h.image.put<std::int32_t>(quest + 0x120, 32);
        check(!h.reader.readQuest(nullptr).active,
              "an unsupported quest category is never active");

        // A zero id is not a quest, whatever the category says.
        h.image.put<std::int32_t>(quest + 0x120, 1);
        h.image.put<std::int32_t>(normal + 0x10, 0);
        check(!h.reader.readQuest(nullptr).active,
              "a zero id is never active");

        // State 2 (InQuest) is required even with a valid id and category.
        h.image.put<std::int32_t>(normal + 0x10, 42);
        h.image.put<std::int32_t>(quest + 0x110, 5);
        const mhw::QuestSnapshot wrongState = h.reader.readQuest(nullptr);
        check(!wrongState.active && wrongState.id == 42,
              "a non-InQuest state keeps the id but is not active");

        // Anomaly (Sunbreak) fallback: no Normal pointer, a readable Anomaly.
        h.image.put<std::int32_t>(quest + 0x110, mhw::kRiseQuestStateInQuest);
        h.image.putPointer(questData + 0x10, 0);
        const std::uintptr_t anomaly = h.freshObject();
        h.image.putPointer(questData + 0x28, anomaly);
        h.image.put<std::int32_t>(anomaly + 0x14, 77);
        h.image.put<std::int32_t>(anomaly + 0x18, 9);   // level -> stars
        const mhw::QuestSnapshot anomalySnap = h.reader.readQuest(nullptr);
        check(anomalySnap.id == 77 && anomalySnap.isAnomaly,
              "the anomaly branch supplies the id and sets isAnomaly");
        check(anomalySnap.stars == 9,
              "an anomaly level is surfaced as stars unchanged");
    }

    // ---- path 3: read failure (a genuine PARTIAL read) --------------------
    {
        // Place the struct so that the elapsed read at +0x170 crosses into the
        // guard page: 2 of 4 bytes copy, then EFAULT.
        const std::uintptr_t quest = h.image.straddlingStruct(0x170);
        h.image.putPointer(questAnchor, quest);
        // Every other field is at a lower offset, so it stays readable.
        h.image.put<std::int32_t>(quest + 0x110, mhw::kRiseQuestStateInQuest);
        h.image.put<std::int32_t>(quest + 0x120, 1);
        const std::uintptr_t questData = h.freshObject();
        const std::uintptr_t normal = h.freshObject();
        h.image.putPointer(quest + 0x118, questData);
        h.image.putPointer(questData + 0x10, normal);
        h.image.put<std::int32_t>(normal + 0x10, 42);

        const mhw::QuestSnapshot partial = h.reader.readQuest(nullptr);
        check(partial.elapsedSeconds == 0.0F,
              "a partial elapsed read leaves the field at its zero default");
        check(partial.maxTimerSeconds == 0.0F,
              "the limit read past the guard page leaves the field at zero");
        check(partial.timeLeftSeconds == 0.0F,
              "a missing timer pair derives no countdown");
        // The lower offsets still read, so the quest is still identified.
        check(partial.id == 42 && partial.state == mhw::kRiseQuestStateInQuest,
              "the fields before the straddling read still resolve");

        // A chain that does not resolve at all: the anchor points nowhere
        // usable, so nothing publishes.
        h.image.putPointer(questAnchor, 0);
        const mhw::QuestSnapshot detached = h.reader.readQuest(nullptr);
        check(detached.id == 0 && !detached.active,
              "an unresolvable quest chain publishes nothing");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    checkShippedMapContracts();
    testReadSharpness();
    testReadQuest();

    if (failures == 0) {
        std::cout << "ALL MHR READER DIRECT TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " check(s) failed\n";
    return 1;
}
