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
#include "ui/ui_theme.h"
#include "core/string_table.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QScrollArea>
#include <QSlider>
#include <QMouseEvent>
#include <QFrame>
#include <QSettings>
#include <QGuiApplication>
#include <QScreen>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QAbstractAnimation>
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
    // Theme-driven QSS: colours come from uiTheme() (ui_theme.h) so the
    // console can switch dark (light-grey deep charcoal) ↔ light (浅灰)
    // at runtime. Structural rules (selectors / padding / font sizes)
    // stay here in one place. Custom-painted widgets (SectionRow,
    // ToggleChip, SectionCountBar) read the same palette themselves.
    const UiTheme &t = uiTheme();
    const QString bg      = t.bg.name();
    const QString bgPanel = t.bgPanel.name();
    const QString bgCtl   = t.bgControl.name();
    const QString bgTrack = t.bgTrack.name();
    const QString fg      = t.fg.name();
    const QString fgMut   = t.fgMuted.name();
    const QString fgDim   = t.fgDim.name();
    const QString border  = t.border.name();
    const QString soft    = t.borderSoft.name();
    const QString orange  = t.accentOrange.name();
    const QString teal    = t.accentTeal.name();
    const QString purple  = t.accentPurple.name();

    QString qss = QStringLiteral(
        "QWidget{background:%1;color:%2;}"
        // Explicit button foreground is required: relying on QWidget's
        // inherited `color` leaves Fusion/QSS cache with stale text after
        // a runtime theme swap on Qt 6.11.
        "QPushButton{color:%2;}"
        "QMainWindow{background:%1;}"
        "QGroupBox{color:%2;border:none;border-radius:0;"
        " margin-top:0;padding:14px 0 12px 0;font-family:'Chakra Petch';"
        " font-weight:600;letter-spacing:1px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:0;padding:0;color:%2;}"
        "QCheckBox{color:%5;spacing:8px;font-family:'Noto Sans SC';font-size:15px;}"
        "QCheckBox::indicator{width:14px;height:14px;border:1px solid %8;"
        " border-radius:3px;background:%3;}"
        "QCheckBox::indicator:checked{background:%10;border-color:%10;}"
        "QLabel#sub{color:%6;font-size:14px;}"
        "QLabel#master{color:%2;font-weight:600;}"
        "QLabel#previewFrame{background:%3;border:1px solid %8;border-radius:4px;}"
        "QScrollArea{border:none;background:transparent;}"
        "QLabel#logoTitle{color:%2;font-family:'Chakra Petch';font-weight:600;"
        " font-size:20px;letter-spacing:4px;background:transparent;}"
        "QLabel#logoAccent{color:%9;font-family:'Chakra Petch';font-weight:600;"
        " font-size:20px;letter-spacing:4px;background:transparent;}"
        "QLabel#logoSub{color:%6;font-family:'Chakra Petch';font-weight:500;"
        " font-size:14px;letter-spacing:3px;background:transparent;}"
        "QLabel#logoBadge{color:%10;font-family:'Chakra Petch';font-weight:600;"
        " font-size:14px;letter-spacing:2px;background:transparent;}"
        "QLabel#logoBadgeDot{color:%10;font-size:17px;background:transparent;"
        " padding-right:6px;}"
        "QLabel#statusBadge{color:%10;font-family:'Chakra Petch';"
        " font-weight:600;font-size:13px;letter-spacing:2px;"
        " background:transparent;border:none;}"
        "QLabel#badgeP{color:%11;font-family:'Chakra Petch';font-weight:700;"
        " font-size:17px;background:transparent;border:1.5px solid %11;"
        " border-radius:3px;qproperty-alignment:AlignCenter;}"
        "QLabel#badgeM{color:%9;font-family:'Chakra Petch';font-weight:700;"
        " font-size:17px;background:transparent;border:1.5px solid %9;"
        " border-radius:3px;qproperty-alignment:AlignCenter;}"
        "QLabel#badgeD{color:%10;font-family:'Chakra Petch';font-weight:700;"
        " font-size:17px;background:transparent;border:1.5px solid %10;"
        " border-radius:3px;qproperty-alignment:AlignCenter;}"
        "QLabel#groupTitle{color:%2;font-family:'Chakra Petch';font-weight:600;"
        " font-size:17px;letter-spacing:2px;background:transparent;"
        " padding-left:10px;}"
        "QLabel#groupSub{color:%6;font-family:'Noto Sans SC';font-weight:400;"
        " font-size:14px;background:transparent;padding-left:10px;}"
        "QFrame#rule{color:%8;background:%8;border:none;"
        " max-height:1px;min-height:1px;}"
        "QPushButton#enterEdit{background:%9;color:%2;"
        " border:none;border-radius:3px;padding:12px 28px;font-family:'Chakra Petch';"
        " font-weight:700;font-size:14px;letter-spacing:2px;}"
        "QPushButton#enterEdit:hover{background:%9;}"
        "QPushButton#enterEdit:pressed{background:%9;}"
        "QPushButton#startBtn{background:%9;color:%2;"
        " border:none;border-radius:3px;padding:12px 28px;font-family:'Chakra Petch';"
        " font-weight:700;font-size:14px;letter-spacing:2px;}"
        "QPushButton#startBtn:hover{background:%9;}"
        "QPushButton#startBtn:pressed{background:%9;}"
        "QPushButton#stopBtn{background:#a13c2a;color:#1a0808;border:none;border-radius:3px;padding:12px 28px;font-family:'Chakra Petch';font-weight:700;font-size:14px;letter-spacing:2px;}"
        "QPushButton#stopBtn:hover{background:#b8482f;}"
        "QPushButton#stopBtn:pressed{background:#8a311f;}"
        "QPushButton#startBtn:disabled{background:%4;color:%6;}"
        "QLabel#editCap{color:%6;font-family:'Noto Sans SC';font-size:13px;"
        " background:transparent;border:none;}"
        "QLabel#previewTitle{color:%6;font-family:'Chakra Petch';"
        " font-weight:600;font-size:14px;letter-spacing:4px;"
        " background:transparent;border:none;}"
        // v0.5 A shell: rail shares the window base colour (no colour band
        // between the rail text and the window behind it).
        // v0.5.6 layout: top row is rail (border-right) | inspector;
        // stage now sits beneath the top row. The divider line is
        // stage's own border-top, which paints correctly only when the
        // stage has any height — collapsing to 0 hides the border
        // automatically (no leftover sliver).
        "QFrame#objectRail{background:%1;border-right:1px solid %8;}"
        "QFrame#inspectorHost{background:%2;}"
        "QFrame#stage{background:%1;border-top:1px solid %8;}"
        "QLabel#railBrand{font-family:'Chakra Petch';font-size:17px;letter-spacing:2px;color:%2;}"
        "QLabel#railBrandSub,QLabel#railHint{font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;color:%6;}"
        "QLabel#sectionCap{font-family:'Chakra Petch';font-size:12px;letter-spacing:2px;color:%6;}"
        "QFrame#navPlayer,QFrame#navMonster,QFrame#navDamage{background:transparent;border:1px solid transparent;border-radius:3px;}"
        "QFrame#navPlayer:hover,QFrame#navMonster:hover,QFrame#navDamage:hover{background:%3;}"
        "QFrame#navPlayer[selected=\"true\"],QFrame#navMonster[selected=\"true\"],QFrame#navDamage[selected=\"true\"]{background:%1;border-color:%8;}"
        "QFrame#navPlayer   > QLabel#navEnabled {color:%11;}"
        "QFrame#navMonster  > QLabel#navEnabled {color:%9;}"
        "QFrame#navDamage   > QLabel#navEnabled {color:%10;}"
        "QLabel#navTitle{font-family:'Chakra Petch';font-size:15px;letter-spacing:1px;color:%2;}"
        "QLabel#navSummary{font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;color:%5;}"
        "QLabel#navEnabled{font-size:11px;color:%10;}"
        "QPushButton#railAction{background:transparent;color:%6;border:1px solid %8;border-radius:3px;"
        "text-align:left;padding:11px 18px;font-family:'Chakra Petch';font-size:13px;letter-spacing:1px;}"
        "QPushButton#railAction:hover{background:%3;color:%2;}"
        "QPushButton#railAction:disabled{color:%6;}"
        "QLabel#inspectorTitle{font-family:'Chakra Petch';font-size:24px;letter-spacing:2px;color:%2;}"
        "QLabel#inspectorSub{font-family:'Noto Sans SC';font-size:14px;color:%6;}"
        "QLabel#countLabel{font-family:'Chakra Petch';font-size:14px;font-weight:600;letter-spacing:1px;color:%2;}"
        "QLabel#countLabelP{color:%11;}"
        "QLabel#countLabelM{color:%9;}"
        "QLabel#countLabelD{color:%10;}"
        "QLabel#sliderLabel{color:%6;font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;min-width:52px;}"
        "QLabel#sliderValue{color:%2;font-family:'Chakra Petch';font-size:13px;min-width:36px;}"
        "QPushButton#foldout{background:transparent;color:%6;border:1px solid %8;border-radius:3px;"
        "text-align:left;padding:13px 20px;font-family:'Chakra Petch';font-size:13px;letter-spacing:1px;}"
        "QPushButton#foldout:disabled:hover{background:%3;}"
        "QPushButton#resetButton{background:transparent;color:%6;border:1px solid %8;border-radius:3px;"
        "padding:11px 18px;font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;}"
        "QPushButton#resetButton:disabled:hover{background:%3;}"
        "QLabel#modified{font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;color:%6;}"
        "QLabel#posLabel{font-family:'Chakra Petch';font-size:13px;font-weight:600;letter-spacing:0.5px;"
        "color:%2;background:%3;border:1px solid %8;border-radius:3px;"
        "padding:8px 13px;margin-top:4px;min-width:330px;}"
        "QScrollBar:vertical{width:7px;background:%2;}QScrollBar::handle:vertical{background:%8;min-height:24px;}"
        "QSlider{background:transparent;border:none;}"
        "QSlider::groove:horizontal{height:4px;background:%4;border-radius:2px;}"
        "QSlider::handle:horizontal{width:14px;height:14px;margin:-5px 0;background:%2;border-radius:7px;}"
        "QSlider::sub-page:horizontal{background:%11;border-radius:2px;}"
        "QPushButton#stageToggle{background:transparent;color:%6;border:1px solid %8;border-radius:3px;padding:7px 18px;font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;}"
        "QPushButton#stageToggle:hover{border-color:%8;color:%2;}"
        "QPushButton#stageToggle:checked{background:%3;border-color:%11;color:%11;}"
        "QPushButton#themeToggle{background:transparent;color:%6;border:1px solid %8;border-radius:3px;padding:7px 16px;font-family:'Chakra Petch';font-size:12px;letter-spacing:1px;}"
        "QPushButton#themeToggle:hover{border-color:%8;color:%2;}"
    );
    // Replace longest placeholders first. QString::arg historically treats
    // %1 as a prefix of %10/%11 in chained substitutions, which produced
    // startup warnings and corrupted the P/M/D accent colours.
    qss.replace(QStringLiteral("%11"), purple);
    qss.replace(QStringLiteral("%10"), teal);
    qss.replace(QStringLiteral("%9"), orange);
    qss.replace(QStringLiteral("%8"), soft);
    qss.replace(QStringLiteral("%7"), border);
    qss.replace(QStringLiteral("%6"), fgDim);
    qss.replace(QStringLiteral("%5"), fgMut);
    qss.replace(QStringLiteral("%4"), bgTrack);
    qss.replace(QStringLiteral("%3"), bgPanel);
    qss.replace(QStringLiteral("%2"), fg);
    qss.replace(QStringLiteral("%1"), bg);
    return qss;
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
    const UiTheme &t = uiTheme();
    if (panel == 0) return t.accentPurple;
    if (panel == 1) return t.accentOrange;
    return t.accentTeal;
}

} // namespace

ControlPanel::ControlPanel(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName("mhw-control-panel");
    setStyleSheet(qssBase());
    setWindowTitle(QStringLiteral("MHW Overlay Control"));
    // v0.5.6: top row consumes rail+inspector height (~600-700px);
    // stage must keep at least canvas's 360px minimum + stagebar padding.
    // Bump default height so the canvas is usable on first open.
    resize(1280, 1040);
    setMinimumSize(1120, 820);

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

    // v0.5.6 layout: top row = object rail (left, fixed 214px) + focused
    // inspector (right, fixed 420px). Bottom row = unified canvas stage
    // spanning the full window width, so the live HUD canvas has the
    // horizontal room the previous "rail + inspector + stage" three-
    // column layout denied it. The canvas is the workbench — it owns
    // the wide axis for drag and the 16:9 aspect; the rail/inspector
    // only consume the top strip.
    //
    // v0.5.6 polish: split the top row from the stage with a vertical
    // QSplitter so the user can resize the proportions live (the canvas
    // is the most-used surface; default ~38/62 in favour of the stage).
    // The splitter handle is hidden via QSS to keep the "single window"
    // feel; the stage's border-top doubles as the visual divider.
    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->setChildrenCollapsible(true);     // stage can fully collapse to 0
    splitter->setHandleWidth(0);               // no visible splitter bar
    splitter->setObjectName("consoleSplitter");

    auto *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(0);

    // v0.5 A: object rail → focused inspector → unified canvas.
    auto *rail = new QFrame();
    rail->setObjectName("objectRail");
    rail->setFixedWidth(214);
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(20, 22, 20, 18);
    railLayout->setSpacing(8);

    // v0.5.6: brand + READY pinned at top (always visible, identity).
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
    railLayout->addSpacing(14);

    // v0.5.6 polish: collapsible stage — single button in the rail that
    // toggles the bottom canvas area. Pinned in the top block (always
    // visible) so the user never has to find the canvas to dismiss it.
    // The ⌄/⌃ icon hints at the direction the stage moves on click.
    stageToggleBtn_ = new QPushButton(QStringLiteral("⌄   HIDE STAGE"));
    stageToggleBtn_->setObjectName("railAction");
    stageToggleBtn_->setCheckable(true);
    stageToggleBtn_->setChecked(true);
    stageToggleBtn_->setCursor(Qt::PointingHandCursor);
    railLayout->addWidget(stageToggleBtn_);
    railLayout->addSpacing(10);

    // v0.5.6: HUD OBJECTS + WORKSPACE go in their own scroll area so the
    // future addition of new objects / workspaces doesn't push the START
    // button off the rail. Pin brand/ready above and START/hint below;
    // only the middle list scrolls.
    auto *railScroll = new QScrollArea();
    railScroll->setObjectName("railScroll");
    railScroll->setWidgetResizable(true);
    railScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    railScroll->setFrameShape(QFrame::NoFrame);
    auto *scrollContent = new QWidget();
    auto *scrollLayout  = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->setSpacing(8);

    auto *objectsTitle = new QLabel(QStringLiteral("HUD OBJECTS"));
    objectsTitle->setObjectName("sectionCap");
    scrollLayout->addWidget(objectsTitle);
    scrollLayout->addWidget(buildObjectButton(QStringLiteral("P"), QStringLiteral("PLAYER"),
                                             QStringLiteral("玩家状态"), 0));
    scrollLayout->addWidget(buildObjectButton(QStringLiteral("M"), QStringLiteral("MONSTER"),
                                             QStringLiteral("怪物 HP"), 1));
    scrollLayout->addWidget(buildObjectButton(QStringLiteral("D"), QStringLiteral("DAMAGE"),
                                             QStringLiteral("DPS / 占比"), 2));
    scrollLayout->addSpacing(20);

    auto *workspaceTitle = new QLabel(QStringLiteral("WORKSPACE"));
    workspaceTitle->setObjectName("sectionCap");
    scrollLayout->addWidget(workspaceTitle);
    editBtn_ = new QPushButton(QStringLiteral("◇   LAYOUT MODE"));
    editBtn_->setObjectName("railAction");
    editBtn_->setCursor(Qt::PointingHandCursor);
    scrollLayout->addWidget(editBtn_);
    auto *presets = new QPushButton(QStringLiteral("▱   PRESETS"));
    presets->setObjectName("railAction");
    presets->setEnabled(false); // visual placeholder; no preset API yet
    scrollLayout->addWidget(presets);
    scrollLayout->addStretch(1);
    railScroll->setWidget(scrollContent);
    railLayout->addWidget(railScroll, 1);

    railLayout->addSpacing(18);

    // v0.5.6: START + ESC hint pinned at bottom (always visible, CTA).
    startBtn_ = new QPushButton(QStringLiteral("▶   START OVERLAY"));
    startBtn_->setObjectName("startBtn");
    startBtn_->setCursor(Qt::PointingHandCursor);
    railLayout->addWidget(startBtn_);
    auto *hint = new QLabel(QStringLiteral("ESC 返回 · 1/2/3 选择"));
    hint->setObjectName("railHint");
    hint->setAlignment(Qt::AlignCenter);
    railLayout->addWidget(hint);
    topRow->addWidget(rail);

    auto *inspectorHost = new QFrame();
    inspectorHost->setObjectName("inspectorHost");
    inspectorHost->setFixedWidth(420);
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
    topRow->addWidget(inspectorHost);

    auto *stage = new QFrame();
    stage->setObjectName("stage");
    // v0.5.6 polish: stage must be allowed to collapse fully. The splitter
    // below uses setChildrenCollapsible(true); a non-zero minimum here
    // would silently clamp the hide animation and leave a 1-2px sliver.
    // The visible "divider" line is the QSS border-top, which the
    // animation handler toggles to 'none' as soon as stage < 4px.
    stage->setMinimumHeight(0);
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
    themeBtn_ = new QPushButton(isDarkTheme() ? QStringLiteral("☀  LIGHT") : QStringLiteral("☾  DARK"));
    themeBtn_->setObjectName("themeToggle");
    themeBtn_->setCursor(Qt::PointingHandCursor);
    connect(themeBtn_, &QPushButton::clicked, this, [this]() {
        setUiTheme(!isDarkTheme());
        themeBtn_->setText(isDarkTheme() ? QStringLiteral("☀  LIGHT") : QStringLiteral("☾  DARK"));
        // Clear the old application stylesheet first. Qt 6.11 keeps
        // cached selector colours on existing QPushButtons if a new QSS
        // is assigned directly; clearing makes the following assignment
        // a real style reset instead of an incremental merge.
        setStyleSheet({});
        setStyleSheet(qssBase());
        // SectionRow owns inline QLabels, therefore parent QSS cannot
        // update CONNECTION / QUEST / etc. Rebuild those child styles
        // explicitly while the new theme is active.
        for (auto &c : ctl_)
            for (SectionRow *row : c.subs)
                row->refreshTheme();
        repolishAllWidgets();
    });
    stagebar->addWidget(themeBtn_);
    stageLayout->addLayout(stagebar);

    canvas_ = new HudCanvas();
    canvas_->installEventFilter(this);
    auto *canvasScroll = new QScrollArea();
    canvasScroll->setObjectName("canvasScroll");
    canvasScroll->setWidget(canvas_);
    // v0.5.6: stage now owns the full window width, so let the canvas
    // stretch horizontally to fill the viewport. The paint path already
    // recomputes layout from width()/height(), so widening the widget
    // widens the 16:9 frame and the three panel slots inside it.
    // Vertical overflow still scrolls (canvas heightForWidth enforces
    // the 16:9 + header/footer ratio).
    canvasScroll->setWidgetResizable(true);
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
    // v0.5.6: top row (rail + inspector) sits in a container that owns
    // the QHBoxLayout; the stage is the second pane of the splitter.
    // Wrap the topRow in a QWidget so QSplitter can manage its size
    // independently of the central widget layout. Default ratio ~38/62
    // (topContainer:stage) gives the canvas the wide axis it deserves
    // while still leaving room for the inspector's 6 rows.
    auto *topContainer = new QWidget();
    topContainer->setObjectName("topContainer");
    topContainer->setLayout(topRow);
    splitter->addWidget(topContainer);
    splitter->addWidget(stage);
    // v0.5.6 polish: ~45/55 split — the canvas (the actual workbench)
    // gets the majority of vertical room, but the top strip keeps the
    // full HUD OBJECTS list + WORKSPACE rows + SCALE/OPACITY sliders
    // visible without scrolling on a 1280×1040 default window.
    splitter->setSizes({470, 570});
    // Remember the user's chosen (or default) stage size so animStageTo
    // can restore it on show. DO NOT read it from splitter->sizes() here:
    // at construction time the splitter isn't in a layout yet, so
    // setSizes clamps to the widget minimums ([332,148] for a 1040px
    // window) — the correct 570 default would be lost. Use the constant
    // default, then let QSettings + splitterMoved override it later.
    savedStageSize_ = 570;
    // Hard minimum on the top pane so the inspector doesn't get crushed.
    topContainer->setMinimumHeight(360);
    {
        QSettings s;
        const QByteArray saved = s.value(QStringLiteral("ui/splitter")).toByteArray();
        if (!saved.isEmpty()) splitter->restoreState(saved);
        // After restore, keep the user's last-chosen stage height as a
        // plain int (easier than decoding the splitter bytearray).
        savedStageSize_ = s.value(QStringLiteral("ui/stageHeight"), 570).toInt();
    }
    consoleSplitter_ = splitter;

    auto *central = new QWidget();
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(splitter);
    setCentralWidget(central);

    // v0.5.6 polish: persist the splitter ratio whenever the user drags
    // it, so the next launch opens with the proportions they preferred.
    connect(splitter, &QSplitter::splitterMoved, this, [this, splitter]{
        // Animation frames call setSizes → splitterMoved thousands of
        // times; we must NOT persist or update savedStageSize_ from an
        // animated frame or we'd store the animation's intermediate
        // value (e.g. 120px) as the user's chosen ratio. Only a real
        // user drag (animation not running) may write QSettings.
        if (stageAnim_ && stageAnim_->state() == QAbstractAnimation::Running)
            return;
        const int newStage = splitter->sizes().value(1, 570);
        QSettings s;
        s.setValue(QStringLiteral("ui/splitter"), splitter->saveState());
        s.setValue(QStringLiteral("ui/stageHeight"), newStage);
        if (stageVisible_) {
            savedStageSize_ = newStage;
        }
    });

    // v0.5.6 polish: wire the HIDE/SHOW STAGE toggle. QPropertyAnimation
    // drives a custom int property (the stage pane height); each frame
    // re-applies setSizes({top, anim}) so the splitter smoothly resizes.
    stageAnim_ = new QPropertyAnimation(this, "stagePaneHeight");
    stageAnim_->setDuration(200);
    stageAnim_->setEasingCurve(QEasingCurve::InOutQuad);
    connect(stageAnim_, &QPropertyAnimation::valueChanged,
            this, [this](const QVariant &v){
        if (!consoleSplitter_) return;
        const int stageH = v.toInt();
        const int topH = consoleSplitter_->sizes().value(0);
        consoleSplitter_->setSizes({topH, stageH});
    });
    // v0.5.6 polish: when the hide animation finishes, force stage to
    // exactly 0 — the splitter occasionally clamps the last frame to its
    // own minimum (≈1-2px) when its pane is collapsible but other widgets
    // still expect a non-zero gutter. setSizes({x, 0}) with a final
    // stage setMaximumHeight(0) lets the stage fully vanish, and the
    // SHOW path restores both back. This is also what kills the
    // "stays stuck — only jiggles" bug when the user re-opens stage.
    connect(stageAnim_, &QPropertyAnimation::finished,
            this, [this]{
        if (!consoleSplitter_) return;
        if (stageVisible_) {
            // show complete: clear any temporary max cap.
            if (auto *stg = consoleSplitter_->widget(1)) stg->setMaximumHeight(QWIDGETSIZE_MAX);
            const int topH = consoleSplitter_->sizes().value(0);
            const int stageH = savedStageSize_ > 0 ? savedStageSize_ : 570;
            consoleSplitter_->setSizes({topH, stageH});
        } else {
            // hide complete: force stage to zero. The QSplitter insists
            // on a minimum pane size (~120 in our case) even when the
            // child has minimumSize 0 and is collapsible; we work around
            // it by setting stage's MAX height to 0 so it really
            // collapses, then setSizes({top, 0}). setVisible(false) was
            // tried but the splitter kept its 120px reservation.
            if (auto *stg = consoleSplitter_->widget(1)) {
                stg->setMaximumHeight(0);
            }
            const int topH = consoleSplitter_->sizes().value(0);
            consoleSplitter_->setSizes({topH, 0});
        }
    });
    connect(stageToggleBtn_, &QPushButton::toggled, this, [this](bool checked){
        stageToggleBtn_->setText(checked ? QStringLiteral("⌄   HIDE STAGE")
                                         : QStringLiteral("⌃   SHOW STAGE"));
        animStageTo(checked);
    });

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

// ─── stage toggle property + animation ─────────────────────────────────────

int ControlPanel::stagePaneHeight() const
{
    return stagePaneHeight_;
}

void ControlPanel::setStagePaneHeight(int h)
{
    // QPropertyAnimation writes the value here every animation tick.
    stagePaneHeight_ = h;
}

void ControlPanel::animStageTo(bool visible)
{
    if (!consoleSplitter_ || !stageAnim_) return;
    stageVisible_ = visible;
    // v0.5.6 polish: when showing, the previous hide pinned stage's
    // maximumHeight to 0. Restore the cap first so the splitter can
    // grow stage back to its saved size; otherwise the cap clamps at
    // 0 and the animation looks stuck.
    if (visible) {
        if (auto *stg = consoleSplitter_->widget(1)) {
            stg->setMaximumHeight(QWIDGETSIZE_MAX);
        }
    }
    // Snapshot the current stage size as the "from" so an in-flight
    // animation can be reversed mid-flight without snapping.
    const int current = consoleSplitter_->sizes().value(1);
    // When hiding, capture the user's preferred stage height so SHOW
    // restores it (not whatever the splitter has after animation).
    if (visible && savedStageSize_ <= 0) {
        savedStageSize_ = current > 0 ? current : 570;
    }
    const int target = visible ? (savedStageSize_ > 0 ? savedStageSize_ : 570) : 0;
    // Avoid pointless animation if we're already there.
    if (current == target) return;
    stageAnim_->stop();
    stageAnim_->setStartValue(current);
    stageAnim_->setEndValue(target);
    stageAnim_->start();
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

    // A real child frame, rather than a parent-QSS border selector: this
    // keeps the identity stripe visible through runtime theme swaps.
    auto *accentBar = new QFrame();
    accentBar->setFixedWidth(3);
    accentBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    const QColor accent = panelAccent(idx);
    accentBar->setStyleSheet(QStringLiteral("background:%1;border:none;border-radius:1px;")
                                 .arg(accent.name()));
    row->addWidget(accentBar);

    auto *badge = new QLabel(letter);
    badge->setObjectName("navBadge");
    badge->setStyleSheet(QStringLiteral(
        "color:%1;background:transparent;border:1.5px solid %1;"
        "border-radius:3px;font-family:'Chakra Petch';font-weight:700;"
        "font-size:17px;").arg(accent.name()));
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
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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

    // v0.5 BG OPACITY: controls the panel background alpha independently
    // of the overall window opacity. KWin's blur-behind is a compositor
    // effect on translucent surfaces; this slider adjusts how much of
    // the game scene shows through the panel body.
    auto *bgRow = new QHBoxLayout();
    bgRow->setSpacing(8);
    auto *bgLab = new QLabel(QStringLiteral("BG ALPHA"));
    bgLab->setObjectName("sliderLabel");
    auto *bgVal = new QLabel();
    bgVal->setObjectName("sliderValue");
    auto *bgSlider = new QSlider(Qt::Horizontal);
    bgSlider->setRange(0, 255);
    {
        Panel *p3 = (idx == 0 ? static_cast<Panel*>(player_)
                   : idx == 1 ? static_cast<Panel*>(monster_)
                              : static_cast<Panel*>(damage_));
        bgSlider->setValue(p3->bgAlpha());
    }
    bgVal->setText(QStringLiteral("%1").arg(bgSlider->value()));
    connect(bgSlider, &QSlider::valueChanged, this, [this, idx, bgVal](int v){
        bgVal->setText(QStringLiteral("%1").arg(v));
        Panel *p = (idx == 0 ? static_cast<Panel*>(player_)
                  : idx == 1 ? static_cast<Panel*>(monster_)
                             : static_cast<Panel*>(damage_));
        p->setBgAlpha(v, false);
        rebuildAndRender(idx);
    });
    bgRow->addWidget(bgLab);
    bgRow->addWidget(bgSlider, 1);
    bgRow->addWidget(bgVal);
    vl->addLayout(bgRow);
    ctl_[idx].bgAlphaSlider = bgSlider;
    // v0.5 BEHAVIOR → POSITION: shows the panel's anchor corner and
    // current margins. The user moves the panel via canvas drag or
    // arrow keys; this label is read-only feedback.
    auto *posLabel = new QLabel();
    posLabel->setObjectName("posLabel");
    // Fixed width: margin digits change as the user drags the panel;
    // without a fixed width the label reflows and the whole inspector
    // column width jitters every frame.
    posLabel->setFixedWidth(350);
    posLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
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
    // First paint seeds demo data and applies setContentSize() at the
    // panel's current scale. Do NOT resize to contentSize() here: that
    // logical (unscaled) size would overwrite Panel::setScale() and make
    // the SCALE slider display 50% while rendering at 100%.
    p->repaint();
    const QSize sz = p->size();
    // paintEvent composites at the panel's opacity via p.setOpacity(),
    // so the rendered pixmap already carries per-pixel alpha. No
    // disable/enable dance needed.
    QPixmap pix(sz);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    p->render(&painter, QPoint(), QRect(QPoint(0, 0), sz));
    painter.end();

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
        if (c.bgAlphaSlider) c.bgAlphaSlider->setValue(panel->bgAlpha());
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
    // Display the panel's *current screen quadrant*, not its immutable
    // layer-shell anchor. Anchor stays fixed (so drag remains stable),
    // while this label follows the actual position when margins cross a
    // half-screen line.
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QRect g = screen ? screen->geometry() : QRect(0, 0, 1, 1);
    const QSize content = p->contentSize() * p->scale();
    int x = g.left();
    int y = g.top();
    switch (p->corner()) {
    case Corner::TopLeft:     x += m.left(); y += m.top(); break;
    case Corner::TopRight:    x = g.right() - content.width() - m.right(); y += m.top(); break;
    case Corner::BottomLeft:  x += m.left(); y = g.bottom() - content.height() - m.bottom(); break;
    case Corner::BottomRight: x = g.right() - content.width() - m.right(); y = g.bottom() - content.height() - m.bottom(); break;
    }
    const QPoint center(x + content.width() / 2, y + content.height() / 2);
    const bool left = center.x() < g.center().x();
    const bool top = center.y() < g.center().y();
    const QString corner = top ? (left ? QStringLiteral("TOP LEFT") : QStringLiteral("TOP RIGHT"))
                               : (left ? QStringLiteral("BOTTOM LEFT") : QStringLiteral("BOTTOM RIGHT"));
    ctl_[idx].posLabel->setText(
        QStringLiteral("POSITION: %1  ·  L%2 T%3 R%4 B%5")
            .arg(corner).arg(m.left()).arg(m.top())
            .arg(m.right()).arg(m.bottom()));
}
