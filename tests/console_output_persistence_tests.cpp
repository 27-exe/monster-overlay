#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QScreen>
#include <QSettings>
#include <QTextStream>
#include <QTemporaryDir>

#include "ui/control_panel.h"
#include "ui/panel.h"
#include "ui/panel_settings_game.h"

#include <cstdio>

// v0.15: the console's SCREEN picker reaches the file.
//
// Panel::loadConfig() has read `outputName` since v0.8 and
// Panel::saveAppearance() has written it since long before that - so the
// per-window picker was never missing a write path in the panel itself.
// What it was missing is a CALLER: ControlPanel builds a per-panel
// outputCombo (since v0.11) and calls setOutputName(name, persist=false),
// and all eleven CLI writers in main.cpp pass false as well. persist=true
// - the branch that would write - had no callers at all.
//
// Before v0.15 the split made that worse rather than better: the eleven
// sub-windows had no write path from either console exit, so the picker
// applied to them and then vanished on exit.
//
// WHAT OFFSCREEN CAN AND CANNOT SHOW. QGuiApplication::screens() returns
// an empty list under the offscreen platform, so there is no output name
// to pick here and a round trip through "DP-1" would assert the test's
// own string. What does not need a screen to exist is the part that was
// broken: that the save writes the key, under the group loadConfig() reads,
// for a split sub-window as well as for a base panel. That is what is
// asserted.

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL  %s\n", what); }
    else std::printf("PASS: %s\n", what);
}
const char *kIniRel = "monster-overlay/panels.ini";
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("monster-overlay"));
    app.setApplicationName(QStringLiteral("monster-control"));

    QTemporaryDir home;
    Q_ASSERT(home.isValid());
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    QDir().mkpath(home.filePath(QStringLiteral("monster-overlay")));

    // Split mode, or the console builds no sub-windows and the window under
    // test does not exist. monster-overlay.conf is read from
    // $XDG_CONFIG_HOME/monster-overlay/ - NOT under a `.config` segment -
    // so writing it in the wrong place leaves the reader finding nothing,
    // the mode resolving Single, and every lookup below vacuous.
    {
        QFile conf(home.filePath(
            QStringLiteral("monster-overlay/monster-overlay.conf")));
        if (conf.open(QIODevice::WriteOnly | QIODevice::Text
                      | QIODevice::Truncate)) {
            QTextStream out(&conf);
            out.setEncoding(QStringConverter::Utf8);
            out << QStringLiteral("layout=split\n");
            out.flush();
        }
    }

    ControlPanel cp;
    cp.show();
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents();

    QStringList screens;
    for (QScreen *s : QGuiApplication::screens())
        if (s) screens << s->name();
    std::printf("screens available: %d\n", int(screens.size()));

    Panel *panel = cp.splitPreviewWindow(QStringLiteral("player-mantles"));
    check(panel != nullptr,
          "the console has a split sub-window whose output can be chosen");

    if (panel) {
        // The console's own write - the same call both exits make.
        cp.saveAllSplitAndBaseWindows();
        for (int i = 0; i < 10; ++i)
            QCoreApplication::processEvents();

        QSettings s(home.path() + QLatin1Char('/') + QLatin1String(kIniRel),
                    QSettings::IniFormat);

        s.beginGroup(mhw::panelSettingsScopeGroup(QStringLiteral("player")));
        const bool basePresent = s.contains(QStringLiteral("outputName"));
        s.endGroup();

        s.beginGroup(mhw::panelSettingsScopeGroup(
            QStringLiteral("player-mantles")));
        const bool subPresent = s.contains(QStringLiteral("outputName"));
        s.endGroup();

        check(basePresent,
              "a base panel's `outputName` key exists after the save - the "
              "key loadConfig() reads");
        check(subPresent,
              "and so does a SPLIT SUB-WINDOW's, under its own scope group - "
              "the eleven windows that had no write path now do");

        // And the two groups are genuinely different, or the second check
        // could be satisfied by the first one's write landing in the wrong
        // place.
        check(mhw::panelSettingsScopeGroup(QStringLiteral("player"))
                  != mhw::panelSettingsScopeGroup(
                      QStringLiteral("player-mantles")),
              "the two groups differ, so a sub-window's key cannot be the "
              "base panel's key read under a different name");
    }

    std::printf("\n== %d checks, %d failures ==\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
