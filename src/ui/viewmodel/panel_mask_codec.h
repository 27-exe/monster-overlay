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
// Those rows are a USER CONTRACT, not an internal detail: the overlay's
// `locale_sync.h` polls the same file, users hand-edit it, and an older
// three-row config (pre-pets) must keep loading. This header exists so
// the contract is pinned down by assertions instead of by a QA click.
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

    // Plain field setters. The legacy three-row migration is applied
    // exactly once, in parsePanelMasks — putting it here as well would
    // OR `OtherMembers` in twice, which happens to be idempotent but
    // hides the fact that the rule has two owners.
    [[nodiscard]] static PanelMaskSet make(
        uint32_t player, uint32_t monster, uint32_t damage, uint32_t pets,
        bool playerValid, bool monsterValid, bool damageValid,
        bool petsValid, bool petsLineSeen)
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
        return s;
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
        }
    }

    if (!petsLineSeen && damageValid && damageMask > 0u)
        damageMask |= static_cast<uint32_t>(mhw::DamageSection::OtherMembers);

    return PanelMaskSet::make(
        playerMask, monsterMask, damageMask, petsMask, playerValid,
        monsterValid, damageValid, petsValid, petsLineSeen);
}

// ---- serialize ---------------------------------------------------------

// Build the exact bytes the console writes.
//
// `existingText` is the conf's current content, ALREADY READ the way the
// console reads it (see parsePanelMasks), or an empty string when the
// file does not exist yet — the original `QFileInfo::exists()` guard
// lives in ControlPanel, which is the part that owns the filesystem.
//
// The layout is fixed and user-visible: the four canonical rows first,
// in player/monster/damage/pets order, then every non-mask line of the
// previous file in its original relative order (`locale=`, comments,
// blank lines, unknown and future keys). Each non-mask line is copied
// VERBATIM — leading and trailing whitespace included — because
// preserving a user's hand-edited spacing is the point of classifying
// rather than parsing those lines.
//
// The scan is a QTextStream loop, not `split(QLatin1Char('\n'))`: a
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
        if (classifyPanelMaskLine(line) == PanelMaskLineKind::Preserved)
            preserved.append(line);
    }

    QByteArray data;
    data += "player=" + QString::number(playerMask, 16).toUtf8() + '\n';
    data += "monster=" + QString::number(monsterMask, 16).toUtf8() + '\n';
    data += "damage=" + QString::number(damageMask, 16).toUtf8() + '\n';
    data += "pets=" + QString::number(petsMask, 16).toUtf8() + '\n';
    for (const QString &line : preserved)
        data += line.toUtf8() + '\n';
    return data;
}

} // namespace mhw
