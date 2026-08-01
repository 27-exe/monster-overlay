#include "hud_canvas.h"

#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QRect>
#include <QRectF>
#include <QScreen>
#include <QSize>
#include <QtGlobal>
#include <algorithm>

#include "panel.h"
#include "panel_source.h"
#include "screen_query.h"
namespace {

// Per-panel accent — kept here so the canvas owns the visual identity
// rather than borrowing from the live panel paint code. The same
// colour family is reused for the selected panel's outline, the
// left-edge tag, and the safe-area marquee.
const QColor kPlayerAccent(167, 79, 255);
const QColor kMonsterAccent(255, 112, 67);
const QColor kDamageAccent(64, 169, 255);
const QColor kAccents[] = {kPlayerAccent, kMonsterAccent, kDamageAccent};
const char *kNames[] = {"PLAYER", "MONSTER", "DAMAGE"};

constexpr int kHeader = 56;
constexpr int kFooter = 38;

// Cached screen detection. The answer doesn't change for the
// lifetime of the application, so we run the multi-stage
// (QPlatformScreen → QScreen → xrandr/kscreen/wlr-randr → fallback)
// detection exactly once and reuse the result from every paint
// event. screen_query::Result is value-typed; the cache lives
// in a function-local static so we don't need a member.
struct ScreenInfo {
    screen_query::Result result;
    bool valid{false};
};

const screen_query::Result &screenInfo()
{
    static const ScreenInfo info{
        screen_query::detect(QGuiApplication::primaryScreen()),
        true};
    return info.result;
}

// Convert one Panel's anchor corner + margins to a rect inside the
// screen rect. The panel is drawn at its zoomed size; margins are
// the offset from the screen edges the live layer-shell surface uses.
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
    switch (c) {
    case Corner::TopLeft:     return QStringLiteral("TOP LEFT");
    case Corner::TopRight:    return QStringLiteral("TOP RIGHT");
    case Corner::BottomLeft:  return QStringLiteral("BOTTOM LEFT");
    case Corner::BottomRight: return QStringLiteral("BOTTOM RIGHT");
    }
    return QStringLiteral("UNKNOWN");
}

} // namespace

// Concrete adapter for a real Panel instance is declared in hud_canvas.h.

HudCanvas::HudCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(520, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

int HudCanvas::heightForWidth(int width) const
{
    const QSize screen = screenInfo().physical;
    const qreal ar = screen.height() / qreal(screen.width());
    return std::max(360, qRound(width * ar) + kHeader + kFooter);
}

QSize HudCanvas::screenSize() const { return screenInfo().physical; }

QString HudCanvas::screenLabel() const
{
    const auto &r = screenInfo();
    return QStringLiteral("%1 × %2")
        .arg(r.physical.width())
        .arg(r.physical.height());
}

QString HudCanvas::cornerLabel(int index) const
{
    if (index < 0 || index >= 3 || !slots_[index].bound) return {};
    return cornerName(slots_[index].src->corner());
}

void HudCanvas::setPanelPixmap(int index, const QPixmap &pixmap, bool enabled)
{
    if (index < 0 || index >= 3) return;
    slots_[index].pixmap = pixmap;
    slots_[index].enabled = enabled;
    update();
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
    if (index < 0 || index >= 3 || selected_ == index) return;
    selected_ = index;
    update();
}

void HudCanvas::bindPanel(int index, const PanelSource *src)
{
    if (index < 0 || index >= 3) return;
    slots_[index].src = src;
    slots_[index].bound = (src != nullptr);
    update();
}

void HudCanvas::resizeEvent(QResizeEvent *) { update(); }

void HudCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.fillRect(rect(), QColor(6, 8, 10));

    // Zero hit-test rects so un-bound slots aren't clickable.
    for (int i = 0; i < 3; ++i) slots_[i].lastTarget_ = QRectF();

    // --- header ---
    QFont headFont(QStringLiteral("Chakra Petch"), 11, QFont::Medium);
    headFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    p.setFont(headFont);
    p.setPen(QColor(140, 145, 147));
    const auto &si = screenInfo();
    const QString head = QStringLiteral(
        "LIVE HUD CANVAS  ·  %1 × %2 PHYS  ·  %3 × %4 LOGICAL  ·  DPR ×%5  ·  %6")
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

    // --- screen frame inside the widget ---
    // The frame represents the PHYSICAL screen (what the user sees in
    // the panel settings). Panel margins, however, are stored by the
    // overlay as LOGICAL pixels (LayerShellQt convention) — the same
    // number a `--margin` value takes on the command line. So we draw
    // the screen at physical scale but compute the panel rect in
    // logical space and remap via the logical fit factor.
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

    // safe area (5% inset, dashed) — toggleable via stagebar.
    if (showSafeArea_) {
        p.setPen(QPen(QColor(255, 255, 255, 28), 1, Qt::DashLine));
        p.drawRect(frame.adjusted(frame.width() * .055, frame.height() * .055,
                                  -frame.width() * .055, -frame.height() * .055));
    }

    // grid crosshair — toggleable via stagebar.
    if (showGrid_) {
        p.setPen(QPen(QColor(255, 255, 255, 33), 1, Qt::DashLine));
        p.drawLine(QPointF(frame.left(),  frame.center().y()),
                   QPointF(frame.right(), frame.center().y()));
        p.drawLine(QPointF(frame.center().x(), frame.top()),
                   QPointF(frame.center().x(), frame.bottom()));
    }

    // scale ruler — physical pixels (what the user can read off
    // a display-settings dialog). The user can mentally divide by
    // DPR × 1.30 to get the logical margin value.
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
    // Fit the LOGICAL screen rect into the canvas frame; panel
    // placement (corner + margin + content×scale) all lives in
    // logical space, matching what Panel::loadConfig() and
    // nudgeMargins() actually write to LayerShellQt.
    const qreal fit = std::min(frame.width()  / logical.width(),
                               frame.height() / logical.height());
    auto logicalToCanvas = [&](const QRect &lr) {
        return QRectF(frame.left() + lr.x() * fit,
                      frame.top()  + lr.y() * fit,
                      lr.width()  * fit,
                      lr.height() * fit);
    };

    for (int i = 0; i < 3; ++i) {
        if (!slots_[i].bound) continue;
        const Slot &s = slots_[i];
        const qreal z = std::max(0.1, s.src->scale());
        const QSize cs = s.src->contentSize();
        // Compute the panel rect in LOGICAL screen coords, then map.
        const QRect lrect = anchoredRect(QRect(QPoint(0, 0), logical),
                                         s.src->corner(),
                                         s.src->margins(), cs, z);
        const QRectF target = logicalToCanvas(lrect);

        if (!s.pixmap.isNull() && s.enabled) {
            // v0.5 UI-link: honor the panel's live opacity so the
            // canvas preview isn't always full-alpha.
            const double opac = s.src ? s.src->opacity() : 1.0;
            if (opac < 1.0)
                p.setOpacity(opac);
            p.save();
            p.setOpacity(i == selected_ ? 1.0 : 0.55);
            p.drawPixmap(target, s.pixmap, s.pixmap.rect());
            if (opac < 1.0)
                p.setOpacity(1.0);
            p.restore();
        } else {
            // disabled placeholder: dashed outline at the same anchored rect
            QColor accent = kAccents[i];
            accent.setAlpha(110);
            p.setPen(QPen(accent, 1, Qt::DashLine));
            p.setBrush(QColor(0, 0, 0, 80));
            p.drawRect(target);

            p.setPen(QColor(170, 174, 176));
            p.setFont(QFont(QStringLiteral("Chakra Petch"), 8, QFont::Medium));
            p.drawText(target, Qt::AlignCenter,
                       QStringLiteral("%1 · DISABLED").arg(QLatin1String(kNames[i])));
        }

        if (i == selected_ && s.enabled) {
            QColor ring = kAccents[i];
            QPen ringPen(ring, 1.4);
            p.setPen(ringPen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(target.adjusted(-2, -2, 2, 2));

            p.setFont(QFont(QStringLiteral("Chakra Petch"), 8, QFont::Medium));
            p.setPen(ring);
            const QString tag = QStringLiteral("SELECTED · %1  ·  %2 PX")
                .arg(QLatin1String(kNames[i]))
                .arg(int(z * cs.width()));
            const QFontMetrics fm(p.font());
            const int tagW = fm.horizontalAdvance(tag) + 14;
            QRectF tagBox(target.left() - 2, target.top() - 18, tagW, 14);
            p.fillRect(tagBox, QColor(0, 0, 0, 180));
            p.drawText(tagBox, Qt::AlignCenter, tag);
        }

        // Cache for hit-test. Mouse clicks outside the visible target
        // shouldn't select a panel we drew underneath.
        slots_[i].lastTarget_ = target;
    }

    // --- footer ---
    p.setPen(QColor(60, 64, 66));
    p.drawLine(22, height() - 30, width() - 22, height() - 30);
    QFont footFont(QStringLiteral("Chakra Petch"), 9, QFont::Medium);
    footFont.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    p.setFont(footFont);

    p.setPen(QColor(170, 174, 176));
    p.drawText(QRectF(22, height() - 24, width() - 44, 16),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("SELECTED: %1     ANCHORED: %2")
                   .arg(QLatin1String(kNames[selected_]))
                   .arg(cornerLabel(selected_)));

    p.setPen(QColor(96, 100, 102));
    p.drawText(QRectF(22, height() - 24, width() - 44, 16),
               Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("SCREEN %1  ·  ZOOM ×%2  ·  DEMO DATA")
                   .arg(screenLabel())
                   .arg(QString::number(slots_[selected_].bound
                                            ? slots_[selected_].src->scale()
                                            : 1.0, 'f', 2)));
}

void HudCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    // Walk the cache populated by paintEvent. We pick the last
    // (top-most) hit on purpose so callers don't need to manage
    // z-order; targets are non-overlapping on the default layout.
    for (int i = 2; i >= 0; --i) {
        if (slots_[i].lastTarget_.contains(e->position())) {
            if (i != selected_) {
                selected_ = i;
                update();
                emit panelSelected(i);
            }
            return;
        }
    }
}
