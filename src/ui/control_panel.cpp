#include "control_panel.h"

#include "ui/panel.h"
#include "ui/panel_player.h"
#include "ui/panel_monster.h"
#include "ui/panel_damage.h"
#include "ui/panel_sections.h"
#include "ui/toggle_chip.h"
#include "ui/section_row.h"
#include "core/string_table.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFrame>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
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

const char *kPanelBg = "#101416";
const char *kPanelFg = "#d3d4d5";
const char *kAccent  = "#ff8040";

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
        "QPushButton#startBtn:disabled{background:#4a2010;color:#806050;}"
        "QLabel#editCap{color:#6f7375;font-family:'Noto Sans SC';font-size:10px;"
        " background:transparent;border:none;}"
        // R6: right-column title ("MOCK PREVIEW")
        "QLabel#previewTitle{color:#6f7375;font-family:'Chakra Petch';"
        " font-weight:600;font-size:11px;letter-spacing:4px;"
        " background:transparent;border:none;}"
        );
}

} // namespace

ControlPanel::ControlPanel(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName("mhw-control-panel");
    setStyleSheet(qssBase());
    setWindowTitle(QStringLiteral("MHW Overlay Control"));
    resize(1180, 980);

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
        // grab to the top slice.
        p->show();
    }

    auto *root = new QVBoxLayout();
    root->setContentsMargins(36, 22, 36, 22);   // R1: wider gutters to
                                                // give the logo row room,
                                                // matching HTML v0.4
    root->setSpacing(18);

    // ---- R1: logo row (MHW OVERLAY CONTROL + sub + PREVIEW badge) ----
    auto *logo = new QHBoxLayout();
    logo->setSpacing(12);

    auto *leftStack = new QVBoxLayout();
    leftStack->setSpacing(4);
    auto *logoRow = new QHBoxLayout();
    logoRow->setSpacing(0);
    auto *l1 = new QLabel(QStringLiteral("MHW "));
    l1->setObjectName("logoTitle");
    auto *l2 = new QLabel(QStringLiteral("OVERLAY "));
    l2->setObjectName("logoAccent");
    auto *l3 = new QLabel(QStringLiteral("CONTROL"));
    l3->setObjectName("logoTitle");
    logoRow->addWidget(l1);
    logoRow->addWidget(l2);
    logoRow->addWidget(l3);
    logoRow->addStretch(1);
    leftStack->addLayout(logoRow);
    auto *sub = new QLabel(QStringLiteral("v0.4 · MOCK PREVIEW"));
    sub->setObjectName("logoSub");
    leftStack->addWidget(sub);
    logo->addLayout(leftStack, 1);

    // Right side: PREVIEW badge (● PREVIEW, teal dot + label).
    auto *badge = new QHBoxLayout();
    badge->setSpacing(6);
    badge->addStretch(1);
    auto *dot = new QLabel(QStringLiteral("●"));
    dot->setObjectName("logoBadgeDot");
    auto *label = new QLabel(QStringLiteral("PREVIEW"));
    label->setObjectName("logoBadge");
    badge->addWidget(dot);
    badge->addWidget(label);
    logo->addLayout(badge, 0);

    // L4: status badge — added as a sibling of the PREVIEW badge, right
    // aligned to the right edge of the row. We reuse the same logo
    // layout (no nesting, no re-parenting) so the existing layout
    // state stays simple and PREVIEW keeps its place.
    auto *status = new QLabel(QStringLiteral("READY"));
    status->setObjectName("statusBadge");
    status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    logo->addWidget(status, 0, Qt::AlignRight | Qt::AlignVCenter);
    statusBadge_ = status;

    root->addLayout(logo);

    // ---- Two-column body: left = switches, right = previews ----
    auto *body = new QHBoxLayout();
    body->setSpacing(28);

    // ---- Left column: switches ----
    auto *left = new QVBoxLayout();
    left->setSpacing(12);
    left->addWidget(buildGroup(QStringLiteral("PLAYER"),
                               QStringLiteral("玩家状态"),
                               mhw::PlayerSection::displayNames(), 0));
    left->addWidget(buildRule());   // R5: hairline PLAYER | MONSTER
    left->addWidget(buildGroup(QStringLiteral("MONSTER"),
                               QStringLiteral("怪物 HP"),
                               mhw::MonsterSection::displayNames(), 1));
    left->addWidget(buildRule());   // R5: hairline MONSTER | DAMAGE
    left->addWidget(buildGroup(QStringLiteral("DAMAGE"),
                               QStringLiteral("DPS / 占比"),
                               mhw::DamageSection::displayNames(), 2));
    left->addWidget(buildRule());   // R5: hairline DAMAGE | EDIT MODE
    left->addWidget(buildEditModeBlock());
    left->addStretch(1);

    auto *leftScroll = new QScrollArea();
    leftScroll->setWidgetResizable(true);
    leftScroll->setMaximumWidth(360);
    auto *leftHost = new QWidget();
    leftHost->setLayout(left);
    leftScroll->setWidget(leftHost);
    body->addWidget(leftScroll, 0);

    // ---- Right column: live previews ----
    auto *right = new QVBoxLayout();
    // R6: right column has a small "MOCK PREVIEW" header at the very top
    // (HTML v0.4 styling — grey, wide tracking, no chrome). Then the
    // three live preview tiles.
    right->setSpacing(18);
    auto *rightTitle = new QLabel(QStringLiteral("MOCK PREVIEW"));
    rightTitle->setObjectName("previewTitle");
    right->addWidget(rightTitle);
    for (int i = 0; i < 3; ++i) {
        auto *lab = new QLabel();
        lab->setObjectName("previewFrame");
        lab->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        lab->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        ctl_[i].preview = lab;
        right->addWidget(lab, 0, Qt::AlignLeft);
    }
    right->addStretch(1);
    auto *rightScroll = new QScrollArea();
    rightScroll->setWidgetResizable(true);
    auto *rightHost = new QWidget();
    rightHost->setLayout(right);
    rightScroll->setWidget(rightHost);
    body->addWidget(rightScroll, 1);

    root->addLayout(body, 1);   // R1: body stretches below the logo

    auto *central = new QWidget();
    central->setLayout(root);
    setCentralWidget(central);

    // L2: pull persisted mask state from disk BEFORE the first render so
    // the section checkboxes and master toggles open in the user's last
    // configuration. Missing file = all-on (default mask).
    loadMaskFromDisk();

    // First render with everything on.
    for (int i = 0; i < 3; ++i)
        rebuildAndRender(i);
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

    saveMaskToDisk();
    QMainWindow::closeEvent(e);
}

QWidget *ControlPanel::buildGroup(const QString &title, const QString &sub,
                                   const QStringList &labels, int idx)
{
    // R2: group is a plain QWidget (not QGroupBox) so we control the
    // title chrome ourselves: a LetterBadge (P/M/D coloured block) +
    // title label + subtitle on a single horizontal row, matching the
    // HTML v0.4 side-bar layout. QGroupBox's title subcontrol would
    // have forced us through stylesheet and fought us on alignment.
    auto *box = new QWidget();
    auto *vl = new QVBoxLayout();
    vl->setSpacing(10);
    vl->setContentsMargins(0, 14, 0, 0);   // gutter between groups

    // ---- Title row: [Badge] PANEL  ← subtitle on a new line
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);
    titleRow->setContentsMargins(0, 0, 0, 0);

    auto *badge = new QLabel(idx == 0 ? QStringLiteral("P")
                                       : idx == 1 ? QStringLiteral("M")
                                                   : QStringLiteral("D"));
    badge->setObjectName(idx == 0 ? QStringLiteral("badgeP")
                                  : idx == 1 ? QStringLiteral("badgeM")
                                              : QStringLiteral("badgeD"));
    badge->setFixedSize(22, 22);
    badge->setAlignment(Qt::AlignCenter);

    auto *titleLab = new QLabel(title);
    titleLab->setObjectName("groupTitle");
    titleLab->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    titleRow->addWidget(badge);
    titleRow->addWidget(titleLab, 1);

    auto *subLab = new QLabel(sub);
    subLab->setObjectName("groupSub");
    subLab->setAlignment(Qt::AlignBottom | Qt::AlignLeft);
    titleRow->addWidget(subLab, 0);

    vl->addLayout(titleRow);

    // R3+R4: Master row = ToggleChip on the left + descriptive label
    // "面板启用" + small caption "关闭后预渲染面板置灰". Then sub-rows
    // are SectionRow instances: dot + Chinese label + grey english key,
    // laid out in a 2-column grid inside a separate subGrid container so
    // long lists wrap cleanly without disturbing the title row.
    auto *masterRow = new QHBoxLayout();
    masterRow->setSpacing(10);
    auto *master = new ToggleChip();
    master->setChecked(true);
    ctl_[idx].master = master;
    masterRow->addWidget(master);
    auto *masterLab = new QLabel(QStringLiteral("面板启用"));
    masterLab->setStyleSheet(QStringLiteral(
        "color:#e7e8e9;font-family:'Noto Sans SC';font-size:12px;"
        "background:transparent;border:none;"));
    masterRow->addWidget(masterLab);
    auto *masterCap = new QLabel(QStringLiteral("关闭后预渲染面板置灰"));
    masterCap->setStyleSheet(QStringLiteral(
        "color:#6f7375;font-family:'Noto Sans SC';font-size:10px;"
        "background:transparent;border:none;padding-left:6px;"));
    masterRow->addWidget(masterCap);
    masterRow->addStretch(1);
    vl->addLayout(masterRow);

    // ---- Sub-switches (R4b): SectionRow per section, in a 2-col grid ----
    // Matches HTML v0.4 layout: each row pair lives on a single line
    // (2-column grid), wrapping to a new line every 2 items. Layout
    // order: item0 item1 / item2 item3 / item4 item5.
    auto *subGrid = new QGridLayout();
    subGrid->setContentsMargins(0, 0, 0, 0);
    subGrid->setHorizontalSpacing(8);   // gap between the two columns
    subGrid->setVerticalSpacing(6);
    const QStringList &keys =
        idx == 0 ? mhw::PlayerSection::names() :
        idx == 1 ? mhw::MonsterSection::names() :
                   mhw::DamageSection::names();
    for (int b = 0; b < labels.size(); ++b) {
        auto *sr = new SectionRow(labels[b],
                                 b < keys.size() ? keys[b] : QString());
        sr->setChecked(true);
        ctl_[idx].subs.push_back(sr);
        const int row = b / 2;
        const int col = b % 2;
        subGrid->addWidget(sr, row, col, 1, 1);
    }
    // Make the two columns share width evenly so a one-item final row
    // doesn't leave a giant gap.
    subGrid->setColumnStretch(0, 1);
    subGrid->setColumnStretch(1, 1);
    vl->addLayout(subGrid);
    box->setLayout(vl);

    // Wire signals: any switch in this group rebuilds + re-renders. PMF
    // syntax needs the concrete signal signature; lambda with sender is
    // the alternative but we already have idx in scope.
    auto rewire = [this, idx](int){ rebuildAndRender(idx); };
    connect(master, &ToggleChip::stateChanged, this, rewire);
    for (auto *sr : ctl_[idx].subs)
        connect(sr, &SectionRow::stateChanged, this, rewire);
    return box;
}

// R5: 1px hairline between groups (QFrame with HLine shape + custom QSS).
QWidget *ControlPanel::buildRule()
{
    auto *line = new QFrame();
    line->setObjectName("rule");
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
    if (!ctl_[idx].master->isChecked()) {
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
        lab->setPixmap(ph);
        return;
    }

    uint32_t mask = 0;
    for (int b = 0; b < ctl_[idx].subs.size(); ++b)
        if (ctl_[idx].subs[b]->isChecked())
            mask |= (1u << b);
    panel->setSectionMask(mask);   // also calls update()
    lab->setPixmap(renderPreview(panel));
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
