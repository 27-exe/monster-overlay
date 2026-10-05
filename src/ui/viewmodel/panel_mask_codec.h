// SPDX-License-Identifier: Apache-2.0
// PanelMaskCodec — the pure text half of the control console's
// `monster-overlay.conf` mask persistence, lifted out of ControlPanel so
// the user-visible format contract can be exercised by a plain logic
// test.
//
// The console reads and writes four mask rows in its config file:
//
//     player=<hex32>
//     monster=<hex32>
//     damage=<hex32>
//     pets=<hex32>
//
// and, since v0.12.1, a fifth row that says which SCHEMA those four
// values are written in:
//
//     maskVersion=<int>
//
// Those rows are a USER CONTRACT, not an internal detail: the overlay's
// `locale_sync.h` polls the same file, users hand-edit it, and an older
// three-row config (pre-pets) must keep loading. This header exists so
// the contract is pinned down by assertions instead of by a QA click.
//
// WHY THE VERSION ROW EXISTS. `PlayerSection::Sharpness` (1u << 8) was
// added to a `player=` field that had only ever needed bits 0-7, so
// every mask written before it stores bit 8 as ZERO — which is
// byte-identical to the value a user who deliberately turns sharpness
// OFF on the new build stores. One value, two meanings: a read-time
// heuristic cannot separate them (see panel_sections.h's
// migrateOldSharpnessBit, which has to guess and therefore resurrects
// a gauge the user switched off).
//
// The version row removes the ambiguity by making the FILE say which
// schema it was written in. It is therefore NOT a migration in this
// header — this codec only turns text into values and back. What it
// does own is the third fact a caller needs to act: WHICH SCHEMA the
// text declared, reported as `PanelMaskSet::schemaVersion`
// (0 = no maskVersion= row at all, i.e. a pre-v0.12.1 file).
//
// THE ONE-TIME SHAPE this enables, and why the version is written on
// EVERY save rather than only when something changed: the first reader
// of an unmarked file migrates in memory and then writes the file back
// with `maskVersion=1` on the very next save. From that moment the
// same `ff` on disk means "everything off" and no reader migrates it
// again — the ambiguity is spent, not re-litigated at every startup.
// Parsing the same unmarked text twice reports version 0 both times,
// which is the assertion that pins it: reading must never write.
//
// Hard rule, same as ConsoleLayoutStore / DamageViewModel /
// MonsterViewModel / RiseReframeworkStatus / console_text_helpers: this
// header and its users carry NO widget dependency — only QtCore
// (QString / QStringList / QByteArray / QTextStream) plus the pure data
// headers that define the DamageSection bit table. There is no QWidget
// here, not forward-declared and not named; a name search for it over
// this file comes back empty by construction rather than by manual
// discipline.
//
// This is a move, not a rewrite. The parse and serialize bodies below
// reproduce the logic from `ControlPanel::loadMaskFromDisk` and
// `ControlPanel::saveMaskToDisk` verbatim: the same prefix tests in the
// same order, the same `toUInt(&ok, 16)` accept/reject rule, the same
// legacy three-row migration, the same duplicate-row convergence, the
// same line classification and the same emission order. What the widgets
// do with a parsed result, and everything that touches the filesystem or
// warns on a failed open, stays in ControlPanel.

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "ui/panel_sections.h"

namespace mhw {

// ---- the mask schema version -------------------------------------------
//
// 0 = no version row (every file written before this constant existed).
// 1 = `maskVersion=1`: the four masks were written by a build whose
// PlayerSection table includes Sharpness, so a zero bit 8 is a decision.
//
// Bump this ONLY when a stored value's meaning changes without its text
// changing. Adding a section bit is exactly that event; adding a whole
// panel with its own row is not (a new row parses as absent and keeps
// the caller's default, which is already handled).
//
// Declared BEFORE PanelMaskSet because needsSchemaMigration() reads it
// from inside the struct, and a namespace-scope constant that follows
// its user is not in scope there.
inline constexpr int kPanelMaskSchemaUnmarked = 0;
inline constexpr int kPanelMaskSchemaCurrent  = 1;

// The row spelling. One literal in the codec's comments and one here, so
// a rename has to touch both.
inline constexpr char kPanelMaskVersionKey[] = "maskVersion";

// One parsed section-mask set, plus the two facts that decide how the
// caller applies it: whether a mask line was present at all, and whether
// a `pets=` line was present (which is what distinguishes a pre-pets
// three-row config from a modern four-row one).
struct PanelMaskSet {
    uint32_t player{0};
    uint32_t monster{0};
    uint32_t damage{0};
    uint32_t pets{0};

    // A field is false when its row was absent or malformed — the caller
    // keeps whatever default it already had for that panel.
    bool playerValid{false};
    bool monsterValid{false};
    bool damageValid{false};
    bool petsValid{false};

    // True once a `pets=` row is seen, even if its value was malformed.
    bool petsLineSeen{false};

    // THE MASK SCHEMA this text declared.
    //
    //   0 — no `maskVersion=` row. A pre-v0.12.1 file: its bits mean
    //       whatever the build that wrote them understood, and the
    //       `player=` value's bit 8 is unconstrained by intent (an old
    //       build could not set it; a new user who turned sharpness off
    //       also stores 0). This is the value that makes a caller
    //       migrate, ONCE.
    //   1 — this build wrote it. Every `maskVersion=` writer emits
    //       kPanelMaskSchemaVersion (below), so bit 8 means exactly what
    //       the checkboxes said.
    //
    // A future schema (2, 3, ...) is reported as its literal number, NOT
    // clamped: a caller that does not recognise it must treat the values
    // as opaque rather than guess. The migration is keyed on == 0.
    //
    // Deliberately NOT named `version`: this is the schema of the stored
    // VALUES, which is a different axis from the file's feature set and
    // from MONSTER_VERSION.
    int schemaVersion{0};

    // Plain field setters. The legacy three-row migration is applied
    // exactly once, in parsePanelMasks — putting it here as well would
    // OR `OtherMembers` in twice, which happens to be idempotent but
    // hides the fact that the rule has two owners.
    //
    // schemaVersion is a parameter rather than a setter call for the
    // same reason: it is decided by the scan, so it travels with the
    // result that carries it.
    [[nodiscard]] static PanelMaskSet make(
        uint32_t player, uint32_t monster, uint32_t damage, uint32_t pets,
        bool playerValid, bool monsterValid, bool damageValid,
        bool petsValid, bool petsLineSeen, int schemaVersion = 0)
    {
        PanelMaskSet s;
        s.player = player;
        s.monster = monster;
        s.damage = damage;
        s.pets = pets;
        s.playerValid = playerValid;
        s.monsterValid = monsterValid;
        s.damageValid = damageValid;
        s.petsValid = petsValid;
        s.petsLineSeen = petsLineSeen;
        s.schemaVersion = schemaVersion;
        return s;
    }

    // True when this text carries no schema marker, i.e. when the one-time
    // bit migration still owes the user a write. Kept as a member because
    // the two call sites (the console's load, the overlay's startup) must
    // not each restate the == 0 test — that is how one of them drifts to
    // `<= 0` or `!= 1` later.
    [[nodiscard]] bool needsSchemaMigration() const noexcept
    {
        return schemaVersion == kPanelMaskSchemaUnmarked;
    }
};

// ---- parse -------------------------------------------------------------

// Classify one raw line as the line categories the codec must tell
// apart. Returned rather than exposed as bools because the save path's
// line classification and the load path's dispatch both answer the same
// question, and duplicating the answer in two spellings is how the two
// directions drift apart.
enum class PanelMaskLineKind {
    // One of the four canonical mask rows: player=/monster=/damage=/pets=.
    Canonical,
    // Anything else that must survive a rewrite verbatim: `locale=`,
    // comments, blank lines, unknown or future keys.
    Preserved,
};

// A mask row is a line whose KEY is one of the four canonical spellings,
// judged on the TRIMMED line exactly as the console did. The consequence,
// and the reason it is spelled out: an indented `  player=1` IS a mask row
// — it is removed and rewritten, not preserved as some unknown line. Both
// directions agree, so a hand-indented row converges instead of lingering
// as a second, stale copy. This is the original behaviour, quirk and all,
// because the file format is a contract.
[[nodiscard]] inline PanelMaskLineKind classifyPanelMaskLine(
    const QString &line)
{
    const QString trimmed = line.trimmed();
    const bool canonical = trimmed.startsWith(QLatin1String("player="))
                           || trimmed.startsWith(QLatin1String("monster="))
                           || trimmed.startsWith(QLatin1String("damage="))
                           || trimmed.startsWith(QLatin1String("pets="));
    return canonical ? PanelMaskLineKind::Canonical
                     : PanelMaskLineKind::Preserved;
}

// Parse one already-isolated `KEY=VALUE` body into a mask. Accepts
// exactly what `QString::toUInt(&ok, 16)` accepts (which, worth pinning
// down, rules out `100000000` and `-1` but admits `0x1f` and a leading
// `+`) and, on rejection, leaves the caller's fields untouched: a
// malformed mask line must never make the console unstartable, and must
// never silently become 0 either.
//
// Not [[nodiscard]]: the call sites discard the result on purpose, the
// way the original lambda's callers did, because `valid` is the
// out-parameter that carries the verdict.
inline void parsePanelMaskValue(const QString &text,
                                 uint32_t &mask, bool &valid)
{
    bool ok = false;
    const uint parsed = text.toUInt(&ok, 16);
    if (ok) {
        mask = parsed;
        valid = true;
    }
}

// Parse a whole conf text into the four masks. `text` must be the conf
// file's content ALREADY read the way the console reads it — a QString
// produced from a `QIODevice::ReadOnly | QIODevice::Text` read, so lone
// carriage returns are already gone. `loadMaskFromDisk()` in
// control_panel.cpp does the file I/O and hands the content here.
//
// Splitting is deliberately spelled as a `QTextStream` scan rather than
// a `split(QLatin1Char('\n'))`, because those two are NOT equivalent on
// every input and the format contract includes their exact behaviour (a
// lone `\r` is a separator in the console's read but not in split()). A
// QTextStream over a plain QString reads it without any text-mode
// translation — verified against the real file path in the unit tests.
//
// Duplicate mask rows converge: the last valid one wins, matching the
// wheel that rewrites the file with a single row per key. Missing or
// malformed rows leave `valid` false so the caller's defaults hold.
//
// `maskVersion=` is read here, not inferred. It is the ONE row this
// codec neither masks nor migrates on: it is metadata about the four
// values, and reporting it verbatim is what lets the caller run its one-
// time migration and then never again. Two rules make that auditable:
//
//   * an ABSENT row reports kPanelMaskSchemaUnmarked (0) — this is the
//     pre-v0.12.1 case, and the only one that owes a write;
//   * a MALFORMED row (`maskVersion=zz`, `maskVersion=`) also reports
//     0, because "unreadable marker" is not "newer than me" and the
//     migration must stay reachable rather than silently switching off;
//   * a HIGHER number than this build knows is passed through literally
//     — the caller sees 2 and treats the values as opaque, which is the
//     only honest answer available.
[[nodiscard]] inline PanelMaskSet parsePanelMasks(const QString &text)
{
    uint32_t playerMask = 0;
    uint32_t monsterMask = 0;
    uint32_t damageMask = 0;
    uint32_t petsMask = 0;
    bool playerValid = false;
    bool monsterValid = false;
    bool damageValid = false;
    bool petsValid = false;
    bool petsLineSeen = false;
    int schemaVersion = kPanelMaskSchemaUnmarked;

    // QTextStream over a plain QString* performs NO text-mode
    // translation, so readLine() here reproduces the line list the
    // original file read produced. (QTextStream has no const QString&
    // overload, hence the non-const take.)
    QString scan = text;
    QTextStream in(&scan);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1String("player="))) {
            parsePanelMaskValue(line.mid(7), playerMask, playerValid);
        } else if (line.startsWith(QLatin1String("monster="))) {
            parsePanelMaskValue(line.mid(8), monsterMask, monsterValid);
        } else if (line.startsWith(QLatin1String("damage="))) {
            parsePanelMaskValue(line.mid(7), damageMask, damageValid);
        } else if (line.startsWith(QLatin1String("pets="))) {
            petsLineSeen = true;
            parsePanelMaskValue(line.mid(5), petsMask, petsValid);
        } else if (line.startsWith(QLatin1String("maskVersion="))) {
            // Trimmed line, same as the mask rows above: an indented
            // `  maskVersion=1` is still this row. A value that is not
            // a non-negative int leaves schemaVersion at "unmarked",
            // so a truncated or hand-mangled marker degrades to the
            // pre-v0.12.1 path instead of to a frozen migration.
            bool ok = false;
            const int v = line.mid(12).toInt(&ok);
            if (ok && v > 0)
                schemaVersion = v;
        }
    }

    if (!petsLineSeen && damageValid && damageMask > 0u)
        damageMask |= static_cast<uint32_t>(mhw::DamageSection::OtherMembers);

    return PanelMaskSet::make(
        playerMask, monsterMask, damageMask, petsMask, playerValid,
        monsterValid, damageValid, petsValid, petsLineSeen, schemaVersion);
}

// ---- serialize ---------------------------------------------------------

// Build the exact bytes the console writes.
//
// `existingText` is the conf's current content, ALREADY READ the way the
// console reads it (see parsePanelMasks), or an empty string when the
// file does not exist yet — the original `QFileInfo::exists()` guard
// lives in ControlPanel, which is the part that owns the filesystem.
//
// The layout is fixed and user-visible: the FOUR canonical mask rows
// first, in player/monster/damage/pets order, then the schema marker
// `maskVersion=1`, then every non-mask line of the previous file in its
// original relative order (`locale=`, comments, blank lines, unknown and
// future keys). Each non-mask line is copied VERBATIM — leading and
// trailing whitespace included — because preserving a user's hand-edited
// spacing is the point of classifying rather than parsing those lines.
//
// THE MARKER IS EMITTED UNCONDITIONALLY, on every save, even when the
// previous file already had it and even when nothing migrated. That is
// what makes the one-time property hold: the write that follows a
// migration is an ordinary save, so there is exactly one code path to
// audit instead of "migrate, then hope the next save happens". A file
// with no marker is by construction one this build has never written.
//
// `maskVersion=` is a PRESERVED-classified row: the save replaces the
// one it finds (so duplicates cannot accumulate) and re-emits it at the
// version this build writes. It therefore does NOT fall through to the
// verbatim-preserved block below, which would keep a stale or
// hand-edited copy alive.
//
// The scan is a QTextStream loop, not `split(QLatin1String('\\n'))`: a
// trailing newline makes split() yield one extra empty element (an
// artifact the original `while (!in.atEnd())` never produced), and that
// phantom line would land in the user's conf on every save.
//
// CR was already stripped by the text-mode read, so a CRLF-authored
// conf is normalized to LF by this writer. That is the original
// behaviour: the load read it text-mode and the save re-emitted LF.
[[nodiscard]] inline QByteArray serializePanelMasks(
    uint32_t playerMask, uint32_t monsterMask, uint32_t damageMask,
    uint32_t petsMask, const QString &existingText)
{
    QStringList preserved;
    QString scan = existingText;
    QTextStream in(&scan);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        // The marker is dropped here and re-emitted by hand below, so it
        // never appears twice and never appears at an arbitrary position
        // in the user's comment block.
        if (line.trimmed().startsWith(QLatin1String("maskVersion=")))
            continue;
        if (classifyPanelMaskLine(line) == PanelMaskLineKind::Preserved)
            preserved.append(line);
    }

    QByteArray data;
    data += "player=" + QString::number(playerMask, 16).toUtf8() + '\n';
    data += "monster=" + QString::number(monsterMask, 16).toUtf8() + '\n';
    data += "damage=" + QString::number(damageMask, 16).toUtf8() + '\n';
    data += "pets=" + QString::number(petsMask, 16).toUtf8() + '\n';
    data += QByteArray(kPanelMaskVersionKey) + '='
            + QByteArray::number(kPanelMaskSchemaCurrent) + '\n';
    for (const QString &line : preserved)
        data += line.toUtf8() + '\n';
    return data;
}

} // namespace mhw
