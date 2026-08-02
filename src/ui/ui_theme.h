#pragma once

#include <QColor>

// Central UI theme for the MHW control console.
//
// Two palettes (dark = "light-grey deep" neutral charcoal, light = light
// grey), switchable at runtime via setUiTheme(). QSS (qssBase) AND the
// custom-painted widgets (SectionRow / ToggleChip / SectionCountBar)
// read colours from uiTheme() so a switch repaints everything consistently.
struct UiTheme {
    // ---- Surfaces (lightest → darkest) ----
    QColor bg;           // window / rail base
    QColor bgPanel;      // inspector column
    QColor bgControl;    // buttons / inputs / checkbox base
    QColor bgTrack;      // toggle track, section-count-bar track, slider groove
    QColor tileDark;     // section-row icon tile
    QColor tileHairline; // section-row icon tile outline
    QColor tileHover;    // section-row / toggle hover state

    // ---- Text ----
    QColor fg;           // primary text
    QColor fgMuted;      // secondary text / disabled
    QColor fgDim;        // captions, hints

    // ---- Chrome ----
    QColor border;       // structural hairlines (rail edge, group rules)
    QColor borderSoft;   // control borders
    QColor handle;       // toggle knob / slider handle
    QColor shadow;       // soft shadow under knobs (with alpha)

    // ---- Brand / accent (shared; saturated enough for both themes) ----
    QColor accentOrange; // logo / primary CTA
    QColor accentTeal;   // ON state / connected
    QColor accentPurple; // player panel
};

// Current active theme. Dark ("light-grey deep") is the default.
const UiTheme &uiTheme();
// Switch theme and notify custom widgets to repaint.
void setUiTheme(bool dark);
bool isDarkTheme();
// Re-run style() on every widget — call AFTER setStyleSheet() swaps so
// QSS-driven chrome (button text colour etc.) picks up the new rules.
void repolishAllWidgets();
