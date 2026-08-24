#pragma once

#include <QColor>
#include <QMainWindow>
#include <QMargins>
#include <QRectF>
#include <QSettings>
#include <QSize>
#include <QWidget>
#include <cstdint>


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

    // Section visibility: per-panel bitmask of independently toggleable
    // blocks (see ui/panel_sections.h). Default = all sections on so
    // live mode is unaffected. The control panel flips individual bits.
    void setSectionMask(uint32_t mask) { sectionMask_ = mask; update(); }
    [[nodiscard]] uint32_t sectionMask() const { return sectionMask_; }

    // Master visibility: a separate flag from the section mask. When
    // false, the panel does not show its layer-shell surface at all
    // (no chrome, no title row, no empty content — truly unmounted).
    // This is independent of sectionMask_ on purpose: turning every
    // section off keeps the panel alive so the user can see where it
    // sits, but disabling the panel itself removes it entirely. The
    // control console's master toggle maps to this flag, not the
    // section mask.
    void setPanelEnabled(bool on);
    [[nodiscard]] bool panelEnabled() const { return panelEnabled_; }

    // Edit-mode demo data is built once on first paint after entering
    // edit mode, then reused — keeps paint() O(display).
    virtual void setupDemoData() {}
    bool demoPrimed() const { return demoPrimed_; }
    void markDemoPrimed() { demoPrimed_ = true; }
    void resetDemoPrimed() { demoPrimed_ = false; }

    void setVisible(bool visible);
    [[nodiscard]] QSize contentSize() const { return logicalSize_; }

    // Read-only geometry/state for the control console's preview canvas.
    // The console paints the panels at the position they will actually
    // occupy on screen: anchor corner + persisted margins, scaled by the
    // user's zoom. Keeping these as getters (not setters) means the
    // canvas is always a passive view, never a driver of state.
    [[nodiscard]] Corner corner() const { return corner_; }
    [[nodiscard]] QMargins margins() const { return margins_; }
    [[nodiscard]] double scale() const { return scale_; }
    [[nodiscard]] double opacity() const { return opacity_; }
    [[nodiscard]] int bgAlpha() const { return bgAlpha_; }

    // v0.5+: explicit setters for the control console's
    // Appearance fold. Both clamp into the same range the wheel
    // editor and the constructor enforce; both apply via
    // setContentSize / setWindowOpacity; both persist only when
    // editMode_ is true (caller can flip persist=false to skip
    // disk for ephemeral drives like the canvas preview repaint).
    void setScale(qreal s, bool persist = true);
    void setOpacity(qreal a, bool persist = true);
    void setBgAlpha(int a, bool persist = true);

    // v0.5 position editing: set margins directly (clamped to [0,8000]
    // per side, matching clampMargin in panel.cpp). persist=true writes
    // to QSettings immediately (used by the live overlay's edit mode);
    // the console passes persist=false and writes on exit instead.
    void setMargins(QMargins m, bool persist = true);

    // Reset mask, scale, opacity, and margins to factory values
    // and re-sync the layer-shell surface. Always persists.
    void resetToDefaults();
    void saveAppearance();
    // Toggle the compositing effect for off-screen render() calls.
    // render() + enabled QGraphicsOpacityEffect produces a=0 (broken);
    // the console's renderPreview disables the effect, renders at full
    // alpha, and lets HudCanvas apply srcOpac itself.
    void setCompositingEnabled(bool on);

protected:
    virtual void paintPanel(QPainter &p) = 0;
    virtual void paintDemo(QPainter &) {}
    virtual bool hasContent() const { return true; }
    virtual void onSnapshot(const mhw::GameSnapshot &) { update(); }
    QWidget *canvas() { return this; }
    void setContentSize(int w, int h);

    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void showEvent(QShowEvent *e) override;

    // v0.3 visual helpers — match monster-overlay-concept.html tokens.
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

    // See setSectionMask(). Each subclass interprets the bits via the
    // matching namespace in panel_sections.h (PlayerSection / ...).
    uint32_t sectionMask_{0xFFFFFFFFu};
    bool panelEnabled_{true};

    QString key_;
    Corner corner_;
    QSize logicalSize_;
    QMargins margins_;
    double scale_{1.0};
    double opacity_{0.85};
    int bgAlpha_{170};   // panel background alpha (0-255), default ~67%
    bool editMode_{false};
    bool minimized_{false};
    QSize normalSize_;   // remembered full-layout size
};
