#pragma once

#include <QPixmap>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QWidget>
#include <array>

class PanelSource;

// Paint the three overlay panels at the position they will actually occupy
// on the user's screen. The control console calls HudCanvas::bindPanel()
// for each panel after construction; the canvas then queries the panel
// for its anchor corner, persisted margins, and zoom so the preview
// reflects the same coordinates the compositor will use at runtime.
//
// When a panel is disabled (master toggle off) its slot in the layout
// stays visible as a faint dashed placeholder so the user can still
// judge the overall composition.
class HudCanvas : public QWidget {
    Q_OBJECT
public:
    explicit HudCanvas(QWidget *parent = nullptr);

    // 0 = player, 1 = monster, 2 = damage.
    void setPanelPixmap(int index, const QPixmap &pixmap, bool enabled);
    void setSelectedPanel(int index);
    void bindPanel(int index, const PanelSource *src);

    // v0.5 P0: stagebar toggles. Defaults both true (matches the
    // v0.4 behaviour). Setting false skips the dashed safe-area
    // rect and the crosshair lines in paintEvent.
    void setShowSafeArea(bool on);
    void setShowGrid(bool on);
    bool showSafeArea() const { return showSafeArea_; }
    bool showGrid()      const { return showGrid_; }

    QSize sizeHint() const override { return QSize(820, 520); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

signals:
    // Emitted when the user clicks one of the three panel rects in
    // the canvas. The selected panel switches the inspector focus
    // (control_panel::selectPanel). Not emitted for clicks that miss
    // all slot rects (e.g. inside the screen frame but on empty
    // space); the caller is responsible for de-duplicating with
    // the rail nav click.
    void panelSelected(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    struct Slot {
        QPixmap pixmap;
        bool enabled{true};
        bool bound{false};
        const PanelSource *src{nullptr};
        // Cached at the end of each paintEvent so mousePressEvent
        // can hit-test. Zeroed at the start of every paint so
        // un-bound slots aren't hit-testable.
        QRectF lastTarget_;
    };

    QSize screenSize() const;
    QString cornerLabel(int index) const;
    QString screenLabel() const;

    std::array<Slot, 3> slots_{};
    int selected_{0};
    bool showSafeArea_{true};
    bool showGrid_{true};
};
