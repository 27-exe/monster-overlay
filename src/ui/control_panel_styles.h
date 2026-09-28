#pragma once

#include <QString>

// The complete control-console stylesheet, built from the live uiTheme()
// palette (ui_theme.h).
//
// Extracted from control_panel.cpp (v0.11 K4-A2): the 220-line QSS literal
// used to live in that file's anonymous namespace as qssBase(). It is a
// pure function of the active theme with no widget dependency, so it
// belongs in its own translation unit — the console only calls it at
// construction and again on every theme switch.
//
// Renamed from qssBase(): "base" implied there were layers stacked on top
// of it, but this IS the whole stylesheet, assigned wholesale via
// QWidget::setStyleSheet(). The custom-painted widgets (SectionRow,
// ToggleChip, SectionCountBar) read the same palette directly from
// uiTheme() and are styled by their own paint code, not by QSS.
QString consoleStyleSheet();
