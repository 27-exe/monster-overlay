#pragma once

#include <QHash>
#include <QMargins>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QWidget>
#include <array>

#include "panel_source.h"
#include "screen_query.h"

// Paint the four overlay panels at the position they will actually occupy
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

    // 0 = player, 1 = monster, 2 = damage, 3 = pets.
    void setPanelPixmap(int index, const QPixmap &pixmap, bool enabled);
    // v0.10.1: a panel that does not exist for the selected game (Rise-only
    // pets while World is selected) is removed from the preview entirely —
    // not painted as a disabled placeholder and not hit-testable.
    void setPanelPresent(int index, bool present);
    [[nodiscard]] bool panelPresent(int index) const;
    void setSelectedPanel(int index);
    void bindPanel(int index, const PanelSource *src);

    // v0.8: name of the Wayland output whose geometry should drive
    // the preview's screen frame. Empty (the default) means "follow
    // QGuiApplication::primaryScreen()". The control console calls
    // this every time the user picks a different panel in the
    // SCREEN dropdown, so the preview rectangle always matches the
    // output the panels will actually land on.
    void setPreviewScreen(QString outputName);
    [[nodiscard]] QString previewScreen() const { return previewOutputName_; }

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
    // v0.7.5: lets the console's ZOOM label/buttons track Ctrl+wheel.
    void zoomChanged(qreal zoom);

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
        bool present{true};
        bool bound{false};
        const PanelSource *src{nullptr};
        QRectF lastTarget_;
    };

    // Compute the target QMargins for a drag/key delta (logical px).
    QMargins movedMargins(int index, int dxLogical, int dyLogical) const;

    QSize screenSize() const;
    QString cornerLabel(int index) const;
    QString screenLabel() const;
    // v0.8: like screenSize()/screenLabel() but honours the user's
    // selected Wayland output (previewOutputName_). Cheap (just a
    // hashmap lookup + cached QScreen data); safe to call from
    // paintEvent / drag / sizeHint.
    const screen_query::Result &previewScreenInfo() const;

    // v0.11.4: the detect()-based answer for the primary screen,
    // resolved at most once per topology change. Separate from
    // rebuildScreenCache() so a SCREEN click never pays for the
    // compositor subprocess detect() may run.
    const screen_query::Result &primaryScreenInfo() const;

    // v0.11.4: rebuild the per-output preview cache from the CURRENT
    // QGuiApplication::screens() list. Declared const because the
    // cache is per-widget state, not observable state: calling it is
    // always safe from the const query paths above (paintEvent /
    // drag / sizeHint) and the result depends only on the live screen
    // list, never on widget identity.
    //
    // Must be called whenever the screen topology can have changed:
    // setPreviewScreen() (the SCREEN dropdown is our screen picker),
    // the miss path in previewScreenInfo(), and the constructor's
    // screenAdded / screenRemoved hooks. Cheap by design — a plain
    // in-process QScreen walk, no I/O, no subprocess; screen_query::
    // detect() is NOT called here, see primaryScreenInfo().
    void rebuildScreenCache() const;

    std::array<Slot, mhw::kPanelCount> slots_{};
    int selected_{0};
    bool showSafeArea_{true};
    bool showGrid_{true};
    qreal zoom_{1.0};
    // v0.8: tracks the user's SCREEN selection so the preview's
    // "screen frame" rectangle and the per-panel drag clamps match
    // the output the panels are bound to. Empty = primary.
    QString previewOutputName_;

    // v0.11.4: per-instance screen cache. Was two function-level statics
    // inside previewScreenInfo() — built once per PROCESS, so a monitor
    // plugged in (or unplugged and re-plugged) later in the session was
    // invisible to the preview forever, and a removed output's stale
    // entry sent the lookup to an equally stale fallback.
    //
    // Now they are per-widget mutable members, invalidated via
    // screenCacheValid_ == false ("rebuild on next use") and repopulated
    // by rebuildScreenCache(). The Result VALUES live inside
    // screenCacheByName_ itself (no separate pointer table), so no
    // pointer ever points into a table that is about to be rebuilt.
    //
    // They are `mutable` because previewScreenInfo() is const — it is
    // called from paintEvent / drag / sizeHint — but has to be able to
    // repopulate the cache.
    //
    // screenFallbackValid_ is a SEPARATE clock on purpose: the name
    // table is a cheap in-process QScreen walk, but the fallback needs
    // screen_query::detect() (a subprocess on Wayland), so it must not
    // be re-resolved on every SCREEN click — only when a topology
    // change actually invalidates the primary-screen answer.
    mutable bool screenCacheValid_{false};
    mutable bool screenFallbackValid_{false};
    mutable QHash<QString, screen_query::Result> screenCacheByName_;
    mutable screen_query::Result screenCacheFallback_;

    // Drag state
    bool dragging_{false};
    int dragIndex_{-1};
    QPoint dragStartMouse_;
    QMargins dragStartMargins_;
};
