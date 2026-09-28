// Control-console stylesheet construction — extracted from
// src/ui/control_panel.cpp (v0.11 K4-A2) so the 220-line QSS literal stops
// competing with the widget-building code in the same translation unit.
//
// The only dependency is ui_theme.h: every colour below is substituted from
// the live palette, which is what lets the console flip dark/light at
// runtime without rebuilding any widget.

#include "ui/control_panel_styles.h"

#include "ui/ui_theme.h"

QString consoleStyleSheet()
{
    // Theme-driven QSS: colours come from uiTheme() (ui_theme.h) so the
    // console can switch dark (light-grey deep charcoal) ↔ light (浅灰)
    // at runtime. Structural rules (selectors / padding / font sizes)
    // stay here in one place. Custom-painted widgets (SectionRow,
    // ToggleChip, SectionCountBar) read the same palette themselves.
    const UiTheme &t = uiTheme();
    const QString bg      = t.bg.name();
    const QString bgPanel = t.bgPanel.name();
    const QString bgCtl   = t.bgControl.name();
    const QString bgTrack = t.bgTrack.name();
    const QString fg      = t.fg.name();
    const QString fgMut   = t.fgMuted.name();
    const QString fgDim   = t.fgDim.name();
    const QString border  = t.border.name();
    const QString soft    = t.borderSoft.name();
    const QString orange  = t.accentOrange.name();
    const QString teal    = t.accentTeal.name();
    const QString purple  = t.accentPurple.name();

    QString qss = QStringLiteral(
        "QWidget{background:%1;color:%2;}"
        // Explicit button foreground is required: relying on QWidget's
        // inherited `color` leaves Fusion/QSS cache with stale text after
        // a runtime theme swap on Qt 6.11.
        "QPushButton{color:%2;}"
        "QMainWindow{background:%1;}"
        "QGroupBox{color:%2;border:none;border-radius:0;"
        " margin-top:0;padding:14px 0 12px 0;font-family:'Chakra Petch';"
        " font-weight:600;letter-spacing:1px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:0;padding:0;color:%2;}"
        "QCheckBox{color:%5;spacing:8px;font-family:'Noto Sans SC';font-size:15px;}"
        "QCheckBox::indicator{width:14px;height:14px;border:1px solid %8;"
        " border-radius:3px;background:%3;}"
        "QCheckBox::indicator:checked{background:%10;border-color:%10;}"
        "QLabel#sub{color:%6;font-size:14px;}"
        "QLabel#master{color:%2;font-weight:600;}"
        "QLabel#previewFrame{background:%3;border:1px solid %8;border-radius:4px;}"
        "QScrollArea{border:none;background:transparent;}"
        "QLabel#logoTitle{color:%2;font-family:'Chakra Petch';font-weight:600;"
        " font-size:20px;letter-spacing:4px;background:transparent;}"
        "QLabel#logoAccent{color:%9;font-family:'Chakra Petch';font-weight:600;"
        " font-size:20px;letter-spacing:4px;background:transparent;}"
        "QLabel#logoSub{color:%6;font-family:'Chakra Petch';font-weight:500;"
        " font-size:14px;letter-spacing:3px;background:transparent;}"
        "QLabel#logoBadge{color:%10;font-family:'Chakra Petch';font-weight:600;"
        " font-size:14px;letter-spacing:2px;background:transparent;}"
        "QLabel#logoBadgeDot{color:%10;font-size:17px;background:transparent;"
        " padding-right:6px;}"
        "QLabel#statusBadge{color:%10;font-family:'Chakra Petch';"
        " font-weight:600;font-size:13px;letter-spacing:2px;"
        " background:transparent;border:none;}"
        "QLabel#badgeP{color:%11;font-family:'Chakra Petch';font-weight:700;"
        " font-size:17px;background:transparent;border:1.5px solid %11;"
        " border-radius:3px;qproperty-alignment:AlignCenter;}"
        "QLabel#badgeM{color:%9;font-family:'Chakra Petch';font-weight:700;"
        " font-size:17px;background:transparent;border:1.5px solid %9;"
        " border-radius:3px;qproperty-alignment:AlignCenter;}"
        "QLabel#badgeD{color:%10;font-family:'Chakra Petch';font-weight:700;"
        " font-size:17px;background:transparent;border:1.5px solid %10;"
        " border-radius:3px;qproperty-alignment:AlignCenter;}"
        "QLabel#groupTitle{color:%2;font-family:'Chakra Petch';font-weight:600;"
        " font-size:17px;letter-spacing:2px;background:transparent;"
        " padding-left:10px;}"
        "QLabel#groupSub{color:%6;font-family:'Noto Sans SC';font-weight:400;"
        " font-size:14px;background:transparent;padding-left:10px;}"
        "QFrame#rule{color:%8;background:%8;border:none;"
        " max-height:1px;min-height:1px;}"
        "QPushButton#enterEdit{background:%9;color:%2;"
        " border:none;border-radius:3px;padding:12px 28px;font-family:'Chakra Petch';"
        " font-weight:700;font-size:14px;letter-spacing:2px;}"
        "QPushButton#enterEdit:hover{background:%9;}"
        "QPushButton#enterEdit:pressed{background:%9;}"
        "QPushButton#startBtn{background:%9;color:%2;"
        " border:none;border-radius:3px;padding:12px 28px;font-family:'Chakra Petch';"
        " font-weight:700;font-size:14px;letter-spacing:2px;}"
        "QPushButton#startBtn:hover{background:%9;}"
        "QPushButton#startBtn:pressed{background:%9;}"
        "QPushButton#stopBtn{background:#a13c2a;color:#1a0808;border:none;border-radius:3px;padding:12px 28px;font-family:'Chakra Petch';font-weight:700;font-size:14px;letter-spacing:2px;}"
        "QPushButton#stopBtn:hover{background:#b8482f;}"
        "QPushButton#stopBtn:pressed{background:#8a311f;}"
        "QPushButton#startBtn:disabled{background:%4;color:%6;}"
        "QLabel#editCap{color:%6;font-family:'Noto Sans SC';font-size:13px;"
        " background:transparent;border:none;}"
        "QLabel#previewTitle{color:%6;font-family:'Chakra Petch';"
        " font-weight:600;font-size:14px;letter-spacing:4px;"
        " background:transparent;border:none;}"
        // v0.5 A shell: rail shares the window base colour (no colour band
        // between the rail text and the window behind it).
        // v0.5.6 layout: top row is rail (border-right) | inspector;
        // stage now sits beneath the top row. The divider line is
        // stage's own border-top, which paints correctly only when the
        // stage has any height — collapsing to 0 hides the border
        // automatically (no leftover sliver).
        "QFrame#objectRail{background:%1;border-right:1px solid %8;}"
        "QFrame#inspectorHost{background:%2;}"
        "QFrame#stage{background:%1;border-top:1px solid %8;}"
        "QLabel#railBrand{font-family:'Chakra Petch';font-size:17px;letter-spacing:2px;color:%2;}"
        "QLabel#railBrandSub,QLabel#railHint{font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;color:%6;}"
        "QLabel#sectionCap{font-family:'Chakra Petch';font-size:12px;letter-spacing:2px;color:%6;}"
        "QFrame#navPlayer,QFrame#navMonster,QFrame#navDamage,QFrame#navPets{background:transparent;border:1px solid transparent;border-radius:3px;}"
        "QFrame#navPlayer:hover,QFrame#navMonster:hover,QFrame#navDamage:hover,QFrame#navPets:hover{background:%3;}"
        "QFrame#navPlayer[selected=\"true\"],QFrame#navMonster[selected=\"true\"],QFrame#navDamage[selected=\"true\"],QFrame#navPets[selected=\"true\"]{background:%1;border-color:%8;}"
        "QFrame#navPlayer   > QLabel#navEnabled {color:%11;}"
        "QFrame#navMonster  > QLabel#navEnabled {color:%9;}"
        "QFrame#navDamage   > QLabel#navEnabled {color:%10;}"
        "QFrame#navPets     > QLabel#navEnabled {color:%10;}"
        "QLabel#navTitle{font-family:'Chakra Petch';font-size:15px;letter-spacing:1px;color:%2;}"
        "QLabel#navSummary{font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;color:%5;}"
        "QLabel#navEnabled{font-size:11px;color:%10;}"
        "QPushButton#railAction{background:transparent;color:%6;border:1px solid %8;border-radius:3px;"
        "text-align:left;padding:11px 18px;font-family:'Chakra Petch';font-size:13px;letter-spacing:1px;}"
        "QPushButton#railAction:hover{background:%3;color:%2;}"
        "QPushButton#railAction:disabled{color:%6;}"
        "QLabel#inspectorTitle{font-family:'Chakra Petch';font-size:24px;letter-spacing:2px;color:%2;}"
        "QLabel#inspectorSub{font-family:'Noto Sans SC';font-size:14px;color:%6;}"
        "QLabel#countLabel{font-family:'Chakra Petch';font-size:14px;font-weight:600;letter-spacing:1px;color:%2;}"
        "QLabel#countLabelP{color:%11;}"
        "QLabel#countLabelM{color:%9;}"
        "QLabel#countLabelD{color:%10;}"
        "QLabel#countLabelPets{color:%10;}"
        "QLabel#sliderLabel{color:%6;font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;min-width:52px;}"
        "QLabel#sliderValue{color:%2;font-family:'Chakra Petch';font-size:13px;min-width:36px;}"
        "QPushButton#foldout{background:transparent;color:%6;border:1px solid %8;border-radius:3px;"
        "text-align:left;padding:13px 20px;font-family:'Chakra Petch';font-size:13px;letter-spacing:1px;}"
        "QPushButton#foldout:disabled:hover{background:%3;}"
        "QPushButton#resetButton{background:transparent;color:%6;border:1px solid %8;border-radius:3px;"
        "padding:11px 18px;font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;}"
        "QPushButton#resetButton:disabled:hover{background:%3;}"
        "QLabel#modified{font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;color:%6;}"
        "QLabel#posLabel{font-family:'Chakra Petch';font-size:13px;font-weight:600;letter-spacing:0.5px;"
        "color:%2;background:%3;border:1px solid %8;border-radius:3px;"
        "padding:8px 13px;margin-top:4px;min-width:330px;}"
        "QScrollBar:vertical{width:7px;background:%2;}QScrollBar::handle:vertical{background:%8;min-height:24px;}"
        "QSlider{background:transparent;border:none;}"
        "QSlider::groove:horizontal{height:4px;background:%4;border-radius:2px;}"
        "QSlider::handle:horizontal{width:14px;height:14px;margin:-5px 0;background:%2;border-radius:7px;}"
        "QSlider::sub-page:horizontal{background:%11;border-radius:2px;}"
        "QPushButton#stageToggle{background:transparent;color:%6;border:1px solid %8;border-radius:3px;padding:7px 18px;font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;}"
        "QPushButton#stageToggle:hover{border-color:%8;color:%2;}"
        "QPushButton#stageToggle:checked{background:%3;border-color:%11;color:%11;}"
        "QLabel#stageToggleLabel{color:%6;font-family:'Chakra Petch';font-size:11px;letter-spacing:1px;}"
        "QPushButton#themeToggle{background:transparent;color:%6;border:1px solid %8;border-radius:3px;padding:7px 16px;font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;}"
        "QPushButton#themeToggle:hover{border-color:%8;color:%2;}"
        // v0.9 i18n: EN/CH chip — same chip family as #themeToggle, but a
        // frame with two segment labels so the ACTIVE language can be
        // highlighted on its own (property-driven, repolished by
        // ControlPanel::updateLocaleChip()).
        "QFrame#localeChip{background:transparent;border:1px solid %8;border-radius:3px;}"
        "QFrame#localeChip:hover{border-color:%11;}"
        "QLabel#localeSeg{background:transparent;border:none;color:%6;"
        " font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;}"
        "QLabel#localeSeg[active=\"true\"]{color:%10;font-weight:700;}"
        "QLabel#localeSep{background:transparent;border:none;color:%8;"
        " font-family:'Chakra Petch';font-size:12px;}"
        // v0.6 Phase 4: World/Rise game selector. selected="true" highlights
        // the active game in the teal accent so the choice reads at a glance.
        "QPushButton#gameBtn{background:transparent;color:%6;border:1px solid %8;border-radius:3px;"
        "padding:8px 0;font-family:'Chakra Petch';font-weight:600;font-size:13px;letter-spacing:1px;}"
        "QPushButton#gameBtn:hover{background:%3;color:%2;}"
        "QPushButton#gameBtn[selected=\"true\"]{background:%3;color:%10;border-color:%10;}"
        // v0.6 Phase 5: auto-detect chip in the GAME row. grey = nothing
        // running, cyan = detected game matches the selection, amber =
        // mismatch (clickable to switch).
        "QLabel#autoDetect{font-family:'Chakra Petch';font-size:12px;"
        " letter-spacing:1px;background:transparent;border:none;padding-top:2px;}"
        "QLabel#autoDetect[state=\"gray\"]{color:%6;}"
        "QLabel#autoDetect[state=\"cyan\"]{color:%10;}"
        "QLabel#autoDetect[state=\"amber\"]{color:%9;}"
        // v0.7.1: GAME column on the left edge of the window. Slightly
        // darker than the rail so the two columns read as distinct at a
        // glance; the rail's border-right is repurposed as the divider
        // between them.
        "QFrame#gameColumn{background:%4;border-right:1px solid %8;}"
        "QFrame#gameColumnRule{background:%8;border:none;max-height:1px;min-height:1px;}"
        "QLabel#gameColumnDetected{font-family:'Chakra Petch';font-size:11px;"
        " letter-spacing:1px;color:%5;background:transparent;border:none;}"
        "QFrame#riseReframeworkCard{background:%3;border:1px solid %8;border-radius:3px;}"
        "QLabel#riseReframeworkTitle{font-family:'Chakra Petch';font-size:15px;"
        "font-weight:700;letter-spacing:1px;color:%2;background:transparent;}"
        "QLabel#riseReframeworkSubtitle{font-family:'Noto Sans SC';font-size:12px;"
        "color:%6;background:transparent;}"
        "QLabel#riseReframeworkStatus{font-family:'Noto Sans SC';font-size:12px;"
        "color:%2;background:transparent;}"
        "QPushButton#installRiseReframeworkButton,"
        "QPushButton#removeRiseLuaButton,"
        "QPushButton#fixMenuStateButton,"
        "QPushButton#restoreMenuStateButton,"
        "QPushButton#removeRiseReframeworkButton{background:transparent;color:%6;"
        "border:1px solid %8;border-radius:3px;padding:8px 10px;"
        "font-family:'Chakra Petch';font-size:11px;letter-spacing:0.5px;"
        "text-align:left;}"
        "QPushButton#installRiseReframeworkButton:hover,"
        "QPushButton#removeRiseLuaButton:hover,"
        "QPushButton#fixMenuStateButton:hover,"
        "QPushButton#restoreMenuStateButton:hover,"
        "QPushButton#removeRiseReframeworkButton:hover{background:%1;color:%2;}"
        "QPushButton#installRiseReframeworkButton:disabled,"
        "QPushButton#removeRiseLuaButton:disabled,"
        "QPushButton#fixMenuStateButton:disabled,"
        "QPushButton#restoreMenuStateButton:disabled,"
        "QPushButton#removeRiseReframeworkButton:disabled{color:%6;background:%4;}"
    );
    // Replace longest placeholders first. QString::arg historically treats
    // %1 as a prefix of %10/%11 in chained substitutions, which produced
    // startup warnings and corrupted the panel accent colours.
    qss.replace(QStringLiteral("%11"), purple);
    qss.replace(QStringLiteral("%10"), teal);
    qss.replace(QStringLiteral("%9"), orange);
    qss.replace(QStringLiteral("%8"), soft);
    qss.replace(QStringLiteral("%7"), border);
    qss.replace(QStringLiteral("%6"), fgDim);
    qss.replace(QStringLiteral("%5"), fgMut);
    qss.replace(QStringLiteral("%4"), bgTrack);
    qss.replace(QStringLiteral("%3"), bgPanel);
    qss.replace(QStringLiteral("%2"), fg);
    qss.replace(QStringLiteral("%1"), bg);
    return qss;
}

