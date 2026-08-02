#pragma once

#include <QMainWindow>
#include <QSlider>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <array>
#include <QtGlobal>

class QCheckBox;
class QFrame;
class QLabel;
class QPushButton;
class QStackedWidget;
class QWidget;
class Panel;
class PlayerPanel;
class MonsterPanel;
class DamagePanel;
class ToggleChip;
class SectionRow;
class SectionCountBar;
class HudCanvas;

// Standalone control console for the MHW overlay. NOT a layer-shell
// surface — a plain QMainWindow the user can move, focus and close like
// any app. It owns three real overlay panel instances rendered off-screen
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
    explicit ControlPanel(QWidget *parent = nullptr);
    ~ControlPanel() override;

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    // L2: persistent mask state lives at ~/.config/MHW Overlay/mhw-overlay.conf
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
    };

    QWidget *buildInspector(const QString &title, const QString &sub,
                            const QStringList &labels, int idx);
    QWidget *buildObjectButton(const QString &letter, const QString &title,
                               const QString &summary, int idx);
    QWidget *buildRule();
    QWidget *buildEditModeBlock();
    void selectPanel(int idx);
    void updatePanelSummary(int idx);
    void launchOverlay(bool editMode);
    void stopOverlay();
    void onOverlayExited();
    void setOverlayRunning(bool running);
    void syncAppearance(int idx);
    void resetPanel(int idx);
    void rebuildAndRender(int idx);
    void updatePosLabel(int idx);
    QPixmap renderPreview(Panel *p);

    PlayerPanel *player_ = nullptr;
    MonsterPanel *monster_ = nullptr;
    DamagePanel *damage_ = nullptr;
    std::array<PanelCtl, 3> ctl_{};

    // L3: handles for the EDIT MODE block launcher buttons (one START,
    // one ENTER EDIT). Stored as plain members — not in ctl_ — because
    // they're not per-panel, just per-window.
    QPushButton *startBtn_ = nullptr;
    QPushButton *editBtn_  = nullptr;
    QStackedWidget *inspectorStack_ = nullptr;
    HudCanvas *canvas_ = nullptr;
    QPushButton *safeAreaBtn_ = nullptr;
    QPushButton *gridBtn_ = nullptr;
    int selectedPanel_ = 0;

    // L4: status badge in the top-right, shows "READY" by default and
    // flips to "RUNNING pid NNNN since HH:MM:SS" while the overlay is
    // alive. Flipped back to "READY" by onOverlayExited().
    QLabel *statusBadge_ = nullptr;

    // L3: state for the running mhw-overlay subprocess. overlayPid_ is
    // 0 when nothing is running; overlayWatch_ fires every 250ms while
    // a process is alive, polling kill(pid,0) for liveness.
    qint64       overlayPid_ = 0;
    QTimer      *overlayWatch_ = nullptr;
};
