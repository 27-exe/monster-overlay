#pragma once

#include <QPixmap>
#include <QRect>
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

    QSize sizeHint() const override { return QSize(820, 520); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Slot {
        QPixmap pixmap;
        bool enabled{true};
        bool bound{false};
        const PanelSource *src{nullptr};
    };

    QSize screenSize() const;
    QString cornerLabel(int index) const;
    QString screenLabel() const;

    std::array<Slot, 3> slots_{};
    int selected_{0};
};
