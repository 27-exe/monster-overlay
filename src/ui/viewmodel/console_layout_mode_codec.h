// SPDX-License-Identifier: Apache-2.0
#pragma once

// ConsoleLayoutModeCodec — the pure text half of the console's `layout=`
// row, lifted out of ControlPanel so the user-visible format contract,
// the priority chain and the argv spelling can all be exercised from a
// plain logic test.
//
// THE CONTRACT, in one line: one new row in the SAME file the console
// already owns, one new flag in the argv the console already builds, and
// a new UI control whose entire semantics are "remember this choice and
// restart the child".
//
//     ~/.config/monster-overlay/monster-overlay.conf
//         layout=single          (the default; the row's ABSENCE is also
//                                single — see the version note below)
//
//     monster-overlay --layout=split
//
// WHY A ROW AND NOT A QSettings KEY
// ---------------------------------
// The console already writes a hand-rolled `key=value` file because that
// file is a cross-process contract with a poller: the OVERLAY reads it
// (main.cpp's mask resolution) and a Lua side reads it via locale_sync.
// QSettings under `monster-control.conf` is a different file that only
// the console ever opens. Putting the layout choice there would create
// a second, weaker store for the same user-facing choice, and the row
// would then have to be reconciled with the file the overlay actually
// reads. One file, one writer, one reader.
//
// WHY THE ABSENCE OF THE ROW IS single, FOR GOOD
// ----------------------------------------------
// `--layout` is read ONCE at overlay startup (main.cpp's
// resolveStartupLayoutMode), so the mode is frozen for the life of the
// child. The default must therefore be the one that cannot surprise
// anybody: v0.11 behaviour, one big window, and the 10 committed pixel
// baselines byte-identical. EVERY resolver path below returns Single
// unless something usable explicitly says Split — a missing row, a
// malformed row, an unrecognised spelling and a hand-edited "off" all
// resolve Single, which is also what PlayerLayoutSource::Default means
// in the shared chain.
//
// WHAT THIS FILE DELIBERATELY DOES NOT DO
// ---------------------------------------
// It does not decide precedence. mhw::resolveStartupLayoutMode()
// (ui/panel_sections.h) already owns the `cli > conf > default` chain
// and is used by the overlay itself; the console must go through the
// SAME function rather than re-implement "conf beats the UI, which
// beats single". Two chains would mean two places to fix a precedence
// bug, and the moment they disagree the user's saved choice is
// silently dropped. So this header only does the parsing and the
// spelling: it turns text into the one fact the shared resolver needs
// (a PlayerLayoutSource) and back.
//
// Hard rule, same as panel_mask_codec / ConsoleLayoutStore /
// DamageViewModel: this header and its users carry NO widget dependency
// — only QtCore (QString / QStringList / QByteArray / QTextStream)
// plus the pure data headers that define the layout enum. There is no
// QWidget here, not forward-declared and not named; a name search for it
// over this file comes back empty by construction rather than by manual
// discipline.

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "ui/panel_sections.h"

namespace mhw {

// The row spelling. One literal in this header's comments and one here,
// so a rename has to touch both.
inline constexpr char kConsoleLayoutRowKey[] = "layout";

// The one conf value that selects the split. Everything else — absent,
// malformed, "single", "off", "0" — resolves Single, because a value
// that does not SAY split must never change a user's window topology.
//
// Reuses PlayerSection's own accepted spellings (parsePlayerLayoutMode)
// rather than a second table here, so "SPLIT" and " split " behave the
// same way in the conf and on the command line.
[[nodiscard]] inline bool parseConsoleLayoutValue(
    const QString &text, PlayerSection::PlayerLayoutMode &out) noexcept
{
    return parsePlayerLayoutMode(text, out);
}

// One parsed `layout=` row, reduced to the single fact
// resolveStartupLayoutMode() needs.
//
// `source` is Conf when and only when the row was present AND parsed as
// Split. That is not a loss of generality: the resolver's conf arm
// (PlayerLayoutSource::Conf) is a yes/no question ("did anything usable
// come out of it"), and the only usable value a `layout=` row can carry
// that is worth handing upstream is Split — Single is what the resolver
// already returns for every other source, so distinguishing
// "row said single" from "no row" would buy nothing and would need the
// Default arm to grow a case it cannot act on.
struct ConsoleLayoutConfRow {
    PlayerLayoutSource source{PlayerLayoutSource::Default};
};

// Read the `layout=` row out of an already-read conf text.
//
// The scan is a QTextStream loop, not split('\n'): the same reason
// serializePanelMasks gives — a trailing newline would yield one phantom
// empty element, and that phantom must never be classified as a row.
[[nodiscard]] inline ConsoleLayoutConfRow parseConsoleLayoutRow(
    const QString &text) noexcept
{
    // Non-const take: QTextStream has no const QString& overload. A plain
    // QString performs NO text-mode translation (same as the conf file
    // read above), so this reproduces the line list the original read
    // produced.
    QString scan = text;
    QTextStream in(&scan);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.startsWith(QLatin1String(
                QByteArray(kConsoleLayoutRowKey) + '=')))
            continue;
        PlayerSection::PlayerLayoutMode parsed{};
        if (parseConsoleLayoutValue(
                line.mid(static_cast<int>(sizeof(kConsoleLayoutRowKey))),
                parsed)
            && parsed == PlayerSection::PlayerLayoutMode::Split) {
            return ConsoleLayoutConfRow{PlayerLayoutSource::Conf};
        }
    }
    return {};
}

// The exact bytes the console writes for this row, or an empty QByteArray
// when the mode is Single — because Single is the DEFAULT and a conf row
// that states the default is a row that can only mislead: it makes a
// hand-edited "single" look like a deliberate choice that must be
// preserved, and it puts the row in the file of every user who ever
// looked at the combo box.
//
// Consequence, and it is intended: choosing single in the UI REMOVES the
// row on the next save, so the "old conf has no key" case is not a
// migration path nobody exercises — it is the state every non-split
// user lives in.
[[nodiscard]] inline QByteArray serializeConsoleLayoutRow(
    PlayerSection::PlayerLayoutMode mode) noexcept
{
    if (mode != PlayerSection::PlayerLayoutMode::Split)
        return {};
    return QByteArray(kConsoleLayoutRowKey) + '='
           + QByteArray("split") + '\n';
}

// The console's own UI choice -> the flag the child is given.
//
// The console NEVER passes an explicit `--layout=single`: single is the
// resolver's default (PlayerLayoutSource::Default), so omitting the flag
// produces the identical mode while leaving the CLI open for the user's
// own --layout override to keep working. Conversely an explicit
// --layout=single would outrank a future conf row and silently discard
// it — the exact failure mode "conf > cli-explicit > default" is shaped
// to prevent. So: Split is spelled out, Single is silence.
[[nodiscard]] inline QStringList consoleLayoutArgv(
    PlayerSection::PlayerLayoutMode mode) noexcept
{
    QStringList args;
    if (mode == PlayerSection::PlayerLayoutMode::Split)
        args << QString::fromLatin1(kConsoleLayoutRowKey)
             + QStringLiteral("=split");
    return args;
}

} // namespace mhw
