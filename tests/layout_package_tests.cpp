// SPDX-License-Identifier: Apache-2.0
//
// LayoutPackage — the format contract, asserted.
//
// The rules here are the ones an import depends on: a file that lies about
// its own contents must be refused, not trusted. Every rule has at least one
// assertion, and the round-trip check runs on the SHIPPED default so the
// file a new user gets is exercised by the same codec the console uses.
//
// Qt6::Core only — no Widgets, matching the codec's dependency budget.

#include <QFile>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>

#include "ui/layout_package.h"

// Set by CMake to the source root so the shipped default is read from the
// tree, not from whatever the working directory happens to be. The fallback
// keeps the TU compilable on its own (and keeps a language server that does
// not know the define from crying wolf).
#ifndef MONSTER_SOURCE_DIR
#define MONSTER_SOURCE_DIR "."
#endif

namespace {

int failures = 0;

void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "PASS:" : "FAIL:",
                what.toUtf8().constData());
    if (!ok)
        ++failures;
}

void checkReject(const QByteArray &json, const QString &needle,
                 const QString &what)
{
    QString error;
    const auto parsed = mhw::LayoutPackage::parse(json, &error);
    const bool rejected = !parsed && error.contains(needle);
    check(rejected,
          QStringLiteral("%1 -- rejected with \"%2\"")
              .arg(what, rejected ? error : QStringLiteral("NOT REJECTED")));
}

// A package with one scope and one panel group, the smallest thing an
// import can carry and still say something true in its header.
QByteArray minimalPackage(const QString &scope = QStringLiteral("rise/mixed"),
                          const QString &panel = QStringLiteral("monster"))
{
    const int slash = scope.indexOf(QLatin1Char('/'));
    const QString mode = scope.mid(slash + 1);
    QStringList keys = mhw::layoutPackageWindowsFor(panel, mode);
    QStringList fields;
    for (const QString &k : keys)
        fields.append(QStringLiteral("  \"%1\": {\"ml\": 1, \"mt\": 2}").arg(k));
    return QStringLiteral(
               "{\n"
               "  \"kind\": \"monster-overlay-layout\",\n"
               "  \"schema\": 1,\n"
               "  \"contains\": {\"global\": false, \"scopes\": {\n"
               "    \"%1\": [\"%2\"]\n"
               "  }},\n"
               "  \"scopes\": {\n"
               "    \"%1\": {\n%3\n    }\n"
               "  }\n"
               "}\n")
        .arg(scope, panel, fields.join(QStringLiteral(",\n")))
        .toUtf8();
}

}  // namespace

int main()
{
    std::printf("== layout_package_tests ==\n");

    // ── 1. round trip is byte-stable ──────────────────────────────────
    {
        QString error;
        const auto parsed =
            mhw::LayoutPackage::parse(minimalPackage(), &error);
        check(parsed.has_value(),
              QStringLiteral("the minimal package parses -- %1").arg(error));
        if (parsed) {
            const QByteArray a = parsed->serialize();
            const QByteArray b = parsed->serialize();
            check(a == b, "serializing twice is byte-identical");
            QString error2;
            const auto again = mhw::LayoutPackage::parse(a, &error2);
            check(again.has_value() && again->serialize() == a,
                  QStringLiteral("parse(serialize(pkg)) round-trips -- %1")
                      .arg(error2));
        }
    }

    // ── 2. kind is a hard requirement ─────────────────────────────────
    checkReject(QByteArray("{\"kind\":\"something-else\",\"schema\":1,")
                    + "\"contains\":{\"global\":false,\"scopes\":{}},"
                      "\"scopes\":{}}",
                "not a layout package", "wrong kind is refused");

    // ── 3. schema ─────────────────────────────────────────────────────
    checkReject(QByteArray("{\"kind\":\"monster-overlay-layout\",")
                    + "\"schema\":99,"
                      "\"contains\":{\"global\":false,\"scopes\":{}},"
                      "\"scopes\":{}}",
                "newer than this build",
                "a newer schema is refused rather than half-read");
    checkReject(QByteArray("{\"kind\":\"monster-overlay-layout\",")
                    + "\"contains\":{\"global\":false,\"scopes\":{}},"
                      "\"scopes\":{}}",
                "schema: missing", "a missing schema is refused");
    checkReject(QByteArray("{\"kind\":\"monster-overlay-layout\",")
                    + "\"schema\":0,"
                      "\"contains\":{\"global\":false,\"scopes\":{}},"
                      "\"scopes\":{}}",
                "expected >= 1", "schema 0 is refused");

    // ── 4/13. the header is the contract ──────────────────────────────
    checkReject(QByteArray("{\"kind\":\"monster-overlay-layout\","
                           "\"schema\":1,"
                           "\"contains\":{\"global\":false,"
                           "\"scopes\":{\"rise/mixed\":[\"monster\"]}},"
                           "\"scopes\":{}}"),
                "declared but scopes has no such scope",
                "a declared scope missing from the body is refused");
    checkReject(QByteArray("{\"kind\":\"monster-overlay-layout\","
                           "\"schema\":1,"
                           "\"contains\":{\"global\":false,\"scopes\":{}},"
                           "\"scopes\":{\"rise/mixed\":{}}}"),
                "does not declare it",
                "a body scope the header never mentions is refused");
    {
        // Declaring a panel group but carrying none of its windows.
        QByteArray json = minimalPackage();
        json.replace("\"monster\": {\"ml\": 1, \"mt\": 2}", "\"monster\": {}");
        json.replace("},\n    \"monster-identity\": {\"ml\": 1, \"mt\": 2}",
                     "}");
        // Rebuild instead of splicing: the shape above is too brittle.
        json = QByteArray("{\"kind\":\"monster-overlay-layout\","
                          "\"schema\":1,"
                          "\"contains\":{\"global\":false,"
                          "\"scopes\":{\"rise/mixed\":[\"monster\"]}},"
                          "\"scopes\":{\"rise/mixed\":{}}}");
        checkReject(json, "none of its windows",
                    "a declared panel group with no windows is refused");
    }

    // ── 5. the global block must agree with its own flag ──────────────
    checkReject(QByteArray("{\"kind\":\"monster-overlay-layout\","
                           "\"schema\":1,"
                           "\"contains\":{\"global\":false,\"scopes\":{}},"
                           "\"global\":{}}"),
                "says there is no global block",
                "a global block behind a false flag is refused");
    checkReject(QByteArray("{\"kind\":\"monster-overlay-layout\","
                           "\"schema\":1,"
                           "\"contains\":{\"global\":true,\"scopes\":{}},"
                           "\"scopes\":{}}"),
                "but there is no global object",
                "a true global flag with no block is refused");

    // ── 6. scope grammar ──────────────────────────────────────────────
    checkReject(QByteArray("{\"kind\":\"monster-overlay-layout\","
                           "\"schema\":1,"
                           "\"contains\":{\"global\":false,"
                           "\"scopes\":{\"rise/night\":[\"monster\"]}},"
                           "\"scopes\":{\"rise/night\":{\"monster\":{}}}}"),
                "not <world|rise>/<single|split|mixed>",
                "an unknown mode name is refused");

    // ── 7/12. windows belong to declared groups ───────────────────────
    checkReject(QByteArray("{\"kind\":\"monster-overlay-layout\","
                           "\"schema\":1,"
                           "\"contains\":{\"global\":false,"
                           "\"scopes\":{\"rise/mixed\":[\"monster\"]}},"
                           "\"scopes\":{\"rise/mixed\":"
                           "{\"monster\":{\"ml\":1},"
                           "\"player-bars\":{\"ml\":1}}}}"),
                "does not declare",
                "a window outside every declared group is refused");
    checkReject(QByteArray("{\"kind\":\"monster-overlay-layout\","
                           "\"schema\":1,"
                           "\"contains\":{\"global\":false,"
                           "\"scopes\":{\"rise/single\":[\"monster\"]}},"
                           "\"scopes\":{\"rise/single\":"
                           "{\"monster-identity\":{\"ml\":1}}}}"),
                "not a window of this mode",
                "a sub-window in a single-mode scope is refused");

    // ── 8. unknown keys survive at every level ────────────────────────
    {
        const QByteArray json = QByteArray(
            "{\"kind\":\"monster-overlay-layout\",\"schema\":1,"
            "\"topUnknown\":{\"a\":1},"
            "\"contains\":{\"global\":true,"
            "\"scopes\":{\"rise/mixed\":[\"monster\"]},"
            "\"containsUnknown\":\"x\"},"
            "\"global\":{\"locale\":\"zh-CN\",\"globalUnknown\":7},"
            "\"scopes\":{\"rise/mixed\":{\"monster\":"
            "{\"ml\":1,\"rowUnknown\":true}}}}");
        QString error;
        const auto parsed = mhw::LayoutPackage::parse(json, &error);
        check(parsed.has_value(),
              QStringLiteral("an unknown-key file still parses -- %1")
                  .arg(error));
        if (parsed) {
            const QByteArray out = parsed->serialize();
            check(out.contains("\"topUnknown\"")
                      && out.contains("\"containsUnknown\"")
                      && out.contains("\"globalUnknown\"")
                      && out.contains("\"rowUnknown\""),
                  "unknown keys survive at all four levels (root/contains/"
                  "global/row)");
            QString error2;
            const auto again = mhw::LayoutPackage::parse(out, &error2);
            check(again.has_value(),
                  QStringLiteral("the unknown-key file round-trips -- %1")
                      .arg(error2));
        }
    }

    // ── 9. float noise is flattened ───────────────────────────────────
    {
        const QByteArray json = QByteArray(
            "{\"kind\":\"monster-overlay-layout\",\"schema\":1,"
            "\"contains\":{\"global\":false,\"scopes\":{\"rise/mixed\":"
            "[\"monster\"]}},"
            "\"scopes\":{\"rise/mixed\":{\"monster\":"
            "{\"scale\":0.7000000000000001}}}}");
        QString error;
        const auto parsed = mhw::LayoutPackage::parse(json, &error);
        check(parsed.has_value(), "a float-noise row parses");
        if (parsed) {
            const QByteArray out = parsed->serialize();
            check(out.contains("\"scale\": 0.7")
                      && !out.contains("0.7000000000000001"),
                  "scale is emitted as 0.7, not 0.7000000000000001");
        }
    }

    // ── 10. a wrong type names the field it refused ───────────────────
    checkReject(QByteArray("{\"kind\":\"monster-overlay-layout\","
                           "\"schema\":1,"
                           "\"contains\":{\"global\":false,"
                           "\"scopes\":{\"rise/mixed\":[\"monster\"]}},"
                           "\"scopes\":{\"rise/mixed\":{\"monster\":"
                           "{\"ml\":\"twelve\"}}}}"),
                "scopes.rise/mixed.monster.ml",
                "a string where an int belongs is refused with its path");

    // ── 11. the queries the UI uses ───────────────────────────────────
    {
        QString error;
        const auto parsed =
            mhw::LayoutPackage::parse(minimalPackage(), &error);
        check(parsed->containsScope(QStringLiteral("rise/mixed"))
                  && !parsed->containsScope(QStringLiteral("world/mixed")),
              "containsScope reports what the header declares");
        check(parsed->panelsOf(QStringLiteral("rise/mixed"))
                  == QStringList{QStringLiteral("monster")},
              "panelsOf returns the declared group");
        check(parsed->windowsOf(QStringLiteral("rise/mixed"),
                                QStringLiteral("monster"))
                  .size() == 3,
              "windowsOf(monster in mixed) is the mother + two sub-windows");
    }

    // ── the registry, spelled once ────────────────────────────────────
    check(mhw::layoutPackagePanelsFor(false).size() == 3
              && mhw::layoutPackagePanelsFor(true).size() == 4,
          "world has 3 panels, rise has 4 — the user's seven");
    check(mhw::layoutPackageWindowsFor(QStringLiteral("player"),
                                       QStringLiteral("single"))
                  .size() == 1
              && mhw::layoutPackageWindowsFor(QStringLiteral("player"),
                                              QStringLiteral("mixed"))
                     .size() == 7,
          "a panel has one window in single and seven in mixed");

    // ── the SHIPPED default must parse and agree with itself ──────────
    {
        QFile f(QStringLiteral(MONSTER_SOURCE_DIR
                               "/data/default-layout.json"));
        check(f.open(QIODevice::ReadOnly), "the shipped default is readable");
        QString error;
        const auto parsed = mhw::LayoutPackage::parse(f.readAll(), &error);
        check(parsed.has_value(),
              QStringLiteral("the shipped default parses -- %1").arg(error));
        if (parsed) {
            check(parsed->containsScope(QStringLiteral("rise/mixed"))
                      && parsed->containsScope(QStringLiteral("world/single")),
                  "the shipped default covers all six scopes");
            check(parsed->hasGlobal,
                  "the shipped default carries the global block");
            QString error2;
            const auto again =
                mhw::LayoutPackage::parse(parsed->serialize(), &error2);
            check(again.has_value(),
                  QStringLiteral("the shipped default round-trips -- %1")
                      .arg(error2));
            check(again->serialize() == parsed->serialize(),
                  "the shipped default serializes to the same bytes twice");
        }
    }

    // ── the disk half: panels.ini in, panels.ini out ──────────────────
    {
        QTemporaryDir tmp;
        QSettings s(tmp.filePath(QStringLiteral("panels.ini")),
                    QSettings::IniFormat);

        // Two scopes on disk already, only one of which the package will
        // mention. This is the contract an import lives or dies by: the
        // scope the file does not carry must come out byte-identical.
        s.beginGroup(QStringLiteral("world/mixed/player-bars"));
        s.setValue(QStringLiteral("ml"), 42);
        s.setValue(QStringLiteral("mt"), 43);
        s.endGroup();
        s.beginGroup(QStringLiteral("rise/single/dps"));
        s.setValue(QStringLiteral("ml"), 7);
        s.endGroup();
        s.sync();

        mhw::LayoutPackage pkg;
        pkg.containsScopes.insert(QStringLiteral("world/mixed"),
                                  QStringList{QStringLiteral("player")});
        mhw::LayoutWindowRow row;
        row.ml = 500;
        row.mt = 600;
        row.scale = 0.7000000000000001;
        row.visible = true;
        pkg.scopes[QStringLiteral("world/mixed")]
               [QStringLiteral("player-bars")] = row;

        QString error;
        check(mhw::layoutPackageToSettings(pkg, s, &error),
              QStringLiteral("writing a package to panels.ini works -- %1")
                  .arg(error));

        s.beginGroup(QStringLiteral("world/mixed/player-bars"));
        check(s.value(QStringLiteral("ml")).toInt() == 500
                  && s.value(QStringLiteral("mt")).toInt() == 600,
              "the declared window took the package's margins");
        check(!s.contains(QStringLiteral("mr")),
              "a field the package omitted is NOT written (mr stayed "
              "absent)");
        // THE STORE'S POLARITY. `winVisible: true` does not write true —
        // "absent means shown" is the contract console_fidelity_windows
        // established, so a show REMOVES the row. Asserting the round trip
        // preserves `true` as a stored value would be asserting the wrong
        // model; what must survive is the OUTCOME: a hidden window becomes
        // shown.
        check(!s.contains(QStringLiteral("winVisible"))
                  && !s.contains(QStringLiteral("visible")),
              "winVisible: true leaves NO row — absence IS the shown state");
        s.endGroup();

        s.beginGroup(QStringLiteral("rise/single/dps"));
        check(s.value(QStringLiteral("ml")).toInt() == 7,
              "THE BOUNDARY: a scope the package never mentioned is "
              "untouched");
        s.endGroup();

        // Read it back: the header must be derived from what is there.
        const auto back = mhw::layoutPackageFromSettings(s, &error);
        check(back.has_value(),
              QStringLiteral("reading panels.ini back into a package works "
                             "-- %1").arg(error));
        if (back) {
            check(back->containsScope(QStringLiteral("world/mixed"))
                      && back->containsScope(QStringLiteral("rise/single")),
                  "the derived header lists both scopes the store carries");
            check(back->panelsOf(QStringLiteral("world/mixed"))
                      .contains(QStringLiteral("player")),
                  "the derived header names the panel group it found");
            const mhw::LayoutWindowRow read =
                back->scopes.value(QStringLiteral("world/mixed"))
                    .value(QStringLiteral("player-bars"));
            check(read.ml == 500 && read.mt == 600,
                  "the row survived the round trip");
            check(read.scale.has_value()
                      && qAbs(*read.scale - 0.7) < 0.0001,
                  "scale survives; the noise is flattened by serialize, "
                  "not by the store");
            check(read.visible == std::nullopt,
                  "read back, the absent row means \"shown\" — no visible "
                  "flag was written and none is read");
        }

        // The other half of the polarity: a real hide must round trip as
        // false, and importing it must be the thing that hides a window.
        mhw::LayoutPackage hidden;
        hidden.containsScopes.insert(QStringLiteral("world/mixed"),
                                     QStringList{QStringLiteral("player")});
        mhw::LayoutWindowRow hiddenRow;
        hiddenRow.visible = false;
        hidden.scopes[QStringLiteral("world/mixed")]
               [QStringLiteral("player-bars")] = hiddenRow;
        check(mhw::layoutPackageToSettings(hidden, s, &error),
              "a hiding package writes");
        s.beginGroup(QStringLiteral("world/mixed/player-bars"));
        check(s.value(QStringLiteral("winVisible"), true).toBool() == false,
              "winVisible: false lands on disk as false");
        s.endGroup();
        const auto afterHide = mhw::layoutPackageFromSettings(s, &error);
        check(afterHide
                  && afterHide->scopes
                         .value(QStringLiteral("world/mixed"))
                         .value(QStringLiteral("player-bars"))
                         .visible == false,
              "a hidden window reads back as hidden");
    }

    std::printf("\nRESULT: %s (%d failures)\n",
                failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
