#pragma once

#include <QMainWindow>
#include <QSettings>
#include <QWidget>

namespace mhw {
struct GameSnapshot;
}

// Base class for all overlay panels. Handles layer-shell setup,
// position persistence (via QSettings INI), and the --edit drag
// interaction. Subclasses implement paintPanel() to draw their
// content into the given QPainter.
class Panel : public QMainWindow {
    Q_OBJECT
public:
    // `settingsKey` is the INI section name (e.g. "player", "monster", "dps").
    // `defaultPos` is used when no saved position exists.
    Panel(const QString &settingsKey, const QPoint &defaultPos, QWidget *parent = nullptr);

    // Read position/scale/opacity from QSettings and apply them.
    void loadConfig();

    // Write current position/scale/opacity to QSettings.
    void saveConfig();

    // Toggle edit mode: show border, accept mouse events.
    void setEditMode(bool on);
    bool editMode() const { return editMode_; }

    void setVisible(bool visible);

protected:
    // Draw panel content. Called on every refresh and on resize.
    // `p` is a QPainter on the panel itself (the panel is a
    // top-level window, so it always has a valid backing store).
    virtual void paintPanel(QPainter &p) = 0;

    // Called when new data arrives from the game.
    // Default implementation just calls update() to trigger repaint.
    virtual void onSnapshot(const mhw::GameSnapshot &) { update(); }

    // The panel itself is the drawing surface. Kept as an accessor
    // so subclasses can call canvas()->update() etc.
    QWidget *canvas() { return this; }

    // Set the panel's logical content size (unscaled). The actual
    // window size is logical * scale_, and paintPanel draws in
    // logical coordinates (the painter is pre-scaled by scale_).
    void setContentSize(int w, int h);

    // Current scale factor (1.0 = 100%).
    double scale() const { return scale_; }

    // Current opacity (0..1).
    double opacity() const { return opacity_; }

    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    QString key_;
    QPoint defaultPos_;
    QSize logicalSize_;
    double scale_{1.0};
    double opacity_{0.85};
    bool editMode_{false};
    bool dragging_{false};
    QPoint dragStart_;
};