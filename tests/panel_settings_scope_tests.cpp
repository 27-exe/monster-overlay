// SPDX-License-Identifier: Apache-2.0
//
// v0.14: the GAME × MODE layout namespace, and the split's mother panels.
//
// WHAT IS UNDER TEST
// ------------------
// Two things, one user report each:
//
//   1. panel_settings_game.h — the four-domain group a Panel's settings
//      section lives under (`[world/single/player-status]` …), the
//      launch-time injection of BOTH axes behind it, and the one-time
//      migration of the two legacy spellings into SINGLE.
//
//   2. The split's mother panels — PlayerPanel / MonsterPanel /
//      DamagePanel must not mount an empty chrome frame in split mode.
//
// WHY THIS MATTERS
// ----------------
// The user reported both defects as one sentence: 「分块和合并疑似是用的
// 同一个配置文件？这不可取，双方布局会互相影响，分开。应该一共四个，世界的
// 分块/整体，崛起的分块/总体」 and 「分块模式下有一个纯空的只显示player的面板」.
//
// Defect 1's mechanism: the namespace had ONE dimension (the game) while
// a window's geometry is a fact about the game AND the topology that
// produced it. Under the one-axis spelling the user's `[rise/monster]`
// held mt=212 mr=1130 — a single-mode position for the large panel —
// beside `[rise/player-bars] ml=2260`, a split-mode position for a
// window that only exists in split. Neither could be read back
// correctly because the address was missing the axis that says which
// one it was.
//
// Defect 2's mechanism: four `setPanelEnabled(!parser.isSet(...))` lines
// that knew nothing about the layout mode. In split, every PlayerSection
// bit had left PlayerPanel (maskPlayerForPanel == 0) and every
// MonsterSection and DamageSection bit but Tenderize had left theirs —
// yet the panels were still enabled, so show() ran and Panel::setVisible()
// let it through: a window with a PLAYER title, a frame, and no blocks.
//
// The migration tests are state machines over real INI files on disk
// (QTemporaryDir, XDG_CONFIG_HOME redirected) because the failure that
// matters — a second run re-copying over an edited section — is a
// property of the file BETWEEN runs, not of one call.

#include "ui/panel.h"
#include "ui/panel_damage.h"
#include "ui/panel_monster.h"
#include "ui/panel_player.h"
#include "ui/panel_settings_game.h"

#include <QApplication>
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

int checks = 0;
int failures = 0;

void check(bool cond, const char *what, const QString &detail = QString())
{
    ++checks;
    if (cond) {
        std::printf("PASS: %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL: %s", what);
        if (!detail.isEmpty())
            std::fprintf(stderr, " -- %s", qPrintable(detail));
        std::fprintf(stderr, "\n");
        ++failures;
    }
    std::fflush(stderr);
    std::fflush(stdout);
}

// QSettings resolves (IniFormat, UserScope, "monster-overlay", "panels")
// to $XDG_CONFIG_HOME/monster-overlay/panels.ini. Redirect the HOME
// tree so nothing here can ever touch the developer's real config —
// the same discipline tests/panel_settings_game_scoping_tests.cpp and
// tests/console_split_sync_tests.cpp follow.
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
// so a handle opened before a write would not see a later one — which
// is exactly what "the next run" means here, and the same property the
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

QString modeSeg(mhw::PlayerLayoutMode m)
{
    return mhw::panelLayoutModeSegment(m);
}

// Restores both globals to a known (game, mode) pair and answers what
// every scope read in this file resolves to. The globals are
// process-wide, so a test that leaves one behind would let the NEXT
// test's assertions pass vacuously off the previous test's setup.
void setScope(mhw::GameId game, mhw::PlayerLayoutMode mode)
{
    mhw::setPanelSettingsGame(game);
    mhw::setPanelSettingsLayout(mode);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // ==================================================================
    // 1. THE SCOPE PREFIX — four domains, spelled from two axes
    // ==================================================================
    check(mhw::panelSettingsScopeFor(mhw::GameId::World,
                                     mhw::PlayerLayoutMode::Single,
                                     QStringLiteral("player-status"))
              == QStringLiteral("world/single/player-status"),
          "World single player-status scopes to world/single/player-status");
    check(mhw::panelSettingsScopeFor(mhw::GameId::World,
                                     mhw::PlayerLayoutMode::Split,
                                     QStringLiteral("player-status"))
              == QStringLiteral("world/split/player-status"),
          "World split player-status scopes to world/split/player-status");
    check(mhw::panelSettingsScopeFor(mhw::GameId::Rise,
                                     mhw::PlayerLayoutMode::Single,
                                     QStringLiteral("player-status"))
              == QStringLiteral("rise/single/player-status"),
          "Rise single player-status scopes to rise/single/player-status");
    check(mhw::panelSettingsScopeFor(mhw::GameId::Rise,
                                     mhw::PlayerLayoutMode::Split,
                                     QStringLiteral("player-status"))
              == QStringLiteral("rise/split/player-status"),
          "Rise split player-status scopes to rise/split/player-status");

    // The whole claim, in the fewest assertions: one game's two modes
    // are different domains, and one mode's two games are different
    // domains. Nothing crosses.
    const auto W_S = mhw::PlayerLayoutMode::Single;
    const auto W_SP = mhw::PlayerLayoutMode::Split;
    check(mhw::panelSettingsScopeFor(mhw::GameId::World, W_S,
                                     QStringLiteral("player"))
              != mhw::panelSettingsScopeFor(mhw::GameId::World, W_SP,
                                            QStringLiteral("player")),
          "one game's single and split are different domains");
    check(mhw::panelSettingsScopeFor(mhw::GameId::World, W_SP,
                                     QStringLiteral("player"))
              != mhw::panelSettingsScopeFor(mhw::GameId::Rise, W_SP,
                                            QStringLiteral("player")),
          "one mode's world and rise are different domains");

    // The mode segment is the same spelling --layout= parses, so a
    // hand-edited [world/split/...] and what the code writes agree.
    check(modeSeg(W_S) == QStringLiteral("single")
              && modeSeg(W_SP) == QStringLiteral("split"),
          "the mode segments are 'single' and 'split', the spellings "
          "--layout= accepts");

    // No idempotence by design: the accessor applies this once per group
    // name. Pinned because a double application would silently address a
    // fifth namespace (world/single/single/player) that nothing writes.
    check(mhw::panelSettingsScopeFor(mhw::GameId::World, W_S,
                                     QStringLiteral("world/single/player"))
              == QStringLiteral("world/single/world/single/player"),
          "the prefix is applied exactly once by its caller, not folded in");

    // ==================================================================
    // 2. THE INJECTION — two launch-time facts, defaulted, overridable
    // ==================================================================
    check(mhw::panelSettingsGame() == mhw::GameId::World
              && mhw::panelSettingsLayout() == mhw::PlayerLayoutMode::Single,
          "the scope defaults to World/single (the domain the legacy "
          "sections belong to)");

    // THE SAME WINDOW, TWO AXIS SETTINGS, TWO DIFFERENT GROUPS. This is
    // the user's 「双方布局会互相影响」 read back: identical settingsKey,
    // identical game, different mode, and the resolved group differs.
    //
    // THE MUTATION FOR THIS SECTION. Revert the scope to the one-axis
    // spelling (game only) and this test FAILS: every Panel in the
    // process would then resolve "player-status" to the same group in
    // both modes, and `scopeGroup(W_SP)` would come back
    // "world/player-status" — the exact value the two-domain namespace
    // wrote, which is what let the split overwrite the single.
    {
        setScope(mhw::GameId::World, W_S);
        const QString singleGroup = mhw::panelSettingsScopeGroup(
            QStringLiteral("player-status"));
        setScope(mhw::GameId::World, W_SP);
        const QString splitGroup = mhw::panelSettingsScopeGroup(
            QStringLiteral("player-status"));
        check(singleGroup != splitGroup,
              "the same window resolves to a DIFFERENT group in single and "
              "in split under the same game",
              QStringLiteral("single=%1 split=%2")
                  .arg(singleGroup, splitGroup));
    }

    // The other axis, same shape: same key, same mode, other game.
    {
        setScope(mhw::GameId::Rise, W_SP);
        const QString riseSplit = mhw::panelSettingsScopeGroup(
            QStringLiteral("player-bars"));
        setScope(mhw::GameId::World, W_SP);
        const QString worldSplit = mhw::panelSettingsScopeGroup(
            QStringLiteral("player-bars"));
        check(riseSplit != worldSplit,
              "the same window resolves to a DIFFERENT group in rise and in "
              "world under the same mode",
              QStringLiteral("rise=%1 world=%2").arg(riseSplit, worldSplit));
    }

    // ==================================================================
    // 3. THE MIGRATION — a legacy section becomes THIS scope's section
    // ==================================================================
    // 3a. A pre-v0.14 (two-domain) file: [world/player] folds into
    // [world/single/player] because single was the only topology that
    // existed when it was written. This is the user's live file.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        check(writeRaw(path, QStringLiteral("[world/player]\n"
                                            "scale=2\nml=7\nminW=48\n\n"
                                            "[world/player-lag]\n"
                                            "scale=5\n\n"
                                            "[rise/player]\n"
                                            "scale=9\n")),
              "seed a pre-v0.14 file: two domains, flat panel keys");

        setScope(mhw::GameId::World, W_S);
        QSettings s = openSettings(path);

        check(mhw::migrateLegacyPanelSection(
                  s, mhw::PanelSettingsScope{mhw::GameId::World, W_S},
                  QStringLiteral("player")),
              "the v0.13 -> v0.14 migration reports it acted");
        s.sync();

        // Keys copied VERBATIM, not reinterpreted.
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("2"),
              "the two-domain scale lands in world/single/player");
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("ml")) == QStringLiteral("7"),
              "the two-domain margin lands in world/single/player");
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("minW")) == QStringLiteral("48"),
              "the two-domain size floor lands in world/single/player");

        // The two-domain spelling is gone: that removal is what makes the
        // migration a one-way door rather than a mirror.
        check(!hasGroup(s, QStringLiteral("world/player")),
              "the two-domain spelling is removed after the copy");

        // The marker is what makes a second call a no-op, and it sits at
        // the GAME level (world/legacy/player), not inside the migrated
        // section — it is migration bookkeeping, not panel state.
        //
        // THE SPELLING IS THE POINT. The marker is a KEY named "player"
        // inside the group "world/legacy", not a key inside a group
        // "world/legacy/player". A probe settles it: contains("world/
        // legacy/player/legacyMigrated") is FALSE on the file the
        // migration writes, because QSettings flattens
        // legacyMarkerKey() into the literal path world/legacy/player.
        // Writing one and reading it back as the other would leave the
        // idempotence guard permanently false, which is the failure mode
        // this whole marker exists to prevent.
        check(s.contains(QStringLiteral("world/legacy/player")),
              "the game carries the legacyMigrated marker");
        check(s.value(QStringLiteral("world/legacy/player")).toInt() == 1,
              "and the marker's value is 1");

        // THE OTHER WINDOW'S section is untouched by THIS call: the
        // migration is per-section, not a file-wide rewrite.
        check(hasGroup(s, QStringLiteral("world/player-lag")),
              "a different window's legacy section is left alone by this "
              "migration");

        // AND THE OTHER GAME's. This is the guarantee that made the
        // v0.13 migration safe and the v0.14 one has to keep.
        check(readKey(s, QStringLiteral("rise/player"),
                      QStringLiteral("scale")) == QStringLiteral("9"),
              "the other game's two-domain section is untouched");
    }

    // 3b. A fully flat pre-v0.13 file: [player] folds into
    // [world/single/player] too. Same destination, because the
    // single-mode reading is the same for both spellings — the game
    // axis was the only one that existed in v0.13, and the mode axis did
    // not exist at all.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        check(writeRaw(path, QStringLiteral("[player]\n"
                                            "scale=2\nml=7\n\n"
                                            "[player-wirebug]\n"
                                            "scale=1\ncolumns=3\n\n")),
              "seed a pre-v0.13 file with flat sections");

        setScope(mhw::GameId::World, W_S);
        QSettings s = openSettings(path);

        check(mhw::migrateLegacyPanelSection(
                  s, mhw::PanelSettingsScope{mhw::GameId::World, W_S},
                  QStringLiteral("player")),
              "the flat migration reports it acted");
        s.sync();
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("2"),
              "the flat scale lands in world/single/player");
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("ml")) == QStringLiteral("7"),
              "the flat margin lands in world/single/player");
        check(!hasGroup(s, QStringLiteral("player")),
              "the flat bare section is removed after the copy");
        check(hasGroup(s, QStringLiteral("player-wirebug")),
              "a different window's flat section is left alone");
    }

    // 3c. THE OTHER DOMAINS ARE NEVER TOUCHED, in either direction.
    // A user who has run both games in both modes has four populated
    // domains plus possibly a legacy spelling still on disk. Migrating
    // under World/single must not read, rewrite or remove any of the
    // other three.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        check(writeRaw(path, QStringLiteral("[world/player]\n"
                                            "scale=9\n\n"
                                            "[world/split/player]\n"
                                            "scale=3\n\n"
                                            "[rise/single/player]\n"
                                            "scale=5\nml=700\n\n"
                                            "[rise/split/player-bars]\n"
                                            "columns=2\n\n")),
              "seed a file with the other three domains populated");

        setScope(mhw::GameId::World, W_S);
        QSettings s = openSettings(path);
        const QString before = slurp(path);
        check(mhw::migrateLegacyPanelSection(
                  s, mhw::PanelSettingsScope{mhw::GameId::World, W_S},
                  QStringLiteral("player")),
              "the World/single migration acts on the legacy section");
        s.sync();

        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("9"),
              "the legacy section lands in the current scope");
        check(readKey(s, QStringLiteral("world/split/player"),
                      QStringLiteral("scale")) == QStringLiteral("3"),
              "the SAME game's split domain is untouched");
        check(readKey(s, QStringLiteral("rise/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("5"),
              "the SAME mode's rise domain is untouched");
        check(readKey(s, QStringLiteral("rise/split/player-bars"),
                      QStringLiteral("columns")) == QStringLiteral("2"),
              "the fourth domain is untouched");
        // And the file changed in exactly the way the copy predicts.
        //
        // THE ON-DISK SPELLING IS NOT THE LOGICAL SPELLING, and that is
        // a QSettings property the test has to know about rather than a
        // detail of the migration: a nested group is written FLATTENED
        // under its top-level parent, so `[world/single/player]` appears
        // in the file as a `single\player\scale=...` line inside a single
        // `[world]` section. A probe settles it (the same one that found
        // the marker's shape). Asserting on the literal `[world/single/
        // player]` header would fail on a perfectly correct file, so the
        // check below is on the flattened key, which is what allKeys()
        // reports too.
        const QString after = slurp(path);
        check(after.contains(QStringLiteral("single\\player\\scale=9")),
              "the migrated-to section is on disk (flattened, as QSettings "
              "writes nested groups)");
        // THE CHECK MUST BE ON THE LINE, NOT ON A SUBSTRING. A bare
        // `contains("player\\scale=9")` is TRUE on the migrated file,
        // because that string is a suffix of the migrated line
        // `single\player\scale=9` — the assertion would pass while
        // proving nothing, which is the worst possible outcome for a
        // guard. Line-start matching is the question actually being
        // asked, and it is asked once, not once per line (a per-line
        // check would inflate the pass count by one for every blank line
        // the migration left behind).
        {
            bool anyLegacyLine = false;
            for (const QString &line : after.split(QLatin1Char('\n'))) {
                if (line.startsWith(QStringLiteral("player\\scale=")))
                    anyLegacyLine = true;
            }
            check(!anyLegacyLine,
                  "the two-domain spelling is gone from disk");
        }
        check(before != after, "the migration actually moved bytes");
    }

    // 3d. A SECTION THAT ALREADY EXISTS IN THE CURRENT SCOPE IS
    // AUTHORITATIVE. This is the guarantee a whole-file rewrite would
    // break: it would copy [player] over an edited [world/single/player]
    // and silently destroy the layout the user has since changed.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        check(writeRaw(path, QStringLiteral("[player]\n"
                                            "scale=2\n\n"
                                            "[world/single/player]\n"
                                            "scale=99\n\n"
                                            "[world/split/player]\n"
                                            "scale=3\n\n"
                                            "[rise/single/player]\n"
                                            "scale=5\n\n")),
              "seed a legacy section alongside all three other domains");

        setScope(mhw::GameId::World, W_S);
        QSettings s = openSettings(path);
        check(!mhw::migrateLegacyPanelSection(
                  s, mhw::PanelSettingsScope{mhw::GameId::World, W_S},
                  QStringLiteral("player")),
              "an already-scoped section is NOT migrated (and reports it)");
        s.sync();
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("99"),
              "the scoped section keeps the user's edited value");
    }

    // 3e. IDEMPOTENCE. A second call is a no-op because the marker is
    // what a second process (and a second panel in the same one) reads.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        check(writeRaw(path, QStringLiteral("[player]\nscale=2\nml=7\n\n")),
              "seed one flat section");

        setScope(mhw::GameId::World, W_S);
        QSettings s = openSettings(path);
        check(mhw::migrateLegacyPanelSection(
                  s, mhw::PanelSettingsScope{mhw::GameId::World, W_S},
                  QStringLiteral("player")),
              "the first call migrates");
        s.sync();
        // Edit the migrated section the way the user would after
        // migrating, so a re-copy would be visible as data loss.
        s.setValue(QStringLiteral("world/single/player/scale"), 42);
        s.sync();
        check(!mhw::migrateLegacyPanelSection(
                  s, mhw::PanelSettingsScope{mhw::GameId::World, W_S},
                  QStringLiteral("player")),
              "the second call is a no-op");
        s.sync();
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("42"),
              "the user's post-migration edit survives the second call");
    }

    // 3f. A SPLIT-MODE RUN NEVER MIGRATES. This is the guard that keeps
    // the migration lossless, and its mechanism is the one thing in this
    // file that could actually destroy the user's layout:
    //
    // Both legacy spellings predate the split, so every legacy section
    // is a SINGLE-mode section. In split mode the four mother panels
    // are still constructed (re-parented, not removed), so their
    // loadConfig() runs — and if it migrated, it would fold [player]
    // into [world/split/player] and DELETE the source. The next
    // single-mode run would then have nothing to migrate and would hand
    // PlayerPanel factory defaults. The user's position, gone.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        check(writeRaw(path, QStringLiteral("[player]\n"
                                            "scale=2\n\n"
                                            "[world/player]\n"
                                            "scale=11\n\n")),
              "seed both legacy spellings");

        setScope(mhw::GameId::World, W_SP);
        QSettings s = openSettings(path);
        check(!mhw::migrateLegacyPanelSection(
                  s, mhw::PanelSettingsScope{mhw::GameId::World, W_SP},
                  QStringLiteral("player")),
              "a split-mode run does not migrate, whatever is on disk");
        s.sync();
        check(!hasGroup(s, QStringLiteral("world/split/player")),
              "the split domain stays empty rather than stealing the "
              "single layout");
        check(readKey(s, QStringLiteral("world/player"),
                      QStringLiteral("scale")) == QStringLiteral("11"),
              "and the legacy section is NOT removed — the single run "
              "still has something to migrate");
        check(hasGroup(s, QStringLiteral("player")),
              "and neither legacy spelling is touched");
    }

    // 3g. THE DOMAINS DO NOT LEAK INTO EACH OTHER, in the one ordering
    // that could leak. A single-mode World run must fold [world/player]
    // into [world/single/player] while leaving [rise/single/player]
    // alone — even though the two share the mode segment. This is the
    // guarantee that makes the four domains four independent histories
    // rather than one history with four addresses.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        check(writeRaw(path, QStringLiteral("[world/player]\n"
                                            "scale=9\n\n"
                                            "[rise/player]\n"
                                            "scale=5\n\n")),
              "seed the two-domain spelling for both games");

        setScope(mhw::GameId::World, W_S);
        QSettings s = openSettings(path);
        check(mhw::migrateLegacyPanelSection(
                  s, mhw::PanelSettingsScope{mhw::GameId::World, W_S},
                  QStringLiteral("player")),
              "the World/single migration acts");
        s.sync();
        check(readKey(s, QStringLiteral("world/single/player"),
                      QStringLiteral("scale")) == QStringLiteral("9"),
              "World's legacy section lands in World/single");
        check(readKey(s, QStringLiteral("rise/player"),
                      QStringLiteral("scale")) == QStringLiteral("5"),
              "Rise's two-domain section is untouched");
        check(!hasGroup(s, QStringLiteral("rise/single/player")),
              "and no Rise domain was created by the World migration");
    }

    // 3h. A FILE WITH NOTHING IN IT, and with nothing relevant to it.
    // Both are no-ops by construction; pinned so a future rewrite that
    // answers "yes" on an empty file cannot ship.
    {
        QTemporaryDir home;
        if (!home.isValid()) {
            std::fprintf(stderr, "FATAL: no temp dir\n");
            return 2;
        }
        qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
        const QString path = iniPath(home);

        check(writeRaw(path, QStringLiteral("[rise/single/player]\n"
                                            "scale=5\n\n")),
              "seed a file with only another domain's section");
        setScope(mhw::GameId::World, W_S);
        QSettings s = openSettings(path);
        check(!mhw::migrateLegacyPanelSection(
                  s, mhw::PanelSettingsScope{mhw::GameId::World, W_S},
                  QStringLiteral("player")),
              "a file with no legacy section at all migrates nothing");
        s.sync();
        check(!hasGroup(s, QStringLiteral("world/single/player")),
              "and nothing is created");

        // An empty legacy section is not a migration either.
        check(writeRaw(path, QStringLiteral("[player]\n\n")),
              "seed an EMPTY legacy section");
        QSettings s2 = openSettings(path);
        check(!mhw::migrateLegacyPanelSection(
                  s2, mhw::PanelSettingsScope{mhw::GameId::World, W_S},
                  QStringLiteral("player")),
              "an empty legacy section is not a migration");
        s2.sync();
    }

    // ==================================================================
    // 4. THE SPLIT'S MOTHER PANELS — no empty chrome frame
    // ==================================================================
    // THE CONTRACT, stated once: in split mode a mother panel mounts iff
    // it still owns a section block. PlayerPanel owns none (every
    // PlayerSection bit is claimed by a row of kPlayerSplitTargets);
    // MonsterPanel is left with Tenderize and DamagePanel with
    // OtherMembers — and NEITHER IS A BLOCK. Tenderize is a strip inside
    // each .pc card (pcHasTenderize()), and OtherMembers is the roster
    // filter, which panel_damage.cpp's gates never read. So all three
    // mothers end up with nothing to draw, and the predicates that say so
    // are mhw::playerPanelHasBlocks / monsterPanelHasBlocks /
    // damagePanelHasBlocks (panel_sections.h).
    //
    // v0.16 update: DamagePanel used to be described as owning
    // "Rows+OtherMembers". Rows is claimed by the damage-rows split
    // window now, so only OtherMembers is left — and it is a filter, not
    // a block, which is what makes the damage frame disappear too. The
    // mask FACT this file asserts is unchanged; only the prose was stale.
    //
    // THE MUTATION FOR THIS SECTION. Delete the split arm of the
    // setPanelEnabled() gate at src/main.cpp and this whole block FAILS:
    //   * PlayerPanel stays enabled with a zero mask, show() runs,
    //     Panel::setVisible()'s guard passes (it is enabled) and the
    //     surface mounts — a bare PLAYER frame with no blocks, which is
    //     exactly the panel the user screenshotted.
    //   * MonsterPanel and DamagePanel likewise mount an empty frame
    //     whenever the user has every one of their remaining sections
    //     switched off.
    // Compiling this section does NOT require main() (the gate is a pure
    // expression over two globals), so the mutation is checked by hand:
    // with the split arm gone, the assertion below about PlayerPanel
    // cannot pass, because the value it reads would be true.
    {
        // Scope first: the panels read their group in their constructors.
        setScope(mhw::GameId::World, W_SP);

        // The three mother panels that the split empties or partly
        // empties, plus the one it does not.
        PlayerPanel player;
        MonsterPanel monster;
        DamagePanel damage;

        // THE MASK SIDE, not a guess: what main.cpp decides each panel's
        // derived mask is in split mode, read from the same tables the
        // gate uses. The input is kAll — every section the schema knows
        // about — because that is the column the tables' own
        // static_asserts are stated against, and it is the value that
        // exposes "what does the split leave this panel".
        const uint32_t playerMaskInSplit =
            mhw::PlayerSection::playerPanelMaskFor(W_SP,
                                                   mhw::PlayerSection::kAll);
        const uint32_t monsterMaskInSplit =
            mhw::monsterPanelMaskFor(W_SP, mhw::MonsterSection::kAll);
        const uint32_t damageMaskInSplit =
            mhw::damagePanelMaskFor(W_SP, mhw::DamageSection::kAll);

        check(playerMaskInSplit == 0,
              "in split, PlayerPanel's derived mask is ZERO — every "
              "PlayerSection bit left for a window of its own",
              QStringLiteral("mask=%1").arg(playerMaskInSplit));
        check(monsterMaskInSplit == mhw::MonsterSection::Tenderize,
              "in split, MonsterPanel keeps exactly Tenderize — the one "
              "block a split table cannot claim",
              QStringLiteral("mask=%1 want=%2")
                  .arg(monsterMaskInSplit)
                  .arg(static_cast<uint64_t>(mhw::MonsterSection::Tenderize)));
        // v0.15: OtherMembers alone. Rows joined the split table when the
        // roster became a window of its own — the console had been
        // building that window while the live overlay kept drawing the
        // rows inside DamagePanel, so the two disagreed and the user saw
        // "the per-player part is gone" from one side and a duplicate from
        // the other.
        //
        // OtherMembers is deliberately NOT claimed by any window: it is the
        // roster FILTER, and its per-row rendering belongs to Rows.
        check(damageMaskInSplit == mhw::DamageSection::OtherMembers,
              "in split, DamagePanel keeps only OtherMembers — Share, Chart "
              "and Rows all left for their own windows",
              QStringLiteral("mask=%1 want=%2")
                  .arg(damageMaskInSplit)
                  .arg(static_cast<uint64_t>(mhw::DamageSection::OtherMembers)));

        // THE GATE ITSELF, reproduced as the expression main.cpp applies
        // (mode => require a non-zero derived mask) so the panels' own
        // panelEnabled() can be asserted against it. This is the value
        // the user's screenshot contradicted.
        // v0.16: `derived != 0` is no longer the gate. A mother panel
        // mounts iff it still owns a CONTENT BLOCK, and two of the three
        // survivors are not blocks: Tenderize is a strip inside each .pc
        // card (pcHasTenderize()), OtherMembers is the roster filter that
        // panel_damage.cpp's gates never read. So all three derived masks
        // are non-zero and all three panels are DISABLED — which is the
        // whole point of v0.16, and the reason the one-line window the user
        // screenshotted ("hdmi 都有一个一行的悬浮窗") is gone.
        //
        // These assertions used to be `derived != 0` with prose saying
        // MonsterPanel and DamagePanel stay ENABLED. They kept passing
        // while describing a state v0.16 had already abolished, which is
        // worse than a red test: it reads like coverage of the behaviour
        // that is now the bug.
        const bool playerShouldMount =
            mhw::PlayerSection::playerPanelHasBlocks(playerMaskInSplit);
        const bool monsterShouldMount =
            mhw::monsterPanelHasBlocks(monsterMaskInSplit);
        const bool damageShouldMount =
            mhw::damagePanelHasBlocks(damageMaskInSplit);
        check(!playerShouldMount,
              "the split gate leaves PlayerPanel DISABLED — it owns no "
              "block left, so no empty frame");
        check(!monsterShouldMount,
              "and MonsterPanel is DISABLED too — Tenderize is a strip "
              "inside a .pc card, not a block of its own");
        check(!damageShouldMount,
              "and DamagePanel is DISABLED too — OtherMembers is the "
              "roster filter, and the rows moved to their own window");

        // AND THE RUNTIME PROOF, from the objects rather than the tables.
        // Panel::setVisible() returns early for a disabled panel, so
        // show() on a disabled window mounts NO layer-shell surface and
        // reports isVisible() false. That is the mechanism the gate relies
        // on, asserted directly.
        check(player.panelEnabled(),
              "a fresh PlayerPanel is enabled by default (the gate is "
              "main.cpp's, not the panel's)");
        player.setPanelEnabled(false);
        player.show();
        // Wait one event-loop turn: show() is asynchronous on some
        // platforms and the assertion below is about the state after it
        // has been processed.
        for (int i = 0; i < 8; ++i)
            QCoreApplication::processEvents();
        check(!player.isVisible(),
              "show() on a disabled Panel mounts NOTHING — Panel::"
              "setVisible()'s master gate swallows it, which is why the "
              "gate removes the frame rather than hiding it");
        check(player.panelEnabled() == false,
              "and the master gate is still the record of the decision");
    }

    // The same gate on the OTHER axis, which is what makes it a four-way
    // matrix rather than a two-way one: in single mode the derived masks
    // are the user's verbatim, so a mother panel with any section on
    // mounts. This is the half of the pixel-identity promise that the
    // mode axis must not disturb.
    {
        setScope(mhw::GameId::World, W_S);
        const uint32_t playerMaskInSingle =
            mhw::PlayerSection::playerPanelMaskFor(W_S,
                                                   mhw::PlayerSection::kAll);
        check(playerMaskInSingle == mhw::PlayerSection::kAll,
              "in single, PlayerPanel's derived mask is the user's "
              "VERBATIM — the split's gate cannot reach it",
              QStringLiteral("mask=%1 want=%2")
                  .arg(playerMaskInSingle)
                  .arg(static_cast<uint64_t>(mhw::PlayerSection::kAll)));
    }

    // ==================================================================
    // SUMMARY
    // ==================================================================
    std::printf("\n%d checks, %d failure(s)\n", checks, failures);
    std::printf("\nMUTATION DISCIPLINE — the one-line change that must make\n"
                "each section above FAIL, with the observed output recorded\n"
                "in the task report:\n"
                "\n"
                "  2. Revert the scope to the one-axis spelling (game only,\n"
                "     no mode segment). The same window then resolves to the\n"
                "     SAME group in both modes, and the two 'different\n"
                "     domain' checks fail.\n"
                "  3a. Make the migration target the two-domain spelling\n"
                "      instead of the four-domain one: the migrated keys\n"
                "      land back where they already were, and the\n"
                "      '[world/single/player]' check fails.\n"
                "  3d. Drop the 'already scoped' guard: the user's edited\n"
                "      value is overwritten by the legacy section.\n"
                "  4. Delete the split arm of setPanelEnabled() in\n"
                "     src/main.cpp: PlayerPanel mounts an empty frame.\n");
    return failures == 0 ? 0 : 1;
}
