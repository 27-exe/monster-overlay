// control_panel_styles.cpp — the control console's complete QSS.
//
// ─── O3 (2026-10-13): a rewrite against the FROZEN HTML, not a retune ──
//
// Every rule below is traced to a line of reference.html (sha1
// 1dc50d73b81afbed4e43b1fd5f9a5071da0b849794983696ed48f7f4f3dc153a).
// The previous stylesheet was a v0.11 prototype palette: `#14171a` /
// `#191d21` / `#e8eaec` and accents `#ff8040 / #50c5b7 / #aa55ff` are
// NOT in the frozen design anywhere, and the colour section was ~160
// lines spent re-deriving values ui_theme() already owns. Both are gone.
//
// 1. TOKENS. All 29 CSS custom properties of reference.html:19-49 live
//    in `Pal` (see kDark/kLight below) and are spelled out as the exact
//    hex of the frozen file — dark = :root, light = [data-theme=light].
//    Nothing is invented: no second near-miss palette, no per-rule hex.
//    `page / rail / surface / surface-2 / surface-3` is the three-level
//    ground the HTML uses; `fg / fg-2 / fg-3 / line / line-strong / clay
//    / clay-light / olive / olive-light / rust / oat / on-clay /
//    on-olive` are the text and accent tokens; `board*` is fixed dark in
//    BOTH themes (reference.html:39-43,69-73) and is never re-tinted —
//    the board is drawn by HudCanvas, not by QSS.
//
// 2. FONTS. `--mono`, `--sans`, `--serif` (reference.html:45-47) are
//    copied VERBATIM, commas and quotes included. Qt 6.12 accepts a
//    comma-separated family list in `font-family:` and keeps every
//    entry (probe qss_probe: families round-trip as `ui-monospace |
//    SF Mono | Menlo | Consolas | Noto Sans Mono CJK SC | monospace`).
//    So Latin resolves the same way Chrome resolves it on THIS box and
//    the CJK fallback is the same font — rather than assuming the old
//    `Inter` / bare `monospace` guesses were the HTML's stack.
//
// 3. WHAT QT CANNOT DO. Probed, not assumed (qss_probe2):
//      Unknown property  -> box-shadow, text-shadow, filter, overflow,
//                           gap, z-index, user-select, pointer-events,
//                           visibility      :: NEVER written here.
//      Accepted, no ring -> outline, outline-offset are parsed without
//                           a warning but paint nothing. Focus is
//                           therefore approximated by a border-colour
//                           change and the real 2px/2px clay-light ring
//                           is handed to the parent as paint work.
//      Works             -> font-family lists, letter-spacing,
//                           text-transform, min-height, padding,
//                           border-*, border-radius, white-space,
//                           text-align, line-height (parsed).
//
// 4. SPECIFICITY. Qt ranks rules by (id count, then type count, then
//    declaration order) — qss_probe4. That makes a single
//    `QFrame#objectRail QWidget{background:transparent}` (1 id + 2
//    types) beat every `QPushButton#sideFootHelp` (1 id + 1 type) in the
//    rail and paint the real buttons transparent — measured, not
//    predicted (probe A5: centre pixel alpha 5 vs 255). Both blanket
//    id-scoped descendant rules are gone; the scroll-area interior is
//    cleared by `QScrollArea QWidget{…}` which carries ZERO ids and so
//    can never outrank a real `#id` rule.
//
// 5. BOX MODEL. `* { box-sizing:border-box }` (reference.html:76) makes
//    an HTML `min-height:32px; border:1px` 32px TALL. QSS `min-height`
//    is content-box: probe qss_probe3 measured min-height:32 + 1px
//    border = 34. Every min-height below is therefore HTML total minus
//    2 x border-width. And because the layouts already carry the HTML
//    paddings as contentsMargins (topbar 18/10, main 16/14/16/16,
//    inspector grid 14/12/14/14, boardHint 14/0/14/0), the QSS adds no
//    padding to those containers — QSS padding would stack on top of the
//    layout margins (probe qss_probe2: child offset = padding + margins)
//    and inflate fixed rows a second time.
//
// The custom-painted widgets (SectionRow, ToggleChip, SectionCountBar)
// still read uiTheme() directly and are NOT styled from here.
// ──────────────────────────────────────────────────────────────────────

#include "control_panel_styles.h"

#include <QColor>
#include <QVector>

#include "ui_theme.h"

namespace {

// ─── the 29 frozen tokens ─────────────────────────────────────────────
//
// reference.html:19-49 (:root, dark) and :50-74 (light). Five tokens are
// theme-independent in the HTML (`ink`, `mono`, `sans`, `serif`,
// `radius`) and stay identical across both tables.
struct Pal {
    // --ink, --page, --rail, --surface, --surface-2, --surface-3
    QColor ink, page, rail, surface, surface2, surface3;
    // --line, --line-strong
    QColor line, lineStrong;
    // --fg, --fg-2, --fg-3
    QColor fg, fg2, fg3;
    // --clay, --clay-light, --olive, --olive-light, --rust, --oat
    QColor clay, clayLight, olive, oliveLight, rust, oat;
    // --on-clay, --on-olive
    QColor onClay, onOlive;
    // --board, --board-line, --board-fg, --board-fg-2, --board-fg-3
    QColor board, boardLine, boardFg, boardFg2, boardFg3;
    // --shadow
    QColor shadow;
    // --mono, --sans, --serif (verbatim stacks), --radius
    QString mono, sans, serif, radius;
};

// The stacks, exactly as reference.html:45-47 spells them. No
// substitution: on this box `Consolas` / `Segoe UI` / `Roboto` /
// `Noto Sans SC` are NOT installed (fc-list family census) and
// `Noto Sans Mono CJK SC` / `Microsoft YaHei` / `Noto Serif` ARE, so the
// HTML itself already resolves to the CJK-capable tail of each list.
const char *kMono =
    "ui-monospace,\"SF Mono\",Menlo,Consolas,\"Noto Sans Mono CJK SC\",monospace";
const char *kSans =
    "system-ui,-apple-system,\"Segoe UI\",Roboto,\"Noto Sans SC\",\"Microsoft YaHei\",sans-serif";
const char *kSerif = "ui-serif,Georgia,\"Times New Roman\",serif";

Pal palette()
{
    const UiTheme &t = uiTheme();
    Pal p;

    // Five theme-independent tokens (reference.html:20,45-48).
    p.ink = QColor(QStringLiteral("#141413"));
    p.mono = QString::fromLatin1(kMono);
    p.sans = QString::fromLatin1(kSans);
    p.serif = QString::fromLatin1(kSerif);
    p.radius = QStringLiteral("8px");

    if (isDarkTheme()) {
        // ── :root, reference.html:19-49 ──
        p.page        = QColor(QStringLiteral("#181816"));
        p.rail        = QColor(QStringLiteral("#20201d"));
        p.surface     = QColor(QStringLiteral("#252522"));
        p.surface2    = QColor(QStringLiteral("#2d2d28"));
        p.surface3    = QColor(QStringLiteral("#36362f"));
        p.line        = QColor(QStringLiteral("#3d3d36"));
        p.lineStrong  = QColor(QStringLiteral("#56564c"));
        p.fg          = QColor(QStringLiteral("#ece9df"));
        p.fg2         = QColor(QStringLiteral("#b8b5aa"));
        p.fg3         = QColor(QStringLiteral("#87867f"));
        p.clay        = QColor(QStringLiteral("#d97757"));
        p.clayLight   = QColor(QStringLiteral("#e39a7e"));
        p.olive       = QColor(QStringLiteral("#788c5d"));
        p.oliveLight  = QColor(QStringLiteral("#a1b482"));
        p.rust        = QColor(QStringLiteral("#b04a3f"));
        p.oat         = QColor(QStringLiteral("#e3dacc"));
        p.onClay      = QColor(QStringLiteral("#1a1208"));
        p.onOlive     = QColor(QStringLiteral("#10140c"));
        p.shadow      = QColor(0, 0, 0, 89);          // rgba(0,0,0,.35)
    } else {
        // ── [data-theme="light"], reference.html:50-74 ──
        p.page        = QColor(QStringLiteral("#f4f2ed"));
        p.rail        = QColor(QStringLiteral("#ebe9e2"));
        p.surface     = QColor(QStringLiteral("#ffffff"));
        p.surface2    = QColor(QStringLiteral("#f0eee8"));
        p.surface3    = QColor(QStringLiteral("#e4e1d8"));
        p.line        = QColor(QStringLiteral("#d8d4c9"));
        p.lineStrong  = QColor(QStringLiteral("#c0bbb0"));
        p.fg          = QColor(QStringLiteral("#24241f"));
        p.fg2         = QColor(QStringLiteral("#5b5952"));
        p.fg3         = QColor(QStringLiteral("#6d6b63"));
        p.clay        = QColor(QStringLiteral("#9c4623"));
        p.clayLight   = QColor(QStringLiteral("#b85c38"));
        p.olive       = QColor(QStringLiteral("#5c713f"));
        p.oliveLight  = QColor(QStringLiteral("#718a52"));
        p.rust        = QColor(QStringLiteral("#9b3e35"));
        p.oat         = QColor(QStringLiteral("#e6ddd0"));
        p.onClay      = QColor(QStringLiteral("#ffffff"));
        p.onOlive     = QColor(QStringLiteral("#ffffff"));
        p.shadow      = QColor(30, 28, 22, 38);       // rgba(30,28,22,.15)
    }

    // --board* is FIXED dark in both themes (reference.html:39-43 and
    // 69-73 are byte-identical). The board never follows the console's
    // theme, so these four are set once, outside the if/else.
    p.board      = QColor(QStringLiteral("#101010"));
    p.boardLine  = QColor(QStringLiteral("#2e2e2a"));
    p.boardFg    = QColor(QStringLiteral("#e9e7de"));
    p.boardFg2   = QColor(QStringLiteral("#adaba1"));
    p.boardFg3   = QColor(QStringLiteral("#87867f"));

    Q_UNUSED(t);
    return p;
}

// Every colour literal below is written as @name and substituted once,
// longest name first so `@fg` can never eat the head of `@fg2`.
QString expand(QString s, const Pal &p)
{
    const QVector<QPair<QString, QString>> map = {
        {QStringLiteral("@clayLight"),   p.clayLight.name()},
        {QStringLiteral("@oliveLight"),  p.oliveLight.name()},
        {QStringLiteral("@lineStrong"),  p.lineStrong.name()},
        {QStringLiteral("@boardLine"),   p.boardLine.name()},
        {QStringLiteral("@boardFg2"),    p.boardFg2.name()},
        {QStringLiteral("@boardFg3"),    p.boardFg3.name()},
        {QStringLiteral("@boardFg"),     p.boardFg.name()},
        {QStringLiteral("@onClay"),      p.onClay.name()},
        {QStringLiteral("@onOlive"),     p.onOlive.name()},
        {QStringLiteral("@surface2"),    p.surface2.name()},
        {QStringLiteral("@surface3"),    p.surface3.name()},
        {QStringLiteral("@surface"),     p.surface.name()},
        {QStringLiteral("@line"),        p.line.name()},
        {QStringLiteral("@board"),       p.board.name()},
        {QStringLiteral("@clay"),        p.clay.name()},
        {QStringLiteral("@olive"),       p.olive.name()},
        {QStringLiteral("@shadow"),      p.shadow.name(QColor::HexArgb)},
        {QStringLiteral("@radius"),      p.radius},
        {QStringLiteral("@page"),        p.page.name()},
        {QStringLiteral("@rail"),        p.rail.name()},
        {QStringLiteral("@rust"),        p.rust.name()},
        {QStringLiteral("@oat"),         p.oat.name()},
        {QStringLiteral("@ink"),         p.ink.name()},
        {QStringLiteral("@fg2"),         p.fg2.name()},
        {QStringLiteral("@fg3"),         p.fg3.name()},
        {QStringLiteral("@fg"),          p.fg.name()},
        {QStringLiteral("@mono"),        p.mono},
        {QStringLiteral("@sans"),        p.sans},
        {QStringLiteral("@serif"),       p.serif},
    };
    for (const auto &kv : map)
        s.replace(kv.first, kv.second);
    return s;
}

const char *kQss = R"QSS(
/* ══ 0 · ground ═══════════════════════════════════════════════════════
   body{background:var(--page);color:var(--fg);font-family:var(--sans)}
   (reference.html:78-83). `margin:0` is reference.html:77 — the 15px in
   the HTML root measurement is the page's own vertical scrollbar, not a
   body margin, so no pseudo-8px edge is painted here.               */
QWidget{background:@page;color:@fg;font-family:@sans;font-size:13px;}
QLabel{background:transparent;}
QSplitter{background:@page;}
QSplitter::handle:horizontal{width:0;background:@page;}
QSplitter::handle:vertical{height:0;background:@page;}
QToolTip{background:@surface3;color:@fg;border:1px solid @lineStrong;
         padding:5px 8px;font-family:@sans;font-size:11px;}

/* Scroll interiors. ZERO ids on purpose: `QScrollArea QWidget` (0 ids,
   2 types) outranks the `QWidget{background:@page}` above (0 ids,
   1 type) — which is all it has to beat — and can never outrank a real
   `#id` rule below it. See the specificity note at the top of file. */
QScrollArea{border:none;background:transparent;}
QScrollArea QWidget{background:transparent;}

/* The HTML page scrollbar is 15px (HTML-ROOT-REMEASUREMENT: innerWidth
   1600 vs clientWidth 1585). Same width, so Qt's root scroll does not
   eat a different amount of horizontal room than the frozen page does. */
QScrollBar:vertical{width:15px;background:transparent;border:none;}
QScrollBar::handle:vertical{background:@lineStrong;border-radius:7px;
                            min-height:26px;border:none;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{
    height:0;background:transparent;border:none;}
QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{
    background:transparent;border:none;}
QScrollBar:horizontal{height:12px;background:transparent;border:none;}
QScrollBar::handle:horizontal{background:@lineStrong;border-radius:7px;
                              min-height:0;min-width:26px;border:none;}
QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{
    width:0;background:transparent;border:none;}
QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal{
    background:transparent;border:none;}

/* RESERVED for the wiring owner's root container (consoleRootScroll /
   consoleRootContent do not exist yet). page ground + the same 15px
   vertical scrollbar, produced by the real scroll container — never by
   subtracting width by hand. */
QWidget#consoleRootScroll,QWidget#consoleRootContent{background:@page;}
QScrollArea#consoleRootScroll{border:none;background:transparent;}

/* ══ 1 · rail ═══════════════════════════════════════════════════════
   `.rail{background:var(--rail);border-right:1px solid var(--line);
   padding:18px 14px 14px}` — reference.html:91-94. The vertical padding
   is the railLayout contentsMargins (control_panel.cpp), not QSS. */
QFrame#objectRail{background:@rail;border-right:1px solid @line;}

/* `.brand h1{font-family:var(--serif);font-weight:500;font-size:24px;
   letter-spacing:-.01em}` — reference.html:98 */
QLabel#railBrand{font-family:@serif;font-size:24px;font-weight:500;
    letter-spacing:-0.01em;color:@fg;background:transparent;}

/* `.brand-kicker{color:var(--clay-light);font-size:10px;
   letter-spacing:.12em;text-transform:uppercase}` — reference.html:97 */
QLabel#railEyebrow{font-family:@mono;font-size:10px;color:@clayLight;
    letter-spacing:0.12em;text-transform:uppercase;background:transparent;}

/* `.brand p{color:var(--fg-3);font-size:11px;line-height:1.45}` — :99 */
QLabel#railBrandSub{font-size:11px;color:@fg3;line-height:1.45;
    background:transparent;}

/* `.nav-title{color:var(--fg-3);font:10px var(--mono);
   letter-spacing:.1em;text-transform:uppercase;padding:8px}` — :107 */
QLabel#sideLabel{font-family:@mono;font-size:10px;color:@fg3;
    letter-spacing:0.1em;text-transform:uppercase;background:transparent;}

/* `.game-switch` has NO frame in the HTML (reference.html:100): a plain
   2-column grid, gap 4. The prototype's seg skin on #gameGroup is gone. */
QFrame#gameGroup{background:transparent;border:none;border-radius:7px;}
/* `.seg button,.game-switch button{border:1px solid var(--line);
   background:transparent;color:var(--fg-2);border-radius:6px;
   min-height:34px;font-family:var(--mono);font-size:11px}` — :101-104.
   34 total - 2x1px border = 33 content (box-sizing:border-box vs QSS). */
QFrame#gameGroup QPushButton{background:transparent;border:1px solid @line;
    border-radius:6px;color:@fg2;font-family:@mono;font-size:11px;
    min-height:32px;padding:0 8px;}
QFrame#gameGroup QPushButton:hover{background:@surface2;color:@fg;}
/* `.seg button.active,.game-switch button.active{background:var(--clay);
   border-color:var(--clay);color:var(--on-clay)}` — :105. A clay FILL,
   not a surface-3 outline. Same rule for the topbar's mode segment. */
QFrame#gameGroup QPushButton[selected="true"]{
    background:@clay;border-color:@clay;color:@onClay;}
QLabel#gameNote{font-size:11px;color:@fg3;line-height:1.45;
    background:transparent;}

/* ── nav rows ──────────────────────────────────────────────────────
   `.nav-item{border:1px solid transparent;background:transparent;
   color:var(--fg-2);border-radius:6px;min-height:38px;font-size:12px}`
   (:109-113); `:hover` → surface-2 (:114); `.active` → surface-3 with
   line-strong border (:115). 38 total - 2x1px = 36.                */
QFrame#navOverview,QFrame#navPlayer,QFrame#navMonster,
QFrame#navDamage,QFrame#navPets{
    background:transparent;border:1px solid transparent;border-radius:6px;
    color:@fg2;font-size:12px;min-height:36px;}
QFrame#navOverview:hover,QFrame#navPlayer:hover,QFrame#navMonster:hover,
QFrame#navDamage:hover,QFrame#navPets:hover{background:@surface2;color:@fg;}
QFrame#navOverview[selected="true"],QFrame#navPlayer[selected="true"],
QFrame#navMonster[selected="true"],QFrame#navDamage[selected="true"],
QFrame#navPets[selected="true"]{
    background:@surface3;border:1px solid @lineStrong;color:@fg;}
/* `.nav-item .count{color:var(--fg-3);font:10px var(--mono)}` — :118 */
QLabel#navCount{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;}
QLabel#navTitle{font-size:12px;font-weight:650;color:@fg2;
    background:transparent;}
QLabel#navSummary{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;}
/* `.nav-item .eye{width:22px;height:22px;padding:0;border:0;
   background:transparent;color:var(--fg-3);font-size:13px;
   border-radius:4px}` (:119-122); `:hover` → fg on line (:123);
   `.eye.off` → rust (:124). The widget is already 22x22 in C++. */
QPushButton#navEnabled{background:transparent;border:none;border-radius:4px;
    color:@fg3;font-family:@mono;font-size:13px;padding:0;
    min-height:22px;max-height:22px;min-width:22px;max-width:22px;}
QPushButton#navEnabled:hover{color:@fg;background:@line;}
QPushButton#navEnabled[off="true"]{color:@rust;}

/* `.status-mini{color:var(--fg-3);font:10px var(--mono)}` (:127). The
   rail's REAL detection line is #autoDetect — the statusline that the
   shell once had was removed by O1 (control_panel.cpp:2775 keeps
   statusRow/statusline/statusDot/statusRun declared and NULL), so those
   four dead rules are deleted rather than left as noise. */
QLabel#autoDetect{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;}
QLabel#autoDetect[state="cyan"]{color:@oliveLight;}
QLabel#autoDetect[state="amber"]{color:@clayLight;}
QLabel#autoDetect[state="gray"]{color:@fg3;}

/* `.rail-actions button{min-height:28px;border:1px solid var(--line);
   background:transparent;border-radius:5px;color:var(--fg-3);
   font:10px var(--mono)}` + `:hover` → fg / line-strong (:131-132).
   28 total - 2x1px = 26. #settingsBtn is the same face. */
QPushButton#sideFootHelp,QPushButton#sideFootReset,
QPushButton#sideFootSave,QPushButton#sideFootExport,QPushButton#settingsBtn{
    background:transparent;border:1px solid @line;border-radius:5px;
    color:@fg3;font-family:@mono;font-size:10px;min-height:26px;
    padding:0 6px;}
QPushButton#sideFootHelp:hover,QPushButton#sideFootReset:hover,
QPushButton#sideFootSave:hover,QPushButton#sideFootExport:hover,
QPushButton#settingsBtn:hover{color:@fg;border-color:@lineStrong;}

/* ══ 2 · topbar ═════════════════════════════════════════════════════
   `.topbar{border-bottom:1px solid var(--line);background:var(--surface);
   padding:10px 18px}` — reference.html:134-137. The 10/18 padding is
   hl->setContentsMargins(18,10,18,10) (control_panel.cpp:4935), so it
   is NOT repeated here. */
QFrame#topbar{background:@surface;border-bottom:1px solid @line;}
/* `.topbar-title strong{font-size:13px;font-weight:650}` (:139) and
   `.topbar-title span{color:var(--fg-3);font:10px var(--mono);
   margin-top:3px}` (:140) */
QLabel#topbarTitle{font-size:13px;font-weight:650;color:@fg;
    background:transparent;}
QLabel#topbarSubtitle{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;}
/* `.topbar-label{color:var(--fg-3);font:10px var(--mono);
   white-space:nowrap}` (:142) */
QLabel#tbLabel{font-family:@mono;font-size:10px;color:@fg3;
    white-space:nowrap;background:transparent;}

/* `.seg{background:var(--surface-2);border:1px solid var(--line);
   padding:3px;border-radius:7px}` (:143) with `.seg button{border:0;
   min-height:28px;padding:0 10px}` (:144). QSS padding would STACK on
   the frame layout's own (2,2) contentsMargins, so the 3px is left to
   the layout — see the box-model note in the file header. */
QFrame#layoutSeg{background:@surface2;border:1px solid @line;
    border-radius:7px;}
QFrame#layoutSeg QPushButton{background:transparent;border:none;
    border-radius:5px;color:@fg2;font-family:@mono;font-size:11px;
    min-height:28px;padding:0 10px;}
QFrame#layoutSeg QPushButton:hover{background:@surface3;color:@fg;}
/* `.seg button.active` — clay fill + on-clay (reference.html:105) */
QFrame#layoutSeg QPushButton:checked{
    background:@clay;border:none;color:@onClay;font-weight:600;}

/* `select,input[type=number]{color:var(--fg);
   background:var(--surface-2);border:1px solid var(--line-strong);
   border-radius:6px;min-height:31px;padding:0 9px;font-size:11px}`;
   `select{max-width:245px}`; `.topbar .screen-select{min-width:206px}`
   (:146-151). 31 total - 2x1px = 29. */
QComboBox{background:@surface2;color:@fg;border:1px solid @lineStrong;
    border-radius:6px;padding:0 9px;font-family:@sans;font-size:11px;
    min-height:29px;}
QComboBox:hover{border-color:@clay;}
QComboBox:focus{border-color:@clay;}
QComboBox:disabled{background:@surface3;color:@fg3;border-color:@line;}
QComboBox::drop-down{subcontrol-origin:padding;subcontrol-position:center right;
    width:18px;border:none;background:transparent;}
QComboBox::down-arrow{image:none;border-left:4px solid transparent;
    border-right:4px solid transparent;border-top:5px solid @fg3;}
QComboBox QAbstractItemView{background:@surface;color:@fg;
    border:1px solid @lineStrong;selection-background-color:@surface3;
    selection-color:@fg;outline:none;font-size:11px;}
QComboBox#screenCombo{min-width:206px;max-width:245px;}

/* `.icon-btn,.action-btn{border:1px solid var(--line-strong);
   background:var(--surface-2);border-radius:6px;min-height:32px;
   padding:0 11px;font:11px var(--mono)}` (:153-156). 32 total - 2x1px
   = 30. `:hover` → surface-3 (:157). */
QPushButton{background:@surface2;color:@fg;border:1px solid @lineStrong;
    border-radius:6px;padding:0 11px;font-family:@mono;font-size:11px;
    min-height:30px;}
QPushButton:hover{background:@surface3;}
QPushButton:pressed{background:@surface;}
QPushButton:disabled{background:@surface3;color:@fg3;border-color:@line;}
/* `.action-btn.primary{background:var(--olive);
   border-color:var(--olive);color:var(--on-olive)}` (:158) */
QPushButton#startBtn{background:@olive;border-color:@olive;color:@onOlive;}
QPushButton#startBtn:hover{background:@oliveLight;border-color:@oliveLight;}
/* `.action-btn.stop{background:var(--rust);border-color:var(--rust);
   color:#fff}` (:159) */
QPushButton#stopBtn{background:@rust;border-color:@rust;color:#ffffff;}
QPushButton#stopBtn:hover{background:@rust;border-color:@rust;}
/* `.action-btn.on{background:var(--clay);border-color:var(--clay);
   color:var(--on-clay)}` (:160) — the board's edit face. The property
   is written by setBoardEditing() (control_panel.cpp:4086) and its own
   note asks for exactly `#enterEdit[on="true"]`. */
QPushButton#enterEdit{background:@surface2;color:@fg;
    border-color:@lineStrong;}
QPushButton#enterEdit[on="true"]{
    background:@clay;border-color:@clay;color:@onClay;}
QPushButton#enterEdit[on="true"]:hover{background:@clayLight;
    border-color:@clayLight;color:@onClay;}
QPushButton#topExport{background:@surface2;color:@fg;
    border-color:@lineStrong;}
/* the settings dialog's own switch buttons carry :checked (the
   QButtonGroup is the wiring owner's) */
QPushButton#themeAction:checked,QPushButton#localeAction:checked{
    background:@clay;border-color:@clay;color:@onClay;font-weight:600;}
QPushButton#themeAction:hover,QPushButton#localeAction:hover{
    background:@surface3;}

/* ══ 3 · stage ══════════════════════════════════════════════════════
   `.panel-shell{border:1px solid var(--line);background:var(--surface);
   border-radius:var(--radius)}` (:162) — the board itself is drawn by
   HudCanvas on var(--board) and is NOT re-painted here. */
/* `.panel-shell{background:var(--surface);border:1px solid var(--line);
   border-radius:var(--radius)}` (:162) is the SHELL — so #stage is
   @surface, which is what the 48px `.shell-head` band above the canvas
   shows. The body under it is `.stage-body{background:var(--page)}` (:169)
   and is carried by the two widgets that actually have an objectName for
   that region: #canvasScroll (the board viewport) and #boardHint (the
   32px hint row). The head frame itself has NO objectName in
   control_panel.cpp — see the report's handoff list. */
QFrame#stage{background:@surface;border:1px solid @line;border-radius:8px;}
QScrollArea#canvasScroll{border:none;background:@page;}
/* `.shell-head h2{font-size:13px;font-weight:650}` (:164) and
   `.shell-head .sub{color:var(--fg-3);font:10px var(--mono)}` (:165) */
QLabel#stageTitle{font-size:13px;font-weight:650;color:@fg;
    background:transparent;}
QLabel#stageSub{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;}
/* `.chip{border:1px solid var(--line-strong);background:var(--surface-2);
   border-radius:999px;padding:4px 9px;color:var(--fg-2);
   font:10px var(--mono)}` (:167); `.chip.accent{border-color:var(--clay);
   color:var(--clay-light)}` (:168) */
QLabel#stageScreenChip,QLabel#stageWindowCountChip,QLabel#selectionChip{
    background:@surface2;border:1px solid @lineStrong;border-radius:999px;
    color:@fg2;font-family:@mono;font-size:10px;padding:4px 9px;}
QLabel#stageScreenChip{border-color:@clay;color:@clayLight;}
/* `.stage-body{background:var(--page)}` (:169) — see #canvasScroll above.
   `.board-hint{border-top:1px solid var(--line);color:var(--fg-3);
   font:10px var(--mono);white-space:nowrap}` (:207). The 32px row is
   s_boardHint->setFixedHeight(32) (control_panel.cpp), not QSS. */
QLabel#boardHint{background:@page;border-top:1px solid @line;
    color:@fg3;font-family:@mono;font-size:10px;
    white-space:nowrap;text-align:left;}

/* the stage toolbar's small toggles + zoom read-out */
QPushButton#stageToggle{background:transparent;border:1px solid @line;
    border-radius:5px;color:@fg3;font-family:@mono;font-size:11px;
    min-height:24px;padding:0 7px;}
QPushButton#stageToggle:hover{color:@fg;border-color:@lineStrong;
    background:@surface2;}
QPushButton#stageToggle:checked{background:@surface3;color:@clayLight;
    border-color:@clay;}
QLabel#stageToggleLabel{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;}
QPushButton#themeToggle{background:transparent;border:1px solid @line;
    border-radius:5px;color:@fg3;font-family:@mono;font-size:11px;
    min-height:24px;padding:0 7px;}
QPushButton#themeToggle:hover{color:@fg;border-color:@lineStrong;
    background:@surface2;}

/* the game column left of the rail */
QFrame#gameColumn{background:@rail;border-right:1px solid @line;}
QFrame#gameColumnRule{background:@line;border:none;max-height:1px;}
QLabel#gameColumnDetected{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;}
QPushButton#gameColumnDetected{background:transparent;}

/* ══ 4 · inspector ══════════════════════════════════════════════════
   `.panel-shell` surface + `.inspector .shell-head{min-height:45px}`
   (:209) + `.inspector-content{padding:12px 14px 14px;overflow:auto}`
   (:210). The column paddings are the grid's contentsMargins
   (14,12,14,14) in control_panel.cpp:6429. */
QFrame#configHost{background:@surface;border:1px solid @line;
    border-radius:8px;}
QFrame#inspectorFace{background:transparent;border:none;}
QFrame#inspectorHead{background:transparent;border:none;
    border-bottom:1px solid @line;}
QLabel#inspectorHeadTitle{font-size:13px;font-weight:650;color:@fg;
    background:transparent;}
QLabel#inspectorHeadSub{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;}
/* `.inspector-col{min-width:0}` (:214) — three transparent columns. */
QFrame#inspectorMeta,QFrame#inspectorSections,
QFrame#inspectorPreview{background:transparent;border:none;}
/* `.section-cap{color:var(--fg-3);font:10px var(--mono);
   text-transform:uppercase;letter-spacing:.08em}` (:215) */
QLabel#sectionCap{font-family:@mono;font-size:10px;color:@fg3;
    text-transform:uppercase;letter-spacing:0.08em;background:transparent;}
/* `.object-title{font-size:13px;font-weight:650}` (:216) and
   `.object-title .meta{color:var(--fg-3);font:10px var(--mono)}` (:217) */
QLabel#windowTitle{font-size:13px;font-weight:650;color:@fg;
    background:transparent;}
QLabel#windowTitleMeta{font-family:@mono;font-size:10px;font-weight:400;
    color:@fg3;background:transparent;}
/* `.field label{color:var(--fg-2);font-size:11px}` (:219) and
   `.value{color:var(--fg-3);font:10px var(--mono);text-align:right;
   min-width:36px}` (:223) */
QLabel#sliderLabel{font-size:11px;color:@fg2;background:transparent;}
QLabel#sliderValue{font-family:@mono;font-size:10px;color:@fg3;
    text-align:right;background:transparent;}
/* `.field input[type=number]{width:100%;min-height:28px}` (:220) —
   28 total - 2x1px = 26. `input[type=number]` chrome is :146-149. */
QSpinBox#metaX,QSpinBox#metaY,QSpinBox#posSpinX,QSpinBox#posSpinY{
    background:@surface2;color:@fg;border:1px solid @lineStrong;
    border-radius:6px;padding:0 9px;font-family:@mono;font-size:11px;
    min-height:23px;}
QSpinBox#metaX:hover,QSpinBox#metaY:hover,
QSpinBox#posSpinX:hover,QSpinBox#posSpinY:hover{border-color:@clay;}
QSpinBox#metaX:focus,QSpinBox#metaY:focus,
QSpinBox#posSpinX:focus,QSpinBox#posSpinY:focus{border-color:@clay;}
QSpinBox:disabled{background:@surface3;color:@fg3;border-color:@line;}
/* The spin box's inner QLineEdit is a child QWidget and would otherwise
   paint the global `background:@page` inside a @surface2 field box. */
QSpinBox QLineEdit,QDoubleSpinBox QLineEdit{
    background:transparent;border:none;color:@fg;padding:0;
    font-family:@mono;font-size:11px;selection-background-color:@clay;
    selection-color:@onClay;}
QSpinBox::up-button,QSpinBox::down-button{
    subcontrol-origin:border;subcontrol-position:center right;width:16px;
    border:none;background:transparent;}
QSpinBox::up-arrow,QSpinBox::down-arrow{image:none;border:none;}
/* `.field select{min-height:28px}` (:221) */
QComboBox#metaOutput{min-height:26px;max-width:none;}
/* `.field input[type=range]{width:100%;accent-color:var(--clay)}` (:222)
   — Qt has no accent-color for sliders, so the track/handle are drawn
   in clay explicitly. */
QSlider::groove:horizontal{height:4px;background:@surface3;
    border-radius:2px;border:none;}
QSlider::sub-page:horizontal{background:@clay;border-radius:2px;
    border:none;}
QSlider::add-page:horizontal{background:@line;border-radius:2px;
    border:none;}
QSlider::handle:horizontal{width:14px;height:14px;margin:-5px 0;
    border-radius:7px;background:@clay;border:none;}
QSlider::handle:horizontal:hover{background:@clayLight;}
QSlider:disabled::groove:horizontal{background:@surface3;}
QSlider:disabled::sub-page:horizontal{background:@line;}
QSlider:disabled::handle:horizontal{background:@fg3;}
/* `.check-line{color:var(--fg-2);font-size:11px}` (:224) with
   `accent-color:var(--clay)` (:225) — spelled as an explicit clay
   indicator because Qt draws no check glyph from accent-color. */
QCheckBox{background:transparent;color:@fg2;font-size:11px;
    spacing:8px;padding:0;}
QCheckBox:disabled{color:@fg3;}
QCheckBox::indicator{width:13px;height:13px;border:1px solid @lineStrong;
    background:@surface2;border-radius:3px;}
QCheckBox::indicator:hover{border-color:@clayLight;}
QCheckBox::indicator:checked{background:@clay;border-color:@clay;}
QCheckBox:disabled::indicator{background:@surface3;border-color:@line;}
/* `.note{color:var(--fg-3);font:10px var(--mono);line-height:1.45}` (:226) */
QLabel#windowPosNote{font-family:@mono;font-size:10px;color:@fg3;
    line-height:1.45;background:transparent;}
/* the second column: group caption + sibling chips */
QLabel#groupSections{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;}
QFrame#windowSectionList{background:transparent;border:none;}
/* `.group-chip{border:1px solid var(--line-strong);
   background:var(--surface-2);border-radius:999px;padding:5px 8px;
   color:var(--fg-2);font:10px var(--mono)}` (:232); `.selected` →
   clay border + clay-light text (:233) */
QPushButton#groupWinChip{background:@surface2;border:1px solid @lineStrong;
    border-radius:999px;color:@fg2;font-family:@mono;font-size:10px;
    min-height:23px;padding:0 8px;}
QPushButton#groupWinChip:hover{background:@surface3;color:@fg;}
QPushButton#groupWinChip:checked{border-color:@clay;color:@clayLight;
    background:@surface3;}
QPushButton#groupWinChip[selected="true"]{border-color:@clay;
    color:@clayLight;background:@surface3;}
/* the third column: a REAL Panel preview on a fixed dark box.
   `.hud-detail{background:var(--board);border:1px solid var(--board-line);
   border-radius:6px;padding:9px;min-height:132px}` (:234) is the
   board-coloured box; --board* never changes with the theme. */
QLabel#windowPreviewBox{background:@board;border:1px solid @boardLine;
    border-radius:6px;color:@boardFg2;font-family:@mono;font-size:11px;}
/* `.detail-warning{margin-top:8px;color:#e3b16b;font:10px var(--mono)}`
   (:236) — the one literal colour in the HTML that is not a token. */
QLabel#detailWarning{font-family:@mono;font-size:10px;color:#e3b16b;
    background:transparent;}
/* `.empty-inspector{min-height:150px;color:var(--fg-3)}` (:211) with
   `strong{color:var(--fg);font:18px var(--serif)}` (:212) */
QFrame#emptyInspector{background:transparent;border:none;min-height:150px;}
QLabel#emptyInspectorTitle{font-family:@serif;font-size:18px;font-weight:700;
    color:@fg;background:transparent;}
QLabel#emptyInspectorBody{font-size:11px;color:@fg3;background:transparent;}
/* the drawer's empty page (the stack stays for the tests) */
QWidget#drawerEmptyPage{background:transparent;}
QLabel#drawerEmptyTitle{font-family:@serif;font-size:18px;font-weight:700;
    color:@fg;background:transparent;}
QLabel#drawerEmpty{font-size:11px;color:@fg3;background:transparent;}
/* the four legacy panel pages + their count headlines. The old QSS
   selected `#countLabel`, which is not an objectName anywhere in the
   tree: the real four are countLabelP / M / D / Pets
   (control_panel.cpp:4422-4425). */
QLabel#countLabelP{color:@olive;}
QLabel#countLabelM{color:@clay;}
QLabel#countLabelD{color:@clayLight;}
QLabel#countLabelPets{color:@oat;}
/* The hidden host that still carries the four legacy panel pages. It is
   a plain QWidget directly under #configHost, so without this it would
   paint `background:@page` inside the @surface drawer card. */
QWidget#panelPagesHost{background:transparent;}
QStackedWidget#inspectorStack{background:transparent;}
/* the legacy page's own title / caption row (hidden, test-driven) */
QLabel#inspectorTitle{font-size:13px;font-weight:650;color:@fg;
    background:transparent;}
QLabel#editCap{font-family:@mono;font-size:10px;color:@fg3;
    text-transform:uppercase;letter-spacing:0.08em;background:transparent;}
QLabel#groupTitle{font-size:13px;font-weight:650;color:@fg;
    background:transparent;}
QLabel#inspectorSub{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;}
QLabel#previewText{font-family:@mono;font-size:11px;color:@fg3;
    background:transparent;}
QLabel#previewBox{background:@board;border:1px solid @boardLine;
    border-radius:6px;color:@boardFg2;}
QLabel#modified{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;}
QLabel#posLabel{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;}
QPushButton#resetButton{background:transparent;border:1px solid @line;
    border-radius:5px;color:@fg3;font-family:@mono;font-size:10px;
    min-height:26px;padding:0 8px;}
QPushButton#resetButton:hover{color:@fg;border-color:@lineStrong;}
QPushButton#layoutApply{background:@olive;border-color:@olive;
    color:@onOlive;}
QComboBox#layoutCombo{max-width:245px;}

/* ══ 5 · modal / settings / help / export / toast ═══════════════════
   The three dialogs are QDialogs parented on ControlPanel's own parent,
   so they only see this stylesheet once the wiring owner applies
   consoleStyleSheet() to them — see the report's handoff list. The
   rules themselves are complete so that step is the only missing one.

   `.modal-backdrop{background:rgba(0,0,0,.62)}` (:237) /
   `.modal{background:var(--surface);border:1px solid var(--line-strong);
   border-radius:10px;overflow:auto}` (:239) / `.modal-head{padding:16px
   18px;border-bottom:1px solid var(--line)}` (:240) / `.modal-body{
   padding:18px}` (:243) / `.modal-actions{gap:7px}` (:248). */

/* RESERVED: the wiring owner's overlay container names. modalBackdrop
   covers the console's client area, modalShell is the centred QFrame
   that carries the `.modal` box. */
QFrame#modalBackdrop{background:rgba(0, 0, 0, 158);}
QFrame#modalShell{background:@surface;border:1px solid @lineStrong;
    border-radius:10px;}
QFrame#modalHead{background:transparent;border:none;
    border-bottom:1px solid @line;padding:16px 18px;}
QFrame#modalBody{background:transparent;border:none;padding:18px;}
QFrame#modalActions{background:transparent;border:none;}

/* Until modalShell exists the QDialog IS the modal box. */
QDialog{background:@surface;color:@fg;font-family:@sans;}
QDialog#settingsDialog,QDialog#helpDialog,QDialog#exportDialog{
    background:@surface;border:1px solid @lineStrong;border-radius:10px;}

/* `.modal-section h3{margin:0 0 9px;font-size:12px}` (:246) — the
   dialogCap labels are the section h3's, NOT the 20px serif h2 of
   `.modal-head h2` (:241, which Qt shows as the window title bar). */
QLabel#dialogCap{font-family:@sans;font-size:12px;font-weight:600;
    color:@fg;background:transparent;}
/* `.modal-section p{margin:4px 0;color:var(--fg-3);font-size:11px;
   line-height:1.55}` (:247) */
QLabel#dialogLabel{font-size:11px;color:@fg3;line-height:1.55;
    background:transparent;}
/* `.note{font:10px var(--mono);color:var(--fg-3)}` (:226) */
QLabel#dialogNote{font-family:@mono;font-size:10px;color:@fg3;
    line-height:1.45;background:transparent;}
QLabel#dialogValue{font-family:@mono;font-size:10px;color:@fg3;
    background:transparent;text-align:right;}
/* `.modal-head .icon-btn` — the 关闭 button (:345/356) */
QPushButton#dialogClose{background:@surface2;border:1px solid @lineStrong;
    border-radius:6px;color:@fg2;font-family:@mono;font-size:11px;
    min-height:30px;padding:0 11px;}
QPushButton#dialogClose:hover{background:@surface3;color:@fg;}
/* `.modal-actions button{min-height:31px;padding:0 10px;border:1px solid
   var(--line-strong);border-radius:6px;background:var(--surface-2);
   color:var(--fg-2);font:10px var(--mono)}` (:249) with `:hover` (:250)
   and `.primary` (:251) / `.danger` (:252). 31 total - 2x1px = 29. */
QPushButton#dialogAction,QPushButton#fixMenuStateButton,
QPushButton#restoreMenuStateButton,QPushButton#installRiseReframeworkButton,
QPushButton#removeRiseLuaButton,QPushButton#removeRiseReframeworkButton{
    background:@surface2;border:1px solid @lineStrong;border-radius:6px;
    color:@fg2;font-family:@mono;font-size:10px;min-height:29px;
    padding:0 10px;}
QPushButton#dialogAction:hover,QPushButton#fixMenuStateButton:hover,
QPushButton#restoreMenuStateButton:hover,
QPushButton#installRiseReframeworkButton:hover,
QPushButton#removeRiseLuaButton:hover,
QPushButton#removeRiseReframeworkButton:hover{color:@fg;background:@surface3;}
QPushButton#installRiseReframeworkButton{
    background:@clay;border-color:@clay;color:@onClay;}
QPushButton#installRiseReframeworkButton:hover{
    background:@clayLight;border-color:@clayLight;color:@onClay;}
QPushButton#removeRiseLuaButton,QPushButton#removeRiseReframeworkButton{
    color:#f0b0a7;border-color:@rust;}
QPushButton#removeRiseLuaButton:hover,
QPushButton#removeRiseReframeworkButton:hover{background:@surface3;
    border-color:@rust;color:#f0b0a7;}

/* the Rise REFramework card that lives inside the settings dialog
   (rise_reframework_card.cpp; it is now the ONE build site). `.modal`
   has no such card in the HTML, so this stays a nested @surface3 panel
   built from the HTML's own tokens rather than a new colour. */
QFrame#riseReframeworkCard{background:@surface3;
    border:1px solid @lineStrong;border-radius:7px;}
QLabel#riseReframeworkTitle{font-family:@serif;font-size:13px;
    font-weight:500;color:@fg;background:transparent;}
QLabel#riseReframeworkSubtitle{font-size:11px;color:@fg2;
    background:transparent;}
QLabel#riseReframeworkStatus{font-family:@mono;font-size:11px;color:@fg3;
    background:transparent;}

/* help dialog table + key rows (reference.html's help modal prose) */
QLabel#helpTh{font-size:12px;font-weight:600;color:@fg;
    background:transparent;}
QLabel#helpPara{font-size:11px;color:@fg3;background:transparent;}
QLabel#helpKey{font-family:@mono;font-size:11px;color:@fg2;
    background:@surface2;border:1px solid @lineStrong;border-radius:4px;
    padding:1px 5px;}
QLabel#helpAct{font-size:11px;color:@clayLight;background:transparent;}

/* `.export-box{width:100%;min-height:230px;background:var(--board);
   color:var(--board-fg);border:1px solid var(--board-line);
   border-radius:6px;padding:10px;font:10px/1.45 var(--mono)}` (:253) */
QPlainTextEdit#exportText{background:@board;color:@boardFg;
    border:1px solid @boardLine;border-radius:6px;padding:10px;
    font-family:@mono;font-size:10px;selection-background-color:@clay;
    selection-color:@onClay;}
QPlainTextEdit#exportText:read-only{background:@board;}

/* `.toast{background:var(--surface-3);color:var(--fg);border:1px solid
   var(--line-strong);border-left:3px solid var(--clay);
   border-radius:6px;padding:10px 12px;font:11px var(--mono)}` (:254).
   The widget already carries toast_->setMargin(8) in control_panel.cpp
   and a QLabel's margin STACKS on top of QSS padding (probe6: same text,
   margin8+qss10/12 -> 99x53 vs qss-only -> 89x37, i.e. +16 = 2x8 on
   height). So the QSS supplies only the remaining 2px/4px: 8+2=10 and
   8+4=12, exactly the HTML box. */
QLabel#toast{background:@surface3;color:@fg;border:1px solid @lineStrong;
    border-left:3px solid @clay;border-radius:6px;padding:2px 4px;
    font-family:@mono;font-size:11px;}
)QSS";

} // namespace

QString consoleStyleSheet()
{
    return expand(QString::fromLatin1(kQss), palette());
}
