// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the per-game panels.ini namespace (v0.13).
//
// WHAT IS UNDER TEST
// ------------------
//   panel_settings_game.h — the group prefix a Panel's settings section
//     lives under, the launch-time game injection behind it, and the
//     one-time migration of a pre-v0.13 (un-prefixed) section.
//
// WHY THIS MATTERS
// ----------------
// Every panel used to live in ONE flat namespace: one [player-status]
// section, one [player-wirebug] section, and no way to say which game
// they were tuned for. The user found that immediately as a preview
// showing 衣装 (World-only) and 翔虫 (Rise-only) at the same time.
//
// The two invariants that must never break, each pinned by a section
// below:
//
//   1. GAME A's section is invisible to GAME B. A layout the user tuned
//      for World must never move a Rise window, and vice versa.
//   2. A pre-v0.13 file MIGRATES into the current game and never has
//      its other sections rewritten. Losing the user's existing layout
//      would be the regression this feature exists to fix.
//
// The migration test is a state machine over a real INI file on disk
// (QTemporaryDir, XDG_CONFIG_HOME redirected) because the failure that
// matters — a second run re-copying over an edited section — is a
// property of the file BETWEEN runs, not of one call.

#include "ui/panel_settings_game.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QStringList>
#include <QTextStream>

#include <cstdio>

namespace {

int failures = 0;
int checks   = 0;

void check(bool ok, const char *what)
{
    ++checks;
    if (ok) {
        std::printf("PASS: %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// QSettings resolves (IniFormat, UserScope, "monster-overlay", "panels")
// to $XDG_CONFIG_HOME/monster-overlay/panels.ini. Redirect the HOME
// tree so nothing here can ever touch the developer's real config —
// the same discipline tests/control_panel_p0_test.cpp and
// tests/console_layout_store_tests.cpp follow.
QString iniPath(const QTemporaryDir &home)
{
    return home.path() + QStringLiteral("/monster-overlay/panels.ini");
}

QString slurp(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    return in.readAll();
}

bool writeRaw(const QString &path, const QString &text)
{
    // QFile will not create the parent. The production path reaches this
    // directory through QSettings, which creates it; a hand-seeded file
    // has to do the same or every seed fails and the checks below are
    // vacuous.
    const QString parent = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(parent))
        return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    const QByteArray bytes = text.toUtf8();
    if (f.write(bytes) != bytes.size())
        return false;
    f.close();
    return true;
}

// A fresh handle on EVERY call. QSettings caches per (org, app, format),
// so a handle opened before a write would not see a later one — which is
// exactly what "the next run" means here, and the same property the
// production code has.
QSettings openSettings(const QString &path)
{
    return QSettings(path, QSettings::IniFormat);
}

// A group is present when any key in the file lives under a path whose
// prefix is that group. childGroups() answers only the TOP level
// ("world", "rise"), which is not what a nested group question needs —
// QSettings has no hasGroup(), so this is spelled from allKeys().
bool hasGroup(const QSettings &s, const QString &group)
{
    const QString prefix = group + QLatin1Char('/');
    const QStringList keys = s.allKeys();
    for (const QString &k : keys) {
        if (k.startsWith(prefix))
            return true;
    }
    // Also true for an empty group, which allKeys() cannot see.
    return s.childGroups().contains(group, Qt::CaseSensitive);
}

QString readKey(const QSettings &s, const QString &group, const QString &key)
{
    const QString full = group + QLatin1Char('/') + key;
    if (!s.contains(full))
        return QStringLiteral("<absent: %1>").arg(full);
    return s.value(full).toString();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ==================================================================
    // 1. THE GROUP PREFIX — the one place the namespace is spelled
    // ==================================================================
    check(mhw::panelSettingsGameGroup(mhw::GameId::World)
              == QStringLiteral("world"),
          "world group prefix is 'world'");
    check(mhw::panelSettingsGameGroup(mhw::GameId::Rise)
              == QStringLiteral("rise"),
          "rise group prefix is 'rise'");

    // v0.14: the mode axis is now part of the answer, so the single-mode
    // spelling carries its segment. This file's old assertions pinned the
    // two-domain spelling (`world/player-status`); the four-domain file
    // section in tests/panel_settings_scope_tests.cpp is where both axes
    // are pinned. What remains pinned HERE is the property this file was
    // written for: the two games do not share a namespace.
    check(mhw::panelSettingsScopeFor(mhw::GameId::World,
                                     mhw::PlayerSection::PlayerLayoutMode::Single,
                                     QStringLiteral("player-status"))
              == QStringLiteral("world/single/player-status"),
          "World single player-status scopes to world/single/player-status");
    check(mhw::panelSettingsScopeFor(mhw::GameId::Rise,
                                     mhw::PlayerSection::PlayerLayoutMode::Single,
                                     QStringLiteral("player-status"))
              == QStringLiteral("rise/single/player-status"),
          "Rise single player-status scopes to rise/single/player-status");
    check(mhw::panelSettingsGameGroup(mhw::GameId::World)
              != mhw::panelSettingsGameGroup(mhw::GameId::Rise),
          "the two games do not share a group prefix");

    // No idempotence by design: the accessor applies this once per group
    // name. Pinned because a double application would silently address a
    // fifth namespace (world/single/world/single/player) that nothing
    // writes.
    check(mhw::panelSettingsScopeFor(mhw::GameId::World,
                                     mhw::PlayerSection::PlayerLayoutMode::Single,
                                     QStringLiteral("world/single/player"))
              == QStringLiteral("world/single/world/single/player"),
          "the prefix is applied exactly once by its caller, not folded in");

    // ==================================================================
    // 2. THE INJECTION — a launch-time fact, defaulted, overridable
    // ==================================================================
    check(mhw::panelSettingsGame() == mhw::GameId::World,
          "the game defaults to World (the game the un-prefixed legacy "
          "sections belong to)");

    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        check(writeRaw(path, QStringLiteral("[player]\nscale=2\nml=7\n\n"
                                            "[world/player]\nscale=1\n\n"
                                            "[rise/player]\nscale=5\n\n")),
              "seed a pre-v0.13 section alongside both games' sections");

        QSettings s = openSettings(path);

        // The file is the contract: the two games address the SAME file.
        check(hasGroup(s, QStringLiteral("player"))
                  && hasGroup(s, QStringLiteral("world/player"))
                  && hasGroup(s, QStringLiteral("rise/player")),
              "all three group spellings coexist in one panels.ini");

        // A World-scoped read must not see Rise's value, and vice versa.
        check(readKey(s, QStringLiteral("world/player"),
                      QStringLiteral("scale")) == QStringLiteral("1"),
              "World reads its own scale");
        check(readKey(s, QStringLiteral("rise/player"),
                      QStringLiteral("scale")) == QStringLiteral("5"),
              "Rise reads its own scale");
        check(s.value(QStringLiteral("world/player/scale")).toString()
                  != QStringLiteral("5"),
              "World does not resolve Rise's scale through its own path");
        check(s.value(QStringLiteral("rise/player/scale")).toString()
                  != QStringLiteral("1"),
              "Rise does not resolve World's scale through its own path");
    }

    // ==================================================================
    // 3. THE MIGRATION — a legacy section becomes THIS game's section
    // ==================================================================
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        check(writeRaw(path, QStringLiteral("[player]\n"
                                            "scale=2\nml=7\nminW=48\n\n"
                                            "[player-wirebug]\n"
                                            "scale=1\ncolumns=3\n\n")),
              "seed a pre-v0.13 file with two flat sections");

        QSettings s = openSettings(path);

        // World reads. Every key is copied verbatim, not reinterpreted.
        check(mhw::migrateLegacyPanelSection(s, mhw::PanelSettingsScope{mhw::GameId::World, mhw::PlayerSection::PlayerLayoutMode::Single},
                                    QStringLiteral("player")),
              "the migration reports it acted");
        s.sync();
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("2"),
              "the legacy scale lands in world/player");
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("ml")) == QStringLiteral("7"),
              "the legacy margin lands in world/player");
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("minW")) == QStringLiteral("48"),
              "the legacy size floor lands in world/player");

        // And the bare section is gone: that removal is what makes the
        // migration a one-way door rather than a mirror.
        check(!hasGroup(s, QStringLiteral("player")),
              "the legacy bare section is removed after the copy");

        // The marker is what makes the second call a no-op. It is a KEY
        // ("player") inside the group "world/legacy", so contains() on the
        // flattened path is the right question — the older spelling of
        // this check asked for a sub-key that does not exist.
        check(readKey(s, QStringLiteral("world/legacy"),
                      QStringLiteral("player")) == QStringLiteral("1")
                  || s.contains(QStringLiteral("world/legacy/player")),
              "the game carries the legacyMigrated marker");

        // The OTHER window's section is untouched by THIS call: the
        // migration is per-section, not a file-wide rewrite.
        check(hasGroup(s, QStringLiteral("player-wirebug")),
              "a different window's legacy section is left alone by this "
              "migration");
    }

    // ==================================================================
    // 4. THE OTHER GAME IS NEVER TOUCHED — the guarantee that matters
    // ==================================================================
    // A user who has run both games has [world/...] and [rise/...]
    // sections plus possibly a legacy [player] still on disk. Migrating
    // under World must not rewrite, remove, or even read Rise's.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        // v0.14: Rise's OWN namespace is now four-domain, so its
        // two-domain `[rise/player]` spelling is itself a legacy section
        // awaiting the same fold. Seeding it here would make the World
        // run fold RISE's legacy section — exactly the cross-game
        // clobber this section exists to forbid — because the
        // two-domain spelling carries no mode segment to tell it apart.
        // So the seed uses rise/single, which is what a Rise session
        // writes today.
        check(writeRaw(path, QStringLiteral("[player]\nscale=9\n\n"
                                            "[rise/single/player]\n"
                                            "scale=5\nml=700\n\n"
                                            "[rise/single/player-status]\n"
                                            "columns=2\n\n")),
              "seed a legacy section plus a fully-populated Rise "
              "namespace");

        QSettings s = openSettings(path);
        // Kept: the comparison below needs the "before" text to prove the
        // migration actually moved bytes rather than passing vacuously.
        const QString riseBefore = slurp(path);
        check(mhw::migrateLegacyPanelSection(s, mhw::PanelSettingsScope{mhw::GameId::World, mhw::PlayerSection::PlayerLayoutMode::Single},
                                    QStringLiteral("player")),
              "the World migration acts on the legacy section");
        s.sync();

        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("9"),
              "the World migration reads the legacy value into "
              "world/single/player");

        // Rise's sections must survive unchanged. This is the whole point
        // of the feature: two games, two layouts, one file.
        //
        // Asserted through the settings API rather than the raw text: the
        // nested group is on disk as `[rise]` + escaped subkeys, not as
        // a literal `[rise/single/player]` header, so a text match on the
        // INI spelling would fail on a file that is perfectly correct.
        check(readKey(s, QStringLiteral("rise/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("5"),
              "Rise's own scale survives the World migration intact");
        check(readKey(s, QStringLiteral("rise/single/player"),
                      QStringLiteral("ml")) == QStringLiteral("700"),
              "Rise's own margin survives the World migration intact");
        check(readKey(s, QStringLiteral("rise/single/player-status"),
                      QStringLiteral("columns")) == QStringLiteral("2"),
              "Rise's other window survives the World migration intact");
        check(riseBefore != slurp(path),
              "the file did change (so the checks above are not vacuous)");

        // Now migrate under RISE. It must NOT re-read [player] (which
        // World already consumed) and must NOT clobber rise/single/player.
        const bool riseMigrated =
            mhw::migrateLegacyPanelSection(s, mhw::PanelSettingsScope{mhw::GameId::Rise, mhw::PlayerSection::PlayerLayoutMode::Single},
                                    QStringLiteral("player"));
        s.sync();
        check(!riseMigrated,
              "Rise finds nothing to migrate once World has consumed the "
              "legacy section");
        check(readKey(s, QStringLiteral("rise/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("5"),
              "Rise's own section keeps its user-edited scale");
    }

    // ==================================================================
    // 5. ALREADY-SCOPED MEANS AUTHORITATIVE — no silent overwrite
    // ==================================================================
    // The dangerous version of this feature is a migration that copies
    // over an existing [world/player]. It would destroy the layout the
    // user edited for this game since migrating.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        // v0.14: the "already scoped" case is now the FOUR-domain
        // spelling. `[world/player]` is itself a legacy spelling (the
        // v0.14 target of the two-domain fold), so seeding it here would
        // assert the opposite of what the migration should do.
        check(writeRaw(path, QStringLiteral("[player]\nscale=9\n\n"
                                            "[world/single/player]\n"
                                            "scale=2\nml=11\n\n")),
              "seed a file with BOTH a legacy section and a "
              "current-scope section");

        QSettings s = openSettings(path);
        const bool acted =
            mhw::migrateLegacyPanelSection(s, mhw::PanelSettingsScope{mhw::GameId::World, mhw::PlayerSection::PlayerLayoutMode::Single},
                                    QStringLiteral("player"));
        s.sync();

        check(!acted,
              "a game that already has its own section does not migrate");
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("2"),
              "the current game's own scale is not replaced by the legacy "
              "one");
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("ml")) == QStringLiteral("11"),
              "the current game's own margin is not replaced by the legacy "
              "one");
        check(hasGroup(s, QStringLiteral("player")),
              "the legacy section is still present (the guard is what "
              "skips it)");
    }

    // ==================================================================
    // 6. IDEMPOTENCE — a second run cannot undo the first
    // ==================================================================
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        check(writeRaw(path, QStringLiteral("[player]\nscale=3\n\n")),
              "seed one legacy section");

        QSettings s = openSettings(path);
        check(mhw::migrateLegacyPanelSection(s, mhw::PanelSettingsScope{mhw::GameId::World, mhw::PlayerSection::PlayerLayoutMode::Single},
                                    QStringLiteral("player")),
              "first run migrates");
        s.sync();

        // The user now edits the migrated section, as they are entitled
        // to. A second run must leave it alone.
        //
        // v0.14: the edited group is the FOUR-domain spelling, which is
        // where the first run actually put the section.
        {
            QSettings edit = openSettings(path);
            edit.beginGroup(QStringLiteral("world/single/player"));
            edit.setValue(QStringLiteral("scale"), 8);
            edit.endGroup();
            edit.sync();
        }

        const bool second =
            mhw::migrateLegacyPanelSection(s, mhw::PanelSettingsScope{mhw::GameId::World, mhw::PlayerSection::PlayerLayoutMode::Single},
                                    QStringLiteral("player"));
        s.sync();
        check(!second, "a second run reports no migration");
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("8"),
              "a second run does not re-import over the user's edit");
    }

    // ==================================================================
    // 7. NOTHING TO MIGRATE IS NOT AN ERROR
    // ==================================================================
    // A fresh install, or a file written entirely under v0.14. The
    // migration must answer "no" and leave the file untouched, so a
    // first run cannot manufacture a legacy section to migrate.
    //
    // v0.14: "written entirely under v0.13" is no longer a no-migration
    // case — a `[world/player]` file is exactly what this version folds
    // (that fold is tested in panel_settings_scope_tests.cpp section 3a).
    // What IS a no-migration case is a file already in the four-domain
    // spelling with nothing under it, and that is what is seeded here.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        check(writeRaw(path, QStringLiteral("[world/single/player]\n"
                                            "scale=1\n\n"
                                            "[rise/single/player]\n"
                                            "scale=5\n\n")),
              "seed a fully v0.14 file (four domains, no legacy "
              "sections)");

        QSettings s = openSettings(path);
        const QString before = slurp(path);
        check(!mhw::migrateLegacyPanelSection(s, mhw::PanelSettingsScope{mhw::GameId::World, mhw::PlayerSection::PlayerLayoutMode::Single},
                                    QStringLiteral("player")),
              "a file with no legacy section reports no migration");
        check(!mhw::migrateLegacyPanelSection(s, mhw::PanelSettingsScope{mhw::GameId::Rise, mhw::PlayerSection::PlayerLayoutMode::Single},
                                    QStringLiteral("player")),
              "same under Rise");
        s.sync();
        check(slurp(path) == before,
              "a no-op migration leaves the file byte-identical");

        // An EMPTY legacy section is not a migration either: an empty
        // [player] with nothing in it would otherwise create an empty
        // [world/single/player] that shadows nothing and means nothing.
        check(writeRaw(path, QStringLiteral("[player]\n\n"
                                            "[world/single/player]\n"
                                            "scale=1\n\n")),
              "seed an empty legacy section beside a real one");
        QSettings s2 = openSettings(path);
        check(!mhw::migrateLegacyPanelSection(s2, mhw::PanelSettingsScope{mhw::GameId::World, mhw::PlayerSection::PlayerLayoutMode::Single},
                                    QStringLiteral("nope")),
              "a key with no section of either spelling reports no "
              "migration");
    }

    // ==================================================================
    // 8. THE SEPARATOR — '/' survives the write/read cycle
    // ==================================================================
    // QSettings treats '[' and ']' as group delimiters in an INI path
    // form, and a group name containing them would not round-trip. The
    // prefix is the one novel spelling in this change, so it is pinned.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        const QString group = mhw::panelSettingsScopeFor(
            mhw::GameId::World, mhw::PlayerSection::PlayerLayoutMode::Single,
            QStringLiteral("player"));
        {
            QSettings w = openSettings(path);
            w.beginGroup(group);
            w.setValue(QStringLiteral("scale"), 1);
            w.endGroup();
            w.sync();
        }
        QSettings r = openSettings(path);
        check(hasGroup(r, group),
              "the prefixed group name round-trips through QSettings");
        // Not a literal `[world/player]` header: QSettings writes the
        // nested path as `[world]` plus backslash-escaped subkeys, so a
        // match on the pretty spelling would fail on a correct file.
        // What is pinned instead is the escape: the '/' does not survive
        // as text, and a group name that round-tripped as two levels
        // would be a different namespace.
        check(slurp(path).contains(QStringLiteral("player\\scale=1")),
              "the nested path is stored escaped under [world]");
        check(readKey(r, QStringLiteral("world/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("1"),
              "a key under the prefixed group reads back through a "
              "different handle");
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    std::printf("%s\n", failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
