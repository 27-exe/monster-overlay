// SPDX-License-Identifier: Apache-2.0
//
// rise-probe — Monster Hunter Rise (PC / Steam / Proton, build 16.0.2.0)
// raw memory probe. Read-only diagnostic used to collect ground-truth
// values from a live game session so the overlay's interpretation can be
// compared against what the process actually contains.
//
// Why it exists: the overlay reports (a) wrong zone name, (b) no HP/ST in
// hubs, (c) stamina > 3000 inside a quest, (d) an empty monster panel. Each
// of those is an *interpretation* of a raw memory region, so this tool
// prints the raw region (plus every intermediate pointer-chain hop) and lets
// the reader's own view sit next to it.
//
// Design notes
// ------------
//  * Read-only. Uses process_vm_readv exclusively (mhw::ProcessMemory);
//    never writes to, pauses, or attaches as a debugger to the game.
//  * Every pointer chain is walked hop by hop here so a failure prints
//    *where* the chain broke instead of the reader's silent "return 0".
//    Both walkers mirror mhw::MhwReader::{followPointerChain,
//    followPointerChainOffsetThenDeref} and cross-check the final result
//    against the production implementation (drift detector).
//  * Nothing is filtered. The monster section dumps raw slots beyond the
//    reader's 5-slot cap and prints the raw id at +0x2D4, so "why is the
//    panel empty" usually answers itself (count 0? pointer garbage? id read
//    failure? stage not a hunting zone?).
//  * The reader's interpreted snapshot (MhrReader::poll) is printed last as
//    [reader] — that is the same computation the overlay performs, so a
//    mismatch between the raw dump above and [reader] is the bug.
//
// Usage: rise-probe [--map <path>] [--help]
// Exit codes: 0 = dumped · 1 = no game process · 2 = attach failed ·
//             3 = image base unresolved · 4 = map file unusable
//
// Offsets/structures derive from HunterPie (Apache-2.0) for the
// MonsterHunterRise.16.0.2.0 address map; see src/rise/mhr_reader.cpp.

#include "core/game_snapshot.h"
#include "mhw_reader.h"
#include "rise/mhr_abnormalities.h"
#include "rise/mhr_reader.h"
#include "rise/mhr_types.h"
#include "world/world_types.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#ifndef MHR_DEFAULT_MAP
#define MHR_DEFAULT_MAP ""
#endif

namespace {

constexpr std::uintptr_t kPointerSize = sizeof(std::uintptr_t);
// The reader caps the monster list at 5 slots (kRiseMonsterListMax); the
// probe dumps more than that on purpose so a too-small/large count field
// is visible.
constexpr int kProbeMonsterSlots = 8;

// ---------------------------------------------------------------------------
// Output helpers (stdout, UTF-8)
// ---------------------------------------------------------------------------

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

void line(const QString &text = QString())
{
    out() << text << '\n';
}

void section(const QString &name)
{
    line();
    line(QStringLiteral("=== [%1] ===").arg(name));
}

void sub(const QString &text)
{
    line(QStringLiteral("  %1").arg(text));
}

QString hexStr(std::uintptr_t value)
{
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(value), 0, 16);
}

QString hexPadded(std::uint64_t value, int width)
{
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(value), width, 16, QLatin1Char('0'));
}

QString fmtI32(std::int32_t value)
{
    return QStringLiteral("%1 (%2)").arg(value).arg(hexPadded(static_cast<std::uint32_t>(value), 8));
}

QString fmtI64(std::int64_t value)
{
    return QStringLiteral("%1 (%2)")
        .arg(static_cast<qlonglong>(value))
        .arg(hexPadded(static_cast<std::uint64_t>(value), 16));
}

QString fmtF32(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return QStringLiteral("%1 (bits %2)")
        .arg(QString::number(static_cast<double>(value), 'f', 3), hexPadded(bits, 8));
}

QString fmtBool(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString fmtFail(const QString &error)
{
    return QStringLiteral("<读取失败: %1>")
        .arg(error.isEmpty() ? QStringLiteral("地址非法或不可读") : error);
}

// ---------------------------------------------------------------------------
// Memory read helpers — every failure is reported with the address
// ---------------------------------------------------------------------------

template <typename T>
struct Field {
    bool ok{false};
    T value{};
    QString error;
};

template <typename T>
Field<T> readField(const mhw::ProcessMemory &memory, std::uintptr_t address)
{
    Field<T> field;
    QString error;
    const auto value = memory.read<T>(address, &error);
    if (!value) {
        field.error = error.isEmpty() ? QStringLiteral("地址非法或不可读") : error;
        return field;
    }
    field.ok = true;
    field.value = *value;
    return field;
}

void fieldLine(const QString &name, std::uintptr_t address, const QString &text)
{
    sub(QStringLiteral("%1 @ %2 = %3").arg(name.leftJustified(24), hexStr(address), text));
}

void printF32(const mhw::ProcessMemory &memory, const QString &name, std::uintptr_t address)
{
    const Field<float> field = readField<float>(memory, address);
    fieldLine(name, address, field.ok ? fmtF32(field.value) : fmtFail(field.error));
}

void printI32(const mhw::ProcessMemory &memory, const QString &name, std::uintptr_t address)
{
    const Field<std::int32_t> field = readField<std::int32_t>(memory, address);
    fieldLine(name, address, field.ok ? fmtI32(field.value) : fmtFail(field.error));
}

void printI64(const mhw::ProcessMemory &memory, const QString &name, std::uintptr_t address)
{
    const Field<std::int64_t> field = readField<std::int64_t>(memory, address);
    fieldLine(name, address, field.ok ? fmtI64(field.value) : fmtFail(field.error));
}

void printPtr(const mhw::ProcessMemory &memory, const QString &name, std::uintptr_t address)
{
    const Field<std::uintptr_t> field = readField<std::uintptr_t>(memory, address);
    if (!field.ok) {
        fieldLine(name, address, fmtFail(field.error));
        return;
    }
    fieldLine(name, address,
              QStringLiteral("%1 (合法指针=%2)")
                  .arg(hexStr(field.value), fmtBool(mhw::isSanePointer(field.value))));
}

void dumpHex(const mhw::ProcessMemory &memory, std::uintptr_t address, std::size_t size,
             const QString &label)
{
    sub(QStringLiteral("[hex] %1 — %2 字节 @ %3")
            .arg(label)
            .arg(static_cast<qulonglong>(size))
            .arg(hexStr(address)));
    for (std::size_t done = 0; done < size; done += 16) {
        const std::size_t chunk = std::min<std::size_t>(16, size - done);
        const std::uintptr_t rowAddress = address + done;
        std::array<unsigned char, 16> buffer{};
        QString error;
        if (!memory.readBytes(rowAddress, buffer.data(), chunk, &error)) {
            sub(QStringLiteral("    %1: %2").arg(hexStr(rowAddress), fmtFail(error)));
            continue;
        }
        QString hex;
        QString ascii;
        for (std::size_t i = 0; i < chunk; ++i) {
            const unsigned char byte = buffer[i];
            hex += QStringLiteral("%1 ").arg(static_cast<uint>(byte), 2, 16, QLatin1Char('0'));
            ascii += (byte >= 32 && byte < 127) ? QLatin1Char(static_cast<char>(byte))
                                                : QLatin1Char('.');
        }
        sub(QStringLiteral("    %1: %2 %3")
                .arg(hexStr(rowAddress), hex.leftJustified(16 * 3 - 1), ascii));
    }
}

void dumpIntWindow(const mhw::ProcessMemory &memory, std::uintptr_t base, std::uintptr_t from,
                   int count, const QString &label)
{
    sub(QStringLiteral("[int32] %1 (@ base+%2 .. +%3)")
            .arg(label, hexStr(from), hexStr(from + static_cast<std::uintptr_t>(count) * 4)));
    QString row;
    for (int i = 0; i < count; ++i) {
        const std::uintptr_t offset = from + static_cast<std::uintptr_t>(i) * 4;
        const Field<std::int32_t> value = readField<std::int32_t>(memory, base + offset);
        row += QStringLiteral("+%1=%2")
                   .arg(hexStr(offset), value.ok ? fmtI32(value.value) : QStringLiteral("<err>"));
        if ((i + 1) % 4 == 0 || i + 1 == count) {
            sub(QStringLiteral("    %1").arg(row));
            row.clear();
        } else {
            row += QStringLiteral("  ");
        }
    }
}

// ---------------------------------------------------------------------------
// v0.8.4-r19 monster-identity: crown/size evidence.
//
// The reader reports a Rise monster's size as the product HunterPie derives in
// MHRMonster.GetMonsterCrown (MHRMonster.cs:501-517):
//   ReadPtrAsync(monster, MONSTER_CROWN_OFFSETS) -> MHRSizeStructure at +0x24.
// 1.00 is a legitimate result (a monster rolled at 100 %), so seeing 1.00 is
// not by itself proof of a bad read — these helpers print the raw factors and
// scan the surrounding windows for other ratio-shaped floats, which is what
// tells the two cases apart in a live session.
// ---------------------------------------------------------------------------

QString scanRatioFloats(const mhw::ProcessMemory &memory, std::uintptr_t base,
                        std::uintptr_t from, std::uintptr_t to, const QString &label)
{
    QStringList hits;
    for (std::uintptr_t offset = from; offset + sizeof(float) <= to; offset += 4) {
        const Field<float> value = readField<float>(memory, base + offset);
        if (!value.ok || !std::isfinite(value.value))
            continue;
        if (value.value >= mhw::kRiseMinPlausibleSize
            && value.value <= mhw::kRiseMaxPlausibleSize) {
            hits << QStringLiteral("+%1=%2")
                        .arg(hexStr(offset), fmtF32(value.value));
        }
    }
    if (hits.isEmpty()) {
        return QStringLiteral("  [scan] %1 内 [%2, %3] 区间没有 float → 体形值不在该窗口")
            .arg(label, fmtF32(mhw::kRiseMinPlausibleSize), fmtF32(mhw::kRiseMaxPlausibleSize));
    }
    return QStringLiteral("  [scan] %1 内可作体形比的 float（%2..%3）：%4")
        .arg(label, fmtF32(mhw::kRiseMinPlausibleSize), fmtF32(mhw::kRiseMaxPlausibleSize),
             hits.join(QStringLiteral("  ")));
}

// ---------------------------------------------------------------------------
// Pointer-chain walkers
//
// The production helpers hide the intermediate hops. These return the same
// value AND a per-hop transcript, and cross-check the final address against
// the production implementation so the diagnostic cannot silently diverge
// from the reader it is meant to explain.
// ---------------------------------------------------------------------------

struct ChainTrace {
    bool ok{false};
    std::uintptr_t result{0};
    QStringList steps;
    QString failure;
    QString verify;
    // The Rise address table mixes two pointer encodings; the same offset list
    // read with the *other* encoding is reported as well, because when the two
    // disagree only the live dump can say which address really holds the
    // structure.
    bool altOk{false};
    std::uintptr_t altResult{0};
    QString altLabel;
};

enum class WalkMode { ReadThenAdd, AddThenRead };

QString walkModeName(WalkMode mode)
{
    return mode == WalkMode::ReadThenAdd ? QStringLiteral("read-then-add（跟随指针再 +偏移）")
                                         : QStringLiteral("add-then-deref（+偏移后解引用）");
}

// One walk of the offset list. ReadThenAdd mirrors
// mhw::MhwReader::followPointerChain, AddThenRead mirrors
// followPointerChainOffsetThenDeref. steps/failure may be null (alt pass).
bool walkChain(const mhw::ProcessMemory &memory, std::uintptr_t address,
               const std::vector<std::uintptr_t> &offsets, WalkMode mode,
               std::uintptr_t *result, QStringList *steps, QString *failure)
{
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const std::uintptr_t offset = offsets[i];
        std::uintptr_t slotAddress = address;
        if (mode == WalkMode::AddThenRead) {
            if (address > std::numeric_limits<std::uintptr_t>::max() - offset) {
                if (failure)
                    *failure = QStringLiteral("第 %1 步：地址 + 偏移溢出").arg(static_cast<int>(i) + 1);
                return false;
            }
            slotAddress = address + offset;
        }
        QString error;
        const auto next = memory.read<std::uintptr_t>(slotAddress, &error);
        if (!next || !mhw::isSanePointer(*next)) {
            if (failure) {
                const QString reason = next
                    ? QStringLiteral("(值 %1 不是合法指针)").arg(hexStr(*next))
                    : QStringLiteral("(%1)").arg(error);
                *failure = mode == WalkMode::ReadThenAdd
                    ? QStringLiteral("第 %1 步：读取 [%2] 失败 %3")
                          .arg(static_cast<int>(i) + 1).arg(hexStr(slotAddress), reason)
                    : QStringLiteral("第 %1 步：读取 [%2 + %3] = [%4] 失败 %5")
                          .arg(static_cast<int>(i) + 1)
                          .arg(hexStr(address), hexStr(offset), hexStr(slotAddress), reason);
            }
            return false;
        }
        const std::uintptr_t pointer = *next;
        if (mode == WalkMode::ReadThenAdd) {
            const std::uintptr_t nextAddress = pointer + offset;
            if (steps) {
                *steps << QStringLiteral("第 %1 步：*[%2] = %3，再 +%4 → %5")
                              .arg(static_cast<int>(i) + 1)
                              .arg(hexStr(slotAddress), hexStr(pointer), hexStr(offset),
                                   hexStr(nextAddress));
            }
            address = nextAddress;
        } else {
            if (steps) {
                *steps << QStringLiteral("第 %1 步：*[%2 + %3] = *[%4] = %5")
                              .arg(static_cast<int>(i) + 1)
                              .arg(hexStr(address), hexStr(offset), hexStr(slotAddress),
                                   hexStr(pointer));
            }
            address = pointer;
        }
    }
    *result = address;
    return true;
}

ChainTrace walk(const mhw::ProcessMemory &memory, std::uintptr_t address,
                const std::vector<std::uintptr_t> &offsets, const QString &name,
                WalkMode primary)
{
    ChainTrace trace;
    const std::uintptr_t start = address;
    if (offsets.empty()) {
        trace.ok = true;
        trace.result = address;
        trace.steps << QStringLiteral("%1 为空 → 直接使用 %2").arg(name, hexStr(address));
        return trace;
    }
    trace.ok = walkChain(memory, start, offsets, primary, &trace.result, &trace.steps, &trace.failure);
    if (!trace.ok)
        trace.result = 0;

    // Cross-check with the production implementation of the same encoding.
    const std::uintptr_t reference = primary == WalkMode::ReadThenAdd
        ? mhw::MhwReader::followPointerChain(memory, start, offsets, nullptr)
        : mhw::MhwReader::followPointerChainOffsetThenDeref(memory, start, offsets, nullptr);
    const QString readerName = primary == WalkMode::ReadThenAdd
        ? QStringLiteral("followPointerChain")
        : QStringLiteral("followPointerChainOffsetThenDeref");
    trace.verify = (reference == trace.result)
        ? QStringLiteral("[verify] 与 MhwReader::%1 结果一致 ✓").arg(readerName)
        : QStringLiteral("[verify] 与 MhwReader::%1 不一致！reader=%2，probe=%3")
              .arg(readerName, hexStr(reference), hexStr(trace.result));

    const WalkMode altMode = primary == WalkMode::ReadThenAdd ? WalkMode::AddThenRead
                                                              : WalkMode::ReadThenAdd;
    std::uintptr_t altResult = 0;
    if (walkChain(memory, start, offsets, altMode, &altResult, nullptr, nullptr)) {
        trace.altOk = true;
        trace.altResult = altResult;
        trace.altLabel = walkModeName(altMode);
    }
    return trace;
}

ChainTrace walkReadThenAdd(const mhw::ProcessMemory &memory, std::uintptr_t address,
                           const std::vector<std::uintptr_t> &offsets, const QString &name)
{
    return walk(memory, address, offsets, name, WalkMode::ReadThenAdd);
}

ChainTrace walkAddThenRead(const mhw::ProcessMemory &memory, std::uintptr_t address,
                           const std::vector<std::uintptr_t> &offsets, const QString &name)
{
    return walk(memory, address, offsets, name, WalkMode::AddThenRead);
}

// v0.8.4-r19 monster-identity — the live evidence for the size value.
//
// 1.00 is a legitimate Rise size (a monster rolled at 100 %), so a plain
// "size=1.00" in the [reader] block cannot tell "the game really says 100 %"
// apart from "the field moved and we read a default". This prints the raw
// factors at the HunterPie location, a hex dump of the crown object and a
// scan for other ratio-shaped floats in both windows, which does tell them
// apart. Read-only, like every other probe section.
void probeMonsterSize(const mhw::ProcessMemory &memory, const mhw::AddressMap &map,
                      std::uintptr_t monster)
{
    const ChainTrace crown = walkAddThenRead(
        memory, monster, map.offsets(QStringLiteral("MONSTER_CROWN_OFFSETS")),
        QStringLiteral("MONSTER_CROWN_OFFSETS"));
    if (!crown.ok) {
        sub(QStringLiteral("  体形链中断 → %1（读者 size 停在 0 = 未读到，不再伪装成 1.00）")
                .arg(crown.failure));
        return;
    }
    sub(QStringLiteral("  体形容器 = %1（= *(monster + MONSTER_CROWN_OFFSETS)）")
            .arg(hexStr(crown.result)));
    const Field<float> factorA = readField<float>(memory, crown.result + 0x24);
    const Field<float> factorB = readField<float>(memory, crown.result + 0x28);
    if (factorA.ok && factorB.ok) {
        sub(QStringLiteral("  MHRSizeStructure +0x24 A=%1  +0x28 B=%2  → 乘积 %3；读者采用 %4")
                .arg(fmtF32(factorA.value), fmtF32(factorB.value),
                     fmtF32(factorA.value * factorB.value),
                     fmtF32(mhw::riseMonsterSizeFromFactors(factorA.value, factorB.value))));
    } else {
        sub(QStringLiteral("  +0x24/+0x28 读取失败 → 读者 size=0（未读到）"));
    }
    dumpHex(memory, crown.result, 0x40, QStringLiteral("体形容器原始字节（核对 +0x24/+0x28）"));
    sub(scanRatioFloats(memory, monster, 0x2C0, 0x400, QStringLiteral("怪物对象 +0x2C0..+0x400")));
    sub(scanRatioFloats(memory, crown.result, 0x00, 0x60, QStringLiteral("体形容器 +0x00..+0x60")));
}

void printTrace(const ChainTrace &trace)
{
    for (const QString &step : trace.steps)
        sub(step);
    if (trace.ok)
        sub(QStringLiteral("解析结果 = %1").arg(hexStr(trace.result)));
    else
        sub(QStringLiteral("链路中断 → %1").arg(trace.failure));
    if (!trace.verify.isEmpty())
        sub(trace.verify);
    if (trace.ok && trace.altOk && trace.altResult != trace.result
        && mhw::isSanePointer(trace.altResult)) {
        sub(QStringLiteral("[alt] 同一偏移链改按 %1 解释 → %2（两者不同时，看哪个地址指向真实结构）")
                .arg(trace.altLabel, hexStr(trace.altResult)));
    }
}

// Mono nint[] header candidate: length at +0x1C, payload at +0x20. Used to
// decide which of two candidate addresses really is the array the reader wants.
void printMonoArrayCandidate(const mhw::ProcessMemory &memory, std::uintptr_t address,
                             const QString &label)
{
    if (!mhw::isSanePointer(address)) {
        sub(QStringLiteral("[cand] %1 = %2（非合法指针）").arg(label, hexStr(address)));
        return;
    }
    const Field<std::int32_t> length =
        readField<std::int32_t>(memory, address + mhw::kRiseMonoArrayLengthOffset);
    const Field<std::uintptr_t> first =
        readField<std::uintptr_t>(memory, address + mhw::kRiseMonoArrayDataOffset);
    const bool plausible = length.ok && first.ok && length.value >= 1 && length.value <= 8
        && mhw::isSanePointer(first.value);
    sub(QStringLiteral("[cand] %1 @ %2 → 长度(+0x1C)=%3，首元素(+0x20)=%4，像 Mono 数组=%5")
            .arg(label, hexStr(address),
                 length.ok ? fmtI32(length.value) : fmtFail(length.error),
                 first.ok ? hexStr(first.value) : fmtFail(first.error),
                 fmtBool(plausible)));
}

// MHRWirebugCountStructure candidate: {Default, Environment, Skill} at +0/+4/+8.
void printWirebugCountCandidate(const mhw::ProcessMemory &memory, std::uintptr_t address,
                                const QString &label)
{
    if (!mhw::isSanePointer(address)) {
        sub(QStringLiteral("[cand] %1 = %2（非合法指针）").arg(label, hexStr(address)));
        return;
    }
    const Field<mhw::MHRWirebugCountStructure> candidate =
        readField<mhw::MHRWirebugCountStructure>(memory, address);
    if (!candidate.ok) {
        sub(QStringLiteral("[cand] %1 @ %2 → 读取失败：%3").arg(label, hexStr(address), candidate.error));
        return;
    }
    sub(QStringLiteral("[cand] %1 @ %2 → default=%3 environment=%4 skill=%5，sane=%6")
            .arg(label, hexStr(address))
            .arg(candidate.value.default_)
            .arg(candidate.value.environment)
            .arg(candidate.value.skill)
            .arg(fmtBool(mhw::isRiseWirebugCountSane(candidate.value))));
}

QString wirebugTypeName(mhw::RiseWirebugType type)
{
    switch (type) {
    case mhw::RiseWirebugType::Default:     return QStringLiteral("默认");
    case mhw::RiseWirebugType::Environment: return QStringLiteral("环境");
    case mhw::RiseWirebugType::Skill:       return QStringLiteral("技能");
    case mhw::RiseWirebugType::None:        break;
    }
    return QStringLiteral("无");
}

// ---------------------------------------------------------------------------
// [stage]
// ---------------------------------------------------------------------------

struct StageProbe {
    bool ok{false};
    std::uintptr_t stageBase{0};
    mhw::MHRStageStructure stage{};
};

StageProbe probeStage(const mhw::ProcessMemory &memory, const mhw::AddressMap &map,
                      std::uintptr_t imageBase, const mhw::GameSnapshot &snapshot)
{
    section(QStringLiteral("stage"));
    StageProbe result;
    const std::uintptr_t absolute = imageBase + map.address(QStringLiteral("STAGE_ADDRESS"));
    sub(QStringLiteral("STAGE_ADDRESS = %1 → 绝对地址 %2")
            .arg(hexStr(map.address(QStringLiteral("STAGE_ADDRESS"))), hexStr(absolute)));
    const ChainTrace trace = walkReadThenAdd(
        memory, absolute, map.offsets(QStringLiteral("STAGE_OFFSETS")),
        QStringLiteral("STAGE_OFFSETS"));
    printTrace(trace);
    if (!trace.ok)
        return result;

    result.stageBase = trace.result;
    const std::uintptr_t structAddress = trace.result + 0x60ULL;
    sub(QStringLiteral("MHRStageStructure 地址 = %1 + 0x60 = %2")
            .arg(hexStr(trace.result), hexStr(structAddress)));
    const Field<mhw::MHRStageStructure> stage =
        readField<mhw::MHRStageStructure>(memory, structAddress);
    if (!stage.ok) {
        sub(QStringLiteral("MHRStageStructure 读取失败：%1").arg(stage.error));
        dumpHex(memory, structAddress > 0x20 ? structAddress - 0x20 : structAddress, 0x60,
                QStringLiteral("stageBase+0x40 附近原始字节"));
        return result;
    }

    result.ok = true;
    result.stage = stage.value;
    const mhw::MHRStageStructure &value = stage.value;
    sub(QStringLiteral("原始 6 个 int："));
    sub(QStringLiteral("  type      = %1").arg(fmtI32(value.type)));
    sub(QStringLiteral("  villageId = %1").arg(fmtI32(value.villageId)));
    sub(QStringLiteral("  unk1      = %1").arg(fmtI32(value.unk1)));
    sub(QStringLiteral("  section   = %1").arg(fmtI32(value.section)));
    sub(QStringLiteral("  unk2      = %1").arg(fmtI32(value.unk2)));
    sub(QStringLiteral("  huntingId = %1").arg(fmtI32(value.huntingId)));
    sub(QStringLiteral("isRiseHuntingZone = %1，isRiseTrainingRoom = %2")
            .arg(fmtBool(mhw::isRiseHuntingZone(value)), fmtBool(mhw::isRiseTrainingRoom(value))));
    sub(QStringLiteral("[reader] computeZoneId(经 MhrReader::poll) → zone = %1，名称 = \"%2\"")
            .arg(static_cast<int>(snapshot.zone))
            .arg(QString::fromUtf8(mhw::zoneName(snapshot.zone))));
    sub(QStringLiteral("        规则：type 0→-1 主菜单 · 3→199 选人 · 4→villageId+700 据点 · "
                       "5..11→huntingId+600 狩猎 · 12→-2 读盘 · 其它→-1"));
    dumpIntWindow(memory, trace.result, 0x50, 20,
                  QStringLiteral("stageBase 窗口（判断结构是否整体错位）"));
    dumpHex(memory, trace.result + 0x40, 0x40, QStringLiteral("stageBase+0x40 原始字节"));
    return result;
}

// ---------------------------------------------------------------------------
// [hud]
// ---------------------------------------------------------------------------

void probeHud(const mhw::ProcessMemory &memory, const mhw::AddressMap &map,
              std::uintptr_t imageBase, const mhw::GameSnapshot &snapshot)
{
    section(QStringLiteral("hud"));
    const std::uintptr_t absolute = imageBase + map.address(QStringLiteral("UI_ADDRESS"));
    sub(QStringLiteral("UI_ADDRESS = %1 → 绝对地址 %2")
            .arg(hexStr(map.address(QStringLiteral("UI_ADDRESS"))), hexStr(absolute)));
    const ChainTrace trace = walkReadThenAdd(
        memory, absolute, map.offsets(QStringLiteral("PLAYER_HUD_OFFSETS")),
        QStringLiteral("PLAYER_HUD_OFFSETS"));
    printTrace(trace);
    if (!trace.ok)
        return;

    const std::uintptr_t hud = trace.result;
    sub(QStringLiteral("MHRPlayerHudStructure @ %1（Pack=1，共 0x34 字节）").arg(hexStr(hud)));
    printF32(memory, QStringLiteral("health               +0x00"), hud + 0x00ULL);
    printF32(memory, QStringLiteral("recoverableHealth    +0x04"), hud + 0x04ULL);
    printF32(memory, QStringLiteral("maxHealth            +0x08"), hud + 0x08ULL);
    printF32(memory, QStringLiteral("currentHealth        +0x0C"), hud + 0x0CULL);
    printF32(memory, QStringLiteral("maximumHealth        +0x10"), hud + 0x10ULL);
    printI64(memory, QStringLiteral("unk14 (long)         +0x14"), hud + 0x14ULL);
    printF32(memory, QStringLiteral("heal                 +0x1C"), hud + 0x1CULL);
    printI64(memory, QStringLiteral("unk20 (long)         +0x20"), hud + 0x20ULL);
    printF32(memory, QStringLiteral("stamina              +0x28"), hud + 0x28ULL);
    printF32(memory, QStringLiteral("maxStamina           +0x2C"), hud + 0x2CULL);
    printF32(memory, QStringLiteral("maxExtendableStamina +0x30"), hud + 0x30ULL);
    sub(QStringLiteral("[reader] PlayerSnapshot: health=%1 maxHealth=%2 stamina=%3 maxStamina=%4 valid=%5")
            .arg(QString::number(snapshot.player.health, 'f', 3),
                 QString::number(snapshot.player.maxHealth, 'f', 3),
                 QString::number(snapshot.player.stamina, 'f', 3),
                 QString::number(snapshot.player.maxStamina, 'f', 3),
                 fmtBool(snapshot.player.valid)));
    dumpHex(memory, hud, 0x34, QStringLiteral("MHRPlayerHudStructure 原始字节"));
    dumpHex(memory, hud + 0x34, 0x40,
            QStringLiteral("结构之后 0x34..0x74（若字段错位，真值通常落在这里）"));
}

// ---------------------------------------------------------------------------
// [petalace] — HunterPie MHRPlayer.GetEquippedPetalaceStatsAsync
//
//   arrayPtr  = GEAR_ADDRESS         + PETALACES_ARRAY_OFFSETS (Mono nint[])
//   selected  = PLAYER_GEAR_ADDRESS  + SELECTED_PETALACE_OFFSETS (int, &0xFFFF)
//   struct    = petalacePtrs[selected % len]   (MHRPetalaceStructure, 0x20)
//   data      = *(struct + 0x18)               (Stats at +0x28)
//   stats     = *(data + 0x28)                 (MHRPetalaceStatsStructure, 0x30)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct PetalaceStructure {
    std::uint64_t reference;  // 0x00
    std::int32_t  unk0;       // 0x08
    std::int32_t  unk1;       // 0x0C
    std::int32_t  unk2;       // 0x10
    std::int32_t  id;         // 0x14
    std::uint64_t data;       // 0x18
};
#pragma pack(pop)
static_assert(sizeof(PetalaceStructure) == 0x20);
static_assert(offsetof(PetalaceStructure, id) == 0x14);
static_assert(offsetof(PetalaceStructure, data) == 0x18);

#pragma pack(push, 1)
struct PetalaceStatsStructure {
    std::int64_t  reference;  // 0x00
    std::int32_t  unk0;       // 0x08
    std::int32_t  unk1;       // 0x0C
    std::int64_t  unk2;       // 0x10
    std::int32_t  unk3;       // 0x18
    std::int32_t  unk4;       // 0x1C
    std::int32_t  healthUp;   // 0x20
    std::int32_t  staminaUp;  // 0x24
    std::int32_t  attackUp;   // 0x28
    std::int32_t  defenseUp;  // 0x2C
};
#pragma pack(pop)
static_assert(sizeof(PetalaceStatsStructure) == 0x30);
static_assert(offsetof(PetalaceStatsStructure, healthUp) == 0x20);
static_assert(offsetof(PetalaceStatsStructure, staminaUp) == 0x24);

void probePetalace(const mhw::ProcessMemory &memory, const mhw::AddressMap &map,
                   std::uintptr_t imageBase)
{
    section(QStringLiteral("petalace"));
    // 1. Array object (GEAR_ADDRESS + PETALACES_ARRAY_OFFSETS).
    const std::uintptr_t gearAbsolute = imageBase + map.address(QStringLiteral("GEAR_ADDRESS"));
    sub(QStringLiteral("GEAR_ADDRESS = %1 → 绝对地址 %2")
            .arg(hexStr(map.address(QStringLiteral("GEAR_ADDRESS"))), hexStr(gearAbsolute)));
    const ChainTrace arrayTrace = walkReadThenAdd(
        memory, gearAbsolute, map.offsets(QStringLiteral("PETALACES_ARRAY_OFFSETS")),
        QStringLiteral("PETALACES_ARRAY_OFFSETS"));
    printTrace(arrayTrace);

    // 2. Selected petalace id (PLAYER_GEAR_ADDRESS + SELECTED_PETALACE_OFFSETS).
    const std::uintptr_t selectedAbsolute =
        imageBase + map.address(QStringLiteral("PLAYER_GEAR_ADDRESS"));
    sub(QStringLiteral("PLAYER_GEAR_ADDRESS = %1 → 绝对地址 %2")
            .arg(hexStr(map.address(QStringLiteral("PLAYER_GEAR_ADDRESS"))),
                 hexStr(selectedAbsolute)));
    const ChainTrace selectedTrace = walkAddThenRead(
        memory, selectedAbsolute, map.offsets(QStringLiteral("SELECTED_PETALACE_OFFSETS")),
        QStringLiteral("SELECTED_PETALACE_OFFSETS"));
    printTrace(selectedTrace);

    if (!arrayTrace.ok || !selectedTrace.ok) {
        sub(QStringLiteral("链路不完整，跳过 petalace 结构读取（上方已标出断点）"));
        return;
    }

    const std::uintptr_t arrayPtr = arrayTrace.result;
    const Field<std::int32_t> arrayLength =
        readField<std::int32_t>(memory, arrayPtr + mhw::kRiseMonoArrayLengthOffset);
    const Field<std::int32_t> rawSelected = readField<std::int32_t>(memory, selectedTrace.result);
    if (!rawSelected.ok) {
        sub(QStringLiteral("选中 petalace id 读取失败：%1（地址 %2）")
                .arg(rawSelected.error, hexStr(selectedTrace.result)));
        return;
    }
    const int selectedId = rawSelected.value & 0xFFFF;
    sub(QStringLiteral("选中 petalace 原始值 = %1 → &0xFFFF = %2")
            .arg(fmtI32(rawSelected.value))
            .arg(selectedId));
    if (!arrayLength.ok) {
        sub(QStringLiteral("petalace 数组长度读取失败：%1").arg(arrayLength.error));
        return;
    }
    sub(QStringLiteral("petalace 数组：header=%1，长度(+0x1C)=%2")
            .arg(hexStr(arrayPtr))
            .arg(fmtI32(arrayLength.value)));
    dumpHex(memory, arrayPtr + 0x10, 0x40, QStringLiteral("数组头部原始字节"));

    const int length = std::clamp(arrayLength.value, 0, 64);
    if (length <= 0) {
        sub(QStringLiteral("数组长度为 0，无法索引 petalace 结构"));
        return;
    }
    const int index = selectedId % length;
    std::vector<std::uintptr_t> pointers(static_cast<std::size_t>(length), 0);
    for (int i = 0; i < length; ++i) {
        const Field<std::uintptr_t> element = readField<std::uintptr_t>(
            memory, arrayPtr + mhw::kRiseMonoArrayDataOffset + static_cast<std::uintptr_t>(i) * kPointerSize);
        if (!element.ok)
            continue;
        pointers[static_cast<std::size_t>(i)] = element.value;
    }
    sub(QStringLiteral("索引 = %1 %% %2 = %3，结构指针 = %4")
            .arg(selectedId)
            .arg(length)
            .arg(index)
            .arg(hexStr(pointers[static_cast<std::size_t>(index)])));
    if (!mhw::isSanePointer(pointers[static_cast<std::size_t>(index)])) {
        sub(QStringLiteral("索引到的结构指针不是合法指针 → petalace 读取会失败（叠加耐力/体力上限显示为 0）"));
        return;
    }

    const Field<PetalaceStructure> structure =
        readField<PetalaceStructure>(memory, pointers[static_cast<std::size_t>(index)]);
    if (!structure.ok) {
        sub(QStringLiteral("MHRPetalaceStructure 读取失败：%1").arg(structure.error));
        return;
    }
    sub(QStringLiteral("MHRPetalaceStructure：reference=%1 unk0=%2 unk1=%3 unk2=%4 id=%5 data=%6")
            .arg(hexStr(static_cast<std::uintptr_t>(structure.value.reference)),
                 fmtI32(structure.value.unk0), fmtI32(structure.value.unk1),
                 fmtI32(structure.value.unk2), fmtI32(structure.value.id),
                 hexStr(static_cast<std::uintptr_t>(structure.value.data))));
    dumpHex(memory, pointers[static_cast<std::size_t>(index)], sizeof(PetalaceStructure),
            QStringLiteral("MHRPetalaceStructure 原始字节"));

    if (!mhw::isSanePointer(static_cast<std::uintptr_t>(structure.value.data))) {
        sub(QStringLiteral("structure.data 不是合法指针，无法继续读 Stats"));
        return;
    }
    const Field<std::uintptr_t> statsPtr = readField<std::uintptr_t>(
        memory, static_cast<std::uintptr_t>(structure.value.data) + 0x28ULL);
    if (!statsPtr.ok || !mhw::isSanePointer(statsPtr.value)) {
        sub(QStringLiteral("Stats 指针读取失败（data+0x28）：%1")
                .arg(statsPtr.ok ? hexStr(statsPtr.value) : statsPtr.error));
        return;
    }
    const Field<PetalaceStatsStructure> stats = readField<PetalaceStatsStructure>(memory, statsPtr.value);
    if (!stats.ok) {
        sub(QStringLiteral("MHRPetalaceStatsStructure 读取失败：%1").arg(stats.error));
        return;
    }
    sub(QStringLiteral("MHRPetalaceStatsStructure @ %1").arg(hexStr(statsPtr.value)));
    sub(QStringLiteral("  HealthUp  +0x20 = %1").arg(fmtI32(stats.value.healthUp)));
    sub(QStringLiteral("  StaminaUp +0x24 = %1").arg(fmtI32(stats.value.staminaUp)));
    sub(QStringLiteral("  AttackUp  +0x28 = %1").arg(fmtI32(stats.value.attackUp)));
    sub(QStringLiteral("  DefenseUp +0x2C = %1").arg(fmtI32(stats.value.defenseUp)));
    // HunterPie MHRiseUtils: HealthUp + 100 (base) + 50 (food);
    // StaminaUp * 30 + 1500 (base) + 3000 (food).
    sub(QStringLiteral("  [HunterPie 公式] MaxPossibleHealth = %1，MaxPossibleStamina = %2")
            .arg(QString::number(stats.value.healthUp + 100 + 50),
                 QString::number(stats.value.staminaUp * 30 + 1500 + 3000)));
    dumpHex(memory, statsPtr.value, sizeof(PetalaceStatsStructure),
            QStringLiteral("MHRPetalaceStatsStructure 原始字节"));
}

// ---------------------------------------------------------------------------
// [player]
// ---------------------------------------------------------------------------

void probePlayer(const mhw::ProcessMemory &memory, const mhw::AddressMap &map,
                 std::uintptr_t imageBase, const mhw::GameSnapshot &snapshot)
{
    section(QStringLiteral("player"));

    // --- name -------------------------------------------------------------
    sub(QStringLiteral("-- 角色名（CHARACTER_ADDRESS → 0xA8,0x18 → [0x00] → +0x10 长度 / +0x14 UTF-16）"));
    const std::uintptr_t nameAbsolute =
        imageBase + map.address(QStringLiteral("CHARACTER_ADDRESS"));
    const ChainTrace nameTrace = walkReadThenAdd(
        memory, nameAbsolute, map.offsets(QStringLiteral("CHARACTER_OFFSETS")),
        QStringLiteral("CHARACTER_OFFSETS"));
    printTrace(nameTrace);
    std::uintptr_t characterNamePtr = 0;
    if (nameTrace.ok) {
        const Field<std::uintptr_t> namePtr =
            readField<std::uintptr_t>(memory, nameTrace.result);
        if (!namePtr.ok || !mhw::isSanePointer(namePtr.value)) {
            sub(QStringLiteral("[savePtr + 0x00] 名字指针无效：%1")
                    .arg(namePtr.ok ? hexStr(namePtr.value) : namePtr.error));
        } else {
            characterNamePtr = namePtr.value;
            const Field<std::int32_t> nameLength =
                readField<std::int32_t>(memory, characterNamePtr + 0x10ULL);
            if (nameLength.ok && nameLength.value > 0 && nameLength.value < 128) {
                std::vector<char16_t> buffer(static_cast<std::size_t>(nameLength.value));
                QString error;
                if (memory.readBytes(characterNamePtr + 0x14ULL, buffer.data(),
                                     sizeof(char16_t) * buffer.size(), &error)) {
                    const QString name = QString::fromUtf16(buffer.data(), nameLength.value).trimmed();
                    sub(QStringLiteral("名字指针 = %1，长度 = %2，解码 = \"%3\"")
                            .arg(hexStr(characterNamePtr))
                            .arg(nameLength.value)
                            .arg(name));
                } else {
                    sub(QStringLiteral("UTF-16 文本读取失败：%1").arg(error));
                }
            } else {
                sub(QStringLiteral("长度字段无效（%1）：%2")
                        .arg(hexStr(characterNamePtr + 0x10ULL),
                             nameLength.ok ? fmtI32(nameLength.value) : nameLength.error));
            }
            dumpHex(memory, characterNamePtr + 0x10ULL, 0x40,
                    QStringLiteral("名字头 + 前 32 个 UTF-16 单元"));
        }
    }

    // --- save slots / HR / MR --------------------------------------------
    sub(QStringLiteral("-- 存档槽 / HR / MR（SAVE_ADDRESS → 0xC0,0x18 → [0x20+slot*8] → +0x10 名字指针 / +0x18 等级）"));
    const std::uintptr_t saveAbsolute = imageBase + map.address(QStringLiteral("SAVE_ADDRESS"));
    const ChainTrace saveTrace = walkReadThenAdd(
        memory, saveAbsolute, map.offsets(QStringLiteral("SAVE_OFFSETS")),
        QStringLiteral("SAVE_OFFSETS"));
    printTrace(saveTrace);
    if (saveTrace.ok) {
        const Field<std::uintptr_t> slotArray =
            readField<std::uintptr_t>(memory, saveTrace.result);
        if (!slotArray.ok || !mhw::isSanePointer(slotArray.value)) {
            sub(QStringLiteral("saveBase 解引用失败：%1")
                    .arg(slotArray.ok ? hexStr(slotArray.value) : slotArray.error));
        } else {
            sub(QStringLiteral("存档槽数组基址 = %1").arg(hexStr(slotArray.value)));
            bool matched = false;
            for (int slot = 0; slot < 3; ++slot) {
                const std::uintptr_t entryAddress =
                    slotArray.value + 0x20ULL + static_cast<std::uintptr_t>(slot) * kPointerSize;
                const Field<std::uintptr_t> slotPtr = readField<std::uintptr_t>(memory, entryAddress);
                if (!slotPtr.ok || !mhw::isSanePointer(slotPtr.value)) {
                    sub(QStringLiteral("  slot %1：entry=%2 → 指针无效（%3）")
                            .arg(slot)
                            .arg(hexStr(entryAddress),
                                 slotPtr.ok ? hexStr(slotPtr.value) : slotPtr.error));
                    continue;
                }
                const Field<std::uintptr_t> slotNamePtr =
                    readField<std::uintptr_t>(memory, slotPtr.value + 0x10ULL);
                const std::optional<std::uintptr_t> slotName =
                    slotNamePtr.ok ? std::optional<std::uintptr_t>(slotNamePtr.value)
                                   : std::nullopt;
                const bool nameMatches = mhw::isRiseSaveSlotNameMatch(characterNamePtr, slotName);
                const Field<mhw::MHRPlayerLevelStructure> level =
                    readField<mhw::MHRPlayerLevelStructure>(memory, slotPtr.value + 0x18ULL);
                sub(QStringLiteral("  slot %1：slotPtr=%2 名字指针=%3 命中当前角色=%4")
                        .arg(slot)
                        .arg(hexStr(slotPtr.value),
                             slotNamePtr.ok ? hexStr(slotNamePtr.value) : QStringLiteral("<err>"),
                             fmtBool(nameMatches)));
                if (level.ok) {
                    sub(QStringLiteral("          等级结构 @ %1：highRank=%2 masterRank=%3")
                            .arg(hexStr(slotPtr.value + 0x18ULL),
                                 fmtI32(level.value.highRank), fmtI32(level.value.masterRank)));
                } else {
                    sub(QStringLiteral("          等级结构读取失败：%1").arg(level.error));
                }
                if (nameMatches) {
                    matched = true;
                    dumpHex(memory, slotPtr.value + 0x10ULL, 0x10,
                            QStringLiteral("命中的存档槽 +0x10..+0x20"));
                }
            }
            if (!matched) {
                sub(QStringLiteral("没有任何槽命中当前角色 → 读者会保留 MR/HR = 0（面板显示空白）"));
            }
        }
    }

    // --- weapon -----------------------------------------------------------
    sub(QStringLiteral("-- 武器（WEAPON_ADDRESS → 0x0 → +0x8C 原始 WeaponType）"));
    const std::uintptr_t weaponAbsolute =
        imageBase + map.address(QStringLiteral("WEAPON_ADDRESS"));
    const ChainTrace weaponTrace = walkReadThenAdd(
        memory, weaponAbsolute, map.offsets(QStringLiteral("WEAPON_OFFSETS")),
        QStringLiteral("WEAPON_OFFSETS"));
    printTrace(weaponTrace);
    if (weaponTrace.ok) {
        const Field<std::int32_t> weapon =
            readField<std::int32_t>(memory, weaponTrace.result + 0x8CULL);
        if (weapon.ok) {
            sub(QStringLiteral("原始 WeaponType(+0x8C) = %1 → Core 枚举映射 = %2")
                    .arg(fmtI32(weapon.value))
                    .arg(mhw::riseWeaponTypeToCore(weapon.value)));
        } else {
            sub(QStringLiteral("WeaponType 读取失败：%1").arg(weapon.error));
        }
    }

    // --- sharpness --------------------------------------------------------
    sub(QStringLiteral("-- 斩味（SHARPNESS_ADDRESS → 0xE0,0x1E8 = {Level,Hits,MaxHits}；"
                       "→ 0xE0,0x4D8,0xE0,0x10,0x0 = int[] 阈值）"));
    const std::uintptr_t sharpAbsolute =
        imageBase + map.address(QStringLiteral("SHARPNESS_ADDRESS"));
    const ChainTrace sharpTrace = walkReadThenAdd(
        memory, sharpAbsolute, map.offsets(QStringLiteral("SHARPNESS_OFFSETS")),
        QStringLiteral("SHARPNESS_OFFSETS"));
    printTrace(sharpTrace);
    if (sharpTrace.ok) {
        printI32(memory, QStringLiteral("level   +0x00"), sharpTrace.result + 0x00ULL);
        printI32(memory, QStringLiteral("hits    +0x04"), sharpTrace.result + 0x04ULL);
        printI32(memory, QStringLiteral("maxHits +0x08"), sharpTrace.result + 0x08ULL);
        dumpHex(memory, sharpTrace.result, 0x10, QStringLiteral("MHRSharpnessStructure"));
    }
    const ChainTrace sharpArrayTrace = walkReadThenAdd(
        memory, sharpAbsolute, map.offsets(QStringLiteral("SHARPNESS_ARRAY_OFFSETS")),
        QStringLiteral("SHARPNESS_ARRAY_OFFSETS"));
    printTrace(sharpArrayTrace);
    if (sharpArrayTrace.ok) {
        const std::uintptr_t arrayPtr = sharpArrayTrace.result;
        const Field<std::int32_t> length =
            readField<std::int32_t>(memory, arrayPtr + mhw::kRiseMonoArrayLengthOffset);
        sub(QStringLiteral("阈值数组长度 = %1")
                .arg(length.ok ? fmtI32(length.value) : fmtFail(length.error)));
        const std::size_t count = (length.ok && length.value > 0)
            ? std::min<std::size_t>(static_cast<std::size_t>(length.value), 7)
            : 0;
        if (count > 0) {
            const auto raw = memory.readArray<std::int32_t>(
                arrayPtr + mhw::kRiseMonoArrayDataOffset, count);
            QString rawText;
            for (std::size_t i = 0; i < raw.size(); ++i)
                rawText += QStringLiteral("[%1]=%2 ").arg(static_cast<int>(i)).arg(raw[i]);
            sub(QStringLiteral("原始阈值 = %1").arg(rawText.trimmed()));
            std::array<int, 7> cumulative{};
            if (mhw::riseBuildSharpnessThresholds(raw, &cumulative)) {
                QString cumulativeText;
                for (int i = 0; i < 7; ++i)
                    cumulativeText += QStringLiteral("[%1]=%2 ")
                                          .arg(i)
                                          .arg(cumulative[static_cast<std::size_t>(i)]);
                sub(QStringLiteral("累加阈值（面板使用）= %1").arg(cumulativeText.trimmed()));
            } else {
                sub(QStringLiteral("累加换算失败（数组值超出范围）"));
            }
        }
        dumpHex(memory, arrayPtr + 0x10, 0x40, QStringLiteral("阈值数组头部原始字节"));
    }
    sub(QStringLiteral("[reader] 昵称=\"%1\" MR=%2 HR=%3 武器=%4 斩味 level=%5 hits=%6 maxHits=%7")
            .arg(snapshot.player.name)
            .arg(snapshot.player.masterRank)
            .arg(snapshot.player.highRank)
            .arg(snapshot.player.weaponId)
            .arg(snapshot.player.sharpness.level)
            .arg(snapshot.player.sharpness.currentHits)
            .arg(snapshot.player.sharpness.maxHits));
}

// ---------------------------------------------------------------------------
// [quest]
// ---------------------------------------------------------------------------

void probeQuest(const mhw::ProcessMemory &memory, const mhw::AddressMap &map,
                std::uintptr_t imageBase, const mhw::GameSnapshot &snapshot)
{
    section(QStringLiteral("quest"));
    const std::uintptr_t absolute = imageBase + map.address(QStringLiteral("QUEST_ADDRESS"));
    sub(QStringLiteral("QUEST_ADDRESS = %1 → 绝对地址 %2")
            .arg(hexStr(map.address(QStringLiteral("QUEST_ADDRESS"))), hexStr(absolute)));
    const ChainTrace trace = walkReadThenAdd(
        memory, absolute, map.offsets(QStringLiteral("QUEST_OFFSETS")),
        QStringLiteral("QUEST_OFFSETS"));
    printTrace(trace);
    if (!trace.ok)
        return;

    const std::uintptr_t quest = trace.result;
    sub(QStringLiteral("MHRQuestStructure @ %1").arg(hexStr(quest)));
    printI32(memory, QStringLiteral("state      +0x110"), quest + 0x110ULL);
    printI32(memory, QStringLiteral("category   +0x120"), quest + 0x120ULL);
    printI32(memory, QStringLiteral("maxDeaths  +0x15C"), quest + 0x15CULL);
    printI32(memory, QStringLiteral("deaths     +0x160"), quest + 0x160ULL);
    printF32(memory, QStringLiteral("elapsed    +0x170"), quest + 0x170ULL);
    printF32(memory, QStringLiteral("timeLimit  +0x178"), quest + 0x178ULL);
    printPtr(memory, QStringLiteral("questDataPtr +0x118"), quest + 0x118ULL);

    const Field<std::uintptr_t> qdp = readField<std::uintptr_t>(memory, quest + 0x118ULL);
    if (qdp.ok && mhw::isSanePointer(qdp.value)) {
        printPtr(memory, QStringLiteral("  normalPtr  +0x10"), qdp.value + 0x10ULL);
        printPtr(memory, QStringLiteral("  anomalyPtr +0x28"), qdp.value + 0x28ULL);
        const Field<std::uintptr_t> normal = readField<std::uintptr_t>(memory, qdp.value + 0x10ULL);
        const Field<std::uintptr_t> anomaly = readField<std::uintptr_t>(memory, qdp.value + 0x28ULL);
        if (normal.ok && mhw::isSanePointer(normal.value)) {
            const Field<std::int32_t> id = readField<std::int32_t>(memory, normal.value + 0x10ULL);
            const Field<std::int32_t> stars = readField<std::int32_t>(memory, normal.value + 0x24ULL);
            const Field<std::int32_t> rank = readField<std::int32_t>(memory, normal.value + 0x28ULL);
            sub(QStringLiteral("  Normal 任务：id(+0x10)=%1 stars原始(+0x24)=%2 rank(+0x28)=%3")
                    .arg(id.ok ? fmtI32(id.value) : QStringLiteral("<err>"),
                         stars.ok ? fmtI32(stars.value) : QStringLiteral("<err>"),
                         rank.ok ? fmtI32(rank.value) : QStringLiteral("<err>")));
            if (stars.ok)
                sub(QStringLiteral("  → 面板星数（riseNormalQuestStars）= %1")
                        .arg(mhw::riseNormalQuestStars(stars.value)));
        } else {
            sub(QStringLiteral("  Normal 指针无效：%1")
                    .arg(normal.ok ? hexStr(normal.value) : normal.error));
        }
        if (anomaly.ok && mhw::isSanePointer(anomaly.value)) {
            const Field<std::int32_t> id = readField<std::int32_t>(memory, anomaly.value + 0x14ULL);
            const Field<std::int32_t> level = readField<std::int32_t>(memory, anomaly.value + 0x18ULL);
            sub(QStringLiteral("  Anomaly 任务：id(+0x14)=%1 level(+0x18)=%2")
                    .arg(id.ok ? fmtI32(id.value) : QStringLiteral("<err>"),
                         level.ok ? fmtI32(level.value) : QStringLiteral("<err>")));
        } else {
            sub(QStringLiteral("  Anomaly 指针无效：%1")
                    .arg(anomaly.ok ? hexStr(anomaly.value) : anomaly.error));
        }
    } else {
        sub(QStringLiteral("questDataPtr 无效：%1")
                .arg(qdp.ok ? hexStr(qdp.value) : qdp.error));
    }
    dumpIntWindow(memory, quest, 0x100, 32, QStringLiteral("quest+0x100 .. +0x180 原始 int32"));
    sub(QStringLiteral("[reader] quest state=%1 category=%2 id=%3 stars=%4 elapsed=%5 max=%6 active=%7")
            .arg(snapshot.quest.state)
            .arg(snapshot.quest.category)
            .arg(snapshot.quest.id)
            .arg(snapshot.quest.stars)
            .arg(QString::number(snapshot.quest.elapsedSeconds, 'f', 1),
                 QString::number(snapshot.quest.maxTimerSeconds, 'f', 1),
                 fmtBool(snapshot.quest.active)));
}

// ---------------------------------------------------------------------------
// [monsters]
// ---------------------------------------------------------------------------

void probeMonsters(const mhw::ProcessMemory &memory, const mhw::AddressMap &map,
                   std::uintptr_t imageBase, const StageProbe &stage,
                   const mhw::GameSnapshot &snapshot)
{
    section(QStringLiteral("monsters"));
    const std::uintptr_t absolute = imageBase + map.address(QStringLiteral("MONSTERS_ADDRESS"));
    sub(QStringLiteral("MONSTERS_ADDRESS = %1 → 绝对地址 %2")
            .arg(hexStr(map.address(QStringLiteral("MONSTERS_ADDRESS"))), hexStr(absolute)));
    const ChainTrace trace = walkReadThenAdd(
        memory, absolute, map.offsets(QStringLiteral("MONSTER_LIST_OFFSETS")),
        QStringLiteral("MONSTER_LIST_OFFSETS"));
    printTrace(trace);
    if (stage.ok) {
        sub(QStringLiteral("注意：MhrReader::poll 仅在 isRiseHuntingZone(stage)=%1 时读取怪物列表")
                .arg(fmtBool(mhw::isRiseHuntingZone(stage.stage))));
    }
    if (!trace.ok) {
        dumpHex(memory, absolute, 0x40, QStringLiteral("MONSTERS_ADDRESS 起点原始字节"));
        return;
    }

    const std::uintptr_t base = trace.result;
    const Field<std::int32_t> rawLength =
        readField<std::int32_t>(memory, base + mhw::kRiseMonoArrayLengthOffset);
    sub(QStringLiteral("数组 header=%1，长度(+0x1C)=%2")
            .arg(hexStr(base))
            .arg(rawLength.ok ? fmtI32(rawLength.value) : fmtFail(rawLength.error)));
    const int readerCount = mhw::riseMonsterListCount(rawLength.ok
        ? std::optional<std::int32_t>(rawLength.value)
        : std::nullopt);
    sub(QStringLiteral("读者采用的 count（cap %1）= %2").arg(mhw::kRiseMonsterListMax).arg(readerCount));
    printMonoArrayCandidate(memory, base, QStringLiteral("读者实际使用的地址"));
    if (trace.altOk && trace.altResult != base)
        printMonoArrayCandidate(memory, trace.altResult, QStringLiteral("[alt] 另一种编码给出的地址"));
    dumpHex(memory, base + 0x10, 0x50, QStringLiteral("数组头部原始字节（含长度与指针槽）"));

    int dumpedIdWindows = 0;
    int collected = 0;
    for (int slot = 0; slot < kProbeMonsterSlots; ++slot) {
        const std::uintptr_t elementAddress =
            base + mhw::kRiseMonoArrayDataOffset + static_cast<std::uintptr_t>(slot) * kPointerSize;
        const Field<std::uintptr_t> element = readField<std::uintptr_t>(memory, elementAddress);
        line();
        sub(QStringLiteral("slot %1（element @ %2，读者%3）：")
                .arg(slot)
                .arg(hexStr(elementAddress),
                     slot < readerCount ? QStringLiteral("会读取") : QStringLiteral("不会读取")));
        if (!element.ok) {
            sub(QStringLiteral("  指针读取失败：%1").arg(element.error));
            continue;
        }
        sub(QStringLiteral("  原始指针 = %1（合法=%2）")
                .arg(hexStr(element.value), fmtBool(mhw::isSanePointer(element.value))));
        if (!mhw::isSanePointer(element.value)) {
            sub(QStringLiteral("  → 读者判定：丢弃（指针非法）"));
            continue;
        }
        const std::uintptr_t monster = element.value;
        const Field<std::int32_t> id = readField<std::int32_t>(memory, monster + 0x2D4ULL);
        if (!id.ok) {
            sub(QStringLiteral("  id(+0x2D4) 读取失败：%1 → 读者判定：丢弃").arg(id.error));
            dumpHex(memory, monster + 0x2C0, 0x40, QStringLiteral("怪物结构 +0x2C0 窗口"));
            continue;
        }
        sub(QStringLiteral("  id(+0x2D4) = %1（hasRiseMonsterId=%2）")
                .arg(fmtI32(id.value),
                     fmtBool(mhw::hasRiseMonsterId(std::optional<std::int32_t>(id.value)))));
        if (dumpedIdWindows < 3) {
            ++dumpedIdWindows;
            dumpIntWindow(memory, monster, 0x2C0, 16,
                          QStringLiteral("怪物 +0x2C0 窗口（校验 id 偏移）"));
        }

        const ChainTrace healthComponent = walkAddThenRead(
            memory, monster, map.offsets(QStringLiteral("MONSTER_HEALTH_COMPONENT_OFFSETS")),
            QStringLiteral("MONSTER_HEALTH_COMPONENT_OFFSETS"));
        float maxHealth = 0.0F;
        bool hasMax = false;
        if (healthComponent.ok) {
            const Field<float> value = readField<float>(memory, healthComponent.result + 0x18ULL);
            if (value.ok) {
                maxHealth = value.value;
                hasMax = true;
            }
            const ChainTrace encoded = walkAddThenRead(
                memory, healthComponent.result,
                map.offsets(QStringLiteral("MONSTER_HEALTH_COMPONENT_ENCODED_OFFSETS")),
                QStringLiteral("MONSTER_HEALTH_COMPONENT_ENCODED_OFFSETS"));
            if (encoded.ok) {
                const Field<float> health = readField<float>(memory, encoded.result + 0x18ULL);
                if (health.ok && hasMax) {
                    sub(QStringLiteral("  HP = %1 / %2")
                            .arg(QString::number(health.value, 'f', 1),
                                 QString::number(maxHealth, 'f', 1)));
                } else if (!health.ok) {
                    sub(QStringLiteral("  HP 读取失败：%1").arg(health.error));
                }
            } else {
                sub(QStringLiteral("  当前 HP 链中断 → %1").arg(encoded.failure));
            }
            sub(QStringLiteral("  最大 HP = %1，读者判定：收录（有 id）")
                    .arg(hasMax ? fmtF32(maxHealth) : QStringLiteral("<读取失败>")));
        } else {
            sub(QStringLiteral("  HP 链中断 → %1（读者会保留该怪但 HP=0）").arg(healthComponent.failure));
            sub(QStringLiteral("  读者判定：收录（有 id，HP 字段为 0）"));
        }
        // v0.8.4-r19 monster-identity: size / crown evidence (raw factors,
        // crown-object dump, ratio scan). See probeMonsterSize().
        probeMonsterSize(memory, map, monster);
        ++collected;
    }
    line();
    sub(QStringLiteral("汇总：count=%1，8 个槽中 %2 个有合法指针+可读 id")
            .arg(readerCount)
            .arg(collected));
    sub(QStringLiteral("[reader] poll 收录怪数 = %1").arg(snapshot.monsters.size()));
    for (const mhw::MonsterSnapshot &monster : snapshot.monsters) {
        sub(QStringLiteral("  reader 怪：id=%1（%2） @ %3 HP=%4/%5 size=%6 enraged=%7 parts=%8 ailments=%9")
                .arg(monster.id)
                .arg(monster.internalName.isEmpty()
                         ? QStringLiteral("无名称")
                         : monster.internalName)
                .arg(hexStr(monster.address))
                .arg(QString::number(monster.health, 'f', 1),
                     QString::number(monster.maxHealth, 'f', 1),
                     QString::number(monster.size, 'f', 2),
                     fmtBool(monster.enraged))
                .arg(monster.parts.size())
                .arg(monster.ailments.size()));
    }

    // v0.8.4-r23 [ailments] raw per-slot dump — forensics for the reported
    // "1-0-1-2-1" buildup wobble. Resolves the SAME chain the reader uses
    // (MONSTER_AILMENTS_OFFSETS / followPointerChainOffsetThenDeref) and
    // prints every slot's raw values + pointer targets, so consecutive probe
    // runs can be diffed for jitter, pointer movement, and reader-vs-raw
    // agreement.
    for (const mhw::MonsterSnapshot &monster : snapshot.monsters) {
        line();
        sub(QStringLiteral("[ailments] 怪 id=%1 @ %2 原始槽位 dump：")
                .arg(monster.id)
                .arg(hexStr(monster.address)));
        const std::uintptr_t ailBase = mhw::MhwReader::followPointerChainOffsetThenDeref(
            memory, monster.address,
            map.offsets(QStringLiteral("MONSTER_AILMENTS_OFFSETS")), nullptr);
        if (!ailBase) {
            sub(QStringLiteral("  MONSTER_AILMENTS_OFFSETS 链中断"));
            continue;
        }
        const Field<std::int32_t> countF = readField<std::int32_t>(memory, ailBase + 0x1CULL);
        if (!countF.ok || countF.value <= 0) {
            sub(QStringLiteral("  count(@+0x1C) 无效：%1")
                    .arg(countF.ok ? fmtI32(countF.value) : fmtFail(countF.error)));
            continue;
        }
        const int slotCount = std::min<int>(countF.value, 17);
        sub(QStringLiteral("  count=%1（cap 17 → %2 槽）").arg(countF.value).arg(slotCount));
        for (int i = 0; i < slotCount; ++i) {
            const std::uintptr_t slotAddress = ailBase + 0x20ULL
                + static_cast<std::uintptr_t>(i) * kPointerSize;
            const Field<std::uintptr_t> aField = readField<std::uintptr_t>(memory, slotAddress);
            if (!aField.ok || !mhw::isSanePointer(aField.value)) {
                sub(QStringLiteral("    slot %1: 指针无效（%2）")
                        .arg(i)
                        .arg(aField.ok ? hexStr(aField.value) : fmtFail(aField.error)));
                continue;
            }
            const std::uintptr_t a = aField.value;
            const Field<std::uintptr_t> cPtr = readField<std::uintptr_t>(memory, a + 0x18ULL);
            const Field<std::uintptr_t> bPtr = readField<std::uintptr_t>(memory, a + 0x68ULL);
            const Field<std::uintptr_t> mPtr = readField<std::uintptr_t>(memory, a + 0x78ULL);
            const Field<std::int32_t> counter = (cPtr.ok && mhw::isSanePointer(cPtr.value))
                ? readField<std::int32_t>(memory, cPtr.value + 0x20ULL) : Field<std::int32_t>{};
            const Field<float> build = (bPtr.ok && mhw::isSanePointer(bPtr.value))
                ? readField<float>(memory, bPtr.value + 0x20ULL) : Field<float>{};
            const Field<float> maxBuild = (mPtr.ok && mhw::isSanePointer(mPtr.value))
                ? readField<float>(memory, mPtr.value + 0x20ULL) : Field<float>{};
            const Field<float> timer = readField<float>(memory, a + 0x48ULL);
            const Field<float> maxTimer = readField<float>(memory, a + 0x44ULL);
            sub(QStringLiteral("    slot %1: counter=%2 buildup=%3 maxBuildup=%4 "
                               "timer=%5/%6 | a=%7 cPtr=%8 bPtr=%9 mPtr=%10")
                    .arg(i)
                    .arg(counter.ok ? QString::number(counter.value) : QStringLiteral("?"))
                    .arg(build.ok ? QString::number(build.value, 'f', 4) : QStringLiteral("?"))
                    .arg(maxBuild.ok ? QString::number(maxBuild.value, 'f', 4) : QStringLiteral("?"))
                    .arg(timer.ok ? QString::number(timer.value, 'f', 2) : QStringLiteral("?"))
                    .arg(maxTimer.ok ? QString::number(maxTimer.value, 'f', 2) : QStringLiteral("?"))
                    .arg(hexStr(a))
                    .arg(cPtr.ok && mhw::isSanePointer(cPtr.value) ? hexStr(cPtr.value)
                                                                   : QStringLiteral("?"))
                    .arg(bPtr.ok && mhw::isSanePointer(bPtr.value) ? hexStr(bPtr.value)
                                                                   : QStringLiteral("?"))
                    .arg(mPtr.ok && mhw::isSanePointer(mPtr.value) ? hexStr(mPtr.value)
                                                                   : QStringLiteral("?")));
        }
        for (const mhw::MonsterAilmentSnapshot &ail : monster.ailments) {
            sub(QStringLiteral("    reader: %1 counter=%2 buildup=%3 maxBuildup=%4 timer=%5/%6")
                    .arg(ail.name)
                    .arg(ail.counter)
                    .arg(QString::number(ail.buildup, 'f', 4))
                    .arg(QString::number(ail.maxBuildup, 'f', 4))
                    .arg(QString::number(ail.timer, 'f', 2))
                    .arg(QString::number(ail.maxTimer, 'f', 2)));
        }
    }

    // v0.8.4-r23 [stamina] raw monster stamina chain. HunterPie
    // MHRMonster.GetMonsterStamina(): monster + MONSTER_STAMINA_OFFSETS(0x320)
    // → ptr → MHRStaminaStructure {Stamina @+0x20, MaxStamina @+0x24}
    // (sequential layout: long Ref; int,int; long; int,int; float Stamina;
    // float MaxStamina). This pair is the REAL fatigue source: the monster
    // drools when stamina is depleted; the exhaust ailment slot (slot 6)
    // tracks the separate 減気/減気ひるみ gauge. Neighbouring floats are
    // printed so the layout can be sanity-checked from a single dump.
    for (const mhw::MonsterSnapshot &monster : snapshot.monsters) {
        const std::uintptr_t st = mhw::MhwReader::followPointerChainOffsetThenDeref(
            memory, monster.address,
            map.offsets(QStringLiteral("MONSTER_STAMINA_OFFSETS")), nullptr);
        if (!st) {
            sub(QStringLiteral("[stamina] 怪 id=%1: MONSTER_STAMINA_OFFSETS 链中断")
                    .arg(monster.id));
            continue;
        }
        const Field<float> stam = readField<float>(memory, st + 0x20ULL);
        const Field<float> maxStam = readField<float>(memory, st + 0x24ULL);
        const Field<float> f18 = readField<float>(memory, st + 0x18ULL);
        const Field<float> f1C = readField<float>(memory, st + 0x1CULL);
        const Field<float> f28 = readField<float>(memory, st + 0x28ULL);
        const Field<float> f2C = readField<float>(memory, st + 0x2CULL);
        sub(QStringLiteral("[stamina] 怪 id=%1: stamina=%2 maxStamina=%3 | "
                           "+0x18=%4 +0x1C=%5 +0x28=%6 +0x2C=%7 | ptr=%8")
                .arg(monster.id)
                .arg(stam.ok ? QString::number(stam.value, 'f', 4) : QStringLiteral("?"))
                .arg(maxStam.ok ? QString::number(maxStam.value, 'f', 4) : QStringLiteral("?"))
                .arg(f18.ok ? QString::number(f18.value, 'f', 4) : QStringLiteral("?"))
                .arg(f1C.ok ? QString::number(f1C.value, 'f', 4) : QStringLiteral("?"))
                .arg(f28.ok ? QString::number(f28.value, 'f', 4) : QStringLiteral("?"))
                .arg(f2C.ok ? QString::number(f2C.value, 'f', 4) : QStringLiteral("?"))
                .arg(hexStr(st)));
    }
}

// ---------------------------------------------------------------------------
// [wirebugs]
// ---------------------------------------------------------------------------

void probeWirebugs(const mhw::ProcessMemory &memory, const mhw::AddressMap &map,
                   std::uintptr_t imageBase, const mhw::GameSnapshot &snapshot)
{
    section(QStringLiteral("wirebugs"));
    const std::uintptr_t absolute =
        imageBase + map.address(QStringLiteral("ABNORMALITIES_ADDRESS"));
    sub(QStringLiteral("ABNORMALITIES_ADDRESS = %1 → 绝对地址 %2")
            .arg(hexStr(map.address(QStringLiteral("ABNORMALITIES_ADDRESS"))), hexStr(absolute)));

    const ChainTrace countTrace = walkReadThenAdd(
        memory, absolute, map.offsets(QStringLiteral("WIREBUG_COUNT_OFFSETS")),
        QStringLiteral("WIREBUG_COUNT_OFFSETS"));
    printTrace(countTrace);
    if (!countTrace.ok)
        return;

    const Field<mhw::MHRWirebugCountStructure> count =
        readField<mhw::MHRWirebugCountStructure>(memory, countTrace.result);
    if (!count.ok) {
        sub(QStringLiteral("MHRWirebugCountStructure 读取失败：%1").arg(count.error));
        return;
    }
    const mhw::MHRWirebugCountStructure value = count.value;
    sub(QStringLiteral("原始 count：default=%1 environment=%2 skill=%3 → total=%4，isRiseWirebugCountSane=%5")
            .arg(value.default_)
            .arg(value.environment)
            .arg(value.skill)
            .arg(value.default_ + value.environment + value.skill)
            .arg(fmtBool(mhw::isRiseWirebugCountSane(value))));
    dumpHex(memory, countTrace.result, 0x20, QStringLiteral("count 结构原始字节"));
    if (countTrace.altOk && countTrace.altResult != countTrace.result)
        printWirebugCountCandidate(memory, countTrace.altResult,
                                   QStringLiteral("[alt] 另一种编码给出的 count 地址"));

    const ChainTrace dataTrace = walkReadThenAdd(
        memory, absolute, map.offsets(QStringLiteral("WIREBUG_DATA_OFFSETS")),
        QStringLiteral("WIREBUG_DATA_OFFSETS"));
    printTrace(dataTrace);
    if (!dataTrace.ok)
        return;

    const std::uintptr_t arrayPtr = dataTrace.result;
    const Field<std::int32_t> length =
        readField<std::int32_t>(memory, arrayPtr + mhw::kRiseMonoArrayLengthOffset);
    sub(QStringLiteral("Mono 数组 header=%1，长度(+0x1C)=%2")
            .arg(hexStr(arrayPtr),
                 length.ok ? fmtI32(length.value) : fmtFail(length.error)));
    const int slotCount = mhw::riseWirebugSlotsToRead(
        value, length.ok ? length.value : 0);
    sub(QStringLiteral("读者会读取的槽数（cap %1）= %2")
            .arg(static_cast<int>(mhw::kRiseWirebugSlotCap))
            .arg(slotCount));
    printMonoArrayCandidate(memory, arrayPtr, QStringLiteral("读者实际使用的地址"));
    if (dataTrace.altOk && dataTrace.altResult != arrayPtr)
        printMonoArrayCandidate(memory, dataTrace.altResult, QStringLiteral("[alt] 另一种编码给出的地址"));
    dumpHex(memory, arrayPtr + 0x10, 0x48, QStringLiteral("wirebug 数组头部原始字节"));

    for (int slot = 0; slot < mhw::kRiseWirebugSlotCap; ++slot) {
        const mhw::RiseWirebugType type = mhw::riseWirebugType(value, slot);
        line();
        sub(QStringLiteral("slot %1（类型 = %2%3）")
                .arg(slot)
                .arg(wirebugTypeName(type),
                     slot < slotCount ? QStringLiteral("，读者会读取") : QStringLiteral("，读者不读取")));
        if (type == mhw::RiseWirebugType::None) {
            sub(QStringLiteral("  → 类型判定为 None，读者跳过该槽（count 分区未覆盖）"));
            continue;
        }
        const auto elementAddress = mhw::riseWirebugElementAddress(arrayPtr, slot);
        if (!elementAddress) {
            sub(QStringLiteral("  元素地址计算失败"));
            continue;
        }
        const Field<std::uintptr_t> wirebug = readField<std::uintptr_t>(memory, *elementAddress);
        if (!wirebug.ok || !mhw::isSanePointer(wirebug.value)) {
            sub(QStringLiteral("  slot 指针无效：%1")
                    .arg(wirebug.ok ? hexStr(wirebug.value) : wirebug.error));
            continue;
        }
        sub(QStringLiteral("  slot 指针 = %1").arg(hexStr(wirebug.value)));
        const Field<mhw::MHRWirebugStructure> structure =
            readField<mhw::MHRWirebugStructure>(memory, wirebug.value);
        if (!structure.ok) {
            sub(QStringLiteral("  MHRWirebugStructure 读取失败：%1").arg(structure.error));
            continue;
        }
        sub(QStringLiteral("  ref=%1 unk0=%2 unk1=%3")
                .arg(hexStr(static_cast<std::uintptr_t>(structure.value.ref)),
                     fmtI32(structure.value.unk0), fmtI32(structure.value.unk1)));
        sub(QStringLiteral("  cooldown(+0x10)=%1 maxCooldown(+0x14)=%2 extraCooldown(+0x18)=%3")
                .arg(fmtF32(structure.value.cooldown),
                     fmtF32(structure.value.maxCooldown),
                     fmtF32(structure.value.extraCooldown)));
        sub(QStringLiteral("  转换为秒：cooldown=%1 maxCooldown=%2 extra=%3")
                .arg(QString::number(mhw::riseWirebugSeconds(structure.value.cooldown), 'f', 2),
                     QString::number(mhw::riseWirebugSeconds(structure.value.maxCooldown), 'f', 2),
                     QString::number(mhw::riseWirebugSeconds(structure.value.extraCooldown), 'f', 2)));
        if (mhw::isRiseWirebugTemporary(type)) {
            const QString key =
                mhw::riseWirebugExtraDataSource(type) == mhw::RiseWirebugExtraDataSource::Environment
                    ? QStringLiteral("WIREBUG_EXTRA_DATA_OFFSETS")
                    : QStringLiteral("WIREBUG_EXTRA_DATA_FROM_SKILL_OFFSETS");
            const ChainTrace extras = walkReadThenAdd(memory, absolute, map.offsets(key),
                                                      QStringLiteral("%1").arg(key));
            printTrace(extras);
            if (extras.ok) {
                const Field<mhw::MHRWirebugExtrasStructure> extra =
                    readField<mhw::MHRWirebugExtrasStructure>(memory, extras.result);
                if (extra.ok) {
                    sub(QStringLiteral("  extras timer = %1 → %2 秒")
                            .arg(fmtF32(extra.value.timer),
                                 QString::number(mhw::riseWirebugNonnegativeSeconds(extra.value.timer), 'f', 2)));
                } else {
                    sub(QStringLiteral("  extras 读取失败：%1").arg(extra.error));
                }
            }
        }
        dumpHex(memory, wirebug.value, sizeof(mhw::MHRWirebugStructure),
                QStringLiteral("MHRWirebugStructure 原始字节"));
    }
    line();
    sub(QStringLiteral("[reader] PlayerSnapshot.wirebugs = %1 个").arg(snapshot.player.wirebugs.size()));
    for (const mhw::WirebugSnapshot &wirebug : snapshot.player.wirebugs) {
        sub(QStringLiteral("  reader 翔虫 slot=%1 temporary=%2 cooldown=%3/%4 timer=%5/%6")
                .arg(wirebug.slot)
                .arg(fmtBool(wirebug.isTemporary))
                .arg(QString::number(wirebug.cooldown, 'f', 2),
                     QString::number(wirebug.maxCooldown, 'f', 2),
                     QString::number(wirebug.timer, 'f', 2),
                     QString::number(wirebug.maxTimer, 'f', 2)));
    }
}

// ---------------------------------------------------------------------------
// [abnormalities] — consumable buffs + debuffs (v0.8.4-r18)
// ---------------------------------------------------------------------------
//
// Raw dump behind PlayerSnapshot::abnormalities: both category blobs, the
// condition words, and every generated schema row (69 consumables + 28
// debuffs) with the value the reader reads and its verdict. The verdict comes
// from the production evaluator (riseEvaluateAbnormality), so this block and
// [reader] can never disagree — a mismatch means the snapshot was not rebuilt
// from this data. Read-only, like every other probe section.

QString fmtFlag(const mhw::RiseAbnormalitySchema &schema)
{
    if (schema.flagType == mhw::RiseAbnormalityFlagType::None)
        return QStringLiteral("None");
    const char *type = schema.flagType == mhw::RiseAbnormalityFlagType::RiseCommon
        ? "RiseCommon" : (schema.flagType == mhw::RiseAbnormalityFlagType::RiseDebuff
                              ? "RiseDebuff" : "RiseAction");
    return QStringLiteral("%1:%2(bit%3)").arg(QString::fromLatin1(type),
                                              QString::fromLatin1(schema.flagName))
        .arg(schema.flag ? QString::number(schema.flag) : QStringLiteral("0"));
}

// One generated table (consumables or debuffs), every row, unfiltered.
void probeAbnormalityTable(const mhw::ProcessMemory &memory,
                           const mhw::RiseAbnormalitySchema *schemas,
                           std::size_t count, const ChainTrace &blob,
                           const mhw::RiseAbnormalityConditions &conditions)
{
    for (std::size_t i = 0; i < count; ++i) {
        const mhw::RiseAbnormalitySchema &schema = schemas[i];
        const QString tag = QStringLiteral("%1 %2")
                                .arg(QString::fromLatin1(schema.id).leftJustified(24),
                                     QString::fromUtf8(schema.name));

        if (!blob.ok) {
            sub(QStringLiteral("%1 <区块未解析，未读取>").arg(tag));
            continue;
        }

        int subId = 0;
        bool subOk = true;
        if (schema.dependsOn != 0) {
            const Field<std::int32_t> subValue =
                readField<std::int32_t>(memory, blob.result
                                                    + static_cast<std::uintptr_t>(schema.dependsOn));
            subOk = subValue.ok;
            if (subValue.ok)
                subId = subValue.value;
        }
        const QString subText = subOk
            ? QStringLiteral("dep=+0x%1:%2/%3").arg(schema.dependsOn, 3, 16, QLatin1Char('0'))
                  .arg(subId).arg(schema.withValue)
            : QStringLiteral("dep=+0x%1:<读取失败>").arg(schema.dependsOn, 3, 16, QLatin1Char('0'));

        bool valueRead = true;
        float rawValue = 0.0F;
        QString valueText = QStringLiteral("infinite");
        if (!schema.isInfinite) {
            const Field<mhw::MHRAbnormalityValue> slot =
                readField<mhw::MHRAbnormalityValue>(memory, blob.result
                                                                + static_cast<std::uintptr_t>(schema.offset));
            valueRead = slot.ok;
            if (slot.ok) {
                rawValue = schema.isInteger ? static_cast<float>(slot.value.integer)
                                            : slot.value.timer;
                valueText = QStringLiteral("off=+0x%1 raw=%2")
                                .arg(schema.offset, 3, 16, QLatin1Char('0'))
                                .arg(schema.isInteger
                                         ? QString::number(slot.value.integer)
                                         : fmtF32(slot.value.timer));
            } else {
                valueText = QStringLiteral("off=+0x%1 <读取失败>")
                                .arg(schema.offset, 3, 16, QLatin1Char('0'));
            }
        }

        const mhw::RiseAbnormalityEvaluation evaluation = mhw::riseEvaluateAbnormality(
            schema, conditions, subId, rawValue, valueRead);
        sub(QStringLiteral("%1 %2 %3 %4 → %5")
                .arg(tag, subText, valueText, fmtFlag(schema),
                     evaluation.active
                         ? QStringLiteral("显示 %1").arg(mhw::riseAbnormalityTimerText(
                               evaluation.timer, schema.isInfinite, schema.isBuildup,
                               schema.isBuildup ? static_cast<float>(schema.maxBuildup)
                                                : schema.maxTimer))
                         : QStringLiteral("<不显示>")));
    }
}

// Whole section: both blobs, the condition words, and every schema row.
void probeAbnormalities(const mhw::ProcessMemory &memory, const mhw::AddressMap &map,
                        std::uintptr_t imageBase, const mhw::GameSnapshot &snapshot)
{
    section(QStringLiteral("abnormalities"));
    const std::uintptr_t absolute =
        imageBase + map.address(QStringLiteral("ABNORMALITIES_ADDRESS"));
    sub(QStringLiteral("ABNORMALITIES_ADDRESS = %1 → 绝对地址 %2")
            .arg(hexStr(map.address(QStringLiteral("ABNORMALITIES_ADDRESS"))), hexStr(absolute)));

    const ChainTrace consumable = walkReadThenAdd(
        memory, absolute, map.offsets(QStringLiteral("CONS_ABNORMALITIES_OFFSETS")),
        QStringLiteral("CONS_ABNORMALITIES_OFFSETS"));
    printTrace(consumable);
    const ChainTrace debuff = walkReadThenAdd(
        memory, absolute, map.offsets(QStringLiteral("DEBUFF_ABNORMALITIES_OFFSETS")),
        QStringLiteral("DEBUFF_ABNORMALITIES_OFFSETS"));
    printTrace(debuff);
    if (consumable.ok)
        dumpHex(memory, consumable.result, 0x30, QStringLiteral("消耗品区块头部原始字节"));
    if (debuff.ok)
        dumpHex(memory, debuff.result, 0x30, QStringLiteral("负面状态区块头部原始字节"));

    // Condition words — MHRPlayer.GetPlayerConditions / GetPlayerActionArgs.
    mhw::RiseAbnormalityConditions conditions;
    const std::uintptr_t playerData =
        imageBase + map.address(QStringLiteral("LOCAL_PLAYER_DATA_ADDRESS"));
    sub(QStringLiteral("LOCAL_PLAYER_DATA_ADDRESS = %1 → 绝对地址 %2")
            .arg(hexStr(map.address(QStringLiteral("LOCAL_PLAYER_DATA_ADDRESS"))),
                 hexStr(playerData)));
    const ChainTrace condition = walkReadThenAdd(
        memory, playerData, map.offsets(QStringLiteral("PLAYER_CONDITION_OFFSETS")),
        QStringLiteral("PLAYER_CONDITION_OFFSETS"));
    printTrace(condition);
    if (condition.ok) {
        const Field<std::uint64_t> common =
            readField<std::uint64_t>(memory, condition.result + mhw::kMhrCommonConditionsOffset);
        const Field<std::uint64_t> debuffWord =
            readField<std::uint64_t>(memory, condition.result + mhw::kMhrDebuffConditionsOffset);
        fieldLine(QStringLiteral("CommonConditions(+0x10)"),
                  condition.result + mhw::kMhrCommonConditionsOffset,
                  common.ok ? hexPadded(common.value, 16) : fmtFail(common.error));
        fieldLine(QStringLiteral("DebuffConditions(+0x38)"),
                  condition.result + mhw::kMhrDebuffConditionsOffset,
                  debuffWord.ok ? hexPadded(debuffWord.value, 16) : fmtFail(debuffWord.error));
        if (common.ok)
            conditions.common = common.value;
        if (debuffWord.ok)
            conditions.debuff = debuffWord.value;
    }
    const ChainTrace action = walkReadThenAdd(
        memory, playerData, map.offsets(QStringLiteral("PLAYER_ACTIONFLAG_OFFSETS")),
        QStringLiteral("PLAYER_ACTIONFLAG_OFFSETS"));
    printTrace(action);
    if (action.ok) {
        const Field<std::uint32_t> flags =
            readField<std::uint32_t>(memory, action.result + mhw::kMhrActionFlagOffset);
        fieldLine(QStringLiteral("ActionFlags(+0x20)"),
                  action.result + mhw::kMhrActionFlagOffset,
                  flags.ok ? hexPadded(flags.value, 8) : fmtFail(flags.error));
        if (flags.ok)
            conditions.action = flags.value;
    }

    std::size_t consumableCount = 0;
    std::size_t debuffCount = 0;
    const mhw::RiseAbnormalitySchema *consumableSchemas =
        mhw::riseConsumableAbnormalities(consumableCount);
    const mhw::RiseAbnormalitySchema *debuffSchemas =
        mhw::riseDebuffAbnormalities(debuffCount);

    line();
    sub(QStringLiteral("区块 = %1，消耗品定义 %2 行 / 负面状态定义 %3 行（生成表，HunterPie AbnormalityData.xml）")
            .arg(consumable.ok ? hexStr(consumable.result) : QStringLiteral("<未解析>"))
            .arg(consumableCount)
            .arg(debuffCount));
    probeAbnormalityTable(memory, consumableSchemas, consumableCount, consumable, conditions);
    line();
    sub(QStringLiteral("区块 = %1")
            .arg(debuff.ok ? hexStr(debuff.result) : QStringLiteral("<未解析>")));
    probeAbnormalityTable(memory, debuffSchemas, debuffCount, debuff, conditions);

    line();
    sub(QStringLiteral("[reader] PlayerSnapshot.abnormalities = %1 项")
            .arg(snapshot.player.abnormalities.size()));
    for (const mhw::PlayerAbnormalitySnapshot &a : snapshot.player.abnormalities) {
        sub(QStringLiteral("  %1 %2 %3 %4")
                .arg(a.id.leftJustified(24),
                     a.kind == mhw::AbnormalityKind::Debuff ? QStringLiteral("负") : QStringLiteral("正"),
                     a.name,
                     mhw::riseAbnormalityTimerText(a.timer, a.isInfinite, a.isBuildup, a.maxTimer)));
    }
}

// ---------------------------------------------------------------------------
// [reader] — the overlay's own interpretation (MhrReader::poll)
// ---------------------------------------------------------------------------

void printReaderSummary(const mhw::GameSnapshot &snapshot, const QString &mapPath)
{
    section(QStringLiteral("reader"));
    sub(QStringLiteral("map 文件 = %1").arg(mapPath));
    sub(QStringLiteral("attached=%1 pid=%2 imageBase=%3")
            .arg(fmtBool(snapshot.attached))
            .arg(snapshot.pid)
            .arg(hexStr(snapshot.imageBase)));
    sub(QStringLiteral("status = %1").arg(snapshot.status));
    sub(QStringLiteral("zone = %1（\"%2\"）")
            .arg(static_cast<int>(snapshot.zone))
            .arg(QString::fromUtf8(mhw::zoneName(snapshot.zone))));
    sub(QStringLiteral("monsters = %1 个").arg(snapshot.monsters.size()));
    sub(QStringLiteral("player.valid=%1 name=\"%2\" MR=%3 HR=%4 weaponId=%5")
            .arg(fmtBool(snapshot.player.valid), snapshot.player.name)
            .arg(snapshot.player.masterRank)
            .arg(snapshot.player.highRank)
            .arg(snapshot.player.weaponId));
    sub(QStringLiteral("player HP=%1/%2 ST=%3/%4")
            .arg(QString::number(snapshot.player.health, 'f', 1),
                 QString::number(snapshot.player.maxHealth, 'f', 1),
                 QString::number(snapshot.player.stamina, 'f', 1),
                 QString::number(snapshot.player.maxStamina, 'f', 1)));
    sub(QStringLiteral("sharpness level=%1 hits=%2 maxHits=%3 valid=%4")
            .arg(snapshot.player.sharpness.level)
            .arg(snapshot.player.sharpness.currentHits)
            .arg(snapshot.player.sharpness.maxHits)
            .arg(fmtBool(snapshot.player.sharpness.valid)));
    sub(QStringLiteral("quest active=%1 id=%2 state=%3 category=%4 stars=%5 elapsed=%6 max=%7")
            .arg(fmtBool(snapshot.quest.active))
            .arg(snapshot.quest.id)
            .arg(snapshot.quest.state)
            .arg(snapshot.quest.category)
            .arg(snapshot.quest.stars)
            .arg(QString::number(snapshot.quest.elapsedSeconds, 'f', 1),
                 QString::number(snapshot.quest.maxTimerSeconds, 'f', 1)));
}

// ---------------------------------------------------------------------------
// map key sanity
// ---------------------------------------------------------------------------

struct MapKey {
    const char *name;
    bool isOffset;
};

void checkMapKeys(const mhw::AddressMap &map)
{
    static const MapKey kKeys[] = {
        {"STAGE_ADDRESS", false},
        {"STAGE_OFFSETS", true},
        {"UI_ADDRESS", false},
        {"PLAYER_HUD_OFFSETS", true},
        {"GEAR_ADDRESS", false},
        {"PETALACES_ARRAY_OFFSETS", true},
        {"PLAYER_GEAR_ADDRESS", false},
        {"SELECTED_PETALACE_OFFSETS", true},
        {"CHARACTER_ADDRESS", false},
        {"CHARACTER_OFFSETS", true},
        {"SAVE_ADDRESS", false},
        {"SAVE_OFFSETS", true},
        {"WEAPON_ADDRESS", false},
        {"WEAPON_OFFSETS", true},
        {"SHARPNESS_ADDRESS", false},
        {"SHARPNESS_OFFSETS", true},
        {"SHARPNESS_ARRAY_OFFSETS", true},
        {"QUEST_ADDRESS", false},
        {"QUEST_OFFSETS", true},
        {"MONSTERS_ADDRESS", false},
        {"MONSTER_LIST_OFFSETS", true},
        {"MONSTER_HEALTH_COMPONENT_OFFSETS", true},
        {"MONSTER_HEALTH_COMPONENT_ENCODED_OFFSETS", true},
        {"ABNORMALITIES_ADDRESS", false},
        {"CONS_ABNORMALITIES_OFFSETS", true},
        {"DEBUFF_ABNORMALITIES_OFFSETS", true},
        {"LOCAL_PLAYER_DATA_ADDRESS", false},
        {"PLAYER_CONDITION_OFFSETS", true},
        {"PLAYER_ACTIONFLAG_OFFSETS", true},
        {"WIREBUG_COUNT_OFFSETS", true},
        {"WIREBUG_DATA_OFFSETS", true},
        {"WIREBUG_EXTRA_DATA_OFFSETS", true},
        {"WIREBUG_EXTRA_DATA_FROM_SKILL_OFFSETS", true},
    };
    QStringList missing;
    for (const MapKey &key : kKeys) {
        const QString name = QString::fromLatin1(key.name);
        const bool present = key.isOffset ? map.hasOffsets(name) : map.hasAddress(name);
        if (!present)
            missing << name;
    }
    if (missing.isEmpty()) {
        sub(QStringLiteral("所需键 %1 个全部存在 ✓").arg(sizeof(kKeys) / sizeof(kKeys[0])));
    } else {
        sub(QStringLiteral("缺少键（相关区块会打印失败原因）：%1").arg(missing.join(QStringLiteral(", "))));
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rise-probe"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.8.4-r18"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "MHRise 16.0.2.0 原始内存探针（只读）：stage / hud / petalace / player / quest / "
        "怪物 / 翔虫 / 状态（消耗品 buff + 负面状态）原始 dump"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption mapOption(
        {QStringLiteral("m"), QStringLiteral("map")},
        QStringLiteral("地址表 .map 路径（默认编译期内置的 Rise 16.0.2.0 地址表）"),
        QStringLiteral("path"), QString::fromLatin1(MHR_DEFAULT_MAP));
    parser.addOption(mapOption);
    parser.process(app);

    const QString mapPath = parser.value(mapOption);
    line(QStringLiteral("rise-probe — Monster Hunter Rise 实测探针（只读）"));
    line(QStringLiteral("map 文件: %1").arg(mapPath));

    // [process]
    section(QStringLiteral("process"));
    const auto pid = mhw::MhrReader::findRisePid();
    if (!pid) {
        line(QStringLiteral("未找到游戏进程：/proc/*/maps 中没有任何进程映射 monsterhunterrise.exe。"));
        line(QStringLiteral("请先启动 Monster Hunter Rise（Proton）并进入游戏，再运行本工具。"));
        out().flush();
        return 1;
    }
    sub(QStringLiteral("pid = %1").arg(*pid));
    QFile mapsFile(QStringLiteral("/proc/%1/maps").arg(*pid));
    if (mapsFile.open(QIODevice::ReadOnly)) {
        const QByteArray maps = mapsFile.readAll();
        int rows = 0;
        for (const QByteArray &row : maps.split('\n')) {
            if (row.toLower().contains("monsterhunterrise.exe"))
                ++rows;
        }
        sub(QStringLiteral("exe = monsterhunterrise.exe（/proc/%1/maps 命中 %2 行）").arg(*pid).arg(rows));
    } else {
        sub(QStringLiteral("警告：无法读取 /proc/%1/maps（%2）").arg(*pid).arg(mapsFile.errorString()));
    }

    mhw::ProcessMemory memory;
    QString attachError;
    if (!memory.attach(*pid, &attachError)) {
        line(QStringLiteral("附加失败：%1").arg(attachError));
        out().flush();
        return 2;
    }
    QString baseError;
    const std::uintptr_t imageBase =
        memory.imageBase(&baseError, QStringLiteral("monsterhunterrise.exe"));
    if (imageBase == 0) {
        line(QStringLiteral("无法解析镜像基址：%1").arg(baseError));
        out().flush();
        return 3;
    }
    sub(QStringLiteral("imageBase = %1").arg(hexStr(imageBase)));

    mhw::AddressMap map;
    QString mapError;
    if (!QFileInfo::exists(mapPath)) {
        line(QStringLiteral("地址表文件不存在：%1（用 --map 指定路径，见 --help）").arg(mapPath));
        out().flush();
        return 4;
    }
    if (!map.load(mapPath, &mapError)) {
        line(QStringLiteral("地址表解析失败：%1").arg(mapError));
        out().flush();
        return 4;
    }
    sub(QStringLiteral("地址表加载成功"));
    checkMapKeys(map);

    // Permission pre-flight: a refused read at the image base explains every
    // "Operation not permitted" below, so say what to do about it up front.
    bool permissionRefused = false;
    QString permissionError;
    std::uint8_t probeByte = 0;
    if (!memory.readBytes(imageBase, &probeByte, sizeof(probeByte), &permissionError)
        && (permissionError.contains(QStringLiteral("permitted"), Qt::CaseInsensitive)
            || permissionError.endsWith(QStringLiteral("(1)")))) {
        permissionRefused = true;
        sub(QStringLiteral("[警告] 读内存被拒：%1").arg(permissionError));
        sub(QStringLiteral("        本机 kernel.yama.ptrace_scope=1，非父进程读取被拒绝。请先放宽权限再重跑："));
        sub(QStringLiteral("          sudo setcap cap_sys_ptrace+ep %1   （每次重新编译后需重做）")
                .arg(QCoreApplication::applicationFilePath()));
        sub(QStringLiteral("          sudo sysctl kernel.yama.ptrace_scope=0                 （重启后失效）"));
        sub(QStringLiteral("        放宽之前，下面每个区块都会打印同样的 Operation not permitted。"));
    }

    // Bracket the stage and abnormality raw reads with the same MhrReader
    // instance. The old order scanned unrelated live regions first, so it
    // could not separate a reader-path failure from a transition during that
    // much larger interval.
    mhw::MhrReader reader(mapPath);
    const mhw::GameSnapshot snapshotBefore = reader.poll();
    const StageProbe stage = probeStage(memory, map, imageBase, snapshotBefore);
    probeAbnormalities(memory, map, imageBase, snapshotBefore);
    const mhw::GameSnapshot snapshotAfter = reader.poll();

    line();
    sub(QStringLiteral("[reader bracket] abnormalities before=%1 after=%2; "
                       "zone before=%3 after=%4; pid before=%5 after=%6; "
                       "base before=%7 after=%8; attached before=%9 after=%10")
            .arg(snapshotBefore.player.abnormalities.size())
            .arg(snapshotAfter.player.abnormalities.size())
            .arg(static_cast<int>(snapshotBefore.zone))
            .arg(static_cast<int>(snapshotAfter.zone))
            .arg(snapshotBefore.pid)
            .arg(snapshotAfter.pid)
            .arg(hexStr(snapshotBefore.imageBase))
            .arg(hexStr(snapshotAfter.imageBase))
            .arg(fmtBool(snapshotBefore.attached))
            .arg(fmtBool(snapshotAfter.attached)));

    // The remaining sections retain the comprehensive dump. They use the
    // post-bracket snapshot so labels describe their nearby live reads.
    probeHud(memory, map, imageBase, snapshotAfter);
    probePetalace(memory, map, imageBase);
    probePlayer(memory, map, imageBase, snapshotAfter);
    probeQuest(memory, map, imageBase, snapshotAfter);
    probeMonsters(memory, map, imageBase, stage, snapshotAfter);
    probeWirebugs(memory, map, imageBase, snapshotAfter);
    printReaderSummary(snapshotAfter, mapPath);

    line();
    if (permissionRefused) {
        line(QStringLiteral("采集失败：找到了游戏进程但读内存被拒（见上方 [警告]），本输出不代表真实游戏数据。"));
        out().flush();
        return 5;
    }
    line(QStringLiteral("采集完成（只读，未修改游戏内存）。请把以上完整输出贴回对话。"));
    out().flush();
    return 0;
}
