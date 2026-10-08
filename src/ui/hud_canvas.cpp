#include "hud_canvas.h"

#include <QColor>
#include <QFont>
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
#include "panel_accent.h"
#include "panel_source.h"
#include "screen_query.h"
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

QString panelName(int index)
{
    const int i = mhw::isPanelIndex(index) ? index : 0;
    const QString key = QString::fromLatin1(kNameKeys[i]);
    const QString val = mh::tr(key);
    return val == key ? QString::fromLatin1(kNames[i]) : val;
}

constexpr int kHeader = 56;
constexpr int kFooter = 38;
constexpr int kArrowStep = 10;      // logical px per arrow press
constexpr int kArrowBigStep = 50;   // with Shift

// v0.8: removed the unused file-scope screenInfo() shim — every caller
// now goes through HudCanvas::previewScreenInfo(), which honours the
// user's output selection. See hud_canvas.h::setPreviewScreen.

QRect anchoredRect(const QRect &screen, Corner corner, const QMargins &m,
                   const QSize &content, qreal scale)
{
    const int w = std::max(1, int(content.width() * scale));
    const int h = std::max(1, int(content.height() * scale));
    int x = 0, y = 0;
    switch (corner) {
    case Corner::TopLeft:
        x = screen.left() + m.left();
        y = screen.top()  + m.top();
        break;
    case Corner::TopRight:
        x = screen.right() - w - m.right();
        y = screen.top()   + m.top();
        break;
    case Corner::BottomLeft:
        x = screen.left()  + m.left();
        y = screen.bottom() - h - m.bottom();
        break;
    case Corner::BottomRight:
        x = screen.right()  - w - m.right();
        y = screen.bottom() - h - m.bottom();
        break;
    }
    return QRect(x, y, w, h);
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
            this, [this] { rebuildScreenCache(); updateGeometry(); update(); });
    connect(static_cast<QGuiApplication *>(QCoreApplication::instance()),
            &QGuiApplication::screenRemoved,
            this, [this] { rebuildScreenCache(); updateGeometry(); update(); });
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
            this, [this] { rebuildScreenCache(); updateGeometry(); update(); });
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

    for (int i = 0; i < subSlots_.size(); ++i) {
        subRects_[i] = QRectF();
        const SubSlot &sub = subSlots_[i];
        if (!sub.src)
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
                                        sub.src->margins(), content, z);
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
    updateGeometry();
    update();
    emit zoomChanged(zoom_);
}

QSize HudCanvas::sizeHint() const
{
    if (zoom_ <= 1.0)
        return QSize(820, 520);
    // When zoomed, report the enlarged size so QScrollArea shows bars.
    const QSize phys = previewScreenInfo().physical;
    const qreal ar = phys.height() / qreal(phys.width());
    const int w = qRound(820 * zoom_);
    const int h = qRound(w * ar) + kHeader + kFooter;
    return QSize(w, std::max(520, h));
}

int HudCanvas::heightForWidth(int width) const
{
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
    updateGeometry();
    update();
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
QString resolveBoundOutput(const T *src, const QString &canvasScreen)
{
    if (!src)
        return canvasScreen;

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
    // Step 3. `name` is empty exactly when the source has no screen of
    // its own to name — which is the pre-fix condition, so the fallback
    // is the canvas's screen and nothing moves.
    return name.isEmpty() ? canvasScreen : name;
}

} // namespace detail

QString HudCanvas::boundOutputName(const PanelSource *src) const
{
    return detail::resolveBoundOutput(src, previewOutputName_);
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
    p.fillRect(rect(), QColor(6, 8, 10));

    for (int i = 0; i < mhw::kPanelCount; ++i)
        slots_[i].lastTarget_ = QRectF();

    // --- header ---
    QFont headFont(QStringLiteral("Chakra Petch"), 11, QFont::Medium);
    headFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    p.setFont(headFont);
    p.setPen(QColor(140, 145, 147));
    const auto &si = previewScreenInfo();
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

    // --- screen frame ---
    const QRectF avail = QRectF(rect()).adjusted(22, 46, -22, -kFooter);
    const QSize phys = si.physical;
    const QSize logical = si.logical;
    const qreal physAR = phys.height() / qreal(phys.width());
    QSizeF screen(avail.width(), avail.width() * physAR);
    if (screen.height() > avail.height())
        screen = QSizeF(avail.height() / physAR, avail.height());
    const QRectF frame(QPointF(avail.center().x() - screen.width() / 2,
                               avail.center().y() - screen.height() / 2),
                       screen);

    QLinearGradient bg(frame.topLeft(), frame.bottomRight());
    bg.setColorAt(0,    QColor(34, 50, 42));
    bg.setColorAt(0.45, QColor(15, 22, 22));
    bg.setColorAt(1,    QColor(6, 9, 10));
    p.fillRect(frame, bg);
    p.setPen(QPen(QColor(56, 60, 62), 1));
    p.drawRect(frame);

    if (showSafeArea_) {
        p.setPen(QPen(QColor(255, 255, 255, 28), 1, Qt::DashLine));
        p.drawRect(frame.adjusted(frame.width() * .055, frame.height() * .055,
                                  -frame.width() * .055, -frame.height() * .055));
    }

    if (showGrid_) {
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
    // SINGLE MODE IS BYTE-IDENTICAL: `!subSlots_.isEmpty()` is exactly
    // the same gate the old sub-window arm used, and setLayoutMode(Single)
    // clears the vector, so a Single-mode paint reaches this loop with
    // the same condition that was already there. The 10 pixel baselines
    // in tests/baseline/ are the authority and they do not move.
    const qreal fit = std::min(frame.width()  / logical.width(),
                               frame.height() / logical.height());
    auto logicalToCanvas = [&](const QRect &lr) {
        return QRectF(frame.left() + lr.x() * fit,
                      frame.top()  + lr.y() * fit,
                      lr.width()  * fit,
                      lr.height() * fit);
    };

    if (subSlots_.isEmpty()) {
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        if (!slots_[i].bound || !slots_[i].present) continue;
        const Slot &s = slots_[i];
        const qreal z = std::max(0.1, s.src->scale());
        const QSize cs = s.src->contentSize();
        const QRect lrect = anchoredRect(QRect(QPoint(0, 0), logical),
                                         s.src->corner(),
                                         s.src->margins(), cs, z);
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
    }
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
        for (const SubWindowPaintInfo &info : boxes)
            paintSubWindowBox(info, p);
    }

    // --- footer ---
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
    // THE PARENT'S OWN COLOUR. The split's whole visual claim is "these
    // boxes left that panel", and the parent's accent is how the preview
    // says it without a second legend.
    QColor accent = mhw::panel_accent::forSlot(info.parentSlot);

    // The box's own rendered pixmap, when the host pushed one. Drawn
    // first so the frame lands on top of it.
    if (info.index >= 0 && info.index < subSlots_.size()
        && !subSlots_[info.index].pixmap.isNull()
        && subSlots_[info.index].enabled) {
        p.save();
        p.drawPixmap(info.rect, subSlots_[info.index].pixmap,
                     subSlots_[info.index].pixmap.rect());
        p.restore();
    }

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

// ─── interaction ────────────────────────────────────────────────────────

void HudCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    // v0.14: IN SPLIT MODE THE SUB-WINDOWS ARE THE ONLY HIT TARGETS.
    // The four mother panels are not drawn, so their boxes must not be
    // hit-testable either — a click that landed on empty space and moved
    // the monster panel by accident is exactly the "several whole blocks
    // in the way" complaint one layer down. So the sub-window arm runs
    // first and returns; the slot arm below is reached only when the
    // canvas is in Single mode (where subSlots_ is empty by contract).
    if (!subSlots_.isEmpty()) {
        for (int i = subSlots_.size() - 1; i >= 0; --i) {
            if (i < subRects_.size() && subRects_[i].contains(e->position())) {
                const bool changed = (subSelected_ != i);
                subSelected_ = i;
                subDragging_ = true;
                subDragIndex_ = i;
                subDragStartMouse_ = e->position().toPoint();
                subDragStartMargins_ = subSlots_[i].src->margins();
                setCursor(Qt::ClosedHandCursor);
                if (changed)
                    emit subWindowSelected(i);
                update();
                return;
            }
        }
        return;   // a click on empty space selects nothing, as before
    }

    for (int i = mhw::kPanelCount - 1; i >= 0; --i) {
        if (slots_[i].lastTarget_.contains(e->position())) {
            if (i != selected_) {
                selected_ = i;
                update();
                emit panelSelected(i);
            }
            // Start drag
            dragging_ = true;
            dragIndex_ = i;
            dragStartMouse_ = e->position().toPoint();
            dragStartMargins_ = slots_[i].src->margins();
            setCursor(Qt::ClosedHandCursor);
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
    const QRectF avail = QRectF(rect()).adjusted(22, 46, -22, -kFooter);
    const QSize phys = si.physical;
    const qreal physAR = phys.height() / qreal(phys.width());
    QSizeF screen(avail.width(), avail.width() * physAR);
    if (screen.height() > avail.height())
        screen = QSizeF(avail.height() / physAR, avail.height());
    frame = QRectF(QPointF(avail.center().x() - screen.width() / 2,
                           avail.center().y() - screen.height() / 2),
                   screen);
    fit = ((logical.width() <= 0 || logical.height() <= 0))
        ? 0.0
        : std::min(frame.width() / logical.width(),
                   frame.height() / logical.height());
}

void HudCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (subDragging_ && subDragIndex_ >= 0) {
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
    // v0.14: the arrow keys move whichever window is selected, in that
    // window's own mode. A sub-window selected in Split nudges by
    // subWindowMoved(); a base panel selected in Single nudges by
    // panelMoved() exactly as before. Two index spaces, one key.
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
        subDragStartMargins_ = subSlots_[subSelected_].src->margins();
        emit subWindowMoved(subSelected_,
                            movedSubMargins(subSelected_, dx, dy));
        return;
    }
    if (!slots_[selected_].bound) return;
    dragStartMargins_ = slots_[selected_].src->margins();
    emit panelMoved(selected_, movedMargins(selected_, dx, dy));
}

void HudCanvas::resizeEvent(QResizeEvent *) { update(); }
