#include "control_panel.h"

#include "ui/panel.h"
#include "ui/panel_player.h"
#include "ui/panel_monster.h"
#include "ui/panel_damage.h"
#include "ui/panel_sections.h"
#include "ui/toggle_chip.h"
#include "ui/section_row.h"
#include "ui/section_count_bar.h"
#include "ui/hud_canvas.h"
#include "ui/panel_source.h"
#include "core/string_table.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QScrollArea>
#include <QSlider>
#include <QMouseEvent>
#include <QFrame>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QGridLayout>
#include <QPixmap>
#include <QPainter>
#include <QFrame>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>
#include <QTime>
#include <signal.h>
#include <sys/types.h>

namespace {

QString qssBase()
{
    // Token palette mirrors the HTML control mock (v0.4) so this QSS can
    // be re-tuned in one place. Roles:
    //   - bg          : window background (#0a0c0d, near-black)
    //   - fg          : primary text (#e7e8e9)
    //   - mute        : secondary text / disabled labels (#6f7375)
    //   - accent      : brand orange (left-edge bar, master toggled dot)
    //   - teal        : "connected / on" indicator (logo dot, toggled rail)
    //   - rule        : hairline separator between groups
    //   - panel chrome: live preview tile border / background
    return QStringLiteral(
        "QWidget{background:#0a0c0d;color:#e7e8e9;}"
        "QMainWindow{background:#0a0c0d;}"
        "QGroupBox{color:#fff;border:none;border-radius:0;"
        " margin-top:0;padding:14px 0 12px 0;font-family:'Chakra Petch';"
        " font-weight:600;letter-spacing:1px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:0;padding:0;color:#e7e8e9;}"
        "QCheckBox{color:#d3d4d5;spacing:8px;font-family:'Noto Sans SC';font-size:12px;}"
        "QCheckBox::indicator{width:14px;height:14px;border:1px solid #3a3e40;"
        " border-radius:3px;background:#1d2022;}"
        "QCheckBox::indicator:checked{background:#50c5b7;border-color:#50c5b7;}"
        "QLabel#sub{color:#929495;font-size:11px;}"
        "QLabel#master{color:#fff;font-weight:600;}"
        "QLabel#previewFrame{background:#16181a;border:1px solid #2a2d2f;border-radius:4px;}"
        "QScrollArea{border:none;background:transparent;}"
        "QLabel#logoTitle{color:#e7e8e9;font-family:'Chakra Petch';font-weight:600;"
        " font-size:18px;letter-spacing:4px;background:transparent;}"
        "QLabel#logoAccent{color:#ff8040;font-family:'Chakra Petch';font-weight:600;"
        " font-size:18px;letter-spacing:4px;background:transparent;}"
        "QLabel#logoSub{color:#6f7375;font-family:'Chakra Petch';font-weight:500;"
        " font-size:11px;letter-spacing:3px;background:transparent;}"
        "QLabel#logoBadge{color:#50c5b7;font-family:'Chakra Petch';font-weight:600;"
        " font-size:11px;letter-spacing:2px;background:transparent;}"
        "QLabel#logoBadgeDot{color:#50c5b7;font-size:14px;background:transparent;"
        " padding-right:6px;}"
        // L4: status badge — same teal as PREVIEW, but slightly dimmer
        // so the eye reads "PREVIEW" as the chrome and the status as
        // the live indicator.
        "QLabel#statusBadge{color:#3a8782;font-family:'Chakra Petch';"
        " font-weight:600;font-size:10px;letter-spacing:2px;"
        " background:transparent;border:none;}"
        // R2: letter badges for group titles (P/M/D) — coloured stroke on
        // a near-black fill, matching the HTML v0.4 side-bar block.
        "QLabel#badgeP{color:#aa55ff;font-family:'Chakra Petch';font-weight:700;"
        " font-size:14px;background:transparent;border:1.5px solid #aa55ff;"
        " border-radius:3px;qproperty-alignment:AlignCenter;}"
        "QLabel#badgeM{color:#ff8040;font-family:'Chakra Petch';font-weight:700;"
        " font-size:14px;background:transparent;border:1.5px solid #ff8040;"
        " border-radius:3px;qproperty-alignment:AlignCenter;}"
        "QLabel#badgeD{color:#50c5b7;font-family:'Chakra Petch';font-weight:700;"
        " font-size:14px;background:transparent;border:1.5px solid #50c5b7;"
        " border-radius:3px;qproperty-alignment:AlignCenter;}"
        "QLabel#groupTitle{color:#e7e8e9;font-family:'Chakra Petch';font-weight:600;"
        " font-size:14px;letter-spacing:2px;background:transparent;"
        " padding-left:10px;}"
        "QLabel#groupSub{color:#6f7375;font-family:'Noto Sans SC';font-weight:400;"
        " font-size:11px;background:transparent;padding-left:10px;}"
        // R5: hairline separator between groups
        "QFrame#rule{color:#1f2224;background:#1f2224;border:none;"
        " max-height:1px;min-height:1px;}"
        // R5: EDIT MODE block — orange filled "ENTER EDIT" button on
        // the right, caption on the left.
        "QPushButton#enterEdit{background:#ff8040;color:#1a0f08;"
        " border:none;border-radius:3px;padding:8px 18px;font-family:'Chakra Petch';"
        " font-weight:700;font-size:11px;letter-spacing:2px;}"
        "QPushButton#enterEdit:hover{background:#ff9050;}"
        "QPushButton#enterEdit:pressed{background:#e66f30;}"
        // L3: START shares the orange CTA look — same rules, just a
        // different object name so we can wire different signals.
        "QPushButton#startBtn{background:#ff8040;color:#1a0f08;"
        " border:none;border-radius:3px;padding:8px 18px;font-family:'Chakra Petch';"
        " font-weight:700;font-size:11px;letter-spacing:2px;}"
        "QPushButton#startBtn:hover{background:#ff9050;}"
        "QPushButton#startBtn:pressed{background:#e66f30;}"
        "QPushButton#stopBtn{background:#a13c2a;color:#1a0808;border:none;border-radius:3px;padding:8px 18px;font-family:\'Chakra Petch\';font-weight:700;font-size:11px;letter-spacing:2px;}"
        "QPushButton#stopBtn:hover{background:#b8482f;}"
        "QPushButton#stopBtn:pressed{background:#8a311f;}"
        "QPushButton#startBtn:disabled{background:#4a2010;color:#806050;}"
        "QLabel#editCap{color:#6f7375;font-family:'Noto Sans SC';font-size:10px;"
        " background:transparent;border:none;}"
        // R6: right-column title ("MOCK PREVIEW")
        "QLabel#previewTitle{color:#6f7375;font-family:'Chakra Petch';"
        " font-weight:600;font-size:11px;letter-spacing:4px;"
        " background:transparent;border:none;}"
        // v0.5 A shell: object rail, focused inspector, unified canvas.
        "QFrame#objectRail{background:#0b0e0f;border-right:1px solid #24282a;}"
        "QFrame#inspectorHost{background:#0d1011;border-right:1px solid #24282a;}"
        "QFrame#stage{background:#07090a;}"
        "QLabel#railBrand{font-family:'Chakra Petch';font-size:15px;letter-spacing:2px;color:#e7e8e9;}"
        "QLabel#railBrandSub,QLabel#railHint{font-family:'Chakra Petch';font-size:9px;letter-spacing:1px;color:#676d6f;}"
        "QLabel#sectionCap{font-family:'Chakra Petch';font-size:9px;letter-spacing:2px;color:#555b5d;}"
        "QFrame#navPlayer,QFrame#navMonster,QFrame#navDamage{background:transparent;border:1px solid transparent;border-radius:3px;border-left-width:2px;}"
        "QFrame#navPlayer:hover,QFrame#navMonster:hover,QFrame#navDamage:hover{background:#121617;}"
        "QFrame#navPlayer[selected=\"true\"]   {background:#15191a;border-color:#303638;border-left-color:#a74fff;}"
        "QFrame#navMonster[selected=\"true\"]  {background:#15191a;border-color:#303638;border-left-color:#ff7043;}"
        "QFrame#navDamage[selected=\"true\"]   {background:#15191a;border-color:#303638;border-left-color:#50c5b7;}"
        "QFrame#navPlayer   > QLabel#navEnabled {color:#a74fff;}"
        "QFrame#navMonster  > QLabel#navEnabled {color:#ff7043;}"
        "QFrame#navDamage   > QLabel#navEnabled {color:#50c5b7;}"
        "QLabel#navTitle{font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;color:#dfe1e2;}"
        "QLabel#navSummary{font-family:'Chakra Petch';font-size:9px;letter-spacing:1px;color:#777d7f;}"
        "QLabel#navEnabled{font-size:8px;color:#50c5b7;}"
        "QPushButton#railAction{background:transparent;color:#a2a6a8;border:1px solid #303638;border-radius:3px;"
        "text-align:left;padding:7px 10px;font-family:'Chakra Petch';font-size:10px;letter-spacing:1px;}"
        "QPushButton#railAction:hover{background:#15191a;color:#e7e8e9;}"
        "QPushButton#railAction:disabled{color:#616769;}"
        "QLabel#inspectorTitle{font-family:'Chakra Petch';font-size:22px;letter-spacing:2px;color:#e7e8e9;}"
        "QLabel#inspectorSub{font-family:'Noto Sans SC';font-size:11px;color:#787e80;}"
        "QLabel#countLabel{font-family:'Chakra Petch';font-size:10px;letter-spacing:1px;color:#aeb2b3;}"
        // v0.5 P1: section count picks up the panel accent via
        // objectName suffix (P/M/D) so the active object's
        // headline is identifiable at a glance.
        "QLabel#countLabelP{color:#a74fff;}"
        "QLabel#countLabelM{color:#ff7043;}"
        "QLabel#countLabelD{color:#50c5b7;}"
        "QLabel#sliderLabel{color:#8d9294;font-family:'Chakra Petch';font-size:9px;letter-spacing:1px;min-width:52px;}"
        "QLabel#sliderValue{color:#e0e0e0;font-family:'Chakra Petch';font-size:10px;min-width:36px;}"
        "QPushButton#foldout{background:transparent;color:#8d9294;border:1px solid #292e30;border-radius:3px;"
        "text-align:left;padding:9px 12px;font-family:'Chakra Petch';font-size:10px;letter-spacing:1px;}"
        "QPushButton#foldout:disabled:hover{background:#131617;}"
        "QPushButton#resetButton{background:transparent;color:#9da1a3;border:1px solid #313638;border-radius:3px;"
        "padding:7px 10px;font-family:'Chakra Petch';font-size:9px;letter-spacing:1px;}"
        "QPushButton#resetButton:disabled:hover{background:#131617;}"
        "QLabel#modified{font-family:'Chakra Petch';font-size:9px;letter-spacing:1px;color:#777d7f;}"
        "QLabel#posLabel{font-family:'Chakra Petch';font-size:10px;letter-spacing:0.5px;"
        "color:#8d9294;background:#131617;border:1px solid #232628;border-radius:3px;"
        "padding:6px 10px;margin-top:4px;}"
        "QScrollBar:vertical{width:7px;background:#0d1011;}QScrollBar::handle:vertical{background:#303638;min-height:24px;}"

        // v0.5 UI-link: APPEARANCE sliders
        "QSlider{background:transparent;border:none;}"
        "QSlider::groove:horizontal{height:4px;background:#2a2d2f;border-radius:2px;}"
        "QSlider::handle:horizontal{width:14px;height:14px;margin:-5px 0;background:#e0e0e0;border-radius:7px;}"
        "QSlider::sub-page:horizontal{background:#a74fff;border-radius:2px;}"
        // v0.5 UI-link: stagebar toggle buttons
        "QPushButton#stageToggle{background:transparent;color:#8d9294;border:1px solid #2a2d2f;border-radius:3px;padding:4px 12px;font-family:'Chakra Petch';font-size:9px;letter-spacing:1px;}"
        "QPushButton#stageToggle:hover{border-color:#4a4e50;color:#c0c4c6;}"
        "QPushButton#stageToggle:checked{background:#1e2224;border-color:#a74fff;color:#a74fff;}"
);
}

// v0.5 P2: map (panel index, section bit index) → SectionRow::Icon.
// The mapping follows panel_sections.h exactly: Player 0..5, Monster 0..4,
// Damage 0..2.
int iconKind(int panel, int section)
{
    static const int kPlayerIcons[] = {
        SectionRow::IconConn, SectionRow::IconQuest, SectionRow::IconWeapon,
        SectionRow::IconBars, SectionRow::IconMantles, SectionRow::IconDebuff,
    };
    static const int kMonsterIcons[] = {
        SectionRow::IconInfo, SectionRow::IconHp, SectionRow::IconEnrage,
        SectionRow::IconAil, SectionRow::IconParts,
    };
    static const int kDamageIcons[] = {
        SectionRow::IconRows, SectionRow::IconShare, SectionRow::IconChart,
    };
    if (panel == 0 && section < 6) return kPlayerIcons[section];
    if (panel == 1 && section < 5) return kMonsterIcons[section];
    if (panel == 2 && section < 3) return kDamageIcons[section];
    return SectionRow::IconNone;
}

QColor panelAccent(int panel)
{
    if (panel == 0) return QColor(167, 79, 255);   // Player purple
    if (panel == 1) return QColor(255, 112, 67);   // Monster orange
    return QColor(80, 197, 183);                   // Damage teal
}

} // namespace

ControlPanel::ControlPanel(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName("mhw-control-panel");
    setStyleSheet(qssBase());
    setWindowTitle(QStringLiteral("MHW Overlay Control"));
    resize(1440, 900);
    setMinimumSize(1160, 720);

    // Real panel instances, rendered off-screen only. WA_DontShowOnScreen
    // lets show()/repaint() run the full paint path (demo data + the
    // setContentSize geometry) WITHOUT mapping a window — so no layer-shell
    // surface, no focus steal, no taskbar entry. This is the whole reason
    // the console is safe to run on-screen while the live overlay runs too.
    player_  = new PlayerPanel();
    monster_ = new MonsterPanel();
    damage_  = new DamagePanel();
    for (Panel *p : {static_cast<Panel*>(player_),
                     static_cast<Panel*>(monster_),
                     static_cast<Panel*>(damage_)}) {
        p->setAttribute(Qt::WA_DontShowOnScreen);
        p->setEditMode(true);   // seeds setupDemoData() on first paint
        // WA_DontShowOnScreen + show() is the supported combo for off-screen
        // rendering: the widget becomes isVisible() (so paintEvent runs and
        // the backing store takes its real height) WITHOUT being mapped to
        // the window system — no focus steal, no taskbar entry, no extra
        // layer-shell surface. show() alone would map it; the attribute
        // alone (without show) leaves isVisible()==false and the backing
        // store stuck at the 320×120 safety-net size, which truncates the
        // preview.
        p->show();
    }
    // Wire the canvas to each panel's real geometry so the preview shows
    // the position the live overlay will sit at on the user's screen.
    // This is done after the canvas is constructed (below) — see the
    // canvas creation block.

    auto *root = new QHBoxLayout();
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // v0.5 A: object rail → focused inspector → unified canvas.
    auto *rail = new QFrame();
    rail->setObjectName("objectRail");
    rail->setFixedWidth(214);
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(20, 22, 20, 18);
    railLayout->setSpacing(8);

    auto *brand = new QLabel(QStringLiteral("MHW  OVERLAY"));
    brand->setObjectName("railBrand");
    auto *brandSub = new QLabel(QStringLiteral("CONTROL CONSOLE  ·  0.5"));
    brandSub->setObjectName("railBrandSub");
    railLayout->addWidget(brand);
    railLayout->addWidget(brandSub);
    railLayout->addSpacing(22);

    auto *ready = new QLabel(QStringLiteral("●   OVERLAY READY"));
    ready->setObjectName("statusBadge");
    statusBadge_ = ready;
    railLayout->addWidget(ready);
    railLayout->addSpacing(24);

    auto *objectsTitle = new QLabel(QStringLiteral("HUD OBJECTS"));
    objectsTitle->setObjectName("sectionCap");
    railLayout->addWidget(objectsTitle);
    railLayout->addWidget(buildObjectButton(QStringLiteral("P"), QStringLiteral("PLAYER"),
                                             QStringLiteral("玩家状态"), 0));
    railLayout->addWidget(buildObjectButton(QStringLiteral("M"), QStringLiteral("MONSTER"),
                                             QStringLiteral("怪物 HP"), 1));
    railLayout->addWidget(buildObjectButton(QStringLiteral("D"), QStringLiteral("DAMAGE"),
                                             QStringLiteral("DPS / 占比"), 2));
    railLayout->addSpacing(20);

    auto *workspaceTitle = new QLabel(QStringLiteral("WORKSPACE"));
    workspaceTitle->setObjectName("sectionCap");
    railLayout->addWidget(workspaceTitle);
    editBtn_ = new QPushButton(QStringLiteral("◇   LAYOUT MODE"));
    editBtn_->setObjectName("railAction");
    editBtn_->setCursor(Qt::PointingHandCursor);
    railLayout->addWidget(editBtn_);
    auto *presets = new QPushButton(QStringLiteral("▱   PRESETS"));
    presets->setObjectName("railAction");
    presets->setEnabled(false); // visual placeholder; no preset API yet
    railLayout->addWidget(presets);
    railLayout->addStretch(1);

    startBtn_ = new QPushButton(QStringLiteral("▶   START OVERLAY"));
    startBtn_->setObjectName("startBtn");
    startBtn_->setCursor(Qt::PointingHandCursor);
    railLayout->addWidget(startBtn_);
    auto *hint = new QLabel(QStringLiteral("ESC 返回 · 1/2/3 选择"));
    hint->setObjectName("railHint");
    hint->setAlignment(Qt::AlignCenter);
    railLayout->addWidget(hint);
    root->addWidget(rail);

    auto *inspectorHost = new QFrame();
    inspectorHost->setObjectName("inspectorHost");
    inspectorHost->setFixedWidth(358);
    auto *inspectorLayout = new QVBoxLayout(inspectorHost);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorStack_ = new QStackedWidget();
    inspectorStack_->setObjectName("inspectorStack");
    inspectorStack_->addWidget(buildInspector(QStringLiteral("PLAYER"),
                                               QStringLiteral("玩家状态面板"),
                                               mhw::PlayerSection::displayNames(), 0));
    inspectorStack_->addWidget(buildInspector(QStringLiteral("MONSTER"),
                                               QStringLiteral("怪物状态面板"),
                                               mhw::MonsterSection::displayNames(), 1));
    inspectorStack_->addWidget(buildInspector(QStringLiteral("DAMAGE"),
                                               QStringLiteral("队伍伤害面板"),
                                               mhw::DamageSection::displayNames(), 2));
    inspectorLayout->addWidget(inspectorStack_);
    root->addWidget(inspectorHost);

    auto *stage = new QFrame();
    stage->setObjectName("stage");
    auto *stageLayout = new QVBoxLayout(stage);
    stageLayout->setContentsMargins(0, 0, 0, 0);

    // v0.5 UI-link: stagebar with SAFE AREA / GRID toggles
    auto *stagebar = new QHBoxLayout();
    stagebar->setContentsMargins(22, 8, 22, 0);
    stagebar->setSpacing(8);
    stagebar->addStretch(1);
    safeAreaBtn_ = new QPushButton(QStringLiteral("SAFE AREA"));
    safeAreaBtn_->setObjectName("stageToggle");
    safeAreaBtn_->setCheckable(true);
    safeAreaBtn_->setChecked(true);
    safeAreaBtn_->setCursor(Qt::PointingHandCursor);
    stagebar->addWidget(safeAreaBtn_);
    gridBtn_ = new QPushButton(QStringLiteral("GRID"));
    gridBtn_->setObjectName("stageToggle");
    gridBtn_->setCheckable(true);
    gridBtn_->setChecked(true);
    gridBtn_->setCursor(Qt::PointingHandCursor);
    stagebar->addWidget(gridBtn_);
    stageLayout->addLayout(stagebar);

    canvas_ = new HudCanvas();
    canvas_->installEventFilter(this);
    auto *canvasScroll = new QScrollArea();
    canvasScroll->setObjectName("canvasScroll");
    canvasScroll->setWidget(canvas_);
    canvasScroll->setWidgetResizable(false);  // canvas reports sizeHint when zoomed
    canvasScroll->setFrameShape(QFrame::NoFrame);
    stageLayout->addWidget(canvasScroll);
    // v0.5 P1.4: clicking a HUD in the unified canvas selects it in the
    // rail/inspector, so the canvas is a real workspace, not a passive
    // preview. selectPanel() is idempotent, so re-clicking the active
    // panel costs nothing.
    connect(canvas_, &HudCanvas::panelSelected, this, [this](int idx){
        selectPanel(idx);
    });
    // v0.5: drag / arrow-key position editing. The canvas emits the
    // target margins; we apply them to the live panel (no persist —
    // the console writes on exit) and re-render the preview.
    connect(canvas_, &HudCanvas::panelMoved, this, [this](int idx, QMargins m){
        Panel *p = idx == 0 ? static_cast<Panel*>(player_)
                 : idx == 1 ? static_cast<Panel*>(monster_)
                            : static_cast<Panel*>(damage_);
        if (!p) return;
        p->setMargins(m, /*persist=*/false);
        rebuildAndRender(idx);
        updatePosLabel(idx);
    });
    connect(safeAreaBtn_, &QPushButton::toggled, this, [this](bool on){
        if (canvas_) canvas_->setShowSafeArea(on);
    });
    connect(gridBtn_, &QPushButton::toggled, this, [this](bool on){
        if (canvas_) canvas_->setShowGrid(on);
    });
    canvas_->bindPanel(0, new PanelSourceAdapter(static_cast<Panel*>(player_)));
    canvas_->bindPanel(1, new PanelSourceAdapter(static_cast<Panel*>(monster_)));
    canvas_->bindPanel(2, new PanelSourceAdapter(static_cast<Panel*>(damage_)));
    root->addWidget(stage, 1);

    auto *central = new QWidget();
    central->setLayout(root);
    setCentralWidget(central);

    // v0.5 P1: startBtn toggles between launch and stop. The
    // handler is rebuilt every time overlay state flips so the
    // same button does the right thing whether ready or running.
    auto wireStartBtn = [this]{
        if (!startBtn_) return;
        if (overlayPid_ == 0) {
            // Stop mode never wired because by definition
            // overlayPid_==0 means ready. Reach here only when
            // re-armed by onOverlayExited.
        } else {}
    };
    connect(startBtn_, &QPushButton::clicked, this, [this]{
        if (overlayPid_ == 0) launchOverlay(/*editMode=*/false);
        else                  stopOverlay();
    });
    connect(editBtn_, &QPushButton::clicked, this,
            [this]{ launchOverlay(/*editMode=*/true); });

    loadMaskFromDisk();
    for (int i = 0; i < 3; ++i)
        rebuildAndRender(i);
    selectPanel(0);
}

ControlPanel::~ControlPanel()
{
    // Persist current mask state on every destruction path (close, app
    // exit, explicit delete). Safe to call even if load was never reached.
    saveMaskToDisk();

    // The three preview panels were created as TOP-level QMainWindows
    // (no parent) so they get their own QWidgetWindow and don't pollute
    // the console's backing store (see renderPreview). They need to be
    // deleted explicitly here — otherwise QApplication sees them in the
    // top-level window list, "no visible windows" never fires, and
    // setQuitOnLastWindowClosed has nothing to quit on. Hiding them
    // first also gives the wayland layer a clean unmap, so the
    // "顶栏小块" residue doesn't linger.
    if (player_)  { player_->setVisible(false);  delete player_;  player_  = nullptr; }
    if (monster_) { monster_->setVisible(false); delete monster_; monster_ = nullptr; }
    if (damage_)  { damage_->setVisible(false);  delete damage_;  damage_  = nullptr; }
}

void ControlPanel::closeEvent(QCloseEvent *e)
{
    // Hide the three preview panels BEFORE accepting close. They're
    // top-level QMainWindows (no parent — keeping them parented would
    // composite their paint into the console's backing store, which is
    // what produced the "panel painted on top of switches" bug). They're
    // also invisible (WA_DontShowOnScreen), but the explicit hide steers
    // QApplication::quitOnLastWindowClosed toward the right answer — it
    // sees 0 visible windows the moment the console goes away and exits.
    if (player_)  player_->setVisible(false);
    if (monster_) monster_->setVisible(false);
    if (damage_)  damage_->setVisible(false);

    // v0.5 UI-link: persist scale/opacity that the APPEARANCE sliders
    // changed (with persist=false). saveAppearance() writes ONLY scale
    // and opacity — NOT margins or visible — so it can't clobber the
    // live overlay's geometry or accidentally hide a panel (the console
    // panels are WA_DontShowOnScreen + hidden; isVisible()==false would
    // write visible=false and break the overlay's next launch).
    if (player_)  player_->saveAppearance();
    if (monster_) monster_->saveAppearance();
    if (damage_)  damage_->saveAppearance();

    saveMaskToDisk();
    QMainWindow::closeEvent(e);
}

// v0.5 P1: 1/2/3 hot-keys for the HUD objects rail. Visible in the
// rail hint at the bottom of the left column.
void ControlPanel::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_1) { selectPanel(0); return; }
    if (e->key() == Qt::Key_2) { selectPanel(1); return; }
    if (e->key() == Qt::Key_3) { selectPanel(2); return; }
    QMainWindow::keyPressEvent(e);
}

bool ControlPanel::eventFilter(QObject *watched, QEvent *event)
{
    // v0.5: Ctrl+wheel over the canvas zooms in/out.
    if (watched == canvas_ && event->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            const qreal delta = (we->angleDelta().y() > 0) ? 0.1 : -0.1;
            canvas_->setZoom(canvas_->zoom() + delta);
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        for (int i = 0; i < 3; ++i) {
            if (watched == ctl_[i].navButton) {
                selectPanel(i);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void ControlPanel::selectPanel(int idx)
{
    if (idx < 0 || idx >= 3) return;
    selectedPanel_ = idx;
    syncAppearance(idx);
    updatePosLabel(idx);
    if (inspectorStack_) inspectorStack_->setCurrentIndex(idx);
    if (canvas_) canvas_->setSelectedPanel(idx);
    for (int i = 0; i < 3; ++i) {
        if (!ctl_[i].navButton) continue;
        ctl_[i].navButton->setProperty("selected", i == idx);
        ctl_[i].navButton->style()->unpolish(ctl_[i].navButton);
        ctl_[i].navButton->style()->polish(ctl_[i].navButton);
    }
}

void ControlPanel::updatePanelSummary(int idx)
{
    int on = 0;
    for (auto *row : ctl_[idx].subs)
        if (row->isChecked()) ++on;
    const int total = ctl_[idx].subs.size();
    if (ctl_[idx].countLabel)
        ctl_[idx].countLabel->setText(QStringLiteral("%1 OF %2 SECTIONS VISIBLE").arg(on).arg(total));
    if (ctl_[idx].countBar)
        ctl_[idx].countBar->setRatio(total > 0 ? qreal(on) / total : 1.0);
    if (ctl_[idx].navSummary)
        ctl_[idx].navSummary->setText(ctl_[idx].master->isChecked()
            ? QStringLiteral("%1 / %2 SECTIONS").arg(on).arg(total)
            : QStringLiteral("PANEL DISABLED"));
}

QWidget *ControlPanel::buildObjectButton(const QString &letter,
                                         const QString &title,
                                         const QString &summary, int idx)
{
    auto *box = new QFrame();
    box->setObjectName(idx == 0 ? "navPlayer"
                       : idx == 1 ? "navMonster"
                                  : "navDamage");
    box->setProperty("navObject", true);
    box->setProperty("panelIndex", idx);
    box->setCursor(Qt::PointingHandCursor);
    box->installEventFilter(this);
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(10, 9, 8, 9);
    row->setSpacing(10);

    auto *badge = new QLabel(letter);
    badge->setObjectName(idx == 0 ? "badgeP" : idx == 1 ? "badgeM" : "badgeD");
    badge->setFixedSize(26, 26);
    badge->setAlignment(Qt::AlignCenter);
    row->addWidget(badge);

    auto *texts = new QVBoxLayout();
    texts->setSpacing(2);
    auto *titleLabel = new QLabel(title);
    titleLabel->setObjectName("navTitle");
    auto *summaryLabel = new QLabel(summary);
    summaryLabel->setObjectName("navSummary");
    texts->addWidget(titleLabel);
    texts->addWidget(summaryLabel);
    row->addLayout(texts, 1);
    auto *enabled = new QLabel(QStringLiteral("●"));
    enabled->setObjectName("navEnabled");
    row->addWidget(enabled);

    ctl_[idx].navButton = box;
    ctl_[idx].navSummary = summaryLabel;
    return box;
}

QWidget *ControlPanel::buildInspector(const QString &title, const QString &sub,
                                      const QStringList &labels, int idx)
{
    auto *host = new QWidget();
    auto *scroll = new QScrollArea(host);
    scroll->setWidgetResizable(true);
    auto *outer = new QVBoxLayout(host);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    auto *content = new QWidget();
    auto *vl = new QVBoxLayout(content);
    vl->setContentsMargins(24, 24, 24, 24);
    vl->setSpacing(10);

    auto *eyebrow = new QLabel(QStringLiteral("SELECTED OBJECT  /  0%1").arg(idx + 1));
    eyebrow->setObjectName("sectionCap");
    vl->addWidget(eyebrow);

    auto *head = new QHBoxLayout();
    auto *titles = new QVBoxLayout();
    auto *titleLabel = new QLabel(title);
    titleLabel->setObjectName("inspectorTitle");
    auto *subLabel = new QLabel(sub);
    subLabel->setObjectName("inspectorSub");
    titles->addWidget(titleLabel);
    titles->addWidget(subLabel);
    head->addLayout(titles, 1);
    auto *master = new ToggleChip();
    ctl_[idx].master = master;
    head->addWidget(master, 0, Qt::AlignVCenter);
    vl->addLayout(head);

    auto *count = new QLabel();
    // v0.5 P1: per-panel objectName suffix (P/M/D) so the QSS rule
    // can tint the section count by the matching panel accent.
    count->setObjectName(idx == 0 ? "countLabelP"
                       : idx == 1 ? "countLabelM"
                                  : "countLabelD");
    ctl_[idx].countLabel = count;
    vl->addWidget(count);

    // v0.5 P2: thin accent progress bar under the count headline
    auto *bar = new SectionCountBar();
    bar->setAccent(panelAccent(idx));
    ctl_[idx].countBar = bar;
    vl->addWidget(bar);
    vl->addSpacing(10);
    auto *contentCap = new QLabel(QStringLiteral("CONTENT"));
    contentCap->setObjectName("sectionCap");
    vl->addWidget(contentCap);

    const QStringList &keys = idx == 0 ? mhw::PlayerSection::names()
                            : idx == 1 ? mhw::MonsterSection::names()
                                       : mhw::DamageSection::names();
    for (int b = 0; b < labels.size(); ++b) {
        auto *row = new SectionRow(labels[b], b < keys.size() ? keys[b] : QString(), iconKind(idx, b));
        row->setAccent(panelAccent(idx));
        ctl_[idx].subs.push_back(row);
        vl->addWidget(row);
    }

    vl->addSpacing(12);

    // v0.5 UI-link: APPEARANCE sliders (scale + opacity), live preview.
    auto *appCap = new QLabel(QStringLiteral("APPEARANCE"));
    appCap->setObjectName("sectionCap");
    vl->addWidget(appCap);

    auto *scaleRow = new QHBoxLayout();
    scaleRow->setSpacing(8);
    auto *scaleLab = new QLabel(QStringLiteral("SCALE"));
    scaleLab->setObjectName("sliderLabel");
    auto *scaleVal = new QLabel();
    scaleVal->setObjectName("sliderValue");
    auto *scaleSlider = new QSlider(Qt::Horizontal);
    scaleSlider->setRange(50, 300);
    {
        Panel *p = (idx == 0 ? static_cast<Panel*>(player_)
                  : idx == 1 ? static_cast<Panel*>(monster_)
                             : static_cast<Panel*>(damage_));
        scaleSlider->setValue(qRound(p->scale() * 100));
    }
    scaleVal->setText(QStringLiteral("%1%").arg(scaleSlider->value()));
    connect(scaleSlider, &QSlider::valueChanged, this, [this, idx, scaleVal](int v){
        scaleVal->setText(QStringLiteral("%1%").arg(v));
        Panel *p = (idx == 0 ? static_cast<Panel*>(player_)
                  : idx == 1 ? static_cast<Panel*>(monster_)
                             : static_cast<Panel*>(damage_));
        p->setScale(v / 100.0, false);
        rebuildAndRender(idx);
    });
    scaleRow->addWidget(scaleLab);
    scaleRow->addWidget(scaleSlider, 1);
    scaleRow->addWidget(scaleVal);
    vl->addLayout(scaleRow);
    ctl_[idx].scaleSlider = scaleSlider;

    auto *opacRow = new QHBoxLayout();
    opacRow->setSpacing(8);
    auto *opacLab = new QLabel(QStringLiteral("OPACITY"));
    opacLab->setObjectName("sliderLabel");
    auto *opacVal = new QLabel();
    opacVal->setObjectName("sliderValue");
    auto *opacSlider = new QSlider(Qt::Horizontal);
    opacSlider->setRange(10, 100);
    {
        Panel *p2 = (idx == 0 ? static_cast<Panel*>(player_)
                   : idx == 1 ? static_cast<Panel*>(monster_)
                              : static_cast<Panel*>(damage_));
        opacSlider->setValue(qRound(p2->opacity() * 100));
    }
    opacVal->setText(QStringLiteral("%1%").arg(opacSlider->value()));
    connect(opacSlider, &QSlider::valueChanged, this, [this, idx, opacVal](int v){
        opacVal->setText(QStringLiteral("%1%").arg(v));
        Panel *p = (idx == 0 ? static_cast<Panel*>(player_)
                  : idx == 1 ? static_cast<Panel*>(monster_)
                             : static_cast<Panel*>(damage_));
        p->setOpacity(v / 100.0, false);
        rebuildAndRender(idx);
    });
    opacRow->addWidget(opacLab);
    opacRow->addWidget(opacSlider, 1);
    opacRow->addWidget(opacVal);
    vl->addLayout(opacRow);
    ctl_[idx].opacitySlider = opacSlider;

    // v0.5 BEHAVIOR → POSITION: shows the panel's anchor corner and
    // current margins. The user moves the panel via canvas drag or
    // arrow keys; this label is read-only feedback.
    auto *posLabel = new QLabel();
    posLabel->setObjectName("posLabel");
    ctl_[idx].posLabel = posLabel;
    vl->addWidget(posLabel);
    vl->addStretch(1);
    auto *foot = new QHBoxLayout();
    auto *reset = new QPushButton(QStringLiteral("↶  RESET %1").arg(title));
    reset->setObjectName("resetButton");
    reset->setCursor(Qt::PointingHandCursor);
    connect(reset, &QPushButton::clicked, this, [this, idx]{ resetPanel(idx); });
    foot->addWidget(reset);
    foot->addStretch(1);
    auto *modified = new QLabel(QStringLiteral("AUTO-SAVED  ●"));
    modified->setObjectName("modified");
    foot->addWidget(modified);
    vl->addLayout(foot);

    scroll->setWidget(content);
    auto rewire = [this, idx](int){ rebuildAndRender(idx); };
    connect(master, &ToggleChip::stateChanged, this, rewire);
    for (auto *row : ctl_[idx].subs)
        connect(row, &SectionRow::stateChanged, this, rewire);
    updatePosLabel(idx);
    return host;
}

// Legacy helpers retained for ABI/source stability; the v0.5 shell no longer uses them.
QWidget *ControlPanel::buildRule()
{
    auto *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    return line;
}

// R5: EDIT MODE block — orange "ENTER EDIT" button on the right,
// caption "进入后三个面板强制显示，方向键移动" on the left.
//
// L3: also add a "START" button to spawn mhw-overlay. Same orange
// CTA visual, separate action. ENTER EDIT still exists as a synonym
// for START with --edit — both hide the console, both re-show on
// exit; the only difference is whether the overlay enters edit mode.
QWidget *ControlPanel::buildEditModeBlock()
{
    auto *box = new QWidget();
    auto *vl = new QVBoxLayout();
    vl->setSpacing(10);
    vl->setContentsMargins(0, 14, 0, 0);

    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);
    auto *titleLab = new QLabel(QStringLiteral("EDIT MODE"));
    titleLab->setObjectName("groupTitle");
    titleRow->addWidget(titleLab, 1);
    vl->addLayout(titleRow);

    auto *row = new QHBoxLayout();
    row->setSpacing(10);
    auto *cap = new QLabel(QStringLiteral(
        "进入后三个面板强制显示，方向键移动"));
    cap->setObjectName("editCap");
    cap->setWordWrap(true);
    row->addWidget(cap, 1);
    auto *startBtn = new QPushButton(QStringLiteral("START"));
    startBtn->setObjectName("startBtn");
    startBtn->setCursor(Qt::PointingHandCursor);
    auto *editBtn = new QPushButton(QStringLiteral("ENTER EDIT"));
    editBtn->setObjectName("enterEdit");
    editBtn->setCursor(Qt::PointingHandCursor);
    row->addWidget(startBtn, 0);
    row->addWidget(editBtn, 0);
    vl->addLayout(row);
    box->setLayout(vl);

    // Stash handles on the window (not on ctl_ — they aren't per-panel).
    startBtn_ = startBtn;
    editBtn_  = editBtn;

    connect(startBtn, &QPushButton::clicked, this, [this]{
        launchOverlay(/*editMode=*/false);
    });
    connect(editBtn,  &QPushButton::clicked, this, [this]{
        launchOverlay(/*editMode=*/true);
    });
    return box;
}

// L3: spawn mhw-overlay as a detached subprocess, hide the console while
// it runs, then show the console again when the overlay exits.
//
// Why detached: the overlay and the console are independent Qt apps
// (each has its own QApplication). If we started the overlay in-process
// or as a tracked child, the overlay's ESC quit would also quit our
// console — that's not what the user asked for. Detached + PID polling
// gives us clean ownership: the overlay owns its own lifetime, and we
// just watch from outside.
//
// The mask state on disk is rewritten synchronously here so the overlay
// sees the user's *current* toggles, not the snapshot from console boot.
void ControlPanel::launchOverlay(bool editMode)
{
    if (overlayPid_ != 0) {
        // Already running — refuse to launch a second copy. The user
        // can press ESC in the overlay to bring the console back, then
        // click again.
        return;
    }
    saveMaskToDisk();
    // v0.5: persist scale / opacity / margins so the overlay's
    // loadConfig() reads the values the user set in the console.
    // Without this, the overlay always starts with the last
    // edit-mode save (or factory defaults).
    if (player_)  player_->saveAppearance();
    if (monster_) monster_->saveAppearance();
    if (damage_)  damage_->saveAppearance();

    // Build argv from the same mask source the file uses.
    auto maskFor = [](const PanelCtl &c) -> uint32_t {
        if (!c.master->isChecked()) return 0u;
        uint32_t m = 0;
        for (int b = 0; b < c.subs.size(); ++b)
            if (c.subs[b]->isChecked())
                m |= (1u << b);
        return m;
    };
    const uint32_t mp = maskFor(ctl_[0]);
    const uint32_t mm = maskFor(ctl_[1]);
    const uint32_t md = maskFor(ctl_[2]);

    QStringList args;
    args << QStringLiteral("--mask-player=%1").arg(mp, 0, 16)
         << QStringLiteral("--mask-monster=%1").arg(mm, 0, 16)
         << QStringLiteral("--mask-damage=%1").arg(md, 0, 16);
    // Master toggle maps to a separate --no-* flag per panel. The
    // mask controls which sub-blocks render inside an enabled panel;
    // --no-* unmounts the layer-shell surface entirely so the user
    // doesn't get a 32-40px chrome stub of the panel they'd switched
    // off.
    if (!ctl_[0].master->isChecked())
        args << QStringLiteral("--no-player");
    if (!ctl_[1].master->isChecked())
        args << QStringLiteral("--no-monster");
    if (!ctl_[2].master->isChecked())
        args << QStringLiteral("--no-damage");
    if (editMode) args << QStringLiteral("--edit");

    // mhw-overlay lives next to mhw-control in the same build dir.
    const QString overlay = QCoreApplication::applicationDirPath()
                          + QStringLiteral("/mhw-overlay");

    qint64 pid = 0;
    if (!QProcess::startDetached(overlay, args,
                                 QCoreApplication::applicationDirPath(),
                                 &pid)) {
        qWarning("mhw-control: failed to launch %s", qPrintable(overlay));
        return;
    }
    overlayPid_ = pid;

    // L4: flip the status badge so the user can tell at a glance which
    // mode the console is in. We're hiding next, so this label only
    // matters when the overlay exits and the console re-shows.
    if (statusBadge_) {
        const QString stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
        statusBadge_->setText(
            QStringLiteral("RUNNING pid %1 since %2")
                .arg(overlayPid_).arg(stamp));
    }

    // Disable both launcher buttons while running so the user can't
    // accidentally spawn a second overlay.
    if (startBtn_) startBtn_->setEnabled(false);
    if (editBtn_)  editBtn_->setEnabled(false);

    // Poll the PID. 250ms feels live but stays well under one paint frame
    // — the console re-shows within a quarter second of overlay death.
    overlayWatch_ = new QTimer(this);
    overlayWatch_->setInterval(250);
    connect(overlayWatch_, &QTimer::timeout, this, [this]{
        if (overlayPid_ == 0) return;
        // kill(pid, 0) is POSIX's "does this PID exist?" — no signal sent.
        // ESRCH means the process is gone.
        if (kill(static_cast<pid_t>(overlayPid_), 0) != 0) {
            onOverlayExited();
        }
    });
    overlayWatch_->start();
    setOverlayRunning(true);

    hide();   // the overlay owns the screen now
}

void ControlPanel::onOverlayExited()
{
    overlayPid_ = 0;
    if (overlayWatch_) {
        overlayWatch_->stop();
        overlayWatch_->deleteLater();
        overlayWatch_ = nullptr;
    }
    if (startBtn_) startBtn_->setEnabled(true);
    if (editBtn_)  editBtn_->setEnabled(true);
    if (statusBadge_) statusBadge_->setText(QStringLiteral("READY"));
    setOverlayRunning(false);
    show();
    raise();
    activateWindow();
}

namespace {
// L2: resolve the persistence path.
//
//   * AppConfigLocation = ~/.config/<OrgName>/<AppName> (Linux). Doesn't
//     honour XDG_CONFIG_HOME, but it's the canonical user-config root.
//   * We append "/mhw-overlay.conf" so the same directory can later
//     carry other keys (locale, map path) without inventing new files.
//
// For tests we override via the env-var honoured by GenericConfigLocation
// (XDG_CONFIG_HOME) — see tests/control_l2_smoke.cpp.
QString maskConfigPath()
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/mhw-overlay");
    return dir + QStringLiteral("/mhw-overlay.conf");
}
} // namespace

void ControlPanel::loadMaskFromDisk()
{
    const QString path = maskConfigPath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // No file = first run, keep the all-on default the buildGroup()
        // calls already established.
        return;
    }
    // File format (3 lines, key=value hex32):
    //   player=<hex32>
    //   monster=<hex32>
    //   damage=<hex32>
    // Anything malformed is silently ignored — we never want a bad
    // config to make the console unstartable.
    QTextStream in(&f);
    int playerMask = -1, monsterMask = -1, damageMask = -1;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1String("player=")))
            playerMask = line.mid(7).toInt(0, 16);
        else if (line.startsWith(QLatin1String("monster=")))
            monsterMask = line.mid(8).toInt(0, 16);
        else if (line.startsWith(QLatin1String("damage=")))
            damageMask = line.mid(7).toInt(0, 16);
    }

    auto applyTo = [](int m, PanelCtl &c) {
        if (m < 0) return;
        // master stays ON if any bit is set; otherwise treat as fully
        // disabled (mirrors the "all off" intent in the file).
        c.master->setChecked(m != 0);
        for (int b = 0; b < c.subs.size(); ++b)
            c.subs[b]->setChecked((m & (1u << b)) != 0);
    };
    applyTo(playerMask,  ctl_[0]);
    applyTo(monsterMask, ctl_[1]);
    applyTo(damageMask,  ctl_[2]);
}

void ControlPanel::saveMaskToDisk() const
{
    const QString path = maskConfigPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    auto maskFor = [](const PanelCtl &c) -> uint32_t {
        if (!c.master->isChecked()) return 0u;
        uint32_t m = 0;
        for (int b = 0; b < c.subs.size(); ++b)
            if (c.subs[b]->isChecked())
                m |= (1u << b);
        return m;
    };
    const uint32_t mp = maskFor(ctl_[0]);
    const uint32_t mm = maskFor(ctl_[1]);
    const uint32_t md = maskFor(ctl_[2]);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning("mhw-control: cannot write %s", qPrintable(path));
        return;
    }
    QTextStream out(&f);
    out << "player="  << QString::number(mp, 16) << '\n'
        << "monster=" << QString::number(mm, 16) << '\n'
        << "damage="  << QString::number(md, 16) << '\n';
}

void ControlPanel::rebuildAndRender(int idx)
{
    Panel *panel = nullptr;
    if (idx == 0) panel = player_;
    else if (idx == 1) panel = monster_;
    else panel = damage_;

    auto *lab = ctl_[idx].preview;
    updatePanelSummary(idx);
    if (!ctl_[idx].master->isChecked()) {
        if (canvas_) canvas_->setPanelPixmap(idx, QPixmap(), false);
        // Master off: show a flat disabled placeholder. The panel is not
        // painted at all (matches live setVisible(false) gate).
        QPixmap ph(378, 90);
        ph.fill(QColor(22, 24, 26));
        QPainter p(&ph);
        p.setPen(QColor(80, 83, 85));
        QFont f("Chakra Petch", 11, QFont::Bold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 2);
        p.setFont(f);
        p.drawText(ph.rect(), Qt::AlignCenter, QStringLiteral("PANEL DISABLED"));
        p.end();
        if (lab) lab->setPixmap(ph);
        return;
    }

    uint32_t mask = 0;
    for (int b = 0; b < ctl_[idx].subs.size(); ++b)
        if (ctl_[idx].subs[b]->isChecked())
            mask |= (1u << b);
    panel->setSectionMask(mask);   // also calls update()
    const QPixmap pix = renderPreview(panel);
    if (lab) lab->setPixmap(pix);
    if (canvas_) {
        canvas_->setPanelPixmap(idx, pix, true);
    }
}

QPixmap ControlPanel::renderPreview(Panel *p)
{
    // First paint seeds demo data and writes the panel's natural geometry
    // into logicalSize_ via setContentSize(); but the matching
    // setFixedSize() is deferred, so size() is still stale here. Read
    // contentSize() (the synchronous logical size) and resize the backing
    // store to match before the second paint — otherwise render() would
    // only grab the top slice of the panel (the truncation bug).
    p->repaint();
    const QSize nat = p->contentSize();
    if (nat.isValid() && !nat.isEmpty())
        p->resize(nat);
    p->repaint();
    const QSize sz = p->size();
    // render() + enabled QGraphicsOpacityEffect produces a=0 (broken).
    // Disable the effect, render at full alpha, and let HudCanvas apply
    // the panel's opacity via PanelSource::opacity() * selDim.
    p->setCompositingEnabled(false);
    QPixmap pix(sz);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    p->render(&painter, QPoint(), QRect(QPoint(0, 0), sz));
    painter.end();
    p->setCompositingEnabled(true);

    // R6: paint a 4-px vertical accent stripe on the left edge of every
    // preview tile (purple for player, orange for monster, teal for
    // damage) so each panel has a clear identity at a glance. The stripe
    // sits OUTSIDE the panel rectangle, so we just draw it on the pixmap.
    QColor accent;
    if (p == player_)      accent = QColor(170, 85, 255);   // #aa55ff
    else if (p == monster_) accent = QColor(255, 128, 64);  // #ff8040
    else                   accent = QColor(80, 197, 183);  // #50c5b7
    QPainter stripe(&pix);
    stripe.fillRect(QRect(0, 0, 4, sz.height()), accent);
    stripe.end();
    return pix;
}

// v0.5 P1: kill the overlay subprocess and let onOverlayExited()
// do the cleanup. Safe to call when no overlay is running.
void ControlPanel::stopOverlay()
{
    if (overlayPid_ == 0) return;
    if (overlayWatch_) overlayWatch_->stop();
    // SIGTERM = gentle. The overlay's own ESC handler will run
    // saveConfig() and quit cleanly. SIGKILL would skip that.
    kill(static_cast<pid_t>(overlayPid_), SIGTERM);
    // Don't zero overlayPid_ here — the 250ms PID-poll timer will
    // observe the exit and call onOverlayExited() which does the
    // teardown. Setting it to 0 now would block a re-launch.
}

// v0.5 P1: switch the START button between launch and stop modes and
// re-style the status badge. Safe to call repeatedly; cheap idempotent
// state flip.
void ControlPanel::setOverlayRunning(bool running)
{
    if (!startBtn_) return;
    if (running) {
        startBtn_->setText(QStringLiteral("■   STOP OVERLAY"));
        startBtn_->setObjectName(QStringLiteral("stopBtn"));
        if (statusBadge_) {
            statusBadge_->setText(
                QStringLiteral("●   RUNNING pid %1")
                    .arg(overlayPid_));
            statusBadge_->setProperty("state", "running");
        }
    } else {
        startBtn_->setText(QStringLiteral("▶   START OVERLAY"));
        startBtn_->setObjectName(QStringLiteral("startBtn"));
        if (statusBadge_) {
            statusBadge_->setText(QStringLiteral("●   OVERLAY READY"));
            statusBadge_->setProperty("state", "ready");
        }
    }
    // Re-apply style so the swapped objectName picks up the QSS rule.
    startBtn_->style()->unpolish(startBtn_);
    startBtn_->style()->polish(startBtn_);
    if (statusBadge_) {
        statusBadge_->style()->unpolish(statusBadge_);
        statusBadge_->style()->polish(statusBadge_);
    }
}

// v0.5 UI-link: sync the APPEARANCE sliders to match the panel's current
// scale/opacity. Called after selectPanel() and resetPanel() so the
// inspector always reflects reality. Blocks signals to avoid feedback
// loops (slider → setScale → rebuildAndRender → syncAppearance → slider…).
void ControlPanel::syncAppearance(int idx)
{
    Panel *panel = (idx == 0 ? static_cast<Panel*>(player_)
                  : idx == 1 ? static_cast<Panel*>(monster_)
                             : static_cast<Panel*>(damage_));
    auto &c = ctl_[idx];
    if (c.scaleSlider) {
        c.scaleSlider->blockSignals(true);
        c.scaleSlider->setValue(qRound(panel->scale() * 100));
        c.scaleSlider->blockSignals(false);
    }
    if (c.opacitySlider) {
        c.opacitySlider->blockSignals(true);
        c.opacitySlider->setValue(qRound(panel->opacity() * 100));
        c.opacitySlider->blockSignals(false);
    }
}

// v0.5 UI-link: reset the selected panel to factory defaults (all
// sections on, scale 1.0, opacity 0.85, default margins). Uses
// persist=false — the change is previewed immediately and written
// by closeEvent's saveAppearance() + saveMaskToDisk().
void ControlPanel::resetPanel(int idx)
{
    Panel *panel = (idx == 0 ? static_cast<Panel*>(player_)
                  : idx == 1 ? static_cast<Panel*>(monster_)
                             : static_cast<Panel*>(damage_));
    panel->resetToDefaults();
    // Sync UI: master on, all subs on, sliders to defaults.
    auto &c = ctl_[idx];
    if (c.master) c.master->setChecked(true);
    for (auto *row : c.subs)
        row->setChecked(true);
    syncAppearance(idx);
    rebuildAndRender(idx);
}

void ControlPanel::updatePosLabel(int idx)
{
    if (!ctl_[idx].posLabel) return;
    Panel *p = idx == 0 ? static_cast<Panel*>(player_)
             : idx == 1 ? static_cast<Panel*>(monster_)
                        : static_cast<Panel*>(damage_);
    if (!p) return;
    const QMargins m = p->margins();
    QString corner;
    switch (p->corner()) {
    case Corner::TopLeft:     corner = "TOP LEFT"; break;
    case Corner::TopRight:    corner = "TOP RIGHT"; break;
    case Corner::BottomLeft:  corner = "BOTTOM LEFT"; break;
    case Corner::BottomRight: corner = "BOTTOM RIGHT"; break;
    }
    ctl_[idx].posLabel->setText(
        QStringLiteral("POSITION: %1  ·  L%2 T%3 R%4 B%5  ·  ← → ↑ ↓ / DRAG")
            .arg(corner).arg(m.left()).arg(m.top())
            .arg(m.right()).arg(m.bottom()));
}
