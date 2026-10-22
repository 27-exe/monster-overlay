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

    // ─── v1.0.0 overview observability ─────────────────────────────────
    //
    // The overview draws ONE display at a time, chosen from a dropdown, and
    // the frame it draws is sized from that display's aspect ratio. Without
    // a reader for "which display is that", the only way to check the
    // switch worked is to look at pixels — and a frame that is tall for
    // BOTH displays looks like a working portrait mode until you compare
    // the two. That is exactly the bug this exposes: both came back tall,
    // because screenFrame()'s avail was dominated by the widget's own
    // heightForWidth() rather than by physAR.
    [[nodiscard]] QString previewOutput() const { return previewOutputName_; }
    [[nodiscard]] QSize previewPhysicalSize() const
    { return previewScreenInfo().physical; }
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
    // resolved a live window: 6 player + 2 monster + 3 damage = 11. Spelled
    // as arithmetic rather than a total, because a total here did not move
    // when the monster regrouping took five rows to two and the DPS roster
    // became a third damage row.
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

    // How many of the sub-windows above belong to ONE parent panel.
    //
    // The .win-panel footer's denominator. The prototype writes
    // `onCount/def.sections.length` — a count over THAT WINDOW's own
    // sections — and the Qt translation is a count over the parent's
    // rows, because a split-table row IS a window here. The per-parent
    // count is what makes "怪物 · 1/2" say the truth; subWindowCount()
    // would say "1/11" and call a monster window one of eleven, which
    // is the preview misrepresenting the thing it previews.
    [[nodiscard]] int subWindowCountForParent(int parentSlot) const;

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

    // ─── v1.0.0: the single-display overview ───────────────────────────
    //
    // 原型语义（console-single-display.html）：总览一次只画一块显示器上的
    // 所有窗口（跨面板），且是只读视图 —— 「总览为只读视图：一次只绘制
    // 一块显示器，点击窗口在下方查看详情」。
    //
    // 它是纯 VIEW 状态，与 previewScreen（也是视图状态）分开保存：
    // overviewScreen_ 记「总览在看哪块屏」，previewOutputName_ 仍然是
    // 「canvas 当前在预览哪块屏」。进入总览时后者被设为前者（否则画面
    // 还是旧屏的框），退出时恢复，这样 selectPanel() 在总览期间被跳过
    // 也不会把画面抢走。
    //
    // overviewMode_ 默认 false，且默认绘制路径一个分支都不加：绘制闸写
    // 成 `subSlots_.isEmpty() || overviewMode_` 的短路形式，
    // overviewMode_ 恒 false 时与改写前逐位等价 —— 这是
    // console_split_sync_tests / damage_rows_window_tests 的
    // canvas->grab() 像素断言能继续通过的根据。
    //
    // `screen` 为空表示「跟随主屏」；为空时用 primaryOutputName() 落一个
    // 真实输出名，让窗口归属与总览屏用同一个字符串空间比较。
    void setOverview(bool on, const QString &screen = QString());
    [[nodiscard]] bool overviewMode() const { return overviewMode_; }
    [[nodiscard]] QString overviewScreen() const { return overviewScreen_; }

    // 「这个窗口配置上属于哪块屏」—— 原型 resolveScreen() 的规则：
    // 窗口自己的 outputName → 所属母面板的 outputName → 主屏。
    //
    // v1.1.0: this comment used to say boundOutputName() was deliberately
    // NOT used here because that chain "falls back to previewOutputName_
    // (the canvas's preview screen, i.e. view state)". That was true when
    // it was written and is true no longer: v1.0.1 (#8) moved
    // boundOutputName()'s third step to the PRIMARY, so the two functions
    // now walk the same own → parent → primary chain and cannot disagree.
    // The split is kept anyway — the reason is now the first two steps and
    // the extra fallback, not step three:
    //   * boundOutputName() additionally consults the source's
    //     previewFallbackOutput(), which the console pushes for a split
    //     window (the parent's output, or the primary's name), so it is the
    //     right answer for MEASURING and dragging;
    //   * windowOutputName() answers the配置事实 for the overview filter,
    //     where the pushed preview fallback must not participate.
    // Do not "simplify" one into the other on the strength of the old
    // comment: they agree on step three, not on step two.
    //
    // `subIndex` 是 subSlots_ 的下标；负数（默认）问母面板 0..3。
    // 返回空串表示「跟随主屏」。
    [[nodiscard]] QString windowOutputName(int panelIndex,
                                           int subIndex = -1) const;

    void setShowSafeArea(bool on);
    void setShowGrid(bool on);
    bool showSafeArea() const { return showSafeArea_; }
    bool showGrid()      const { return showGrid_; }

    // v1.0.0: the prototype's `board.editing` state — one boolean that
    // switches the whole board into its edit-face:
    //
    //   * the 80-logical-px board grid fades in (HTML's .board.editing
    //     .board-grid opacity 0→1),
    //   * the board's own border turns clay (--clay #D97757) instead of
    //     the resting --line2 #4C4C45,
    //   * every .win card's border becomes DASHED
    //     (.board.editing .win { border-style:dashed }).
    //
    // Deliberately NOT folded into showGrid_: that flag is the user's
    // long-standing "grid on/off" preference for the centre crosshair
    // and is toggled from the stagebar, while `editing` is a transient
    // mode the prototype drives from the toolbar's 编辑布局 button.
    // Reusing one flag for both would make the prototype's editing
    // state silently overwrite the user's saved preference.
    //
    // Default false: every non-editing paint keeps the resting face, so
    // the pixel-identity argument for the default path is unchanged.
    void setEditing(bool on);
    bool editing() const { return editing_; }
    // ─── v1.0.0 sa-B: the member this accessor reads ──────────────────
    //
    // The ACCESSOR above was added for the board's editing state, but the
    // field it returns was not yet declared, so the class does not
    // compile without it. Declared here, next to the accessor and with
    // the same default (false), because that is the one place the
    // declaration belongs — a member next to the getter that returns it
    // is the only layout where a reader finds both.
    //
    // IF SA-D ALSO DECLARES IT, the merge will show a duplicate
    // declaration and the parent session should keep EITHER one. This is
    // noted in the report.
    bool editing_{false};

    // The short handle the prototype's WIN_META[].short carries — the
    // word a card prints in .win-name ("info", "status", "hp", "pet").
    // For a sub-window it is the SPLIT TABLE ROW's own handle, so the
    // board names a card with the same short word the prototype would,
    // read from the same table rather than a second hand-written list.
    // For a mother panel it is the panel's own handle
    // ("player" / "monster" / "damage" / "pet").
    [[nodiscard]] QString winName(int panelIndex, int subIndex = -1) const;

    // The scale badge a card prints in .win-sc — the prototype's
    // `win.scale.toFixed(2) + "x"` ("1.00x", "2.50x"), read live from
    // the window's own PanelSource::scale().
    [[nodiscard]] QString winScale(int panelIndex, int subIndex = -1) const;

    // The third row a card prints in .win-panel: the owning panel's
    // display name. For a sub-window this is the panel it was carved
    // out of (parentSlot), which is exactly the question the prototype
    // answers with `panel.<def.panel>` — the overview spans several
    // panels, so the card has to say whose it is.
    [[nodiscard]] QString winPanelName(int panelIndex, int subIndex = -1) const;

    // v0.5 zoom: 1.0 = fit-to-widget (default). >1 enlarges the
    // screen frame beyond the viewport; the parent QScrollArea
    // provides scrollbars. Ctrl+wheel adjusts in 0.1 steps.
    // v1.0.1 (#5): screenFrame()/fidelityHeightForWidth() consume the
    // same zoom, so the FRAME itself and the scroll child's minimum
    // geometry both scale — once, never z² — and zoom<1 shrinks the
    // frame inside the still-viewport-sized widget.
    void setZoom(qreal z);
    qreal zoom() const { return zoom_; }

    // ─── v1.0.1 (#9): FIXED-VIEWPORT MODE ──────────────────────────────
    //
    // THE DEFECT IT EXISTS FOR. Outside this mode zoom>1 fixes the
    // widget to sizeHint()*zoom (see setZoom's first arm), so the
    // canvas's natural size IS a function of zoom: a bare page measured
    // 1078×1062 at zoom 1 and 2156×2112 at zoom 2, and the console's
    // 1400×760 scroll viewport's child grew 1398×758 → 2156×2112 with
    // scroll ranges of 1368/772 — the PAGE, not the picture, got bigger.
    //
    // IN THIS MODE the widget's geometry is NOT a function of zoom:
    // sizeHint()/heightForWidth() answer the zoom-1 numbers, no
    // setFixedSize ever runs, and a user's splitter height survives
    // every zoom step. What zoom changes instead is the INSIDE:
    //
    //   viewportContentSize()   the content extent the frame lives in
    //                           (board + board padding, ≥ widget size)
    //   viewportScrollRange()   content − viewport, clamped ≥ 0 per axis
    //   setViewportScrollOffset / viewportScrollOffset()
    //                           the pan inside the viewport, clamped to
    //                           the range — so the big image can only
    //                           scroll INSIDE the viewport, never grow
    //                           it, and no HUD geometry is persisted.
    //
    // ONE TRANSFORM, STILL. screenFrame() remains the single mapping —
    // it subtracts the scroll offset itself — so paint, hit-test and
    // drag all consume the same offset frame and cannot drift.
    //
    // DEFAULT FALSE, and the false arm is the untouched legacy path
    // (zoom>1 still fixes the widget to its zoomed hint). Only the
    // console switches this on; the seam the console needs to wire
    // scrollbars is viewportContentChanged/viewportScrollChanged below.
    void setFixedViewportMode(bool on);
    [[nodiscard]] bool fixedViewportMode() const { return fixedViewport_; }
    // The content extent (logical widget pixels). Equals size() whenever
    // there is nothing to scroll (fixed mode off, or zoom small enough
    // that the frame fits).
    [[nodiscard]] QSize viewportContentSize() const;
    // max(0, content − viewport) per axis. QSize(0,0) when the mode is
    // off — there is nothing to scroll.
    [[nodiscard]] QSize viewportScrollRange() const;
    [[nodiscard]] QPoint viewportScrollOffset() const { return viewportScrollOffset_; }
    // Clamped to the range; a value outside it is WRITTEN CLAMPED, never
    // ignored (a console pushing a stale offset must not desynchronise
    // the scrollbar it is driving). No-op on an unchanged value.
    void setViewportScrollOffset(QPoint offset);

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

    // ─── v1.0.0 (OUTPUT-CONTRACT-COMPLETION): the four-slot arm's own ────
    // boxes, by panel index, in CANVAS pixels — the mother half of
    // subWindowRects(). One entry per slot, sized mhw::kPanelCount, and
    // an EMPTY rect is exactly what the paint left for a panel it did
    // not draw (every panel's lastTarget_ is reset at the top of
    // paintEvent, so a skipped panel keeps the empty one). That makes
    // "which windows did this frame actually paint" answerable from the
    // rects rather than from a screenshot, which is what lets a test
    // assert chip == painted boxes across BOTH arms of the board — the
    // sub-window one above and this one — instead of comparing the chip
    // to a sub-window count that was never the whole picture in Single,
    // or in the Rise Split overview's pets row.
    //
    // READ-ONLY, and it exposes no algorithm: the rectangles come from
    // the same paint pass the click handler hit-tests, so a box that is
    // not painted here is not clickable either.
    [[nodiscard]] QVector<QRectF> motherWindowRects() const;
    // Same for the drag highlight. -1 when nothing is selected.
    [[nodiscard]] int selectedSubWindow() const { return subSelected_; }

    // ─── v1.0.0: the console writes the selection back ──────────────
    //
    // Until now the selection had exactly ONE writer: a press inside a
    // sub-window's box (mousePressEvent, which emits subWindowSelected).
    // The console's inspector is the other direction — the user picks a
    // window in the rail or the inspector list and the board has to
    // highlight the same one — and that direction did not exist, so a
    // selection made anywhere but on the board left the board drawing
    // the previous ring while the inspector described another window.
    //
    // This is that write, and it is the only public way to set it.
    //
    // IT DOES NOT EMIT subWindowSelected. That signal already means "the
    // user selected this ON THE CANVAS", and a setter that echoed it
    // would let the console's own handler call straight back in — the
    // inspector/board ping-pong this pair exists to avoid. The setter
    // writes the state and schedules the repaint; the mouse path stays
    // the only emitter, so keyboard/Inspector/mouse cannot form a loop.
    //
    // BOUNDS. -1 clears, which is what the console sends when the
    // inspector has nothing selected. Any other value that is not a live
    // index — below -1, or at/above subWindowCount() — ALSO clears
    // rather than being ignored: a stale index is exactly what a rebuilt
    // binding list leaves behind, and a selection pointing past the end
    // of the vector would let the drag and arrow-key paths act on a
    // window that no longer exists. Clearing is the safe answer, and it
    // is why setSubWindowBindings() drops an out-of-range selection too.
    //
    // The no-op guard is the one every other setter here uses: a console
    // that pushes the same selection on every refresh must not schedule
    // a repaint each time. Works in every view state — the overview
    // selects for the inspector exactly as the prototype does.
    void setSelectedSubWindow(int index);

    // ─── v1.0.0: THE CONSOLE-FIDELITY BOARD ────────────────────────────
    //
    // `reference.html` is the frozen authority for what the console's
    // board looks like, and until this flag existed the canvas painted
    // the OLD prototype's stage: a #06080a field, a ruler, a header
    // strip, a footer strip and a safe-area rectangle. None of those
    // exist in the frozen HTML — `.board` is a `#101010` rectangle with
    // a `1px --board-line` border, an inset 12px band, a 10px mono label
    // and, in edit mode, a 5%×5% lattice. That is what this flag turns
    // on.
    //
    // DEFAULT FALSE, AND THE DEFAULT PATH IS UNTOUCHED. Every fidelity
    // branch below is written as a short-circuit on a false member, so a
    // canvas nobody put into fidelity mode paints exactly what it
    // painted before this flag existed — that is the guarantee the 12
    // Single-mode baselines and the two canvas->grab() suites rest on,
    // and it is why the old chrome is GATED OFF rather than deleted.
    //
    // ONLY ControlPanel turns it on. The flag is deliberately not read
    // from settings, not wired to the stagebar, and not consumed by the
    // overlay: it is a paint/geometry switch for the console's board and
    // nothing else.
    void setConsoleFidelityMode(bool on);
    [[nodiscard]] bool consoleFidelityMode() const { return consoleFidelity_; }
    void setTransparencyGuideEnabled(bool on) { transparencyGuide_ = on; update(); }

    // ─── v1.0.1 (#10): the board label's RESOLUTION half can go ─────────
    //
    // fidelityBoardLabel() prints `output · W×H · logical coordinate
    // space` inside the board (`.board-label`). The console's own top
    // bar ALREADY carries the full display identity — its SCREEN
    // dropdown is the same string shape — so inside the console the
    // in-board copy is redundant, and this switch hides it.
    //
    // CONSOLE-ONLY, DEFAULT TRUE. The label only exists in fidelity
    // mode (the legacy chrome arm never paints it), and the default
    // keeps painting it, so every existing pixel baseline — fidelity
    // and legacy — is untouched until the console opts in. Hiding is a
    // PAINT-ONLY change: the label has no geometry role (it is
    // pointer-events:none and takes no part in hit-testing), so frame,
    // content size, scroll range and drag are all byte-identical either
    // way. The top-bar display picker is the parent's; nothing here
    // touches it.
    void setFidelityBoardLabelVisible(bool visible);
    [[nodiscard]] bool fidelityBoardLabelVisible() const { return boardLabelVisible_; }

    // ─── v1.0.0: WINDOW VISIBILITY — the prototype's `.visible` ────────
    //
    // NOT THE SAME THING AS `enabled`. `SubSlot::enabled` / `Slot::enabled`
    // mean "every section of this window is off", and the board answers
    // that with the prototype's `.hidden-window` placeholder (opacity
    // .28, a `--rust` border, the word `hidden` in place of the body) —
    // a window that is DRAWN AS HIDDEN. `visible` is the other knob: the
    // prototype drops the window out of `visibleWindows()` altogether
    // (reference.html:442), so it is not laid out, not painted and not
    // hit-testable, and the board looks exactly as it would if the
    // window did not exist.
    //
    // DEFAULT TRUE. A console that never calls these two setters paints
    // every window it was given, which is the pre-1.0.0 behaviour and
    // the reason these can be added without touching a single baseline.
    //
    // UNKNOWN INDEX IS A SAFE NO-OP — in both directions. The setter
    // never grows `subSlots_` (growing would renumber every later
    // binding, which is the same trap setSubWindowPixmap() documents);
    // the getter answers `true` — the default — for an index that is not
    // a live window, so a stale index reads as "visible" rather than
    // crashing or inventing a hidden state.
    //
    // CONSUMPTION IS FIDELITY-GATED, exactly like the chrome it lives
    // inside. A non-fidelity canvas keeps its pre-1.0.0 semantics
    // byte-for-byte no matter what the shell pushes.
    //
    // HIDING WRITES NOTHING. The window's source keeps its margins, its
    // output and its sections; showing it again re-lays it out from the
    // source it always had. The only canvas state a hide can disturb is
    // the arrow-key nudge, which refuses to move a hidden window rather
    // than clearing `subSelected_` — so a hide/show round trip leaves
    // the selection exactly where the user left it.
    //
    // REBINDING RESETS. setSubWindowBindings() rebuilds `subSlots_` from
    // scratch, and visibility is per-binding state that the new list does
    // not carry — so every window comes back visible. See the setter's
    // own note: the shell must re-push visibility after a rebind.
    void setSubWindowVisible(int index, bool visible);
    [[nodiscard]] bool subWindowVisible(int index) const;
    // The same pair for the four base panels, addressed by panel index.
    void setPanelWindowVisible(int panelIndex, bool visible);
    [[nodiscard]] bool panelWindowVisible(int panelIndex) const;

    QSize sizeHint() const override;
    // v1.0.1 (#9): fixed-viewport mode keeps the widget on the zoom-1
    // height-for-width contract at EVERY zoom (the widget never grows),
    // so the mode's answer is the zoom<=1 answer regardless of zoom_.
    bool hasHeightForWidth() const override
    {
        return fixedViewport_ || zoom_ <= 1.0;
    }
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
    // v1.0.1 (#7): fires on EVERY user press inside a panel's painted
    // rect — including a re-click on the already-selected /
    // default-highlighted panel and, in Split, the pets box — so the
    // console can always refill its property panel. Also fires in the
    // overview (v1.0.0: a click selects so the inspector can show the
    // window, and does nothing else). The no-loop guarantee lives on
    // the other side: setSelectedPanel() never emits, so a console
    // echo cannot come back as this signal.
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
    // v1.0.1 (#9): the fixed-viewport seam. viewportContentChanged
    // fires when the CONTENT extent changes (zoom, fidelity flag,
    // preview screen, widget resize) while the mode is on — the console
    // re-derives its scrollbars from it — and viewportScrollChanged
    // fires whenever the offset actually moves (setter, or a clamp after
    // the content shrank). Neither exists as a behaviour outside fixed
    // mode: the content signal is gated on the flag and the scroll
    // signal only ever fires when the offset changes, which cannot
    // happen while the range is 0.
    void viewportContentChanged(QSize content);
    void viewportScrollChanged(QPoint offset);


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
        // v1.0.0: THE PROTOTYPE'S `visible`, NOT ANOTHER `enabled`.
        // enabled == false means "every section is off" and the board
        // draws the `.hidden-window` placeholder; visible == false means
        // the window is dropped out of `visibleWindows()` entirely
        // (reference.html:442) — not laid out, not painted, not
        // hit-testable. Default true, so a binding list nobody has
        // filtered behaves exactly as it did before this field existed.
        //
        // Owned by the CANVAS, not by the binding: setSubWindowBindings()
        // rebuilds this vector wholesale and a fresh SubSlot is visible,
        // so a rebind resets the filter and the shell has to push it
        // again. That is a deliberate choice — carrying a per-INDEX
        // visibility across a rebind would apply window A's filter to
        // whatever window the shell happens to put at A's slot next.
        bool visible{true};
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

    // ─── v1.0.0 sa-B: the window card's text, as paintWindowCard needs it ─
    // The prototype's `.win` is three rows: a header (name + scale), three
    // grey content bars, and a footer naming the panel and how many
    // sections are on. SubWindowPaintInfo carries the GEOMETRY (rect,
    // selection); this carries the TEXT and the two flags that change the
    // border, so a card can be painted from one struct pair without the
    // painter reaching back into the console for strings.
    //
    // WHY A SECOND STRUCT AND NOT THREE MORE FIELDS ON THE FIRST. The
    // geometry struct is filled by layoutSubWindows() and is consumed by
    // code that must not know what a card looks like. Keeping the card's
    // copy separate means the geometry pass stays unchanged — which is
    // what keeps the drag/hit-test paths and their tests independent of
    // this paint.
    struct WindowCardPaintInfo {
        QString name;        // .win-name — the window's short label
        qreal scale{1.0};    // .win-sc — "1.00x"
        QString panelLine;   // .win-panel — "怪物 · 4/6"
        bool hovered{false}; // .win:hover — border lifts to --board-fg3
        bool editing{false}; // .board.editing .win — border goes dashed
    };

    // Paint one sub-window box: its frame, its title, and the drag
    // highlight when it is the one being moved or nudged.
    void paintSubWindowBox(const SubWindowPaintInfo &info,
                           QPainter &p) const;
    // ─── v1.0.0 sa-B ──────────────────────────────────────────────────
    // Paint the prototype's window CARD inside a sub-window's rect: the
    // three-row .win structure (header / content bars / panel footer),
    // the accent ring and soft shadow when selected, and the dashed
    // border in the editing state.
    //
    // It is a SEPARATE function from paintSubWindowBox() so the default
    // (non-card) path keeps its exact pixels: the 12 Single-mode
    // baselines and the two canvas->grab() suites in
    // console_split_sync_tests / damage_rows_window_tests pin what the
    // canvas paints today, and the card is the v1.0.0 rework's addition
    // on top of it. The caller decides which one a window gets.
    // ─── end sa-B ─────────────────────────────────────────────────────
    void paintWindowCard(const SubWindowPaintInfo &info,
                         const WindowCardPaintInfo &card,
                         QPainter &p) const;

    // The one layout pass, called from paintEvent. Fills subRects_ and
    // returns the boxes the painter draws, so the painter stays a pure
    // function of what this returns. `fit` and `frameTopLeft` are the
    // logical→canvas mapping the four-slot arm established.
    QVector<SubWindowPaintInfo> layoutSubWindows(
        qreal fit, const QPointF &frameTopLeft, const QSize &logical);

    // v1.0.0: THE BOARD'S OWN HEIGHT, at a given widget width. The
    // fidelity board is not "whatever the screen's aspect ratio fills":
    // it is `.board`, which caps its width and carries a 12px viewport
    // pad on top, so the height has to follow the WIDTH rule and not the
    // other way round. Spelled once and used by both sizeHint() and
    // heightForWidth(), because the two disagreeing is exactly how the
    // frame collapsed into a horizontal band in v0.11.4 (see the note on
    // sizeHint()).
    [[nodiscard]] int fidelityHeightForWidth(int width) const;

    // v1.0.0: the `.board-label` line, composed the way
    // reference.html:475 composes it — the display's own label, then
    // " · logical coordinate space".
    [[nodiscard]] QString fidelityBoardLabel() const;

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

    // ─── v1.0.1 (#9): the fixed-viewport geometry, spelled once ─────────
    //
    // fidelityBoardSizeF() is the fidelity board's SIZE with zoom
    // consumed exactly once — the widget-avail arm the legacy path uses
    // (the widget carried the zoom for z>1 there) and the plain
    // base×zoom arm fixed mode uses (the widget never carries it). One
    // function so sizeHint's family, viewportContentSizeF() and
    // screenFrame() cannot drift into a z².
    [[nodiscard]] QSizeF fidelityBoardSizeF() const;
    // The content extent the frame lives in: board + board padding in
    // fidelity mode, fitted frame + chrome in the legacy arm, and the
    // widget itself whenever there is nothing to scroll (mode off, or a
    // zoom that still fits). NEVER smaller than size() — that is what
    // keeps viewportScrollRange() ≥ 0 by construction.
    [[nodiscard]] QSizeF viewportContentSizeF() const;
    // Re-emits viewportContentChanged when the extent moved (fixed mode
    // only) and re-clamps the scroll offset to the new range. Called
    // from every state change that can move the extent: zoom, the
    // fidelity flag, the preview screen, the mode itself, resize.
    void refreshViewportMetrics();
    void clampViewportScroll();
    // Shared write path of the two public/私有 scroll entry points:
    // applies an ALREADY-clamped offset, no-op on sameness, repaints,
    // emits viewportScrollChanged.
    void applyViewportScroll(QPoint offset);


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

    // v0.15: the same resolution previewScreenInfo() performs, spelled
    // once and parameterised by output name, so a per-window drag clamp
    // can ask about a DIFFERENT screen than the one the canvas is
    // drawing. Extracted verbatim rather than reimplemented: two copies
    // of this order-of-resolution is exactly how the canvas and the
    // panel end up disagreeing about which screen a window lives on.
    //   ""                       -> primaryScreenInfo()
    //   a live output name       -> its cached QScreen geometry
    //   a name that is not live  -> rebuild once, retry, else primary
    const screen_query::Result &screenInfoForOutput(
            const QString &outputName) const;

    // v0.15, revised v1.0.1 (#8): the output a BOUND window is measured
    // against, on the ONE output chain: the window's own outputName,
    // else its preview-only fallback (what the console pushes in — its
    // parent panel's output or the primary's name), else the PRIMARY,
    // through detail::effectiveOutputName (the stand-in for
    // screen_query::effectiveOutputName while the runtime lane's
    // function is still landing). Deliberately NOT previewOutputName_
    // (the viewer) as the last step: a mother/child that follows lives
    // where the chain puts it, and the viewer is a view state, not a
    // location.
    [[nodiscard]] QString boundOutputName(const PanelSource *src) const;

    // v1.0.0: 「跟随主屏」的空串归一成一个真实输出名，让总览的窗口归属
    // 与 overviewScreen_ 落在同一个字符串空间里比较。空串、未连接的输
    // 出都归一为 Qt 的主屏名；连那个也取不到就保持空串（此时总览按
    // 「什么都归属主屏」处理，等价于不过滤）。
    [[nodiscard]] QString primaryOutputName() const;

    // v1.0.0: 总览过滤。回答「这块屏上应该画哪些窗口」，规则见
    // windowOutputName() 的注释。
    //
    // 母面板额外要求 present && !pixmap.isNull()：总览下恢复绘制三个
    // 母面板时，control_panel.cpp 传进来的 mask 还是 Split 语义的（见
    // 它自己 4489-4494 行的说明），pixmap 可能是空或旧值。本轮的稳妥
    // 处理是「空 pixmap 就跳过」，不修 mask 语义 —— 那是更大的坑，
    // 留待单独任务。
    [[nodiscard]] bool overviewDrawsPanel(int panelIndex) const;
    [[nodiscard]] bool overviewDrawsSub(int subIndex) const;

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
    // v1.0.0: the four base panels' own `visible` (see the public pair).
    // A plain C array rather than a QVector so the length is the compile
    // time constant it must be — an initializer that lists four `true`s
    // stops the build if `mhw::kPanelCount` ever stops being four,
    // which is the same guard `static_assert(kAccents.size() == ...)`
    // buys at the top of the .cpp.
    bool panelVisible_[mhw::kPanelCount] = {true, true, true, true};
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
    // v1.0.0: the prototype's `board.editing` state. False by default,
    // so a canvas nobody has put into edit mode paints the resting face
    // — the same short-circuit discipline as overviewMode_.
    qreal zoom_{1.0};
    // v0.8: tracks the user's SCREEN selection so the preview's
    // "screen frame" rectangle and the per-panel drag clamps match
    // the output the panels are bound to. Empty = primary.
    QString previewOutputName_;

    // v1.0.0: the single-display overview. Two separate pieces of state,
    // and the separation is the whole point:
    //
    //   overviewMode_    — the view is read-only and draws every window
    //                      on ONE display. Default false, and the paint
    //                      gate is written as a short-circuit so a false
    //                      here costs nothing (see the header comment).
    //   overviewScreen_  — WHICH display the overview is looking at.
    //                      Deliberately not previewOutputName_: entering
    //                      the overview saves the old preview screen and
    //                      points the canvas at this one, so leaving
    //                      restores exactly what the user was editing.
    //
    // savedPreviewOutput_ is what the exit path restores. It is only
    // meaningful while overviewMode_ is true.
    bool overviewMode_{false};
    QString overviewScreen_;
    QString savedPreviewOutput_;

    // v1.0.0: paint the FROZEN prototype's board instead of the old
    // prototype's stage. False by default, so every branch reading it is
    // a short-circuit — see the public setter's note for why the old
    // chrome is gated off rather than deleted.
    bool consoleFidelity_{false};
    bool transparencyGuide_{false};
    // v1.0.1 (#9): fixed-viewport mode + its pan. Both default to the
    // legacy contract: mode off (zoom still fixes the widget above 1)
    // and offset at the origin. viewportScrollOffset_ is only ever
    // non-zero while fixedViewport_ is true — clampViewportScroll()
    // zeroes it the moment the mode or the range goes away.
    bool fixedViewport_{false};
    QPoint viewportScrollOffset_{0, 0};
    // Last extent emitted on viewportContentChanged, so a caller that
    // re-pushes the same numbers sees no signal. -1,−1 = "nothing
    // emitted yet", which can never equal a real (≥0) extent.
    QSize notifiedContentSize_{-1, -1};
    // v1.0.1 (#10): paint the .board-label line. TRUE by default — the
    // label's historical presence — so the console opts INTO hiding it.
    bool boardLabelVisible_{true};


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
