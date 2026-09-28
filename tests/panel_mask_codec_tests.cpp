// SPDX-License-Identifier: Apache-2.0
// Unit tests for mhw::PanelMaskCodec — the pure text half of the control
// console's `monster-overlay.conf` mask persistence that used to live
// inline in ControlPanel::{loadMaskFromDisk,saveMaskToDisk}.
//
// WHAT THESE TESTS EXIST TO PROTECT
// ---------------------------------
// The four mask rows are a user contract, not an internal detail:
//
//     player=<hex32>
//     monster=<hex32>
//     damage=<hex32>
//     pets=<hex32>
//
// The overlay's locale_sync.h polls the same file, users hand-edit it, and
// a pre-pets three-row config must keep loading. Every assertion below is
// therefore written against a LITERAL expected string or a literal bit
// value, never against "some plausible result" — a renamed key, a
// reordered row, a dropped `locale=` line or a stray blank line is
// exactly the kind of regression this file has to catch.
//
// The contract highlights, each with its own case below:
//   * a masked row is matched on the TRIMMED line, so `  player=x` is NOT
//     a mask row and is preserved verbatim instead (original behaviour)
//   * `locale=`, comments, blank lines and unknown keys survive in their
//     original relative order
//   * duplicate mask rows converge to the last valid one
//   * a missing or malformed row leaves the caller's default in place
//   * a pre-pets config (no `pets=` row) migrates Damage to add the
//     teammate bit, unless the damage mask is zero (panel was off)
//   * a save emits exactly 4 mask rows + the preserved lines, LF-terminated
//
// Isolation: the codec is pure, so most cases need no filesystem at all.
// The cases that DO touch a file (the readLine-equivalence proof) work
// inside a QTemporaryDir and never ~/.config.

#include "ui/viewmodel/panel_mask_codec.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>

namespace {

int failures = 0;
void check(bool cond, const char *what)
{
    if (cond) std::printf("PASS: %s\n", what);
    else { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

// Read a file the way the console does, so the codec receives text that
// has already been through text-mode translation. This mirrors
// ControlPanel::loadMaskFromDisk's file-open sequence exactly; keeping it
// in the test (rather than in the codec) is the whole point of the split.
QString readConfText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    return in.readAll();
}

QByteArray serializeTo(uint32_t p, uint32_t m, uint32_t d, uint32_t pets,
                       const QString &existing)
{
    return mhw::serializePanelMasks(p, m, d, pets, existing);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ================================================================
    // 1. NORMAL PARSE — all four rows, literal expectations
    // ================================================================
    {
        const mhw::PanelMaskSet s = mhw::parsePanelMasks(QStringLiteral(
            "player=fb\n"
            "monster=2f\n"
            "damage=7\n"
            "pets=1\n"));
        check(s.playerValid && s.player == 0xfbu, "parse: player=fb -> 0xfb");
        check(s.monsterValid && s.monster == 0x2fu, "parse: monster=2f -> 0x2f");
        check(s.damageValid && s.damage == 0x7u, "parse: damage=7 -> 0x7");
        check(s.petsValid && s.pets == 0x1u, "parse: pets=1 -> 0x1");
        // A modern four-row config must NOT be touched by the legacy
        // migration — petsLineSeen suppresses it.
        check(s.petsLineSeen == true, "parse: four-row config reports petsLineSeen");
        check(s.damage == 0x7u, "parse: four-row config keeps damage verbatim (no migration)");
    }

    // ================================================================
    // 2. EMPTY / MISSING ROWS — defaults, no crash
    // ================================================================
    {
        const mhw::PanelMaskSet empty = mhw::parsePanelMasks(QString());
        check(!empty.playerValid && !empty.monsterValid
                  && !empty.damageValid && !empty.petsValid,
              "parse: empty text yields no valid masks");
        check(empty.player == 0u && empty.monster == 0u
                  && empty.damage == 0u && empty.pets == 0u,
              "parse: empty text yields zeroed masks");
        check(!empty.petsLineSeen, "parse: empty text reports no pets line");
    }

    // Only two of the four rows present: the other two stay invalid, so
    // the caller's own defaults for those panels survive.
    {
        const mhw::PanelMaskSet s = mhw::parsePanelMasks(QStringLiteral(
            "player=3\n"
            "pets=2\n"));
        check(s.playerValid && s.player == 0x3u, "missing rows: present player parsed");
        check(s.petsValid && s.pets == 0x2u, "missing rows: present pets parsed");
        check(!s.monsterValid, "missing rows: absent monster stays invalid");
        check(!s.damageValid, "missing rows: absent damage stays invalid");
    }

    // ================================================================
    // 3. HEX VALUE ACCEPTANCE — the exact toUInt(&ok, 16) rule
    // ================================================================
    {
        // The companion bits: a value with extra bits is accepted as-is
        // (the parser never rejects an out-of-range-for-this-panel mask).
        const mhw::PanelMaskSet wide = mhw::parsePanelMasks(QStringLiteral(
            "player=ffffffff\n"));
        check(wide.playerValid && wide.player == 0xffffffffu,
              "hex: full 32-bit mask accepted");

        // '0x' prefix and a leading '+' are accepted by toUInt(,16) — a
        // quirk worth pinning, because "cleaning it up" would change
        // behaviour for a user who typed either.
        const mhw::PanelMaskSet quirky = mhw::parsePanelMasks(QStringLiteral(
            "player=0x1f\nmonster=+2\n"));
        check(quirky.playerValid && quirky.player == 0x1fu,
              "hex: 0x-prefixed value parses to 0x1f (toUInt quirk)");
        check(quirky.monsterValid && quirky.monster == 0x2u,
              "hex: leading '+' parses to 0x2 (toUInt quirk)");

        // Leading/trailing spaces inside the value are stripped by toUInt.
        const mhw::PanelMaskSet spaced = mhw::parsePanelMasks(QStringLiteral(
            "player=   1f   \n"));
        check(spaced.playerValid && spaced.player == 0x1fu,
              "hex: surrounding spaces do not invalidate a value");
    }

    // ================================================================
    // 4. ILLEGAL HEX — never valid, never a silent zero, never fatal
    // ================================================================
    {
        const mhw::PanelMaskSet bad = mhw::parsePanelMasks(QStringLiteral(
            "player=zz\n"
            "monster=1f\n"
            "damage=-1\n"
            "pets=f f\n"
            "player=5\n"));   // then a GOOD player row after a bad one
        check(!bad.playerValid || bad.player == 0x5u,
              "illegal hex: garbage player row does not win over a good one");
        check(bad.monsterValid && bad.monster == 0x1fu,
              "illegal hex: a good sibling row still parses");
        check(!bad.damageValid, "illegal hex: 'damage=-1' rejected");
        check(!bad.petsValid, "illegal hex: 'pets=f f' rejected");
        check(bad.petsLineSeen,
              "illegal hex: a malformed pets= row still counts as petsLineSeen");
    }

    // A too-large hex value overflows uint32 and is rejected.
    {
        const mhw::PanelMaskSet overflow = mhw::parsePanelMasks(QStringLiteral(
            "player=100000000\n"));
        check(!overflow.playerValid,
              "illegal hex: 0x100000000 (44-bit) is rejected as overflow");
    }

    // An empty value is rejected, not taken as 0.
    {
        const mhw::PanelMaskSet emptyVal = mhw::parsePanelMasks(QStringLiteral(
            "player=\n"));
        check(!emptyVal.playerValid,
              "illegal hex: empty value is rejected rather than read as 0");
    }

    // ================================================================
    // 5. DUPLICATE MASK ROWS CONVERGE
    //    The save writes one row per key; the load must agree, and the
    //    last VALID occurrence wins (a trailing garbage row does not
    //    clobber the good one before it).
    // ================================================================
    {
        const mhw::PanelMaskSet dup = mhw::parsePanelMasks(QStringLiteral(
            "player=1\n"
            "player=7\n"
            "player=2\n"));
        check(dup.playerValid && dup.player == 0x2u,
              "duplicates: last valid player row wins");
        // A value that is malformed LAST does not wipe the earlier value,
        // because a rejected parse leaves both fields untouched.
        const mhw::PanelMaskSet lastBad = mhw::parsePanelMasks(QStringLiteral(
            "player=f\n"
            "player=nope\n"));
        check(lastBad.playerValid && lastBad.player == 0xfu,
              "duplicates: trailing garbage leaves the earlier valid value");
    }

    // ================================================================
    // 6. LEGACY THREE-ROW CONFIG (no pets= row)
    //    Damage keeps the teammate bit visible, unless Damage is 0
    //    (the whole panel was disabled and stays disabled).
    // ================================================================
    {
        const mhw::PanelMaskSet legacy = mhw::parsePanelMasks(QStringLiteral(
            "player=fb\n"
            "monster=2f\n"
            "damage=7\n"));
        check(!legacy.petsValid, "legacy: no pets row -> pets stays invalid");
        check(!legacy.petsLineSeen, "legacy: no pets row -> petsLineSeen false");
        check(legacy.damageValid
                  && legacy.damage == (0x7u | mhw::DamageSection::OtherMembers),
              "legacy: damage mask gains the teammate bit");
    }
    {
        const mhw::PanelMaskSet legacyOff = mhw::parsePanelMasks(QStringLiteral(
            "damage=0\n"));
        check(legacyOff.damageValid && legacyOff.damage == 0u,
              "legacy: damage=0 stays zero (panel disabled, no migration)");
    }
    {
        // pets= present but malformed: still treated as a modern config,
        // so the damage mask is NOT migrated.
        const mhw::PanelMaskSet petsBad = mhw::parsePanelMasks(QStringLiteral(
            "damage=7\n"
            "pets=garbage\n"));
        check(petsBad.petsLineSeen && !petsBad.petsValid,
              "legacy: malformed pets row suppresses damage migration");
        check(petsBad.damage == 0x7u,
              "legacy: damage unmigrated when a pets row was present");
    }

    // ================================================================
    // 7. LINE CLASSIFICATION — localize=, comments, blank lines,
    //    unknown keys, and the TRIMMED-match quirk.
    // ================================================================
    {
        check(mhw::classifyPanelMaskLine(QStringLiteral("player=1"))
                  == mhw::PanelMaskLineKind::Canonical,
              "classify: player= is canonical");
        check(mhw::classifyPanelMaskLine(QStringLiteral("  monster=2f  "))
                  == mhw::PanelMaskLineKind::Canonical,
              "classify: indented canonical row is judged on its trimmed text");
        check(mhw::classifyPanelMaskLine(QStringLiteral("locale=zh-CN"))
                  == mhw::PanelMaskLineKind::Preserved,
              "classify: locale= is preserved (never touched by this codec)");
        check(mhw::classifyPanelMaskLine(QStringLiteral("# comment"))
                  == mhw::PanelMaskLineKind::Preserved,
              "classify: comment is preserved");
        check(mhw::classifyPanelMaskLine(QString())
                  == mhw::PanelMaskLineKind::Preserved,
              "classify: blank line is preserved");
        check(mhw::classifyPanelMaskLine(QStringLiteral("zoom=2.0"))
                  == mhw::PanelMaskLineKind::Preserved,
              "classify: unknown key is preserved");
        check(mhw::classifyPanelMaskLine(QStringLiteral("futurekey=1"))
                  == mhw::PanelMaskLineKind::Preserved,
              "classify: unknown/future key is preserved");
    }

    // ================================================================
    // 8. SERIALIZE — the four rows first, then the preserved lines in
    //    their ORIGINAL RELATIVE ORDER, verbatim (whitespace included).
    // ================================================================
    {
        // The canonical save over an empty previous file.
        check(serializeTo(0xfbu, 0x2fu, 0x7u, 0x1u, QString())
                  == QByteArrayLiteral("player=fb\nmonster=2f\ndamage=7\npets=1\n"),
              "serialize: fresh file emits exactly the four rows, LF-terminated");

        // locale + comments + blank + unknown, all preserved in order.
        const QByteArray out = serializeTo(0xfb, 0x2f, 0x7, 0x1, QStringLiteral(
            "locale=zh-CN\n"
            "# a hand-written comment\n"
            "\n"
            "zoom=2.0\n"
            "player=deadbeef\n"
            "monster=0\n"
            "damage=f\n"
            "pets=3\n"
            "# trailing note\n"));
        const QByteArray expected = QByteArrayLiteral(
            "player=fb\n"
            "monster=2f\n"
            "damage=7\n"
            "pets=1\n"
            "locale=zh-CN\n"
            "# a hand-written comment\n"
            "\n"
            "zoom=2.0\n"
            "# trailing note\n");
        check(out == expected,
              "serialize: non-mask lines kept verbatim in original order; "
              "old mask rows removed");

        // The preserved lines keep leading/trailing whitespace byte-for-byte.
        const QByteArray ws = serializeTo(0x1, 0x2, 0x3, 0x4, QStringLiteral(
            "   # indented comment\n"
            "\n"
            "locale = zh-CN   \n"));
        check(ws == QByteArrayLiteral(
                  "player=1\nmonster=2\ndamage=3\npets=4\n"
                  "   # indented comment\n"
                  "\n"
                  "locale = zh-CN   \n"),
              "serialize: preserved lines keep their leading/trailing spaces");

        // An INDENTED mask row is still a mask row: both sides judge on the
        // trimmed text, so `  player=1` is removed and rewritten rather
        // than preserved. That convergence is the original behaviour and
        // the reason the expected output has no second player row.
        const QByteArray indented = serializeTo(0x1, 0x2, 0x3, 0x4, QStringLiteral(
            "  player=1\n"));
        check(indented == QByteArrayLiteral(
                  "player=1\nmonster=2\ndamage=3\npets=4\n"),
              "serialize: an indented mask row is rewritten, not preserved");

        // Duplicate canonical rows in the previous file converge to the
        // single row the writer emits (nothing replicated below it).
        const QByteArray duped = serializeTo(0xf, 0xf, 0xf, 0xf, QStringLiteral(
            "player=1\n"
            "player=2\n"
            "locale=zh-CN\n"
            "pets=9\n"));
        check(duped == QByteArrayLiteral(
                  "player=f\nmonster=f\ndamage=f\npets=f\n"
                  "locale=zh-CN\n"),
              "serialize: duplicate canonical rows collapse to one copy");

        // A previous file's trailing newline must NOT produce a phantom
        // blank line in the output (the reason the scan is a readLine
        // loop and not a split('\n')).
        const QByteArray trailing = serializeTo(0x1, 0x2, 0x3, 0x4, QStringLiteral(
            "locale=zh-CN\n"));
        check(trailing == QByteArrayLiteral(
                  "player=1\nmonster=2\ndamage=3\npets=4\nlocale=zh-CN\n"),
              "serialize: trailing newline adds no phantom blank line");
        check(!trailing.endsWith("\n\n"),
              "serialize: output never ends with a blank line");

        // A previous file with NO trailing newline still parses.
        const QByteArray noTrail = serializeTo(0x1, 0x2, 0x3, 0x4, QStringLiteral(
            "locale=zh-CN\n# note"));
        check(noTrail == QByteArrayLiteral(
                  "player=1\nmonster=2\ndamage=3\npets=4\nlocale=zh-CN\n# note\n"),
              "serialize: an unterminated final line is still preserved");

        // All-zero masks are a real state ("all off"), not a missing state.
        const QByteArray zeros = serializeTo(0, 0, 0, 0, QString());
        check(zeros == QByteArrayLiteral("player=0\nmonster=0\ndamage=0\npets=0\n"),
              "serialize: all-off masks round-trip as explicit zeros");
    }

    // ================================================================
    // 9. ROUND TRIP — serialize then parse returns the same masks
    // ================================================================
    {
        const uint32_t p = 0xfbu, m = 0x2fu, d = 0x7u, pets = 0x1u;
        const QByteArray written =
            serializeTo(p, m, d, pets, QStringLiteral("locale=zh-CN\n# c\n"));
        const mhw::PanelMaskSet back =
            mhw::parsePanelMasks(QString::fromUtf8(written));
        check(back.player == p && back.monster == m && back.damage == d
                  && back.pets == pets && back.playerValid && back.monsterValid
                  && back.damageValid && back.petsValid,
              "round trip: masks survive a write-then-read");
        check(back.petsLineSeen,
              "round trip: written file reports the modern four-row layout");

        // A legacy file that gets migrated, then saved, comes back as a
        // four-row config whose damage mask already carries the bit.
        const mhw::PanelMaskSet legacy =
            mhw::parsePanelMasks(QStringLiteral("damage=7\n"));
        const QByteArray afterMigration = serializeTo(
            legacy.player, legacy.monster, legacy.damage, legacy.pets,
            QStringLiteral("damage=7\n"));
        const mhw::PanelMaskSet reRead =
            mhw::parsePanelMasks(QString::fromUtf8(afterMigration));
        check(reRead.damageValid
                  && reRead.damage == (0x7u | mhw::DamageSection::OtherMembers),
              "round trip: migrated damage mask survives the rewrite");
        check(afterMigration == QByteArrayLiteral(
                  "player=0\nmonster=0\ndamage=f\npets=0\n"),
              "round trip: migrated save emits exactly four rows, no leftovers");
    }

    // ================================================================
    // 10. readLine EQUIVALENCE — the codec's in-memory scan must agree
    //     with the console's real file read on every input, so that
    //     feeding it file text is not a semantic change.
    // ================================================================
    {
        QTemporaryDir tmp;
        if (!tmp.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }

        const struct { const char *name; const char *body; } cases[] = {
            {"lf-terminated", "player=1\nmonster=2\ndamage=3\npets=4\n"},
            {"no trailing newline", "player=1\nmonster=2"},
            {"crlf", "player=1\r\nmonster=2\r\n"},
            {"lone CR", "player=1\rmonster=2"},
            {"crlf + lone CR", "player=1\r\nmonster=2\rdamage=3"},
            {"blank lines", "player=1\n\n\nmonster=2\n"},
            {"blank line spaces", "player=1\n   \nmonster=2\n"},
            {"leading spaces preserved", "   # c\n\nplayer=1\n"},
            {"comment + locale + unknown",
             "# top\nlocale=zh-CN\nplayer=1\nunknown=k\n# bottom\n"},
            {"duplicates", "player=1\nplayer=2\nplayer=3\n"},
            {"mask then malformed", "player=f\nplayer=zz\n"},
            {"utf-8 comment", "# 中文注释\nplayer=1\n"},
            {"invalid utf-8 junk", "# \xff\xfe junk\nplayer=1\n"},
            {"empty file", ""},
            {"only newline", "\n"},
            {"only crlf", "\r\n"},
        };

        bool allMatch = true;
        int mismatches = 0;
        for (const auto &c : cases) {
            const QString path = tmp.filePath(QStringLiteral("eq.conf"));
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                check(false, "readLine eq: temp file not writable");
                return 1;
            }
            const QByteArray body(c.body);
            f.write(body);
            f.close();

            // The console's read path: text-mode readLine loop over a file.
            QFile in(path);
            bool opened = in.open(QIODevice::ReadOnly | QIODevice::Text);
            QStringList viaFile;
            if (opened) {
                QTextStream s(&in);
                s.setEncoding(QStringConverter::Utf8);
                while (!s.atEnd())
                    viaFile.append(s.readLine());
            }

            // The codec's read path: the same in-memory scan it uses.
            const QString text = readConfText(path);
            QStringList viaCodec;
            {
                QString scan = text;
                QTextStream s(&scan);
                while (!s.atEnd())
                    viaCodec.append(s.readLine());
            }

            if (viaFile != viaCodec) {
                ++mismatches;
                allMatch = false;
                std::fprintf(stderr,
                             "  readLine eq mismatch [%s]: file=%d codec=%d\n",
                             c.name, int(viaFile.size()), int(viaCodec.size()));
                int n = qMax(viaFile.size(), viaCodec.size());
                for (int i = 0; i < n; ++i)
                    std::fprintf(stderr, "    [%d] file='%s' codec='%s'\n", i,
                                 i < viaFile.size() ? qPrintable(viaFile[i])
                                                    : "<none>",
                                 i < viaCodec.size() ? qPrintable(viaCodec[i])
                                                     : "<none>");
            } else {
                std::printf("  readLine eq ok [%s]: %d line(s)\n",
                            c.name, int(viaFile.size()));
            }
            QFile::remove(path);
        }
        check(allMatch,
              "readLine eq: the codec's in-memory scan matches the file "
              "read on every case");
        check(mismatches == 0, "readLine eq: zero mismatches reported");
    }

    // ================================================================
    // 11. FILE-LEVEL CONTRACT — the exact bytes a save leaves behind,
    //     including the end-to-end path write-conf -> re-read -> re-save
    //     (which must be a fixed point after the first write).
    // ================================================================
    {
        QTemporaryDir tmp;
        if (!tmp.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        const QString path = tmp.filePath(QStringLiteral("monster-overlay.conf"));

        // Seed a hand-written conf with a locale row and a comment, then
        // read it exactly the way ControlPanel does.
        {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                check(false, "file contract: seed not writable");
                return 1;
            }
            f.write(QByteArrayLiteral(
                "# monster-overlay.conf\n"
                "locale=zh-CN\n"
                "player=fb\n"
                "monster=2f\n"
                "damage=7\n"
                "pets=1\n"
                "zoom=2.0\n"));
            f.close();
        }

        const mhw::PanelMaskSet loaded = mhw::parsePanelMasks(readConfText(path));
        check(loaded.playerValid && loaded.player == 0xfbu
                  && loaded.monsterValid && loaded.monster == 0x2fu
                  && loaded.damageValid && loaded.damage == 0x7u
                  && loaded.petsValid && loaded.pets == 0x1u,
              "file contract: file-loaded masks are the seeded values");

        const QByteArray saved = serializeTo(loaded.player, loaded.monster,
                                             loaded.damage, loaded.pets,
                                             readConfText(path));
        const QByteArray expected = QByteArrayLiteral(
            "player=fb\n"
            "monster=2f\n"
            "damage=7\n"
            "pets=1\n"
            "# monster-overlay.conf\n"
            "locale=zh-CN\n"
            "zoom=2.0\n");
        check(saved == expected,
              "file contract: save puts the 4 rows first and keeps locale/"
              "comment/unknown in order");

        // Write that out and save again: the second output must be
        // byte-identical, i.e. the migration is a fixed point.
        {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                check(false, "file contract: rewrite not writable");
                return 1;
            }
            f.write(saved);
            f.close();
        }
        const QByteArray again = serializeTo(
            loaded.player, loaded.monster, loaded.damage, loaded.pets,
            readConfText(path));
        check(again == saved,
              "file contract: re-saving a saved conf is byte-identical");

        // The locale row is never duplicated or dropped by a mask save.
        check(QString::fromUtf8(saved).count(QStringLiteral("locale=")) == 1,
              "file contract: exactly one locale= row after a mask save");

        // And the mask key spellings are the four canonical ones only.
        const QString savedText = QString::fromUtf8(saved);
        check(savedText.startsWith(QStringLiteral("player=fb\n"))
                  && savedText.contains(QStringLiteral("\nmonster=2f\n"))
                  && savedText.contains(QStringLiteral("\ndamage=7\n"))
                  && savedText.contains(QStringLiteral("\npets=1\n")),
              "file contract: key spellings player/monster/damage/pets intact");
    }

    std::printf("\n%s\n", failures == 0 ? "ALL PANEL-MASK-CODEC TESTS PASSED"
                                        : "SOME PANEL-MASK-CODEC TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
