#include "ui_theme.h"

#include <QApplication>
#include <QStyle>
#include <QWidget>

namespace {

const UiTheme kDark{
    // surfaces: neutral charcoal with a cool grey cast (no green)
    QColor(0x14, 0x17, 0x1a),   // bg            #14171a
    QColor(0x19, 0x1d, 0x21),   // bgPanel       #191d21
    QColor(0x1e, 0x22, 0x26),   // bgControl     #1e2226
    QColor(0x25, 0x2a, 0x2e),   // bgTrack       #252a2e
    QColor(0x1a, 0x1e, 0x22),   // tileDark      #1a1e22
    QColor(0x26, 0x2b, 0x30),   // tileHairline  #262b30
    QColor(0x2d, 0x32, 0x36),   // tileHover     #2d3236
    QColor(0xe8, 0xea, 0xec),   // fg            #e8eaec
    QColor(0x9a, 0xa0, 0xa5),   // fgMuted       #9aa0a5 (brighter for contrast)
    QColor(0x6f, 0x76, 0x7b),   // fgDim         #6f767b (brighter for contrast)
    QColor(0x2a, 0x30, 0x35),   // border        #2a3035
    QColor(0x20, 0x25, 0x2a),   // borderSoft    #20252a
    QColor(0xe8, 0xea, 0xec),   // handle        #e8eaec
    QColor(0, 0, 0, 60),        // shadow
    QColor(0xff, 0x80, 0x40),   // accentOrange
    QColor(0x50, 0xc5, 0xb7),   // accentTeal
    QColor(0xaa, 0x55, 0xff),   // accentPurple
};

const UiTheme kLight{
    // surfaces: light grey (user's requested 浅灰)
    QColor(0xee, 0xf0, 0xf2),   // bg            #eef0f2
    QColor(0xf6, 0xf7, 0xf8),   // bgPanel       #f6f7f8
    QColor(0xff, 0xff, 0xff),   // bgControl     #ffffff
    QColor(0xd3, 0xd7, 0xda),   // bgTrack       #d3d7da
    QColor(0xe4, 0xe7, 0xea),   // tileDark      #e4e7ea
    QColor(0xc9, 0xce, 0xd3),   // tileHairline  #c9ced3
    QColor(0xdb, 0xdf, 0xe3),   // tileHover     #dbdfe3
    QColor(0x1c, 0x20, 0x24),   // fg            #1c2024
    QColor(0x4d, 0x55, 0x5a),   // fgMuted       #4d555a (deeper for contrast)
    QColor(0x6e, 0x76, 0x7b),   // fgDim         #6e767b (deeper for contrast)
    QColor(0xc6, 0xcb, 0xd0),   // border        #c6cbd0
    QColor(0xd8, 0xdc, 0xe0),   // borderSoft    #d8dce0
    QColor(0xff, 0xff, 0xff),   // handle        #ffffff
    QColor(0, 0, 0, 20),        // shadow
    QColor(0xe0, 0x5a, 0x1e),   // accentOrange (deepened for light bg)
    QColor(0x0d, 0x8a, 0x7c),   // accentTeal   (deepened)
    QColor(0x8b, 0x3d, 0xd6),   // accentPurple (deepened)
};

bool g_dark = true;

} // namespace

const UiTheme &uiTheme()
{
    return g_dark ? kDark : kLight;
}

void setUiTheme(bool dark)
{
    if (g_dark == dark)
        return;
    g_dark = dark;
    // Repaint everything that reads uiTheme() in paintEvent, AND force
    // re-polish so QSS-driven widgets (QPushButton text colour, borders)
    // pick up the new palette. update() alone leaves button text stale
    // until the next mouse hover/click triggers a repaint.
    for (QWidget *w : QApplication::allWidgets()) {
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
    }
}

bool isDarkTheme()
{
    return g_dark;
}

void repolishAllWidgets()
{
    // Force every widget to re-run its style() pass. QSS-driven chrome
    // (QPushButton text colour, borders, indicators) is applied lazily by
    // the style; after a setStyleSheet() swap the new rules don't reach
    // existing widgets until they are unpolished + polished again.
    for (QWidget *w : QApplication::allWidgets()) {
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
    }
}
