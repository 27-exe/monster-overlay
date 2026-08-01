#include "panel.h"

#include <LayerShellQt/Window>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QScreen>
#include <QTimer>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kMinScale = 0.5;
constexpr double kMaxScale = 3.0;
constexpr double kScaleStep = 0.1;
constexpr int kBorderWidth = 2;

QSettings &settings()
{
    static QSettings s(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("mhw-linux-overlay"),
                       QStringLiteral("panels"));
    return s;
}

// Default margin (offset from the anchored edges) per corner. Chosen
// so the three panels (player=top-left, monster=top-right,
// damage=bottom-right) do not overlap on a typical screen.
QMargins defaultMarginsFor(Corner corner)
{
    switch (corner) {
    case Corner::TopLeft:     return QMargins(20, 20, 0, 0);
    case Corner::TopRight:    return QMargins(0, 20, 20, 0);
    case Corner::BottomLeft:  return QMargins(20, 0, 0, 20);
    case Corner::BottomRight: return QMargins(0, 0, 20, 20);
    }
    return QMargins(20, 20, 0, 0);
}

// Clamp a margin component to a sane non-negative range so a panel
// can never be dragged off-screen.
int clampMargin(int v)
{
    return std::clamp(v, 0, 8000);
}

} // namespace

Panel::Panel(const QString &settingsKey, Corner corner, QWidget *parent)
    : QMainWindow(parent)
    , key_(settingsKey)
    , corner_(corner)
    , margins_(defaultMarginsFor(corner))
{
    setObjectName(QStringLiteral("mhw-panel-%1").arg(settingsKey));
    // v0.3: layer-shell with a transparent surface lets us composite
    // qpa-level alpha. Setting WA_OpaquePaintEvent caused SVG/PNG
    // icons to drop out (any QPixmap painted with Qt::transparent
    // alpha was discarded by the compositor). Keep the window
    // chromeless and transparent; the per-pixel chrome still draws
    // an opaque background inside each panel.
    setAttribute(Qt::WA_TranslucentBackground);
    // Don't let Qt raise / activate the panel window when it transitions
    // visible→shown (e.g. when we enter a hunting zone and the monster
    // / damage panels pop up).  KWin otherwise steals keyboard focus
    // from the game and adds an entry for our panel to the task bar.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setMouseTracking(true);

    // Safety-net default size so the surface is never 0x0. Subclasses
    // call setContentSize() once they know their real content size.
    setFixedSize(320, 120);

    // Load persisted config, then set up the layer-shell surface
    // (anchors + margins) BEFORE the window is shown. Showing first
    // and configuring layer-shell after triggers "already has a shell
    // integration" warnings.
    loadConfig();
    applyGeometry();
}

void Panel::applyGeometry()
{
    // Ensure a native window exists before touching layer-shell state.
    QWindow *native = windowHandle();
    if (!native) {
        (void)winId();
        native = windowHandle();
    }
    if (!native)
        return;

    LayerShellQt::Window *layer = LayerShellQt::Window::get(native);
    if (!layer)
        return;

    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    // Default: KeyboardInteractivityNone — the surface is invisible
    // to the compositor's focus chain. Show/hide never steals focus,
    // no taskbar entry, game stays fullscreen. Clicking the panel
    // dynamically switches to OnDemand (see mousePressEvent).
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setExclusiveZone(-1);
    layer->setScope(QStringLiteral("mhw-linux-overlay"));
    layer->setActivateOnShow(false);

    // Anchor the panel to its corner. This is the KEY difference from
    // the buggy version: without anchors, layer-shell expands the
    // overlay to fill the entire screen (locking the user out).
    LayerShellQt::Window::Anchors anchors;
    switch (corner_) {
    case Corner::TopLeft:
        anchors |= LayerShellQt::Window::AnchorTop;
        anchors |= LayerShellQt::Window::AnchorLeft;
        break;
    case Corner::TopRight:
        anchors |= LayerShellQt::Window::AnchorTop;
        anchors |= LayerShellQt::Window::AnchorRight;
        break;
    case Corner::BottomLeft:
        anchors |= LayerShellQt::Window::AnchorBottom;
        anchors |= LayerShellQt::Window::AnchorLeft;
        break;
    case Corner::BottomRight:
        anchors |= LayerShellQt::Window::AnchorBottom;
        anchors |= LayerShellQt::Window::AnchorRight;
        break;
    }
    layer->setAnchors(anchors);
    layer->setMargins(margins_);

    // Tell the compositor the desired surface size (the free,
    // non-anchored dimensions for a corner anchor).
    layer->setDesiredSize(size());
}

void Panel::loadConfig()
{
    const QMargins def = defaultMarginsFor(corner_);
    settings().beginGroup(key_);
    margins_.setLeft(clampMargin(settings().value(QStringLiteral("ml"), def.left()).toInt()));
    margins_.setTop(clampMargin(settings().value(QStringLiteral("mt"), def.top()).toInt()));
    margins_.setRight(clampMargin(settings().value(QStringLiteral("mr"), def.right()).toInt()));
    margins_.setBottom(clampMargin(settings().value(QStringLiteral("mb"), def.bottom()).toInt()));
    scale_ = settings().value(QStringLiteral("scale"), 2.0).toDouble();   // v0.3: 2x scale to make icons undeniably visible
    opacity_ = settings().value(QStringLiteral("opacity"), 1.0).toDouble();
    settings().endGroup();

    scale_ = std::clamp(scale_, kMinScale, kMaxScale);
    opacity_ = std::clamp(opacity_, 0.1, 1.0);

    // Note: visibility is NOT applied here. The window must not be
    // shown until after applyGeometry() configures the layer-shell
    // surface (anchors + margins); showing first triggers "already
    // has a shell integration" warnings. main.cpp shows the panels.
    if (!testAttribute(Qt::WA_DontShowOnScreen))
        setWindowOpacity(opacity_);
}

void Panel::saveConfig()
{
    settings().beginGroup(key_);
    settings().setValue(QStringLiteral("ml"), margins_.left());
    settings().setValue(QStringLiteral("mt"), margins_.top());
    settings().setValue(QStringLiteral("mr"), margins_.right());
    settings().setValue(QStringLiteral("mb"), margins_.bottom());
    settings().setValue(QStringLiteral("scale"), scale_);
    settings().setValue(QStringLiteral("opacity"), opacity_);
    settings().setValue(QStringLiteral("visible"), isVisible());
    settings().endGroup();
    settings().sync();
}

void Panel::saveAppearance()
{
    settings().beginGroup(key_);
    settings().setValue(QStringLiteral("scale"), scale_);
    settings().setValue(QStringLiteral("opacity"), opacity_);
    settings().endGroup();
}

void Panel::setEditMode(bool on)
{
    editMode_ = on;
    setCursor(on ? Qt::SizeAllCursor : Qt::ArrowCursor);
    // Edit mode needs keyboard from the start (arrow keys, Esc).
    // applyGeometry() resets to None, so override afterwards.
    applyGeometry();
    setLayerKeyboardInteractivity(on);
    update();
}

void Panel::setVisible(bool visible)
{
    // Master visibility gate. When panelEnabled_ is false the panel is
    // completely unmounted regardless of what the caller asks for. This
    // is what the control console's master toggle drives: flipping the
    // toggle off should not leave a 32-40px chrome sliver in the
    // corner, it should remove the layer-shell surface entirely. The
    // section mask is a separate axis (each sub independently visible /
    // hidden) — the master gate is strictly above that.
    if (!panelEnabled_ && visible)
        return;

    // Hide on first hide is fine; show without activating is the
    // important path. Without WA_ShowWithoutActivating, KWin raises
    // the panel into focus as soon as it appears, which (a) makes
    // the task bar pop up with an entry for our window and (b) takes
    // keyboard focus away from the game whenever we go visible.
    // Setting this once is enough; Qt honours it for every show.
    QMainWindow::setVisible(visible);
}

void Panel::setPanelEnabled(bool on)
{
    if (panelEnabled_ == on)
        return;
    panelEnabled_ = on;
    if (!on)
        setVisible(false);   // honor the new gate immediately
    else
        update();            // caller will follow up with setVisible(true)
}

void Panel::setContentSize(int w, int h)
{
    logicalSize_ = QSize(w, h);
    // Actual window size tracks the zoom so painted content is
    // never clipped at scale != 1.0.
    const int aw = static_cast<int>(std::lround(w * scale_));
    const int ah = static_cast<int>(std::lround(h * scale_));

    // Idempotent guard: paintDemo/paintPanel call setContentSize() on
    // every repaint, and a drag fires update() each frame. Re-issuing
    // setFixedSize() + setDesiredSize() every frame makes the
    // compositor reconfigure the surface repeatedly — that's the
    // flicker. Only touch the compositor when the size actually
    // changed.
    if (size() == QSize(aw, ah))
        return;

    setFixedSize(aw, ah);

    // Keep the layer-shell desired size in sync so the compositor
    // allocates the right surface dimensions.
    QWindow *native = windowHandle();
    if (native) {
        LayerShellQt::Window *layer = LayerShellQt::Window::get(native);
        if (layer)
            layer->setDesiredSize(QSize(aw, ah));
    }
}

void Panel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);


    // Edit-mode border so the user knows this panel is interactive.
    // Drawn in actual (device) coordinates, before any scaling.
    if (editMode_) {
        p.setPen(QPen(QColor(120, 180, 255), kBorderWidth, Qt::DashLine));
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }

    // Apply the user's zoom (edit-mode wheel scroll). paintPanel
    // then draws in logical coordinates; the window size is kept in
    // sync via setContentSize() so nothing is clipped.
    if (scale_ != 1.0)
        p.scale(scale_, scale_);

    // Minimized: small colored block with panel initial.
    if (minimized_) {
        paintMinimized(p);
        return;
    }

    // In edit mode, draw demo content so the panel stays visible
    // and identifiable for positioning. In live mode, paintPanel()
    // handles both the connected (real data) and disconnected
    // ("not connected" placeholder) cases itself.
    // Demo content shown in edit mode: build mock data on the first
    // call only (paintEvent may re-enter when setContentSize resizes
    // the widget, and any heavy work inside paintDemo otherwise becomes
    // a CPU hotspot). We delegate to paintPanel so the edit preview is
    // identical to live behaviour.
    if (!demoPrimed_) {
        demoPrimed_ = true;
        setupDemoData();
    }

    paintPanel(p);
}
void Panel::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    // Dynamically enter the focus chain: switch layer-shell keyboard
    // interactivity from None → OnDemand, then request activation.
    // The compositor grants keyboard focus; Esc/Space become usable.
    setLayerKeyboardInteractivity(true);
    if (QWindow *w = windowHandle())
        w->requestActivate();
    setFocus();
}

void Panel::focusOutEvent(QFocusEvent *e)
{
    QMainWindow::focusOutEvent(e);
    // Drop out of the focus chain so the next show/hide cycle is
    // invisible to the compositor (no focus steal, no taskbar entry).
    if (!editMode_)
        setLayerKeyboardInteractivity(false);
}

void Panel::setLayerKeyboardInteractivity(bool interactive)
{
    QWindow *native = windowHandle();
    if (!native)
        return;
    LayerShellQt::Window *layer = LayerShellQt::Window::get(native);
    if (!layer)
        return;
    layer->setKeyboardInteractivity(interactive
        ? LayerShellQt::Window::KeyboardInteractivityOnDemand
        : LayerShellQt::Window::KeyboardInteractivityNone);
}

void Panel::keyPressEvent(QKeyEvent *e)
{
    // Space: toggle minimize — works in BOTH edit and live mode.
    if (e->key() == Qt::Key_Space) {
        minimized_ = !minimized_;
        if (minimized_) {
            normalSize_ = logicalSize_;
            setContentSize(32, 32);
        } else {
            setContentSize(normalSize_.width(), normalSize_.height());
        }
        update();
        return;
    }

    // Esc: graceful quit — works in BOTH edit and live mode.
    if (e->key() == Qt::Key_Escape) {
        saveConfig();
        QCoreApplication::quit();
        return;
    }

    if (!editMode_)
        return QMainWindow::keyPressEvent(e);

    // Arrow keys: nudge panel margins (edit mode only).
    constexpr int kStep = 10;
    constexpr int kBigStep = 50;
    const int step = (e->modifiers() & Qt::ShiftModifier) ? kBigStep : kStep;

    int dx = 0, dy = 0;
    switch (e->key()) {
    case Qt::Key_Left:  dx = -step; break;
    case Qt::Key_Right: dx =  step; break;
    case Qt::Key_Up:    dy = -step; break;
    case Qt::Key_Down:  dy =  step; break;
    default:
        return QMainWindow::keyPressEvent(e);
    }

    if (dx != 0 || dy != 0)
        nudgeMargins(dx, dy);
}

void Panel::nudgeMargins(int dx, int dy)
{
    QMargins m = margins_;
    switch (corner_) {
    case Corner::TopLeft:
        m.setLeft(clampMargin(m.left() + dx));
        m.setTop(clampMargin(m.top() + dy));
        break;
    case Corner::TopRight:
        m.setRight(clampMargin(m.right() - dx));
        m.setTop(clampMargin(m.top() + dy));
        break;
    case Corner::BottomLeft:
        m.setLeft(clampMargin(m.left() + dx));
        m.setBottom(clampMargin(m.bottom() - dy));
        break;
    case Corner::BottomRight:
        m.setRight(clampMargin(m.right() - dx));
        m.setBottom(clampMargin(m.bottom() - dy));
        break;
    }
    margins_ = m;

    QWindow *native = windowHandle();
    if (native) {
        LayerShellQt::Window *layer = LayerShellQt::Window::get(native);
        if (layer)
            layer->setMargins(margins_);
    }
    update();
    // Persist immediately so a SIGTERM / crash doesn't lose position.
    // Saves only happen in edit mode (this method isn't called
    // otherwise), so live mode never pays this cost.
    saveConfig();
}

void Panel::setMargins(QMargins m, bool persist)
{
    m.setLeft(clampMargin(m.left()));
    m.setTop(clampMargin(m.top()));
    m.setRight(clampMargin(m.right()));
    m.setBottom(clampMargin(m.bottom()));
    if (m == margins_)
        return;
    margins_ = m;

    QWindow *native = windowHandle();
    if (native) {
        LayerShellQt::Window *layer = LayerShellQt::Window::get(native);
        if (layer)
            layer->setMargins(margins_);
    }
    update();
    if (persist)
        saveConfig();
}

void Panel::paintMinimized(QPainter &p)
{
    // Small 32×32 rounded square with the panel's first letter.
    const QColor bg(40, 40, 50, 100);
    const QColor fg(160, 180, 220, 120);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(0, 0, 32, 32, 6, 6);
    p.setPen(QPen(QColor(120, 180, 255, 60), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(0.5, 0.5, 31, 31, 6, 6);
    // First letter of the settings key as identifier.
    const QString letter = key_.isEmpty() ? QStringLiteral("?")
                                          : key_.left(1).toUpper();
    p.setPen(fg);
    p.setFont(QFont(QStringLiteral("Work Sans"), 12, QFont::Bold));
    p.drawText(QRectF(0, 0, 32, 32), Qt::AlignCenter, letter);
}

void Panel::wheelEvent(QWheelEvent *e)
{
    if (!editMode_)
        return;
    const double delta = (e->angleDelta().y() > 0) ? kScaleStep : -kScaleStep;
    scale_ = std::clamp(scale_ + delta, kMinScale, kMaxScale);
    // Re-sync the window size to the new zoom (keeps the logical
    // content size, rescales the actual window).
    if (logicalSize_.isValid())
        setContentSize(logicalSize_.width(), logicalSize_.height());
    update();
    saveConfig();
}

// -- v0.3 visual helpers (mirrors mhw-overlay-concept.html tokens) --

QColor Panel::accentColor(Accent a) const
{
    switch (a) {
    case Accent::Player:  return QColor(167, 79, 255);  // --accent-purple
    case Accent::Monster: return QColor(255, 112, 67);  // --enrage orange
    case Accent::Damage:  return QColor(80, 197, 183);  // --accent-teal
    }
    return QColor(120, 180, 255);
}

void Panel::drawV03Chrome(QPainter &p, Accent accent) const
{
    // HTML tokens:
    //   --bg-panel #16181a + 1px --border #2a2d2f + 2px radius
    //   2px left accent stripe (orange for monster, purple for player, teal for damage)
    //   1px top gloss gradient (transparent → white-6% → transparent)
    const QRectF r(rect());
    constexpr double kRadius = 2.0;
    const QColor accentCol = accentColor(accent);

    // Panel body — #16181a base + a darker bottom edge for depth
    p.setPen(Qt::NoPen);
    // Deep semi-transparent background. No blur simulation: the panel is a
    // stable dark tint over the game scene, while its data rows carry the
    // coloured progress information.
    p.setBrush(QColor(12, 14, 16, 170));           // #0c0e10 @ ~67%
    p.drawRoundedRect(r, kRadius, kRadius);

    // Top gloss line — REMOVED 2026-07-27 (added subtle smudge on top edge
    // that read as blur rather than crisp glass; the panel body's ~28%
    // alpha now gives all the translucency we need.)

    // Left accent stripe (4px wide, vertically centered with margin).
    // 2026-07-27: bumped from 2px → 4px so it reads as a deliberate
    // accent on the now-glassy panel rather than disappearing.
    const double stripeTop    = r.top() + 8;
    const double stripeBottom = r.bottom() - 8;
    p.setBrush(accentCol);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(r.left(), stripeTop, 4.0, stripeBottom - stripeTop),
                      1.0, 1.0);

    // 1px border (HTML: --border #2a2d2f)
    p.setBrush(Qt::NoBrush);
    p.setPen(QColor(42, 45, 47, 255));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
}

void Panel::drawBarV03(QPainter &p, const QRectF &rect, float pct,
                        const QColor &c, int radius) const
{
    // HTML: --c track #1d2022, fill linear-gradient(180deg, --c-hi, --c)
    const float clamped = std::clamp(pct, 0.0F, 1.0F);

    // Track
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(29, 32, 34, 255));           // --bg-cell
    p.drawRoundedRect(rect, radius, radius);

    // Fill (hi → c vertical gradient)
    if (clamped <= 0.0F)
        return;
    QRectF fill(rect.x(), rect.y(), rect.width() * clamped, rect.height());
    QLinearGradient grad(fill.topLeft(), fill.bottomLeft());
    grad.setColorAt(0.0, c.lighter(125));
    grad.setColorAt(1.0, c);
    p.setBrush(grad);
    p.drawRoundedRect(fill, radius, radius);

    // 1px top inner highlight
    QLinearGradient hi(rect.topLeft(), rect.bottomLeft());
    hi.setColorAt(0.0, QColor(255, 255, 255, 80));
    hi.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillRect(QRectF(fill.x(), fill.y(), fill.width(), 1), hi);
}

// v0.5 P0: explicit setters for the control console's Appearance fold.
// Both clamp into the same range the wheel editor and the constructor
// enforce; both apply via setContentSize / setWindowOpacity; both
// persist only when editMode_ is true (caller can flip persist=false
// to skip disk for ephemeral drives like the canvas preview repaint).
// Pattern: the same shape as the wheel zoom but exposed to caller code.

void Panel::setScale(qreal s, bool persist)
{
    const qreal clamped = std::clamp(s, kMinScale, kMaxScale);
    if (qFuzzyCompare(clamped + 1.0, scale_ + 1.0))
        return;                                // no-op
    scale_ = clamped;
    if (logicalSize_.isValid())
        setContentSize(logicalSize_.width(), logicalSize_.height());
    update();
    if (persist && editMode_)
        saveConfig();
}

void Panel::setOpacity(qreal a, bool persist)
{
    const qreal clamped = std::clamp(a, 0.1, 1.0);
    if (qFuzzyCompare(clamped, opacity_))
        return;
    opacity_ = clamped;
    // setWindowOpacity only works on a real composited window.
    // Console preview panels are WA_DontShowOnScreen; calling it
    // there spams "This plugin does not support setting window
    // opacity" on every slider tick. The canvas preview reads
    // opacity via PanelSource::opacity() + QPainter::setOpacity,
    // so no window-level opacity is needed for the preview path.
    if (!testAttribute(Qt::WA_DontShowOnScreen))
        setWindowOpacity(opacity_);
    update();
    if (persist && editMode_)
        saveConfig();
}

void Panel::resetToDefaults()
{
    sectionMask_  = 0xFFFFFFFFu;
    scale_        = 1.0;
    opacity_      = 0.85;
    margins_      = defaultMarginsFor(corner_);
    if (logicalSize_.isValid())
        setContentSize(logicalSize_.width(), logicalSize_.height());
    if (!testAttribute(Qt::WA_DontShowOnScreen))
        setWindowOpacity(opacity_);
    update();
    saveConfig();
}
