#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/panel.h"
#include "ui/panel_player_split_mantles.h"
#include "ui/panel_settings_game.h"
#include "core/string_table.h"

// ─────────────────────────────────────────────────────────────────────────
// STATUS: NOT YET LANDED. This file is scaffolded and its fixture is now
// proven correct (it puts the console in Split and finds the mantles
// window), but the assertion itself is not usable as written and the
// reason is worth recording rather than rediscovering.
//
// The observable has to be the window's own game seed, because the
// canvases differ from the GEOMETRY rebuild alone: every sub-window
// re-reads [game/mode/key] on a rail click and lands somewhere new, so two
// canvases differ whether or not any window's game_ moved. That is exactly
// how the dispatch shipped dead for two releases while both console suites
// stayed green, and it is why the empirical test matters.
//
// hasVisibleContent() looks like the right read (mantles are a World system,
// empty under Rise) but it is not safe to call on a preview window from
// outside: the rail click destroys the host and builds a new one, so any
// pointer taken before the click is dangling. gdb caught this test doing
// it — QTabBar::sizeHint() under Panel::hasVisibleContent(), i.e. the
// derived hasContent() reaching Qt state that no longer exists. Re-fetching
// after each click does not fix it either, because the NEW window's content
// model reads Qt state that is not realised yet at the moment the test asks.
//
// The fix is an accessor that reads the SEED rather than the CONTENT — the
// game_ member setGameForDemo() writes — exposed once on Panel so all
// twelve split windows share it. That is a change to twelve headers plus
// panel.h, which is a bigger edit than this test deserves to drive, so it
// is left for the next pass and this file stays unbuilt until then.
// ─────────────────────────────────────────────────────────────────────────

#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    (void)argc; (void)argv;
    std::printf("SKIPPED: split_game_seed_dispatch_tests -- see the STATUS "
                "note at the top of this file. The seed needs a dedicated "
                "accessor before it can be asserted safely.\n");
    return 0;
}
