#include "hud_canvas.h"

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QRect>
#include <QRectF>
#include <QScreen>
#include <QScrollBar>
#include <QSize>
#include <QWheelEvent>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <concepts>

#include "panel.h"
#include "window_layout_geometry.h"
#include "panel_accent.h"
#include "panel_source.h"
#include "screen_query.h"
#include "ui_theme.h"
#include "core/string_table.h"

namespace mh {
// v0.9 i18n: same inline alias as the console / overlay panels (ODR-safe).
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

namespace {

// PRODUCT DECISION (v0.11, owner: user): the overlay's panel colours are
// FIXED and deliberately do NOT follow the console theme. A HUD floating
// on top of the game must look the same in every theme, so uiTheme() —
// which drives the control console's own chrome (surfaces, QSS, chips) —
// must never be read here.
//
// The colours are NOT re-declared here. They come from panel_accent.h,
// which is also what Panel::accentColor() reads, so the preview and the
// live overlay paint from one table by construction rather than by two
// hand-copied literals happening to agree. Before this converged the two
// had already drifted: the damage and pets slots previewed blue/green
// while the overlay drew teal, which is exactly the "预览在骗用户"
// failure a preview exists to prevent. Do NOT re-hardcode literals here
// and do NOT swap these for uiTheme() lookups — that would change the
// overlay's rendered colours, and this is a product decision, not a bug.
const std::array<QColor, mhw::kPanelCount> kAccents = {
    mhw::panel_accent::forSlot(0),
    mhw::panel_accent::forSlot(1),
    mhw::panel_accent::forSlot(2),
    mhw::panel_accent::forSlot(3),
};
// i18n: panel display names come from the console string table
// (console.panel.*) and are resolved at PAINT time, so a language switch
// only needs a repaint (ControlPanel::retranslateUi() → canvas_->update()).
// The ASCII names stay as the fallback for a missing key.
const std::array<const char *, mhw::kPanelCount> kNameKeys = {
    "console.panel.player",
    "console.panel.monster",
    "console.panel.damage",
    "console.panel.pets",
};
const std::array<const char *, mhw::kPanelCount> kNames = {
    "PLAYER", "MONSTER", "DAMAGE", "PETS",
};

static_assert(kAccents.size() == mhw::kPanelCount);
static_assert(kNameKeys.size() == mhw::kPanelCount);
static_assert(kNames.size() == mhw::kPanelCount);

// ─── v0.16: the pets slot, named ──────────────────────────────────────
//
// The ONE base panel the Split preview still draws. Every other mother
// panel has a split table (panel_sections.h: PlayerSection,
// MonsterSection, DamageSection) whose rows became the eleven
// sub-windows, so v0.14 correctly excluded its mother box from the
// Split preview. PetDamageSection has no such table — there is nothing
// to carve a pets sub-window out of — so in Split it had no
// representation at all while main.cpp kept mounting the real panel.
//
// The number 3 is the fixed slot order panel_accent.h and
// panel_sections.h already key off (see panel_source.h's
// kPanelCount comment). Spelling it here keeps the three from drifting
// apart, and gives the v0.16 gates below something to read that is not
// a literal.
constexpr int kPetsPanel = 3;
static_assert(kPetsPanel < mhw::kPanelCount,
              "the pets slot must be one of the four base panels");

// ─── v1.0.0: the prototype's mono stack ───────────────────────────────
//
// The prototype's `.win` text (`.win-name`, `.win-sc`, `.win-panel`,
// `.be-title`) all read `font-family:var(--mono)`, which the HTML
// resolves to `ui-monospace, "SF Mono", Menlo, Consolas, "Noto Sans
// Mono CJK SC", monospace`. Qt's answer to the same question is
// QFontDatabase::systemFont(FixedFont), which returns a real
// monospace on every platform we ship — the same resolution
// control_panel_styles.cpp's monoFamily() performs for the console's
// QSS. Reading it through one static keeps the card's three rows in
// ONE family instead of three fonts that happen to agree today.
//
// WHY NOT "Chakra Petch". That is the console's display face, and the
// card's rows are mono in the prototype — a proportional face makes
// `1.00x` a different width than `2.50x`, which is exactly what the
// right-aligned scale badge must not do.
QFont cardMonoFont(int pointSize)
{
    static const QString family = QFontDatabase::systemFont(
                                      QFontDatabase::FixedFont).family();
    QFont f(family, pointSize);
    f.setStyleStrategy(QFont::StyleStrategy::PreferAntialias);
    return f;
}

// WHY THIS EXEMPTION IS CHECKED AND NOT HOPED. There is exactly one
// split table per NON-pets panel — player, monster, damage — and pets
// is the fourth panel with none. That count is the whole reason pets
// is exempt, so it is asserted here: if a fourth table is ever added
// (most plausibly a pets one, the only section without a table today),
// this fires and points whoever added it at the paint gate below,
// which would then be drawing a mother panel whose blocks have already
// moved into sub-windows — the exact v0.14 defect this gate exists for.
static_assert(mhw::PlayerSection::kPlayerSplitTargetCount
                      + mhw::kMonsterSplitTargetCount
                      + mhw::DamageSplitSlot::kCount
                  == 11,
              "the eleven split sub-windows, counted from the tables");
static_assert(mhw::kPanelCount == 4
                  && mhw::PlayerSection::kPlayerSplitTargetCount > 0
                  && mhw::kMonsterSplitTargetCount > 0
                  && mhw::DamageSplitSlot::kCount > 0,
              "three of the four base panels have a split table and the "
              "fourth (pets) has none — that asymmetry is the exemption");

QString panelName(int index)
{
    const int i = mhw::isPanelIndex(index) ? index : 0;
    const QString key = QString::fromLatin1(kNameKeys[i]);
    const QString val = mh::tr(key);
    return val == key ? QString::fromLatin1(kNames[i]) : val;
}

// ─── v1.0.0: the prototype's WIN_META[].short handles ─────────────────
//
// The HTML board names a card with a SHORT handle — "info", "status",
// "bars", "hp", "share" — read from WIN_META[def.id].short, and that is
// the word the .win-name row prints. Qt has the same identity already:
// the split table row IS the window, so its row index is the same
// handle the prototype's CATALOG row is.
//
// WHY THIS IS A TABLE AND NOT A SETTINGS-KEY LOOKUP. The window's own
// Panel::settingsKey() would be the obvious source ("player-info",
// "monster-identity"), but it is not reachable from here: the canvas
// holds a PanelSource*, which carries geometry and nothing else, and
// widening that interface for one display string would put the console's
// persistence vocabulary into the paint path. The (parentSlot, tableRow)
// pair the binding already carries is the same identity, and this table
// is its spelling.
//
// ASCII ON PURPOSE, exactly like kNames: these are mono glyphs drawn
// inside a card whose width is the window's own content width, and a
// localized long name would be elided to nothing on the narrow strips
// (the 翔虫 window is 520x220 logical, and at a 0.33 fit that is ~62
// device px of card). The prototype's `short` is English in both
// locales for the same reason — its own zh table spells
// WIN_META["player-info"].short as "info".
struct WinShortEntry {
    int parentSlot;
    int row;
    const char *shortName;
};

constexpr WinShortEntry kWinShorts[] = {
    // kPlayerSplitTargets, in the table's own order:
    //   row 0 Debuff|Buff, row 1 Mantles, row 2 Wirebug, row 3 Bars,
    //   row 4 Weapon|Sharpness, row 5 Conn|Quest
    {0, 0, "status"},
    {0, 1, "mantles"},
    {0, 2, "wirebug"},
    {0, 3, "bars"},
    {0, 4, "weapon"},
    {0, 5, "info"},
    // kMonsterSplitTargets: row 0 Identity, row 1 Conditions
    {1, 0, "identity"},
    {1, 1, "conds"},
    // kDamageSplitTargets: row 0 Share, row 1 Chart, row 2 Rows
    {2, 0, "share"},
    {2, 1, "chart"},
    {2, 2, "rows"},
};

// The four mother panels' handles, in the fixed slot order.
const std::array<const char *, mhw::kPanelCount> kPanelShorts = {
    "player", "monster", "damage", "pet",
};

// One row of the table above, or the empty string for an index pair the
// tables do not have. Written as a linear walk over 11 entries rather
// than a lookup structure because 11 comparisons is cheaper than a hash
// build and the table is compile-time constant.
QString winShortFor(int parentSlot, int row)
{
    for (const WinShortEntry &e : kWinShorts) {
        if (e.parentSlot == parentSlot && e.row == row)
            return QString::fromLatin1(e.shortName);
    }
    return {};
}

constexpr int kHeader = 56;
constexpr int kFooter = 38;
// v1.0.0: THE PROTOTYPE'S OWN STEPS, NOT ROUND NUMBERS.
//
//     if(e.shiftKey){dx*=10;dy*=10;}
//
// so a plain arrow moves the window 1px and Shift+arrow moves it 10px,
// in the window's LOGICAL space (the same space the drag converts to —
// see mouseMoveEvent's qRound(delta / fit)). The 10/50 these used to be
// was the console's own invention and it was two orders of magnitude
// coarser than the HTML's: a single tap teleported a window across a
// quarter of the board, so the fine adjustment the keys exist for was
// not possible at all. The clamp in movedMargins()/movedSubMargins() is
// unchanged — only the delta that reaches it is smaller.
constexpr int kArrowStep = 1;       // logical px per arrow press
constexpr int kArrowBigStep = 10;   // with Shift

// v0.8: removed the unused file-scope screenInfo() shim — every caller
// now goes through HudCanvas::previewScreenInfo(), which honours the
// user's output selection. See hud_canvas.h::setPreviewScreen.

// ── v1.0.0 integration (stage D): ONE FORMULA, SHARED ───────────────────
// This used to be the only copy of the four-corner arithmetic. The
// inspector's X/Y fields now have to convert the SAME way (a field means
// "logical top-left in the target output", which is NOT `margins.left()`
// for three of the four corners), and two copies of a four-way switch is
// exactly how the field and the box drift apart again.
//
// The whole body moved verbatim into ui/window_layout_geometry.h — same
// `max(1, int(content * scale))` extent, same screen.left()/right()/
// bottom() edges, same QRect(tl, extent) — so this is a delegation, not
// a rewrite: the default path is byte-for-byte what it was, including
// the rounding, and the 12 baselines that read this rect are the proof.
QRect anchoredRect(const QRect &screen, Corner corner, const QMargins &m,
                   const QSize &content, qreal scale)
{
    return mhw::layout::anchoredRect(screen, corner, m, content, scale);
}

QString cornerName(Corner c)
{
    // i18n: corner labels (console.corner.*) with an ASCII fallback.
    struct Entry { Corner corner; const char *key; const char *ascii; };
    static const Entry kCorners[] = {
        {Corner::TopLeft,     "console.corner.top_left",     "TOP LEFT"},
        {Corner::TopRight,    "console.corner.top_right",    "TOP RIGHT"},
        {Corner::BottomLeft,  "console.corner.bottom_left",  "BOTTOM LEFT"},
        {Corner::BottomRight, "console.corner.bottom_right", "BOTTOM RIGHT"},
    };
    for (const Entry &e : kCorners) {
        if (e.corner != c)
            continue;
        const QString key = QString::fromLatin1(e.key);
        const QString val = mh::tr(key);
        return val == key ? QString::fromLatin1(e.ascii) : val;
    }
    const QString key = QStringLiteral("console.corner.unknown");
    const QString val = mh::tr(key);
    return val == key ? QStringLiteral("UNKNOWN") : val;
}

} // namespace

// ─── v1.0.0: the prototype's --clay-light token, both themes ───────────
//
// The prototype's board is a PICTURE OF A GAME OVERLAY ON A DESKTOP, so
// its --board-* colours are pinned: they must not read the console
// theme, or the board's own pixels would change when the operator
// switches the console's chrome (see the PRODUCT DECISION above, and
// paintWindowCard's note on why --fg measured 1.11:1 on #101010).
//
// --clay-light is the ONE board colour the prototype itself switches
// per theme (reference.html L32 / L62):
//
//     :root                    --clay-light:#e39a7e   (227,154,126)
//     :root[data-theme=light]  --clay-light:#b85c38   (184, 92, 56)
//
// and .board-window.selected { border:2px solid var(--clay-light) } is
// the selected window's whole identity — the probe read a constant
// QColor(227,154,126) in both themes and saw 0 differing pixels.
// So this branches on the SAME predicate the rest of the console
// branches on (isDarkTheme(), the one uiTheme() is selected by), and
// the two values below are that token's two values, transcribed from
// the HTML rather than invented. Reading the palette through the same
// predicate — rather than hardcoding a third dark-only literal — is
// what makes a theme switch actually reach the board.
//
// NOTHING ELSE HERE MOVES. The 1px resting .board-window border is
// rgba(224,138,108,.75) in the HTML for BOTH themes (no var() at all),
// the selection ring is a literal rgba(217,119,87,.35) in both themes,
// and the dashed edit-mode border only changes its STYLE — so those
// stay pinned and only the clay-light stroke follows the theme.
QColor boardClayLight()
{
    return isDarkTheme() ? QColor(227, 154, 126)   // #e39a7e
                         : QColor(184, 92, 56);    // #b85c38
}

// ─── v1.0.0: the FROZEN board's own tokens ─────────────────────────────
//
// Every value below is transcribed from `reference.html` line by line.
// These four are the whole reason the fidelity board reads as the
// prototype's board and not as the old stage recoloured: `--board` is a
// flat `#101010` in BOTH themes (lines 39 and 69 are identical), the
// border is `--board-line` (40/70), and the ground OUTSIDE the board is
// `--page`, which is the one board-adjacent colour that DOES move with
// the theme (21 / 51).

// `.board { background: var(--board) }` — the same value in both themes.
QColor boardGround()
{
    return QColor(0x10, 0x10, 0x10);
}

// `.board { border: 1px solid var(--board-line) }` — same in both themes.
QColor boardLine()
{
    return QColor(0x2e, 0x2e, 0x2a);
}

// `.board-viewport` sits on `.stage-body { background: var(--page) }`,
// so the canvas's own ground around the board is the PAGE token and it
// is the one that follows the theme. Reading it through isDarkTheme()
// rather than pinning the dark value is what keeps a light console from
// growing a black ring around a black board.
QColor boardPageGround()
{
    return isDarkTheme() ? QColor(0x18, 0x18, 0x16)   // #181816
                         : QColor(0xf4, 0xf2, 0xed);  // #f4f2ed
}

// `.board.editing { border-color: var(--clay) }` — DARK #d97757, LIGHT
// #9c4623 (lines 31 / 61). Note this is --clay, NOT --clay-light: the
// edit-mode board frame is the duller of the two, and the two are 0x9c
// apart in the red channel, so confusing them is visible at a glance.
//
// PAIRED WITH, AND NOT THE SAME AS, the inset band below.
QColor boardClay()
{
    return isDarkTheme() ? QColor(0xd9, 0x77, 0x57)   // #d97757
                         : QColor(0x9c, 0x46, 0x23);  // #9c4623
}

// ─── the three box-shadow layers, as painters ──────────────────────────
//
// `.board`'s shadow line is
//     box-shadow: 0 0 0 1px rgba(255,255,255,.03),
//                 inset 0 0 0 12px rgba(255,255,255,.015);
// and `.board.editing`'s is
//     box-shadow: 0 0 0 1px var(--clay),
//                 inset 0 0 0 12px rgba(217,119,87,.06);
// Three distinct regions and one detail worth spelling out: the INNER
// band is a hardcoded rgba(217,119,87,.06), NOT var(--clay) — so in the
// light theme the border and the outer ring go to #9c4623 while the
// inset band stays warm orange at 6%. Transcribing that literally is the
// difference between matching the CSS and matching an approximation of
// it.
constexpr qreal kBoardInset = 12.0;   // the 12px inset band
// v1.0.1 (#6): the alpha-read checker painted inside the fidelity
// board's inset rect. Purely a console-preview aid — the real HUD never
// sees it and no pixel baseline measures it. The alphas are baked into
// the colours, never QPainter::setOpacity (see the paint note: the
// HUD's opacity is already in its pixmap's alpha and a second opacity
// multiply is the double-alpha bug this fix must not reintroduce).
constexpr qreal kBoardCheckerCell = 16.0;    // 16px cells
constexpr int kBoardCheckerLightAlpha = 16;  // white @ ~6%
constexpr int kBoardCheckerDarkAlpha = 32;   // black @ ~12.5%

// `.board.editing::before` — the lattice. `inset:12px` puts it on the
// same rect the inset band frames, `opacity:.28` multiplies everything
// below, and `background-size:5% 5%` makes it a 20x20 cell grid.
constexpr qreal kBoardGridOpacity = 0.28;
constexpr qreal kBoardGridLineAlpha = 0.08;   // rgba(255,255,255,.08)
constexpr int kBoardGridCells = 20;           // 100% / 5%
// The line's final alpha. Compositing .08 white at .28 opacity is what
// the browser does; resolving it to ONE number here means the painter
// never has to lean on QPainter::setOpacity and therefore never has to
// save/restore around a line the prototype would have drawn flat.
constexpr int kBoardGridLineAlphaInt =
    int(255.0 * kBoardGridLineAlpha * kBoardGridOpacity + 0.5);   // 6

// The `.board` outer ring, one pixel OUTSIDE the border.
constexpr qreal kBoardRingWidth = 1.0;

// The `.board-viewport` padding, which is what the canvas widget itself
// stands for in fidelity mode: `padding:12px 14px 0`. The zero at the
// bottom is the HTML's, not an omission — `.board-viewport` has no
// bottom pad and the `.board-hint` row that follows it is a sibling
// widget (ControlPanel's boardHint label), not part of the canvas.
constexpr qreal kBoardPadTop = 12.0;
constexpr qreal kBoardPadX = 14.0;

// `.board` vs `.board.portrait`, as WIDTH caps.
constexpr qreal kBoardMaxWidthLandscape = 1050.0;   // min(100%, 1050px)
constexpr qreal kBoardMaxWidthPortrait = 560.0;     // min(48%, 560px)
constexpr qreal kBoardPortraitFraction = 0.48;      // the 48%
constexpr qreal kBoardPortraitMinHeight = 220.0;    // min-height:220px

// `.board-label { font:10px var(--mono) }` and `.board-empty`'s
// `font:11px var(--mono)` are the two sizes the file already maps
// through cardMonoFont(8); the empty state's `<strong>` is
// `font:15px var(--serif)`, the one place on the board where the serif
// stack is used at all.
constexpr int kBoardMonoPt = 8;        // 10-11px mono  → 8pt
constexpr int kBoardEmptyTitlePt = 11; // 15px serif    → 11pt

// `.board-empty { padding:24px }` and `<strong>{margin-bottom:6px}`.
constexpr qreal kBoardEmptyPad = 24.0;
constexpr qreal kBoardEmptyGap = 6.0;

// The display the board describes, as `.board-portrait`'s toggle sees
// it. reference.html:474 writes `classList.toggle('portrait', sc.rot)`
// and its two displays are 2560x1440 (rot:false) and 1080x1920
// (rot:true) — so the only thing `rot` can mean, for a board that reads
// geometry rather than a hardcoded display table, is "this output is
// taller than it is wide". That is the predicate below, deliberately
// derived from the real output's logical size instead of hardcoding the
// demo's DP-1 / HDMI-A-1 names.
bool isPortrait(const QSize &physical)
{
    return physical.width() > 0 && physical.height() > physical.width();
}

// The real output's aspect ratio, with the guard the legacy arm of
// screenFrame() never needed because it could assume a screen existed.
qreal physicalAspect(const QSize &physical)
{
    return (physical.width() > 0)
               ? physical.height() / qreal(physical.width())
               : (9.0 / 16.0);
}

// ─── v0.13: the split's sub-window preview ─────────────────────────────

namespace {

// The row's own section label, in the active locale, read from the
// console's EXISTING section table (console.section.*) — so the split
// preview names a window with the same word the inspector's switch row
// uses, and adds no i18n key of its own.
//
// DERIVED FROM THE TABLE, NOT LISTED: the caller passes a table ROW and
// this reads that row's mask out of the matching table. A row that
// claims several sections (player row 0 owns Debuff|Buff, row 4 owns
// Weapon|Sharpness, row 5 owns Conn|Quest) is labelled by its LOWEST
// bit — one deterministic rule, chosen because the row is one window
// that draws all of its bits together and therefore needs one name.
// An out-of-range row answers an empty string rather than reading past
// the table, exactly as xxxSubWindowMask() does.
QString subWindowLabel(int parentSlot, int row)
{
    // Lowest set bit of `mask`, as a section-bit index, or -1 for none.
    const auto lowestBit = [](uint32_t mask) -> int {
        if (mask == 0)
            return -1;
        int n = 0;
        while ((mask & 1u) == 0u) {
            mask >>= 1;
            ++n;
        }
        return n;
    };

    uint32_t mask = 0;
    if (parentSlot == 0) {
        if (row < 0 || row >= mhw::PlayerSection::kPlayerSplitTargetCount)
            return {};
        mask = mhw::PlayerSection::kPlayerSplitTargets[row].mask;
    } else if (parentSlot == 1) {
        if (row < 0 || row >= mhw::kMonsterSplitTargetCount)
            return {};
        mask = mhw::kMonsterSplitTargets[row].mask;
    } else if (parentSlot == 2) {
        if (row < 0 || row >= mhw::DamageSplitSlot::kCount)
            return {};
        mask = mhw::kDamageSplitTargets[row].mask;
    } else {
        return {};   // pets has no split table
    }

    const int bit = lowestBit(mask);
    if (bit < 0)
        return {};
    // The per-panel tables' own displayName() answers the row's lowest
    // bit in the active locale. Reading them here rather than through
    // console_text_helpers::sectionLabel() keeps this file's include
    // list at the tables it already needs (panel_sections.h) instead of
    // adding a viewmodel header to the paint path for one string.
    switch (parentSlot) {
    case 0:
        return mhw::PlayerSection::displayName(bit);
    case 1:
        return mhw::MonsterSection::displayName(bit);
    case 2:
        return mhw::DamageSection::displayName(bit);
    default:
        return {};
    }
}

} // namespace

// ─── construction ───────────────────────────────────────────────────────

HudCanvas::HudCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(520, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::ClickFocus);   // receive arrow keys after click

    // v0.11.4: monitor hot-plug invalidates the preview screen cache
    // without the user touching anything. Hyprland keeps the user's
    // side plugged into a dock/external display, so "plugin a new
    // monitor" is a real, routine case — the old process-wide static
    // tables would have shown the pre-plug topology forever.
    // screenAdded/screenRemoved fire on both adding and re-adding, so
    // this covers the unplug-then-replug case too.
    connect(static_cast<QGuiApplication *>(QCoreApplication::instance()),
            &QGuiApplication::screenAdded,
            this, [this] { rebuildScreenCache(); refreshViewportMetrics(); updateGeometry(); update(); });
    connect(static_cast<QGuiApplication *>(QCoreApplication::instance()),
            &QGuiApplication::screenRemoved,
            this, [this] { rebuildScreenCache(); refreshViewportMetrics(); updateGeometry(); update(); });
    // v0.15: the PRIMARY can also change with no screen coming or going
    // — a compositor setting, a kscreen re-arrangement, a headless
    // session handing primary to a different output. screenAdded /
    // screenRemoved do not fire for that, and the cache is not the only
    // thing that goes stale: Panel::marginLimits() reads
    // primaryScreen() LIVE on every call, while our tables are a
    // snapshot. Left unconnected, the two resolve different answers for
    // every panel and every sub-window that follows primary, which is
    // the same "canvas and panel disagree about which screen a window
    // lives on" divergence this file already has a measured case of —
    // just triggered by a settings change instead of a cable.
    //
    // rebuildScreenCache() is the whole response: it clears
    // screenCacheByName_ AND sets screenFallbackValid_ = false, so the
    // next primaryScreenInfo() re-runs detect() against the new primary
    // instead of serving the one cached for the old one.
    connect(static_cast<QGuiApplication *>(QCoreApplication::instance()),
            &QGuiApplication::primaryScreenChanged,
            this, [this] { rebuildScreenCache(); refreshViewportMetrics(); updateGeometry(); update(); });
}

// ─── public API ─────────────────────────────────────────────────────────

void HudCanvas::setPanelPixmap(int index, const QPixmap &pixmap, bool enabled)
{
    if (!mhw::isPanelIndex(index)) return;
    slots_[index].pixmap = pixmap;
    slots_[index].enabled = enabled;
    update();
}

void HudCanvas::setPanelPresent(int index, bool present)
{
    if (!mhw::isPanelIndex(index)) return;
    if (slots_[index].present == present) return;
    slots_[index].present = present;
    update();
}

bool HudCanvas::panelPresent(int index) const
{
    return mhw::isPanelIndex(index) && slots_[index].present;
}

void HudCanvas::setShowSafeArea(bool on)
{
    if (showSafeArea_ == on) return;
    showSafeArea_ = on;
    update();
}

void HudCanvas::setShowGrid(bool on)
{
    if (showGrid_ == on) return;
    showGrid_ = on;
    update();
}

// ─── v1.0.0: the board's edit state and the card's three strings ──────
//
// setEditing() is the one entry point the console calls when the
// prototype's 编辑布局 button toggles. It is a plain flag plus a repaint
// because that is all the state there is: the grid, the board border
// colour and the cards' dash pattern are all READ from it at paint time
// (see paintEvent), so there is no second copy to keep in step.
//
// The no-op guard is the same one every other setter here uses: a
// console that calls setEditing(true) on every toolbar repaint must not
// schedule a repaint each time.
//
// v1.0.0: AND THE MODE SWITCH INVALIDATES THE GESTURE IN FLIGHT — it
// does not wait for the release. Leaving edit mode drops the drag
// state (see the body), so a press that started while editing cannot
// be finished by a later motion once the mode has been toggled off
// and on again. That is the prototype's own shape: `state.editing` is
// the only writer and setupDrag() re-runs from scratch on the next
// press, so no gesture outlives the mode it was validated in.
void HudCanvas::setEditing(bool on)
{
    if (editing_ == on)
        return;
    editing_ = on;
    // v1.0.0: THE DRAG IS SCOPED TO ONE EDITING SESSION.
    //
    // The press validated `editing_` once and mouseMoveEvent re-reads
    // it on every motion — but "the flag is true again" is not the
    // same as "THIS gesture was allowed". Press while editing → Esc →
    // edit again → the next motion satisfies both gates and FINISHES
    // the drag the user already cancelled, emitting subWindowMoved()
    // from a start point captured two mode changes ago. The probe
    // reproduced exactly that: press, setEditing(false),
    // setEditing(true), move → subWindowMoved fired.
    //
    // So the drag state is torn down with the mode, the same way
    // setLayoutMode(Single) already tears it down when the sub-windows
    // it indexes go away: a live drag is only ever valid inside the
    // editing session that started it. This is NOT a global ban on
    // dragging — a fresh press in edit mode starts a fresh drag, which
    // is the only gesture the prototype recognises.
    //
    // Nothing else is cleared: the selection stays (selecting is not
    // editing, and a click that also armed a drag keeps what it
    // selected), and mouseReleaseEvent still clears the cursor and
    // both flags, so a cancelled gesture ends in the same consistent
    // state it always did — the only thing removed is the window in
    // which a cancelled gesture stays live.
    subDragging_ = false;
    subDragIndex_ = -1;
    dragging_ = false;
    dragIndex_ = -1;
    unsetCursor();
    update();
}

QString HudCanvas::winName(int panelIndex, int subIndex) const
{
    // A sub-window is named by its split-table row; a mother panel by
    // its own handle. subIndex >= 0 is the discriminator, matching
    // windowOutputName()'s convention so the two accessors read alike.
    if (subIndex >= 0) {
        if (subIndex >= subSlots_.size())
            return {};
        return winShortFor(subSlots_[subIndex].parentSlot,
                           subSlots_[subIndex].tableRow);
    }
    if (!mhw::isPanelIndex(panelIndex))
        return {};
    return QString::fromLatin1(kPanelShorts[panelIndex]);
}

QString HudCanvas::winScale(int panelIndex, int subIndex) const
{
    // The window's OWN scale, read through the same PanelSource the
    // geometry path reads — so the badge can never disagree with the
    // box's size, which is the same "one mapping" rule screenFrame()
    // exists for. A window that is not bound has no scale, and the
    // prototype's own default is 1.
    const PanelSource *src = nullptr;
    if (subIndex >= 0) {
        if (subIndex < subSlots_.size())
            src = subSlots_[subIndex].src;
    } else if (mhw::isPanelIndex(panelIndex)) {
        src = slots_[panelIndex].src;
    }
    if (!src)
        return QStringLiteral("1.00x");
    return QStringLiteral("%1x").arg(std::max(0.05, src->scale()), 0, 'f', 2);
}

QString HudCanvas::winPanelName(int panelIndex, int subIndex) const
{
    // The owning panel's DISPLAY name, localized. For a sub-window that
    // is the panel it was carved out of — the question the prototype's
    // .win-panel answers with `T("panel."+def.panel)`, and the reason
    // the third row exists at all: the overview spans every panel, so a
    // card has to say whose it is.
    if (subIndex >= 0) {
        if (subIndex >= subSlots_.size())
            return {};
        return panelName(subSlots_[subIndex].parentSlot);
    }
    return panelName(panelIndex);
}

int HudCanvas::subWindowCountForParent(int parentSlot) const
{
    if (parentSlot < 0)
        return 0;
    int n = 0;
    for (const SubSlot &s : subSlots_)
        if (s.parentSlot == parentSlot)
            ++n;
    return n;
}

// The four-slot arm's painted boxes, by panel index. SEE THE HEADER for
// why this exists; the body is deliberately trivial — it is a read of
// what paintEvent already decided (line ~1911 resets every entry, the
// mother loop writes the ones it drew), not a second layout, so the
// rects a test reads are the rects the click handler hit-tests.
QVector<QRectF> HudCanvas::motherWindowRects() const
{
    QVector<QRectF> out;
    out.reserve(mhw::kPanelCount);
    for (int i = 0; i < mhw::kPanelCount; ++i)
        out.append(slots_[i].lastTarget_);
    return out;
}

void HudCanvas::setSelectedPanel(int index)
{
    if (!mhw::isPanelIndex(index) || selected_ == index) return;
    selected_ = index;
    update();
}

void HudCanvas::bindPanel(int index, const PanelSource *src)
{
    if (!mhw::isPanelIndex(index)) return;
    slots_[index].src = src;
    slots_[index].bound = (src != nullptr);
    update();
}

// ─── v0.13 split-mode preview ──────────────────────────────────────────
//
// The mode is a WHOLE-CANVAS decision, not a per-panel one: every
// sub-window of a split launch exists because the layout is split, and
// switching to Single removes all of them at once. So the setter takes
// no index and clears rather than toggling one entry.
void HudCanvas::setLayoutMode(mhw::PlayerSection::PlayerLayoutMode mode)
{
    if (layoutMode_ == mode)
        return;
    layoutMode_ = mode;
    // THE SINGLE-MODE GUARANTEE, in one line. Entering Single drops
    // every sub-window, so the four-slot paint path below is the only
    // thing that runs and its output is what it was before this feature
    // existed. A caller that forgets this line would leave stale boxes
    // on top of the single-panel preview — which is exactly what
    // hud_canvas_split_tests.cpp pins by asserting the vector is empty
    // after the switch (and the mutation test confirmed it FAILS when
    // this line is removed).
    if (mode == mhw::PlayerSection::PlayerLayoutMode::Single) {
        subSlots_.clear();
        // v0.14: the drag state goes with them, for the same reason the
        // vector does. A drag still in flight when the mode flips would
        // otherwise keep a live subDragIndex_ into a vector the next
        // setSubWindowBindings() replaces, and the release would then
        // move whatever window inherited that index.
        subDragging_ = false;
        subDragIndex_ = -1;
        subSelected_ = -1;
        subRects_.clear();
        subRects_.squeeze();
        unsetCursor();
    }
    update();
}

void HudCanvas::setSubWindowBindings(QVector<SubWindowBinding> bindings)
{
    // Never accumulate: the caller owns the derivation and passes the
    // full set each time, so a replace is the only correct write. Keeping
    // old entries would draw windows whose parent window is gone.
    subSlots_.clear();
    subSlots_.reserve(bindings.size());
    for (const SubWindowBinding &b : bindings) {
        if (!b.src)
            continue;   // a row whose window did not resolve: absent, not blank
        SubSlot slot;
        slot.src = b.src;
        // Provenance travels with the binding so the paint side never has
        // to guess which parent a box belongs to.
        slot.parentSlot = b.parentSlot;
        slot.tableRow = b.tableRow;
        subSlots_.append(slot);
    }
    // v1.0.0: AND THE SELECTION DROPS OUT OF RANGE WITH THEM. A replace
    // can shrink the vector (a panel un-split, a section masked off), and
    // the selection it leaves behind would then point past the end —
    // where every consumer reads: subRects_[i] in the hit-test,
    // subSlots_[i].src in the drag start, movedSubMargins()'s own bounds
    // check, and the arrow-key arm's index guard. Clearing here makes the
    // setter's own contract ("an out-of-range index clears") true of the
    // path that produces out-of-range indices in the first place, rather
    // than leaving it to whichever consumer happens to check first.
    //
    // Same reason as setLayoutMode(Single): no repaint of its own, the
    // update() below covers it.
    if (subSelected_ >= subSlots_.size())
        subSelected_ = -1;
    // ── v1.0.0 integration (stage A / O2 review ②) ──────────────────────
    // AND THE DRAG INDEX GOES WITH IT, for the same reason as the
    // selection above: the bindings it indexes were just replaced, so an
    // index kept across a replace points at whatever window landed there
    // next and mouseMoveEvent's sub arm would keep writing THAT window's
    // margins from this gesture's start point. The selection arm's own
    // note lists the four consumers that read an out-of-range index; the
    // drag adds a fifth (movedSubMargins via subDragIndex_). Clearing the
    // flags rather than only the index means a release that arrives after
    // the replace finds nothing to clean up — a rebind cancels the
    // gesture outright, which is the only honest answer to "whose window
    // am I moving?". Does NOT touch `dragging_`/`dragIndex_`: those index
    // the four mother panels, which setSubWindowBindings() does not
    // replace (setLayoutMode() owns that vector).
    subDragging_ = false;
    subDragIndex_ = -1;
    update();
}

void HudCanvas::setSubWindowPixmap(int index, const QPixmap &pixmap, bool enabled)
{
    if (index < 0 || index >= subSlots_.size())
        return;   // not our window; ignore rather than grow the vector
    subSlots_[index].pixmap = pixmap;
    subSlots_[index].enabled = enabled;
    update();
}

// ─── v1.0.0: the console writes the selection back ─────────────────────
//
// The one public write for `subSelected_`, so the inspector and the
// board cannot disagree. See the header for the full contract; the
// shape here is:
//
//   1. normalise out-of-range to -1, never ignore it. A rebuilt binding
//      list leaves the previous selection pointing past the end, and an
//      index the drag/arrow paths would act on is the bug this avoids.
//   2. no-op guard, same as every other setter here.
//   3. update() ONLY. No signal — see the header on why emitting
//      subWindowSelected from here would close the loop.
void HudCanvas::setSelectedSubWindow(int index)
{
    const int want = (index >= 0 && index < subSlots_.size()) ? index : -1;
    if (subSelected_ == want)
        return;
    subSelected_ = want;
    update();
}

// ─── v1.0.0: the console-fidelity flag ─────────────────────────────────
//
// A paint-and-geometry switch, nothing more. It does not read settings,
// it is not wired to the stagebar, and the overlay never sees it: the
// console turns it on once at construction and every effect is a branch
// inside this file.
//
// updateGeometry() is here and not an optimisation. The fidelity board
// is `width:min(100%,1050px)` with `aspect-ratio:16/9`, so the height
// the layout wants is a different function of the width than the legacy
// `820 * physAR + header + footer` — and a QScrollArea that has already
// sized the widget will not re-ask until the widget says its hint
// changed.
void HudCanvas::setConsoleFidelityMode(bool on)
{
    if (on == consoleFidelity_)
        return;
    consoleFidelity_ = on;
    // The flag swaps the whole fitting rule (board-viewport vs legacy
    // stage), so the fixed-mode CONTENT extent moves with it.
    refreshViewportMetrics();
    updateGeometry();
    update();
}

// ─── v1.0.1 (#10): hide the in-board `.board-label` copy ───────────────
//
// Paint-only, console-only, DEFAULT VISIBLE — see the header note for
// why the top bar's display picker stays the single full copy and this
// one may go. Not gated on consoleFidelity_ in the SETTER: the flag is
// state either way, and only the fidelity arm reads it, so forcing the
// value outside fidelity mode cannot move a single legacy pixel (that
// arm never painted the label).
void HudCanvas::setFidelityBoardLabelVisible(bool visible)
{
    if (visible == boardLabelVisible_)
        return;
    boardLabelVisible_ = visible;
    update();
}

// ─── v1.0.0: window visibility, consumed ───────────────────────────────
//
// The setter is deliberately boring: bounds-check, no-op on an unchanged
// value, repaint. Everything interesting is in what the flag DOES — see
// layoutSubWindows() for the single gate that takes a hidden window out
// of the layout, the paint and the hit-test all at once.
//
// A REBIND RESETS THIS, deliberately: setSubWindowBindings() builds a
// fresh QVector<SubSlot> and a fresh SubSlot is visible. The shell has
// to push its filter again after every rebuild — carrying a per-INDEX
// visibility across a rebind would apply whatever window was at slot A
// last time's filter to whatever window lands at A next, which is worse
// than forgetting the filter entirely.
void HudCanvas::setSubWindowVisible(int index, bool visible)
{
    if (index < 0 || index >= subSlots_.size())
        return;   // unknown index: a safe no-op, never a growth
    if (subSlots_[index].visible == visible)
        return;
    subSlots_[index].visible = visible;
    update();
}

bool HudCanvas::subWindowVisible(int index) const
{
    if (index < 0 || index >= subSlots_.size())
        return true;   // not a live window: the DEFAULT, not a guess
    return subSlots_[index].visible;
}

void HudCanvas::setPanelWindowVisible(int panelIndex, bool visible)
{
    if (!mhw::isPanelIndex(panelIndex))
        return;   // unknown index: a safe no-op
    if (panelVisible_[panelIndex] == visible)
        return;
    panelVisible_[panelIndex] = visible;
    update();
}

bool HudCanvas::panelWindowVisible(int panelIndex) const
{
    if (!mhw::isPanelIndex(panelIndex))
        return true;   // not a live panel: the DEFAULT
    return panelVisible_[panelIndex];
}

// ─── v0.14: the overlap-free wall ──────────────────────────────────────
//
// Read hud_canvas.h's comment on this first — it argues why a per-corner
// grid and not the vertical stack this replaces.
//
// THE ARITHMETIC, in one line: with `ordinal` the window's position among
// the sub-windows that share its corner,
//
//     row = ordinal % kSubWallColumns      column = ordinal / columns
//
// and each box occupies its own cell, so two different ordinals land in
// different cells and their rects cannot intersect. That is the whole
// non-overlap proof, and it is why the columns constant is asserted by
// the test rather than left implicit.
//
// THE UNIT IS LOGICAL PIXELS. Margins are what a Panel persists, so
// laying out in margin space means a drag delta needs no second
// conversion and the wall's extent is independent of the widget's
// current size — the property the live overlay's own cascade comment
// demands of a default placement ("needs no screen size, no output
// enumeration, and no per-window magic").
QRectF HudCanvas::subWindowGridRect(Corner corner, int ordinal,
                                    const QSize &logical, const QSize &cell)
{
    const int w = qMax(1, logical.width());
    const int h = qMax(1, logical.height());

    // The cell is sized by the window that goes into it, not by a
    // constant. The gap rides along with the extent so a wide window does
    // not end up flush against the next cell's window — which is the same
    // defect one notch apart from the one the constant caused.
    const int cw = cell.width() > 0 ? cell.width() + kSubWallGap
                                    : kSubWallCellW;
    const int ch = cell.height() > 0 ? cell.height() + kSubWallGap
                                     : kSubWallCellH;

    const int row = ordinal % kSubWallColumns;
    const int col = ordinal / kSubWallColumns;

    // Primary axis: the one pointing AWAY from the anchored screen edge,
    // walked column by column. Secondary axis: across the column.
    //
    // The direction is chosen per corner rather than derived, because a
    // TopRight wall must grow leftwards from the right edge while a
    // TopLeft one grows rightwards from the left; getting that wrong is
    // how the previous layout ended up drawing every cluster against
    // the canvas's top edge regardless of which panel it came from.
    const int inward = kSubWallOrigin + kSubWallOffset;
    int x = 0;
    int y = 0;
    switch (corner) {
    case Corner::TopLeft:
        x = kSubWallOrigin + kSubWallOffset + col * cw;
        y = kSubWallOrigin + kSubWallOffset + row * ch;
        break;
    case Corner::TopRight:
        x = w - inward - cw - col * cw;
        y = kSubWallOrigin + kSubWallOffset + row * ch;
        break;
    case Corner::BottomLeft:
        x = kSubWallOrigin + kSubWallOffset + col * cw;
        y = h - inward - ch - row * ch;
        break;
    case Corner::BottomRight:
        x = w - inward - cw - col * cw;
        y = h - inward - ch - row * ch;
        break;
    }
    Q_UNUSED(inward)
    return QRectF(x, y, cw, ch);
}

// The wall for the sub-windows currently bound, in LOGICAL screen
// pixels, indexable by the same index the matching binding was passed
// at. Empty in Single mode.
//
// GROUPED BY CORNER, NOT BY PARENT. Player and monster sub-windows are
// both anchored TopLeft, so they share one wall and must share one
// ordinal counter — counting per parent would hand two of them the
// same ordinal and place them in the same cell. The corner is the thing
// that decides which region of the screen a window can be in, so it is
// the right thing to partition on; parentSlot survives as the accent
// colour and the title.
QVector<QRectF> HudCanvas::subWindowWall(const QSize &logical) const
{
    QVector<QRectF> out;
    if (subSlots_.isEmpty())
        return out;
    out.resize(subSlots_.size());

    // ONE LATTICE FOR THE WHOLE WALL, not one per corner.
    //
    // The cell is sized by the largest window anywhere in the split, and
    // the same lattice is used by every corner. Two things force that, and
    // both were measured as overlapping pairs before they were fixed:
    //
    // 1. PER-WINDOW SIZING IS NOT ENOUGH, because of the lattice itself. A
    //    cell's position depends on the cells before it, so if each cell
    //    were sized by its own window, a column's x would advance by a
    //    different width on every step while every box in that column is
    //    drawn at that same x. Twelve overlapping pairs became ten.
    //
    // 2. PER-CORNER SIZING IS NOT ENOUGH, because the two sides of the
    //    screen are one screen. A TopLeft cluster walks rightwards from the
    //    left edge and a TopRight one leftwards from the right edge; with
    //    independent pitches they are not complements of each other, and
    //    they meet and overlap in the middle. That is what the last two
    //    pairs were: a TopLeft window in column 2 at x=1296 and a TopRight
    //    one in its own column 0 at x=1536, sharing 120px. Ten became two.
    //
    // A shared pitch makes the left walk and the right walk mirror images
    // of the same lattice, so a column counted from the left edge and a
    // column counted from the right edge can only meet if they are the same
    // column — which is still the same cell, and the corner's own column
    // count is what keeps them from being.
    int maxW = 0;
    int maxH = 0;
    for (const SubSlot &sub : subSlots_) {
        if (!sub.src)
            continue;
        maxW = qMax(maxW, qRound(sub.src->contentSize().width()
                                 * sub.src->scale()));
        maxH = qMax(maxH, qRound(sub.src->contentSize().height()
                                 * sub.src->scale()));
    }
    const QSize lattice(maxW, maxH);

    // per-corner ordinal counter
    QHash<int, int> ordinal;
    for (int i = 0; i < subSlots_.size(); ++i) {
        const SubSlot &sub = subSlots_[i];
        if (!sub.src)
            continue;   // an unresolved row: no box, keeps the index space
        const Corner corner = sub.src->corner();
        const int c = static_cast<int>(corner);
        const int n = ordinal.value(c, 0);
        out[i] = subWindowGridRect(corner, n, logical, lattice);
        ordinal.insert(c, n + 1);
    }
    return out;
}

// ─── the one layout pass ───────────────────────────────────────────────
//
// Called from paintEvent, which is the only place the frame rectangle,
// the fit factor and the logical size are all known at once. Fills
// subRects_ (canvas pixels, by sub-window index) from each window's OWN
// corner + margins — the same two numbers the four-slot arm reads — so a
// box the user dragged lands where the panel will actually land, without
// a second code path for "moved" versus "fresh".
QVector<HudCanvas::SubWindowPaintInfo> HudCanvas::layoutSubWindows(
    qreal fit, const QPointF &frameTopLeft, const QSize &logical)
{
    QVector<SubWindowPaintInfo> out;
    if (subSlots_.isEmpty()) {
        subRects_.clear();
        return out;
    }
    out.reserve(static_cast<int>(subSlots_.size()));
    subRects_.resize(subSlots_.size());

    // ── v1.0.0 (OUTPUT-OWNERSHIP-FIX, C3): the panel view's filter ─────
    // reference.html:442's `visibleWindows()` is ONE list with ONE
    // filter, and `resolveOutput(w) === state.display` plus `d.panel ===
    // state.view` apply in the overview AND in the panel view. The board
    // only ever had the first half, and only under overviewMode_, so a
    // panel view painted all eleven split boxes against a chip of six.
    // Computed ONCE, outside the loop, for the same reason `fit` is.
    //
    // Both degenerates below are "no filter", and they are what keeps
    // this a no-op off the console: an unnamed viewer (offscreen /
    // headless) and a canvas nobody put into fidelity mode. A standalone
    // HudCanvas in a test therefore paints every binding it was given,
    // exactly as before this block existed.
    const bool panelView = consoleFidelity_ && !overviewMode_;
    const QString viewDisplay =
        panelView ? (previewOutputName_.isEmpty() ? primaryOutputName()
                                                  : previewOutputName_)
                  : QString();

    for (int i = 0; i < subSlots_.size(); ++i) {
        subRects_[i] = QRectF();
        const SubSlot &sub = subSlots_[i];
        if (!sub.src)
            continue;   // an unresolved row: no box, keeps the index space
        // v1.0.0: the overview draws ONLY the windows configured for its
        // display. The box is not laid out at all, so it is not painted
        // and not hit-testable either — a window that is not part of
        // this display's picture must not be clickable in it. Same
        // short-circuit discipline as the paint gate: with
        // overviewMode_ false this is one comparison against a false.
        if (overviewMode_ && !overviewDrawsSub(i))
            continue;
        // THE PANEL VIEW'S OWN TWO CLAUSES, same discipline, same place.
        // Skipping the layout is what buys paint + hit-test + the wall
        // overlap pass from one condition; a box filtered here leaves an
        // empty subRects_[i], so the press handler and subWindowWall()
        // see nothing to click and nothing to dodge.
        if (panelView) {
            // `d.panel === state.view` — this panel's windows only.
            // The overview is exempt: it is the cross-panel picture.
            if (mhw::isPanelIndex(selected_)
                && sub.parentSlot != selected_)
                continue;
            // `resolveOutput(w) === state.display` — windowOutputName()
            // already answers own → parent → primary, so this is the
            // SAME chain the #window-count predicate resolves with.
            if (!viewDisplay.isEmpty()
                && windowOutputName(-1, i) != viewDisplay)
                continue;
        }
        // v1.0.0: THE PROTOTYPE'S `visible`, CONSUMED. reference.html:442
        // filters `w.visible===false` out of visibleWindows(), and the
        // consequence is not "draw it faintly" — the window simply is not
        // on the board. Skipping the layout is the one gate that buys
        // all three properties the contract names: nothing is appended
        // so nothing is PAINTED; subRects_[i] keeps the empty rect the
        // resize above just wrote, so nothing is HIT-TESTABLE (both the
        // press handler and the overview arm test
        // `subRects_[i].contains`); and the box never reaches
        // subWindowWall()'s overlap pass either.
        //
        // FIDELITY-GATED, like every other v1.0.0 branch in this file:
        // a canvas nobody put into fidelity mode lays out every binding
        // it was given, exactly as before this field existed.
        //
        // Note the contrast with `enabled` in the same struct: an
        // ENABLED-false window is drawn as the prototype's
        // `.hidden-window` placeholder, a VISIBLE-false window is not
        // drawn at all.
        if (consoleFidelity_ && !sub.visible)
            continue;

        // ── v1.0.0 integration (stage A / O2 review ①) ──────────────────
        // THE PANEL MASTER TAKES ITS CHILDREN WITH IT. reference.html:442
        // drops a window from visibleWindows() when EITHER the window's
        // own `visible` OR its `panel.visible` is false — the predicate is
        // one filter over one list, so a hidden panel cannot leave its
        // split windows on the board while the mother is gone. This is
        // the Split half of the same rule the four-slot arm gets for free
        // (line ~2175 gates the whole panel body).
        //
        // WHY THE GATE LIVES HERE AND NOT IN THE PAINT LOOP. This
        // function is the ONE place that fills `subRects_`, and an empty
        // rect is what makes a box un-hittable in the press handler and
        // the overview arm. Putting the test in paint only would draw the
        // exclusion and leave the click target behind; putting it here
        // buys layout + paint + hit-test + subWindowWall() from a single
        // comparison, and a restore (setPanelWindowVisible(i, true)) is
        // just this branch not being taken — the margins, the output and
        // the pixmap are untouched by a hide, so the window comes back
        // exactly where it was.
        if (consoleFidelity_
            && (sub.parentSlot < 0 || sub.parentSlot >= mhw::kPanelCount
                || !panelVisible_[sub.parentSlot]))
            continue;

        // THE CONTRACT, READ NOT GUESSED. The box's extent is the
        // window's own contentWidth()/contentHeight() (Panel's v0.12
        // layout vocabulary) — the number the live surface reserves the
        // same way. Nothing here re-derives a width, so a window whose
        // content grew automatically gets a box that grew with it, and
        // the split preview cannot drift from the window it previews.
        const QSize content = sub.src->contentSize();
        if (content.width() <= 0 || content.height() <= 0)
            continue;   // nothing reserved, so nothing to represent

        const qreal z = std::max(0.1, sub.src->scale());
        const QRect lrect = anchoredRect(QRect(QPoint(0, 0), logical),
                                        sub.src->corner(),
                                        consoleFidelity_ ? sub.src->presentationMargins()
                                                         : sub.src->margins(), content, z);
        const QRectF canvas(frameTopLeft.x() + lrect.x() * fit,
                           frameTopLeft.y() + lrect.y() * fit,
                           lrect.width() * fit, lrect.height() * fit);
        subRects_[i] = canvas;

        SubWindowPaintInfo info;
        info.rect = canvas;
        info.parentSlot = sub.parentSlot;
        info.title = subWindowLabel(sub.parentSlot, sub.tableRow);
        info.index = i;
        info.selected = (i == subSelected_);
        out.append(info);
    }
    return out;
}

void HudCanvas::setZoom(qreal z)
{
    z = std::clamp(z, 0.5, 4.0);
    if (qFuzzyCompare(z, zoom_)) return;
    zoom_ = z;
    if (fixedViewport_) {
        // v1.0.1 (#9): THE WIDGET'S GEOMETRY IS NOT A FUNCTION OF ZOOM
        // HERE. No setFixedSize, no size-policy change — the natural
        // height the ancestors see (sizeHint()/heightForWidth(), both
        // zoom-suppressed in this mode) is the zoom-1 hint at every
        // step, so a bare page, a QScrollArea child and a user's
        // splitter keep the size they had. What moves is the INSIDE:
        // the frame/content extent and the scroll range, picked up by
        // refreshViewportMetrics() below and repainted through the one
        // screenFrame() mapping.
        refreshViewportMetrics();
        updateGeometry();
        update();
        emit zoomChanged(zoom_);
        return;
    }
    if (zoom_ > 1.0) {
        // Enlarged: fix the canvas to its sizeHint so QScrollArea
        // shows scrollbars and the screen frame actually grows.
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(sizeHint());
    } else {
        // Default fit: let the canvas expand to fill the viewport,
        // exactly like the pre-zoom layout.
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(520, 360);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    }
    refreshViewportMetrics();
    updateGeometry();
    update();
    emit zoomChanged(zoom_);
}

// ─── v1.0.1 (#9): fixed-viewport mode ──────────────────────────────────
//
// The mode switch and the three numbers the console's scrollbars need.
// Everything geometric lives in fidelityBoardSizeF() /
// viewportContentSizeF(); this block only selects behaviour.
void HudCanvas::setFixedViewportMode(bool on)
{
    if (fixedViewport_ == on)
        return;
    fixedViewport_ = on;
    if (on) {
        // Was the legacy zoom>1 arm's setFixedSize() in effect? It is
        // the ONLY thing in this widget that pins maximum to a zoomed
        // hint (the constructor and the zoom<=1 arm both leave maximum
        // at QWIDGETSIZE_MAX), so this predicate reads "we fixed this
        // canvas to its zoomed size ourselves" — captured BEFORE the
        // restoration below resets maximum.
        const bool wasZoomFixed =
            maximumWidth() != QWIDGETSIZE_MAX
            || maximumHeight() != QWIDGETSIZE_MAX;
        // RELEASE the zoom>1 fixed size the legacy arm may have left
        // behind — turning the mode on while zoomed is the migration
        // path a console takes, and without this the ancestor would
        // keep inheriting the 2× hint forever. These three lines are
        // the legacy arm's own zoom<=1 restoration, reused verbatim, so
        // the widget lands on exactly the state a zoom<=1 canvas has.
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(520, 360);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        // AND shrink the artifact back: a canvas nobody else resizes
        // (a widgetResizable(false) scroll child, a bare page) would
        // otherwise sit at 2156×2112 forever — updateGeometry() cannot
        // shrink a widget, only ask a layout to. sizeHint() here is
        // already the zoom-1 hint (the flag flipped above), so this
        // undoes exactly our own legacy damage. A parent that wants a
        // different size may resize after us; its resize wins.
        if (wasZoomFixed)
            resize(sizeHint());
    } else {
        // LEAVING: re-apply the legacy arm for the CURRENT zoom, so
        // mode-off is byte-identical to never-having-turned-it-on.
        if (zoom_ > 1.0) {
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            setFixedSize(sizeHint());
        } else {
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            setMinimumSize(520, 360);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
        // The pan is a fixed-mode concept; leaving zeroes it (the clamp
        // below does that because the range is 0 outside the mode).
    }
    refreshViewportMetrics();
    updateGeometry();
    update();
}

QSizeF HudCanvas::fidelityBoardSizeF() const
{
    const QSize phys = previewScreenInfo().physical;
    const bool portrait = isPortrait(phys);
    const qreal ar = physicalAspect(phys);
    // The board is laid out against the WIDGET's avail in both arms —
    // that base is the un-zoomed viewport in fixed mode, and the
    // already-zoom-grown widget in the legacy mode (which is why the
    // legacy arm divides the zoom back out below, exactly as
    // screenFrame()/fidelityHeightForWidth() spell it).
    const qreal availW = std::max(0.0, QRectF(rect())
                                            .adjusted(kBoardPadX, kBoardPadTop,
                                                      -kBoardPadX, 0.0)
                                            .width());
    if (fixedViewport_) {
        // The widget never carries the zoom here, so the cap is the CSS
        // cap at every step and the ×zoom is the WHOLE zoom — once.
        const qreal boardW = (portrait
            ? std::min(availW * kBoardPortraitFraction,
                       kBoardMaxWidthPortrait)
            : std::min(availW, kBoardMaxWidthLandscape)) * zoom_;
        const qreal boardH = portrait
            ? std::max(boardW * ar, kBoardPortraitMinHeight * zoom_)
            : boardW * ar;
        return QSizeF(boardW, boardH);
    }
    const qreal fitWidth = zoom_ > 1.0 ? availW / zoom_ : availW;
    const qreal boardW = (portrait
        ? std::min(fitWidth * kBoardPortraitFraction, kBoardMaxWidthPortrait)
        : std::min(fitWidth, kBoardMaxWidthLandscape)) * zoom_;
    const qreal boardH = portrait
        ? std::max(boardW * ar, kBoardPortraitMinHeight * zoom_)
        : boardW * ar;
    return QSizeF(boardW, boardH);
}

QSizeF HudCanvas::viewportContentSizeF() const
{
    if (!fixedViewport_)
        return QSizeF(size());   // widget IS the content: nothing scrolls
    if (consoleFidelity_) {
        // `.board-viewport { padding:12px 14px 0 }` around the board, and
        // at least the widget — content is NEVER smaller than the
        // viewport, so the range below is ≥ 0 by construction.
        const QSizeF board = fidelityBoardSizeF();
        return QSizeF(std::max(qreal(width()),
                               board.width() + 2.0 * kBoardPadX),
                      std::max(qreal(height()),
                               board.height() + kBoardPadTop));
    }
    // Legacy arm: fit into the widget's own avail (zoom-1 space), then
    // scale by zoom ONCE, then add the header/footer chrome. The fit
    // mirrors screenFrame()'s legacy arm — minus its frameZoom split,
    // because here the widget never pre-carried the zoom.
    const QRectF avail0 =
        QRectF(rect()).adjusted(22, 46, -22, -kFooter);
    const QSize phys = previewScreenInfo().physical;
    const qreal physAR = phys.height() / qreal(phys.width());
    QSizeF screen(std::max(0.0, avail0.width()),
                  std::max(0.0, avail0.width()) * physAR);
    if (screen.height() > avail0.height())
        screen = QSizeF(avail0.height() / physAR, avail0.height());
    screen *= zoom_;
    return QSizeF(std::max(qreal(width()), screen.width() + 44.0),
                  std::max(qreal(height()),
                           screen.height() + 46.0 + kFooter));
}

QSize HudCanvas::viewportContentSize() const
{
    const QSizeF c = viewportContentSizeF();
    // Ceil, not round: a fractional board edge must still be INSIDE the
    // content, or the last half-pixel of the frame would sit past the
    // scrollable extent and be unreachable.
    return QSize(int(std::ceil(c.width())), int(std::ceil(c.height())));
}

QSize HudCanvas::viewportScrollRange() const
{
    if (!fixedViewport_)
        return QSize(0, 0);   // no mode: no scroll, whatever the size
    const QSize c = viewportContentSize();
    return QSize(std::max(0, c.width() - width()),
                 std::max(0, c.height() - height()));
}

void HudCanvas::setViewportScrollOffset(QPoint offset)
{
    const QSize r = viewportScrollRange();
    applyViewportScroll(QPoint(qBound(0, offset.x(), r.width()),
                               qBound(0, offset.y(), r.height())));
}

void HudCanvas::applyViewportScroll(QPoint offset)
{
    if (viewportScrollOffset_ == offset)
        return;
    viewportScrollOffset_ = offset;
    // screenFrame() subtracts this offset itself, so paint, hit-test
    // and drag all move together with one update().
    update();
    emit viewportScrollChanged(viewportScrollOffset_);
}

void HudCanvas::clampViewportScroll()
{
    // Re-applies the CURRENT range to the stored offset: an offset that
    // used to be legal and isn't anymore (content shrank, widget grew,
    // mode switched off → range 0,0) is written clamped, never left
    // dangling outside the scrollable extent.
    setViewportScrollOffset(viewportScrollOffset_);
}

void HudCanvas::refreshViewportMetrics()
{
    if (fixedViewport_) {
        const QSize c = viewportContentSize();
        if (c != notifiedContentSize_) {
            notifiedContentSize_ = c;
            emit viewportContentChanged(c);
        }
    }
    clampViewportScroll();
}


QSize HudCanvas::sizeHint() const
{
    // v1.0.0: the hint follows the CURRENT display's aspect ratio, not a
    // fixed 820x520.
    //
    // The fixed size was written when the preview was always one 16:9
    // landscape screen, so 520 was 820's own ratio plus chrome and the
    // discrepancy never showed. With a portrait output in the mix the two
    // disagree — heightForWidth(1640) answers 3010 while sizeHint() says
    // 520 — and Qt's layout takes the smaller, so the widget is 520 tall
    // while screenFrame() computes its avail from rect() and gets 520 too.
    // The frame then collapses: a 1596-wide avail times physAR 0.5625 gives
    // 898, which does not fit in 520, so the height arm fires and yields
    // 924x520 — and the measured pixels showed the screen drawn as a thin
    // horizontal band, which is what a 1596x898 frame clipped to 520 looks
    // like from the inside.
    //
    // Zoom still scales the width; the height now follows from the ratio.
    //
    // v1.0.1 (#9): FIXED-VIEWPORT MODE answers the zoom-1 hint at every
    // zoom — `hz` below — because there the widget's natural size must
    // NOT be a function of zoom: the measured defect was a bare page at
    // 1078×1062 (z1) growing to 2156×2112 (z2) and the console's
    // 1400×760 scroll child going 1398×758 → 2156×2112. The zoom lives
    // in viewportContentSizeF()/screenFrame() instead — inside the
    // viewport, never in the hint the ancestors lay out from.
    const QSize phys = previewScreenInfo().physical;
    const qreal ar = phys.height() / qreal(phys.width());
    const qreal hz = fixedViewport_ ? 1.0 : zoom_;
    const int w = qRound(820 * (hz > 1.0 ? hz : 1.0));
    const int h = qRound(w * ar) + kHeader + kFooter;
    if (consoleFidelity_) {
        // THE NATURAL WIDTH OF THE FIDELITY BOARD, so a shell sizing
        // itself from this hint gets a board at its CSS cap rather than
        // one squeezed to 820-28. Landscape reaches 1050 at avail 1050;
        // portrait only reaches 560 at avail 1166.67, because its cap is
        // a FRACTION of the viewport first (min(48%, 560px)) — so the
        // two hints are genuinely different numbers and neither is
        // reusable for the other.
        const qreal naturalW = isPortrait(phys)
            ? kBoardMaxWidthPortrait / kBoardPortraitFraction
            : kBoardMaxWidthLandscape;
        const qreal scaled =
            std::max(820.0, naturalW + 2.0 * kBoardPadX)
                * (hz > 1.0 ? hz : 1.0);
        const int fw = qRound(scaled);
        return QSize(fw, std::max(360, fidelityHeightForWidth(fw)));
    }
    return QSize(w, std::max(360, h));
}

// v1.0.0: the fidelity board's height at a given widget width, and the
// ONE spelling of the `.board` width rule. `.board-viewport` is
// `padding:12px 14px 0` and `.board` caps its own width, so the height
// is NOT `width * physAR` — it is `(width - 28) capped, times physAR`,
// and past the cap the extra widget width is simply empty ground on
// either side, which is what `justify-content:center` does with room it
// does not need.
//
// The cap differs by orientation, exactly as the CSS does:
//     landscape  width:min(100%, 1050px)   aspect-ratio:16/9
//     portrait   width:min(48%,   560px)   aspect-ratio:9/16
// but the ASPECT RATIO is read from the real output rather than from
// either literal, because the two literals are only correct for the
// prototype's two demo displays. `min-height:220px` is the portrait
// arm's, and it is the one number that can make the board TALLER than
// its aspect ratio — so it is applied to the height, after the ratio,
// and never to the width.
//
// NATURAL OVERFLOW, NOT A SQUEEZED BOARD. The frozen page lets the root
// grow rather than shrinking the board to fit a 1000px viewport:
// measured at 1600x1000 DPR1 the body margin is 0, the root scrollbar
// takes 15px of width, and the root height comes out 1134.9375 for the
// landscape display and 1742.3125 for the portrait one — the board
// never gives up a pixel of its 1050x590.625 / 560x995.546875. This
// function is what lets a Qt container do the same: it answers the
// height the board NEEDS at this width, so a QScrollArea sized from
// sizeHint()/heightForWidth() grows the widget and scrolls instead of
// compressing the board and calling the squeezed result a match. The
// shell's root scroll is the consumer; this file only supplies the
// numbers.
//
// Both sizeHint() and heightForWidth() call this. The fidelity flag
// changes the answer, so the flag setter has to call updateGeometry()
// for the QScrollArea to notice.
int HudCanvas::fidelityHeightForWidth(int width) const
{
    const QSize phys = previewScreenInfo().physical;
    const qreal ar = physicalAspect(phys);
    const qreal availW = std::max(0.0, width - 2.0 * kBoardPadX);
    // v1.0.1 (#5): THE CAP CONSUMES ZOOM. At zoom 1.0 this is
    // byte-for-byte the CSS rule (min(100%,1050px) / min(48%,560px)),
    // so the cap baseline every existing check measures is untouched.
    // At z>1 the widget width already grew by z in sizeHint(), so the
    // cap growing by z too yields board = cap*z exactly ONCE — never
    // z² — and the height follows with the same z. Without this the
    // widget widened while the board sat at its cap and every zoom step
    // only painted more empty ground.
    //
    // v1.0.1 (#9): in FIXED-VIEWPORT MODE `hz` is 1 — this is the
    // ANCESTOR-facing height and the widget never grows there, so it
    // answers the zoom-1 height at every zoom; the zoomed extent that
    // makes the image scroll instead comes from viewportContentSizeF().
    const qreal hz = fixedViewport_ ? 1.0 : zoom_;
    const qreal fitWidth = hz > 1.0 ? availW / hz : availW;
    const qreal boardW = (isPortrait(phys)
        ? std::min(fitWidth * kBoardPortraitFraction, kBoardMaxWidthPortrait)
        : std::min(fitWidth, kBoardMaxWidthLandscape)) * hz;
    const qreal boardH = isPortrait(phys)
        ? std::max(boardW * ar, kBoardPortraitMinHeight * hz)
        : boardW * ar;
    return qRound(kBoardPadTop + boardH);
}

int HudCanvas::heightForWidth(int width) const
{
    if (consoleFidelity_)
        return std::max(360, fidelityHeightForWidth(width));
    const QSize phys = previewScreenInfo().physical;
    const qreal ar = phys.height() / qreal(phys.width());
    return std::max(360, qRound(width * ar) + kHeader + kFooter);
}

QSize HudCanvas::screenSize() const { return previewScreenInfo().physical; }

QString HudCanvas::screenLabel() const
{
    const auto &r = previewScreenInfo();
    // v0.8: surface which output the preview is showing. Before this
    // field existed, the label was just "<w> × <h>" — useless once the
    // user could redirect the panels to a non-primary output, because
    // the dimensions alone can't tell the two screens apart on Niri
    // (HDMI-A-1 portrait vs DP-1 landscape, same diagonal but very
    // different layouts).
    const QString suffix = previewOutputName_.isEmpty()
        ? mh::tr(QStringLiteral("console.canvas.primary"))   // i18n
        : previewOutputName_;
    return QStringLiteral("%1 × %2  ·  %3").arg(r.physical.width())
                                            .arg(r.physical.height())
                                            .arg(suffix);
}

// v1.0.0: `.board-label`, composed the way reference.html:475 composes
// it — `labelDisplay(state.display) + ' · logical coordinate space'` —
// where labelDisplay is the display's own name, its geometry, and the
// prototype's rotation marker on a portrait output (DISPLAYS[id].zh is
// 「HDMI-A-1 · 1080×1920 (旋转)」). The shape is the one the console's
// SCREEN dropdown already builds in control_panel.cpp, so the two lists
// of displays read the same; the one difference is the SPACE before the
// marker, which the HTML has and the dropdown's string concatenation
// drops — the frozen file is the authority here.
//
// THE SUFFIX IS VERBATIM, INCLUDING THAT IT IS ENGLISH. The frozen JS
// hardcodes the string and does NOT route it through `t()`, so the
// prototype's own zh-CN console prints "· logical coordinate space"
// too. Transcribing it as-is is the fidelity answer; a translated
// version of a string the reference never translates would be an
// invention, and there is no i18n key for it in console.json to read
// instead (adding one would mean touching src/resources, which is
// outside this task's file allowlist).
//
// An empty output name falls through the same chain the rest of this
// file uses: the live preview screen, then the primary, then the
// string-table's 「主屏」.
QString HudCanvas::fidelityBoardLabel() const
{
    const auto &r = previewScreenInfo();
    QString out = previewOutputName_;
    if (out.isEmpty())
        out = primaryOutputName();
    if (out.isEmpty())
        out = mh::tr(QStringLiteral("console.canvas.primary"));   // i18n
    QString label = QStringLiteral("%1 · %2×%3")
                        .arg(out)
                        // ── v1.0.0 integration (stage A / O2 review ③) ──
                        // LOGICAL, NOT PHYSICAL. Every other coordinate
                        // in this console — the #screen-select dropdown's
                        // `o.geometry`, the X/Y spins, the margins the
                        // board lays out from — is Qt's logical size, and
                        // `Result::physical` is documented as "logical ×
                        // dpr" (screen_query.h:57). On a dpr=2 screen the
                        // physical spelling printed 5120×2880 beside a
                        // dropdown saying 2560×1440 for the SAME output,
                        // which is the same class of defect as a coordinate
                        // space named in the wrong unit.
                        .arg(r.logical.width())
                        .arg(r.logical.height());
    // And the rotation test follows the same size, for the same reason:
    // "portrait" is a property of the frame the user is looking at, and
    // the user is looking at the logical one.
    if (isPortrait(r.logical))
        label += QStringLiteral(" ")
                 + mh::tr(QStringLiteral("console.proto.screen.rot"));
    return label + QStringLiteral(" · logical coordinate space");
}

// v0.15: the screen-resolution rule, spelled ONCE and parameterised by
// output name. previewScreenInfo() is now the one-liner that passes
// previewOutputName_; movedSubMargins() passes whatever screen the
// window being dragged is bound to. Two copies of this resolution order
// is precisely how the canvas and the panel end up measuring the same
// window against two different screens — see the note on
// movedSubMargins().
//
// Resolution order, unchanged from the v0.8 spelling:
//   1. `outputName` matched against QGuiApplication::screens()
//   2. QGuiApplication::primaryScreen() (the v0.7.x behaviour where the
//      preview always showed the primary output)
//
// We do NOT call screen_query::detect() per-paint: that path can shell
// out to kscreen-doctor / wlr-randr with a 2 s budget, and it would
// stutter the canvas every time the user drags a panel. QScreen's own
// geometry + devicePixelRatio is good enough for preview purposes;
// the per-output table below is a plain in-process QScreen walk.
//
// v0.11.4: the two tables that used to be function-level statics here
// were built exactly once per PROCESS. Hot-plugging a monitor (unplug +
// replug, or docking a new one) therefore never reached the preview:
// the name table still held the removed output, a fresh one was absent
// from it, and both lookups fell through to an equally stale fallback.
// "The next SCREEN click will repaint" — the original comment's
// acceptable-worst-case claim — was never true: nothing rebuilt those
// statics. They are now per-widget mutable members invalidated by
// rebuildScreenCache(); see hud_canvas.h.
const screen_query::Result &HudCanvas::screenInfoForOutput(
        const QString &outputName) const
{
    // Both paths read CURRENT data: any cache miss triggers a rebuild,
    // so a newly plugged output is found and a removed one is gone.
    if (!screenCacheValid_)
        rebuildScreenCache();

    // Unchanged semantics: an empty name (the default) follows the
    // primary screen.
    if (outputName.isEmpty())
        return primaryScreenInfo();

    const auto it = screenCacheByName_.constFind(outputName);
    if (it != screenCacheByName_.cend())
        return *it;
    // The name no longer resolves to a live output — the monitor was
    // unplugged, or Qt has not announced a hot-plugged one yet. Rebuild
    // once in case the screen list moved since the last build, then
    // re-check, so a just-plugged-in output is picked up on this very
    // call instead of on the next one.
    rebuildScreenCache();
    const auto retry = screenCacheByName_.constFind(outputName);
    if (retry != screenCacheByName_.cend())
        return *retry;
    // Still nothing: the output is genuinely gone. Same destination as
    // the empty-name case, but the fallback was invalidated along with
    // the topology, so primaryScreenInfo() re-resolves it rather than
    // reusing a stale answer.
    return primaryScreenInfo();
}

const screen_query::Result &HudCanvas::previewScreenInfo() const
{
    return screenInfoForOutput(previewOutputName_);
}

// Resolve (at most once per topology change) the detect()-based answer
// for the primary screen. Kept separate from rebuildScreenCache() so a
// SCREEN dropdown click never pays for the compositor subprocess
// detect() may run — only the paths that actually follow primary do.
const screen_query::Result &HudCanvas::primaryScreenInfo() const
{
    if (!screenFallbackValid_) {
        screenCacheFallback_ = screen_query::detect();
        screenFallbackValid_ = true;
    }
    return screenCacheFallback_;
}

void HudCanvas::rebuildScreenCache() const
{
    screenCacheByName_.clear();
    for (QScreen *s : QGuiApplication::screens()) {
        if (!s) continue;
        screen_query::Result r;
        r.physical = s->geometry().size() * s->devicePixelRatio();
        r.logical  = s->geometry().size();
        r.dpr      = s->devicePixelRatio();
        r.source   = screen_query::Source::QScreen;
        screenCacheByName_.insert(s->name(), r);
    }
    screenCacheValid_ = true;
    // The name table above now describes the NEW topology, so any
    // fallback resolved against the old one is stale: force it to
    // re-resolve the next time the primary path is asked for.
    screenFallbackValid_ = false;
}

void HudCanvas::setPreviewScreen(QString outputName)
{
    if (outputName == previewOutputName_)
        return;
    previewOutputName_ = std::move(outputName);
    // The user just re-pointed the preview — walk today's
    // QGuiApplication::screens() so it lands on real geometry. Cheap:
    // this is the plain in-process QScreen walk, not detect().
    rebuildScreenCache();
    // v1.0.1 (#9): a new preview screen is a new aspect ratio ⇒ a new
    // board size ⇒ a new fixed-mode content extent.
    refreshViewportMetrics();
    updateGeometry();
    update();
}

// ─── v1.0.0: the single-display overview ──────────────────────────────
//
// ONE DISPLAY AT A TIME, READ-ONLY. The prototype's contract, verbatim:
// 「总览为只读视图：一次只绘制一块显示器，点击窗口在下方查看详情。」
//
// WHY THIS IS NOT setPreviewScreen() PLUS A FILTER. The overview needs
// its own screen slot because it must survive the console's own
// navigation: selectPanel() calls canvas_->setPreviewScreen() on every
// tab change, and the SCREEN dropdown does the same on every pick. If
// the overview read previewOutputName_ as its screen, a single rail
// click would move it — the overview would show whatever panel the user
// happened to inspect, which is the "filtering by view state is
// self-referential" trap. So the two are separate: overviewScreen_ is
// what the overview looks at, and setPreviewScreen() is what the
// per-panel editing looks at.
//
// ENTERING points the canvas at the overview's screen (the frame the
// user sees has to be the display the overview describes), and EXITING
// puts back whatever was there. Both paths rebuild the screen cache so
// a screen plugged or unplugged during the session still lands on real
// geometry.
//
// THE DEFAULT PATH IS UNTOUCHED. setOverview(false) is what every
// non-overview code path calls (and what the console calls on leaving),
// and it restores rather than clears: a console that never enters the
// overview never reads these members at all, and the paint gate below
// is a short-circuit on overviewMode_ being false.
void HudCanvas::setOverview(bool on, const QString &screen)
{
    if (on == overviewMode_ && (!on || screen == overviewScreen_))
        return;

    if (on) {
        savedPreviewOutput_ = previewOutputName_;
        overviewMode_ = true;
        // An empty screen means "follow primary" in the console's
        // vocabulary; normalise it to a REAL output name so the window
        // attribution and the overview screen compare in one string
        // space. An unnamed screen (offscreen, headless) stays empty,
        // and the filter then treats every window as belonging here —
        // the degenerate case draws everything, which is what the
        // single-monitor user sees anyway.
        overviewScreen_ = screen.isEmpty() ? primaryOutputName() : screen;
        // The frame the user looks at has to be the display described.
        if (overviewScreen_ != previewOutputName_)
            previewOutputName_ = overviewScreen_;
    } else {
        overviewMode_ = false;
        overviewScreen_.clear();
        previewOutputName_ = savedPreviewOutput_;
        savedPreviewOutput_.clear();
    }
    rebuildScreenCache();
    // v1.0.1 (#9): a new preview screen is a new aspect ratio ⇒ a new
    // board size ⇒ a new fixed-mode content extent.
    refreshViewportMetrics();
    updateGeometry();
    update();
}

// ─── v1.0.0: which display a window is CONFIGURED for ────────────────
//
// The prototype's resolveScreen(), spelled once:
//
//     the window's own outputName
//       -> its parent panel's outputName      (a split sub-window)
//       -> the primary
//
// THE DELIBERATE DIFFERENCE FROM boundOutputName(). That function's
// last step falls back to previewOutputName_ — the screen the canvas
// happens to be previewing — because a drag clamp wants "the screen
// this window is measured against, as far as the console has told us".
// The overview asks a different question: "where is this window
// CONFIGURED to live". Those coincide for a window that names a screen
// and diverge for one that follows primary, and in the overview the
// divergence is fatal: with previewOutputName_ == DP-1 every
// primary-following window would claim to be on DP-1, so the overview
// would show DP-1's windows while the user looks at the primary and
// vice versa — filtering by the view state it is supposed to be
// filtering. Hence the explicit primary fallback here.
//
// This is a CONFIGURATION fact, not a view state, so it is stable under
// the screen switch and under selectPanel(): the user can flip the
// display dropdown all they like without moving a single window.
namespace detail {
// v1.0.1 (#8): forward declaration of the output-resolution seam. The
// full definition — and why it exists — sits with resolveBoundOutput
// below; this declaration is here only because windowOutputName(),
// defined before that block, already routes through it.
QString effectiveOutputName(const QString &own, const QString &parentOutput,
                            const QString &primaryOutput);
} // namespace detail

QString HudCanvas::windowOutputName(int panelIndex, int subIndex) const
{
    const PanelSource *src = nullptr;
    if (subIndex >= 0) {
        if (subIndex >= subSlots_.size())
            return {};
        src = subSlots_[subIndex].src;
        if (!src)
            return {};
        // A split sub-window names no screen of its own; its parent's
        // answer is the next link in the chain, exactly as the
        // prototype's `scope().panels[win.panel].output` is.
        const QString own = src->outputName();
        if (!own.isEmpty())
            return screen_query::effectiveOutputName(own);
        const int parent = subSlots_[subIndex].parentSlot;
        if (mhw::isPanelIndex(parent))
            panelIndex = parent;
        else
            panelIndex = -1;
    } else if (mhw::isPanelIndex(panelIndex)) {
        src = slots_[panelIndex].src;
        if (!src)
            return {};
        // v1.0.0 (OUTPUT-OWNERSHIP-FIX): NO EARLY RETURN HERE ANY MORE.
        // This branch used to `return src->outputName()` raw, so a base
        // panel that follows primary answered "" while the window model
        // behind #window-count answered `resolveWindowOutput(own, "",
        // primary)` = the primary's NAME. Two spellings of one fact, and
        // they only ever agreed because the console happened to persist
        // an output for every mother. The fall-through below is the SAME
        // chain the sub-window arm takes and mhw::resolveWindowOutput()
        // takes: own → parent → primary, spelled once.
        // Deliberately not re-labelled: for a mother the "parent" step
        // reads back its own outputName, which is exactly the own step,
        // so the chain collapses to own → primary for free.
    } else {
        return {};
    }

    QString parentName;
    if (mhw::isPanelIndex(panelIndex) && slots_[panelIndex].src)
        parentName = slots_[panelIndex].src->outputName();
    // v1.0.1 (#8): the tail is ONE chain-spelling with boundOutputName's
    // — detail::effectiveOutputName, the stand-in for
    // screen_query::effectiveOutputName (own → parent → primary). For a
    // mother the "parent" step re-reads its own outputName, so this
    // collapses to own → primary for free; for a sub-window the early
    // return above already took a non-empty own. The last step is the
    // PRIMARY and never previewOutputName_ (the viewer).
    return detail::effectiveOutputName({}, parentName, primaryOutputName());
}

QString HudCanvas::primaryOutputName() const
{
    // The in-process walk, matching rebuildScreenCache()'s table: Qt's
    // own QScreen list. detect() is deliberately NOT called — it shells
    // out with a 2 s budget and this runs per painted window.
    if (QScreen *s = QGuiApplication::primaryScreen())
        return s->name();
    return {};
}

// ─── v1.0.0: the overview's paint filter ──────────────────────────────
//
// A MOTHER PANEL is drawn in the overview only when it is on the
// overview's screen AND it actually has something to show. The second
// clause is the conservative half: under Split the console pushes a
// Split-semantics mask (see control_panel.cpp's own note at its
// setPanelPresent call site), so a mother's pixmap can be empty or
// stale while `present` still says true. Skipping a null pixmap avoids
// drawing an empty frame; the mask semantics themselves are a separate,
// larger fix and are deliberately not attempted here.
bool HudCanvas::overviewDrawsPanel(int panelIndex) const
{
    if (!overviewMode_ || !mhw::isPanelIndex(panelIndex))
        return false;
    const Slot &s = slots_[panelIndex];
    if (!s.bound || !s.src)
        return false;
    if (!s.present || s.pixmap.isNull() || !s.enabled)
        return false;
    return windowOutputName(panelIndex) == overviewScreen_;
}

bool HudCanvas::overviewDrawsSub(int subIndex) const
{
    if (!overviewMode_ || subIndex < 0 || subIndex >= subSlots_.size())
        return false;
    const SubSlot &s = subSlots_[subIndex];
    if (!s.src)
        return false;
    // A sub-window whose pixmap was never pushed is a window the console
    // could not resolve; drawing its frame would claim a window that
    // does not exist.
    if (s.pixmap.isNull())
        return false;
    return windowOutputName(-1, subIndex) == overviewScreen_;
}

QString HudCanvas::cornerLabel(int index) const
{
    if (!mhw::isPanelIndex(index) || !slots_[index].bound) return {};
    return cornerName(slots_[index].src->corner());
}

// v0.15: the output a BOUND window is measured against.
//
// WHY THIS EXISTS AT ALL, with the measured numbers. The drag clamp has
// to measure the window against the screen it is DRAWN ON, and "the
// screen the canvas is drawing" and "the screen this window is bound
// to" are two different questions that this file used to answer with
// one variable. previewOutputName_ is the CANVAS's screen — the frame
// it paints and the ruler it draws. It is not the dragged window's.
//
// In the reporter's Rise split console they disagreed by a whole
// monitor:
//
//     HDMI-A-1  1080x1920   <- primaryScreen(), portrait
//     DP-1      2560x1440   <- previewOutputName_, what was drawn
//
// The 11 split sub-windows have no outputName of their own, so the
// window-side answer fell through to primaryScreen() while the
// canvas-side answer was DP-1. A drag that asked for left=2538 came
// back clamped to left=1058 — 1480 px short — and the window could not
// be dragged past the middle of the screen because the middle it was
// being measured against was the middle of the OTHER one. That is the
// whole 「悬浮窗无法越过中线拖动」 report.
//
// The resolution rule is Panel::marginLimits()'s, so the preview clamp
// and the surface clamp are derived from the same answer:
//   1. the window's own outputName, when it names one
//   2. the console's preview fallback for a split window (it has no
//      outputName of its own; the console pushes the preview screen in)
//   3. the canvas's preview screen — today's behaviour, kept as the
//      last word so this file cannot regress if the interface below
//      ever loses an accessor.
//
// THE ACCESSOR DOES NOT EXIST ON PanelSource YET, and this file is not
// where it gets added (panel_source.h belongs to the other half of this
// fix; see the report for the exact signature). The concepts below make
// that a COMPILE-TIME fact rather than a silent guess: while
// PanelSource exposes only geometry, both `if constexpr` arms are
// discarded and this returns previewOutputName_ — byte-for-byte the
// pre-fix behaviour, so nothing regresses while the interface lands.
// The moment PanelSource grows outputName() / previewFallbackOutput(),
// the same source compiles into the real answer with no edit here.
namespace detail {

// "this source can name the screen it lives on" — Panel::outputName()
template <typename T>
concept HasOutputName = requires(const T &t) {
    { t.outputName() } -> std::convertible_to<QString>;
};

// "this source carries the console's preview-only fallback" —
// Panel::previewFallbackOutput(), the split sub-window case.
template <typename T>
concept HasPreviewFallback = requires(const T &t) {
    { t.previewFallbackOutput() } -> std::convertible_to<QString>;
};

// ─── v1.0.1 (#8): THE OUTPUT-RESOLUTION SEAM ──────────────────────────
//
// windowOutputName(), boundOutputName() and the console's filter/count
// all have to answer ONE question with ONE chain:
//
//     own output  ->  parent panel's output  ->  the primary
//
// This forwarder is that chain, spelled once in this file. The runtime
// lane owns the canonical screen_query::effectiveOutputName and the
// parent session swaps this body for that call — the signature the
// sibling must provide is:
//
//     QString screen_query::effectiveOutputName(const QString &own,
//                                               const QString &parentOutput,
//                                               const QString &primaryOutput);
//
// While it is missing, this implements the same chain, so no consumer
// waits on the interface. THE LAST STEP IS THE PRIMARY AND NEVER
// previewOutputName_ (the viewer): a window that follows its
// parent/primary LIVES on that screen, and the canvas's preview screen
// is a view state, not a location.
QString effectiveOutputName(const QString &own,
                            const QString &parentOutput,
                            const QString &primaryOutput)
{
    const QString intent = !own.isEmpty() ? own
                         : !parentOutput.isEmpty() ? parentOutput : primaryOutput;
    return screen_query::effectiveOutputName(intent);
}

// Resolve one binding's screen, by the rule in the comment above.
//
// A TEMPLATE, not a plain function, and that is load-bearing: `if
// constexpr` only discards a branch inside a template, so this is what
// lets the same source compile both today (PanelSource exposes only
// geometry, both arms are discarded) and after the interface lands
// (both arms compile). Written as an ordinary function it would have
// to name members that do not exist yet and would simply not build.
//
// Deduced as PanelSource from every call site — the canvas only ever
// holds a base pointer — so the capability is probed on the INTERFACE,
// which is exactly what has to be true for the split sub-window path
// to start working the day panel_source.h grows the accessors.
template <typename T>
QString resolveBoundOutput(const T *src, const QString &chainFallback)
{
    if (!src)
        return chainFallback;

    QString name;
    if constexpr (HasOutputName<T>)
        name = src->outputName();
    if constexpr (HasPreviewFallback<T>) {
        // A RUNTIME guard, deliberately: the fallback only applies when
        // the window names no screen of its own, and `name` is a runtime
        // QString — `if constexpr (name.isEmpty() && …)` would try to
        // read a QString in a constant expression and fail to compile.
        if (name.isEmpty())
            name = src->previewFallbackOutput();
    }
    // v1.0.1 (#8): Step 3, ONE chain-spelling with windowOutputName's —
    // detail::effectiveOutputName, the stand-in for
    // screen_query::effectiveOutputName. `name` is empty exactly when
    // the source names no screen even after the console's pushed
    // fallback, so the answer is the CHAIN's primary — never the
    // canvas's preview screen (the viewer).
    return effectiveOutputName(name, {}, chainFallback);
}

} // namespace detail

QString HudCanvas::boundOutputName(const PanelSource *src) const
{
    // v1.0.1 (#8): the chain's last step is the PRIMARY, not the
    // canvas's preview screen. The console already pushes the parent
    // panel's output (or the primary's name) into a split window's
    // previewFallbackOutput, so what is left to remove here is exactly
    // the viewer fallback: a mother/child that FOLLOWS must be measured
    // against where the output chain puts it.
    return detail::resolveBoundOutput(src, primaryOutputName());
}

// ─── geometry helpers ───────────────────────────────────────────────────

QMargins HudCanvas::movedMargins(int index, int dxLogical, int dyLogical) const
{
    if (!mhw::isPanelIndex(index) || !slots_[index].bound) return {};
    const Corner corner = slots_[index].src->corner();
    QMargins m = dragStartMargins_;   // set at drag/key start
    // v0.15: the dragged panel's OWN screen, not the canvas's. For the
    // four base panels the two coincide today — selectPanel() and the
    // SCREEN dropdown both push the selected panel's outputName into
    // previewOutputName_ — but that is a property of the console's
    // wiring, not of this function, and the split path above is the
    // proof of what happens when it stops holding. Deriving it from the
    // panel removes the coincidence; while PanelSource has no screen
    // accessor, boundOutputName() answers previewOutputName_ and this
    // line is the same expression it has always been.
    const QSize logical = screenInfoForOutput(
            boundOutputName(slots_[index].src)).logical;
    const QSize panel = slots_[index].src->contentSize() * slots_[index].src->scale();
    const int maxX = std::max(0, logical.width() - panel.width());
    const int maxY = std::max(0, logical.height() - panel.height());
    auto clampX = [maxX](int v){ return std::clamp(v, 0, maxX); };
    auto clampY = [maxY](int v){ return std::clamp(v, 0, maxY); };
    switch (corner) {
    case Corner::TopLeft:
        m.setLeft(clampX(m.left() + dxLogical));
        m.setTop(clampY(m.top() + dyLogical));
        break;
    case Corner::TopRight:
        m.setRight(clampX(m.right() - dxLogical));
        m.setTop(clampY(m.top() + dyLogical));
        break;
    case Corner::BottomLeft:
        m.setLeft(clampX(m.left() + dxLogical));
        m.setBottom(clampY(m.bottom() - dyLogical));
        break;
    case Corner::BottomRight:
        m.setRight(clampX(m.right() - dxLogical));
        m.setBottom(clampY(m.bottom() - dyLogical));
        break;
    }
    return m;
}

// v0.14: the sub-window twin of movedMargins(). Reads that window's OWN
// corner and margins rather than a slot's, so the two index spaces never
// meet: `index` is a sub-window index, and every number it touches comes
// from that sub-window's PanelSource.
//
// THE CLAMP IS THE SAME ONE, applied per-corner the same way: a TopLeft
// window is placed by left+top and grows rightward, so a positive delta
// to right would move it the WRONG way (the anchored edge's margin is
// what the compositor reads). Getting this per-corner sign wrong is the
// classic off-by-a-mirror here, and it is why the case list is spelled
// out rather than derived.
//
// The bound is the SCREEN, not the canvas frame: the box must never be
// dragged somewhere the real surface could not be put, which is the same
// reason movedMargins() clamps against previewScreenInfo().logical.
//
// v0.15: WHICH screen, though, is not the canvas's — it is the dragged
// window's own. previewOutputName_ is the frame the canvas paints; a
// split sub-window is bound to whatever screen its Panel resolves, and
// on the reporter's machine those two are different monitors:
//
//     canvas previewOutputName_ = DP-1        2560 wide
//     sub-window, empty name    = primary     1080 wide
//
// so the clamp used to be 1080-panel while the box was drawn on a
// 2560-panel canvas. A drag that asked for left=2538 got back
// left=1058 — 1480 px short — which is exactly 「悬浮窗无法越过中线
// 拖动」: the window stopped at the middle of the screen it was being
// measured against, not the middle of the one it was drawn on. Asking
// the window (boundOutputName, i.e. Panel::marginLimits()'s own rule)
// makes the two agree by construction.
QMargins HudCanvas::movedSubMargins(int index, int dxLogical,
                                    int dyLogical) const
{
    if (index < 0 || index >= subSlots_.size() || !subSlots_[index].src)
        return {};
    const Corner corner = subSlots_[index].src->corner();
    QMargins m = subDragStartMargins_;   // set at drag start
    // v0.15: THIS window's screen, resolved the way its own
    // Panel::marginLimits() resolves it — not previewOutputName_.
    const QSize logical = screenInfoForOutput(
            boundOutputName(subSlots_[index].src)).logical;
    const QSize panel = subSlots_[index].src->contentSize()
                        * subSlots_[index].src->scale();
    const int maxX = std::max(0, logical.width() - panel.width());
    const int maxY = std::max(0, logical.height() - panel.height());
    auto clampX = [maxX](int v){ return std::clamp(v, 0, maxX); };
    auto clampY = [maxY](int v){ return std::clamp(v, 0, maxY); };
    switch (corner) {
    case Corner::TopLeft:
        m.setLeft(clampX(m.left() + dxLogical));
        m.setTop(clampY(m.top() + dyLogical));
        break;
    case Corner::TopRight:
        m.setRight(clampX(m.right() - dxLogical));
        m.setTop(clampY(m.top() + dyLogical));
        break;
    case Corner::BottomLeft:
        m.setLeft(clampX(m.left() + dxLogical));
        m.setBottom(clampY(m.bottom() - dyLogical));
        break;
    case Corner::BottomRight:
        m.setRight(clampX(m.right() - dxLogical));
        m.setBottom(clampY(m.bottom() - dyLogical));
        break;
    }
    return m;
}

// ─── paint ──────────────────────────────────────────────────────────────

void HudCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    // v1.0.0: THE GROUND. In fidelity mode the canvas stands for
    // `.board-viewport`, which sits on `.stage-body`'s `--page` — the
    // one board-adjacent colour that DOES follow the console theme. The
    // legacy field is the old prototype's #06080a blue-black, and the
    // frozen HTML contains no such colour anywhere.
    p.fillRect(rect(), consoleFidelity_ ? boardPageGround()
                                        : QColor(6, 8, 10));

    for (int i = 0; i < mhw::kPanelCount; ++i)
        slots_[i].lastTarget_ = QRectF();

    // v1.0.0: has the board actually got a window on it? `.board-empty`
    // fires on `visibleWindows().length === 0` and the frozen JS does
    // not care which view the board is in, so this counts every box the
    // two arms below paint — including the mother panels a Single or an
    // overview board draws, and EXCLUDING anything a window's own
    // `visible:false` took out of layoutSubWindows().
    bool paintedAnyWindow = false;

    const auto &si = previewScreenInfo();

    // --- header ---
    //
    // v1.0.0: NOT PART OF THE FROZEN BOARD. `.board-viewport` holds
    // `.board` and nothing else; this resolution strip and its rule
    // belong to the old prototype's stage, which the fidelity path does
    // not reproduce. The board's own caption is `.board-label`, painted
    // inside the frame further down.
    if (!consoleFidelity_) {
        QFont headFont(QStringLiteral("Chakra Petch"), 11, QFont::Medium);
        headFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
        p.setFont(headFont);
        p.setPen(QColor(140, 145, 147));
        const QString head = mh::tr(QStringLiteral("console.canvas.header"))
            .arg(si.physical.width())
            .arg(si.physical.height())
            .arg(si.logical.width())
            .arg(si.logical.height())
            .arg(QString::number(si.dpr, 'f', 2))
            .arg(screen_query::sourceLabel(si.source));
        p.drawText(QRectF(22, 14, width() - 44, 22),
                   Qt::AlignLeft | Qt::AlignVCenter, head);

        p.setPen(QColor(60, 64, 66));
        p.drawLine(22, 38, width() - 22, 38);
    }

    // --- screen frame ---
    // v1.0.0: ask screenFrame() instead of repeating the arithmetic.
    //
    // This block used to compute the frame inline, so paintEvent and
    // mouseMoveEvent disagreed the moment either changed — the exact
    // two-copies-of-one-mapping drift this file's own comments warn about
    // (hud_canvas.cpp:1384-1391). It also meant the overview's portrait /
    // landscape fix only reached the drag path, and the drawn frame stayed
    // portrait for BOTH displays.
    //
    // The symptom that made it visible: both DP-1 (2560x1440) and
    // HDMI-A-1 (1080x1920) produced a frame taller than wide, so the
    // portrait display looked correct and hid the landscape one's bug.
    //
    // v1.0.0 (fidelity): screenFrame() is now the ONE place the two
    // fitting rules live — `.board`'s width cap plus the output's own
    // aspect ratio, or the legacy 22/46/-22/-kFooter fit. Same function,
    // so the box and the drag still convert through one mapping.
    const QSize phys = si.physical;
    QSize logical = si.logical;
    QRectF frame;
    qreal fit = 0.0;
    screenFrame(frame, fit, logical);

    // `.board.editing` toggles on `state.editing && state.view!=='overview'`
    // (reference.html:474), which is NOT the same predicate the legacy
    // arm below reads. The legacy arm keeps its bare `editing_` so the
    // default path's pixels do not move; the fidelity arm uses this.
    const bool boardEditing = editing_ && !overviewMode_;

    if (consoleFidelity_) {
        // ─── `.board`, transcribed layer by layer ─────────────────────
        //
        // Painted in the CSS's own order, because the three pieces of
        // box-shadow overlap in a specific way and getting the order
        // wrong makes the border sit on the wrong side of the ring:
        //
        //   1. `box-shadow:0 0 0 1px <ring>`   the OUTER ring, one pixel
        //      OUTSIDE the border box.
        //   2. `background:var(--board)`        the ground, covering the
        //      whole border box.
        //   3. `box-shadow:inset 0 0 0 12px <band>` a 12px band INSIDE
        //      the border, painted over the ground.
        //   4. `border:1px solid <line>`        the hairline, in the
        //      outermost pixel of the border box.
        //
        // The ring and the band are the one thing Qt has no built-in
        // for: a box-shadow. Both are donuts — two rects in one
        // OddEvenFill path — which is the same shape, filled with a
        // translucent colour over what is already on the board. The
        // band's result over `--board` is what the browser would compute
        // (20,20,20) at rest; doing it with alpha rather than as a
        // pre-blended colour keeps the editing state's warm band honest
        // too.
        const QColor ringColor = boardEditing ? boardClay()
                                              : QColor(255, 255, 255, 8);
        // NOTE, and it is worth the parenthesis: the INSET band is a
        // hardcoded rgba(217,119,87,.06) in the CSS, NOT var(--clay).
        // In the light theme the border and the ring go to #9c4623 while
        // the band stays warm orange at 6% — so these are two colours,
        // not one repeated.
        const QColor bandColor = boardEditing
            ? QColor(217, 119, 87, 15)      // rgba(217,119,87,.06)
            : QColor(255, 255, 255, 4);     // rgba(255,255,255,.015)

        p.save();
        p.setPen(Qt::NoPen);
        {
            QPainterPath ring;
            ring.addRect(frame.adjusted(-kBoardRingWidth, -kBoardRingWidth,
                                        kBoardRingWidth, kBoardRingWidth));
            ring.addRect(frame);
            p.fillPath(ring, ringColor);
        }
        p.fillRect(frame, boardGround());
        {
            QPainterPath band;
            band.addRect(frame);
            band.addRect(frame.adjusted(kBoardInset, kBoardInset,
                                        -kBoardInset, -kBoardInset));
            p.fillPath(band, bandColor);
        }

        // ─── v1.0.1 (#6): THE ALPHA-READ CHECKER ─────────────────────
        //
        // A neutral micro-checker under the windows so a translucent
        // pixmap has something structured to reveal it against: opacity
        // is baked into the pixmap's alpha, and on a flat ground a 60%
        // panel and a solid one look alike — on a checker the ghosting
        // is obvious. Console-preview only; the real HUD never sees it
        // and no pixel baseline measures it. The alphas are baked into
        // the colours (never QPainter::setOpacity — a second opacity
        // multiply is the double-alpha bug). The selection outline
        // deliberately does not scale with a window's opacity: it is
        // chrome, not content.
        if (transparencyGuide_) {
            const QRectF cells = frame.adjusted(kBoardInset, kBoardInset,
                                                 -kBoardInset, -kBoardInset);
            // One cached repeating brush, not thousands of rectangles
            // per mouse move when the display preview is zoomed in.
            static const QPixmap tile = [] {
                const int cell = int(kBoardCheckerCell);
                QPixmap pm(2 * cell, 2 * cell);
                pm.fill(QColor(0, 0, 0, kBoardCheckerDarkAlpha));
                QPainter tp(&pm);
                tp.setCompositionMode(QPainter::CompositionMode_Source);
                const QColor light(255, 255, 255, kBoardCheckerLightAlpha);
                tp.fillRect(QRect(cell, 0, cell, cell), light);
                tp.fillRect(QRect(0, cell, cell, cell), light);
                return pm;
            }();
            p.save();
            p.setBrushOrigin(cells.topLeft());
            p.fillRect(cells, QBrush(tile));
            p.restore();
        }

        // ─── `.board.editing::before` — the 5% x 5% lattice ───────────
        //
        // `inset:12px` puts it on exactly the rect the band frames;
        // `opacity:.28` multiplies the line's rgba(255,255,255,.08) down
        // to a 6/255 alpha, which is resolved once at the top of the
        // file rather than through QPainter::setOpacity — a flat alpha
        // is what the browser ends up painting, and it avoids the
        // save/restore a per-line opacity would need.
        //
        // `background-size:5% 5%` makes the grid 20 columns by 20 rows
        // of the INSET rect, not of the frame: the percentages resolve
        // against the ::before element's own box, which `inset:12px` has
        // already shrunk. Walking an integer cell index rather than
        // accumulating `+= cell` is what keeps the twentieth line from
        // landing a few hundredths of a pixel past the right edge.
        if (boardEditing) {
            const QRectF g = frame.adjusted(kBoardInset, kBoardInset,
                                            -kBoardInset, -kBoardInset);
            const QColor gridLine(255, 255, 255, kBoardGridLineAlphaInt);
            p.setPen(QPen(gridLine, 1));
            for (int i = 0; i < kBoardGridCells; ++i) {
                // +0.5: a CSS 1px background line at position t occupies
                // the device pixel [t, t+1], and a 1px QPainter pen is
                // CENTRED on its coordinate — so the pen has to sit half
                // a pixel further right/down than the CSS line starts,
                // or the hairline is smeared across two pixels.
                const qreal x =
                    g.left() + g.width() * i / kBoardGridCells + 0.5;
                p.drawLine(QPointF(x, g.top()), QPointF(x, g.bottom()));
                const qreal y =
                    g.top() + g.height() * i / kBoardGridCells + 0.5;
                p.drawLine(QPointF(g.left(), y), QPointF(g.right(), y));
            }
        }

        // The hairline. `adjusted(0.5, 0.5, -0.5, -0.5)` puts a 1px pen
        // exactly inside the frame's outer pixel — the cell a CSS border
        // occupies — instead of straddling the edge and painting a
        // two-tone smear. screenFrame() already rounded the frame to
        // whole pixels for precisely this.
        p.setPen(QPen(boardEditing ? boardClay() : boardLine(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(frame.adjusted(0.5, 0.5, -0.5, -0.5));
        p.restore();

        // ─── `.board-label` ───────────────────────────────────────────
        //
        // `top:12px; left:14px` of the BOARD, mono 10px, `--board-fg-3`,
        // `letter-spacing:.04em` (0.4px at 10px), `pointer-events:none`
        // — which in a canvas means it is painted last among the chrome
        // and takes no part in hit-testing, exactly as below.
        //
        // v1.0.1 (#10): CONSOLE-ONLY SWITCH. The console's top bar keeps
        // the one authoritative display picker, so this in-board repeat
        // of `output · W×H · logical coordinate space` can be turned off
        // with setFidelityBoardLabelVisible(false). Default true = this
        // block paints byte-for-byte as before; paint-only (no geometry
        // role above), so frame/content/scroll/drag are identical
        // either way. The legacy arm below never painted the label at
        // all, so there is no legacy baseline to move.
        if (boardLabelVisible_) {
            p.save();
            QFont labelFont = cardMonoFont(kBoardMonoPt);
            labelFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
            p.setFont(labelFont);
            p.setPen(QColor(0x87, 0x86, 0x7f));   // --board-fg-3
            p.drawText(QRectF(frame.left() + 14.0, frame.top() + 12.0,
                              std::max(0.0, frame.width() - 28.0), 16.0),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       fidelityBoardLabel());
            p.restore();
        }
    } else {
    QLinearGradient bg(frame.topLeft(), frame.bottomRight());
    bg.setColorAt(0,    QColor(34, 50, 42));
    bg.setColorAt(0.45, QColor(15, 22, 22));
    bg.setColorAt(1,    QColor(6, 9, 10));
    p.fillRect(frame, bg);
    // v1.0.0: the board's own border is --line2 (#4C4C45) at rest and
    // --clay (#D97757) while the board is in edit mode — the prototype's
    // `.board.editing{border-color:var(--clay);cursor:crosshair}`.
    // MEASURED against the 1600x1000 capture: the resting border samples
    // as (68,68,62) on the top edge and (46,46,42) at the rounded
    // corners, which is #4C4C45 at 1px over the board ground with the
    // browser's own antialiasing. The clay step is the HTML token
    // verbatim, since no capture was taken in editing mode.
    p.setPen(QPen(editing_ ? QColor(217, 119, 87) : QColor(76, 76, 69), 1));
    p.drawRect(frame);

    // v1.0.0: the safe-area rectangle has no counterpart in the frozen
    // board — `.board-viewport` contains `.board` and nothing else. It
    // is gated off in fidelity mode and left exactly where it was for
    // the default path.
    if (showSafeArea_ && !consoleFidelity_) {
        p.setPen(QPen(QColor(255, 255, 255, 28), 1, Qt::DashLine));
        p.drawRect(frame.adjusted(frame.width() * .055, frame.height() * .055,
                                  -frame.width() * .055, -frame.height() * .055));
    }

    // v1.0.0: the prototype's .board-grid — an 80-LOGICAL-pixel lattice
    // in rgba(233,231,222,.07), drawn ONLY while the board is editing
    // (`.board.editing .board-grid{opacity:1}`, the resting opacity is 0
    // so nothing is painted at all).
    //
    // THE CELL SIZE IS THE PROTOTYPE'S OWN NUMBER, read from its source
    // (console-single-display.html L1514):
    //     grid.style.backgroundSize = (80*fit)+"px "+(80*fit)+"px";
    // so the pitch in canvas pixels is 80 * fit, with `fit` the very
    // factor screenFrame() just returned — not a second computation of
    // it, which is the two-copies-of-one-mapping rule this file is built
    // on. At the capture's 0.3366 fit that is ~26.9 canvas px per cell,
    // which is what the measurement in WIN-CARD-GEOMETRY.md recorded.
    //
    // The lines are anchored at the frame's left/top and walk rightwards
    // and downwards, mirroring how CSS lays a background image out from
    // the padding box's origin. A cell smaller than a device pixel is
    // skipped rather than drawn as a smeared wash: at fit < 0.0125 the
    // lattice would be denser than the screen can show, and the
    // prototype's own 0.04 fit floor makes that unreachable anyway.
    if (editing_ && fit > 0.0) {
        const qreal cell = 80.0 * fit;
        if (cell >= 1.0) {
            p.save();
            p.setPen(QPen(QColor(233, 231, 222, 18), 1));   // .07 * 255
            for (qreal x = frame.left(); x <= frame.right(); x += cell)
                p.drawLine(QPointF(x, frame.top()),
                           QPointF(x, frame.bottom()));
            for (qreal y = frame.top(); y <= frame.bottom(); y += cell)
                p.drawLine(QPointF(frame.left(), y),
                           QPointF(frame.right(), y));
            p.restore();
        }
    }

    // v1.0.0: GATED OFF IN FIDELITY MODE, all three of these. The
    // frozen `.board` has a lattice but it is `.board.editing::before`'s
    // 5% x 5% at inset 12 / opacity .28 — a different cell size, a
    // different inset and a different alpha from the 80-logical-pixel
    // draft below, which is painted above as the fidelity board's own
    // lattice. The centre crosshair and the scale ruler have no
    // counterpart in the frozen HTML at all. None of them is deleted,
    // because the default path's pixels are the 12 baselines' contract.
    if (showGrid_ && !consoleFidelity_) {
        p.setPen(QPen(QColor(255, 255, 255, 33), 1, Qt::DashLine));
        p.drawLine(QPointF(frame.left(),  frame.center().y()),
                   QPointF(frame.right(), frame.center().y()));
        p.drawLine(QPointF(frame.center().x(), frame.top()),
                   QPointF(frame.center().x(), frame.bottom()));
    }

    // scale ruler
    p.setPen(QColor(60, 64, 66));
    const int tickCount = 5;
    for (int i = 0; i <= tickCount; ++i) {
        const qreal x = frame.left() + frame.width() * i / tickCount;
        p.drawLine(QPointF(x, frame.top() - 4), QPointF(x, frame.top()));
    }
    p.setFont(QFont(QStringLiteral("Chakra Petch"), 7, QFont::Medium));
    p.setPen(QColor(96, 100, 102));
    for (int i = 0; i <= tickCount; ++i) {
        const qreal x = frame.left() + frame.width() * i / tickCount;
        const int px = int(phys.width() * i / tickCount);
        p.drawText(QRectF(x - 40, frame.top() - 22, 80, 14),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   QString::number(px));
    }
    }   // end of the legacy (non-fidelity) chrome arm

    // --- panels ---
    //
    // v0.14: THE WHOLE ARM IS SPLIT-ONLY EXCLUDED. In Split the four
    // mother panels are not drawn at all — not a lighter box, not a
    // placeholder, nothing — which is the first half of the user's
    // 「分块模式下不显示那几个整块」. The screen frame, grid, ruler and
    // header/footer are untouched, so the sub-windows occupy the whole
    // frame instead of the 70% that was left over around four big
    // mother boxes.
    //
    // v0.16: EVERY MOTHER PANEL BUT PETS. The pets panel is the one
    // mother with NO row in any split table (panel_sections.h gives
    // PlayerSection / MonsterSection / DamageSection a table and gives
    // PetDamageSection none), so in Split it has no sub-window to be
    // drawn by, and the overlay mounts the panel itself either way
    // (main.cpp's pets master gate is `setPanelEnabled(!--no-pets)`,
    // which the layout mode does not touch). Hiding it here therefore
    // made the preview disagree with the overlay: the user switched to
    // Rise+Split, selected 伙伴伤害 in the rail, and the LIVE HUD
    // below the inspector showed nothing at all.
    //
    // THREE of the four stay excluded, and that exclusion is the
    // v0.14 feature, not a bug: player/monster/damage each have a table
    // whose rows ARE the windows, so drawing their mother box would
    // put the old big block back next to the eleven boxes that left it.
    // Pets is the exception precisely because it has no rows.
    //
    // SINGLE MODE IS STILL BYTE-IDENTICAL: `!subSlots_.isEmpty()` is
    // exactly the same gate the old sub-window arm used, and
    // setLayoutMode(Single) clears the vector, so a Single-mode paint
    // reaches this loop with the same condition that was already there.
    // The 10 pixel baselines in tests/baseline/ are the authority and
    // they do not move.
    //
    // THE `!present` GUARD STAYS A SEPARATE CONDITION, never folded
    // into this one: a pets panel that is not present (World selected)
    // must still be skipped, and merging the two predicates into one
    // guard once segfaulted the Split paint on an unbound canvas.
    // `fit` came out of screenFrame() above — the same expression, computed
    // once. Recomputing it here is what let the two drift.
    auto logicalToCanvas = [&](const QRect &lr) {
        return QRectF(frame.left() + lr.x() * fit,
                      frame.top()  + lr.y() * fit,
                      lr.width()  * fit,
                      lr.height() * fit);
    };

    // v1.0.0: THE PROTOTYPE'S BOARD, AS THE CONSOLE'S OWN STATE.
    //
    // drawMothers answers "does the four-slot arm draw at all", and it is
    // `subSlots_.isEmpty()` — the v1.0.0 spelling added `|| overviewMode_`
    // so the overview could show base panels too. OUTPUT-OWNERSHIP-FIX
    // (C3/P3) takes that back: in Split the window model behind
    // #window-count is the ELEVEN SUB-WINDOWS (buildWindowStates() has no
    // mother row there), so a base panel the overview painted was a box
    // nobody could count — measured on a private two-output fixture as
    // 7 boxes against a chip of 6, the extra one being a mother. The
    // invariant this board must keep is "the chip is the number of boxes
    // you can see", and it is cheaper to draw what the model knows than
    // to invent a second model. In Single the model rows ARE the four
    // mothers, so drawMothers is true and nothing changes there.
    //
    // v1.0.0 (OUTPUT-CONTRACT-COMPLETION): AND THE ONE MOTHER THAT IS IN
    // THE MODEL EVEN UNDER SPLIT — pets. PetDamageSection has no split
    // table, so buildWindowStates() emits exactly one real row for it in
    // a Rise Split scope (subIndex -1) and none under World; the gate
    // below therefore names the panel and never the view, and the two
    // arms agree because both read the same list. The old spelling's
    // `|| overviewMode_` was a second, competing model: it kept the pets
    // box OFF the overview (which now counts it) and left it ON every
    // panel view (which counted it nowhere, the model having no row).
    // v1.0.1 (#11): MIXED DRAWS MOTHERS ALONGSIDE THEIR DETACHED CHILDREN.
    // The old gate was Split-only thinking — `subSlots_.isEmpty()` said
    // "no child bindings ⇒ this is a monolith, paint the mothers", so in
    // Mixed (per-row topology: some rows in the mother, some detached)
    // ANY binding suppressed EVERY present mother while its children
    // drew, and the retained rows fell out of the preview entirely.
    // Mixed adds itself to the gate: present mothers paint (the
    // bound/present/visible guards below still decide each one), and
    // the children's own arm is untouched. PURE SPLIT IS BYTE-IDENTICAL
    // — non-empty vector and mode != Mixed still yields false, pets
    // exception included; SINGLE still clears its bindings in
    // setLayoutMode(), so isEmpty() remains true there.
    const bool drawMothers = subSlots_.isEmpty()
        || layoutMode_ == mhw::PlayerSection::PlayerLayoutMode::Mixed;
    // v1.0.0 (OUTPUT-OWNERSHIP-FIX, C3): the PANEL VIEW's own filter.
    // reference.html:442 applies `resolveOutput(w) === state.display` in
    // BOTH views — it is not an overview-only rule — and Qt's board used
    // to apply it only under overviewMode_, so a panel view painted all
    // eleven split boxes while the chip counted the six on this display.
    // Two clauses, both reference.html:442's own:
    //
    //   display  → windowOutputName() === state.display (the viewer)
    //   scope    → d.panel === state.view (this panel only; the overview
    //              is exempt, and the overview's own gate is above)
    //
    // Two deliberate degenerates keep the DEFAULT path byte-identical:
    // an unnamed viewer (offscreen / headless, where primaryOutputName()
    // is "" too) means "no display filter", and a canvas nobody put into
    // fidelity mode is not the console's board and gets no view scope —
    // which is what keeps every standalone HudCanvas test painting what
    // it bound.
    const bool panelView = consoleFidelity_ && !overviewMode_;
    const QString viewDisplay =
        panelView ? (previewOutputName_.isEmpty() ? primaryOutputName()
                                                  : previewOutputName_)
                  : QString();
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        if (!drawMothers) {
            // Split: only the pets base panel has no split table, so only
            // it still belongs to the four-slot arm — and since
            // buildWindowStates() now carries that one real row (subIndex
            // -1) it is drawn in the OVERVIEW as well, which is why there
            // is no overviewMode_ test here. The old `|| overviewMode_`
            // spelled a second model: it kept the pets box off the
            // overview (which counts it now) while leaving it on every
            // panel view (which counted it nowhere, the model having no
            // row). Which views show this panel is the model's answer:
            // overviewDrawsPanel below, and the panelView scope next to
            // it.
            if (i != kPetsPanel) continue;
        }
        // v1.0.0: the panel's OWN `visible`, consumed the way the
        // sub-window arm consumes its window's — the body is skipped, so
        // `lastTarget_` keeps the empty rect reset at the top of this
        // function and the panel is neither painted nor hit-testable.
        // It is deliberately NOT folded into the `!bound || !present`
        // guard two lines down: those are two different facts ("nobody
        // told us about this panel" / "the game does not have it"), and
        // merging the predicates is the exact move that once segfaulted
        // the Split paint on an unbound canvas.
        if (consoleFidelity_ && !panelVisible_[i]) continue;
        if (overviewMode_ && !overviewDrawsPanel(i)) continue;
        // `d.panel === state.view`, THE FOUR-SLOT HALF. reference.html:
        // 442 applies it to ONE list that contains mother rows too, so in
        // the panel view this arm paints the SELECTED panel's window and
        // no other — the same clause layoutSubWindows() gives the sub
        // boxes and the same one the chip's scope applies to the model.
        // All three read `selected_`, so a panel outside the view is
        // neither painted, nor hittable (lastTarget_ stays empty), nor
        // counted. Fidelity-gated twice over: panelView is false off the
        // console, and without fidelity `selected_` is not a view.
        if (panelView && mhw::isPanelIndex(selected_) && i != selected_)
            continue;
        // The panel view's display clause. In Single the model row and
        // the painted box are the same panel, so this and the chip's
        // `outputResolved === screenName` are the same comparison —
        // windowOutputName() is resolveWindowOutput() for a mother since
        // the early return in it was removed.
        if (panelView && !viewDisplay.isEmpty()
            && windowOutputName(i) != viewDisplay) continue;
        if (!slots_[i].bound || !slots_[i].present) continue;
        const Slot &s = slots_[i];
        const qreal z = std::max(0.1, s.src->scale());
        const QSize cs = s.src->contentSize();
        // v1.0.1 (#11): NO EMPTY SHELL IN MIXED. In Mixed the mother is
        // supposed to hold exactly the ROWS nobody detached, and the
        // SOURCE is the single place that knows how much that is: the
        // overlay's panel drops a detached section's pixels AND its
        // height (panel_sections.h: sectionOn() gates both), so a mother
        // whose rows have all been detached reports a zero extent.
        // Painting its old box anyway would be a shell whose body
        // replays pixels its children now draw — the double-draw this
        // lane must not ship. Skip the mother; its bindings' boxes
        // paint below.
        // GATED ON MIXED ONLY: Single/Split keep their exact legacy
        // painting, and the `bound` guard above stays a separate
        // predicate (see its comment — merging it once segfaulted Split).
        if (layoutMode_ == mhw::PlayerSection::PlayerLayoutMode::Mixed
            && (cs.width() <= 0 || cs.height() <= 0))
            continue;
        const QRect lrect = anchoredRect(QRect(QPoint(0, 0), logical),
                                         s.src->corner(),
                                         consoleFidelity_ ? s.src->presentationMargins()
                                                          : s.src->margins(), cs, z);
        const QRectF target = logicalToCanvas(lrect);

        if (!s.pixmap.isNull() && s.enabled) {
            // The pixmap already carries per-pixel alpha from paintEvent's
            // p.setOpacity(opacity_), so no extra opacity multiply here.
            // Only the canvas-only selection dimming (unselected → 55%).
            const double selDim  = (i == selected_) ? 1.0 : 0.55;
            p.save();
            p.setOpacity(selDim);
            p.drawPixmap(target, s.pixmap, s.pixmap.rect());
            p.restore();
        } else {
            QColor accent = kAccents[i];
            accent.setAlpha(110);
            p.setPen(QPen(accent, 1, Qt::DashLine));
            p.setBrush(QColor(0, 0, 0, 80));
            p.drawRect(target);
            p.setPen(QColor(170, 174, 176));
            p.setFont(QFont(QStringLiteral("Chakra Petch"), 8, QFont::Medium));
            p.drawText(target, Qt::AlignCenter,
                       mh::tr(QStringLiteral("console.canvas.disabled"))
                           .arg(panelName(i)));
        }

        if (i == selected_ && s.enabled) {
            QColor ring = kAccents[i];
            p.setPen(QPen(ring, 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawRect(target.adjusted(-2, -2, 2, 2));

            p.setFont(QFont(QStringLiteral("Chakra Petch"), 8, QFont::Medium));
            p.setPen(ring);
            const QString tag = mh::tr(QStringLiteral("console.canvas.selected_tag"))
                .arg(panelName(i))
                .arg(int(z * cs.width()));
            const QFontMetrics fm(p.font());
            const int tagW = fm.horizontalAdvance(tag) + 14;
            QRectF tagBox(target.left() - 2, target.top() - 18, tagW, 14);
            p.fillRect(tagBox, QColor(0, 0, 0, 180));
            p.drawText(tagBox, Qt::AlignCenter, tag);
        }

        slots_[i].lastTarget_ = target;
        paintedAnyWindow = true;
    }

    // --- v0.14: the split's sub-windows ---
    //
    // ENTIRELY ABSENT IN SINGLE MODE, same gate as before and same
    // reason (setLayoutMode(Single) empties the vector).
    //
    // Each box is drawn from ITS OWN window's corner + margins + content
    // size — layoutSubWindows() reads exactly the three numbers the
    // four-slot arm reads, for the window that owns them — so the box is
    // where that window's surface will be, at the same fit factor, with
    // no second geometry path. A window the user dragged lands where
    // they put it because that IS its margin now.
    if (!subSlots_.isEmpty()) {
        const QVector<SubWindowPaintInfo> boxes =
            layoutSubWindows(fit, frame.topLeft(), logical);
        for (const SubWindowPaintInfo &info : boxes) {
            paintSubWindowBox(info, p);
            paintedAnyWindow = true;
        }

        // ─── v1.0.0: the board's empty state ───────────────────────────
        //
        // The prototype's .board-empty: when the board has NO window to
        // draw it says so, in two different pairs of strings depending
        // on whether the view is the overview (spanning every panel) or
        // one panel's windows:
        //
        //   overview → board.empty.overview      + board.empty.overview.sub
        //   panel    → board.empty.panel         + board.empty.panel.sub
        //
        // WHY TWO AND NOT ONE. The two states have different causes and
        // therefore different advice: an empty overview means "nothing
        // on this display at all", and the sub-line tells the user to
        // switch displays or retarget a window; an empty panel means
        // "this panel's windows live elsewhere or are hidden", and its
        // sub-line says exactly that. Collapsing them into one string
        // would have the board give advice that does not apply to the
        // state it is in — which is the failure the prototype's own
        // i18n table is shaped to avoid.
        //
        // The condition is "the layout pass produced no box", which is
        // the same fact the loop above just acted on. It is NOT
        // "subSlots_ is empty" — that case is Single mode, where the
        // four mother panels are the picture and there is nothing to be
        // empty about.
        //
        // DRAWN INSIDE THE FRAME, centred, exactly as the prototype's
        // .board-empty{position:absolute;inset:0;display:flex;
        // align-items:center;justify-content:center} places it: the
        // board is the frame here, so the text block is centred in the
        // frame with the prototype's 20px padding.
        //
        // v1.0.0: FIDELITY MODE SKIPS THIS ARM ENTIRELY. The frozen
        // `.board-empty` is a different typographic block (a serif
        // `<strong>` over a mono span, 24px of padding, no 340px cap)
        // and the frozen JS fires it on `visibleWindows().length === 0`
        // regardless of view mode — which Single mode reaches too, and
        // this arm cannot see. `!consoleFidelity_` keeps the legacy
        // spelling byte-identical for the default path and hands the
        // fidelity path to the arm below.
        if (boxes.isEmpty() && !consoleFidelity_) {
            const bool overview = overviewMode_;
            const QString titleKey = overview
                ? QStringLiteral("console.proto.board.empty.overview")
                : QStringLiteral("console.proto.board.empty.panel");
            const QString subKey = overview
                ? QStringLiteral("console.proto.board.empty.overview.sub")
                : QStringLiteral("console.proto.board.empty.panel.sub");
            const QString sub = mh::tr(subKey);
            // 20px of padding, and the text is capped at 340px wide —
            // .be-sub{max-width:340px}. A narrow portrait frame gets a
            // narrower block rather than a clipped one.
            const qreal maxTextW = qMin(340.0, frame.width() - 40.0);
            if (maxTextW > 40.0) {
                const QRectF block(frame.center().x() - maxTextW / 2,
                                   frame.center().y() - 20,
                                   maxTextW, 40);
                p.save();
                // .be-title — mono 11px, uppercase, letter-spaced,
                // --fg2 (#ADABA1).
                QFont titleFont = cardMonoFont(8);
                titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.7);
                p.setFont(titleFont);
                p.setPen(QColor(173, 171, 161));
                const QFontMetrics tfm(p.font());
                const QString title = tfm.elidedText(mh::tr(titleKey),
                                                     Qt::ElideRight,
                                                     int(maxTextW));
                p.drawText(QRectF(block.left(), block.top(),
                                  block.width(), 12),
                           Qt::AlignHCenter | Qt::AlignVCenter, title);
                // .be-sub — 11.5px, --fg3 (#87867F), 5px below the title.
                QFont subFont = cardMonoFont(8);
                p.setFont(subFont);
                p.setPen(QColor(135, 134, 127));
                const QFontMetrics sfm(p.font());
                const QString subText = sfm.elidedText(sub, Qt::ElideRight,
                                                       int(maxTextW));
                p.drawText(QRectF(block.left(), block.top() + 17,
                                  block.width(), 14),
                           Qt::AlignHCenter | Qt::AlignVCenter, subText);
                p.restore();
            }
        }
    }

    // ─── v1.0.0: `.board-empty`, the FROZEN spelling ────────────────────
    //
    // reference.html:205-206 and 477:
    //     .board-empty { inset:0; place-content:center; text-align:center;
    //                    color:--board-fg-2; font:11px var(--mono);
    //                    padding:24px }
    //     .board-empty strong { color:--board-fg; font:15px var(--serif);
    //                           margin-bottom:6px }
    //     <div class="board-empty"><div><strong>TITLE</strong><span>SUB
    //                                      </span></div></div>
    //
    // So the TITLE is the serif one and the SUB is the mono one — the
    // opposite way round from the legacy arm above, which drew both in
    // the mono face and put the brighter colour on the title's smaller
    // sibling. `place-content:center` is the whole block centred, not
    // two independently centred lines, hence one height computation and
    // the 6px gap between them.
    //
    // WHEN IT FIRES: `visibleWindows().length === 0`, which the frozen
    // JS applies in every view — including Single, where the four mother
    // panels are normally the picture. `paintedAnyWindow` is the Qt
    // spelling of "is there anything on this board", so a board whose
    // only window is hidden by setSubWindowVisible() correctly reads as
    // empty rather than as a blank rectangle.
    if (consoleFidelity_ && !paintedAnyWindow) {
        const bool overview = overviewMode_;
        const QString title = mh::tr(overview
            ? QStringLiteral("console.proto.board.empty.overview")
            : QStringLiteral("console.proto.board.empty.panel"));
        const QString sub = mh::tr(overview
            ? QStringLiteral("console.proto.board.empty.overview.sub")
            : QStringLiteral("console.proto.board.empty.panel.sub"));
        const qreal maxTextW =
            std::max(0.0, frame.width() - 2.0 * kBoardEmptyPad);
        if (maxTextW > 40.0) {
            const QFont titleFont(QStringLiteral("serif"), kBoardEmptyTitlePt);
            const QFont subFont = cardMonoFont(kBoardMonoPt);
            const QFontMetrics tfm(titleFont);
            const QFontMetrics sfm(subFont);
            const qreal titleH = tfm.height();
            const qreal subH = sfm.height();
            const qreal blockH = titleH + kBoardEmptyGap + subH;
            qreal top = frame.center().y() - blockH / 2.0;
            // `inset:0` plus `padding:24px` means the block can never
            // start closer than 24px to the board's edge; the centre is
            // still the anchor whenever it fits.
            if (top < frame.top() + kBoardEmptyPad)
                top = frame.top() + kBoardEmptyPad;
            p.save();
            p.setFont(titleFont);
            p.setPen(QColor(0xe9, 0xe7, 0xde));   // --board-fg
            p.drawText(QRectF(frame.left() + kBoardEmptyPad, top,
                              maxTextW, titleH),
                       Qt::AlignHCenter | Qt::AlignVCenter,
                       tfm.elidedText(title, Qt::ElideRight, int(maxTextW)));
            p.setFont(subFont);
            p.setPen(QColor(0xad, 0xab, 0xa1));   // --board-fg-2
            p.drawText(QRectF(frame.left() + kBoardEmptyPad,
                              top + titleH + kBoardEmptyGap,
                              maxTextW, subH),
                       Qt::AlignHCenter | Qt::AlignVCenter,
                       sfm.elidedText(sub, Qt::ElideRight, int(maxTextW)));
            p.restore();
        }
    }

    // --- footer ---
    // v1.0.0: GATED OFF IN FIDELITY MODE for the same reason the header
    // is — the frozen `.board-viewport` has no footer strip. The
    // zoom/screen readout it carried still exists as the console's own
    // stagebar, which is a separate widget.
    if (!consoleFidelity_) {
    p.setPen(QColor(60, 64, 66));
    p.drawLine(22, height() - 30, width() - 22, height() - 30);
    QFont footFont(QStringLiteral("Chakra Petch"), 9, QFont::Medium);
    footFont.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    p.setFont(footFont);

    // Footer left: selected + anchor + move hint. Capped at 55% width
    // so it never collides with the right-aligned screen/zoom text.
    const int footLeftW = qMin(int((width() - 44) * 0.55), 480);
    p.setPen(QColor(170, 174, 176));
    p.drawText(QRectF(22, height() - 24, footLeftW, 16),
               Qt::AlignLeft | Qt::AlignVCenter,
               mh::tr(QStringLiteral("console.canvas.footer"))
                   .arg(panelName(selected_))
                   .arg(cornerLabel(selected_)));

    p.setPen(QColor(96, 100, 102));
    p.drawText(QRectF(22, height() - 24, width() - 44, 16),
               Qt::AlignRight | Qt::AlignVCenter,
               mh::tr(QStringLiteral("console.canvas.footer_right"))
                   .arg(screenLabel())
                   .arg(QString::number(zoom_, 'f', 1)));
    }   // end of the legacy (non-fidelity) footer strip
}

// ─── v0.14: the split's sub-windows ────────────────────────────────────

// One sub-window box: the frame, its title, and — when it is the one
// under the pointer or the keyboard — a highlight ring plus the title
// the console's status line shows.
//
// NO CONNECTION LINE. The old cluster drew a dashed leader from each box
// to its mother panel's edge, which made sense when the mother panel
// was on screen and needed linking to. With the mother panels no longer
// drawn, a leader points at empty space and reads as a bug, so it is
// gone. The parent's identity survives in the two places that need it:
// the accent colour (the same table Panel::accentColor() reads, so a
// purple box is the player's and an orange one is the monster's) and
// the title, which is the section the window was carved from.
void HudCanvas::paintSubWindowBox(const SubWindowPaintInfo &info,
                                 QPainter &p) const
{
    // ─── v1.0.0: THE REAL PANEL, NOT A STAND-IN FOR IT ────────────────
    //
    // A bound sub-window's content IS the window's own rendered surface,
    // and the console already hands it to us: setSubWindowPixmap()
    // receives the offscreen QWidget render of that Panel (see
    // control_panel.cpp's renderSplitWindow — repaint, then blit into a
    // pixmap with the panel's own opacity already composited in).
    //
    // So the window's box is painted as
    //
    //     the pixmap, at the box's full extent
    //     + the prototype's .board-window chrome on top of it
    //
    // and NOTHING ELSE. No three grey bars: those were the prototype's
    // stand-in for "this window has content", drawn as the v1.0.0
    // card's body, and the card path then returned before the pixmap
    // arm ever ran — so a bound window drew three grey bars and its own
    // real pixels stayed invisible. The user rejected the bars
    // explicitly; they are a placeholder for content that is already in
    // hand, and this closes the early return that was hiding it.
    //
    // NO SECOND SCALING. The pixmap is already the surface at the
    // window's own scale (Panel::paintEvent composites scale+opacity, so
    // the pixmap carries per-pixel alpha and no extra opacity is
    // multiplied here either — the same discipline the four-slot arm's
    // pixmap path documents). It is drawn into `info.rect`, the box the
    // layout pass computed from THIS window's corner + margins +
    // contentSize, which is the same rectangle every consumer agrees
    // on. Multiplying by PanelSource::scale() again would shrink a
    // 2.5x window to a quarter of its box.
    //
    // WHAT STILL DRAWS: only the chrome the HTML puts on .board-window —
    // the border (dashed while editing), the selection outline, and the
    // clip that keeps the pixmap inside a 3px-radius box. The parent's
    // accent colour still identifies which panel the window left.
    //
    // UNBOUND ROWS KEEP THE OLD PATH. A row whose window did not resolve
    // has no pixmap, and the dashed frame plus title is the honest
    // vocabulary for "there is nothing here yet".
    const int idx = info.index;
    // A window WITH a pixmap is a window the console resolved, and its
    // pixels are the content — full stop. `enabled` is NOT part of this
    // test: it is the HTML's .hidden-window state (the window exists and
    // is dimmed), not "there is no content", and treating it as the
    // latter is what put three grey bars back over a real panel.
    const bool hasPixmap =
        (idx >= 0 && idx < subSlots_.size()
         && !subSlots_[idx].pixmap.isNull());
    const bool hidden = hasPixmap && !subSlots_[idx].enabled;
    const PanelSource *src = (idx >= 0 && idx < subSlots_.size())
                                 ? subSlots_[idx].src
                                 : nullptr;

    // The parent's own colour. The split's whole visual claim is "these
    // boxes left that panel", and the parent's accent is how the preview
    // says it without a second legend.
    QColor accent = mhw::panel_accent::forSlot(info.parentSlot);

    if (hasPixmap) {
        // .hidden-window — opacity .28, border --rust, greyscale. The
        // prototype's own spelling, and it is a DIM, not a removal: the
        // window is still there, still outlined, still clickable.
        if (hidden) {
            p.save();
            p.setOpacity(0.28);
        }
        // v1.0.0: THE BOARD-WINDOW BACKFILL IS ONLY FOR AN OPAQUE
        // PIXMAP. The prototype's `.board-window` background
        // rgba(19,19,18,.98) exists so a window with NO content of its
        // own reads as a surface; a window that DOES have content
        // paints that content over it, which is what the probe saw go
        // wrong — a fully transparent QPixmap(500,200) composited onto
        // the near-black backfill instead of onto the board, so the
        // window read #ff141413 where the empty board read #ff1c2924.
        //
        // So the fill is skipped when the bound pixmap carries an
        // alpha channel (Format_ARGB32 / Format_ARGB32_Premultiplied,
        // or a mask attached by hasAlphaChannel()). Then the panel's
        // OWN translucent pixels composite onto the board underneath,
        // which is the whole point of Panel::paintEvent baking opacity
        // into per-pixel alpha (see the "NO SECOND SCALING" note above
        // — the same alpha is drawn here, just not buried).
        //
        // AN OPAQUE PIXMAP KEEPS THE FILL. It cannot change a pixel —
        // its own opaque pixels cover the fill entirely — so the
        // existing real-Panel path is bit-for-bit what it was, which
        // is what PASS 1 ("source pixmap reaches board") pins.
        const bool pixmapHasAlpha =
            subSlots_[idx].pixmap.hasAlphaChannel()
            || subSlots_[idx].pixmap.hasAlpha();
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath clip;
        clip.addRoundedRect(info.rect, 3, 3);   // the prototype's 3px
        p.setClipPath(clip);
        if (!pixmapHasAlpha)
            p.fillPath(clip, QColor(19, 19, 18, 250));
        p.drawPixmap(info.rect, subSlots_[idx].pixmap,
                     subSlots_[idx].pixmap.rect());
        p.restore();

        // ── the border: .board-window, 1px rgba(224,138,108,.75) ──────
        //
        // Drawn OUTSIDE the clip above so the edge stays a crisp 1px
        // rather than half of it. A hidden window's border is --rust
        // instead, per .hidden-window.
        p.save();
        p.setPen(QPen(hidden ? QColor(176, 74, 63) : QColor(224, 138, 108, 191),
                      1.0,
                      editing_ ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(info.rect.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        p.restore();
        if (hidden)
            p.restore();   // the opacity above

        // ── .selected: 2px --clay-light + the ring ────────────────────
        //
        // The prototype's selected window is 2px solid --clay-light
        // (#e39a7e) with a 2px ring of rgba(217,119,87,.35) outside it.
        // Drawn on top of the pixmap, never under it, so the selection
        // is readable no matter what the panel painted.
        //
        // v1.0.0: THE BORDER IS THE TOKEN, THE RING IS NOT. Both are
        // literal in the prototype except the border, which is
        // `var(--clay-light)` and therefore DOES move between themes
        // (#e39a7e dark → #b85c38 light). The ring is a bare
        // rgba(217,119,87,.35) in both themes and the resting 1px
        // .board-window border is a bare rgba(224,138,108,.75) in both,
        // so neither reads the palette. The probe's two-theme grab saw
        // 0 differing pixels with the border pinned; routing it through
        // boardClayLight() is what makes the switch visible.
        if (info.selected) {
            p.save();
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(217, 119, 87, 89), 2.0));   // the ring
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(info.rect.adjusted(-2, -2, 2, 2), 5, 5);
            p.setPen(QPen(boardClayLight(), 2.0));           // --clay-light
            p.drawRoundedRect(info.rect.adjusted(1, 1, -1, -1), 2, 2);
            p.restore();
        }
        return;
    }

    // ── an unbound row: the old box, unchanged ────────────────────────
    //
    // Kept for a row whose window did not resolve — no pixmap was ever
    // pushed for it, so there is no content to show and the dashed
    // frame plus title is the honest vocabulary for it. `src` is null
    // there by construction (setSubWindowBindings drops null sources),
    // so the card path that used to run for bound windows can no longer
    // shadow this one.
    if (src) {
        WindowCardPaintInfo card;
        // .win-name — the window's short handle. winName() is the
        // authoritative accessor: it reads the SAME split-table row the
        // prototype's WIN_META[].short reads, so the card names itself
        // with the word the HTML board would print, from one table
        // rather than a second hand-written list. It falls back to the
        // geometry pass's title when the index does not resolve.
        card.name = winName(info.parentSlot, idx);
        if (card.name.isEmpty())
            card.name = info.title;
        card.scale = std::max(0.05, src->scale());
        // .win-panel — the prototype writes
        //     T("panel."+def.panel) · onCount/def.sections.length
        // i.e. the owning panel's name, then how many of the WINDOW'S
        // OWN sections are on. Qt's equivalent is the parent panel's
        // display name plus the window's position among its parent's
        // rows and that parent's row count — both read from the console's
        // own tables (panel_sections.h), so a card's footer can never
        // name a count the inspector does not agree with.
        //
        // WHY NOT subWindowCount(). That is every sub-window on the
        // canvas (6+2+3 = 11), so "怪物 · 1/11" would claim a monster
        // window is one of eleven when it is one of TWO — the exact
        // "the preview misrepresents the thing it previews" failure
        // panel_accent.h's header comment exists to prevent. The
        // per-parent count is the honest denominator.
        const int parentRowCount = subWindowCountForParent(info.parentSlot);
        card.panelLine = QStringLiteral("%1 · %2/%3")
                             .arg(panelName(info.parentSlot))
                             .arg(parentRowCount > 0
                                      ? subSlots_[idx].tableRow + 1 : 1)
                             .arg(parentRowCount > 0 ? parentRowCount : 1);
        // .board.editing — the dashed border the prototype draws while
        // the board is in edit mode. `editing()` is the flag setEditing()
        // owns; the card only reads it, so there is one writer.
        card.editing = editing();
        paintWindowCard(info, card, p);
        return;
    }

    // ── neither: the row never resolved at all ────────────────────────
    //
    // No pixmap and no PanelSource — the binding was null, so
    // setSubWindowBindings dropped it and this box only exists because
    // the layout pass reserved its index. The dashed frame and title are
    // the placeholder state, exactly as before.
    accent.setAlpha(150);
    p.save();
    p.setPen(QPen(accent, 1, Qt::DashLine));
    p.setBrush(QColor(10, 12, 14, 200));
    p.drawRect(info.rect);
    p.restore();

    // The title, above the box. Dashed border plus a label is the same
    // "this is a preview, not the thing itself" vocabulary the disabled
    // placeholder already uses.
    if (!info.title.isEmpty()) {
        p.save();
        p.setFont(QFont(QStringLiteral("Chakra Petch"), 7, QFont::Medium));
        p.setPen(QColor(178, 183, 186));
        p.drawText(QRectF(info.rect.left(), info.rect.top() - kSubTitleH,
                          info.rect.width(), kSubTitleH),
                   Qt::AlignHCenter | Qt::AlignVCenter, info.title);
        p.restore();
    }

    // The drag / keyboard highlight: a solid ring in the accent colour,
    // plus the row's section name beside the box so the user can read
    // WHAT they are dragging without guessing from the picture. The
    // ring is deliberately the only solid line on the canvas — every
    // other edge is dashed — so "the one I am moving" is unambiguous.
    if (info.selected) {
        p.save();
        p.setPen(QPen(accent, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawRect(info.rect.adjusted(-2, -2, 2, 2));
        p.restore();

        p.save();
        p.setFont(QFont(QStringLiteral("Chakra Petch"), 8, QFont::Medium));
        p.setPen(accent);
        const QFontMetrics fm(p.font());
        const QString tag = info.title.isEmpty()
            ? mhw::panel_accent::forSlot(info.parentSlot).name()
            : info.title;
        const int tagW = fm.horizontalAdvance(tag) + 12;
        QRectF tagBox(info.rect.left() - 2, info.rect.bottom() + 4, tagW, 14);
        p.fillRect(tagBox, QColor(0, 0, 0, 190));
        p.drawText(tagBox, Qt::AlignCenter, tag);
        p.restore();
    }
}

// ─── v1.0.0 sa-B: the window card (.win) ──────────────────────────────
//
// The prototype's window card is a THREE-ROW structure, and it is the
// single most recognisable thing in the board — the user compares the Qt
// console against the HTML screenshots window-by-window:
//
//   .win        border 1px --line2, radius 4px, board-fg text,
//               background linear-gradient(180deg, --on-dark-10, --on-dark-04)
//               padding 3px 4px, gap 2px, flex column
//   .win-hd     [.win-name 名字] [.win-sc 1.0x]        ← header row
//   .win-body   <i>100%</i> <i>72%</i> <i>48%</i>      ← three grey bars
//   .win-panel  所属面板名 · 开/总                    ← footer row
//   .win.sel    border --board-accent + a 1.5px ring and a soft shadow
//   .board.editing .win   border-style dashed
//
// Every colour below is PINNED and does NOT follow the console theme,
// because the board is a picture of a game overlay on a desktop: the
// prototype's own note records that a near-black --fg on #101010 measured
// 1.11:1, which is why --board-* exists at all. Painting the card from
// uiTheme() would make it unreadable in the light theme and would also
// make the board's pixels change with a theme switch — which is exactly
// what the canvas->grab() pixel assertions in console_split_sync_tests
// and damage_rows_window_tests must not see.
//
// WHY THIS IS A SEPARATE FUNCTION AND NOT MORE OF paintSubWindowBox().
// The box the split preview draws today is a dashed frame plus a title
// above it — a "this is a preview, not the thing itself" vocabulary. The
// prototype's card is the window itself, drawn where the window will be.
// Keeping the two apart means the default (non-overview) path can keep
// its existing pixels byte-for-byte, which is what the 12 Single-mode
// baselines and the two grab() suites pin.
// ─── end sa-B ─────────────────────────────────────────────────────────
void HudCanvas::paintWindowCard(const SubWindowPaintInfo &info,
                                const WindowCardPaintInfo &card,
                                QPainter &p) const
{
    const QRectF r = info.rect;
    if (r.width() < 4 || r.height() < 4)
        return;      // too small to carry a card; nothing to draw

    // --- the card's own surface -----------------------------------------
    // background: linear-gradient(180deg, on-dark-10, on-dark-04) over
    // the board's own ground. Qt has no "rgba over what is behind", so
    // the gradient's two stops are composited here as the alpha the
    // prototype's tokens describe: 10% and 4% of #E9E7DE over the board
    // ground (#101010) — the same two numbers, resolved once.
    const QColor onDark10(233, 231, 222, 26);   // .10 * 255
    const QColor onDark04(233, 231, 222, 10);   // .04 * 255
    QLinearGradient surface(r.topLeft(), QPointF(r.left(), r.bottom()));
    surface.setColorAt(0, onDark10);
    surface.setColorAt(1, onDark04);

    p.save();
    p.setBrush(surface);
    // border: 1px --line2 (#4C4C45), dashed in the editing state and on
    // hover. The prototype's `.board.editing .win{border-style:dashed}`.
    const QColor line2(76, 76, 69);
    const QColor accent(224, 138, 108);         // --board-accent
    // .win:hover — border-color --board-fg3 (#87867F) plus
    // box-shadow 0 0 0 1px --on-dark-14. NOT dashed: the editing dash
    // and the hover lift are two different states in the prototype, and
    // a hover that also dashed would read as "I am editing this" when
    // the board is merely being pointed at.
    const QColor boardFg3Hover(135, 134, 127);   // --board-fg3
    const bool dashed = card.editing;
    QPen pen(dashed ? accent : line2, 1,
             dashed ? Qt::DashLine : Qt::SolidLine);
    if (!dashed && card.hovered)
        pen.setColor(boardFg3Hover);
    p.save();
    p.setPen(pen);
    p.setBrush(surface);
    p.drawRoundedRect(r, 4, 4);
    // The hover ring: 1px of --on-dark-14 (14% of #E9E7DE) hugging the
    // card. Measured against the prototype this is the whole hover
    // shadow, so it is drawn rather than skipped.
    if (!dashed && card.hovered) {
        p.setPen(QPen(QColor(233, 231, 222, 36), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), 4, 4);
    }
    p.restore();

    // --- row 1: .win-hd  [name] [1.00x] ---------------------------------
    // MEASURED, NOT ASSUMED (html-shots/WIN-CARD-GEOMETRY.md): on the
    // 1600x1000 capture the card's header text occupies 9 pixel rows
    // starting 9px below the card's top border, the name begins at the
    // content box's left edge and the scale badge ends at its right
    // edge. So the row is 9px tall with a 3px top inset — the numbers
    // below, not a round guess.
    //
    // name is mono 11px in --board-fg (#E9E7DE); the scale badge is mono
    // 10px in --board-fg2 (#ADABA1), right-aligned with margin-left:auto.
    const QColor boardFg(233, 231, 222);
    const QColor boardFg2(173, 171, 161);
    const QColor boardFg3(135, 134, 127);
    const int pad = 4;                    // .win padding: 3px 4px
    const int hdH = 9;                    // measured glyph rows
    QRectF hd = r.adjusted(pad, 3, -pad, 0);
    hd.setHeight(hdH);
    if (hd.width() > 24) {
        p.save();
        // 11px CSS ≈ 8.3pt; 10px ≈ 7.5pt. Qt sizes in points, and the
        // two must stay one step apart or the badge reads as the name.
        p.setFont(cardMonoFont(8));
        const QFontMetrics fm(p.font());
        const QString scaleText = QStringLiteral("%1x")
                                      .arg(card.scale, 0, 'f', 2);
        const int scaleW = fm.horizontalAdvance(scaleText) + 4;
        const int nameW = int(hd.width()) - scaleW;
        if (nameW > 8) {
            p.setPen(boardFg);
            // The name is ellipsised, exactly as .win-name's
            // `text-overflow: ellipsis` does, so a long section name
            // never runs into the scale.
            p.drawText(QRectF(hd.left(), hd.top(), nameW, hd.height()),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(card.name, Qt::ElideRight, nameW));
            p.setFont(cardMonoFont(7));
            p.setPen(boardFg2);
            p.drawText(QRectF(hd.right() - scaleW, hd.top(),
                              scaleW, hd.height()),
                       Qt::AlignRight | Qt::AlignVCenter, scaleText);
        }
        p.restore();
    }

    // --- row 2: .win-body  three grey bars ------------------------------
    // 100% / 72% / 48% of the card's CONTENT width, in --on-dark-22
    // (22% of #E9E7DE). This is the prototype's stand-in for "this
    // window has content"; the Qt console paints the REAL content on
    // top of it when the window's own pixmap is available (see the
    // caller), so the bars are what an unbound or empty window shows —
    // the same information, at the same size.
    //
    // MEASURED: the three bars are 3 device rows tall with a 1px gap
    // between them on the capture (HTML's 2px height + 2px gap lose a
    // row to antialiasing at this scale). They start 22px below the
    // card's top and end well before the footer row.
    const QColor onDark22(233, 231, 222, 56);   // .22 * 255
    const int barTop = int(r.top()) + 22;
    const int barH = 3;                    // measured glyph rows
    const int barGap = 1;                  // measured gap
    const int barW = int(r.width()) - 2 * pad;
    for (int i = 0; i < 3; ++i) {
        const qreal frac = (i == 0) ? 1.0 : (i == 1) ? 0.72 : 0.48;
        const int w = int(barW * frac);
        if (w <= 0)
            continue;
        const int y = barTop + i * (barH + barGap);
        if (y + barH > int(r.bottom()) - 11)
            break;      // would collide with the footer row
        p.fillRect(QRectF(r.left() + pad, y, w, barH), onDark22);
    }

    // --- row 3: .win-panel  所属面板名 · 开/总 ---------------------------
    // mono 9.5px board-fg3 (#87867F), ellipsised. The prototype writes
    // `T("panel."+panel) · onCount/sections.length`; Qt's equivalent is
    // the panel's display name plus the section counts the window
    // actually carries.
    //
    // MEASURED: the footer's glyph rows sit 10px above the card's bottom
    // border, i.e. a 3px bottom inset plus 10 rows of text.
    QRectF ft = r.adjusted(pad, 0, -pad, -3);
    ft.setHeight(10);                      // measured glyph rows
    ft.moveBottom(r.bottom() - 3);
    if (ft.width() > 24 && ft.height() > 6) {
        p.save();
        p.setFont(cardMonoFont(7));        // 9.5px ≈ 7pt
        p.setPen(boardFg3);
        const QFontMetrics fm(p.font());
        p.drawText(ft, Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(card.panelLine, Qt::ElideRight, int(ft.width())));
        p.restore();
    }

    // --- .win.sel: the accent ring + soft shadow ------------------------
    // border-color --board-accent plus box-shadow 0 0 0 1.5px
    // --board-accent, 0 4px 14px --shadow. Qt has no box-shadow, so the
    // ring is drawn as a second rounded rect just outside the card — the
    // same visual weight at the same place.
    if (info.selected) {
        p.save();
        p.setPen(QPen(accent, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(-1.5, -1.5, 1.5, 1.5), 4, 4);
        // The soft shadow, as four low-alpha bands under the ring. A real
        // blur is not available on a QPainter path without a QGraphicsScene,
        // and a solid dark band would read as a border rather than a lift.
        QColor shadow(0, 0, 0, 70);
        for (int i = 1; i <= 3; ++i) {
            shadow.setAlpha(70 - i * 18);
            p.setPen(QPen(shadow, 1));
            p.drawRoundedRect(r.adjusted(-1.5 - i, -1.5 - i,
                                         1.5 + i, 1.5 + i), 4 + i, 4 + i);
        }
        p.restore();
    }
}

// ─── interaction ────────────────────────────────────────────────────────

void HudCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    // v1.0.0: THE OVERVIEW IS READ-ONLY. 「总览为只读视图：一次只绘制一
    // 块显示器，点击窗口在下方查看详情。」 — a click SELECTS so the
    // inspector can show the window's geometry, and does nothing else.
    //
    // The drag state is deliberately never entered: subDragging_ /
    // dragging_ / the ClosedHandCursor are all below this return, so
    // the cursor stays an arrow and mouseMoveEvent's drag arms have no
    // live state to act on. That is the whole read-only guarantee for
    // the mouse, and it is one branch at the top rather than a guard in
    // each of the three drag arms — a guard in each arm is three
    // chances to miss one.
    //
    // The selection is not cosmetic: the console's panelSelected /
    // subWindowSelected handlers open the inspector page and move the
    // rail's highlight, which is exactly how the prototype's "click a
    // window to inspect below" works. So the SAME two signals fire, and
    // nothing else does.
    if (overviewMode_) {
        for (int i = subSlots_.size() - 1; i >= 0; --i) {
            if (i < subRects_.size() && subRects_[i].contains(e->position())) {
                subSelected_ = i;
                // v1.0.1 (#7): a RE-CLICK notifies too. The console's
                // handler is what fills the inspector, and a silent
                // press on the already-selected box was the 「重点击静默
                // 失效」 report. setSelectedSubWindow() (the
                // console-driven setter) never emits, so the echo
                // cannot loop.
                emit subWindowSelected(i);
                update();
                return;
            }
        }
        for (int i = mhw::kPanelCount - 1; i >= 0; --i) {
            if (!slots_[i].lastTarget_.contains(e->position()))
                continue;
            selected_ = i;
            update();
            // v1.0.1 (#7): every user click notifies — including the
            // re-click on the already-highlighted default panel. The
            // no-loop guarantee is the same as below: setSelectedPanel()
            // is the console-driven setter and never emits.
            emit panelSelected(i);
            return;
        }
        return;   // empty space: no drag, no selection change
    }

    // v1.0.0: SELECTING IS NOT EDITING. The prototype lets a click pick
    // the window the inspector describes in EVERY view state — that is
    // how the rail, the inspector and the board agree on "which window
    // am I looking at" — and only the DRAG half of the gesture is an
    // edit-mode verb. So a non-editing board keeps its hit-test, its
    // selection and its subWindowSelected signal exactly as they were,
    // and skips only the four lines that start the drag.
    //
    // OVERVIEW: NOTHING CHANGES. The overview arm above already returns
    // before this point on every hit, so an overview click can never
    // leave the overview or enter editing. That is the HTML's
    // 「总览为只读查看」 and it stays true because the gate below is
    // reached, not because a new branch was added ahead of it.
    //
    // THE GATE IS ONE BOOLEAN READ PER GESTURE rather than a branch in
    // each of the two drag arms below — the same discipline the overview
    // read-only rule uses, for the same reason: one chance to be right
    // beats three.
    const bool canDrag = editing_;
    // v0.14: IN SPLIT MODE THE SUB-WINDOWS ARE THE ONLY HIT TARGETS.
    // The mother panels are not drawn, so their boxes must not be
    // hit-testable either — a click that landed on empty space and moved
    // the monster panel by accident is exactly the "several whole blocks
    // in the way" complaint one layer down. So the sub-window arm runs
    // first and returns; the slot arm below is reached only when the
    // canvas is in Single mode (where subSlots_ is empty by contract).
    //
    // v0.16: THAT IS THE v0.14 PAINT GATE, RESTATED. The sub-window arm
    // is still the only hit target for the eleven sub-windows, and it
    // still runs first — but "the mother panels are not drawn" no longer
    // means all four of them. Pets IS drawn in Split (see the paint
    // arm's kPetsPanel exemption), so a pets click has to reach the slot
    // arm to be selectable and draggable. Without this, pets would be
    // painted but not hit-testable: a box the user can see and cannot
    // move, which is worse than the box not being there.
    //
    // The slot arm is reached in Single (subSlots_ empty by contract)
    // AND in Split for a pets click. For the three excluded mothers it
    // stays unreachable, because lastTarget_ is left empty whenever the
    // paint arm skipped them — `lastTarget_.contains()` on an empty
    // rect is false, so the guard is one thing, not two.
    if (!subSlots_.isEmpty()) {
        for (int i = subSlots_.size() - 1; i >= 0; --i) {
            if (i < subRects_.size() && subRects_[i].contains(e->position())) {
                subSelected_ = i;
                // v1.0.0: the drag half, gated. Selection and the signal
                // are above and unconditional; these four lines are the
                // part that writes margins, so they are the part that
                // waits for edit mode.
                if (canDrag) {
                    subDragging_ = true;
                    subDragIndex_ = i;
                    subDragStartMouse_ = e->position().toPoint();
                    subDragStartMargins_ = subSlots_[i].src->margins();
                    setCursor(Qt::ClosedHandCursor);
                }
                // v1.0.1 (#7): unconditional — see the overview arm's
                // note. A child re-click also notifies the inspector.
                emit subWindowSelected(i);
                update();
                return;
            }
        }
        // v0.16: a click on empty space STILL selects nothing — the v0.14
        // behaviour is unchanged — but it no longer swallows the pets box.
        // Falling through to the slot arm is safe precisely because the
        // paint arm left lastTarget_ empty for every mother it skipped,
        // so the only box the slot arm can possibly claim in Split is
        // the pets one the user can actually see.
        //
        // Single mode reaches here with subSlots_ empty and never falls
        // through the branch at all.
    }

    for (int i = mhw::kPanelCount - 1; i >= 0; --i) {
        if (slots_[i].lastTarget_.contains(e->position())) {
            selected_ = i;
            update();
            // v1.0.1 (#7): EVERY user click emits — including a re-click
            // on the default-highlighted panel and, in Split, the pets
            // box this arm also serves. console's panelSelected handler
            // fills the WindowState inspector; the console-driven
            // setSelectedPanel() stays signal-free, so a console echo
            // cannot close the loop.
            emit panelSelected(i);
            // Start drag — v1.0.0: gated, exactly as the sub-window arm
            // above. Selection is not a write; this is.
            if (canDrag) {
                dragging_ = true;
                dragIndex_ = i;
                dragStartMouse_ = e->position().toPoint();
                dragStartMargins_ = slots_[i].src->margins();
                setCursor(Qt::ClosedHandCursor);
            }
            return;
        }
    }
}

// The one fit factor every drag and every paint share. Factored out of
// paintEvent because mouseMoveEvent needs the same mapping in the other
// direction (canvas px → logical px) and two copies of this arithmetic
// is two chances to make a drag disagree with the box it drags.
//
// Also the single spelling of how the screen frame is fitted into the
// canvas's usable area — the reason paintEvent's frame and
// mouseMoveEvent's conversion cannot drift apart.
void HudCanvas::screenFrame(QRectF &frame, qreal &fit, QSize &logical) const
{
    const auto &si = previewScreenInfo();
    logical = si.logical;
    const QSize phys = si.physical;
    const qreal physAR = phys.height() / qreal(phys.width());

    // ─── v1.0.0: THE FIDELITY BOARD, `.board` + `.board-viewport` ──────
    //
    // A different fitting rule from the legacy arm below, and the reason
    // it is a branch inside this function rather than a second function:
    // paintEvent, mouseMoveEvent, mouseReleaseEvent and keyPressEvent
    // all resolve the frame through HERE. One arm means the box the user
    // sees and the coordinate a drag converts through cannot drift
    // apart, which is the property that function's own comment claims.
    //
    // The rule, from the CSS:
    //   `.board-viewport { padding:12px 14px 0; align-items:center;
    //                       justify-content:center; overflow:auto }`
    //   `.board          { width:min(100%,1050px); aspect-ratio:16/9 }`
    //   `.board.portrait { width:min(48%,560px);   aspect-ratio:9/16 }`
    //
    // So: shrink the widget by the viewport padding, cap the width by
    // the orientation's constant, derive the height from the OUTPUT'S
    // aspect ratio (not from a hardcoded 16/9 — those two literals are
    // only correct for the prototype's two demo displays, and this
    // canvas must describe whatever the user actually has plugged in).
    //
    // ONE DELIBERATE DEVIATION, and it is a fix rather than a
    // shortcut. CSS `align-items:center` on an `overflow:auto` container
    // clips the TOP of an over-tall child and makes it unscrollable —
    // a long-standing flexbox behaviour, and one the audit's own
    // LIMITATIONS.json records as a browser artefact rather than a
    // design. Clamping the top to the viewport instead costs nothing
    // when the board fits (the clamp never fires) and stops the board
    // from losing its upper edge when it does not.
    //
    // The size is ROUNDED TO WHOLE PIXELS on purpose: a 1px border on a
    // fractional rect is a two-pixel smear in Qt, and the fidelity board
    // is a flat rectangle with a hairline — every sub-pixel on its edge
    // is visible. The rounding costs at most half a logical pixel of
    // aspect ratio and it buys an exact border.
    if (consoleFidelity_) {
        // v1.0.1 (#9): WHERE THE FRAME IS MEASURED FROM. Legacy mode
        // centres it in the WIDGET (rect()); fixed-viewport mode
        // centres it in the CONTENT extent and then subtracts the
        // scroll offset — inside this one function, so paint,
        // hit-test and drag consume the offset frame by construction
        // and cannot drift apart. fidelityBoardSizeF() applies the zoom
        // exactly once in both arms (its legacy arm divides back the
        // zoom the grown widget already carries, its fixed arm has no
        // widget growth to divide out).
        const QSizeF board = fidelityBoardSizeF();
        const QRectF base = fixedViewport_
            ? QRectF(QPointF(0.0, 0.0), viewportContentSizeF())
            : QRectF(rect());
        const QRectF avail =
            base.adjusted(kBoardPadX, kBoardPadTop, -kBoardPadX, 0.0);
        const qreal cx = avail.center().x();
        qreal top = avail.center().y() - board.height() / 2.0;
        if (top < avail.top())
            top = avail.top();
        frame = QRectF(QPointF(qRound(cx - board.width() / 2.0), qRound(top)),
                       QSizeF(qRound(board.width()), qRound(board.height())));
        if (fixedViewport_)
            frame.translate(-qreal(viewportScrollOffset_.x()),
                            -qreal(viewportScrollOffset_.y()));
        fit = ((logical.width() <= 0 || logical.height() <= 0))
            ? 0.0
            : std::min(frame.width() / logical.width(),
                       frame.height() / logical.height());
        return;
    }

    const QRectF avail = QRectF(rect()).adjusted(22, 46, -22, -kFooter);

    // v1.0.1 (#5): ZOOM BELOW 1 SHRINKS THE FRAME ITSELF. At zoom>1
    // setZoom() fixed the widget to sizeHint()*z, so `avail` already
    // carries the zoom and multiplying here too would square it (z²);
    // at zoom<=1 the widget stays viewport-sized (Expanding), so the
    // shrink has nowhere else to come from. At 1.0 the factor is 1 and
    // this is the exact baseline fit. `screen *= factor` after the fit
    // keeps the frame <= avail in both arms, so the centred rect can
    // never go negative and get clipped at an edge.
    //
    // v1.0.1 (#9): FIXED-VIEWPORT MODE flips the first sentence — the
    // widget NEVER carries the zoom there, so the fit runs against the
    // widget's zoom-1 avail and frameZoom IS the zoom, once. The frame
    // is then centred in the CONTENT extent (never against the already
    // zoomed numbers — that is the z² path) and the scroll offset comes
    // off last, through the same mapping paint and hit-test read.
    const qreal frameZoom = fixedViewport_
        ? zoom_
        : ((zoom_ > 1.0) ? 1.0 : zoom_);
    QSizeF screen(avail.width(), avail.width() * physAR);
    if (screen.height() > avail.height())
        screen = QSizeF(avail.height() / physAR, avail.height());
    screen *= frameZoom;
    if (fixedViewport_) {
        const QRectF cAvail =
            QRectF(QPointF(0.0, 0.0), viewportContentSizeF())
                .adjusted(22, 46, -22, -kFooter);
        frame = QRectF(QPointF(cAvail.center().x() - screen.width() / 2,
                               cAvail.center().y() - screen.height() / 2),
                       screen);
        frame.translate(-qreal(viewportScrollOffset_.x()),
                        -qreal(viewportScrollOffset_.y()));
    } else {
        frame = QRectF(QPointF(avail.center().x() - screen.width() / 2,
                               avail.center().y() - screen.height() / 2),
                       screen);
    }
    fit = ((logical.width() <= 0 || logical.height() <= 0))
        ? 0.0
        : std::min(frame.width() / logical.width(),
                   frame.height() / logical.height());
}

void HudCanvas::mouseMoveEvent(QMouseEvent *e)
{
    // v1.0.0: THE OVERVIEW NEVER DRAGS. Read-only means read-only for
    // the pointer too — the press handler above never sets the drag
    // state, so these arms are already inert, but a stray state (a drag
    // started before entering the overview, say) would otherwise still
    // write real margins. One branch here is the belt to that braces.
    if (overviewMode_)
        return;
    // v1.0.0: AND NEITHER DOES A NON-EDITING BOARD. The prototype's
    //     if(state.editing && state.view!=='overview') setupDrag();
    // is the whole rule: dragging is an EDIT-mode verb, and a board that
    // is merely being inspected must not move windows under a stray
    // mouse-down. `editing_` is read here as well as in the press
    // handler because a mode toggle mid-gesture (Esc while the button is
    // down, or the console leaving the overview) must stop the write on
    // the next motion rather than finish it — the press already
    // validated the mode once, and this is what makes that validation
    // still true on the second and tenth motion.
    //
    // Nothing is cleared here: a release still lands in
    // mouseReleaseEvent and clears the cursor and both flags, so a
    // gesture that is refused mid-flight ends in a consistent state.
    if (!editing_)
        return;
    if (subDragging_ && subDragIndex_ >= 0) {
        // ── v1.0.0 integration (stage A / O2 review ②) ──────────────────
        // THE VISIBILITY GUARD THE KEYBOARD ARM ALREADY HAS (see
        // :3255/:3266 — the arrow-key nudge refuses to move a window that
        // is not on the board). The mouse arm was the one path with no
        // such test: hide the window (or its whole panel) while the button
        // was down and every subsequent motion kept writing margins for a
        // box nobody can see, so the window drifted off behind the
        // user's back and reappeared somewhere they never put it.
        //
        // WHY A RETURN AND NOT A CANCEL. Mirrors the keyboard arm exactly:
        // the gesture state stays, the writes stop. A release still lands
        // in mouseReleaseEvent and clears the flags, so refusing a motion
        // mid-flight leaves the same consistent state the keyboard's
        // refusal does — and re-showing the window resumes from the same
        // start point rather than teleporting it by the distance the user
        // moved while it was hidden.
        //
        // FIDELITY-GATED like every other visibility read in this file: a
        // canvas nobody put into fidelity mode has consoleFidelity_ false
        // and never reaches this test, so its drag semantics are the ones
        // it always had.
        if (consoleFidelity_ && subDragIndex_ < subSlots_.size()) {
            const SubSlot &dragged = subSlots_[subDragIndex_];
            if (!dragged.visible)
                return;
            if (dragged.parentSlot < 0
                || dragged.parentSlot >= mhw::kPanelCount
                || !panelVisible_[dragged.parentSlot])
                return;
        }
        QRectF frame;
        qreal fit = 0.0;
        QSize logical;
        screenFrame(frame, fit, logical);
        if (fit <= 0.0) return;

        const QPoint delta = e->position().toPoint() - subDragStartMouse_;
        const int dxLogical = qRound(delta.x() / fit);
        const int dyLogical = qRound(delta.y() / fit);

        emit subWindowMoved(subDragIndex_,
                            movedSubMargins(subDragIndex_, dxLogical, dyLogical));
        return;
    }
    if (!dragging_ || dragIndex_ < 0) return;
    // ── v1.0.0 integration (stage A / O2 review ②), the mother half ─────
    // The keyboard nudge already refuses a hidden panel (:3266); the
    // mouse arm is the same write through a different gesture and needs
    // the same refusal. Bound-check first, because `dragging_` can
    // outlive a setLayoutMode() that emptied the slot vector.
    if (consoleFidelity_
        && (dragIndex_ >= mhw::kPanelCount || !panelVisible_[dragIndex_]))
        return;
    QRectF frame;
    qreal fit = 0.0;
    QSize logical;
    screenFrame(frame, fit, logical);
    if (fit <= 0.0) return;

    const QPoint delta = e->position().toPoint() - dragStartMouse_;
    const int dxLogical = qRound(delta.x() / fit);
    const int dyLogical = qRound(delta.y() / fit);

    emit panelMoved(dragIndex_, movedMargins(dragIndex_, dxLogical, dyLogical));
}

void HudCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    if (subDragging_) {
        subDragging_ = false;
        subDragIndex_ = -1;
        setCursor(Qt::ArrowCursor);
    }
    if (dragging_) {
        dragging_ = false;
        dragIndex_ = -1;
        setCursor(Qt::ArrowCursor);
    }
}

void HudCanvas::keyPressEvent(QKeyEvent *e)
{
    // v1.0.0: THE OVERVIEW NEVER NUDGES. The arrow keys write REAL
    // margins — panelMoved() / subWindowMoved() land on the live Panel —
    // so a read-only view must not pass them through. The base class
    // still gets them (scrolling, focus navigation), which is why this
    // forwards rather than swallows.
    if (overviewMode_) {
        QWidget::keyPressEvent(e);
        return;
    }
    // v1.0.0: AND NEITHER DOES A NON-EDITING BOARD. The prototype's
    //     if(state.editing && state.selected && state.view!=='overview'
    //        && ['ArrowUp',…].includes(e.key))
    // is the whole rule: a nudge is an edit-mode verb, and a keypress
    // that lands while the user is merely inspecting the board must not
    // move a window. Forwarding to the base class keeps the arrow keys
    // doing what Qt does with them when a QScrollArea owns the viewport
    // (scrolling), which is exactly what the overview arm does above —
    // so "not editing" and "overview" now read the same way, and the
    // one behaviour that changed is the missing write.
    if (!editing_) {
        QWidget::keyPressEvent(e);
        return;
    }
    // v0.14: the arrow keys move whichever window is selected, in that
    // window's own mode. A sub-window selected in Split nudges by
    // subWindowMoved(); a base panel selected in Single nudges by
    // panelMoved() exactly as before. Two index spaces, one key.
    //
    // v0.16: AND THE SAME EXEMPTION AS THE PAINT GATE. Pets is a base
    // panel the Split preview draws and the user can select, so with
    // pets selected in Split the arrow key must nudge the pets panel by
    // panelMoved() — not fall through to the base panel arm with
    // selected_ pointing at a panel that was never drawn. The sub-window
    // arm below already only fires for a live subSelected_, which a pets
    // selection never sets, so this reads the same two index spaces the
    // mouse path does.
    const int step = (e->modifiers() & Qt::ShiftModifier) ? kArrowBigStep : kArrowStep;
    int dx = 0, dy = 0;
    switch (e->key()) {
    case Qt::Key_Left:  dx = -step; break;
    case Qt::Key_Right: dx =  step; break;
    case Qt::Key_Up:    dy = -step; break;
    case Qt::Key_Down:  dy =  step; break;
    default:
        return QWidget::keyPressEvent(e);
    }
    if (!subSlots_.isEmpty() && subSelected_ >= 0
        && subSelected_ < subSlots_.size() && subSlots_[subSelected_].src) {
        // v1.0.0: A HIDDEN WINDOW DOES NOT MOVE. The pointer path
        // cannot reach one at all — layoutSubWindows() left its rect
        // empty, so no press lands in it — but the keyboard path works
        // off `subSelected_`, and a hide deliberately does NOT clear
        // that (a hide/show round trip must leave the user's selection
        // exactly where they left it). Without this guard the arrow keys
        // would write real margins for a window that is not on the
        // board: a change nobody can see from the console and nobody can
        // undo from it either.
        if (consoleFidelity_ && !subSlots_[subSelected_].visible) return;
        subDragStartMargins_ = subSlots_[subSelected_].src->margins();
        emit subWindowMoved(subSelected_,
                            movedSubMargins(subSelected_, dx, dy));
        return;
    }
    if (!slots_[selected_].bound) return;
    // v1.0.0: same guard for a base panel hidden by
    // setPanelWindowVisible(). It cannot be clicked either, but
    // setSelectedPanel() is a console-driven setter that does not go
    // through the hit test, so the key path needs its own check.
    if (consoleFidelity_ && !panelVisible_[selected_]) return;
    dragStartMargins_ = slots_[selected_].src->margins();
    emit panelMoved(selected_, movedMargins(selected_, dx, dy));
}

void HudCanvas::resizeEvent(QResizeEvent *)
{
    // v1.0.1 (#9): a resize moves the CONTENT-vs-VIEWPORT split (the
    // extent is ≥ size(), so shrinking the widget opens scroll range
    // and growing it closes it) — re-emit and re-clamp, then repaint.
    refreshViewportMetrics();
    update();
}
