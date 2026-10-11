// Control-console stylesheet construction — v1.0.0 console rework.
//
// The HTML prototype is the TRUTH SOURCE for every colour and every rule
// below:
//   experiment/monster-overlay-v1.0.0/prototype/console-single-display.html
//   :root                          (dark, the default)
//   :root[data-theme="light"]      (the light override)
// plus QT-SPEC.md §1 (colour tokens) and §2 (control CSS).
//
// Nothing here reads a hand-written palette. Every token the prototype
// declares is captured in the Tokens struct below and substituted through
// the same `%1`..`%N` + .arg() mechanism the file has always used, so the
// console flips dark/light at runtime with no widget rebuild: the console
// clears its stylesheet, calls consoleStyleSheet() again and repolishes
// (see control_panel.cpp's theme handler).
//
// TOKEN MAPPING (HTML custom property -> Tokens field)
// ----------------------------------------------------
//   --ink       page ground          tk.ink
//   --surf      panel surface        tk.surf
//   --surf2     raised / hover       tk.surf2
//   --surf3     input / active       tk.surf3
//   --line      hairline border      tk.line
//   --line2     stronger border      tk.line2
//   --fg        primary text         tk.fg
//   --fg2       secondary text       tk.fg2
//   --fg3       muted / mono labels  tk.fg3
//   --clay      primary accent       tk.clay  (+ --clay-d / --clay-l)
//   --olive     ON / connected       tk.olive (+ --olive-l)
//   --rust      running / offline    tk.rust  (+ --rust-l)
//   --on-clay   text ON a clay fill  tk.onClay   <-- INVERTS per theme
//   --on-olive  text ON an olive fill tk.onOlive  <-- INVERTS per theme
//   --on-rust   text ON a rust fill  tk.onRust   <-- INVERTS per theme
//   --board-*   the simulated display tk.board*  <-- pinned dark, BOTH themes
//   --preview-bg the data-panel preview tk.previewBg <-- FOLLOWS the theme
//   --tint-*/--edge-*/--on-dark-*    the washes and hairlines
//
// WHAT IS *NOT* FROM THE PROTOTYPE. The four panel identity colours
// (player = purple, monster = orange, damage/pets = teal) are read from
// UiTheme::accentPurple / accentOrange / accentTeal, because those same
// three colours are painted by HudCanvas, SectionRow and SectionCountBar
// and a second set would let the rail and the canvas disagree. The
// prototype's rail has no per-panel colour at all, so this is the one
// place Qt is richer than the prototype rather than poorer.
//
// ---------------------------------------------------------------------------
// THE FIVE REGIONS, in the prototype's order:
//   0. the app shell        (html, body, scrollbars, tooltips)
//   1. the sidebar   .side  (brand, game segment, nav, .side-foot)
//   2. the topbar    .topbar(segment, selects, chip, .btn / .btn.on)
//   3. the statusline .statusline (dot / text / run)
//   4. the stage + board     (.card, .board-hint — an ordinary surface)
//   5. the inspector .inspector (fields, preview, foot)
// ---------------------------------------------------------------------------

#include "ui/control_panel_styles.h"

#include "ui/ui_theme.h"

#include <QFontDatabase>
#include <QString>

namespace {

// ---------------------------------------------------------------------------
// The prototype's mono / sans stacks, resolved against what Qt actually has
// on this box. The prototype uses `ui-monospace` first; QFontDatabase's
// system font queries are Qt's answer to the same question and return a
// real monospace on every platform we ship.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// The prototype's full token set. Every field is the HTML value verbatim;
// the light column is what :root[data-theme="light"] overrides. A field
// the light block does NOT override keeps its :root value, which is why
// --board-fg* are the same in both themes (the prototype's own note: a
// near-black --fg on a #101010 ground measured 1.11:1, so the board's
// text is pinned rather than inherited).
// ---------------------------------------------------------------------------
struct Tokens {
    // surfaces
    QString ink, surf, surf2, surf3, line, line2;
    // text
    QString fg, fg2, fg3;
    // house accents — dark surface needs the lighter steps, so the light
    // theme darkens every one of them.
    QString clay, clayL, olive, oliveL, rust, rustL;
    // text placed ON a filled accent. The one set that inverts: a dark
    // amber fill needs near-black ink, a light amber fill needs white.
    // QT-SPEC §1.9 puts all three at #FFFFFF in the light theme.
    QString onClay, onOlive, onRust;
    // the simulated display inside the board: its own ground, always dark,
    // because that is what a real overlay window looks like on a desktop.
    QString boardBg, boardLine, boardFg, boardFg2, boardFg3, boardAccent;
    // the data-panel preview. Follows the theme, unlike the board.
    QString previewBg;
    // translucent washes + tinted hairlines
    QString tintClay, tintOlive, tintRust;
    QString edgeClay, edgeOlive;
    // radii, spelled inline in the rules (--r-panel 12 / --r-row 8 /
    // --r-pill 999)
};

Tokens tokens(bool dark)
{
    Tokens k;
    if (dark) {
        // ---- HTML :root -------------------------------------------------
        k.ink    = QStringLiteral("#141413");
        k.surf   = QStringLiteral("#1D1D1B");
        k.surf2  = QStringLiteral("#242420");
        k.surf3  = QStringLiteral("#2C2C27");
        k.line   = QStringLiteral("#383832");
        k.line2  = QStringLiteral("#4C4C45");
        k.fg     = QStringLiteral("#E9E7DE");
        k.fg2    = QStringLiteral("#ADABA1");
        k.fg3    = QStringLiteral("#87867F");
        k.clay   = QStringLiteral("#D97757");
        k.clayL  = QStringLiteral("#E08A6C");
        k.olive  = QStringLiteral("#788C5D");
        k.oliveL = QStringLiteral("#9BB07E");
        k.rust   = QStringLiteral("#B04A3F");
        k.rustL  = QStringLiteral("#C65A4E");
        k.onClay  = QStringLiteral("#1A1208");
        k.onOlive = QStringLiteral("#10140C");
        k.onRust  = QStringLiteral("#1A0D0A");
        k.boardBg     = QStringLiteral("#101010");
        k.boardLine   = QStringLiteral("#2E2E2A");
        k.boardFg     = QStringLiteral("#E9E7DE");
        k.boardFg2    = QStringLiteral("#ADABA1");
        k.boardFg3    = QStringLiteral("#87867F");
        k.boardAccent = QStringLiteral("#E08A6C");
        k.previewBg = QStringLiteral("#131312");
        k.tintClay = QStringLiteral("rgba(217,119,87,0.12)");
        k.tintOlive = QStringLiteral("rgba(120,140,93,0.14)");
        k.tintRust = QStringLiteral("rgba(176,74,63,0.14)");
        k.edgeClay = QStringLiteral("rgba(217,119,87,0.45)");
        k.edgeOlive = QStringLiteral("rgba(120,140,93,0.45)");
    } else {
        // ---- HTML :root[data-theme="light"] ------------------------------
        // Not a mechanical inversion: each value is chosen so contrast
        // holds at the same ROLES the dark theme uses. The accents darken
        // because the light ground already supplies the visual weight, the
        // on-accent text inverts to white, and --board-bg /
        // --preview-bg stay where they are on purpose.
        k.ink    = QStringLiteral("#F5F3EE");
        k.surf   = QStringLiteral("#FFFFFF");
        k.surf2  = QStringLiteral("#F0EEE7");
        k.surf3  = QStringLiteral("#E7E4DB");
        k.line   = QStringLiteral("#DEDBD1");
        k.line2  = QStringLiteral("#C7C3B6");
        k.fg     = QStringLiteral("#23231F");
        k.fg2    = QStringLiteral("#585650");
        k.fg3    = QStringLiteral("#6E6C65");
        k.clay   = QStringLiteral("#9C4623");
        k.clayL  = QStringLiteral("#B4552F");
        k.olive  = QStringLiteral("#5B6E42");
        k.oliveL = QStringLiteral("#5A6E3E");
        k.rust   = QStringLiteral("#9A3A30");
        k.rustL  = QStringLiteral("#B4473C");
        // QT-SPEC §1.9 / HTML light block: the on-accent text INVERTS.
        k.onClay  = QStringLiteral("#FFFFFF");
        k.onOlive = QStringLiteral("#FFFFFF");
        k.onRust  = QStringLiteral("#FFFFFF");
        // The board stays dark in the light theme — it is a picture of a
        // DISPLAY, and previewing a game overlay on white would
        // misrepresent what the user will see in-game.
        k.boardBg     = QStringLiteral("#1A1A18");
        k.boardLine   = QStringLiteral("#33332E");
        k.boardFg     = QStringLiteral("#E9E7DE");   // not overridden
        k.boardFg2    = QStringLiteral("#ADABA1");   // not overridden
        k.boardFg3    = QStringLiteral("#87867F");   // not overridden
        k.boardAccent = QStringLiteral("#E08A6C");   // not overridden
        k.previewBg = QStringLiteral("#FFFFFF");
        k.tintClay = QStringLiteral("rgba(180,85,47,0.10)");
        k.tintOlive = QStringLiteral("rgba(91,110,66,0.12)");
        k.tintRust = QStringLiteral("rgba(154,58,48,0.10)");
        k.edgeClay = QStringLiteral("rgba(180,85,47,0.40)");
        k.edgeOlive = QStringLiteral("rgba(91,110,66,0.40)");
    }
    return k;
}

} // namespace

QString consoleStyleSheet()
{
    const Tokens tk = tokens(isDarkTheme());
    const QString mono = monoFamily();
    const QString sans = sansFamily();

    // The four panel identity colours stay on the UiTheme side: HudCanvas,
    // SectionRow and SectionCountBar paint the same three from uiTheme(),
    // and the prototype's rail carries no per-panel colour of its own.
    const UiTheme &t = uiTheme();
    const QString accentP = t.accentPurple.name();   // player
    const QString accentM = t.accentOrange.name();   // monster
    const QString accentD = t.accentTeal.name();     // damage / pets

    // The type scale, as the prototype spells it:
    //   .eyebrow / .side-label / .card-head  mono 10.5px ls .09em uppercase fg3
    //   .nav-count / .board-hint             mono 10px   fg3
    //   .statusline / .win-name              mono 11px   fg2
    //   .nav-name / .btn                     sans 12.5px / 12px
    //   .brand h1                            sans 16px 600 fg
    //   body                                 sans 13px fg
    QString qss;

    // =====================================================================
    // 0. THE APP SHELL  (html / body / scrollbars / tooltips)
    // =====================================================================
    qss += QStringLiteral(
        "QWidget{background:%1;color:%2;font-family:'%3';font-size:13px;}"
        // Explicit button foreground: relying on QWidget's inherited
        // `color` leaves Fusion/QSS with stale text after a runtime theme
        // swap on Qt 6.11.
        "QLabel{background:transparent;color:%2;}"
        "QMainWindow{background:%1;}"
        "QToolTip{background:%4;color:%2;border:1px solid %5;padding:4px 6px;}"
        // Every scroll surface is transparent: the region behind it owns
        // the colour (the rail is --surf, the stage is --ink).
        "QScrollArea{border:none;background:transparent;}"
        "QScrollBar:vertical{width:7px;background:transparent;margin:0;}"
        "QScrollBar::handle:vertical{background:%6;min-height:24px;"
        "border-radius:3px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{"
        "height:0;border:none;background:none;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{"
        "background:none;}"
        "QScrollBar:horizontal{height:7px;background:transparent;margin:0;}"
        "QScrollBar::handle:horizontal{background:%6;min-width:24px;"
        "border-radius:3px;}"
        "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{"
        "width:0;border:none;background:none;}"
        "QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal{"
        "background:none;}"
        // Splitter handles are invisible: the visible dividers are the
        // regions' own borders.
        "QSplitter::handle{background:transparent;}"
        // The three splitters are named explicitly as well as through the
        // ::handle sub-control, so each region's geometry and ground is
        // addressable on its own. leftSplitter = [game column | rail],
        // rootSplitter = [left column | right column], and the vertical
        // consoleSplitter (index 0 = drawer, index 1 = stage) is declared
        // in the stage block below.
        "QSplitter#leftSplitter,QSplitter#rootSplitter{background:%1;}"
        "QSplitter#leftSplitter::handle,QSplitter#rootSplitter::handle{width:0;}"
        "QSplitter#consoleSplitter::handle{height:0;}"
        // The right column is the page ground the cards sit on.
        "QWidget#rightHost{background:%1;}"
        ).arg(tk.ink, tk.fg, sans, tk.surf, tk.line, tk.line2);

    // =====================================================================
    // 1. THE SIDEBAR  (HTML .side)
    //    background --surf; border-right 1.5px --line; padding 16px 14px;
    //    display flex; flex-direction column; gap 14px
    // =====================================================================
    qss += QStringLiteral(
        // .side — the rail is the leftSplitter's second page. A 1.5px
        // border is not a legal QSS width on every style, so the hairline
        // is drawn at 2px: visually identical at 1x DPI and it never
        // disappears on a fractional-DPI screen the way a 1.5px request
        // can.
        "QFrame#objectRail{background:%1;border-right:2px solid %2;}"
        // The rail's scrolling middle must not paint its own ground, or
        // the brand block and the nav list sit on two different colours.
        "QFrame#objectRail QWidget{background:transparent;}"
        // .eyebrow — mono 10.5px ls .09em uppercase --fg3.
        "QLabel#railEyebrow{font-family:'%3';font-size:10.5px;"
        "letter-spacing:0.09em;color:%4;}"
        // .brand h1 — sans 16px 600, -0.01em, --fg.
        "QLabel#railBrand{font-family:'%5';font-size:16px;font-weight:600;"
        "letter-spacing:-0.01em;color:%6;}"
        // .side-note — sans 11px --fg3.
        "QLabel#railBrandSub{font-family:'%5';font-size:11px;color:%4;}"
        // .side-label — mono 10.5px ls .09em uppercase --fg3. The
        // inspector's group captions (.cap) share the face.
        "QLabel#sideLabel,QLabel#sectionCap{font-family:'%3';font-size:10.5px;"
        "letter-spacing:0.09em;color:%4;}"
        // .note-line under the game segment.
        "QLabel#gameNote{font-family:'%5';font-size:11px;color:%4;}"
        // The auto-detect badge. Three states, one property.
        "QLabel#autoDetect{font-family:'%3';font-size:11px;color:%4;"
        "border:none;padding-top:2px;}"
        "QLabel#autoDetect[state=\"gray\"]{color:%4;}"
        "QLabel#autoDetect[state=\"cyan\"]{color:%7;}"
        "QLabel#autoDetect[state=\"amber\"]{color:%8;}"
        // ---- the game segment (HTML .seg) -------------------------------
        // --surf2 ground, 1px --line border, radius 8px, 2px padding,
        // 2px gap. The ACTIVE entry is --surf3 with a --line2 border and
        // clay text — the prototype paints the active segment in clay
        // rather than in a solid fill.
        "QFrame#gameGroup{background:%9;border:1px solid %2;border-radius:8px;}"
        "QFrame#gameGroup QPushButton{background:transparent;color:%4;"
        "border:1px solid transparent;border-radius:6px;padding:7px 10px;"
        "font-family:'%5';font-size:12.5px;}"
        "QFrame#gameGroup QPushButton:hover{background:%10;color:%6;}"
        "QFrame#gameGroup QPushButton:checked{background:%10;color:%8;"
        "border-color:%11;font-weight:600;}"
        // The old narrow game column is emptied and pinned to 0 width, but
        // it is still in the tree, so it keeps rules that paint it as part
        // of the rail rather than as a black box.
        "QFrame#gameColumn{background:transparent;}"
        "QFrame#gameColumnRule{background:%2;border:none;"
        "min-height:1px;max-height:1px;}"
        "QLabel#gameColumnDetected{font-family:'%3';font-size:10.5px;color:%4;}"
        // ---- the view nav (HTML .nav / .nav-item) -----------------------
        // gap 4px; .nav-item padding 7px 9px, radius 8px, transparent
        // ground, transparent 1px border; hover --surf2; active --surf3 +
        // --line2 border + clay nav-name.
        "QFrame#navPlayer,QFrame#navMonster,QFrame#navDamage,QFrame#navPets,"
        "QFrame#navOverview{background:transparent;border:1px solid transparent;"
        "border-radius:8px;}"
        "QFrame#navPlayer:hover,QFrame#navMonster:hover,QFrame#navDamage:hover,"
        "QFrame#navPets:hover,QFrame#navOverview:hover{background:%9;}"
        "QFrame#navPlayer[selected=\"true\"],QFrame#navMonster[selected=\"true\"],"
        "QFrame#navDamage[selected=\"true\"],QFrame#navPets[selected=\"true\"],"
        "QFrame#navOverview[selected=\"true\"]{background:%10;border-color:%11;}"
        // .nav-name — sans 12.5px, clay on the active item.
        "QLabel#navTitle{font-family:'%5';font-size:12.5px;color:%6;}"
        "QFrame#navPlayer[selected=\"true\"] > QLabel#navTitle,"
        "QFrame#navMonster[selected=\"true\"] > QLabel#navTitle,"
        "QFrame#navDamage[selected=\"true\"] > QLabel#navTitle,"
        "QFrame#navPets[selected=\"true\"] > QLabel#navTitle,"
        "QFrame#navOverview[selected=\"true\"] > QLabel#navTitle{color:%8;}"
        // .nav-count — mono 10px --fg3. The overview row's total carries
        // the same face.
        "QLabel#navCount{font-family:'%3';font-size:10px;color:%4;}"
        // Qt's own richer second line (the prototype has none in the row).
        "QLabel#navSummary{font-family:'%3';font-size:10px;color:%4;}"
        // The eye toggle's enabled dot — olive while the panel is on.
        "QLabel#navEnabled{font-size:10px;color:%7;}"
        // .nav-item.overview-item — margin-top 2px, a 1px --line rule
        // above it, padding-top 9px, bottom-rounded 8px.
        "QFrame#navOverview{margin-top:2px;border-top:1px solid %2;"
        "border-radius:0 0 8px 8px;padding-top:9px;}"
        // ---- .side-foot -------------------------------------------------
        // margin-top auto (a stretch in the rail's layout), gap 7px, a 1px
        // --line rule above. The three text entries are plain .btn pills.
        "QPushButton[objectName^=\"sideFoot\"]{background:%9;color:%6;"
        "border:1px solid %11;border-radius:999px;padding:6px 13px;"
        "font-family:'%5';font-size:12px;}"
        "QPushButton[objectName^=\"sideFoot\"]:hover{background:%10;color:%6;"
        "border-color:%4;}"
        "QPushButton[objectName^=\"sideFoot\"]:disabled{color:%4;}"
        // The three named entries, so each is addressable on its own
        // (findChild() needs a distinct objectName, and a per-button tweak
        // needs a per-button selector). The prefix selector above still
        // paints any future sideFoot* entry that is not listed here.
        "QPushButton#sideFootHelp,QPushButton#sideFootReset{background:%9;"
        "color:%6;border:1px solid %11;border-radius:999px;padding:6px 13px;"
        "font-family:'%5';font-size:12px;}"
        "QPushButton#sideFootHelp:hover,QPushButton#sideFootReset:hover{"
        "background:%10;color:%6;border-color:%4;}"
        "QPushButton#sideFootHelp:disabled,QPushButton#sideFootReset:disabled{"
        "color:%4;}"
        // SAVE / EXPORT are the prototype's .btn.small (4px 10px, 11px).
        "QPushButton#sideFootSave,QPushButton#sideFootExport{"
        "padding:4px 10px;font-size:11px;}"
        // 设置 — the prototype's #settings-btn is a plain .btn, the same
        // face as 帮助 / 重置演示.
        "QPushButton#settingsBtn{background:%9;color:%6;border:1px solid %11;"
        "border-radius:999px;padding:6px 13px;font-family:'%5';font-size:12px;}"
        "QPushButton#settingsBtn:hover{background:%10;border-color:%4;}"
        "QPushButton#settingsBtn:disabled{color:%4;}"
        // ---- the legacy rail chrome -------------------------------------
        // The EN/CH chip and the theme button left the sidebar (language
        // is the topbar's localeCombo, theme is the settings dialog's
        // segmented control), but both widgets are still constructed and
        // driven by tests through findChild("localeChip"), so both keep a
        // rule that matches the new face.
        "QFrame#localeChip{background:%9;border:1px solid %11;border-radius:999px;}"
        "QFrame#localeChip:hover{border-color:%4;}"
        "QLabel#localeSeg{background:transparent;border:none;color:%12;"
        "font-family:'%3';font-size:10.5px;}"
        "QLabel#localeSeg[active=\"true\"]{color:%8;font-weight:700;}"
        "QLabel#localeSep{background:transparent;border:none;color:%11;"
        "font-family:'%3';font-size:10.5px;}"
        "QPushButton#themeToggle{background:transparent;color:%12;"
        "border:1px solid %11;border-radius:999px;padding:6px 13px;"
        "font-family:'%5';font-size:12px;}"
        "QPushButton#themeToggle:hover{border-color:%4;color:%6;}"
        // The rail's scroll area shows the rail's own surface.
        "QScrollArea#railScroll{background:transparent;border:none;}"
        ).arg(tk.surf, tk.line, mono, tk.fg3, sans, tk.fg, tk.oliveL, tk.clay,
              tk.surf2, tk.surf3, tk.line2, tk.fg2);

    // =====================================================================
    // 2. THE TOPBAR  (HTML .topbar)
    //    background --surf; border 1.5px --line; border-radius 12px;
    //    padding 8px 10px; display flex; align-items center; gap 8px
    // =====================================================================
    qss += QStringLiteral(
        "QFrame#topbar{background:%1;border:2px solid %2;border-radius:12px;}"
        // .tb-label — mono 10.5px ls .08em uppercase --fg3, the same face
        // the .card-head uses, so a topbar group label and a card header
        // read as one system.
        "QLabel#tbLabel{font-family:'%3';font-size:10.5px;letter-spacing:0.08em;"
        "color:%4;}"
        // .tb-note — mono 10px --fg3.
        "QLabel#tbNote{font-family:'%3';font-size:10px;color:%4;}"
        // ---- the layout segment (HTML .seg inside .topbar) --------------
        // Same face as the game segment: --surf2 ground, --line border,
        // radius 8px, 2px padding; the ACTIVE entry is --surf3 + --line2
        // + clay text.
        "QFrame#layoutSeg{background:%5;border:1px solid %2;border-radius:8px;}"
        "QFrame#layoutSeg QPushButton{background:transparent;color:%4;"
        "border:1px solid transparent;border-radius:6px;padding:6px 10px;"
        "font-family:'%3';font-size:12px;font-weight:600;}"
        "QFrame#layoutSeg QPushButton:hover{background:%6;color:%7;}"
        "QFrame#layoutSeg QPushButton:checked{background:%6;color:%8;"
        "border-color:%9;font-weight:600;}"
        // ---- every topbar select (HTML <select>) ------------------------
        // --surf2 ground, 1px --line2 border, radius 6px, mono 11.5px.
        // Qt's QComboBox needs its frame styled explicitly or it keeps the
        // Fusion chrome.
        "QComboBox{background:%5;color:%7;border:1px solid %9;border-radius:6px;"
        "padding:5px 8px;font-family:'%3';font-size:11.5px;min-height:20px;}"
        "QComboBox:hover{border-color:%4;}"
        "QComboBox:focus{border-color:%8;}"
        "QComboBox::drop-down{border:none;width:16px;background:transparent;}"
        "QComboBox::down-arrow{image:none;width:0px;height:0px;}"
        "QComboBox QAbstractItemView{background:%1;color:%7;border:1px solid %9;"
        "border-radius:8px;selection-background-color:%6;selection-color:%8;"
        "outline:none;padding:2px;}"
        // The topbar's own selects, named so a rule can address one of
        // them without dragging the inspector's with it. All five carry
        // the same <select> face as the generic rule above; the named
        // rules exist so the topbar's controls are individually addressable
        // (a future per-control tweak, a debugger, a test).
        "QComboBox#screenCombo,QComboBox#demoStateCombo,QComboBox#instanceCombo,"
        "QComboBox#localeCombo,QComboBox#densityCombo{background:%5;color:%7;"
        "border:1px solid %9;border-radius:6px;padding:5px 8px;"
        "font-family:'%3';font-size:11.5px;min-height:20px;}"
        "QComboBox#screenCombo:hover,QComboBox#demoStateCombo:hover,"
        "QComboBox#instanceCombo:hover,QComboBox#localeCombo:hover,"
        "QComboBox#densityCombo:hover{border-color:%4;}"
        "QComboBox#screenCombo::drop-down,QComboBox#demoStateCombo::drop-down,"
        "QComboBox#instanceCombo::drop-down,QComboBox#localeCombo::drop-down,"
        "QComboBox#densityCombo::drop-down{border:none;width:16px;"
        "background:transparent;}"
        "QComboBox#screenCombo::down-arrow,QComboBox#demoStateCombo::down-arrow,"
        "QComboBox#instanceCombo::down-arrow,QComboBox#localeCombo::down-arrow,"
        "QComboBox#densityCombo::down-arrow{image:none;width:0px;height:0px;}"
        "QComboBox#screenCombo QAbstractItemView,"
        "QComboBox#demoStateCombo QAbstractItemView,"
        "QComboBox#instanceCombo QAbstractItemView,"
        "QComboBox#localeCombo QAbstractItemView,"
        "QComboBox#densityCombo QAbstractItemView{background:%1;color:%7;"
        "border:1px solid %9;border-radius:8px;selection-background-color:%6;"
        "selection-color:%8;outline:none;padding:2px;}"
        // The segment's two entries, named as well as grouped: the frame
        // rule above styles them through the descendant selector, and these
        // two make each entry addressable on its own.
        "QPushButton#layoutSegSingle,QPushButton#layoutSegSplit{"
        "background:transparent;color:%4;border:1px solid transparent;"
        "border-radius:6px;padding:6px 10px;font-family:'%3';font-size:12px;"
        "font-weight:600;}"
        "QPushButton#layoutSegSingle:hover,QPushButton#layoutSegSplit:hover{"
        "background:%6;color:%7;}"
        "QPushButton#layoutSegSingle:checked,QPushButton#layoutSegSplit:checked{"
        "background:%6;color:%8;border-color:%9;font-weight:600;}"
        // ---- the connection chip (HTML .chip) ---------------------------
        // 1px --line2 border, radius 999px, mono 10.5px --fg2, with a 6px
        // status dot. Connected paints the border and text clay on a clay
        // wash and the dot olive-l; disconnected dims both.
        "QFrame#connectChip{background:transparent;border:1px solid %9;"
        "border-radius:999px;padding:3px 9px;}"
        "QLabel#connectChipText{font-family:'%3';font-size:10.5px;color:%10;}"
        "QLabel#connectChipDot{background:%4;border:none;border-radius:3px;"
        "min-width:6px;max-width:6px;min-height:6px;max-height:6px;}"
        "QFrame#connectChip[connected=\"true\"]{border-color:%8;background:%11;}"
        "QFrame#connectChip[connected=\"true\"] QLabel#connectChipText{color:%8;}"
        "QFrame#connectChip[connected=\"true\"] QLabel#connectChipDot{background:%12;}"
        "QFrame#connectChip[connected=\"false\"] QLabel#connectChipText{color:%4;}"
        "QFrame#connectChip[connected=\"false\"] QLabel#connectChipDot{"
        "background:transparent;border:1px solid %4;border-radius:3px;}"
        // ---- .btn — 编辑布局 ---------------------------------------------
        "QPushButton#enterEdit{background:%5;color:%7;border:1px solid %9;"
        "border-radius:999px;padding:6px 13px;font-family:'%3';font-size:12px;}"
        "QPushButton#enterEdit:hover{background:%6;border-color:%4;color:%7;}"
        "QPushButton#enterEdit:disabled{color:%4;border-color:%2;}"
        // .btn.on — the button's face WHILE THE BOARD IS EDITING: clay
        // fill, on-clay text. This is the rule the T5 gap left unwritten;
        // see the report for the fact that no call site sets the `on`
        // property yet, so the rule is correct but currently inert.
        "QPushButton#enterEdit[on=\"true\"]{background:%8;border:1px solid %8;"
        "color:%13;font-weight:600;}"
        "QPushButton#enterEdit[on=\"true\"]:hover{background:%14;border-color:%14;}"
        // ---- .btn.primary / .btn.running — 启动 --------------------------
        // The prototype's primary is OLIVE, not clay, and the running arm
        // is rust. setOverlayRunning() swaps the objectName between
        // startBtn and stopBtn, so the two faces are two selectors.
        "QPushButton#startBtn{background:%15;color:%16;border:1px solid %15;"
        "border-radius:999px;padding:6px 13px;font-family:'%3';font-size:12px;"
        "font-weight:600;}"
        "QPushButton#startBtn:hover{background:%17;border-color:%17;}"
        "QPushButton#stopBtn{background:%18;color:%19;border:1px solid %18;"
        "border-radius:999px;padding:6px 13px;font-family:'%3';font-size:12px;"
        "font-weight:600;}"
        "QPushButton#stopBtn:hover{background:%20;border-color:%20;}"
        "QPushButton#startBtn:disabled,QPushButton#stopBtn:disabled{"
        "background:%6;color:%4;border-color:%2;}"
        // ---- the legacy layout pair -------------------------------------
        // The combo-plus-apply pair is hidden but kept in the tree: four
        // test files drive the layout mode through findChild("layoutCombo")
        // / findChild("layoutApply"), and one walks every combo in the
        // inspector page and must SKIP layoutCombo by name. Both keep
        // rules so a stray reference paints as part of the topbar.
        "QComboBox#layoutCombo{background:%5;color:%7;border:1px solid %9;"
        "border-radius:6px;padding:5px 8px;font-family:'%3';font-size:11.5px;}"
        "QComboBox#layoutCombo:hover{border-color:%4;}"
        "QComboBox#layoutCombo::drop-down{border:none;width:16px;"
        "background:transparent;}"
        "QComboBox#layoutCombo::down-arrow{image:none;width:0px;height:0px;}"
        "QPushButton#layoutApply{background:%5;color:%7;border:1px solid %9;"
        "border-radius:999px;padding:6px 13px;font-family:'%3';font-size:12px;}"
        "QPushButton#layoutApply:hover{background:%6;border-color:%4;color:%7;}"
        "QPushButton#layoutApply:disabled{color:%4;border-color:%2;}"
        // The legacy full-width game buttons (hidden, kept for five test
        // files that look them up by name + localized text).
        "QPushButton#gameBtn{background:transparent;color:%10;"
        "border:1px solid %9;border-radius:8px;padding:6px 10px;"
        "font-family:'%3';font-size:11px;}"
        "QPushButton#gameBtn:hover{background:%6;color:%7;border-color:%4;}"
        "QPushButton#gameBtn:checked{background:%6;border-color:%8;color:%8;}"
        ).arg(tk.surf, tk.line, mono, tk.fg3, tk.surf2, tk.surf3, tk.fg,
              tk.clay, tk.line2, tk.fg2, tk.tintClay, tk.oliveL, tk.onClay,
              tk.clayL, tk.olive, tk.onOlive, tk.oliveL, tk.rust, tk.onRust,
              tk.rustL);

    // =====================================================================
    // 3. THE STATUSLINE  (HTML .statusline)
    //    mono 11px --fg2, min-height 24px, a 7px dot whose colour is the
    //    connection state, padding 0 4px
    // =====================================================================
    qss += QStringLiteral(
        // The row itself is transparent: the statusline sits on the page
        // ground between the topbar and the content, not in a card.
        "QFrame#statusRow{background:transparent;border:none;}"
        "QLabel#statusline{font-family:'%1';font-size:11px;color:%2;border:none;}"
        "QLabel#statusDot{background:%3;border:none;border-radius:4px;"
        "min-width:7px;max-width:7px;min-height:7px;max-height:7px;}"
        "QLabel#statusDot[state=\"live\"]{background:%4;}"
        "QLabel#statusDot[state=\"off\"]{background:%5;}"
        "QLabel#statusDot[state=\"nodata\"]{background:%6;}"
        // .st-run — olive-l while the overlay is alive.
        "QLabel#statusRun{font-family:'%1';font-size:11px;color:%7;border:none;}"
        ).arg(mono, tk.fg2, tk.fg3, tk.oliveL, tk.clay, tk.rust, tk.oliveL);

    // =====================================================================
    // 4. THE STAGE + THE BOARD
    //    .card (--surf, 1.5px --line, radius 12px) wraps the stage; the
    //    board itself stays dark in BOTH themes (see the note below).
    // =====================================================================
    qss += QStringLiteral(
        "QFrame#stage{background:%1;border:none;}"
        "QSplitter#consoleSplitter{background:%1;}"
        "QScrollArea#canvasScroll{background:%1;border:none;}"
        // .board-hint — THE STAGE'S OWN CAPTION, an ordinary console
        // surface: mono 10px --fg3 of the ACTIVE theme. It is NOT a board
        // element — it lives under the canvas, on the page ground — so it
        // must follow --fg3 like every other caption. The old rule pinned
        // it to the board's dark #87867F, which on a light --surf read
        // near-black on white.
        "QLabel#boardHint{font-family:'%2';font-size:10px;color:%3;border:none;}"
        // The board's own labels. --board-* is pinned dark in both themes
        // because the board is a PICTURE of a game overlay on a desktop:
        // painting it on a light ground would misrepresent what the user
        // sees in-game, and a near-black --fg on #101010 measured 1.11:1.
        // HudCanvas paints its own ground and cards; these are the label
        // faces a board surface owns.
        "QLabel#boardTitle{font-family:'%2';font-size:11px;color:%4;}"
        "QLabel#boardVal{font-family:'%2';font-size:10px;color:%5;}"
        "QLabel#boardEmptyTitle{font-family:'%2';font-size:11px;"
        "letter-spacing:0.06em;color:%5;}"
        "QLabel#boardEmptySub{font-family:'%6';font-size:11.5px;color:%7;}"
        ).arg(tk.ink, mono, tk.fg3, tk.boardFg, tk.boardFg2, sans, tk.boardFg3);

    // =====================================================================
    // 5. THE INSPECTOR  (HTML .inspector)
    //    .card (--surf, 1.5px --line, radius 12px) is the drawer the
    //    inspector stack lives in; the fields are the prototype's own.
    // =====================================================================
    qss += QStringLiteral(
        // .card — the drawer's own face.
        "QFrame#configHost{background:%1;border:2px solid %2;border-radius:12px;}"
        // The stack and its pages are transparent so the drawer's surface
        // shows through; a page that painted its own ground would put a
        // second colour inside the card.
        "QStackedWidget#inspectorStack{background:transparent;border:none;}"
        "QStackedWidget#inspectorStack QWidget{background:transparent;}"
        // .insp-title — sans 13px 600 --fg; .insp-sub — mono 10.5px
        // ls .08em uppercase --fg3.
        "QLabel#inspectorTitle{font-family:'%3';font-size:13px;font-weight:600;"
        "color:%4;}"
        "QLabel#inspectorSub{font-family:'%5';font-size:10.5px;"
        "letter-spacing:0.08em;color:%6;}"
        // The empty drawer's two lines.
        "QLabel#drawerEmptyTitle{font-family:'%5';font-size:11px;"
        "letter-spacing:0.08em;color:%7;}"
        "QLabel#drawerEmpty{font-family:'%3';font-size:11.5px;color:%6;}"
        // The section count, tinted by the matching panel accent. The
        // per-panel objectName suffix is what makes that possible.
        "QLabel#countLabel{font-family:'%5';font-size:11px;font-weight:600;"
        "letter-spacing:0.02em;color:%4;}"
        "QLabel#countLabelP{color:%8;}"
        "QLabel#countLabelM{color:%9;}"
        "QLabel#countLabelD,QLabel#countLabelPets{color:%10;}"
        // ---- range sliders (HTML input[type=range], fully drawn) --------
        // 4px track on --surf3, clay sub-page, 13px clay thumb with a 2px
        // --surf ring. accent-color did not survive contact with the
        // renderer in the prototype either, so the widget is painted.
        "QSlider{background:transparent;border:none;}"
        "QSlider::groove:horizontal{height:4px;background:%11;border:none;"
        "border-radius:2px;}"
        "QSlider::sub-page:horizontal{background:%12;border-radius:2px;}"
        "QSlider::add-page:horizontal{background:%11;border-radius:2px;}"
        "QSlider::handle:horizontal{width:13px;height:13px;margin:-5px 0;"
        "background:%12;border:2px solid %1;border-radius:7px;}"
        "QSlider::handle:horizontal:hover{background:%13;}"
        "QSlider::groove:vertical{width:4px;background:%11;border:none;"
        "border-radius:2px;}"
        "QSlider::sub-page:vertical{background:%12;border-radius:2px;}"
        "QSlider::add-page:vertical{background:%11;border-radius:2px;}"
        "QSlider::handle:vertical{width:13px;height:13px;margin:0 -5px;"
        "background:%12;border:2px solid %1;border-radius:7px;}"
        // .field label — mono 10.5px --fg3, min-width 52px; .val-out —
        // mono 11px --fg, min-width 44px, right-aligned.
        "QLabel#sliderLabel{color:%6;font-family:'%5';font-size:10.5px;"
        "min-width:52px;}"
        "QLabel#sliderValue{color:%4;font-family:'%5';font-size:11px;"
        "min-width:44px;}"
        // ---- position (HTML input[type=number]) -------------------------
        // --surf2 ground, 1px --line2 border, radius 6px, mono 11.5px.
        // The spin arrows are hidden rather than left in Fusion chrome;
        // the field stays editable by typing and by the wheel.
        "QSpinBox#posSpinX,QSpinBox#posSpinY{background:%14;color:%4;"
        "border:1px solid %15;border-radius:6px;padding:4px 6px;"
        "font-family:'%5';font-size:11.5px;}"
        "QSpinBox#posSpinX:hover,QSpinBox#posSpinY:hover{border-color:%6;}"
        "QSpinBox#posSpinX:focus,QSpinBox#posSpinY:focus{border-color:%12;}"
        "QSpinBox#posSpinX::up-button,QSpinBox#posSpinY::up-button,"
        "QSpinBox#posSpinX::down-button,QSpinBox#posSpinY::down-button{"
        "background:transparent;border:none;width:12px;}"
        "QSpinBox#posSpinX::up-button:hover,QSpinBox#posSpinY::up-button:hover,"
        "QSpinBox#posSpinX::down-button:hover,QSpinBox#posSpinY::down-button:hover{"
        "background:%11;}"
        "QSpinBox#posSpinX::up-arrow,QSpinBox#posSpinY::up-arrow,"
        "QSpinBox#posSpinX::down-arrow,QSpinBox#posSpinY::down-arrow{"
        "image:none;width:0;height:0;}"
        "QSpinBox#posSpinX::up-button:disabled,QSpinBox#posSpinY::up-button:disabled,"
        "QSpinBox#posSpinX::down-button:disabled,QSpinBox#posSpinY::down-button:disabled{"
        "background:transparent;}"
        // The quadrant readout below the spins — a raised field face.
        "QLabel#posLabel{font-family:'%5';font-size:11px;font-weight:600;"
        "letter-spacing:0.01em;color:%4;background:%14;border:1px solid %15;"
        "border-radius:8px;padding:7px 10px;margin-top:4px;}"
        // ---- the target-screen select -----------------------------------
        // The prototype's <select>, same face as the topbar's.
        // NOTE: no widget is ever given this objectName today (the combo
        // is reached through ctl_[idx].outputCombo, not findChild), so the
        // rule is kept for the objectName the brief lists and is
        // currently inert. The generic QComboBox rule above is what
        // actually paints it.
        "QComboBox#outputCombo{background:%14;color:%4;border:1px solid %15;"
        "border-radius:6px;padding:5px 8px;font-family:'%5';font-size:11.5px;}"
        "QComboBox#outputCombo:hover{border-color:%6;}"
        "QComboBox#outputCombo::drop-down{border:none;width:16px;"
        "background:transparent;}"
        "QComboBox#outputCombo::down-arrow{image:none;width:0px;height:0px;}"
        // ---- THE PREVIEW (HTML .preview-wrap) ---------------------------
        // THE PREVIEW FOLLOWS THE THEME, UNLIKE THE BOARD. The board is a
        // picture of a DISPLAY — a monitor has its own black ground
        // whichever theme the console wears — but the preview is a picture
        // of the DATA PANEL, an ordinary console surface: white in light,
        // near-black in dark. Keeping it dark in the light theme made it a
        // black hole in a light UI and, once --fg turned near-black, put
        // dark text on a dark ground (measured 1.18:1). Both selectors are
        // written: the objectName for the rule and the dynamic property
        // setProperty("previewWidget", true) as the fallback.
        "QLabel#previewBox,QWidget[previewWidget=\"true\"]{background:%16;"
        "border:1px solid %15;border-radius:8px;}"
        "QLabel#previewText{color:%4;font-family:'%3';font-size:11.5px;}"
        // ---- the foot row ------------------------------------------------
        "QPushButton#resetButton{background:transparent;color:%6;"
        "border:1px solid %15;border-radius:999px;padding:6px 13px;"
        "font-family:'%5';font-size:11px;}"
        "QPushButton#resetButton:hover{background:%11;color:%4;border-color:%12;}"
        "QPushButton#resetButton:disabled{color:%6;}"
        // .save-hint — mono 10.5px olive-l.
        "QLabel#modified{font-family:'%5';font-size:10.5px;color:%17;}"
        // ---- the stage bar (safe area / grid / zoom / theme) -------------
        // Plain .btn pills; the checked arm is the clay one.
        "QPushButton#stageToggle{background:transparent;color:%6;"
        "border:1px solid %15;border-radius:999px;padding:6px 12px;"
        "font-family:'%5';font-size:11px;}"
        "QPushButton#stageToggle:hover{border-color:%12;color:%4;}"
        "QPushButton#stageToggle:checked{background:%11;border-color:%12;"
        "color:%12;}"
        "QLabel#stageToggleLabel{color:%6;font-family:'%5';font-size:11px;}"
        // ---- the Rise REFramework management card ------------------------
        // .card inside the inspector: --surf3 ground, --line2 border,
        // radius 12px, plain .btn pills for its actions.
        "QFrame#riseReframeworkCard{background:%11;border:1px solid %15;"
        "border-radius:12px;}"
        "QLabel#riseReframeworkTitle{font-family:'%3';font-size:12.5px;"
        "font-weight:700;color:%4;}"
        "QLabel#riseReframeworkSubtitle,QLabel#riseReframeworkStatus{"
        "font-family:'%5';font-size:11px;color:%6;}"
        "QPushButton#installRiseReframeworkButton,"
        "QPushButton#removeRiseLuaButton,"
        "QPushButton#fixMenuStateButton,"
        "QPushButton#restoreMenuStateButton,"
        "QPushButton#removeRiseReframeworkButton{background:transparent;"
        "color:%4;border:1px solid %15;border-radius:999px;padding:8px 10px;"
        "font-family:'%5';font-size:11px;}"
        "QPushButton#installRiseReframeworkButton:hover,"
        "QPushButton#removeRiseLuaButton:hover,"
        "QPushButton#fixMenuStateButton:hover,"
        "QPushButton#restoreMenuStateButton:hover,"
        "QPushButton#removeRiseReframeworkButton:hover{background:%11;color:%4;}"
        "QPushButton#installRiseReframeworkButton:disabled,"
        "QPushButton#removeRiseLuaButton:disabled,"
        "QPushButton#fixMenuStateButton:disabled,"
        "QPushButton#restoreMenuStateButton:disabled,"
        "QPushButton#removeRiseReframeworkButton:disabled{color:%6;}"
        // The dead edit-mode block's widgets (buildEditModeBlock has no
        // caller) keep rules so a stray reference paints sanely.
        "QLabel#groupTitle{font-family:'%3';font-size:12.5px;font-weight:600;"
        "color:%4;}"
        "QLabel#editCap{font-family:'%5';font-size:10.5px;letter-spacing:0.09em;"
        "color:%6;}"
        ).arg(tk.surf, tk.line, sans, tk.fg, mono, tk.fg3, tk.fg2, accentP,
              accentM, accentD, tk.surf3, tk.clay, tk.clayL, tk.surf2,
              tk.line2, tk.previewBg, tk.oliveL);

    return qss;
}
