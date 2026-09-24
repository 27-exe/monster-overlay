#pragma once

#include "core/game_snapshot.h"
#include "ui/panel_source.h"

#include <QMainWindow>
#include <QPointer>
#include <QSlider>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <array>
#include <functional>
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

private:
    // L2: persistent mask state lives at ~/.config/monster-overlay/monster-overlay.conf
    // so the user's last toggle choices survive across console restarts. The
    // console writes on exit (and any time we explicitly call saveMask());
    // reads happen once at construction so the checkboxes open with the
    // previous session's state.
    void loadMaskFromDisk();
    void saveMaskToDisk() const;

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
    void launchOverlay(bool editMode);
    void stopOverlay();
    void switchGame(mhw::GameId game);
    void restartOverlayWithCurrentGame();
    void refreshAutoDetect();
    void onOverlayExited();
    void setOverlayRunning(bool running);
    void refreshRiseReframeworkStatus();
    void requestRiseReframeworkInstall();
    void requestRiseLuaRemoval();
    void requestRiseMenuStateFix(bool restoreDefault);
    void requestRiseReframeworkRemoval();
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


    PlayerPanel *player_ = nullptr;
    MonsterPanel *monster_ = nullptr;
    DamagePanel *damage_ = nullptr;
    PetDamagePanel *pets_ = nullptr;
    std::array<PanelCtl, mhw::kPanelCount> ctl_{};

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
    // alive. Flipped back to "READY" by onOverlayExited().
    QLabel *statusBadge_ = nullptr;

    // L3: state for the running monster-overlay subprocess. overlayPid_ is
    // 0 when nothing is running; overlayWatch_ fires every 250ms while
    // a process is alive, polling kill(pid,0) for liveness.
    qint64       overlayPid_ = 0;
    QTimer      *overlayWatch_ = nullptr;

    // v0.6 Phase 4: currently selected game (drives the --game flag passed
    // to the overlay subprocess). Pending restart: when the user switches
    // game while the overlay is running we SIGTERM it and relaunch with the
    // new --game once onOverlayExited() observes the exit.
    mhw::GameId  currentGame_ = mhw::GameId::World;
    bool         pendingRestart_ = false;
    // v0.6 Phase 5: most recent successful /proc scan (persisted under the
    // "detectedGame" QSettings key). Refreshed every 5s by the live badge.
    mhw::GameId  lastDetectedGame_ = mhw::GameId::World;

    // Rise-only REFramework card. Install/removal is delegated through the
    // request signals above; these handles only own presentation/state.
    QFrame *riseReframeworkCard_ = nullptr;
    QLabel *riseReframeworkStatus_ = nullptr;
    QPushButton *installRiseReframeworkButton_ = nullptr;
    QPushButton *removeRiseLuaButton_ = nullptr;
    QPushButton *menuStateFixButton_ = nullptr;
    QPushButton *menuStateRestoreButton_ = nullptr;
    QLabel *menuStateStatus_ = nullptr;
    QPushButton *removeRiseReframeworkButton_ = nullptr;
    QTimer *riseReframeworkRefreshTimer_ = nullptr;
    QString riseGameDir_;
    QString riseReframeworkResultDetail_;
    bool riseReframeworkOperationPending_ = false;
    bool riseReframeworkHasResult_ = false;
    bool riseReframeworkResultOk_ = false;

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
