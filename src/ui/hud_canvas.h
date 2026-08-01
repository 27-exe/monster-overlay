#pragma once

#include <QMargins>
#include <QPixmap>
#include <QPoint>
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
// v0.5: supports drag-to-move and arrow-key nudging of the selected
// panel, plus Ctrl+wheel zoom with QScrollArea scrollbars.
class HudCanvas : public QWidget {
    Q_OBJECT
public:
    explicit HudCanvas(QWidget *parent = nullptr);

    // 0 = player, 1 = monster, 2 = damage.
    void setPanelPixmap(int index, const QPixmap &pixmap, bool enabled);
    void setSelectedPanel(int index);
    void bindPanel(int index, const PanelSource *src);

    void setShowSafeArea(bool on);
    void setShowGrid(bool on);
    bool showSafeArea() const { return showSafeArea_; }
    bool showGrid()      const { return showGrid_; }

    // v0.5 zoom: 1.0 = fit-to-widget (default). >1 enlarges the
    // screen frame beyond the viewport; the parent QScrollArea
    // provides scrollbars. Ctrl+wheel adjusts in 0.1 steps.
    void setZoom(qreal z);
    qreal zoom() const { return zoom_; }

    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return zoom_ <= 1.0; }
    int heightForWidth(int width) const override;

signals:
    void panelSelected(int index);
    // Emitted during drag / keyboard nudge. `margins` is the
    // target QMargins in logical pixels, already clamped. The
    // console calls Panel::setMargins(margins, false) + rebuild.
    void panelMoved(int index, QMargins margins);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Slot {
        QPixmap pixmap;
        bool enabled{true};
        bool bound{false};
        const PanelSource *src{nullptr};
        QRectF lastTarget_;
    };

    // Compute the target QMargins for a drag/key delta (logical px).
    QMargins movedMargins(int index, int dxLogical, int dyLogical) const;

    QSize screenSize() const;
    QString cornerLabel(int index) const;
    QString screenLabel() const;

    std::array<Slot, 3> slots_{};
    int selected_{0};
    bool showSafeArea_{true};
    bool showGrid_{true};
    qreal zoom_{1.0};

    // Drag state
    bool dragging_{false};
    int dragIndex_{-1};
    QPoint dragStartMouse_;
    QMargins dragStartMargins_;
};
