// SPDX-License-Identifier: Apache-2.0
// Core offsets and structures are derived from HunterPie/HunterPie (Apache-2.0).

#pragma once

// ProcessMemory, AddressMap and the four stateless process-memory helpers
// used by both games' readers. This header lives in monster-core so the Rise
// reader (src/rise/mhr_reader.cpp, a monster-core source) can compile without
// depending on the World-only mhw-reader library.
#include "core/game_snapshot.h"
#include "monster/monster_types.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mhw {

// Shared helpers used by the per-domain reader functions (monster, player,
// quest — each lives in its own .cpp linked into the same target).
inline bool isSanePointer(std::uintptr_t value)
{
    return value >= 0x10000 && value < 0x0000800000000000ULL;
}

#pragma pack(push, 1)
struct QuestData {
    std::int32_t maxDeaths;
    std::int32_t deaths;
};
#pragma pack(pop)
static_assert(sizeof(QuestData) == 8);

class AddressMap {
public:
    bool load(const QString &path, QString *error = nullptr);
    [[nodiscard]] std::uintptr_t address(const QString &key) const;
    [[nodiscard]] const std::vector<std::uintptr_t> &offsets(const QString &key) const;
    [[nodiscard]] bool hasAddress(const QString &key) const;
    [[nodiscard]] bool hasOffsets(const QString &key) const;

private:
    std::unordered_map<std::string, std::uintptr_t> addresses_;
    std::unordered_map<std::string, std::vector<std::uintptr_t>> offsets_;
};

class ProcessMemory {
public:
    ProcessMemory() = default;
    ~ProcessMemory();
    ProcessMemory(const ProcessMemory &) = delete;
    ProcessMemory &operator=(const ProcessMemory &) = delete;

    bool attach(qint64 pid, QString *error = nullptr);
    void detach();
    [[nodiscard]] bool attached() const;
    [[nodiscard]] qint64 pid() const;
    [[nodiscard]] std::uintptr_t imageBase(QString *error = nullptr,
                                           const QString &exeName = QStringLiteral("monsterhunterworld.exe")) const;
    bool readBytes(std::uintptr_t address, void *destination, std::size_t size, QString *error = nullptr) const;

    template <typename T>
    std::optional<T> read(std::uintptr_t address, QString *error = nullptr) const
    {
        T value{};
        if (!readBytes(address, &value, sizeof(T), error))
            return std::nullopt;
        return value;
    }

    template <typename T>
    std::vector<T> readArray(std::uintptr_t address, std::size_t count, QString *error = nullptr) const
    {
        // C1 (v0.7.5 audit): clamp garbage counts before they feed the
        // vector allocator. Every current call site passes a small
        // constant (2..128), so a bogus count can only arrive if future
        // code reads it from game memory at a TOCTOU moment (process
        // death / zone transition); an uncapped std::vector<T>(count)
        // would then throw bad_alloc/length_error straight into the
        // poll loop and terminate the whole overlay.
        constexpr std::size_t kMaxElements = 4096;
        if (count > kMaxElements) {
            if (error)
                *error = QStringLiteral("readArray: count %1 exceeds safety cap")
                             .arg(static_cast<qulonglong>(count));
            return {};
        }
        std::vector<T> values(count);
        if (count == 0)
            return values;
        if (!readBytes(address, values.data(), sizeof(T) * count, error))
            return {};
        return values;
    }

private:
    qint64 pid_{-1};
    int memFd_{-1};
};

struct HpCluster { std::uintptr_t hpAddr = 0; float maxHealth = 0.0F; };

// The four process-memory helpers. They are deliberately NOT members of
// MhwReader: each is a static function whose state lives only in its
// parameters (plus function-local statics for the PID cache), so both games'
// readers share them without either library depending on the other. They
// used to be MhwReader static members, which made monster-core's
// mhr_reader.cpp depend on symbols defined in mhw-reader — a library
// dependency turned upside down.
//
// findGamePid keeps its default exe name so the World call sites that omit
// the argument keep their original spelling.
std::optional<qint64> findGamePid(
    const QString &exeName = QStringLiteral("monsterhunterworld.exe"));
// Return the first candidate whose AddressMap parses. If none parses,
// retain the first path so the caller can report an actionable error.
[[nodiscard]] QString selectLoadableMap(const QStringList &candidates);
// HunterPie ReadAsync semantics: dereference at address, then add offset.
std::uintptr_t followPointerChain(const ProcessMemory &memory,
                                  std::uintptr_t address,
                                  const std::vector<std::uintptr_t> &offsets,
                                  QString *error = nullptr);
// HunterPie ReadPtrAsync semantics: at each hop read the pointer stored at
// address + offset. Keep it separate from followPointerChain (ReadAsync)
// because Rise maps deliberately use both encodings.
std::uintptr_t followPointerChainOffsetThenDeref(
    const ProcessMemory &memory, std::uintptr_t address,
    const std::vector<std::uintptr_t> &offsets, QString *error = nullptr);

// The World-side readers spell these four helpers as MhwReader static
// members, and so do the probe/doctor tools under tests/. MhwReader is
// defined in "mhw_reader.h"; this alias header pulls in neither that header
// nor its implementation, it just re-exports the names in the mhw namespace
// so existing World call sites keep compiling unchanged.
class MhwReader;

} // namespace mhw
