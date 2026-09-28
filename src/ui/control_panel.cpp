#include "control_panel.h"

#include "ui/panel.h"
#include "ui/panel_player.h"
#include "ui/panel_monster.h"
#include "ui/panel_damage.h"
#include "ui/panel_pet_damage.h"
#include "ui/panel_sections.h"
#include "ui/toggle_chip.h"
#include "ui/section_row.h"
#include "ui/section_count_bar.h"
#include "ui/hud_canvas.h"
#include "ui/panel_source.h"
#include "ui/screen_query.h"
#include "ui/ui_theme.h"
#include "ui/control_panel_styles.h"
#include "ui/viewmodel/console_text_helpers.h"
#include "ui/viewmodel/overlay_process_controller.h"
#include "ui/viewmodel/rise_reframework_status.h"
#include "ui/viewmodel/console_layout_store.h"
#include "ui/viewmodel/panel_mask_codec.h"
#include "core/game_detector.h"
#include "core/game_profile.h"
#include "core/rise_reframework_manager.h"
#include "core/steam_game_locator.h"
#include "core/locale_conf.h"
#include "core/string_table.h"

namespace mh {
// v0.9 i18n: local alias for the shared StringTable. Identical definition to
// panel_player.cpp / panel_monster.cpp / panel_damage.cpp (inline → ODR-safe),
// so the console and the overlay panels resolve their copy through one path.
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QComboBox>
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
#include <QMessageBox>
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
#include <QSaveFile>
#include <QProcess>
#include <QTextStream>
#include <QTime>
#include <signal.h>
#include <sys/types.h>

// v0.11: the console stage-pane default height lives as the named
// constant mhw::kConsoleStageHeightDefault (ConsoleLayoutStore) so tests
// and the panel read one source of truth instead of a re-typed literal.
// This file already includes that header; pull the name in once at file
// scope because the stage-pane call sites (below) are member functions
// at global scope where the unqualified name is otherwise invisible.
using mhw::kConsoleStageHeightDefault;

// The three text dispatchers that moved out of the anonymous namespace
// below — consoleText(), gameName(), sectionLabel() — now live in
// src/ui/viewmodel/console_text_helpers.{h,cpp} as mhw:: names. Pull
// them in once at file scope so their call sites (ControlPanel member
// functions sitting at global scope, where an unqualified mhw:: name is
// invisible) stay exactly as they were written. Same pattern as the
// kConsoleStageHeightDefault declaration directly above.
using mhw::consoleText;
using mhw::gameName;
using mhw::sectionLabel;

namespace {

// ---- what stays in this anonymous namespace, and why -------------------
//
// The four text/QSS dispatchers that used to open this namespace have
// moved out to their own translation units: the three text helpers
// (console_text_helpers.{h,cpp}) and the 220-line QSS builder
// (control_panel_styles.{h,cpp}, as consoleStyleSheet()). What is left is
// exactly the set that cannot leave — each one reads a live GUI object.
// prepareDamagePreview needs a DamagePanel plus Panel's virtual demo
// hook, iconKind() needs the SectionRow::Icon enum, panelAccent() reads
// the live uiTheme() palette. None can run without a constructed
// console, so they stay with the View.

void prepareDamagePreview(DamagePanel *damage,
                          const mhw::RiseDamageDisplayOptions &options)
{
    // DamagePanel's production/demo implementation is frozen. Re-seed its
    // ordinary four-hunter demo from the console, then replace it with a
    // one-hunter Rise snapshot when OtherMembers is disabled. Calling through
    // Panel keeps the virtual demo hook accessible without widening the
    // DamagePanel API.
    Panel *panel = damage;
    panel->setupDemoData();
    panel->markDemoPrimed();
    if (options.showOtherMembers)
        return;

    mhw::RiseDamageActor local;
    local.key = QStringLiteral("demo-player-local");
    local.kind = mhw::RiseDamageActorKind::Player;
    local.entityIndex = 0;
    local.displaySlot = 0;
    local.name = QStringLiteral("A27exe");
    local.total = 184220;
    local.local = true;

    mhw::RiseDamageSnapshot snapshot;
    snapshot.valid = true;
    snapshot.questActive = true;
    snapshot.questEpoch = 1;
    snapshot.actors.append(local);
    damage->updateRiseDamage(snapshot, options);
}


// v0.5 P2: map (panel index, section bit index) → SectionRow::Icon.
// The mapping follows panel_sections.h exactly.
int iconKind(int panel, int section)
{
    static const int kPlayerIcons[] = {
        SectionRow::IconConn, SectionRow::IconQuest, SectionRow::IconWeapon,
        SectionRow::IconBars, SectionRow::IconMantles, SectionRow::IconDebuff,
        SectionRow::IconBuff, SectionRow::IconWirebug,
    };
    static const int kMonsterIcons[] = {
        SectionRow::IconInfo, SectionRow::IconHp, SectionRow::IconEnrage,
        SectionRow::IconAil, SectionRow::IconParts,
    };
    static const int kDamageIcons[] = {
        SectionRow::IconRows, SectionRow::IconShare, SectionRow::IconChart,
        SectionRow::IconRows,
    };
    if (panel == 0 && section < 8) return kPlayerIcons[section];
    if (panel == 1 && section < 5) return kMonsterIcons[section];
    if (panel == 2 && section < mhw::DamageSection::kCount) return kDamageIcons[section];
    if (panel == 3 && section < mhw::PetDamageSection::kCount)
        return SectionRow::IconRows;
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

// v0.11.2: extracted from the two identical lambdas in
// launchOverlayChild() (argv builder) and saveMaskToDisk() (mask
// writer) — they were byte-for-byte the same 8 lines, so they now read
// the checkbox state through one shared private static helper. Body is
// unchanged from the lambda it replaces.
uint32_t ControlPanel::maskFor(const PanelCtl &c)
{
    if (!c.master->isChecked()) return 0u;
    uint32_t m = 0;
    for (int b = 0; b < c.subs.size(); ++b)
        if (c.subs[b]->isChecked())
            m |= (1u << b);
    return m;
}

Panel *ControlPanel::panelAt(int idx) const
{
    return mhw::isPanelIndex(idx) ? ctl_[idx].panel : nullptr;
}

ControlPanel::ControlPanel(QWidget *parent)
    : QMainWindow(parent)
{
    // v0.9 i18n: guarantee a loaded StringTable before any widget text is
    // built. main_control.cpp resolves --locale > conf > system locale before
    // constructing us (and every string below is read through it); this is
    // the safety net for embedders — e.g. tests/control_l2_smoke.cpp
    // constructs a bare ControlPanel with no startup resolution.
    if (mhw::StringTable::instance().currentLocale().isEmpty()) {
        // Mirrors the startup policy in main_control.cpp; the last resort is
        // the detected system locale, not a hardcoded zh-CN (v0.9.2).
        const QString fromConf = mhw::readLocaleFromConf();
        const QString fallback = mhw::systemLocale();
        if (fromConf.isEmpty() || !mhw::StringTable::instance().load(fromConf))
            mhw::StringTable::instance().load(fallback);
    }

    // Owned here — before any build*() call reads currentGame() — but
    // wired to the launcher buttons further down, once those exist.
    overlay_ = new mhw::OverlayProcessController(this);

    setObjectName("monster-control-panel");
    setStyleSheet(consoleStyleSheet());
    // i18n: the window title is not a widget text property, so it has its
    // own replay hook.
    trWindowTitle(QStringLiteral("console.window_title"));
    // v0.5.6: top row consumes rail+inspector height (~600-700px);
    // stage must keep at least canvas's 360px minimum + stagebar padding.
    // Bump default height so the canvas is usable on first open.
    // v0.7.5: default width 800 was too narrow — the brand rail and the
    // START OVERLAY button elided ("TART OVE") and the inspector rows
    // scrolled out of view. 1200 gives the three-column top row room
    // while the 16:9 stage below keeps its wide axis.
    resize(1200, 1040);
    setMinimumSize(960, 820);
    {
        const mhw::ConsoleLayoutStore layout;
        const mhw::ConsoleLayoutStore::WindowFrame frame = layout.windowFrame();
        if (!frame.geometry.isEmpty()) restoreGeometry(frame.geometry);
        if (!frame.windowState.isEmpty()) restoreState(frame.windowState);
    }

    // Real panel instances, rendered off-screen only. WA_DontShowOnScreen
    // lets show()/repaint() run the full paint path (demo data + the
    // setContentSize geometry) WITHOUT mapping a window — so no layer-shell
    // surface, no focus steal, no taskbar entry. This is the whole reason
    // the console is safe to run on-screen while the live overlay runs too.
    player_  = new PlayerPanel();
    monster_ = new MonsterPanel();
    damage_  = new DamagePanel();
    pets_    = new PetDamagePanel();
    const std::array<Panel *, mhw::kPanelCount> panels = {
        static_cast<Panel *>(player_),
        static_cast<Panel *>(monster_),
        static_cast<Panel *>(damage_),
        static_cast<Panel *>(pets_),
    };
    static_assert(panels.size() == mhw::kPanelCount);
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        Panel *p = panels[i];
        ctl_[i].panel = p;
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
    // v0.7.1: the rail no longer owns its own width — the horizontal
    // QSplitter below owns the split between the narrow game column
    // (left) and the rail (right). The rail keeps its 214px minimum via
    // its own minimumWidth so the resize handle still respects the
    // existing layout's "comfortable" width on first launch.
    auto *rail = new QFrame();
    rail->setObjectName("objectRail");
    rail->setMinimumWidth(130);
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(20, 22, 20, 18);
    railLayout->setSpacing(8);

    // v0.5.6: brand + READY pinned at top (always visible, identity).
    auto *brand = new QLabel();
    trSet(brand, QStringLiteral("console.brand"));
    brand->setObjectName("railBrand");
    auto *brandSub = new QLabel();
    brandSub->setObjectName("railBrandSub");
    // The version is substituted at runtime: writing it into the translation
    // string is how the console shipped "控制台 · 0.5" for four releases.
    trHook([brandSub] {
        brandSub->setText(mh::tr(QStringLiteral("console.brand_sub"))
                              .arg(QCoreApplication::applicationVersion()));
    });
    railLayout->addWidget(brand);
    railLayout->addWidget(brandSub);
    railLayout->addSpacing(22);

    auto *ready = new QLabel();
    trSet(ready, QStringLiteral("console.status.ready"));
    ready->setObjectName("statusBadge");
    statusBadge_ = ready;
    railLayout->addWidget(ready);
    railLayout->addSpacing(14);

    // v0.5.6 polish: collapsible stage — single button in the rail that
    // toggles the bottom canvas area. Pinned in the top block (always
    // visible) so the user never has to find the canvas to dismiss it.
    // The ⌄/⌃ icon hints at the direction the stage moves on click.
    stageToggleBtn_ = new QPushButton();
    updateStageToggleText();   // i18n: copy depends on the checked state
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

    // v0.7.1: the GAME selector itself moved to buildGameColumn() (its
    // own narrow column on the left edge of the window). The auto-
    // detect badge stays in the rail as a status line — clicking it
    // still hot-swaps to the detected game, the same way it did in v0.6.
    autoDetectBadge_ = new QLabel();
    trSet(autoDetectBadge_, QStringLiteral("console.detect.none"));
    autoDetectBadge_->setObjectName("autoDetect");
    autoDetectBadge_->setProperty("state", "gray");
    autoDetectBadge_->setWordWrap(true);
    autoDetectBadge_->installEventFilter(this);
    scrollLayout->addWidget(autoDetectBadge_);
    scrollLayout->addSpacing(20);

    auto *objectsTitle = new QLabel();
    trSet(objectsTitle, QStringLiteral("console.rail.hud_objects"));
    objectsTitle->setObjectName("sectionCap");
    scrollLayout->addWidget(objectsTitle);
    // i18n: the factory takes translation KEYS (not pre-translated text) so
    // retranslateUi() can re-query them on a language switch.
    scrollLayout->addWidget(buildObjectButton(QStringLiteral("P"),
                                              QStringLiteral("console.nav.player"),
                                              QStringLiteral("console.nav.player_summary"), 0));
    scrollLayout->addWidget(buildObjectButton(QStringLiteral("M"),
                                              QStringLiteral("console.nav.monster"),
                                              QStringLiteral("console.nav.monster_summary"), 1));
    scrollLayout->addWidget(buildObjectButton(QStringLiteral("D"),
                                              QStringLiteral("console.nav.damage"),
                                              QStringLiteral("console.nav.damage_summary"), 2));
    scrollLayout->addWidget(buildObjectButton(QStringLiteral("C"),
                                              QStringLiteral("console.nav.pets"),
                                              QStringLiteral("console.nav.pets_summary"), 3));
    scrollLayout->addSpacing(20);

    auto *workspaceTitle = new QLabel();
    trSet(workspaceTitle, QStringLiteral("console.rail.workspace"));
    workspaceTitle->setObjectName("sectionCap");
    scrollLayout->addWidget(workspaceTitle);
    editBtn_ = new QPushButton();
    trSet(editBtn_, QStringLiteral("console.rail.layout_mode"));
    editBtn_->setObjectName("railAction");
    editBtn_->setCursor(Qt::PointingHandCursor);
    scrollLayout->addWidget(editBtn_);
    auto *presets = new QPushButton();
    trSet(presets, QStringLiteral("console.rail.presets"));
    presets->setObjectName("railAction");
    presets->setEnabled(false); // visual placeholder; no preset API yet
    scrollLayout->addWidget(presets);
    scrollLayout->addStretch(1);
    railScroll->setWidget(scrollContent);
    railLayout->addWidget(railScroll, 1);

    railLayout->addSpacing(18);

    // v0.5.6: START + ESC hint pinned at bottom (always visible, CTA).
    // NOTE (pre-existing quirk, preserved): buildEditModeBlock() later
    // re-points startBtn_ at the EDIT-MODE block's START button, so this
    // rail button is display-only. It still registers for retranslation.
    startBtn_ = new QPushButton();
    trSet(startBtn_, QStringLiteral("console.rail.start_overlay"));
    startBtn_->setObjectName("startBtn");
    startBtn_->setCursor(Qt::PointingHandCursor);
    railLayout->addWidget(startBtn_);
    auto *hint = new QLabel();
    trSet(hint, QStringLiteral("console.rail.hint"));
    hint->setObjectName("railHint");
    hint->setAlignment(Qt::AlignCenter);
    railLayout->addWidget(hint);
    // v0.7.1: wrap the rail in a horizontal QSplitter alongside the
    // dedicated game column. The splitter handle width is 0 (QSS keeps
    // it hidden); the visible divider is the rail's border-right, which
    // now lives between the game column and the rail instead of at the
    // window's left edge.
    auto *leftSplitter = new QSplitter(Qt::Horizontal);
    leftSplitter->setObjectName("leftSplitter");
    leftSplitter->setHandleWidth(0);
    leftSplitter->setChildrenCollapsible(false);
    leftSplitter->addWidget(buildGameColumn());
    leftSplitter->addWidget(rail);
    leftSplitter->setSizes({150, 180});
    leftSplitter_ = leftSplitter;
    {
        const mhw::ConsoleLayoutStore layout;
        const QByteArray saved = layout.leftSplitterState();
        if (!saved.isEmpty()) leftSplitter->restoreState(saved);
    }
    connect(leftSplitter, &QSplitter::splitterMoved, this, [this, leftSplitter]{
        mhw::ConsoleLayoutStore layout;
        layout.saveLeftSplitterState(leftSplitter->saveState());
    });
    topRow->addWidget(leftSplitter);

    auto *inspectorHost = new QFrame();
    inspectorHost->setObjectName("inspectorHost");
    inspectorHost->setFixedWidth(480);
    auto *inspectorLayout = new QVBoxLayout(inspectorHost);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorStack_ = new QStackedWidget();
    inspectorStack_->setObjectName("inspectorStack");
    inspectorStack_->addWidget(buildInspector(QStringLiteral("console.nav.player"),
                                               QStringLiteral("console.inspector.sub.player"),
                                               mhw::PlayerSection::displayNames(), 0));
    inspectorStack_->addWidget(buildInspector(QStringLiteral("console.nav.monster"),
                                               QStringLiteral("console.inspector.sub.monster"),
                                               mhw::MonsterSection::displayNames(), 1));
    inspectorStack_->addWidget(buildInspector(QStringLiteral("console.nav.damage"),
                                               QStringLiteral("console.inspector.sub.damage"),
                                               mhw::DamageSection::displayNames(), 2));
    inspectorStack_->addWidget(buildInspector(QStringLiteral("console.nav.pets"),
                                               QStringLiteral("console.inspector.sub.pets"),
                                               mhw::PetDamageSection::displayNames(), 3));
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
    safeAreaBtn_ = new QPushButton();
    trSet(safeAreaBtn_, QStringLiteral("console.stage.safe_area"));
    safeAreaBtn_->setObjectName("stageToggle");
    safeAreaBtn_->setCheckable(true);
    safeAreaBtn_->setChecked(true);
    safeAreaBtn_->setCursor(Qt::PointingHandCursor);
    // v0.7.5: visible zoom controls on the stage bar. Ctrl+wheel worked
    // since v0.5 but was undiscoverable in the preview console, where
    // the 1:1 panels on a 2560×1600 logical screen were unreadable at
    // fit-to-widget. Buttons + live label make the feature obvious.
    zoomOutBtn_ = new QPushButton(QStringLiteral("−"));
    zoomOutBtn_->setObjectName("stageToggle");
    zoomOutBtn_->setFixedSize(26, 26);
    zoomOutBtn_->setCursor(Qt::PointingHandCursor);
    zoomInBtn_ = new QPushButton(QStringLiteral("+"));
    zoomInBtn_->setObjectName("stageToggle");
    zoomInBtn_->setFixedSize(26, 26);
    zoomInBtn_->setCursor(Qt::PointingHandCursor);
    zoomLabel_ = new QLabel();
    trHook([this]{ updateZoomLabel(); });
    zoomLabel_->setObjectName("stageToggleLabel");
    stagebar->addWidget(zoomOutBtn_);
    stagebar->addWidget(zoomLabel_);
    stagebar->addWidget(zoomInBtn_);
    stagebar->addWidget(safeAreaBtn_);
    gridBtn_ = new QPushButton();
    trSet(gridBtn_, QStringLiteral("console.stage.grid"));
    gridBtn_->setObjectName("stageToggle");
    gridBtn_->setCheckable(true);
    gridBtn_->setChecked(true);
    gridBtn_->setCursor(Qt::PointingHandCursor);
    stagebar->addWidget(gridBtn_);
    themeBtn_ = new QPushButton();
    updateThemeChipText();   // i18n: copy names the theme, so it follows the locale
    themeBtn_->setObjectName("themeToggle");
    themeBtn_->setCursor(Qt::PointingHandCursor);
    connect(themeBtn_, &QPushButton::clicked, this, [this]() {
        setUiTheme(!isDarkTheme());
        updateThemeChipText();
        // Clear the old application stylesheet first. Qt 6.11 keeps
        // cached selector colours on existing QPushButtons if a new QSS
        // is assigned directly; clearing makes the following assignment
        // a real style reset instead of an incremental merge.
        setStyleSheet({});
        setStyleSheet(consoleStyleSheet());
        // SectionRow owns inline QLabels, therefore parent QSS cannot
        // update CONNECTION / QUEST / etc. Rebuild those child styles
        // explicitly while the new theme is active.
        for (auto &c : ctl_)
            for (SectionRow *row : c.subs)
                row->refreshTheme();
        repolishAllWidgets();
    });
    // ---- v0.9 i18n: EN / CH language chip ------------------------------
    // Two segments — "中文" | "EN" — with the ACTIVE language highlighted
    // through the `active` dynamic property (QSS: QLabel#localeSeg). The
    // chip is a single click target sitting left of the theme chip: the
    // labels are mouse-transparent, so the frame receives the release and
    // routes it through ControlPanel::eventFilter → switchLocale().
    // The segment texts are language ENDONYMS (中文 / EN) — never translated.
    localeChip_ = new QFrame();
    localeChip_->setObjectName(QStringLiteral("localeChip"));
    localeChip_->setCursor(Qt::PointingHandCursor);
    trTip(localeChip_, QStringLiteral("console.locale.tooltip"));
    localeChip_->installEventFilter(this);
    {
        auto *hl = new QHBoxLayout(localeChip_);
        hl->setContentsMargins(10, 4, 10, 4);
        hl->setSpacing(6);
        localeZhLabel_ = new QLabel(mh::tr(QStringLiteral("console.locale.zh")));
        localeZhLabel_->setObjectName(QStringLiteral("localeSeg"));
        localeZhLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto *localeSep = new QLabel(QStringLiteral("|"));
        localeSep->setObjectName(QStringLiteral("localeSep"));
        localeSep->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        localeEnLabel_ = new QLabel(mh::tr(QStringLiteral("console.locale.en")));
        localeEnLabel_->setObjectName(QStringLiteral("localeSeg"));
        localeEnLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        hl->addWidget(localeZhLabel_);
        hl->addWidget(localeSep);
        hl->addWidget(localeEnLabel_);
    }
    updateLocaleChip();
    stagebar->addWidget(localeChip_);
    stagebar->addWidget(themeBtn_);
    // i18n: replay the theme chip copy together with the language chip.
    trHook([this]{ updateThemeChipText(); });
    stageLayout->addLayout(stagebar);

    canvas_ = new HudCanvas();
    canvas_->installEventFilter(this);
    auto *canvasScroll = new QScrollArea();
    canvasScroll->setObjectName("canvasScroll");
    canvasScroll->setWidget(canvas_);
    // v0.5.6: stage now owns the full window width, so let the canvas
    // stretch horizontally to fill the viewport. The paint path already
    // recomputes layout from width()/height(), so widening the widget
    // widens the 16:9 frame and the four panel slots inside it.
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
        Panel *p = panelAt(idx);
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
    // v0.7.5: zoom buttons. The label tracks both button clicks and
    // Ctrl+wheel via the canvas's zoomChanged signal (single source of
    // truth). Zoom is persisted next to the window geometry so the
    // preview opens at the size the user last chose.
    connect(zoomInBtn_, &QPushButton::clicked, this, [this]{
        if (canvas_) canvas_->setZoom(canvas_->zoom() + 0.5);
    });
    connect(zoomOutBtn_, &QPushButton::clicked, this, [this]{
        if (canvas_) canvas_->setZoom(canvas_->zoom() - 0.5);
    });
    connect(canvas_, &HudCanvas::zoomChanged, this, [this](qreal){
        updateZoomLabel();   // i18n: the format lives in one place
    });
    {
        const mhw::ConsoleLayoutStore layout;
        canvas_->setZoom(layout.zoom());
        updateZoomLabel();
    }
    for (int i = 0; i < mhw::kPanelCount; ++i)
        canvas_->bindPanel(i, new PanelSourceAdapter(panelAt(i)));
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
    // v0.7.5: with the default width now 1200 the top row (rail +
    // inspector) gets enough horizontal room; give it a bit more of the
    // vertical axis too so the inspector's CONTENT rows and APPEARANCE
    // sliders are visible without scrolling on first open.
    splitter->setSizes({520, 520});
    // Remember the user's chosen (or default) stage size so animStageTo
    // can restore it on show. DO NOT read it from splitter->sizes() here:
    // at construction time the splitter isn't in a layout yet, so
    // setSizes clamps to the widget minimums ([332,148] for a 1040px
    // window) — the correct 570 default would be lost. Use the constant
    // default, then let QSettings + splitterMoved override it later.
    savedStageSize_ = kConsoleStageHeightDefault;
    // Hard minimum on the top pane so the inspector doesn't get crushed.
    topContainer->setMinimumHeight(360);
    {
        const mhw::ConsoleLayoutStore layout;
        const mhw::ConsoleLayoutStore::SplitterLayout saved = layout.splitterLayout();
        if (!saved.splitterState.isEmpty()) splitter->restoreState(saved.splitterState);
        // After restore, keep the user's last-chosen stage height as a
        // plain int (easier than decoding the splitter bytearray).
        savedStageSize_ = saved.stageHeight;
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
        const int newStage = splitter->sizes().value(1, kConsoleStageHeightDefault);
        mhw::ConsoleLayoutStore layout;
        layout.saveSplitterLayout(splitter->saveState(), newStage);
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
            const int stageH = savedStageSize_ > 0 ? savedStageSize_ : kConsoleStageHeightDefault;
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
        updateStageToggleText();   // i18n: ⌄/⌃ copy lives in one helper
        animStageTo(checked);
    });

    // ---- overlay subprocess lifecycle ---------------------------------
    // v0.11: extracted into mhw::OverlayProcessController (the same
    // MVVM split DamagePanel got for its statistics engine). The
    // controller owns the child pid, the 250 ms kill(pid,0) poll, the
    // pending hot-swap restart and the --game value; this file keeps the
    // widget-styling half and reacts to its signals.
    connect(overlay_, &mhw::OverlayProcessController::launched, this,
            // The argv/editMode parameters carry no View state: the
            // controller owns the last-launch copy for hot-swap, so the
            // signal is used purely as a "the overlay is up" trigger.
            [this](const QStringList &, bool){
        // L4: flip the status badge so the user can tell at a glance which
        // mode the console is in. We're hiding next, so this label only
        // matters when the overlay exits and the console re-shows.
        if (statusBadge_) {
            const QString stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
            statusBadge_->setText(
                mh::tr(QStringLiteral("console.status.running_since"))
                    .arg(overlay_->overlayPid()).arg(stamp));
        }
        // Disable both launcher buttons while running so the user can't
        // accidentally spawn a second overlay.
        if (startBtn_) startBtn_->setEnabled(false);
        if (editBtn_)  editBtn_->setEnabled(false);
        hide();   // the overlay owns the screen now
    });
    // runningChanged replays setOverlayRunning() so the START/STOP caption
    // and the badge's `state` property (which drive the QSS) are reset on
    // every liveness flip — the exit half of the old onOverlayExited().
    connect(overlay_, &mhw::OverlayProcessController::runningChanged, this,
            [this](bool running, qint64){ setOverlayRunning(running); });
    connect(overlay_, &mhw::OverlayProcessController::exited, this, [this]{
        if (startBtn_) startBtn_->setEnabled(true);
        if (editBtn_)  editBtn_->setEnabled(true);
        if (statusBadge_)
            statusBadge_->setText(mh::tr(QStringLiteral("console.status.plain_ready")));
        show();
        raise();
        activateWindow();
    });

    // v0.5 P1: startBtn toggles between launch and stop. The controller
    // owns the child's pid, so the View just asks it whether one is alive.
    connect(startBtn_, &QPushButton::clicked, this, [this]{
        if (overlay_->isRunning()) overlay_->stop();
        else { launchOverlayChild(/*editMode=*/false); }
    });
    connect(editBtn_, &QPushButton::clicked, this,
            [this]{ launchOverlayChild(/*editMode=*/true); });

    loadMaskFromDisk();
    for (int i = 0; i < mhw::kPanelCount; ++i)
        rebuildAndRender(i);
    selectPanel(0);

    // v0.7.1: World-only / Rise-only section rows are hidden in place
    // from here on. Initially all rows are visible (buildInspector
    // doesn't filter), so we apply the visibility rule for the
    // default game (= World) before any user interaction. Subsequent
    // changes go through switchGame().
    //
    // PlayerSection::* are BIT values (Mantles = 1<<4, Wirebug = 1<<7),
    // not indices — comparing `b == int(Wirebug)` was always false and
    // the rows leaked into both game views. Use a proper bit mask:
    // each row's section bit is `(1u << b)` because the bits are
    // assigned in display order.
    if (!ctl_[0].subs.isEmpty()) {
        const uint32_t wirebugBit = uint32_t(mhw::PlayerSection::Wirebug);
        const uint32_t mantlesBit = uint32_t(mhw::PlayerSection::Mantles);
        for (int b = 0; b < ctl_[0].subs.size(); ++b) {
            auto *row = ctl_[0].subs[b];
            if (!row) continue;
            const uint32_t rowBit = (1u << b);
            const bool hideForWorld = (currentGame() == mhw::GameId::World)
                                      && (rowBit == wirebugBit);
            const bool hideForRise  = (currentGame() == mhw::GameId::Rise)
                                      && (rowBit == mantlesBit);
            const bool hide = hideForWorld || hideForRise;
            row->setVisible(!hide);
            row->setEnabled(!hide);
        }
    }

    // v0.6 Phase 4: initial game selection. Honour the persisted choice
    // (written by switchGame); on first run — no saved value — fall back
    // to auto-detecting a running World/Rise process.
    // v0.6 Phase 5: surface the scan result in the rail badge and persist
    // it as "detectedGame"; the 5s timer keeps the badge live afterwards.
    {
        QSettings s;
        const QString savedGame = s.value(QStringLiteral("game")).toString();
        const auto detected = mhw::detectGame();
        if (detected) {
            lastDetectedGame_ = detected->game;
            s.setValue(QStringLiteral("detectedGame"),
                       mhw::gameIdToString(detected->game));
        }
        mhw::GameId savedGameId = mhw::GameId::World;
        if (mhw::gameIdFromString(savedGame, &savedGameId))
            switchGame(savedGameId);
        else
            switchGame(detected ? detected->game : mhw::GameId::World);
        // One-shot startup text ("keep / change?" framing); the first
        // timer tick after 5s takes over with the live-badge format.
        if (autoDetectBadge_) {
            if (!detected) {
                autoDetectBadge_->setText(
                    mh::tr(QStringLiteral("console.detect.startup_none")));
                autoDetectBadge_->setProperty("state", "gray");
            } else {
                const QString name = gameName(detected->game);
                if (detected->game == currentGame()) {
                    autoDetectBadge_->setText(
                        mh::tr(QStringLiteral("console.detect.startup_running"))
                            .arg(name).arg(detected->pid));
                    autoDetectBadge_->setProperty("state", "cyan");
                } else {
                    autoDetectBadge_->setText(
                        mh::tr(QStringLiteral("console.detect.startup_switch")).arg(name));
                    autoDetectBadge_->setProperty("state", "amber");
                }
            }
            autoDetectBadge_->style()->unpolish(autoDetectBadge_);
            autoDetectBadge_->style()->polish(autoDetectBadge_);
        }
        auto *autoDetectTimer = new QTimer(this);
        connect(autoDetectTimer, &QTimer::timeout,
                this, [this]{ refreshAutoDetect(); });
        autoDetectTimer->start(5000);
    }

    // REFramework status is cheap file inspection, but the timer is kept
    // deliberately coarse. Archive download/extraction never runs here; the
    // request signals below are the only install/removal entry points.
    riseReframeworkRefreshTimer_ = new QTimer(this);
    riseReframeworkRefreshTimer_->setInterval(1500);
    connect(riseReframeworkRefreshTimer_, &QTimer::timeout,
            this, [this]{ refreshRiseReframeworkStatus(); });
    riseReframeworkRefreshTimer_->start();
    refreshRiseReframeworkStatus();
}

ControlPanel::~ControlPanel()
{
    // Persist current mask state on every destruction path (close, app
    // exit, explicit delete). Safe to call even if load was never reached.
    saveMaskToDisk();

    // The four preview panels were created as TOP-level QMainWindows
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
    if (pets_)    { pets_->setVisible(false);    delete pets_;    pets_    = nullptr; }
}

// ---- v0.9 i18n: registration + replay -----------------------------------
//
// Design: a language switch must not rebuild the window (the console
// owns four live panel instances, a splitter layout and a scroll state
// that would all be lost). Instead every localized string is registered
// ONCE at construction with the key it came from; retranslateUi() replays
// the registry and then re-runs the ordinary refresh paths for anything
// that is formatted from live state.
//
// Registration applies the string immediately, so construction-time UI is
// localized without a second pass.
void ControlPanel::trSet(QWidget *widget, const QString &key)
{
    if (!widget)
        return;
    trPairs_.append({QPointer<QWidget>(widget), key});
    const QString text = consoleText(key);
    if (auto *label = qobject_cast<QLabel *>(widget))
        label->setText(text);
    else if (auto *button = qobject_cast<QAbstractButton *>(widget))
        button->setText(text);
}

void ControlPanel::trTip(QWidget *widget, const QString &key)
{
    if (!widget)
        return;
    trTips_.append({QPointer<QWidget>(widget), key});
    widget->setToolTip(mh::tr(key));
}

void ControlPanel::trHook(std::function<void()> fn)
{
    trHooks_.append(fn);
    fn();   // localize immediately, replay later
}

void ControlPanel::trWindowTitle(const QString &key)
{
    setWindowTitle(mh::tr(key));
    trHooks_.append([this, key]{ setWindowTitle(mh::tr(key)); });
}

void ControlPanel::retranslateUi()
{
    // Phase 1 — replay the build-time registry (static copy + hooks).
    for (const auto &pair : trPairs_) {
        QWidget *widget = pair.first.data();
        if (!widget)
            continue;
        const QString text = consoleText(pair.second);
        if (auto *label = qobject_cast<QLabel *>(widget))
            label->setText(text);
        else if (auto *button = qobject_cast<QAbstractButton *>(widget))
            button->setText(text);
    }
    for (const auto &pair : trTips_) {
        if (QWidget *widget = pair.first.data())
            widget->setToolTip(mh::tr(pair.second));
    }
    for (const auto &fn : trHooks_)
        fn();

    // Phase 2 — dynamic elements. Re-running the existing refresh paths
    // (instead of duplicating their format strings here) is what keeps a
    // language flip consistent with what the live timers write 5s later.
    // The overlay START/STOP + badge re-style lives in the runningChanged
    // slot; replaying it here re-applies the current copy.
    setOverlayRunning(overlay_->isRunning());  // rail START/STOP + badge
    for (int i = 0; i < mhw::kPanelCount; ++i)
        updatePanelSummary(i);                    // counts + nav summaries
    for (int i = 0; i < mhw::kPanelCount; ++i)
        updatePosLabel(i);                        // corner + margin readout
    refreshAutoDetect();                          // badge + GAME column
    refreshRiseReframeworkStatus();               // Rise-only management card
    for (int i = 0; i < mhw::kPanelCount; ++i)
        if (Panel *panel = panelAt(i))
            panel->retranslateUi();               // cached preview-panel copy
    // Preview tiles carry PAINTED chrome (e.g. the disabled placeholder)
    // and the canvas paints its header/footer — repaint both.
    for (int i = 0; i < mhw::kPanelCount; ++i)
        rebuildAndRender(i);
    if (canvas_)
        canvas_->update();
}

// Load `locale` into the shared StringTable and relabel the console.
// Returns silently when the locale directory is missing (the previous
// table stays active — StringTable::load() guarantees that).
void ControlPanel::applyLocale(const QString &locale)
{
    if (locale.isEmpty())
        return;
    auto &table = mhw::StringTable::instance();
    if (locale == table.currentLocale()) {
        updateLocaleChip();
        return;
    }
    if (!table.load(locale)) {
        qWarning("monster-control: locale '%s' unavailable; keeping '%s'",
                 qPrintable(locale), qPrintable(table.currentLocale()));
        updateLocaleChip();
        return;
    }
    retranslateUi();
    updateLocaleChip();
}

// User-facing switch (the EN/CH chip): apply + persist. The conf's
// `locale=` row is the console -> overlay handshake — the overlay polls
// it (~1 s) and reloads itself, so no restart is needed. The console is
// the ONLY writer of that file (mask rows + locale row).
void ControlPanel::switchLocale(const QString &locale)
{
    const QString before = mhw::StringTable::instance().currentLocale();
    applyLocale(locale);
    if (mhw::StringTable::instance().currentLocale() == before)
        return;   // load failed — do not advertise a language we do not have
    if (!mhw::writeLocaleToConf(locale))
        qWarning("monster-control: cannot write locale to %s",
                 qPrintable(mhw::localeConfPath()));
}

void ControlPanel::updateLocaleChip()
{
    const bool en = mhw::StringTable::instance().isEnglish();
    for (QLabel *seg : {localeZhLabel_, localeEnLabel_}) {
        if (!seg)
            continue;
        const bool active = (seg == localeZhLabel_) ? !en : en;
        seg->setProperty("active", active);
        // Dynamic-property selectors need an explicit repolish.
        seg->style()->unpolish(seg);
        seg->style()->polish(seg);
    }
}

void ControlPanel::updateThemeChipText()
{
    if (!themeBtn_)
        return;
    themeBtn_->setText(isDarkTheme() ? mh::tr(QStringLiteral("console.theme.light"))
                                     : mh::tr(QStringLiteral("console.theme.dark")));
}

void ControlPanel::updateStageToggleText()
{
    if (!stageToggleBtn_)
        return;
    stageToggleBtn_->setText(
        stageToggleBtn_->isChecked()
            ? mh::tr(QStringLiteral("console.rail.hide_stage"))
            : mh::tr(QStringLiteral("console.rail.show_stage")));
}

void ControlPanel::updateZoomLabel()
{
    if (!zoomLabel_)
        return;
    zoomLabel_->setText(mh::tr(QStringLiteral("console.canvas.zoom"))
                            .arg(canvas_ ? canvas_->zoom() : 1.0, 0, 'f', 1));
}

void ControlPanel::closeEvent(QCloseEvent *e)
{
    // Hide the four preview panels BEFORE accepting close. They're
    // top-level QMainWindows (no parent — keeping them parented would
    // composite their paint into the console's backing store, which is
    // what produced the "panel painted on top of switches" bug). They're
    // also invisible (WA_DontShowOnScreen), but the explicit hide steers
    // QApplication::quitOnLastWindowClosed toward the right answer — it
    // sees 0 visible windows the moment the console goes away and exits.
    for (int i = 0; i < mhw::kPanelCount; ++i)
        if (Panel *panel = panelAt(i))
            panel->setVisible(false);

    // v0.5 UI-link: persist scale/opacity that the APPEARANCE sliders
    // changed (with persist=false). saveAppearance() writes ONLY scale
    // and opacity — NOT margins or visible — so it can't clobber the
    // live overlay's geometry or accidentally hide a panel (the console
    // panels are WA_DontShowOnScreen + hidden; isVisible()==false would
    // write visible=false and break the overlay's next launch).
    for (int i = 0; i < mhw::kPanelCount; ++i)
        if (Panel *panel = panelAt(i))
            panel->saveAppearance();

    saveMaskToDisk();
    {
        mhw::ConsoleLayoutStore layout;
        layout.saveWindowFrame(saveGeometry(),
                               canvas_ ? canvas_->zoom() : 1.0,
                               saveState());
    }
    QMainWindow::closeEvent(e);
}

// v0.5 P1: 1/2/3/4 hot-keys for the HUD objects rail. Visible in the
// rail hint at the bottom of the left column.
void ControlPanel::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_1) { selectPanel(0); return; }
    if (e->key() == Qt::Key_2) { selectPanel(1); return; }
    if (e->key() == Qt::Key_3) { selectPanel(2); return; }
    if (e->key() == Qt::Key_4) { selectPanel(3); return; }
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
        savedStageSize_ = current > 0 ? current : kConsoleStageHeightDefault;
    }
    const int target = visible ? (savedStageSize_ > 0 ? savedStageSize_ : kConsoleStageHeightDefault) : 0;
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
        // v0.9 i18n: the EN/CH chip flips the console language. The
        // segment labels are WA_TransparentForMouseEvents, so the frame
        // is what receives this release. The active locale comes from
        // the shared table (single source of truth), not from a member.
        if (watched == localeChip_) {
            switchLocale(mhw::StringTable::instance().isEnglish()
                             ? QStringLiteral("zh-CN")
                             : QStringLiteral("en-US"));
            return true;
        }
        // v0.6 Phase 5: clicking the auto-detect chip switches to the
        // detected game (hot-swaps when the overlay is running).
        if (watched == autoDetectBadge_) {
            const auto detected = mhw::detectGame();
            if (detected) {
                switchGame(detected->game);
                refreshAutoDetect();
            }
            return true;
        }
        for (int i = 0; i < mhw::kPanelCount; ++i) {
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
    if (!mhw::isPanelIndex(idx)) return;
    // v0.10.1: the pets inspector is Rise-only; it cannot be reached while
    // World is selected (the rail card is hidden; hot-keys land here).
    if (idx == 3 && currentGame() == mhw::GameId::World) return;
    selectedPanel_ = idx;
    syncAppearance(idx);
    updatePosLabel(idx);
    if (inspectorStack_) inspectorStack_->setCurrentIndex(idx);
    if (canvas_) canvas_->setSelectedPanel(idx);
    // v0.8: when the user switches which panel the inspector is
    // editing, retarget the preview's screen frame to that panel's
    // chosen output. Each panel can live on a different screen, so
    // the preview has to follow whichever is in focus — otherwise
    // the user sees the panel's actual position drawn against the
    // wrong output's rectangle.
    if (canvas_) {
        Panel *p = panelAt(idx);
        canvas_->setPreviewScreen(p ? p->outputName() : QString());
    }
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        if (!ctl_[i].navButton) continue;
        ctl_[i].navButton->setProperty("selected", i == idx);
        ctl_[i].navButton->style()->unpolish(ctl_[i].navButton);
        ctl_[i].navButton->style()->polish(ctl_[i].navButton);
    }
}

void ControlPanel::updatePanelSummary(int idx)
{
    if (!mhw::isPanelIndex(idx))
        return;
    int on = 0;
    for (auto *row : ctl_[idx].subs)
        if (row->isChecked()) ++on;
    const int total = ctl_[idx].subs.size();
    if (ctl_[idx].countLabel)
        ctl_[idx].countLabel->setText(
            mh::tr(QStringLiteral("console.inspector.count")).arg(on).arg(total));
    if (ctl_[idx].countBar)
        ctl_[idx].countBar->setRatio(total > 0 ? qreal(on) / total : 1.0);
    if (ctl_[idx].navSummary) {
        if (idx == 3 && currentGame() == mhw::GameId::World) {
            ctl_[idx].navSummary->setText(
                consoleText(QStringLiteral("console.panel.pets_rise_only")));
        } else {
            ctl_[idx].navSummary->setText(ctl_[idx].master->isChecked()
                ? mh::tr(QStringLiteral("console.inspector.sections")).arg(on).arg(total)
                : mh::tr(QStringLiteral("console.panel.disabled")));
        }
    }
}

// v0.11: the selected target game lives in mhw::OverlayProcessController
// (it appends the child's --game flag). Read-only convenience for the
// several View consumers that only branch on it.
mhw::GameId ControlPanel::currentGame() const
{
    return overlay_ ? overlay_->currentGame() : mhw::GameId::World;
}

QWidget *ControlPanel::buildObjectButton(const QString &letter,
                                         const QString &titleKey,
                                         const QString &summaryKey, int idx)
{
    auto *box = new QFrame();
    box->setObjectName(idx == 0 ? "navPlayer"
                       : idx == 1 ? "navMonster"
                       : idx == 2 ? "navDamage"
                                  : "navPets");
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
    auto *titleLabel = new QLabel();
    trSet(titleLabel, titleKey);
    titleLabel->setObjectName("navTitle");
    auto *summaryLabel = new QLabel();
    trSet(summaryLabel, summaryKey);
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

QWidget *ControlPanel::buildInspector(const QString &titleKey, const QString &subKey,
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

    // The eyebrow embeds the 1-based panel index — a formatting hook, not a
    // plain (widget, key) pair.
    auto *eyebrow = new QLabel();
    trHook([eyebrow, idx]{
        eyebrow->setText(mh::tr(QStringLiteral("console.inspector.selected_object"))
                             .arg(idx + 1));
    });
    eyebrow->setObjectName("sectionCap");
    vl->addWidget(eyebrow);

    auto *head = new QHBoxLayout();
    auto *titles = new QVBoxLayout();
    auto *titleLabel = new QLabel();
    trSet(titleLabel, titleKey);
    titleLabel->setObjectName("inspectorTitle");
    auto *subLabel = new QLabel();
    trSet(subLabel, subKey);
    subLabel->setObjectName("inspectorSub");
    titles->addWidget(titleLabel);
    titles->addWidget(subLabel);
    head->addLayout(titles, 1);
    auto *master = new ToggleChip();
    ctl_[idx].master = master;
    head->addWidget(master, 0, Qt::AlignVCenter);
    vl->addLayout(head);

    auto *count = new QLabel();
    // v0.5 P1: per-panel objectName suffix so the QSS rule
    // can tint the section count by the matching panel accent.
    count->setObjectName(idx == 0 ? "countLabelP"
                       : idx == 1 ? "countLabelM"
                       : idx == 2 ? "countLabelD"
                                  : "countLabelPets");
    ctl_[idx].countLabel = count;
    vl->addWidget(count);

    // v0.5 P2: thin accent progress bar under the count headline
    auto *bar = new SectionCountBar();
    bar->setAccent(panelAccent(idx));
    ctl_[idx].countBar = bar;
    vl->addWidget(bar);
    vl->addSpacing(10);
    auto *contentCap = new QLabel();
    trSet(contentCap, QStringLiteral("console.inspector.content"));
    contentCap->setObjectName("sectionCap");
    vl->addWidget(contentCap);

    const QStringList &keys = idx == 0 ? mhw::PlayerSection::names()
                            : idx == 1 ? mhw::MonsterSection::names()
                            : idx == 2 ? mhw::DamageSection::names()
                                       : mhw::PetDamageSection::names();
    for (int b = 0; b < labels.size(); ++b) {
        auto *row = new SectionRow(labels[b], b < keys.size() ? keys[b] : QString(), iconKind(idx, b));
        row->setAccent(panelAccent(idx));
        // i18n: the row's bit index is `keys[b]` (a stable ASCII id); the
        // display label is re-resolved from panel_sections displayName()
        // on every language switch instead of freezing Chinese at build.
        trHook([row, idx, b]{ row->setDisplayText(sectionLabel(idx, b)); });
        // v0.7.1: World/Rise row visibility is set in switchGame() after
        // construction so all rows are created equal. See ControlPanel
        // constructor → switchGame() for the source of truth.
        ctl_[idx].subs.push_back(row);
        vl->addWidget(row);
    }

    vl->addSpacing(12);

    if (idx == 2) {
        auto *card = new QFrame(content);
        card->setObjectName(QStringLiteral("riseReframeworkCard"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 12, 14, 12);
        cardLayout->setSpacing(7);

        auto *cardTitle = new QLabel();
        trSet(cardTitle, QStringLiteral("console.reframework.title"));
        cardTitle->setObjectName(QStringLiteral("riseReframeworkTitle"));
        cardLayout->addWidget(cardTitle);

        auto *cardSubtitle = new QLabel();
        trSet(cardSubtitle, QStringLiteral("console.reframework.subtitle"));
        cardSubtitle->setObjectName(QStringLiteral("riseReframeworkSubtitle"));
        cardSubtitle->setWordWrap(true);
        cardLayout->addWidget(cardSubtitle);

        auto *status = new QLabel();
        trSet(status, QStringLiteral("console.reframework.not_found"));
        status->setObjectName(QStringLiteral("riseReframeworkStatus"));
        status->setWordWrap(true);
        riseReframeworkStatus_ = status;
        cardLayout->addWidget(status);

        auto *actions = new QVBoxLayout();
        actions->setSpacing(5);
        auto *install = new QPushButton();
        trSet(install, QStringLiteral("console.reframework.install"));
        install->setObjectName(QStringLiteral("installRiseReframeworkButton"));
        install->setCursor(Qt::PointingHandCursor);
        connect(install, &QPushButton::clicked, this,
                [this]{ requestRiseReframeworkInstall(); });
        actions->addWidget(install);
        installRiseReframeworkButton_ = install;

        auto *removeLua = new QPushButton();
        trSet(removeLua, QStringLiteral("console.reframework.remove_lua"));
        removeLua->setObjectName(QStringLiteral("removeRiseLuaButton"));
        removeLua->setCursor(Qt::PointingHandCursor);
        connect(removeLua, &QPushButton::clicked, this,
                [this]{ requestRiseLuaRemoval(); });
        actions->addWidget(removeLua);
        removeRiseLuaButton_ = removeLua;

        // v0.10.10: menu-state fix. Deliberately NOT part of install — it is a
        // user-config overlay on a file REFramework shares with ~80 other
        // settings, so it stays an explicit click. Both directions are offered
        // because the point is to give the player control, not to force one
        // behaviour; the label under them reflects what the files say now.
        auto *menuHeader = new QLabel();
        trSet(menuHeader, QStringLiteral("console.reframework.menu_state_header"));
        menuHeader->setObjectName(QStringLiteral("riseReframeworkSubtitle"));
        menuHeader->setWordWrap(true);
        actions->addWidget(menuHeader);

        auto *menuState = new QLabel();
        menuState->setObjectName(QStringLiteral("riseReframeworkStatus"));
        menuState->setWordWrap(true);
        actions->addWidget(menuState);
        menuStateStatus_ = menuState;

        auto *menuFix = new QPushButton();
        trSet(menuFix, QStringLiteral("console.reframework.fix_menu_state"));
        menuFix->setObjectName(QStringLiteral("fixMenuStateButton"));
        menuFix->setCursor(Qt::PointingHandCursor);
        // trTip, not setToolTip: only trTip registers the widget with the
        // locale switcher, so a plain setToolTip would freeze it in whatever
        // language the panel happened to be built in.
        trTip(menuFix, QStringLiteral("console.reframework.fix_menu_state_tip"));
        connect(menuFix, &QPushButton::clicked, this,
                [this]{ requestRiseMenuStateFix(false); });
        actions->addWidget(menuFix);
        menuStateFixButton_ = menuFix;

        auto *menuRestore = new QPushButton();
        trSet(menuRestore,
              QStringLiteral("console.reframework.restore_menu_state"));
        menuRestore->setObjectName(QStringLiteral("restoreMenuStateButton"));
        menuRestore->setCursor(Qt::PointingHandCursor);
        trTip(menuRestore,
              QStringLiteral("console.reframework.restore_menu_state_tip"));
        connect(menuRestore, &QPushButton::clicked, this,
                [this]{ requestRiseMenuStateFix(true); });
        actions->addWidget(menuRestore);
        menuStateRestoreButton_ = menuRestore;

        auto *removeReframework = new QPushButton();
        trSet(removeReframework,
              QStringLiteral("console.reframework.remove_reframework"));
        removeReframework->setObjectName(
            QStringLiteral("removeRiseReframeworkButton"));
        removeReframework->setCursor(Qt::PointingHandCursor);
        connect(removeReframework, &QPushButton::clicked, this,
                [this]{ requestRiseReframeworkRemoval(); });
        actions->addWidget(removeReframework);
        removeRiseReframeworkButton_ = removeReframework;

        cardLayout->addLayout(actions);
        riseReframeworkCard_ = card;
        riseReframeworkCard_->setVisible(currentGame() == mhw::GameId::Rise);
        vl->addWidget(riseReframeworkCard_);
        vl->addSpacing(12);
    }

    // v0.5 UI-link: APPEARANCE sliders (scale + opacity), live preview.
    auto *appCap = new QLabel();
    trSet(appCap, QStringLiteral("console.inspector.appearance"));
    appCap->setObjectName("sectionCap");
    vl->addWidget(appCap);

    auto *scaleRow = new QHBoxLayout();
    scaleRow->setSpacing(8);
    auto *scaleLab = new QLabel();
    trSet(scaleLab, QStringLiteral("console.inspector.scale"));
    scaleLab->setObjectName("sliderLabel");
    auto *scaleVal = new QLabel();
    scaleVal->setObjectName("sliderValue");
    auto *scaleSlider = new QSlider(Qt::Horizontal);
    scaleSlider->setRange(50, 300);
    {
        Panel *p = panelAt(idx);
        scaleSlider->setValue(qRound(p->scale() * 100));
    }
    scaleVal->setText(QStringLiteral("%1%").arg(scaleSlider->value()));
    connect(scaleSlider, &QSlider::valueChanged, this, [this, idx, scaleVal](int v){
        scaleVal->setText(QStringLiteral("%1%").arg(v));
        Panel *p = panelAt(idx);
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
    auto *opacLab = new QLabel();
    trSet(opacLab, QStringLiteral("console.inspector.opacity"));
    opacLab->setObjectName("sliderLabel");
    auto *opacVal = new QLabel();
    opacVal->setObjectName("sliderValue");
    auto *opacSlider = new QSlider(Qt::Horizontal);
    opacSlider->setRange(10, 100);
    {
        Panel *p2 = panelAt(idx);
        opacSlider->setValue(qRound(p2->opacity() * 100));
    }
    opacVal->setText(QStringLiteral("%1%").arg(opacSlider->value()));
    connect(opacSlider, &QSlider::valueChanged, this, [this, idx, opacVal](int v){
        opacVal->setText(QStringLiteral("%1%").arg(v));
        Panel *p = panelAt(idx);
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
    auto *bgLab = new QLabel();
    trSet(bgLab, QStringLiteral("console.inspector.bg_alpha"));
    bgLab->setObjectName("sliderLabel");
    auto *bgVal = new QLabel();
    bgVal->setObjectName("sliderValue");
    auto *bgSlider = new QSlider(Qt::Horizontal);
    bgSlider->setRange(0, 255);
    {
        Panel *p3 = panelAt(idx);
        bgSlider->setValue(p3->bgAlpha());
    }
    bgVal->setText(QStringLiteral("%1").arg(bgSlider->value()));
    connect(bgSlider, &QSlider::valueChanged, this, [this, idx, bgVal](int v){
        bgVal->setText(QStringLiteral("%1").arg(v));
        Panel *p = panelAt(idx);
        p->setBgAlpha(v, false);
        rebuildAndRender(idx);
    });
    bgRow->addWidget(bgLab);
    bgRow->addWidget(bgSlider, 1);
    bgRow->addWidget(bgVal);
    vl->addLayout(bgRow);
    ctl_[idx].bgAlphaSlider = bgSlider;
    // v0.8: SCREEN row — per-panel output selection. Lists every
    // QScreen the Qt platform plugin reports, plus a "<PRIMARY>"
    // pseudo-entry meaning "follow OS primary" (empty userData).
    // Picking an entry calls Panel::setOutputName(), which validates
    // against the same QScreen list and writes through to panels.ini.
    {
        auto *screenRow = new QHBoxLayout();
        screenRow->setSpacing(8);
        auto *screenLab = new QLabel();
        trSet(screenLab, QStringLiteral("console.inspector.screen"));
        screenLab->setObjectName(QStringLiteral("sliderLabel"));
        auto *combo = new QComboBox();
        combo->setCursor(Qt::PointingHandCursor);
        // First entry: explicit "follow primary" — empty userData so
        // Panel::setOutputName("") clears the override.
        combo->addItem(mh::tr(QStringLiteral("console.inspector.primary")), QString());
        // Item 0 is UI copy ("<PRIMARY>") — re-localize it in place. The
        // remaining items are screen names + geometry and must not move.
        trHook([combo]{
            combo->setItemText(0, mh::tr(QStringLiteral("console.inspector.primary")));
        });
        const auto outs = screen_query::listOutputs();
        for (const auto &o : outs) {
            QString label = o.name;
            if (o.primary)
                label += mh::tr(QStringLiteral("console.inspector.primary_tag"));
            // Append the geometry so the user can tell two same-named
            // outputs apart (rare on Niri, common on X11 multi-GPU).
            label += QStringLiteral("  ·  %1×%2 @%3,%4")
                .arg(o.geometry.width()).arg(o.geometry.height())
                .arg(o.geometry.x()).arg(o.geometry.y());
            combo->addItem(label, o.name);
        }
        // Pick the persisted value (if any) — match either by userData
        // (output name) or fall back to the first entry.
        const Panel *p = panelAt(idx);
        const QString cur = p ? p->outputName() : QString();
        if (!cur.isEmpty()) {
            const int found = combo->findData(cur);
            if (found >= 0) combo->setCurrentIndex(found);
        }
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this, idx, combo](int /*idx2*/){
            Panel *pan = panelAt(idx);
            const QString name = combo->currentData().toString();
            pan->setOutputName(name, /*persist=*/false);
            // saveAppearance is gated on editMode for most setters, but
            // screen selection is meaningful in live mode too — the
            // console is the only place that ever writes it back, so we
            // call the persist path explicitly here.
            pan->saveAppearance();
            updatePosLabel(idx);
            // v0.8: redirect the preview's screen frame to the output
            // the user just picked. The preview is a single widget
            // shared by all four panels, so we follow whichever panel
            // the user is currently configuring — switching the P/M/D/C
            // inspector tab will update it again from the next combo's
            // currentData() if the user picks different screens per
            // panel.
            if (canvas_)
                canvas_->setPreviewScreen(name);
        });
        screenRow->addWidget(screenLab);
        screenRow->addWidget(combo, 1);
        vl->addLayout(screenRow);
        ctl_[idx].outputCombo = combo;
    }
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
    // The reset copy embeds the panel name → formatting hook.
    auto *reset = new QPushButton();
    trHook([reset, titleKey]{
        reset->setText(mh::tr(QStringLiteral("console.inspector.reset"))
                           .arg(consoleText(titleKey)));
    });
    reset->setObjectName("resetButton");
    reset->setCursor(Qt::PointingHandCursor);
    connect(reset, &QPushButton::clicked, this, [this, idx]{ resetPanel(idx); });
    foot->addWidget(reset);
    foot->addStretch(1);
    auto *modified = new QLabel();
    trSet(modified, QStringLiteral("console.inspector.auto_saved"));
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

// v0.7.1: narrow column on the left edge of the window — hosts the
// GAME title and the WORLD / RISE selector buttons. Lives inside the
// horizontal leftSplitter (sibling of the original rail), so the user
// can drag the divider to resize both columns and the choice persists
// via QSettings under ui/leftSplitter.
//
// The column also carries a tiny auto-detect caption so the user still
// sees "which game is running" feedback after the badge left the rail.
// The badge's live state still lives in the rail (it's the click target);
// this caption is read-only — it's pulled from lastDetectedGame_ in
// refreshAutoDetect().
QWidget *ControlPanel::buildGameColumn()
{
    auto *col = new QFrame();
    col->setObjectName("gameColumn");
    col->setMinimumWidth(100);
    col->setMaximumWidth(170);
    auto *vl = new QVBoxLayout(col);
    vl->setContentsMargins(10, 22, 10, 18);
    vl->setSpacing(10);

    auto *title = new QLabel();
    trSet(title, QStringLiteral("console.rail.game"));
    title->setObjectName("sectionCap");
    vl->addWidget(title);

    gameWorldBtn_ = new QPushButton();
    trSet(gameWorldBtn_, QStringLiteral("console.game.world"));
    gameWorldBtn_->setObjectName("gameBtn");
    gameWorldBtn_->setCursor(Qt::PointingHandCursor);
    gameRiseBtn_ = new QPushButton();
    trSet(gameRiseBtn_, QStringLiteral("console.game.rise"));
    gameRiseBtn_->setObjectName("gameBtn");
    gameRiseBtn_->setCursor(Qt::PointingHandCursor);
    connect(gameWorldBtn_, &QPushButton::clicked, this, [this]{ switchGame(mhw::GameId::World); });
    connect(gameRiseBtn_,  &QPushButton::clicked, this, [this]{ switchGame(mhw::GameId::Rise); });
    vl->addWidget(gameWorldBtn_);
    vl->addWidget(gameRiseBtn_);

    auto *sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("gameColumnRule");
    vl->addWidget(sep);

    auto *detectedCap = new QLabel();
    trSet(detectedCap, QStringLiteral("console.rail.detected"));
    detectedCap->setObjectName("sectionCap");
    vl->addWidget(detectedCap);
    // v0.7.1: read-only caption showing which game the auto-detector
    // last saw. Updated alongside the rail badge so the user has the
    // information in both columns.
    auto *detectedValue = new QLabel();
    trSet(detectedValue, QStringLiteral("console.rail.none"));
    detectedValue->setObjectName("gameColumnDetected");
    detectedValue->setWordWrap(true);
    detectedValue->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    vl->addWidget(detectedValue);
    // Stash the caption so refreshAutoDetect() can update it without a
    // second lookup table. Stored as a property so we don't need a new
    // member variable in the header — refreshAutoDetect() walks the
    // column's children the same way it walks the rail badge.
    col->setProperty("detectedValue", QVariant::fromValue(static_cast<void*>(detectedValue)));

    vl->addStretch(1);
    return col;
}

// R5: EDIT MODE block — orange "ENTER EDIT" button on the right,
// caption "进入后三个面板强制显示，方向键移动" on the left.
//
// L3: also add a "START" button to spawn monster-overlay. Same orange
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
    auto *titleLab = new QLabel();
    trSet(titleLab, QStringLiteral("console.edit.title"));
    titleLab->setObjectName("groupTitle");
    titleRow->addWidget(titleLab, 1);
    vl->addLayout(titleRow);

    auto *row = new QHBoxLayout();
    row->setSpacing(10);
    auto *cap = new QLabel();
    trSet(cap, QStringLiteral("console.edit.hint"));
    cap->setObjectName("editCap");
    cap->setWordWrap(true);
    row->addWidget(cap, 1);
    auto *startBtn = new QPushButton();
    trSet(startBtn, QStringLiteral("console.edit.start"));
    startBtn->setObjectName("startBtn");
    startBtn->setCursor(Qt::PointingHandCursor);
    auto *editBtn = new QPushButton();
    trSet(editBtn, QStringLiteral("console.edit.enter"));
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
        launchOverlayChild(/*editMode=*/false);
    });
    connect(editBtn,  &QPushButton::clicked, this, [this]{
        launchOverlayChild(/*editMode=*/true);
    });
    return box;
}

// v0.6 Phase 4: select the target game. Highlights the matching rail
// button and persists the choice (read back at construction); the next
// launch passes it to the overlay via --game.
//
// v0.6 Phase 5: hot-swap. Clicking the OTHER game while the overlay is
// running SIGTERMs it and arms pendingRestart_; the 250ms PID-poll in
// onOverlayExited() observes the exit and relaunches with the new
// --game. Clicking the SAME game is a no-op — no persistence write, no
// SIGTERM, no log noise.
void ControlPanel::switchGame(mhw::GameId game)
{
    const bool changed = (game != currentGame());
    // The controller owns the game value (it drives the child's --game
    // flag) and runs the hot-swap itself, so this half is pure View.
    // Guarded: currentGame() above reads overlay_, and switchGame() is
    // reachable from the rail buttons during teardown.
    if (overlay_) overlay_->setCurrentGame(game);
    const bool isRise = (game == mhw::GameId::Rise);
    refreshRiseReframeworkStatus();

    // v0.10.1: companion surfaces are Rise-only. Selecting World removes the
    // pets rail card, its inspector page and the stage preview tile
    // entirely — there is no World data source for them.
    const bool petsAvailable = (game == mhw::GameId::Rise);
    if (ctl_[3].navButton)
        ctl_[3].navButton->setVisible(petsAvailable);
    if (canvas_)
        canvas_->setPanelPresent(3, petsAvailable);
    if (!petsAvailable && selectedPanel_ == 3)
        selectPanel(2);

    if (gameWorldBtn_) {
        gameWorldBtn_->setProperty("selected", !isRise);
        gameWorldBtn_->style()->unpolish(gameWorldBtn_);
        gameWorldBtn_->style()->polish(gameWorldBtn_);
    }
    if (gameRiseBtn_) {
        gameRiseBtn_->setProperty("selected", isRise);
        gameRiseBtn_->style()->unpolish(gameRiseBtn_);
        gameRiseBtn_->style()->polish(gameRiseBtn_);
    }

    // v0.7.1: rebuild the inspector preview so the player panel shows
    // the right side row for the new game. Without this the inspector
    // was stuck on whatever setupDemoData() first seeded (Rise-only
    // wirebug row, no mantles) regardless of which game the rail
    // picked.
    if (player_) player_->setGameForDemo(game);
    // Toggle visibility of the World-only / Rise-only section rows in
    // place. The rows were created once at construction; show/hide them
    // as the rail selection flips. Rebuilt-then-deleteLater turned out
    // to crash on connect() during the next paint cycle (SEGV in
    // QObjectPrivate::connectImpl), so we mutate the existing widgets
    // instead of recreating the stack.
    //
    // v0.8.4-r23: only the PLAYER panel (idx 0) owns game-specific rows.
    // Do NOT run this over Monster/Damage: `rowBit` is the row index
    // *within that panel*, so e.g. MonsterSection::Parts (1<<4) collides
    // with PlayerSection::Mantles (1<<4) and the monster 部位 row was
    // hidden + disabled whenever Rise was selected.
    // (A previous version gated this on selectedPanel_ == 0 which broke
    // the user flow "switch game while looking at the Monster
    // inspector": the gate skipped the hide, then switching back to
    // Player still showed both rows.)
    for (int p = 0; p < 1; ++p) {
        const uint32_t wirebugBit = uint32_t(mhw::PlayerSection::Wirebug);
        const uint32_t mantlesBit = uint32_t(mhw::PlayerSection::Mantles);
        for (int b = 0; b < ctl_[p].subs.size(); ++b) {
            auto *row = ctl_[p].subs[b];
            if (!row) continue;
            const uint32_t rowBit = (1u << b);
            const bool hideForWorld = (game == mhw::GameId::World)
                                      && (rowBit == wirebugBit);
            const bool hideForRise  = (game == mhw::GameId::Rise)
                                      && (rowBit == mantlesBit);
            const bool hide = hideForWorld || hideForRise;
            row->setVisible(!hide);
            row->setEnabled(!hide);
        }
    }
    for (int i = 0; i < mhw::kPanelCount; ++i)
        rebuildAndRender(i);

    if (changed) {
        QSettings s;
        s.setValue(QStringLiteral("game"),
                   mhw::gameIdToString(isRise ? mhw::GameId::Rise
                                             : mhw::GameId::World));
    }

    if (changed && overlay_->isRunning()) {
        if (statusBadge_) {
            statusBadge_->setText(
                mh::tr(QStringLiteral("console.status.switching"))
                    .arg(gameName(isRise ? mhw::GameId::Rise
                                         : mhw::GameId::World)));
        }
        // The hot-swap itself (SIGTERM + pendingRelunch + poll restart)
        // lives in the controller's setCurrentGame() above.
    }
}

void ControlPanel::refreshRiseReframeworkStatus()
{
    if (!riseReframeworkCard_)
        return;

    const bool riseSelected = currentGame() == mhw::GameId::Rise;
    riseReframeworkCard_->setVisible(riseSelected);
    if (!riseSelected)
        return;

    // The locator is the single source of truth for the Steam install. The
    // manager is still queried for an empty path so every refresh exercises
    // the same validation/status path; no mutation happens in this method.
    riseGameDir_ = mhw::findRiseInstallDir();
    const mhw::RiseReFrameworkManager manager(QCoreApplication::applicationDirPath());
    const mhw::RiseReFrameworkManager::Status status = manager.status(riseGameDir_);
    const bool gameRunning = mhw::detectGame().has_value();

    // The readable status lines are derived by the widget-free view model
    // (src/ui/viewmodel/rise_reframework_status.cpp), which carries the
    // derivation rules and their i18n keys so the REFramework state machine
    // is reachable from a plain logic test. The query above, the console's
    // operation flags and the game-process check are the model's whole input;
    // the join and the write below are still this panel's job.
    mhw::RiseReframeworkStatusInput statusInput;
    statusInput.gameDir = riseGameDir_;
    statusInput.status = status;
    statusInput.gameRunning = gameRunning;
    statusInput.operationPending = riseReframeworkOperationPending_;
    statusInput.hasResult = riseReframeworkHasResult_;
    statusInput.resultOk = riseReframeworkResultOk_;
    statusInput.resultDetail = riseReframeworkResultDetail_;
    const QStringList lines = mhw::riseReframeworkStatusLines(statusInput);
    riseReframeworkStatus_->setText(lines.join(QLatin1Char('\n')));

    // A missing/invalid locator result is not actionable. A running game or
    // an in-flight worker is also a hard safety gate. Destructive actions are
    // enabled only when there is actually something owned to remove.
    const bool canChange = status.gameDirValid && !gameRunning
                           && !riseReframeworkOperationPending_;
    const bool usableCore =
        status.core == mhw::RiseReFrameworkManager::CoreState::Managed
        || status.core == mhw::RiseReFrameworkManager::CoreState::External;
    const bool ready = usableCore
                       && status.lua == mhw::RiseReFrameworkManager::LuaState::Current
                       && status.manifest == mhw::RiseReFrameworkManager::ManifestState::Valid;
    const bool installSafe = status.core != mhw::RiseReFrameworkManager::CoreState::Conflict
                             && status.manifest
                                    != mhw::RiseReFrameworkManager::ManifestState::Invalid;
    installRiseReframeworkButton_->setEnabled(canChange && installSafe && !ready);
    removeRiseLuaButton_->setEnabled(
        canChange && status.lua != mhw::RiseReFrameworkManager::LuaState::Missing);
    removeRiseReframeworkButton_->setEnabled(
        canChange
        && (status.manifest == mhw::RiseReFrameworkManager::ManifestState::Valid
            || status.lua != mhw::RiseReFrameworkManager::LuaState::Missing));
    // The menu-state fix edits REFramework's own user config. The overlay Lua
    // may legitimately be gone (it is ours to remove), and the config file
    // itself survives — but the REFramework CORE is the thing that happens to
    // read that file. With it uninstalled the key is an orphan line nothing
    // parses, so editing it would be a no-op dressed up as a fix. Gate on the
    // core, exactly like the removal buttons do.
    menuStateFixButton_->setEnabled(canChange && usableCore);

    // Report what the files actually say, and label the two actions by the
    // state they lead to rather than by fixed text — the point is that the
    // player can see which one is in effect right now.
    if (menuStateStatus_) {
        const mhw::ReFrameworkMenuStateReport menuReport =
            mhw::queryReFrameworkMenuState(riseGameDir_);
        const char *stateKey =
            menuReport.state == mhw::ReFrameworkMenuState::Overridden
                ? "console.reframework.menu_state_overridden"
                : menuReport.state == mhw::ReFrameworkMenuState::Default
                    ? "console.reframework.menu_state_default"
                    : "console.reframework.menu_state_unknown";
        const QString keyText = mh::tr(QString::fromLatin1(stateKey));
        // With the core gone the key is real but inert — say so rather than
        // leaving the player to read an "active" state that nothing consumes.
        const QString note =
            usableCore ? QString()
                       : QStringLiteral("\n")
                         + mh::tr(QStringLiteral(
                             "console.reframework.menu_state_no_core"));
        // Only name the paths when there is more than one: a single game-dir
        // file needs no listing, while several mean the fallback locations are
        // in play and the player should know which ones were touched.
        QString pathText;
        if (menuReport.paths.size() > 1) {
            pathText = QStringLiteral("\n")
                       + mh::tr(QStringLiteral("console.reframework.menu_state_paths"))
                             .arg(menuReport.paths.size())
                       + QStringLiteral("\n")
                       + menuReport.paths.join(QStringLiteral("\n"));
        }
        menuStateStatus_->setText(keyText + note + pathText);
        menuStateRestoreButton_->setEnabled(
            canChange && usableCore
            && menuReport.state != mhw::ReFrameworkMenuState::Default);
    }
}

void ControlPanel::requestRiseReframeworkInstall()
{
    if (currentGame() != mhw::GameId::Rise || riseGameDir_.isEmpty()
        || riseReframeworkOperationPending_ || mhw::detectGame().has_value()) {
        refreshRiseReframeworkStatus();
        return;
    }
    const auto answer = QMessageBox::question(
        this,
        mh::tr(QStringLiteral("console.reframework.confirm_title")),
        mh::tr(QStringLiteral("console.reframework.confirm_install")).arg(riseGameDir_),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    riseReframeworkOperationPending_ = true;
    riseReframeworkHasResult_ = false;
    riseReframeworkResultDetail_.clear();
    refreshRiseReframeworkStatus();
    if (receivers(SIGNAL(installRiseReframeworkRequested(QString))) == 0) {
        finishRiseReframeworkOperation(
            false, mh::tr(QStringLiteral("console.reframework.no_worker")));
        return;
    }
    emit installRiseReframeworkRequested(riseGameDir_);
}

void ControlPanel::requestRiseLuaRemoval()
{
    if (currentGame() != mhw::GameId::Rise || riseGameDir_.isEmpty()
        || riseReframeworkOperationPending_ || mhw::detectGame().has_value()) {
        refreshRiseReframeworkStatus();
        return;
    }
    const auto answer = QMessageBox::question(
        this,
        mh::tr(QStringLiteral("console.reframework.confirm_title")),
        mh::tr(QStringLiteral("console.reframework.confirm_remove_lua")).arg(riseGameDir_),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    riseReframeworkOperationPending_ = true;
    riseReframeworkHasResult_ = false;
    riseReframeworkResultDetail_.clear();
    refreshRiseReframeworkStatus();
    if (receivers(SIGNAL(removeRiseLuaRequested(QString))) == 0) {
        finishRiseReframeworkOperation(
            false, mh::tr(QStringLiteral("console.reframework.no_worker")));
        return;
    }
    emit removeRiseLuaRequested(riseGameDir_);
}

void ControlPanel::requestRiseMenuStateFix(bool restoreDefault)
{
    // Same core gate as the button: without a REFramework core there is
    // nothing to read the key we would write, so the action is meaningless.
    const mhw::RiseReFrameworkManager manager(
        QCoreApplication::applicationDirPath());
    const mhw::RiseReFrameworkManager::Status rfStatus =
        manager.status(mhw::findRiseInstallDir());
    const bool rfCorePresent =
        rfStatus.core == mhw::RiseReFrameworkManager::CoreState::Managed
        || rfStatus.core == mhw::RiseReFrameworkManager::CoreState::External;

    if (currentGame() != mhw::GameId::Rise || riseGameDir_.isEmpty()
        || riseReframeworkOperationPending_ || !rfCorePresent
        || mhw::detectGame().has_value()) {
        refreshRiseReframeworkStatus();
        return;
    }

    // Every place REFramework might read its config from — the game dir plus
    // the %APPDATA% fallback it switches to when the game dir is unreachable.
    const QStringList configPaths =
        mhw::reframeworkConfigPaths(riseGameDir_);
    const QString pathList = configPaths.join(QStringLiteral("\n"));
    const char *confirmKey = restoreDefault
        ? "console.reframework.confirm_restore_menu_state"
        : "console.reframework.confirm_fix_menu_state";
    const auto answer = QMessageBox::question(
        this,
        mh::tr(QStringLiteral("console.reframework.confirm_title")),
        mh::tr(QString::fromLatin1(confirmKey)).arg(pathList),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    // One key, a handful of files. Nothing is downloaded and no core file is
    // touched, so this runs inline — but it still routes through the shared
    // pending/result state so the card reads the same as install/removal.
    riseReframeworkOperationPending_ = true;
    riseReframeworkHasResult_ = false;
    riseReframeworkResultDetail_.clear();
    refreshRiseReframeworkStatus();

    mhw::ReFrameworkMenuStateReport report;
    if (!mhw::applyReFrameworkMenuStateFix(riseGameDir_, restoreDefault, &report)) {
        finishRiseReframeworkOperation(
            false, mh::tr(QStringLiteral("console.reframework.fix_menu_state_failed")));
        return;
    }
    finishRiseReframeworkOperation(true, QString());
}

void ControlPanel::requestRiseReframeworkRemoval()
{
    if (currentGame() != mhw::GameId::Rise || riseGameDir_.isEmpty()
        || riseReframeworkOperationPending_ || mhw::detectGame().has_value()) {
        refreshRiseReframeworkStatus();
        return;
    }
    const auto answer = QMessageBox::question(
        this,
        mh::tr(QStringLiteral("console.reframework.confirm_title")),
        mh::tr(QStringLiteral("console.reframework.confirm_remove_reframework"))
            .arg(riseGameDir_),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    riseReframeworkOperationPending_ = true;
    riseReframeworkHasResult_ = false;
    riseReframeworkResultDetail_.clear();
    refreshRiseReframeworkStatus();
    if (receivers(SIGNAL(removeRiseReframeworkRequested(QString))) == 0) {
        finishRiseReframeworkOperation(
            false, mh::tr(QStringLiteral("console.reframework.no_worker")));
        return;
    }
    emit removeRiseReframeworkRequested(riseGameDir_);
}

void ControlPanel::finishRiseReframeworkOperation(bool ok, const QString &detail)
{
    riseReframeworkOperationPending_ = false;
    riseReframeworkHasResult_ = true;
    riseReframeworkResultOk_ = ok;
    riseReframeworkResultDetail_ = detail;
    refreshRiseReframeworkStatus();
}

// K4-A3 split. The old single function did three things: persist the
// user's current mask + per-panel appearance, build the overlay's argv
// from that same state, and hand both to the controller. Those are now
// three named halves:
//
//   saveBeforeLaunch()     — the write-disk half, also called directly by
//                            the four UI entry points so the on-disk mask
//                            is fresh before anything touches it
//   buildOverlayArgv()     — the pure argv construction (const)
//   launchOverlayChild()   — the liveness check plus the delegation
//
// The spawn/poll/hot-swap half of the original launchOverlay() moved out
// in v0.11 to mhw::OverlayProcessController::launch(); what is left here
// is the View's contribution (persist → build → hand off).
void ControlPanel::saveBeforeLaunch()
{
    saveMaskToDisk();
    // v0.5: persist scale / opacity / margins so the overlay's
    // loadConfig() reads the values the user set in the console.
    // Without this, the overlay always starts with the last
    // edit-mode save (or factory defaults).
    for (int i = 0; i < mhw::kPanelCount; ++i)
        if (Panel *panel = panelAt(i))
            panel->saveAppearance();
}

QStringList ControlPanel::buildOverlayArgv(bool editMode) const
{
    // Build argv from the same mask source the file uses.
    const uint32_t mp = ControlPanel::maskFor(ctl_[0]);
    const uint32_t mm = ControlPanel::maskFor(ctl_[1]);
    const uint32_t md = ControlPanel::maskFor(ctl_[2]);
    const uint32_t mt = ControlPanel::maskFor(ctl_[3]);

    // v0.10.1: the companion (pets) surface is Rise-only — in World its mask
    // is not passed and the panel is force-disabled below.
    const bool petsAvailable = (currentGame() == mhw::GameId::Rise);
    QStringList args;
    args << QStringLiteral("--mask-player=%1").arg(mp, 0, 16)
         << QStringLiteral("--mask-monster=%1").arg(mm, 0, 16)
         << QStringLiteral("--mask-damage=%1").arg(md, 0, 16);
    if (petsAvailable)
        args << QStringLiteral("--mask-pets=%1").arg(mt, 0, 16);
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
    if (!petsAvailable || !ctl_[3].master->isChecked())
        args << QStringLiteral("--no-pets");
    if (editMode) args << QStringLiteral("--edit");

    // v0.9 i18n: start the overlay in the console's active language.
    // The overlay resolves --locale > conf (src/main.cpp), so passing it
    // explicitly means a freshly spawned overlay never flips language on
    // its first conf poll. The locale= row written by saveMaskToDisk()
    // below covers the manually-started case; WS-C's poll (locale_sync.h)
    // remains the runtime path once the user flips the chip.
    {
        const QString loc = mhw::StringTable::instance().currentLocale();
        if (!loc.isEmpty())
            args << QStringLiteral("--locale=%1").arg(loc);
    }

    // v0.8: per-panel screen selection. Empty outputName == "<PRIMARY>"
    // pseudo-entry in the console, which means "follow OS primary" —
    // we omit the flag in that case so the overlay's loadConfig() can
    // fall through to its own default. A non-empty name matches
    // QScreen::name() on the overlay side; on Niri that's the wlr-output
    // id and LayerShellQt::Window::setScreen() binds correctly.
    auto appendOutput = [&](int idx, const char *flag) {
        Panel *p = panelAt(idx);
        const QString name = p ? p->outputName() : QString();
        if (!name.isEmpty())
            args << QString::fromLatin1(flag) + QStringLiteral("=") + name;
    };
    appendOutput(0, "--output-player");
    appendOutput(1, "--output-monster");
    appendOutput(2, "--output-damage");
    appendOutput(3, "--output-pets");

    return args;
}

// The remains of the old one-function launch path: refuse a second copy,
// then persist → build → hand off. Already-running is checked here (and
// again inside the controller) because the View-side early return also
// saves a pointless disk write.
void ControlPanel::launchOverlayChild(bool editMode)
{
    if (overlay_->isRunning()) {
        // Already running — refuse to launch a second copy. The user
        // can press ESC in the overlay to bring the console back, then
        // click again.
        return;
    }
    saveBeforeLaunch();
    overlay_->launch(buildOverlayArgv(editMode), editMode);
}

// v0.6 Phase 5: live auto-detect badge. Rescans /proc and repaints the
// chip in the GAME row: grey = nothing running, cyan = detected game
// matches currentGame_, amber = mismatch (click switches / hot-swaps).
void ControlPanel::refreshAutoDetect()
{
    if (!autoDetectBadge_) return;
    const auto detected = mhw::detectGame();
    QString detectedShort;
    if (detected) {
        lastDetectedGame_ = detected->game;
        QSettings s;
        s.setValue(QStringLiteral("detectedGame"),
                   mhw::gameIdToString(detected->game));
        const QString name = gameName(detected->game);
        const bool match = (detected->game == currentGame());
        detectedShort = mh::tr(QStringLiteral("console.detect.short"))
                            .arg(name).arg(detected->pid);
        autoDetectBadge_->setText(
            mh::tr(QStringLiteral("console.detect.badge"))
                .arg(name).arg(detected->pid));
        autoDetectBadge_->setProperty("state", match ? "cyan" : "amber");
        autoDetectBadge_->setCursor(match ? Qt::ArrowCursor
                                          : Qt::PointingHandCursor);
    } else {
        detectedShort = mh::tr(QStringLiteral("console.rail.none"));
        autoDetectBadge_->setText(mh::tr(QStringLiteral("console.detect.none")));
        autoDetectBadge_->setProperty("state", "gray");
        autoDetectBadge_->setCursor(Qt::ArrowCursor);
    }
    autoDetectBadge_->style()->unpolish(autoDetectBadge_);
    autoDetectBadge_->style()->polish(autoDetectBadge_);
    // v0.7.1: mirror the detected game into the GAME column's read-only
    // caption so the user sees it without scrolling to the rail badge.
    if (leftSplitter_) {
        auto *col = leftSplitter_->widget(0);
        if (col) {
            QLabel *cap = static_cast<QLabel*>(col->property("detectedValue").value<void*>());
            if (cap) cap->setText(detectedShort);
        }
    }
}

void ControlPanel::loadMaskFromDisk()
{
    const QString path = mhw::localeConfPath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // No file = first run, keep the all-on default the buildGroup()
        // calls already established.
        return;
    }
    // File format (4 mask lines, key=value hex32):
    //   player=<hex32>
    //   monster=<hex32>
    //   damage=<hex32>
    //   pets=<hex32>
    // Anything malformed is silently ignored — we never want a bad
    // config to make the console unstartable.
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QString text = in.readAll();

    const mhw::PanelMaskSet masks = mhw::parsePanelMasks(text);

    // v0.9 i18n (integration fix): the optional `locale=` row is the
    // console->overlay handshake (core/locale_conf.h). It is resolved at
    // startup by main_control.cpp (--locale > conf > system locale) and by the
    // ctor guard above; it must NOT be re-applied here. Doing so silently
    // overrode an explicit `--locale` with a stale conf value and produced
    // a split-language UI (already-registered chrome flipped to the conf
    // locale via applyLocale()'s replay while widgets built before/after
    // the apply kept the CLI locale). Runtime switches go through
    // switchLocale() (the EN/CH chip); cross-process sync is the overlay's
    // conf poll (locale_sync.h).

    auto applyTo = [](bool valid, uint32_t mask, PanelCtl &c) {
        if (!valid)
            return;
        // master stays ON if any bit is set; otherwise treat as fully
        // disabled (mirrors the "all off" intent in the file).
        c.master->setChecked(mask != 0u);
        for (int b = 0; b < c.subs.size(); ++b)
            c.subs[b]->setChecked((mask & (1u << b)) != 0u);
    };
    applyTo(masks.playerValid, masks.player, ctl_[0]);
    applyTo(masks.monsterValid, masks.monster, ctl_[1]);
    applyTo(masks.damageValid, masks.damage, ctl_[2]);
    applyTo(masks.petsValid, masks.pets, ctl_[3]);
}

void ControlPanel::saveMaskToDisk() const
{
    const QString path = mhw::localeConfPath();

    const uint32_t mp = ControlPanel::maskFor(ctl_[0]);
    const uint32_t mm = ControlPanel::maskFor(ctl_[1]);
    const uint32_t md = ControlPanel::maskFor(ctl_[2]);
    const uint32_t mt = ControlPanel::maskFor(ctl_[3]);

    // The mask writer owns exactly these four rows. Preserve locale, comments
    // and unknown/future keys in their original relative order, while removing
    // every old copy of a canonical mask row so duplicates converge.
    QString preservedText;
    if (QFileInfo::exists(path)) {
        QFile existing(path);
        if (!existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning("monster-control: cannot read %s before saving: %s",
                     qPrintable(path), qPrintable(existing.errorString()));
            return;
        }
        QTextStream in(&existing);
        in.setEncoding(QStringConverter::Utf8);
        preservedText = in.readAll();
    }

    const QByteArray data = mhw::serializePanelMasks(
        mp, mm, md, mt, preservedText);

    const QString parent = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(parent)) {
        qWarning("monster-control: cannot create config directory %s",
                 qPrintable(parent));
        return;
    }

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("monster-control: cannot atomically open %s: %s",
                 qPrintable(path), qPrintable(out.errorString()));
        return;
    }
    if (out.write(data) != data.size()) {
        qWarning("monster-control: cannot atomically write %s: %s",
                 qPrintable(path), qPrintable(out.errorString()));
        out.cancelWriting();
        return;
    }
    if (!out.commit()) {
        qWarning("monster-control: cannot atomically commit %s: %s",
                 qPrintable(path), qPrintable(out.errorString()));
    }
}

void ControlPanel::rebuildAndRender(int idx)
{
    if (!mhw::isPanelIndex(idx))
        return;
    Panel *panel = panelAt(idx);
    if (!panel)
        return;

    auto *lab = ctl_[idx].preview;
    updatePanelSummary(idx);

    uint32_t mask = 0;
    for (int b = 0; b < ctl_[idx].subs.size(); ++b)
        if (ctl_[idx].subs[b]->isChecked())
            mask |= (1u << b);
    panel->setSectionMask(mask);   // also calls update()

    // Section bits that filter actors are also expressed through the frozen
    // Rise display-options interface. The generic section mask still owns
    // rows/chart layout; these options own actor inclusion.
    if (idx == 2) {
        mhw::RiseDamageDisplayOptions options;
        options.showOtherMembers = (mask & mhw::DamageSection::OtherMembers) != 0;
        damage_->setRiseDisplayOptions(options);
        prepareDamagePreview(damage_, options);
    } else if (idx == 3) {
        mhw::RiseDamageDisplayOptions options;
        options.showLocalPets = (mask & mhw::PetDamageSection::LocalPets) != 0;
        options.showOtherPets = (mask & mhw::PetDamageSection::OtherPets) != 0;
        pets_->setDisplayOptions(options);
    }

    const bool riseOnlyUnavailable =
        idx == 3 && currentGame() == mhw::GameId::World;
    if (riseOnlyUnavailable || !ctl_[idx].master->isChecked()) {
        if (canvas_)
            canvas_->setPanelPixmap(idx, QPixmap(), false);
        // Master off (or World-mode pets): show a flat disabled placeholder.
        // The panel is not painted into the HUD canvas, matching the live
        // visibility gate.
        QPixmap ph(378, 90);
        ph.fill(QColor(22, 24, 26));
        QPainter p(&ph);
        p.setPen(QColor(80, 83, 85));
        QFont f("Chakra Petch", 11, QFont::Bold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 2);
        p.setFont(f);
        p.drawText(ph.rect(), Qt::AlignCenter,
                   riseOnlyUnavailable
                       ? consoleText(QStringLiteral("console.panel.pets_rise_only"))
                       : mh::tr(QStringLiteral("console.panel.disabled")));
        p.end();
        if (lab)
            lab->setPixmap(ph);
        return;
    }

    const QPixmap pix = renderPreview(panel);
    if (lab)
        lab->setPixmap(pix);
    if (canvas_)
        canvas_->setPanelPixmap(idx, pix, true);
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
    // preview tile (purple for player, orange for monster, blue for damage,
    // green for pets) so each panel has a clear identity at a glance. The stripe
    // sits OUTSIDE the panel rectangle, so we just draw it on the pixmap.
    QColor accent;
    if (p == player_)      accent = QColor(170, 85, 255);   // #aa55ff
    else if (p == monster_) accent = QColor(255, 128, 64);  // #ff8040
    else if (p == damage_) accent = QColor(64, 169, 255);   // #40a9ff
    else                   accent = QColor(103, 214, 157);  // #67d69d
    QPainter stripe(&pix);
    stripe.fillRect(QRect(0, 0, 4, sz.height()), accent);
    stripe.end();
    return pix;
}

// v0.5 P1: switch the START button between launch and stop modes and
// re-style the status badge. Safe to call repeatedly; cheap idempotent
// state flip.
void ControlPanel::setOverlayRunning(bool running)
{
    if (!startBtn_) return;
    if (running) {
        startBtn_->setText(mh::tr(QStringLiteral("console.rail.stop_overlay")));
        startBtn_->setObjectName(QStringLiteral("stopBtn"));
        if (statusBadge_) {
            statusBadge_->setText(
                mh::tr(QStringLiteral("console.status.running_pid"))
                    .arg(overlay_->overlayPid()));
            statusBadge_->setProperty("state", "running");
        }
    } else {
        startBtn_->setText(mh::tr(QStringLiteral("console.rail.start_overlay")));
        startBtn_->setObjectName(QStringLiteral("startBtn"));
        if (statusBadge_) {
            statusBadge_->setText(mh::tr(QStringLiteral("console.status.ready")));
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
    if (!mhw::isPanelIndex(idx))
        return;
    Panel *panel = panelAt(idx);
    if (!panel)
        return;
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
    if (!mhw::isPanelIndex(idx))
        return;
    Panel *panel = panelAt(idx);
    if (!panel)
        return;
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
    if (!mhw::isPanelIndex(idx))
        return;
    if (!ctl_[idx].posLabel) return;
    Panel *p = panelAt(idx);
    if (!p) return;
    const QMargins m = p->margins();
    // Display the panel's *current screen quadrant*, not its immutable
    // layer-shell anchor. Anchor stays fixed (so drag remains stable),
    // while this label follows the actual position when margins cross a
    // half-screen line.
    // v0.8.4-r23: honour the panel's own SCREEN selection — the canvas
    // preview and the live overlay both bind to p->outputName(); using
    // the primary screen here made the quadrant/coords readout wrong
    // for any panel assigned to a non-primary output.
    const QScreen *screen = nullptr;
    const QString outputName = p->outputName();
    if (!outputName.isEmpty()) {
        const auto screens = QGuiApplication::screens();
        for (QScreen *s : screens) {
            if (s && s->name() == outputName) { screen = s; break; }
        }
    }
    if (!screen) screen = QGuiApplication::primaryScreen();
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
    const QString corner = top
        ? (left ? mh::tr(QStringLiteral("console.corner.top_left"))
                : mh::tr(QStringLiteral("console.corner.top_right")))
        : (left ? mh::tr(QStringLiteral("console.corner.bottom_left"))
                : mh::tr(QStringLiteral("console.corner.bottom_right")));
    ctl_[idx].posLabel->setText(
        mh::tr(QStringLiteral("console.pos.label"))
            .arg(corner).arg(m.left()).arg(m.top())
            .arg(m.right()).arg(m.bottom()));
}
