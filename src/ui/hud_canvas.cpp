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

// Preview chrome, in CANVAS pixels (not logical screen pixels): the gap
// between a parent panel and its cluster of sub-windows, and between
// successive boxes in that cluster. Both have to stay legible at the
// default fit, where an entire 1920px screen is about 760px wide.
constexpr qreal kSubClusterGap = 8.0;
constexpr qreal kSubBoxGap = 5.0;
// The title strip above each box, so the user can name the window.
constexpr int kSubTitleH = 12;

// How far DOWN the cluster may grow before it starts a new column.
//
// The first cut stacked every sub-window of a parent into one vertical
// column, because that is the most obvious layout and because it cannot
// collide with the parent. With the player panel it is also fine: six
// windows, a few hundred pixels. With seven to thirteen of them the column
// runs past the edge of a screen that is only ~760px wide at the default
// fit, and everything below the fold is simply not drawn — the preview
// then shows "all the panels piled in one corner", which is what the
// user reported.
//
// So the cluster WRAPS at a depth, starting a fresh column further out
// from the parent, in the direction the cluster is already growing. Depth
// is measured in boxes rather than pixels so the number of columns is
// predictable from the mask: 4 max keeps the whole player cluster inside a
// 1080p screen at the default fit (4 boxes x ~110px = 440px of column,
// plus the parent's own box).
constexpr int kSubClusterMaxRows = 4;

// The gap between two columns of the same cluster. Wider than kSubBoxGap
// so the wrap reads as a deliberate step rather than a stray box.
constexpr qreal kSubColumnGap = 14.0;

// Every sub-window box is drawn at this fraction of the size its content
// reserves, for the same reason a parent's pixmap is drawn at the
// window's own scale: a 380px damage bar drawn 1:1 beside the 378px
// damage panel it came from would say nothing about why it became a
// separate window.
constexpr qreal kSubPreviewScale = 0.5;

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
    if (mode == mhw::PlayerSection::PlayerLayoutMode::Single)
        subSlots_.clear();
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

// v0.8: pick the screen_query::Result that drives every "screen frame"
// decision in this widget — preview rectangle, drag clamps, ruler ticks.
// Resolution order:
//   1. previewOutputName_ matched against QGuiApplication::screens()
//   2. QGuiApplication::primaryScreen() (matches the v0.7.x behaviour
//      where the preview always showed the primary output)
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
const screen_query::Result &HudCanvas::previewScreenInfo() const
{
    // Both paths read CURRENT data: any cache miss triggers a rebuild,
    // so a newly plugged output is found and a removed one is gone.
    if (!screenCacheValid_)
        rebuildScreenCache();

    // Unchanged semantics: empty previewOutputName_ (the default)
    // follows the primary screen.
    if (previewOutputName_.isEmpty())
        return primaryScreenInfo();

    const auto it = screenCacheByName_.constFind(previewOutputName_);
    if (it != screenCacheByName_.cend())
        return *it;
    // previewOutputName_ no longer resolves to a live output — the
    // monitor was unplugged, or Qt has not announced a hot-plugged one
    // yet. Rebuild once in case the screen list moved since the last
    // build, then re-check, so a just-plugged-in output is picked up on
    // this very call instead of on the next one.
    rebuildScreenCache();
    const auto retry = screenCacheByName_.constFind(previewOutputName_);
    if (retry != screenCacheByName_.cend())
        return *retry;
    // Still nothing: the output is genuinely gone. Same destination as
    // the empty-name case, but the fallback was invalidated along with
    // the topology, so primaryScreenInfo() re-resolves it rather than
    // reusing a stale answer.
    return primaryScreenInfo();
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

// ─── geometry helpers ───────────────────────────────────────────────────

QMargins HudCanvas::movedMargins(int index, int dxLogical, int dyLogical) const
{
    if (!mhw::isPanelIndex(index) || !slots_[index].bound) return {};
    const Corner corner = slots_[index].src->corner();
    QMargins m = dragStartMargins_;   // set at drag/key start
    const QSize logical = previewScreenInfo().logical;
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
    const qreal fit = std::min(frame.width()  / logical.width(),
                               frame.height() / logical.height());
    auto logicalToCanvas = [&](const QRect &lr) {
        return QRectF(frame.left() + lr.x() * fit,
                      frame.top()  + lr.y() * fit,
                      lr.width()  * fit,
                      lr.height() * fit);
    };

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

    // --- v0.13: the split's sub-windows ---
    //
    // ENTIRELY ABSENT IN SINGLE MODE. The gate is `subSlots_` being
    // non-empty, which setLayoutMode(Single) and a never-populated
    // canvas both guarantee. So the four-slot arm above is the last
    // thing a single-mode preview paints, and its output is byte-for-byte
    // what it was before any of this existed.
    //
    // Drawn cluster by cluster, grouped by the parent each sub-window
    // came from, because that is what makes the derivation readable: the
    // user sees the boxes that left the monster panel stacking beside the
    // monster panel's own rectangle, in the monster panel's accent
    // colour. A flat list of 14 boxes would say nothing about which
    // window owns which block.
    if (!subSlots_.isEmpty()) {
        for (int parent = 0; parent < mhw::kPanelCount; ++parent) {
            if (!slots_[parent].bound || !slots_[parent].present)
                continue;   // the parent is not in this preview
            if (slots_[parent].lastTarget_.isNull())
                continue;   // it painted nothing, so nothing to stack beside
            paintSubWindowCluster(slots_[parent].lastTarget_, parent,
                                  slots_[parent].src->corner(), p);
        }
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

// ─── v0.13: the split's sub-windows ────────────────────────────────────

// Paint every sub-window that was carved out of one parent panel.
//
// GROUPED BY PARENT, in table-row order, because that is what makes the
// derivation readable: the user sees the boxes that left the monster
// panel stacked beside the monster panel's own rectangle, in the
// monster panel's accent colour. A flat list of fourteen boxes would
// say nothing about which window owns which block.
//
// THE PARENT STAYS WHERE IT IS. Its rectangle is the anchor the live
// overlay will use, and the split does not move it — it only takes
// blocks out of it. So the cluster is laid out BESIDE the parent on its
// anchor side (right for a left anchor, left for a right anchor),
// growing away from the screen edge the parent is pinned to.
void HudCanvas::paintSubWindowCluster(const QRectF &parentTarget, int parentSlot,
                                      const Corner parentCorner, QPainter &p) const
{
    QVector<SubWindowPaintInfo> boxes;
    boxes.reserve(static_cast<int>(subSlots_.size()));

    // Where the cluster starts, and which way it grows.
    const bool growRight = (parentCorner == Corner::TopLeft
                            || parentCorner == Corner::BottomLeft);
    qreal edgeX = growRight ? parentTarget.right() + kSubClusterGap
                            : parentTarget.left() - kSubClusterGap;
    // Boxes placed in the column currently being filled. Reset when the
    // column wraps — see the wrap below.
    int rowsInThisColumn = 0;
    qreal cursorY = parentTarget.top();
    // A right-anchored cluster extends leftwards from the same edge, so
    // `leftX` tracks the next box's left edge as it goes. Both axes are
    // plain qreals (not const) because the loop advances them per box.
    qreal leftX = edgeX;

    for (const SubSlot &sub : subSlots_) {
        if (sub.parentSlot != parentSlot || !sub.src)
            continue;
        // THE CONTRACT, READ NOT GUESSED. The box's extent is the
        // window's own contentWidth()/contentHeight() (Panel's v0.12
        // layout vocabulary) — the number the live surface reserves the
        // same way, plus the margin the window reports. Nothing here
        // re-derives a width, so a window whose content grew
        // automatically gets a box that grew with it, and the split
        // preview cannot drift from the window it is previewing.
        const QSize content = sub.src->contentSize();
        const int cw = content.width();
        const int ch = content.height();
        if (cw <= 0 || ch <= 0)
            continue;   // nothing reserved, so nothing to represent

        SubWindowPaintInfo info;
        info.parentSlot = parentSlot;
        info.title = subWindowLabel(parentSlot, sub.tableRow);
        const qreal w = qMax<qreal>(1.0, cw * kSubPreviewScale);
        const qreal h = qMax<qreal>(1.0, ch * kSubPreviewScale);

        if (growRight) {
            info.rect = QRectF(QPointF(edgeX, cursorY), QSizeF(w, h));
            edgeX += w + kSubBoxGap;
        } else {
            leftX -= w;
            info.rect = QRectF(QPointF(leftX, cursorY), QSizeF(w, h));
            leftX -= kSubBoxGap;
        }
        // The line starts on the parent's near edge, level with the box.
        info.anchor = QPointF(growRight ? parentTarget.right()
                                        : parentTarget.left(),
                              info.rect.center().y());
        boxes.append(info);

        // Vertical stacking keeps the cluster clear of the screen's
        // other corner, which would otherwise collide when two panels
        // share the top edge — until the column is kSubClusterMaxRows
        // deep, at which point starting another column further out beats
        // running off the canvas and drawing nothing.
        //
        // The wrap is per column, so the row index resets with it: a new
        // column always starts level with the parent's top edge.
        ++rowsInThisColumn;
        if (rowsInThisColumn >= kSubClusterMaxRows) {
            rowsInThisColumn = 0;
            cursorY = parentTarget.top();
            const qreal step = w + kSubColumnGap;
            if (growRight)
                edgeX += step;
            else
                leftX -= step;
            continue;
        }
        cursorY += h + kSubBoxGap;
    }

    if (boxes.isEmpty())
        return;

    // Drawn nearest-the-parent LAST, so the cluster reads outward in
    // both directions and the parent's own box is never occluded.
    for (int i = boxes.size() - 1; i >= 0; --i)
        paintSubWindowBox(boxes[i], parentTarget, parentCorner, p);
}

// One sub-window box: the connection line, the frame, the title.
void HudCanvas::paintSubWindowBox(const SubWindowPaintInfo &info,
                                 const QRectF &parentTarget,
                                 const Corner parentCorner, QPainter &p) const
{
    Q_UNUSED(parentTarget)
    Q_UNUSED(parentCorner)

    // THE PARENT'S OWN COLOUR. The split's whole visual claim is "these
    // boxes left that panel", and the parent's accent — the same table
    // the parent's own frame and the live overlay read — is how the
    // preview says it without a second legend. Dimmer than the parent's
    // fill, so the main surface still reads as primary.
    QColor accent = mhw::panel_accent::forSlot(info.parentSlot);

    // Connection line first, so the frame's border lands on top of it.
    accent.setAlpha(80);
    p.save();
    p.setPen(QPen(accent, 1, Qt::DashLine));
    p.drawLine(info.anchor, QPointF(info.rect.center().x(),
                                    info.rect.center().y()));
    p.restore();

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
}

// ─── interaction ────────────────────────────────────────────────────────

void HudCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
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

void HudCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (!dragging_ || dragIndex_ < 0) return;
    const auto &si = previewScreenInfo();
    const QSize logical = si.logical;
    const QRectF avail = QRectF(rect()).adjusted(22, 46, -22, -kFooter);
    const QSize phys = si.physical;
    const qreal physAR = phys.height() / qreal(phys.width());
    QSizeF screen(avail.width(), avail.width() * physAR);
    if (screen.height() > avail.height())
        screen = QSizeF(avail.height() / physAR, avail.height());
    const qreal fit = std::min(screen.width()  / logical.width(),
                               screen.height() / logical.height());
    if (fit <= 0.0) return;

    const QPoint delta = e->position().toPoint() - dragStartMouse_;
    const int dxLogical = qRound(delta.x() / fit);
    const int dyLogical = qRound(delta.y() / fit);

    emit panelMoved(dragIndex_, movedMargins(dragIndex_, dxLogical, dyLogical));
}

void HudCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        dragIndex_ = -1;
        setCursor(Qt::ArrowCursor);
    }
}

void HudCanvas::keyPressEvent(QKeyEvent *e)
{
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
    if (!slots_[selected_].bound) return;
    dragStartMargins_ = slots_[selected_].src->margins();
    emit panelMoved(selected_, movedMargins(selected_, dx, dy));
}

void HudCanvas::resizeEvent(QResizeEvent *) { update(); }
