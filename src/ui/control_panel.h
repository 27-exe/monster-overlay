#pragma once

#include <QMainWindow>
#include <QStringList>
#include <QVector>
#include <array>

class QCheckBox;
class QFrame;
class QLabel;
class QPushButton;
class QWidget;
class Panel;
class PlayerPanel;
class MonsterPanel;
class DamagePanel;
class ToggleChip;
class SectionRow;

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

private:
    struct PanelCtl {
        Panel *panel = nullptr;
        ToggleChip *master = nullptr;
        // subs[i] corresponds to bit (1u << i), matching the order of
        // mhw::*Section::names() in panel_sections.h.
        QVector<SectionRow *> subs;
        QLabel *preview = nullptr;
    };

    QWidget *buildGroup(const QString &title, const QString &sub,
                        const QStringList &labels, int idx);
    QWidget *buildRule();
    QWidget *buildEditModeBlock();
    void rebuildAndRender(int idx);
    QPixmap renderPreview(Panel *p);

    PlayerPanel *player_ = nullptr;
    MonsterPanel *monster_ = nullptr;
    DamagePanel *damage_ = nullptr;
    std::array<PanelCtl, 3> ctl_{};
};
