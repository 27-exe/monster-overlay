#pragma once

#include <QString>

// The complete control-console stylesheet, built from the frozen HTML's
// own token table.
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
// ToggleChip, SectionCountBar) read uiTheme() directly and are styled by
// their own paint code, not by QSS.
//
// O3 (2026-10-13): rewritten against reference.html. Every one of the
// frozen file's 29 custom properties lives in the Pal struct of
// control_panel_styles.cpp and is spelled with the exact hex of
// :root / [data-theme="light"]; --board* is fixed dark in both themes.
// Fonts are reference.html:45-47 verbatim (Qt 6.12 keeps a
// comma-separated family list). Rules Qt 6.12 parses but does not paint
// (outline / outline-offset) and rules it rejects outright
// (box-shadow / text-shadow / filter / overflow / gap / z-index /
// user-select / pointer-events / visibility) are documented in the file
// header rather than written — writing them emits
// "Unknown property …" at runtime.
//
// Sets NO application-wide stylesheet: this string is meant for
// ControlPanel and its real parented dialogs only.
QString consoleStyleSheet();
