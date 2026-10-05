#pragma once

#include <QHash>
#include <QMargins>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>
#include <array>

#include "panel_sections.h"
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

    // ─── v0.13 split-mode preview ───────────────────────────────────────
    //
    // SINGLE MODE IS UNTOUCHED BY ALL OF THIS. The four `slots_` above are
    // exactly what a Single-mode launch renders, byte for byte, and nothing
    // below may read or write them when the mode is Single:
    //
    //   * subSlots_ is EMPTY while layoutMode_ == Single, and setLayoutMode()
    //     clears it on every switch into Single — never leaving a stale
    //     window behind for the paint loop to draw.
    //   * subSlots_ is only ever APPENDED to (setSubWindowSources replaces
    //     the whole vector), so no code path can write through it into the
    //     four slots' pixmaps, margins or selection state.
    //   * The paint loop's four-slot arm runs first and is guarded by the
    //     same `bound` check it always had; the sub-window arm that
    //     follows is a strictly additive block that a Single-mode launch
    //     never reaches, because it is gated on `!subSlots_.isEmpty()`.
    //
    // That is the whole pixel-identity argument: Single-mode preview
    // output cannot move because no Single-mode code path differs from
    // what is already committed, and a test pins the emptiness claim.

    // Which window topology the PREDICTED overlay is running in — the same
    // `--layout` value main.cpp resolves at startup. The console reads the
    // mode in force (its conf row, or what the apply button committed) and
    // pushes it here; the canvas then rebuilds its sub-window geometry.
    //
    // Single clears every sub-slot. Split keeps them until the next call.
    // Calling this with the mode already in force is a no-op, so the
    // console can call it on every mask change without churning geometry.
    void setLayoutMode(mhw::PlayerSection::PlayerLayoutMode mode);
    [[nodiscard]] mhw::PlayerSection::PlayerLayoutMode layoutMode() const
    {
        return layoutMode_;
    }

    // One split sub-window's binding: WHERE its geometry comes from, and
    // WHICH panel it was carved out of.
    //
    // `parentSlot` / `tableRow` are provenance, not a second index into
    // the canvas: they are what lets the preview draw the sub-window in
    // its parent panel's accent colour and label it with the row it came
    // from, which is how the user sees "this box is the HP strip that
    // left the monster panel" rather than "this box exists". The console
    // fills them from the derivation (see buildSubWindowSources in
    // control_panel.cpp), so they cannot disagree with the tables.
    //
    // `src` follows bindPanel()'s ownership contract exactly: the canvas
    // stores it without taking ownership and never deletes it.
    struct SubWindowBinding {
        const PanelSource *src{nullptr};
        int parentSlot{-1};   // 0 player / 1 monster / 2 damage / 3 pets
        int tableRow{-1};     // row index inside that parent's split table
    };

    // The geometry sources for the split's sub-windows, one per row that
    // resolved a live window (7 player + 5 monster + 2 damage = 14 max).
    //
    // REPLACES, never appends: the caller re-derives the whole set from
    // the three tables on every mask change, so a replace is the only
    // write that cannot leave a window behind whose parent is gone. A
    // row whose window did not resolve is simply ABSENT from the vector
    // — the index space is "position in this vector", not "table row",
    // which is why the caller may pass fewer than 14.
    void setSubWindowBindings(QVector<SubWindowBinding> bindings);

    // One sub-window's rendered pixmap, addressed by the same index the
    // matching binding was passed at. Unknown indices are ignored rather
    // than silently resizing the vector (a resize would make every later
    // index refer to a different window).
    void setSubWindowPixmap(int index, const QPixmap &pixmap, bool enabled);
    [[nodiscard]] int subWindowCount() const
    {
        return static_cast<int>(subSlots_.size());
    }

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

    // v0.13: one split sub-window's place in the preview.
    //
    // Kept OUT of Slot on purpose. Slot is four elements addressed by a
    // fixed panel index, sized mhw::kPanelCount, and the four-slot paint
    // arm reads it as such; grafting a mode-dependent "sometimes there
    // are sub-windows" flag into it would put a branch in the one loop
    // the Single-mode pixel-identity guarantee is built on. SubSlot is a
    // separate type in a separate vector, so the single mode's arm cannot
    // see it and the split's arm cannot see the slots.
    struct SubSlot {
        const PanelSource *src{nullptr};
        QPixmap pixmap;
        bool enabled{true};
        // Which of the four base panels this window was carved out of,
        // so the preview can paint it in that panel's accent colour and
        // prove the derivation (see buildSubWindowSources' doc comment).
        int parentSlot{-1};
        // The row index inside that parent's split table, kept for
        // diagnostics and for the title the canvas draws above the box.
        int tableRow{-1};
    };

    // Everything one sub-window box needs, gathered in one pass so the
    // painter stays a pure function of its arguments (and therefore
    // testable without a live paint).
    struct SubWindowPaintInfo {
        QRectF rect;          // the box, in canvas pixels
        QPointF anchor;       // where the connection line starts
        int parentSlot{-1};   // whose accent colour and corner it uses
        QString title;        // the section label the row claims
    };

    // Paint the sub-window boxes for one parent's corner cluster. Called
    // once per parent slot that has live sub-windows; a parent with none
    // is skipped entirely so its main panel keeps the same layout.
    void paintSubWindowCluster(const QRectF &parentTarget, int parentSlot,
                               const Corner parentCorner, QPainter &p) const;
    // One box plus its title and its connection line.
    void paintSubWindowBox(const SubWindowPaintInfo &info,
                           const QRectF &parentTarget,
                           const Corner parentCorner, QPainter &p) const;

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
    // v0.13: the split's sub-windows. EMPTY in Single mode — that
    // emptiness is the whole pixel-identity guarantee (see the comment
    // on the public API above). Sized and filled by setSubWindowSources(),
    // cleared by setLayoutMode(Single).
    QVector<SubSlot> subSlots_;
    // Which window topology the preview is describing. NOT the same
    // question as "does the preview have sub-windows": the mode is the
    // user's choice, the vector is what happened to be derivable from
    // the tables. A Split console whose windows all failed to resolve
    // would still be in Split mode with an empty vector.
    mhw::PlayerSection::PlayerLayoutMode layoutMode_{
        mhw::PlayerSection::PlayerLayoutMode::Single};
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
