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

#include <QMainWindow>
#include <QPointer>
#include <QSlider>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <array>
#include <functional>
#include <memory>
#include <QtGlobal>

class QSplitter;
class QPropertyAnimation;

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QPushButton;
class QStackedWidget;
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
    static constexpr int kLayoutComboSingleIndex = 0;
    static constexpr int kLayoutComboSplitIndex  = 1;

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
    // v0.7.1: dedicated narrow column for the World/Rise game selector.
    // Returns a QWidget the caller parents to the horizontal QSplitter
    // on the left edge of the window; the existing rail sits to its
    // right and keeps its HUD OBJECTS / WORKSPACE rows unchanged.
    QWidget *buildGameColumn();
    void selectPanel(int idx);
    void updatePanelSummary(int idx);
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
    void saveBeforeLaunch();
    [[nodiscard]] QStringList buildOverlayArgv(bool editMode) const;
    void launchOverlayChild(bool editMode);
    // Read-only forward to the controller's copy of the target game.
    [[nodiscard]] mhw::GameId currentGame() const;
    void refreshAutoDetect();
    // v0.11: re-style the START/STOP toggle + status badge for a liveness
    // state. Called from the controller's runningChanged signal and from
    // retranslateUi().
    void setOverlayRunning(bool running);
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
    QLabel      *zoomLabel_   = nullptr;
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
    std::unique_ptr<SplitWindowPreviewHost> splitPreview_;

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
