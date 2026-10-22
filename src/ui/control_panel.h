#pragma once

#include "core/game_snapshot.h"
#include "ui/panel_source.h"
// v0.13: the Single/Split enum, for the layout-mode members and the
// three layout method signatures below. This is an INCLUDE and not a
// forward declaration on purpose: the member's default initializer
// needs PlayerLayoutMode::Single, and a `enum class X : int;` forward
// declaration gives the type without its enumerators. panel_sections.h
// is already a dependency of this console's .cpp (it drives the four
// inspector row sets), so nothing new is being pulled into the
// translation unit — only into the header, where it is used.
#include "ui/panel_sections.h"

// v1.0.0 fidelity port: the window model. A header-only struct plus a
// handful of free functions in console_fidelity_windows.cpp; including
// it here costs one small TU (already in mhw-ui) and keeps the model's
// contract next to the members that hold its state.
#include "ui/console_fidelity_windows.h"

// v1.1.0 Mixed: the per-window detach plan. The console is the WRITER of
// the conf's one `layout.windows.<game>=` row per game (saveMaskToDisk),
// so the type has to be complete here rather than forward declared. The
// header is QtCore-only, which is exactly what a value member may pull
// into this header — no QWidget, no QSettings, nothing new to the TU.
#include "ui/window_layout_plan.h"
#include "ui/layout_package.h"

#include <QMainWindow>
#include <QComboBox>
#include <QPointer>
#include <QSlider>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <array>
#include <functional>
#include <memory>
#include <QtGlobal>
#include <QSpinBox>

class QSplitter;
class QScrollArea;
class QPropertyAnimation;
class QHBoxLayout;
class QResizeEvent;

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QPlainTextEdit;
class QWidget;
class Panel;
class PlayerPanel;
class MonsterPanel;
class DamagePanel;
class PetDamagePanel;
class ToggleChip;
class SectionRow;
class SectionCountBar;
class HudCanvas;
class RiseReframeworkCard;

// v0.13: the split's sub-windows, held for the CONSOLE's preview.
// The class lives in control_panel.cpp rather than here, because it
// needs every split window's complete type (a unique_ptr<Panel> cannot
// be destroyed through a forward declaration) and those headers belong
// to the translation unit that already includes them.
class SplitWindowPreviewHost;

namespace mhw {
class OverlayProcessController;
}

// Standalone control console for Monster Overlay. NOT a layer-shell
// surface — a plain QMainWindow the user can move, focus and close like
// any app. It owns four real overlay panel instances rendered off-screen
// (WA_DontShowOnScreen) so toggling a switch re-paints the matching
// preview with the exact QPainter code the live overlay uses.
//
// Left column  = master + per-section switches (mirrors panel_sections.h)
// Right column = live preview of each panel under the current mask
//
// No IPC to the running overlay yet (the live side is not decoupled for
// remote control). This is a pure design/preview tool.
class ControlPanel : public QMainWindow {
    Q_OBJECT
public:
    // v0.5.6 polish: a custom int property animated by stageAnim_. The
    // animation drives the stage pane height through QVariantAnimation;
    // each frame we read this value and call consoleSplitter_->setSizes.
    // Kept public because Q_PROPERTY accessors must be.
    Q_PROPERTY(int stagePaneHeight READ stagePaneHeight WRITE setStagePaneHeight)
    int stagePaneHeight() const;
    void setStagePaneHeight(int h);
    // v1.0.0 (O1): the stage pane, addressed by NAME rather than by a
    // splitter index spelled out at five call sites. The stage moved from
    // index 1 to index 0 when the prototype's stage-on-top layout was
    // adopted, and one call site left behind collapses the wrong pane.
    // See the definitions in the .cpp for the full contract — including
    // why the splitter's live height is stagePaneSize() and not the
    // Q_PROPERTY's stagePaneHeight().
    [[nodiscard]] QWidget *stagePane() const;
    [[nodiscard]] int stagePaneSize() const;

    explicit ControlPanel(QWidget *parent = nullptr);
    ~ControlPanel() override;

    // v0.12 sub-windows: registerSubWindow / unregisterSubWindows /
    // subWindowCount / isSubWindowSlot / subWindows() / selectedSubWindow
    // were removed here (v0.12 stage 1 revert). They drove
    // mhw::SubWindowRegistry, which had zero callers outside this
    // console — the main.cpp wiring that would have decided WHEN a
    // sub-window exists was never written, so the console kept a
    // permanent, always-empty registry and a selection index that no
    // code path ever set. ui/panel_player_split.h (the pilot window)
    // is self-sufficient: it needs setChromeVisible(false) +
    // Panel::isSubWindow(), not a console slot.

public slots:
    // Parent-owned async worker/fetcher calls this on the GUI thread after
    // completing a request emitted below. The UI never downloads or extracts
    // the archive itself.
    void finishRiseReframeworkOperation(bool ok, const QString &detail);

signals:
    // Replaceable integration seam: a parent session connects these signals
    // to its worker/fetcher and calls finishRiseReframeworkOperation(). No
    // manager/fetcher implementation is referenced by this UI translation
    // unit for install/removal work.
    void installRiseReframeworkRequested(const QString &gameDir);
    void removeRiseLuaRequested(const QString &gameDir);
    void removeRiseReframeworkRequested(const QString &gameDir);

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *e) override;
    // v1.1.1 (顶栏溢出): THE TOPBAR WRAPS WHEN THE WINDOW IS NARROW. The
    // header's four action buttons live at the far right of ONE row, so
    // at the console's own minimum width (1272 measured on the user's
    // desktop) they sat past the visible area of the root scroll —
    // "Start overlay" read as `Start ov…` and 导出 was off-screen with no
    // way to reach it except scrolling the whole page sideways. The row
    // is now two rows when the single row cannot hold its own minimum,
    // and resizeEvent is what re-decides that: the available width is the
    // window's, not the bar's (the bar grows to its minimum inside a
    // scroll area, so its own width is exactly the number that cannot be
    // trusted here).
    void resizeEvent(QResizeEvent *e) override;

    // RiseReframeworkCard (src/ui/rise_reframework_card.{h,cpp}) reaches
    // trSet()/trTip() so its widgets land in the i18n replay registry above,
    // and QObject::receivers(), to see that a worker is connected at all.
    // Befriending it is narrower than widening this API for one caller.
    friend class RiseReframeworkCard;

private:
    // L2: persistent mask state lives at ~/.config/monster-overlay/monster-overlay.conf
    // so the user's last toggle choices survive across console restarts. The
    // console writes on exit (and any time we explicitly call saveMask());
    // reads happen once at construction so the checkboxes open with the
    // previous session's state.
    void loadMaskFromDisk();
    void saveMaskToDisk() const;

    // v0.13: shared-combo index constants. Spelled out once so the
    // builder, the resolver replay and the test all name the same two
    // numbers — a literal 0/1 at three call sites is how a reordered
    // combo silently flips which entry writes `layout=split`.
    // v1.1.0: Mixed is index 2. Single/Split keep 0/1 because
    // layoutComboIndexFor() and layoutComboMode() are the ONLY two
    // converters between the combo and the enum, and both are exhaustive
    // switches — so a fourth mode lands as a -Wswitch-visible edit here
    // rather than as another `?:` that quietly answers Single. Index ORDER
    // itself is not a compat surface: the combo is rebuilt from
    // layoutModeInForce_, never restored from a saved index.
    static constexpr int kLayoutComboSingleIndex = 0;
    static constexpr int kLayoutComboSplitIndex  = 1;
    static constexpr int kLayoutComboMixedIndex  = 2;

    // ---- v0.13 console layout mode (Single / Split) --------------------
    //
    // WHAT STAYS PURE: the row parsing and the flag spelling live in
    // ui/viewmodel/console_layout_mode_codec.h; the precedence chain is
    // mhw::resolveStartupLayoutMode() in ui/panel_sections.h, shared
    // with the overlay itself.
    //
    // WHAT THE VIEW OWNS: the widget state (layoutCombo_ /
    // layoutApplyBtn_ / layoutModeInForce_) and the three operations
    // below. None of them opens a QSettings handle, so the whole
    // read/write path stays drivable from a plain test.
    //
    // Read: resolveStartupLayoutMode()'s OWN conf arm. The console does
    // not implement "conf beats the UI" anywhere — it feeds its
    // `layout=` row into the shared resolver and takes the answer.
    int layoutComboIndexFor(mhw::PlayerSection::PlayerLayoutMode mode) const;
    void applyStartupLayoutMode();
    void applyLayoutChoice(mhw::PlayerSection::PlayerLayoutMode mode);
    // The mode the last LAUNCH actually used (what the conf row said at
    // startup, or what applyLayoutChoice() committed). Distinct from
    // layoutCombo_'s current text, which is only a pending selection.
    [[nodiscard]] mhw::PlayerSection::PlayerLayoutMode layoutModeInForce() const
    { return layoutModeInForce_; }
    // In-force mode as the combo spells it. Returns Single when the
    // combo does not exist, so a caller with no UI still gets a sane
    // answer.
    [[nodiscard]] mhw::PlayerSection::PlayerLayoutMode layoutComboMode() const;
    // Re-enable the apply button only when the selection differs from
    // the mode in force, so the control never invites a pointless
    // restart. Also disables it when nothing is selected.
    void updateLayoutApplyEnabled();

    // ---- v0.13: the split sub-window preview --------------------------
    //
    // Push the in-force mode into the canvas and (re)build the
    // sub-window geometry when it changed. Called from
    // applyStartupLayoutMode() and applyLayoutChoice() — the two places
    // the mode in force can move — and from nowhere else, so the canvas
    // is never rebuilt on a mask change it does not care about.
    //
    // In Single this is what keeps the canvas subSlots_-free: the mode
    // is pushed, the host is dropped, and the canvas clears its vector
    // itself (see HudCanvas::setLayoutMode).
    void syncSplitPreviewToCanvas();
    // Re-render the sub-windows after a mask change. Only does work in
    // Split mode; the canvas's own subSlots_ is the authority on
    // whether there is anything to draw.
    void refreshSplitWindowPreviews();

    struct PanelCtl {
        Panel *panel = nullptr;
        ToggleChip *master = nullptr;
        // subs[i] corresponds to bit (1u << i), matching the order of
        // mhw::*Section::names() in panel_sections.h.
        QVector<SectionRow *> subs;
        QLabel *preview = nullptr;
        QWidget *navButton = nullptr;
        QLabel *navSummary = nullptr;
        QLabel *countLabel = nullptr;
    SectionCountBar *countBar = nullptr;
        QSlider *scaleSlider = nullptr;
        QSlider *opacitySlider = nullptr;
        QSlider *bgAlphaSlider = nullptr;
            QLabel *posLabel = nullptr;
        // v1.0.0 T2: editable position. The label reports the quadrant;
        // these two carry the raw margins the user types.
        QSpinBox *posSpinX = nullptr;
        QSpinBox *posSpinY = nullptr;
        // v0.8: per-panel screen selection dropdown. Entries are the
        // outputs QGuiApplication::screens() reports; the special first
        // entry "<PRIMARY>" (empty userData) means "follow OS primary".
        // Selection is persisted to panels.ini via Panel::setOutputName().
        QComboBox *outputCombo = nullptr;
    };

    // Shared mask reader for the four chrome panels: a panel is "all
    // off" (0) when its master chip is unchecked, otherwise every
    // checked sub row sets its own bit. Used by the argv builder
    // (--mask-*) and by the mask writer so both read the same state.
    // Static: it needs PanelCtl's full type but no ControlPanel state.
    static uint32_t maskFor(const PanelCtl &c);

    QWidget *buildInspector(const QString &titleKey, const QString &subKey,
                            const QStringList &labels, int idx);
    QWidget *buildObjectButton(const QString &letter, const QString &titleKey,
                               const QString &summaryKey, int idx);
    QWidget *buildRule();
    QWidget *buildEditModeBlock();
    // v1.0.0 console rework: the horizontal bar across the top of the
    // content column — layout mode (combo + apply), EDIT and the real
    // START. These four controls used to sit in the rail's scroll area;
    // they moved here because they act on the whole window, not on one
    // panel. The objectNames are unchanged, so the tests that drive them
    // through findChild() keep working.
    QWidget *buildToolbar();
    /* ── v1.0.0 sa-A: topbar / sidebar / statusline ──
       Three refresh passes the rebuilt shell needs, all of them driven
       from state changes that already existed:

         syncLayoutSegmentToMode()  re-asserts the segmented control and
             the hidden combo pair after a layout change, and re-applies
             the Split-only visibility of 分开密度. Called from the
             segment's click handler and from applyLayoutChoice().
         refreshNavCounts()         writes the nav rows' window counts
             ("{n} 窗") and the overview row's total. Called from
             retranslateUi(), switchGame() and the mode change.
         refreshStatusline()        rewrites the status line's five states
             from demoState_ + the overlay's liveness. Called from
             retranslateUi(), the demo-state combo, setOverlayRunning()
             and refreshAutoDetect().

       They are members rather than file-local statics because they read
       members (splitPreview_, layoutModeInForce_, demoState_, the nav
       count labels) that a static could only reach through a lookup
       table — and because sa-D needs the same three entry points.
       ── end sa-A ── */
    void syncLayoutSegmentToMode();
    void refreshNavCounts();
    void refreshStatusline();
    // v1.0.0 (sa-D): the topbar's 实例 picker. Rise-only; it lists the
    // one running game process mhw::detectGame() can see, or 未检测到.
    // Called from refreshAutoDetect(), which is where the detector's
    // answer arrives — so this reads a cached fact rather than scanning
    // /proc a second time on the same tick.
    void refreshInstanceCombo();
    // ─── v1.0.0 fidelity port: the window model ──────────────────────────
    //
    // The prototype's inspector addresses ONE window — the selected one —
    // and the canvas, the rail, the export payload and the eye toggles all
    // read or write the same list. These four methods are the whole model:
    //
    //   buildWindowStates()   the live list for the scope in force
    //   selectedWindow()      the one row the inspector describes
    //   selectWindow()        the write-back, from the canvas or the rail
    //   refreshWindowModel()  rebuild + push into canvas + inspector
    //
    // They are members rather than free functions because they read
    // splitPreview_ / ctl_ / layoutModeInForce_ / currentGame() — four
    // pieces of state that a file-local table would have to mirror.
    // ─── end v1.0.0 window model ───────────────────────────────────────
    // v1.0.0 (O1): THE FOUR MODEL METHODS ARE PUBLIC. They are the one
    // answer to "which windows does this scope have / which one is
    // selected", and the tests that pin the visible behaviour (the
    // console's structure test and the scope-persistence test) must be
    // able to ask that question without reaching through a hidden
    // widget — which is precisely the trap those two files used to fall
    // into. The writers (selectWindow / refreshWindowModel) come along
    // for symmetry: they are the canvas's own entry points and are
    // already reachable through the canvas signals.
public:
    [[nodiscard]] mhw::WindowList buildWindowStates() const;
    // ── v1.1.2 (Mixed 拓扑): the FIFTH model answer — the windows the
    // RUN has, not the windows the BOARD can draw ──────────────────────
    //
    // buildWindowStates() answers "which windows does the console draw",
    // and since v1.1.3 that is complete in Mixed too — the Mixed host
    // exists, so the board list carries the detached windows and the
    // mothers that still mount. This stays the COUNT model and stays
    // separate for two reasons: it never returns anything but
    // buildWindowStates() in Single and Split (one early return, no
    // branch to drift), and in Mixed it still answers even where the
    // board cannot — before the first sync (no host yet) the detached
    // rows are fabricated from the plan alone, so the stage chip, the
    // rail counts and the topbar subtitle are right whether or not a
    // Panel exists. Rows the board already carries are NOT appended
    // twice (see the dedupe in the definition), so with a host present
    // the two lists are equal, row for row.
    [[nodiscard]] mhw::WindowList buildWindowTopology() const;
    [[nodiscard]] mhw::WindowState selectedWindow() const;
    // 验收项5: the Panel the capability rows act on — the split
    // sub-window when one is selected, else the mother, else null. One
    // spelling for every row so a write cannot land on the wrong
    // instance (the defect the same expression is spelled out inline for
    // the position fields to avoid).
    [[nodiscard]] Panel *selectedPanelOrNull() const;
    // ── D1: THE ONE EDIT TARGET ─────────────────────────────────────────
    // Every selection-derived control asks THIS accessor what is selected:
    // the capability rows, 缩放 / 透明度 / 目标显示器 / 显示该窗口 / 坐标,
    // and the readouts above them. It exists because those two halves used
    // to read TWO different answers for ONE selection — the capability rows
    // went through selectedPanelOrNull() (which can answer a mother panel)
    // while the metadata form read selectedWindow() (which cannot: Split
    // emits no mother row) — so in a Split mother focus the form showed a
    // default-constructed window's numbers as if they were the panel's, and
    // every write was refused by a guard that only knew the window half.
    //
    //   panel         the instance every write lands on.
    //                 nullptr ⇒ NOTHING editable: the five controls go
    //                 dark AND their readouts are cleared — a disabled
    //                 control still displaying the previous object's
    //                 numbers is a control that lies.
    //   panelIndex/   the slot the selection names (rail / stack index);
    //   subIndex      subIndex < 0 = the panel itself, no sub-window.
    //   hasWindow     a WINDOW ROW backs this selection (it matched a row
    //                 of buildWindowStates()). Only then does 显示该窗口
    //                 have a key the model actually reads, so only then is
    //                 the checkbox enabled — a mother panel that is not a
    //                 window in the scope in force has no window switch.
    //   window        that row; only meaningful when hasWindow.
    //   resolvedOutput the honest logical frame: the same own → parent →
    //                 primary chain buildWindowStates() resolves, EMPTY
    //                 when no screen can be named — never a silently
    //                 substituted primary. The coordinate form is enabled
    //                 exactly when this and panel are both present.
    //   ownOutput     panel->outputName(), what the 目标显示器 combo selects
    //                 (empty = follow).
    //   title         what the selection chip and the object title say.
    struct EditableSelection {
        Panel *panel = nullptr;
        int panelIndex = -1;
        int subIndex = -1;
        bool hasWindow = false;
        mhw::WindowState window;
        QString resolvedOutput;
        QString ownOutput;
        QString title;
    };
    [[nodiscard]] EditableSelection currentEditableSelection() const;
    void selectWindow(int panelIndex, int subIndex);
    void refreshWindowModel();
    // ─── v1.1.4 (verification hook): the --snap-dialog=<...> CLI arm ─────
    // The three modals (help / settings / export) are private because the
    // toolbar owns them. This one narrow entry exists so the offscreen
    // screenshot hook can photograph them — a screenshot cannot run
    // beside a modal (openXxxDialog() blocks in exec()), so it has to be
    // opened FROM the panel. Enumerated on purpose: it opens ONE named
    // dialog and nothing else, and an unknown name opens the export one
    // rather than becoming a general "reach any private member" door.
    void snapOpenDialog(const QString &which);
    // ─── v1.0.0 integration (stage A): the visibility filter ────────────
    // Pushes buildWindowStates()' `visiblePersisted` into the canvas's
    // two visibility setters (setSubWindowVisible for a split window,
    // setPanelWindowVisible for a mother). PUBLIC because the contract is
    // "every bind / render / refresh re-pushes it" and a test has to be
    // able to prove the push happened after a rebinding, which reset the
    // canvas side back to its default of true. Pure read → write; it
    // never touches panels.ini, never touches panelEnabled (a different
    // axis with a different consumer) and is safe to call at any time —
    // an empty model means "every live binding is visible", which is the
    // canvas's own default, so calling it before anything is bound is a
    // no-op rather than a bug.
    void pushWindowVisibility();
    // D3: the one liveness entry that re-styles START/STOP, the status
    // badge and (since D3) the 在覆盖层中编辑 button's enable state.
    // PUBLIC for the same reason saveAllSplitAndBaseWindows() is: a test
    // cannot spawn a real overlay, and this is literally the call the
    // controller's `runningChanged` replays — so driving it exercises the
    // production path instead of a stand-in.
    void setOverlayRunning(bool running);
private:
    // The inspector's three columns, rebuilt for the selected window.
    void buildInspectorForSelection();
    // Builds the three columns' widgets into the drawer's frame. Called
    // once from the ctor; buildInspectorForSelection() fills them.
    void buildWindowInspectorFace(QWidget *parent, QVBoxLayout *parentLayout);
    // (D) the stage's shell-head: sub line, screen chip, window count.
    void refreshStageHead();
    // ── v1.0.1: three CHEAP refreshes (设置 / 预览 / 属性 + 拖动跟手) ──
    // Each one rewrites only the widgets its caller says changed; none
    // rebuilds the inspector or rebinds the canvas's window list.
    void refreshWindowPreviewPixmap();          // 验收项2: 检查器第三列图源
    void refreshSelectedWindowPreview();        // 验收项6: 不透明度/缩放图源
    // 验收项1 (B 第1阶段): the detail viewport's own zoom, decoupled from
    // the real HUD scale. z == 0 means Fit (the default); 1.0 is 1:1 with
    // the source render; anything else is an explicit ratio.
    void setPreviewZoom(qreal z);
    void applyPreviewDisplay();                 // source + zoom -> label pixmap
    [[nodiscard]] qreal previewFitScale() const;
    [[nodiscard]] qreal previewDefaultScale() const;   // 实测缺陷2: the margin default
    // One ± step on the preview dial, FROM WHERE THE USER IS LOOKING:
    // from an explicit zoom it steps that ratio, from Fit it steps the
    // fit scale actually on screen (so the first press is not a jump
    // from "whole image" to an unrelated number). Clamped to [0.1, 8].
    [[nodiscard]] qreal stepPreviewZoom(int dir) const;
    void refreshSelectedWindowPositionFields(); // 验收项4: 拖动时位置字段
    void rebuildScreenCombo();                  // 验收项8: 内屏/热插拔列表
    // (C) one section checkbox of the selected window: writes the owning
    // PANEL's mask (a sub-window's sections are its panel's bits) and
    // re-renders that panel.
    bool toggleSelectedWindowSection(uint32_t bit, bool on);
    // (C) the whole per-selection column: object title, coordinate note,
    // section rows and count. Called by the refill and by the one trHook
    // that refreshes live text on a language flip.
    void refreshGroupColumn();
    // ── v1.1.0 Mixed: the per-window 独立/合并 half of that column ──────
    //
    // One plan PER GAME (the conf carries one `layout.windows.<game>=`
    // row per game), indexed by game — never a single plan shared by
    // both, or saving World would rewrite Rise's list.
    [[nodiscard]] mhw::WindowLayoutPlan &mixedPlanFor(mhw::GameId game);
    [[nodiscard]] const mhw::WindowLayoutPlan &
    mixedPlanFor(mhw::GameId game) const;
    // ── v1.1.3 (Mixed 画布): the three readings of the plan the canvas
    // needs, each spelled ONCE so the board, the model and the count can
    // never drift apart ────────────────────────────────────────────────
    //
    // mixedPlanSignature() identifies the plan for the host rebuild
    // guard (see splitPreviewHostPlan_).
    //
    // effectiveMotherMask() is the overlay's own rule applied to a
    // console Panel: 母面板 mask = 用户 mask − 本 run 拆走的行的 claim
    // (main.cpp: layoutPlan.retainedMask(parent, userMask)). Single and
    // Split pass the user mask through unchanged, so the two legacy
    // topologies are byte-identical; in Mixed it is what keeps a
    // detached block from being painted TWICE — once in its own window
    // and once inside the mother that gave it up.
    //
    // mixedMotherMounts() is the mount gate, again only ever mixed in
    // Mixed: mhw::planMotherPanelEnabled()'s mask half spelled with the
    // console's own Panel predicates (the console has no --no-* flags,
    // so disabledByFlag is false by construction). Both buildWindowStates()
    // (the board) and buildWindowTopology() (the count) and
    // rebuildAndRender() (the canvas's `present` flag) read THIS, which
    // is why they answer one question three ways and agree.
    [[nodiscard]] quint32 mixedPlanSignature() const;
    [[nodiscard]] uint32_t effectiveMotherMask(int panel,
                                               uint32_t userMask) const;
    [[nodiscard]] bool mixedMotherMounts(int panel, uint32_t userMask) const;
    // Read both games' rows out of the conf text this process just read
    // (applyStartupLayoutMode's own read — no second file open, and no
    // write: a load must never seed anything).
    void loadMixedLayoutPlans(const QString &confText);
    // The block itself: built ONCE (its 11 rows are the registry, which
    // does not depend on the selection) and re-parented into the group
    // column on every refill, because that column's layout is rebuilt per
    // selection. refresh() re-derives visibility, titles, checked state
    // and the 独立窗口/合并显示 labels from the plan in force.
    void buildMixedGroupBlock();
    void refreshMixedGroupBlock();
    // One switch, and the two quick operations over all registry rows.
    bool setMixedWindowDetached(const QString &settingsKey, bool detached);
    void setAllMixedWindowsDetached(bool detached);
    // v1.1.3: what follows a plan change — rebuild the Mixed preview host
    // if the detached set really changed, re-state the mothers' mount
    // gate, and refresh the model. A no-op outside Mixed, which is what
    // keeps Single/Split byte-identical while the plan is being staged.
    void refreshAfterMixedPlanChange();
    // Writes the selected window's position from the inspector's two spin
    // boxes. Returns false when there is no selection, so the caller can
    // report "nothing selected" rather than writing into a stale window.
    bool applySelectedWindowPosition();
    // Writes the selected window's target display. Empty `outputName`
    // means "follow the parent panel", which is the prototype's `follow`
    // entry and the chain default (own → parent → primary). The write
    // lands on the SELECTED window's own Panel — a split sub-window
    // included — never on its mother; see the definition for the four
    // pieces of evidence.
    bool applySelectedWindowOutput(const QString &outputName);
    bool applySelectedWindowScale(qreal scale);
    bool applySelectedWindowOpacity(qreal opacity);
    // The per-window visibility switch — the SECOND axis, independent of
    // Panel::panelEnabled() (see the model header for why they differ).
    bool applySelectedWindowVisible(bool on);
    // v0.7.1: dedicated narrow column for the World/Rise game selector.
    // Returns a QWidget the caller parents to the horizontal QSplitter
    // on the left edge of the window; the existing rail sits to its
    // right and keeps its HUD OBJECTS / WORKSPACE rows unchanged.
    QWidget *buildGameColumn();
    void selectPanel(int idx, bool fromNav = true);
    // The topbar's title/subtitle pair. `.topbar-title` is a two-line
    // block whose copy follows the VIEW (overview → 显示器总览, a panel
    // view → 「玩家面板」), so it is a function of the selection rather
    // than a frozen string: this refreshes both lines from live state.
    void refreshTopbarTitle();
    // v1.1.1 (顶栏溢出): decide the topbar's row count. Called from
    // resizeEvent (the width is what changes), from retranslateUi() (the
    // copy is what changes the minimums) and from the two places that
    // rewrite a topbar button's text. Pure function of the CURRENT
    // minimums and the window's available width, so it is idempotent —
    // two calls in a row cannot disagree and nothing can oscillate.
    void refreshTopbarWrap();
    // The draft's SCOPE RESET. Switching the game or the layout mode
    // rewrites `state.view='overview'` and `state.selected=null`
    // (reference.html:332 / :334) and, for the layout switch,
    // `state.editing=false`. IMPLEMENTATION-PLAN §O1 states the same
    // rule: 切 game/mode → overview / null / nonedit. `alsoExitEditing`
    // is the game path's own distinction (that handler does not touch
    // `state.editing` in the draft).
    void resetViewStateAfterScopeChange(bool alsoExitEditing);
    void updatePanelSummary(int idx);
    // The sidebar's eye — reference.html's `<button class="eye"
    // data-toggle-panel=…>` (line 300-303). It is ONE state with the
    // inspector's master chip (the chip is what maskFor() gates on), so
    // this toggles the chip rather than a second flag that could
    // disagree with it.
    void togglePanelVisible(int idx);
    void switchGame(mhw::GameId game);
    // v0.11: View half of the old launchOverlay() — persists the user's
    // current mask + per-panel appearance to disk, builds argv, and hands
    // both to the controller. The spawn/poll/hot-swap half is
    // mhw::OverlayProcessController::launch().
    //
    // Split K4-A3: the write-disk half lives in saveBeforeLaunch() so the
    // four UI entry points below can persist *before* they touch the
    // launching path at all; the argv half is the const builder
    // buildOverlayArgv(). launchOverlayChild() itself is what remains —
    // the running check plus the delegation.
    // The one write that makes a console drag durable, shared by both
    // exits (saveBeforeLaunch() and closeEvent()). See the comment on the
    // definition for why there is one loop instead of two.
public:
    // Exposed so a test can perform the SAME write the console does on its
    // two exits. closeEvent() is not reachable offscreen — a window that was
    // never mapped does not close — so a test that waits for it would hang
    // on a green build and prove nothing.
    void saveAllSplitAndBaseWindows();

private:

    void saveBeforeLaunch();
    [[nodiscard]] QStringList buildOverlayArgv(bool editMode) const;
    void launchOverlayChild(bool editMode);
    // Read-only forward to the controller's copy of the target game.
    [[nodiscard]] mhw::GameId currentGame() const;
    void refreshAutoDetect();
    // v0.11: re-style the START/STOP toggle + status badge for a liveness
    // state. Called from the controller's runningChanged signal and from
    // retranslateUi(). Declared PUBLIC above (D3); the body is unchanged.
    void syncAppearance(int idx);
    void resetPanel(int idx);
    void rebuildAndRender(int idx);
    void updatePosLabel(int idx);
    [[nodiscard]] Panel *panelAt(int idx) const;
    QPixmap renderPreview(Panel *p);
    // v0.5.6 polish: animated show/hide of the bottom canvas stage.
    // animStageTo(true) restores the splitter sizes stored in
    // savedStageSize_, animStageTo(false) collapses the stage to 0 and
    // lets the top row (rail + inspector) fill the whole window.
    void animStageTo(bool visible);

    // ---- v0.9 i18n (EN/CH switch) -----------------------------------
    // Every user-visible string in the console is registered here at
    // construction time and replayed by retranslateUi():
    //
    //   trSet(widget, key)   static copy (QLabel / QAbstractButton text)
    //   trTip(widget, key)   tooltips
    //   trHook(lambda)       anything that needs formatting, a state
    //                        check or a non-Text property (combo item
    //                        text, state-dependent captions, SectionRow
    //                        relabelling, window title, …)
    //
    // Registration applies the string IMMEDIATELY (so construction-time
    // text is localized too) and remembers it for the next replay.
    void trSet(QWidget *widget, const QString &key);
    void trTip(QWidget *widget, const QString &key);
    void trHook(std::function<void()> fn);
    void trWindowTitle(const QString &key);

    // Replay every registered string, then refresh the dynamic elements
    // (badges, counts, summaries, position readout, preview pixmaps) by
    // re-running their normal refresh paths — no duplicated format
    // strings. Called after a successful locale load.
    void retranslateUi();

    // StringTable::load() + retranslateUi() (no persistence).
    void applyLocale(const QString &locale);
    // applyLocale() + write the conf `locale=` row for the overlay's poll
    // (the console is the only writer — see core/locale_conf.h).
    void switchLocale(const QString &locale);

    void updateLocaleChip();
    void updateThemeChipText();
    void updateStageToggleText();
    void updateZoomLabel();

    // v0.14: destroy and rebuild the four mother panels against the panel
    // settings scope in force RIGHT NOW.
    //
    // WHY THIS EXISTS. A Panel reads its QSettings namespace exactly once,
    // inside its constructor — Panel::loadConfig() -> settingsGroup(),
    // which resolves to `[<game>/<mode>/<key>]`. Construct-then-repoint
    // is therefore a no-op for every number the namespace decides
    // (margins, scale, opacity, floors, lock, the columns dial): by the
    // time the constructor returns they are members of a live widget, and
    // setGameForDemo() only changes WHAT the panel paints.
    //
    // The split preview already knew this (syncSplitPreviewToCanvas()
    // destroys and rebuilds its host on a game change for exactly this
    // reason, see its comment). The four mother panels did not: they were
    // constructed once in the ctor and switchGame() retargeted only their
    // demo seed. So the user got a Rise preview showing World's geometry
    // — «整块模式下 Rise 预览不同步» — which is the same bug one layer
    // down, and the same fix.
    //
    // Caller contract: the panel-settings game and mode must already be
    // in force (this method does not set them) and the caller must
    // rebuildAndRender() afterwards. Rebound sources are refreshed here
    // because the canvas holds raw pointers for the whole session.
    // v0.14.1: the ctor calls these two, in this order, with the four
    // buildInspector() calls BETWEEN them. A rail click calls
    // rebuildMotherPanels(), which is all three at once.
    //
    // They are separate because each needs something the other produces:
    // buildInspector() reads the panels, rebuildMotherPanels() adopts the
    // PanelCtl the inspector fills. See the ctor's note for the full
    // argument and for the null-deref that ordering mistake produced.
    void constructMotherPanels();
    void adoptMotherPanels();
    void rebuildMotherPanels();

    // Repoint the HudCanvas's four bindings after the panels they describe
    // were destroyed. The canvas stores a non-owning const PanelSource*, so
    // replacing the panels leaves it holding freed pointers — the same
    // refresh the split-preview host performs on a rebuild.
    void rebindMotherPanels();


    // Source of the four pre-paint masks and the master toggles.
    PlayerPanel *player_ = nullptr;
    MonsterPanel *monster_ = nullptr;
    DamagePanel *damage_ = nullptr;
    PetDamagePanel *pets_ = nullptr;
    std::array<PanelCtl, mhw::kPanelCount> ctl_{};
    // v0.11.4: ownership for the HudCanvas's geometry sources.
    //
    // The ctor binds one of these per slot: canvas_->bindPanel(i, new
    // PanelSourceAdapter(panelAt(i))). The canvas keeps a non-owning
    // const PanelSource* (that is its documented contract — see
    // ui/hud_canvas.h bindPanel), so it never deletes them and nothing
    // else did either. The four adapters therefore leaked for the whole
    // process, and because each held a raw Panel*, the only thing that
    // kept them from dangling was an unstated ~ControlPanel ordering
    // guarantee: delete the four preview panels first and the adapters
    // never touched again afterwards.
    //
    // Holding them here in unique_ptrs makes the ownership explicit and
    // deterministic — the adapters die with the console, and the
    // member-declaration order below (declared after ctl_, so destroyed
    // BEFORE ctl_/player_/... in reverse order) still puts them after
    // the panel hand-off in ~ControlPanel as before. They stay
    // heap-allocated because the canvas holds a raw pointer to them and
    // must never see a stack object that outlives the binding.
    std::array<std::unique_ptr<PanelSourceAdapter>, mhw::kPanelCount> boundSources_{};

    // v0.12 sub-windows: mhw::SubWindowRegistry subWindows_ and
    // int selectedSubWindow_ were removed here together with the
    // register/unregister API above — the registry was constructed,
    // never filled and never read (zero external callers), so the
    // console paid a QVector + adapters_ member for nothing. See the
    // note at the top of the class for why the pilot window does not
    // need a console slot. src/ui/panel_sections.h now carries the
    // live slot concept (PlayerSection::PlayerSplitSlot /
    // kPlayerSplitTargets) and does not go through this console.

    // L3: handles for the EDIT MODE block launcher buttons (one START,
    // one ENTER EDIT). Stored as plain members — not in ctl_ — because
    // they're not per-panel, just per-window.
    QPushButton *startBtn_ = nullptr;
    QPushButton *editBtn_  = nullptr;
    // D3: 在覆盖层中编辑 — the topbar's REAL `--edit` launch. It is a member
    // (it used to be a buildTopbar local) for one reason: the enable state
    // has to follow the overlay's liveness from three places — the launched
    // handler, the exited handler and setOverlayRunning() — and a widget
    // nobody can name after construction is a widget that silently stays
    // clickable while a second copy would be refused (the refusal was a
    // bare `return`, so the click looked broken rather than forbidden).
    QPushButton *overlayEditBtn_ = nullptr;
    // v0.6 Phase 4: World/Rise game selector at the top of the rail.
    QPushButton *gameWorldBtn_ = nullptr;
    QPushButton *gameRiseBtn_  = nullptr;
    // v0.6 Phase 5: live auto-detect chip beside the game selector.
    QLabel *autoDetectBadge_ = nullptr;
    QStackedWidget *inspectorStack_ = nullptr;
    HudCanvas *canvas_ = nullptr;
    QPushButton *safeAreaBtn_ = nullptr;
    QPushButton *gridBtn_ = nullptr;
    QPushButton *themeBtn_ = nullptr;
    QPushButton *stageToggleBtn_ = nullptr;
    // v0.9 i18n: EN/CH language chip (stage bar, left of the theme chip).
    // localeChip_ is the click target; the two segment labels are
    // mouse-transparent and highlighted through the `active` property.
    QFrame *localeChip_    = nullptr;
    QLabel *localeZhLabel_ = nullptr;
    QLabel *localeEnLabel_ = nullptr;
    // i18n replay registries — filled during construction (see trSet /
    // trTip / trHook), replayed by retranslateUi(). QPointer so a widget
    // deleted early (none today, but the panels own children too) can't
    // turn a replay into a dangling-pointer crash.
    QVector<QPair<QPointer<QWidget>, QString>> trPairs_;
    QVector<QPair<QPointer<QWidget>, QString>> trTips_;
    QVector<std::function<void()>>             trHooks_;
    // v0.7.5: visible zoom controls on the stage bar (the canvas always
    // supported Ctrl+wheel; the buttons make it discoverable in the
    // preview console).
    QPushButton *zoomOutBtn_  = nullptr;
    QPushButton *zoomInBtn_   = nullptr;
    // v1.0.1 (验收项5): 复位到 1.0 的第四个缩放入口（− / % / + / 1:1）。
    QPushButton *zoomFitBtn_  = nullptr;
    QLabel      *zoomLabel_   = nullptr;
    // v1.0.0: the rail footer's SETTINGS button. A no-op placeholder this
    // round — the settings popup (Rise REFramework card, appearance,
    // language) is not part of the layout rework — but the shell is final,
    // so the button exists, is styled and is visible.
    QPushButton *settingsBtn_ = nullptr;

    // ─── v1.0.0 sa-A: the prototype's sidebar + topbar ──────────────────
    //
    // The HTML prototype (console-single-display.html) is now the truth
    // source for the console's chrome. What follows is the widget set the
    // prototype's .side / .topbar / .statusline need and the old shell did
    // not have. Every one of them is built in the ctor, in one block
    // marked `/* ── v1.0.0 sa-A: sidebar ── */` or
    // `/* ── v1.0.0 sa-A: topbar ── */`.
    //
    // WHY THESE ARE MEMBERS AND NOT FILE-LOCAL STATICS. The overview's nav
    // card (s_overviewNav) is file-local because it was added under a
    // no-header rule and nothing needs it from outside the .cpp. This round
    // is allowed to touch the header, and these need more than a pointer:
    // a status line that several state changes rewrite, a demo-state combo
    // that gates the status line's text, and a screen combo the display
    // switch owns. Members are also what lets sa-D wire the same handles
    // without a second lookup table.
    //
    // v1.0.0 (sa-D): THE SCREEN COMBO HAS BOTH. screenCombo_ is the member
    // the toolbar owns; s_screenCombo (in the .cpp) is the file-local alias
    // the two overview toggles read, because a free function has no object
    // to call a member accessor on. Same widget, two spellings — see the
    // declaration's comment for why that is not the duplicate it looks like.
    //
    // THE SIDEBAR BRAND (three lines, as the prototype spells them):
    //   .eyebrow   "MONSTER CONTROL"  mono 10.5px uppercase --fg3
    //   h1         "Monster Control 控制台"  12.5px --fg
    //   .side-note explains that the preview is demo content and Save writes
    //   the current game × layout scope.
    // The eyebrow is a NEW widget (the old rail had only brand + brandSub);
    // railBrandSub_ is reused for the third line and keeps its objectName
    // so control_l2_smoke's locale-chip assertion still finds a widget
    // whose text flips with the language.
    QLabel *railEyebrow_ = nullptr;
    // The game segment's note line (HTML #game-note): "怪物猎人:崛起 ·
    // 伙伴 UI 可用" — one line per game, so switching games rewrites it.
    QLabel *gameNote_ = nullptr;
    // The four nav rows' window counts (HTML [data-count]) and the
    // overview row's total. The nav FRAMES themselves stay where they
    // were (ctl_[i].navButton / s_overviewNav) — their objectNames are
    // test-visible — so only the count labels are new.
    std::array<QLabel *, mhw::kPanelCount> navCounts_{};
    QLabel *navOverviewCount_ = nullptr;
    // The .side-foot buttons: 设置 / 帮助 / 重置布局 + 导出 JSON.
    // settingsBtn_ (above) is the 设置 entry.
    // v1.1.5: 重置演示 and 保存 were removed — a reload-from-disk beside a
    // reset-to-default, and a save beside closeEvent()'s own write, were
    // both second doors to rooms with one entrance. resetPrototypeScope()
    // and the save functions themselves stay (tests and closeEvent call
    // them directly); only the buttons are gone.
    QPushButton *helpBtn_ = nullptr;
    QPushButton *resetLayoutBtn_ = nullptr;
    QPushButton *resetLayoutAllBtn_ = nullptr;
    QPushButton *importLayoutBtn_ = nullptr;
    QPushButton *exportJsonBtn_ = nullptr;
    // v1.1.5: the export's granularity — 当前面板组 / 当前作用域 / 全部打包.
    QComboBox *exportScope_ = nullptr;

    // THE TOPBAR. Nine controls, in the prototype's order:
    //   layoutSeg_    整块 / 分块   (two exclusive buttons — NOT a combo)
    //   screenCombo   显示器        (the overview's display switch)
    //   demoStateCombo 演示状态      实时 / 离线 / 无数据
    //   instanceCombo 实例          Rise only
    //   localeCombo   语言          中文 / English
    //   densityCombo  分开密度       紧凑 / 标准   split only
    //   connectChip   连接 chip     已连接 / 未连接
    //   editBtn_ / startBtn_  编辑布局 / 启动   (both existed already)
    //
    // THE LAYOUT SWITCH IS TWO BUTTONS, NOT A COMBO. The prototype's
    // #mode-seg is a segmented control, and the Qt shell that preceded it
    // used a combo + apply because `--layout` is frozen for the child's
    // lifetime. That argument still holds for the LAUNCH, so the two
    // buttons keep the apply semantics: clicking the entry that differs
    // from the mode in force commits it through applyLayoutChoice(), which
    // is the same function the old apply button called. The widget that
    // records the intention is gone; the commit path is unchanged.
    //
    // layoutCombo_ / layoutApplyBtn_ KEEP EXISTING. Four test files drive
    // the mode through findChild("layoutCombo") / findChild("layoutApply")
    // (console_split_sync_tests, console_scope_persistence_tests ×2,
    // plus the picker-identification walk that must skip layoutCombo by
    // name). Removing them would break those tests, so they stay
    // constructed — hidden, with the segmented control driving the same
    // state — and layoutComboIndexFor() / layoutComboMode() /
    // updateLayoutApplyEnabled() keep working off them exactly as before.
    // See buildToolbar() for how the two stay in sync.
    QFrame *layoutSeg_ = nullptr;
    QPushButton *layoutSingleBtn_ = nullptr;
    QPushButton *layoutSplitBtn_ = nullptr;
    // v1.1.0: Mixed's segment. Sibling of the two above, checked by the
    // same syncLayoutSegmentToMode() so the topbar can never light 整块
    // while the overlay boots Mixed.
    QPushButton *layoutMixedBtn_ = nullptr;
    QComboBox *screenCombo_ = nullptr;
    QComboBox *demoStateCombo_ = nullptr;
    QComboBox *instanceCombo_ = nullptr;
    QComboBox *localeCombo_ = nullptr;
    QComboBox *densityCombo_ = nullptr;
    QFrame *connectChip_ = nullptr;
    QLabel *connectChipText_ = nullptr;

    // THE STATUSLINE (HTML .statusline): mono 11px --fg2, four states
    // keyed off the demo-state combo and the real process detector.
    //   live    "Monster Hunter {g} · 数据流 12 ms"
    //   offline "演示离线 · 未连接游戏进程"
    //   nodata  "未检测到游戏数据"
    //   stopped "未启动(演示)"          ← the overlay is not running
    //   running "模拟运行中 · PID {p}"   ← the overlay IS running
    // The only REAL data in any of them is mhw::detectGame()'s process
    // detection; the rest is the prototype's demo copy.
    // ─── v1.0.0 fidelity port: the toast ────────────────────────────────
    //
    // The prototype's `.toast` is how every action reports itself: the
    // rail's eye says 面板已显示 / 面板已隐藏, the layout segment says
    // 已切换为分块作用域, the display picker says 当前查看屏幕：DP-1.
    // It is a transient line at the bottom centre, 2.2s, and it is the
    // ONLY feedback the prototype's buttons have — no QMessageBox, no
    // status bar. Qt's console has no equivalent: its buttons were
    // silent, which is why a user could not tell a working control from
    // a no-op.
    //
    // WHY IT IS A MEMBER AND NOT A FILE-LOCAL STATIC. The four entries
    // that raise it (help / reset / settings / export, plus the eye and
    // the display picker) are all member functions, and a static would
    // have to be reached through a lookup table. One member, one owner,
    // destroyed with the window.
    //
    // WHY IT IS A QLABEL AND NOT A QDialog. A dialog steals focus and
    // blocks; the prototype's toast does neither. It is a frameless,
    // mouse-transparent label parented to the central widget, raised
    // above the splitters, and it disappears on a timer — so a click on
    // the board is never swallowed by it.
    //
    // ─── end v1.0.0 toast ─────────────────────────────────────────────
    QLabel *toast_ = nullptr;
    QTimer *toastTimer_ = nullptr;
    // The export dialog's text box, owned by the dialog and null while it
    // is closed. refreshExportPayload() and the copy/download handlers
    // both reach it through this pointer, and both guard on it.
    QPlainTextEdit *exportText_ = nullptr;

    void showToast(const QString &key, const QStringList &args = {});
    void openHelpDialog();
    void openSettingsDialog();
    void openExportDialog();
    void resetPrototypeScope();

    // ── v1.1.5: the layout package round trip ─────────────────────────
    // The shared middle of 重置布局 / 导入: turn a package's rows into
    // Panel state for the scope the console is showing. `focusPanel` is
    // the group the rail has in front (-1 = every group), because the
    // bulk controls in this console already act on "the rows you can see
    // right now" and a reset must not be the one that reaches further.
    [[nodiscard]] Panel *windowForKey(const QString &key);
    void applyLayoutRowTo(Panel *panel, const mhw::LayoutWindowRow &row);
    bool applyLayoutPackage(const mhw::LayoutPackage &pkg, int focusPanel,
                            QString *error);
    // The shipped default, parsed once from :/default-layout.json.
    [[nodiscard]] bool loadShippedDefault(mhw::LayoutPackage &out,
                                          QString *error) const;
    // 重置布局 / 全部重置: the shipped default, applied to the focused
    // group or to every group. `focusPanel` is the same bound the bulk
    // controls use; -1 is the explicit "all of it" case and asks first.
    void resetLayoutToDefault(int focusPanel);
    // The four mask rows, the language row and both per-game plan rows —
    // the part of the configuration that belongs to no single scope. Only
    // 「全部打包」 carries it: a single-group export that also rewrote the
    // masks on import would be a far bigger hammer than the user asked
    // for, and the header says `global:false` so the import knows.
    [[nodiscard]] mhw::LayoutGlobalBlock readGlobalBlockFromConf() const;
    // 导入布局包: pick a file, show what its header says it covers, back
    // the current configuration up, then write ONLY what the file
    // declares. The confirm step names the scopes — a file that turns out
    // to be 「全部打包」 is a much larger action than one scope, and the
    // header is what makes that knowable BEFORE anything is written.
    void importLayoutPackage();
    // Copies the two files an import can touch into
    // <config>/backups/<timestamp>/. Returns an empty string on success,
    // the reason on failure.
    [[nodiscard]] QString backupConfigForImport() const;
    void checkForUpdates();
    void refreshExportPayload();
    // The theme switch, spelled once so the stage bar's chip and the
    // settings dialog's dark/light pair cannot drift apart. See the
    // definition for the four steps and why the canvas is repainted last.
    void applyThemeChange();

    // The statusline, so an action's outcome can also land where the
    // prototype's status line lives.
    QLabel *statusline_ = nullptr;
    QLabel *statusDot_ = nullptr;
    QLabel *statusRun_ = nullptr;

    // ─── v1.0.0 fidelity port: the selected-window state ────────────────
    //
    // THE PROTOTYPE'S OWN ADDRESS, not the shell's. `state.selected` is a
    // WINDOW key — "player-status", "monster-identity", "player" — and the
    // inspector describes whatever it names. The Qt console addressed a
    // PANEL SLOT (selectedPanel_, 0..3), which is why its inspector could
    // only ever describe a mother panel and had nothing to say about a
    // split sub-window.
    //
    // selectedPanel_ STAYS AND KEEPS ITS MEANING: it is which of the four
    // panels the inspector's mask rows / appearance sliders belong to, and
    // four test files drive the layout through it. What is added here is
    // the second axis — the window inside that panel — with -1 meaning
    // "the mother panel itself" so a Single console needs no special case.
    //
    // WHY A PAIR AND NOT A STRING KEY. The canvas's selection is an INDEX
    // (subWindowSelected carries a position in the bindings vector), and
    // an index pair maps onto the model's rows with no lookup. The key is
    // derived when it is needed (the export payload, the settings scope)
    // rather than stored, so a rebuilt host's shifted indices cannot
    // leave a stale string behind.
    int selectedSubWindow_ = -1;   // -1 = the mother panel of selectedPanel_
    // The inspector's own widgets for the selected window. Null until
    // buildInspectorForSelection() has run; every accessor guards.
    QSpinBox *winPosX_ = nullptr;
    QSpinBox *winPosY_ = nullptr;
    QComboBox *winOutput_ = nullptr;
    /* ── 实测缺陷5 (usability-2): WHAT 「目标显示器」 ACTUALLY RESOLVES TO ──
       The combo's empty-data entry is the automatic mode, and its name
       used to be a single fixed 跟随所属面板 — a phrase nobody could map
       onto a screen. The chain behind it is `own → parent → primary`:
       a sub-window with no own outputName inherits its MOTHER PANEL's
       screen, and a mother panel with no own outputName falls back to
       the SYSTEM PRIMARY (there is no parent above it), with an offline
       screen landing on the primary too — see
       mhw::resolveWindowOutput() and screen_query::effectiveFrom().

       So the entry is now named for the case the user is looking at
       (跟随主面板 for a window row, 跟随主屏 for a panel), and
       winOutputHint_ states the screen the chain resolves to RIGHT
       NOW — the answer the user actually wanted from the dropdown.
       ── end 实测缺陷5 ── */
    QLabel *winOutputHint_ = nullptr;
    QSlider *winScale_ = nullptr;
    QSlider *winOpacity_ = nullptr;
    QLabel  *winScaleValue_ = nullptr;
    QLabel  *winOpacityValue_ = nullptr;
    QCheckBox *winVisible_ = nullptr;
    /* ── 验收项5 (B 第1阶段): the capability-driven property rows ─────────
       Built ONCE in buildWindowInspectorFace(), refilled (values +
       visibility) per selection in buildInspectorForSelection(). The
       columns row is a widget rather than a bare layout so it can be
       hidden as a unit when the selected window answers
       supportsColumns() == false — a visible control that cannot act
       would be a control lying about the window. ── */
    QWidget     *winColumnsRow_ = nullptr;
    QSpinBox    *winColumnsSpin_ = nullptr;
    /* ── 实测缺陷8 (user-8): 列数 gets a − / + pair around the box ─────────
       「列数控件加 −/+ 按钮」 — the box stays for exact typing (same
       objectName, same single writer: valueChanged), the buttons only
       step it by Panel::kColumnsStep (=1), so a click can never take a
       different path into the Panel than a typed number does. Both live
       inside winColumnsRow_, so they hide with the row when the selected
       window answers supportsColumns() == false, and they are enabled
       with it (never a live button over a window with no column axis).
       ── end 实测缺陷8 ── */
    QPushButton *winColumnsMinus_ = nullptr;
    QPushButton *winColumnsPlus_ = nullptr;
    QSpinBox    *winMinWSpin_ = nullptr;
    QSpinBox    *winMinHSpin_ = nullptr;
    QSpinBox    *winLockWSpin_ = nullptr;
    QSpinBox    *winLockHSpin_ = nullptr;
    QPushButton *winAutoBtn_ = nullptr;
    /* ── 实测缺陷6 (usability-2): the four size rows are DRAGGABLE ────────
       「这里的细节尺寸我希望能更有交互的调整，而不是手填数值」 — each of
       最小宽度/最小高度/固定宽度/固定高度 is now a QSlider AND its QSpinBox:
       the slider is the interactive half (dragging writes live, the
       current value is read off the spin box right next to it), the box
       stays for exact typing and keeps the objectName every existing
       fixture finds.

       THE TWO WIDGETS ARE ONE FIELD, NEVER TWO: the spin box's
       valueChanged is the ONLY writer to the Panel (see the connections
       in buildWindowInspectorFace), the slider writes by setting the
       spin box, and the spin box writes back with signals blocked — so a
       drag can never produce two writes per step and a refill can never
       leave the two halves disagreeing.

       RANGE IS PER ROW, NOT ONE SHARED CEILING (实测缺陷9 / user-8): the
       two WIDTH rows and the two HEIGHT rows each get their own honest
       maximum — see kSizeRowMaxWidth / kSizeRowMaxHeight in the .cpp,
       with the reasoning — instead of all four spanning 0..4096. The
       shared Panel::maxDialValue() stays the HARD ceiling: a refill
       grows a row's range to fit whatever this window already stores
       (the overlay's edit keys can put a bigger number there), so a
       narrower range can never turn a stored value into a lie.
       ── end 实测缺陷6 ── */
    QSlider     *winMinWSlider_ = nullptr;
    QSlider     *winMinHSlider_ = nullptr;
    QSlider     *winLockWSlider_ = nullptr;
    QSlider     *winLockHSlider_ = nullptr;
    // The inspector's own face (built by buildWindowInspectorFace): the
    // selection chip in the head, the per-window group column and the
    // preview box. The chip and the two column frames are filled on
    // selection; the group column's contents are rebuilt each time
    // because the sibling list is per-window.
    QLabel *selectionChip_ = nullptr;
    // The head's `.sub` line and the content's empty state — both are
    // functions of the SELECTION (reference.html:486 rewrites them in
    // renderInspector), so they need to be reachable after construction.
    QLabel *inspectorHeadSub_ = nullptr;
    // The content half of the face: the scroll box that carries the three
    // columns, and the `.empty-inspector` block that REPLACES it while
    // nothing is selected (reference.html:486 swaps innerHTML for one or
    // the other). QWidget* for both so the header needs no QScrollArea
    // declaration.
    QWidget *contentScrollHost_ = nullptr;
    // round2: the left half's content host (inspectorGridHost) — the card's
    // height budget is read from it (see refreshWindowPreviewPixmap), so it
    // has to be reachable outside buildWindowInspectorFace().
    QWidget *inspectorGridHost_ = nullptr;
    // round2 (左右两半各自独立竖直滚动): the OTHER half's scroll box — the one
    // that carries the 当前内容状态 card. It is hidden and shown TOGETHER
    // with contentScrollHost_ above: with nothing selected both halves
    // leave the face, which is what makes the room the empty block needs.
    QWidget *previewScrollHost_ = nullptr;
    QWidget *inspectorEmpty_ = nullptr;
    // The topbar's `.topbar-title` pair — the two lines the shell's
    // toolbar never had. Kept as members because their text follows the
    // view (refreshTopbarTitle()), so a rail click has to be able to
    // reach them after construction.
    QLabel *topbarTitle_ = nullptr;
    QLabel *topbarSubtitle_ = nullptr;
    // 第6条: no topbar export button — the rail's sideFootExport is the one
    // export entry (see the deleted build site in buildTopbar()).
    // ── v1.1.1 (顶栏溢出): the topbar's wrap state ──────────────────────
    //
    // topbar_ is the QFrame#topbar itself; row1 carries the title block,
    // the two labels, the mode segment and the display combo, row2 the
    // four action buttons right-aligned behind their own stretch. Row2
    // is EMPTY (and its margins are zeroed) while the single row fits,
    // which is what keeps the 66px band pixel-identical to the measured
    // `.topbar` at every width where the prototype itself is one row.
    QFrame *topbar_ = nullptr;
    QHBoxLayout *topbarRow1_ = nullptr;
    QHBoxLayout *topbarRow2_ = nullptr;
    // The four actions, in row order. The buttons are moved between the
    // two rows by refreshTopbarWrap(); holding them here (rather than
    // re-finding them by objectName) keeps the wrap set exactly the set
    // the layout was BUILT with, including the stop-state objectName
    // swap startBtn → stopBtn.
    QVector<QPushButton *> topbarActions_;
    bool topbarWrapped_ = false;
    // The band height the wrap last applied: 66 while the header is one
    // row, ~98 once the buttons have their own row. Kept so a refresh
    // that changes nothing can tell (see refreshTopbarWrap()'s cheap
    // path) without re-activating the layout.
    int topbarBandHeight_ = 66;
    // Re-entrancy guard: refreshTopbarWrap() moves widgets between
    // layouts and re-activates the bar's layout, both of which queue the
    // very events it listens to.
    bool topbarWrapBusy_ = false;
    // True while no saved consoleSplitter state exists, so the FIRST
    // resize can lay the two panes out at the prototype's 2fr : 1fr
    // instead of at Qt's sizeHint ratio. Cleared the moment it runs.
    bool pendingStageRatioDefault_ = false;
    // Set only while that one setSizes() is on the stack. QSplitter
    // emits splitterMoved for a programmatic setSizes() too (the note on
    // the splitterMoved lambda says so from its own animation frames),
    // and the slot it reaches PERSISTS ui/splitter — a derived default
    // must not be written back as if the user had chosen it, so the
    // slot returns on this flag instead.
    bool applyingStageRatioDefault_ = false;
    QFrame *windowGroupCol_ = nullptr;
    // ── v1.1.0 Mixed: the 独立/合并 block inside that column ─────────────
    //
    // BUILT ONCE (the registry has eleven rows and none of them is a
    // function of the selection) and re-attached to the group column's
    // layout on every refill, which is why the refill detaches it before
    // it purges the old layout's widgets — the purge DELETES what it
    // takes, and this block has to survive it.
    //
    // Raw pointers, like every other widget member here: the block is a
    // child of windowGroupCol_, so it lives exactly as long as the
    // column does, and neither is ever rebuilt while the console runs.
    QFrame *mixedGroupHost_ = nullptr;
    QVector<QCheckBox *> mixedGroupToggles_;    // registry order, 11
    QVector<QLabel *> mixedGroupStateLabels_;   // registry order, 11
    // One plan per game, defaulting to Mixed's own visible fallback (ALL
    // rows detached) so a plan is never "empty" before the conf read has
    // run — an absent row means all-detached to the overlay too, and a
    // half-loaded console must not describe the Single topology while
    // claiming Mixed. applyStartupLayoutMode() replaces both from disk.
    std::array<mhw::WindowLayoutPlan, 2> mixedPlans_{
        mhw::WindowLayoutPlan::allDetached(),
        mhw::WindowLayoutPlan::allDetached()};
    QFrame *windowPreviewCol_ = nullptr;
    QLabel *windowPreviewBox_ = nullptr;
    /* ── 验收项1 (B 第1阶段): THE DETAIL VIEWPORT, INDEPENDENT OF SCALE ──
       windowPreviewBox_ keeps its QLabel / setPixmap contract and its
       objectName (the QA fixtures find it), but what it DISPLAYS is now
       a derived pixmap: windowPreviewSource_ is the immutable render and
       previewZoom_ 0 means "Fit it into the viewport". The old behaviour
       — one render size, scroll to see the rest — is still reachable,
       via 1:1 (previewActualBtn_) or any zoom step.

       NONE OF THIS IS A Panel WRITE. setPreviewZoom() only re-derives
       the label's pixmap; Panel::scale() and the persisted `scale=` row
       are untouched, which is the "独立于真实HUD scale" half of the
       acceptance item.
       ── end 验收项1: detail viewport members ── */
    QScrollArea  *windowPreviewScroll_ = nullptr;
    QLabel       *previewZoomLabel_ = nullptr;
    QLabel       *previewSizeLabel_ = nullptr;
    QPushButton  *previewFitBtn_ = nullptr;
    QPushButton  *previewActualBtn_ = nullptr;
    QPushButton  *previewZoomInBtn_ = nullptr;
    QPushButton  *previewZoomOutBtn_ = nullptr;
    QPixmap       windowPreviewSource_;   // the render, never scaled in place
    qreal         previewZoom_{0.0};      // 0 = Fit, 1.0 = 100%, >0 = ratio
    /* ── 实测缺陷2 (usability-2): THE DEFAULT IS NOT "FILL THE BOX" ──────
       The old default was previewZoom_ == 0, i.e. Fit — and Fit is
       CAPPED AT 1:1 yet still ends up touching the pane's edges for
       every real panel: measured offscreen, the four mother panels
       render 378×361 / 380×345 / 380×337 / 380×337 into a 444×318
       viewport, so Fit lands at 88–94% and the HUD window fills the
       preview frame exactly. That is what the user reported as
       「这里这个太大了」: the preview looked like a zoomed crop of a
       giant window rather than a window.

       previewMarginDefault_ keeps the OLD default reachable-and-named
       (the 适应窗口 button still means exactly Fit) while the cold
       start shows previewDefaultScale() instead — the Fit ratio
       scaled to 75%, so every axis keeps a margin and no source is
       ever enlarged past 1:1. The flag is dropped by the first
       explicit zoom of any kind (Fit / 实际大小 / − / + / Ctrl+wheel)
       and by nothing else, so a selection change keeps the user's
       chosen zoom exactly as previewZoom_ always did.
       ── end 实测缺陷2: the default-view flag ── */
    bool          previewMarginDefault_{true};
    // ── v1.0.0 (C): the per-selection column's own widgets ─────────────
    // `.object-title` (window name + "panel · resolved output"), the
    // coordinate `.note` under the position fields, the `on/total`
    // count above the section list, and the host frame the section rows
    // are rebuilt into.
    QLabel *winObjectTitleName_ = nullptr;
    QLabel *winObjectTitleMeta_ = nullptr;
    QLabel *winPosNote_ = nullptr;
    QLabel *winContentCount_ = nullptr;
    QFrame *windowSectionListHost_ = nullptr;
    // Encodes which window the section rows in the host were built for
    // (panel * 1000 + subIndex, -1 = none), so a refill that follows a
    // mask change only re-checks the rows instead of deleting the
    // SectionRow that is emitting its own toggled() signal.
    int groupSectionKey_ = -1;
    // True while refreshWindowModel() is writing into those widgets, so
    // the valueChanged handlers can tell a programmatic refill from a
    // user edit. A signal blocker would do the same job per widget; one
    // flag covers the whole refill and is what the "do not steal a
    // focused input's value" rule needs (see the position handler).
    bool windowModelRefilling_ = false;

    // -- v1.0.0 (D): the stage's .shell-head -----------------------------
    // reference.html:325-328 -- h2 + sub on the left, a growing gap, then
    // the screen chip and the window-count chip. Members (not locals) so
    // refreshStageHead() can rewrite them whenever the display or the
    // window list changes.
    QLabel *stageTitle_ = nullptr;
    QLabel *stageSub_ = nullptr;
    QLabel *stageScreenChip_ = nullptr;
    QLabel *stageWindowCountChip_ = nullptr;

    // -- v1.0.0 (B): WINDOW selection is a separate axis from the PANEL --
    // selectedPanel_/selectedSubWindow_ describe "which row of which
    // panel" and are ALSO read as the old stack's page index, so they
    // could not answer "is a window selected?" -- selecting panel 0 (the
    // ctor does, and so does every rail click) looked exactly like a user
    // clicking the player panel's box. This flag is set ONLY by
    // selectWindow(); resetViewStateAfterScopeChange() clears it together
    // with the panel slot (there is no separate "clear the window half"
    // helper any more — D2: the one that existed was never called);
    // selectedWindow() is invalid while it is false.
    bool windowSelectionActive_ = false;

    // v1.1.0: "a panel slot was explicitly chosen by the user". Distinct from
    // `selectedPanel_ != -1`, which is true from the first frame because the
    // member defaults to 0: the inspector's empty state has to survive a cold
    // start AND still be able to show a mother panel's own size/sliders once
    // the user picks one. Both selections yield a window state with no
    // panelIndex, so nothing downstream can tell them apart without this.
    bool panelSlotChosen_ = false;

    // v1.1.0: consumed by the next selectPanel() to mean "this one was a
    // consequence, not a click". One-shot and self-clearing so it can never
    // leak into a later genuine selection.
    bool panelRedirect_ = false;

    // The demo state in force, mirrored from demoStateCombo_ so the
    // status line can be rewritten without a combo lookup. 0 = live,
    // 1 = offline, 2 = noData — the combo's index order.
    int demoState_ = 0;
    // The last game detectGame() saw, so the status line's "已连接 /
    // 未连接" half reflects the same fact the rail badge does.
    bool gameDetected_ = false;
    // v1.0.0 (sa-D): the pid that came with it. -1 when nothing is
    // running, which is also what GameDetection's default would be; the
    // instance picker prints it, and the connect chip deliberately does
    // not (the badge names the process, the chip says 已连接).
    qint64 lastDetectedPid_ = -1;

    int selectedPanel_ = 0;

    // L4: status badge in the top-right, shows "READY" by default and
    // flips to "RUNNING pid NNNN since HH:MM:SS" while the overlay is
    // alive. Flipped back to "READY" when the controller emits exited().

    QLabel *statusBadge_ = nullptr;

    // v0.13: the Single/Split selector in the rail's WORKSPACE block.
    // layoutCombo_ is the choice; layoutApplyBtn_ is the "commit and
    // (re)start" button. Neither one alone is a complete control:
    // pick-only would silently change nothing (the mode is frozen for
    // the child's lifetime), and click-only would restart an overlay
    // the user is watching with no prompt to review the choice. Widget
    // pointers only — all persistence goes through applyLayoutChoice().
    QComboBox *layoutCombo_   = nullptr;
    QPushButton *layoutApplyBtn_ = nullptr;
    // What the last LAUNCH used. The combo's current text is only a
    // PENDING selection; this is what the child is actually running in,
    // and what applyLayoutChoice() writes before it restarts anything.
    //
    // The enum's full definition comes from the ui/panel_sections.h
    // include at the top of this header. An `enum class X : int;` forward
    // declaration would NOT do: it names the type, but this member's
    // default initializer needs PlayerLayoutMode::Single, and a scoped
    // enum's enumerators only exist where the enum is defined.
    mhw::PlayerSection::PlayerLayoutMode layoutModeInForce_ =
        mhw::PlayerSection::PlayerLayoutMode::Single;

    // v0.13: the split's sub-window preview. Held by unique_ptr because
    // the class is defined in the .cpp (it needs every split window's
    // complete type) and ControlPanel's destructor here cannot see its
    // definition to destroy an inline member. Pushed into the canvas
    // whenever the mode is in force AND the mode changes; never read in
    // Single mode, where the canvas's subSlots_ is empty by contract.
    //
    // v0.13 (the rail-switch fix): the host is ALSO rebuilt when the GAME
    // changes, because a Panel reads its game-scoped QSettings group
    // inside its constructor and no second read ever runs — so re-pointing
    // the namespace cannot reach a window that already exists, and the
    // only honest update is a new one.
    // splitPreviewHostGame_ records which game the current host was built
    // for, so syncSplitPreviewToCanvas() can tell "no host" apart from
    // "host for the OTHER game" apart from "host for this game". The
    // sentinel is World, which is both the namespace default and the game
    // a fresh console starts on when nothing has been built yet.
    mhw::GameId splitPreviewHostGame_{mhw::GameId::World};
    // v1.1.3 (Mixed 画布): WHAT ELSE THE HOST WAS BUILT FOR.
    //
    // The Split host is the whole split table — eleven windows, built
    // once, and game was the only thing that could invalidate it. The
    // Mixed host is a FILTERED set: one Panel per row this run's plan
    // detached, so the same "can I reuse this host" guard needs two more
    // answers.
    //
    //   * the MODE — a Mixed host must never be handed to Split (it is
    //     missing rows) nor a Split host to Mixed (it has rows the plan
    //     merged back into their mother);
    //   * the PLAN — toggling one row's 独立/合并 changes WHICH windows
    //     the host owns, so the plan's detached bitmap is part of the
    //     host's identity.
    //
    // Recorded at build time beside splitPreviewHostGame_; any mismatch
    // is what makes needsRebuild true. That comparison is the ONLY thing
    // that ever rebuilds the host, so a drag, a paint or a visibility
    // refresh never does — a Mixed sync with an unchanged (game, mode,
    // plan) is as cheap as a Split one.
    mhw::PlayerSection::PlayerLayoutMode splitPreviewHostMode_ =
        mhw::PlayerSection::PlayerLayoutMode::Single;
    // Bit i of the registry (kWindowLayoutRows) detached at build time;
    // 0 means "no plan was in force", which is also what an empty plan
    // answers, so an all-merged host and a never-built one are told
    // apart by `!splitPreview_` rather than by this number.
    quint32 splitPreviewHostPlan_{0};
    // The host is created, destroyed and re-created with the rail: a
    // sub-window reads its game-scoped QSettings group inside its
    // constructor, so re-pointing the namespace cannot reach a window that
    // already exists. The only thing that does get asked about it from
    // outside is the test that asserts a rebuilt host actually re-seeds
    // its windows — the whole "switch the rail and the preview follows"
    // contract, which nothing else covers (see the note in
    // split_game_seed_dispatch_tests.cpp). It goes through the public
    // accessor below rather than reaching in here.
    std::unique_ptr<SplitWindowPreviewHost> splitPreview_;

public:
    // A split-preview window by settings key, or nullptr. Non-owning.
    //
    // WHY THIS EXISTS. SplitWindowPreviewHostImpl is private to
    // control_panel.cpp, so the `Panel` it builds cannot be named from
    // outside — and the one observable that proves the rail's contract,
    // whether a window's DEMO SEED followed the switch, is not visible in
    // pixels: two canvases differ from the geometry rebuild alone, which is
    // exactly how that dispatch shipped dead for two releases while both
    // console suites stayed green (see split_game_seed_dispatch_tests.cpp).
    // A mantles window is empty under Rise and full under World, so this
    // hands back an object whose hasVisibleContent() reads the seed.
    //
    // Keyed rather than indexed: the split table's row order is free to
    // change, and a caller holding an index would start reading a different
    // window the day it does.
    [[nodiscard]] Panel *splitPreviewWindow(const QString &key) const;

    // The game a split-preview window's DEMO is currently seeded for, or
    // nil when no such window exists.
    //
    // This is the observable the canvas cannot supply. Two canvases built
    // for different games differ because every window re-reads
    // [game/mode/key] and lands somewhere new - the GEOMETRY rebuild - not
    // because any window's game_ moved. So a canvas comparison passes
    // whether or not the seed dispatch works, which is how twelve dead
    // overloads shipped in v0.13 and v0.14 with both console suites green.
    // Asking the window itself, and re-asking after every rail click
    // because the host is destroyed and rebuilt, reads the cause rather
    // than a symptom.
    [[nodiscard]] mhw::GameId splitPreviewDemoGame(const QString &key,
                                                   bool *found) const;

    // v0.6 Phase 5: most recent successful /proc scan (persisted under the
    // "detectedGame" QSettings key). Refreshed every 5s by the live badge.
    mhw::GameId  lastDetectedGame_ = mhw::GameId::World;

    // Process lifecycle is owned by the controller (overlay pid, the
    // 250 ms kill(pid,0) poll, the pending hot-swap restart and the
    // --game value). hot-swap keeps its own copy of the last argv and
    // edit flag (mhw::OverlayProcessController::lastArgv_/lastEditMode_),
    // so the View holds no argv state of its own.
    mhw::OverlayProcessController *overlay_ = nullptr;

    // Rise-only REFramework card. The presentation half (widgets, status
    // lines, the four buttons' enablement) lives in RiseReframeworkCard;
    // the request signals and the result slot above stay here — they are the
    // seam src/main_control.cpp connects to. So does the 1500 ms timer:
    // retranslateUi() and switchGame() refresh the card on their own.
    RiseReframeworkCard *riseCard_ = nullptr;
    QTimer *riseReframeworkRefreshTimer_ = nullptr;

    // v0.5.6 polish: animated stage toggle. savedStageSize_ captures the
    // user-chosen (or default 45/55) stage height when the user hides
    // the canvas; stageAnim_ is the QPropertyAnimation that drives the
    // smooth open/close. stageVisible_ is the logical state.
    int                 savedStageSize_ = 0;
    bool                stageVisible_   = true;
    QSplitter          *consoleSplitter_ = nullptr;
    // v0.7.1: horizontal splitter that splits the left edge of the window
    // into [narrow game column | original rail]. Sized 80:320 by default
    // and persisted through QSettings under ui/leftSplitter.
    QSplitter          *leftSplitter_   = nullptr;
    QPropertyAnimation *stageAnim_      = nullptr;
    // backing storage for the Q_PROPERTY — the actual animated value.
    int                 stagePaneHeight_ = 0;
};
