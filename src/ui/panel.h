#pragma once

#include <QColor>
#include <QMainWindow>
#include <QMargins>
#include <QRectF>
#include <QSettings>
#include <QWidget>

namespace mhw {
struct GameSnapshot;
}

// Which screen corner a panel is anchored to. Wayland layer-shell
// cannot place windows at arbitrary screen coordinates — position is
// defined solely by the anchor corner plus margins (offset from the
// anchored edges). Keyboard arrow keys nudge margins in edit mode.
enum class Corner {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

// Base class for all overlay panels. Handles layer-shell setup
// (anchor corner + margins), position persistence (via QSettings
// INI), and keyboard-driven positioning in --edit mode.
class Panel : public QMainWindow {
    Q_OBJECT
public:
    Panel(const QString &settingsKey, Corner corner, QWidget *parent = nullptr);

    // Schedule a repaint. Needed externally because the per-panel
    // update(Data) overloads shadow QWidget::update().
    void triggerUpdate() { update(); }

    void loadConfig();
    void saveConfig();
    void setEditMode(bool on);
    bool editMode() const { return editMode_; }

    // Edit-mode demo data is built once on first paint after entering
    // edit mode, then reused — keeps paint() O(display).
    virtual void setupDemoData() {}
    bool demoPrimed() const { return demoPrimed_; }
    void markDemoPrimed() { demoPrimed_ = true; }
    void resetDemoPrimed() { demoPrimed_ = false; }

    void setVisible(bool visible);

protected:
    virtual void paintPanel(QPainter &p) = 0;
    virtual void paintDemo(QPainter &) {}
    virtual bool hasContent() const { return true; }
    virtual void onSnapshot(const mhw::GameSnapshot &) { update(); }
    QWidget *canvas() { return this; }
    void setContentSize(int w, int h);
    double scale() const { return scale_; }
    double opacity() const { return opacity_; }

    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

    // v0.3 visual helpers — match mhw-overlay-concept.html tokens.
    enum class Accent { Player, Monster, Damage };
    void drawV03Chrome(QPainter &p, Accent accent) const;
    QColor accentColor(Accent a) const;
    // Status-coloured bar (HTML: --c-hi gradient over --c).
    void drawBarV03(QPainter &p, const QRectF &rect, float pct,
                    const QColor &c, int radius = 3) const;

    // Minimized: show a small colored block with the panel's
    // first letter instead of the full layout. Toggle with Space.
    bool minimized() const { return minimized_; }

private:
    void applyGeometry();
    void setLayerKeyboardInteractivity(bool interactive);
    void nudgeMargins(int dx, int dy);
    void paintMinimized(QPainter &p);

    bool demoPrimed_{false};

    QString key_;
    Corner corner_;
    QSize logicalSize_;
    QMargins margins_;
    double scale_{1.0};
    double opacity_{0.85};
    bool editMode_{false};
    bool minimized_{false};
    QSize normalSize_;   // remembered full-layout size
};
