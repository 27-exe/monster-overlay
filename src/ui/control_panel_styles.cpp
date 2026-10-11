// Control-console stylesheet construction — v1.0.0 console rework.
//
// Extracted from src/ui/control_panel.cpp (v0.11 K4-A2) so the QSS literal
// stops competing with the widget-building code in the same translation
// unit, and REWRITTEN for the v1.0.0 shell: the HTML prototype
// (experiment/monster-overlay-v1.0.0/prototype/console-single-display.html)
// is now the truth source for the console's chrome, and every rule below
// is a transcription of a rule from that prototype's <style> block.
//
// The only dependency is ui_theme.h: every colour below is substituted
// from the live palette, which is what lets the console flip dark/light at
// runtime without rebuilding any widget.
//
// TOKEN MAPPING (HTML CSS custom property -> UiTheme field)
// ---------------------------------------------------------
//   --ink   page ground          -> UiTheme::bg        (deepest surface)
//   --surf  panel surface        -> UiTheme::bgPanel
//   --surf2 raised / hover       -> UiTheme::bgControl
//   --surf3 input / active       -> UiTheme::bgTrack
//   --line  hairline border      -> UiTheme::border
//   --line2 stronger border      -> UiTheme::borderSoft
//   --fg    primary text         -> UiTheme::fg
//   --fg2   secondary text       -> UiTheme::fgMuted
//   --fg3   muted / mono labels  -> UiTheme::fgDim
//   --clay  primary accent       -> UiTheme::accentOrange  (kept: the orange
//        IS the clay house colour in both themes; ui_theme.cpp's value was
//        already re-derived for the light palette the same way HTML darkens
//        --clay on a light ground, so the existing field carries it)
//   --on-clay text on a clay fill -> UiTheme::bg   (near-black in dark
//        theme, near-white in light — ui_theme.cpp already inverts it, and
//        the inverse of the accent is what both themes use)
//
// Anything the UiTheme struct does not carry (the olive/rust house
// accents, the board's pinned dark palette, the on-accent text colours)
// is spelled out literally below, per theme, from the prototype's
// :root / :root[data-theme="light"] blocks. Two complete QSS strings are
// built and one is selected by isDarkTheme() — the same two-branch shape
// the prototype uses, because a single string with per-rule overrides
// would have to duplicate every selector anyway.

#include "ui/control_panel_styles.h"

#include "ui/ui_theme.h"

#include <QFontDatabase>
#include <QString>

// ---------------------------------------------------------------------------
// v1.0.0: the prototype's mono / sans stacks, resolved against what Qt
// actually has on this box. The prototype uses `ui-monospace` first;
// QFontDatabase::systemFont(FixedFont) is Qt's answer to the same
// question and returns a real monospace on every platform we ship.
// ---------------------------------------------------------------------------
namespace {

QString monoFamily()
{
    static const QString cached = QFontDatabase::systemFont(
                                      QFontDatabase::FixedFont).family();
    return cached;
}

QString sansFamily()
{
    static const QString cached = QFontDatabase::systemFont(
                                      QFontDatabase::GeneralFont).family();
    return cached;
}

// The prototype's house accents. NOT in UiTheme (that struct predates the
// prototype and its two entries are shared across themes), and NOT
// theme-invariant: --olive darkens on a light ground exactly like --clay.
struct HouseAccents {
    QString olive;
    QString oliveL;
    QString rust;
    QString onClay;
    QString onOlive;
};

HouseAccents houseAccents(bool dark)
{
    HouseAccents a;
    if (dark) {
        // HTML :root
        a.olive   = QStringLiteral("#788C5D");
        a.oliveL  = QStringLiteral("#9BB07E");
        a.rust    = QStringLiteral("#B04A3F");
        a.onClay  = QStringLiteral("#1A1208");
        a.onOlive = QStringLiteral("#10140C");
    } else {
        // HTML :root[data-theme="light"] — every accent is DARKENED,
        // because a #788C5D olive on ivory is washed out, and the text
        // that sits on a filled accent inverts to white.
        a.olive   = QStringLiteral("#5B6E42");
        a.oliveL  = QStringLiteral("#5A6E3E");
        a.rust    = QStringLiteral("#9A3A30");
        a.onClay  = QStringLiteral("#FFFFFF");
        a.onOlive = QStringLiteral("#FFFFFF");
    }
    return a;
}

// The board is a PICTURE of a game overlay on a desktop, so it stays dark
// in both themes (HTML --board-*). The prototype's own note: painting it
// on a light ground would misrepresent what the user sees in-game, and a
// near-black --fg on #101010 measured 1.11:1.
QString boardPalette()
{
    return QStringLiteral(
        "QWidget#boardGround{background:#101010;border:1px solid #4C4C45;"
        "border-radius:4px;}"
        "QLabel#boardTitle,QLabel#boardVal{color:#E9E7DE;}"
        "QLabel#boardVal{color:#ADABA1;}"
        "QLabel#boardHint{color:#87867F;}"
        "QLabel#boardEmptyTitle{color:#ADABA1;}"
        "QLabel#boardEmptySub{color:#87867F;}"
        );
}

} // namespace

QString consoleStyleSheet()
{
    const UiTheme &t = uiTheme();
    const bool dark = isDarkTheme();
    const HouseAccents ha = houseAccents(dark);

    const QString ink    = t.bg.name();         // --ink    page ground
    const QString surf   = t.bgPanel.name();    // --surf   panel surface
    const QString surf2  = t.bgControl.name();  // --surf2  raised / hover
    const QString surf3  = t.bgTrack.name();    // --surf3  input / active
    const QString line   = t.border.name();     // --line   hairline
    const QString line2  = t.borderSoft.name(); // --line2  stronger border
    const QString fg     = t.fg.name();         // --fg     primary text
    const QString fg2    = t.fgMuted.name();    // --fg2    secondary
    const QString fg3    = t.fgDim.name();      // --fg3    muted / mono
    const QString clay   = t.accentOrange.name();
    const QString teal   = t.accentTeal.name();
    const QString purple = t.accentPurple.name();
    const QString olive  = ha.olive;
    const QString oliveL = ha.oliveL;
    const QString rust   = ha.rust;
    const QString onClay = ha.onClay;
    const QString onOlive= ha.onOlive;
    const QString mono   = monoFamily();
    const QString sans   = sansFamily();

    // ---- the type scale, as the prototype spells it ----------------------
    //   .side-label  mono 10.5px ls .09em uppercase fg3
    //   .side-note   sans 11px  fg3 line-height 1.5
    //   .nav-name    sans 12.5px
    //   .nav-count   mono 10px  fg3
    //   .card-head   mono 10.5px ls .06em uppercase
    //   .statusline  mono 11px  fg2
    //   .win-name    mono 11px
    //   .win-sc      mono 10px  board-fg2
    //   .win-panel   mono 9.5px board-fg3
    //   .topbar      sans 13px (inherited from body)
    QString qss;

    // =====================================================================
    // 0. the app shell
    // =====================================================================
    qss += QStringLiteral(
        "QWidget{background:%1;color:%2;font-family:'%3';font-size:13px;}"
        // Explicit button foreground: relying on QWidget's inherited
        // `color` leaves Fusion/QSS with stale text after a runtime theme
        // swap on Qt 6.11.
        "QMainWindow{background:%1;}"
        "QToolTip{background:%4;color:%2;border:1px solid %6;padding:4px 6px;}"
        ).arg(ink, fg, sans, surf, line, line2);

    // =====================================================================
    // 1. THE SIDEBAR  (HTML .side)
    //    background --surf; border-right 1.5px --line;
    //    padding 16px 14px 14px; display flex; flex-direction column;
    //    gap 14px
    // =====================================================================
    qss += QStringLiteral(
        // The sidebar is the leftSplitter_'s first page. A 1.5px border is
        // not a legal QSS width on every style, so the hairline is drawn
        // at 2px — visually identical at 1x DPI and it never disappears on
        // a fractional-DPI screen the way a 1.5px request can.
        "QSplitter#leftSplitter::handle{width:0;}"
        "QFrame#objectRail{background:%1;border-right:2px solid %5;}"
        // .brand
        "QLabel#railBrand{font-family:'%6';font-size:12.5px;font-weight:600;"
        "letter-spacing:-0.01em;color:%2;background:transparent;}"
        // HTML .eyebrow — "MONSTER CONTROL", mono 10.5px uppercase --fg3.
        "QLabel#railEyebrow{font-family:'%6';font-size:10.5px;"
        "letter-spacing:0.09em;color:%4;background:transparent;}"
        // .side-note — 11px --fg3, line-height 1.5.
        "QLabel#railBrandSub{font-family:'%7';font-size:11px;letter-spacing:0;"
        "color:%4;background:transparent;}"
        // .side-label — mono 10.5px uppercase --fg3.
        "QLabel#sideLabel,QLabel#sectionCap{font-family:'%6';font-size:10.5px;"
        "letter-spacing:0.09em;color:%4;background:transparent;}"
        // .side-note under the game segment.
        "QLabel#gameNote{font-family:'%7';font-size:11px;color:%4;"
        "background:transparent;}"
        "QLabel#autoDetect{font-family:'%6';font-size:11px;"
        " letter-spacing:0.01em;background:transparent;border:none;padding-top:2px;}"
        "QLabel#autoDetect[state=\"gray\"]{color:%4;}"
        "QLabel#autoDetect[state=\"cyan\"]{color:%8;}"
        "QLabel#autoDetect[state=\"amber\"]{color:%9;}"
        // ---- the game segment (HTML .seg) ------------------------------
        // background --surf2, border 1px --line, radius 8px, padding 2px,
        // gap 2px. Two exclusive buttons; the ACTIVE one is --surf3 with a
        // --line2 border and --clay text (the prototype paints the active
        // segment in clay rather than in a solid fill).
        "QFrame#gameGroup{background:%10;border:1px solid %5;border-radius:8px;}"
        "QFrame#gameGroup QPushButton{background:transparent;color:%4;"
        "border:1px solid transparent;border-radius:6px;padding:7px 10px;"
        "font-family:'%6';font-size:12.5px;font-weight:600;letter-spacing:0.02em;}"
        "QFrame#gameGroup QPushButton:hover{background:%11;color:%2;}"
        "QFrame#gameGroup QPushButton:checked{background:%11;color:%9;"
        "border-color:%12;font-weight:600;}"
        // ---- the view nav (HTML .nav / .nav-item) ----------------------
        // gap 4px; .nav-item padding 7px 9px, radius 8px, transparent
        // ground, transparent 1px border; hover --surf2; active --surf3 +
        // --line2 border + clay nav-name.
        "QFrame#navPlayer,QFrame#navMonster,QFrame#navDamage,QFrame#navPets,"
        "QFrame#navOverview{background:transparent;border:1px solid transparent;"
        "border-radius:8px;}"
        "QFrame#navPlayer:hover,QFrame#navMonster:hover,QFrame#navDamage:hover,"
        "QFrame#navPets:hover,QFrame#navOverview:hover{background:%10;}"
        "QFrame#navPlayer[selected=\"true\"],QFrame#navMonster[selected=\"true\"],"
        "QFrame#navDamage[selected=\"true\"],QFrame#navPets[selected=\"true\"],"
        "QFrame#navOverview[selected=\"true\"]{background:%11;border-color:%12;}"
        // .nav-name — 12.5px, and clay on the active item.
        "QLabel#navTitle{font-family:'%7';font-size:12.5px;letter-spacing:0;"
        "color:%2;background:transparent;}"
        "QFrame#navPlayer[selected=\"true\"] > QLabel#navTitle,"
        "QFrame#navMonster[selected=\"true\"] > QLabel#navTitle,"
        "QFrame#navDamage[selected=\"true\"] > QLabel#navTitle,"
        "QFrame#navPets[selected=\"true\"] > QLabel#navTitle,"
        "QFrame#navOverview[selected=\"true\"] > QLabel#navTitle{color:%9;}"
        // .nav-count — mono 10px --fg3.
        "QLabel#navCount{font-family:'%6';font-size:10px;letter-spacing:0.02em;"
        "color:%4;background:transparent;}"
        // .nav-item.is-hidden — struck-through, muted name.
        "QFrame#navPlayer[hidden=\"true\"] > QLabel#navTitle,"
        "QFrame#navMonster[hidden=\"true\"] > QLabel#navTitle,"
        "QFrame#navDamage[hidden=\"true\"] > QLabel#navTitle,"
        "QFrame#navPets[hidden=\"true\"] > QLabel#navTitle{color:%4;}"
        "QLabel#navSummary{font-family:'%6';font-size:10px;letter-spacing:0.01em;"
        "color:%4;background:transparent;}"
        "QLabel#navEnabled{font-size:10px;color:%8;background:transparent;}"
        // .nav-item.overview-item — margin-top 2px, a 1px --line rule
        // above it, padding-top 9px, and a bottom-rounded 8px box.
        "QFrame#navOverview{margin-top:2px;border-top:1px solid %5;"
        "border-radius:0 0 8px 8px;padding-top:9px;}"
        // ---- .side-foot -----------------------------------------------
        // margin-top auto (a stretch in the layout), gap 7px, a 1px --line
        // rule above. Three plain 11px --fg2 buttons, hover --surf2; the
        // two footer actions ride the same face.
        "QPushButton[objectName^=\"sideFoot\"]{background:transparent;color:%13;"
        "border:1px solid transparent;border-radius:8px;padding:7px 9px;"
        "text-align:left;font-family:'%7';font-size:11px;}"
        "QPushButton[objectName^=\"sideFoot\"]:hover{background:%10;color:%2;}"
        "QPushButton[objectName^=\"sideFoot\"]:disabled{color:%4;}"
        "QPushButton#settingsBtn{background:transparent;color:%13;"
        "border:1px solid %5;border-radius:8px;padding:8px 10px;"
        "font-family:'%7';font-size:11px;}"
        "QPushButton#settingsBtn:hover{background:%10;color:%2;border-color:%12;}"
        // The rail's decorative START stays a full-width CTA (it is the
        // only button in the rail footer that launches anything), styled
        // with the olive the prototype reserves for a primary action.
        "QPushButton#railStart{background:%14;color:%15;"
        "border:none;border-radius:8px;padding:10px 12px;"
        "font-family:'%6';font-size:12.5px;font-weight:600;letter-spacing:0.03em;}"
        "QPushButton#railStart:hover{background:%16;}"
        "QPushButton#railStart:disabled{background:%11;color:%4;}"
        // ---- the rail's remaining chrome ------------------------------
        "QPushButton#railAction{background:transparent;color:%4;"
        "border:1px solid transparent;border-radius:8px;"
        "text-align:left;padding:7px 9px;font-family:'%7';font-size:11px;}"
        "QPushButton#railAction:hover{background:%10;color:%2;}"
        "QPushButton#railAction:checked{background:%11;border-color:%12;}"
        "QLabel#railHint{font-family:'%6';font-size:10px;color:%4;"
        "background:transparent;}"
        "QLabel#railBrand{font-family:'%6';font-size:12.5px;font-weight:600;"
        "letter-spacing:0.02em;color:%2;background:transparent;}"
        // ---- the old game column ---------------------------------------
        // The game selector moved into the sidebar, so the column it used
        // to live in is gone from the tree. The rule is kept so a stray
        // reference paints as a plain surface rather than as a black box.
        "QFrame#gameColumn{background:%1;}"
        "QLabel#gameColumnDetected{font-family:'%6';font-size:10.5px;"
        "letter-spacing:0.01em;color:%4;background:transparent;}"
        ).arg(surf, fg, line, fg3, line, mono, sans, teal, clay,
              surf2, surf3, line2, fg2, olive, onOlive, oliveL);

    // =====================================================================
    // 2. THE TOPBAR  (HTML .topbar)
    //    background --surf; border 1.5px --line; border-radius 12px;
    //    padding 8px 10px; display flex; align-items center; gap 8px;
    //    flex-wrap wrap
    // =====================================================================
    qss += QStringLiteral(
        "QFrame#topbar{background:%1;border:2px solid %2;border-radius:12px;}"
        // .tb-label — mono 10.5px uppercase, --fg3. The same face the
        // .card-head uses, so a topbar group label and a card header read
        // as one system.
        "QLabel#tbLabel{font-family:'%3';font-size:10.5px;letter-spacing:0.06em;"
        "color:%4;background:transparent;}"
        // ---- the layout segment (HTML .seg, inside the topbar) --------
        // Same face as the game segment: --surf2 ground, --line border,
        // radius 8px, 2px padding. The ACTIVE entry is --surf3 + --line2
        // + clay text.
        "QFrame#layoutSeg{background:%5;border:1px solid %2;border-radius:8px;}"
        "QFrame#layoutSeg QPushButton{background:transparent;color:%4;"
        "border:1px solid transparent;border-radius:6px;padding:6px 10px;"
        "font-family:'%6';font-size:12px;font-weight:600;}"
        "QFrame#layoutSeg QPushButton:hover{background:%7;color:%8;}"
        "QFrame#layoutSeg QPushButton:checked{background:%7;color:%9;"
        "border-color:%10;font-weight:600;}"
        // ---- every topbar combo ---------------------------------------
        // HTML <select>: inherits the topbar's --surf, 1px --line border,
        // radius 8px, 11px copy. Qt's QComboBox needs its frame styled
        // explicitly or it keeps the Fusion chrome.
        "QComboBox{background:%1;color:%8;border:1px solid %2;border-radius:8px;"
        "padding:5px 8px;font-family:'%6';font-size:11px;min-height:20px;}"
        "QComboBox:hover{border-color:%10;}"
        "QComboBox:focus{border-color:%9;}"
        "QComboBox::drop-down{border:none;width:16px;}"
        "QComboBox::down-arrow{image:none;width:0px;height:0px;}"
        "QComboBox QAbstractItemView{background:%1;color:%8;"
        "border:1px solid %2;border-radius:8px;selection-background-color:%7;"
        "selection-color:%9;outline:none;padding:2px;}"
        // ---- the connection chip (HTML .chip) -------------------------
        // 1px --line2 border, radius 999px, mono 10px --fg2, with a 6px
        // status dot. .sel paints the border and text clay on a clay
        // wash; .off dims both.
        "QFrame#connectChip{background:transparent;border:1px solid %10;"
        "border-radius:12px;padding:3px 9px;}"
        "QLabel#connectChipText{font-family:'%3';font-size:10px;color:%11;"
        "background:transparent;}"
        "QFrame#connectChip[connected=\"true\"]{border-color:%9;"
        "background:rgba(217,119,87,0.12);}"
        "QFrame#connectChip[connected=\"true\"] QLabel#connectChipText{color:%9;}"
        "QLabel#connectChipDot{background:%12;border:none;border-radius:3px;"
        "min-width:6px;max-width:6px;min-height:6px;max-height:6px;}"
        "QFrame#connectChip[connected=\"true\"] QLabel#connectChipDot{background:%12;}"
        "QFrame#connectChip[connected=\"false\"] QLabel#connectChipDot{"
        "background:transparent;border:1px solid %4;border-radius:3px;}"
        // ---- .tb-note (HTML's "仅界面演示 · 非实机") -------------------
        "QLabel#tbNote{font-family:'%6';font-size:10px;color:%4;"
        "background:transparent;}"
        // ---- EDIT / START (HTML .btn / .btn.primary / .btn.running) ---
        "QPushButton#topBtn{background:transparent;color:%8;"
        "border:1px solid %2;border-radius:8px;padding:6px 12px;"
        "font-family:'%6';font-size:11.5px;font-weight:600;}"
        "QPushButton#topBtn:hover{background:%7;border-color:%10;color:%8;}"
        "QPushButton#topBtn:disabled{color:%4;border-color:%2;}"
        // .btn.primary — olive fill, on-olive text.
        "QPushButton#topBtnPrimary{background:%13;color:%14;border:1px solid %13;"
        "border-radius:8px;padding:6px 12px;font-family:'%6';font-size:11.5px;"
        "font-weight:600;}"
        "QPushButton#topBtnPrimary:hover{background:%15;border-color:%15;}"
        "QPushButton#topBtnPrimary:disabled{background:%7;color:%4;border-color:%2;}"
        // .btn.running — rust fill.
        "QPushButton#topBtnRunning{background:%16;color:%17;border:1px solid %16;"
        "border-radius:8px;padding:6px 12px;font-family:'%6';font-size:11.5px;"
        "font-weight:600;}"
        "QPushButton#topBtnRunning:hover{background:%16;}"
        // .btn.on — the EDIT button while the board is in edit mode: clay
        // fill, on-clay text.
        "QPushButton#topBtnOn{background:%9;color:%18;border:1px solid %9;"
        "border-radius:8px;padding:6px 12px;font-family:'%6';font-size:11.5px;"
        "font-weight:600;}"
        "QPushButton#topBtnOn:hover{background:%9;}"
        // The old combo-plus-apply pair is gone (the layout switch is the
        // segmented control now). The rules stay so a stray reference
        // paints as part of the topbar instead of as Fusion chrome.
        "QComboBox#layoutCombo{background:%1;color:%8;border:1px solid %2;"
        "border-radius:8px;padding:5px 8px;font-family:'%6';font-size:11px;}"
        "QPushButton#layoutApply{background:transparent;color:%8;"
        "border:1px solid %2;border-radius:8px;padding:6px 12px;"
        "font-family:'%6';font-size:11px;}"
        "QPushButton#layoutApply:hover{background:%7;border-color:%10;color:%8;}"
        "QPushButton#layoutApply:disabled{color:%4;border-color:%2;}"
        // The legacy full-width CTA pair the toolbar used to carry.
        "QPushButton#enterEdit{background:transparent;color:%8;"
        "border:1px solid %2;border-radius:8px;padding:6px 12px;"
        "font-family:'%6';font-size:11.5px;font-weight:600;}"
        "QPushButton#enterEdit:hover{background:%7;border-color:%10;color:%8;}"
        "QPushButton#startBtn,QPushButton#stopBtn{background:%13;color:%14;"
        "border:1px solid %13;border-radius:8px;padding:6px 12px;"
        "font-family:'%6';font-size:11.5px;font-weight:600;}"
        "QPushButton#startBtn:hover,QPushButton#stopBtn:hover{background:%15;"
        "border-color:%15;}"
        "QPushButton#startBtn:disabled,QPushButton#stopBtn:disabled{"
        "background:%7;color:%4;border-color:%2;}"
        ).arg(surf, line, mono, fg3, surf2, mono /*dup*/, surf3, fg,
              clay, line2, fg2, oliveL, olive, onOlive, oliveL, rust,
              QStringLiteral("#FFFFFF"), onClay);

    // =====================================================================
    // 3. THE STATUSLINE  (HTML .statusline)
    //    mono 11px --fg2, min-height 24px, a 6px dot, padding 0 4px
    // =====================================================================
    qss += QStringLiteral(
        "QLabel#statusline{font-family:'%1';font-size:11px;color:%2;"
        "background:transparent;border:none;}"
        "QLabel#statusDot{background:%3;border:none;border-radius:4px;"
        "min-width:7px;max-width:7px;min-height:7px;max-height:7px;}"
        "QLabel#statusDot[state=\"live\"]{background:%4;}"
        "QLabel#statusDot[state=\"off\"]{background:%5;}"
        "QLabel#statusDot[state=\"nodata\"]{background:%6;}"
        "QLabel#statusRun{font-family:'%1';font-size:11px;color:%7;"
        "background:transparent;border:none;}"
        ).arg(mono, fg2, fg3, teal, rust, olive, fg3);

    // =====================================================================
    // 4. THE BOARD (always dark, both themes — see boardPalette())
    // =====================================================================
    qss += boardPalette();
    qss += QStringLiteral(
        // .card-head — mono 10.5px ls .06em uppercase, --fg3, with the
        // value runs in --fg2 and NOT uppercased.
        "QFrame#card{background:%1;border:2px solid %2;border-radius:12px;}"
        "QLabel#cardHead{font-family:'%3';font-size:10.5px;letter-spacing:0.06em;"
        "color:%4;background:transparent;text-transform:none;}"
        "QLabel#cardVal{font-family:'%3';font-size:10.5px;letter-spacing:0.03em;"
        "color:%5;background:transparent;}"
        "QLabel#cardHint{font-family:'%3';font-size:10px;color:%4;"
        "background:transparent;}"
        // .card-head of the inspector drawer, and the drawer's own face.
        "QFrame#configHost{background:%1;border:2px solid %2;border-radius:12px;}"
        "QFrame#toolbar{background:%1;border:2px solid %2;border-radius:12px;}"
        "QFrame#stage{background:%6;border:none;}"
        "QLabel#inspectorTitle{font-family:'%3';font-size:10.5px;"
        "letter-spacing:0.06em;color:%5;background:transparent;}"
        "QLabel#inspectorSub{font-family:'%7';font-size:11px;color:%4;"
        "background:transparent;}"
        // The empty drawer / empty inspector face.
        "QLabel#drawerEmptyTitle{font-family:'%3';font-size:10.5px;"
        "letter-spacing:0.06em;color:%5;background:transparent;}"
        "QLabel#drawerEmpty{font-family:'%7';font-size:11px;color:%4;"
        "background:transparent;}"
        ).arg(surf, line, mono, fg3, fg2, ink, sans);

    // =====================================================================
    // 5. THE INSPECTOR'S FORM CONTROLS
    //    (unchanged behaviour, re-skinned to the prototype's face)
    // =====================================================================
    qss += QStringLiteral(
        "QScrollArea{border:none;background:transparent;}"
        "QScrollBar:vertical{width:7px;background:%1;}"
        "QScrollBar::handle:vertical{background:%2;min-height:24px;border-radius:3px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:none;}"
        "QSlider{background:transparent;border:none;}"
        "QSlider::groove:horizontal{height:4px;background:%3;border-radius:2px;}"
        "QSlider::handle:horizontal{width:14px;height:14px;margin:-5px 0;"
        "background:%4;border-radius:7px;}"
        "QSlider::sub-page:horizontal{background:%5;border-radius:2px;}"
        "QLabel#countLabel{font-family:'%6';font-size:11px;font-weight:600;"
        "letter-spacing:0.02em;color:%4;}"
        "QLabel#countLabelP{color:%7;}"
        "QLabel#countLabelM{color:%8;}"
        "QLabel#countLabelD{color:%5;}"
        "QLabel#countLabelPets{color:%5;}"
        "QLabel#sliderLabel{color:%9;font-family:'%6';font-size:11px;"
        "letter-spacing:0.01em;min-width:52px;}"
        "QLabel#sliderValue{color:%4;font-family:'%6';font-size:11px;"
        "min-width:36px;}"
        "QPushButton#foldout{background:transparent;color:%4;"
        "border:1px solid %2;border-radius:8px;text-align:left;"
        "padding:9px 12px;font-family:'%6';font-size:11px;}"
        "QPushButton#foldout:hover{background:%10;}"
        "QPushButton#resetButton{background:transparent;color:%9;"
        "border:1px solid %2;border-radius:8px;padding:9px 12px;"
        "font-family:'%6';font-size:11px;}"
        "QPushButton#resetButton:hover{background:%10;color:%4;}"
        "QLabel#modified{font-family:'%6';font-size:11px;letter-spacing:0.01em;"
        "color:%9;}"
        "QLabel#posLabel{font-family:'%6';font-size:11px;font-weight:600;"
        "letter-spacing:0.01em;color:%4;background:%11;border:1px solid %2;"
        "border-radius:8px;padding:7px 10px;margin-top:4px;min-width:300px;}"
        // The Rise REFramework management card and its buttons.
        "QFrame#riseReframeworkCard{background:%10;border:1px solid %2;"
        "border-radius:12px;}"
        "QLabel#riseReframeworkTitle{font-family:'%6';font-size:12.5px;"
        "font-weight:700;letter-spacing:0.01em;color:%4;background:transparent;}"
        "QLabel#riseReframeworkSubtitle,QLabel#riseReframeworkStatus{"
        "font-family:'%12';font-size:11px;color:%9;background:transparent;}"
        "QPushButton#installRiseReframeworkButton,"
        "QPushButton#removeRiseLuaButton,"
        "QPushButton#fixMenuStateButton,"
        "QPushButton#restoreMenuStateButton,"
        "QPushButton#removeRiseReframeworkButton{background:transparent;"
        "color:%4;border:1px solid %2;border-radius:8px;padding:8px 10px;"
        "font-family:'%6';font-size:11px;text-align:left;}"
        "QPushButton#installRiseReframeworkButton:hover,"
        "QPushButton#removeRiseLuaButton:hover,"
        "QPushButton#fixMenuStateButton:hover,"
        "QPushButton#restoreMenuStateButton:hover,"
        "QPushButton#removeRiseReframeworkButton:hover{background:%11;color:%4;}"
        "QPushButton#installRiseReframeworkButton:disabled,"
        "QPushButton#removeRiseLuaButton:disabled,"
        "QPushButton#fixMenuStateButton:disabled,"
        "QPushButton#restoreMenuStateButton:disabled,"
        "QPushButton#removeRiseReframeworkButton:disabled{color:%9;background:%3;}"
        // The stage bar (zoom / grid / safe-area / the display picker).
        "QPushButton#stageToggle{background:transparent;color:%9;"
        "border:1px solid %2;border-radius:8px;padding:6px 12px;"
        "font-family:'%6';font-size:11px;}"
        "QPushButton#stageToggle:hover{border-color:%13;color:%4;}"
        "QPushButton#stageToggle:checked{background:%10;border-color:%5;color:%5;}"
        "QLabel#stageToggleLabel{color:%9;font-family:'%6';font-size:11px;}"
        "QLabel#statusBadge{color:%5;font-family:'%6';font-weight:600;"
        "font-size:11px;letter-spacing:0.04em;background:transparent;"
        "border:none;}"
        // The v0.9 EN/CH chip and the theme toggle are no longer in the
        // sidebar (language moved to the topbar combo, theme to the
        // settings dialog), but both widgets are still constructed, so
        // both keep a rule that matches the new face.
        "QFrame#localeChip{background:transparent;border:1px solid %2;"
        "border-radius:8px;}"
        "QFrame#localeChip:hover{border-color:%5;}"
        "QLabel#localeSeg{background:transparent;border:none;color:%9;"
        "font-family:'%6';font-size:11px;}"
        "QLabel#localeSeg[active=\"true\"]{color:%5;font-weight:700;}"
        "QLabel#localeSep{background:transparent;border:none;color:%2;"
        "font-family:'%6';font-size:11px;}"
        "QPushButton#themeToggle{background:transparent;color:%9;"
        "border:1px solid %2;border-radius:8px;padding:6px 12px;"
        "font-family:'%6';font-size:11px;}"
        "QPushButton#themeToggle:hover{border-color:%13;color:%4;}"
        // The canvas's own chrome (grid, safe area, frame labels) is
        // painted by HudCanvas and reads uiTheme() directly.
        ).arg(surf, line, fg3, fg, clay, mono, purple, teal, fg2, surf2,
              surf3, sans, line2);

    return qss;
}
