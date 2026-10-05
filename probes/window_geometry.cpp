// Probe: what does each split sub-window actually ask for, right now?
//
// Run from the repo root against the built library:
//   g++ -std=c++20 -fPIC probes/window_geometry.cpp -o /tmp/wg \
//       -Isrc $(pkg-config --cflags Qt6Widgets Qt6Gui Qt6Core) \
//       -Lbuild -lmhw-ui -lmonster-core ... && QT_QPA_PLATFORM=offscreen /tmp/wg
//
// The point is the floors: a window's content size and the floor the user's
// panels.ini stored are two different numbers, and the one the user SEES is
// max(content, floor). This prints both so a stale floor is visible rather
// than inferred.
#include "ui/panel_player_split.h"
#include "ui/panel_player_split_bars.h"
#include "ui/panel_player_split_info.h"
#include "ui/panel_player_split_mantles.h"
#include "ui/panel_player_split_wirebug.h"
#include "ui/panel_player_split_weapon.h"

#include <QApplication>
#include <cstdio>

template <class P>
static void report(const char *name, P &w)
{
    // Edit mode is where setupDemoData() seeds content, which is what a
    // window reserves against when there is no live snapshot.
    w.setEditMode(true);
    w.resetDemoPrimed();
    w.show();
    for (int i = 0; i < 6; ++i) {
        w.repaint();
        QApplication::processEvents();
    }
    const int cw = w.contentWidth();
    const int ch = w.contentHeight();
    std::printf("%-16s content=%-4d x %-4d   minW=%-4d minH=%-4d\n",
                name, cw, ch, w.minWidth(), w.minHeight());
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    PlayerStatusStripPanel  status;
    PlayerBarsStripPanel    bars;
    PlayerInfoStripPanel    info;
    PlayerMantlesStripPanel mantles;
    PlayerWirebugStripPanel wirebug;
    PlayerWeaponStripPanel  weapon;

    std::printf("=== split sub-window geometry (edit mode, demo content) ===\n");
    report("status",  status);
    report("bars",    bars);
    report("info",    info);
    report("mantles", mantles);
    report("wirebug", wirebug);
    report("weapon",  weapon);

    std::printf("\nA window is shown as  max(content, floor)  on each axis.\n"
                "If floor > content the extra space is empty background.\n");
    return 0;
}
