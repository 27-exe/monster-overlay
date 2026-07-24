#include "panel.h"

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>
#include <QWindow>

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

} // namespace

Panel::Panel(const QString &settingsKey, const QPoint &defaultPos, QWidget *parent)
    : QMainWindow(parent)
    , key_(settingsKey)
    , defaultPos_(defaultPos)
{
    setObjectName(QStringLiteral("mhw-panel-%1").arg(settingsKey));
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setMouseTracking(true);

    // Safety net: give the panel a sane default size immediately.
    // Subclasses normally call canvas()->setFixedSize(...) once they
    // have data, but before the first data arrives a top-level
    // frameless window would otherwise expand to fill the screen.
    setFixedSize(320, 120);

    // Layer-shell: overlay on top of everything. Force native window
    // creation via winId(), then configure the layer on the QWindow.
    QWindow *native = windowHandle();
    if (!native) {
        (void)winId();
        native = windowHandle();
    }
    if (native) {
        LayerShellQt::Window *layer = LayerShellQt::Window::get(native);
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setExclusiveZone(-1);
        layer->setScope(QStringLiteral("mhw-linux-overlay"));
        layer->setActivateOnShow(false);
    }

    loadConfig();
}

void Panel::loadConfig()
{
    settings().beginGroup(key_);
    const int x = settings().value(QStringLiteral("x"), defaultPos_.x()).toInt();
    const int y = settings().value(QStringLiteral("y"), defaultPos_.y()).toInt();
    scale_ = settings().value(QStringLiteral("scale"), 1.0).toDouble();
    opacity_ = settings().value(QStringLiteral("opacity"), 0.85).toDouble();
    const bool vis = settings().value(QStringLiteral("visible"), true).toBool();
    settings().endGroup();

    scale_ = std::clamp(scale_, kMinScale, kMaxScale);
    opacity_ = std::clamp(opacity_, 0.1, 1.0);

    move(x, y);
    setVisible(vis);
    setWindowOpacity(opacity_);
}

void Panel::saveConfig()
{
    settings().beginGroup(key_);
    settings().setValue(QStringLiteral("x"), pos().x());
    settings().setValue(QStringLiteral("y"), pos().y());
    settings().setValue(QStringLiteral("scale"), scale_);
    settings().setValue(QStringLiteral("opacity"), opacity_);
    settings().setValue(QStringLiteral("visible"), isVisible());
    settings().endGroup();
    settings().sync();
}

void Panel::setEditMode(bool on)
{
    editMode_ = on;
    setAttribute(Qt::WA_TransparentForMouseEvents, !on);
    setCursor(on ? Qt::SizeAllCursor : Qt::ArrowCursor);
    update();
}

void Panel::setVisible(bool visible)
{
    QMainWindow::setVisible(visible);
}

void Panel::setContentSize(int w, int h)
{
    logicalSize_ = QSize(w, h);
    // Actual window size tracks the zoom so painted content is
    // never clipped at scale != 1.0.
    const int aw = static_cast<int>(std::lround(w * scale_));
    const int ah = static_cast<int>(std::lround(h * scale_));
    setFixedSize(aw, ah);
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

    paintPanel(p);
}

void Panel::mousePressEvent(QMouseEvent *e)
{
    if (editMode_ && e->button() == Qt::LeftButton) {
        dragging_ = true;
        dragStart_ = e->globalPosition().toPoint() - pos();
    }
}

void Panel::mouseMoveEvent(QMouseEvent *e)
{
    if (dragging_ && editMode_)
        move(e->globalPosition().toPoint() - dragStart_);
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
}