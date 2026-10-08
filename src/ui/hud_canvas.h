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

    // ─── v0.14: the split canvas the user actually asked for ───────────
    //
    // Two halves of the same complaint — 「分块模式的控制台几乎不可用.我要的是
    // 分块模式下不显示那几个整块,自由拖动各个分块.各个分块的初始位置互不重叠
    // 的分块摆放」 — and each is a canvas contract, not a console wiring
    // change:
    //
    //   1. THE MOTHER PANELS ARE NOT DRAWN in Split. Not a lighter box,
    //      not a placeholder, nothing: the four `slots_` paint arm is
    //      skipped entirely, so the screen frame the sub-windows occupy
    //      is the whole canvas rather than 70% of it. A mother panel
    //      still lives in the rail and is still selectable there — the
    //      canvas is just no longer the place you look for it.
    //
    //   2. EVERY SUB-WINDOW IS FREELY DRAGGABLE, and lands on its own
    //      margins. This reuses the machinery the four slots already
    //      use: a press inside a sub-window box starts a drag on
    //      subDragIndex_ (NOT dragIndex_ — a drag is one window at a
    //      time, and mixing the two index spaces is how a mother panel
    //      would move when the user dragged a sub-window), the drag
    //      emits subWindowMoved(index, margins), and the console calls
    //      that OWN window's Panel::setMargins(). No slot arithmetic is
    //      involved, so the drag cannot touch the four base panels.
    //
    //   3. THE INITIAL PLACEMENT IS AN OVERLAP-FREE WALL, computed once
    //      per setSubWindowBindings() and rebuilt on every replacement.
    //      It is anchored by PARENT CORNER and grouped by parent, so a
    //      window that left the monster panel draws in the monster's
    //      accent beside the monster's origin — the same "why is this
    //      here" question the real overlay's cascade answers (see
    //      defaultMarginsForSubWindow in panel.cpp). The shared row/col
    //      walk in subWindowGridRect() is what guarantees
    //      non-overlap, and the test asserts the rects rather than the
    //      pixels because the rects are the contract and the pixels are
    //      their consequence.
    //
    // SINGLE MODE IS UNTOUCHED BY ALL THREE. setLayoutMode(Single) clears
    // subSlots_ AND resets the drag state, and the paint arm for the
    // sub-windows is gated on subSlots_ being non-empty, exactly as
    // before. The four-slot arm and every mouse path that reaches it are
    // unchanged: mousePressEvent only reaches the sub-window hit test when
    // dragIndex_ selection found nothing in the four slots first.

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

    // ─── v0.14: the layout the last paint computed ─────────────────────
    //
    // The sub-window boxes in CANVAS pixels, by the index the matching
    // binding was passed at. This exists so a test can assert the
    // placement contract — "any two sub-window boxes do not overlap" —
    // against the rects that are actually painted, rather than against
    // pixels it would have to reverse-engineer out of a screenshot.
    //
    // Const and populated by paintEvent (which is the only place the
    // frame + fit factor are both known). Read it after a paint.
    [[nodiscard]] QVector<QRectF> subWindowRects() const
    {
        return subRects_;
    }
    // Same for the drag highlight. -1 when nothing is selected.
    [[nodiscard]] int selectedSubWindow() const { return subSelected_; }

    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return zoom_ <= 1.0; }
    int heightForWidth(int width) const override;

    // ─── v0.14 placement: the overlap-free wall ─────────────────────────
    //
    // THE RULE AND WHY IT IS THIS ONE. The previous layout stacked one
    // parent's sub-windows into a vertical column, so a user's own
    // screenshot showed 「左侧堆着一团框」: the four player boxes ran
    // from the mother panel's top edge down past the bottom of the
    // canvas, each box overlapping the next in the column's scroll and
    // the whole cluster losing its "which block left which panel" claim.
    //
    // The replacement is a WALK over a per-parent grid whose row and
    // column advance together, which is what makes non-overlap a
    // property of the arithmetic rather than something asserted
    // afterwards: box N is placed at
    //
    //     row = N % kSubWallColumns      column = N / kSubWallColumns
    //
    // so two different indices cannot share a cell unless the column
    // count is zero or one. With kSubWallColumns = 3 that is a lattice
    // of |−1, 0, +1| cells whose extent is a function of how many
    // windows this parent contributed, nothing else.
    //
    // Dragged positions win over the wall: a window the user has moved
    // keeps its own box, and only the never-dragged siblings are laid
    // out. That is what makes the wall a FIRST-RUN placement (the same
    // contract defaultMarginsForSubWindow() makes for the live overlay)
    // rather than a constraint the user has to fight.
    //
    // ─── the one walk, in LOGICAL pixels ───────────────────────────────
    //
    // Margins are the unit the panel persists in, so the walk is done
    // there and mapped to canvas pixels ONCE per paint at the same fit
    // factor the four-slot arm uses (layoutSubWindows). Doing the walk
    // in canvas pixels instead would make a box's position a function
    // of the widget's current size, which is exactly the "a layout that
    // is right at one resolution and wrong at every other" failure the
    // live overlay's cascade comment warns against — and a drag delta
    // measured in logical px could not be applied to it without a
    // second conversion.
    //
    // `corner` is the window's OWN anchor, which is what decides which
    // region of the screen its wall starts in: a TopLeft cluster grows
    // down-and-right, a TopRight one grows down-and-left. Two frames
    // that end up on the same corner share ONE wall and therefore one
    // ordinal counter — see subWindowWall().
    //
    // Returns the cell for this ordinal, in LOGICAL screen coordinates.
    //
    // STATIC, on purpose. The console's split-preview host needs the
    // same rule to write the cells into the windows' own margins, and
    // that host is constructed before (and independently of) any
    // canvas. A free function or a second copy of the arithmetic would
    // let the two drift, and the drift would show up as boxes painted
    // somewhere other than where the surface lands — the exact lie a
    // preview exists to prevent. So there is one rule and both callers
    // reach it.
    // v0.14: `cell` is the EXTENT this cell must hold, and the caller
    // supplies it from the window actually going there. A cell is not the
    // box — the box is drawn at the window's own content size — it is the
    // lattice a box is assigned to, so it must be at least as large as the
    // box. Passing one constant for every window is what made the wall
    // overlap: 11 windows, 12 overlapping pairs, and every overlapped
    // window unreachable by a click (sub_window_wall_tests.cpp).
    //
    // A zero or negative extent falls back to kSubWallCellW/H, so a caller
    // that has not measured yet still gets a sane cell rather than a
    // degenerate one that would stack every box on the origin.
    static QRectF subWindowGridRect(Corner corner, int ordinal,
                                    const QSize &logical,
                                    const QSize &cell);

    // Every bound sub-window's cell, by the index its binding was passed
    // at, in LOGICAL pixels. This is what the test asserts against: the
    // pairwise-disjoint property of THIS vector is the placement
    // contract, and the canvas pixels painted are its consequence.
    QVector<QRectF> subWindowWall(const QSize &logical) const;
    // The fallback grid pitch, in LOGICAL px, used only when a caller
    // supplies no measured extent. The real pitch is the window's own size
    // (see subWindowGridRect's cell argument); these constants are the
    // seed so the first window of a corner still has somewhere to go.
    static constexpr int kSubWallColumns = 3;
    // v0.14: THE FALLBACK CELL, not the cell.
    //
    // These used to BE the cell, and that was the defect behind "the
    // sub-windows cannot be dragged past the midline": a 260x128 cell
    // cannot hold a 560x183 window, so consecutive cells claimed
    // rectangles their neighbours' window bodies already occupied —
    // twelve overlapping pairs out of eleven windows, measured in
    // sub_window_wall_tests.cpp. mousePressEvent walks the boxes in
    // REVERSE, so a press in a shared area selected the last box and the
    // window underneath could not be clicked at all. It was not a clamp
    // and it was not the midline; the window was never being clicked.
    //
    // A cell must now be SIZED for the window it holds, which means the
    // extent is an argument. These constants remain only as the seed for
    // the first window in a corner, so a group whose windows report no
    // size yet still lays out somewhere sane instead of dividing by zero.
    static constexpr int kSubWallCellW = 260;
    static constexpr int kSubWallCellH = 128;
    // The gap the wall leaves between two cells, in logical px. Scaled
    // with the cell rather than fixed, so a wide window does not end up
    // flush against its neighbour's cell.
    static constexpr int kSubWallGap = 8;
    // The gap between a corner's default margin and its wall's first
    // cell, in LOGICAL px. Non-zero so the wall never sits on top of
    // where the mother panel's own box WOULD be.
    static constexpr int kSubWallOffset = 12;
    static constexpr int kSubWallOrigin = 20;   // mirrors defaultMarginsFor
    // The title strip above each box, so the user can name the window.
    static constexpr int kSubTitleH = 12;
    // Every sub-window box is drawn at this fraction of the size its
    // content reserves, for the same reason a parent's pixmap is drawn
    // at the window's own scale: a 380px damage bar drawn 1:1 beside
    // the 378px damage panel it came from would say nothing about why
    // it became a separate window.
    static constexpr qreal kSubPreviewScale = 0.5;


signals:
    void panelSelected(int index);
    // Emitted during drag / keyboard nudge. `margins` is the
    // target QMargins in logical pixels, already clamped. The
    // console calls Panel::setMargins(margins, false) + rebuild.
    void panelMoved(int index, QMargins margins);
    // v0.14: the same signal for a SPLIT SUB-WINDOW. `index` is the
    // sub-window's position in the bindings vector, NOT a slot index,
    // and `margins` is that window's own QMargins — the console routes
    // it to that window's Panel, never to one of the four base panels.
    // Emitted only while layoutMode() == Split.
    void subWindowMoved(int index, QMargins margins);
    // v0.14: emitted on a press that landed inside a sub-window box.
    // Its `index` is the SUB-WINDOW's index, so the console's status
    // line and inspector can name the window being dragged without
    // pretending it is a base panel.
    void subWindowSelected(int index);
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
        // v0.14: this window's box, in CANVAS pixels, from the last
        // layout pass. Overwritten on every paint — it is a cache for
        // hit-testing and for the test that asserts non-overlap, never
        // a source of truth. Recomputed from the window's own corner +
        // margins + content size on every paint.
        QRectF lastRect_;
    };

    // Everything one sub-window box needs, gathered in one pass so the
    // painter stays a pure function of its arguments (and therefore
    // testable without a live paint).
    struct SubWindowPaintInfo {
        QRectF rect;          // the box, in canvas pixels
        int parentSlot{-1};   // whose accent colour and corner it uses
        QString title;        // the section label the row claims
        int index{-1};        // the SUB-WINDOW's own index, not a slot
        bool selected{false}; // the drag / click highlight
    };

    // Paint one sub-window box: its frame, its title, and the drag
    // highlight when it is the one being moved or nudged.
    void paintSubWindowBox(const SubWindowPaintInfo &info,
                           QPainter &p) const;

    // The one layout pass, called from paintEvent. Fills subRects_ and
    // returns the boxes the painter draws, so the painter stays a pure
    // function of what this returns. `fit` and `frameTopLeft` are the
    // logical→canvas mapping the four-slot arm established.
    QVector<SubWindowPaintInfo> layoutSubWindows(
        qreal fit, const QPointF &frameTopLeft, const QSize &logical);

    // ─── drag state (v0.14) ────────────────────────────────────────────
    //
    // subDragIndex_ is deliberately a SEPARATE member from dragIndex_.
    // A drag is one window, identified by which index space it came
    // from: a press inside a sub-window box never sets dragIndex_, so
    // movedMargins() — which reads slots_[dragIndex_] — cannot be
    // reached with a sub-window's index and silently drag a base panel
    // by that much. Two members, two index spaces, no shared state.
    bool subDragging_{false};
    int subDragIndex_{-1};
    QPoint subDragStartMouse_;
    QMargins subDragStartMargins_;
    // Which sub-window the highlight belongs to. -1 means none.
    int subSelected_{-1};
    // The boxes from the LAST layout pass, in canvas pixels, by
    // sub-window index. Sised to match subSlots_.
    QVector<QRectF> subRects_;

    // Compute the target QMargins for a drag/key delta (logical px).
    QMargins movedMargins(int index, int dxLogical, int dyLogical) const;

    // The screen frame and the logical→canvas fit factor, in one call.
    // paintEvent and mouseMoveEvent share it so the drag's px→logical
    // conversion uses the exact mapping the box was painted with.
    void screenFrame(QRectF &frame, qreal &fit, QSize &logical) const;

    // v0.14: the sub-window twin of movedMargins(). A SEPARATE function
    // rather than an overload on the same name, because the two take
    // different index spaces and the overload set would make the wrong
    // one silently callable (an int is an int). Same per-corner sign
    // convention, same screen-relative clamp — see the .cpp.
    QMargins movedSubMargins(int index, int dxLogical, int dyLogical) const;

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
