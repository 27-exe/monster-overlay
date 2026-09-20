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
#include "panel_source.h"
#include "screen_query.h"
#include "core/string_table.h"

namespace mh {
// v0.9 i18n: same inline alias as the console / overlay panels (ODR-safe).
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

namespace {

const QColor kPlayerAccent(167, 79, 255);
const QColor kMonsterAccent(255, 112, 67);
const QColor kDamageAccent(64, 169, 255);
const QColor kPetsAccent(103, 214, 157);
const std::array<QColor, mhw::kPanelCount> kAccents = {
    kPlayerAccent, kMonsterAccent, kDamageAccent, kPetsAccent,
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
        {Corner::TopLeft,     "console.corner.topLeft",     "TOP LEFT"},
        {Corner::TopRight,    "console.corner.topRight",    "TOP RIGHT"},
        {Corner::BottomLeft,  "console.corner.bottomLeft",  "BOTTOM LEFT"},
        {Corner::BottomRight, "console.corner.bottomRight", "BOTTOM RIGHT"},
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

// ─── construction ───────────────────────────────────────────────────────

HudCanvas::HudCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(520, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::ClickFocus);   // receive arrow keys after click
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
// out to kscreen-doctor / wlr-randr with an 800 ms budget, and it would
// stutter the canvas every time the user drags a panel. QScreen's own
// geometry + devicePixelRatio is good enough for preview purposes.
const screen_query::Result &HudCanvas::previewScreenInfo() const
{
    // v0.8: cached screen list, keyed by QScreen::name(). Built lazily
    // (first call) and never invalidated — outputs don't come and go
    // mid-session often enough to bother hooking QScreen::destroyed;
    // if the user docks a new monitor the next setPreviewScreen() call
    // will trigger a re-paint and the worst case is "preview shows the
    // old size until you click SCREEN again", which is acceptable.
    static const screen_query::Result *kFallback = []() -> const screen_query::Result * {
        static const screen_query::Result r = screen_query::detect();
        return &r;
    }();
    if (previewOutputName_.isEmpty())
        return *kFallback;
    static const QHash<QString, const screen_query::Result *> kByName = []() {
        QHash<QString, const screen_query::Result *> m;
        static QHash<QString, screen_query::Result> owned;
        owned.clear();
        for (QScreen *s : QGuiApplication::screens()) {
            if (!s) continue;
            screen_query::Result r;
            r.physical = s->geometry().size() * s->devicePixelRatio();
            r.logical  = s->geometry().size();
            r.dpr      = s->devicePixelRatio();
            r.source   = screen_query::Source::QScreen;
            owned.insert(s->name(), r);
            m.insert(s->name(), &owned[s->name()]);
        }
        return m;
    }();
    const auto it = kByName.constFind(previewOutputName_);
    if (it != kByName.cend())
        return *it.value();
    return *kFallback;
}

void HudCanvas::setPreviewScreen(QString outputName)
{
    if (outputName == previewOutputName_)
        return;
    previewOutputName_ = std::move(outputName);
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
            const QString tag = mh::tr(QStringLiteral("console.canvas.selectedTag"))
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
               mh::tr(QStringLiteral("console.canvas.footerRight"))
                   .arg(screenLabel())
                   .arg(QString::number(zoom_, 'f', 1)));
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
