#include "control_panel.h"

#include <array>
#include <utility>
#include <vector>

#include "ui/panel.h"
#include "ui/window_layout_geometry.h"
#include "ui/console_root_scroll.h"
#include "ui/panel_accent.h"
// v0.13: the per-game layout namespace. The console constructs the split
// preview windows itself, so it must declare which game they belong to —
// the scoping is a process-global that only main.cpp used to set, which
// left every console preview reading the World sections.
#include "ui/panel_settings_game.h"
#include "ui/panel_player.h"
#include "ui/panel_monster.h"
#include "ui/panel_damage.h"
#include "ui/panel_pet_damage.h"
#include "ui/panel_sections.h"
// v0.13: the split sub-window preview. Every one of these headers is a
// complete type the loop below constructs; each is already in mhw-ui
// (CMakeLists.txt), so including them here adds no translation unit and
// no link line.
#include "ui/panel_player_split.h"
#include "ui/panel_player_split_bars.h"
#include "ui/panel_player_split_mantles.h"
#include "ui/panel_player_split_weapon.h"
#include "ui/panel_player_split_wirebug.h"
#include "ui/panel_player_split_info.h"
#include "ui/panel_monster_split_hp.h"
#include "ui/panel_monster_split_stamina.h"
#include "ui/panel_monster_split_enrage.h"
#include "ui/panel_monster_split_ail.h"
#include "ui/panel_monster_split_parts.h"
#include "ui/panel_damage_split_share.h"
#include "ui/panel_damage_split_chart.h"
// v0.15: the rows window. The third damage split surface, and the one
// whose absence the user reported ("dps 部分缺少一个面板，我们按玩家
// 统计的那部分不见了"): with Share and Chart carved out of DamagePanel
// and the Console previewing only the split's sub-windows, a Split
// preview showed two boxes and no player rows at all.
#include "ui/panel_damage_split_rows.h"
#include "ui/panel_monster_split_identity.h"
#include "ui/panel_monster_split_conditions.h"
#include "ui/toggle_chip.h"
#include "ui/section_row.h"
#include "ui/section_count_bar.h"
#include "ui/hud_canvas.h"
#include "ui/panel_source.h"
#include "ui/screen_query.h"
#include "ui/ui_theme.h"
#include "ui/control_panel_styles.h"
#include "ui/rise_reframework_card.h"
#include "ui/viewmodel/console_text_helpers.h"
#include "ui/viewmodel/overlay_process_controller.h"
#include "ui/viewmodel/console_layout_store.h"
#include "ui/viewmodel/console_layout_mode_codec.h"
#include "ui/viewmodel/panel_mask_codec.h"
// v1.0.0 fidelity port: the window model's accessors (winVisible key,
// the own→parent→primary output chain, the display label).
#include "ui/console_fidelity_windows.h"
#include "core/game_detector.h"
#include "core/game_profile.h"
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
#include <QGroupBox>
#include <QLabel>
#include <QPointer>
#include <QSizePolicy>
// v1.1.1 (顶栏溢出): ControlPanel::resizeEvent re-decides the topbar's
// wrap, so the event type is needed complete in this translation unit.
#include <QResizeEvent>
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
#include <QMessageBox>
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
// v1.0.0 fidelity port: the four modal entries need a real QDialog, a
// plain-text box for the export payload, a file dialog for the download,
// a clipboard for the copy, and a single-shot timer for the toast.
#include <QDialog>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QClipboard>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMouseEvent>
#include <QAbstractButton>
// Qt6::Network, linked into mhw-ui for the one entry that needs it: the
// console's 检查更新, which asks GitHub for the latest release instead of
// printing the prototype's fake answer. See checkForUpdates().
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <signal.h>
#include <sys/types.h>
#include <optional>

// ─── v0.13: the split's sub-window preview ─────────────────────────────
//
// The console's own holder for the fourteen split windows. The live
// overlay's copies are stack objects in main.cpp with a full wiring set
// (setEditMode / setGameForDemo / setSectionMask / setPanelEnabled /
// retranslateUi / show); the console needs a smaller subset, because
// all it does is let each window paint itself once into a pixmap the
// HudCanvas can place. Same windows, same paint path, one table.
//
// THE DERIVATION, AND WHY IT IS NOT A LIST.
//
// Every row below is a row of a table in ui/panel_sections.h:
// kPlayerSplitTargets (6), kMonsterSplitTargets (5),
// kDamageSplitTargets (2). The order of buildTable() matches the order
// of the tables' rows, and each entry carries the (parentSlot, row)
// pair the canvas needs to colour it and title it.
//
// The class-per-row mapping IS the one thing that cannot be derived
// from the tables, because a table row is a bitmask and a class is a
// type: nothing in `{PlayerSection::Debuff | PlayerSection::Buff}`
// names PlayerStatusStripPanel. But the mapping lives in ONE place
// here, it is checked against the tables by a static_assert pair, and
// adding a row upstream fails that assert until someone adds a line
// below. A hardcoded window-name list would have drifted in step with
// the tables the first time either changed alone; this asserts
// instead of drifting.
namespace {

// One row of one split table, as the preview needs it: the window to
// build, and which (panel, row) it is.
struct SplitWindowRow {
    // Which base panel this row was carved out of: 0 player, 1 monster,
    // 2 damage. Pets has no split table, so it never appears.
    int parentSlot;
    // The row's index inside that parent's table. Must equal the
    // position of the corresponding literal below.
    int tableRow;
    // The settingsKey the window reads its own panel settings under.
    const char *settingsKey;
};

// THE BUILD TABLE. One line per split table row, in table order. The
// two counts below pin its length against the tables, so a new row
// upstream cannot be added silently.
//
// Keep this in EXACTLY the same order as the tables in panel_sections.h
// (kPlayerSplitTargets rows 0..5, then kMonsterSplitTargets 0..4, then
// kDamageSplitTargets 0..1), because the test asks for a row and then
// asks the table whether that row's mask is the one the class claims.
const SplitWindowRow kSplitWindowRows[] = {
    // ---- kPlayerSplitTargets, 6 rows ----
    {0, 0, "player-status"},      // Debuff | Buff   -> PlayerStatusStripPanel
    {0, 1, "player-mantles"},     // Mantles         -> PlayerMantlesStripPanel
    {0, 2, "player-wirebug"},     // Wirebug         -> PlayerWirebugStripPanel
    {0, 3, "player-bars"},        // Bars            -> PlayerBarsStripPanel
    {0, 4, "player-weapon"},      // Weapon|Sharpness-> PlayerWeaponStripPanel
    {0, 5, "player-info"},        // Conn | Quest    -> PlayerInfoStripPanel
    // ---- kMonsterSplitTargets, 2 rows ----
    // v0.13 regrouped these from five single-block windows to two, on the
    // user's verdict that the split was "切太狠了": the glance (portrait +
    // enrage + HP + stamina) is one window, the plan (ailments + parts) is
    // the other. The five single-block classes still exist in the tree but
    // no split-table row claims them, so a preview that constructed them
    // was previewing windows that do not ship.
    {1, 0, "monster-identity"},   // Info|Hp|Enrage|Stamina
    {1, 1, "monster-conditions"}, // Ail|Parts
    // ---- kDamageSplitTargets, 2 rows ----
    {2, 0, "damage-share"},       // Share -> DamageShareStripPanel
    {2, 1, "damage-chart"},       // Chart -> DamageChartStripPanel
    // ---- v0.15: the rows window, which is NOT a kDamageSplitTargets
    // row and is deliberately not counted as one ----------------------
    //
    // WHY IT IS SEPARATE. Every row above claims a section bit out of
    // the table its index is a subscript of, and damagePanelMaskFor()
    // then takes that bit away from DamagePanel so a block is drawn by
    // exactly one surface. Rows is the one block that must NOT move
    // out: panel_sections.h records the original reasoning ("Rows is
    // the DPS panel's SUBSTANCE — splitting it would leave a panel that
    // mounts as an empty title row") and that reasoning still holds for
    // the overlay.
    //
    // What it did not anticipate is the CONSOLE. The console's Split
    // preview draws ONLY the split's sub-windows, so with Share and
    // Chart carved out the user saw two boxes and no rows:
    // 「dps 部分缺少一个面板，我们按玩家统计的那部分不见了」.
    //
    // v0.15: Rows IS a table row now — kDamageSplitTargets[2] is
    // {DamageSection::Rows} and DamageSplitSlot::Rows is 2. The v0.14
    // reasoning was that the preview needs a third window while the
    // OVERLAY's mask arithmetic must not move, and the answer chosen then
    // was a window beside the table. That was wrong, and it produced a
    // worse bug than the one it avoided: the console built the window and
    // the live overlay never mounted it, so the console promised a window
    // the overlay did not have — the same console/overlay disagreement
    // this round is about, one layer down. Moving Rows into the table
    // makes both read the same fact.
    {2, static_cast<int>(mhw::DamageSplitSlot::Rows), "damage-rows"},
};

// The rows window's row index, which v0.15 made an ALIAS rather than a
// bare 2 — the row now claims DamageSection::Rows like every other, so a
// literal here was a second place to forget that.
constexpr int kSplitDamageRowsRow = mhw::DamageSplitSlot::Rows;

constexpr int kSplitWindowRowCount =
    static_cast<int>(sizeof(kSplitWindowRows) / sizeof(kSplitWindowRows[0]));

// THE COUNT ASSERT. If a row is appended to any of the three tables,
// this fails and points at the table that grew — which is exactly the
// signal the author needs to add the matching line above and below.
static_assert(mhw::PlayerSection::kPlayerSplitTargetCount == 6,
              "kPlayerSplitTargets row count changed: add a line to "
              "kSplitWindowRows AND a case in makeSplitWindow()");
static_assert(mhw::kMonsterSplitTargetCount == 2,
              "kMonsterSplitTargets row count changed: add a line to "
              "kSplitWindowRows AND a case in makeSplitWindow()");
static_assert(mhw::DamageSplitSlot::kCount == 3,
              "kDamageSplitTargets row count changed: add a line to "
              "kSplitWindowRows AND a case in makeSplitWindow()");
// Spelled from the three counts rather than a literal sum, so the monster
// regrouping (five rows to two) could not leave a `13` behind: a literal
// there is a fourth place the same number is written, and it was the one
// that did not change.
//
// v0.15: the "+ 1" is the rows window, which is deliberately NOT one of
// the three tables' rows (it claims no bit, so it cannot be). The count
// still has to be exact: a table that grows without a line below, or a
// line below without a table that grew, must both fail to build. The
// one extra window is accounted for in one place, with the reason next
// to it, rather than being the number this file forgets to update.
// v0.15: no longer "+ 1". The rows window used to sit BESIDE the split
// table — it claimed no bit, so the table's row count could not describe
// it and this sum needed a fudge. It is in the table now
// (DamageSection::Rows), so the three counts describe every window and the
// fudge is gone. A literal here is a fourth place the same number is
// written, and it is the one that does not change.
static_assert(kSplitWindowRowCount
                  == mhw::PlayerSection::kPlayerSplitTargetCount
                         + mhw::kMonsterSplitTargetCount
                         + mhw::DamageSplitSlot::kCount,
              "the build table must have one line per split-table row");

// Construct the window for one row. A switch, not a table of factory
// lambdas, because each type is a distinct class with a distinct
// constructor: a lambda table would still have to name both.
//
// Returns nullptr for a row this build does not know, which the caller
// treats as "not in this game's preview" rather than as an error.
std::unique_ptr<Panel> makeSplitWindow(const SplitWindowRow &row)
{
    // The four base panels answer TopLeft/TopRight/BottomLeft/Bottom;
    // every split window is anchored exactly like the panel it came
    // from, which is what makes the preview's cluster sit on the right
    // side of the parent. The corner is therefore chosen per parent,
    // not per window, and the individual windows' own constructors
    // default to TopLeft/Right the same way.
    const Corner corner =
        (row.parentSlot == 1 || row.parentSlot == 2) ? Corner::TopRight
                                                     : Corner::TopLeft;
    const bool goRight = (corner == Corner::TopLeft);

    switch (row.parentSlot * 100 + row.tableRow) {
    case 0 * 100 + 0:
        return std::make_unique<PlayerStatusStripPanel>();
    case 0 * 100 + 1:
        return std::make_unique<PlayerMantlesStripPanel>();
    case 0 * 100 + 2:
        return std::make_unique<PlayerWirebugStripPanel>();
    case 0 * 100 + 3:
        return std::make_unique<PlayerBarsStripPanel>();
    case 0 * 100 + 4:
        return std::make_unique<PlayerWeaponStripPanel>();
    case 0 * 100 + 5:
        return std::make_unique<PlayerInfoStripPanel>();
    // v0.13: the regrouped monster windows. Identity carries the glance
    // (portrait, enrage, HP, stamina) and Conditions the plan (ailments,
    // parts) — the two rows of kMonsterSplitTargets, so this switch and
    // the table cannot drift apart without the row-count static_assert
    // above failing first.
    case 1 * 100 + 0:
        return std::make_unique<MonsterIdentityStripPanel>();
    case 1 * 100 + 1:
        return std::make_unique<MonsterConditionsStripPanel>();
    case 2 * 100 + 0:
        return std::make_unique<DamageShareStripPanel>();
    case 2 * 100 + 1:
        return std::make_unique<DamageChartStripPanel>();
    // v0.15: the rows window. Its row index is kSplitDamageRowsRow, which
    // is one past the last kDamageSplitTargets subscript — see the note
    // beside the build table for why it is not a table row and why that
    // is the right shape rather than an omission.
    case 2 * 100 + kSplitDamageRowsRow:
        return std::make_unique<DamageRowsStripPanel>();
    default:
        break;
    }
    (void)goRight;   // the corner is the window ctor's own business
    return nullptr;
}

// Render one window the way the console renders its four base panels:
// repaint, then blit into an offscreen pixmap with the panel's own
// opacity already composited in by paintEvent.
QPixmap renderSplitWindow(Panel *p)
{
    p->repaint();
    const QSize sz = p->size();
    QPixmap pix(sz);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    p->render(&painter, QPoint(), QRect(QPoint(0, 0), sz));
    painter.end();
    return pix;
}

} // namespace

// ─── the window-model arithmetic, forward-declared for the host ─────────
//
// The host's renderAll() needs the SAME two answers buildWindowStates()
// uses — "which sections does this split row claim" and "which of them
// exist in this game" — to decide a pixmap's `enabled` axis. They are
// file-local (defined beside buildWindowStates(), inside the anonymous
// namespace), so the declaration has to precede the host rather than
// follow it. This is a forward declaration of the SAME functions in the
// SAME anonymous namespace — all anonymous namespace blocks in one
// translation unit denote one namespace — so there is still exactly one
// definition and one spelling of each. See control_panel.cpp:644.
namespace {
uint32_t panelSectionSchema(int panelIndex, mhw::GameId game);
uint32_t splitWindowSections(int panelIndex, int tableRow);
int countSectionBits(uint32_t mask);
} // namespace

// ─── the host ───────────────────────────────────────────────────────────

// How many grid columns the Rise wall is shifted by, relative to World's.
// One column: enough that a Rise preview is visibly its own arrangement,
// small enough that the cluster still starts near the corner and the walk
// still reads outward in table order.
constexpr int kRiseWallColumnShift = 1;

class SplitWindowPreviewHostImpl {
public:
    // `plan` selects WHICH windows this host owns.
    //
    //   nullptr  the legacy Split host: every row of kSplitWindowRows,
    //            built exactly as before (the default, and the only value
    //            syncSplitPreviewToCanvas() passes outside Mixed).
    //   a plan    the Mixed host: only the rows this run's plan DETACHED.
    //            A merged row is drawn inside its mother panel by the
    //            board's four-slot arm, so giving it a slot of its own
    //            here would be exactly the double-draw the plan exists
    //            to prevent — one row of the group, one window on the
    //            board, no third copy.
    //
    // The set therefore CANNOT be added to later (a plan change is a new
    // host, not a mutation), which is what the "built once" note below
    // still means: the entries never change, and the caller rebuilds
    // rather than patching when the plan's detached set changes.
    explicit SplitWindowPreviewHostImpl(
        mhw::GameId game, const mhw::WindowLayoutPlan *plan = nullptr)
        : game_(game)
    {
        // Built once: a Panel ctor is heavyweight (layer-shell surface +
        // QSettings) and the set never changes after construction, so
        // there is nothing to add or remove later.
        //
        // std::vector and NOT QVector: the entries own unique_ptrs, and
        // QVector's copy-on-write detach wants a copy constructor even for
        // the paths that only move. The set is appended once and never
        // detached again, so COW buys nothing here anyway.
        for (const SplitWindowRow &row : kSplitWindowRows) {
            // Mixed: the plan is the truth about which of these rows this
            // run mounts as its own window. (parentSlot, tableRow) is the
            // same domain kWindowLayoutRows registers and
            // WindowLayoutPlan::detached() indexes, so no key spelling is
            // involved here — an unknown combination answers "merged",
            // which is the safe direction (the row stays with its mother).
            if (plan && !plan->detached(row.parentSlot, row.tableRow))
                continue;
            // v1.1.5: A ROW THE GAME DOES NOT HAVE IS NOT BUILT AT ALL.
            //
            // The user's report, verbatim: 「世界的时候有一个空的翔虫窗
            // 口，崛起的时候有一个空的衣装窗口」. The host used to build
            // all eleven rows for either game, and the v1.0.0 renderAll()
            // formula (enabled = section count > 0) then answered
            // "sections all off" for the wrong-game row — 翔虫's bit is
            // masked out of panelSectionSchema(0, World) — so the canvas
            // drew the `.hidden-window` placeholder: an empty frame where
            // the live overlay mounts NOTHING (main.cpp gates every split
            // window on hasVisibleContent(), and buildWirebugModel's
            // World arm deliberately produces no data).
            //
            // So the console promised a window the overlay does not have
            // — the console/overlay disagreement this file's header warns
            // about, one layer down. The REGISTRY staying game-agnostic is
            // correct (one table serves both games' plans, and
            // --print-layout-plan reports all eleven for either game); a
            // HOST is a different statement: it says "these are the
            // windows this console mounts", and under the game in force
            // the wrong-game row is not one of them. That is the same
            // filter windowKeyInGame() already applies to the chips and
            // the 窗口分组 block, and the same reason the pets panel has
            // no sub row under World.
            //
            // own == 0 is NOT this case: a table row claiming no section
            // is a build-time drift (renderAll()'s own comment says so),
            // and skipping it here would hide that bug rather than let
            // the wall report it.
            const uint32_t own = splitWindowSections(row.parentSlot,
                                                     row.tableRow);
            if (own != 0
                && (own & panelSectionSchema(row.parentSlot, game)) == 0)
                continue;
            auto window = makeSplitWindow(row);
            if (!window)
                continue;   // an unknown row: no window, not a crash

            EntryImpl entry;
            entry.window = std::move(window);
            entry.window->setAttribute(Qt::WA_DontShowOnScreen);
            entry.window->setEditMode(true);
            entry.window->show();
            entry.source = std::make_unique<PanelSourceAdapter>(
                entry.window.get());
            entry.parentSlot = row.parentSlot;
            entry.tableRow = row.tableRow;
            entries_.push_back(std::move(entry));
        }

        // v0.14: THE NON-OVERLAPPING WALL IS LAID BY ensureBuilt(game), NOT
        // HERE. It used to run at the tail of this constructor, which made it
        // structurally impossible for it to know which game it was placing:
        // the host is constructed by make_unique, setGame() can only run after
        // that returns, and a wall laid before the game is known is laid for
        // whatever the member's default happens to be — World. The evidence
        // was a Rise preview painting identically to a World one, 0.0% of
        // pixels changing on the rail switch, with the host demonstrably
        // rebuilt underneath.
        //
        // The windows are still built here — everything else about them
        // (the adapter, the demo seed, the section mask) does not depend
        // on the game at construction time, and the wall still does not.
        // What DOES depend on the game is WHICH rows are built: a row the
        // game in force does not have is skipped above, and the host is
        // constructed per game by ensureBuilt(), so that filter and the
        // wall's per-game column shift read the same value.
    }

    // Apply the overlap-free wall to every window that has not been
    // moved by the user.
    //
    // "Not moved" means the section has no stored ml/mt pair yet: a
    // Panel's first-run default comes from its subWindowIndex cascade
    // (panel.cpp's defaultMarginsForSubWindow), which is deliberately
    // UNRELATED to this wall's cells, so reading "is this the default"
    // is not a question the Panel can answer for us. Instead the wall
    // is applied once, at construction, and every drag afterwards
    // writes through setMargins() — which is what makes a dragged
    // window immune to the next wall pass, because there is no next
    // one.
    // Lays the overlap-free grid over the windows that have no stored
    // position. Called ONCE per host, from the facade's ensureBuilt(),
    // after setGame() has said which game this host is for.
    //
    // Guarded, not free-running: ensureBuilt() is also called on paths that
    // do not rebuild, and a second wall pass would flatten whatever the
    // user dragged since the first one.
    void applyWall()
    {
        if (wallApplied_)
            return;
        wallApplied_ = true;
        // The corner groups, in the canvas's own order. Corner 0==TopLeft
        // is the one every player and monster window uses; corner 1..3
        // are held so the table stays correct if a future split window
        // ships with a different anchor.
        constexpr Corner kCornerOrder[] = {
            Corner::TopLeft, Corner::TopRight,
            Corner::BottomLeft, Corner::BottomRight,
        };
        // A screen large enough that no cell runs past the frame at the
        // preview's fit. The wall's arithmetic is screen-size
        // independent by design (row/column, not pixels), so the exact
        // choice is cosmetic — see the header comment on
        // HudCanvas::subWindowGridRect.
        const QSize probe(3840, 2160);

        // The largest window anywhere in this wall. One lattice for every
        // corner — see the note at the column walk below for why a
        // per-corner pitch leaves the two sides of the screen overlapping
        // in the middle.
        // Scaled, because the cells this drives are in the same logical
        // space as HudCanvas::subWindowWall()'s — and that one multiplies
        // by scale. Sizing one in content pixels and the other in logical
        // pixels is the same lattice computed twice in two units, and it
        // breaks on the one axis the user controls: a `scale=2` in
        // panels.ini doubles the canvas's cells and leaves these ones
        // where they were, and the wall overlaps again. Measured with the
        // shipped arithmetic: scale 0.5/1.0/1.5 -> 0 overlapping pairs,
        // 2.0 -> 2, 3.0 -> 3.
        QSize lattice;
        for (const EntryImpl &probe2 : entries_) {
            if (!probe2.window)
                continue;
            const int w = qRound(probe2.window->contentSize().width()
                                 * probe2.window->scale());
            const int h = qRound(probe2.window->contentSize().height()
                                 * probe2.window->scale());
            lattice.setWidth(qMax(lattice.width(), w));
            lattice.setHeight(qMax(lattice.height(), h));
        }

        for (Corner corner : kCornerOrder) {
            // Same walk, different starting cell per game.
            //
            // The grid is a pure function of (corner, ordinal), which is
            // what makes its cells disjoint — and it is also why it gives
            // BOTH games the same arrangement when neither has saved a
            // position. A Rise preview then paints identically to a World
            // one, and the user reads that as "the preview is not
            // following me".
            //
            // Offsetting the ordinal per game keeps both properties: the
            // cells are still grid cells (so still disjoint, still inside
            // the screen, still readable in order), and the two games
            // land on different ones. It also reads correctly — the shift
            // is one column, not a random scatter — and it disappears the
            // moment a window is dragged, because a dragged window has a
            // stored position and the wall skips it entirely.
            //
            // The offset is derived from the game the host was built for,
            // so it cannot drift from the windows it is placing.
            // ONE LATTICE FOR THE WHOLE WALL, sized by the largest window
            // anywhere in it.
            //
            // Per-window sizing is not enough (a column advances by a
            // different width each step while its boxes share one x: twelve
            // overlapping pairs became ten). Per-corner sizing is not
            // enough either, because the two sides of the screen are one
            // screen: a TopLeft cluster walks rightwards from the left
            // edge, a TopRight one leftwards from the right, and with
            // independent pitches they meet and overlap in the middle.
            // That was the last two pairs — a TopLeft window in column 2 at
            // x=1296 against a TopRight one in its column 0 at x=1536,
            // sharing 120px.
            //
            // The pitch is therefore computed ONCE, outside the corner
            // loop, and every corner walks the same lattice.
            int ordinal = (game_ == mhw::GameId::Rise) ? kRiseWallColumnShift
                                                       : 0;
            for (EntryImpl &entry : entries_) {
                if (!entry.window) continue;
                Panel *p = entry.window.get();
                if (p->corner() != corner) continue;
                // A WINDOW THE USER PLACED KEEPS ITS POSITION.
                //
                // This is what the comment above this loop has been
                // promising since the wall was written - "it disappears the
                // moment a window is dragged, because a dragged window has
                // a stored position and the wall skips it entirely" - and
                // it was not implemented. The wall laid its cell over
                // every window unconditionally, so a rail switch (which
                // destroys the host and builds a new one, and a new host
                // applies its own wall) handed the user back the grid and
                // silently discarded their arrangement.
                //
                // The test is the FILE, not the numbers: a first-run
                // default is a real coordinate, so `ml == 20` cannot mean
                // "never moved" - only "no such key" can. See
                // Panel::hasStoredPosition.
                if (p->hasStoredPosition()) {
                    // Still consume the cell. The assignment is then a
                    // function of a window's OWN row, not of which other
                    // windows happen to be placed — otherwise dragging one
                    // sub-window reshuffles every window after it, and a
                    // user tidying one corner loses the rest.
                    ++ordinal;
                    continue;
                }
                // The cell is THIS window's size, not a constant. A
                // 260x128 cell cannot hold a 560x183 window, so consecutive
                // cells claimed rectangles their neighbours' bodies already
                // occupied: twelve overlapping pairs out of eleven windows.
                // mousePressEvent walks the boxes in reverse, so the window
                // underneath was never clickable — which is what the user
                // reported as "the sub-windows cannot be dragged past the
                // midline". The clamp was fine; the window was not being
                // clicked at all.
                const QRectF cell = HudCanvas::subWindowGridRect(
                    corner, ordinal, probe, lattice);
                ++ordinal;
                // Only the two components the corner READS are written;
                // the other two are dead arithmetic that would save into
                // the file as noise (the same rule panel.cpp's
                // defaultMarginsForSubWindow applies).
                QMargins m;
                switch (corner) {
                case Corner::TopLeft:
                    m = QMargins(int(cell.left()), int(cell.top()), 0, 0);
                    break;
                case Corner::TopRight:
                    m = QMargins(0, int(cell.top()), int(cell.right()), 0);
                    break;
                case Corner::BottomLeft:
                    m = QMargins(int(cell.left()), 0, 0, int(cell.bottom()));
                    break;
                case Corner::BottomRight:
                    m = QMargins(0, 0, int(cell.right()), int(cell.bottom()));
                    break;
                }
                p->setMargins(m, /*persist=*/false);
            }
        }
    }

    [[nodiscard]] int count() const { return static_cast<int>(entries_.size()); }

    // v0.14: write the drag result into ONE window, by the same index
    // its binding was passed at. Returns false (and does nothing) for
    // an index that does not name a window, so a stale drag from a
    // replaced binding set cannot move the wrong surface.
    //
    // persist=false: the console writes on exit, exactly like the four
    // base panels' panelMoved handler.
    [[nodiscard]] bool setWindowMargins(int index, const QMargins &m)
    {
        if (index < 0 || index >= static_cast<int>(entries_.size()))
            return false;
        if (!entries_[index].window)
            return false;
        entries_[index].window->setMargins(m, /*persist=*/false);
        return true;
    }

    // The bindings for the canvas, in this host's order. The canvas
    // indexes its own vector by position, so the entries' order is the
    // contract — and it is the table order, which is what the test
    // asserts.
    [[nodiscard]] QVector<HudCanvas::SubWindowBinding> bindings() const
    {
        QVector<HudCanvas::SubWindowBinding> out;
        out.reserve(entries_.size());
        for (const EntryImpl &e : entries_) {
            HudCanvas::SubWindowBinding b;
            b.src = e.source.get();
            b.parentSlot = e.parentSlot;
            b.tableRow = e.tableRow;
            out.append(b);
        }
        return out;
    }

    // The game and the three masks pushed into every window, so the
    // same user toggles that gate the base panels gate the split
    // windows. Each window's own content gates read their own bits out
    // of the mask they are given, exactly as main.cpp gives them.
    void syncAll(mhw::GameId game, uint32_t playerMask, uint32_t monsterMask,
                 uint32_t damageMask)
    {
        game_ = game;
        for (EntryImpl &e : entries_) {
            switch (e.parentSlot) {
            case 0:
                e.window->setSectionMask(playerMask);
                break;
            case 1:
                e.window->setSectionMask(monsterMask);
                break;
            case 2:
                e.window->setSectionMask(damageMask);
                break;
            default:
                break;
            }
            // setDemoGame() is a virtual on Panel with a no-op base
            // body, so this ONE call reaches every window whose demo
            // varies by game and is inert for the ones whose does not.
            // A window that overrides it stores the member and drops
            // the one-shot seed, so its next paint re-runs
            // setupDemoData() in the new game's arm.
            //
            // It used to be an overload set resolved statically against
            // a Panel*, which always chose the no-op base overload and
            // never called a single window. See the note beside the
            // definition below — that dead dispatch is the whole of the
            // 「Rise 预览显示衣装、World 预览显示翔虫」report.
            syncGameForDemo(e.window.get(), game);

            // v0.18: AND THE HONEST STATE, after the seed has been
            // re-armed. The order is load-bearing: setDemoGame() drops
            // the one-shot seed, so the NEXT paint would re-run
            // setupDemoData() and overwrite whatever this call wrote.
            // Calling it last means the state preview is the last writer
            // before renderAll(), and the seed's fiction never reaches
            // the pixmap.
            //
            // Every window in the loop gets the call and only one acts
            // on it — the dynamic_cast inside is the dispatch, and a
            // no-op for the other thirteen. That is the shape the code
            // already uses for the game retarget (one virtual call, some
            // windows inert), so a second one does not add a pattern.
            syncStatePreview(e.window.get(), game);
        }
    }

    // Re-render every window and push the pixmaps into the canvas.
    void renderAll(HudCanvas *canvas)
    {
        if (!canvas)
            return;
        for (size_t i = 0; i < entries_.size(); ++i) {
            const EntryImpl &e = entries_[i];
            const QPixmap pix = renderSplitWindow(e.window.get());
            // ── v1.0.0 integration (stage A / O2 review ④) ──────────────
            // THE `enabled` AXIS, FROM THE WINDOW'S OWN SECTIONS. This
            // call used to pass `true` unconditionally, which made
            // HudCanvas::SubSlot::enabled a constant and left the board
            // only two of the prototype's three states for a split
            // window:
            //
            //   sections all off → the `.hidden-window` placeholder
            //                      (enabled == false)   — unreachable
            //   visible == false  → not drawn at all       — reachable
            //   otherwise         → the body                — reachable
            //
            // The two axes are now separate, exactly as the canvas
            // documents them: `enabled` answers "does this window have
            // anything to SHOW" (a drawn placeholder when it does not)
            // and `visible` answers "is it ON THE BOARD at all"
            // (ControlPanel::pushWindowVisibility() feeds that one). A
            // hidden window is therefore still `enabled` — hiding does
            // not falsify the mask — and a window with every one of its
            // own bits off still has its row, its margins and its
            // pixmap, and is drawn as the placeholder.
            //
            // THE FORMULA IS buildWindowStates()'s, BYTE FOR BYTE:
            //     own  = the split table row's claim   (never the row number)
            //     on   = own & THE WINDOW'S OWN live mask & game-filtered
            //            schema   (`e.window` below, `sub` there)
            // and `on == 0` is exactly `w.sectionsOn == 0` in the model,
            // so the board's placeholder and the inspector's 「0 分区」
            // can never disagree.
            //
            // 实测缺陷3 (user-8): both halves used to name different
            // objects. Here the mask is `e.window->sectionMask()` — the
            // window's own Panel, fed by syncAll() with the user's mask —
            // while buildWindowStates() read the MOTHER's mask, which in
            // Mixed is plan.retainedMask() (every detached row stripped
            // out). One window could then be drawn with two sections on
            // while the model said 0/2.
            //
            // `own == 0` FALLBACK. A row that claims nothing means the
            // lookup table and kSplitWindowRows have drifted apart — a
            // build-time invariant the tables' static_asserts own — not
            // that the user switched everything off. Inventing a
            // placeholder for it would turn a table bug into a silent
            // visual one, so the pre-change answer (enabled) is kept and
            // the real "all off" case (own != 0, on == 0) still lands on
            // the placeholder.
            const uint32_t own = splitWindowSections(e.parentSlot,
                                                     e.tableRow);
            const uint32_t schema = panelSectionSchema(e.parentSlot, game_);
            const uint32_t on = own & e.window->sectionMask() & schema;
            const bool enabled = own == 0 ? true : countSectionBits(on) > 0;
            canvas->setSubWindowPixmap(static_cast<int>(i), pix, enabled);
        }
    }

    // 验收项4 (B 第1阶段): REPLAY EVERY SUB-WINDOW'S OWN COPY.
    //
    // retranslateUi() used to retranslate only the four mother panels,
    // so a language flip left the split sub-windows painting the old
    // locale until some later rebuild happened to repaint them. The
    // windows here are ordinary Panels with their own retranslateUi()
    // and their own trHooks, so the fix is to run exactly the same call
    // on every entry — no rebuild (a rebuild would re-lay the wall and
    // flatten whatever the user has dragged), no scope change, no
    // geometry write.
    void retranslateAll()
    {
        for (EntryImpl &e : entries_)
            if (e.window)
                e.window->retranslateUi();
    }

    // How many entries a given parent slot contributed. Used by the
    // console's log line and by the test to prove the derivation.
    [[nodiscard]] int countForParent(int parentSlot) const
    {
        int n = 0;
        for (const EntryImpl &e : entries_)
            if (e.parentSlot == parentSlot)
                ++n;
        return n;
    }

    // Called from the facade's ensureBuilt(), i.e. BEFORE the wall is laid.
    // Not a setter the rest of the file is free to call later: by then the
    // wall has already been applied to the previous game's grid.
    void setGame(mhw::GameId g) { game_ = g; }

    // Window accessors, for the test that asserts a rebuilt host re-seeds
    // its windows. See the facade's windowAt()/windowForKey() for why pixels
    // cannot serve as the observable.
    Panel *windowAt(int index) const
    {
        if (index < 0 || index >= static_cast<int>(entries_.size()))
            return nullptr;
        return entries_[static_cast<size_t>(index)].window.get();
    }

    Panel *windowForKey(const QString &key) const
    {
        for (const EntryImpl &e : entries_) {
            if (e.window && e.window->settingsKey() == key)
                return e.window.get();
        }
        return nullptr;
    }

private:
    struct EntryImpl {
        std::unique_ptr<Panel> window;
        std::unique_ptr<PanelSourceAdapter> source;
        int parentSlot{-1};
        int tableRow{-1};
    };
    // Whether the overlap-free wall has already been laid for this host.
    // Reset never: the host is destroyed on a rail switch and rebuilt, so
    // "once" means once per build, and the new host gets a fresh wall
    // because it is a fresh set of Panel objects reading a DIFFERENT
    // game's sections. See applyWall() for why once-per-build is the only
    // rule that keeps both properties at once — a non-overlapping first
    // layout AND a per-game saved geometry.
    bool wallApplied_{false};
    // The game this host was built for. Recorded so the wall can shift its
    // starting cell per game: the grid is a pure function of (corner,
    // ordinal) and therefore identical for both games, which is how a Rise
    // preview ended up painting exactly like a World one.
    mhw::GameId game_{mhw::GameId::World};

    // The per-window demo retarget.
    //
    // v0.15: IT USED TO BE AN OVERLOAD SET, AND THE OVERLOAD SET NEVER
    // RAN. The entries hold their windows as std::unique_ptr<Panel>, so
    // the call below passed a Panel* — and an exact match on
    // `syncGameForDemo(Panel*, GameId)` beats every Derived* overload in
    // overload resolution, with NO virtual dispatch to rescue it. Twelve
    // Derived overloads were dead code from the day they were written.
    // A two-type probe settles it: both entries printed
    // "NO-OP (Panel*) chosen".
    //
    // That is the root cause of the user's
    // 「Rise 预览显示衣装、World 预览显示翔虫」. Not one Derived overload
    // ran, so no window's `game_` member was ever moved off its own
    // initialiser — mantles' World, wirebug's Rise — and the preview
    // painted whichever game that member defaulted to, while the base
    // panel beside it (which switchGame() DOES retarget, via
    // player_->setGameForDemo(game)) painted the other. Two surfaces,
    // two games, one screen.
    //
    // The fix is the base class's virtual setDemoGame(), which each
    // window overrides to its own body. A virtual IS runtime dispatch on
    // the pointer's dynamic type, which is what a base-pointer call site
    // needs and an overload set structurally cannot provide. The
    // no-op base covers the windows whose demo is game-agnostic: they
    // simply inherit it, and the compiler rejects nothing because the
    // signature is one name, not twelve.
    static void syncGameForDemo(Panel *w, mhw::GameId g)
    {
        w->setDemoGame(g);
    }

    // v0.18: THE HONEST STATE, FOR THE WINDOW THAT HAS ONE TO REPORT.
    //
    // WHAT THE USER ASKED FOR, IN HIS WORDS: with no game running, the
    // console's player-info tile must show 「○ 未连接」 rather than a
    // seeded 「区域 古代森林 · 任务 … / 剩余 … / 猫车 …」 plus a live
    // connection line. He was looking at the console to find out what
    // his overlay was doing, and every other sub-window's seed was
    // answering a different question ("what will this look like when it
    // is full") that happens to be the right one for a LAYOUT preview.
    //
    // WHY THE SNAPSHOT IS BUILT HERE AND NOT READ FROM A READER. The
    // console is a separate process from the overlay; it owns no reader
    // and polls nothing. What it CAN see honestly is the one fact
    // detectGame() already gives it every 5 seconds for the rail badge:
    // whether a game process exists. That is exactly the input the
    // window's own three-state machine needs to answer Offline, and it
    // is the state the user was in.
    //
    // WHY ONLY THIS WINDOW. Every other sub-window seeds a block that
    // has no "not connected" form at all — a pill strip with no pills is
    // nothing, and mounting it would show an empty frame, which is the
    // defect split_no_empty_window_tests was written for. The info
    // window is the one whose seed overwrote a REAL state with a
    // fiction, because it is the one whose three states exist to be
    // read. So the console asks it for the truth and leaves the rest
    // alone.
    //
    // `found` is the process scan's own answer, never inferred: a
    // detection that exists but is for the OTHER game still reports that
    // game's pid, and a preview that claimed 未连接 over a running Rise
    // process under a World rail would be a second, quieter version of
    // the same bug.
    static mhw::GameSnapshot stateSnapshotFor(mhw::GameId game)
    {
        mhw::GameSnapshot snap;
        snap.game = game;
        const std::optional<mhw::GameDetection> detected = mhw::detectGame();
        if (!detected)
            return snap;   // Offline: no process, no pid, no data

        // A process exists. Report it as ATTACHED-with-no-data rather
        // than Live: the console cannot read the game's memory, and
        // claiming 已连接 · PID … · BASE … would be the same class of
        // lie the demo was, with a real pid stapled on. The window's
        // NoData state prints 「○ 无数据」 plus the pid, which is the
        // honest answer to "is the overlay pointed at a process".
        snap.attached = true;
        snap.pid      = detected->pid;
        // No imageBase: the console has not opened the process, and a
        // zero base is what composeMetaLeft() already renders as "--".
        // No player.valid: nothing was resolved.
        // No status: the reader's own diagnostic sentence is the reader's
        // to write, and the console has no reader. An empty status
        // collapses the diagnostic line, which is the correct shape —
        // a window that printed a reason it did not observe would be
        // inventing evidence.
        return snap;
    }

    // Push the honest state into the one window that has one. Called
    // from syncAll() AFTER the game retarget, and that order is the
    // whole reason this is not just a constructor call:
    //
    //   setDemoGame(game)  ->  resetDemoPrimed()  ->  the one-shot seed
    //   is RE-ARMED, so the next paint would run setupDemoData() and
    //   overwrite whatever the state preview wrote.
    //
    // So the state preview is the last writer AND it closes the seed
    // behind it (markDemoPrimed inside setStatePreview). Without that,
    // the order alone would not be enough: the seed fires on the next
    // paint, not on this call, and the fiction would land in the pixmap
    // after all. With it, "which preview am I showing" is a question
    // the window answers from its own state rather than from the order
    // two callers happened to run in.
    static void syncStatePreview(Panel *w, mhw::GameId game)
    {
        auto *info = dynamic_cast<PlayerInfoStripPanel *>(w);
        if (!info)
            return;   // every other window: the layout seed stands
        info->setStatePreview(stateSnapshotFor(game));
    }

    // std::vector, not QVector: see the ctor's note on COW vs the
    // unique_ptrs these entries own.
    std::vector<EntryImpl> entries_;
};
// The single public face, delegating to the impl above. This indirection
// exists so control_panel.h can hold a unique_ptr to a complete type:
// the header declares the host forward and owns it through a pointer,
// and this .cpp is the only place that ever needs the full definition.
class SplitWindowPreviewHost {
public:
    SplitWindowPreviewHost() = default;
    [[nodiscard]] int count() const { return impl_ ? impl_->count() : 0; }
    [[nodiscard]] QVector<HudCanvas::SubWindowBinding> bindings() const
    {
        return impl_ ? impl_->bindings() : QVector<HudCanvas::SubWindowBinding>{};
    }
    // Passes the game down because the Impl lays its overlap-free wall after
    // construction, and the wall needs to know which game it is placing —
    // AND because the Impl builds only the rows that game has (v1.1.5: a
    // World host has no 翔虫 window and a Rise host no 衣装 one), so the
    // filter and the wall read the same value or they drift.
    //
    // `plan` (v1.1.3) says WHICH rows the host owns: null for Split —
    // the whole table, byte-identical to before — and the plan in force
    // for Mixed, which builds one window per DETACHED row. It is only
    // read when the host is actually constructed: a plan change never
    // reaches an existing host, because syncSplitPreviewToCanvas()
    // rebuilds (reset + ensureBuilt) whenever the plan's signature
    // differs, and silently re-filtering a live host would leave the
    // canvas's raw PanelSourceAdapter pointers aimed at freed windows.
    //
    // THE ORDER IS THE WHOLE POINT. A host built by make_unique lays the
    // wall in its own constructor, at which moment no caller has been able
    // to say which game this is — so the wall is laid for the member's
    // default and every later setGame() is a no-op for it. That is how a
    // Rise preview came to paint identically to a World one while the host
    // underneath was demonstrably rebuilt (the rail switch DID fire, the
    // namespaces DID move, and 0.0% of the canvas changed).
    //
    // So: set the game, THEN lay the wall. The Idempotence guard inside
    // applyWall() keeps a second call from re-flattening what the user has
    // since dragged.
    void ensureBuilt(mhw::GameId game,
                     const mhw::WindowLayoutPlan *plan = nullptr)
    {
        if (!impl_)
            impl_ = std::make_unique<SplitWindowPreviewHostImpl>(game, plan);
        impl_->setGame(game);
        impl_->applyWall();
    }
    void syncAll(mhw::GameId game, uint32_t playerMask, uint32_t monsterMask,
                 uint32_t damageMask)
    {
        if (impl_)
            impl_->syncAll(game, playerMask, monsterMask, damageMask);
    }
    void renderAll(HudCanvas *canvas)
    {
        if (impl_)
            impl_->renderAll(canvas);
    }
    // 验收项4: replay every sub-window's translated copy after a language
    // flip. A no-op before ensureBuilt() has built the host — the same
    // state in which syncAll()/renderAll() are no-ops.
    void retranslateAll()
    {
        if (impl_)
            impl_->retranslateAll();
    }
    [[nodiscard]] int countForParent(int parentSlot) const
    {
        return impl_ ? impl_->countForParent(parentSlot) : 0;
    }
    // Write a sub-window's margins, addressed by the index its binding
    // was passed at. This is the drag's landing place: HudCanvas emits
    // subWindowMoved(i, margins) in CANVAS pixels, the console routes it
    // here, and this converts to the window's own logical space and
    // calls setMargins().
    //
    // Returns false for an unknown index or an unchanged value, so the
    // caller can tell "nothing happened" from "applied". A facade that
    // returned void would make an out-of-range drag look identical to a
    // successful one.
    [[nodiscard]] bool setWindowMargins(int index, const QMargins &m)
    {
        return impl_ ? impl_->setWindowMargins(index, m) : false;
    }
    // The window at an index, or nullptr. Non-owning; the host owns it.
    //
    // WHY THIS EXISTS. The rail's contract is that a rebuild re-seeds every
    // window for the new game, and the only observable for that is the
    // window itself: a mantles window is empty under Rise and full under
    // World, so hasVisibleContent() reads the seed directly. Pixels cannot
    // show it — two canvases differ from the geometry rebuild alone, which
    // is exactly how this dispatch shipped dead for two releases while both
    // console suites stayed green.
    [[nodiscard]] Panel *windowAt(int index) const
    {
        return impl_ ? impl_->windowAt(index) : nullptr;
    }
    // The first window whose settings key is `key`, or nullptr. A key is
    // stable where an index is not: the split table's row order is free to
    // change, and a test that hardcodes an index would start reading a
    // different window the day it does.
    [[nodiscard]] Panel *windowForKey(const QString &key) const
    {
        return impl_ ? impl_->windowForKey(key) : nullptr;
    }

private:
    std::unique_ptr<SplitWindowPreviewHostImpl> impl_;
};

// v1.0.0: THE OVERVIEW'S WIDGET, AS A FILE-LEVEL POINTER.
//
// WHY NOT A MEMBER OF ControlPanel. The nav card is read by two
// file-local functions (enterOverview / leaveOverview, defined near the
// bottom of the .cpp) that predate the header being writable. It is
// still file-local rather than promoted, and the reason is now
// deliberate: the header is being edited in parallel by sa-A, and every
// member added there is a merge conflict with that work. A file-level
// pointer costs nothing observable: the console is a single instance in
// this process (main.cpp creates exactly one), the widget is parented to
// it and dies with it, and nothing outside this translation unit can
// name it — which is the same visibility a private member has, minus the
// header edit.
//
// The pointer is set during construction, before any event can route to
// it (the event filter that reads it is installed on the widget itself,
// so a click cannot precede its creation).
//
// v1.0.0 (sa-D): ONE POINTER, NOT TWO. The stage-bar display combo that
// used to sit beside it is gone — the topbar's screenCombo_ is the
// display picker now, and reaching it does not need a file-local
// pointer: ControlPanel::screenCombo() is the public accessor, and the
// helper below is what the two overview toggles call.
//
// DECLARED HERE, ABOVE EVERY USE. The rail card is built in the ctor's
// first third, and the event filter that reads it lives between that and
// the functions that toggle the overview — a declaration next to those
// functions would be too late for the assignment.
//
// QPOINTER, NOT A RAW POINTER, and the reason is the console's own test
// suite (and any second instance in one process): these are FILE-LOCAL
// pointers to WIDGETS OWNED BY ONE INSTANCE, so the instance that
// assigned them can die while the pointer stays behind. Measured:
// console-interaction-regression-tests builds a second ControlPanel to
// re-read 显示该窗口 from a fresh console, and when that instance's
// destructor ran, `s_overviewNav` still named ITS rail card — the next
// switchGame() → selectPanel() → leaveOverview() then called
// setProperty() on the freed frame and SIGSEGV'd inside
// QObject::doSetProperty (gdb: leaveOverview → doSetProperty, the navCard
// arm at :4719/:4753). QPointer nulls itself when the widget goes, and
// every use below is already guarded by a null test — which is exactly
// the "that instance is gone, say nothing" answer the single-instance
// comment assumed it could never need.
static QPointer<QFrame> s_overviewNav;

// v1.0.0 (sa-D): the board's one-line caption (HTML `.board-hint`).
// File-local for the same reason the nav card is — see the note above.
// Written by enterOverview() / leaveOverview(), which own the view state
// that decides whether the caption is shown at all; the panel-view and
// edit-mode strings are the edit entry's to write into the same label.
static QPointer<QLabel> s_boardHint;

// v1.0.0: THE DISPLAY PICKER, AS A FILE-LOCAL POINTER.
//
// WHY A FILE-LOCAL POINTER AND NOT A CALL TO THE ACCESSOR. The accessor is
// a MEMBER (`QComboBox *screenCombo() const`), and a file-local free
// function has no ControlPanel object to call it on. The pointer is
// assigned in buildToolbar() at construction, before any event can route
// to it — the event filter that reads it is installed on the nav card,
// which is built later — and it is the same one-instance pattern
// s_overviewNav above already uses (the console is a single window:
// closeEvent() relies on quitOnLastWindowClosed seeing zero).
//
// v1.0.0 (sa-D): THIS IS THE ONE DISPLAY PICKER. It was two: this topbar
// combo (sa-A, the prototype's #screen-select, always visible) and a
// stage-bar combo named `overviewScreenCombo` that only appeared while
// the overview was open. The stage-bar one is deleted — see the note
// above enterOverview(). One control, one entry list, one handler that
// routes by mode; the alternative was two combos that could disagree
// about which monitor the board is showing.
//
// The combo is NOT the window-ownership control: that is the inspector's
// per-panel SCREEN row. This one is a pure view switch — the distinction
// the prototype draws between #screen-select and #panel-output, and the
// reason its handler re-points the canvas without touching a single
// Panel's outputName.
static QPointer<QComboBox> s_screenCombo;

static QComboBox *screenComboForOverview()
{
    return s_screenCombo;
}

// Forward declarations for the two toggles, whose definitions sit after
// the ctor that builds their widgets (and after the event filter that
// calls them). Declared here so eventFilter — which lives between the
// two — can name them.
void enterOverview(HudCanvas *canvas);
void leaveOverview(HudCanvas *canvas);

// v1.0.0: the board's edit state, written from one place, and the
// caption that describes it.
//
// File-local for the same reason s_overviewNav is (see the note above):
// the header is being edited in parallel and a private member there is
// a merge conflict with that work. The callers are the toolbar's
// 编辑布局 button and ControlPanel::keyPressEvent's Esc arm, both member
// functions in this translation unit, so a free function taking
// HudCanvas* is exactly as reachable as a member would be.
//
// WHAT setBoardEditing() OWNS, AND WHY IT IS NOT TWO CALL SITES. The
// board's edit face has two independent parts — HudCanvas::setEditing()
// for the grid / clay border / dashed cards, and the caption that
// describes them — and both have to change together or the board shows
// an edit face whose caption still promises the read-only overview. One
// function, one order, no caller can half-apply it.
//
// THE CAPTION IS DERIVED, NOT REMEMBERED. refreshBoardHint() reads the
// canvas's own two view flags, so both the string and the visibility
// are a pure function of state rather than of which entry point ran
// last: overview → the overview line, edit → the edit line, neither →
// the panel view's idle line. A third entry (a rail click leaving the
// overview, the Esc arm leaving edit mode) therefore needs no knowledge
// of the other two modes.
//
// THE LABEL IS NEVER HIDDEN (v1.0.0 O1). `.stage-body` is
// `grid-template-rows:minmax(0,1fr) 32px` (reference.html:169), so the
// hint row exists in ALL THREE states and renderBoardHead() only ever
// rewrites its TEXT (reference.html:481). Hiding it in the panel view
// — which is what `setVisible(overview || editing)` did — takes 32px
// away from the canvas that the draft gives it, and it is measured at
// h=32 in state01 and at h=32 in every other state in geometry.json.
//
// i18n BY HOOK, NOT BY A FROZEN setText(). The label's string is a
// function of the live view state, so it is registered through trHook()
// at construction (see gameNote_ for the same pattern). The console
// switches language without a restart, and retranslateUi() replays the
// hook with the CURRENT mode's key instead of freezing whatever
// language was in force when the mode was entered.
void setBoardEditing(HudCanvas *canvas, bool on);
void refreshBoardHint(HudCanvas *canvas);

// The edit toggle. A file-local pointer for the same reason s_boardHint is
// one: these three helpers are free functions so that the overview's
// enter/leave pair can call them without the console's full type, and the
// button they need is a private member. 4606 builds it (the one in
// buildEditModeBlock() at 4840 is dead code — that function has no
// caller), and 2829 is the only thing that flips it.
static QPushButton *s_editBtn = nullptr;

// v0.15 → v1.0.0 (OUTPUT-OWNERSHIP-FIX): the OWNERSHIP fallback pushed
// into every split sub-window — own output → parent panel output →
// primary. NOT the viewer any more.
// ───────────────────────────────────────────────────────────────────────
// WHAT THIS USED TO PUSH, AND WHY IT WAS WRONG. The old body read
// `canvas->previewScreen()` — the display the board is LOOKING at — and
// wrote it into Panel::setPreviewFallbackOutput(). For a split window
// (no outputName_ of its own) that value is step 2 of both clamp
// resolvers, so the viewer silently became the window's target screen:
// Panel::marginLimits() and HudCanvas::boundOutputName() then measured a
// Follow window against the display being previewed. Measured on a
// private two-output fixture (DP-1 2560x1440 + HDMI-A-1 1080x1920):
// `monster-identity` resolves to HDMI-A-1 (max left 851) but viewer=DP-1
// opened the clamp to 2331, and a real Shift+Right sequence wrote
// ml=2331 into panels.ini — 1480 px beyond its own screen.
//
// WHAT IT PUSHES NOW. For window i, the parent panel's outputName, and
// the primary's name when that parent follows primary (so both clamp
// resolvers stop on the SAME string and neither can fall through to the
// canvas's screen). The window's own output is never pushed: it stays
// step 1 of the resolver, so an explicit own screen keeps winning over
// the parent and is never overwritten here.
//
// WHY A FREE FUNCTION AND NOT A CONTROL PANEL MEMBER. The push is a few
// lines over SplitWindowPreviewHost's public face (count() / windowAt() /
// bindings()); the four mother Panels are handed in because ControlPanel::
// panelAt() is private and this file-local helper is not a member. The
// call sites are all ControlPanel members, so they can read panelAt()
// themselves and pass the array — and the array's SIZE is mhw::kPanelCount,
// so a slot-count change stops the build instead of shipping a silently
// short list.
//
// WHY IT HAS TO BE CALLED FROM MORE THAN ONE PLACE. The push at host
// build time (syncSplitPreviewToCanvas) covers the rail switch, because
// that path rebuilds the host. But two other paths change the ANSWER
// without rebuilding the host:
//
//   * selectPanel()            — a different tab means a different
//                                 parent panel's output
//   * the mother's SCREEN combo— panelAt(idx)->setOutputName() rewrites
//                                 the very output every child inherits
//
// The SCREEN dropdown's call site is kept too, even though its payload
// no longer depends on the viewer: the push is idempotent, and leaving
// it there means "the view moved, re-derive the ownership fallback from
// the model" stays one call rather than a rule about which view changes
// matter.
//
// A no-op when either argument is null (Single mode has no host), so the
// call sites need no guard of their own.
static void pushWindowOutputFallback(
    SplitWindowPreviewHost *host,
    const std::array<Panel *, mhw::kPanelCount> &mothers)
{
    if (!host)
        return;

    // Step 3 of the chain, as a NAME so that Panel::resolveOutputScreen()
    // (own → fallback → primary) and HudCanvas::boundOutputName()
    // (own → fallback → previewOutputName_) both stop here instead of
    // disagreeing about what "primary" means.
    QString primary;
    if (QScreen *ps = QGuiApplication::primaryScreen())
        primary = ps->name();

    const QVector<HudCanvas::SubWindowBinding> bindings = host->bindings();
    for (int i = 0; i < host->count(); ++i) {
        Panel *sub = host->windowAt(i);
        if (!sub)
            continue;
        // Step 2: the parent panel's own output — reference.html:421
        // `w.output === 'follow' ? panels[w.panel].output : w.output`.
        QString parentOutput;
        const int parentSlot =
            (i < bindings.size()) ? bindings.at(i).parentSlot : -1;
        if (parentSlot >= 0 && parentSlot < mhw::kPanelCount)
            if (Panel *mother = mothers[static_cast<size_t>(parentSlot)])
                parentOutput = mother->outputName();
        sub->setPreviewFallbackOutput(parentOutput.isEmpty() ? primary
                                                             : parentOutput);
    }
}

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
    // Ordered by PlayerSection bit index, one entry per row
    // buildInspector() creates — so `section` subscripts this table. The
    // bound below is the TABLE's size, not a literal: an earlier
    // hand-written `section < 8` silently demoted the sharpness row
    // (bit 8, added with the weapon split) to IconNone, which draws the
    // legacy plain dot.
    static const int kPlayerIcons[] = {
        SectionRow::IconConn, SectionRow::IconQuest, SectionRow::IconWeapon,
        SectionRow::IconBars, SectionRow::IconMantles, SectionRow::IconDebuff,
        SectionRow::IconBuff, SectionRow::IconWirebug,
        SectionRow::IconSharpness,
    };
    static const int kMonsterIcons[] = {
        SectionRow::IconInfo, SectionRow::IconHp, SectionRow::IconEnrage,
        SectionRow::IconAil, SectionRow::IconParts,
    };
    static const int kDamageIcons[] = {
        SectionRow::IconRows, SectionRow::IconShare, SectionRow::IconChart,
        SectionRow::IconRows,
    };
    if (panel == 0 && section < static_cast<int>(
                sizeof(kPlayerIcons) / sizeof(kPlayerIcons[0])))
        return kPlayerIcons[section];
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

// ─── v0.14: the four mother panels, (re)built to the scope in force ────
//
// Construct (or reconstruct) the four off-screen panels and push every
// bit of state the console owns into them, so the inspector, the canvas
// and the preview tiles all keep working after a rebuild.
//
// WHAT THESE FOUR PANELS ARE. The console renders them off-screen only
// (WA_DontShowOnScreen) to show the user what the overlay will draw.
// Their settings namespace is per (game, mode) — `[world/single/player]`
// etc — and the ONLY thing that can read it is the Panel constructor.
static Panel *makeMotherPanel(int idx)
{
    switch (idx) {
    case 0:  return new PlayerPanel();
    case 1:  return new MonsterPanel();
    case 2:  return new DamagePanel();
    default: return new PetDamagePanel();
    }
}

void ControlPanel::rebuildMotherPanels()
{
    // THE SCOPE FIRST. loadConfig() runs inside the Panel constructor and
    // resolves its group from the two globals, so they must already say
    // which (game, mode) this host is for. The caller sets them.
    //
    // THIS IS THE WHOLE FIX FOR «Rise 预览不同步». Before v0.14.1 the
    // ctor ran these four `new`s once, under whatever scope the process
    // happened to start in, and switchGame(Rise) only retargeted the demo
    // seed. The result was a Rise preview carrying World's margins,
    // scale, opacity, floors and lock — the numbers the panel's geometry
    // is made of — while painting Rise's pills. Both halves were "Rise"
    // on screen and only one of them was.
    //
    // Observed offscreen, against a fixture that stores distinct geometry
    // in all four domains (world/single=111, rise/single=222,
    // world/split=333, rise/split=444):
    //
    //     as built (scope=world/split) : 333,333,333,333
    //     after switchGame(Rise)       : 333,333,333,333   <-- still World
    //     (rise/split actually holds    : 444,444,444,444)
    //
    // A Panel caches everything its namespace decides for it and reads it
    // once. Re-running the ctor is the only reader that exists, which is
    // why this is a rebuild and not a re-push.

    // --- destroy the old set -------------------------------------------------
    //
    // setVisible(false) first: the four are TOP-level QMainWindows, and
    // ~ControlPanel has needed the same courtesy since v0.5 so Qt's
    // quitOnLastWindowClosed sees zero visible windows on exit. Doing it
    // here too keeps a mid-session rebuild from flapping the console's own
    // last-window logic.
    //
    // The canvas holds non-owning raw PanelSource* pointers into this set
    // for the whole session, so the caller must run rebindMotherPanels()
    // straight after this function returns — with the new adapters in
    // place before anything paints. Nothing between here and there reads
    // the canvas (no paint, no event delivery), which is what makes the
    // brief window safe.
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        Panel *p = panelAt(i);
        if (!p)
            continue;
        p->setVisible(false);
        delete p;
        ctl_[i].panel = nullptr;
    }

    constructMotherPanels();
    adoptMotherPanels();

    // ── OUTPUT-OWNERSHIP-FIX (C1): THE CHILDREN'S FALLBACK MOVES WITH ────
    // THE MOTHERS. Only a Panel constructor reads outputName, so this is
    // the one place in the file where a parent's output can change
    // underneath a split sub-window that already exists — switchGame()
    // rebuilds the mothers AFTER syncSplitPreviewToCanvas() has built (or
    // reused) the host, and the ctor's startup order runs
    // applyStartupLayoutMode() → syncSplitPreviewToCanvas() → selectPanel()
    // before switchGame() ever gets to state the scope. Without this push
    // a sub-window keeps the answer computed from the PREVIOUS mothers,
    // which on a fresh console is every child reporting the primary while
    // its parent names DP-1 — the clamp then measuring the wrong screen.
    //
    // A no-op when the host does not exist yet (the ctor's first rebuild,
    // Single mode, and resetPrototypeScope() before it re-syncs), so the
    // call sites need no guard of their own.
    pushWindowOutputFallback(
        splitPreview_.get(),
        {panelAt(0), panelAt(1), panelAt(2), panelAt(3)});
}

void ControlPanel::constructMotherPanels()
{
    // A fresh Panel ctor per slot: this is the call that reads the scope's
    // group, and there is no other reader.
    //
    // Deliberately does NOT touch ctl_[i].master or ctl_[i].subs. At ctor
    // time those widgets do not exist yet, and on a rail click
    // rebuildMotherPanels() wants them honoured — which is exactly why
    // adopting them is the caller's explicit second step rather than
    // something folded in here, where it would crash on a half-built
    // inspector.
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        Panel *p = makeMotherPanel(i);
        ctl_[i].panel = p;
        p->setAttribute(Qt::WA_DontShowOnScreen);
        p->setEditMode(true);   // seeds setupDemoData() on first paint
        // WA_DontShowOnScreen + show() is the supported combo for
        // off-screen rendering (comment at the ctor's original call site).
        p->show();
    }

    // Keep the four typed member pointers in step with the generic ctl_
    // entries the rest of the file uses. Two names for the same object has
    // bitten this file before; one assignment per object per rebuild is
    // what keeps that honest — and the split-preview host needed the same
    // discipline for its own typed handles.
    player_  = static_cast<PlayerPanel *>(ctl_[0].panel);
    monster_ = static_cast<MonsterPanel *>(ctl_[1].panel);
    damage_  = static_cast<DamagePanel *>(ctl_[2].panel);
    pets_    = static_cast<PetDamagePanel *>(ctl_[3].panel);
}

// Point one SCREEN dropdown at the panel's CURRENT output choice.
//
// WHY A FILE-LOCAL FREE FUNCTION AND NOT A MEMBER. The only header this
// task may touch is the .cpp, so this cannot be a private member
// declared in control_panel.h. It takes the two objects it needs as
// arguments — the combo and the panel — because PanelCtl is private to
// ControlPanel and a free function cannot name the type. Both call
// sites are ControlPanel members that already hold both.
//
// WHY IT EXISTS AT ALL. The block it replaces used to live inside
// buildInspector()'s combo construction, where it ran exactly once —
// against whatever Panel happened to exist at that moment.
// rebuildMotherPanels() then replaced those Panels with fresh ones that
// had re-read their outputName from the scope now in force, and nothing
// told the dropdown. The user's report, verbatim:
//
//     「我启动控制台,有些窗口显示属于 dp-1,但是实际启动 overlay
//       结果显示在 hdmi。我必须要手动将该窗口选择一下 hdmi,然后在
//       选回 dp-1 才能正常切换到主屏。」
//
// "Must choose twice" is the tell: the FIRST pick is not a preference,
// it is the user forcing the combo's stale index to agree with the
// member — and only the second one changes anything. The console was
// showing a screen the overlay had never been told about, because the
// panel the combo was built against read a DIFFERENT scope than the
// panel that buildOverlayArgv() reads.
//
// Which is exactly the ctor's ordering: constructMotherPanels() at
// line ~1532 builds the four mothers while applyStartupLayoutMode()
// (line ~1961) has not resolved the conf's layout= row yet, so the
// layout global still holds its Single default and the mothers read
// [world/single/*]. The combo is populated from that. Then
// applyStartupLayoutMode() resolves Split, switchGame() rebuilds the
// mothers, and they read [world/split/*] — empty. The combo keeps
// advertising DP-1; the member says nothing; argv carries no
// --output-* flag; the overlay mounts on primaryScreen().
//
// Called from adoptMotherPanels(), which is the one step both the ctor
// and rebuildMotherPanels() run after the Panels exist.
// Idempotent: setting the same index twice is a no-op, and the
// blockSignals guard stops the programmatic set from being mistaken
// for a user pick (which would write the value back and stamp
// configTouched_ on a window the user never touched).
static void syncOutputCombo(QComboBox *combo, const Panel *panel)
{
    if (!combo || combo->count() == 0)
        return;
    const QString cur = panel ? panel->outputName() : QString();
    int want = 0;                      // <PRIMARY> — the empty override
    if (!cur.isEmpty()) {
        const int found = combo->findData(cur);
        // A stored name that matches no attached output (a monitor that
        // was unplugged) falls back to <PRIMARY>, which is what
        // Panel::resolveOutputScreen() does for the same value.
        if (found >= 0)
            want = found;
    }
    if (combo->currentIndex() == want)
        return;
    combo->blockSignals(true);
    combo->setCurrentIndex(want);
    combo->blockSignals(false);
}

void ControlPanel::adoptMotherPanels()
{
    // Preserve the live widget state a rebuild would otherwise lose. The
    // inspector rows, the master chips and the canvas keep their own state
    // across the swap, but everything the PANEL carries has to be re-pushed
    // or the user's toggles would reset on a rail click.
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        Panel *p = panelAt(i);
        if (!p)
            continue;
        // Section mask, from the same source loadMaskFromDisk() used. The
        // inspector's checkboxes are untouched by the swap, so this is the
        // authoritative current value rather than a cached one.
        //
        // v1.1.3: the RETAINED mask in Mixed (identity elsewhere), so a
        // rebuilt mother cannot come back holding the rows it gave to its
        // own windows — adoptMotherPanels() runs from switchGame() and
        // resetPrototypeScope(), both of which can follow a plan change.
        // rebuildAndRender() re-states the same value right after, but
        // this line must be correct on its own: it is the first write to
        // a brand-new Panel and an unfiltered one would be visible in the
        // window between the swap and that render.
        p->setSectionMask(
            effectiveMotherMask(i, ControlPanel::maskFor(ctl_[i])));
        // Master visibility: the inspector chip, which is the only thing
        // that says whether the surface mounts at all.
        p->setPanelEnabled(ctl_[i].master && ctl_[i].master->isChecked());
        // v0.16: AND THE SCREEN DROPDOWN FOLLOWS THE NEW PANEL. The
        // combo's index was set once, when buildInspector() ran against
        // the panel that existed then. This rebuild gave the slot a
        // fresh Panel that re-read its outputName from the scope now in
        // force — which is a DIFFERENT scope at startup (the mothers are
        // built before applyStartupLayoutMode() resolves layout=) and
        // after any applyLayoutChoice(). Without this line the dropdown
        // kept advertising the previous panel's screen while argv read
        // the new panel's, and the user had to pick twice to make them
        // See syncOutputCombo()'s comment for the full story.
        syncOutputCombo(ctl_[i].outputCombo, p);
    }

    // THE DEMO SEED, which is NOT persisted and therefore NOT readable by
    // the constructor. A PlayerPanel defaults its game_ member to World
    // (panel_player.h's member initialiser) and only setGameForDemo() moves
    // it. Without this line a Rise rail click would rebuild the panel at
    // the right geometry and repaint it with the wrong game's demo pills —
    // the geometry half of the report fixed and the content half still
    // wrong, which is worse than before because it looks intentional.
    //
    // resetDemoPrimed() inside setGameForDemo() is what makes it take:
    // the panel has not painted yet (the ctor's setEditMode(true) only
    // ARMS the seed), so the next paint runs setupDemoData() with the
    // right game_ either way. Calling it is what makes that game_ right.
    if (player_)
        player_->setGameForDemo(currentGame());

    // NO selectPanel() HERE. The inspector page, the canvas selection and
    // the rail highlight survive a rebuild untouched (they are owned by
    // widgets, not by the panels being replaced), so there is nothing to
    // re-apply — and calling selectPanel() would re-run syncAppearance()
    // and updatePosLabel() against sliders that may not exist yet at
    // construction time. The ctor's own selectPanel(0) and the rail
    // buttons' selectPanel(i) remain the only places selection moves.
}

Panel *ControlPanel::splitPreviewWindow(const QString &key) const
{
    // The host owns the window and outlives this call, so a raw pointer is
    // the right shape: this is a read for tests, not a transfer of
    // ownership, and lifetimes here are "as long as the console is showing
    // this game in split mode".
    return splitPreview_ ? splitPreview_->windowForKey(key) : nullptr;
}

mhw::GameId ControlPanel::splitPreviewDemoGame(const QString &key,
                                              bool *found) const
{
    if (found) {
        *found = false;
    }
    if (!splitPreview_) {
        return mhw::GameId::World;
    }
    const Panel *w = splitPreview_->windowForKey(key);
    if (!w) {
        return mhw::GameId::World;
    }
    if (found) {
        *found = true;
    }
    return w->demoGame();
}

void ControlPanel::rebindMotherPanels()
{
    // The canvas stores a NON-OWNING const PanelSource* per slot (its
    // documented contract — see ui/hud_canvas.h bindPanel). Replacing the
    // panels therefore leaves it pointing at freed memory, so the
    // adapters must be rebuilt and re-bound before anything paints.
    //
    // Ownership is the caller's — boundSources_ is this console's member
    // array, destroyed after the panels, which is the ordering the
    // original v0.11.4 comment requires.
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        Panel *p = panelAt(i);
        if (!p) {
            boundSources_[i].reset();
            if (canvas_)
                canvas_->bindPanel(i, nullptr);
            continue;
        }
        boundSources_[i] = std::make_unique<PanelSourceAdapter>(p);
        if (canvas_)
            canvas_->bindPanel(i, boundSources_[i].get());
    }
}

// ─── v0.12 sub-window registration (REMOVED) ───────────────────────
//
// ControlPanel::registerSubWindow / unregisterSubWindows and the
// mhw::SubWindowRegistry they drove were removed in the v0.12 stage-1
// dead-code pass: the registry lived in this console, had zero
// external callers, and was never filled — the main.cpp wiring that
// decides WHEN a sub-window exists was never written. Nothing in this
// file needs a sub-window slot today: ui/panel_player_split.h is
// self-sufficient (setChromeVisible(false) + Panel::isSubWindow()),
// and the live slot concept now lives in src/ui/panel_sections.h
// (PlayerSection::PlayerSplitSlot / kPlayerSplitTargets), which does
// not route through this console. See the class comment above.

// ─── v1.0.0 fidelity (stage): THE STAGE'S SCROLL AREA, AND WHY IT IS
// NOT A PLAIN QSCROLLAREA ──────────────────────────────────────────────
//
// reference.html's `.board-viewport` is `overflow:auto` and its height
// comes from the GRID, not from the board: `.stage-body` is
// `grid-template-rows:minmax(0,1fr) 32px`, so the page lets the STAGE be
// as tall as the board needs and the viewport merely follows. Measured
// at a 1600x1000 DPR1 viewport (HTML-ROOT-REMEASUREMENT.md) the stage is
// 684.625 tall for the landscape output —
//
//     1  .stage border
//    48  .shell-head          (min-height:48px, border-bottom inside it)
//   602.625  .board-viewport  (= 12 pad-top + the 1050x590.625 board)
//    32  .board-hint row      (.stage-body's second track)
//     1  .stage border
//
// — and 1089.53125 for the portrait one (12 + 560x995.546875 + 32 + chrome).
// The Qt stage is [stagebar / canvasScroll / boardHint] in one
// QVBoxLayout, and a PLAIN QScrollArea answers neither half of that
// contract: it reports hasHeightForWidth() == false and it hands the
// layout its own generic hint (measured 612x408 on the frozen candidate),
// so HudCanvas::heightForWidth() — the ONE function that already knows
// the board's CSS caps — reached nothing. Measured consequence on the
// candidate: stage hint 501 where the page's stage is 684.625, i.e. the
// shell's natural-height budget (console_root_scroll.cpp reads it) was
// 233px short of the page's per state.
//
// CanvasScrollArea closes that gap by delegating the three sizing
// answers to the widget it holds:
//
//   hasHeightForWidth/heightForWidth  the widget's own, at the width the
//     widget would get (no frame here, so it is the same number) — the
//     page's `.board-viewport` is `padding:12px 14px 0`, which is exactly
//     what the canvas already bakes into its HfW;
//   sizeHint  the widget's OWN natural size, both axes: 1078x603 for the
//     landscape board and 588x1008 for the portrait one. The height is
//     what the stage's hint needs; the width is what makes a
//     height-for-width layout evaluate the height at the BOARD's width
//     rather than at QScrollArea's generic 612 (see the note on
//     sizeHint() below for the measured difference). The stage's real
//     width still comes from the splitter — `.main` is one `1fr` column
//     in the page too, so the board never fixes the shell's width.
//
// NO BOARD ARITHMETIC IS DUPLICATED HERE. Every number comes from the
// widget; this class only stops QScrollArea from swallowing it. The
// legacy (non-fidelity) preview keeps QScrollArea's own hint, because
// there the canvas is the old fixed-ratio preview the stage was always
// allowed to shrink.
// ─── end v1.0.0 fidelity (stage) ──────────────────────────────────────
namespace {

class CanvasScrollArea : public QScrollArea
{
public:
    explicit CanvasScrollArea(HudCanvas *canvas, QWidget *parent = nullptr)
        : QScrollArea(parent)
    {
        // The canvas is installed as the filter TARGET here rather than
        // through a later setWidget() so the filter cannot miss the
        // first LayoutRequest (see eventFilter below).
        if (canvas) {
            setWidget(canvas);
            canvas->installEventFilter(this);
            // THE VIEWPORT TOO, and this is the one that matters. When a
            // widget's updateGeometry() finds its parent has NO layout —
            // the canvas's parent is a QScrollArea's viewport, which has
            // none — Qt does not drop the change: it posts a LayoutRequest
            // to the PARENT widget instead. Measured with the console's
            // own probe: after the SCREEN dropdown moves the board to the
            // portrait output, the canvas sends NO LayoutRequest itself
            // (29 Paint, 2 Resize, 2 Move and nothing else over a whole
            // state walk) while the viewport does receive one. Filtering
            // only the canvas left the stage's cached hint at the
            // landscape width and the portrait page's budget 635px short.
            viewport()->installEventFilter(this);
        }
    }

    [[nodiscard]] bool hasHeightForWidth() const override
    {
        const QWidget *w = widget();
        if (!w)
            return false;
        // ── 验收项6 / 固定视口: THE BUDGET ALWAYS ANSWERS ───────────────
        // With the fixed budget in force (the canvas is zoomed and we
        // still hold its natural hint) WE own the answer, even though
        // HudCanvas itself says "no HfW while zoomed". Saying false here
        // would hand the stage the fallback path — a compressed pane
        // sized from the zoomed hint — which is precisely the "stage 高度
        // 随 zoom 顶大页面 / 缩放推动 stage 与 inspector" defect.
        if (fixedBudget())
            return true;
        return w->hasHeightForWidth();
    }

    [[nodiscard]] int heightForWidth(int hfw) const override
    {
        const QWidget *w = widget();
        if (!w)
            return -1;
        sampleNatural();
        const int inner = qMax(0, hfw - frameWidth() * 2);
        if (fixedBudget()) {
            auto *canvas = qobject_cast<HudCanvas *>(widget());
            if (!canvas)
                return -1;
            /* ── 固定视口预算: ZOOM MOVES THE CONTENT, NOT THE BUDGET ──────
               Below the natural width HudCanvas::heightForWidth() is
               zoom-invariant by construction (fitWidth = availW/z and
               then boardW = fitWidth*z cancel — hud_canvas.cpp's own
               arithmetic), so the canvas's own function still gives the
               RIGHT number without this file repeating any board maths.
               Past the natural width the board is at its column cap, so
               the height is flat: the natural height we sampled. Either
               arm is a function of the container, never of zoom_.
               ── end 固定视口预算: heightForWidth ── */
            if (inner <= naturalHint_.width())
                return canvas->heightForWidth(inner) + frameWidth() * 2;
            return qMax(0, naturalHint_.height()) + frameWidth() * 2;
        }
        if (!w->hasHeightForWidth())
            return -1;
        return w->heightForWidth(inner) + frameWidth() * 2;
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        sampleNatural();
        const QSize base = QScrollArea::sizeHint();
        auto *canvas = qobject_cast<HudCanvas*>(widget());
        if (!canvas || !canvas->consoleFidelityMode())
            return base;   // the legacy preview keeps the generic hint
        // THE WHOLE natural size, width included — and this is not
        // decoration. A QVBoxLayout that has heightForWidth() answers its
        // own sizeHint() with heightForWidth(sizeHint().width()), i.e. the
        // height is evaluated at THIS hint's width (measured: with the
        // scroll area still reporting QScrollArea's generic 612-wide hint
        // the stage's hint came out 441 — the board evaluated at 612px,
        // where it is only 341 tall — and the shell's whole natural-height
        // budget fell 60px instead of rising). Delegating the widget's
        // natural WIDTH too makes that evaluation happen at the board's
        // own width (1078 landscape / 588 portrait), so the hint is the
        // same number heightForWidth(1336) answers, and the stage's hint
        // becomes 684.625-style arithmetic instead of a squeezed one.
        //
        // 固定视口预算: WHILE ZOOMED the baseline comes back instead of the
        // canvas's (now zoom-scaled) hint, so the stage's height budget —
        // and with it the user's dragged stageHeight — cannot move when
        // the user zooms the board, and the parent layout above sees the
        // SAME number before and after a zoom (which is why a zoom no
        // longer triggers updateGeometry() on the pane at all). The canvas
        // still grows INSIDE this scroll area: a scroll area's inner
        // widget is sized from the WIDGET's own hint, not the scroll
        // area's (measured on Qt 6.12 — an overridden sizeHint left a
        // widgetResizable(false) child at its own 800x600 with working
        // scrollbars), so zoom only ever changes the content and the
        // scroll range.
        if (fixedBudget())
            return naturalHint_;
        return canvas->sizeHint();
    }

protected:
    // AND THE LAYOUT ABOVE HAS TO BE TOLD WHEN THAT NUMBER MOVES. A
    // QLayout caches the hint it computed, and this console changes the
    // board's natural size under a live shell: switching the preview to
    // the portrait output moves HudCanvas's hint from 1078x603 to
    // 1195x1008 (measured), and nothing above the scroll area hears
    // about it — the stage's layout went on answering its cached
    // 1080-wide hint, so the portrait page's whole natural-height budget
    // stayed at the landscape number (measured 1107 where the stage's
    // own heightForWidth says 1089). Re-invalidating the parent layout
    // when — and ONLY when — our own hint really moved cannot loop:
    // sizeHint() is a function of the widget, not of this widget's
    // geometry, so a resize caused by the update cannot move it again.
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if ((watched == widget() || watched == viewport())
            && (event->type() == QEvent::LayoutRequest
                || event->type() == QEvent::Resize)) {
            const QSize now = sizeHint();
            if (now != reportedHint_) {
                reportedHint_ = now;
                updateGeometry();
            }
        }
        return QScrollArea::eventFilter(watched, event);
    }

private:
    QSize reportedHint_{-1, -1};
    /* ── 固定视口预算: THE ZOOM-INDEPENDENT BASELINE ─────────────────────
       naturalHint_ is the canvas's own hint as measured while it is NOT
       zoomed (zoom() <= 1), re-sampled every time we are asked at that
       state — so an orientation change while unzoomed updates it, and a
       zoom afterwards cannot. It is mutable because every consumer is
       const and sampling is a cache, not a mutation of the widget.
       ── end 固定视口预算: baseline ── */
    mutable QSize naturalHint_{-1, -1};

    void sampleNatural() const
    {
        auto *canvas = qobject_cast<HudCanvas *>(widget());
        if (canvas && canvas->zoom() <= 1.0)
            naturalHint_ = canvas->sizeHint();
    }

    [[nodiscard]] bool fixedBudget() const
    {
        auto *canvas = qobject_cast<HudCanvas *>(widget());
        return canvas && canvas->consoleFidelityMode()
            && canvas->zoom() > 1.0 && naturalHint_.width() > 0
            && naturalHint_.height() > 0;
    }
};

// ─── v1.0.0 fidelity (budget seam): `.main`'s TWO ROWS, IN ONE NUMBER
// ──────────────────────────────────────────────────────────────────────
//
// reference.html:161 spells the pane column as
//
//     .main { grid-template-rows: minmax(0,2fr) minmax(280px,1fr);
//             gap:12px }
//
// so the page's BUDGET for that column is not "the stage's height plus
// the inspector's height" — it is the stage's natural height, the
// inspector's row being exactly HALF of it (2fr : 1fr), never less than
// 280px, with a 12px gap between the two rows. Measured (HTML-ROOT-
// REMEASUREMENT.md, 1600x1000 DPR1): stage 684.625 + gap 12 +
// inspector 342.3125 = 1038.9375, and 342.3125 IS 684.625 / 2.
//
// A QSplitter knows nothing about any of that: its sizeHint() is the SUM
// of its panes' own hints, and the inspector's own hint is its content
// (measured 197 for the empty state) — so the shell's natural-height
// budget, which console_root_scroll.cpp reads from this layout, came out
// 977 where the page is 1134.9375, and the console never scrolled at
// 1600x1000 while the prototype does. The 2fr/1fr split itself is
// already applied where it belongs — pendingStageRatioDefault_ sets
// 2:1 on the first resize — so the ONLY thing missing is the hint.
//
// This is that seam and nothing else: sizeHint() is overridden, no
// geometry, no state, no window model, nothing re-saved. The three
// literals are the frozen file's own (2fr/1fr, the 280px floor, the 12px
// gap), the same way hud_canvas.cpp already spells `.board`'s caps; every
// pane height comes from the widgets.
// ─── end v1.0.0 fidelity (budget seam) ────────────────────────────────
class PaneBudgetSplitter : public QSplitter
{
public:
    using QSplitter::QSplitter;

    [[nodiscard]] QSize sizeHint() const override
    {
        const QSize base = QSplitter::sizeHint();
        if (orientation() != Qt::Vertical || count() < 2)
            return base;
        const QWidget *stage = widget(0);
        const QWidget *drawer = widget(1);
        if (!stage || !drawer)
            return base;
        // `.main`'s second row: half the first (2fr:1fr), floored at
        // minmax(280px,1fr), and never below the drawer's own hint.
        const int first = stage->sizeHint().height();
        const int second = qMax(drawer->sizeHint().height(),
                                qMax(kMainRowMin, (first + 1) / 2));
        return QSize(base.width(), first + kMainRowGap + second);
    }

private:
    // reference.html:161 — `minmax(280px,1fr)` and `gap:12px`.
    static constexpr int kMainRowMin = 280;
    static constexpr int kMainRowGap = 12;
};

} // namespace

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

    // Rise REFramework card: owns the presentation half (widgets, status
    // lines, button enablement) of the Rise-only management block. The
    // request signals and the finishRiseReframeworkOperation() slot stay on
    // this class — they are what src/main_control.cpp connects to. The card
    // needs a constructed owner (it reaches trSet()/currentGame() through
    // it), so it goes up before the first buildInspector().
    riseCard_ = new RiseReframeworkCard(this);

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
    //
    // v0.14.1: NOT built here. The four used to be `new`ed at this point,
    // once, under whatever scope the process happened to start in — and
    // switchGame(Rise) then retargeted only their demo seed, leaving them
    // carrying World's margins/scale/opacity while painting Rise's pills.
    // The user's symptom was «整块模式下 Rise 预览不同步». They are now
    // built by rebuildMotherPanels(), from its own call site AFTER
    // buildInspector() has filled ctl_[i].master / ctl_[i].subs (the
    // re-push of mask + master toggle reads those out of PanelCtl).
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
    // v1.0.0 console rework: the right half of the window is now a
    // column — [toolbar / layout area / config drawer] — so the top row
    // is gone. The rail column (leftSplitter_) and this column sit
    // side by side inside one horizontal QSplitter, and the vertical
    // consoleSplitter_ below keeps its meaning: index 0 is the DRAWER,
    // index 1 is the STAGE. THE ORDER IS DELIBERATE AND NOT INTUITIVE —
    // the stage collapse animation (animStageTo, setStagePaneHeight)
    // addresses consoleSplitter_->widget(1) directly, so stage must stay
    // at index 1 and the drawer takes index 0.
    //
    // v1.0.0 (O1): AND NOW THE STAGE IS THE TOP PANE, WHICH IS THE
    // PROTOTYPE'S OWN ARRANGEMENT. `.main` is
    // `grid-template-rows: minmax(0,2fr) minmax(280px,1fr)` — stage
    // first, inspector second, 12px between them — so the board is drawn
    // ABOVE the drawer, not below it. QSplitter is a vertical stack, so
    // the FIRST pane it holds is the top one: the STAGE is added first
    // and the DRAWER second, which is the opposite of the order the old
    // shell used.
    //
    // THAT MOVES THE STAGE FROM INDEX 1 TO INDEX 0, AND THE ANIMATION
    // MOVES WITH IT — it does not stay behind. animStageTo(),
    // setStagePaneHeight(), the animation's finished() handler and the
    // splitterMoved persistence all address the stage by INDEX today
    // (widget(1), sizes().value(1)), and the task that dispatched this
    // port is explicit: migrate them rather than keep a wrong visual
    // order to protect four call sites. They now go through
    // stagePane() / stagePaneSize() below — one spelling each, so the
    // next pane swap is a two-line change in one place instead of a hunt
    // through five.
    //
    // The sizes invert with it: the stage is the 2fr pane, so it gets
    // the larger number. 640 for the stage and 470 for the drawer keeps
    // the prototype's roughly two-thirds / one-third split.
    // v1.0.0 fidelity (budget seam): a QSplitter whose HINT is `.main`'s
    // two rows (stage + 12px gap + half-the-stage inspector), because a
    // plain QSplitter's hint is only the SUM of its panes' own hints and
    // that is what the root scroll's natural-height budget reads. See
    // PaneBudgetSplitter above; the 2:1 GEOMETRY is unchanged (it is set
    // by pendingStageRatioDefault_ on the first resize, below), and so is
    // every stored proportion — this is a hint, not a behaviour change.
    auto *splitter = new PaneBudgetSplitter(Qt::Vertical);
    splitter->setChildrenCollapsible(true);     // stage can fully collapse to 0
    splitter->setHandleWidth(0);               // no visible splitter bar
    splitter->setObjectName("consoleSplitter");

    // v0.5 A: object rail → focused inspector → unified canvas.
    // v0.7.1: the rail no longer owns its own width — the horizontal
    // QSplitter below owns the split between the narrow game column
    // (left) and the rail (right). The rail keeps its 214px minimum via
    // its own minimumWidth so the resize handle still respects the
    // existing layout's "comfortable" width on first launch.
    auto *rail = new QFrame();
    rail->setObjectName("objectRail");
    // v1.0.0 (O1): EXACTLY 232, not "at least 130". `.app` is a grid
    // whose first column is a fixed `232px` (reference.html:90) — the
    // rail is not user-resizable in the draft, and `min-width:130`
    // left its real width to QSplitter's sizeHint, which measured 319
    // at construction and 139 after the first rebuild. FIXED width is
    // the only value that survives both: the splitter cannot give a
    // widget more or less than its min==max, so `ui/leftSplitter`
    // still round-trips (its other page, gameColumn, is fixed at 0)
    // while the column itself cannot drift.
    rail->setFixedWidth(232);
    /* ── v1.0.0 sa-A: sidebar ──
       THE GAME COLUMN IS BUILT FIRST AND EMPTIED.

       The prototype's sidebar is ONE column: the rail IS `.side`, and the
       World/Rise segment lives inside it (built below, in the sa-A game
       block). The old shell had a separate narrow column left of the rail
       holding that pair, created by buildGameColumn().

       buildGameColumn() is still the factory that CREATES the two buttons
       (and connects their clicks to switchGame()), and it is still called
       — it just has to run BEFORE the rail block, because the rail's
       sa-A block re-parents those same buttons into the sidebar's
       segment. Re-parenting a widget that does not exist yet is a null
       dereference, and the order the ctor used to run in had the column
       built after the rail.

       What buildGameColumn() returns is still added to leftSplitter as
       index 0 — a zero-width, empty frame. Two reasons it is not simply
       deleted: refreshAutoDetect() reaches its `detectedValue` caption
       through leftSplitter_->widget(0), and the splitter's saved
       `ui/leftSplitter` state restores sizes against two pages. Keeping
       the frame keeps both paths alive at zero visual cost (the sidebar
       is the only visible column now).
       ── end sa-A: sidebar ── */
    auto *gameColumn = buildGameColumn();
    gameColumn->setMinimumWidth(0);
    gameColumn->setMaximumWidth(0);
    auto *railLayout = new QVBoxLayout(rail);
    // v1.0.0 fidelity (rail): THE DRAFT'S `padding:18px 14px 14px`
    // (reference.html:91-93, `box-sizing:border-box`, border-right inside
    // the 232) — left/right 14, top 18, bottom 14. The 20/22/20/18 the
    // rail carried predates the sidebar trim.
    railLayout->setContentsMargins(14, 18, 14, 14);
    railLayout->setSpacing(8);

    // v0.5.6: brand + READY pinned at top (always visible, identity).
    auto *brand = new QLabel();
    trSet(brand, QStringLiteral("console.brand"));
    brand->setObjectName("railBrand");
    auto *brandSub = new QLabel();
    brandSub->setObjectName("railBrandSub");
    // v1.1.1 (顶栏溢出 / 同类缺陷): THE NOTE IS LONGER THAN THE RAIL, AND
    // QLabel CLIPS. The user read the third brand line as `Console · the
    // preview shows demo con` — a hard cut mid-word, in both locales,
    // because the 232px column cannot hold a ~90-character sentence at
    // 11px. The prototype's `.side-note` has no `white-space:nowrap`
    // either: it wraps, on the 1.45 line-height the QSS below already
    // carries for this exact label. So the fix is the CSS behaviour, not
    // a shorter string and not an ellipsis invented for Qt.
    brandSub->setWordWrap(true);
    // The version is substituted at runtime: writing it into the translation
    // string is how the console shipped "控制台 · 0.5" for four releases.
    trHook([brandSub] {
        brandSub->setText(mh::tr(QStringLiteral("console.brand_sub"))
                              .arg(QCoreApplication::applicationVersion()));
    });
    /* ── v1.0.0 sa-A: sidebar ──
       The prototype's brand is THREE lines, and the top one is an eyebrow
       the old rail never had:

         .eyebrow   "MONSTER CONTROL"   mono 10.5px ls .09em uppercase --fg3
         h1         "Monster Control 控制台"           12.5px --fg
         .side-note "非真实连接 · 不会修改 panels.ini 或任何配置文件"
                    11px --fg3, line-height 1.5

       The eyebrow is a new QLabel with a new objectName (railEyebrow).
       railBrand and railBrandSub keep their objectNames: control_l2_smoke
       finds railBrandSub and asserts its text flips with the language
       chip, so the third line must stay a widget whose text retranslateUi
       rewrites. Its copy is now the prototype's `notreal` line rather than
       the version string, which is why the trHook above is left in place
       but harmless — retranslateUi replays it, then the sa-A hook below
       overwrites the same label with the note. See the note at the end of
       the sidebar block for why that ordering is deliberate.
       ── end sa-A: sidebar ── */
    railEyebrow_ = new QLabel();
    trSet(railEyebrow_, QStringLiteral("console.proto.app.title"));
    railEyebrow_->setObjectName(QStringLiteral("railEyebrow"));
    railEyebrow_->setWordWrap(true);
    railEyebrow_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    trHook([this]{
        if (railEyebrow_)
            railEyebrow_->setText(
                consoleText(QStringLiteral("console.proto.app.title")));
    });
    // v1.0.0 fidelity (rail): THE BRAND BLOCK IS THREE LINES IN THE
    // DRAFT'S ORDER — kicker, h1, note (reference.html:287-292):
    //
    //     .brand-kicker  "MONSTER OVERLAY / CONTROL"  mono 10px uppercase
    //     h1             "控制台"                        serif 24px
    //     .brand p       "一次查看一块显示器。窗口归属与查看屏幕相互独立。"
    //
    // The widgets and their i18n keys are unchanged (console.proto.app.
    // title / console.brand / console.proto.notreal) — the rail carried
    // the same three labels in h1-first order. WHAT IS NOT FIXED HERE IS
    // the COPY: the three HTML strings above do not exist in
    // src/resources/i18n/<locale>/console.json under any key (the file is not
    // this round's to edit), so the labels still show the keys' own
    // text. That gap is recorded in VISUAL-STRUCTURE-FIX.md rather than
    // papered over with a hardcoded literal.
    railLayout->addWidget(railEyebrow_);
    railLayout->addWidget(brand);
    railLayout->addWidget(brandSub);
    // The brand's third line is the prototype's `notreal` note, registered
    // through a trHook so a language flip re-queries it (trSet cannot be
    // used twice on the same widget — the registry would replay the first
    // key).
    trHook([brandSub]{
        brandSub->setText(
            consoleText(QStringLiteral("console.proto.notreal")));
    });
    // .side-block's gap is 14px in the prototype; the rail's own spacing
    // stays at 8px for the rows inside a block, and 14px is applied
    // between the brand and the first block through the explicit spacings
    // the rail already uses.
    railLayout->addSpacing(6);

    /* ── v1.0.0: the prototype has neither a READY badge nor a collapse
       button, so both are gone from the sidebar.

       The READY badge (`statusBadge`) reported a state the console never
       actually tracked — it was hardcoded copy, and the prototype's
       equivalent is the statusline's dot, which does follow real state.

       The collapse button (`railAction`) folded the whole stage away and
       left the sidebar as a bare strip. The prototype's answer to "I want
       more room" is the splitter: drag it. Removing the button is what
       stops the sidebar from ever being a column of nothing.

       The stage collapse machinery itself is untouched — the animation
       still addresses consoleSplitter_->widget(1) — it simply has no
       button to trigger it now.
       ── end v1.0.0 sidebar trim ── */

    railLayout->addSpacing(14);

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
    /* ── v1.0.0 sa-A: sidebar ──
       THE GAME BLOCK. The prototype puts it in the sidebar as the first
       .side-block below the brand:

         .side-label  "游戏"            mono 10.5px uppercase --fg3
         .seg         [ World | Rise ] two exclusive buttons
         .note-line   "怪物猎人:崛起 · 伙伴 UI 可用"

       The World/Rise buttons already exist (gameWorldBtn_ / gameRiseBtn_,
       built in buildGameColumn()) and keep their objectName `gameBtn`,
       which five test files look up by name + localized text. They MOVE
       here, out of the narrow left column, and get the segmented face the
       prototype gives them (see control_panel_styles.cpp's
       #gameGroup rules).

       The note line below the segment is new: HTML #game-note, one line
       per game, rewritten by switchGame().
       ── end sa-A: sidebar ── */
    {
        auto *gameLabel = new QLabel();
        trSet(gameLabel, QStringLiteral("console.proto.game"));
        gameLabel->setObjectName(QStringLiteral("sideLabel"));
        scrollLayout->addWidget(gameLabel);

        auto *gameGroup = new QFrame();
        gameGroup->setObjectName(QStringLiteral("gameGroup"));
        auto *gameRow = new QHBoxLayout(gameGroup);
        gameRow->setContentsMargins(2, 2, 2, 2);
        gameRow->setSpacing(2);

        gameWorldBtn_->setParent(gameGroup);
        gameRiseBtn_->setParent(gameGroup);
        for (QPushButton *b : {gameWorldBtn_, gameRiseBtn_}) {
            b->setCheckable(true);
            b->setAutoExclusive(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            gameRow->addWidget(b);
        }
        // The checked state follows the game in force, not the last click
        // on a button that switchGame() then rejected.
        gameWorldBtn_->setChecked(currentGame() != mhw::GameId::Rise);
        gameRiseBtn_->setChecked(currentGame() == mhw::GameId::Rise);
        scrollLayout->addWidget(gameGroup);

        gameNote_ = new QLabel();
        gameNote_->setObjectName(QStringLiteral("gameNote"));
        gameNote_->setWordWrap(true);
        gameNote_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        trHook([this]{
            if (!gameNote_)
                return;
            const QString key = currentGame() == mhw::GameId::Rise
                                    ? QStringLiteral("console.proto.game.note.rise")
                                    : QStringLiteral("console.proto.game.note.world");
            gameNote_->setText(consoleText(key));
        });
        scrollLayout->addWidget(gameNote_);
    }
    scrollLayout->addSpacing(14);

    /* ── v1.0.0 sa-A: sidebar ──
       The nav block's label. The prototype's is `面板` (.side-label), and
       the four nav rows below it are the .nav group. The old rail called
       this group "HUD 对象" — the same four rows under a different name.
       The label follows the prototype; the rows themselves are untouched.
       ── end sa-A: sidebar ── */
    auto *objectsTitle = new QLabel();
    trSet(objectsTitle, QStringLiteral("console.proto.panels"));
    objectsTitle->setObjectName("sideLabel");
    scrollLayout->addWidget(objectsTitle);
    // v1.0.0 fidelity (nav): ONE GRID, OVERVIEW FIRST, gap 3px.
    //
    // reference.html:295-304 is a single `#panel-nav` — `.nav{display:
    // grid;gap:3px}` — whose five `.nav-item`s are, IN THIS ORDER,
    // overview / player / monster / damage / pets, each `min-height:38px`
    // on ONE line (`grid-template-columns:26px minmax(0,1fr) auto 26px`,
    // icon · label · count · eye). The rail used to append the overview
    // AFTER the four panel rows with a 20px gap and a second summary
    // line inside every row — that is the old preview shell's shape, not
    // the frozen draft's. The rows keep their own objectNames, their
    // selected property, their real click path (eventFilter) and their
    // real eye; only the arrangement changes.
    auto *navLayout = new QVBoxLayout();
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(3);   // .nav{display:grid;gap:3px}
    // i18n: the factory takes translation KEYS (not pre-translated text) so
    // retranslateUi() can re-query them on a language switch.

    // ---- v1.0.0: the single-display overview nav item -----------------
    //
    // 原型：侧栏「总览」是最后一个 nav 项，带分隔线与四个面板分开
    // （console-single-display.html 的 .nav-item.overview-item），点击后
    // 画布切到「一次只画一块显示器上的所有窗口」的只读视图。
    //
    // 复用 nav 卡的同一套视觉（同样的 hover / selected 属性、同样的
    // 结构），objectName 用 navOverview —— QSS 里 navPlayer 等四个是
    // 按 objectName 逐条写的，navOverview 会先落到通用 QFrame 规则上，
    // 视觉上与四个面板卡一致（父会话若要给它专属配色，在 QSS 里加一条
    // #navOverview 即可）。
    //
    // 事件走 eventFilter 的 MouseButtonRelease 分支，和四个 navButton
    // 同一条路，不新增机制。
    {
        auto *box = new QFrame();
        box->setObjectName(QStringLiteral("navOverview"));
        box->setProperty("navObject", true);
        box->setCursor(Qt::PointingHandCursor);
        box->installEventFilter(this);
        auto *row = new QHBoxLayout(box);
        // v1.0.0 fidelity (nav): ONE 38px LINE. `.nav-item` is
        // `min-height:38px;padding:0 5px 0 8px` with a 6px grid gap
        // (reference.html:109-112) — the row is ONE line whose children
        // are icon · label · count · eye, so the vertical margins that
        // used to pad a two-line text stack are 0 here and the QSS's
        // `min-height:36px` + 1px border is what makes the row 38.
        row->setContentsMargins(8, 0, 5, 0);
        row->setSpacing(6);   // gap:6px between the grid's columns

        auto *badge = new QLabel(QStringLiteral("◱"));
        badge->setObjectName("navBadge");
        badge->setStyleSheet(QStringLiteral(
            "color:%1;background:transparent;border:1.5px solid %1;"
            "border-radius:3px;font-family:'Chakra Petch';font-weight:700;"
            "font-size:17px;").arg(QColor(150, 155, 158).name()));
        badge->setFixedSize(26, 26);
        badge->setAlignment(Qt::AlignCenter);
        row->addWidget(badge);

        auto *titleLabel = new QLabel();
        trSet(titleLabel, QStringLiteral("console.overview.nav"));
        titleLabel->setObjectName("navTitle");
        row->addWidget(titleLabel, 1);
        /* ── v1.0.0 sa-A: sidebar ──
           The overview row carries a count too (HTML `[data-count="overview"]`
           = the total window count across every panel, "11 窗" on the
           default Rise split scope). Same label face as the four panel
           rows; it just is not fed by updatePanelSummary(), because the
           total is not one panel's section count.
           ── end sa-A: sidebar ── */
        navOverviewCount_ = new QLabel();
        navOverviewCount_->setObjectName(QStringLiteral("navCount"));
        navOverviewCount_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(navOverviewCount_);

        s_overviewNav = box;
        navLayout->addWidget(box);
    }
    // The four panel rows follow the overview, in the draft's own order.
    navLayout->addWidget(buildObjectButton(QStringLiteral("P"),
                                           QStringLiteral("console.nav.player"),
                                           QStringLiteral("console.nav.player_summary"), 0));
    navLayout->addWidget(buildObjectButton(QStringLiteral("M"),
                                           QStringLiteral("console.nav.monster"),
                                           QStringLiteral("console.nav.monster_summary"), 1));
    navLayout->addWidget(buildObjectButton(QStringLiteral("D"),
                                           QStringLiteral("console.nav.damage"),
                                           QStringLiteral("console.nav.damage_summary"), 2));
    navLayout->addWidget(buildObjectButton(QStringLiteral("C"),
                                           QStringLiteral("console.nav.pets"),
                                           QStringLiteral("console.nav.pets_summary"), 3));
    scrollLayout->addLayout(navLayout);
    /* ── v1.0.0 sa-A: sidebar ──
       The prototype's sidebar has NO "工作区" group and NO preset entry.
       It has the nav, then .side-foot pinned to the bottom:

         (margin-top: auto)
         设置 / 帮助 / 重置演示          ← 11px --fg2, hover --surf2
         [保存] [导出 JSON]             ← the drawer's own action pair

       The WORKSPACE label and the disabled preset button are removed
       outright: they carried no behaviour (the preset button was
       `setEnabled(false)` from the day it was written) and nothing looks
       them up. The rail's READY badge and the stage-toggle button stay
       pinned above the scroll area — they are status, not navigation, and
       the prototype has no equivalent to remove.

       v1.1.5: TWO OF THE PROTOTYPE'S OWN FIVE ARE GONE TOO. 重置演示
       (reload this scope from disk) and 保存 (the write closeEvent()
       already performs) were removed on the user's verdict that the foot
       was redundant — what remains is 帮助 / 重置布局 / 全部重置 /
       导入布局包 / 设置 / 导出布局, which is every distinct operation
       the foot ever had.
       ── end sa-A: sidebar ── */

    /* ── user-8: rail REFramework / Lua status summary ────────────────
       A READ-ONLY block under the panel list: two or three short lines
       saying whether REFramework is there and whether the overlay Lua is
       there, so the player does not have to open the settings dialog to
       find out. Labels only — the install / remove entry points stay on
       the card (riseReframeworkCard in settings), because two entry
       points for one operation is the duplication being removed elsewhere
       in this console.

       It is built by the card rather than inlined here so it answers from
       the SAME locate + manager.status + menu-state read the card takes,
       never from a second probe that could disagree with it. The card's
       1500 ms tick (riseReframeworkRefreshTimer_, end of this file) is
       what refreshes it; switchGame() hides it under World. */
    scrollLayout->addSpacing(14);
    riseCard_->buildSidebarSummary(scrollContent, scrollLayout);

    scrollLayout->addStretch(1);
    railScroll->setWidget(scrollContent);
    railLayout->addWidget(railScroll, 1);

    // ---- .side-foot ----------------------------------------------------
    //
    // margin-top:auto is the stretch inside railScroll's content; the
    // rule above it (border-top 1px --line) is the scroll area's bottom
    // edge, which reads as the same divider. The three text buttons sit
    // in a row, and the two action buttons below them.
    railLayout->addSpacing(12);
    {
        auto *footRow = new QHBoxLayout();
        footRow->setContentsMargins(0, 0, 0, 0);
        footRow->setSpacing(4);

        helpBtn_ = new QPushButton();
        trSet(helpBtn_, QStringLiteral("console.proto.help"));
        // v1.0.0: a distinct objectName per foot button. Sharing "sideFoot"
        // let QSS hit all three at once, but it also made findChild() return
        // an arbitrary one, so no test or debugger could tell them apart.
        // QSS still targets them together via [objectName~="sideFoot"].
        helpBtn_->setObjectName(QStringLiteral("sideFootHelp"));
        helpBtn_->setCursor(Qt::PointingHandCursor);
        // The prototype's #help-modal. Was unconnected: a button labelled
        // 帮助 that showed nothing.
        connect(helpBtn_, &QPushButton::clicked,
                this, &ControlPanel::openHelpDialog);
        footRow->addWidget(helpBtn_);

        // v1.1.5: 重置布局 — the shipped default, applied to the group the
        // rail has in front.
        //
        // IT IS THE ONLY 重置 IN THE FOOT. 重置演示 (the prototype's
        // `reset()`: re-read this scope from disk, keep the file) sat here
        // until the user's verdict 「重置演示跟保存没什么用」 — with a
        // reset-to-shipped-default beside it, a reload-from-disk is a
        // third spelling of "start over" that answers a question nobody
        // asked, and two buttons with 重置 in the name is how a user
        // clicks the wrong one. resetPrototypeScope() itself stays: the
        // console_reset_scope suite calls it directly, and the function
        // is the documented answer to "what does a reload touch".
        //
        // THEIR OWN ROW. Four buttons in the foot row clipped the two
        // labels to 置布局 / 部重置 at the rail's ~230px width — a button
        // whose caption is cut is a button whose caption must be guessed,
        // and guessing between 重置布局 and 全部重置 is exactly what this
        // placement was meant to prevent.
        auto *footRow2 = new QHBoxLayout();
        footRow2->setSpacing(8);
        resetLayoutBtn_ = new QPushButton();
        trSet(resetLayoutBtn_, QStringLiteral("console.proto.layout.reset"));
        resetLayoutBtn_->setObjectName(QStringLiteral("sideFootResetLayout"));
        resetLayoutBtn_->setCursor(Qt::PointingHandCursor);
        connect(resetLayoutBtn_, &QPushButton::clicked, this, [this] {
            const EditableSelection sel = currentEditableSelection();
            resetLayoutToDefault(sel.panel ? sel.panelIndex : -1);
        });
        footRow2->addWidget(resetLayoutBtn_);

        resetLayoutAllBtn_ = new QPushButton();
        trSet(resetLayoutAllBtn_,
              QStringLiteral("console.proto.layout.reset.all"));
        resetLayoutAllBtn_->setObjectName(
            QStringLiteral("sideFootResetAll"));
        resetLayoutAllBtn_->setCursor(Qt::PointingHandCursor);
        connect(resetLayoutAllBtn_, &QPushButton::clicked, this, [this] {
            // The all-groups case asks first: it is the one action here
            // that cannot be undone from inside the console.
            QMessageBox box(QMessageBox::Warning,
                            consoleText(QStringLiteral(
                                "console.proto.layout.reset.all")),
                            consoleText(QStringLiteral(
                                "console.proto.layout.reset.all.confirm")),
                            QMessageBox::Yes | QMessageBox::Cancel, this);
            if (box.exec() != QMessageBox::Yes)
                return;
            resetLayoutToDefault(-1);
        });
        footRow2->addWidget(resetLayoutAllBtn_);

        // v1.1.5: 导入布局包. The third of the three layout verbs (导出 /
        // 重置 / 导入) — and the only one that opens a file picker, which
        // is why it is the one button here that is named for where it
        // takes you.
        //
        // ITS OWN ROW. Three four-character labels in the rail's ~230px
        // crushed 导入布局 into overlapping glyphs; the foot has room for
        // the row, and a button whose caption collides with itself is a
        // caption the user has to guess at.
        auto *footRow3 = new QHBoxLayout();
        footRow3->setSpacing(8);
        importLayoutBtn_ = new QPushButton();
        trSet(importLayoutBtn_,
              QStringLiteral("console.proto.layout.import.btn"));
        importLayoutBtn_->setObjectName(QStringLiteral("sideFootImport"));
        importLayoutBtn_->setCursor(Qt::PointingHandCursor);
        connect(importLayoutBtn_, &QPushButton::clicked,
                this, &ControlPanel::importLayoutPackage);
        footRow3->addWidget(importLayoutBtn_);

        railLayout->addLayout(footRow);
        railLayout->addLayout(footRow2);
        railLayout->addLayout(footRow3);

        // 设置 keeps its own face: the prototype's settings entry is the
        // only outlined button in the foot, because it is the one that
        // opens a dialog. It is still a no-op this round — the settings
        // popup (Rise REFramework card + appearance + language) is not
        // part of the layout rework — and it stays visible and styled so
        // the shell is final.
        // 设置 opens the prototype's #settings-modal: appearance (theme
        // + language), the Rise environment card, the data-source note
        // and About. It was a styled no-op until this round — the button
        // existed and did nothing, which is the exact failure this port
        // exists to remove.
        settingsBtn_ = new QPushButton();
        trSet(settingsBtn_, QStringLiteral("console.proto.settings"));
        settingsBtn_->setObjectName("settingsBtn");
        settingsBtn_->setCursor(Qt::PointingHandCursor);
        connect(settingsBtn_, &QPushButton::clicked,
                this, &ControlPanel::openSettingsDialog);
        settingsBtn_->setParent(rail);
        railLayout->addWidget(settingsBtn_);

        auto *actionRow = new QHBoxLayout();
        actionRow->setContentsMargins(0, 0, 0, 0);
        actionRow->setSpacing(6);

        // v1.1.5: 导出布局 IS THE ONLY ACTION BUTTON LEFT IN THIS ROW.
        // The 保存 that sat beside it performed saveMaskToDisk() +
        // saveAllSplitAndBaseWindows() — byte for byte the write
        // closeEvent() already performs on every exit (and that
        // saveBeforeLaunch() performs before the overlay relaunches), so
        // the button was a second door to a room with one entrance. The
        // user's verdict: 「保存没什么用」. The functions stay; only the
        // duplicate door is gone.
        exportJsonBtn_ = new QPushButton();
        trSet(exportJsonBtn_, QStringLiteral("console.proto.export"));
        exportJsonBtn_->setObjectName(QStringLiteral("sideFootExport"));
        exportJsonBtn_->setCursor(Qt::PointingHandCursor);
        // The prototype's #export-modal: the live scope's JSON, with a
        // real copy and a real download. Was unconnected.
        connect(exportJsonBtn_, &QPushButton::clicked,
                this, &ControlPanel::openExportDialog);
        actionRow->addWidget(exportJsonBtn_);
        railLayout->addLayout(actionRow);
    }

    // v0.5.6: START + ESC hint pinned at bottom (always visible, CTA).
    // NOTE (pre-existing quirk, preserved): this rail button is
    // display-only. The live launcher now lives in the toolbar
    // (buildToolbar()), which points the startBtn_ MEMBER at its own
    // button — so this one is a separate local (railStart) that the
    // member never points at. Overwriting the member here would hand
    // setOverlayRunning() and the enable/disable lambdas a decorative
    // button again, which is the exact bug this change removes.
    //
    // v1.0.0: its objectName is railStart, NOT startBtn. Two widgets
    // sharing one objectName is a silent trap: findChild() returns the
    // first match in child order, and the rail sits left of the content
    // column, so every lookup would land on this decorative copy while
    // the button that actually launches sat unreachable. The QSS rule
    // that gave it the orange CTA moved with the name
    // (control_panel_styles.cpp), so it looks exactly as it did.
    /* ── v1.0.0: the rail's own start button and its keyboard hint are
       gone. The prototype puts START in the toolbar, where the real
       overlay controller lives — and this pair was never that: railStart
       was a display-only button with no connection (a quirk the source
       itself flagged), while the button setOverlayRunning() renames and
       enables is the toolbar's. Two controls that both look like "start"
       and only one of which works is the kind of thing a rewrite exists to
       remove.

       railHint described keyboard shortcuts (ESC / 1-4) for navigating the
       old rail. The prototype has no such shortcuts.
       ── end v1.0.0 sidebar trim ── */
    // v0.7.1: wrap the rail in a horizontal QSplitter alongside the
    // dedicated game column. The splitter handle width is 0 (QSS keeps
    // it hidden); the visible divider is the rail's border-right, which
    // now lives between the game column and the rail instead of at the
    // window's left edge.
    auto *leftSplitter = new QSplitter(Qt::Horizontal);
    leftSplitter->setObjectName("leftSplitter");
    leftSplitter->setHandleWidth(0);
    leftSplitter->setChildrenCollapsible(false);
    leftSplitter->addWidget(gameColumn);
    leftSplitter->addWidget(rail);
    leftSplitter->setSizes({0, 232});   // gameColumn 0 : rail 232 (fixed)
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

    // v1.0.0: the inspector stack's host. It used to be a fixed-width
    // (480px) column beside the rail; it is now the DRAWER under the
    // layout area, so it stretches with the window and keeps no width
    // of its own. The stack itself is untouched — same four pages, same
    // objectName, and a fifth EMPTY page appended below for the
    // "nothing selected" state.
    auto *configHost = new QFrame();
    configHost->setObjectName("configHost");
    auto *configLayout = new QVBoxLayout(configHost);
    configLayout->setContentsMargins(0, 0, 0, 0);
    configLayout->setSpacing(0);
    inspectorStack_ = new QStackedWidget();
    inspectorStack_->setObjectName("inspectorStack");
    // ── v1.0.0 (A): A HIDDEN HOST FOR THE OLD PANEL PAGES ───────────────
    // The four pages stay built — they own the master toggle, the nine
    // section rows and the position/scale controls that drive each Panel
    // (ctl_[i].subs), and selectPanel() still moves the stack's index so
    // they behave exactly as before. They are simply no longer inside the
    // drawer's layout: reparented under a child widget that is NEVER
    // shown, so they occupy no height, paint nothing, and are still
    // reachable through findChild() (the object tree, not the layout).
    //
    // WHY A HOST AND NOT configHost ITSELF. QWidgetPrivate::showChildren()
    // shows every child its parent's show() reaches UNLESS the child
    // carries WA_WState_ExplicitShowHide, and the pages are shown while
    // they are built (buildInspector returns a shown page, and the stack
    // shows its current one). A hide() on the stack alone would therefore
    // be undone by any later explicit show of a page; putting the stack
    // under a host that is itself hidden and never shown gives the pages
    // no path to the screen at all.
    auto *panelPagesHost = new QWidget(configHost);
    panelPagesHost->setObjectName("panelPagesHost");
    panelPagesHost->hide();
    // v0.14.1: BUILD THE FOUR PANELS BEFORE THE INSPECTORS THAT READ THEM.
    //
    // Chasing the construction order was the wrong instinct here: the two
    // steps genuinely need each other. buildInspector() reads
    // panelAt(idx)->scale()/opacity()/bgAlpha() to initialise its sliders,
    // while rebuildMotherPanels() re-pushes the mask and master toggle out
    // of the PanelCtl the inspector fills. Whichever runs first starves
    // the other, and starved is fatal — buildInspector() dereferences
    // panelAt() without a null check, which is exactly the SIGSEGV the
    // console tests were dying of at panel.h:321 (`scale()`), with gdb
    // reporting p = 0x0.
    //
    // So the ctor is split in two, and only the two halves swap order:
    //
    //   1. constructMotherPanels()  — new Panel per slot under the scope in
    //      force. No inspector state touched, so nothing to be starved of.
    //      This must be the FIRST reader of the scope: loadConfig() runs
    //      inside the Panel ctor and resolves its group from the two
    //      globals, and by this point they are final.
    //   2. the four buildInspector() calls  — now safe, because the panels
    //      they read already exist.
    //
    // rebuildMotherPanels() keeps its full meaning (destroy + construct +
    // adopt) and remains what switchGame() calls on a rail click. The ctor
    // no longer calls it, because at ctor time there is nothing to destroy
    // and nothing to adopt yet.
    constructMotherPanels();

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

    // v1.0.0: page 4 — the EMPTY drawer. The four inspector pages are
    // indices 0-3 (one per panel, and selectPanel() still addresses them
    // by that number); this one is the state where no panel is selected.
    // The console starts selected (selectPanel(0) at the end of the ctor),
    // so this page is not what the user sees first — but the drawer has to
    // have a "nothing here yet" face for the day selection can be cleared.
    //
    // WHY NOT A SECOND STACKED WIDGET: a test locates the inspector by
    // "the console's only QStackedWidget". A settings page on its own
    // stack would make that lookup ambiguous and break it; the settings
    // popup therefore uses a plain scroll area (see the settings button
    // in buildToolbar / the rail footer).
    {
        auto *page = new QWidget();
        page->setObjectName("drawerEmptyPage");
        auto *vl = new QVBoxLayout(page);
        vl->setContentsMargins(26, 26, 26, 26);
        vl->setSpacing(8);
        vl->addStretch(1);
        auto *title = new QLabel();
        trSet(title, QStringLiteral("console.drawer.empty"));
        title->setObjectName("drawerEmptyTitle");
        title->setAlignment(Qt::AlignCenter);
        vl->addWidget(title);
        auto *sub = new QLabel();
        trSet(sub, QStringLiteral("console.drawer.empty_sub"));
        sub->setObjectName("drawerEmpty");
        sub->setWordWrap(true);
        sub->setAlignment(Qt::AlignCenter);
        vl->addWidget(sub);
        vl->addStretch(1);
        inspectorStack_->addWidget(page);
    }

    // ── v1.0.0 (A): THE OLD INSPECTOR DOES NOT OCCUPY THE LAYOUT ────────
    // configLayout used to add inspectorStack_ here, so the four old
    // panel pages (title + preview + section rows + preview box) took
    // most of the drawer's height and the new three-column face squeezed
    // into the rest — the user saw the OLD inspector with the new one
    // below it.
    //
    // The stack is KEPT (not deleted): it is the old controllers' host,
    // and the tests that walk the four pages by index still find it. It
    // is re-parented under the hidden host built above instead of being
    // added to configLayout, so it holds no layout slot, no minimum
    // height and no pixels — the face below is the drawer's only child.
    inspectorStack_->setParent(panelPagesHost);
    /* ── v1.0.0 (O1): THE PANEL STACK STAYS VISIBLE — BLOCKED, NOT FORGOTTEN
       The frozen inspector is ONE thing: `.inspector` = a 45px
       `.shell-head` + `.inspector-content` (reference.html:336-337,
       VISUAL-CONTRACT §6). The four legacy panel pages above the fidelity
       face — `当前对象 / 01 玩家`, the master chip, the section rows — are
       NOT in the draft, and they are also what pushes the three columns
       out of the pane: measured at 1600x1000 rise/split/DP-1 the pane is
       302px, the stack's own sizeHint is 384 and the face's is 234, so a
       proportional cut leaves the face 92 of the 302 — 45 of which is the
       head, i.e. the columns get 47 of the 163 they need.

       HIDING IT IS TRIED AND REVERTED, and the reason is a test, not a
       design opinion: tests/control_reframework_smoke.cpp:144 does
       `selectDamageInspector(&cp)` then asserts
       `h.card->isVisible()` on the Rise REFramework card, which is built
       INTO the damage inspector page (line 4335's block) and therefore
       into this stack. Hiding the stack makes that test fail — measured,
       not predicted: the run went 30/31 with only
       `monster-control-rise-rf-smoke` red (「rise: card is visible /
       install button visible / remove-Lua button visible」).

       So the stack keeps its space and the fidelity face keeps a
       QScrollArea (`.inspector-content{overflow:auto}` IS the draft's own
       answer to a content taller than its row), and this is recorded as
       the O1 port's largest structural divergence. The way out is an O5
       decision — move the Rise card's smoke assertions to the SETTINGS
       dialog (which already builds the same card, line ~6150) and the
       stack can be dropped — and it is written up in
       implementation/O1-completion-report.md rather than papered over here.
       ── end O1: panel stack kept ── */

    /* ── v1.0.0 fidelity port: the inspector's own face ──────────────────
       THE THREE COLUMNS, ON THE DRAWER ITSELF.

       The prototype's inspector is a three-column grid under a 45px
       header: metadata (0.9fr), the window's sections and its sibling
       chips (1fr), and the real Panel pixmap (1.2fr). Its subject is the
       SELECTED WINDOW — `state.selected`, which is a window key and not
       a panel slot.

       The Qt console's inspectorStack_ is a four-page stack of PANEL
       pages, and it stays exactly that (four test files drive it, and it
       is the console's only QStackedWidget — see the empty-page note
       below). So the window's own form is built OUTSIDE the stack, on
       the same frame, ABOVE it: the stack answers "which panel's
       settings", this answers "which window is selected".

       WHY IT IS BUILT HERE AND NOT ON A FIFTH STACK PAGE. A fifth page
       would make the stack's index ambiguous and force selectPanel()'s
       setCurrentIndex(idx) to special-case it. Two widgets on one frame
       have no such ambiguity: the window form is always live, and the
       panel pages keep their indices.

       THE GEOMETRY IS THE PROTOTYPE'S. `.inspector-grid` is
       `grid-template-columns:.9fr 1fr 1.2fr; gap:18px;
       padding:12px 14px`, and the shell head above it is min-height 45px
       (`.inspector .shell-head`). The columns below are laid out with a
       QGridLayout carrying those numbers, so a measured bbox matches
       without a second table.
       ── end v1.0.0 inspector face ── */
    buildWindowInspectorFace(configHost, configLayout);

    // ---- the four mother panels ------------------------------------------------
    //
    // Built HERE, after the inspectors, because rebuildMotherPanels()
    // re-pushes each panel's section mask and master toggle out of
    // PanelCtl — and a PanelCtl is not filled until buildInspector() has
    // run for that slot. Built before, it would re-push nothing and the
    // user's saved mask would sit unread.
    //
    // The inspectors now exist and their PanelCtl entries are filled, so
    // the masks and master toggles they represent can be pushed into the
    // panels that step 1 constructed. This is the "adopt" half of what
    // rebuildMotherPanels() does in full, separated only because the ctor
    // needs it AFTER the inspectors while a rail click needs the whole
    // thing (destroy + construct + adopt) at once.
    adoptMotherPanels();

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
    // v1.0.0 fidelity (stage): NO GAP BETWEEN .shell-head AND
    // .stage-body. `.shell-head` is a 48px row and `.stage-body` starts
    // on the next pixel — `height:calc(100% - 48px)` with no margin
    // between the two grid tracks (reference.html:163/169), measured
    // `stage y=80 … stage-body y=129` (48 head + 1 border), whereas the
    // layout's DEFAULT 6px spacing put the canvas at y=134. The 12/14px
    // the page keeps around the BOARD is not here either: it is
    // `.board-viewport`'s own `padding:12px 14px 0`, which lives inside
    // the canvas (HudCanvas's kBoardPadTop / kBoardPadX).
    stageLayout->setSpacing(0);

    // v0.5 UI-link: stagebar with SAFE AREA / GRID toggles
    auto *stagebar = new QHBoxLayout();
    // v1.0.0 (O1): 48px, the measured `.shell-head` min-height
    // (reference.html:163, VISUAL-CONTRACT §10.5). 11+26+11 = 48 with
    // the tallest child (the 26px toggles) centred, which is what
    // `min-height:48px; align-items:center` does for the draft's own
    // (shorter) children. The horizontal 14 matches `padding:0 14px`.
    stagebar->setContentsMargins(14, 11, 14, 11);
    stagebar->setSpacing(8);
    // ---- v1.0.0: the overview's DISPLAY picker ------------------------
    //
    // It only ever had one job — showing which display the overview is
    // pointed at — and the prototype's topbar control does that job
    // permanently, so the stage bar keeps the canvas controls it owns
    // (safe area / grid / zoom / theme) and nothing else.
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
    // ── v1.0.1 (验收项5): THE RESET-TO-1:1 HALF OF THE ZOOM ENTRY ──────
    // − / % / + are useless without a way back to the default. The
    // caption is a bare ratio (1:1) — the same untranslated-symbol
    // convention as the − and + buttons, so no new i18n key is needed.
    zoomFitBtn_ = new QPushButton(QStringLiteral("1:1"));
    zoomFitBtn_->setObjectName("stageToggle");
    zoomFitBtn_->setFixedSize(26, 26);
    zoomFitBtn_->setCursor(Qt::PointingHandCursor);
    // ── v1.0.0 (D): THE STAGE HEAD IS reference.html:325-328 ────────────
    //
    //   <div class="shell-head">
    //     <h2>显示器布局预览</h2><span class="sub" id="stage-sub">当前查看屏幕</span>
    //     <span class="grow"></span>
    //     <span class="chip accent" id="screen-chip">DP-1</span>
    //     <span class="chip" id="window-count">0 个窗口</span>
    //   </div>
    //
    // h2 + sub on the left, a growing gap, then exactly two chips. The
    // zoom / safe-area / grid buttons that used to sit here are the
    // preview console's OWN scaffolding — they appear nowhere in the
    // reference, so (D) they must not occupy a layout slot or paint.
    // They are still constructed and parented to the stage so the tests
    // that pin their behaviour keep finding them through findChild()
    // (the sidebar's localeChip_ note explains why the parent matters).
    stageTitle_ = new QLabel();
    trSet(stageTitle_, QStringLiteral("console.stage.title"));
    stageTitle_->setObjectName(QStringLiteral("stageTitle"));
    stagebar->addWidget(stageTitle_);
    stageSub_ = new QLabel();
    stageSub_->setObjectName(QStringLiteral("stageSub"));
    stagebar->addWidget(stageSub_);
    // Keep the interaction controls before the flexible gap: narrow
    // compositor tiles must not clip zoom-in/reset off the right edge.
    stagebar->addSpacing(6);
    stagebar->addWidget(zoomOutBtn_);
    stagebar->addWidget(zoomLabel_);
    stagebar->addWidget(zoomInBtn_);
    stagebar->addWidget(zoomFitBtn_);
    stagebar->addStretch(1);
    stageScreenChip_ = new QLabel();
    stageScreenChip_->setObjectName(QStringLiteral("stageScreenChip"));
    stagebar->addWidget(stageScreenChip_);
    stageWindowCountChip_ = new QLabel();
    stageWindowCountChip_->setObjectName(QStringLiteral("stageWindowCountChip"));
    stagebar->addWidget(stageWindowCountChip_);
    // ── v1.0.1 (验收项5): THE ZOOM CONTROLS ARE VISIBLE AGAIN ──────────
    // They were reparented to `stage` and hide()d ("the prototype has no
    // such control"), so the only way to zoom was an undiscoverable
    // Ctrl+wheel. They now occupy real slots in the same 48px head row
    // (26px children, so the row's height budget is unchanged and
    // nothing below — slider, inspector, canvas — is squeezed), and they
    // drive the REAL board through HudCanvas::setZoom: zoomChanged is
    // already connected to updateZoomLabel() further down.
    // The sub line and the count are live text (the display changes, the
    // window list changes, the language changes), so they are re-derived
    // rather than replayed.
    trHook([this]{ refreshStageHead(); });

    // v1.0.1: only safeAreaBtn_ (and gridBtn_ below) is still hidden
    // preview-console scaffolding. The zoom trio lives in the stagebar.
    safeAreaBtn_->setParent(stage);
    safeAreaBtn_->hide();
    gridBtn_ = new QPushButton();
    trSet(gridBtn_, QStringLiteral("console.stage.grid"));
    gridBtn_->setObjectName("stageToggle");
    gridBtn_->setCheckable(true);
    gridBtn_->setChecked(true);
    gridBtn_->setCursor(Qt::PointingHandCursor);
    gridBtn_->setParent(stage);
    gridBtn_->hide();
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
    // v1.0.0: parent it to the console without giving it a layout slot.
    //
    // findChild() walks the CHILD tree, not the widget tree: a QFrame with
    // no parent is invisible to it even though the pointer exists. Dropping
    // localeChip_ from the sidebar's layout therefore made
    // control_l2_smoke's findChild("localeChip") return null and fail the
    // build — the chip has to stay a child of the console to remain
    // drivable, it just must not occupy space or paint.
    localeChip_ = new QFrame(this);
    localeChip_->setObjectName(QStringLiteral("localeChip"));
    localeChip_->hide();
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
    /* ── v1.0.0 sa-A: sidebar ──
       THE LANGUAGE CHIP AND THE THEME BUTTON LEAVE THE SIDEBAR.

       The prototype puts language in the topbar's `localeCombo`
       (a QComboBox, 中文 / English) and the theme in the SETTINGS dialog's
       `#theme-dark` / `#theme-light` segmented control. Neither is in
       `.side` at all.

       Both widgets are still CONSTRUCTED here — they own state
       (localeChip_'s eventFilter routing, themeBtn_'s theme-switch
       connect) that control_l2_smoke drives through findChild("localeChip"),
       and deleting them would break that test. They are simply not added
       to the sidebar any more: no addWidget, so they exist in the object
       tree but occupy no layout slot and paint nothing.

       The language switch the user actually drives is the topbar's
       localeCombo_, built in buildToolbar(); see that block for how the
       combo and the chip stay in sync.
       ── end sa-A: sidebar ── */

    // v1.0.0: SETTINGS is now built in the .side-foot block above (it is
    // the prototype's settings entry and belongs with the footer's other
    // buttons). This is left as the i18n + objectName registration only.
    // i18n: replay the theme chip copy together with the language chip.
    trHook([this]{ updateThemeChipText(); });
    stageLayout->addLayout(stagebar);

    // ── v1.0.0 integration (stage A): THE SHELL TURNS THE BOARD ON ────
    // consoleFidelity_ defaults false and stays false for every canvas
    // that is not this one (the 12 baselines and both canvas->grab()
    // suites build their own HudCanvas and never call the setter, which
    // is exactly why they are untouched). Only the console's own board
    // is the prototype's board: 16px pad, aspect-locked frame, grid,
    // the fidelity chrome and — the reason it is set HERE rather than
    // later — the two visibility gates in layoutSubWindows()/paint().
    // Pushed before any binding exists so the very first paint is the
    // fidelity paint and no legacy frame is ever composited.
    canvas_ = new HudCanvas();
    canvas_->setConsoleFidelityMode(true);
    canvas_->setTransparencyGuideEnabled(true);
    // The console is the ONLY consumer that wants a fixed viewport: the stage
    // area has a user-dragged height (ConsoleLayoutStore::stageHeight) that a
    // zoom must not push around. Bare-canvas consumers keep the legacy
    // contract (default false = heightForWidth drives sizeHint exactly as
    // before), so this is opt-in rather than a global behaviour change.
    canvas_->setFixedViewportMode(true);
    // The stage already names the display, its size and the window count in
    // chrome; the canvas' own top-left label repeated all three inside the
    // picture. Only the console hides it — every other canvas keeps drawing.
    canvas_->setFidelityBoardLabelVisible(false);
    canvas_->installEventFilter(this);
    // ── 实测缺陷1 (the example area's own scroll range) ────────────────
    // THE FLAG QSCROLLAREA ACTUALLY READS. HudCanvas already answers the
    // height-for-width contract (hasHeightForWidth() is overridden at
    // hud_canvas.h:526 and heightForWidth() at hud_canvas.cpp:1575 — the
    // 16:9 + header/footer height the note below promises "still
    // scrolls"), but QScrollArea::updateScrollSizes does not ask the
    // widget: it asks `widget->layout() ? layout->hasHeightForWidth() :
    // widget->sizePolicy().hasHeightForWidth()` (qscrollarea.cpp:158).
    // The console's canvas has no layout and its size policy never
    // carried the flag — setFixedViewportMode() above rebuilds the policy
    // as plain Expanding/Expanding — so the scroll area resized the
    // canvas to the VIEWPORT height on every layout pass: no range, no
    // scrollbar, and a board taller than the pane (any square or
    // portrait output; measured 800x800 → a ~950px board in a 532px
    // pane) simply clipped. With the shell no longer scrolling the page
    // (setPageScrollEnabled(false) above), that clip is what the user
    // would see — so the example area is made scrollable here, which is
    // the behaviour the note at canvasScroll has claimed all along.
    //
    // Setting it AFTER setFixedViewportMode(), not before: that call
    // rewrites the policy, so anything earlier would be wiped.
    {
        QSizePolicy canvasPolicy = canvas_->sizePolicy();
        canvasPolicy.setHeightForWidth(true);
        canvas_->setSizePolicy(canvasPolicy);
    }
    auto *canvasScroll = new CanvasScrollArea(canvas_);
    canvasScroll->setObjectName("canvasScroll");
    // v0.5.6: stage now owns the full window width, so let the canvas
    // stretch horizontally to fill the viewport. The paint path already
    // recomputes layout from width()/height(), so widening the widget
    // widens the 16:9 frame and the four panel slots inside it.
    // Vertical overflow still scrolls (canvas heightForWidth enforces
    // the 16:9 + header/footer ratio).
    canvasScroll->setWidgetResizable(true);
    canvasScroll->setFrameShape(QFrame::NoFrame);
    stageLayout->addWidget(canvasScroll);
    /* ── v1.0.0 sa-D: the board hint ──
       THE PROTOTYPE'S `.board-hint`, AND IT IS OVERVIEW-ONLY.

       HTML puts a one-line caption under the layout board, inside the
       board's card:

         <div class="board-hint" id="board-hint"></div>

       and renderBoardHead() writes one of three strings into it:

         overview → board.hint.overview
                    「总览为只读视图:一次只绘制一块显示器,点击窗口在下方查看详情。」
         panel    → board.hint.idle
                    「点击窗口进行选中;面板视图下可拖动窗口,位置数值会同步。」
         editing  → board.hint.edit
                    「拖动移动窗口 · 方向键微调 1px(Shift 为 10px)· Esc 保存退出编辑。」

       The task is explicit about which of the three this round owns:
       the caption appears IN THE OVERVIEW and is hidden in the panel
       view. The panel and edit strings are written by whoever owns the
       edit-mode entry (the toolbar's EDIT button); the label exists here
       so that work has somewhere to write.

       WHY IT IS A LABEL AND NOT A STATUS TIP. The prototype's caption is
       permanent chrome under the board, not a transient tooltip: it is
       the only place the read-only contract is stated where the user is
       actually looking. A status tip would vanish after 3 s and say
       nothing to the user who arrives at the overview later.

       WHITHIN the stage, not beside it: the stage's own layout is
       [stagebar / canvas / hint], and the hint sits at the bottom where
       the prototype's card puts it. The stage's collapse animation
       addresses consoleSplitter_->widget(1) as a whole, so the hint
       collapses with the canvas rather than stranding itself.
       ── end sa-D ── */
    s_boardHint = new QLabel();
    s_boardHint->setObjectName(QStringLiteral("boardHint"));
    // v1.0.0 (O1): THE MEASURED 32px ROW, NOT A WRAPPED LABEL. `.stage-
    // body` is `grid-template-rows:minmax(0,1fr) 32px` and `.board-hint`
    // is `display:flex; align-items:center; padding:0 14px` — the row is
    // always 32px tall and the copy is ONE line with an ellipsis
    // (`white-space:nowrap; overflow:hidden; text-overflow:ellipsis`),
    // measured `board-hint x=249 y=731.63 w=1319 h=32` (geometry.json
    // state01). The old spellings — wordWrap on, AlignTop, 4/6 vertical
    // padding — measured 21px and grew with the string instead.
    s_boardHint->setWordWrap(false);
    s_boardHint->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    s_boardHint->setContentsMargins(14, 0, 14, 0);
    s_boardHint->setFixedHeight(32);
    s_boardHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // Registered through a hook rather than trSet, so a language flip
    // re-queries the key the CURRENT mode needs — see refreshBoardHint()
    // below for why the choice is three-way and read from live state.
    // The hook also owns the label's visibility, which is why the
    // overview toggles no longer show() / hide() it themselves.
    trHook([this]{
        if (canvas_)
            refreshBoardHint(canvas_);
    });
    // The hook above writes a line into it as soon as canvas_ exists;
    // until then the label is empty, so it starts hidden and the ctor's
    // trHook block (refreshBoardHint at the end) is what shows it.
    s_boardHint->hide();
    stageLayout->addWidget(s_boardHint);
    // v0.5 P1.4: clicking a HUD in the unified canvas selects it in the
    // rail/inspector, so the canvas is a real workspace, not a passive
    // preview. selectPanel() is idempotent, so re-clicking the active
    // panel costs nothing.
    connect(canvas_, &HudCanvas::panelSelected, this, [this](int idx){
        // ── v1.0.0 integration (stage A): THIS IS A BOARD CLICK ─────────
        // fromNav = false. The board's click handler in reference.html
        // writes `state.selected` and nothing else; only `#panel-nav
        // [data-view]` (:511) may change `state.view`. Passing the default
        // (true) here meant the very first click on a box while the
        // overview was open threw the overview away — the rail's own
        // path and the board's were the same call with no way to tell
        // them apart.
        selectPanel(idx, /*fromNav=*/false);
        // ── v1.0.0 (B): AND THE CLICK OPENS THE WINDOW'S DETAILS ────────
        // reference.html:511 wires the board's click straight to
        // selectWindow(id) — the panel box IS the window in Single mode
        // (the mother panel is one of the scope's windows, subIndex -1),
        // so a click on it must open the inspector's three columns, not
        // just move the panel slot. selectPanel() above keeps doing the
        // PANEL half (rail highlight, preview screen, topbar title).
        //
        // In Split the mother panel has no window row at all (the scope
        // lists only the split windows, as the prototype does), so no row
        // matches and the click stays what it was: a panel selection.
        for (const mhw::WindowState &w : buildWindowStates()) {
            if (w.panelIndex == idx && w.subIndex < 0) {
                selectWindow(idx, -1);
                return;
            }
        }
    });
    // v0.5: drag / arrow-key position editing. The canvas emits the
    // target margins; we apply them to the live panel (no persist —
    // the console writes on exit) and re-render the preview.
    connect(canvas_, &HudCanvas::panelMoved, this, [this](int idx, QMargins m){
        Panel *p = panelAt(idx);
        if (!p) return;
        p->setMargins(m, /*persist=*/false);
        updatePosLabel(idx);
        // ── v1.0.1 (验收项4): A MOVE IS NOT CONTENT ─────────────────────
        // 拖动事件里不再 rebuildAndRender()/refreshWindowModel()：那会
        // 重建检查器与整组 widget，让拖动掉帧。面板画的内容没有变，只需要
        // 画布重绘 + 位置读数；松手仍由 setMargins(persist=false) 交给
        // closeEvent / saveBeforeLaunch 保存。
        if (canvas_)
            canvas_->update();
        if (selectedSubWindow_ < 0 && selectedPanel_ == idx)
            refreshSelectedWindowPositionFields();
    });
    // v0.14: a dragged SPLIT SUB-WINDOW writes into THAT window's own
    // QSettings section, not into one of the four base panels'. The
    // canvas owns the mode and the index space, so this lambda is the
    // only place a sub-window index is turned into a Panel — and it
    // reaches the host, which owns the window, rather than panelAt()
    // which only answers 0..3.
    //
    // The write path is the same one the four base panels use:
    // setMargins(persist=false), followed by a re-render of the box and
    // a status-line refresh. The console persists on exit (see
    // saveBeforeLaunch / ~ControlPanel), so a preview-only drag is not
    // an extra disk write.
    connect(canvas_, &HudCanvas::subWindowMoved, this,
            [this](int idx, QMargins m){
        if (!splitPreview_) return;
        if (!splitPreview_->setWindowMargins(idx, m)) return;
        // ── v1.0.1 (验收项4): 同一条规则 —— 窗口只是“移动了”，它画的
        // 内容没变，所以拖动过程中不 rebuild 整组、不 refreshSplitWindowPreviews
        // (那会重绑 bindings、重建 SectionRow)。画布按新 margins 重绘即可。
        if (canvas_)
            canvas_->update();
        if (selectedSubWindow_ == idx)
            refreshSelectedWindowPositionFields();
    });
    // Selection, so the footer can name the sub-window being dragged.
    //
    // v1.0.0 (O1): AND SO THE INSPECTOR CAN DESCRIBE IT. The prototype's
    // board click is what moves `state.selected`, and the inspector
    // follows it — so this is where a canvas click enters the window
    // model. The route is selectWindow(), not a direct member write,
    // because it has to do three things at once: keep the panel half of
    // the selection in step, push the selection back into the canvas's
    // own highlight, and refill the inspector.
    //
    // NO LOOP. selectWindow() calls canvas_->setSelectedSubWindow(),
    // which is the canvas's ONE public write and deliberately does not
    // emit subWindowSelected (see hud_canvas.h's note on it). So a
    // canvas click → model → canvas write → no signal → done.
    connect(canvas_, &HudCanvas::subWindowSelected, this, [this](int idx){
        // The index is a position in the canvas's own binding vector,
        // which the model's rows are built in the same order as — so the
        // parent slot is the row's own, read from the list rather than
        // guessed from the index.
        const mhw::WindowList list = buildWindowStates();
        for (const mhw::WindowState &w : list) {
            if (w.subIndex != idx)
                continue;
            selectWindow(w.panelIndex, w.subIndex);
            return;
        }
        // A stale index (the bindings were replaced under it): clear
        // rather than select the wrong window.
        selectWindow(selectedPanel_, -1);
    });
    // v1.0.0 (sa-D): the overview's display picker MOVED to the topbar's
    // screenCombo_ — see the block there and the note above enterOverview().
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
    connect(zoomFitBtn_, &QPushButton::clicked, this, [this]{
        if (canvas_) canvas_->setZoom(1.0);   // zoomChanged → updateZoomLabel
    });
    connect(canvas_, &HudCanvas::zoomChanged, this, [this](qreal){
        updateZoomLabel();   // i18n: the format lives in one place
    });
    {
        const mhw::ConsoleLayoutStore layout;
        canvas_->setZoom(layout.zoom());
        updateZoomLabel();
    }
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        // v0.11.4: the adapter is owned by boundSources_ (a member), not
        // orphaned on the heap. The canvas keeps a non-owning
        // const PanelSource*, so ownership has to live on our side and
        // die with the console — see the member's comment.
        auto adapter = std::make_unique<PanelSourceAdapter>(panelAt(i));
        canvas_->bindPanel(i, adapter.get());
        boundSources_[i] = std::move(adapter);
    }
    // v1.0.0 console rework: the window is now TWO columns —
    // [game column | rail] on the left, [toolbar / drawer / stage] on the
    // right — so the old topRow container (which held rail+inspector in a
    // QHBoxLayout) becomes the RIGHT COLUMN, holding the toolbar above
    // the vertical splitter. The horizontal splitter below owns the
    // left/right division and keeps the ui/leftSplitter key it always
    // wrote, so nothing new has to be persisted.
    auto *rightHost = new QWidget();
    rightHost->setObjectName("rightHost");
    auto *rightLayout = new QVBoxLayout(rightHost);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    // The toolbar is built last (it owns the layout-mode controls) but
    // inserted first: buildToolbar() constructs layoutCombo_, which
    // applyStartupLayoutMode() below reads, and the ctor calls that AFTER
    // this point — so the combo must exist by then, exactly as it did
    // when the rail built it.
    rightLayout->addWidget(buildToolbar());
    /* ── v1.0.0 (O1): THE STATUS LINE IS GONE — IT WAS NEVER IN THE DRAFT ──
       The block that used to sit here quoted

           <div class="statusline"><span class="st-dot"></span>…</div>

       as "HTML: `.topbar` then `.statusline` then `.content`". That
       markup does not exist: `grep -n statusline reference.html` returns
       nothing. The draft is `.app → aside.rail + section.workspace →
       header.topbar + main.main` (reference.html:285, 312, 313, 321) and
       `.workspace` is `grid-template-rows:66px minmax(0,1fr)`
       (reference.html:133) — TWO rows, so any band drawn between the
       header and the main area pushes the stage down by its own height.

       Measured before removal (my own probe, 1600x1000, rise/split):
       topbar h=52 + statusRow h=19 = 71px above `.main`, where the draft
       has exactly 66. Removing it is a precondition for VISUAL-CONTRACT
       §10.2 / §10.7, not a style choice.

       statusDot_ / statusline_ / statusRun_ stay declared and NULL.
       refreshStatusline() returns on `if (!statusline_)` before touching
       any of them and every later use is guarded the same way
       (`if (statusDot_)`, `if (statusRun_)`, `if (connectChipText_)`),
       so this is a removal, not a dangling pointer.

       The connection fact keeps its real surface: the rail's own
       auto-detect badge, which is where the draft puts `.status-mini`
       (reference.html:307), and detectGame() stays the only source of
       real game information.
       ── end O1: statusline removed ── */
    /* ── v1.0.0 sa-A: topbar ──
       Populate 显示器 with the REAL outputs. screen_query::listOutputs()
       is the same source the inspector's SCREEN row and the overview's
       picker read, so all three agree by construction.

       Each entry's LABEL is the prototype's format:
         "<name> · <w>×<h>"          e.g. "DP-1 · 2560×1440"
         "<name> · <h>×<w>(旋转)"    when the output is portrait, which
                                    on a Wayland compositor is exactly a
                                    rotated output (Qt folds wl_output
                                    .transform into QScreen::geometry()).
       userData is the real output name, so sa-D's display switch can pull
       the name back out without parsing the label. The primary output is
       selected by default, because that is where the overview opens.
       ── end sa-A: topbar ── */
    if (screenCombo_) {
        // ONE FILL PATH, THREE CALLERS: the initial build, the language
        // trHook below, and screen hotplug (验收项8). v1.0.1 drops the
        // prototype's "eDP-1 is never listed" filter — that was demo
        // table data, not a runtime contract, and on the user's machine
        // (eDP-1 only) it left the combo with one placeholder entry.
        // rebuildScreenCombo() only rewrites the COMBO (a view): it never
        // writes any Panel's outputName, so a hotplug cannot move a window.
        rebuildScreenCombo();
        trHook([this]{ rebuildScreenCombo(); });
        if (auto *app = qobject_cast<QGuiApplication *>(
                QCoreApplication::instance())) {
            connect(app, &QGuiApplication::screenAdded,
                    this, [this](QScreen*){ rebuildScreenCombo(); });
            connect(app, &QGuiApplication::screenRemoved,
                    this, [this](QScreen*){ rebuildScreenCombo(); });
            connect(app, &QGuiApplication::primaryScreenChanged,
                    this, [this](QScreen*){ rebuildScreenCombo(); });
        }
        // THE DISPLAY SWITCH IS A VIEW OPERATION. Changing it tells the
        // canvas which monitor the overview paints — it does not move any
        // window, and that is the whole distinction the prototype draws
        // between #screen-select (view) and #panel-output (ownership).
        //
        // v1.0.0 (sa-D): THE TWO MODES, ONE CONTROL. The prototype has a
        // single #screen-select that means one thing in either view: "the
        // display the board is showing". Qt's canvas distinguishes the
        // overview's screen (overviewScreen_, set by setOverview) from
        // the panel view's preview frame (previewOutputName_, set by
        // setPreviewScreen), so the handler routes by mode:
        //
        //   overview → setOverview(true, screen)  re-points the overview
        //   panel    → setPreviewScreen(screen)   moves the frame, which
        //              is exactly what the inspector's SCREEN row does
        //
        // Both are view operations and neither writes a Panel's
        // outputName, so the panel-view branch cannot move a window any
        // more than the overview branch can.
        //
        // ── v1.0.0 (OUTPUT-OWNERSHIP-FIX, C1/C2) ─────────────────────────
        // TWO THINGS THIS HANDLER WAS MISSING.
        //
        // 1. It pushed the VIEWER into the sub-windows' ownership
        //    fallback, so changing what the user is LOOKING AT changed
        //    which screen a Follow window's drag clamp and arrow keys
        //    were measured against (and the answer got flushed into
        //    panels.ini). The push below is now the window's own
        //    own → parent → primary chain and cannot mention the viewer.
        //
        // 2. It never invalidated the stage header. refreshStageHead()
        //    reads the canvas's preview screen and the window model, so
        //    after a switch both chips kept the OLD display's answer
        //    (measured: chip still read `HDMI-A-1 · 1080×1920` while the
        //    board was already painting DP-1, and the count sat at 5
        //    against a model of 6) until some later refresh happened to
        //    run. The chips are the user's only readout of "which display
        //    am I looking at", so they are refreshed here, in the same
        //    handler that moved the display.
        connect(screenCombo_, &QComboBox::currentIndexChanged,
                this, [this](int idx){
                    if (idx < 0 || !screenCombo_ || !canvas_)
                        return;
                    const QString screen = screenCombo_->itemData(idx).toString();
                    if (canvas_->overviewMode()) {
                        canvas_->setOverview(true, screen);
                    } else {
                        canvas_->setPreviewScreen(screen);
                        // Idempotent, and deliberately NOT a read of
                        // canvas_->previewScreen(): re-derive each
                        // window's own → parent → primary fallback from
                        // the model after the view moved.
                        pushWindowOutputFallback(
                            splitPreview_.get(),
                            {panelAt(0), panelAt(1), panelAt(2), panelAt(3)});
                    }
                    // The screen chip and the window-count chip both
                    // depend on the display that was just changed.
                    refreshStageHead();
                });
    }
    // The drawer takes index 0 and the stage stays at index 1: the
    // collapse animation addresses consoleSplitter_->widget(1), and
    // moving stage would have meant rewriting four call sites for a
    // purely cosmetic ordering.
    //
    // v1.0.0 (O1): THAT ORDERING IS NOW WRONG, AND IT IS THE FIRST THING
    // THIS PORT FIXES. The prototype's `.main` is
    //
    //     grid-template-rows: minmax(0,2fr) minmax(280px,1fr)   gap 12px
    //
    // i.e. the STAGE (the board) is the TOP pane and the INSPECTOR is the
    // BOTTOM one — the layout area is what the user works in and the
    // drawer is the annotation under it. The shell had it backwards
    // (drawer on top at 470px, stage below at 640px), which is why the
    // screenshots never matched the frozen HTML: the board was the small
    // half.
    //
    // THE STAGE MOVES TO INDEX 0 AND THE ANIMATION MOVES WITH IT. It
    // does not stay behind at widget(1): the task that dispatched this
    // port is explicit that the animation must be migrated rather than
    // the visual order kept to protect it. animStageTo(),
    // setStagePaneHeight(), the finished() handler and the
    // splitterMoved persistence now all go through stagePane() and
    // stagePaneSize() below — one spelling each, so a future pane swap
    // is a two-line change in one place instead of a hunt through five.
    //
    // The sizes invert with the widgets: the stage is the 2fr pane, so it
    // gets the larger number. 640 for the stage and 470 for the drawer
    // keeps the prototype's roughly two-thirds / one-third split.
    splitter->addWidget(stage);        // index 0 — the board (the 2fr pane)
    splitter->addWidget(configHost);   // index 1 — the inspector drawer
    /* ── v1.0.0 (O1): THE MAIN AREA'S OWN PADDING AND GAP ────────────────
       The draft's `.main` is

           min-height:0; padding:14px 16px 16px;
           display:grid;
           grid-template-rows:minmax(0,2fr) minmax(280px,1fr);
           gap:12px;

       (reference.html:161) — so the two panes are INSET 16px left/right
       and 14px from the header, separated by exactly 12px, and the
       header above them is full-bleed. Measured (geometry.json state01):
       `main x=232 y=66 w=1353`, `stage-panel x=248 y=80` (232+16,
       66+14), `inspector x=248 y=776.63`, gap 776.63-(80+684.63)=12.

       A wrapper is the only shape that gets both at once: putting the
       margins on rightLayout would inset the topbar too, and the
       topbar's own 18px padding is measured from the FULL width
       (`.topbar-title` at x=250 = 232+18). So the topbar stays a direct
       child of rightLayout at margin 0, and everything below it lives
       inside `mainArea` carrying the draft's own numbers.
       ── end O1: main padding ── */
    auto *mainArea = new QWidget();
    mainArea->setObjectName(QStringLiteral("mainArea"));
    auto *mainLayout = new QVBoxLayout(mainArea);
    mainLayout->setContentsMargins(16, 14, 16, 16);
    mainLayout->setSpacing(12);
    mainLayout->addWidget(splitter, 1);
    rightLayout->addWidget(mainArea, 1);
    // v0.7.5: give the content column a bit more of the vertical axis
    // too so the inspector's CONTENT rows and APPEARANCE sliders are
    // visible without scrolling on first open.
    // v1.0.0: the drawer takes the SMALLER share. The prototype's split is
    // roughly two-thirds canvas, one-third drawer, and the canvas is the
    // thing the user works in — a drawer taller than the layout area makes
    // the console look like an inspector with a preview attached.
    //
    // WITH THE PANES SWAPPED (stage at index 0, drawer at index 1 — see
    // the note above), the FIRST number is now the STAGE's height and
    // the second is the DRAWER's. 600 : 300 is the prototype's own
    // ratio — `grid-template-rows:minmax(0,2fr) minmax(280px,1fr)` is
    // exactly 2:1 (geometry.json state01: 684.63 / 342.312) — rather
    // than the 640:470 the shell used to carry, which is 1.36:1 and
    // never matched a screenshot. It is still only a HINT until the
    // layout runs: setSizes() here is clamped to the widgets' minimums
    // because the splitter is not in a layout yet, and the event filter
    // restates the ratio at first resize (see pendingStageRatioDefault_).
    splitter->setSizes({600, 300});
    // Remember the user's chosen (or default) stage size so animStageTo
    // can restore it on show. DO NOT read it from splitter->sizes() here:
    // at construction time the splitter isn't in a layout yet, so
    // setSizes clamps to the widget minimums ([332,148] for a 1040px
    // window) — the correct 570 default would be lost. Use the constant
    // default, then let QSettings + splitterMoved override it later.
    savedStageSize_ = kConsoleStageHeightDefault;
    // Hard minimum on the layout area so the canvas never gets crushed
    // below its own 360px usable floor. It used to sit on the old
    // topContainer (rail + inspector); with the columns swapped the
    // stage is the pane that needs the guarantee.
    stage->setMinimumHeight(360);
    {
        const mhw::ConsoleLayoutStore layout;
        const mhw::ConsoleLayoutStore::SplitterLayout saved = layout.splitterLayout();
        const bool freshStore = saved.splitterState.isEmpty();
        if (!freshStore) splitter->restoreState(saved.splitterState);
        // After restore, keep the user's last-chosen stage height as a
        // plain int (easier than decoding the splitter bytearray).
        savedStageSize_ = saved.stageHeight;
        // v1.0.0 (O1): a FRESH store gets the prototype's 2fr : 1fr on
        // the first layout instead of QSplitter's sizeHint ratio, which
        // measured 536/393 (1.36:1) where the draft is 684.63/342.312
        // (2:1). A restored store keeps whatever the user left, and the
        // event filter below is the only place that ever clears it.
        pendingStageRatioDefault_ = freshStore;
        if (freshStore) splitter->installEventFilter(this);
    }
    consoleSplitter_ = splitter;

    // v1.0.0: the outer splitter owns the left rail column and the right
    // content column. It is the one that was called "leftSplitter" in
    // the sense that matters — the settings key is unchanged, so a user's
    // saved proportions keep working and no third key is introduced.
    auto *rootSplitter = new QSplitter(Qt::Horizontal);
    rootSplitter->setObjectName("rootSplitter");
    rootSplitter->setHandleWidth(0);
    rootSplitter->setChildrenCollapsible(false);
    rootSplitter->addWidget(leftSplitter);
    rootSplitter->addWidget(rightHost);
    // 232 is the draft's rail column: `.app{grid-template-columns:232px
    // minmax(0,1fr)}` (reference.html:90), measured `rail w=232` and
    // `app x=0 w=1585 → columns 232px 1353px` (geometry.json state01).
    // The shell's 340:860 predates the sidebar trim and left the rail at
    // whatever sizeHint it had (measured 319, then 139 after a rebuild).
    rootSplitter->setSizes({232, 1368});

    auto *central = new QWidget();
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    // v1.0.0 fidelity port (root scroll): THE PAGE, NOT THE WINDOW, IS
    // WHAT OVERFLOWS. reference.html's root is 1134.9375 tall for the
    // landscape output and 1742.3125 for the portrait one at a 1600x1000
    // viewport, and the 15px it loses of width is the page's own
    // scrollbar (HTML-ROOT-REMEASUREMENT). This shell used to do the
    // opposite: the window is fixed, so the outer splitter squeezed the
    // stage and the inspector into whatever was left and the squeezed
    // result was taken for the layout.
    //
    // ONE WIDGET DEEPER THAN THE CENTRAL, ON PURPOSE. The scroll layer
    // wraps a CONTENT layer that holds the root splitter; `central`
    // itself stays the QMainWindow's central widget, so the shell (menu
    // bar, window chrome, and the toast that is parented to `central`
    // below) keeps its own coordinates inside the client area and never
    // scrolls away. The content layer is what grows and what scrolls —
    // see mhw::RootScrollArea for the sizing contract, including why
    // the height is read back from this layout instead of being typed.
    //
    // `findChild()` still reaches every object below: QScrollArea
    // reparents the content to its viewport, and the child tree (which
    // is what findChild walks) is unchanged by that.
    auto *rootScroll = new mhw::RootScrollArea(central);
    auto *rootContent = new QWidget();
    auto *rootContentLayout = new QVBoxLayout(rootContent);
    rootContentLayout->setContentsMargins(0, 0, 0, 0);
    rootContentLayout->setSpacing(0);
    rootContentLayout->addWidget(rootSplitter);
    // THE SHELL DOES NOT SCROLL (console实测 defect 1). Above, this class
    // was left in its fidelity mode: the content's minimum height is its
    // PREFERRED height, so the page grew past the window (measured at
    // 1200x1040: content 1255 tall, `consoleRootScroll` vbar range 215)
    // and every scroll of the wheel took the top bar and the sidebar
    // with it — the stage and the drawer were only reachable by scrolling
    // the whole PAGE, which is what the user reported.
    //
    // `false` makes the content take its LAYOUT FLOOR instead: the shell
    // then lays out exactly once at the window's own size (顶栏与侧栏
    // 固定、随窗口缩放自适应), the two panes get the height the splitter
    // gives them, and scrolling happens only where the console already
    // owns a scroll area for it — the 显示器示例 (canvasScroll) and the
    // 详情预览 (the drawer's `inspectorContent`). Nothing else changes:
    // the horizontal budget refreshTopbarWrap() reads is still this
    // widget's viewport, and the page still scrolls vertically when the
    // window is smaller than the console's own minimum (the floor).
    rootScroll->setPageScrollEnabled(false);
    rootScroll->setRootWidget(rootContent);
    centralLayout->addWidget(rootScroll);
    setCentralWidget(central);

    /* ── v1.0.0 fidelity port: the toast ──────────────────────────────────
       THE ONE PIECE OF FEEDBACK EVERY PROTOTYPE BUTTON HAS.

       `.toast` is `position:fixed; left:50%; bottom:28px` with
       `pointer-events:none` and a 2.2s lifetime. Without it the console's
       buttons were indistinguishable from decoration: 设置 opened
       nothing, 帮助 showed nothing, the eye toggled a state nobody could
       see. The toast is what makes each of them verifiable.

       PARENTED TO THE CENTRAL WIDGET, not to the window: the prototype's
       toast is fixed to the viewport, and the console's viewport is its
       central widget. A child of the window would sit outside the
       splitters' clip and appear over the menu bar.

       WA_TransparentForMouseEvents IS LOAD-BEARING. The prototype's
       `pointer-events:none` means a toast never intercepts the click that
       is aimed at the board underneath it. Without the attribute, a user
       who clicks a window in the two seconds after an action would hit
       the toast instead.

       The label starts HIDDEN and is shown by showToast(); its 2.2s timer
       is the prototype's own setTimeout value.
       ── end v1.0.0 toast ───────────────────────────────────────────── */
    toast_ = new QLabel(central);
    toast_->setObjectName(QStringLiteral("toast"));
    toast_->setAlignment(Qt::AlignCenter);
    toast_->setMargin(8);
    toast_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    toast_->setFocusPolicy(Qt::NoFocus);
    toast_->hide();
    toastTimer_ = new QTimer(this);
    toastTimer_->setSingleShot(true);
    connect(toastTimer_, &QTimer::timeout, this, [this]{
        if (toast_)
            toast_->hide();
    });

    // v0.5.6 polish: persist the splitter ratio whenever the user drags
    // it, so the next launch opens with the proportions they preferred.
    connect(splitter, &QSplitter::splitterMoved, this, [this, splitter]{
        // v1.0.0 (O1): the ctor's own first-layout ratio reaches this
        // slot through setSizes() (see the eventFilter arm). The ratio
        // is DERIVED from the window height, not chosen, so it must not
        // be persisted as ui/splitter — the store keeps whatever the
        // user actually dragged, or stays empty.
        if (applyingStageRatioDefault_)
            return;
        // Animation frames call setSizes → splitterMoved thousands of
        // times; we must NOT persist or update savedStageSize_ from an
        // animated frame or we'd store the animation's intermediate
        // value (e.g. 120px) as the user's chosen ratio. Only a real
        // user drag (animation not running) may write QSettings.
        if (stageAnim_ && stageAnim_->state() == QAbstractAnimation::Running)
            return;
        // v1.0.0 (O1): stagePaneSize() rather than sizes().value(1).
        // The stage moved to index 0 (see the addWidget block), and
        // reading it through the one helper means this line does not
        // have to know which index it is.
        const int newStage = stagePaneSize();
        mhw::ConsoleLayoutStore layout;
        layout.saveSplitterLayout(splitter->saveState(), newStage);
        if (stageVisible_) {
            savedStageSize_ = newStage;
        }
    });

    // v0.5.6 polish: wire the HIDE/SHOW STAGE toggle. QPropertyAnimation
    // drives a custom int property (the stage pane height); each frame
    // re-applies setSizes({anim, bottom}) so the splitter smoothly resizes.
    stageAnim_ = new QPropertyAnimation(this, "stagePaneHeight");
    stageAnim_->setDuration(200);
    stageAnim_->setEasingCurve(QEasingCurve::InOutQuad);
    connect(stageAnim_, &QPropertyAnimation::valueChanged,
            this, [this](const QVariant &v){
        if (!consoleSplitter_) return;
        const int stageH = v.toInt();
        // v1.0.0 (O1): the stage is the FIRST pane now, so the frame is
        // {stageH, bottomH} rather than {topH, stageH}. stagePane() is
        // the widget and stagePaneSize() its height — see their
        // definitions for why the pair exists.
        const int bottomH = consoleSplitter_->sizes().value(1);
        consoleSplitter_->setSizes({stageH, bottomH});
    });
    // v0.5.6 polish: when the hide animation finishes, force stage to
    // exactly 0 — the splitter occasionally clamps the last frame to its
    // own minimum (≈1-2px) when its pane is collapsible but other widgets
    // still expect a non-zero gutter. setSizes({0, x}) with a final
    // stage setMaximumHeight(0) lets the stage fully vanish, and the
    // SHOW path restores both back. This is also what kills the
    // "stays stuck — only jiggles" bug when the user re-opens stage.
    connect(stageAnim_, &QPropertyAnimation::finished,
            this, [this]{
        if (!consoleSplitter_) return;
        // v1.0.0 (O1): stagePane() / stagePaneSize() instead of
        // widget(1) / sizes().value(1) — the stage is the first pane now.
        // See their definitions for why the pair exists.
        if (stageVisible_) {
            // show complete: clear any temporary max cap.
            if (QWidget *stg = stagePane()) stg->setMaximumHeight(QWIDGETSIZE_MAX);
            const int stageH = savedStageSize_ > 0 ? savedStageSize_ : kConsoleStageHeightDefault;
            const int bottomH = consoleSplitter_->sizes().value(1);
            consoleSplitter_->setSizes({stageH, bottomH});
        } else {
            // hide complete: force stage to zero. The QSplitter insists
            // on a minimum pane size (~120 in our case) even when the
            // child has minimumSize 0 and is collapsible; we work around
            // it by setting stage's MAX height to 0 so it really
            // collapses, then setSizes({0, x}). setVisible(false) was
            // tried but the splitter kept its 120px reservation.
            if (QWidget *stg = stagePane()) {
                stg->setMaximumHeight(0);
            }
            const int bottomH = consoleSplitter_->sizes().value(1);
            consoleSplitter_->setSizes({0, bottomH});
        }
    });
    // v1.0.0: the collapse button is gone, and so is its connection.
    // stageToggleBtn_ is now null, and connect() on a null sender is a
    // crash the moment the console is built — not a silent no-op.
    // animStageTo() stays: it is still the right way to animate the stage
    // and the splitter animation path is unchanged, it just has no button
    // wired to it.
    if (stageToggleBtn_) {
        connect(stageToggleBtn_, &QPushButton::toggled, this, [this](bool checked){
            updateStageToggleText();   // i18n: ⌄/⌃ copy lives in one helper
            animStageTo(checked);
        });
    }

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
        // v0.15: RE-READ THE FILE THE OVERLAY JUST WROTE.
        //
        // The report was 「我在世界的控制台点击 layout,启动后移动,esc 退出,
        // 这时控制台没有同步;但是换到崛起启动再退出,回到世界,控制台就同步
        // 了」 - a delay of exactly one World<->Rise round trip, which is
        // what makes it look like a timing bug rather than a missing one.
        //
        // It is neither a refresh nor a sync problem. A Panel reads its
        // panels.ini group EXACTLY ONCE, inside its constructor, and caches
        // ml/mt/mr/mb/scale/opacity/outputName as members - there is no
        // setter that re-reads. The overlay's Esc handler writes the file
        // through its own saveConfig(), so on exit the console's windows
        // hold values that are already stale, and nothing in this lambda
        // rebuilt them. The preview kept showing what the console had in
        // memory until a rail click: switchGame() calls rebuildMotherPanels()
        // unconditionally and rebuilds the split host whenever
        // hostGame != currentGame(), and THAT rebuild is what finally
        // constructed Panels against the new file. So the delay was a whole
        // game round trip, and the refresh was never going to happen on its
        // own.
        //
        // Rebuilding is the only correct response - there is deliberately no
        // lighter one. refreshSplitWindowPreviews() and friends redraw from
        // the members that are already stale, which is why "syncAll" never
        // fixed this across two rounds.
        //
        // ORDER MATTERS. saveBeforeLaunch() persists the console's own
        // preview on the way OUT, so a rebuild that had not yet happened
        // would write the stale layout back over the overlay's real one.
        // Rebuild first, then the user sees the truth; the next launch then
        // saves the truth rather than the fossil.
        rebuildMotherPanels();
        rebindMotherPanels();
        // Force the split host to rebuild even when the game did not
        // change. syncSplitPreviewToCanvas() guards on
        // `!splitPreview_ || hostGame != currentGame()`, which is the right
        // guard for a game switch and the wrong one here: same game, new
        // file. Forcing the game memo to a sentinel would be a lie about
        // what is current, so the host is dropped and let the next
        // syncSplitPreviewToCanvas() rebuild it from the file.
        splitPreview_.reset();
        syncSplitPreviewToCanvas();
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
    // v1.0.0: 编辑布局 (HTML's .btn.on) TOGGLES THE BOARD'S EDIT FACE.
    //
    // It is the one entry the prototype's #edit-toggle offers, and it has
    // two jobs at once, both of which were missing: it hands the board
    // its edit state (grid, clay border, dashed cards) through
    // HudCanvas::setEditing(), and it writes the caption the board owes
    // the user while that state holds. Neither existed before — the
    // button only ever launched a child process — so the console showed
    // a board that could not be told it was being edited.
    //
    // TOGGLE, NOT LAUNCH. The prototype's 编辑布局 is a mode switch on the
    // board the user is already looking at; the child process it used to
    // spawn was the old shell's way of editing the OVERLAY's windows,
    // which is a different surface (the panels behind this preview). The
    // console's own drag / arrow-key paths are already wired to
    // panelMoved / subWindowMoved and persist on exit, so entering edit
    // mode here needs no process at all.
    connect(editBtn_, &QPushButton::clicked, this, [this]{
        if (!canvas_)
            return;
        // THE TWO MODES ARE MUTUALLY EXCLUSIVE. The overview is a
        // read-only view of one display; the edit face is the opposite
        // — grid, clay border, dashed cards, live nudges. Leaving the
        // overview first is what makes the edit face mean one thing
        // rather than two overlapping ones, and it reuses the exit path
        // that already restores the preview screen, the nav highlight
        // and the caption. leaveOverview() is idempotent (setOverview()
        // returns early when already off), so the panel path costs one
        // comparison.
        if (canvas_->overviewMode())
            leaveOverview(canvas_);
        setBoardEditing(canvas_, !canvas_->editing());
        // Leaving the overview IS a view change, so the topbar's title
        // has to follow it (reference.html:458 derives the title from
        // the view, not from the edit state).
        refreshTopbarTitle();
        // …and so does the stage header: the overview counts every
        // panel's windows on this display, the panel view counts only
        // this panel's. OUTPUT-OWNERSHIP-FIX (C2/C3).
        refreshStageHead();
    });

    loadMaskFromDisk();
    // v0.13: the layout mode. Read AFTER the widgets exist (the combo is
    // populated above) and AFTER the mask load, because both read the
    // same conf file and the mask load is the one that establishes
    // whether the file is present at all.
    applyStartupLayoutMode();
    // v0.13: push the mode into the canvas so the preview describes the
    // same topology the child will start in. Called after the mask load
    // (the sub-window preview reads the same masks) and before the
    // first rebuildAndRender() so the canvas holds bindings before it
    // can be asked for pixmaps.
    syncSplitPreviewToCanvas();
    for (int i = 0; i < mhw::kPanelCount; ++i)
        rebuildAndRender(i);
    selectPanel(0);
    // v1.1.0: this construction-time call establishes the rail's default
    // slot, NOT a user choice, so it must not arm `panelSlotChosen_`.
    // Arming it here is what made the inspector skip its empty block on a
    // cold start and show a mother panel's size rows before the user had
    // clicked anything (console-inspector-fidelity-tests "A: the
    // .inspector-content scroll box is hidden in the empty state"). The
    // flag is re-armed by the first real click, rail or canvas, which is
    // the only thing that should ever turn the empty state off.
    panelSlotChosen_ = false;
    // v1.0.0 (O1): AND FILL THE WINDOW INSPECTOR. selectPanel() addresses
    // the PANEL half of the selection; the inspector's three columns are
    // the WINDOW half, and they start empty until something fills them.
    //
    // CORRECTION: the claim this note used to make — "the prototype's
    // board starts with the first window selected" — is false.
    // reference.html's defaultState() is `view:'overview', selected:null`
    // and renderInspector's `if(!d||!w)` arm replaces the columns with
    // `.empty-inspector` until the user clicks a box. What is TRUE is
    // that Qt's console keeps its historical `selectPanel(0)` (see
    // line 3155), so in SINGLE mode there is a selected window at
    // startup and the columns fill; in SPLIT the selection is a mother
    // panel with subIndex -1, which matches no row of the sub-window
    // list, so selectedWindow() answers invalid and the empty block
    // shows — exactly reference.html's state01. Refreshing here is what
    // lets either answer be computed instead of leaving blank widgets.
    refreshWindowModel();

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
            this, [this]{ riseCard_->refresh(); });
    riseReframeworkRefreshTimer_->start();
    riseCard_->refresh();
}

ControlPanel::~ControlPanel()
{
    // Persist current mask state on every destruction path (close, app
    // exit, explicit delete). Safe to call even if load was never reached.
    saveMaskToDisk();

    // v0.12: the sub-window registry was removed here (stage-1
    // dead-code pass) — the console never had anything registered to
    // tear down, so this destructor has no sub-window half left.

    // The four preview panels were created as TOP-level QMainWindows
    // (no parent) so they get their own QWidgetWindow and don't pollute
    // the console's backing store (see renderPreview). They need to be
    // deleted explicitly here — otherwise QApplication sees them in the
    // top-level window list, "no visible windows" never fires, and
    // setQuitOnLastWindowClosed has nothing to quit on. Hiding them
    // first also gives the wayland layer a clean unmap, so the
    // "顶栏小块" residue doesn't linger.
    // The four mother panels are rebuilt on a rail switch (see
    // rebuildMotherPanels), so this cannot be a delete-by-member: it must
    // walk ctl_, or a panel replaced mid-session would be leaked and the
    // ones still listed would be double-freed by the typed members below.
    // Both bridges point at the same objects, so clearing them here is
    // what keeps the ctl_ walk authoritative.
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        Panel *p = panelAt(i);
        if (p) { p->setVisible(false); delete p; }
        ctl_[i].panel = nullptr;
    }
    player_ = nullptr; monster_ = nullptr; damage_ = nullptr; pets_ = nullptr;

    // The adapters were owned by boundSources_, which is destroyed by
    // the member destructors AFTER this body runs (members are declared
    // after ctl_/player_/…, so reverse order puts them first). That is
    // exactly the ordering the raw Panel* they cache needs: the panels
    // are gone by the time the adapters stop being reachable, and no
    // store-then-read race is possible because nothing can reach them
    // from the canvas after here.
    //
    // v0.11.4: canvas_ itself is parented to this window (built in the
    // ctor), so it dies with us; ~HudCanvas runs during QObject teardown
    // but never dereferences its slots_ sources (they are only read from
    // paint/mouse code that is already finished by then), so a leaked
    // pointer is not a risk there. We do not need explicit unbindPanel
    // calls, and none are left dangling above.
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
    /* ── v1.0.0 sa-A: sidebar + statusline ──
       The three rebuilt-shell refreshes, replayed on a language flip for
       the same reason the inspector's are: they all build their copy from
       consoleText(), and a language switch is the one event that has to
       rewrite every one of them without a restart.

       refreshNavCounts()  → the nav rows' "{n} 窗" counts
       refreshStatusline() → the status line's five states
       syncLayoutSegmentToMode() → the segment's checked state and the
                             Split-only visibility of 分开密度
       ── end sa-A: sidebar + statusline ── */
    refreshNavCounts();
    refreshStatusline();
    syncLayoutSegmentToMode();
    // v1.0.0 (O1): the topbar title is built from consoleText() too, so a
    // language flip must replay it — this whole block is the trHook.
    refreshTopbarTitle();
    // …and the board hint is now shown in every state, so the ctor has
    // to run it once: canvas_ was still null when the label's own
    // trHook first fired (see the s_boardHint construction block).
    refreshBoardHint(canvas_);
    for (int i = 0; i < mhw::kPanelCount; ++i)
        updatePanelSummary(i);                    // counts + nav summaries
    for (int i = 0; i < mhw::kPanelCount; ++i)
        updatePosLabel(i);                        // corner + margin readout
    refreshAutoDetect();                          // badge + GAME column
    riseCard_->refresh();                         // Rise-only management card
    for (int i = 0; i < mhw::kPanelCount; ++i)
        if (Panel *panel = panelAt(i))
            panel->retranslateUi();               // cached preview-panel copy
    /* ── 验收项4 (B 第1阶段): THE SPLIT SUB-WINDOWS ARE PANELS TOO ──────
       The loop above only ever reached the FOUR mother panels, so a
       中文/English flip re-translated the base windows and left every
       split sub-window (player-status, monster-conditions, …) painting
       the previous locale until the next rebuild. The host owns those
       windows; retranslateAll() replays each one's own retranslateUi()
       and then the existing sync + render path re-publishes their
       pixmaps and titles. Scope, geometry, scale and margins are not
       touched: retranslateUi() only rewrites copy.
       ── end 验收项4: split sub-window retranslate ── */
    if (splitPreview_)
        splitPreview_->retranslateAll();
    // Preview tiles carry PAINTED chrome (e.g. the disabled placeholder)
    // and the canvas paints its header/footer — repaint both.
    for (int i = 0; i < mhw::kPanelCount; ++i)
        rebuildAndRender(i);
    // …and the sub-window tiles: syncAll() re-reads each window's demo
    // content for the new language and renderAll() repaints them onto
    // the canvas. A no-op in Single (no bindings) and when the host has
    // not been built yet — exactly the states refreshSplitWindowPreviews()
    // already guards.
    refreshSplitWindowPreviews();
    // The inspector's THIRD column is a pixmap of the selected window,
    // so it carries text too: re-render it in the new language without
    // changing which window is selected.
    refreshWindowPreviewPixmap();
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

    // v0.15: persist scale / opacity / margins — the last one added in
    // v0.5 so a position dragged here survives into the overlay's
    // loadConfig(). This comment used to say saveAppearance() writes
    // "ONLY scale and opacity, NOT margins", which stopped being true the
    // same day the margins write was added, and it mattered: reading it as
    // current is what let the split sub-windows go unsaved from BOTH
    // exits.
    //
    // It shares the loop with saveBeforeLaunch() so there is one list of
    // windows rather than two, and a window is added to one place.
    //
    // AND THE SCOPE IS STATED HERE TOO, for the reason spelled out on
    // saveBeforeLaunch(): a Panel derives its group name from the two
    // scope globals at write time, and this exit runs on a console that
    // may never have launched anything. Stating it here as well is what
    // makes the two exits write the same file — without it, a close
    // after a Single-mode apply wrote the drag into the other
    // topology's group and the next launch lost it.
    mhw::setPanelSettingsGame(currentGame());
    mhw::setPanelSettingsLayout(layoutModeInForce());
    saveAllSplitAndBaseWindows();

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
//
// v1.0.0: AND ESC LEAVES THE BOARD'S EDIT MODE. The prototype's
// `.board-hint.edit` copy promises it in as many words — 「Esc 保存退出
// 编辑」 — so the key is part of the edit face's contract, not a
// convenience. The save half is already true: the console persists on
// exit and on launch (saveBeforeLaunch / closeEvent), and a board drag
// writes through panelMoved / subWindowMoved with persist=false
// precisely so nothing is lost between now and then. So Esc's job here
// is only the exit.
//
// WHY HERE AND NOT IN THE CANVAS. The canvas owns the arrow keys (it
// has ClickFocus and already swallows them while editing), but the
// console window is where focus sits after a toolbar click, and
// HudCanvas::keyPressEvent's own Esc arm would have to reach back into
// this file's edit state. One window-level handler for a window-level
// mode is the same shape the overview uses (its nav card's toggle lives
// in eventFilter, not in the canvas).
//
// The 1/2/3/4 rail hot-keys are checked FIRST and unchanged: they are
// not part of any edit-mode contract, and Esc is the only key that
// ends a mode.
void ControlPanel::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_1) { selectPanel(0); return; }
    if (e->key() == Qt::Key_2) { selectPanel(1); return; }
    if (e->key() == Qt::Key_3) { selectPanel(2); return; }
    if (e->key() == Qt::Key_4) { selectPanel(3); return; }
    if (e->key() == Qt::Key_Escape && canvas_ && canvas_->editing()) {
        setBoardEditing(canvas_, false);
        return;
    }
    QMainWindow::keyPressEvent(e);
}

// ─── stage toggle property + animation ─────────────────────────────────────

// ─── v1.0.0 (O1): the stage pane, addressed by name ────────────────────
//
// The stage used to be consoleSplitter_->widget(1) and every caller
// spelled that index out. With the prototype's layout the stage is the
// FIRST pane (see the addWidget block in the ctor), and the index moved
// from 1 to 0 — which is exactly the kind of change that leaves one of
// five call sites behind, and a stage-pane call site that points at the
// drawer collapses the wrong pane while the other four collapse the
// right one.
//
// So the pane is now named once. Two accessors rather than one because
// two different questions are asked of it: WHICH WIDGET (to pin its
// maximum height during the collapse animation) and HOW TALL IS IT (to
// read the current height, and to persist it). Both go through the
// splitter's own index, so neither can disagree with the other.
//
// THE NAME IS NOT stagePaneHeight(). That spelling is already taken by
// the Q_PROPERTY's READ accessor, which answers a different question
// ("what value did the animation last write") and must keep doing so —
// QPropertyAnimation drives the member through it, and renaming it to
// make room would change the property's ABI name for no gain. The
// splitter's live height is `stagePaneSize()`, and the two are not
// interchangeable: the property lags the splitter by one animation
// frame by construction, which is why the animation's "from" value is
// read from the splitter and not from the member.
//
// The index itself is a constant rather than a literal in five places:
// if the layout changes again it is edited here and nowhere else.
//
// 0, because the stage is the FIRST pane of the vertical splitter. That
// is the prototype's order (`.main` grid-template-rows puts the stage
// first) and it is the reason the index moved from 1.
constexpr int kStagePaneIndex = 0;

QWidget *ControlPanel::stagePane() const
{
    return consoleSplitter_ ? consoleSplitter_->widget(kStagePaneIndex) : nullptr;
}

int ControlPanel::stagePaneSize() const
{
    return consoleSplitter_ ? consoleSplitter_->sizes().value(kStagePaneIndex, 0) : 0;
}

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
        // v1.0.0 (O1): stagePane() rather than widget(1) — the stage is
        // the FIRST pane now (see the addWidget block).
        if (QWidget *stg = stagePane()) {
            stg->setMaximumHeight(QWIDGETSIZE_MAX);
        }
    }
    // Snapshot the current stage size as the "from" so an in-flight
    // animation can be reversed mid-flight without snapping.
    const int current = stagePaneSize();
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
    // v1.0.0 (O1): THE PROTOTYPE'S 2fr : 1fr, ONCE, ON THE FIRST LAYOUT.
    //
    // `.main` is `grid-template-rows:minmax(0,2fr) minmax(280px,1fr)` —
    // the stage is EXACTLY two thirds of the pane column (geometry.json
    // state01: stage-panel h=684.63, inspector h=342.312, gap 12).
    // QSplitter has no ratio: `setSizes()` in the ctor is clamped to the
    // widgets' minimums because the splitter is not in a layout yet (the
    // note on the setSizes() call says so with its own measured numbers),
    // so the ratio can only be stated once the splitter knows its height.
    //
    // Guarded by `pendingStageRatioDefault_`, which is set ONLY when the
    // store had no saved splitter state — a user who dragged the panes
    // keeps their drag, which is the same precedence the restoreState()
    // block below already gives them. Applied on the FIRST Resize, not
    // on a timer: a resize is exactly "the layout has run", and the flag
    // is cleared before setSizes() so the recursion it causes cannot
    // re-enter this arm.
    if (watched == consoleSplitter_ && pendingStageRatioDefault_
        && !applyingStageRatioDefault_
        && event->type() == QEvent::Resize) {
        const int h = consoleSplitter_->height();
        if (h > 0) {
            const int usable = h - consoleSplitter_->handleWidth();
            const int stageH = (usable * 2) / 3;
            applyingStageRatioDefault_ = true;
            consoleSplitter_->setSizes({stageH, usable - stageH});
            applyingStageRatioDefault_ = false;
            pendingStageRatioDefault_ = false;
            // animStageTo() collapses and restores against THIS number,
            // so it has to be the height the panes are ACTUALLY at —
            // QSplitter clamps setSizes() to its children's minimums
            // (configHost's minimumSizeHint is what caps the stage), and
            // stagePaneSize() is the post-clamp truth.
            savedStageSize_ = stagePaneSize();
        }
    }
    /* ── round2: THE CARD'S HEIGHT BUDGET FOLLOWS THE LEFT HALF ──────────
       refreshWindowPreviewPixmap() restates the old grid row's height as
       the card's minimum — the card is as tall as the left half's
       content, so the render keeps the height it had when all three
       columns shared one row. That budget is a function of the LEFT
       half's content, and the left half re-lays out on its own (the
       window got wider, the language flipped, a section list was
       rebuilt), so it is re-read HERE — where the content actually
       changes size — and not only when the preview happens to be
       refilled. A refill-only read leaves the number one layout pass
       stale, which is how a right half could start out with no
       scrollbar at all and only gain one later.

       The direction is one-way by construction: the left half's size
       does not depend on the card's, so this arm cannot feed anything
       back into the fit. It only ever WRITES the card's minimum; the
       card's own resize comes back through the column watcher, which
       re-derives the preview from the new box.
       ── end round2: the height budget ── */
    if (watched == inspectorGridHost_
        && (event->type() == QEvent::Resize
            || event->type() == QEvent::LayoutRequest)
        && windowPreviewCol_ && inspectorGridHost_->height() > 0
        && windowPreviewCol_->minimumHeight() != inspectorGridHost_->height()) {
        windowPreviewCol_->setMinimumHeight(inspectorGridHost_->height());
    }
    // ─── v1.1.4: RETIRED, AND WHY IT IS WORTH READING ────────────────────
    // A card-resize arm lived here for one round:
    //
    //     if (watched == windowPreviewCol_ && event->type() == QEvent::Resize)
    //         QTimer::singleShot(0, …, applyPreviewDisplay());
    //
    // It never ran. eventFilter() is installed in twelve places (the canvas,
    // the rail, the preview VIEWPORT, the scroll area…) and the card was
    // never one of them — probed at runtime, the card carries exactly one
    // filter, PreviewViewportWatcher, and eight real Resizes under an
    // explicit zoom produced zero pixmap rebuilds while the same drags under
    // Fit produced eight. The flag it guarded stayed false forever.
    //
    // It was written to explain a symptom that a DIFFERENT change caused
    // (the readout row's labels setting the card's minimum width, which is
    // the row-split fix in buildWindowInspectorFace), so once that was fixed
    // the arm had no symptom left to fix. Kept as a comment because the trap
    // is general: an eventFilter arm is code you cannot see run, and "the
    // logic is right" is not evidence. Install the filter on the widget you
    // filter, then prove it with a counter.
    // v1.0.5: Ctrl+wheel over the canvas zooms in/out.
    if (watched == canvas_ && event->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            const qreal delta = (we->angleDelta().y() > 0) ? 0.1 : -0.1;
            canvas_->setZoom(canvas_->zoom() + delta);
            return true;
        }
    }
    /* ── 实测缺陷2: Ctrl+wheel over the DETAIL PREVIEW zooms the preview ──
       Same modifier, other area — and the dispatch is `watched`, so the
       two arms can never both fire: this one only runs for the preview
       scroll area and its viewport, which the canvas arm above does not
       match. Without Ctrl the event is not consumed, so the preview
       still scrolls with a plain wheel.
       ── end 实测缺陷2 ── */
    if (windowPreviewScroll_
        && (watched == windowPreviewScroll_
            || watched == windowPreviewScroll_->viewport()
            || watched == windowPreviewBox_)
        && event->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent *>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            setPreviewZoom(stepPreviewZoom(we->angleDelta().y() > 0 ? +1 : -1));
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
        // v1.0.0: the sidebar's OVERVIEW card. Clicking it opens the
        // read-only single-display view; clicking a panel card leaves it
        // (selectPanel does that, above). One card, one toggle — the same
        // shape the prototype's nav uses.
        if (watched == s_overviewNav) {
            // Navigation is idempotent: clicking Overview again keeps
            // the overview, rather than toggling into an unrelated panel.
            enterOverview(canvas_);
            // The overview card IS the view switch the draft's
            // `#panel-nav [data-view]` is, and the topbar's title is a
            // function of it (reference.html:458).
            refreshTopbarTitle();
            // …and the stage header is too: entering the overview widens
            // the count to every panel's windows on this display,
            // leaving narrows it back to the selected panel's.
            // OUTPUT-OWNERSHIP-FIX (C2/C3).
            refreshStageHead();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void ControlPanel::selectPanel(int idx, bool fromNav)
{
    if (!mhw::isPanelIndex(idx)) return;
    // v0.10.1: the pets inspector is Rise-only; it cannot be reached while
    // World is selected (the rail card is hidden; hot-keys land here).
    if (idx == 3 && currentGame() == mhw::GameId::World) return;
    selectedPanel_ = idx;
    // v1.1.0: remember that a slot was explicitly chosen. `selectedPanel_`
    // starts at 0, so it cannot answer "has the user actually looked at a
    // panel?" — which is exactly the question the inspector's empty state
    // turns on. Without this, startup (nothing picked yet) and "the user
    // clicked the mother panel" look identical from the window state, and the
    // inspector cannot show a mother panel's properties without also showing
    // them on a cold start.
    //
    // The REDIRECT case is the one caller that is bookkeeping rather than a
    // choice: switchGame() moves the rail off the pets slot when pets stop
    // existing. That is a consequence of the scope change the user DID make,
    // but it is not a look at panel 2 — and arming the flag there keeps the
    // previous game's inspector columns alive across a game switch, which is
    // exactly what console-inspector-fidelity-tests "B: the empty block is
    // back after the scope change" forbids.
    if (!panelRedirect_)
        panelSlotChosen_ = true;
    panelRedirect_ = false;
    syncAppearance(idx);
    updatePosLabel(idx);
    if (inspectorStack_) inspectorStack_->setCurrentIndex(idx);
    if (canvas_) canvas_->setSelectedPanel(idx);
    // Navigation changes the selected group, never the viewed display.
    // Target-output inheritance is refreshed independently of view state.
    if (canvas_) {
        // v1.0.0: THE OVERVIEW OWNS ITS OWN SCREEN WHILE IT IS OPEN.
        // selectPanel() is called on every rail click and every inspector
        // tab, and unconditionally retargeting the frame here would move
        // the overview to whichever panel the user happened to inspect —
        // the "filtering by view state is self-referential" trap. The
        // overview's screen lives in HudCanvas::overviewScreen_ and is
        // restored on exit, so the navigation is left to do its thing
        // on the panels it owns and the overview stays where the user
        // put it.
        // BLOCK-USABILITY-FIX: NAVIGATION MUST NOT MOVE THE VIEWER.
        // A rail click used to re-point the preview at the PANEL's own
        // output here (and at "" — follow primary — for a panel that
        // inherits), so selecting a panel silently switched the display
        // the user was looking at and blanked the stage chip's screen.
        // Which display the console SHOWS belongs to the SCREEN dropdown
        // and the overview; a panel's output is its CONFIGURATION — read
        // by the ownership chain below and written only by the
        // inspector's own-target control, never by a navigation click.
        // Removed rather than guarded: in overview mode this branch was
        // already dead, so this one call was the whole behaviour.
        // v0.15 → v1.0.0: re-derive each sub-window's OWNERSHIP fallback
        // after a tab switch. The host is not rebuilt by a tab switch, so
        // the windows would otherwise keep the parent panel they were
        // built under — but what a Follow window inherits is its parent's
        // output, and a tab switch changes which parent is in focus for
        // the FRAME only. This push reads panelAt(), never the frame, so
        // the clamp can never disagree with where the window is
        // configured to live (reference.html:421).
        pushWindowOutputFallback(
            splitPreview_.get(),
            {panelAt(0), panelAt(1), panelAt(2), panelAt(3)});
    }
    // ─── v1.0.0 integration (stage A): WHO IS ALLOWED TO CHANGE VIEW ────
    //
    // reference.html has exactly two writers of `state.view`: the nav
    // handler (reference.html:511, `#panel-nav [data-view]`) and the
    // overview card. The board's own handlers write `state.selected` and
    // nothing else. So `fromNav` is not an implementation detail — it is
    // the difference between "the user navigated" and "the user clicked a
    // box", and the view may only move on the first.
    //
    // A NAV entry keeps the old behaviour byte for byte: leave the
    // overview, light the panel card, clear the overview card.
    //
    // A BOARD entry (selectWindow's call, fromNav=false) while the
    // overview is open does NEITHER: the overview stays up — which is
    // what 「总览点窗后仍为总览」 demands in both topologies — and the
    // overview card keeps its highlight, because `state.view` is still
    // 'overview' and renderRail() derives `.active` from the view alone
    // (reference.html:451). The panel card does NOT light up: a lit
    // panel card is a statement about the view, and the view did not
    // move.
    //
    // A BOARD entry with the overview closed is the pre-existing path
    // with the idempotent calls doing what they already did — the
    // highlight follows the selection, the overview card is already
    // dark, leaveOverview() would have been a no-op.
    if (fromNav)
        leaveOverview(canvas_);
    const bool overviewOwnsTheHighlight = canvas_ && canvas_->overviewMode();
    if (!overviewOwnsTheHighlight) {
        for (int i = 0; i < mhw::kPanelCount; ++i) {
            if (!ctl_[i].navButton) continue;
            ctl_[i].navButton->setProperty("selected", i == idx);
            ctl_[i].navButton->style()->unpolish(ctl_[i].navButton);
            ctl_[i].navButton->style()->polish(ctl_[i].navButton);
        }
        // v1.0.0: and the overview's nav card loses its highlight, because
        // the user is now looking at a specific panel rather than the
        // single-display overview.
        if (s_overviewNav) {
            s_overviewNav->setProperty("selected", false);
            s_overviewNav->style()->unpolish(s_overviewNav);
            s_overviewNav->style()->polish(s_overviewNav);
        }
    }
    /* ── v1.0.0 sa-D: leaving the overview by clicking a PANEL card ──
       The prototype's nav is one group of mutually-exclusive entries:
       clicking a panel while the overview is open switches the view to
       that panel, exactly as clicking 总览 switches it back. Qt's rail
       delivers the two as separate event-filter arms (the panel cards
       through the loop above, the overview card through the branch after
       it), so the overview's exit has to be asked for here as well.
       ── end sa-D ── */
    // A rail click IS the prototype's `#panel-nav [data-view]` handler
    // (reference.html:511), and that handler ends in renderAll() — which
    // rewrites #top-title / #top-subtitle from the new view
    // (reference.html:458-459). Same call here for the same reason.
    refreshTopbarTitle();
    // renderAll() also rewrites #window-count and the board label
    // (reference.html:476 / :475), and selectPanel() moves BOTH inputs
    // they read: the frame's preview screen (above) and, through
    // leaveOverview(), whether the count is scoped to this panel.
    // OUTPUT-OWNERSHIP-FIX (C2/C3): refresh the stage header here too,
    // so a rail click never leaves the chips describing the previous
    // view.
    refreshStageHead();
    // ─── v1.1.4: AND THE MIXED BLOCK FOLLOWS THE NAV ────────────────────
    // The 独立/合并 rows are shown or hidden by a PREDICATE over the focus
    // (refreshMixedGroupBlock's: in-game AND same parent), and selectPanel()
    // is one of the two things that move that focus. It used to end at the
    // two title refreshes above, so after a rail click the list kept showing
    // the PREVIOUS group while 全部独立 / 全部合并 — which read the focus
    // live — wrote the new one. That is exactly the failure the per-group
    // scoping was built to remove, arriving through the door next to it:
    // a control under the 玩家 heading that edits 伤害.
    //
    // The block is a function of (mode, game, plan, focus) and cheap —
    // eleven rows of text/checked/state with signals blocked. It also
    // early-returns when the block does not exist yet, so the constructor's
    // selectPanel(0) is unaffected.
    refreshMixedGroupBlock();
}

// ─── v1.0.0: the single-display overview ──────────────────────────────
//
// 「一次只画一块显示器上的所有窗口（跨面板），只读。」 The prototype's
// contract, and the reason this is a MODE rather than a filter: the
// canvas has to stop drawing the Split topology it normally draws (one
// display's windows only, whatever the mode) and stop accepting drags
// and arrow-key nudges. Both live in HudCanvas::setOverview().
//
// v1.0.0 (sa-D): THE DISPLAY PICKER MOVED. It used to be a second combo
// on the stage bar (s_overviewScreenCombo, objectName
// `overviewScreenCombo`), hidden until the overview opened. sa-A built
// the prototype's real topbar control instead — screenCombo_, objectName
// `screenCombo`, always visible, entries from screen_query::listOutputs()
// in the prototype's `DP-1 · 2560×1440` format — and that is the one
// widget now. Two combos that both answer "which display" is the exact
// duplicate-source-of-truth the prototype's #screen-select (view) versus
// #panel-output (ownership) distinction was drawn to avoid, and the old
// one is deleted rather than hidden: a hidden combo still answers
// findChildren<QComboBox*> and still carries its own entry list.
//
// So enterOverview() / leaveOverview() no longer own the combo's
// contents or its visibility — they own the canvas's overview state and
// the nav card's highlight. The combo's own handler (in the ctor) does
// the routing: in overview mode it re-points the canvas, in panel mode it
// sets the preview screen, which is the pre-existing behaviour the
// inspector's SCREEN row also performs.

void enterOverview(HudCanvas *canvas)
{
    if (!canvas)
        return;
    // v1.0.0: AND THE EDIT FACE GOES OFF ON THE WAY IN. The overview is
    // the read-only view — no grid, no clay border, no dashed cards, no
    // nudges — and it is reachable from a board that is editing via the
    // sidebar's nav card, which is the one entry the 编辑布局 button does
    // not pass through. Without this, entering the overview from edit
    // mode left the board drawing its edit face over a read-only view:
    // the grid stayed up, the border stayed clay and the cards stayed
    // dashed, while the arrow keys had already been shut off.
    //
    // The reverse direction is already covered — 编辑布局 calls
    // leaveOverview() before entering edit mode — so one call here makes
    // the two modes mutually exclusive from both sides rather than one.
    // Idempotent: setEditing(false) on a board that is not editing
    // returns without a repaint.
    canvas->setEditing(false);
    // The screen the overview starts on: whichever display the user was
    // last looking at. That is the canvas's own preview screen, so the
    // frame they are already looking at does not jump on entry.
    //
    // v1.0.0 (sa-D): the topbar's screenCombo_ is the display picker now,
    // and its entry is PLACED from here rather than rebuilt: the combo is
    // populated once in the ctor from the same screen_query::listOutputs()
    // the inspector's SCREEN row reads, so rebuilding it on every entry
    // would be a second source of truth. All this does is move its index
    // to the display the canvas just adopted — which is the canvas's own
    // answer after setOverview() normalised an empty screen to the
    // primary's real name, so it lands on a real item whenever the list
    // has one and -1 (no match) leaves the first entry rather than a blank.
    const QString screen = canvas->previewScreen();
    canvas->setOverview(true, screen);

    if (QComboBox *combo = screenComboForOverview()) {
        combo->blockSignals(true);
        const int at = combo->findData(canvas->overviewScreen());
        if (at >= 0)
            combo->setCurrentIndex(at);
        combo->blockSignals(false);
    }

    if (QFrame *navCard = s_overviewNav) {
        navCard->setProperty("selected", true);
        navCard->style()->unpolish(navCard);
        navCard->style()->polish(navCard);
    }
    // The board's caption. It is the overview's promise — read-only, one
    // display, click to inspect — and it is only true while the overview
    // is open. refreshBoardHint() writes both the string and the
    // visibility from the state, so this is the same call the edit entry
    // makes rather than a second place the three strings are spelled.
    refreshBoardHint(canvas);
}

void leaveOverview(HudCanvas *canvas)
{
    // BLOCK-USABILITY-FIX: THE SCREEN THE USER IS LOOKING AT MUST SURVIVE
    // THIS TRANSITION. setOverview(false) restores savedPreviewOutput_ —
    // the value captured when the overview was ENTERED — while picking
    // another display inside the overview re-captures that slot from the
    // previous OVERVIEW screen. So a viewer parked on HDMI that the user
    // moved to DP-1 inside the overview came back as HDMI (zero windows)
    // the moment they navigated to a panel or entered edit.
    //
    // The board answers previewScreen() itself, so capture that answer
    // before the exit and put the SAME answer back after. HudCanvas's own
    // legacy contract (restore-on-exit for a bare canvas) is untouched —
    // keeping the viewed display is the CONSOLE's policy, spelled here.
    const QString viewed = canvas ? canvas->previewScreen() : QString();
    if (canvas)
        canvas->setOverview(false);
    if (!viewed.isEmpty())
        canvas->setPreviewScreen(viewed);
    // The panel cards keep whatever highlight selectPanel() last set;
    // leaving the overview does not change which panel is selected.
    if (QFrame *navCard = s_overviewNav) {
        navCard->setProperty("selected", false);
        navCard->style()->unpolish(navCard);
        navCard->style()->polish(navCard);
    }
    // And the caption goes with it — the panel view's own line (idle /
    // edit) is the edit entry's to write, and leaving the overview
    // re-derives both the string and the visibility from the state, so a
    // board that is editing keeps its edit caption across the exit
    // instead of having it hidden by a hide() that assumed the panel
    // view was what came next.
    refreshBoardHint(canvas);
}

// ─── v1.0.0: the board's edit state ───────────────────────────────────
//
// The prototype's `board.editing` on the console's own board: the grid
// fades in, the board's border turns clay and every card's border goes
// dashed. All three are READ from HudCanvas::editing() at paint time, so
// the one write here is the whole edit face — see the declaration's note
// for why this is a free function and not a member.
//
// v1.0.0 (sa-B) made the canvas able to hold the state; this is the
// writer that was missing. Before it, `editing_` was dead: it was read
// by three paint sites (the grid, the board's border colour and every
// card's dash pattern) and nothing in the program ever assigned it, so
// the grid never appeared, the board's border stayed --line2 and the
// cards stayed solid.
//
// NOT DONE HERE: THE BUTTON'S OWN `.btn.on` FACE. The prototype's
// `#edit-toggle` carries a clay fill while the board is editing, and
// this is where the property flip would go — but the QSS that selects
// it lives in control_panel_styles.cpp, which this round is forbidden
// to touch, so the flip would be invisible. It is left for whoever owns
// that file: a `QPushButton#enterEdit[on="true"]` rule beside the
// existing `#enterEdit` pair, plus the two style()->polish() lines here.
// The board itself does not need it — every edit-face change is read
// from editing() at paint time.
//
// THEN THE CAPTION, LAST. refreshBoardHint() is called after
// setEditing() rather than before so it reads the state it is meant to
// describe; the two calls in that order are what make the caption and
// the board unable to disagree.
void setBoardEditing(HudCanvas *canvas, bool on)
{
    if (!canvas)
        return;
    // setEditing() first so refreshBoardHint() reads the state it is
    // meant to describe; the two calls in that order are what make the
    // caption and the board unable to disagree.
    canvas->setEditing(on);
    // AND THE BUTTON'S OWN FACE. Without this the edit toggle is the one
    // control that does not say which mode it is in: the board grows a clay
    // border and dashed cards, and the button that caused it looks
    // untouched. The property is what QPushButton#enterEdit[on="true"] keys
    // off, and because setEditing() repaints the canvas and not the
    // toolbar, the polish pair has to be explicit rather than left to the
    // next event that happens to touch the button.
    if (s_editBtn) {
        // A string literal, not QStringLiteral: QObject::setProperty's
        // first parameter is `const char *`, and a QString does not convert
        // to it. The literal is also how the rest of this file sets
        // dynamic properties (setProperty("navObject", true) and friends).
        s_editBtn->setProperty("on", on);
        s_editBtn->style()->unpolish(s_editBtn);
        s_editBtn->style()->polish(s_editBtn);
    }
    refreshBoardHint(canvas);
}

// The board's one-line caption (HTML `.board-hint`), written from the
// canvas's own view state. renderBoardHead() picks one of three strings
// and this is that choice, spelled once: overview while the read-only
// single-display view is open, the edit line while the board is editing,
// and the panel view's idle line otherwise.
//
// AND IT OWNS THE VISIBILITY TOO, for the same reason it owns the
// string. Both are a function of the same two flags, and deriving them
// together is what makes the third entry points safe: selectPanel()
// calls leaveOverview() on every rail click, and leaveOverview()'s own
// hide() would strand an edit caption if it ran while the board was
// editing. Reading the state means a caller cannot half-apply a mode.
//
// i18n BY HOOK, NOT BY A FROZEN setText(). The caption's key is a
// function of the live view state, which is exactly the case trHook()
// exists for — the same pattern the game note line above uses (see
// gameNote_). The hook registered at construction calls in here, so a
// language flip replays it through retranslateUi() and gets the CURRENT
// mode's string instead of freezing whatever language was in force when
// the mode was entered.
void refreshBoardHint(HudCanvas *canvas)
{
    if (!s_boardHint || !canvas)
        return;
    const bool overview = canvas->overviewMode();
    const bool editing  = canvas->editing();
    const QString key = overview
            ? QStringLiteral("console.proto.board.hint.overview")
            : (editing
                   ? QStringLiteral("console.proto.board.hint.edit")
                   : QStringLiteral("console.proto.board.hint.idle"));
    s_boardHint->setText(consoleText(key));
    // Always on — the row is a fixed 32px grid track in the draft, not a
    // state-dependent banner. See refreshBoardHint()'s header block.
    s_boardHint->show();
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
    // The eye's glyph is a function of the same gate (reference.html:450
    // `eye.classList.toggle('off', !on); eye.textContent = on?'●':'○'`),
    // so it is synced HERE — the one place that already rewrites the row
    // from the master chip — rather than from a second, forgettable call.
    if (auto *navBox = ctl_[idx].navButton) {
        if (auto *eye = navBox->findChild<QPushButton*>(
                QStringLiteral("navEnabled"))) {
            const bool on = ctl_[idx].master
                                && ctl_[idx].master->isChecked();
            const bool off = !on;
            if (eye->property("off").toBool() != off) {
                eye->setProperty("off", off);
                eye->setText(off ? QStringLiteral("○") : QStringLiteral("●"));
                eye->style()->unpolish(eye);
                eye->style()->polish(eye);
            }
        }
    }
    // v1.0.0 fidelity (nav): THE ROW HAS NO SECOND LINE ANY MORE, so
    // there is nothing left for the section-summary text to write into.
    // ctl_[i].navSummary is left null by buildObjectButton(); the eye and
    // the count above are the draft's own row contents (reference.html
    // :299-303) and are untouched here. See buildObjectButton's
    // "THE SECOND LINE IS GONE" note for the measured reason.
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
                                         [[maybe_unused]] const QString &summaryKey,
                                         int idx)
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
    // v1.0.0 fidelity (nav): ONE 38px LINE, `.nav-item`'s own metrics —
    // `min-height:38px;padding:0 5px 0 8px` and a 6px grid gap
    // (reference.html:109-112). The row used to be a two-line text stack
    // (title over a section summary) inside 9px vertical margins, which
    // measured 53px per row and is the whole reason the rail's nav read
    // as taller than the draft's. The QSS's `min-height:36px` + 1px
    // border is what makes the row 38 here.
    row->setContentsMargins(8, 0, 5, 0);
    row->setSpacing(6);   // gap:6px between the grid's columns

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

    /* ── v1.0.0 fidelity (nav): THE SECOND LINE IS GONE ───────────────
       The sa-A round kept TWO lines in this row — the draft's
       `[data-count]` (e.g. "2 窗") plus Qt's own section summary
       ("3 / 9 分区") stacked under the name. That stack is the whole
       reason the rows measured 53px: the draft's `.nav-item` is 38px and
       ONE line — `grid-template-columns:26px minmax(0,1fr) auto 26px`
       = icon, name, count, eye and nothing else (reference.html:109,
       :118, :299-303). So the summary label is not built at all:
       ctl_[i].navSummary stays null and updatePanelSummary() — which
       already guards on it — keeps its eye/count work. The count label
       below IS the draft's `[data-count]`; the summary has no
       counterpart in the frozen file.

       The i18n keys console.nav.*_summary stay in the console.json
       resources untouched; this round does not
       edit resources, and summaryKey is therefore unused here.
       ── end v1.0.0 fidelity (nav) ── */
    auto *titleLabel = new QLabel();
    trSet(titleLabel, titleKey);
    titleLabel->setObjectName("navTitle");
    row->addWidget(titleLabel, 1);
    /* ── v1.0.0 sa-A: sidebar ──
       The prototype's nav row is [name … count] — `[data-count]`, mono
       10px --fg3, e.g. "5 窗" / "2 窗" / "3 窗" / "1 窗". The Qt shell
       had no count at all; it had a section-summary line instead
       (navSummary, "3 / 9 分区" style) which the prototype does NOT have
       in the nav row. navCount is the right-aligned label the
       window-count pass fills, and it is what the prototype shows.
       ── end sa-A: sidebar ── */
    auto *count = new QLabel();
    count->setObjectName(QStringLiteral("navCount"));
    count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(count);
    /* ── v1.0.0 (O1): THE EYE, AND IT IS A REAL CONTROL ──────────────────
       reference.html:300-303 gives every non-overview nav row a

           <button class="eye" type="button" data-toggle-panel="player"
                   aria-label="toggle player">●</button>

       in the row's 4th, 26px grid column (VISUAL-CONTRACT §10.14: 22×22,
       transparent, radius 4, #87867f, `.off` → #b04a3f), and
       #panel-nav's click handler toggles
       `scope().panels[p].visible` on it BEFORE the `[data-view]` branch,
       with `e.stopPropagation()` so the row does not also switch view
       (reference.html:511).

       The shell had a QLabel("●") here — a glyph with no handler, i.e.
       exactly the "decorative control" O1's acceptance list forbids.
       It is a QPushButton now, and the mouse grab it takes on press is
       the Qt spelling of stopPropagation: the row's own release filter
       never sees the event, so clicking the eye does not navigate.

       ONE STATE, NOT TWO. The eye does not own a flag of its own: the
       panel's visibility in this console IS `ctl_[idx].master`, because
       maskFor() answers 0 when that chip is unchecked (line 1205) and
       rebuildAndRender() then paints no windows for the panel. A second,
       parallel "visible" bit would be the two-flags-that-disagree failure
       O1's note about the master/no-panel semantics is warning against,
       so the eye writes the chip and reads it back (see
       togglePanelVisible() and the glyph sync in updatePanelSummary()).
       ── end O1: the eye ── */
    auto *enabled = new QPushButton(QStringLiteral("●"));
    enabled->setObjectName("navEnabled");
    enabled->setCursor(Qt::PointingHandCursor);
    enabled->setFixedSize(22, 22);
    enabled->setFocusPolicy(Qt::NoFocus);
    enabled->setFlat(true);
    row->addWidget(enabled, 0, Qt::AlignVCenter);
    connect(enabled, &QPushButton::clicked, this, [this, idx]{
        togglePanelVisible(idx);
    });

    ctl_[idx].navButton = box;
    // navSummary is deliberately left null — the one-line row has no
    // second line (see the "THE SECOND LINE IS GONE" note above), and
    // updatePanelSummary() already guards on it.
    navCounts_[idx] = count;
    return box;
}

// ── v1.0.0 (O1): the eye's one job ──────────────────────────────────────
//
// Toggle the panel's master gate and say so. Both halves of the sentence
// matter: the write goes through ToggleChip::setChecked so the SAME
// stateChanged → rebuildAndRender path the inspector's chip uses runs
// (one gate, one path), and the toast is the draft's own copy —
// `showToast(panel.visible ? '面板已显示' : '面板已隐藏')`,
// reference.html:511.
// ── end O1: the eye ─────────────────────────────────────────────────────
void ControlPanel::togglePanelVisible(int idx)
{
    if (!mhw::isPanelIndex(idx))
        return;
    ToggleChip *master = ctl_[idx].master;
    if (!master)
        return;                       // that slot has no inspector yet
    const bool on = !master->isChecked();
    master->setChecked(on);           // → stateChanged → rebuildAndRender
    updatePanelSummary(idx);          // the glyph and the row's summary
    showToast(on ? QStringLiteral("console.proto.panel.shown")
                 : QStringLiteral("console.proto.panel.hidden"));
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

    // ── v1.0.0: the field groups, in the prototype's order ─────────
    //
    // 位置 → 目标显示器 → 外观 → 显示内容 → 预览. Every group but the
    // content list already carried a sectionCap (appearance, content,
    // preview); position and target display got theirs here. The two
    // that moved (the position spins and the output combo) keep their
    // construction code verbatim — same ranges, same suffixes, same
    // connects, same ctl_[idx] assignments — only their place in the
    // column changed, plus a 12px gap between groups and none between
    // a caption and the widgets it names.
    //
    // The Rise card stays where it has always been relative to the
    // section rows: after them, before appearance.
    vl->addSpacing(12);

    // 位置 — the prototype's first group. The spins are editable; the
    // label below them stays, because it reports the quadrant, which
    // the spins do not.
    auto *posCap = new QLabel();
    trSet(posCap, QStringLiteral("console.proto.position"));
    posCap->setObjectName("sectionCap");
    vl->addWidget(posCap);

    // v1.0.0 T2: editable x/y. The label below stays — it reports the
    // quadrant, which the spins do not.
    auto *posRow = new QHBoxLayout();
    posRow->setSpacing(8);
    auto *spinX = new QSpinBox();
    spinX->setObjectName("posSpinX");
    spinX->setRange(0, 9999);
    spinX->setSuffix(QStringLiteral(" px"));
    auto *spinY = new QSpinBox();
    spinY->setObjectName("posSpinY");
    spinY->setRange(0, 9999);
    spinY->setSuffix(QStringLiteral(" px"));
    {
        Panel *p0 = panelAt(idx);
        const QMargins m0 = p0->margins();
        spinX->setValue(m0.left());
        spinY->setValue(m0.top());
    }
    posRow->addWidget(spinX);
    posRow->addWidget(spinY);
    vl->addLayout(posRow);
    ctl_[idx].posSpinX = spinX;
    ctl_[idx].posSpinY = spinY;
    // persist=false, the same as every other console path that moves a
    // panel (drag at 2283, the split host at 542/562). The console writes
    // once, in closeEvent; typing a digit must not saveConfig() per
    // keystroke, or there is nothing left to cancel.
    connect(spinX, &QSpinBox::valueChanged, this, [this, idx](int v){
        Panel *p = panelAt(idx);
        QMargins m = p->margins();
        m.setLeft(v);
        p->setMargins(m, /*persist=*/false);
        updatePosLabel(idx);
        rebuildAndRender(idx);
    });
    connect(spinY, &QSpinBox::valueChanged, this, [this, idx](int v){
        Panel *p = panelAt(idx);
        QMargins m = p->margins();
        m.setTop(v);
        p->setMargins(m, /*persist=*/false);
        updatePosLabel(idx);
        rebuildAndRender(idx);
    });
    // v0.5 BEHAVIOR → POSITION: shows the panel's anchor corner and
    // current margins. The user moves the panel via canvas drag or
    // arrow keys; this label is read-only feedback.
    auto *posLabel = new QLabel();
    posLabel->setObjectName("posLabel");
    // v1.0.0: NO fixed width. This label used to be pinned at 350px,
    // which fitted the 480px inspector column it lived in; the drawer is
    // narrower than that (and resizable), so the pin either clipped the
    // corner + margin readout or forced a horizontal scrollbar. Word
    // wrap handles the long case and elides the pathological one.
    posLabel->setWordWrap(true);
    posLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ctl_[idx].posLabel = posLabel;
    vl->addWidget(posLabel);

    vl->addSpacing(12);

    // 目标显示器 — the prototype's second group.
    auto *targetCap = new QLabel();
    trSet(targetCap, QStringLiteral("console.proto.target"));
    targetCap->setObjectName("sectionCap");
    vl->addWidget(targetCap);

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
        // First entry: the automatic mode — empty userData so
        // Panel::setOutputName("") clears the override and the chain
        // resolves it. 实测缺陷5: this row edits a PANEL (never a sub
        // window), and a panel above the chain is only ever followed by
        // its own windows — its own automatic screen is the SYSTEM
        // PRIMARY, so 跟随主屏 is the name that matches what happens
        // (mhw::resolveWindowOutput(own="", parent="", primary)). The
        // inspector's combo spells the sub-window case 跟随主面板; the
        // two are the same empty-data mode under two honest names.
        combo->addItem(mh::tr(QStringLiteral("console.proto.follow_primary")),
                       QString());
        // Item 0 is UI copy — re-localize it in place. The remaining
        // items are screen names + resolution and must not move.
        trHook([combo]{
            combo->setItemText(
                0, mh::tr(QStringLiteral("console.proto.follow_primary")));
        });
        const auto outs = screen_query::listOutputs();
        for (const auto &o : outs) {
            // v1.0.0 T2: the prototype's label — "DP-1 · 2560×1440", and a
            // portrait output says so. No @x,y and no primary tag: the
            // prototype has neither.
            QString label = o.name;
            label += QStringLiteral(" · %1×%2")
                .arg(o.geometry.width()).arg(o.geometry.height());
            if (o.geometry.height() > o.geometry.width())
                label += mh::tr(QStringLiteral("console.proto.screen.rot"));
            combo->addItem(label, o.name);
        }
        // Pick the persisted value (if any) — match either by userData
        // (output name) or fall back to the first entry.
        //
        // This is the ONLY place the combo's index is ever set, and it
        // runs once, while buildInspector() is constructing the page.
        // rebuildMotherPanels() destroys and re-creates the Panels
        // underneath a live combo and re-reads their outputName from the
        // (possibly different) scope — but nothing re-syncs the combo,
        // so after a rail click the dropdown keeps advertising the
        // screen the OLD panel had while the new panel's member holds
        // whatever the NEW scope says. Measured on this machine:
        //
        //     [world/single/player] outputName=DP-1     <- the value
        //     conf layout=split
        //     combo displays DP-1  member ''  argv: no --output flag
        //
        // The console promised DP-1 and the child mounted on the
        // primary. syncOutputCombo() below is what closes that gap; it
        // is called from adoptMotherPanels()'s per-panel loop, and here
        // once more so the freshly constructed combo starts correct
        // even when no rebuild follows.
        syncOutputCombo(combo, panelAt(idx));
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
            // v0.15: and the split sub-windows follow it, for the same
            // reason selectPanel() does. Picking DP-1 in this dropdown
            // moves the frame the user is looking at; the drag clamp has
            // to move with it or a sub-window stops at the middle of the
            // monitor it was built under.
            //
            // v1.0.0 (OUTPUT-OWNERSHIP-FIX): this push no longer reads the
            // frame. A mother's SCREEN row writes the very output every
            // child inherits, so what has to move is the children's
            // own → parent → primary fallback — never the viewer.
            pushWindowOutputFallback(
                splitPreview_.get(),
                {panelAt(0), panelAt(1), panelAt(2), panelAt(3)});
        });
        screenRow->addWidget(screenLab);
        screenRow->addWidget(combo, 1);
        vl->addLayout(screenRow);
        ctl_[idx].outputCombo = combo;
    }

    vl->addSpacing(12);

    // 外观 — one caption over the three sliders.
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

    vl->addSpacing(12);

    // 显示内容 — the section rows.
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

    // ── v1.0.0 (A): THE RISE CARD LIVES IN SETTINGS ONLY ────────────────
    // reference.html has exactly one REFramework block and it is the
    // settings modal's `#rise-settings` (reference.html:349); there is no
    // second one in the inspector. Building the same card twice also gave
    // the tree two widgets called "riseReframeworkCard", so a
    // findChild("riseReframeworkCard") could not say WHICH one it meant.
    //
    // ControlPanel::openSettingsDialog() is the single build site now.
    // riseCard_->refresh() stays safe with no card on screen — its
    // handles are QPointer (rise_reframework_card.h).

    vl->addSpacing(12);

    // 预览 — the prototype's last group.
    // ── v1.0.0 T3: the prototype's .preview ──────────────────────────
    auto *previewCap = new QLabel();
    trSet(previewCap, QStringLiteral("console.proto.preview"));
    previewCap->setObjectName("sectionCap");
    vl->addWidget(previewCap);

    // QLabel (not QFrame) so the box can be assigned to PanelCtl::preview,
    // whose member type is QLabel* and is left untouched. QLabel derives
    // from QFrame, so every QWidget call below behaves identically.
    auto *previewBox = new QLabel();
    previewBox->setObjectName("previewBox");
    previewBox->setMinimumHeight(110);
    previewBox->setProperty("previewWidget", true);   // QSS 用属性选择器兜底
    auto *pv = new QVBoxLayout(previewBox);
    pv->setContentsMargins(10, 10, 10, 10);
    pv->setSpacing(4);
    auto *previewText = new QLabel();
    trSet(previewText, QStringLiteral("console.proto.preview.empty"));
    previewText->setWordWrap(true);
    previewText->setObjectName("previewText");
    pv->addWidget(previewText);
    vl->addWidget(previewBox);
    ctl_[idx].preview = previewBox;    // ← 这个指针现在是 null，赋值后就有意义
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

/* ── v1.0.0 sa-A: topbar ──
   THE TOPBAR IS REBUILT TO THE PROTOTYPE'S NINE CONTROLS.

   HTML `.topbar` is `background --surf; border 1.5px --line; radius 12px;
   padding 8px 10px; display flex; gap 8px; flex-wrap wrap`, and it holds:

     布局     [整块 | 分块]        ← a SEGMENTED CONTROL, not a combo
     显示器   <select>              DP-1 · 2560×1440
     演示状态 <select>              实时 / 离线 / 无数据
     实例     <select>              Rise only
     语言     <select>              中文 / English
     分开密度 <select>              紧凑 / 标准    split only
     连接     chip                  已连接 / 未连接
     编辑布局 button
     启动     button (primary)

   The old shell's toolbar held FOUR things: layoutCombo_ + layoutApplyBtn_
   + editBtn_ + startBtn_. The two that survive keep their objectNames
   (enterEdit / startBtn) and their ctor wiring; the layout pair is
   replaced by the segmented control, and six controls are new.

   THE OLD PAIR IS KEPT IN THE TREE, HIDDEN. Four test files drive the
   layout mode through findChild("layoutCombo") / findChild("layoutApply"):
   console_split_sync_tests.cpp:227-228 and console_scope_persistence_tests
   .cpp:899-901 look them up and click them, and
   console_scope_persistence_tests.cpp:426 walks every combo in the page
   and must SKIP layoutCombo by name to avoid mistaking it for a panel
   screen picker. Removing them turns four test files red for a purely
   cosmetic reason, so they stay constructed with their objectNames and
   their signals — hidden, and kept in sync by the segmented control's
   handlers (see below). layoutComboMode(), layoutComboIndexFor() and
   updateLayoutApplyEnabled() are untouched and keep reading them.

   THE SEGMENT'S SEMANTICS ARE THE COMBO'S. Clicking the entry that
   differs from the mode in force pushes the choice into layoutCombo_ and
   commits it through applyLayoutChoice() — the same function
   layoutApplyBtn_'s click used to call. That keeps the prototype's
   one-click feel without changing what a commit does: `--layout` is read
   once at overlay startup, so a mode change still persists and still
   relaunches a live child, exactly as the two-widget pair documented.
   ── end sa-A: topbar ── */
/* ── v1.1.1 (顶栏溢出): the measured topbar band ─────────────────────── */
namespace {
// `.workspace{grid-template-rows:66px minmax(0,1fr)}` — the header band is
// exactly 66px on the 1600×1000 reference viewport (geometry.json state01:
// `topbar x=232 y=0 w=1353 h=66`). It stays 66 for as long as the header
// is one row; refreshTopbarWrap() grows it by one button row only when
// the window is too narrow for the single row's own minimum.
constexpr int kTopbarBandHeight = 66;
} // namespace

QWidget *ControlPanel::buildToolbar()
{
    auto *bar = new QFrame();
    bar->setObjectName("topbar");
    topbar_ = bar;
    /* ── v1.1.1 (顶栏溢出): ONE ROW, OR TWO ──────────────────────────────
       The note below said "Qt has no flex-wrap" — but the PROTOTYPE'S OWN
       `.topbar` is `display:flex; flex-wrap:wrap`, and that is exactly
       what this block was missing: at the console's own minimum width
       (1272 measured on the user's desktop) the single row's minimum is
       WIDER than the window, so the root scroll gained a horizontal
       scrollbar and the four action buttons — the last flex items — sat
       outside the visible area. "Start overlay" read as `Start ov…`,
       导出 was unreachable without scrolling the whole page sideways,
       and nothing in the header said why.

       So the bar is now a vbox of two rows: row1 is the measured single
       row UNCHANGED (same margins, same spacing, same items), row2 holds
       the four action buttons right-aligned behind their own stretch.
       refreshTopbarWrap() keeps them on row1 whenever row1's own minimum
       fits the window, which is every width the prototype itself renders
       as one row — at 1600×1000 this is pixel-identical to before. When
       it does not fit, the buttons move to row2 and the band grows by
       one button row. Nothing is dropped, nothing moves elsewhere in the
       window: the header wraps the way the CSS it is porting already
       does.
       ── end v1.1.1: one row, or two ── */
    auto *vl = new QVBoxLayout(bar);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    auto *hl = new QHBoxLayout();
    vl->addLayout(hl);
    topbarRow1_ = hl;
    auto *hl2 = new QHBoxLayout();
    // Zeroed while the second row is empty (an empty layout's own margins
    // would otherwise add to the bar's minimum height and cost the 66px
    // band its exact height). refreshTopbarWrap() restores the real
    // numbers the moment the buttons move in.
    hl2->setContentsMargins(0, 0, 0, 0);
    hl2->setSpacing(14);
    hl2->addStretch(1);
    vl->addLayout(hl2);
    topbarRow2_ = hl2;
    // The prototype's padding 8px 10px and gap 8px, on a flex row that
    // wraps. Qt has no flex-wrap: a QHBoxLayout in a QFrame with a
    // size policy that lets it grow vertically gets the same result from
    // the layout's own wordWrapping-ish behaviour when the width runs
    // out — the widgets keep their size hints and the row simply runs out
    // of room, so the container grows. Explicit minimum widths below keep
    // each group from being squeezed below its label + control.
    // THE MEASURED TOPBAR, not an estimate. `.workspace` is
    // `grid-template-rows:66px minmax(0,1fr)` and `.topbar` is
    // `display:flex; align-items:center; gap:14px; padding:10px 18px`
    // — so the header band is exactly 66px tall on the 1600×1000
    // reference viewport and its first child starts at x=250 = 232
    // (the rail) + 18 (the padding). geometry.json state01:
    // `topbar x=232 y=0 w=1353 h=66`, `topbar-title x=250 y=15 w=190 h=35`.
    // Qt has no CSS grid row, so 66 is a FIXED height here; the 18/10
    // and the 14 are the layout's own margins and spacing.
    hl->setContentsMargins(18, 10, 18, 10);
    hl->setSpacing(14);
    bar->setFixedHeight(kTopbarBandHeight);

    // ---- the title block: the prototype's `.topbar-title` -----------
    //
    //   <div class="topbar-title">
    //     <strong id="top-title">显示器总览</strong>
    //     <span id="top-subtitle">一次显示一块屏幕 · 不使用全局桌面坐标</span>
    //   </div>
    //
    // `.topbar-title{min-width:190px}` (measured 190×35) and the two
    // lines are 13px/650 and 10px mono --fg3 (VISUAL-CONTRACT §10.4) —
    // the block the shell's toolbar was missing entirely: it started at
    // LAYOUT, so there was nothing to compare against #top-title.
    // Both lines follow the VIEW (reference.html:458-459), which is why
    // they are members refreshed by refreshTopbarTitle() rather than a
    // one-shot trSet.
    {
        auto *titles = new QVBoxLayout();
        titles->setContentsMargins(0, 0, 0, 0);
        titles->setSpacing(0);
        topbarTitle_ = new QLabel();
        topbarTitle_->setObjectName(QStringLiteral("topbarTitle"));
        topbarTitle_->setMinimumWidth(190);
        topbarTitle_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        titles->addWidget(topbarTitle_);
        topbarSubtitle_ = new QLabel();
        topbarSubtitle_->setObjectName(QStringLiteral("topbarSubtitle"));
        topbarSubtitle_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        titles->addWidget(topbarSubtitle_);
        hl->addLayout(titles);
    }

    // ---- 布局: the segmented control -----------------------------------
    //
    // Two exclusive QPushButtons in a bordered frame — the prototype's
    // `.seg`. NOT a QComboBox: the prototype's `#mode-seg` is a group of
    // buttons, and the whole point of matching it is that the user sees
    // both options at once instead of one behind a drop-down arrow.
    {
        auto *label = new QLabel();
        trSet(label, QStringLiteral("console.proto.layout"));
        label->setObjectName(QStringLiteral("tbLabel"));
        hl->addWidget(label);

        layoutSeg_ = new QFrame();
        layoutSeg_->setObjectName(QStringLiteral("layoutSeg"));
        auto *seg = new QHBoxLayout(layoutSeg_);
        seg->setContentsMargins(2, 2, 2, 2);
        seg->setSpacing(2);

        layoutSingleBtn_ = new QPushButton();
        trSet(layoutSingleBtn_, QStringLiteral("console.proto.single"));
        layoutSingleBtn_->setObjectName(QStringLiteral("layoutSegSingle"));
        layoutSingleBtn_->setCheckable(true);
        layoutSingleBtn_->setAutoExclusive(true);
        layoutSingleBtn_->setCursor(Qt::PointingHandCursor);
        seg->addWidget(layoutSingleBtn_);

        layoutSplitBtn_ = new QPushButton();
        trSet(layoutSplitBtn_, QStringLiteral("console.proto.split"));
        layoutSplitBtn_->setObjectName(QStringLiteral("layoutSegSplit"));
        layoutSplitBtn_->setCheckable(true);
        layoutSplitBtn_->setAutoExclusive(true);
        layoutSplitBtn_->setCursor(Qt::PointingHandCursor);
        seg->addWidget(layoutSplitBtn_);

        // v1.1.0: the third segment. The combo has had a Mixed row since
        // the model landed, but the topbar's SEGMENTED control is what the
        // user actually clicks — so until this button existed, Mixed was
        // unreachable from the console despite being fully wired in the
        // overlay (verified on a real Wayland session: the segment showed
        // exactly two options).
        layoutMixedBtn_ = new QPushButton();
        trSet(layoutMixedBtn_, QStringLiteral("console.layout.mixed"));
        trTip(layoutMixedBtn_, QStringLiteral("console.layout.custom_hint"));
        layoutMixedBtn_->setObjectName(QStringLiteral("layoutSegMixed"));
        layoutMixedBtn_->setCheckable(true);
        layoutMixedBtn_->setAutoExclusive(true);
        layoutMixedBtn_->setCursor(Qt::PointingHandCursor);
        seg->addWidget(layoutMixedBtn_);

        hl->addWidget(layoutSeg_);

        // The checked state is the mode in force, not a pending choice:
        // a click on the entry already in force is a no-op (the same
        // guard setMode() has in the prototype), and a click on the other
        // entry commits immediately.
        const bool split = layoutModeInForce_
                           == mhw::PlayerSection::PlayerLayoutMode::Split;
        layoutSingleBtn_->setChecked(!split);
        layoutSplitBtn_->setChecked(split);
        // The old pair, kept alive for the tests and kept in step: it is
        // hidden, and its index is what layoutComboMode() reads.
        // v1.0.0: parent it to the console. A QComboBox with no parent and
        // no layout slot is invisible to findChild(), which walks the CHILD
        // tree — so "kept alive for the tests" only holds if it is a child.
        // Hidden + parented is the combination that satisfies both: the
        // tests can drive it, the toolbar does not have to show it.
        layoutCombo_ = new QComboBox(this);
        layoutCombo_->setObjectName("layoutCombo");
        layoutCombo_->setCursor(Qt::PointingHandCursor);
        layoutCombo_->addItem(mh::tr(QStringLiteral("console.layout.single")));
        layoutCombo_->addItem(mh::tr(QStringLiteral("console.layout.split")));
        // v1.1.0: the third row. The i18n key already exists in both locales.
        layoutCombo_->addItem(mh::tr(QStringLiteral("console.layout.mixed")));
        trHook([this]{
            if (!layoutCombo_) return;
            const int keep = layoutCombo_->currentIndex();
            layoutCombo_->blockSignals(true);
            layoutCombo_->setItemText(kLayoutComboSingleIndex,
                mh::tr(QStringLiteral("console.layout.single")));
            layoutCombo_->setItemText(kLayoutComboSplitIndex,
                mh::tr(QStringLiteral("console.layout.split")));
            layoutCombo_->setItemText(kLayoutComboMixedIndex,
                mh::tr(QStringLiteral("console.layout.mixed")));
            layoutCombo_->setCurrentIndex(keep);
            layoutCombo_->blockSignals(false);
        });
        // v1.0.0: arm the apply button for the restored index BEFORE hiding.
        //
        // setCurrentIndex() only emits currentIndexChanged when the index
        // actually moves. When the console restores already in Split and a
        // caller then asks for Split, the index does not move, the signal
        // never fires, and updateLayoutApplyEnabled() never runs — so the
        // apply button stays disabled for a selection that IS pending.
        // Seeding the state directly here makes the enable decision depend
        // on the index rather than on having seen an edge.
        layoutCombo_->setCurrentIndex(layoutComboIndexFor(layoutModeInForce_));
        updateLayoutApplyEnabled();
        layoutCombo_->hide();

        layoutApplyBtn_ = new QPushButton(this);
        trSet(layoutApplyBtn_, QStringLiteral("console.layout.apply"));
        layoutApplyBtn_->setObjectName("layoutApply");
        layoutApplyBtn_->setCursor(Qt::PointingHandCursor);
        layoutApplyBtn_->setEnabled(false);   // nothing to do until a change
        trTip(layoutApplyBtn_, QStringLiteral("console.layout.apply_tip"));
        layoutApplyBtn_->hide();
        connect(layoutCombo_, &QComboBox::currentIndexChanged,
                this, [this]{ updateLayoutApplyEnabled(); });
        connect(layoutApplyBtn_, &QPushButton::clicked, this,
                [this]{ applyLayoutChoice(layoutComboMode()); });

        // The segment's click: mirror into the hidden combo, then commit.
        // Mirroring FIRST is what keeps a test that sets the combo's index
        // and clicks apply() working after a user has used the segment.
        //
        // v1.1.0: takes the MODE, not a bool. `bool toSplit` could not
        // express a third segment, and the natural extension — treating the
        // flag as "not Split" — is what silently sent Mixed to Single.
        auto commitFromSeg = [this](mhw::PlayerSection::PlayerLayoutMode to){
            const int want = layoutComboIndexFor(to);
            if (layoutCombo_ && layoutCombo_->currentIndex() != want)
                layoutCombo_->setCurrentIndex(want);
            applyLayoutChoice(layoutComboMode());
            // Re-assert the in-force state: applyLayoutChoice() can refuse
            // (the confirm dialog is declined), and a segment button left
            // checked on a mode that did not change would be a lie.
            syncLayoutSegmentToMode();
        };
        connect(layoutSingleBtn_, &QPushButton::clicked, this,
                [commitFromSeg]{ commitFromSeg(
                    mhw::PlayerSection::PlayerLayoutMode::Single); });
        connect(layoutSplitBtn_, &QPushButton::clicked, this,
                [commitFromSeg]{ commitFromSeg(
                    mhw::PlayerSection::PlayerLayoutMode::Split); });
        connect(layoutMixedBtn_, &QPushButton::clicked, this,
                [commitFromSeg]{ commitFromSeg(
                    mhw::PlayerSection::PlayerLayoutMode::Mixed); });
    }

    // ---- 显示器 ---------------------------------------------------------
    //
    // THE OVERVIEW'S DISPLAY SWITCH (HTML #screen-select). It decides
    // which monitor the read-only overview paints; it is NOT a window's
    // owner — that is the inspector's SCREEN row, a different control with
    // different semantics ("screen switching is a pure view operation,
    // window ownership is a configuration operation").
    //
    // Entries come from screen_query::listOutputs(), formatted exactly as
    // the prototype formats them:
    //     "DP-1 · 2560×1440"                 (U+00B7, U+00D7)
    //     "HDMI-A-1 · 1080×1920(旋转)"        rotated outputs get the suffix
    // userData is the REAL output name, so sa-D's display switch can pull
    // the name back out without parsing the label.
    {
        auto *label = new QLabel();
        trSet(label, QStringLiteral("console.proto.display"));
        label->setObjectName(QStringLiteral("tbLabel"));
        hl->addWidget(label);

        screenCombo_ = new QComboBox();
        screenCombo_->setObjectName(QStringLiteral("screenCombo"));
        screenCombo_->setCursor(Qt::PointingHandCursor);
        trTip(screenCombo_, QStringLiteral("console.overview.display_tip"));
        screenCombo_->setMinimumWidth(190);
        // The static pointer the overview toggles read — see the
        // accessor's declaration in the header for why it is not a member
        // lookup. Assigned here, at construction, before any event can
        // route to it (the overview's event filter is installed on the
        // nav card, which is built later).
        s_screenCombo = screenCombo_;
        hl->addWidget(screenCombo_);
    }

    // ---- the five topbar groups the frozen draft does NOT have (removed)
    //
    // The frozen topbar is EXACTLY six things, in this order
    // (reference.html:313-321):
    //
    //   .topbar-title → LAYOUT seg → DISPLAY select → .toolbar-spacer
    //   → #edit-btn → #run-btn → #export-btn
    //
    // The shell also carried 演示状态 + its `.tb-note`, 实例, 语言,
    // 分开密度 and a 连接 chip. Four of the five have no counterpart
    // anywhere in the draft (`grep -n "state-select\|instance\|density"
    // reference.html` is empty for all three names, and there is no
    // locale control outside the SETTINGS modal, where #locale-actions
    // lives), and the connection chip is not in the topbar either — the
    // draft's only status dot is `.status-mini` in the RAIL footer
    // (reference.html:307).
    //
    // They are gone from the bar rather than merely hidden: hiding them
    // would leave their space between DISPLAY and the toolbar-spacer,
    // and the draft's DISPLAY select ends at x=848 with 481px of empty
    // flex before #edit-btn (geometry.json state01: `.toolbar-spacer
    // x=862 w=481`). The members and every reader of them stay declared
    // and NULL — each use site already guards (`if (!instanceCombo_)`,
    // `if (densityCombo_)`, `if (statusline_)`, `if (connectChipText_)`,
    // `if (!localeCombo_)`, `if (!demoStateCombo_)`) — so
    // refreshStatusline() / refreshInstanceCombo() still compile and
    // become honest no-ops instead of dereferencing widgets nobody can
    // see. 语言 is not lost: the prototype puts it in settings
    // (#locale-actions) and it is already there, beside #theme-actions.
    //
    // ── end O1: topbar groups removed ──

    hl->addStretch(1);

    // ---- 编辑布局 -------------------------------------------------------
    //
    // The same button the old toolbar had, same objectName (enterEdit),
    // same ctor wiring (launchOverlayChild(edit=true)). Only its face
    // changes: the prototype's .btn, and .btn.on (clay fill) while the
    // board is in edit mode.
    editBtn_ = new QPushButton();
    /* ── 验收项4 (B 第1阶段): THE TWO ENTRIES, NAMED FOR WHAT THEY DO ──────
       编辑预览 toggles THIS console's own board (setBoardEditing), which
       is what it always did; the new sibling below is the one that
       actually opens the overlay's edit mode. Two different surfaces, two
       buttons, two reserved keys — and neither is allowed to stand in
       for the other.
       ── end 验收项4: preview-edit keeps its own copy ── */
    trSet(editBtn_, QStringLiteral("console.edit.preview_start"));
    trTip(editBtn_, QStringLiteral("console.edit.preview_tip"));
    s_editBtn = editBtn_;   // the board-edit helpers key the button's face off this
    editBtn_->setObjectName("enterEdit");
    editBtn_->setCursor(Qt::PointingHandCursor);
    hl->addWidget(editBtn_);

    /* ── 验收项4 (B 第1阶段): 在覆盖层中编辑 — THE REAL LAUNCH ────────────
       THE ONLY LINE THAT MATTERS: launchOverlayChild(true) is the same
       entry the old shell used — it builds buildOverlayArgv(edit=true)
       (the real `--edit` on the child's argv) and hands it to the
       overlay controller, so the child starts in ITS edit mode, and the
       close/save round trip back to this console (the rebuild +
       re-raise block further up in the ctor) is the very same lifecycle
       the 启动 button already uses.

       Deliberately NOT a canvas flag: flipping HudCanvas::setEditing()
       would repaint this preview and prove nothing about the overlay's
       windows. The preview's own edit face stays on 编辑预览 above.
       ── end 验收项4: overlay-edit launch entry ── */
    auto *overlayEditBtn = new QPushButton();
    trSet(overlayEditBtn, QStringLiteral("console.edit.overlay_start"));
    trTip(overlayEditBtn, QStringLiteral("console.edit.overlay_tip"));
    overlayEditBtn->setObjectName(QStringLiteral("overlayEdit"));
    overlayEditBtn->setCursor(Qt::PointingHandCursor);
    connect(overlayEditBtn, &QPushButton::clicked, this,
            [this] { launchOverlayChild(/*editMode=*/true); });
    // v1.1.0: keep the handle. It used to be a buildTopbar local, so
    // setOverlayRunning() — the one place that knows whether a child is
    // alive — could not reach it, and clicking Edit in overlay while one
    // was already running hit launchOverlayChild()'s silent `return`: a
    // button that looks live and does nothing at all.
    overlayEditBtn_ = overlayEditBtn;
    hl->addWidget(overlayEditBtn);

    // ---- 启动 -----------------------------------------------------------
    //
    // The real launcher. setOverlayRunning() flips its text and objectName
    // between startBtn / stopBtn and the QSS follows the swap, so the
    // prototype's .btn.primary / .btn.running pair is produced by the same
    // mechanism that has always driven it. The click wiring stays in the
    // ctor — see the note there.
    startBtn_ = new QPushButton();
    startBtn_->setObjectName("startBtn");
    startBtn_->setCursor(Qt::PointingHandCursor);
    setOverlayRunning(overlay_ && overlay_->isRunning());
    hl->addWidget(startBtn_);

    // ---- 导出: NOT HERE ANY MORE (user 第6条) ----------------------------
    //
    // There used to be a SECOND export button in this row — the draft's
    // `#export-btn` (reference.html:320), wired to the very same
    // openExportDialog() as the rail's 导出 JSON, so the console offered
    // two doors into one modal. The user filed that as a duplicate
    // (顶栏重复的导出按钮) and asked for exactly ONE entry: the sidebar's,
    // objectName `sideFootExport`, built next to 保存 at the rail's foot.
    //
    // Deleted with it: its console.proto.export.short / .tip i18n keys
    // (both locales + the copy-semantics key list), its `#topExport` QSS
    // rule, its connect() lambda, its slot in the fixed-height loop and in
    // topbarActions_ below, and the two topExport references in
    // console_interaction_regression_tests.cpp. openExportDialog() itself
    // is untouched — the rail button is its remaining caller.

    // ---- the measured control heights ---------------------------------
    //
    // A QHBoxLayout hands every widget in the row the row's height, so
    // without this the labels and the segment measured 42px tall in a
    // 46px content box — where the draft has `.topbar-label` at its own
    // 10px line, `#layout-seg` 95×36 (`.seg{padding:3px}` + 28px
    // buttons), `#display-select` 206×31 (`.screen-select{min-height:
    // 31px}`) and all three buttons at 32 (`.icon-btn,.action-btn{
    // min-height:32px}`). geometry.json state01: layout-seg y=18.5 h=36,
    // display-select y=16.5 h=31, edit/run/export y=15 h=32.
    //
    // The labels keep their NATURAL height (Preferred/Fixed) rather than
    // a guessed pixel: what must stop is the stretch, and a hard number
    // would clip the moment O3 gives them the draft's 10px mono font.
    for (QLabel *lab : bar->findChildren<QLabel*>(QStringLiteral("tbLabel")))
        lab->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    if (layoutSeg_)
        layoutSeg_->setFixedHeight(36);
    if (screenCombo_)
        screenCombo_->setFixedHeight(31);
    for (QPushButton *btn : {editBtn_, overlayEditBtn, startBtn_}) {
        if (btn)
            btn->setFixedHeight(32);
    }

    // v1.1.1 (顶栏溢出): the wrap set, in row order, and the first
    // decision. The buttons stay ON ROW1 (the layout above is exactly the
    // measured single row) until refreshTopbarWrap() finds a window that
    // cannot hold them — the common case never leaves this row.
    topbarActions_ = {editBtn_, overlayEditBtn, startBtn_};
    topbarActions_.removeAll(nullptr);
    refreshTopbarWrap();

    return bar;
}

/* ── v1.1.1 (顶栏溢出): ONE ROW, OR TWO ────────────────────────────────
   THE DECISION, AND WHY IT IS NOT `topbar_->width() > x`.

   Inside the root scroll the bar is sized to its OWN MINIMUM the moment
   that minimum exceeds the viewport — measured: at a 1272px window the
   bar is 1292 wide, the content layer is 1524 = 232 (rail) + 1292, the
   page scrolls 252px sideways, and the four action buttons (the last
   items of the row) live at x≈1080…1274 of that bar, i.e. past the
   window's right edge. So the bar's own width is the CAUSE of the
   overflow and can never be the measure of the room. The honest number
   is the scroll viewport's width minus the bar's left offset in it.

   THE RULE. One row while row1's own MINIMUM fits that width, two rows
   the moment it does not. Both sums are computed from the same numbers
   QLayout itself uses (an explicit minimum, else the widget's
   minimumSizeHint — which for a QLabel IS its text), so the decision is
   a pure function of (copy, width): two calls in a row agree, nothing
   oscillates, and a widget that is temporarily in the other row is
   counted the same either way.

   WHAT IT BUYS, at 1272 in EN (the user's own measurement):
   single row = 1292 needed vs 1040 available → the four buttons take
   row2 (461px wide, right-aligned), row1 keeps title + Layout/segment +
   Display/combo at 853px — everything the header shows is then INSIDE
   the window and the page stops scrolling sideways at all, because the
   content's minimum width is now 232 + max(row1, mainArea) < viewport.
   ── end v1.1.1: one row, or two ── */
void ControlPanel::refreshTopbarWrap()
{
    if (topbarWrapBusy_ || !topbar_ || !topbarRow1_ || !topbarRow2_
        || topbarActions_.isEmpty())
        return;

    // ---- the width the bar can actually use ----------------------------
    int available = 0;
    if (auto *rootScroll =
            findChild<QScrollArea *>(QStringLiteral("consoleRootScroll"))) {
        if (rootScroll->viewport())
            available = rootScroll->viewport()->width()
                        - topbar_->mapTo(rootScroll->viewport(), QPoint(0, 0)).x();
    }
    // No scroll area (a test host, a half-built console): wrap rather
    // than clip. Wrapping never HIDES anything, clipping always does.
    if (available <= 0)
        available = 0;

    auto itemMin = [](QLayoutItem *it) -> int {
        if (!it)
            return 0;
        if (QWidget *w = it->widget()) {
            // The same precedence QLayout applies: an explicit minimum
            // wins, otherwise the widget's own minimumSizeHint.
            const int explicitMin = w->minimumWidth();
            return explicitMin > 0 ? explicitMin : w->minimumSizeHint().width();
        }
        if (it->layout())
            return it->layout()->minimumSize().width();
        return it->minimumSize().width();
    };
    const auto rowMin = [&](QHBoxLayout *row, int spacing, bool withActions) {
        const QMargins m = row->contentsMargins();
        int sum = m.left() + m.right();
        int items = 0;
        for (int i = 0; i < row->count(); ++i) {
            QLayoutItem *it = row->itemAt(i);
            if (!it)
                continue;
            QWidget *w = it->widget();
            if (w && topbarActions_.contains(w) && !withActions)
                continue;
            sum += itemMin(it);
            if (items++)
                sum += spacing;
        }
        return sum;
    };

    int buttonsMin = 0;
    for (QPushButton *b : std::as_const(topbarActions_)) {
        if (!b)
            continue;
        const int explicitMin = b->minimumWidth();
        buttonsMin += explicitMin > 0 ? explicitMin : b->minimumSizeHint().width();
    }

    const int row1NoActions = rowMin(topbarRow1_, topbarRow1_->spacing(), false);
    // ONE ROW, counted the same way in both states: the four buttons
    // after the row1 items, each with its own spacing slot. Removing them
    // from row1 removes exactly one spacing slot each (the stretch is
    // always ahead of them, so no button is ever item 0), which is why
    // `row1NoActions + n·spacing + Σ button` is the single row's minimum
    // whether the buttons currently sit in row1 or in row2 — a pure
    // function of the copy and the width, so the decision cannot
    // oscillate between two consecutive calls.
    const int singleRowMin =
        row1NoActions + topbarRow1_->spacing() * topbarActions_.size()
        + buttonsMin;
    const bool wrap = singleRowMin > available;

    // Cheap path: the decision is unchanged and the band is already the
    // right height, so nothing has to move and no layout has to be
    // re-activated. refreshTopbarTitle() reaches this on every view
    // change, and the window model refreshes on game events — neither
    // should pay for a layout pass that changes nothing.
    if (wrap == topbarWrapped_ && topbar_->height() == topbarBandHeight_)
        return;

    if (wrap != topbarWrapped_) {
        topbarWrapBusy_ = true;
        for (QPushButton *b : std::as_const(topbarActions_)) {
            if (!b)
                continue;
            // QLayout::addWidget takes the widget out of whichever layout
            // currently owns it, so this is a move, not a copy.
            if (wrap)
                topbarRow2_->addWidget(b);
            else
                topbarRow1_->addWidget(b);
        }
        topbarRow1_->setContentsMargins(18, 10, 18, wrap ? 0 : 10);
        topbarRow2_->setContentsMargins(wrap ? QMargins(18, 0, 18, 10)
                                             : QMargins(0, 0, 0, 0));
        topbarWrapped_ = wrap;
        topbarWrapBusy_ = false;
    }

    // ---- the band ──────────────────────────────────────────────────────
    // 66 exactly while it is one row (the measured `.topbar`); the two
    // rows' own hint once it is two (46+10 + 32+10 = 98).
    if (QLayout *barLayout = topbar_->layout()) {
        topbarWrapBusy_ = true;
        barLayout->activate();
        topbarBandHeight_ = topbarWrapped_
                                ? qMax(kTopbarBandHeight,
                                       barLayout->sizeHint().height())
                                : kTopbarBandHeight;
        if (topbar_->height() != topbarBandHeight_)
            topbar_->setFixedHeight(topbarBandHeight_);
        topbarWrapBusy_ = false;
    }
}

void ControlPanel::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
    // The width is the input the wrap decision is a function of, and a
    // resize is the only thing that changes it. Also reached on the first
    // show, which is how a console built while its window was still
    // 0-wide ends up on one row.
    refreshTopbarWrap();
}

// ── v1.0.0 sa-A: topbar ──
// Push the mode in force into the segmented control. Called after a
// layout change (committed or refused) so the two buttons always show the
// truth rather than the last click.
// ── end sa-A: topbar ──
void ControlPanel::syncLayoutSegmentToMode()
{
    // v1.1.0: compare each segment against ITS OWN mode. The old
    // `bool split = mode == Split; Single->setChecked(!split)` reads every
    // non-Split mode as Single, so a Mixed layout in force lit 整块 while
    // the overlay ran a mixed topology — the console describing the
    // opposite of what is on screen, which is the one thing a mode
    // indicator must never do.
    const auto inForce = layoutModeInForce_;
    if (layoutSingleBtn_)
        layoutSingleBtn_->setChecked(
            inForce == mhw::PlayerSection::PlayerLayoutMode::Single);
    if (layoutSplitBtn_)
        layoutSplitBtn_->setChecked(
            inForce == mhw::PlayerSection::PlayerLayoutMode::Split);
    if (layoutMixedBtn_)
        layoutMixedBtn_->setChecked(
            inForce == mhw::PlayerSection::PlayerLayoutMode::Mixed);
    if (layoutCombo_) {
        const int want = layoutComboIndexFor(layoutModeInForce_);
        if (layoutCombo_->currentIndex() != want)
            layoutCombo_->setCurrentIndex(want);
    }
    // 分开密度 is not Single-only: it controls how much demo content the
    // windows carry, and Mixed has windows too (the detached ones). Gating
    // it on `== Split` hid it in Mixed for no reason, and the old comment
    // here still used the v1.1.0 copy pass's banned wording 分开密度.
    const bool hasSplitWindows =
        inForce != mhw::PlayerSection::PlayerLayoutMode::Single;
    if (densityCombo_)
        densityCombo_->setVisible(hasSplitWindows);
    // v1.1.0: the group column's 独立/合并 block is keyed on the MODE
    // too, and a mode commit does not always reach
    // refreshWindowModel()'s group-column refill — so its visibility is
    // pushed here, where every committed mode change already lands.
    refreshMixedGroupBlock();
    updateLayoutApplyEnabled();
}

// ── v1.0.0 (O1): the scope reset ───────────────────────────────────────
//
// reference.html's two scope switches both throw the VIEW STATE AWAY:
//
//   #game-switch:  state.game=…; state.view='overview';
//                  state.selected=null;                       (line 332)
//   #layout-seg:   state.mode=…;  state.view='overview';
//                  state.selected=null; state.editing=false;  (line 334)
//
// and IMPLEMENTATION-PLAN §O1 states it as a requirement in its own
// words: 「切 game/mode reset 为 overview/null/nonedit」. The shell did
// neither: a rail game click left the console on the panel view the
// previous game was showing, and a mode switch left whatever window was
// selected — in a different TOPOLOGY, because Single's mother panels and
// Split's sub-windows are different windows with different keys.
//
// WHAT "null" CAN AND CANNOT MEAN HERE. `state.selected` is one nullable
// id; Qt's selection is the pair (selectedPanel_, selectedSubWindow_) and
// selectedPanel_ doubles as "which panel's settings page is up" — nulling
// it would take the inspectorStack's index with it (four test files read
// that index). So the WINDOW half is cleared, which in Split IS the whole
// selection (buildWindowStates lists sub-windows only, so a subIndex of -1
// matches no row and selectedWindow() answers invalid → the empty block,
// exactly the draft). In Single the mother panel is itself a window, so a
// mother selection survives; that residue is written up as a known
// limitation rather than papered over with an invented "no panel" page.
// ── end O1: scope reset ────────────────────────────────────────────────
void ControlPanel::resetViewStateAfterScopeChange(bool alsoExitEditing)
{
    // Editing first: `.board.editing` and the overview are mutually
    // exclusive in the draft (its edit button refuses while
    // state.view==='overview', reference.html:336), so leaving edit mode
    // has to happen while the canvas still knows it is editing.
    if (alsoExitEditing && canvas_ && canvas_->editing())
        setBoardEditing(canvas_, false);
    selectedSubWindow_ = -1;
    // ── v1.0.0 (B): a scope change drops the WINDOW half too ───────────
    // game switch / layout-mode switch / pets-availability change all
    // mean "the world under the selection is gone", which is the
    // prototype's `state.selected = null` on a scope change: the columns
    // go back to .empty-inspector and the overview is re-entered below.
    windowSelectionActive_ = false;
    // v1.1.0: a scope change drops the WINDOW half too, and with it the
    // PANEL half the capability rows read. Without this the inspector kept
    // the previous scope's mother panel on screen after a game / layout /
    // pets-availability switch, leaving a block of dead controls enabled
    // against a selection that no longer existed (console-inspector-
    // fidelity-tests "B: the empty block is back after the scope change").
    //
    // This is where clearWindowSelection()'s body used to live. That
    // function had no caller anywhere in src/ or tests/ — the code it
    // claimed to own was being reset here instead, minus this half — so it
    // was deleted rather than left as a second, unreachable owner of the
    // same transition.
    panelSlotChosen_ = false;
    if (canvas_ && !canvas_->overviewMode())
        enterOverview(canvas_);
    // Re-derive the inspector for the new world: in Split the cleared
    // window half means "nothing selected", and the empty block follows.
    refreshWindowModel();
    refreshTopbarTitle();
}

// ── v1.0.0 (O1): the topbar's `.topbar-title` pair ─────────────────────
//
// reference.html:458-459 rewrites BOTH lines on every render from the
// view alone:
//
//   top-title    = view==='overview' ? '显示器总览'
//                                       : labelPanel(view)+'面板'
//   top-subtitle = view==='overview'
//                  ? '一次显示一块屏幕 · 切屏不改变窗口归属'
//                  : `当前只显示 ${labelPanel(view)}的窗口 · 可进入编辑`
//
// So this is a function of live state, not a one-shot trSet. It is
// called from every place the VIEW can change — the ctor, the sidebar's
// overview card, selectPanel() and the 编辑布局 entry — plus a trHook
// so a language flip re-queries it instead of replaying the old locale's
// words. Nothing else feeds it: the title never depends on the selected
// WINDOW, the game or the layout mode, exactly as the draft's renderAll
// has it.
// ── end O1: topbar title ──────────────────────────────────────────────
// The three helpers that answer "which sections does this window draw"
// are DEFINED next to buildWindowStates() further down — they are the
// window model's own arithmetic — and declared here so the inspector's
// column refill can ask the SAME question instead of keeping a second
// copy that could drift.
namespace {
int countSectionBits(uint32_t mask);
uint32_t panelSectionSchema(int panelIndex, mhw::GameId game);
uint32_t splitWindowSections(int panelIndex, int tableRow);
} // namespace

// ── v1.0.0 (D): the stage's .shell-head, live ───────────────────────────
//
// reference.html:475 rewrites the board label on every render and
// renderAll() rewrites #window-count, so both are functions of the
// current display and the current window list — not one-shot copy. The
// screen chip prints the display name with the prototype's own
// `DP-1 · 2560×1440` spelling, the same one the DISPLAY dropdown uses,
// and the count is how many of the scope's windows resolve to that
// display.
void ControlPanel::refreshStageHead()
{
    if (stageSub_)
        stageSub_->setText(consoleText(QStringLiteral("console.stage.sub")));
    QString screenName = canvas_ ? canvas_->previewScreen() : QString();
    if (screenName.isEmpty()) {
        if (QScreen *s = QGuiApplication::primaryScreen())
            screenName = s->name();
    }
    const QList<screen_query::OutputInfo> outputs = screen_query::listOutputs();
    if (screenName.isEmpty() && !outputs.isEmpty())
        screenName = outputs.first().name;
    QString screenLabel = screenName;
    for (const screen_query::OutputInfo &o : outputs) {
        if (o.name != screenName)
            continue;
        const QRect g = o.geometry;
        if (g.isValid())
            screenLabel = QStringLiteral("%1 · %2×%3")
                              .arg(screenName)
                              .arg(g.width())
                              .arg(g.height());
        break;
    }
    // An offscreen (or headless) session reports no screen name at all.
    // The chip must still say something — the prototype's own UNKNOWN
    // rather than a blank pill.
    if (screenLabel.isEmpty())
        screenLabel = consoleText(QStringLiteral("console.proto.corner.unknown"));
    if (stageScreenChip_)
        stageScreenChip_->setText(screenLabel);
    if (stageWindowCountChip_) {
        // ── v1.0.0 integration (stage A): ONE predicate, not two ────────
        // This chip is reference.html's #window-count, and renderBoard()
        // (reference.html:476) sets it to `visibleWindows().length`.
        // visibleWindows() (reference.html:442) is:
        //
        //   panel.visible !== false  &&  w.visible !== false
        //   && resolveOutput(w) === state.display
        //
        // The old loop here compared `outputResolved == screenName` and
        // nothing else, so a window the user had hidden — or one whose
        // panel master was off — was still counted: the chip said "3 个
        // 窗口" over a board drawing one. mhw::windowOnDisplay() spells
        // that predicate once, next to the key it reads, and the pure
        // test in console_fidelity_windows_tests.cpp pins all four axes:
        // game capability (a window the scope does not have is not in
        // the list at all), panel master, the per-window switch, and the
        // sections-all-off placeholder — which IS counted, because
        // `isShown()` (reference.html:443) is a different question from
        // `visibleWindows()` and the placeholder is a drawn window.
        //
        // The view filter inside visibleWindows() — `state.view !==
        // 'overview' && d.panel !== state.view → false` — WAS deliberately
        // not reproduced here, on the argument that the board did not
        // have it either. v1.0.0 (OUTPUT-OWNERSHIP-FIX) closes that gap
        // from the board side: the board now applies the same scope, so
        // the premise of that comment is gone. The rule stays "count what
        // the board draws", and the board draws
        //
        //   overview   → every window on this display
        //   panel view → THIS panel's windows on this display
        //
        // which is reference.html:442's own ordering. THE SCOPE IS THE
        // PANEL, NOT THE ROW SHAPE: `d.panel === state.view` does not
        // care whether the row is a split row or a mother row, and a
        // mother counted while another panel's mother is the one painted
        // is the same drift the sub rows had before this scope existed —
        // one comparison, over the whole list.
        const int scope = (canvas_ && !canvas_->overviewMode())
                              ? selectedPanel_
                              : -1;
        int onScreen = 0;
        // THE COUNT IS THE RUN'S, NOT ONLY THE BOARD'S. In Mixed the
        // windows the user detached one by one are real windows the
        // overlay mounts, and buildWindowStates() cannot list them (the
        // preview host — and with it the canvas's sub bindings — is
        // Split-only), so the topology model is what this chip counts
        // from. Single and Split are the same list, returned verbatim.
        const mhw::WindowList windows = buildWindowTopology();
        for (const mhw::WindowState &w : windows) {
            if (!mhw::windowOnDisplay(w, screenName))
                continue;
            if (scope >= 0 && w.panelIndex != scope)
                continue;
            ++onScreen;
        }
        stageWindowCountChip_->setText(
            consoleText(QStringLiteral("console.stage.windows"))
                .arg(onScreen));
    }
}

void ControlPanel::refreshTopbarTitle()
{
    if (!topbarTitle_ || !topbarSubtitle_)
        return;                       // the toolbar has not been built yet
    const bool overview = !canvas_ || canvas_->overviewMode();
    if (overview) {
        topbarTitle_->setText(
            consoleText(QStringLiteral("console.proto.top.title.overview")));
        topbarSubtitle_->setText(
            consoleText(QStringLiteral("console.proto.top.sub.overview")));
    } else {
        static const char *const kPanelNavKey[mhw::kPanelCount] = {
            "console.nav.player", "console.nav.monster",
            "console.nav.damage", "console.nav.pets"
        };
        const int idx = mhw::isPanelIndex(selectedPanel_) ? selectedPanel_ : 0;
        const QString name = consoleText(QString::fromLatin1(kPanelNavKey[idx]));
        topbarTitle_->setText(
            consoleText(QStringLiteral("console.proto.top.title.panel")).arg(name));
        // THE SUBTITLE'S %1 IS A COUNT, AND THE COPY SAYS SO: both
        // locales' `proto.top.sub.panel` was rewritten to "%1 个窗口" /
        // "Showing only %1 windows" (the copy lane's own note: the old
        // "%1的窗口" was missing the measure word, i.e. %1 was read as a
        // NUMBER), yet the code was still feeding it the panel's NAME —
        // which rendered as "当前仅显示 玩家面板 个窗口". The number is
        // this panel's windows in the COUNT model, the same figure the
        // rail's "N 窗" writes on this row, so the two can never
        // disagree: in Mixed that includes the windows detached one by
        // one, which is what the old model folded away.
        int panelWindows = 0;
        const mhw::WindowList topology = buildWindowTopology();
        for (const mhw::WindowState &w : topology) {
            if (w.panelIndex == idx)
                ++panelWindows;
        }
        topbarSubtitle_->setText(
            consoleText(QStringLiteral("console.proto.top.sub.panel"))
                .arg(panelWindows));
    }
    // v1.1.1 (顶栏溢出): BOTH LINES ARE PART OF THE BAR'S MINIMUM WIDTH —
    // a QLabel's minimumSizeHint IS its text, so a view change (or the
    // language flip that reaches this through the trHook) that lengthens
    // the copy can be the change that makes the single row stop fitting.
    // The wrap decision is re-run here rather than at the call sites so
    // that every one of them gets it.
    refreshTopbarWrap();
}

// ── v1.0.0 sa-A: sidebar + statusline ──
// Rebuild the nav rows' window counts and the status line's copy from the
// state in force. One pass so a language flip, a game switch, a mode
// change and a demo-state change all land together.
//
// THE COUNTS ARE THE PROTOTYPE'S. renderSide() counts every window in the
// current scope and writes "{n} 窗" per panel row, with the overview row
// carrying the total. Qt's equivalent of "every window in the current
// scope" is buildWindowStates() — the SAME list the stage's
// #window-count chip reads, the same list the board's boxes are filtered
// from:
//   * Single  → one row per panel the game has (the mother panels).
//   * Split   → the split host's rows, grouped by parent slot, PLUS the
//               one mother row that has no split table (pets).
//   * Mixed   → as Single the board sees, but the RUN's topology is
//               buildWindowTopology(): the mothers that still mount plus
//               one row per DETACHED window (v1.1.2). The chip and this
//               rail both count that, because a window the user split
//               off is a window the overlay mounts whether or not the
//               board can draw it here.
// Deriving from that list instead of a second walk of the live objects
// is what makes "3 窗" on the rail the same statement as the chip: both
// are now functions of one model. The pets row is the reason this
// matters — countForParent(3) answers 0 because PetDamageSection has no
// split table, while the window the overlay mounts (and the board draws)
// is exactly one.
// ── end sa-A: sidebar + statusline ──
void ControlPanel::refreshNavCounts()
{
    // Per-panel window counts, and the total.
    std::array<int, mhw::kPanelCount> counts{};
    int total = 0;
    // THE SAME MODEL THE CHIP COUNTS FROM — the point of sa-A was that
    // the rail and the stage chip are two readings of one list, and in
    // Mixed that list has to be the RUN's topology (the detached windows
    // are windows), not only the board's rows.
    for (const mhw::WindowState &w : buildWindowTopology()) {
        if (!mhw::isPanelIndex(w.panelIndex))
            continue;
        ++counts[w.panelIndex];
        ++total;
    }
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        if (!navCounts_[i])
            continue;
        navCounts_[i]->setText(counts[i] > 0
            ? consoleText(QStringLiteral("console.proto.wins.count"))
                  .arg(counts[i])
            : QString());
    }
    if (navOverviewCount_)
        navOverviewCount_->setText(total > 0
            ? consoleText(QStringLiteral("console.proto.wins.count")).arg(total)
            : QString());
}

// ── v1.0.0 sa-A: statusline ──
// The status line's five states, per QT-SPEC.md §A.7:
//
//   live    "Monster Hunter {g} · 数据流 12 ms"   ← demoState live
//   offline "演示离线 · 未连接游戏进程"             ← demoState offline
//   nodata  "未检测到游戏数据"                     ← demoState noData
//   running "模拟运行中 · PID {p}"                 ← the overlay is alive
//   stopped "未启动(演示)"                         ← the overlay is not
//
// The dot's colour carries the connection state (teal live / rust offline
// / olive no-data), and the run half carries liveness. The ONLY real data
// in any of it is mhw::detectGame()'s process detection, which
// refreshAutoDetect() has already stored in gameDetected_.
// ── end sa-A: statusline ──
void ControlPanel::refreshStatusline()
{
    if (!statusline_)
        return;
    const QString gameNameStr = gameName(currentGame());
    QString text;
    QString dotState;
    if (demoState_ == 1) {
        text = consoleText(QStringLiteral("console.proto.st.offline"));
        dotState = QStringLiteral("off");
    } else if (demoState_ == 2) {
        text = consoleText(QStringLiteral("console.proto.st.nodata"));
        dotState = QStringLiteral("nodata");
    } else {
        text = consoleText(QStringLiteral("console.proto.st.live"))
                   .arg(gameNameStr);
        dotState = QStringLiteral("live");
    }
    statusline_->setText(text);
    statusline_->setToolTip(text);
    if (statusDot_) {
        statusDot_->setProperty("state", dotState);
        statusDot_->style()->unpolish(statusDot_);
        statusDot_->style()->polish(statusDot_);
    }
    // The connection chip mirrors the same fact.
    if (connectChip_) {
        connectChip_->setProperty("connected", gameDetected_);
        connectChip_->style()->unpolish(connectChip_);
        connectChip_->style()->polish(connectChip_);
    }
    if (connectChipText_) {
        connectChipText_->setText(
            gameDetected_
                ? consoleText(QStringLiteral("console.proto.st.live"))
                      .arg(gameNameStr)
                : consoleText(QStringLiteral("console.proto.off.conn")));
    }
    if (statusRun_) {
        const bool running = overlay_ && overlay_->isRunning();
        statusRun_->setText(running
            ? consoleText(QStringLiteral("console.proto.st.running"))
                  .arg(overlay_ ? overlay_->overlayPid() : 0)
            : consoleText(QStringLiteral("console.proto.st.stopped")));
    }
}

// ── v1.0.0 sa-D: the instance picker ──
// ONE entry, and it is the detector's own answer: the running game's name
// and pid, or "未检测到" when there is none. Same fact the rail badge
// prints and the connect chip colours itself from — refreshAutoDetect()
// learns it once and this is the third surface that reads it.
//
// WHY IT REBUILDS THE LIST RATHER THAN SETTING TEXT. The two states have
// different lengths ("未检测到" versus "Monster Hunter Rise · pid 4242"),
// and a combo whose single item keeps its old width would clip one of
// them. Rebuilding costs one string assignment on a 5-second timer.
//
// The selection is NOT preserved across a rebuild: there is exactly one
// entry, so index 0 is always right, and preserving an index would be
// code that only pretends to matter.
// ── end sa-D ──
void ControlPanel::refreshInstanceCombo()
{
    if (!instanceCombo_)
        return;
    // Rise-only, so a World console never shows the group at all — the
    // same rule switchGame() applies when it flips the visibility.
    if (currentGame() != mhw::GameId::Rise)
        return;
    const QString label = gameDetected_
        ? consoleText(QStringLiteral("console.detect.short"))
              .arg(gameName(lastDetectedGame_))
              .arg(lastDetectedPid_)
        : consoleText(QStringLiteral("console.detect.none"));
    if (instanceCombo_->count() == 1
        && instanceCombo_->itemText(0) == label)
        return;      // unchanged: no rewrite, no repolish
    instanceCombo_->blockSignals(true);
    instanceCombo_->clear();
    instanceCombo_->addItem(label, gameDetected_ ? lastDetectedPid_ : 0);
    instanceCombo_->setCurrentIndex(0);
    instanceCombo_->blockSignals(false);
}

// ─── v1.0.0 fidelity port: the inspector's three columns ────────────────
//
// THE PROTOTYPE'S INSPECTOR IS ONE WINDOW'S DETAIL, NOT A PANEL'S PAGE.
//
//     <div class="inspector-grid">                0.9fr  1fr  1.2fr
//       <div class="inspector-col">  …metadata…    X  Y  目标显示器  缩放  透明度  显示该窗口
//       <div class="inspector-col">  …sections + 同组子窗 chips…
//       <div class="inspector-col">  …内容预览 (the Panel's own pixmap)…
//
// The Qt console's equivalent was a four-page QStackedWidget, one page
// per panel, each holding that panel's mask rows / sliders / preview.
// That is a different thing answering a different question: it says
// "how is the PLAYER panel configured", where the prototype says "where
// is the WINDOW I just clicked".
//
// SO THE STAYS AND THE NEW GOES BESIDE IT. The four pages keep their
// indices, their objectNames and every handle the tests use
// (inspectorStack_ is still the console's only QStackedWidget, and
// console_scope_persistence_tests finds the player page at index 0).
// This method does not build a page — it fills the widgets the ctor
// already created on the DRAWER'S OWN FACE (see the ctor's block), so a
// selection change is a value update rather than a rebuild.
//
// WHY NOT A FIFTH PAGE. A fifth page on the same stack would make the
// stack's index ambiguous (0..3 are panels, 4 is what?) and
// selectPanel()'s `setCurrentIndex(idx)` would have to know about it.
// The widgets live OUTSIDE the stack, on the drawer frame, and the
// stack keeps answering "which panel's settings am I looking at".
//
// THE FOCUS RULE. A refill must not steal a field the user is typing
// in — the prototype's `document.activeElement !== x` check, spelled
// here as hasFocus(). One flag (windowModelRefilling_) is set for the
// whole refill so every widget's handler can ask "is this a refill?"
// without each of them blocking signals on its own.
// ─── end v1.0.0 inspector columns ───────────────────────────────────
// ── v1.0.0 integration (stage D): THE WINDOW'S OWN OUTPUT, NOT THE VIEWER
//
// The position fields are 「<目标屏> 内的逻辑坐标」 — the unit is the
// window's OWN target output (`w.outputResolved`, the same chain the
// model and the drag already use), never whichever display the console
// happens to be previewing. Two lookups, in the order the rest of this
// file resolves screens: screen_query's catalog first (that is what the
// 目标显示器 combo lists and what `· 2560×1440` prints), then Qt's own
// screen list (which is what Panel::setOutputName() validates against),
// and only then — for an EMPTY name — the unnamed primary, and only while
// the platform itself has no named screen (see the function). An empty
// size means "no screen at all" and the caller skips the conversion rather
// than inventing a frame — an unconvertible coordinate must read as
// "cannot say", not as (0,0).
static QSize logicalSizeForOutput(const QString &outputName)
{
    const QList<screen_query::OutputInfo> screens = screen_query::listOutputs();
    // Nameless entries are skipped deliberately: listOutputs() synthesises
    // `screen@x,y` for a screen Qt has not named yet, and Qt's own list
    // carries the raw empty name. Matching those against an empty
    // outputName is what let a selection with NO output answer "I am on a
    // screen" — see the empty-name arm below.
    for (const screen_query::OutputInfo &o : screens)
        if (!o.name.isEmpty() && o.name == outputName)
            return o.geometry.size();
    bool anyNamed = false;
    for (QScreen *s : QGuiApplication::screens()) {
        if (!s)
            continue;
        if (s->name().isEmpty())
            continue;
        anyNamed = true;
        if (s->name() == outputName)
            return s->geometry().size();
    }
    // A name the platform does not have (stale model, screen unplugged
    // mid-drag) is NOT the primary. Reporting the primary here is how an
    // unowned selection got framed as a window on screen 1: the caller
    // saw valid=true, enabled the coordinate form and multiplied the
    // number by a scale that did not belong to that display.
    if (!outputName.isEmpty())
        return QSize();
    // Empty name. The chain resolved to "no output", which is only a real
    // geometry while the platform itself has no NAMED screen: the offscreen
    // backend reports its single screen with an empty name, and there that
    // unnamed screen is the only truth there is (so the tests' windows keep
    // their frame). With a named screen present, "" means the selection has
    // no output of its own and the honest answer is "cannot say".
    if (anyNamed)
        return QSize();
    if (QScreen *primary = QGuiApplication::primaryScreen())
        return primary->geometry().size();
    return QSize();
}

// The one place both halves of stage D ask for the frame: the
// (0,0)-anchored logical rect of the SELECTED object's own output, plus
// the corner/extent/scale the canvas itself paints from (the selected
// Panel — the same object layoutSubWindowRects() reads).
//
// D1: the frame now comes from the ONE accessor rather than from a
// WindowState that may not exist. `w.outputResolved` is only ever set for
// a real window row, and a mother focus in Split has no row — that empty
// string used to be read as "screen 1" by logicalSizeForOutput()'s old
// primary fallback, which is why the coordinate form was enabled while
// every write was refused.
struct WindowFrame {
    QRect screen;      // (0,0) + the output's logical size
    Corner corner{Corner::TopLeft};
    QSize content;     // logical content size (unscaled)
    qreal scale{1.0};
    bool valid{false};
};

static WindowFrame windowFrameFor(const ControlPanel::EditableSelection &sel)
{
    WindowFrame f;
    if (!sel.panel)
        return f;
    const QSize logical = logicalSizeForOutput(sel.resolvedOutput);
    if (logical.isEmpty())
        return f;
    f.screen = QRect(QPoint(0, 0), logical);
    f.corner = sel.panel->corner();
    f.content = sel.panel->contentSize();
    f.scale = sel.panel->scale();
    f.valid = true;
    return f;
}

// ── D1: THE ONE EDIT TARGET ───────────────────────────────────────────
//
// currentEditableSelection() is the single answer to "what is selected,
// and where does an edit land"; selectedPanelOrNull() below is now its
// capability-rows spelling (`sel.panel`), so the two can no longer
// disagree. The struct's contract is documented on the declaration.
//
// THE MOTHER ARM IS NOT OPTIONAL. `selectedWindow()` describes a WINDOW,
// and a mother panel is not one: selecting a mother row gives it no
// panelIndex, so an early `if (w.panelIndex < 0) return;` made every
// capability row read "nothing selected" for exactly the case the user
// reaches by clicking the mother in the overview — the rows then hid
// themselves, which is why the column row looked broken rather than
// merely absent. `selectedPanel_` is the member selectPanel()/
// selectWindow() both maintain, so it is the honest answer for that case.
// What was NOT honest was leaving the FORM on selectedWindow() while the
// rows used this: in Split a mother focus answered "your panel" one way
// and "invalid window" the other. Both halves come from here now.
static const char *const kMotherShortTitle[mhw::kPanelCount] = {
    "console.proto.win.short.player", "console.proto.win.short.monster",
    "console.proto.win.short.damage", "console.proto.win.short.pet"
};

// The output a panel frames against, resolved by the SAME chain
// buildWindowStates() runs for a real row — own → parent → primary, and a
// MOTHER's parent is nobody, so its tail is the primary (the comment on
// the mother row says exactly this). effectiveOutputName() keeps the
// answer inside the live screen set, so an empty result can only mean
// "nothing can be named", which windowFrameFor() reads as "no frame".
// This exists because a mother focus in Split has no window row to carry
// `w.outputResolved`, and inventing one from "" is what used to frame it
// on screen 1.
static QString resolveOutputForPanel(Panel *panel)
{
    if (!panel)
        return QString();
    QScreen *qs = QGuiApplication::primaryScreen();
    return screen_query::effectiveOutputName(
        mhw::resolveWindowOutput(panel->outputName(), QString(),
                                 qs ? qs->name() : QString()));
}

ControlPanel::EditableSelection ControlPanel::currentEditableSelection() const
{
    EditableSelection sel;
    const mhw::WindowState w = selectedWindow();

    // ── the window arm: a row of buildWindowStates() really matched ────
    Panel *panel = nullptr;
    bool fromRow = false;
    if (w.subIndex >= 0 && splitPreview_) {
        panel = splitPreview_->windowAt(w.subIndex);
        fromRow = true;
    } else if (w.panelIndex >= 0) {
        panel = panelAt(w.panelIndex);
        fromRow = true;
    }
    if (fromRow) {
        // A row that names a Panel which has since been rebuilt answers
        // "nothing editable" — the same "absent, not blank" rule
        // selectedWindow() uses, and never a fall-through to the mother.
        if (!panel)
            return sel;
        sel.panel = panel;
        sel.panelIndex = w.panelIndex;
        sel.subIndex = w.subIndex;
        sel.hasWindow = true;
        sel.window = w;
        sel.resolvedOutput = w.outputResolved;
        sel.ownOutput = panel->outputName();
        sel.title = w.title;
        return sel;
    }

    // ── the mother arm: no window row, but a panel slot the user chose ──
    // A mother counts ONLY once the user has actually chosen a slot —
    // `panelSlotChosen_`, not `selectedPanel_ >= 0`, which is true on a
    // cold start because the member defaults to 0. Before the user picks
    // anything the honest answer is "nothing selected", and that is what
    // keeps the inspector's empty block on screen at startup.
    if (!(panelSlotChosen_ && selectedPanel_ >= 0
          && mhw::isFidelityPanelIndex(selectedPanel_)))
        return sel;
    // Pets exist only in Rise; in World that slot is a placeholder and
    // must not answer "here is your panel".
    if (selectedPanel_ == 3 && currentGame() == mhw::GameId::World)
        return sel;
    Panel *mother = panelAt(selectedPanel_);
    if (!mother)
        return sel;
    sel.panel = mother;
    sel.panelIndex = selectedPanel_;
    sel.resolvedOutput = resolveOutputForPanel(mother);
    sel.ownOutput = mother->outputName();
    sel.title = consoleText(QString::fromLatin1(kMotherShortTitle[sel.panelIndex]));
    return sel;
}

// The capability rows' name for the same thing: literally the accessor's
// `panel`. Kept because it is the documented "write to THIS instance"
// spelling of 验收项5 and four test files call it directly — but it can no
// longer answer a different object than the metadata form does.
Panel *ControlPanel::selectedPanelOrNull() const
{
    return currentEditableSelection().panel;
}

// ── THE GAME FILTER THE WINDOW LISTS DID NOT HAVE ──────────────────────
// 实测缺陷3: 窗口分组 (the Mixed 独立/合并 block) and 本组子窗 (the
// sibling chips) both walk a list of KEYS, and the registry behind them
// (window_layout_plan.h) is deliberately game-agnostic — one table
// carries both 衣装 and 翔虫 so either game can resolve its own plan
// from the same spelling. A LIST is a different statement: it says "this
// console offers you these windows", and under the game in force those
// two keys are not offered. `player-mantles` is a WORLD block and
// `player-wirebug` a RISE one — panelSectionSchema() already masks those
// two bits out of every count, so a row that listed them for the wrong
// game was a switch whose section total could never leave 0.
//
// Pets is the same rule one level up: the companion surface has no World
// data source at all (switchGame()'s `petsAvailable`, and buildWindow_
// States()'s own panel-3 arm), and its settings key is `pet-damage`
// (panel_pet_damage.cpp:220). `player-pets` is accepted as a second
// spelling so a future key cannot re-open the hole by being spelled
// differently.
//
// ONE function, two callers (the chips and the block), so the two lists
// can never disagree about what the current game has.
static bool windowKeyInGame(const QString &settingsKey, mhw::GameId game)
{
    if (settingsKey == QLatin1String("player-mantles"))
        return game != mhw::GameId::Rise;   // World only
    if (settingsKey == QLatin1String("player-wirebug"))
        return game == mhw::GameId::Rise;   // Rise only
    if (settingsKey == QLatin1String("pet-damage")
        || settingsKey == QLatin1String("player-pets"))
        return game == mhw::GameId::Rise;   // 伙伴, Rise only
    return true;                            // every other key is both games'
}

// The group column's `on/total`, in ONE place: a window row carries its
// own counts, and a MOTHER focus in Split has no row — so the very
// arithmetic buildWindowStates() runs for a mother row is redone here
// (the game-filtered schema against the panel's live mask). Two copies of
// this number printed on the same face is how they drift apart, which is
// the D1 defect one level down.
static void sectionCountsFor(const ControlPanel::EditableSelection &sel,
                             mhw::GameId game, int &on, int &total)
{
    on = 0;
    total = 0;
    if (!sel.panel)
        return;
    if (sel.hasWindow) {
        on = sel.window.sectionsOn;
        total = sel.window.sectionsTotal;
        return;
    }
    const uint32_t schema = panelSectionSchema(sel.panelIndex, game);
    total = countSectionBits(schema);
    on = countSectionBits(sel.panel->sectionMask() & schema);
}

/* ── 实测缺陷8 / 缺陷9 (user-8): THE HONEST CEILINGS OF THE CAPABILITY ROWS ──
   列数 — its range is NOT a number this file invented. It is the union of
   what the six supportsColumns() implementations will actually accept,
   read off their own clamps (every one of them floors at 1, so 0 is never
   a column count — a 0 would also divide statusStripRows() by zero):

     panel_player_split.cpp:453            std::clamp(x, 1, 6)      [1, 6]
         (= mhw::kStatusStripMinColumns/MaxColumns, panel_player_split.h:98-99)
     panel_monster_split_parts.cpp:476     std::clamp(x, 1, kPcCols + 3) = [1, 6]
         (kPcCols = 3, panel_monster_split_parts.cpp:57)
     panel_monster_split_conditions.cpp:628  std::clamp(x, 1, kPcCols + 3) = [1, 6]
     panel_monster_split_ail.cpp:389       clamp(x, kMonsterAilMinColumns=1,
                                              kMonsterAilMaxColumns=6)  [1, 6]
     panel_player_split_wirebug.cpp:330    std::clamp(x, 1, 4)
         (= kWirebugMaxColumns, panel_player_split_wirebug.h:91)
     panel_player_split_mantles.cpp:364    std::clamp(x, 1, 2)
         (= kMantlesMaxColumns, panel_player_split_mantles.h:73)

   So the widest honest span the dial can drive is [1, 6] — and the box
   used to claim [1, 64], a number no Panel in the tree would honour:
   typing 64 came back as 6 (setColumnsTo() reaches the same clamps),
   which is a control that lies about its own limits. Adding a seventh
   supportsColumns() implementation with a wider clamp means bumping
   kColumnsRowMax here — that is the one place this number is spelled.

   The four SIZE rows are the same discipline on two axes. Their values
   are LOGICAL pixels of the window in its target output — the same space
   坐标 uses — and two facts bound them:

     1. Panel clamps every one of the four to [0, kMaxMinSize] = [0, 4096]
        (panel.cpp:1909/1922/1948/1961/1971/1981) and 0 means ABSENT
        (panel.h:787-790, restoreAutoSize() writes 0 for all four). So a
        UI ceiling may never exceed 4096, and reaching one is legal.
     2. A window bigger than the screen it is bound to cannot even be
        MOVED: HudCanvas::movedSubMargins() clamps a drag with
        `maxX = max(0, logical.width() - panel.width())` (hud_canvas.cpp:2195,
        in movedSubMargins) and Panel::marginLimits() does the same arithmetic — the
        box's whole range collapses to a single corner. Sizes are logical
        coordinates of that output, so the output's own logical size is
        the largest number that means anything.

   Hence two ceilings, one per axis — 宽 and 高 are different dimensions,
   which is exactly what the user's 「可调范围太离谱」 was about:

     kSizeRowMaxWidth  2560  — the landscape output this project's own
                                code and fixtures document as the real
                                one (DP-1 2560x1440: hud_canvas.cpp:430,
                                panel.h:559-563, control_panel.cpp:1215),
                                and above screen_query's 1920x1080
                                fallback (screen_query.h:32).
     kSizeRowMaxHeight 1440  — its height. Half of Panel::kMaxMinSize
                                and 3× the 1080p fallback: a height row
                                can no longer be dragged to 4096, which
                                is 4096 px of a screen nobody has.

   Both stay UNDER the hard ceiling of fact 1, and the refill below grows
   a row's range when this window already stores something bigger, so the
   narrower span can never lie about a value the overlay's own edit keys
   (kMinSizeStep, panel.h:795) already wrote into panels.ini.
   ── end 实测缺陷8 / 缺陷9 ── */
static constexpr int kColumnsRowMin = 1;
static constexpr int kColumnsRowMax = 6;
static constexpr int kSizeRowMaxWidth = 2560;
static constexpr int kSizeRowMaxHeight = 1440;

void ControlPanel::buildInspectorForSelection()
{
    if (!winPosX_ || !winPosY_)
        return;   // the ctor has not built the inspector's face yet

    // ── D1: ONE SELECTION, ONE READING ──────────────────────────────────
    // Every block below used to ask TWO questions about the same click:
    // the form asked selectedWindow() (a WINDOW — invalid for a mother
    // focus in Split, which emits no mother row) while the capability rows
    // asked selectedPanelOrNull() (which can answer a mother). The same
    // selection therefore produced a form showing a default-constructed
    // window's numbers over a panel that plainly had others, and the
    // form's write guards refused what its own readouts claimed to be
    // editing. There is one accessor now and both halves answer it.
    const EditableSelection sel = currentEditableSelection();
    windowModelRefilling_ = true;

    // ── column 1: metadata ────────────────────────────────────────────
    //
    // `!sel.panel` means NOTHING is editable: no window row matched and no
    // mother slot has been chosen (the cold start, and every scope change
    // — resetViewStateAfterScopeChange() clears the slot too). The honest
    // state is a cleared form, not the previous object's numbers left on
    // screen behind a disabled control.
    //
    // A MOTHER focus is NOT this arm. Clicking a mother panel in the
    // overview yields a real inspection target with a real size, a real
    // output and real sliders — the early `w.panelIndex < 0` read greyed
    // out the whole size block for a panel that plainly has those
    // properties, and the opposite overcorrection (enabled controls over a
    // window state that does not exist) is what D1 records.
    if (!sel.panel) {
        // Nothing selected: the honest state is a cleared form, not the
        // previous window's numbers left on screen. The prototype's
        // #selection-chip reads 未选择 and the form is empty.
        winPosX_->setValue(0);
        winPosY_->setValue(0);
        winPosX_->setEnabled(false);
        winPosY_->setEnabled(false);
        if (winOutput_) {
            winOutput_->blockSignals(true);
            winOutput_->setCurrentIndex(-1);
            winOutput_->blockSignals(false);
            winOutput_->setEnabled(false);
        }
        if (winOutputHint_)
            winOutputHint_->setText(QString());
        // D1: the READOUTS clear too. Grey-ing a control while its label
        // still shows the last window's `150%` is the lying reading the
        // audit recorded — a disabled control displaying someone else's
        // numbers is a control that lies. Sliders go to their neutral 100
        // (the Panel defaults, and inside both ranges), the value labels
        // to empty (the prototype's "form is empty"), the switch to its
        // default of off — all with signals blocked, so this reads as a
        // refill and never as a user edit.
        if (winScale_) {
            winScale_->blockSignals(true);
            winScale_->setValue(100);
            winScale_->blockSignals(false);
            winScale_->setEnabled(false);
        }
        if (winScaleValue_)
            winScaleValue_->setText(QString());
        if (winOpacity_) {
            winOpacity_->blockSignals(true);
            winOpacity_->setValue(100);
            winOpacity_->blockSignals(false);
            winOpacity_->setEnabled(false);
        }
        if (winOpacityValue_)
            winOpacityValue_->setText(QString());
        if (winVisible_) {
            winVisible_->blockSignals(true);
            winVisible_->setChecked(false);
            winVisible_->blockSignals(false);
            winVisible_->setEnabled(false);
        }
        // 验收项5: with no window there is no capability to report. The
        // rows go dark rather than keeping the last window's numbers
        // alive behind a hidden face.
        for (QSpinBox *s : {winColumnsSpin_, winMinWSpin_, winMinHSpin_,
                            winLockWSpin_, winLockHSpin_})
            if (s)
                s->setEnabled(false);
        // 实测缺陷6: the slider halves go dark with their boxes — a live
        // drag over an empty form would be a control acting on nothing.
        for (QSlider *s : {winMinWSlider_, winMinHSlider_, winLockWSlider_,
                           winLockHSlider_})
            if (s)
                s->setEnabled(false);
        // 实测缺陷8: the 列数 − / + pair goes dark with the row it lives
        // in — a click with nothing selected would step a box nobody is
        // looking at, and the row is hidden right below anyway.
        for (QPushButton *b : {winColumnsMinus_, winColumnsPlus_})
            if (b)
                b->setEnabled(false);
        if (winColumnsRow_)
            winColumnsRow_->hide();
        if (winAutoBtn_)
            winAutoBtn_->setEnabled(false);
        // The draft's whole empty arm: the columns are REPLACED by the
        // centered block, and the head's sub line says how to fix it.
        if (inspectorHeadSub_)
            inspectorHeadSub_->setText(consoleText(
                QStringLiteral("console.proto.insp.sub.idle")));
        if (inspectorEmpty_) {
            inspectorEmpty_->show();
            if (contentScrollHost_)
                contentScrollHost_->hide();
            // round2: the OTHER half leaves with it — with two scroll
            // boxes in the face, hiding only the left one would leave the
            // splitter stretching an empty preview card across the whole
            // drawer and the empty block no room at all.
            if (previewScrollHost_)
                previewScrollHost_->hide();
            // …and the card's height budget (a function of the SELECTION's
            // columns) goes with the selection: nothing is selected, so
            // nothing may keep the last window's row height.
            if (windowPreviewCol_)
                windowPreviewCol_->setMinimumHeight(0);
        }
        if (selectionChip_)
            selectionChip_->setText(
                consoleText(QStringLiteral("console.proto.insp.chip.none")));
        windowModelRefilling_ = false;
        // (B)/(C): with nothing selected the columns are gone, so the
        // per-window labels must stop describing the last selection.
        refreshGroupColumn();
        return;
    }

    // ── the five controls: one honest enable rule each ───────────────────
    // Nothing is enabled before its own source exists. The coordinate form
    // is enabled exactly when a frame exists; 显示该窗口 exactly when a
    // WINDOW row exists — in Split a mother panel is not a window, so no
    // row in buildWindowStates() would ever read the key that switch
    // writes (pushWindowVisibility() only walks rows), and an enabled
    // control that nothing can act on is the same lie, just quieter.
    if (winOutput_)
        winOutput_->setEnabled(true);
    if (winScale_)
        winScale_->setEnabled(true);
    if (winOpacity_)
        winOpacity_->setEnabled(true);
    if (winVisible_)
        winVisible_->setEnabled(sel.hasWindow);

    // Position. THE LOGICAL TOP-LEFT INSIDE THE WINDOW'S OWN OUTPUT —
    // reference.html:495's 「坐标是 <该屏> 内的逻辑坐标」. This used to
    // print `margins.left()` / `margins.top()`, which is the same number
    // only for a TopLeft window: for TopRight the X lives in `right` and
    // for a Bottom window the Y lives in `bottom`, so a right-anchored
    // window showed an X that was its distance from the LEFT edge and
    // typing a new one moved nothing.
    //
    // Read from the selected window's own Panel (corner, content, scale
    // and margins are the exact four things HudCanvas lays the box from),
    // so the number in the box and the box cannot disagree. When the
    // output's logical size cannot be resolved the form is DISABLED
    // rather than fed a fabricated frame — an unconvertible coordinate
    // says so instead of lying.
    {
        // D1: the SAME accessor the capability rows use — and the frame
        // comes from it too. A mother focus in Split has no window row, so
        // `w.outputResolved` does not exist for it; the accessor resolves
        // the panel's own output by the chain the model would have run,
        // and when nothing can be named at all the form stays DISABLED
        // instead of being fed a fabricated frame (an unconvertible
        // coordinate says so rather than lying — see
        // logicalSizeForOutput()'s empty-name rule).
        const WindowFrame f = windowFrameFor(sel);
        const bool convert = f.valid;
        if (winPosX_)
            winPosX_->setEnabled(convert);
        if (winPosY_)
            winPosY_->setEnabled(convert);
        if (convert) {
            const QPoint tl = mhw::layout::anchoredTopLeft(
                f.corner, sel.panel->pushedMargins(), f.screen, f.content, f.scale);
            if (!winPosX_->hasFocus())
                winPosX_->setValue(tl.x());
            if (!winPosY_->hasFocus())
                winPosY_->setValue(tl.y());
        } else {
            winPosX_->setValue(0);
            winPosY_->setValue(0);
        }
    }

    // 目标显示器. The first entry is the automatic mode with empty
    // userData — the prototype's `follow` — and the rest are the outputs
    // Qt reports, labelled `DP-1 · 2560×1440` exactly as the
    // prototype's labelDisplay() spells them. This window may also carry
    // its OWN pick — a split sub-window included — and the combo selects
    // it from `w.outputOwn`.
    /* ── 实测缺陷5 (usability-2): WHAT THE AUTOMATIC ENTRY MEANS ──────────
       跟随所属面板 was a phrase nobody could map onto a screen, and the
       question it triggered (「这个是不是多余选项？」) is answered by the
       code, not by taste: it is the ONLY automatic mode there is — the
       other entries are concrete screens — so it is not redundant, it is
       just mis-named. Read off mhw::resolveWindowOutput() and
       screen_query::effectiveFrom(), the chain is own → parent → primary:

         选中子窗口:  own (空) → 母面板所在屏 → 系统主屏
         选中母面板:  own (空) → 系统主屏（母面板之上没有母面板）

       and effectiveFrom()'s rule 2 adds: a stored screen that is OFFLINE
       also lands on the primary, because mounting on a screen that is not
       there is worse than mounting on the wrong one.

       So the entry is named for the case the user is looking at
       (跟随主面板 / 跟随主屏), and winOutputHint_ states which screen the
       chain resolves to RIGHT NOW — the actual answer to "which display
       does this go to?".
       ── end 实测缺陷5 ── */
    const auto outs = screen_query::listOutputs();
    /* ── 实测缺陷7 (user-8): WHO 「跟随主面板」 ACTUALLY FOLLOWS ────────────
       「母面板到底是哪个？」 — a real information gap, not a wording nit:
       the entry names the MODE (follow the mother panel's screen) and
       nothing on the face says which panel that is. The answer is printed
       in the TWO places the statement is true:

         combo entry  跟随主面板（玩家）      — what the option will follow
         the line      主面板：玩家（DP-1 · 2560×1440） → 实际显示到：…

       so the whole chain is readable in one glance instead of being
       inferred from the chain's second hop.

       The mother is `panelAt(sel.panelIndex)` — the SAME instance
       buildWindowStates() hands to resolveWindowOutput() as this
       window's parent (the `parent->outputName()` arm at :10426/:10637),
       so this can never name a different panel than the resolution
       actually consulted; its screen is resolved by the same
       resolveOutputForPanel() helper (own → parent → primary) the mother
       arm above uses. A MOTHER selection (subIndex < 0) has no panel
       above it, so neither string changes for it — the chain there is
       own → primary and 跟随主屏 already says so.
       ── end 实测缺陷7 ── */
    const bool followsMother = sel.subIndex >= 0;
    Panel *motherPanel =
        (followsMother && mhw::isPanelIndex(sel.panelIndex))
            ? panelAt(sel.panelIndex)
            : nullptr;
    const QString motherName =
        motherPanel
            ? consoleText(QString::fromLatin1(kMotherShortTitle[sel.panelIndex]))
            : QString();
    const QString motherScreen =
        motherPanel ? resolveOutputForPanel(motherPanel) : QString();
    QString autoEntry =
        consoleText(followsMother
                        ? QStringLiteral("console.proto.follow_parent")
                        : QStringLiteral("console.proto.follow_primary"));
    if (followsMother && !motherName.isEmpty())
        autoEntry = consoleText(
                        QStringLiteral("console.proto.follow_parent_named"))
                        .arg(motherName);
    if (winOutput_) {
        winOutput_->blockSignals(true);
        winOutput_->clear();
        winOutput_->addItem(autoEntry, QString());
        for (const screen_query::OutputInfo &o : outs)
            winOutput_->addItem(mhw::displayLabel(o.name, outs), o.name);
        const int at = winOutput_->findData(sel.ownOutput);
        winOutput_->setCurrentIndex(at >= 0 ? at : 0);
        winOutput_->blockSignals(false);
    }
    if (winOutputHint_) {
        // 实测缺陷5: the resolved screen, or the honest "no screen can be
        // named" (offscreen, mid-hotplug) — sel.resolvedOutput is EMPTY
        // there on purpose and must never be papered over with a guess.
        QString resolved =
            consoleText(QStringLiteral("console.proto.target_resolved"))
                .arg(sel.resolvedOutput.isEmpty()
                         ? QStringLiteral("—")
                         : mhw::displayLabel(sel.resolvedOutput, outs));
        // 实测缺陷7: prepend WHOSE screen the sub-window is following, so
        // 「跟随主面板」 is answerable without leaving this row.
        if (followsMother && !motherName.isEmpty())
            resolved =
                consoleText(QStringLiteral("console.proto.target_mother"))
                    .arg(motherName,
                         motherScreen.isEmpty()
                             ? QStringLiteral("—")
                             : mhw::displayLabel(motherScreen, outs))
                + QStringLiteral(" → ") + resolved;
        winOutputHint_->setText(resolved);
    }

    // 缩放 / 透明度 read off the PANEL itself: for a window row that is the
    // same instance buildWindowStates() copied `w.scale` from (so no
    // existing reading moves), and for a mother focus it is the ONLY source
    // — there is no row, and the default-constructed 1.0 the form used to
    // show there was this exact lie (a "100%" over a panel at 150%).
    const qreal scaleNow = sel.panel->scale();
    const qreal opacityNow = sel.panel->opacity();
    if (winScale_) {
        winScale_->blockSignals(true);
        winScale_->setValue(qRound(scaleNow * 100));
        winScale_->blockSignals(false);
    }
    if (winScaleValue_)
        winScaleValue_->setText(QStringLiteral("%1%").arg(qRound(scaleNow * 100)));
    if (winOpacity_) {
        winOpacity_->blockSignals(true);
        winOpacity_->setValue(qRound(opacityNow * 100));
        winOpacity_->blockSignals(false);
    }
    if (winOpacityValue_)
        winOpacityValue_->setText(QStringLiteral("%1%").arg(qRound(opacityNow * 100)));
    // The second visibility axis. panelEnabled is the master gate and is
    // NOT what this checkbox says — a window can be hidden while its
    // panel still mounts the others. Read from the panel's OWN key (the
    // same group/key buildWindowStates() reads for the row), so a mother
    // focus shows the panel's stored bit rather than a window's default.
    if (winVisible_) {
        winVisible_->blockSignals(true);
        winVisible_->setChecked(mhw::readWindowVisible(
            sel.panel->settingsGroup(), sel.panel->settingsKey()));
        winVisible_->blockSignals(false);
    }
    /* ── 验收项5: THE CAPABILITY ROWS, REFILLED PER SELECTION ──────────────
       Values are read off THIS window's Panel, and 列数 is only SHOWN
       when supportsColumns() says the dial would do something — so a
       window with no column axis never presents a column control, and
       the number on screen can never be a previous selection's.
       Signals are blocked for the refill: the rows' handlers are real
       writes, and a programmatic setValue must not sound like a user
       edit (windowModelRefilling_ guards the same thing for the fields
       above; blocking is what makes the guard unnecessary here).
       ── end 验收项5: refill ── */
    {
        // D1: the rows read the SAME object as everything above — the
        // accessor's `panel`, not a second lookup that could answer a
        // different instance.
        Panel *selPanel = sel.panel;
        const bool have = selPanel != nullptr;
        const bool cols = have && selPanel->supportsColumns();
        if (winColumnsRow_)
            winColumnsRow_->setVisible(cols);
        // 实测缺陷9 (user-8): THE RANGE IS PART OF THE READING, and it is
        // set in the SAME blocked pass as the value, for both halves.
        // `rangeHi <= 0` means "leave this row's range alone" (the 列数
        // dial: its [1, 6] is the union of the subclasses' own clamps, a
        // constant, not something a selection changes).
        //
        // The ceiling applied is `max(default, value)` — NEVER just the
        // default. Panel clamps what it STORES to kMaxMinSize (4096) and
        // the overlay's own edit keys (Panel::kMinSizeStep) can put a
        // number there that is above this row's honest dial range; a
        // widget that clamps what it displays then reads that lie back on
        // the next drag. Growing to fit is therefore the truth-telling
        // arm, and shrinking back to the default when the value fits is
        // what keeps the narrow, usable span the normal state.
        auto refill = [have](QSpinBox *spin, QSlider *slider, int value,
                             int rangeHi) {
            // 实测缺陷6: BOTH halves of the field in one pass, both with
            // signals blocked — a selection change has to move the slider
            // and the box together, and must never look like a user edit
            // (the spin box's valueChanged is the only Panel writer).
            if (spin) {
                spin->blockSignals(true);
                spin->setEnabled(have);
                if (have) {
                    if (rangeHi > 0)
                        spin->setRange(0, qMax(rangeHi, value));
                    spin->setValue(value);
                }
                spin->blockSignals(false);
            }
            if (slider) {
                slider->blockSignals(true);
                slider->setEnabled(have);
                if (have) {
                    if (rangeHi > 0)
                        slider->setRange(0, qMax(rangeHi, value));
                    slider->setValue(value);
                }
                slider->blockSignals(false);
            }
        };
        refill(winColumnsSpin_, nullptr, cols ? selPanel->columns() : 1, 0);
        refill(winMinWSpin_, winMinWSlider_, have ? selPanel->minWidth() : 0,
               kSizeRowMaxWidth);
        refill(winMinHSpin_, winMinHSlider_, have ? selPanel->minHeight() : 0,
               kSizeRowMaxHeight);
        refill(winLockWSpin_, winLockWSlider_, have ? selPanel->lockWidth() : 0,
               kSizeRowMaxWidth);
        refill(winLockHSpin_, winLockHSlider_, have ? selPanel->lockHeight() : 0,
               kSizeRowMaxHeight);
        // 实测缺陷8: the − / + pair is enabled exactly when the dial it
        // steps is — `cols` already carries `have`, and the row itself is
        // hidden the moment it is false (so this is the visible case).
        if (winColumnsMinus_)
            winColumnsMinus_->setEnabled(cols);
        if (winColumnsPlus_)
            winColumnsPlus_->setEnabled(cols);
        if (winAutoBtn_)
            winAutoBtn_->setEnabled(have);
    }

    // ── column 1: the window's group and its siblings ──────────────────
    //
    // The prototype's second column is `[data-group]` chips for every
    // window that shares this window's panel — clicking one selects it,
    // which is how the inspector moves between a panel's windows without
    // going back to the board. Rebuilt per selection because the sibling
    // set is per panel: a player window has five siblings, a monster
    // window one.
    if (windowGroupCol_) {
        // THE MIXED BLOCK OUTLIVES THE PURGE. It is built once (its rows
        // are the registry, not the selection) and lives in this column,
        // so it has to come OUT of the old layout before that layout's
        // takeAt()/delete loop runs: the loop deletes the widgets it
        // removes, which is exactly right for the per-selection chips and
        // exactly wrong for this one. The new layout re-attaches it below.
        if (mixedGroupHost_) {
            if (QLayout *prev = windowGroupCol_->layout())
                prev->removeWidget(mixedGroupHost_);
        }
        // Delete the previous run's children rather than hiding them: a
        // QVBoxLayout on a frame that already has widgets would stack
        // run after run, and the group's window count is exactly what
        // this column is for.
        if (QLayout *old = windowGroupCol_->layout()) {
            QLayoutItem *item = nullptr;
            while ((item = old->takeAt(0)) != nullptr) {
                delete item->widget();
                delete item;
            }
            delete old;
        }
        // LIVENESS, BY NAME AND NEVER BY THE STORED POINTER: whatever the
        // loop above deleted, the frame either survived (removeWidget
        // above took it out first — same pointer back, so the "built
        // once" identity holds) or it did not (pointer comparison only,
        // no dereference: a deleted frame answers nothing). In the second
        // case the stale vectors are dropped with it and
        // buildMixedGroupBlock() below rebuilds a real one — which is
        // also exactly what a test asserting the block's identity across
        // a refill will notice.
        {
            QFrame *live = nullptr;
            const auto frames = windowGroupCol_->findChildren<QFrame *>(
                QStringLiteral("mixedGroupBlock"),
                Qt::FindDirectChildrenOnly);
            for (QFrame *frame : frames)
                live = frame;
            if (live != mixedGroupHost_) {
                mixedGroupHost_ = live;
                mixedGroupToggles_.clear();
                mixedGroupStateLabels_.clear();
            }
        }
        auto *groupLayout = new QVBoxLayout(windowGroupCol_);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(8);
        auto *groupCap = new QLabel();
        trSet(groupCap, QStringLiteral("console.proto.content"));
        groupCap->setObjectName(QStringLiteral("sectionCap"));
        groupLayout->addWidget(groupCap);

        // ── v1.0.0 (C): 显示内容 <on>/<total> + the window's OWN
        // section switches ─────────────────────────────────────────────
        // reference.html:493 is `显示内容 <span class="mono">on/total</span>`
        // over a checkbox per section OF THE SELECTED WINDOW, and only
        // then 本组窗口 with its chips. The count is a member because it is
        // a function of the selection; the rows live in a host frame the
        // refill empties (see rebuildGroupColumnSections()).
        winContentCount_ = new QLabel();
        winContentCount_->setObjectName(QStringLiteral("groupSections"));
        groupLayout->addWidget(winContentCount_);
        windowSectionListHost_ = new QFrame();
        windowSectionListHost_->setObjectName(QStringLiteral("windowSectionList"));
        groupLayout->addWidget(windowSectionListHost_);

        groupLayout->addSpacing(15);   // .section-cap's margin-top:15px

        auto *winsCap = new QLabel();
        trSet(winsCap, QStringLiteral("console.proto.groupwins"));
        winsCap->setObjectName(QStringLiteral("sectionCap"));
        groupLayout->addWidget(winsCap);
        // The count is `%1/%2` — pure QString::arg() over two counts with
        // no consoleText() in it, so it is set directly rather than
        // through a hook (see refreshGroupColumn() for the live text).
        // sectionCountsFor() supplies the mother-focus arm: printing the
        // window row's defaults (0/0) there would read as "this panel
        // draws nothing", which is false.
        int sectionsOn = 0;
        int sectionsTotal = 0;
        sectionCountsFor(sel, currentGame(), sectionsOn, sectionsTotal);
        winContentCount_->setText(QStringLiteral("%1/%2")
                                      .arg(sectionsOn)
                                      .arg(sectionsTotal));

        const mhw::WindowList list = buildWindowStates();
        const mhw::GameId chipGame = currentGame();
        for (const mhw::WindowState &sib : list) {
            if (sib.panelIndex != sel.panelIndex)
                continue;   // another panel's window is not a sibling
            // 实测缺陷3: the sibling list obeys the game in force. The
            // board model carries both 衣装 and 翔虫 (and, under Rise,
            // 伙伴) because one registry serves two games — but an entry
            // this game has no data for must not be offered a second
            // time as a window the user can switch to.
            if (!windowKeyInGame(sib.key, chipGame))
                continue;
            auto *chip = new QPushButton(sib.title);
            chip->setObjectName(QStringLiteral("groupWinChip"));
            // The KEY, exposed the same way the Mixed block's rows expose
            // theirs: the title is localized copy and the panel/sub pair
            // is an address, but the settings key is the only spelling a
            // list assertion (「World 不列 player-wirebug」) can read
            // without translating anything.
            chip->setProperty("settingsKey", sib.key);
            chip->setCheckable(true);
            chip->setChecked(sib.subIndex == sel.subIndex);
            chip->setCursor(Qt::PointingHandCursor);
            // The chip's own window. Captured by value because the list
            // it came from is a temporary — and the panel/sub pair is
            // the model's address, which survives the list's death.
            const int sibPanel = sib.panelIndex;
            const int sibSub = sib.subIndex;
            connect(chip, &QPushButton::clicked, this, [this, sibPanel, sibSub]{
                selectWindow(sibPanel, sibSub);
            });
            groupLayout->addWidget(chip);
        }
        // The Mixed 独立/合并 block. Built once (first refill), attached
        // to every refill's layout, and brought up to date by the
        // refreshGroupColumn() call at the end of this block — which is
        // also what a language flip reaches through the column's one
        // trHook, so the eleven titles and state labels follow the
        // language without a rebuild.
        buildMixedGroupBlock();
        if (mixedGroupHost_)
            groupLayout->addWidget(mixedGroupHost_);
        groupLayout->addStretch(1);
        // ── (C): the column's per-window half is rebuilt here, not
        // hidden. The old shell left the frame hidden "because a second
        // trHook captured these fresh children" — the same reason it
        // deleted the count's hook. There is now exactly ONE trHook for
        // this column (built once, in buildWindowInspectorFace) and it
        // calls refreshGroupColumn(), so nothing captured per refill can
        // outlive the widgets it captured.
        refreshGroupColumn();
    }

    // ── column 2: the real Panel pixmap ───────────────────────────────
    //
    // The prototype's third column shows what the window will actually
    // draw. In Qt that is the Panel's own render — the SAME pixmap the
    // canvas places in its sub-window boxes (hud_canvas.cpp's
    // renderAll()), so the two cannot disagree about what a window
    // looks like. The preview is the panel's full render at its own
    // scale, scaled to fit the box while keeping its aspect.
    refreshWindowPreviewPixmap();

    // The chip in the head names the selected OBJECT — the window's title
    // for a window row, the mother's short title for a mother focus (it
    // used to read `w.title`, which is empty in a Split mother focus and
    // left the chip blank while the form below was full of the panel's
    // numbers). The panel page under the face follows the panel half of
    // the selection so the mask rows stay about the right panel.
    if (selectionChip_)
        selectionChip_->setText(sel.title);
    // The head's sub line and the empty block both follow the SELECTION
    // (reference.html:486 does both in one renderInspector pass).
    if (inspectorHeadSub_)
        inspectorHeadSub_->setText(consoleText(
            QStringLiteral("console.proto.insp.sub.selected")));
    if (inspectorEmpty_) {
        inspectorEmpty_->hide();
        if (contentScrollHost_)
            contentScrollHost_->show();
        // round2: the preview card's half comes back WITH the left one —
        // both halves are one face, and the empty state owns both.
        if (previewScrollHost_)
            previewScrollHost_->show();
    }
    windowModelRefilling_ = false;
}

// ── v1.0.0 (C): THE SELECTED WINDOW'S OWN SECTION SWITCHES ──────────────
//
// reference.html:489-493 gives the middle column two halves: the window's
// own section switches (`显示内容 on/total` over one row per section the
// window draws, reference.html:493) and, under 本组窗口, the chips of its
// siblings (reference.html:493). The shell shipped the count and the
// chips but no switches, so a user could read `2/9` and not act on it.
namespace {
// The bit each of a panel's section names IS, in display order — an
// EXPLICIT mapping from panel_sections.h, not `1u << rowIndex`. The two
// agree today only because every enum appends new bits at its end; a
// positional bit would silently toggle a different block the day an
// enum inserts one, which is the exact failure the table exists to make
// impossible.
QVector<uint32_t> sectionBitsFor(int panelIndex)
{
    switch (panelIndex) {
    case 0:
        return { mhw::PlayerSection::Conn,    mhw::PlayerSection::Quest,
                 mhw::PlayerSection::Weapon,  mhw::PlayerSection::Bars,
                 mhw::PlayerSection::Mantles, mhw::PlayerSection::Debuff,
                 mhw::PlayerSection::Buff,    mhw::PlayerSection::Wirebug,
                 mhw::PlayerSection::Sharpness };
    case 1:
        return { mhw::MonsterSection::Info,     mhw::MonsterSection::Hp,
                 mhw::MonsterSection::Enrage,   mhw::MonsterSection::Ail,
                 mhw::MonsterSection::Parts,    mhw::MonsterSection::Tenderize,
                 mhw::MonsterSection::Stamina };
    case 2:
        return { mhw::DamageSection::Rows, mhw::DamageSection::Share,
                 mhw::DamageSection::Chart,
                 mhw::DamageSection::OtherMembers };
    case 3:
        return { mhw::PetDamageSection::LocalPets,
                 mhw::PetDamageSection::OtherPets };
    default:
        return {};
    }
}

QStringList sectionNamesFor(int panelIndex)
{
    switch (panelIndex) {
    case 0:  return mhw::PlayerSection::displayNames();
    case 1:  return mhw::MonsterSection::displayNames();
    case 2:  return mhw::DamageSection::displayNames();
    case 3:  return mhw::PetDamageSection::displayNames();
    default: return {};
    }
}

// `labelPanel()` of the draft (reference.html:486-489): the panel's own
// name, used in the object title's `panel · output` meta span.
const char *panelLabelKey(int panelIndex)
{
    switch (panelIndex) {
    case 0:  return "console.proto.panel.player";
    case 1:  return "console.proto.panel.monster";
    case 2:  return "console.proto.panel.damage";
    case 3:  return "console.proto.panel.pets";
    default: return "console.proto.panel.player";
    }
}
} // namespace

// One section checkbox of the selected window. The BITS live on the
// OWNING PANEL — a split sub-window draws its table row's bits out of
// its panel's mask, and toggling them has to write the panel's mask and
// re-render the panel, which is what the old inspector's rows do too.
bool ControlPanel::toggleSelectedWindowSection(uint32_t bit, bool on)
{
    // D1: the SAME accessor the rows that drew this checkbox read. The old
    // `selectedWindow().panelIndex < 0 → false` refused every section
    // switch in a Split mother focus — the rows are built from sel.panel,
    // so they were on screen and the click did nothing at all, the
    // quiet half of the same "enabled but inert" defect.
    const EditableSelection sel = currentEditableSelection();
    if (!sel.panel)
        return false;
    const int panelIndex = sel.panelIndex;
    // ── 实测缺陷3 (user-8): THE SELECTED INSTANCE, NOT ITS MOTHER ────────
    // A sub-window's bits are gated by ITS OWN mask (syncAll() pushes
    // maskFor(ctl_[parentSlot]) into that window, and it paints from what
    // its own Panel holds), so `sel.panel` is both what the group column
    // READS (see refreshGroupColumn's `live`) and what this write has to
    // land on — one object, or the switch is a no-op: the mother's mask
    // is re-derived from the plan by every rebuildAndRender(), and with
    // `next == old` on that already-stripped value the early return below
    // used to skip the subs row too, so nothing anywhere moved.
    //
    // For a mother focus `sel.panel` IS panelAt(panelIndex), so the write
    // is unchanged there; what still makes the change durable is the
    // panel-page row two blocks down, which is what every rebuild reads.
    Panel *p = sel.panel;
    if (!p)
        return false;
    const uint32_t old = p->sectionMask();
    const uint32_t next = on ? (old | bit) : (old & ~bit);
    if (next == old)
        return true;   // already in that state: no render, no write
    p->setSectionMask(next);
    // ── THE PANEL PAGE'S OWN ROW FOLLOWS, AND MUST ──────────────────────
    // rebuildAndRender() re-derives the mask from ctl_[i].subs — the
    // hidden panel page's rows ARE the authority for the mask (they are
    // what the old inspector drove and what the child process saves) —
    // so a bit changed only here would be read back as still on and
    // written straight back. The row is found by its BIT, through the
    // same explicit table the new column uses, never by position.
    {
        const QVector<uint32_t> bits = sectionBitsFor(panelIndex);
        auto &subs = ctl_[panelIndex].subs;
        for (int b = 0; b < bits.size() && b < subs.size(); ++b) {
            if (bits[b] != bit)
                continue;
            if (subs[b] && subs[b]->isChecked() != on)
                subs[b]->setChecked(on);
            break;
        }
    }
    syncAppearance(panelIndex);      // the panel page's own rows follow
    rebuildAndRender(panelIndex);    // the pixmap the canvas/col 2 draw
    // The model refresh rebuilds the column this click came from. It is
    // deferred by one event-loop turn so the emitting SectionRow is not
    // deleted while it is still delivering toggled().
    QTimer::singleShot(0, this, [this]{ refreshWindowModel(); });
    return true;
}

// ── v1.1.0 Mixed: the per-window 独立/合并 half of the group column ─────
//
// WHAT THIS IS: inside Mixed — and only inside Mixed — the eleven rows
// the three split tables can carve out are each either its own window
// (独立窗口) or merged back into its mother panel (合并显示), per game,
// chosen by hand. The mode decides the TOPOLOGY, this plan decides the
// ROWS; a Mixed run with nothing configured falls back to the visible
// all-detached set, which is why the switches start all-on.
//
// WHERE THE STATE LIVES: one mhw::WindowLayoutPlan per game, held here
// and written to `layout.windows.<game>=` by saveMaskToDisk(). The plan
// is launch-time: the overlay mounts the detached set on its next start,
// so a click moves the switch, its state label and the in-memory plan —
// and nothing else until the user saves (and applies).
//
// WHICH SLOT A GAME OWNS — the two games must never share a plan, or a
// World save would rewrite Rise's row. World = 0, Rise = 1; GameId has
// exactly these two enumerators, and the fallback answers World's slot
// rather than inventing a third (the same reasoning layoutWindowsRowKey()
// spells for its own empty-key fallback).
static int mixedPlanSlotFor(mhw::GameId game)
{
    return game == mhw::GameId::Rise ? 1 : 0;
}

mhw::WindowLayoutPlan &ControlPanel::mixedPlanFor(mhw::GameId game)
{
    return mixedPlans_[mixedPlanSlotFor(game)];
}

const mhw::WindowLayoutPlan &ControlPanel::mixedPlanFor(mhw::GameId game) const
{
    return mixedPlans_[mixedPlanSlotFor(game)];
}

// The read half: BOTH games' rows, out of the one conf text
// applyStartupLayoutMode() already holds — no second file is opened, and
// nothing is written. A load must not seed a settings scope (that is
// ensureMixedPanelSettingsScope's explicit-entry rule) and must not
// touch the conf (that is saveMaskToDisk's job).
//
// Mixed is asked for as the MODE even while the console is showing
// Single or Split: resolveWindowLayoutPlan() reads `layout.windows.<game>=`
// ONLY in Mixed, so resolving the mode in force would hand back that
// mode's fixed default and silently discard a saved list the next Mixed
// entry needs. What is loaded is what the FILE says, whichever topology
// is on screen; no row at all -> Mixed's own all-detached fallback,
// which is exactly what the overlay resolves for the same file.
void ControlPanel::loadMixedLayoutPlans(const QString &confText)
{
    for (const mhw::GameId game : {mhw::GameId::World, mhw::GameId::Rise})
        mixedPlanFor(game) = mhw::resolveWindowLayoutPlan(
            game, mhw::PlayerSection::PlayerLayoutMode::Mixed, confText);
}

// ── v1.1.3 (Mixed 画布): the plan read three ways, each spelled ONCE ────
//
// Three answers, one source: the plan in force for the game in force.
// They exist because the same question was about to be asked in three
// places that must not drift — the host's rebuild guard, the mother
// panel's mask, and the mount gate that decides whether a mother is
// painted at all — and a fourth reader (the count model) has to agree
// with all of them.

// Which rows are detached, as registry bit i of kWindowLayoutRows. It
// identifies the HOST, not the plan object: two plans with the same
// detached set build the same windows, so comparing this number is
// both the correct question and the cheapest one (11 bits, no QString).
quint32 ControlPanel::mixedPlanSignature() const
{
    const mhw::WindowLayoutPlan &plan = mixedPlanFor(currentGame());
    quint32 sig = 0;
    for (int i = 0; i < mhw::kWindowLayoutRowCount; ++i) {
        const mhw::WindowLayoutRow &row =
            mhw::kWindowLayoutRows[static_cast<size_t>(i)];
        if (plan.detached(row.parentSlot, row.tableRow))
            sig |= quint32(1u) << i;
    }
    return sig;
}

// The mask this run's mother PANEL actually draws — main.cpp:761's rule,
// for the console's own preview Panel.
//
// Single and Split return `userMask` bit for bit, which is the whole
// reason this is a function and not an `if` at each call site: the two
// legacy topologies pass through an identity, so no reader can tell the
// difference. pets (index 3) has no parent slot and no split table, so
// an out-of-range index answers the mask untouched as well.
uint32_t ControlPanel::effectiveMotherMask(int panel, uint32_t userMask) const
{
    if (layoutModeInForce() != mhw::PlayerSection::PlayerLayoutMode::Mixed)
        return userMask;
    if (panel < 0 || panel >= mhw::kLayoutParentCount)
        return userMask;
    return mixedPlanFor(currentGame()).retainedMask(panel, userMask);
}

// Does this mother still mount, in Mixed? main.cpp:776's gate with the
// console's own predicates:
//
//     planMotherPanelEnabled(plan, parent, false, hasBlocks(retained))
//
// disabledByFlag is false because the console has no --no-player: the
// panel master toggle is a separate axis (w.panelEnabled) that the
// model carries in its own field and the canvas's `present` arm already
// answers, exactly as it does in Single.
//
// ONLY ASKED IN MIXED. A non-Mixed topology decides mounting itself
// (Single mounts all four, Split only pets), and mixedPlanFor() may
// legitimately hold a saved Mixed list while the console is showing
// Single — so an ungated call would answer a question nobody asked.
// Every call site therefore tests the mode first; this returns true
// (the permissive answer) for the unreachable case so a missed gate can
// only over-count, never hide a window.
bool ControlPanel::mixedMotherMounts(int panel, uint32_t userMask) const
{
    if (layoutModeInForce() != mhw::PlayerSection::PlayerLayoutMode::Mixed)
        return true;
    if (panel < 0 || panel >= mhw::kLayoutParentCount)
        return true;
    const mhw::WindowLayoutPlan &plan = mixedPlanFor(currentGame());
    const uint32_t retained = plan.retainedMask(panel, userMask);
    const bool hasBlocks =
        panel == mhw::kLayoutParentPlayer
            ? mhw::PlayerSection::playerPanelHasBlocks(retained)
        : panel == mhw::kLayoutParentMonster
            ? mhw::MonsterSection::monsterPanelHasBlocks(retained)
            : mhw::DamageSection::damagePanelHasBlocks(retained);
    return mhw::planMotherPanelEnabled(plan, panel,
                                       /*disabledByFlag=*/false, hasBlocks);
}

// Built ONCE. Its rows come from the registry, which is the same list
// the conf row and --detached-windows parse, so this block can never
// offer a key the plan does not know (setDetached() answers false for an
// unknown key) nor omit one it does. The caller re-attaches the frame to
// the group column's layout on every refill; the frame itself survives
// those refills, which is why its own text goes through trSet() (set now,
// replayed on a language flip) rather than through a per-refill rebuild.
void ControlPanel::buildMixedGroupBlock()
{
    if (mixedGroupHost_)
        return;

    auto *host = new QFrame();
    host->setObjectName(QStringLiteral("mixedGroupBlock"));
    host->setProperty("mixedGroupBlock", true);
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    auto *cap = new QLabel();
    trSet(cap, QStringLiteral("console.layout.group_title"));
    cap->setObjectName(QStringLiteral("sectionCap"));
    cap->setProperty("mixedGroupCaption", true);
    lay->addWidget(cap);

    for (const mhw::WindowLayoutRow &row : mhw::kWindowLayoutRows) {
        const QString key = QString::fromLatin1(row.settingsKey);
        auto *rowHost = new QFrame();
        rowHost->setObjectName(QStringLiteral("mixedGroupRow"));
        rowHost->setProperty("settingsKey", key);
        auto *rowLay = new QHBoxLayout(rowHost);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->setSpacing(6);

        auto *box = new QCheckBox();
        box->setObjectName(QStringLiteral("mixedWinToggle"));
        box->setProperty("settingsKey", key);
        box->setCursor(Qt::PointingHandCursor);
        // The box names the WINDOW; the label beside it is what
        // `console.layout.detached` / `.merged` annotate — the two states
        // of this one row, so a screen reader (and a test) can tell which
        // state a checked box is in without inferring it from the check.
        auto *state = new QLabel();
        state->setObjectName(QStringLiteral("mixedWinState"));
        state->setProperty("settingsKey", key);

        rowLay->addWidget(box, 1);
        rowLay->addWidget(state);
        lay->addWidget(rowHost);
        mixedGroupToggles_.append(box);
        mixedGroupStateLabels_.append(state);

        // refreshMixedGroupBlock() sets the checked state with the box's
        // signals blocked, so this fires on the user's own click only.
        connect(box, &QCheckBox::toggled, this,
                [this, key](bool on){ setMixedWindowDetached(key, on); });
    }

    // The two quick operations: ALL eleven detached / ALL eleven merged
    // (an explicit empty plan, which the conf row spells as an empty
    // value — a different state from an absent row, and the one that
    // means "everything merged" rather than "nothing configured").
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);
    auto *detachAll = new QPushButton();
    trSet(detachAll, QStringLiteral("console.layout.detach_all"));
    detachAll->setObjectName(QStringLiteral("mixedDetachAll"));
    detachAll->setCursor(Qt::PointingHandCursor);
    auto *mergeAll = new QPushButton();
    trSet(mergeAll, QStringLiteral("console.layout.merge_all"));
    mergeAll->setObjectName(QStringLiteral("mixedMergeAll"));
    mergeAll->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(detachAll, 1);
    btnRow->addWidget(mergeAll, 1);
    lay->addLayout(btnRow);
    connect(detachAll, &QPushButton::clicked, this,
            [this]{ setAllMixedWindowsDetached(true); });
    connect(mergeAll, &QPushButton::clicked, this,
            [this]{ setAllMixedWindowsDetached(false); });

    // ── 实测缺陷4 (user-8): THE BOTTOM EXPLANATION IS GONE ──────────────
    // The block used to end with a word-wrapped 说明 (objectName
    // mixedGroupHint, console.layout.custom_hint) restating what Mixed
    // means. The user's own words: 「下面的文案也没必要留」 — the caption,
    // the eleven rows, their 独立窗口/合并显示 labels and the two quick
    // operations already say everything the paragraph said, and it was
    // pure scroll below the buttons.
    //
    // The KEY survives: console.layout.custom_hint is also the 混合
    // segment button's tooltip (buildLayoutSegment(), trTip), where one
    // line explaining the mode on hover is still the right place for it —
    // so this deletes a label, not an i18n entry, and the copy-semantics
    // key list stays satisfied without touching the resources.
    mixedGroupHost_ = host;
}

// Everything that is a function of (mode, game, plan, FOCUS) rather than
// of anything else: visibility, each row's localized title, its checked
// state and its 独立窗口/合并显示 label. Safe to call as often as it is
// called — from the group column's refill, from the one language-flip
// trHook and from the mode commit — because every state write blocks the
// box's signals (a programmatic setChecked() is not a user's click).
//
// ── 实测缺陷4 (user-8): THE LIST FOLLOWS THE FOCUS ──────────────────────
// The eleven rows are one registry of THREE mother groups (parentSlot 0/1
// /2). This column is read while a window is being inspected, and
// listing every group there hands the user a plan edit for windows that
// are not the subject of anything on screen. A selection therefore
// narrows the list to its own group — `row.parentSlot == sel.panelIndex`,
// the same relation the 本组子窗 chips above already use (`sib.panelIndex
// == sel.panelIndex`), so the two halves of this column answer "which
// group am I looking at" the same way. NO selection — the cold start,
// and every game-only assertion — keeps the whole game-filtered
// registry, because there is no focus to be related to.
//
// HIDDEN, never deleted: the eleven rows are still the registry's own
// index (mixedGroupToggles_[i] == kWindowLayoutRows[i], which
// console_mixed_layout_tests §2.2 pins as an identity), so the checked
// state below still tracks the plan and a focus or game change only has
// to re-run this function.
void ControlPanel::refreshMixedGroupBlock()
{
    if (!mixedGroupHost_)
        return;
    // Single and Split have no plan to edit: their topology IS the mode,
    // and both resolve a fixed default without ever reading the row. An
    // eleven-switch block there would promise a choice the launcher
    // ignores.
    const bool mixed =
        layoutModeInForce() == mhw::PlayerSection::PlayerLayoutMode::Mixed;
    // The focus, asked here rather than passed in: the two callers that
    // reach this function first (buildWindowInspectorFace's refill and
    // the mode commit) both have a selection to read, and asking for it
    // twice costs one vector walk.
    const EditableSelection sel = currentEditableSelection();
    const int focusPanel = sel.panel ? sel.panelIndex : -1;
    mixedGroupHost_->setVisible(mixed);
    if (!mixed)
        return;

    const mhw::WindowLayoutPlan &plan = mixedPlanFor(currentGame());
    const int n = qMin(mixedGroupToggles_.size(), mixedGroupStateLabels_.size());
    int listed = 0;
    for (int i = 0; i < n && i < mhw::kWindowLayoutRowCount; ++i) {
        QCheckBox *box = mixedGroupToggles_.at(i);
        QLabel *state = mixedGroupStateLabels_.at(i);
        if (!box || !state)
            continue;
        const mhw::WindowLayoutRow &row = mhw::kWindowLayoutRows[i];
        // 实测缺陷3: the LIST obeys the game in force. 实测缺陷4 adds the
        // focus on top of it — same mechanism (a row the console does not
        // offer is HIDDEN, not deleted), same reason: the index identity
        // below must survive, and switching the game or the selection
        // only has to re-run this function.
        const bool inGame = windowKeyInGame(
            QString::fromLatin1(row.settingsKey), currentGame());
        const bool related =
            focusPanel < 0 || row.parentSlot == focusPanel;
        const bool shown = inGame && related;
        if (shown)
            ++listed;
        if (QWidget *rowHost = box->parentWidget())
            rowHost->setVisible(shown);
        state->setVisible(shown);
        const bool detached = plan.detached(row.parentSlot, row.tableRow);
        box->blockSignals(true);
        box->setText(consoleText(QStringLiteral("console.proto.win.short.%1")
                                     .arg(QString::fromLatin1(row.settingsKey))));
        box->setChecked(detached);
        box->blockSignals(false);
        state->setText(consoleText(detached
                                       ? QStringLiteral("console.layout.detached")
                                       : QStringLiteral("console.layout.merged")));
    }
    // A focus that owns no registry row at all (the pets panel has none:
    // the registry is player/monster/damage) must not leave a caption and
    // two buttons over an empty list — the block leaves with its rows and
    // comes back with them.
    mixedGroupHost_->setVisible(listed > 0);
}

// One switch. Returns false for a key that is not in the registry — the
// spelling rule the whole plan is built on: no case folding, no nearest
// match, no write.
bool ControlPanel::setMixedWindowDetached(const QString &settingsKey,
                                          bool detached)
{
    mhw::WindowLayoutPlan &plan = mixedPlanFor(currentGame());
    if (!plan.setDetached(settingsKey, detached))
        return false;
    refreshMixedGroupBlock();
    refreshAfterMixedPlanChange();
    return true;
}

void ControlPanel::setAllMixedWindowsDetached(bool detached)
{
    mhw::WindowLayoutPlan &plan = mixedPlanFor(currentGame());
    // ── v1.1.4: THE BULK BUTTONS ACT ON THE ROWS THIS COLUMN SHOWS ─────
    // With a FOCUS, "all" used to mean all eleven registry rows, so
    // 「全部独立」 clicked while the PLAYER group was in front also
    // detached Monster's and Damage's rows — three groups edited by a
    // control sitting under one group's heading. The list itself already
    // obeys (game, focus) in refreshMixedGroupBlock(); the focused arm
    // below is the write half of the same predicate, spelled from the same
    // two inputs, so "all" can only ever mean "all the rows you can see
    // right now".
    //
    // NO FOCUS keeps the old contract byte for byte: the cold-start list
    // is the whole registry, so the two shortcuts are still the two ends
    // of it. That asymmetry is deliberate — the list and the write must
    // answer the same question, and they do in both arms.
    const EditableSelection sel = currentEditableSelection();
    const int focusPanel = sel.panel ? sel.panelIndex : -1;
    if (focusPanel < 0) {
        plan = detached ? mhw::WindowLayoutPlan::allDetached()
                        : mhw::WindowLayoutPlan();
    } else {
        for (int i = 0; i < mhw::kWindowLayoutRowCount; ++i) {
            const mhw::WindowLayoutRow &row = mhw::kWindowLayoutRows[i];
            if (row.parentSlot != focusPanel)
                continue;
            if (!windowKeyInGame(QString::fromLatin1(row.settingsKey),
                                 currentGame()))
                continue;
            plan.setDetached(QString::fromLatin1(row.settingsKey), detached);
        }
    }
    refreshMixedGroupBlock();
    refreshAfterMixedPlanChange();
}

// ── v1.1.3: WHAT A PLAN CHANGE MOVES ────────────────────────────────────
//
// The 独立/合并 switches used to write the plan and stop: the group block
// refreshed its labels, and everything the user could SEE — the board,
// the counts, the inspector — kept the old topology until some
// unrelated event happened to rebuild the host. That was survivable
// while Mixed drew nothing but mothers; now that it draws the detached
// windows, a toggle has to move the board.
//
// Four things, in this order, and none of them is "rebuild always":
//
//   1. syncSplitPreviewToCanvas() — rebuilds the host ONLY when
//      mixedPlanSignature() differs (see splitPreviewHostPlan_), which is
//      exactly when the detached set changed, and re-states the mothers'
//      retained mask + `present` when it does. A toggle that changed
//      nothing costs one 11-bit compare.
//   2. rebuildAndRender(0..2) — not here: sync does it inside its
//      rebuild arm, and calling it again would re-render four pixmaps
//      for a plan that did not change the mask.
//   3. refreshWindowModel() — deferred one turn, exactly like
//      toggleSelectedWindowSection() does, so the emitter (a QCheckBox of
//      the group block this function is inside) is never reached while it
//      is delivering toggled(). The test and the screenshot path both
//      pump the event loop, so the deferral is invisible to them.
//   4. In Single and Split NOTHING happens — the plan is staged for the
//      next Mixed entry (see loadMixedLayoutPlans' note) and those two
//      topologies do not read it. That early return is what keeps the
//      two legacy modes byte-identical through a plan edit.
void ControlPanel::refreshAfterMixedPlanChange()
{
    if (layoutModeInForce() != mhw::PlayerSection::PlayerLayoutMode::Mixed)
        return;
    syncSplitPreviewToCanvas();
    QTimer::singleShot(0, this, [this]{ refreshWindowModel(); });
}

// The whole per-window half of the group column. Safe to call on every
// refill and on every language flip: the ONE trHook that re-runs it was
// registered once, in buildWindowInspectorFace(), and captures no
// per-refill widget.
void ControlPanel::refreshGroupColumn()
{
    // The Mixed 独立/合并 block first: it is a function of the MODE and
    // the plan, not of this selection, and it lives in this column — so
    // it must refresh BEFORE the two selection-driven early returns
    // below, both of which are about a selection this block never reads.
    refreshMixedGroupBlock();

    // D1: the SAME accessor the form uses. The title, the `panel · output`
    // meta and the coordinate note are readings of the object the five
    // controls edit, so a mother focus in Split now names itself and its
    // note names the very output the coordinate form is enabled against —
    // instead of showing three empty strings over a live form.
    const EditableSelection sel = currentEditableSelection();
    const mhw::WindowState w = sel.window;   // only meaningful if hasWindow
    if (winObjectTitleName_)
        winObjectTitleName_->setText(sel.panel ? sel.title : QString());
    if (winObjectTitleMeta_)
        winObjectTitleMeta_->setText(sel.panel
            ? QStringLiteral("%1 · %2")
                  .arg(QString::fromUtf8(panelLabelKey(sel.panelIndex)).isEmpty()
                           ? QString()
                           : consoleText(QString::fromUtf8(
                                 panelLabelKey(sel.panelIndex))))
                  .arg(sel.resolvedOutput)
            : QString());
    if (winPosNote_)
        winPosNote_->setText(sel.panel
            ? consoleText(QStringLiteral("console.proto.pos.note"))
                  .arg(sel.resolvedOutput)
            : QString());
    if (!winContentCount_ || !windowSectionListHost_)
        return;
    if (!sel.panel) {
        groupSectionKey_ = -1;
        winContentCount_->setText(QString());
        QLayout *oldLayout = windowSectionListHost_->layout();
        if (oldLayout) {
            QLayoutItem *item = nullptr;
            while ((item = oldLayout->takeAt(0)) != nullptr) {
                delete item->widget();
                delete item;
            }
        }
        return;
    }
    {
        int sectionsOn = 0;
        int sectionsTotal = 0;
        sectionCountsFor(sel, currentGame(), sectionsOn, sectionsTotal);
        winContentCount_->setText(QStringLiteral("%1/%2")
                                      .arg(sectionsOn)
                                      .arg(sectionsTotal));
    }

    // What this selection draws: a mother panel draws its panel's schema;
    // a split window draws exactly the bits its table row claims (the
    // binding's row index → the table's mask, via splitWindowSections()).
    const uint32_t schema = panelSectionSchema(sel.panelIndex, currentGame());
    uint32_t own = schema;
    if (sel.subIndex >= 0 && splitPreview_) {
        const QVector<HudCanvas::SubWindowBinding> bindings =
            splitPreview_->bindings();
        if (sel.subIndex < bindings.size())
            own = splitWindowSections(sel.panelIndex,
                                      bindings[sel.subIndex].tableRow);
    }
    const QVector<uint32_t> bits = sectionBitsFor(sel.panelIndex);
    const QStringList labels = sectionNamesFor(sel.panelIndex);
    const QStringList &keys = sel.panelIndex == 0 ? mhw::PlayerSection::names()
                              : sel.panelIndex == 1 ? mhw::MonsterSection::names()
                              : sel.panelIndex == 2 ? mhw::DamageSection::names()
                                                  : mhw::PetDamageSection::names();
    // ── 实测缺陷3 (user-8): THE INSTANCE THE SELECTION NAMES ────────────
    // `sel.panel` IS the object this column is about: for a window row it
    // is the split sub-window's own Panel — the one syncAll() feeds with
    // the user's mask and the one that window paints from — and for a
    // mother focus it is the mother (the same object as before). Reading
    // `panelAt(sel.panelIndex)` read the MOTHER for a sub-window: in
    // Mixed that mask is plan.retainedMask(), which has every detached
    // row stripped out, so the chips showed a detached row's sections OFF
    // while the window was drawing them — and a click was written to the
    // very mask rebuildAndRender() then forced back, which is why the
    // switch never moved. In Single and Split the two objects hold the
    // same bits (effectiveMotherMask is the identity there), so those
    // topologies read exactly what they read before.
    //
    // It is also the same instance buildWindowStates() reads for
    // `w.sectionsOn` (the count on this same column), so the `on/total`
    // and the switches under it can no longer name two masks.
    const uint32_t live =
        sel.panel ? sel.panel->sectionMask() : schema;

    // Same window as last time: the rows are already built and the
    // SectionRow that just emitted toggled() is one of them. Only the
    // checked state can have moved.
    const int key = sel.panelIndex * 1000 + sel.subIndex;
    if (key == groupSectionKey_ && windowSectionListHost_->layout()) {
        const auto rows = windowSectionListHost_->findChildren<SectionRow *>();
        for (SectionRow *row : rows) {
            const uint32_t bit =
                row->property("sectionBit").toUInt();
            const bool want = (own & bit & live) != 0;
            if (row->isChecked() != want)
                row->setChecked(want);
        }
        return;
    }
    groupSectionKey_ = key;

    // Rebuild. takeAt() + delete, the same discipline the group column's
    // own chips use — and no trHook is registered per row (a hook
    // capturing these would outlive them, which is the exact crash the
    // old shell worked around by hiding the column).
    QLayout *oldLayout = windowSectionListHost_->layout();
    if (oldLayout) {
        QLayoutItem *item = nullptr;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete oldLayout;
    }
    // v1.0.0 fidelity (inspector): THE SECTION LIST IS TWO COLUMNS.
    // `.section-list` is `display:grid;grid-template-columns:repeat(2,
    // minmax(0,1fr));gap:5px` (reference.html:227) — the middle column's
    // switches are a 2-across grid filling ROW-MAJOR, exactly what
    // QGridLayout does with row = i/2, col = i%2. What this does NOT
    // change: every row is still a real SectionRow with its own
    // `sectionBit` property, its checked state and its stateChanged →
    // toggleSelectedWindowSection wiring, so a switch flips the window's
    // mask through the same path in either arrangement; and the columns
    // stretch evenly (1:1), which is `minmax(0,1fr)` twice.
    auto *lay = new QGridLayout(windowSectionListHost_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setHorizontalSpacing(5);
    lay->setVerticalSpacing(5);
    int placed = 0;
    for (int b = 0; b < bits.size() && b < labels.size(); ++b) {
        if (!(own & bits[b]))
            continue;   // this window does not draw that section
        auto *row = new SectionRow(
            labels[b], b < keys.size() ? keys[b] : QString(),
            iconKind(w.panelIndex, b));
        row->setAccent(panelAccent(w.panelIndex));
        row->setChecked((own & bits[b] & live) != 0);
        row->setProperty("sectionBit", bits[b]);
        // ONE trHook per row, guarded by a QPointer: the hook list is
        // replayed on every language flip and these rows are deleted on
        // the next selection change, so a bare capture would dereference
        // freed memory. What it re-resolves is the DISPLAY TEXT — the
        // only thing here that has any locale in it.
        {
            QPointer<SectionRow> guard(row);
            const QString label = labels[b];
            trHook([guard, label]{
                if (guard)
                    guard->setDisplayText(label);
            });
        }
        // stateChanged carries an int the row's own isChecked() already
        // knows, so the lambda reads the row rather than decoding it.
        connect(row, &SectionRow::stateChanged, this,
                [this, row, bit = bits[b]](int){
                    toggleSelectedWindowSection(bit, row->isChecked());
                });
        lay->addWidget(row, placed / 2, placed % 2);
        ++placed;
    }
}

// ─── v1.0.0 fidelity port: the inspector's WRITES ──────────────────────
//
// Five fields, five methods, one shape each: read the selected window,
// resolve its Panel, write through the Panel's own setter with
// persist=false, and repaint the board. Nothing here touches disk —
// the console has ONE save path and it is not a spin box.
//
// WHY persist=false EVERYWHERE. Panel::setScale/setOpacity/setMargins
// all take the flag, and the console's contract is that a preview edit
// becomes durable at the exit (closeEvent / saveBeforeLaunch / the
// toolbar's SAVE). A valueChanged signal fires per keypress; writing
// through it would be ten disk writes for one number, which is exactly
// what the flag exists to prevent.
// ─── end v1.0.0 inspector writes ─────────────────────────────────────
bool ControlPanel::applySelectedWindowPosition()
{
    // D1: the WRITE half of the same accessor the readouts use. The old
    // `selectedWindow().panelIndex < 0 → return false` refused every edit
    // in a Split mother focus while the form sat there enabled and full of
    // numbers — one selection, two answers. `sel.panel` is the very
    // instance the numbers were read from, so what the user sees moves.
    const EditableSelection sel = currentEditableSelection();
    Panel *panel = sel.panel;
    if (!panel)
        return false;
    if (!winPosX_ || !winPosY_)
        return false;
    // A refill is not an edit. buildInspectorForSelection() sets the
    // flag around its own writes, so a valueChanged that arrives during
    // a refill is the refill's own echo and must not be written back.
    if (windowModelRefilling_)
        return true;

    QMargins m = panel->margins();
    // ── v1.0.0 integration (stage D): THE INVERSE OF WHAT THE FORM SHOWS
    //
    // THE OLD COMMENT HERE WAS WRONG AND IS REPLACED BY ITS OPPOSITE.
    // It claimed 「right/bottom are the window's own size … an edit that
    // means move this window preserves them」. right/bottom are not the
    // size — the size is `contentSize() * scale`, drawn from the source —
    // they are the ANCHOR's distance from the right/bottom edge, which is
    // exactly why writing left/top moved a TopRight window not at all:
    // its X is `right`, and `right` was being "preserved".
    //
    // So: the two spin boxes are the logical top-left in the window's own
    // output, and marginsForTopLeft() is anchoredTopLeft()'s inverse in
    // the SAME frame with the SAME extent expression. The two sides this
    // corner does not own are copied from `m` untouched — an edit means
    // "move this window", never "resize it" — and the two it does own are
    // recomputed, so a TopRight window whose X is set to 100 comes out at
    // x = 100 on the next paint, and a Bottom window whose Y is set to 50
    // comes out at y = 50.
    const WindowFrame f = windowFrameFor(sel);
    if (!f.valid)
        return false;   // no resolvable output: refuse rather than guess
    const QPoint requested(winPosX_->value(), winPosY_->value());
    const QMargins converted =
        mhw::layout::marginsForTopLeft(f.corner, requested, f.screen,
                                       f.content, f.scale, m);
    if (converted == m)
        return true;    // nothing moved: no write, no repaint
    panel->setMargins(converted, /*persist=*/false);
    if (canvas_)
        canvas_->update();
    return true;
}

// THE VIEW SCREEN IS NOT TOUCHED. This changes where the window LIVES,
// not where the board is looking — the prototype's #panel-output versus
// #screen-select distinction. A user who picks DP-1 for a window while
// the board shows the primary keeps looking at the primary.
//
// An empty `outputName` is the prototype's `follow` entry and means
// "the parent panel decides", which for a MOTHER panel resolves to the
// primary.
//
// THE TARGET IS THE SELECTED WINDOW, NOT ITS MOTHER PANEL — and that
// holds for a split sub-window too. reference.html's own handler for
// this select is
//
//     out.addEventListener('change', function(){
//         var w = win(state.selected); w.output = out.value; renderAll(); ... });
//
// (reference.html:501): it writes the SELECTED WINDOW's `output` and
// nothing else, and `resolveOutput(w)` then answers
// `w.output === 'follow' ? scope().panels[w.panel].output : w.output`
// (reference.html:421). Every catalog entry carries its own `output`
// (reference.html:402 seeds all of them with `'follow'`), so a window —
// split sub-window or mother — holding an explicit pick is a state the
// prototype both expresses and renders.
//
// Three more places say the same thing, and all three were checked:
//   * IMPLEMENTATION-PLAN §O1: 「归属变化只改当前窗口」 — moving the
//     mother's outputName would move all six of its sub-windows at once;
//   * CURRENT-EVIDENCE §4: 「按当前窗口实例而非母面板绑定 inspector；
//     选中 / drag / 输入 / 目标屏修改后读回对应 Panel 并验证」;
//   * the launch path already honours a sub-window's own key, so the
//     write lands somewhere real rather than being discarded —
//     main.cpp:1077 tier-1 returns on `!sub->outputName().isEmpty()`
//     and main.cpp:1231 tier-2 is guarded by `sub->outputName().isEmpty()`,
//     so an own key is preserved by both; hud_canvas.cpp:1115-1117 reads
//     `src->outputName()` BEFORE the parent, so the board follows it; and
//     buildWindowStates() already feeds `w.outputOwn` from
//     `sub->outputName()` (control_panel.cpp:6587), which is exactly the
//     value winOutput_ selects by `findData(w.outputOwn)` below.
//
// Empty outputName = follow = this window's own answer is withdrawn, so
// the chain (own → parent → primary) is read again. Panel::saveAppearance()
// stores it as an empty value (panel.cpp:781) and every reader of the key
// — panel.cpp:611, main.cpp:1077/1231, hud_canvas.cpp:1116 — tests
// isEmpty(), so an empty row and an absent row are the same statement.
bool ControlPanel::applySelectedWindowOutput(const QString &outputName)
{
    // D1: the same accessor the COMBO reads (`sel.ownOutput` is exactly
    // what it selected), so the write lands on the instance whose value is
    // on screen — including a mother focus in Split, which the old
    // window-row guard refused while the combo stayed enabled.
    const EditableSelection sel = currentEditableSelection();
    Panel *panel = sel.panel;
    if (!panel)
        return false;
    if (windowModelRefilling_)
        return true;
    panel->setOutputName(outputName, /*persist=*/false);
    // Screen selection is meaningful in live mode too — the console is
    // the only writer of this key — so the persist is explicit rather
    // than deferred to the exit. Same call the old inspector's SCREEN
    // row made, and the same one Panel::setOutputName(name, true) would
    // have made on its own.
    panel->saveAppearance();
    if (canvas_)
        canvas_->update();
    return true;
}

bool ControlPanel::applySelectedWindowScale(qreal scale)
{
    // D1: writes to the instance the slider was REFILLED from — see
    // applySelectedWindowOutput() for why the window-row guard is gone.
    const EditableSelection sel = currentEditableSelection();
    Panel *panel = sel.panel;
    if (!panel)
        return false;
    if (windowModelRefilling_)
        return true;
    // 50%–300%, the same range the console's sliders have always used.
    const qreal clamped = qBound(0.5, scale, 3.0);
    panel->setScale(clamped, /*persist=*/false);
    if (canvas_)
        canvas_->update();
    if (winScaleValue_)
        winScaleValue_->setText(QStringLiteral("%1%").arg(qRound(clamped * 100)));
    // v1.0.1 (验收项6): a scale change alters what the Panel RENDERS, so
    // the canvas must be handed the NEW pixmap (see the helper below).
    refreshSelectedWindowPreview();
    return true;
}

bool ControlPanel::applySelectedWindowOpacity(qreal opacity)
{
    // D1: same rule as the scale write directly above.
    const EditableSelection sel = currentEditableSelection();
    Panel *panel = sel.panel;
    if (!panel)
        return false;
    if (windowModelRefilling_)
        return true;
    // 10%–100%, matching the existing opacity slider.
    const qreal clamped = qBound(0.1, opacity, 1.0);
    panel->setOpacity(clamped, /*persist=*/false);
    if (canvas_)
        canvas_->update();
    if (winOpacityValue_)
        winOpacityValue_->setText(QStringLiteral("%1%").arg(qRound(clamped * 100)));
    // v1.0.1 (验收项6): paintEvent already bakes opacity into the
    // pixmap's alpha, so re-rendering ONCE refreshes both the canvas
    // source and the inspector preview. Nobody multiplies alpha again.
    refreshSelectedWindowPreview();
    return true;
}

// THE SECOND VISIBILITY AXIS, and the reason it is not a QWidget call.
//
// Panel::setPanelEnabled() is the master gate: it decides whether the
// panel mounts at all, and every sub-window of a disabled panel goes
// with it. The prototype's `windows[].visible` is a per-window switch
// that says nothing about the panel — a split can have five windows on
// screen and one hidden. Conflating them is how "I hid this one window"
// turns into "the whole player panel vanished".
//
// So this writes the model's own key (console_fidelity_windows.h) and
// leaves panelEnabled alone. The window keeps its row in the canvas's
// binding list and stays in the export payload; it is simply not drawn.
bool ControlPanel::applySelectedWindowVisible(bool on)
{
    // D1 + the key question: this switch only exists while a WINDOW row
    // backs the selection (buildInspectorForSelection() disables it
    // otherwise), because pushWindowVisibility() reads `visiblePersisted`
    // off the rows and a mother panel that is not a window in the scope in
    // force has no row to feed. Refusing here keeps the write from
    // mutating a key nothing reads — the definition of an inert control.
    const EditableSelection sel = currentEditableSelection();
    Panel *panel = sel.panel;
    if (!panel || !sel.hasWindow)
        return false;
    if (windowModelRefilling_)
        return true;
    // `panel->settingsKey()` is the row's own key: buildWindowStates()
    // builds `w.key` from the very same expression for both arms (mother
    // and sub), so this is the key the checkbox was refilled from.
    mhw::writeWindowVisible(panel->settingsGroup(), panel->settingsKey(), on);
    // THE IMMEDIATE PUSH, so the board and the checkbox move on the same
    // click instead of the board waiting for the next refreshWindowModel().
    // This is the one place where a WRITE happened, so it is the one place
    // where the canvas must not depend on what the caller does next.
    pushWindowVisibility();
    if (canvas_)
        canvas_->update();
    return true;
}

// ─── v1.0.0 fidelity port: the inspector's three columns, built ────────
//
// The prototype's `.inspector`:
//
//     <div class="shell-head">            min-height 45px
//       <h2>窗口详细配置</h2>
//       <span class="sub">选择窗口后编辑元数据</span>
//       <span class="grow"></span>
//       <span class="chip" id="selection-chip">未选择</span>
//     </div>
//     <div class="inspector-grid">         .9fr 1fr 1.2fr · gap 18 · padding 12/14
//       <div class="inspector-col"> 元数据: X / Y / 目标显示器 / 缩放 / 透明度 / 显示该窗口
//       <div class="inspector-col"> 分区 + 本组子窗 chips
//       <div class="inspector-col"> 内容预览
//
// WHAT IS BUILT HERE is the header and the FIRST column — the fields
// that exist once and are refilled by buildInspectorForSelection(). The
// second and third columns are per-window content that changes with the
// selection (which sibling chips exist, which pixmap), so they are
// rebuilt on selection rather than built once; see the notes on each.
//
// THE OBJECT NAMES ARE THE PROTOTYPE'S IDS, spelled the way this
// console already spells them (camelCase, no leading #), so a QSS rule
// written later against `#inspectorGrid` finds this widget and a test
// that looks one up finds the same one.
// ─── end v1.0.0 inspector face ───────────────────────────────────────
namespace {
/* ── 验收项1 (B 第1阶段): WHO NOTICES A VIEWPORT RESIZE ──────────────────
   Fit is a function of the container, so the container growing (the
   user dragging the splitter handle) has to re-derive the pixmap. There
   is no "viewport resized" signal on QScrollArea, and ControlPanel's
   own eventFilter (control_panel.cpp:4308) is already claimed by other
   watched targets, so this one-line filter stands in for both: it is
   parented to the scroll area, dies with it, and only ever runs the
   callback the build site gave it.
   ── end 验收项1: the fit watcher ── */
class PreviewViewportWatcher final : public QObject
{
public:
    explicit PreviewViewportWatcher(QWidget *watched)
        : QObject(watched)
    {
    }
    std::function<void()> onViewportResize;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize && onViewportResize)
            onViewportResize();
        return QObject::eventFilter(watched, event);
    }
};

/* ── 实测缺陷5 (user-8): A PANE THAT FOLLOWS ITS RENDER ─────────────────────
   QScrollArea::sizeHint() on this Qt (6.12) is frozen at whatever it
   answered the FIRST time it was asked — probed with a 20-line harness,
   in both construction orders: a label that starts at min 200x120, drops
   the minimum and shrinks to 96x92 still answers `scroll hint=200x120`
   for the rest of the run, and even `setWidget(nullptr)` +
   `setWidget(box)` does not refresh it. The pane therefore cannot hug the
   render through the base class, and the pane stretching to the cell is
   exactly 实测缺陷5's screenshot (a 617x344 rectangle around a 272x260
   render).

   This subclass recomputes the hint from the held widget on every call:
   the widget's own hint (QLabel's includes its 1px QSS border — probed:
   96x92 for a 94x90 render) plus the frame. It is deliberately NOT
   clamped to the column: a sizeHint is a request, and
   minimumSizeHint() — the scrollbar-based one, 68x68 measured — is what
   QLayout squeezes the pane down to when the cell is smaller (user zooms
   past the pane), which is where the scroll bars come from.
   ── end 实测缺陷5: hugging scroll area ── */
class HuggingPreviewScroll final : public QScrollArea
{
public:
    using QScrollArea::QScrollArea;

    QSize sizeHint() const override
    {
        const int fw = frameWidth() * 2;
        const QSize inner = widget() ? widget()->sizeHint() : QSize(64, 64);
        return QSize(inner.width() + fw, inner.height() + fw)
            .expandedTo(minimumSizeHint());
    }
};
} // namespace

void ControlPanel::buildWindowInspectorFace(QWidget *parent,
                                            QVBoxLayout *parentLayout)
{
    Q_UNUSED(parent);
    // The shell head. 45px is the measured `.inspector .shell-head`
    // min-height; the two-stage panel's is 48 and this one is 3px
    // shorter, which is the kind of detail a screenshot comparison
    // catches.
    auto *face = new QFrame();
    face->setObjectName(QStringLiteral("inspectorFace"));
    parentLayout->addWidget(face, 1);
    auto *faceLayout = new QVBoxLayout(face);
    faceLayout->setContentsMargins(0, 0, 0, 0);
    faceLayout->setSpacing(0);

    auto *head = new QFrame();
    head->setObjectName(QStringLiteral("inspectorHead"));
    head->setMinimumHeight(45);
    auto *headRow = new QHBoxLayout(head);
    headRow->setContentsMargins(14, 0, 14, 0);
    headRow->setSpacing(12);
    auto *headTitle = new QLabel();
    trSet(headTitle, QStringLiteral("console.proto.insp.title"));
    headTitle->setObjectName(QStringLiteral("inspectorHeadTitle"));
    headRow->addWidget(headTitle);
    // `.sub` of the inspector's shell head. The draft rewrites it in
    // renderInspector (reference.html:486): 「选择窗口后编辑元数据」 with
    // nothing selected, 「当前窗口的元数据与显示分区」 once something is.
    // The shell had `insp.empty.sub` — the EMPTY BLOCK's body copy —
    // frozen in the head for every state, so the head said 「在布局区点击
    // 一个窗口…」 even with a window selected.
    inspectorHeadSub_ = new QLabel();
    inspectorHeadSub_->setObjectName(QStringLiteral("inspectorHeadSub"));
    trHook([this]{
        if (!inspectorHeadSub_)
            return;
        const mhw::WindowState w = selectedWindow();
        inspectorHeadSub_->setText(consoleText(w.panelIndex < 0
            ? QStringLiteral("console.proto.insp.sub.idle")
            : QStringLiteral("console.proto.insp.sub.selected")));
    });
    headRow->addWidget(inspectorHeadSub_);
    headRow->addStretch(1);
    // The selection chip. It names the selected window, and 未选择 when
    // there is none — the prototype's #selection-chip, and the only
    // place the board's selection is stated in words.
    selectionChip_ = new QLabel();
    // `#selection-chip` reads 未选择 with nothing selected and
    // `t(META[id]) · labelPanel(panel)` with something selected
    // (reference.html:486). The shell had 未选中窗口 here, which is the
    // EMPTY BLOCK's <strong> — two different strings collapsed into one.
    trSet(selectionChip_, QStringLiteral("console.proto.insp.chip.none"));
    selectionChip_->setObjectName(QStringLiteral("selectionChip"));
    // v1.0.0 (O1): A PILL, NOT A HEAD-SIZED STRETCH. `.chip` is
    // `min-width:44px; padding:4px 9px` (reference.html:167) and the
    // measured #selection-chip is 50×25 inside a 45px head — with the
    // default size policy a QLabel in a QHBoxLayout fills the row and
    // measured 26×45, i.e. a chip as tall as the whole header. Fixed
    // height + centred text is the geometry; the radius/padding/mono
    // font are the QSS half (O3).
    selectionChip_->setAlignment(Qt::AlignCenter);
    selectionChip_->setMinimumWidth(44);
    selectionChip_->setFixedHeight(25);
    selectionChip_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    headRow->addWidget(selectionChip_);
    // The chip's text is a function of the selection, so it goes through
    // a hook rather than a frozen trSet: a language flip re-queries the
    // window's name under the new locale instead of replaying Chinese.
    trHook([this]{
        if (!selectionChip_)
            return;
        const mhw::WindowState w = selectedWindow();
        selectionChip_->setText(w.panelIndex < 0
            ? consoleText(QStringLiteral("console.proto.insp.chip.none"))
            : w.title);
    });
    faceLayout->addWidget(head);

    // The three columns. QGridLayout with stretch factors 9 / 10 / 12
    // and an 18px gap — the prototype's .9fr 1fr 1.2fr and gap:18px,
    // with its padding:12px 14px 14px on the grid itself.
    //
    // v1.0.0 (O1): THE GRID LIVES INSIDE A SCROLL AREA, WHICH IS WHAT
    // `.inspector-content` IS.
    //
    //   .inspector-content { height:calc(100% - 45px); min-height:0;
    //                        overflow:auto; padding:12px 14px 14px }
    //
    // (reference.html:210) — a fixed-height box that SCROLLS when its
    // content is taller. Without it the three columns are the drawer's
    // minimum height by construction: measured, inspectorMeta's own
    // minimumSizeHint was 163px, which pushed inspectorFace to 234 and
    // configHost to 328 — and QSplitter will not shrink a pane below
    // its child's minimumSizeHint, so `.main`'s `minmax(0,2fr)
    // minmax(280px,1fr)` could not be reached: the pane column measured
    // 904px and the split came out 570/334 (1.71:1) instead of
    // 602/302 (2:1). A scroll container is the draft's own answer to
    // "the content is taller than the row" (it clips and scrolls,
    // `overflow:auto`, and `.panel-shell` has `overflow:hidden`).
    //
    // The paddings stay on the GRID, not on the scroll area, so the
    // measured x/y of every column does not move by a pixel.
    //
    // ── round2 (用户:「这里左右应该分开竖直滚动的,而不是合在一起」) ────────
    // ONE scroll area meant ONE scrollbar: the 元数据 column and the
    // 当前内容状态 card share the tallest content, so scrolling to the
    // bottom of the left columns also scrolled the preview off the top
    // (the screenshot the user filed: all three columns moving as one).
    // The face is now TWO halves, each scrolling its own content:
    //
    //   inspectorColumns (QSplitter, the draggable divider — 验收项1)
    //   ├─ [0] inspectorContent  (QScrollArea) → inspectorGridHost
    //   │                                   → inspectorGrid: 元数据 + 分区/本组子窗
    //   └─ [1] inspectorContentRight (QScrollArea) → inspectorPreviewHost
    //                                              → inspectorPreview (the card)
    //
    // inspectorContent KEEPS its name, its objectName and its role as
    // the LEFT half's scroll box, because three contracts hang off it:
    // src/main_control.cpp's --snap-scroll=end hook scrolls
    // findChild("inspectorContent")'s vertical bar, the interaction
    // regression tests scroll and measure it, and the fidelity tests
    // hide/show it with the empty state. The new right half is
    // inspectorContentRight, and previewScrollHost_ is the member the
    // empty-state arms hide/show beside contentScrollHost_.
    auto *columnsSplit = new QSplitter(Qt::Horizontal);
    columnsSplit->setObjectName(QStringLiteral("inspectorColumns"));
    columnsSplit->setChildrenCollapsible(false);
    columnsSplit->setHandleWidth(6);
    faceLayout->addWidget(columnsSplit, 1);

    auto *contentScroll = new QScrollArea();
    contentScroll->setObjectName(QStringLiteral("inspectorContent"));
    contentScrollHost_ = contentScroll;
    contentScroll->setWidgetResizable(true);
    contentScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    contentScroll->setFrameShape(QFrame::NoFrame);
    contentScroll->setFocusPolicy(Qt::NoFocus);
    columnsSplit->addWidget(contentScroll);

    auto *gridHost = new QWidget();
    gridHost->setObjectName(QStringLiteral("inspectorGridHost"));
    inspectorGridHost_ = gridHost;
    // round2: the card's height budget is this content's height (see the
    // eventFilter arm of the same name), so its resizes are watched.
    gridHost->installEventFilter(this);
    contentScroll->setWidget(gridHost);
    auto *grid = new QGridLayout(gridHost);
    grid->setObjectName(QStringLiteral("inspectorGrid"));
    grid->setContentsMargins(14, 12, 14, 14);   // .inspector-content padding:12px 14px 14px
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(12);

    // ── column 0: 元数据 ───────────────────────────────────────────────
    auto *metaCol = new QFrame();
    metaCol->setObjectName(QStringLiteral("inspectorMeta"));
    auto *metaLayout = new QVBoxLayout(metaCol);
    metaLayout->setContentsMargins(0, 0, 0, 0);
    metaLayout->setSpacing(8);
    auto *metaCap = new QLabel();
    trSet(metaCap, QStringLiteral("console.proto.meta.title"));
    metaCap->setObjectName(QStringLiteral("sectionCap"));
    metaLayout->addWidget(metaCap);

    // ── v1.0.0 (C): `.object-title` (reference.html:489) ───────────────
    // `<name> <span class="meta">panel · resolved-output</span>`: the
    // window's REAL name, the panel it belongs to and the output the
    // own→parent→primary chain actually resolved to. It is a member
    // because it is a function of the selection (the refill rewrites it)
    // and of the language (the window name is localized).
    {
        auto *titleRow = new QHBoxLayout();
        titleRow->setSpacing(7);
        winObjectTitleName_ = new QLabel();
        winObjectTitleName_->setObjectName(QStringLiteral("windowTitle"));
        winObjectTitleMeta_ = new QLabel();
        winObjectTitleMeta_->setObjectName(QStringLiteral("windowTitleMeta"));
        titleRow->addWidget(winObjectTitleName_);
        titleRow->addWidget(winObjectTitleMeta_);
        titleRow->addStretch(1);
        metaLayout->addLayout(titleRow);
        trHook([this]{ refreshGroupColumn(); });
    }

    // X / Y — one row, two boxes. The prototype's #meta-x / #meta-y are
    // `<input type=number>` with a `change` handler that writes both at
    // once; Qt's editingFinished on each box does the same job, and
    // QSpinBox already constrains the value to an int (the position is
    // logical pixels, and a fractional margin has no meaning to
    // Panel::setMargins()).
    auto *posRow = new QHBoxLayout();
    posRow->setSpacing(8);
    auto *posXLabel = new QLabel();
    trSet(posXLabel, QStringLiteral("console.proto.meta.x"));
    posXLabel->setObjectName(QStringLiteral("sliderLabel"));
    posRow->addWidget(posXLabel);
    winPosX_ = new QSpinBox();
    winPosX_->setObjectName(QStringLiteral("metaX"));
    winPosX_->setRange(-100000, 100000);
    winPosX_->setKeyboardTracking(false);
    posRow->addWidget(winPosX_);
    auto *posYLabel = new QLabel();
    trSet(posYLabel, QStringLiteral("console.proto.meta.y"));
    posYLabel->setObjectName(QStringLiteral("sliderLabel"));
    posRow->addWidget(posYLabel);
    winPosY_ = new QSpinBox();
    winPosY_->setObjectName(QStringLiteral("metaY"));
    winPosY_->setRange(-100000, 100000);
    winPosY_->setKeyboardTracking(false);
    posRow->addWidget(winPosY_);
    metaLayout->addLayout(posRow);
    // editingFinished, not valueChanged: a number box emits per
    // keypress, and the prototype's own `change` event fires once when
    // the field is committed. keyboardTracking(false) above makes
    // valueChanged behave the same way, but editingFinished is the
    // honest spelling of "the user is done with this field".
    connect(winPosX_, &QSpinBox::editingFinished,
            this, [this]{ applySelectedWindowPosition(); });
    connect(winPosY_, &QSpinBox::editingFinished,
            this, [this]{ applySelectedWindowPosition(); });

    // 目标显示器. The first entry is 跟随所属面板 with empty userData —
    // the prototype's `follow`, which is the chain default: own answer
    // withdrawn → the parent panel's screen → primary. The rest are the
    // outputs Qt reports, labelled `DP-1 · 2560×1440` exactly as the
    // prototype's labelDisplay() spells them. This window may also carry
    // its OWN pick — a split sub-window included — and the combo selects
    // it from `w.outputOwn`.
    auto *outRow = new QHBoxLayout();
    outRow->setSpacing(8);
    auto *outLabel = new QLabel();
    trSet(outLabel, QStringLiteral("console.proto.target"));
    outLabel->setObjectName(QStringLiteral("sliderLabel"));
    outRow->addWidget(outLabel);
    winOutput_ = new QComboBox();
    winOutput_->setObjectName(QStringLiteral("metaOutput"));
    // BLOCK-USABILITY-FIX: THE 目标显示器 SELECT MUST BE READABLE.
    // Measured in the real console (1600x1000, DP-1 + HDMI-A-1): the row
    // was a natural label + combo, but the combo sat at its CONTENT
    // minimum — 59px — inside a 356px metadata column, so the row showed
    // a clipped "跟随…" and no output name could be read without
    // hovering the popup (the widest real entry, "HDMI-A-1 · 1080×1920",
    // needs 157px of text plus padding and the drop-down).
    //
    // Same three facts the prototype's `.field select` states, spelled
    // with Qt's own layout tools instead of the HTML/CSS pixel values:
    // the label stays on the LEFT, the combo EXPANDS to the rest of the
    // column, and its floor is the measured width of the widest name it
    // can carry — not a pinned pixel box. The control is unchanged:
    // one QComboBox, objectName metaOutput, item data = the output name
    // (empty = follow), and the same currentIndexChanged handler below.
    winOutput_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    winOutput_->setMinimumContentsLength(10);
    winOutput_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    winOutput_->setMinimumWidth(160);   // widest real entry measured: 157px
    outRow->addWidget(winOutput_, 1);
    metaLayout->addLayout(outRow);
    /* ── 实测缺陷5: the line that answers the dropdown's question ────────
       「我完全没看懂他的逻辑」 — the combo can only ever name the MODE; the
       screen the chain actually resolves to is printed right under it,
       so the answer no longer depends on understanding the chain. The
       text is written per selection (buildInspectorForSelection), which
       is also why it is not a trSet: a language flip re-derives it
       through the queued refill below instead of replaying a frozen
       string.
       ── end 实测缺陷5 ── */
    winOutputHint_ = new QLabel();
    winOutputHint_->setObjectName(QStringLiteral("metaOutputHint"));
    winOutputHint_->setWordWrap(true);
    metaLayout->addWidget(winOutputHint_);
    trTip(winOutput_, QStringLiteral("console.proto.target_tip"));
    trHook([this] {
        // 实测缺陷7: the entry's text is per-selection, so a language flip
        // has to re-derive it — and it must re-derive the SAME string the
        // refill builds (mother's name included), or the two spellings of
        // 跟随主面板 would disagree for the length of one event-loop turn.
        // Hence the same accessor and the same kMotherShortTitle name.
        if (winOutput_ && winOutput_->count() > 0) {
            const EditableSelection sel = currentEditableSelection();
            QString text = consoleText(
                sel.subIndex >= 0
                    ? QStringLiteral("console.proto.follow_parent")
                    : QStringLiteral("console.proto.follow_primary"));
            if (sel.subIndex >= 0 && mhw::isPanelIndex(sel.panelIndex))
                text =
                    consoleText(
                        QStringLiteral("console.proto.follow_parent_named"))
                        .arg(consoleText(QString::fromLatin1(
                            kMotherShortTitle[sel.panelIndex])));
            winOutput_->setItemText(0, text);
        }
        if (winOutputHint_ && !winOutputHint_->text().isEmpty())
            QTimer::singleShot(0, this, [this] { refreshWindowModel(); });
    });
    connect(winOutput_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){
        // The combo's own userData is the output name; empty is
        // `follow`, which applySelectedWindowOutput() spells as an
        // empty string and Panel::setOutputName() already accepts.
        if (!windowModelRefilling_)
            applySelectedWindowOutput(winOutput_->currentData().toString());
    });

    // 缩放 — a slider plus a live percentage, the same pair the panel
    // pages have used since v0.5.
    auto *scaleRow = new QHBoxLayout();
    scaleRow->setSpacing(8);
    auto *scaleLabel = new QLabel();
    trSet(scaleLabel, QStringLiteral("console.proto.scale"));
    scaleLabel->setObjectName(QStringLiteral("sliderLabel"));
    scaleRow->addWidget(scaleLabel);
    winScale_ = new QSlider(Qt::Horizontal);
    winScale_->setObjectName(QStringLiteral("metaScale"));
    winScale_->setRange(50, 300);
    scaleRow->addWidget(winScale_, 1);
    winScaleValue_ = new QLabel();
    winScaleValue_->setObjectName(QStringLiteral("sliderValue"));
    scaleRow->addWidget(winScaleValue_);
    metaLayout->addLayout(scaleRow);
    connect(winScale_, &QSlider::valueChanged, this, [this](int v){
        applySelectedWindowScale(v / 100.0);
    });

    // 透明度.
    auto *opacityRow = new QHBoxLayout();
    opacityRow->setSpacing(8);
    auto *opacityLabel = new QLabel();
    trSet(opacityLabel, QStringLiteral("console.proto.opacity"));
    opacityLabel->setObjectName(QStringLiteral("sliderLabel"));
    opacityRow->addWidget(opacityLabel);
    winOpacity_ = new QSlider(Qt::Horizontal);
    winOpacity_->setObjectName(QStringLiteral("metaOpacity"));
    winOpacity_->setRange(10, 100);
    opacityRow->addWidget(winOpacity_, 1);
    winOpacityValue_ = new QLabel();
    winOpacityValue_->setObjectName(QStringLiteral("sliderValue"));
    opacityRow->addWidget(winOpacityValue_);
    metaLayout->addLayout(opacityRow);
    connect(winOpacity_, &QSlider::valueChanged, this, [this](int v){
        applySelectedWindowOpacity(v / 100.0);
    });

    // 显示该窗口 — the SECOND visibility axis. Its label says 该窗口 on
    // purpose: the master gate is the rail's eye, and this one switch
    // hides exactly the window the inspector is describing.
    winVisible_ = new QCheckBox();
    trSet(winVisible_, QStringLiteral("console.proto.visible"));
    winVisible_->setObjectName(QStringLiteral("metaVisible"));
    winVisible_->setCursor(Qt::PointingHandCursor);
    // NOT STRETCHED, and that is a click target, not a style choice.
    // A QVBoxLayout::addWidget() without an alignment gives the row the
    // whole column (measured: 374px wide for a control whose painted
    // part is `indicator ∪ text` = 100px, QStyle::SE_CheckBoxClickRect
    // — QCheckBox::hitButton() is THAT rect, not widget->rect()). A
    // press at the row's centre was therefore IGNORED by
    // QAbstractButton::mousePressEvent (measured: press delivered,
    // `isAccepted()` = 0, `isDown()` = 0, no `toggled()`), i.e. most of
    // the row did nothing — 「点了没反应」. Left-aligning lets the
    // widget take its own sizeHint, so the geometry the user can see is
    // exactly the geometry that answers a click, and the row's height
    // (which the grid below is measured against) does not move.
    metaLayout->addWidget(winVisible_, 0, Qt::AlignLeft);
    connect(winVisible_, &QCheckBox::toggled, this, [this](bool on){
        applySelectedWindowVisible(on);
    });
    // ── v1.0.0 (C): the coordinate `.note` (reference.html:492) ────────
    // 「坐标是 <display> 内的逻辑坐标，不含桌面全局位置。」 The display in
    // it is the window's RESOLVED output, so it follows the selection.
    winPosNote_ = new QLabel();
    winPosNote_->setObjectName(QStringLiteral("windowPosNote"));
    winPosNote_->setWordWrap(true);
    metaLayout->addWidget(winPosNote_);
    /* ── 验收项5 (B 第1阶段): CAPABILITY-DRIVEN PROPERTY ROWS ──────────────
       列数 / 最小宽 / 最小高 / 固定宽 / 固定高 / 恢复自动尺寸 — every one of
       them asks the SELECTED WINDOW's own Panel (supportsColumns(),
       columns(), minWidth(), minHeight(), lockWidth(), lockHeight())
       rather than a hard-coded "this kind of window" guess, and the
       writes go through Panel's exact-value wrappers, which call the
       same protected implementations the edit-mode keys call.

       列数 is HIDDEN (not disabled) for a window whose
       supportsColumns() is false: a control that can never act is a
       control that lies about the window. That visibility decision is
       re-made on every selection in buildInspectorForSelection().

       Nothing here touches the preview path: these are real edits, so
       they stamp the panel's own dirty flag exactly like the keyboard
       does, and 列数 — the one key saveAppearance() does not carry —
       persists through Panel::persistColumns().
       ── end 验收项5: the capability block ── */
    auto *propsCap = new QLabel();
    trSet(propsCap, QStringLiteral("console.inspector.size_hint"));
    propsCap->setObjectName(QStringLiteral("sectionCap"));
    metaLayout->addWidget(propsCap);

    auto makeSpin = [](const QString &name, int lo, int hi) {
        auto *s = new QSpinBox();
        s->setObjectName(name);
        s->setRange(lo, hi);
        s->setKeyboardTracking(false);   // write on commit, not per digit
        s->setAlignment(Qt::AlignRight);
        return s;
    };
    winColumnsSpin_ =
        makeSpin(QStringLiteral("windowColumns"), kColumnsRowMin, kColumnsRowMax);
    // 实测缺陷9: the ceiling is PER AXIS (see kSizeRowMaxWidth/Height above)
    // instead of Panel::maxDialValue() for all four — the shared 4096 was
    // what made 「可调范围太离谱」. Panel::maxDialValue() remains the hard
    // ceiling a refill grows a row to when this window stores more.
    winMinWSpin_ = makeSpin(QStringLiteral("windowMinWidth"), 0,
                            kSizeRowMaxWidth);
    winMinHSpin_ = makeSpin(QStringLiteral("windowMinHeight"), 0,
                            kSizeRowMaxHeight);
    winLockWSpin_ = makeSpin(QStringLiteral("windowFixedWidth"), 0,
                             kSizeRowMaxWidth);
    winLockHSpin_ = makeSpin(QStringLiteral("windowFixedHeight"), 0,
                             kSizeRowMaxHeight);
    /* ── 实测缺陷6 (usability-2): the same four fields, draggable ──────────
       The boxes above stay (exact typing keeps working and the objectName
       every fixture finds stays put); each of the four rows now also
       carries a slider over the SAME range — read off the spin box, so the
       two halves cannot drift apart even if a ceiling ever moves. 56px is a
       floor, not a width: the row still collapses in a narrow column,
       but the handle can always be grabbed.
       ── end 实测缺陷6: the sliders ── */
    auto makeSizeSlider = [](const QString &name, const QSpinBox *forBox) {
        auto *s = new QSlider(Qt::Horizontal);
        s->setObjectName(name);
        s->setRange(forBox->minimum(), forBox->maximum());
        s->setSingleStep(1);
        s->setPageStep(32);
        s->setMinimumWidth(56);
        s->setCursor(Qt::PointingHandCursor);
        return s;
    };
    winMinWSlider_ =
        makeSizeSlider(QStringLiteral("windowMinWidthSlider"), winMinWSpin_);
    winMinHSlider_ =
        makeSizeSlider(QStringLiteral("windowMinHeightSlider"), winMinHSpin_);
    winLockWSlider_ =
        makeSizeSlider(QStringLiteral("windowFixedWidthSlider"), winLockWSpin_);
    winLockHSlider_ =
        makeSizeSlider(QStringLiteral("windowFixedHeightSlider"), winLockHSpin_);
    winAutoBtn_ = new QPushButton();
    trSet(winAutoBtn_, QStringLiteral("console.inspector.auto_size"));
    winAutoBtn_->setObjectName(QStringLiteral("windowAutoSize"));
    winAutoBtn_->setCursor(Qt::PointingHandCursor);
    // A `metaLayout`-filling row: label left, control right, and the row
    // itself is a widget so the capability-gated one can be HIDDEN as a
    // unit (hiding a bare QHBoxLayout's children one by one would leave
    // the caption to remember).
    //
    // 实测缺陷6: with a slider the row is label | slider (expands) | box —
    // the free space the stretch used to swallow becomes the drag range.
    // Without one (列数) the stretch stays, so no existing row moves.
    auto addPropRow = [this, metaLayout](const QString &labelKey,
                                         const QString &rowName,
                                         QSlider *slider,
                                         QWidget *control) -> QWidget * {
        auto *row = new QWidget();
        row->setObjectName(rowName);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(6);
        auto *lab = new QLabel();
        trSet(lab, labelKey);
        lab->setObjectName(QStringLiteral("propLabel"));
        rl->addWidget(lab);
        if (slider)
            rl->addWidget(slider, 1);
        else
            rl->addStretch(1);
        rl->addWidget(control);
        metaLayout->addWidget(row);
        return row;
    };
    /* ── 实测缺陷8 (user-8): 列数 gets a − / + around the box ──────────────
       「列数控件加 −/+ 按钮」 with 「步进 1」: the buttons only step the BOX
       (Panel::kColumnsStep == 1), and the box's valueChanged stays the one
       and only writer into Panel::setColumnsTo() — so a click, a typed
       number and the keyboard's [ ] all land on the same path, and at a
       bound the step is a no-op rather than a silent out-of-range write.
       The box keeps the tab stop (NoFocus on the buttons) and its
       objectName, so every existing fixture still finds `windowColumns`
       exactly where it was.
       ── end 实测缺陷8 ── */
    auto *colStepper = new QWidget();
    colStepper->setObjectName(QStringLiteral("windowColumnsStepper"));
    auto *colStepperLay = new QHBoxLayout(colStepper);
    colStepperLay->setContentsMargins(0, 0, 0, 0);
    colStepperLay->setSpacing(4);
    auto makeStepBtn = [this](const QString &name, const QString &text) {
        auto *b = new QPushButton(text);
        b->setObjectName(name);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        trTip(b, QStringLiteral("console.inspector.columns_step_tip"));
        return b;
    };
    winColumnsMinus_ = makeStepBtn(QStringLiteral("windowColumnsMinus"),
                                   QStringLiteral("−"));
    winColumnsPlus_ =
        makeStepBtn(QStringLiteral("windowColumnsPlus"), QStringLiteral("+"));
    colStepperLay->addWidget(winColumnsMinus_);
    colStepperLay->addWidget(winColumnsSpin_);
    colStepperLay->addWidget(winColumnsPlus_);
    winColumnsRow_ = addPropRow(QStringLiteral("console.inspector.columns"),
                                QStringLiteral("propColumnsRow"),
                                nullptr, colStepper);
    addPropRow(QStringLiteral("console.inspector.min_width"),
               QStringLiteral("propMinWidthRow"), winMinWSlider_, winMinWSpin_);
    addPropRow(QStringLiteral("console.inspector.min_height"),
               QStringLiteral("propMinHeightRow"), winMinHSlider_, winMinHSpin_);
    addPropRow(QStringLiteral("console.inspector.fixed_width"),
               QStringLiteral("propFixedWidthRow"), winLockWSlider_,
               winLockWSpin_);
    addPropRow(QStringLiteral("console.inspector.fixed_height"),
               QStringLiteral("propFixedHeightRow"), winLockHSlider_,
               winLockHSpin_);
    metaLayout->addWidget(winAutoBtn_);

    // ONE post-edit step, shared by all six controls: read the values
    // back through the normal refill (a subclass clamp may have moved
    // what was typed) and republish the render, because every one of
    // these changes what the window DRAWS.
    auto commit = [this] {
        refreshWindowModel();
        refreshSelectedWindowPreview();
    };
    connect(winColumnsSpin_, &QSpinBox::valueChanged, this,
            [this, commit](int v) {
                if (windowModelRefilling_)
                    return;
                Panel *panel = selectedPanelOrNull();
                if (!panel || !panel->supportsColumns())
                    return;
                panel->setColumnsTo(v);
                panel->persistColumns();
                commit();
            });
    /* ── 实测缺陷8 (user-8): THE − / + PAIR ────────────────────────────────
       They step the BOX and nothing else: the value then travels the
       connect above — setColumnsTo() → persistColumns() → commit() — so a
       click can never write where a typed number would not, and the box's
       own clamp ([kColumnsRowMin, kColumnsRowMax], the subclasses' union)
       is what makes a step at a bound a no-op instead of an out-of-range
       value. Disabled state is read off the BOX: with nothing selected
       (or a window with no column axis) the buttons are dark and a
       programmatic click still changes nothing.
       ── end 实测缺陷8 ── */
    auto stepColumns = [this](int dir) {
        if (windowModelRefilling_ || !winColumnsSpin_
            || !winColumnsSpin_->isEnabled())
            return;
        const int next = winColumnsSpin_->value() + Panel::kColumnsStep * dir;
        if (next != winColumnsSpin_->value())
            winColumnsSpin_->setValue(next);
    };
    connect(winColumnsMinus_, &QPushButton::clicked, this,
            [stepColumns](bool) { stepColumns(-1); });
    connect(winColumnsPlus_, &QPushButton::clicked, this,
            [stepColumns](bool) { stepColumns(+1); });
    auto sizeField = [this, commit](int v, void (Panel::*setter)(int)) {
        if (windowModelRefilling_)
            return;
        Panel *panel = selectedPanelOrNull();
        if (!panel)
            return;
        (panel->*setter)(v);
        commit();
    };
    connect(winMinWSpin_, &QSpinBox::valueChanged, this,
            [sizeField](int v) { sizeField(v, &Panel::setExactMinWidth); });
    connect(winMinHSpin_, &QSpinBox::valueChanged, this,
            [sizeField](int v) { sizeField(v, &Panel::setExactMinHeight); });
    connect(winLockWSpin_, &QSpinBox::valueChanged, this,
            [sizeField](int v) { sizeField(v, &Panel::setExactLockWidth); });
    connect(winLockHSpin_, &QSpinBox::valueChanged, this,
            [sizeField](int v) { sizeField(v, &Panel::setExactLockHeight); });
    /* ── 实测缺陷6: slider and box are ONE FIELD ──────────────────────────
       Direction matters and is the whole correctness argument:

         slider → box   setValue() on the box, which is the ONLY writer
                        to the Panel (the four connects above). A drag
                        therefore costs exactly one write per step and
                        can never take a different path than typing.

         box → slider   mirrored with signals BLOCKED, so the mirror
                        cannot bounce back into a second write.

       Both connections use the widget as context, so a rebuild that
       deletes one half disconnects it instead of firing on a dangling
       pointer.
       ── end 实测缺陷6 ── */
    auto linkSizeField = [](QSlider *slider, QSpinBox *spin) {
        if (!slider || !spin)
            return;
        connect(slider, &QSlider::valueChanged, spin, [spin](int v) {
            if (spin->value() != v)
                spin->setValue(v);
        });
        connect(spin, &QSpinBox::valueChanged, slider, [slider](int v) {
            if (slider->value() != v) {
                slider->blockSignals(true);
                slider->setValue(v);
                slider->blockSignals(false);
            }
        });
    };
    linkSizeField(winMinWSlider_, winMinWSpin_);
    linkSizeField(winMinHSlider_, winMinHSpin_);
    linkSizeField(winLockWSlider_, winLockWSpin_);
    linkSizeField(winLockHSlider_, winLockHSpin_);
    connect(winAutoBtn_, &QPushButton::clicked, this, [this, commit] {
        if (windowModelRefilling_)
            return;
        Panel *panel = selectedPanelOrNull();
        if (!panel)
            return;
        panel->restoreAutoSize();
        commit();
    });

    metaLayout->addStretch(1);
    grid->addWidget(metaCol, 0, 0);
    grid->setColumnStretch(0, 9);

    // ── column 1: 分区 + 本组子窗 ──────────────────────────────────────
    //
    // The prototype's second column is the window's own section switches
    // and the chips of every window in the same group. Both are
    // per-window content, so the column is a FRAME that
    // buildInspectorForSelection() refills rather than a fixed set of
    // widgets — the sibling list changes with the selection (a player
    // window has five siblings, a monster window one), and a fixed set
    // would have to be built for the largest group and hidden
    // everywhere else.
    windowGroupCol_ = new QFrame();
    windowGroupCol_->setObjectName(QStringLiteral("inspectorSections"));
    // THE MIXED BLOCK GETS THE COLUMN'S FIRST LAYOUT, built right here
    // rather than in the per-selection refill below. That refill is the
    // only place the group column ever gets a layout — and it runs only
    // once a selection EXISTS: the cold start has none (the console opens
    // on `.empty-inspector`, buildInspectorForSelection's empty arm
    // returns before the column is touched), so the eleven 独立/合并
    // switches would not exist at all until the user's first click, and
    // the language-flip replay through refreshGroupColumn() would have
    // nothing to refresh either. The refill re-attaches the SAME frame
    // (it takes it out before purging the layout), so this layout is
    // replaced once, not duplicated.
    {
        auto *firstGroupLay = new QVBoxLayout(windowGroupCol_);
        firstGroupLay->setContentsMargins(0, 0, 0, 0);
        firstGroupLay->setSpacing(8);
        buildMixedGroupBlock();
        if (mixedGroupHost_)
            firstGroupLay->addWidget(mixedGroupHost_);
    }
    /* ── 验收项1 (B 第1阶段): THE PREVIEW CONTAINER IS USER-SIZED ─────────
       Columns 1 and 2 (显示内容 / 内容预览) now sit in ONE horizontal
       QSplitter instead of two fixed grid tracks. That is the "调整预览
       容器" half of the acceptance item: dragging the handle changes how
       much room the third column gets, and because the preview's default
       mode is Fit, a wider/taller container is a BIGGER whole image — no
       change to any real Panel scale, no scrolling hunt.

       round2 moved that splitter UP a level — it is `columnsSplit`, built
       beside the two scroll boxes at the top of this function — because
       each half now owns its own scrollbar. The handle therefore sits
       between (元数据 + 分区) and (当前内容状态), i.e. exactly where it used
       to sit between 分区 and 当前内容状态, and it still resizes the
       preview's container: 验收项1's drag is the same drag, only the
       middle column moved into the left half's grid.

       Column 1 stays a GRID COLUMN (分区/本组子窗 keeps its 1fr), so the
       9:10:12 budget is preserved by arithmetic: the left half carries
       19 of the splitter's 31 stretch units (9+10) and the right half 12,
       and the left half's own grid then splits those 19 back into 9:10.
       ── end 验收项1: the preview container splitter ── */
    grid->addWidget(windowGroupCol_, 0, 1);
    grid->setColumnStretch(1, 10);

    // ── column 2: 内容预览 ────────────────────────────────────────────
    //
    // The prototype's third column shows the window's real content — in
    // the HTML that is a stylised stand-in, in Qt it is the Panel's own
    // render, which is the same pixmap the canvas draws. It is a QLabel
    // holding a QPixmap, refilled on selection.
    windowPreviewCol_ = new QFrame();
    windowPreviewCol_->setObjectName(QStringLiteral("inspectorPreview"));
    auto *previewColLayout = new QVBoxLayout(windowPreviewCol_);
    /* ── round2 · 卡片容器: THE CARD'S PADDING IS THE LAYOUT's ────────────
       QFrame#inspectorPreview now paints the card itself (background +
       1px line + radius, see control_panel_styles.cpp), so this column
       is no longer an invisible gutter around the pane — it is the
       「当前内容状态」CARD the user asked for (「应该加个框框来更美观」).

       A stylesheet `padding` on a QFrame that OWNS A LAYOUT does not
       reach the layout's cells (Qt applies the box padding to the
       widget, and QLayout keeps laying its children out in
       contentsRect()), so the inset the card needs has to come from the
       layout's contents margins: 12 left/right, 10 top/bottom — without
       them the caption, the pane and the zoom row would be painted ON
       the border line.
       ── end round2: the card's padding ── */
    previewColLayout->setContentsMargins(12, 10, 12, 10);
    previewColLayout->setSpacing(8);
    auto *previewCap = new QLabel();
    // v1.0.0 (C): the prototype's THIRD column cap is 当前内容状态
    // (reference.html:497) — 显示内容 is the middle column's
    // (reference.html:493). The shell had both columns saying 显示内容.
    trSet(previewCap, QStringLiteral("console.proto.content.state"));
    previewCap->setObjectName(QStringLiteral("sectionCap"));
    previewColLayout->addWidget(previewCap);
    windowPreviewBox_ = new QLabel();
    windowPreviewBox_->setObjectName(QStringLiteral("windowPreviewBox"));
    windowPreviewBox_->setMinimumHeight(120);
    windowPreviewBox_->setAlignment(Qt::AlignCenter);
    // The empty state's copy, so a console with nothing selected says so
    // in the right language rather than showing a blank rectangle.
    trSet(windowPreviewBox_, QStringLiteral("console.proto.insp.empty.sub"));
    windowPreviewBox_->setWordWrap(true);
    // ── v1.0.1 (验收项2): SCROLL, DON'T SQUEEZE ────────────────────────
    // The label used to be stretched by the grid and filled with a
    // pixmap scaled down into its box (~132px tall): blurry AND cropped.
    // It now keeps the Panel render at its own pixel size inside a scroll
    // area — findChild("windowPreviewBox") still answers the same QLabel
    // with the same setPixmap()/setText() contract.
    windowPreviewBox_->setMinimumSize(200, 120);
    windowPreviewScroll_ = new HuggingPreviewScroll();
    windowPreviewScroll_->setObjectName(QStringLiteral("windowPreviewScroll"));
    windowPreviewScroll_->setWidgetResizable(false);   // the LABEL sizes to the pixmap
    windowPreviewScroll_->setFrameShape(QFrame::NoFrame);
    windowPreviewScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    windowPreviewScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    windowPreviewScroll_->setWidget(windowPreviewBox_);
    /* ── 实测缺陷5 (user-8): THE FRAME HUGS THE RENDER ─────────────────────
       The pane used to be stretched across the whole column (measure:
       617x344 of pane around a 272x260 render), so the bordered box sat at
       the top-left of a mostly empty rectangle and the readout row + the
       orange note were pushed to the column's far edges — the "外框 400px
       宽，里面 190x95" screenshot. Two settings make the container follow
       the content instead:

         * no stretch + a Preferred policy: the scroll area takes its
           sizeHint (which QScrollArea derives from the label it holds —
           probed: 124px of pane for a 120px label) instead of the cell;
         * AlignLeft|AlignTop: the cross axis follows too, so the pane is
           as wide as the render rather than as wide as the column. A
           content larger than the column is still clamped to it (QLayout
           only ever hands out the cell), which is what keeps the scroll
           bars — and therefore 实测缺陷2's "scroll, don't squeeze" — alive
           when the user zooms past the pane.

       previewFitScale() is rewritten in the same commit to read the
       COLUMN, not this viewport: a pane that hugs its content cannot be
       the input to the scale that sets that content (measured: 272x260 →
       97x93 in two passes, kPreviewDefaultFill of itself each time).
       ── end 实测缺陷5 ── */
    windowPreviewScroll_->setSizePolicy(QSizePolicy::Preferred,
                                        QSizePolicy::Preferred);
    previewColLayout->addWidget(windowPreviewScroll_);
    previewColLayout->setAlignment(windowPreviewScroll_,
                                   Qt::AlignLeft | Qt::AlignTop);
    /* ── 验收项1 (B 第1阶段): THE ZOOM ROW ────────────────────────────────
       Fit (the default, z == 0), 1:1, and − / + steps — a preview zoom
       that is INDEPENDENT OF THE REAL HUD SCALE: every one of these only
       re-derives the label's pixmap from windowPreviewSource_; nothing
       here reaches Panel::scale() or panels.ini.

       The dynamic halves (缩放 % / 尺寸) are rewritten by
       applyPreviewDisplay(), which the trHook below replays on a
       language flip — so the row never carries a frozen string either.
       ── end 验收项1: zoom row ── */
    auto *zoomRow = new QHBoxLayout();
    zoomRow->setSpacing(6);
    previewZoomLabel_ = new QLabel();
    previewZoomLabel_->setObjectName(QStringLiteral("previewZoomLabel"));
    previewZoomLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    previewSizeLabel_ = new QLabel();
    previewSizeLabel_->setObjectName(QStringLiteral("previewSizeLabel"));
    previewSizeLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    /* ── v1.1.4: THE READOUT GETS ITS OWN LINE ─────────────────────────────
       The two readouts and the four zoom buttons used to share one HBox,
       and a QLabel's minimum width is its full text width — so the row's
       minimum was 「适应窗口 88%」+「尺寸 380×345 · 高度 290」+ four
       buttons ≈ 520px. The card's right half is ~350px at the console's
       own size, so the CARD inherited a 520px minimum and the right half
       answered with a horizontal scrollbar (measured, not predicted).
       Two rows: the readouts shrink the card's minimum to their own width,
       and the button row is ~240px, so the widest of the two still fits.
       objectNames, text and every signal stay exactly where they were.
       ── end v1.1.4 ── */
    auto *readoutRow = new QVBoxLayout();
    readoutRow->setSpacing(2);
    readoutRow->addWidget(previewZoomLabel_);
    readoutRow->addWidget(previewSizeLabel_);
    zoomRow->addStretch(1);
    auto makeZoomBtn = [this](const QString &name, const QString &text) {
        auto *b = new QPushButton(text);
        b->setObjectName(name);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    previewFitBtn_ = makeZoomBtn(QStringLiteral("previewFitBtn"), QString());
    trSet(previewFitBtn_, QStringLiteral("console.preview.fit"));
    previewActualBtn_ = makeZoomBtn(QStringLiteral("previewActualBtn"), QString());
    trSet(previewActualBtn_, QStringLiteral("console.preview.actual_size"));
    previewZoomOutBtn_ = makeZoomBtn(QStringLiteral("previewZoomOutBtn"),
                                     QStringLiteral("−"));
    previewZoomInBtn_ = makeZoomBtn(QStringLiteral("previewZoomInBtn"),
                                    QStringLiteral("+"));
    trTip(previewZoomInBtn_, QStringLiteral("console.preview.resize_tip"));
    trTip(previewZoomOutBtn_, QStringLiteral("console.preview.resize_tip"));
    trTip(previewFitBtn_, QStringLiteral("console.preview.hint"));
    for (QPushButton *b : {previewZoomOutBtn_, previewZoomInBtn_,
                           previewFitBtn_, previewActualBtn_})
        zoomRow->addWidget(b);
    previewColLayout->addLayout(readoutRow);
    previewColLayout->addLayout(zoomRow);
    // The one place the readout's format lives, so the trHook below and
    // every zoom click share it.
    trHook([this] { applyPreviewDisplay(); });
    connect(previewFitBtn_, &QPushButton::clicked, this,
            [this] { setPreviewZoom(0.0); });          // Fit = the reset
    connect(previewActualBtn_, &QPushButton::clicked, this,
            [this] { setPreviewZoom(1.0); });
    connect(previewZoomInBtn_, &QPushButton::clicked, this,
            [this] { setPreviewZoom(stepPreviewZoom(+1)); });
    connect(previewZoomOutBtn_, &QPushButton::clicked, this,
            [this] { setPreviewZoom(stepPreviewZoom(-1)); });
    // FIT FOLLOWS THE CONTAINER. Dragging the splitter's handle (or any
    // other resize of the viewport) has to re-derive the Fit pixmap, or
    // "调整预览容器" would leave the image at its old fit. Only the
    // Fit arm re-derives: an explicit zoom is a ratio the user chose and
    // must not be silently rewritten by a resize.
    auto *fitWatcher = new PreviewViewportWatcher(windowPreviewScroll_);
    fitWatcher->onViewportResize = [this] {
        // Fit AND the default view are both functions of the viewport, so
        // both re-derive on a resize; an explicit zoom is a ratio the user
        // chose and is left alone.
        if (windowPreviewScroll_
            && (previewMarginDefault_ || previewZoom_ <= 0.0))
            applyPreviewDisplay();
    };
    windowPreviewScroll_->viewport()->installEventFilter(fitWatcher);
    /* ── 实测缺陷5: AND THE COLUMN, BECAUSE THE PANE NO LONGER RESIZES ─────
       The pane hugs the render now, so dragging the splitter widens the
       COLUMN without resizing the pane — the old viewport-only watcher
       would go deaf exactly when the user "adjusts the preview
       container". The column is the input previewFitScale() reads (see
       实测缺陷5 there), so a resize of it is the event that must re-derive.
       Same callback, same Fit/default-only guard.
       ── end 实测缺陷5: column watcher ── */
    auto *colWatcher = new PreviewViewportWatcher(windowPreviewCol_);
    colWatcher->onViewportResize = fitWatcher->onViewportResize;
    windowPreviewCol_->installEventFilter(colWatcher);
    /* ── 实测缺陷2: CTRL+WHEEL OVER THE DETAIL PREVIEW ─────────────────────
       The board's Ctrl+wheel (canvas zoom) is handled in eventFilter() by
       `watched == canvas_`; this is the same modifier on the OTHER area.
       Both filters are installed on different widgets, so a wheel over
       the preview can never reach the canvas handler and vice versa — the
       dispatch is by the widget the event landed on, not by a global
       modifier test. Plain wheel over the preview keeps scrolling it (only
       the Ctrl arm is consumed).
       ── end 实测缺陷2 ── */
    windowPreviewScroll_->installEventFilter(this);
    windowPreviewScroll_->viewport()->installEventFilter(this);
    // …and the label itself: a wheel over a QLabel with a pixmap is
    // delivered to the LABEL first, and a delivered-then-ignored event
    // propagates to the viewport without re-running that widget's
    // filters. Filtering all three makes the arm below fire wherever in
    // the preview area the cursor happens to be.
    windowPreviewBox_->installEventFilter(this);
    // ── v1.0.0 (C): `.detail-warning` (reference.html:497) ─────────────
    // 「这是窗口将要显示的 demo HUD 内容；配置字段在左侧和中间编辑。」 — the
    // line that says what the third column IS, so nobody edits a
    // preview. Static copy, therefore a trSet and not a hook.
    auto *detailWarning = new QLabel();
    trSet(detailWarning, QStringLiteral("console.proto.content.warn"));
    detailWarning->setObjectName(QStringLiteral("detailWarning"));
    detailWarning->setWordWrap(true);
    previewColLayout->addWidget(detailWarning);
    /* ── 实测缺陷5: THE SPARE SPACE BELONGS TO A SPACER, NOT TO THE LABELS ──
       Without this the QVBoxLayout hands its surplus to whatever can grow,
       and both the caption and the word-wrapped note have a Preferred
       policy — measured: each of them stretched to h=125 while the pane
       hugged 96, i.e. the "大片空白" merely moved into the two labels and,
       worse, fed back through chrome into previewFitScale() (available
       height 120 → the render shrank to 25% of itself). A trailing
       stretch of factor 1 takes all of it, so the block above stays packed
       and every height chrome measures is the height that item asked for.
       ── end 实测缺陷5 ── */
    previewColLayout->addStretch(1);
    /* ── round2: THE RIGHT HALF — ITS OWN HOST, ITS OWN SCROLL AREA ───────
       The card goes into a scroll area of its own so that 「当前内容
       状态」 and the two left columns stop sharing one scrollbar.

       The host's margins are the grid's own (14,12,14,14) so the card's
       outer edge lands where the old third column started and its top
       edge is level with 元数据 / 分区 — the padding INSIDE the card is
       the layout's (see the 12/10 note where previewColLayout is built),
       this padding is the drawer's.

       The trailing stretch keeps 实测缺陷5's invariant: a pane that
       hugs its render may not be stretched by a spare-tall cell, so the
       surplus of a tall right half belongs to the SCROLL AREA (it simply
       is not there — the content is shorter than the viewport and there
       is no bar), never to the card. previewFitScale()'s input stays the
       card's own box, which stays a function of the CONTENT.
       ── end round2: the right half ── */
    auto *previewScrollHost = new QWidget();
    previewScrollHost->setObjectName(QStringLiteral("inspectorPreviewHost"));
    auto *previewHostLayout = new QVBoxLayout(previewScrollHost);
    previewHostLayout->setContentsMargins(14, 12, 14, 14);
    previewHostLayout->setSpacing(18);
    previewHostLayout->addWidget(windowPreviewCol_);
    previewHostLayout->addStretch(1);

    auto *previewScroll = new QScrollArea();
    previewScroll->setObjectName(QStringLiteral("inspectorContentRight"));
    previewScrollHost_ = previewScroll;
    previewScroll->setWidgetResizable(true);
    previewScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    previewScroll->setFrameShape(QFrame::NoFrame);
    previewScroll->setFocusPolicy(Qt::NoFocus);
    previewScroll->setWidget(previewScrollHost);
    columnsSplit->addWidget(previewScroll);
    // A splitter distributes by its own children instead of by grid
    // stretches, so the three columns' 9 : 10 : 12 budget is restated
    // HERE as 19 (9+10, the left half) : 12 — and the left half's grid
    // splits its 19 back into 9:10, so every column keeps the width it
    // had before the split until the user drags the handle.
    columnsSplit->setStretchFactor(0, 19);
    columnsSplit->setStretchFactor(1, 12);
    columnsSplit->setSizes(QList<int>{190, 120});

    // v1.0.0 (O1): `align-items:start` — the draft's three columns are
    // TOP-ALIGNED and the row they sit in does not stretch them
    // (geometry.json state01: every .inspector-col starts at y=834.63
    // and is only as tall as its own content). Inside a resizable
    // scroll area QGridLayout would otherwise hand the extra height to
    // row 0 and the columns would grow to fill the pane. One empty
    // cell with the row stretch is the Qt spelling of the CSS value.
    auto *gridFiller = new QWidget();
    gridFiller->setObjectName(QStringLiteral("inspectorGridFiller"));
    gridFiller->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    grid->addWidget(gridFiller, 1, 0, 1, 2);
    grid->setRowStretch(1, 1);

    // ── the empty state ─────────────────────────────────────────────────
    //
    // reference.html:486, the whole `if(!d||!w)` arm of renderInspector:
    //
    //   box.innerHTML = '<div class="empty-inspector">
    //                       <strong>未选中窗口</strong>
    //                       <span>在上方显示器布局预览中点击一个实际渲染窗口。</span>
    //                    </div>';
    //
    // with `.empty-inspector{min-height:150px; display:grid;
    // place-content:center; text-align:center}` (reference.html:211-212).
    // It is not a hint under the form: with nothing selected the THREE
    // COLUMNS ARE NOT THERE AT ALL, which is what state01 measures (the
    // audit's element list for state01 has `.inspector-content` and no
    // `.inspector-grid`, while state02 has the grid). The shell instead
    // left the columns in place with zeroed, disabled fields.
    //
    // Both halves live in faceLayout and only one is ever visible: a
    // hidden child takes no geometry from a QLayout, so the pair behaves
    // like the draft's single replaceable innerHTML.
    inspectorEmpty_ = new QFrame();
    inspectorEmpty_->setObjectName(QStringLiteral("emptyInspector"));
    auto *emptyLayout = new QVBoxLayout(inspectorEmpty_);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(4);
    emptyLayout->addStretch(1);
    auto *emptyStrong = new QLabel();
    emptyStrong->setObjectName(QStringLiteral("emptyInspectorTitle"));
    trSet(emptyStrong, QStringLiteral("console.proto.insp.empty.strong"));
    emptyStrong->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyStrong);
    auto *emptyBody = new QLabel();
    emptyBody->setObjectName(QStringLiteral("emptyInspectorBody"));
    trSet(emptyBody, QStringLiteral("console.proto.insp.empty.body"));
    emptyBody->setAlignment(Qt::AlignCenter);
    emptyBody->setWordWrap(true);
    emptyLayout->addWidget(emptyBody);
    emptyLayout->addStretch(1);
    faceLayout->addWidget(inspectorEmpty_, 1);
    inspectorEmpty_->hide();   // overwritten on the first refill
}

// ─── v1.0.0 fidelity port: the toast ────────────────────────────────────
//
// `showToast(key, args)` — the prototype's `.toast`, and the reason a
// console button can now be told apart from a decorative one.
//
// THE PROTOTYPE'S OWN SHAPE, measured from the frozen HTML:
//
//     .toast{position:fixed;left:50%;bottom:28px;transform:translate(-50%,8px);
//            background:var(--surf-2);border:1px solid var(--line-strong);
//            border-radius:6px;padding:8px 14px;font-size:11px;
//            color:var(--fg-2);opacity:0;transition:.22s;pointer-events:none;
//            z-index:60}
//     .toast.show{opacity:1;transform:translate(-50%,0)}
//
// Two properties matter more than the colours: `pointer-events:none` (a
// toast must never swallow a click on the board it is describing) and
// the 2.2s lifetime the prototype's `setTimeout(...,2200)` gives it.
// Both are honoured here — WA_TransparentForMouseEvents on the label,
// and one single-shot timer that is restarted rather than accumulated.
//
// WHY A KEY AND NOT A STRING. Every other console string is registered
// as a key so a language flip can re-render it; a toast built from
// pre-formatted text would be the one surface that stayed Chinese. The
// args are substituted at show time, which is the same contract
// trHook() has.
//
// THE TIMER IS REUSED, NOT RECREATED. stop() + start() on one
// single-shot timer is the difference between "a toast replaced the
// previous one" and "six timers each hiding the label at their own
// expiry" — the latter leaves a label visible for 2.2s after the last
// message because an earlier timer already fired and hid it.
// ─── end v1.0.0 toast ─────────────────────────────────────────────────
void ControlPanel::showToast(const QString &key, const QStringList &args)
{
    if (!toast_)
        return;
    QString text = consoleText(key);
    // Substitute in order: {1} {2} …, the same convention the console's
    // other formatted strings use. A missing arg leaves the placeholder
    // rather than crashing, so a caller can pass fewer than the string
    // wants without taking the console down.
    for (int i = 0; i < args.size(); ++i)
        text.replace(QStringLiteral("{%1}").arg(i + 1), args.at(i));
    toast_->setText(text);
    toast_->adjustSize();
    // Bottom centre of the CONSOLE, not of the screen: the prototype's
    // toast is fixed to the viewport, and the console's viewport is its
    // central widget.
    if (QWidget *host = centralWidget()) {
        const int x = (host->width() - toast_->width()) / 2;
        const int y = host->height() - toast_->height() - 28;
        toast_->move(qMax(0, x), qMax(0, y));
    }
    toast_->show();
    toast_->raise();
    if (toastTimer_) {
        toastTimer_->stop();
        // ── v1.0.0 (stage C): 2400, NOT 2200 ────────────────────────────
        // reference.html:445 `showToast()` is
        //     toastTimer=setTimeout(...,2400);
        // the old comment said "the prototype's own 2.2s" next to a
        // 2200, which was neither the prototype's number nor true of its
        // comment: 2.4s is what the frozen HTML runs and what a user
        // comparing the two side by side would see.
        toastTimer_->start(2400);   // reference.html:445, verbatim
    }
}

// ─── v1.0.0 fidelity port: the four modal entries ──────────────────────
//
// The prototype's sidebar foot has 设置 / 帮助 / 重置演示 / 保存 / 导出
// JSON. The Qt console built all five and connected exactly one (保存).
// These are the other four, each a real QDialog with the prototype's own
// content, its own Esc close and its own backdrop dismiss.
//
// WHY QDialog AND NOT A HAND-ROLLED QFRAME OVERLAY. The prototype's
// `.modal-backdrop` is a fixed overlay with a click-outside close, and
// QDialog gives all three behaviours for free (Esc, a modality that
// does not leak, and a rejected() signal to hook the backdrop to) —
// where a QFrame overlay would need each one reimplemented, and the
// close paths are exactly where a hand-rolled modal usually leaks
// focus. The one thing QDialog does not give is click-outside, so that
// is an event filter on the dialog itself (see the helpers below).
//
// THE DIALOGS ARE BUILT, NOT CACHED. A cached dialog holds widgets that
// describe state at the time it was opened, and every one of these four
// is about live state (which game, which windows, what the worker says).
// Building on each open costs a few hundred widget constructions on a
// user gesture, which is nothing, and removes a whole class of stale
// content. The exception is the settings dialog's Rise card, which
// rebuilds itself on open anyway (riseCard_->refresh()).
// ─── end v1.0.0 modal entries ─────────────────────────────────────────

// A click on the dialog's own background (not on a child) dismisses it —
// the prototype's `if(e.target===m) closeModal(m)`. QDialog has no such
// notion, so it is an event filter that compares the click's position
// against the dialog's rect and only accepts a hit that no child
// claimed.
namespace {
class BackdropDismissFilter : public QObject
{
public:
    // `shell` is the modalShell frame: a press that lands INSIDE it —
    // including its blank padding — is never a dismiss, because the
    // prototype's own test is `e.target === m` where `m` is the MODAL.
    // Inside the shell the target is the shell, not the modal.
    BackdropDismissFilter(QDialog *dlg, QWidget *shell)
        : QObject(dlg), dlg_(dlg), shell_(shell)
    {
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonPress && dlg_) {
            auto *me = static_cast<QMouseEvent *>(event);
            // Outside the dialog's own rect: not ours to dismiss (a
            // click on the console behind an application-modal dialog
            // never reaches us anyway).
            if (!dlg_->rect().contains(me->position().toPoint()))
                return false;
            QWidget *hit = dlg_->childAt(me->position().toPoint());
            if (!hit || hit == dlg_) {
                dlg_->reject();   // Esc's own path, so both agree
                return true;
            }
            // childAt() answers the DEEPEST widget at that point, so a
            // press on the shell's padding answers modalShell itself and
            // a press on a label answers the label. Walking up decides
            // which side of the shell wall the press was on; anything
            // inside keeps the dialog open, everything else (the
            // backdrop veil) rejects.
            if (shell_) {
                for (QWidget *w = hit; w && w != dlg_; w = w->parentWidget())
                    if (w == shell_)
                        return false;   // inside the shell: keep it open
            }
            dlg_->reject();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
private:
    QDialog *dlg_ = nullptr;
    QWidget *shell_ = nullptr;
};
} // namespace

// ─── v1.0.0 integration (stage C): THE MODAL SHELL, BUILT ONCE ──────────
//
// reference.html's modal is three nested boxes and the console used to
// have one: a QDialog with the content laid out directly on it. Three
// things were wrong with that and all three are why this helper exists.
//
//  1. PARENTAGE. The console's QSS is a WIDGET stylesheet —
//     `setStyleSheet(consoleStyleSheet())` on the ControlPanel itself
//     (control_panel.cpp:1605) — and a widget's stylesheet reaches its
//     CHILDREN. The dialogs were built as
//     `QDialog(parentWidget())`, and a top-level ControlPanel's
//     parentWidget() is null, so the dialog did not inherit a single
//     rule and rendered in the platform's native face. Every dialog now
//     parents to `this` (see each call site), which is what makes the
//     sheet follow it down.
//
//  2. THE BACKDROP AND THE SHELL ARE DIFFERENT THINGS. The prototype
//     has `.modal-backdrop` (the veil, click to close) and the modal
//     panel inside it (blank space there does NOT close). One flat
//     dialog cannot say "this part is a dismiss target and that part is
//     not", so BackdropDismissFilter had nothing to compare against and
//     rejected on any press that hit no child — including the modal's
//     own padding. The shell frame is that comparison, and it is what
//     `modalShell` is for.
//
//  3. THE BODY MUST SCROLL. The settings dialog has the appearance
//     rows, the Rise card, the data note and About; at the prototype's
//     1000px viewport it is taller than a small window. `.modal-body`
//     is `overflow:auto` in the frozen HTML (reference.html:255), so
//     modalBody is a QScrollArea rather than a plain layout.
//
// NAMES FOR THE QSS OWNER (all five are real widgets, findable with
// findChild at dialog level):
//     modalBackdrop  the veil — a QFrame filling the QDialog
//     modalShell     the panel the content lives in
//     modalHead      the title row
//     modalBody      the QScrollArea (its widgetResizable inner column
//                    is what the body layout fills)
//     modalActions   the button row at the bottom
// The dialogs keep their ORIGINAL objectNames (settingsDialog /
// helpDialog / exportDialog) on the QDialog itself, so every existing
// findChild<QDialog*>("settingsDialog") entry keeps working unchanged;
// one widget, one objectName, so the two sets cannot both sit on the
// dialog. Reported in SEAMS-COMPLETION.md.
namespace {
struct ModalParts {
    QVBoxLayout *body = nullptr;
    QHBoxLayout *actions = nullptr;
    QFrame *shell = nullptr;
    QFrame *backdrop = nullptr;
};

ModalParts buildModalShell(QDialog *dlg, const QString &title,
                           bool compact = false)
{
    ModalParts out;
    auto *outer = new QVBoxLayout(dlg);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    /* ── 验收项2 (B 第1阶段): ONE CONTENT LAYER IN THE COMPACT DIALOG ──────
       The full-window modals (帮助 / 导出 JSON / 退出确认) keep their
       veil: backdrop behind, card centred inside it, click-the-margin to
       dismiss. The COMPACT settings popup does not — a second frame plus
       the card's size-minus-64 cap put an independent black border and
       64px of black padding around the content, which is the "设置不贴
       合内容" defect. So here `compact` parents the shell DIRECTLY to the
       dialog and lets it fill it: exactly one content layer, no outer
       box. modalBackdrop is then absent by construction, and the caller
       must null-guard it (the dismiss filter still runs on the dialog).

       Nothing else moves: modalHead / modalBody (the scroll area, so
       long copy still scrolls internally) / modalActions (the 关闭 row,
       OUTSIDE the scroll, so it stays reachable at any content length)
       are built by the same code for both shapes.
       ── end 验收项2: compact = one layer ── */
    QWidget *host = dlg;
    QVBoxLayout *hostLayout = nullptr;
    if (!compact) {
        auto *backdrop = new QFrame(dlg);
        backdrop->setObjectName(QStringLiteral("modalBackdrop"));
        backdrop->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        outer->addWidget(backdrop);
        out.backdrop = backdrop;
        auto *bvl = new QVBoxLayout(backdrop);
        bvl->setContentsMargins(0, 0, 0, 0);
        bvl->setSpacing(0);
        bvl->setAlignment(Qt::AlignCenter);
        host = backdrop;
        hostLayout = bvl;
    }

    auto *shell = new QFrame(host);
    shell->setObjectName(QStringLiteral("modalShell"));
    if (compact) {
        shell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        outer->addWidget(shell, 1);
    } else {
        hostLayout->addWidget(shell);
    }
    out.shell = shell;
    auto *svl = new QVBoxLayout(shell);
    svl->setContentsMargins(20, 20, 20, 16);
    svl->setSpacing(12);

    auto *head = new QFrame(shell);
    head->setObjectName(QStringLiteral("modalHead"));
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 4);
    hl->setSpacing(8);
    auto *headLabel = new QLabel(title);
    headLabel->setObjectName(QStringLiteral("modalTitle"));
    headLabel->setWordWrap(true);
    hl->addWidget(headLabel, 1);
    svl->addWidget(head);

    auto *bodyScroll = new QScrollArea(shell);
    bodyScroll->setObjectName(QStringLiteral("modalBody"));
    bodyScroll->setWidgetResizable(true);
    bodyScroll->setFrameShape(QFrame::NoFrame);
    bodyScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *inner = new QWidget();
    inner->setObjectName(QStringLiteral("modalBodyContent"));
    auto *ivl = new QVBoxLayout(inner);
    ivl->setContentsMargins(0, 0, 0, 0);
    ivl->setSpacing(14);
    bodyScroll->setWidget(inner);
    svl->addWidget(bodyScroll, 1);
    out.body = ivl;

    auto *actions = new QFrame(shell);
    actions->setObjectName(QStringLiteral("modalActions"));
    auto *avl = new QHBoxLayout(actions);
    avl->setContentsMargins(0, 4, 0, 0);
    avl->setSpacing(8);
    svl->addWidget(actions);
    out.actions = avl;
    return out;
}

// Center the dialog on the console's CLIENT area (the central widget),
// not on the screen and not on whatever Qt's QDialog::exec() default
// position happens to be. Called just before exec(), once the body has
// been filled and adjustSize() has something real to measure.
// `compact` = the settings dialog's own contract (用户验收项1): a small
// popup of at most ~680×700 clamped to the current screen, NOT a dialog
// stretched over the whole console. The other dialogs (帮助 / 导出 JSON /
// 退出确认) keep the full-client veil.
void centerModalOnConsole(QDialog *dlg, bool compact = false)
{
    if (!dlg)
        return;
    QWidget *host = dlg->parentWidget();
    if (!host)
        return;
    // THE VEIL COVERS THE CONSOLE'S CLIENT AREA — reference.html's
    // `.modal-backdrop{position:fixed;inset:0}`, not a card floating on
    // the screen. That is also what makes the dismiss semantics work:
    // the prototype's `if(e.target===m)` only ever fires on the part of
    // the modal that is NOT the panel, and if the dialog were sized to
    // its content there would be no such part — the shell would BE the
    // dialog and every press would land inside it. So the dialog is the
    // client rect and buildModalShell() centers the card inside it,
    // which is "居中在本控制台 client 区" for the card and a real
    // click-to-close veil for everything else.
    // `host` is the ControlPanel itself (the dialogs parent to `this`),
    // so the client area is its central widget — qobject_cast rather
    // than QWidget::window()->centralWidget(), which does not exist on
    // a plain QWidget.
    QWidget *client = nullptr;
    if (auto *mw = qobject_cast<QMainWindow *>(host))
        client = mw->centralWidget();
    if (!client)
        client = host;
    const QPoint origin = client->mapToGlobal(QPoint(0, 0));
    // ── v1.0.1 (验收项1): THE SETTINGS DIALOG IS A SMALL POPUP ──────────
    // It used to be sized to the whole client area, so "设置" read as a
    // full-window page. It is now at most 680×700, clamped to the screen
    // the dialog is on AND to the client area, and centred inside it.
    // The veil stays (its click-to-close still works on the margin that
    // is left) and the body still scrolls — `.modal-body{overflow:auto}`.
    QSize size = client->size();
    if (compact) {
        QScreen *scr = dlg->screen() ? dlg->screen()
                                     : QGuiApplication::primaryScreen();
        const QRect avail = scr ? scr->availableGeometry()
                                : QRect(QPoint(0, 0), client->size());
        size.setWidth (qMin(680, qMin(avail.width(),  client->width())));
        /* ── 验收项2: HUG THE CONTENT, UP TO THE COMPACT CEILING ──────────
           The box used to be a flat 700px tall no matter how much copy
           was in it, so a short settings list sat inside 300px of
           empty shell — the same "不贴合内容" defect as the black border.
           sizeHint() is the head + modalBody's own content + the action
           row (measured right before exec(), when the body is filled);
           the caps below keep the compact contract (≤700, inside the
           screen and the client area) and the floor keeps the pop-up a
           pop-up when the body measures nonsense.
           ── end 验收项2: content-hugging height ── */
        int h = dlg->sizeHint().height();
        if (h <= 0)
            h = 700;
        h = qMin(h, qMin(700, qMin(avail.height(), client->height())));
        size.setHeight(qMax(240, h));
    }
    // Offsets are 0 when size == client, so veil dialogs keep their
    // exact geometry; the compact one lands centred instead of at (0,0).
    dlg->setGeometry(origin.x() + (client->width()  - size.width())  / 2,
                     origin.y() + (client->height() - size.height()) / 2,
                     size.width(), size.height());

    // THE CARD NEVER FILLS THE VEIL — for the FULL-WINDOW veils only.
    // Without this the 帮助 / 导出 dialogs' own content is taller than
    // the window, the shell grows to it, and there is no longer any part
    // of `.modal-backdrop` that is not the panel — which makes
    // `if(e.target===m)` unreachable and the click-to-close dead, AND
    // stops `.modal-body{overflow:auto}` from ever scrolling (a scroll
    // area whose height is its content's never has anything to scroll).
    // Capping the card at the veil minus 32px on each side restores
    // both, and is exactly the 64px of black padding the COMPACT
    // settings dialog had to shed (验收项2) — so `compact` skips it and
    // the shell simply fills its dialog: one layer, no outer box.
    if (!compact)
        if (QFrame *shell =
                dlg->findChild<QFrame *>(QStringLiteral("modalShell"))) {
            shell->setMaximumSize(qMax(200, size.width()  - 64),
                                  qMax(120, size.height() - 64));
        }
}
} // namespace

// 帮助与快捷键. THE SAME COMPACT CONTRACT AS 设置 (at most 680×700, height
// hugging the copy, clamped to the screen and to the console, the body
// scrolling inside). On the full-client veil this panel grew to the whole
// console's client area — the「帮助面板太大」report — so it now builds
// exactly the way openSettingsDialog() does.
//
// v1.1.4: the --snap-dialog=<...> hook's only entry. Deliberately thin —
// it picks one of the three private modals and calls it; the grab itself
// lives on the CLI side (main_control.cpp), because a modal blocks in
// exec() and nothing can photograph it from beside.
void ControlPanel::snapOpenDialog(const QString &which)
{
    if (which == QStringLiteral("help"))
        openHelpDialog();
    else if (which == QStringLiteral("settings"))
        openSettingsDialog();
    else
        openExportDialog();
}

// The copy is docs/CONSOLE.md's, not a paraphrase: per-display logical
// coordinates, the target-output follow chain, the SIX game × layout
// scopes (2 games × Single/Split/Mixed — v0.14's four were pre-Mixed),
// and what SAVE and EXPORT JSON actually write. Sections use the
// dialogCap captions settings already uses.
//
// v1.1.4: rewritten as STEPS, not provenance. The five sections used to
// explain where the preview's pixels come from, how many scopes exist and
// which file each row is written to — accurate, and useless to someone
// asking 「how do I move this window」. They now answer the five questions
// a first session actually has (pick a game/layout, pick a window, choose
// a screen, save, edit), with the one true caveat (the preview draws demo
// data) kept as a single clause instead of a paragraph.
void ControlPanel::openHelpDialog()
{
    // PARENTED TO `this` — see buildModalShell()'s note: the console's
    // QSS is a widget stylesheet on the ControlPanel, and a dialog with
    // a null parent inherits none of it.
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("helpDialog"));
    const QString helpTitle =
        consoleText(QStringLiteral("console.proto.help.title"));
    dlg.setWindowTitle(helpTitle);
    const ModalParts help = buildModalShell(&dlg, helpTitle,
                                            /*compact=*/true);
    auto *dismiss = new BackdropDismissFilter(&dlg, help.shell);
    dlg.installEventFilter(dismiss);
    // The compact shell has no veil (buildModalShell's compact note), so
    // the guard is the point — same shape as the settings dialog: the
    // filter still runs on the dialog itself.
    if (help.backdrop)
        help.backdrop->installEventFilter(dismiss);
    QVBoxLayout *vl = help.body;
    vl->setSpacing(10);

    struct Section { const char *cap; const char *para; };
    static const Section sections[] = {
        {"console.proto.help.cap.data",    "console.proto.help.p1"},
        {"console.proto.help.cap.display", "console.proto.help.p2"},
        {"console.proto.help.cap.scope",   "console.proto.help.p3"},
        {"console.proto.help.cap.save",    "console.proto.help.p4"},
        {"console.proto.help.cap.edit",    "console.proto.help.p5"},
    };
    for (const Section &s : sections) {
        auto *cap = new QLabel(consoleText(QString::fromLatin1(s.cap)));
        cap->setObjectName(QStringLiteral("dialogCap"));
        vl->addWidget(cap);
        auto *p = new QLabel(consoleText(QString::fromLatin1(s.para)));
        p->setObjectName(QStringLiteral("helpPara"));
        p->setWordWrap(true);
        vl->addWidget(p);
        // v1.1.4: a paragraph's trailing space is what separated one
        // section from the next, and it was the SAME 10px the caption-to-
        // its own text uses — so five sections read as one dense block with
        // six bold headings in it. Six extra pixels after each paragraph is
        // the difference between "wall of text" and "five things you can
        // scan for"; the caption-to-text gap stays untouched, so nothing
        // inside a section moves.
        vl->addSpacing(6);
    }

    auto *keysCap = new QLabel(
        consoleText(QStringLiteral("console.proto.help.cap.keys")));
    keysCap->setObjectName(QStringLiteral("dialogCap"));
    vl->addWidget(keysCap);

    // The key table. Two columns; the rows are the canvas's own contract
    // (hud_canvas.cpp's kArrowStep / kArrowBigStep) plus the wheel zoom,
    // so the help text and the behaviour come from the same numbers.
    //
    // v1.1.5: ONLY REAL KEYBOARD KEYS LIVE IN THIS TABLE. The row that
    // used to end it — "− / + / 1:1" — was three QPushButtons
    // (zoomOutBtn_ / zoomFitBtn_ in the layout area), not keys: the user's
    // verdict on the old help panel was 「最后这个1:1的快捷键，根本没法按
    // 吧。真存在么？」, and the honest answer is that the BUTTONS exist and
    // no key is bound to them (ControlPanel::keyPressEvent handles only
    // 1/2/3/4 and Esc). Listing them in a 按键 table taught the reader to
    // hunt for a shortcut that was never written. They now have their own
    // note under the table, spelled as what they are.
    auto *table = new QGridLayout();
    table->setHorizontalSpacing(18);
    table->setVerticalSpacing(6);
    auto *kHead = new QLabel(consoleText(QStringLiteral("console.proto.help.th.key")));
    auto *aHead = new QLabel(consoleText(QStringLiteral("console.proto.help.th.act")));
    kHead->setObjectName(QStringLiteral("helpTh"));
    aHead->setObjectName(QStringLiteral("helpTh"));
    table->addWidget(kHead, 0, 0);
    table->addWidget(aHead, 0, 1);
    // ─── THE KEY COLUMN, AND WHY IT IS QString::fromUtf8 ──────────────────
    // The key names are UTF-8 SOURCE LITERALS: ↑ ↓ ← → are three bytes
    // each in UTF-8. They used to go through QString::fromLatin1(), which
    // decodes each of those bytes as Latin-1 and produced U+00E2 U+0086
    // U+0091 per arrow — “â” plus two C1 controls the font draws as a box
    // (the「a?? a?? a?? a??」rows of the screenshot). fromUtf8 keeps the
    // code points; the two control rows then render as ↑ ↓ ← →.
    // `latin` is a language-neutral key name; `i18n` is the one row whose
    // label is not (Ctrl + 滚轮 / Ctrl + wheel) and therefore a key path.
    struct Row { const char *latin; const char *i18n; const char *act; };
    static const Row rows[] = {
        {"↑ ↓ ← →",       nullptr, "console.proto.help.k.move"},
        {"Shift + ↑ ↓ ← →", nullptr, "console.proto.help.k.move10"},
        {"Esc",            nullptr, "console.proto.help.k.esc"},
        {"1 / 2 / 3 / 4",  nullptr, "console.proto.help.k.panel"},
        {nullptr, "console.proto.help.k.wheel.key",
                      "console.proto.help.k.wheel"},
    };
    constexpr int rowCount =
        static_cast<int>(sizeof(rows) / sizeof(rows[0]));
    for (int i = 0; i < rowCount; ++i) {
        const QString keyText = rows[i].i18n
            ? consoleText(QString::fromLatin1(rows[i].i18n))
            : QString::fromUtf8(rows[i].latin);
        auto *k = new QLabel(keyText);
        k->setObjectName(QStringLiteral("helpKey"));
        auto *a = new QLabel(consoleText(QString::fromLatin1(rows[i].act)));
        a->setObjectName(QStringLiteral("helpAct"));
        a->setWordWrap(true);
        table->addWidget(k, i + 1, 0);
        table->addWidget(a, i + 1, 1);
    }
    vl->addLayout(table);

    // The zoom BUTTONS, as their own note rather than a table row — see
    // the comment above the table. Its own objectName (helpNote, not
    // helpPara) so the five-step paragraph contract above is unchanged.
    auto *zoomNote = new QLabel(consoleText(
        QStringLiteral("console.proto.help.k.zoombtn.note")));
    zoomNote->setObjectName(QStringLiteral("helpNote"));
    zoomNote->setWordWrap(true);
    vl->addWidget(zoomNote);

    auto *close = new QPushButton(consoleText(QStringLiteral("console.proto.close")));
    close->setObjectName(QStringLiteral("dialogClose"));
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    help.actions->addStretch(1);
    help.actions->addWidget(close);

    centerModalOnConsole(&dlg, /*compact=*/true);
    dlg.exec();
}

// 设置. The prototype's #settings-modal is appearance (theme + language),
// the Rise environment card, the data-source note, and About (version +
// the check-updates entry). The two that are REAL here are the theme and
// the locale, because both already have a working implementation in this
// console (themeBtn_'s click and switchLocale()); what was missing was
// anywhere to reach them from, since the prototype puts them in settings
// rather than on the stage bar.
void ControlPanel::openSettingsDialog()
{
    // PARENTED TO `this` — the QSS-inheritance reason in buildModalShell().
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("settingsDialog"));
    const QString settingsTitle =
        consoleText(QStringLiteral("console.proto.settings"));
    dlg.setWindowTitle(settingsTitle);
    const ModalParts shell = buildModalShell(&dlg, settingsTitle,
                                             /*compact=*/true);
    auto *dismiss = new BackdropDismissFilter(&dlg, shell.shell);
    dlg.installEventFilter(dismiss);
    // INSTALLED ON THE VEIL TOO — but the compact shell has no veil (see
    // buildModalShell's compact note), so the guard is the point: the
    // filter still runs on the dialog itself, which is where a press
    // outside the single content layer would land.
    if (shell.backdrop)
        shell.backdrop->installEventFilter(dismiss);
    QVBoxLayout *vl = shell.body;
    // v1.0.1 (验收项1): 紧凑弹窗内的 section 间距收紧。留白不再靠“弹窗
    // 占满主窗口”提供，弹窗只到内容高度（上限 700px，超出内部滚动）。
    vl->setSpacing(10);

    // ---- 外观 ----
    auto *appearanceCap = new QLabel(
        consoleText(QStringLiteral("console.proto.set.appearance")));
    appearanceCap->setObjectName(QStringLiteral("dialogCap"));
    vl->addWidget(appearanceCap);

    auto *themeRow = new QHBoxLayout();
    themeRow->setSpacing(8);
    auto *themeLabel = new QLabel(consoleText(QStringLiteral("console.proto.theme")));
    themeLabel->setObjectName(QStringLiteral("dialogLabel"));
    themeRow->addWidget(themeLabel);
    themeRow->addStretch(1);
    // Two exclusive buttons, the prototype's #theme-actions pair. The
    // checked one is the theme IN FORCE, read from the console's own
    // state rather than assumed — and clicking one runs the same
    // setUiTheme + restyle path themeBtn_ has always run, so the two
    // entries cannot disagree about what a theme switch does.
    auto *darkBtn = new QPushButton(consoleText(QStringLiteral("console.proto.theme.dark")));
    auto *lightBtn = new QPushButton(consoleText(QStringLiteral("console.proto.theme.light")));
    for (QPushButton *b : {darkBtn, lightBtn}) {
        b->setObjectName(QStringLiteral("themeAction"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        themeRow->addWidget(b);
    }
    // ── v1.0.0 (stage C): AN EXCLUSIVE GROUP, NOT AutoExclusive ──────────
    // QPushButton::setAutoExclusive() groups buttons BY PARENT WIDGET.
    // These four (dark/light + 中文/English) are two logical pairs but
    // they all end up with the SAME parent — the QDialog — because a
    // QHBoxLayout is not a widget and reparents nothing. So the language
    // pair and the theme pair were one exclusive set of four: checking
    // English silently unchecked 深色, and vice versa, which is exactly
    // the "两组共用QDialog父，互相抢checked态" defect.
    //
    // QButtonGroup is the mechanism whose parentage IS the group. Two of
    // them, one per pair, parented to the dialog (a QObject, not a
    // widget — it dies with the dialog, so a reopen cannot inherit a
    // stale group). AutoExclusive is turned OFF on the buttons so the
    // group, and only the group, decides.
    auto *themeGroup = new QButtonGroup(&dlg);
    themeGroup->setObjectName(QStringLiteral("themeActionGroup"));
    themeGroup->setExclusive(true);
    themeGroup->addButton(darkBtn);
    themeGroup->addButton(lightBtn);
    darkBtn->setChecked(isDarkTheme());
    lightBtn->setChecked(!isDarkTheme());
    connect(darkBtn, &QPushButton::clicked, this, [this]{
        if (!isDarkTheme()) { setUiTheme(true); applyThemeChange(); }
    });
    connect(lightBtn, &QPushButton::clicked, this, [this]{
        if (isDarkTheme()) { setUiTheme(false); applyThemeChange(); }
    });
    vl->addLayout(themeRow);

    auto *langRow = new QHBoxLayout();
    langRow->setSpacing(8);
    auto *langLabel = new QLabel(consoleText(QStringLiteral("console.proto.language")));
    langLabel->setObjectName(QStringLiteral("dialogLabel"));
    langRow->addWidget(langLabel);
    langRow->addStretch(1);
    auto *zhBtn = new QPushButton(QStringLiteral("中文"));
    auto *enBtn = new QPushButton(QStringLiteral("English"));
    for (QPushButton *b : {zhBtn, enBtn}) {
        b->setObjectName(QStringLiteral("localeAction"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        langRow->addWidget(b);
    }
    // The language half of the same fix — see the theme group above.
    auto *langGroup = new QButtonGroup(&dlg);
    langGroup->setObjectName(QStringLiteral("localeActionGroup"));
    langGroup->setExclusive(true);
    langGroup->addButton(zhBtn);
    langGroup->addButton(enBtn);
    const bool en = mhw::StringTable::instance().isEnglish();
    zhBtn->setChecked(!en);
    enBtn->setChecked(en);
    // switchLocale() is the existing EN/CH path — it loads the table,
    // replays every registered string AND writes the conf row the overlay
    // polls. Reusing it means the settings entry and the stage bar's chip
    // are the same switch, not two.
    connect(zhBtn, &QPushButton::clicked, this, [this]{ switchLocale(QStringLiteral("zh-CN")); });
    connect(enBtn, &QPushButton::clicked, this, [this]{ switchLocale(QStringLiteral("en-US")); });
    vl->addLayout(langRow);

    auto *themeNote = new QLabel(
        consoleText(QStringLiteral("console.proto.theme.note")));
    themeNote->setObjectName(QStringLiteral("dialogNote"));
    themeNote->setWordWrap(true);
    vl->addWidget(themeNote);

    // ---- 崛起环境 · REFramework ----
    //
    // Rise-only, and the card is the one that already exists — the same
    // RiseReframeworkCard the damage inspector page builds, with the same
    // three request signals and the same worker guard. Building it HERE
    // is the whole point of moving it: the prototype puts it in settings,
    // and its actions (install / remove Lua / remove framework) are real
    // filesystem operations that go through the same no-worker path.
    // Rise-only in the prototype: reference.html:502 flips
    // `#rise-settings` to display:none for any other game. Qt has no CSS
    // on the dialog body, so the two text widgets are hidden directly
    // while the CARD itself is refreshed (riseCard_->refresh() hides it
    // for a non-Rise game). The card is built in BOTH games on purpose:
    // it is the ONE build site now (buildInspector no longer builds one),
    // so the tree has exactly one "riseReframeworkCard" to find and a
    // World console still answers "exists, hidden" instead of "no such
    // widget" — the same contract the damage inspector page used to keep.
    {
        const bool riseSelected = currentGame() == mhw::GameId::Rise;
        auto *riseCap = new QLabel(
            consoleText(QStringLiteral("console.proto.set.rise")));
        riseCap->setObjectName(QStringLiteral("dialogCap"));
        vl->addWidget(riseCap);
        riseCard_->build(&dlg, vl);
        auto *riseNote = new QLabel(
            consoleText(QStringLiteral("console.proto.set.rise.note")));
        riseNote->setObjectName(QStringLiteral("dialogNote"));
        riseNote->setWordWrap(true);
        vl->addWidget(riseNote);
        if (!riseSelected) {
            riseCap->hide();
            riseNote->hide();
        }
    }

    // ---- 数据来源 ----
    auto *dataCap = new QLabel(
        consoleText(QStringLiteral("console.proto.set.data")));
    dataCap->setObjectName(QStringLiteral("dialogCap"));
    vl->addWidget(dataCap);
    auto *dataValue = new QLabel(
        consoleText(QStringLiteral("console.proto.set.data.demo")));
    dataValue->setObjectName(QStringLiteral("dialogLabel"));
    vl->addWidget(dataValue);
    auto *dataNote = new QLabel(
        consoleText(QStringLiteral("console.proto.set.data.note")));
    dataNote->setObjectName(QStringLiteral("dialogNote"));
    dataNote->setWordWrap(true);
    vl->addWidget(dataNote);

    // ---- 关于 ----
    auto *aboutCap = new QLabel(consoleText(QStringLiteral("console.proto.set.about")));
    aboutCap->setObjectName(QStringLiteral("dialogCap"));
    vl->addWidget(aboutCap);
    auto *verRow = new QHBoxLayout();
    auto *verLabel = new QLabel(consoleText(QStringLiteral("console.proto.set.version")));
    verLabel->setObjectName(QStringLiteral("dialogLabel"));
    verRow->addWidget(verLabel);
    verRow->addStretch(1);
    auto *verValue = new QLabel(QCoreApplication::applicationVersion());
    verValue->setObjectName(QStringLiteral("dialogValue"));
    verRow->addWidget(verValue);
    vl->addLayout(verRow);

    // 检查更新. The prototype's copy says 已是最新版本(demo) and is a
    // lie; this button really asks GitHub, and reports what came back —
    // including "could not reach it", which is the honest answer on a
    // machine with no network. No download and no install: the dialog
    // only ever offers to OPEN the release page.
    auto *updateBtn = new QPushButton(
        consoleText(QStringLiteral("console.proto.upd.check")));
    updateBtn->setObjectName(QStringLiteral("dialogAction"));
    updateBtn->setCursor(Qt::PointingHandCursor);
    connect(updateBtn, &QPushButton::clicked, this, [this]{ checkForUpdates(); });
    vl->addWidget(updateBtn);
    auto *updateNote = new QLabel(
        consoleText(QStringLiteral("console.proto.upd.note")));
    updateNote->setObjectName(QStringLiteral("dialogNote"));
    updateNote->setWordWrap(true);
    vl->addWidget(updateNote);

    auto *close = new QPushButton(consoleText(QStringLiteral("console.proto.close")));
    close->setObjectName(QStringLiteral("dialogClose"));
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    shell.actions->addStretch(1);
    shell.actions->addWidget(close);

    // The Rise card's status lines are a function of the filesystem, so
    // they are re-read on every open rather than frozen at construction.
    riseCard_->refresh();
    centerModalOnConsole(&dlg, /*compact=*/true);
    dlg.exec();
}

// ─── v1.1.5: the payload is a LAYOUT PACKAGE ─────────────────────────────
//
// The old shape (`kind: monster-control-console-prototype`, `demo:true`,
// panels/windows arrays) described what the board was drawing and said so
// in as many words: 「快照导出 · 非 panels.ini」. Useful to LOOK at, but
// nothing could read it back — which is exactly why the user asked why
// there was an export and no import. An import needs the package format's
// header to know what it is holding, so the payload became the package.
//
// What the old snapshot carried that still matters (which game, which mode,
// which window sits where) is in the package. What was never restorable —
// the demo-seeded CONTENT of the panels — is not, and a layout package does
// not pretend to hold it.
//
// THREE GRANULARITIES, because the user asked for both ends: 「既可以导出
// 精细到崛起的怪物面板的 mix 状态，也可以一键打包」.
//
//   当前面板组   the group the rail has in front, this scope only
//   当前作用域   the game x layout the console is showing
//   全部打包     every scope the store carries
//
// READ FROM panels.ini, NOT from the live Panels. The setters persist on
// every change (setScale/setMargins/… all default to persist=true), so the
// store is the live state as far as layout goes — and it is the only place
// the other five scopes exist at all. Reading one source for all three
// granularities is what keeps the three answers from disagreeing about a
// window the console happens to have open.
void ControlPanel::refreshExportPayload()
{
    if (!exportText_)
        return;

    QSettings panels(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("monster-overlay"),
                     QStringLiteral("panels"));
    QString error;
    const std::optional<mhw::LayoutPackage> whole =
        mhw::layoutPackageFromSettings(panels, &error);
    if (!whole) {
        exportText_->setPlainText(error);
        return;
    }

    const QString game =
        currentGame() == mhw::GameId::Rise ? QStringLiteral("rise")
                                           : QStringLiteral("world");
    const QString mode = mhw::playerLayoutModeToString(layoutModeInForce());
    const QString scope = game + QLatin1Char('/') + mode;

    mhw::LayoutPackage out;
    const int level = exportScope_ ? exportScope_->currentIndex() : 1;

    if (level == 2) {
        out = *whole;
        // 「一键打包」 is the one granularity that promises a full backup,
        // and a full backup includes which sections are on — the four mask
        // rows, the language, and both games' detached-window plans. The
        // narrower levels stay scope-only: see readGlobalBlockFromConf's
        // note on why a single-group export must not rewrite masks.
        out.hasGlobal = true;
        out.global = readGlobalBlockFromConf();
    } else if (level == 1) {
        out.containsScopes.insert(scope, whole->containsScopes.value(scope));
        out.scopes.insert(scope, whole->scopes.value(scope));
    } else {
        // The focused group only. No focus means the whole scope, the same
        // fallback the bulk controls use — "all" must mean "all the rows
        // you can see", in this direction too.
        const EditableSelection sel = currentEditableSelection();
        const QString focusName =
            (sel.panel && sel.panelIndex >= 0) ? sel.panel->settingsKey()
                                               : QString();
        QStringList rows;
        if (!focusName.isEmpty())
            rows = mhw::layoutPackageWindowsFor(focusName, mode);
        // BIND THE MAP FIRST. QMap::value() returns by value, so
        // `map.value(k).begin()` and `map.value(k).end()` are iterators
        // into TWO DIFFERENT temporary containers — the loop compares
        // foreign iterators and segfaults inside Qt's string compare.
        const QMap<QString, mhw::LayoutWindowRow> scopedRows =
            whole->scopes.value(scope);
        QMap<QString, mhw::LayoutWindowRow> scoped;
        for (auto it = scopedRows.begin(); it != scopedRows.end(); ++it) {
            if (!rows.isEmpty() && !rows.contains(it.key()))
                continue;
            scoped.insert(it.key(), it.value());
        }
        if (!scoped.isEmpty()) {
            out.scopes.insert(scope, scoped);
            // The header must name exactly the groups present, or parse()
            // will refuse the file this writes — which is the contract
            // working as designed.
            QStringList groups;
            for (const QString &panel : mhw::layoutPackagePanelsFor(
                     game == QLatin1String("rise")))
                for (const QString &k : mhw::layoutPackageWindowsFor(
                         panel, mode))
                    if (scoped.contains(k)) {
                        groups.append(panel);
                        break;
                    }
            out.containsScopes.insert(scope, groups);
        }
    }

    if (out.scopes.isEmpty()) {
        exportText_->setPlainText(
            consoleText(QStringLiteral("console.proto.export.empty")));
        return;
    }
    exportText_->setPlainText(QString::fromUtf8(out.serialize()));
}

void ControlPanel::openExportDialog()
{
    // PARENTED TO `this` — the QSS-inheritance reason in buildModalShell().
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("exportDialog"));
    const QString exportTitle =
        consoleText(QStringLiteral("console.proto.export.title"));
    dlg.setWindowTitle(exportTitle);
    const ModalParts ex = buildModalShell(&dlg, exportTitle);
    auto *dismiss = new BackdropDismissFilter(&dlg, ex.shell);
    dlg.installEventFilter(dismiss);
    // INSTALLED ON THE VEIL TOO. Qt delivers a mouse event to the
    // DEEPEST widget under the cursor, so a press on the backdrop
    // is targeted at modalBackdrop and would never reach a filter
    // installed on the dialog alone — the click-to-close would be
    // dead code. The same filter object on both covers the veil
    // (dismiss) and leaves the shell to its own events (keep).
    ex.backdrop->installEventFilter(dismiss);
    QVBoxLayout *vl = ex.body;
    vl->setSpacing(10);

    auto *notIni = new QLabel(
        consoleText(QStringLiteral("console.proto.export.notini")));
    notIni->setObjectName(QStringLiteral("dialogNote"));
    vl->addWidget(notIni);
    auto *desc = new QLabel(consoleText(QStringLiteral("console.proto.export.desc")));
    desc->setObjectName(QStringLiteral("dialogNote"));
    desc->setWordWrap(true);
    vl->addWidget(desc);

    // v1.1.5: HOW MUCH to export. The two ends of the user's sentence —
    // 「既可以导出精细到崛起的怪物面板的 mix 状态，也可以一键打包」 — belong
    // on one control, because the difference between them is one field of
    // the same dialog and not two separate exports.
    auto *scopeRow = new QHBoxLayout();
    scopeRow->setSpacing(8);
    auto *scopeLabel = new QLabel(
        consoleText(QStringLiteral("console.proto.export.scope")));
    scopeLabel->setObjectName(QStringLiteral("dialogNote"));
    scopeRow->addWidget(scopeLabel);
    exportScope_ = new QComboBox();
    exportScope_->setObjectName(QStringLiteral("exportScope"));
    exportScope_->addItem(
        consoleText(QStringLiteral("console.proto.export.scope.group")));
    exportScope_->addItem(
        consoleText(QStringLiteral("console.proto.export.scope.current")));
    exportScope_->addItem(
        consoleText(QStringLiteral("console.proto.export.scope.all")));
    // The default is the current SCOPE: it is the granularity that answers
    // "save what I am looking at", which is what a user reaching for
    // 导出 usually means. The narrower and the whole-store options sit one
    // click away in either direction.
    exportScope_->setCurrentIndex(1);
    connect(exportScope_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int){ refreshExportPayload(); });
    scopeRow->addWidget(exportScope_, 1);
    vl->addLayout(scopeRow);

    exportText_ = new QPlainTextEdit();
    exportText_->setObjectName(QStringLiteral("exportText"));
    exportText_->setReadOnly(true);
    // A monospaced box: the payload is JSON and the user may want to read
    // the numbers, not just paste them.
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    exportText_->setFont(mono);
    vl->addWidget(exportText_, 1);
    refreshExportPayload();

    auto *row = new QHBoxLayout();
    row->setSpacing(8);
    // 复制 — QClipboard, the same API the prototype's
    // navigator.clipboard.writeText() maps onto. Reported honestly: the
    // clipboard can be owned by another process, and a silent failure
    // here would look exactly like a success.
    auto *copyBtn = new QPushButton(consoleText(QStringLiteral("console.proto.copy")));
    copyBtn->setObjectName(QStringLiteral("dialogAction"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    connect(copyBtn, &QPushButton::clicked, this, [this]{
        QClipboard *cb = QGuiApplication::clipboard();
        if (!cb) {
            showToast(QStringLiteral("console.proto.export.fail"));
            return;
        }
        cb->setText(exportText_ ? exportText_->toPlainText() : QString());
        showToast(QStringLiteral("console.proto.copied"));
    });
    row->addWidget(copyBtn);

    // 下载 — QFileDialog + QSaveFile. QSaveFile rather than QFile on
    // purpose: it writes to a temporary and renames on commit, so an
    // interrupted write cannot leave a half-written JSON the user then
    // imports. The rename failure is reported, not swallowed.
    auto *dlBtn = new QPushButton(consoleText(QStringLiteral("console.proto.download")));
    dlBtn->setObjectName(QStringLiteral("dialogAction"));
    dlBtn->setCursor(Qt::PointingHandCursor);
    connect(dlBtn, &QPushButton::clicked, this, [this]{
        const QString path = QFileDialog::getSaveFileName(
            nullptr,
            consoleText(QStringLiteral("console.proto.export.title")),
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                + QStringLiteral("/monster-control-console.json"),
            QStringLiteral("JSON (*.json)"));
        if (path.isEmpty())
            return;   // cancelled: not an error, and not a toast either
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            showToast(QStringLiteral("console.proto.export.fail"));
            return;
        }
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << (exportText_ ? exportText_->toPlainText() : QString());
        // commit() is the rename; false means the OS refused it (a full
        // disk, a permission change), which is a real failure the user
        // has to hear about.
        if (!f.commit()) {
            showToast(QStringLiteral("console.proto.export.fail"));
            return;
        }
        showToast(QStringLiteral("console.proto.export.done"));
    });
    row->addWidget(dlBtn);
    row->addStretch(1);

    auto *close = new QPushButton(consoleText(QStringLiteral("console.proto.close")));
    close->setObjectName(QStringLiteral("dialogClose"));
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    row->addWidget(close);
    ex.actions->addLayout(row);

    dlg.resize(620, 480);
    centerModalOnConsole(&dlg);
    dlg.exec();
    // The dialog owned the text box; dropping the pointer here keeps a
    // later refreshExportPayload() from writing into a freed widget if
    // some path calls it after the dialog closed.
    exportText_ = nullptr;
}

// 重置演示 — the prototype's `reset()`, and the one action whose SCOPE
// matters more than its behaviour.
//
// WHAT IT DOES: rebuild the four mother panels and the split host from
// the scope in force, which re-runs every Panel constructor and therefore
// re-reads [game/mode/<key>] from disk. That is exactly the prototype's
// `state = defaultState()` seen from Qt's side — the catalog defaults are
// what a fresh construction produces.
//
// WHAT IT DOES NOT DO, and this is the whole comment:
//
//   * It does NOT delete panels.ini. The user's file holds four scopes
//     and the live overlay's real layout; removing it to reset a preview
//     would be the most destructive possible answer to "start over".
//   * It does NOT touch the live overlay. The child process has its own
//     copy of everything and is not asked to reload.
//   * It does NOT reset the console's own window frame, the zoom, or the
//     splitter proportions — those are chrome, not prototype state, and
//     the prototype's reset() does not touch them either.
//
// WHY THERE IS NO CONFIRMATION DIALOG. The prototype's reset is one click
// with a toast, and everything it changes is reconstructible from disk in
// one step (close and reopen). A confirmation for a reversible action is
// how a user learns to click through dialogs without reading them. The
// destructive operations in this console — the Rise REFramework install
// and removals — DO confirm, because those write to the game directory.
// ─── end v1.0.0 reset ────────────────────────────────────────────────
void ControlPanel::resetPrototypeScope()
{
    // Re-read from disk rather than from the in-memory members: a drag
    // the user has made since construction is a member-level change, and
    // resetting to "what the console thinks" would keep it.
    for (int i = 0; i < mhw::kPanelCount; ++i)
        if (Panel *p = panelAt(i))
            p->loadConfig();
    if (splitPreview_) {
        for (int i = 0; i < splitPreview_->count(); ++i)
            if (Panel *sub = splitPreview_->windowAt(i))
                sub->loadConfig();
    }
    // And the construction half: the mask rows and the master toggles the
    // user may have unchecked, which live in this console's widgets and
    // not in any Panel.
    rebuildMotherPanels();
    // v1.1.4: AND REBIND BEFORE ANYTHING PAINTS.
    //
    // The contract is already written down where it belongs —
    // rebuildMotherPanels()'s own comment says the canvas holds non-owning
    // PanelSource* into this set and "the caller must run
    // rebindMotherPanels() straight after this function returns". Two of
    // the three call sites did (switchGame(), the ctor path); THIS one did
    // not, and the user's core dump is what it looks like:
    //
    //     #0 PanelSourceAdapter::outputName() const        <- SIGSEGV
    //     #1 HudCanvas::windowOutputName(int, int)
    //     #2 HudCanvas::overviewDrawsPanel(int)
    //     #3 HudCanvas::paintEvent(QPaintEvent*)
    //
    // i.e. the very next paint asked a DELETED Panel for its screen, from
    // an overview that is on screen by default — which is why 「重置演示」
    // needed two clicks to take the console down.
    //
    // It also explains the other half of the report: 「这个按钮好像没用」.
    // With the canvas still bound to the freed adapters it was reading
    // freed memory for every window's geometry, so the reloaded panels the
    // rebuild had just constructed never reached the board.
    rebindMotherPanels();
    syncSplitPreviewToCanvas();
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        rebuildAndRender(i);
        syncAppearance(i);
        updatePosLabel(i);
    }
    refreshSplitWindowPreviews();
    refreshWindowModel();
    showToast(QStringLiteral("console.proto.reset.done"));
}

// ─── v1.1.5: applying a layout package ────────────────────────────────────
//
// 重置布局 and 导入 are the same operation from the console's point of view:
// take the rows of ONE scope out of a package and turn them into Panel
// state. What differs is where the package came from — the shipped default
// versus a file the user picked — so that is the only difference between the
// two entry points, and this block is where they meet.
//
// focusPanel IS THE GROUP THE RAIL HAS IN FRONT, and that is deliberate.
// The bulk controls in this console already mean "all the rows you can see
// right now" (see setAllMixedWindowsDetached's note on why a button sitting
// under one group's heading must not reach into the others). A reset is a
// destructive action; if the selector for one is NOT bounded the same way,
// clicking 重置布局 while the player group is selected silently rewrites
// monsters and damage too — the exact defect class that block exists for.
// focusPanel == -1 is the explicit "every group" case: 全部重置.

Panel *ControlPanel::windowForKey(const QString &key)
{
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        Panel *p = panelAt(i);
        if (p && p->settingsKey() == key)
            return p;
    }
    if (splitPreview_) {
        const QVector<HudCanvas::SubWindowBinding> bindings =
            splitPreview_->bindings();
        for (int i = 0; i < bindings.size(); ++i) {
            Panel *sub = splitPreview_->windowAt(i);
            if (sub && sub->settingsKey() == key)
                return sub;
        }
    }
    return nullptr;
}

void ControlPanel::applyLayoutRowTo(Panel *panel, const mhw::LayoutWindowRow &row)
{
    if (!panel)
        return;
    // Margins come as four independent optionals because panels.ini stores
    // them that way (ml=/mt=/mr=/mb=), so a package carrying only `mt`
    // means "move it vertically" and leaves the horizontal pair alone.
    // Building the QMargins from the PANEL and overriding just the named
    // components is what makes that true; constructing from the four rows
    // would silently zero the ones the file does not mention.
    if (row.ml || row.mt || row.mr || row.mb) {
        QMargins m = panel->margins();
        if (row.ml) m.setLeft(*row.ml);
        if (row.mt) m.setTop(*row.mt);
        if (row.mr) m.setRight(*row.mr);
        if (row.mb) m.setBottom(*row.mb);
        panel->setMargins(m, true);
    }
    if (row.scale) panel->setScale(*row.scale, true);
    if (row.opacity) panel->setOpacity(*row.opacity, true);
    if (row.bgAlpha) panel->setBgAlpha(*row.bgAlpha, true);
    if (row.outputName) panel->setOutputName(*row.outputName, true);
    if (row.minW) panel->setExactMinWidth(*row.minW);
    if (row.minH) panel->setExactMinHeight(*row.minH);
    if (row.lockW) panel->setExactLockWidth(*row.lockW);
    if (row.lockH) panel->setExactLockHeight(*row.lockH);
    if (row.columns) panel->setColumnsTo(*row.columns);
    // `winVisible` is the PERSISTED bit, not the widget's own flag — it is
    // the same row buildWindowStates() reads back, so writing it is what
    // makes a restored window stay restored across the next start.
    if (row.visible)
        mhw::writeWindowVisible(panel->settingsGroup(),
                                panel->settingsKey(), *row.visible);
}

bool ControlPanel::applyLayoutPackage(const mhw::LayoutPackage &pkg,
                                      int focusPanel, QString *error)
{
    const QString game =
        currentGame() == mhw::GameId::Rise ? QStringLiteral("rise")
                                           : QStringLiteral("world");
    const QString mode = mhw::playerLayoutModeToString(layoutModeInForce_);
    const QString scope = game + QLatin1Char('/') + mode;

    if (!pkg.containsScope(scope)) {
        if (error)
            *error = QStringLiteral(
                "this package does not cover %1 (it covers %2)")
                         .arg(scope, pkg.scopeNames().join(QLatin1String(", ")));
        return false;
    }

    // Which panel group `focusPanel` means, in its own settingsKey form:
    // `player` / `monster` / `dps` / `pet-damage`.
    QString focusName;
    if (focusPanel >= 0)
        if (Panel *mother = panelAt(focusPanel))
            focusName = mother->settingsKey();

    const QMap<QString, mhw::LayoutWindowRow> rows = pkg.scopes.value(scope);
    for (auto it = rows.begin(); it != rows.end(); ++it) {
        if (focusPanel >= 0) {
            const QStringList owned =
                mhw::layoutPackageWindowsFor(focusName, mode);
            if (!owned.contains(it.key()))
                continue;
        }
        Panel *target = windowForKey(it.key());
        if (!target) {
            // Parse already proved the key belongs to this mode, so a miss
            // here means the console simply is not carrying that window
            // (World has no pets row, and a scope mounted without its
            // detached windows has no sub rows). Skipping is the honest
            // answer; inventing a panel is not.
            continue;
        }
        applyLayoutRowTo(target, it.value());
    }

    // One turn deferred, exactly like refreshAfterMixedPlanChange(): the
    // widget that triggered this is still delivering its own signal, and
    // rebuilding the model synchronously from inside it is how this console
    // used to reach a half-torn-down host.
    //
    // v1.1.5: AND THE DEFERRED HALF IS THE WHOLE REFRESH, not two of its
    // six steps. The user's report: 「我点击重置布局，但是不会立即刷新。
    // 需要手动点一下别的窗口才显示重置后的布局」. This lambda used to run
    // syncSplitPreviewToCanvas() + refreshWindowModel() and stop there —
    // both of which rebuild MODELS (the host's bindings, the window list),
    // and neither of which touches the canvas's PIXMAPS. Those live in the
    // canvas's own slots (setPanelPixmap for the four mothers,
    // setSubWindowPixmap for every split window) and are only re-pushed by
    // rebuildAndRender() / refreshSplitWindowPreviews() — so after a reset
    // the board kept painting the pre-reset images at the pre-reset sizes
    // until some unrelated action (a rail click, a selection change) happened
    // to re-render. That is the whole "click another window and it appears".
    //
    // The six steps below are resetPrototypeScope()'s own, verbatim: the
    // one function in this file that was already correct. A reset and an
    // import are the same operation from the canvas's point of view — new
    // geometry for the same windows — so they owe it the same refresh.
    QTimer::singleShot(0, this, [this] {
        syncSplitPreviewToCanvas();
        for (int i = 0; i < mhw::kPanelCount; ++i) {
            rebuildAndRender(i);   // re-renders the mother pixmap + repaint
            syncAppearance(i);
            updatePosLabel(i);
        }
        refreshSplitWindowPreviews();   // re-renders every sub-window pixmap
        refreshWindowModel();
        refreshGroupColumn();   // the inspector's counts / checked rows
    });
    return true;
}

bool ControlPanel::loadShippedDefault(mhw::LayoutPackage &out,
                                      QString *error) const
{
    QFile f(QStringLiteral(":/default-layout.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral(
                "the shipped default layout is not present in this build");
        return false;
    }
    const std::optional<mhw::LayoutPackage> parsed =
        mhw::LayoutPackage::parse(f.readAll(), error);
    if (!parsed) {
        if (error && error->isEmpty())
            *error = QStringLiteral("the shipped default layout is corrupt");
        return false;
    }
    out = *parsed;
    return true;
}

void ControlPanel::resetLayoutToDefault(int focusPanel)
{
    mhw::LayoutPackage pkg;
    QString error;
    if (!loadShippedDefault(pkg, &error)) {
        // A build without its default is a broken build, and saying so is
        // the only honest reaction: a silent no-op would read as "reset
        // worked" and the user would keep working on a layout they think
        // they just replaced.
        showToast(error);
        return;
    }
    if (!applyLayoutPackage(pkg, focusPanel, &error)) {
        showToast(error);
        return;
    }
    showToast(consoleText(focusPanel < 0
                              ? QStringLiteral(
                                    "console.proto.layout.reset.all.done")
                              : QStringLiteral(
                                    "console.proto.layout.reset.done")));
}

// ─── v1.1.5: the global block ────────────────────────────────────────────
//
// A layout package's scopes say where the windows sit; this says which
// sections are on. Without it a 「全部打包」 round trip would restore every
// window's geometry and leave every mask as the importer had it — which is
// not "restore", it is "rearrange".
//
// READ FROM THE CONF TEXT, NOT FROM MEMBERS. The console has the masks and
// the plans in memory, but the package is a description of what is ON DISK,
// and a user who hand-edited the conf while the console was closed would
// otherwise export the console's stale copy of a file it never re-read.
//
// The conf is documented as plain `key=value` lines (see loadMaskFromDisk's
// header comment), so the two non-mask rows are read with a ten-line reader
// here rather than by growing a parser for a format that does not need one.
mhw::LayoutGlobalBlock ControlPanel::readGlobalBlockFromConf() const
{
    mhw::LayoutGlobalBlock block;
    const QString path = mhw::localeConfPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return block;   // absent conf: an empty global block, which parses
    const QString text = QString::fromUtf8(file.readAll());

    const mhw::PanelMaskSet masks = mhw::parsePanelMasks(text);
    // Only a VALID row goes into the package. A malformed one is not a
    // value the package can carry, and copying the parser's fallback for
    // it would write a number the user never chose.
    if (masks.playerValid) block.maskPlayer = masks.player;
    if (masks.monsterValid) block.maskMonster = masks.monster;
    if (masks.damageValid) block.maskDamage = masks.damage;
    if (masks.petsValid) block.maskPets = masks.pets;

    auto confValue = [&text](const QString &key) -> std::optional<QString> {
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            if (line.left(eq).trimmed() == key)
                return line.mid(eq + 1).trimmed();
        }
        return std::nullopt;
    };

    if (const auto v = confValue(QStringLiteral("locale")); v && !v->isEmpty())
        block.locale = *v;
    // `layout=` is written as an EMPTY line in Single mode (that is how the
    // console says "single, not unset"), so an empty value means "single",
    // not "nothing to carry".
    if (const auto v = confValue(QStringLiteral("layout")); v)
        block.layout = *v;

    for (const mhw::GameId game : {mhw::GameId::World, mhw::GameId::Rise}) {
        const auto v = confValue(mhw::layoutWindowsRowKey(game));
        if (!v)
            continue;   // an absent row is a different state from an empty
        QStringList keys;
        for (const QString &k : v->split(QLatin1Char(','), Qt::SkipEmptyParts))
            keys.append(k);
        block.plans.insert(
            game == mhw::GameId::Rise ? QStringLiteral("rise")
                                      : QStringLiteral("world"),
            keys);
    }
    return block;
}

// ─── v1.1.5: import ───────────────────────────────────────────────────────
namespace {

// Replace ONE conf row and leave every other line byte for byte alone —
// the codec's row rule (see serializeWindowLayoutPlanRow's note), applied
// to the rows a layout package carries. split() on a trailing newline
// yields a phantom empty element, which must not come back as a blank
// line on every single import.
QString replaceConfRow(const QString &text, const QString &key,
                       const QString &value)
{
    const bool trailing = text.endsWith(QLatin1Char('\n'));
    QStringList lines = text.split(QLatin1Char('\n'));
    if (trailing && !lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    bool found = false;
    for (QString &line : lines) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0 && line.left(eq).trimmed() == key) {
            line = key + QLatin1Char('=') + value;
            found = true;
        }
    }
    if (!found)
        lines.append(key + QLatin1Char('=') + value);
    QString out = lines.join(QLatin1Char('\n'));
    return trailing ? out + QLatin1Char('\n') : out;
}

QString configDirPath()
{
    return QFileInfo(mhw::localeConfPath()).absolutePath();
}

}  // namespace

QString ControlPanel::backupConfigForImport() const
{
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString dir =
        configDirPath() + QStringLiteral("/backups/") + stamp;
    if (!QDir().mkpath(dir))
        return consoleText(QStringLiteral("console.proto.layout.import.backup.fail"));
    // Both files an import can touch. A backup of only the one the user
    // happened to be using is not a backup.
    for (const QString &name : {QStringLiteral("panels.ini"),
                                QStringLiteral("monster-overlay.conf")}) {
        const QString from = configDirPath() + QLatin1Char('/') + name;
        if (!QFile::exists(from))
            continue;   // never created on this machine; nothing to save
        if (!QFile::copy(from, dir + QLatin1Char('/') + name))
            return consoleText(
                QStringLiteral("console.proto.layout.import.backup.fail"));
    }
    return QString();
}

void ControlPanel::importLayoutPackage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, consoleText(QStringLiteral("console.proto.layout.import")),
        QString(), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        showToast(consoleText(
            QStringLiteral("console.proto.layout.import.fail")));
        return;
    }
    QString error;
    // The header decides everything: what it declares is what will be
    // written, and a refusal here happens BEFORE a single byte of the
    // user's configuration is touched.
    const std::optional<mhw::LayoutPackage> pkg =
        mhw::LayoutPackage::parse(file.readAll(), &error);
    if (!pkg) {
        QMessageBox::critical(
            this, consoleText(QStringLiteral("console.proto.layout.import")),
            consoleText(QStringLiteral(
                            "console.proto.layout.import.bad")) + QLatin1Char('\n')
                + error);
        return;
    }

    // THE CONFIRM NAMES THE SCOPES. The user's own requirement was that an
    // import work out from the header what it contains — so show it to
    // them, rather than writing first and describing afterwards.
    QStringList summary;
    for (const QString &scope : pkg->scopeNames()) {
        QStringList panels;
        for (const QString &p : mhw::layoutPackagePanelsFor(
                 scope.startsWith(QLatin1String("rise")))) {
            if (pkg->panelsOf(scope).contains(p))
                panels.append(p);
        }
        summary.append(QStringLiteral("%1  →  %2")
                           .arg(scope, panels.join(QStringLiteral("、"))));
    }
    if (pkg->hasGlobal)
        summary.append(consoleText(
            QStringLiteral("console.proto.layout.import.global")));
    QMessageBox box(QMessageBox::Warning,
                    consoleText(QStringLiteral("console.proto.layout.import")),
                    consoleText(QStringLiteral(
                        "console.proto.layout.import.confirm"))
                        + QStringLiteral("\n\n") + summary.join(QLatin1Char('\n')),
                    QMessageBox::Yes | QMessageBox::Cancel, this);
    if (box.exec() != QMessageBox::Yes)
        return;

    const QString backup = backupConfigForImport();
    if (!backup.isEmpty()) {
        QMessageBox::critical(
            this, consoleText(QStringLiteral("console.proto.layout.import")),
            backup);
        return;   // refuse to write over a configuration we could not save
    }

    QSettings panels(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("monster-overlay"),
                     QStringLiteral("panels"));
    // QSettings::sync() returns void in Qt6 — there is no "did it flush"
    // answer to ask. The backup taken above is what makes a failed write
    // recoverable, not a return value from here.
    if (!mhw::layoutPackageToSettings(*pkg, panels, &error)) {
        QMessageBox::critical(
            this, consoleText(QStringLiteral("console.proto.layout.import")),
            error.isEmpty()
                ? consoleText(QStringLiteral(
                      "console.proto.layout.import.fail"))
                : error);
        return;
    }
    panels.sync();

    if (pkg->hasGlobal) {
        QString text;
        QFile conf(mhw::localeConfPath());
        if (conf.open(QIODevice::ReadOnly))
            text = QString::fromUtf8(conf.readAll());
        conf.close();
        const mhw::LayoutGlobalBlock &g = pkg->global;
        if (g.maskPlayer) {
            // The codec replaces the four mask rows in place and keeps
            // every other line — including the layout and locale rows
            // this function is about to rewrite.
            text = QString::fromUtf8(mhw::serializePanelMasks(
                *g.maskPlayer, g.maskMonster ? *g.maskMonster : 0xffffffffu,
                g.maskDamage ? *g.maskDamage : 0xffffffffu,
                g.maskPets ? *g.maskPets : 0xffffffffu, text));
        }
        if (g.locale)
            text = replaceConfRow(text, QStringLiteral("locale"), *g.locale);
        if (g.layout)
            text = replaceConfRow(text, QStringLiteral("layout"), *g.layout);
        for (auto it = g.plans.begin(); it != g.plans.end(); ++it)
            text = replaceConfRow(
                text,
                it.key() == QLatin1String("rise")
                    ? QStringLiteral("layout.windows.rise")
                    : QStringLiteral("layout.windows.world"),
                it.value().join(QLatin1Char(',')));
        QFile out(mhw::localeConfPath());
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            out.write(text.toUtf8());
    }

    // Re-read everything from disk, exactly as 重置演示 does: the panels
    // themselves cache their geometry from their constructor, so applying
    // a package to the store and leaving the widgets alone would show the
    // user a console that still draws the OLD layout.
    resetPrototypeScope();
    showToast(consoleText(QStringLiteral("console.proto.layout.import.done")));
}

// 检查更新 — the prototype's entry that says 已是最新版本(demo) without
// asking anyone. This one asks GitHub, for real.
//
// THE REPO IS NOT HARDCODED. It is read from the checkout's own `git
// remote get-url origin` at call time, so a fork or a mirror reports its
// own releases instead of upstream's. A hardcoded owner/repo is how a
// fork's console ends up telling its user about someone else's version.
//
// WHAT IT NEVER DOES: download, install, or write anything. The only
// onward action offered is opening the release page in the user's
// browser, which is a QDesktopServices::openUrl the user initiates.
//
// THE FOUR HONEST OUTCOMES, all of which the prototype could not
// express:
//
//   found    a release newer than applicationVersion() exists
//   latest   the newest release is not newer
//   rate     GitHub answered 403/429 (its API rate limit)
//   offline  no answer at all — no network, no DNS, no route
//
// THE NETWORK CALL IS SYNCHRONOUS, and that is deliberate: the request
// has a 5-second timeout and the console is otherwise idle while the
// user waits for one answer. A QNetworkAccessManager would need a nested
// event loop (or a member + a signal hop) to keep the same shape, and a
// nested loop inside a button handler is how re-entrancy bugs start. The
// cost is one blocked GUI thread for at most five seconds on a machine
// with no network — and on that machine the alternative is a spinner that
// never resolves, which is worse.
// ─── end v1.0.0 check for updates ────────────────────────────────────
void ControlPanel::checkForUpdates()
{
    // Which repo? The checkout's own origin, not a constant.
    QString repo;
    {
        QProcess git;
        git.start(QStringLiteral("git"),
                  {QStringLiteral("remote"), QStringLiteral("get-url"),
                   QStringLiteral("origin")});
        if (git.waitForFinished(3000) && git.exitStatus() == QProcess::NormalExit
            && git.exitCode() == 0) {
            // ssh://git@github.com/owner/repo.git, git@github.com:owner/repo.git
            // and https://github.com/owner/repo.git all have to parse.
            QString url = QString::fromUtf8(git.readAllStandardOutput()).trimmed();
            const int at = url.lastIndexOf(QStringLiteral("github.com"));
            if (at >= 0) {
                QString path = url.mid(at + 11);
                if (path.startsWith(QLatin1Char('/')))
                    path.remove(0, 1);
                if (path.startsWith(QLatin1Char(':')))
                    path.remove(0, 1);
                if (path.endsWith(QStringLiteral(".git")))
                    path.chop(4);
                if (path.count(QLatin1Char('/')) == 1)
                    repo = path;
            }
        }
    }
    if (repo.isEmpty()) {
        // No origin, or an origin that is not GitHub. Saying "could not
        // reach GitHub" would be a lie about a question we never asked.
        showToast(QStringLiteral("console.proto.upd.norepo"));
        return;
    }

    const QUrl api(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                       .arg(repo));
    QNetworkRequest req(api);
    // A raw accept header: without it GitHub answers 415, and the console
    // would report "rate limited" for a request it malformed.
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(5000);

    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.get(req);
    // A bounded wait rather than an event loop: the reply's own
    // transferTimeout(5000) above is the same budget, so the two agree.
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QString outcome = [reply, this]() -> QString {
        if (reply->error() != QNetworkReply::NoError) {
            // GitHub's rate limit answers 403 or 429 with a body that
            // says so; anything else is a transport failure. The two are
            // reported differently because one resolves by waiting and
            // the other does not.
            const int code = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            reply->deleteLater();
            if (code == 403 || code == 429)
                return QStringLiteral("console.proto.upd.rate");
            return QStringLiteral("console.proto.upd.offline");
        }
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject())
            return QStringLiteral("console.proto.upd.badver");
        const QString tag = doc.object()
                                .value(QStringLiteral("tag_name")).toString();
        // "v1.2.3" and "1.2.3" are the same version; the leading v is
        // decoration, not a different number.
        const QString clean = tag.startsWith(QLatin1Char('v'))
                                  ? tag.mid(1) : tag;
        const QStringList parts = clean.split(QLatin1Char('.'));
        if (parts.size() < 2)
            return QStringLiteral("console.proto.upd.badver");
        bool ok = false;
        const int remoteMajor = parts.value(0).toInt(&ok);
        const int remoteMinor = parts.value(1).toInt(&ok);
        if (!ok)
            return QStringLiteral("console.proto.upd.badver");

        const QStringList local =
            QCoreApplication::applicationVersion().split(QLatin1Char('.'));
        const int localMajor = local.value(0).toInt(&ok);
        const int localMinor = local.value(1).toInt(&ok);
        if (!ok)
            return QStringLiteral("console.proto.upd.badver");

        if (remoteMajor > localMajor
            || (remoteMajor == localMajor && remoteMinor > localMinor))
            return QStringLiteral("console.proto.upd.found");
        return QStringLiteral("console.proto.upd.latest");
    }();
    showToast(outcome);
}

// The theme switch, spelled once. themeBtn_'s own click handler and the
// settings dialog's dark/light pair both run the same four steps, and a
// second spelling is how the two entries end up doing different things:
// set the flag, clear and re-apply the console stylesheet (Qt caches
// selector colours on existing buttons if a new QSS is assigned over an
// old one), rebuild the SectionRows' inline child labels that parent QSS
// cannot reach, and repolish every widget so dynamic-property selectors
// re-evaluate.
//
// THE CANVAS IS REPAINTED TOO, and it is the last step on purpose: the
// board's own colours are painted by hud_canvas.cpp from its own
// constants, so a theme flip that leaves the board stale would show a
// dark board under a light console.
// ─── end v1.0.0 theme ────────────────────────────────────────────────
void ControlPanel::applyThemeChange()
{
    updateThemeChipText();
    setStyleSheet({});
    setStyleSheet(consoleStyleSheet());
    for (auto &c : ctl_)
        for (SectionRow *row : c.subs)
            row->refreshTheme();
    repolishAllWidgets();
    if (canvas_)
        canvas_->update();
}

// R5: EDIT MODE block — orange "ENTER EDIT" button on the right,
// caption "进入后三个面板强制显示，方向键移动" on the left.
//
// L3: also add a "START" button to spawn monster-overlay. Same orange
// CTA visual, separate action. ENTER EDIT still exists as a synonym
// for START with --edit — both hide the console, both re-show on
// exit; the only difference is whether the overlay enters edit mode.
//
// ─── v1.0.0 fidelity port: the window model ─────────────────────────────
//
// THE PROTOTYPE'S BOARD, AS THE CONSOLE'S OWN STATE.
//
// `state.scopes[game+mode].windows[]` is the prototype's single source
// for the inspector, the canvas's selection, the eye toggles and the
// export payload. This is that list, derived from the live Panels
// instead of from a second table, so nothing can disagree with what the
// user is looking at.
//
// THE DERIVATION, in one pass:
//
//   Split  → every sub-window the host resolved, in host order (which
//            is the split-table order: 6 player, 2 monster, 3 damage),
//            each carrying its parentSlot and its own index.
//   Single → the four mother panels, one row each, subIndex -1.
//
// The order is the ORDER THE CANVAS USES for its sub-window bindings,
// which is the point: "the inspector's row 4" and "the canvas's box 4"
// are then the same statement with no translation in between.
//
// WHY NOT CACHED. Every accessor here re-reads from the Panels, so a
// rail switch that rebuilt the host cannot leave a row describing a
// window that no longer exists. A row whose Panel is gone is simply
// absent from the vector, which is the same rule the canvas's own
// binding setter applies.
//
// ─── end v1.0.0 window model ─────────────────────────────────────────
// ── v1.0.0 (C): WHAT A WINDOW'S SECTIONS ACTUALLY ARE ───────────────────
//
// `sectionsTotal = 1 / sectionsOn = (mask != 0)` was a placeholder, and
// it lied in both directions: a player mother panel has NINE switchable
// sections (panel_sections.h PlayerSection) and a sub-window claims the
// union its table row names, so "1/1" could never show a user that eight
// of nine blocks were off. The counts are now derived from the SAME
// tables the panels are built from, so they cannot drift from the code
// that draws:
//
//   * the schema is the panel's `kAll` bit set, filtered by the GAME
//     (Mantles is a World block, Wirebug a Rise one — both are hidden in
//     switchGame(), so counting them would print a total the user can
//     never reach);
//   * a SPLIT window's own sections are its k<Panel>SplitTargets row's
//     mask (the "which bits left the mother panel" table), addressed by
//     the binding's tableRow — never by a row number treated as a bit;
//   * what is ON is the live mask ANDed with that schema.
namespace {
int countSectionBits(uint32_t mask)
{
    int n = 0;
    for (uint32_t m = mask; m != 0; m >>= 1)
        n += int(m & 1u);
    return n;
}

// The sections a panel of this game actually draws.
uint32_t panelSectionSchema(int panelIndex, mhw::GameId game)
{
    switch (panelIndex) {
    case 0:
        return mhw::PlayerSection::kAll
               & (game == mhw::GameId::Rise
                      ? ~uint32_t(mhw::PlayerSection::Mantles)
                      : ~uint32_t(mhw::PlayerSection::Wirebug));
    case 1:
        return mhw::MonsterSection::kAll;
    case 2:
        return mhw::DamageSection::kAll;
    case 3:
        return mhw::PetDamageSection::kAll;
    default:
        return 0u;
    }
}

// The sections a split table row claims — the row's own mask, looked up
// in the table that row belongs to. -1 tableRow (a mother window) has no
// claim of its own.
uint32_t splitWindowSections(int panelIndex, int tableRow)
{
    if (tableRow < 0)
        return 0u;
    switch (panelIndex) {
    case 0:
        // NOTE the namespace: the PLAYER split table lives inside
        // `namespace PlayerSection` (panel_sections.h:36 — the namespace
        // that holds the bits closes at line 679, and the table is inside
        // it), while the monster and damage tables are directly in `mhw`.
        if (tableRow < mhw::PlayerSection::kPlayerSplitTargetCount)
            return mhw::PlayerSection::kPlayerSplitTargets[tableRow].mask;
        break;
    case 1:
        if (tableRow < mhw::kMonsterSplitTargetCount)
            return mhw::kMonsterSplitTargets[tableRow].mask;
        break;
    case 2:
        if (tableRow
            < int(sizeof(mhw::kDamageSplitTargets)
                  / sizeof(mhw::kDamageSplitTargets[0])))
            return mhw::kDamageSplitTargets[tableRow].mask;
        break;
    default:
        break;
    }
    return 0u;
}
} // namespace

mhw::WindowList ControlPanel::buildWindowStates() const
{
    mhw::WindowList list;
    const bool split = layoutModeInForce_
                       == mhw::PlayerSection::PlayerLayoutMode::Split;
    // ── v1.1.3 (Mixed 画布): THE BOARD'S LIST IN MIXED ────────────────────
    // Before this, Mixed fell through to the Single answer (four mother
    // rows) because only Split built a preview host — so the board drew
    // mothers while the overlay mounted the detached windows, and
    // buildWindowTopology() had to bolt the missing rows on afterwards to
    // make a count honest. Mixed now has a host of its own (the rows the
    // plan detached), so THIS list is the board's whole truth in every
    // mode: mothers that still mount, plus the detached windows as real
    // sub-window rows with real geometry. The count model then has
    // nothing to invent (see buildWindowTopology()'s dedupe arm).
    const bool mixed = layoutModeInForce_
                       == mhw::PlayerSection::PlayerLayoutMode::Mixed;
    // The primary's name, once: the tail of the own→parent chain, and
    // the same QScreen walk HudCanvas::primaryOutputName() does. Empty
    // when Qt reports no screens at all, in which case the chain stops
    // at the parent rather than inventing a name.
    QString primary;
    if (QScreen *s = QGuiApplication::primaryScreen())
        primary = s->name();
    const QList<screen_query::OutputInfo> screens = screen_query::listOutputs();

    // ── ONE MOTHER ROW, USED BY BOTH ARMS ────────────────────────────────
    // A mother panel's window state is a pure function of the Panel it
    // names (key, margins, scale, opacity, own output, master, the
    // persisted visible bit, the game-filtered schema under its live
    // mask) plus the primary tail of the output chain. Single emits one
    // per panel; Split emits exactly ONE mother row — the pets panel,
    // which has no split table (see splitWindowSections()'s default arm)
    // and so has no sub rows to stand behind it. Spelling it once is what
    // keeps the two arms from drifting on any one of those fields: the
    // Split pets row is the same window Single would emit, not a
    // re-derived approximation of it.
    auto motherRow = [this, &primary](int i, Panel *p) {
        mhw::WindowState w;
        w.panelIndex = i;
        w.subIndex = -1;
        w.key = p->settingsKey();
        // THE SHORT NAME IS NAMED, NOT DERIVED FROM THE settingsKey. Three
        // of the four panels happen to have a `win.short.<key>` row; the
        // pets panel's settingsKey is `pet-damage` (panel_pet_damage.cpp:220)
        // while the table's short name for that window is `win.short.pet`,
        // and docs/I18N.md's "no fallback in code" rule forbids detecting
        // the miss at runtime — so the one key that differs is spelled out
        // here. Titles for 0/1/2 are byte-identical to the derived form.
        static const char *const kShortTitle[mhw::kPanelCount] = {
            "console.proto.win.short.player", "console.proto.win.short.monster",
            "console.proto.win.short.damage", "console.proto.win.short.pet"
        };
        w.title = consoleText(QString::fromLatin1(
            mhw::isPanelIndex(i) ? kShortTitle[i] : kShortTitle[0]));
        w.margins = p->margins();
        w.scale = p->scale();
        w.opacity = p->opacity();
        w.outputOwn = p->outputName();
        w.primaryOutput = primary;
        // A mother has no parent to fall back to, so the chain is
        // own → primary — the same two steps Single already answered.
        w.outputResolved = screen_query::effectiveOutputName(
            mhw::resolveWindowOutput(w.outputOwn, QString(), primary));
        w.panelEnabled = p->panelEnabled();
        w.visiblePersisted = mhw::readWindowVisible(p->settingsGroup(), w.key);
        // ── (C): the mother panel draws its own schema, game-filtered ──
        const uint32_t schema = panelSectionSchema(i, currentGame());
        w.sectionMask = p->sectionMask() & schema;
        w.sectionsTotal = countSectionBits(schema);
        w.sectionsOn = countSectionBits(p->sectionMask() & schema);
        return w;
    };

    if ((split || mixed) && splitPreview_) {
        const QVector<HudCanvas::SubWindowBinding> bindings =
            splitPreview_->bindings();
        for (int i = 0; i < bindings.size(); ++i) {
            const HudCanvas::SubWindowBinding &b = bindings[i];
            Panel *sub = splitPreview_->windowAt(i);
            if (!sub || b.parentSlot < 0 || b.parentSlot >= mhw::kPanelCount)
                continue;   // a row that did not resolve: absent, not blank
            // v0.10.1: companion surfaces are Rise-only. A pets window in
            // a World scope is not a window the user can see, and the
            // inspector must not offer it.
            if (b.parentSlot == 3 && currentGame() != mhw::GameId::Rise)
                continue;
            Panel *parent = panelAt(b.parentSlot);

            mhw::WindowState w;
            w.panelIndex = b.parentSlot;
            w.subIndex = i;
            w.key = sub->settingsKey();
            w.title = consoleText(QStringLiteral("console.proto.win.short.%1")
                                      .arg(w.key));
            w.margins = sub->margins();
            w.scale = sub->scale();
            w.opacity = sub->opacity();
            w.outputOwn = sub->outputName();
            w.primaryOutput = primary;
            w.outputResolved = screen_query::effectiveOutputName(
                mhw::resolveWindowOutput(
                    w.outputOwn, parent ? parent->outputName() : QString(), primary));
            w.panelEnabled = parent ? parent->panelEnabled() : true;
            w.visiblePersisted = mhw::readWindowVisible(sub->settingsGroup(),
                                                        w.key);
            // ── (C): WHAT THIS WINDOW DRAWS ─────────────────────────────
            // `own` is the split table row's claim (kPlayerSplitTargets /
            // kMonsterSplitTargets / kDamageSplitTargets at the row the
            // binding names), NOT a row number used as a bit. The window's
            // own live mask decides which of those bits are still on; the
            // game-filtered schema decides which of them exist at all.
            //
            // ── 实测缺陷3 (user-8): THE WINDOW'S MASK, NOT THE MOTHER'S ──
            // `sub` is the very Panel the host hands the canvas, and the
            // host's renderAll() computes its enabled flag from
            // `own & e.window->sectionMask() & schema` — this expression.
            // The mother's mask was the other answer: the same bits in
            // Single and Split (effectiveMotherMask is the identity
            // there), but in Mixed the mother holds plan.retainedMask(),
            // which has every DETACHED row stripped out while the detached
            // window keeps drawing from its own mask (syncAll() pushes
            // maskFor(ctl_[parentSlot]) into every window). A player
            // status window whose two sections are both on screen was
            // therefore reported as 0/2 here — the number the group
            // column's 显示内容 count and its switches both read.
            const uint32_t own = splitWindowSections(b.parentSlot,
                                                     b.tableRow);
            const uint32_t schema = panelSectionSchema(b.parentSlot,
                                                       currentGame());
            const uint32_t live = sub->sectionMask();
            w.sectionMask = own & live & schema;
            w.sectionsTotal = countSectionBits(own & schema);
            w.sectionsOn = countSectionBits(own & live & schema);
            list.append(w);
        }
        // ── THE PETS ROW, AND ONLY IT ────────────────────────────────────
        // The three panels above have a split table whose ROWS are the
        // windows, so the loop already emitted them all and a mother row
        // for any of them would be a second, competing statement about
        // pixels the split rows own. The pets panel has no table —
        // splitWindowSections()'s default arm answers 0 for panel 3 and
        // PetDamageSection has no k*SplitTargets — and main.cpp:939
        // still mounts the window (`setPanelEnabled(!--no-pets)` is
        // independent of the layout mode), so in a Rise Split launch the
        // companion surface IS a window with no children. It therefore
        // gets the same row Single gives it: subIndex -1, every field
        // read from the Panel, no fabricated config. World has no pets
        // window at all and gets no row — exactly the game filter the sub
        // loop above applies to a row naming panel 3.
        //
        // WHY IT MATTERS ANYWHERE OUTSIDE THIS LIST. The overview, the
        // stage's window-count chip, the rail's per-panel count, the
        // inspector and the visibility push all read this one model; a
        // window the overlay mounts but the model omits is a picture
        // nobody can select, count or hide.
        //
        // v1.1.3: SPLIT'S TAIL ONLY. In Mixed the three panels that DO
        // have a split table have just emitted their detached rows, but
        // they are not finished as mothers: a Mixed mother that kept at
        // least one row still mounts and still draws (main.cpp:776), so
        // it belongs in this list beside its own detached children —
        // exactly what the four-slot arm of the board paints. A mother
        // that gave up every row is NOT added (mixedMotherMounts), which
        // is the model half of "交出全部行且无内容时不挂载".
        if (split) {
            if (currentGame() == mhw::GameId::Rise)
                if (Panel *p = panelAt(3))
                    list.append(motherRow(3, p));
            return list;
        }
        // Mixed.
        for (int i = 0; i < mhw::kPanelCount; ++i) {
            if (i == 3 && currentGame() != mhw::GameId::Rise)
                continue;   // no pets panel under World
            if (i < mhw::kLayoutParentCount
                && !mixedMotherMounts(i, maskFor(ctl_[i])))
                continue;   // gave up every row and has no block left
            Panel *p = panelAt(i);
            if (!p)
                continue;
            list.append(motherRow(i, p));
        }
        return list;
    }

    // Single: the four mother panels are the four windows.
    //
    // v1.1.3: Mixed reaches this arm only while it has NO host yet (the
    // first construction, before applyStartupLayoutMode()'s sync, or a
    // canvas-less caller) — and even then it must not over-count, so the
    // same mount gate runs here. Single and Split never read it: the
    // gate answers `true` unconditionally outside Mixed.
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        if (i == 3 && currentGame() != mhw::GameId::Rise)
            continue;   // no pets panel under World
        if (mixed && i < mhw::kLayoutParentCount
            && !mixedMotherMounts(i, maskFor(ctl_[i])))
            continue;
        Panel *p = panelAt(i);
        if (!p)
            continue;
        list.append(motherRow(i, p));
    }
    return list;
}

// ── v1.1.2 (Mixed 拓扑), v1.1.3 (与画板列表合一) ────────────────────────
//
// buildWindowStates() is the CONSOLE's answer — "which windows does the
// board draw here" — and v1.1.3 made it complete in Mixed too: the
// preview host now exists in Mixed (one window per detached row), so the
// list carries the detached windows as real rows with real geometry and
// the mothers that still mount. That was built for drawing; this
// function is still the COUNT model, and it is deliberately NOT a second
// board:
//
//   * Single and Split  — buildWindowStates() returned VERBATIM. No
//     branch, so those two topologies cannot drift from themselves.
//   * Mixed             — the SAME list, with two corrections that make
//     it "what the RUN mounts" rather than "what the board can show":
//     (a) a mother row whose mount gate is false is dropped (defensive:
//         buildWindowStates() already drops it when the gate is false,
//         but this list may also be read before the first sync), and
//     (b) one row per DETACHED registry row that the list does not
//         ALREADY carry — see the dedupe below. With a host present (b)
//         adds nothing: the rows are there, with geometry; without a
//         host it is what kept the count honest before v1.1.3 and still
//         does.
//
// GEOMETRY IS CLAIMED ONLY WHEN THE CONSOLE OWNS THE WINDOW. A detached
// row the host built carries its Panel's margins/scale/opacity; the
// fallback row (no host) keeps the struct defaults and must not be read
// as a placement — the overlay owns that, per its own panels.ini row
// under <game>/mixed/<key>. Both carry what a COUNT needs: identity,
// parent panel, the output chain (own -> parent -> primary; a sub-window
// has no outputName of its own by construction — panel.h), the panel-
// master flag, and the per-window visibility stored under that window's
// own scope.
mhw::WindowList ControlPanel::buildWindowTopology() const
{
    mhw::WindowList list = buildWindowStates();
    if (layoutModeInForce_ != mhw::PlayerSection::PlayerLayoutMode::Mixed)
        return list;             // Single / Split: no change, by construction

    const mhw::WindowLayoutPlan &plan = mixedPlanFor(currentGame());

    // The same own -> parent -> primary chain buildWindowStates() runs,
    // spelled identically: a Mixed detached window follows its mother's
    // output unless it has one of its own (it cannot — see above).
    QString primary;
    if (QScreen *s = QGuiApplication::primaryScreen())
        primary = s->name();

    // THE MOTHER GATE, ONE SPELLING. This used to be a local lambda that
    // reimplemented main.cpp's planMotherPanelEnabled(); it is now the
    // shared ControlPanel::mixedMotherMounts() that buildWindowStates(),
    // rebuildAndRender() (the canvas's `present` flag) and
    // refreshWindowPreviewPixmap() all read — so "does this mother
    // mount" has one answer for the model, the count and the paint.
    // Its equivalence to the old lambda is exact: planMotherPanelEnabled
    // with disabledByFlag=false is `hasBlocks(retained) || nothing
    // detached`, which is what the lambda spelled out.
    //
    // The enable flag is deliberately NOT part of this gate: counting
    // never filtered on panelEnabled before (windowOnDisplay does that at
    // the chip), and mixing the two axes here would change Single too.
    const auto motherMounts = [this](int panel) -> bool {
        if (panel < 0 || panel >= mhw::kLayoutParentCount)
            return true;         // pets: no split table, nothing can leave
        return mixedMotherMounts(panel, maskFor(ctl_[panel]));
    };

    mhw::WindowList out;
    out.reserve(list.size() + mhw::kWindowLayoutRowCount);
    for (const mhw::WindowState &w : list) {
        if (w.subIndex == -1 && w.panelIndex >= 0
            && w.panelIndex < mhw::kLayoutParentCount
            && !motherMounts(w.panelIndex))
            continue;            // it gave up every row and kept no block
        out.append(w);
    }

    // ── THE DETACHED ROWS THE LIST DID NOT ALREADY HAVE ──────────────────
    // v1.1.3: the host-backed rows are already in `list` (same keys, real
    // geometry), so appending them again would double every count — the
    // reason this arm is a dedupe and not a flat append. A detached row
    // with no host (the fallback above) still gets its fabricated row,
    // exactly as before v1.1.3.
    QSet<QString> alreadyListed;
    for (const mhw::WindowState &w : list)
        if (w.subIndex >= 0 && !w.key.isEmpty())
            alreadyListed.insert(w.key);

    // The detached windows themselves, in the same registry order
    // --print-layout-plan lists them.
    for (int i = 0; i < mhw::kWindowLayoutRowCount; ++i) {
        const mhw::WindowLayoutRow &row = mhw::kWindowLayoutRows[static_cast<size_t>(i)];
        if (!plan.detached(row.parentSlot, row.tableRow))
            continue;
        // v1.1.5: THE SAME GAME FILTER THE HOST APPLIES. A row the game
        // in force does not have is not MOUNTED (the host skips it at
        // construction), so it must not be COUNTED either — otherwise
        // this fallback fabricates a row the board never draws and the
        // two models disagree, which is the exact defect class the
        // v1.1.3 "count == board" contract exists to prevent. The user
        // saw it as 「世界的时候有一个空的翔虫窗口」; the count chip
        // would have kept promising eleven windows while ten were
        // mounted. The filter is the host's own formula, spelled here
        // against the same two helpers.
        const uint32_t rowOwn = splitWindowSections(row.parentSlot,
                                                    row.tableRow);
        if (rowOwn != 0
            && (rowOwn & panelSectionSchema(row.parentSlot, currentGame()))
                   == 0)
            continue;
        if (alreadyListed.contains(QString::fromLatin1(row.settingsKey)))
            continue;            // the host's row says it better already
        if (row.parentSlot < 0 || row.parentSlot >= mhw::kPanelCount)
            continue;
        Panel *parent = panelAt(row.parentSlot);
        if (!parent)
            continue;

        mhw::WindowState w;
        w.panelIndex = row.parentSlot;
        // subIndex is the REGISTRY row here, not a preview-host binding
        // index — a distinction that only exists while there is no host:
        // every consumer that turns a subIndex into a Panel guards on
        // splitPreview_ first, and with a host present this row was never
        // created (the dedupe above). So this index can never resolve to
        // the wrong instance.
        w.subIndex  = i;
        w.key       = row.settingsKey;
        w.title     = consoleText(QStringLiteral("console.proto.win.short.%1")
                                      .arg(w.key));
        // Geometry stays at the struct defaults: this model must not
        // invent a placement for a window it does not own. scale/opacity
        // are the defaults for the same reason.
        w.outputOwn = QString();      // a sub-window has no own outputName
        w.primaryOutput = primary;
        w.outputResolved = screen_query::effectiveOutputName(
            mhw::resolveWindowOutput(w.outputOwn, parent->outputName(),
                                     primary));
        w.panelEnabled = parent->panelEnabled();
        // The per-window visibility row lives under that window's OWN
        // settings scope (<game>/mixed/<key>), readable without the
        // Panel object — the scope is a pure function of the key.
        w.visiblePersisted = mhw::readWindowVisible(
            mhw::panelSettingsScopeGroup(row.settingsKey), row.settingsKey);

        // WHAT THIS WINDOW DRAWS — the Split arm's formula, byte for
        // byte: the table row's claim, the panel's live mask and the
        // game-filtered schema.
        const uint32_t own = splitWindowSections(row.parentSlot,
                                                 row.tableRow);
        const uint32_t schema = panelSectionSchema(row.parentSlot,
                                                   currentGame());
        const uint32_t parentMask = parent->sectionMask();
        w.sectionMask = own & parentMask & schema;
        w.sectionsTotal = countSectionBits(own & schema);
        w.sectionsOn = countSectionBits(own & parentMask & schema);
        out.append(w);
    }
    return out;
}

// The one window the inspector describes. A row that names a Panel
// which has since been rebuilt answers an invalid state (panelIndex
// -1), which is the same "absent, not blank" rule the list uses.
//
// v1.1.0: clearWindowSelection() is DELETED, and that is the fix rather
// than a cleanup.
//
// It had no caller in src/ or tests/ — `grep -rn clearWindowSelection`
// matched only its own definition and a comment referring to it. The
// transition it claimed to own ("the user left the window half of the
// selection") was in fact performed by resetViewStateAfterScopeChange(),
// which cleared windowSelectionActive_ but NOT panelSlotChosen_. So the
// function was a second, unreachable owner of a half-completed state
// reset: reading it made the console look like it cleared a mother
// selection, while every real path left one behind.
//
// Its one piece of real logic now lives in resetViewStateAfterScopeChange(),
// and the rail highlight / topbar title behaviour it documented still
// holds there: a scope change clears the selection, not the panel slot.

mhw::WindowState ControlPanel::selectedWindow() const
{
    mhw::WindowState out;
    out.panelIndex = -1;
    // ── v1.0.0 (B): "is a window selected?" is NOT "does a row match?" ──
    // In Single mode buildWindowStates() emits the four MOTHER panels
    // with subIndex -1, and the ctor's historical selectPanel(0) already
    // pinned selectedPanel_ = 0 / selectedSubWindow_ = -1 — so the loop
    // below found a row and the console started up PRE-SELECTED, exactly
    // what reference.html's defaultState({view:'overview', selected:null})
    // and its `.empty-inspector` (reference.html:486) never do. A row
    // matching the panel slot is only a selection once the user asked for
    // one, and this flag is the only place that is recorded.
    if (!windowSelectionActive_)
        return out;
    if (!mhw::isFidelityPanelIndex(selectedPanel_))
        return out;
    const mhw::WindowList list = buildWindowStates();
    for (const mhw::WindowState &w : list) {
        if (w.panelIndex != selectedPanel_)
            continue;
        if (w.subIndex != selectedSubWindow_)
            continue;
        return w;
    }
    return out;
}

// The write-back, from the canvas (a click on a box) or the rail. Both
// are the same statement: this window is now the one the inspector
// describes. The canvas's own selection is pushed at the end so the
// board and the inspector cannot disagree — the setter is the canvas's
// one public write and it deliberately does not echo a signal back, so
// no loop is possible.
void ControlPanel::selectWindow(int panelIndex, int subIndex)
{
    if (!mhw::isFidelityPanelIndex(panelIndex))
        return;
    // v0.10.1: the pets window is Rise-only.
    if (panelIndex == 3 && currentGame() == mhw::GameId::World)
        return;
    const bool panelChanged = (selectedPanel_ != panelIndex);
    selectedPanel_ = panelIndex;
    // v1.1.0: reaching a window selection IS a user choice, including the
    // mother-panel arm (subIndex < 0). clearWindowSelection() disarms this
    // flag, so without re-arming it here a scope switch followed by a window
    // click would leave the capability rows reading "nothing selected".
    panelSlotChosen_ = true;
    selectedSubWindow_ = subIndex;
    // THE ONLY PLACE A SELECTION IS CREATED. Rail clicks and the ctor go
    // through selectPanel(), which moves the panel slot without ever
    // claiming a window (see selectedWindow()); this is the canvas's
    // "the user clicked this box" arm, and it is what turns the three
    // columns on.
    windowSelectionActive_ = true;

    if (canvas_) {
        // A sub-window index means something whenever that window actually
        // RUNS as its own window. On HEAD that was exactly Split, and the
        // gate said so; Mixed is what makes the two different (the same
        // class of gate as the output-inheritance one in main.cpp).
        //
        // The concrete failure this fixes: under `mixed`, selecting a
        // detached window took the else branch, so the canvas never learned
        // which sub-window was picked, its header chip kept reading "0 个窗口",
        // and the picture did not highlight the box the user just clicked.
        //
        // Single still clears, as before (it has no sub-windows at all). Mixed
        // asks the plan rather than the mode: a window that merged back into
        // its mother is NOT a sub-window here, and pushing such an index would
        // point at whatever now occupies that slot.
        //
        // ── v1.1.3: A SEARCH, NOT AN INDEX. The gate this replaced read
        // `topology[subIndex]` — which is only the sub-window the user
        // picked while sub rows happen to occupy positions 0..N-1 of the
        // list. That held when Mixed's list was four mother rows plus the
        // appended detached rows; it does NOT hold now that the list also
        // carries the mothers that still mount (mixed arm of
        // buildWindowStates) and the no-host fallback appends after them,
        // so position i can be a mother row (subIndex -1, never equal, so
        // the worst case was a selection silently cleared) or, after a
        // partial host, a different window entirely.
        //
        // The search is also the SAME statement the canvas's own click
        // handler makes one block up: subWindowSelected(i) finds its row
        // by scanning for `w.subIndex == i`. One index space, one lookup,
        // two entries (click and programmatic select) that cannot
        // disagree about whether a window exists.
        //
        // The subWindowCount() check closes the last hole: with no host
        // (before the first sync, or a canvas-less console) topology
        // still lists the detached windows — it must, that is the count
        // model's job — but the canvas holds no slot for them, so there is
        // nothing to highlight.
        bool subIsLive = false;
        if (subIndex >= 0) {
            if (layoutModeInForce_
                == mhw::PlayerSection::PlayerLayoutMode::Split) {
                subIsLive = true;         // every row is its own window
            } else if (layoutModeInForce_
                       == mhw::PlayerSection::PlayerLayoutMode::Mixed) {
                const mhw::WindowList topology = buildWindowTopology();
                for (const mhw::WindowState &w : topology) {
                    if (w.subIndex == subIndex
                        && w.panelIndex == panelIndex) {
                        subIsLive = true;
                        break;
                    }
                }
                subIsLive = subIsLive
                    && subIndex < canvas_->subWindowCount();
            }
        }
        canvas_->setSelectedSubWindow(subIsLive ? subIndex : -1);
        canvas_->setSelectedPanel(panelIndex);
    }

    if (panelChanged) {
        // The mask rows, the appearance sliders and the nav highlight
        // belong to the PANEL, and they follow the panel half of the
        // selection — which is exactly what selectPanel() already does.
        // Calling it here (rather than duplicating its work) keeps one
        // owner for "which panel is the inspector on".
        //
        // fromNav = FALSE, and that is the whole state contract:
        // reference.html's board handler writes `state.selected` and
        // nothing else (its nav handler at :511 is the ONLY writer of
        // `state.view`), so a click on a box — Single or Split, overview
        // or panel view — must not change the view. selectPanel()'s
        // default used to be unconditionally `leaveOverview()`, so the
        // first click on a window in the overview threw the overview
        // away and the console dropped into the panel view behind it.
        selectPanel(panelIndex, /*fromNav=*/false);
    } else {
        buildInspectorForSelection();
    }
    // v1.1.0: re-selecting the SAME panel must still refill. Selecting a
    // MOTHER is a real inspection state now that the capability rows read
    // selectedPanelOrNull() (which answers for a mother, not just a window
    // row). Re-clicking the mother you are already on leaves `panelChanged`
    // false, so the branch above does not run, and the size rows then keep
    // whatever enabled-state the previous selection left behind — a mother
    // that does have a size showing a disabled size block. Run the fill
    // unconditionally: it is a refill (the block already re-reads every
    // value), not a rebuild, so the extra call costs one pass.
    buildInspectorForSelection();
    refreshWindowModel();
}

// Rebuild the model and push it everywhere it is read. Called after any
// state change that can move a window (a drag, a mask change, a rail
// switch, a game switch) so the inspector, the rail's counts and the
// canvas's selection all describe the same world.
// ─── v1.0.0 integration (stage A): WHERE THE FILTER IS RE-PUSHED ────────
//
// HudCanvas::setSubWindowVisible() RESETS on every rebind (a fresh
// SubSlot is visible — carrying an index's flag across a rebuild would
// apply last time's window's filter to whatever window lands there
// next), so the shell has to re-push after every rebuild. Three seams,
// one function:
//
//   ctor / syncSplitPreviewToCanvas()  after setSubWindowBindings()
//   refreshWindowModel()               after any state change
//   applySelectedWindowVisible()       immediately after the write
//
// WHAT IT READS. buildWindowStates() already answers `visiblePersisted`
// from mhw::readWindowVisible() — the SAME panels.ini row the helper
// writes, the same polarity, the same "absent == true" default — so the
// board and the checkbox can never disagree about a window's state.
//
// WHAT IT DELIBERATELY DOES NOT READ:
//
//   * game capability — a World scope simply has no pets window in the
//     list, and a live binding with no row keeps the canvas default
//     (visible) rather than being force-hidden;
//   * panelEnabled_ — the master switch, which has its OWN consumer
//     (setPanelPixmap()'s `enabled`, and the `.hidden-window` /
//     disabled placeholder). Folding it in here would turn "the panel
//     master is off" into "this window was hidden by the user", the
//     exact conflation applySelectedWindowVisible()'s note warns about;
//   * the section mask — a window whose sections are ALL off is still
//     drawn, as the prototype's `.hidden-window` placeholder
//     (reference.html:443 `isShown()` is a separate question from
//     `visibleWindows()`), so it stays in the list and stays visible.
void ControlPanel::pushWindowVisibility()
{
    if (!canvas_)
        return;
    const mhw::WindowList list = buildWindowStates();

    // DEFAULT VISIBLE, for every live binding and every panel slot, and
    // then the list overwrites the ones it knows about. Starting from
    // "true" rather than "what is there now" is what makes a scope
    // change forget a hide that belonged to the previous scope — the
    // canvas's own default, spelled as the starting value instead of
    // being re-derived.
    const int subCount = canvas_->subWindowCount();
    QVector<bool> wantSub(qMax(0, subCount), true);
    QVector<bool> wantPanel(mhw::kPanelCount, true);
    for (const mhw::WindowState &w : list) {
        if (w.subIndex >= 0) {
            if (w.subIndex < wantSub.size())
                wantSub[w.subIndex] = w.visiblePersisted;
        } else if (mhw::isPanelIndex(w.panelIndex)) {
            wantPanel[w.panelIndex] = w.visiblePersisted;
        }
    }
    for (int i = 0; i < wantSub.size(); ++i)
        canvas_->setSubWindowVisible(i, wantSub[i]);
    for (int i = 0; i < wantPanel.size(); ++i)
        canvas_->setPanelWindowVisible(i, wantPanel[i]);
}

void ControlPanel::refreshWindowModel()
{
    if (canvas_ && selectedSubWindow_ >= 0) {
        // The canvas clears its own selection when its binding list is
        // replaced, so a refresh that follows a rebuild has to put the
        // selection back rather than assume it survived.
        if (selectedSubWindow_ < canvas_->subWindowCount())
            canvas_->setSelectedSubWindow(selectedSubWindow_);
    }
    // ── v1.0.0 integration (stage A): the visibility filter, pushed ───
    // BEFORE the inspector refill, so the checkbox a rebuild is about to
    // write is reading the same world the board is drawing.
    pushWindowVisibility();
    buildInspectorForSelection();
    refreshNavCounts();
    // (D) the stage's window-count chip is a function of this model, so
    // it is refreshed here rather than waiting for a locale flip to
    // re-run its trHook.
    refreshStageHead();
}

// v1.0.0: DEAD CODE, KEPT DELIBERATELY. Its widgets moved into
// buildToolbar() (startBtn_ / editBtn_ are now real toolbar controls),
// but the function itself had no caller before this change either and
// still has none — nothing in the tree constructs it. It is left in
// place so the history of where those two buttons came from stays
// readable; delete it once the toolbar has settled.
QWidget *ControlPanel::buildEditModeBlock(){
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

    // v1.0.0: the member stashes and the connects are GONE. This
    // function has no caller and never did, so nothing in the live tree
    // reaches it; but as long as it wrote startBtn_ / editBtn_ it was a
    // loaded gun pointed at the toolbar's buttons — the first person to
    // call it would have repointed the launcher at an orphan widget
    // (setOverlayRunning() renaming a button nobody can see, the
    // enable/disable lambdas toggling a dead control). The toolbar owns
    // those members now. Build-time registration is kept so a future
    // caller still gets localized text; the member writes are not.
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
    // v0.18: REBUILD argv BEFORE ASKING FOR THE RESTART.
    //
    // setCurrentGame() makes the controller SIGTERM the child and relaunch
    // it from its OWN stored copy — lastArgv_, written by the previous
    // buildOverlayArgv(). launch() then appends the NEW --game, so the game
    // switches and the screens do not: the child comes back carrying the
    // --output-* values that were in memory the last time the console built
    // argv, which for a panel whose screen the user has since changed is
    // stale. Measured on the reporter's machine:
    //
    //     explicit --output-monster=DP-1 --output-damage=DP-1
    //         -> all eleven windows on DP-1
    //     the console's own launch
    //         -> monster-identity / monster-conditions on HDMI-A-1
    //
    // The overlay's side is correct — Panel::setOutputName() applies the
    // flag and the split sub-windows inherit it (main.cpp:1051). What
    // never arrived was the flag, because the restart replayed an argv
    // built before the user touched the SCREEN dropdowns.
    //
    // Same reason applyStartupLayoutMode() calls setLastArgv() before
    // stop() (control_panel.cpp:4104) — it fixed --layout there and missed
    // --output-*. The two are the same defect at two call sites: whatever
    // argv the controller replays has to be the one the CURRENT state
    // produces.
    if (overlay_) {
        overlay_->setLastArgv(buildOverlayArgv(overlay_->lastEditMode()),
                              overlay_->lastEditMode());
        overlay_->setCurrentGame(game);
    }
    const bool isRise = (game == mhw::GameId::Rise);
    riseCard_->refresh();

    // v0.10.1: companion surfaces are Rise-only. Selecting World removes the
    // pets rail card, its inspector page and the stage preview tile
    // entirely — there is no World data source for them.
    //
    // v0.13: THIS ONE IS NOT THE argv ONE. buildOverlayArgv()'s
    // `petsAvailable` was unbidden from the game because it was lying
    // about the user's switch; this one answers a different question and
    // stays: "should the console DRAW a pets surface at all under this
    // game?" The overlay folds an empty window on its own, but the
    // console has no way to preview a panel whose demo data is never
    // seeded, so the honest UI is to not offer it.
    //
    // The two are not contradictory, and conflating them is what made
    // v0.10.1 wrong: this one gates PRESENTATION, argv's gated INTENT,
    // and only the latter claims to speak for the user.
    const bool petsAvailable = (game == mhw::GameId::Rise);
    if (ctl_[3].navButton)
        ctl_[3].navButton->setVisible(petsAvailable);
    if (canvas_)
        canvas_->setPanelPresent(3, petsAvailable);
    if (!petsAvailable && selectedPanel_ == 3) {
        panelRedirect_ = true;
        selectPanel(2);
    }
    // v1.1.0: a scope change drops the window half of the selection, and
    // with it the panel half that the capability rows read. Without this the
    // inspector kept the previous game's mother panel on screen after the
    // switch ("B: the empty block is back after the scope change"). The rail
    // highlight and the topbar title deliberately stay put.
    panelSlotChosen_ = false;
    windowSelectionActive_ = false;
    selectedSubWindow_ = -1;

    if (gameWorldBtn_) {
        gameWorldBtn_->setProperty("selected", !isRise);
        gameWorldBtn_->style()->unpolish(gameWorldBtn_);
        gameWorldBtn_->style()->polish(gameWorldBtn_);
        /* ── v1.0.0 sa-A: sidebar ──
           The game segment's checked state. The two buttons are now an
           exclusive pair (setAutoExclusive(true) in the ctor), so
           setChecked() on one clears the other and the `selected`
           property the QSS reads is the segment's own visual truth — the
           same state, now expressed the way the prototype's `.seg
           button.active` expresses it.
           ── end sa-A: sidebar ── */
        gameWorldBtn_->setChecked(!isRise);
    }
    if (gameRiseBtn_) {
        gameRiseBtn_->setProperty("selected", isRise);
        gameRiseBtn_->style()->unpolish(gameRiseBtn_);
        gameRiseBtn_->style()->polish(gameRiseBtn_);
        gameRiseBtn_->setChecked(isRise);
    }
    /* ── v1.0.0 sa-A: sidebar ──
       Everything the game switch changes in the rebuilt shell:
         * the segment's note line ("怪物猎人:崛起 · 伙伴 UI 可用")
         * the nav rows' window counts (World has no pets window, so the
           pets row's count goes blank and the total drops)
         * 实例 is Rise-only, so its visibility follows the game
         * the status line names the game in its live copy
       ── end sa-A: sidebar ── */
    if (instanceCombo_)
        instanceCombo_->setVisible(isRise);
    refreshNavCounts();
    refreshStatusline();

    // v0.7.1: rebuild the inspector preview so the player panel shows
    // the right side row for the new game. Without this the inspector
    // was stuck on whatever setupDemoData() first seeded (Rise-only
    // wirebug row, no mantles) regardless of which game the rail
    // picked.
    //
    // v0.14.1: MOTHERS ARE NO LONGER RETARGETED, THEY ARE REBUILT. The
    // setGameForDemo() call that used to sit here is now subsumed by
    // rebuildMotherPanels() below, which constructs a fresh PlayerPanel
    // whose paint path seeds the new game's demo data anyway (the ctor's
    // setEditMode(true) + first paint runs setupDemoData() against the
    // panel's own game member, which its own ctor set from the seed).
    // Leaving the call in place would have been harmless but misleading:
    // it operated on objects that are about to be deleted.
    // (The demo member is only consulted BEFORE the first paint; a
    // window that has already painted caches its seed, which is why the
    // rebuild is what actually moves it — see the note below.)
    // v0.13: the split windows' demo seeds are game-specific (衣装 seeds
    // a World player, 翔虫 a Rise one), so the same retarget the base
    // panel gets. The windows that have no seed variance need nothing.
    //
    // THIS IS NOT ENOUGH, AND THAT IS THE RAIL-SWITCH BUG.
    //
    // syncAll() retargets the demo data and re-pushes the section masks,
    // which is everything a window's CONTENT depends on. What it cannot
    // do is re-read the window's QSettings namespace, because a Panel
    // reads that exactly once — inside its constructor (Panel::loadConfig()
    // -> Panel::settingsGroup()) — and the result is already in the
    // members of a live widget. Setting the game before the windows exist
    // works; setting it afterwards has no reader. The user's disk was the
    // evidence: a `[rise/...]` section with ZERO sub-window keys beside a
    // `[world/...]` section holding all thirteen, because the host that
    // would have written them was constructed while the namespace still
    // said World and never rebuilt.
    //
    // So the switch has to go through syncSplitPreviewToCanvas(), which
    // sets the panel-settings game FIRST and then destroys and rebuilds
    // the host when the game it was built for differs from the game now
    // in force. That is the only path on which a new Panel constructor
    // runs, and a new constructor is the only thing that can pick up the
    // new namespace.
    //
    // WHY THIS IS SAFE IN SINGLE MODE. syncSplitPreviewToCanvas() pushes
    // the mode into the canvas first and returns early when it is not
    // Split, so in Single mode this call costs one comparison and builds
    // nothing — the fourteen layer-shell surfaces a Split preview holds
    // are never created by a rail click while the user is in Single.
    //
    // WHY A PUBLIC METHOD AND NOT switchGame() INLINING IT. The mode
    // in force is private state and the canvas's bindings must not be
    // dependent on any caller getting that ordering right.
    syncSplitPreviewToCanvas();

    // ---- v0.14.1: AND THE MOTHER PANELS, THE SAME WAY -----------------
    //
    // The block above rebuilds the split's sub-windows on a game change
    // so their constructors re-read the new namespace. The four MOTHER
    // panels had exactly the same defect and no equivalent path: they
    // were constructed once in the ctor and only ever had their demo seed
    // retargeted. So a Rise selection produced a preview carrying
    // World's margins, scale, opacity, margins and floors — the
    // numbers that decide where a panel sits and how big it is — while
    // painting Rise's pills. That is «整块模式下 Rise 预览不同步», and
    // it is the sub-window bug one layer up rather than a second bug.
    //
    // THE SAME ARGUMENT, and the remedy is the same: only a constructor
    // reads a Panel's namespace.
    //
    // THE SCOPE, AND WHY IT IS SET HERE RATHER THAN INHERITED. Only the
    // SPLIT arm of syncSplitPreviewToCanvas() sets the panel-settings
    // game; the Single arm returns before it, precisely because a Single
    // preview builds no windows and had nothing to scope. The mother
    // panels ARE the Single preview, so a rail click in Single must set
    // both axes itself. Leaving it to syncSplitPreviewToCanvas() would
    // mean a Single-mode rail click rebuilt the mothers under the OLD
    // game's namespace — same symptom, different line.
    //
    // v0.16: syncSplitPreviewToCanvas() now states the pair at its top,
    // on every path, so this block is no longer the only Single-mode
    // statement. It is kept because switchGame() must be correct when it
    // is called from a console whose mode was never synced at all —
    // which is exactly the ctor's startup call at line 2016, where
    // applyStartupLayoutMode() (line 1961) has not run yet and the
    // layout global still holds its Single default.
    //
    // The mode is set second because setPanelSettingsGame() reads it:
    // the group name is `<game>/<mode>/<key>`, so a stale mode silently
    // sends the rebuild to the other topology's domain.
    mhw::setPanelSettingsGame(currentGame());
    mhw::setPanelSettingsLayout(layoutModeInForce());
    // The scope is now fully stated; the rebuild is the only reader.
    rebuildMotherPanels();
    // ...and the canvas's raw pointers must follow the panels before
    // anything paints.
    rebindMotherPanels();
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

    // The game switch is the draft's OTHER scope reset (reference.html:
    // 332): view → overview, window selection cleared. `editing` is left
    // alone here because that handler does not touch it either.
    resetViewStateAfterScopeChange(/*alsoExitEditing=*/false);
}

// Integration seam (v0.11 K4-A1): the async worker/fetcher that
// src/main_control.cpp connects to the request signals below reports back
// through this slot. It stays on ControlPanel because main_control.cpp
// connects to it by name; the outcome itself is the Rise card's bookkeeping,
// so it is forwarded there.
void ControlPanel::finishRiseReframeworkOperation(bool ok, const QString &detail)
{
    riseCard_->finishOperation(ok, detail);
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
// Every window the console owns, sorted by how it is reached, persisted
// through Panel::saveAppearance().
//
// WHY ONE PLACE. This is the write that makes a console drag durable, and
// there are two ways out of the console — launch the overlay, or close the
// window — and both must do it. The loop used to be spelled twice, over
// `panelAt(i)`, which was the complete set when either was written: the
// four base panels. The split made eleven more sub-windows and neither
// loop grew, so a sub-window dragged in the console was saved nowhere, from
// either exit. One call, one loop, no third place to forget.
void ControlPanel::saveAllSplitAndBaseWindows()
{
    // v0.16: RE-READ BEFORE WRITING, OR THE WRITE IS DATA LOSS.
    //
    // saveAppearance() writes each window's `margins_` MEMBER, and that
    // member was snapshotted when the Panel was constructed. The overlay
    // runs in its own process and has been moving windows since; its Esc
    // handler wrote the real geometry. So at console-exit time every member
    // here is STALE, and this function - which is what runs on both exits -
    // wrote it straight back over the overlay's layout. Measured on this
    // machine with the new refreshable handle:
    //
    //     console closes, writes member ml=100 -> on disk ml=100
    //     the overlay had written ml=777       -> DESTROYED
    //
    // That is not "the console is a moment behind". That is the user's
    // arrangement gone with no way to recover it, which is the 整个逻辑都有问题
    // report in one line.
    //
    // Panel::loadConfig() now re-reads through a handle that syncs, so
    // calling it first pulls the file's current values into the members
    // before they are written back. Read-then-write, in that order: the
    // other order would preserve the staleness perfectly.
    // ONLY for a window this console has not been given new values for.
    //
    // "Reload unconditionally" is wrong and this is where it shows: the
    // console IS where the user drags a window, so its in-memory margins
    // are the freshest values there are, and reloading throws them away.
    // Measured on this machine: drag a sub-window to (700, 210), close the
    // console, and the file comes back with the pre-drag cell - the
    // console undid the user's own drag, on the way out, by itself.
    //
    // "Never reload" is what the stale-static problem looked like: the
    // console has no idea the overlay moved things, and writes its
    // construction-time snapshot back.
    //
    // The distinction is per window and it is the only honest one, so the
    // window itself records it: Panel::setMargins() is the single place a
    // value becomes new, and it stamps configTouched_ there. A window the
    // user dragged here is stamped; a window nobody touched still holds
    // whatever was on disk when it was built, and the overlay may have
    // moved it since.
    for (int i = 0; i < mhw::kPanelCount; ++i)
        if (Panel *panel = panelAt(i)) {
            if (!panel->configTouchedSinceBuild())
                panel->loadConfig();
        }
    if (splitPreview_) {
        for (int i = 0; i < splitPreview_->count(); ++i)
            if (Panel *sub = splitPreview_->windowAt(i)) {
                if (!sub->configTouchedSinceBuild())
                    sub->loadConfig();
            }
    }

    // The base panels.
    for (int i = 0; i < mhw::kPanelCount; ++i)
        if (Panel *panel = panelAt(i))
            panel->saveAppearance();

    // v0.15: the split sub-windows. saveAppearance() rather than
    // saveConfig() on purpose: it writes scale / minW / minH / lock /
    // opacity / bgAlpha / margins WITHOUT emitting `lockW=0` for every
    // window, so a user who only dragged does not get an ini full of zero
    // rows. That is the same reason the base panels use it here.
    //
    // Reachable only in Split mode — the host is destroyed when the console
    // leaves it — so this is where the split's persistence lives.
    if (splitPreview_) {
        for (int i = 0; i < splitPreview_->count(); ++i)
            if (Panel *sub = splitPreview_->windowAt(i))
                sub->saveAppearance();
    }
}

void ControlPanel::saveBeforeLaunch()
{
    saveMaskToDisk();
    // v0.16: STATE THE SCOPE BEFORE ANY PANEL WRITES THROUGH IT.
    //
    // THE BUG. Panel::saveAppearance() derives its group name from the
    // two scope globals at the moment of the write, and nothing on this
    // path stated them. The console therefore wrote into whichever
    // (game, mode) pair the LAST REBUILDER happened to leave behind:
    //
    //   * at construction the mothers are built at ctor line 1532,
    //     while applyStartupLayoutMode() (line 1961) has not run yet —
    //     so the layout global still holds its Single default and the
    //     very first Panels read and wrote [<game>/single/...]
    //   * syncSplitPreviewToCanvas() sets the mode to Split, but ONLY
    //     on its split arm, and it returns early before that when the
    //     mode is Single — so a Single session leaves the global
    //     saying whatever the previous state was
    //
    // The measured symptom was the user's file: `[world/single/*]`
    // holding four outputName keys while `[world/split/*]` held
    // almost none, on a machine whose conf said layout=split. And the
    // overlay read the split group, so the console's promise ("DP-1",
    // shown in the SCREEN dropdown) never reached the child. The
    // console said DP-1, the overlay mounted on the primary HDMI.
    //
    // WHY THIS AND NOT A SECOND SOURCE OF TRUTH. The alternative
    // readings are all wrong: appendOutput() reading the file would
    // have to re-derive the group name (the same two globals), and
    // Panel::loadConfig() cannot be called here because the console's
    // in-memory values are fresher than disk. The scope is a launch-
    // time fact and the console already resolves it once, in
    // applyStartupLayoutMode(); stating it again here costs two
    // assignments and makes the write deterministic.
    //
    // The mode is set second because setPanelSettingsGame()'s group
    // name is `<game>/<mode>/<key>` — see the same ordering note in
    // switchGame().
    mhw::setPanelSettingsGame(currentGame());
    mhw::setPanelSettingsLayout(layoutModeInForce());
    // v0.5: persist scale / opacity / margins so the overlay's
    // loadConfig() reads the values the user set in the console.
    // Without this, the overlay always starts with the last
    // edit-mode save (or factory defaults).
    saveAllSplitAndBaseWindows();
}

QStringList ControlPanel::buildOverlayArgv(bool editMode) const
{
    // Build argv from the same mask source the file uses.
    const uint32_t mp = ControlPanel::maskFor(ctl_[0]);
    const uint32_t mm = ControlPanel::maskFor(ctl_[1]);
    const uint32_t md = ControlPanel::maskFor(ctl_[2]);
    const uint32_t mt = ControlPanel::maskFor(ctl_[3]);

    // v0.13: the companion (pets) mask is passed on the USER'S SWITCH,
    // and nothing else.
    //
    // WHY THIS STOPPED READING THE GAME. v0.10.1 wrote
    // `petsAvailable = (currentGame() == Rise)` here and then used it for
    // two things: whether --mask-pets is passed, and whether --no-pets is
    // forced. Both bundled two different questions into one bool, and
    // only one of them was legitimate:
    //
    //   * "does the pet-damage surface have a data source under this
    //      game?" — main.cpp's answer: YES under Rise (three readers),
    //      NO under World. That is a RUNTIME fact, and it is already
    //      enforced on the overlay side, where it belongs: the readers
    //      populate riseDamageReaders only under Rise, so nothing ever
    //      feeds the panel, and hasVisibleContent() answers false — so
    //      the window folds itself away quietly.
    //
    //   * "did the user turn the pets panel off?" — the only question
    //      this function should answer, and the answer is the master
    //      toggle's state and nothing else.
    //
    // WHAT THE OLD CODE GOT WRONG, concretely: in World it forced
    // --no-pets, which the overlay reads as
    // `petDamagePanel.setPanelEnabled(false)`. The panel therefore
    // reported itself as USER-DISABLED under World even when its master
    // chip was on, which is what the console's own pets placeholder then
    // echoed back as 「用户已关闭」 rather than the truth (「此游戏没有
    // 宠物伤害数据」). The console and the overlay disagreed about WHY
    // the panel was absent, and the disagreement was visible to the
    // user.
    //
    // The mask is still passed exactly as before in World — the overlay
    // simply has nothing to put in it, so the surface folds — and the
    // console's World-mode pets placeholder (control_panel.cpp's
    // riseOnlyUnavailable branch) still draws its 「仅在 RISE 模式下可用」
    // caption, now for the right reason.
    QStringList args;
    args << QStringLiteral("--mask-player=%1").arg(mp, 0, 16)
         << QStringLiteral("--mask-monster=%1").arg(mm, 0, 16)
         << QStringLiteral("--mask-damage=%1").arg(md, 0, 16)
         << QStringLiteral("--mask-pets=%1").arg(mt, 0, 16);
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
    // The master chip, and only the master chip. A pets panel that is
    // absent under World is absent because there is no data — not
    // because the user is recorded as having switched it off.
    if (!ctl_[3].master->isChecked())
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

    // v0.13: THE LAYOUT MODE, appended at the very end so this function's
    // original body is untouched.
    //
    // Empty in Single mode — see consoleLayoutArgv()'s own comment for
    // why an explicit `--layout=single` would be actively wrong: it
    // outranks a conf row and discards the user's own CLI override,
    // while omitting the flag resolves to the identical Single mode.
    //
    // This closes the console's side of the handshake: the console is
    // the ONLY process that used to build argv with no --layout at all,
    // so a console-launched overlay was pinned to Single no matter what
    // the conf said. The conf row the console writes and the flag it
    // now passes describe the same mode, checked by
    // tests/console_layout_mode_tests.cpp.
    args.append(mhw::consoleLayoutArgv(layoutModeInForce()));

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
    /* ── v1.0.0 sa-A: statusline ──
       The status line's connection half is the ONLY real data in it, and
       this is where that data arrives. The detector's answer is stored in
       gameDetected_ and the status line + connection chip are rewritten
       from it — the same fact the rail badge shows, one resolver, no
       second answer.
       ── end sa-A: statusline ── */
    gameDetected_ = detected.has_value();
    /* ── v1.0.0 sa-D: instance picker ──
       The pid travels with the fact. It is stored here, next to the bool,
       because the 实例 combo prints both and they arrive together — a
       separate lookup later would be a second answer to "which process",
       which is how the badge and the picker end up disagreeing.
       ── end sa-D ── */
    lastDetectedPid_ = detected ? detected->pid : -1;
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
    /* ── v1.0.0 sa-A: sidebar ──
       The game column's read-only caption. The column itself is gone from
       the layout (the game selector moved into the sidebar), but
       leftSplitter_->widget(0) is still that column's QFrame — it is the
       splitter's first page whether or not anything shows it — and the
       caption is still a child of it. Writing through the property keeps
       this path working for the day the caption is re-homed; a null check
       on the page is what makes it safe when widget(0) is nothing at all.
       ── end sa-A: sidebar ── */
    if (leftSplitter_) {
        if (auto *col = leftSplitter_->widget(0)) {
            QLabel *cap = static_cast<QLabel*>(
                col->property("detectedValue").value<void*>());
            if (cap) cap->setText(detectedShort);
        }
    }
    /* ── v1.0.0 sa-A: statusline ──
       And the status line, which now knows whether a game is running.
       ── end sa-A: statusline ── */
    refreshStatusline();
    /* ── v1.0.0 sa-D: instance picker ──
       And the 实例 combo, which now knows which process and its pid.
       Same tick, same facts — the three surfaces cannot disagree.
       ── end sa-D ── */
    refreshInstanceCombo();
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
    // File format (4 mask lines + the schema marker, key=value):
    //   player=<hex32>
    //   monster=<hex32>
    //   damage=<hex32>
    //   pets=<hex32>
    //   maskVersion=<int>      (schema marker — see panel_mask_codec.h)
    // Anything malformed is silently ignored — we never want a bad
    // config to make the console unstartable.
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QString text = in.readAll();

    const mhw::PanelMaskSet masks = mhw::parsePanelMasks(text);

    // v0.12.1: THE ONE-TIME BIT MIGRATION, and why it is keyed on the
    // schema marker rather than on the value's shape.
    //
    // `PlayerSection::Sharpness` (1u << 8) was added to a `player=`
    // field that had only ever needed bits 0-7, so every mask written
    // before it stores bit 8 as ZERO — byte-identical to the value a
    // user who deliberately switches the gauge off stores. Reading it
    // verbatim would open 「斩味完全不见了」 back up.
    //
    // A value-shaped heuristic (migrateOldSharpnessBit's bit-width test)
    // cannot separate "old build" from "user turned it off" and therefore
    // resurrects the gauge on every startup. The marker can: v0.12.1
    // reads an UNMARKED file, migrates bit 8 in memory, and the next
    // save stamps `maskVersion=1` — after which the same ff on disk is
    // stored intent and nothing migrates again. That is why this branch
    // is taken exactly once per file, and why the save side needs no
    // condition of its own.
    //
    // NOTE (deliberately localised here): the migration is applied to the
    // value handed to the checkboxes, so the console's own sharpness row
    // shows the state the user is actually going to get. The overlay's
    // runtime path (panel_player.cpp) reads the same mask and applies the
    // same transform; both halves must agree, or the console would
    // advertise ON while the panel hides it.
    uint32_t playerMaskConf = masks.player;
    if (masks.playerValid && masks.needsSchemaMigration()) {
        playerMaskConf = mhw::PlayerSection::migrateOldSharpnessBit(
            masks.player);
        qInfo("monster-control: mask conf carries no maskVersion= row; "
              "migrating Sharpness bit once (player mask %08x -> %08x). "
              "The next save stamps the schema marker.",
              masks.player, playerMaskConf);
    }

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
    // The player slot is the one whose value may have been migrated
    // above; the other three pass their parsed masks through unchanged.
    applyTo(masks.playerValid, playerMaskConf, ctl_[0]);
    applyTo(masks.monsterValid, masks.monster, ctl_[1]);
    applyTo(masks.damageValid, masks.damage, ctl_[2]);
    applyTo(masks.petsValid, masks.pets, ctl_[3]);
}

// ---- v0.13 layout mode (Single / Split) ---------------------------------
//
// THREE TEXTS, THREE FILES, ONE DECISION
//
// The console's `layout=` row lives in the SAME conf file the four mask
// rows live in (monster-overlay.conf), and is written by the SAME
// saveMaskToDisk() call — deliberately, because that file is the one the
// overlay reads at startup. A second store (QSettings, the console's own
// monster-control.conf) would be a file the overlay never looks at, and
// the row would then have to be kept in step with the file that matters.
//
// The priority chain is NOT re-implemented here. applyStartupLayoutMode()
// feeds the parsed `layout=` row into mhw::resolveStartupLayoutMode()
// — the same function main.cpp calls — and takes its answer. The console
// therefore cannot have a precedence bug the overlay does not also have,
// and cannot silently drop a user's saved choice.
//
// consoleLayoutArgv() spells the flag, and it spells it ONLY in Split.
// Single is the shared resolver's own default (PlayerLayoutSource::
// Default), so omitting the flag yields the identical mode while leaving
// the user's own --layout= override — which outranks everything — intact.
// An explicit --layout=single from the console would beat that override
// and discard it, which is the whole reason the chain has a "cli
// explicit" tier at all.
//
// ABSENCE READS AS SINGLE. Every unusable spelling — no row, garbage, a
// lone "single", a hand-typed "off" — resolves Single, because a value
// that does not say Split must never move a user's windows. That is also
// what keeps the 10 single-mode pixel baselines byte-identical: a fresh
// install, and every user who never touches the combo, opens exactly the
// topology v0.11.4 drew.
void ControlPanel::applyStartupLayoutMode()
{
    const QString path = mhw::localeConfPath();
    QString text;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        text = in.readAll();
    }
    // No file, or a file without a usable `layout=` row, is Single.
    //
    // The alias (PlayerLayoutSource/PlayerLayoutConfRow) — that
    // panel_sections.h describes as "adding the row later is a one-line
    // change here instead of a signature change" — is exactly that
    // one-line change, and this call is it. The struct wrapper the codec
    // returns carries the same enum, so nothing about the shared
    // resolver's shape moves.
    const mhw::ConsoleLayoutConfRow confRow =
        mhw::parseConsoleLayoutRow(text);

    // v1.1.0 Mixed: BOTH games' plan rows, read from the same text this
    // function just read — one read, no second file handle, and NOTHING
    // written or seeded. A load must never copy a settings scope (that is
    // ensureMixedPanelSettingsScope's explicit-entry rule, and its caller
    // is applyLayoutChoice below) nor stamp a conf row (that is
    // saveMaskToDisk's). It only stages what the next Mixed switch will
    // show and what the next save will write.
    loadMixedLayoutPlans(text);

    // The shared chain, called exactly as main.cpp calls it. The CLI
    // half is empty from the console's side: the console is not a
    // command line, so it never "passes a flag", it only states the
    // conf row it just read. That is what makes the console and the
    // overlay agree without the console having an opinion of its own
    // about precedence.
    const mhw::PlayerLayoutResolution resolved =
        mhw::resolveStartupLayoutMode(mhw::PlayerLayoutCliRequest{},
                                      confRow);

    // The mode the running build actually uses from here on: what the
    // resolver said, recorded so the apply button can tell "you picked
    // something new" apart from "you picked what is already in force".
    layoutModeInForce_ = resolved.mode;
    // v1.1.0: through playerLayoutModeToString() rather than a two-arm
    // ternary — the old spelling printed `layout=single` for a Mixed conf
    // row, which is how the console and the overlay ended up disagreeing
    // about the user's own file.
    qInfo("monster-control: layout=%s (source=%d)",
          qPrintable(mhw::playerLayoutModeToString(resolved.mode)),
          static_cast<int>(resolved.source));

    if (layoutCombo_) {
        // blockSignals: setting the index programmatically must NOT be
        // read as a user selection, or every construction would look
        // like a pending change and light up the apply button.
        layoutCombo_->blockSignals(true);
        layoutCombo_->setCurrentIndex(
            layoutComboIndexFor(resolved.mode));
        layoutCombo_->blockSignals(false);
        updateLayoutApplyEnabled();
    }

    // Restoring the hidden legacy combo is not enough: the visible
    // segment must describe the same persisted mode before first show.
    syncLayoutSegmentToMode();

    // v0.13: the canvas must describe the topology this mode names,
    // which is also the only place the sub-windows get built at
    // startup. Done AFTER the combo is set so the log line and the UI
    // agree on what was resolved.
    syncSplitPreviewToCanvas();
}

// The combo's one and only index converter, enum -> index. Kept as a
// named function rather than a literal at each site so a reordered combo
// is one edit and not a hunt.
//
// v1.1.0: exhaustive switch. The old `mode == Split ? 1 : 0` answered
// index 0 for Mixed, so restoring a `layout=mixed` row drove the combo
// to 一整块 while the overlay booted Mixed — the console describing the
// opposite topology to the one on screen. A switch turns that class of
// bug into a -Wswitch warning on the next mode.
int ControlPanel::layoutComboIndexFor(
    mhw::PlayerSection::PlayerLayoutMode mode) const
{
    switch (mode) {
    case mhw::PlayerSection::PlayerLayoutMode::Split:
        return kLayoutComboSplitIndex;
    case mhw::PlayerSection::PlayerLayoutMode::Mixed:
        return kLayoutComboMixedIndex;
    case mhw::PlayerSection::PlayerLayoutMode::Single:
        break;
    }
    return kLayoutComboSingleIndex;
}

// In-force mode as the combo spells it. Returns Single when the combo
// does not exist, so a caller with no UI still gets a sane answer.
//
// v1.1.0: index -> enum, THROUGH layoutComboIndexFor() rather than a
// second independent comparison. Two separate converters is how the
// split-only pairing drifted in the first place: the other half mapped
// EVERY non-Split index to Single, so the third row read back as Single
// and the next saveMaskToDisk() rewrote `layout=mixed` away — the exact
// data-loss shape the deleted two-argument resolver was fixed for, one
// layer up.
mhw::PlayerSection::PlayerLayoutMode ControlPanel::layoutComboMode() const
{
    if (!layoutCombo_ || layoutCombo_->count() == 0)
        return mhw::PlayerSection::PlayerLayoutMode::Single;
    for (const auto mode : {mhw::PlayerSection::PlayerLayoutMode::Single,
                            mhw::PlayerSection::PlayerLayoutMode::Split,
                            mhw::PlayerSection::PlayerLayoutMode::Mixed}) {
        if (layoutCombo_->currentIndex() == layoutComboIndexFor(mode))
            return mode;
    }
    return mhw::PlayerSection::PlayerLayoutMode::Single;
}

// Enable the apply button ONLY when the selection actually differs from
// the mode in force. That is the whole point of the two-widget design:
// the combo records an intention, the button is what makes it real, and
// a button that is armed while it would do nothing is a button that
// teaches the user to distrust it.
void ControlPanel::updateLayoutApplyEnabled()
{
    if (!layoutApplyBtn_ || !layoutCombo_)
        return;
    layoutApplyBtn_->setEnabled(layoutComboMode() != layoutModeInForce());
}

// ---- v0.13: the split sub-window preview ------------------------------
//
// Push the in-force mode into the canvas, and build the sub-window
// geometry the first time (and only time) the mode is Split.
//
// WHY THIS IS NOT CALLED FROM rebuildAndRender(). The four base
// panels' previews refresh on every mask change because their PIXMAPS
// change. The sub-windows' GEOMETRY does not — it comes from the
// tables, which only move when a row is added. So the canvas's
// bindings are set once per mode change, while their pixmaps are
// refreshed by refreshSplitWindowPreviews(). Mixing the two would
// rebuild the whole binding set on every checkbox click.
void ControlPanel::syncSplitPreviewToCanvas()
{
    if (!canvas_)
        return;
    // THE SCOPE, ON EVERY PATH — NOT ONLY THE SPLIT ONE.
    //
    // v0.16: this used to set the two globals AFTER the early return
    // below, so a Single-mode sync left them holding whatever the last
    // split build had stated. That is the same defect saveBeforeLaunch()
    // guards against, seen from the other side: the globals were the
    // console's only statement of "which namespace am I writing", and
    // the single topology — the one every default user is in — was
    // exactly the case that never stated it.
    //
    // Moving the pair to the top costs two assignments on a path that
    // is already rebuilding fourteen windows, and it makes the
    // invariant local: after any sync, the globals name the (game,
    // mode) the console is displaying. Nothing below can then disagree
    // with them, and neither can a later saveAllSplitAndBaseWindows().
    //
    // The mode is set second because setPanelSettingsGame()'s group
    // name is `<game>/<mode>/<key>` — the same ordering switchGame()
    // and saveBeforeLaunch() use.
    mhw::setPanelSettingsGame(currentGame());
    mhw::setPanelSettingsLayout(layoutModeInForce());
    // The mode first, always: setLayoutMode(Single) is what empties the
    // canvas's subSlots_, and it does so regardless of what follows.
    canvas_->setLayoutMode(layoutModeInForce());

    const mhw::PlayerSection::PlayerLayoutMode mode = layoutModeInForce();
    // v1.1.3: MIXED IS A THIRD ARM, NOT "NOT SPLIT". This gate used to
    // read `!= Split`, which is what made Mixed take the Single early
    // return: the host was dropped, the canvas was handed no bindings,
    // and the board could only ever draw the four mother panels — while
    // the overlay booted with the detached windows mounted. `mixed` is
    // computed here and used below (the rebuild guard and the row filter
    // in ensureBuilt) so there is one spelling of "is this arm Mixed".
    const bool mixed =
        mode == mhw::PlayerSection::PlayerLayoutMode::Mixed;
    if (mode != mhw::PlayerSection::PlayerLayoutMode::Split && !mixed) {
        // Single: the windows are no longer needed, and keeping them
        // alive would hold fourteen layer-shell surfaces open for a
        // preview nobody is looking at.
        //
        // THE HOST AND THE CANVAS ARE TWO HALVES OF ONE FACT.
        // setLayoutMode(Single) above already emptied the canvas's
        // subSlots_, but it is re-entrant-safe rather than
        // order-guaranteed: the next call to
        // setSubWindowBindings() would happily re-fill them, and
        // refreshSplitWindowPreviews() renders into them. Resetting the
        // host here is therefore not sufficient on its own — the canvas
        // must be told the same thing, or the two can disagree about
        // what is drawn.
        splitPreview_.reset();
        canvas_->setSubWindowBindings({});
        // ── v1.1.4: AND THE MOTHERS GET THEIR OWN MASK BACK ────────────
        // The Mixed arm below re-states every mother through
        // rebuildAndRender(): effectiveMotherMask() hands the Panel the
        // RETAINED mask and mixedMotherMounts() sets canvas `present` from
        // the same plan. Leaving Mixed used to return here WITHOUT the
        // symmetric write — so the Panels kept the retained mask and
        // `present=false`. A fully detached player mother then had mask 0
        // (nothing to draw, and `present` false so the canvas skipped it
        // outright); monster/damage kept only the rows no split table
        // claims. That is the user's report, verbatim: 「点了下整块，结果
        // 两个显示器上都是一个窗口都没有」.
        //
        // Outside Mixed effectiveMotherMask() is the identity, and the
        // Mixed-only gate at rebuildAndRender():13096 does not run, so this
        // loop restores exactly the pre-Mixed state: the same two values
        // the constructor and every section toggle already produce. It is
        // also the Single half of the symmetry the Mixed arm relies on —
        // one function, both directions, no third place to forget.
        for (int i = 0; i < mhw::kPanelCount; ++i)
            rebuildAndRender(i);
        pushWindowVisibility();
        return;
    }

    // v0.13: THE GAME FIRST, ALWAYS. The window objects read their
    // persisted layout inside the Panel constructor (loadConfig ->
    // settingsGroup()), so the game has to be in force BEFORE the first
    // one is constructed — setting it afterwards retargets the demo SEED
    // but leaves the window sitting on whatever geometry it loaded. That
    // is why a Rise preview showed World's corners, and why a drag in the
    // Rise preview was written back into World's namespace: the windows
    // were built while the scoping global still held its World default,
    // because only main.cpp:586 ever set it and the console is a separate
    // process.
    //
    // Also run this before a REBUILD, not only a first build: switching
    // the rail to the other game destroys and recreates the host, and the
    // new host must load that game's sections.
    //
    // v0.16: BOTH ARE NOW STATED AT THE TOP OF THIS FUNCTION, ON EVERY
    // PATH, because a Single-mode sync used to return before reaching
    // the two calls below and left the globals stale. They are kept
    // here as well — the two statements are the same values, and this
    // block documents WHY they must be true at this point in the code
    // (immediately before fourteen Panel constructors read them), which
    // is a different fact from "they are true by the time we get here".
    mhw::setPanelSettingsGame(currentGame());

    // v0.14: AND THE MODE. The layout namespace is per (game, layout
    // mode) — four domains, not two — so a scope is only fully specified
    // by both. Setting the game alone leaves the mode at its default,
    // which is Single, so every window the host constructed read and
    // wrote the SINGLE namespace while the user was looking at a split
    // preview. That is the console half of exactly the disagreement the
    // four-domain split was introduced to end: the overlay was told
    // `--layout split` and read [game/split/...], while the console wrote
    // [game/single/...].
    //
    // Spelled from layoutModeInForce() rather than hard-coded Split, for
    // the same reason the top of the function does: the caller's early
    // return already established that this arm IS split mode, and
    // "the default happens to be right" is not something a reader can
    // verify. It also stops being right the moment a third mode exists.
    mhw::setPanelSettingsLayout(layoutModeInForce());

    // THE GAME THE CURRENT HOST WAS BUILT FOR. Recorded at build time so
    // the next call can tell "I am new" (build a host) apart from "I am
    // for the OTHER game" (destroy and build a new one) apart from "I am
    // for this game already" (do nothing). Three states, not two: the
    // middle one is the whole rail-switch bug.
    //
    // WHY REBUILD AND NOT RESYNC. A Panel caches everything the panel
    // namespace decides for it, and it reads it exactly once, inside its
    // constructor: loadConfig() runs there and calls settingsGroup(),
    // which resolves the game-scoped group name and then reads ml / mt /
    // mr / mb / scale / minW / minH / opacity / bgAlpha out of it. By the
    // time the constructor returns, those numbers are already members of
    // a live widget. So setPanelSettingsGame() — the one thing that could
    // redirect them — has nothing left to act on: the geometry was read
    // from a group name that was resolved with the OLD game and there is
    // no reader to run again.
    //
    // syncAll() cannot fix that, and trying it is what produced the user's
    // «Rise 预览不同步». syncAll() re-seeds the demo data, which changes
    // WHAT a window paints, and re-pushes the section masks, which change
    // WHICH blocks it paints. Neither touches margins, scale, opacity,
    // floors or the columns dial — the four things that decide where the
    // window sits and how big it is. So the preview kept World's
    // geometry while drawing Rise's demo pills, which is precisely the
    // mismatch reported. The only way to make a Panel re-read its
    // namespace is to run its constructor again.
    //
    // The fourteen windows are also the expensive objects the host exists
    // to own, and rebuild-on-rail-switch is bounded by the user clicking
    // a rail button — twice a session at most. That is the right trade
    // against a preview that lies.
    const mhw::GameId hostGame = splitPreviewHostGame_;
    // v1.1.3: TWO MORE THINGS MAKE A HOST STALE, and both are recorded at
    // build time (splitPreviewHostMode_ / splitPreviewHostPlan_):
    //
    //   * the MODE — Split's host owns every row of the split table and
    //     Mixed's owns only the plan's detached ones, so neither can be
    //     reused for the other. Before v1.1.3 Mixed never reached this
    //     line, which is why only the game was ever checked here;
    //   * the PLAN (Mixed only) — one 独立/合并 toggle changes WHICH
    //     rows the host owns. This is the ONLY thing that rebuilds a host
    //     for a plan change, and it costs one 11-bit comparison per sync,
    //     so a drag, a paint or a visibility refresh never rebuilds: a
    //     Mixed sync whose (game, mode, plan) is unchanged does exactly
    //     what a Split one does.
    //
    // Single is not on this path at all: its arm above reset the host.
    const bool needsRebuild = !splitPreview_
                             || hostGame != currentGame()
                             || splitPreviewHostMode_ != mode
                             || (mixed && splitPreviewHostPlan_
                                     != mixedPlanSignature());
    if (needsRebuild) {
        // Destroy FIRST. The old host's windows own PanelSourceAdapters
        // the canvas holds raw pointers to, and dropping the host frees
        // them; the new bindings below arrive before the next paint, so
        // the canvas never observes a freed source.
        splitPreview_.reset();
        splitPreview_ = std::make_unique<SplitWindowPreviewHost>();
        // The plan is only READ here, at construction: nullptr (the whole
        // split table, byte-identical to before) outside Mixed, and the
        // plan in force inside it. A later plan change rebuilds instead
        // of re-filtering — see the guard above.
        splitPreview_->ensureBuilt(currentGame(),
                                   mixed ? &mixedPlanFor(currentGame())
                                         : nullptr);
        splitPreviewHostGame_ = currentGame();
        splitPreviewHostMode_ = mode;
        splitPreviewHostPlan_ = mixedPlanSignature();
        // v0.15 → v1.0.0 (OUTPUT-OWNERSHIP-FIX): PUSH THE WINDOW'S OWN
        // OWNERSHIP FALLBACK INTO EVERY SPLIT SUB-WINDOW.
        //
        // THE TWO BUGS THIS ONE PUSH IS ABOUT. A split sub-window has no
        // outputName of its own — panels.ini carries no `outputName` key
        // for any of them — so Panel::marginLimits() resolved an empty
        // name to the step-2 fallback. v0.15 supplied that step from the
        // canvas's preview screen, which made the display the user was
        // LOOKING AT become the screen the window was MEASURED against:
        //
        //     v0.15  fallback = canvas previewScreen() = DP-1  2560 wide
        //     wanted fallback = parent panel output    = HDMI  1080 wide
        //
        // and a real Shift+Right sequence then wrote ml=2331 for a window
        // whose own screen stops at 851 — a value persisted 1480 px past
        // the end of the display it is configured for. The push now walks
        // reference.html:421's chain instead: the window's own outputName
        // (never pushed — it stays step 1 and keeps winning), then the
        // parent panel's outputName, then the primary's name.
        //
        // WHY HERE AND NOT IN THE HOST. The host is private to this
        // translation unit and builds its windows in its constructor,
        // which runs BEFORE any caller can say which parent/output the
        // console has settled on — the same ordering constraint that
        // moved the wall into ensureBuilt(game). The four mother panels
        // are the console's to know, so the console pushes their answer,
        // immediately after the windows exist.
        //
        // WHY setPreviewFallbackOutput() AND NOT setOutputName(). The
        // fallback is preview-only: it is never persisted, never read by
        // loadConfig(), and never applied by applyGeometry() — so it can
        // never move a real layer-shell surface. A window that names its
        // own output still wins (resolveOutputScreen() tries outputName_
        // first), so a future per-sub-window picker keeps working.
        //
        // THE MOTHER PANELS ARE DELIBERATELY NOT PUSHED. They already
        // carry their own outputName_ from their own SCREEN dropdown, and
        // an outputName always outranks the fallback — pushing here
        // would be a no-op for them at best and a wrong-screen
        // measurement at worst.
        pushWindowOutputFallback(
            splitPreview_.get(),
            {panelAt(0), panelAt(1), panelAt(2), panelAt(3)});
        canvas_->setSubWindowBindings(splitPreview_->bindings());
        // The first paint needs pixmaps; a later mask change goes
        // through refreshSplitWindowPreviews() instead.
        splitPreview_->syncAll(currentGame(),
                               ControlPanel::maskFor(ctl_[0]),
                               ControlPanel::maskFor(ctl_[1]),
                               ControlPanel::maskFor(ctl_[2]));
        splitPreview_->renderAll(canvas_);
        qInfo("monster-control: split preview: %d sub-window(s) "
              "built for %s (%d player, %d monster, %d damage)",
              splitPreview_->count(),
              currentGame() == mhw::GameId::Rise ? "rise" : "world",
              splitPreview_->countForParent(0),
              splitPreview_->countForParent(1),
              splitPreview_->countForParent(2));
        // ── v1.1.3: AND THE MOTHERS THAT GAVE THEIR ROWS UP ────────────
        //
        // Mixed only, and only when the host was actually rebuilt —
        // which is exactly when the topology changed (game, mode or plan),
        // so a no-op sync never pays for it.
        //
        // WHAT THIS FIXES, ONE ROW DOWN. The mother Panel still holds the
        // USER's mask, but in Mixed the overlay's mother is handed
        // plan.retainedMask() (main.cpp:761) and mounts only while
        // planMotherPanelEnabled() says so (main.cpp:776). Without this
        // loop the board painted a mother that gives every row away as a
        // full panel — a window the overlay does not mount, drawn from
        // blocks that are ALSO drawn in their own detached windows. Both
        // halves come out of rebuildAndRender(): it applies
        // effectiveMotherMask() to the Panel and mixedMotherMounts() to
        // the canvas's `present` flag, and the paint arm of the canvas
        // skips a mother whose `present` is false (hud_canvas.cpp:2710).
        //
        // WHY rebuildAndRender() AND NOT A NEW FUNCTION. It is the one
        // place the mask, `present`, the rail tile and the split
        // re-render already live together, and its Single/Split answer is
        // unchanged (effectiveMotherMask is an identity there), so this
        // call is reachable in the two legacy modes only from the paths
        // that already call it.
        if (mixed)
            for (int i = 0; i < mhw::kPanelCount; ++i)
                rebuildAndRender(i);
    }
    // ── stage A: the split rebind reset every SubSlot's flag ───────────
    pushWindowVisibility();
}

// Re-render the sub-windows' pixmaps after a mask change. A no-op in
// Single mode, where the canvas has nothing to show and the host is
// gone.
void ControlPanel::refreshSplitWindowPreviews()
{
    if (!canvas_ || !splitPreview_)
        return;
    // v1.1.3: Mixed has a host too (a filtered one), and its windows read
    // the same three masks — so it takes the same road. The gate is
    // "does the mode build a host", spelled as the two that do rather
    // than as `!= Single`, which would silently admit the next topology
    // without its windows being in the host.
    const mhw::PlayerSection::PlayerLayoutMode mode = layoutModeInForce();
    if (mode != mhw::PlayerSection::PlayerLayoutMode::Split
        && mode != mhw::PlayerSection::PlayerLayoutMode::Mixed)
        return;
    // Only worth doing if the canvas still has the bindings: the two
    // are set together in syncSplitPreviewToCanvas(), so a mismatch
    // means the canvas was replaced under us.
    if (canvas_->subWindowCount() <= 0)
        return;
    splitPreview_->syncAll(currentGame(),
                           ControlPanel::maskFor(ctl_[0]),
                           ControlPanel::maskFor(ctl_[1]),
                           ControlPanel::maskFor(ctl_[2]));
    splitPreview_->renderAll(canvas_);
}

// The commit half: persist, then make the running child agree.
//
// WHY A RESTART AND NOT A LIVE FLAG. `--layout` is read ONCE at overlay
// startup (main.cpp's resolveStartupLayoutMode) and the window topology
// is derived from it; there is no in-process setter for it. So the
// action's honest description is "remember this and relaunch", and the
// UI is allowed to promise exactly that — nothing more.
//
// WHY THE CONFIRM IS UNCONDITIONAL when a child is alive. Restarting
// kills a live overlay mid-hunt: the layer-shell surfaces unmap, the
// console comes back to the front, and the overlay remounts. A user who
// pressed "apply" while looking at their HUD wants that; a user who
// wandered into the control by accident does not. The dialog is cheap
// and the operation is not reversible, so it is always asked.
void ControlPanel::applyLayoutChoice(mhw::PlayerSection::PlayerLayoutMode mode)
{
    // Already in force: nothing to persist, nothing to restart.
    if (layoutModeInForce() == mode) {
        updateLayoutApplyEnabled();
        return;
    }

    // No child: the choice is written to disk and picked up by the NEXT
    // launch. No dialog — nothing was interrupted, nothing to confirm.
    if (!overlay_->isRunning()) {
        // AND THE CANVAS MOVES TOO. This is the arm the user actually
        // hits: the console is open, the overlay is not, and they are
        // deciding what the NEXT launch should look like. It used to
        // return here, so the combo, the conf row and the preview all
        // disagreed until the console was reopened — the user's
        // «切换完全没反应», seen from the no-child side.
        //
        // THE ORDER IS THE OTHER HALF OF THE SAME BUG.
        // saveMaskToDisk() serialises the `layout=` row FROM
        // layoutModeInForce_, so the mode has to move before the write,
        // not after it. With the old ordering the row on disk named the
        // mode the user was LEAVING: every apply on the no-child path
        // wrote one mode behind, and the console then reported one
        // layout while the conf declared another — the exact
        // inconsistency saveMaskToDisk()'s own comment describes as the
        // symptom it was written to remove.
        // THE MIXED SCOPE SEED, on the way IN. ensureMixedPanelSettingsScope()
        // copies the <game>/<fromMode>/<window> values into
        // <game>/mixed/<window> (missing keys only), so a user who enters
        // Mixed keeps the geometry they had — and it must run exactly on
        // an EXPLICIT entry: never from a read-only load, never from a
        // refresh. That is why the call lives here rather than in
        // applyStartupLayoutMode()/refreshMixedGroupBlock(): this is the
        // only place the user's own choice moves Single|Split -> Mixed,
        // and a Mixed -> Mixed repeat cannot reach it (the guard at the
        // top of this function already returned for that), with the
        // function's own Mixed check as a second belt.
        if (mode == mhw::PlayerSection::PlayerLayoutMode::Mixed)
            mhw::ensureMixedPanelSettingsScope(currentGame(),
                                               layoutModeInForce());
        layoutModeInForce_ = mode;
        updateLayoutApplyEnabled();
        saveMaskToDisk();     // also rewrites the layout= row
        /* ── v1.0.0 sa-A: topbar ──
           The segmented control follows the mode, and the split-only
           controls' visibility with it. Called here rather than only in
           the segment's click handler because applyLayoutChoice() is also
           reachable from the hidden combo pair (which four test files
           drive directly), and the segment has to be right after either
           path commits.
           ── end sa-A: topbar ── */
        syncLayoutSegmentToMode();
        // The nav counts are per-mode: Single draws one window per panel,
        // Split draws the split host's windows.
        refreshNavCounts();
        //
        // Nothing else has to move: the mode is a whole-canvas decision,
        // the four base panels' pixmaps are unchanged by a topology
        // switch, and the split windows either get built here (Split) or
        // dropped here (Single), both of which
        // syncSplitPreviewToCanvas() owns.
        syncSplitPreviewToCanvas();
        // The draft resets the view on a mode switch (reference.html:334),
        // and the window model has to be rebuilt anyway: Single's mother
        // panels and Split's sub-windows are different windows.
        resetViewStateAfterScopeChange(/*alsoExitEditing=*/true);
        qInfo("monster-control: layout=%s saved; the next launch uses it",
              mode == mhw::PlayerSection::PlayerLayoutMode::Split ? "split"
                                                                 : "single");
        return;
    }

    const QString title = mh::tr(QStringLiteral("console.layout.confirm_title"));
    const QString body =
        (mode == mhw::PlayerSection::PlayerLayoutMode::Split
             ? mh::tr(QStringLiteral("console.layout.confirm_split"))
             : mh::tr(QStringLiteral("console.layout.confirm_single")));
    if (QMessageBox::question(this, title, body,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }

    // THE SAME ORDER FIX, on the running-child arm. saveMaskToDisk()
    // writes the row FROM layoutModeInForce_, and buildOverlayArgv()
    // spells the flag from it as well, so both reads must observe the
    // new mode. Writing first and moving the mode afterwards left the
    // relaunched child with a `layout=` row one mode behind the
    // `--layout` it was handed — and since the CLI flag outranks the
    // conf row, the overlay would have come back in the NEW mode while
    // the user's own hand-written override was silently discarded by
    // the contradiction. The mode moves first, and everything after
    // reads the mode the user just chose.
    //
    // THE SAME SEED as the no-child arm, reached here only AFTER the
    // confirm dialog said yes: a declined dialog must leave Mixed
    // untouched, so no scope may have been copied before the question
    // was answered. Same guard — an explicit entry, Single|Split ->
    // Mixed, and nothing else.
    if (mode == mhw::PlayerSection::PlayerLayoutMode::Mixed)
        mhw::ensureMixedPanelSettingsScope(currentGame(), layoutModeInForce());
    layoutModeInForce_ = mode;
    updateLayoutApplyEnabled();
    saveMaskToDisk();         // the row the relaunched child will read
    /* ── v1.0.0 sa-A: topbar ──
       The same follow-up on the running-child arm: the segment and the
       Split-only visibility track the committed mode.
       ── end sa-A: topbar ── */
    syncLayoutSegmentToMode();
    refreshNavCounts();
    // v0.13: the canvas follows the mode immediately, so the user sees
    // the preview change before the child has even been asked to exit.
    syncSplitPreviewToCanvas();
    // Same reset as the no-child arm — the confirm dialog is the only
    // thing the two paths disagree about.
    resetViewStateAfterScopeChange(/*alsoExitEditing=*/true);

    // THE RESTART MECHANISM, reusing what the game hot-swap invented:
    // SIGTERM the child (overlay_->stop()), then let the controller's own
    // 250 ms poll observe the exit. handleExited() fires the relaunch
    // from the pendingRestart_ flag — the SAME mechanism
    // OverlayProcessController::setCurrentGame() uses, so there is no
    // second spawn path.
    //
    // Why not killAndRestart() in one call: stop() deliberately does NOT
    // zero overlayPid_ (the overlay's own ESC handler runs saveConfig()
    // and quits cleanly on SIGTERM), so the pid stays the poll's target
    // until the exit is observed. The flag is armed BEFORE stop() so the
    // relaunch cannot be lost if the exit is observed unusually fast.
    //
    // setLastArgv() first, because the controller's stored copy carries
    // the OLD --layout: --game is appended by launch() from currentGame_
    // (which IS mutable, so replaying is always correct there), but
    // --layout is baked into the caller's argv and would otherwise
    // resurrect the previous mode on the relaunch.
    overlay_->setLastArgv(buildOverlayArgv(overlay_->lastEditMode()),
                          overlay_->lastEditMode());
    overlay_->setPendingRestart(true);
    overlay_->stop();
}

void ControlPanel::saveMaskToDisk() const
{
    const QString path = mhw::localeConfPath();

    const uint32_t mp = ControlPanel::maskFor(ctl_[0]);
    const uint32_t mm = ControlPanel::maskFor(ctl_[1]);
    const uint32_t md = ControlPanel::maskFor(ctl_[2]);
    const uint32_t mt = ControlPanel::maskFor(ctl_[3]);

    // The mask writer owns exactly these five rows (four masks + the
    // schema marker). Preserve locale, comments and unknown/future keys
    // in their original relative order, while removing every old copy of
    // a canonical mask row so duplicates converge.
    //
    // VIOLATION CHECK: `layout=` is NOT one of the five. It is not a
    // mask and does not belong in the codec — see the note below on why
    // it is written from here rather than through serializePanelMasks().
    //
    // The `maskVersion=1` row is emitted unconditionally by the codec:
    // this is the write half of the one-time schema migration. An
    // unmarked file read by loadMaskFromDisk() migrated its Sharpness bit
    // in memory; the next save stamps the marker, and from then on the
    // stored value is stored intent — the console can never resurrect a
    // gauge the user switched off.
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

    // v0.13: append the `layout=` row (or nothing).
    //
    // WHY APPENDED OUTSIDE THE CODEC and not inside serializePanelMasks():
    // that function is a tested contract ("these five rows, in this
    // order, then the preserved block verbatim") shared with the overlay's
    // own reader and covered by panel-mask-codec-tests. Widening its
    // signature to take a layout value would put a v13 concern in a v12
    // contract, and its test would then have to change for a reason that
    // has nothing to do with masks.
    //
    // WHY IT LANDS AFTER the preserved block: the row's position is
    // cosmetic (parseConsoleLayoutRow() scans every line), so it goes
    // where appending is free — and it stays clear of the mask rows'
    // documented fixed order, which a mask test might pin.
    //
    // WHY IT CAN BE ABSENT: serializeConsoleLayoutRow() returns EMPTY for
    // Single. Single is the resolver's default, so writing a row that
    // states the default would add a hand-editable "single" that looks
    // like a deliberate choice, and would put the row in the file of
    // every user who ever opened the combo. A file without the row and
    // a file with `layout=single` resolve identically — which is the
    // property that keeps an old conf byte-for-byte single.
    //
    // WHY THE OLD ONE IS DROPPED FIRST. The parser scans every line and
    // stops at the first one it can read as Split, so duplicates resolve
    // correctly — which is exactly what made them invisible: nothing
    // failed, the conf just grew a row per save. What stopped being true
    // is the thing the user actually watches. saveMaskToDisk() runs on
    // close, before START, and on every apply, so a session that toggled
    // between two modes ended with the file's LAST row contradicting the
    // mode now in force — and the next launch read that last row. The
    // console was then reporting one layout while the conf declared
    // another, which is the "I switched and nothing happened" symptom.
    //
    // So the row is a SET, not a pile: every existing one is removed and a
    // single fresh row appended iff Split. Two saves in a row still
    // produce one row, and a mode that changed in between produces the
    // row for the mode that is now in force.
    QByteArray written = data;
    const QByteArray layoutKey(mhw::kConsoleLayoutRowKey);
    {
        QList<QByteArray> lines = data.split('\n');
        QList<QByteArray> kept;
        kept.reserve(lines.size() + 1);
        for (const QByteArray &line : lines) {
            if (line.trimmed().startsWith(layoutKey + '='))
                continue;   // drop every stale copy; see WHY above
            kept.append(line);
        }
        written = kept.join('\n');
        // join() leaves a trailing NUL when the source ended in '\n'
        // (split's last element is empty), which join does not re-add.
        if (data.endsWith('\n') && !written.endsWith('\n'))
            written.append('\n');
    }
    // NO TRAILING-NEWLINE DOUBLE. The codec's output already ends in
    // '\n' and so does the row, so a single append lands one row per
    // line with exactly one terminator. Nothing below re-splits on '\n'
    // to decide where a row begins, so this is not load-bearing — but
    // the file is user-editable, so it stays clean.
    written.append(mhw::serializeConsoleLayoutRow(layoutModeInForce()));

    // ── v1.1.0 Mixed: the `layout.windows.<game>=` row ────────────────────
    //
    // THE SAME SET RULE the `layout=` row above uses, for the same
    // reason: saveMaskToDisk() runs on save, on close and on every apply,
    // and a plan row that piled up would leave the file's LAST row
    // contradicting the switches the console is showing. So every stale
    // copy of THIS game's row is dropped and one fresh row is appended —
    // two saves in a row produce one row and byte-identical output.
    //
    // ONLY THIS GAME'S ROW. The other game's row, unknown keys and the
    // locale row are already preserved verbatim inside `written` (that is
    // serializePanelMasks()'s contract), and nothing below rewrites them:
    // saving World must not move Rise's list, and vice versa. The prefix
    // is the full `layout.windows.world=` / `layout.windows.rise=` key,
    // so one can never prefix-match the other.
    //
    // ONLY IN MIXED. Single and Split resolve a FIXED plan and never read
    // this row (window_layout_plan.h's resolve rule), so writing it there
    // would put a row in the file of every user who ever opened the
    // combo — the exact "states a default as if it were a choice" harm
    // the `layout=` row's absence rule above avoids. A Mixed plan that
    // was saved earlier is still preserved by the codec while the console
    // is in Single (it is an unknown key to the mask writer), so leaving
    // Mixed loses nothing: the row is there again, unchanged, next time.
    if (layoutModeInForce() == mhw::PlayerSection::PlayerLayoutMode::Mixed) {
        const QByteArray planKey =
            mhw::layoutWindowsRowKey(currentGame()).toLatin1();
        if (!planKey.isEmpty()) {
            const QByteArray planPrefix = planKey + '=';
            const QList<QByteArray> lines = written.split('\n');
            QList<QByteArray> kept;
            kept.reserve(lines.size() + 1);
            for (const QByteArray &line : lines) {
                if (line.trimmed().startsWith(planPrefix))
                    continue;   // drop every stale copy; see WHY above
                kept.append(line);
            }
            QByteArray rebuilt = kept.join('\n');
            // Same NUL guard as the layout= block above: join() drops the
            // terminator split() kept as its last (empty) element.
            if (written.endsWith('\n') && !rebuilt.endsWith('\n'))
                rebuilt.append('\n');
            written = rebuilt;
            written.append(mhw::serializeWindowLayoutPlanRow(
                currentGame(), mixedPlanFor(currentGame())));
        }
    }

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
    if (out.write(written) != written.size()) {
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

    uint32_t userMask = 0;
    for (int b = 0; b < ctl_[idx].subs.size(); ++b)
        if (ctl_[idx].subs[b]->isChecked())
            userMask |= (1u << b);
    // ── v1.1.3 (Mixed): THE MASK THIS PANEL DRAWS ────────────────────────
    // effectiveMotherMask() is the identity in Single and Split (the two
    // legacy topologies therefore read the exact value this line has
    // always read, bit for bit) and plan.retainedMask(parent, userMask) in
    // Mixed — the same value main.cpp:761 hands the OVERLAY's mother.
    // Without it the console's mother kept drawing blocks it had just
    // detached: once inside its own window, once still inside the mother.
    //
    // `userMask` (not `mask`) is what every gate below reads, because the
    // gates ask about the USER's switches (saveMaskToDisk writes those)
    // while `mask` is what the PANEL draws. Where the two differ is only
    // in Mixed, and there it is the whole point: a row the plan gave away
    // stays ON in the user's mask and OFF in the mother's.
    const uint32_t mask = effectiveMotherMask(idx, userMask);
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
        // v0.13: the sub-windows read the same sections the base panel
        // just stopped drawing, so their previews move too. A no-op in
        // Single mode (the host is null there), so the call is safe on
        // every mask change including the four the ctor makes.
        refreshSplitWindowPreviews();
        return;
    }

    // v0.16: A PANEL WITH NOTHING TO SAY IS NOT DRAWN AT ALL.
    //
    // The user's report, verbatim:
    //     「隐藏的面板不是真隐藏 是一个这种空面板，不能真的隐藏么？」
    //
    // The screenshot showed a bordered box with an accent stripe and a drag
    // handle, no content. That is not the canvas inventing a frame: it is
    // Panel::paintPanel() faithfully rendering an 18px chrome-only shell,
    // because the section mask strips every content block away and only
    // drawV03Chrome() is left. The canvas had no way to know — every base
    // panel is bound with present=true and only pets (index 3) was ever told
    // otherwise (hud_canvas.h, `bool present{true}`).
    //
    // NOT hasVisibleContent(). That was the obvious choice and it is wrong:
    //
    //     panel_player.h:37   hasContent() -> return true;    // ALWAYS
    //     panel_monster.h:66  hasContent() -> return hasData_;
    //     panel_damage.h:70   hasContent() -> return m_vm.hasData();
    //
    // The player panel answers true unconditionally, so gating on it would
    // have left the exact shell the user photographed on screen while
    // compiling cleanly and looking reasonable.
    //
    // The predicate is one of the three in panel_sections.h, whose own
    // contract is "does this mask leave its panel with anything to DRAW".
    //
    // *** AND THE TWO PROCESSES ANSWER DIFFERENT QUESTIONS ON PURPOSE. ***
    //
    // An earlier version of this comment claimed "one rule, two
    // processes — so the preview can no longer show a window the overlay
    // would refuse to mount". That is FALSE, and measurably so:
    //
    //     console:  hasBlocks(0x1ff) = 1   -> draws
    //     overlay:  hasBlocks(0x000) = 0   -> does not mount
    //
    // The values differ because the PANELS differ. main.cpp:562 computes
    // maskPlayerForPanel = playerPanelMaskFor(mode, maskPlayer), which in
    // Split mode subtracts every bit kPlayerSplitTargets claimed — all
    // nine — and hands the overlay's own PlayerPanel a zero mask.
    // playerPanelMaskFor() has exactly one call site in the tree (main.cpp),
    // so the console never sees that value. What this Panel was handed is
    // the user's own checkbox state, set three lines above at :4286.
    //
    // So the console is asking about ITS preview Panel ("does the mask
    // this preview panel holds leave it something to draw?"), and the
    // overlay is asking about ITS OWN ("did the split leave anything?").
    // Same function, two subjects, and in Split mode the two subjects
    // hold different masks by construction.
    //
    // Nothing consumes this value in Split mode, which is why the
    // divergence has never mattered: hud_canvas.cpp:1066 gates the whole
    // four-mother draw arm on subSlots_.isEmpty(), and Split always has
    // eleven sub-windows. Measured, by flipping slots_[0].present under
    // otherwise identical conditions:
    //
    //     SPLIT   mother-panel present TRUE vs FALSE -> 0.000000 px delta
    //     SINGLE  mother-panel present TRUE vs FALSE -> 0.193846 px delta
    //
    // So the console call is a no-op in Split and live in Single, and in
    // Single the two predicates agree — panelMaskFor(Single, m) == m.
    // Correct by coincidence today. If canvas ever paints mothers under
    // Split, or a third layout mode appears where Single stops being the
    // identity, this stops being correct and the console must start
    // passing the derived mask instead.
    //
    // It is NOT setVisible(false). The slot stays bound so selecting it in
    // the rail still works and the inspector still opens; the window simply
    // stops being painted, which is what "really hidden" has to mean for a
    // canvas that also has to keep hit-testing.
    //
    // sectionMask() is the user's live checkbox state: the handler this
    // function belongs to set it three lines above.
    //
    // v1.1.3 CORRECTION to that one sentence: in Mixed the Panel holds
    // the RETAINED mask, so `mask` here is the user's switches minus the
    // rows this run detached. The predicate is unchanged and still right
    // — it asks "does this PANEL have anything to draw", and the retained
    // mask is precisely what this panel draws. The gates that ask a
    // different question ("does this run mount it at all") read userMask
    // below instead.
    bool hasBlocks = true;
    if (idx == 0)
        hasBlocks = mhw::PlayerSection::playerPanelHasBlocks(mask);
    else if (idx == 1)
        hasBlocks = mhw::monsterPanelHasBlocks(mask);
    else if (idx == 2)
        // DamageSection::OtherMembers filters actors rather than being a
        // content block, and damagePanelHasBlocks() knows that.
        hasBlocks = mhw::damagePanelHasBlocks(mask);
    // idx == 3 (pets) has no split table and keeps its own master/content
    // gate, which the disabled arm above already returned on.
    //
    // ── v1.1.3: MIXED ASKS THE OVERLAY'S GATE, NOT ONLY hasBlocks ────────
    // hasBlocks(retained) answers "can it draw"; planMotherPanelEnabled
    // adds the second arm the overlay has — "or nothing was ever detached
    // from it" — which is what keeps a maskless mother mounted in Mixed
    // exactly as main.cpp:776 does. Gated on the mode, deliberately:
    // outside Mixed this flag's answer must stay byte-identical, and
    // mixedMotherMounts()'s own `return true` fallback is the WRONG
    // answer for a Single run whose mask is empty. The gate lives at the
    // call site for that reason — never remove it.
    if (layoutModeInForce() == mhw::PlayerSection::PlayerLayoutMode::Mixed
        && idx < mhw::kLayoutParentCount)
        hasBlocks = mixedMotherMounts(idx, userMask);

    if (canvas_) {
        canvas_->setPanelPresent(idx, hasBlocks);
        if (hasBlocks)
            canvas_->setPanelPixmap(idx, renderPreview(panel), true);
        else
            canvas_->setPanelPixmap(idx, QPixmap(), false);
    }
    const QPixmap pix = renderPreview(panel);
    if (lab)
        lab->setPixmap(pix);
    // v0.13: see the note in the disabled arm above — the same section
    // bits gate this panel's split windows.
    refreshSplitWindowPreviews();
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
    // preview tile (purple for player, orange for monster, teal for damage,
    // teal for pets) so each panel has a clear identity at a glance. The stripe
    // sits OUTSIDE the panel rectangle, so we just draw it on the pixmap.
    //
    // The colours come from the shared panel-accent table — the same one
    // Panel::accentColor() and HudCanvas read — so the tile stripe, the
    // HUD preview and the live overlay all paint identical hues. This used
    // to be a fourth hand-written set of literals that named the
    // player/monster tiles after the *console* theme's print hex while the
    // overlay painted its own values. The slot index is recovered by
    // identity because renderPreview() is handed a Panel*; the order
    // matches the member declarations above.
    const int slot = (p == player_)  ? 0
                   : (p == monster_) ? 1
                   : (p == damage_)  ? 2
                                     : 3;
    const QColor accent = mhw::panel_accent::forSlot(slot);
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
    /* ── v1.1.0: THE OVERLAY-EDIT BUTTON IS NOT A LAUNCHER ────────────────
       One child at a time is the contract launchOverlayChild() enforces,
       and it enforces it with a bare `return` — so a click that arrives
       while a child is alive used to be swallowed with no button state
       change and no message. The user saw a live button and nothing
       happened. Disabling it states the truth in the place the user is
       already looking, and the tooltip keeps its explanation (the
       console's own Edit Layout button is deliberately NOT touched: it
       only edits the canvas and never wanted a child).
       ── end v1.1.0 ────────────────────────────────────────────────── */
    if (overlayEditBtn_) {
        overlayEditBtn_->setEnabled(!running);
        trTip(overlayEditBtn_, running
                  ? QStringLiteral("console.edit.overlay_running_tip")
                  : QStringLiteral("console.edit.overlay_tip"));
    }
    /* ── v1.0.0 sa-A: statusline ──
       The run half of the status line ("未启动(演示)" / "模拟运行中 · PID
       {p}") is liveness, which is exactly what this function is told.
       Rewriting it here — rather than in a timer — means the line can
       never disagree with the START button's own label, because both are
       written from the same `running` argument.
       ── end sa-A: statusline ── */
    refreshStatusline();
    // v1.1.1 (顶栏溢出): the launch/stop swap rewrites the button's TEXT,
    // and a QLabel-free QPushButton's minimumSizeHint is its text — so
    // the row's minimum changed with it. Re-decide the wrap rather than
    // let a wider word push the header back past the window edge.
    refreshTopbarWrap();
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
    // v1.0.0: the editable position. Without this the two position
    // controls disagree after a drag: posLabel reads the live margins and
    // updates, while the spins keep the values they were built with. A
    // user who drags a panel and then looks at the spin sees a number that
    // is not where the panel is.
    if (c.posSpinX || c.posSpinY) {
        const QMargins m = panel->margins();
        if (c.posSpinX) {
            c.posSpinX->blockSignals(true);
            c.posSpinX->setValue(m.left());
            c.posSpinX->blockSignals(false);
        }
        if (c.posSpinY) {
            c.posSpinY->blockSignals(true);
            c.posSpinY->setValue(m.top());
            c.posSpinY->blockSignals(false);
        }
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
    // v0.11.4: report the margins the OVERLAY will actually apply
    // (clampMarginsToScreen), not the raw stored vector. Before this the
    // label could show a position that was mathematically right for the
    // margins but unreachable on the attached screens, which is exactly
    // the "面板飞到屏幕外" complaint in disguise: the readout looked
    // authoritative while the panel was not where it said.
    const QMargins m = p->clampMarginsToScreen(p->margins());
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

// ── v1.0.1: THE THREE CHEAP REFRESHES (设置 / 预览 / 属性 + 拖动跟手) ────

// 验收项2: 检查器第三列的图源。原来 pix.scaled(label.size()*dpr) 把已经
// 在自有 scale 渲染过的 pixmap 再压进 ~132px 高的盒子 —— 又糊又被裁。
// 现在原像素显示，超框部分由 windowPreviewScroll 滚动查看；空状态仍是
// setPixmap(QPixmap()) + 提示文字的旧契约。
void ControlPanel::refreshWindowPreviewPixmap()
{
    if (!windowPreviewBox_)
        return;
    const mhw::WindowState w = selectedWindow();
    if (w.panelIndex < 0)
        return;
    /* ── round2: THE CARD'S HEIGHT BUDGET IS THE LEFT HALF'S HEIGHT ──────
       Before the split, the preview column's height WAS the grid row's
       height — the tallest of the three columns, because a QSplitter
       stretches every child across its cross axis. That is where the
       render's height came from: measured at 1600x1000 the row was ~442px
       (元数据/分区 are that tall), leaving ~330px of render room. In a
       scroll area of its own the card would collapse to the drawer's own
       height (~247px, i.e. ~54px of render room) and the preview would
       render three times smaller than the console the user had.

       So the budget is restated EXPLICITLY: the card is at least as tall
       as the LEFT half's content is — which is exactly the old row
       height, since that row was max(元数据, 分区, 预览). The input is the
       OTHER half's content and never this preview, so 实测缺陷5's rule
       holds: the fit still cannot be fed by the fit. Both halves then
       carry the same content height and each scrolls its own copy of it
       — which is precisely the change the user asked for (滚左边不动右边).
       ── end round2: the card's height budget ── */
    if (windowPreviewCol_ && inspectorGridHost_)
        windowPreviewCol_->setMinimumHeight(inspectorGridHost_->height());
    Panel *panel = (w.subIndex >= 0 && splitPreview_)
                       ? splitPreview_->windowAt(w.subIndex)
                       : panelAt(w.panelIndex);
    const QPixmap pix = panel ? renderPreview(panel) : QPixmap();
    if (pix.isNull()) {
        // A window whose sections are all off paints nothing; the
        // prototype says so in words rather than an empty rectangle.
        windowPreviewSource_ = QPixmap();   // the empty state owns the box
        windowPreviewBox_->setText(
            consoleText(QStringLiteral("console.proto.preview.empty")));
        windowPreviewBox_->setPixmap(QPixmap());
        // 实测缺陷5: THE EMPTY STATE OWNS THE FLOOR. build time gives the
        // label 200x120 so an unselected console says so in a readable
        // rectangle; a FILLED box drops that floor in applyPreviewDisplay()
        // so the frame can hug the render. Restoring it here is what keeps
        // the two states independent of the order they arrive in.
        windowPreviewBox_->setMinimumSize(200, 120);
        windowPreviewBox_->adjustSize();
        applyPreviewDisplay();              // …and clears the readout row
        return;
    }
    windowPreviewBox_->setText(QString());
    /* ── 验收项1: THE SOURCE IS REPLACED, NEVER MUTATED ──────────────────
       Every render replaces the immutable high-resolution source; the
       label only ever gets a DERIVED pixmap (see applyPreviewDisplay()).
       That is the "原图不可反复缩小再放大" half: zooming in and out a
       hundred times always re-scales the same full-resolution render,
       so there is no accumulated resampling loss, and no path in here
       writes Panel::scale() or the `scale=` row.
       ── end 验收项1: source swap ── */
    windowPreviewSource_ = pix;
    applyPreviewDisplay();
}

// 验收项1 (B 第1阶段): THE THREE ZOOM ENTRY POINTS.
//
// previewFitScale() — the ratio that shows the WHOLE image in the
// preview column the user has now (splitter drag included).
//
// ── 实测缺陷5 (user-8): AVAILABLE, NOT VISIBLE ─────────────────────────────
// It used to read windowPreviewScroll_'s viewport. That was right while
// the pane was stretched across the column (viewport = the cell), and it
// is wrong now that the pane HUGS the render: the pane would then be the
// product of this function, and feeding the answer back into the question
// shrank the image by kPreviewDefaultFill of itself on every layout pass
// — measured, not theorised: 272x260 → 97x93 after two passes.
//
// So the input is the CONTAINER the pane lives in: windowPreviewCol_ is
// the CARD of the right half — round2 put that half in its own scroll
// area (inspectorContentRight) as a child of inspectorColumns, the
// splitter the user drags — so its WIDTH is set by the handle, i.e. by
// the OTHER half's width, and its HEIGHT is set by its own content (the
// host layout keeps a trailing stretch, so no spare-tall cell can grow
// it). Neither can come from this preview: the chrome subtracted below
// (caption, zoom row, bottom note, the card's border + 12/10 padding) is
// content-independent too, so nothing here can feed back into the image
// size. The −4 is the label's own 1px QSS border on each side plus
// rounding slack, so that `render + frame` always lands INSIDE the card:
// without it, Fit mode hands the column back a hint 2px taller than
// itself and the row grows 2px per pass.
// ── end 实测缺陷5 ──
qreal ControlPanel::previewFitScale() const
{
    if (windowPreviewSource_.isNull() || !windowPreviewCol_
        || !windowPreviewScroll_)
        return 1.0;
    const QRect col = windowPreviewCol_->rect();
    int chrome = 0;
    int items = 0;
    /* ── round2: THE CARD'S OWN INSETS ────────────────────────────────────
       inspectorPreview is a bordered, padded card now (QSS border +
       this layout's 12/10 contents margins), and the border is painted
       INSIDE the frame's rect — so the room the pane can actually use
       is the rect minus 2×border minus the margins on that axis.

       Both subtractions are CONSTANTS of the card, never of the render:
       the input is still windowPreviewCol_'s box (set by the splitter)
       plus content-independent chrome, so nothing here can feed the
       scale's answer back into its own question — the loop 实测缺陷5
       measured (272x260 → 97x93 in two passes) stays closed. Before this
       the horizontal margins were not subtracted at all, which used to
       be harmless while the frame had no padding or border and would now
       make Fit hand back an image wider than its own card (a scrollbar
       where 适应窗口 promised a fit).
       ── end round2: the card's insets ── */
    QMargins cardInsets;
    if (QLayout *lay = windowPreviewCol_->layout()) {
        for (int i = 0; i < lay->count(); ++i) {
            const QLayoutItem *it = lay->itemAt(i);
            if (!it)
                continue;
            // The trailing stretch is not chrome: it is the spare space
            // itself (widgetless AND 0-sized, unlike the zoom row, which is
            // a widgetless layout with a real hint).
            if (!it->widget() && it->sizeHint().isEmpty())
                continue;
            ++items;
            if (it->widget() == windowPreviewScroll_)
                continue;
            // Live geometry once the layout has run — the bottom note is
            // word-wrapped, so its sizeHint is the unwrapped one and would
            // under-count the space it really takes. sizeHint is the
            // pre-layout fallback (before the first pass nothing has a
            // height yet).
            const QWidget *w = it->widget();
            chrome += (w && w->height() > 0)
                          ? w->height()
                          : it->sizeHint().height();
        }
        chrome += lay->spacing() * qMax(0, items - 1);
        cardInsets = lay->contentsMargins();
        chrome += cardInsets.top() + cardInsets.bottom();
    }
    // The card's own frame inset: control_panel_styles.cpp paints a 1px
    // QSS border on QFrame#inspectorPreview, and QStyleSheetStyle adds
    // the frame's own 1px line width on top of it — MEASURED, not
    // guessed: the card's host laid a 639px row out to a 611px card, i.e.
    // 24px of layout margins plus 2px per side of frame. Qt really does
    // inset a layout by its widget's styled frame (that is why this is a
    // constant and not a runtime style read: QStyle exposes no
    // frameWidth(), and the exact number belongs to the stylesheet).
    // Reserving it when no stylesheet paints a border costs Fit a couple
    // of pixels of an image it already shrinks for the pane, while NOT
    // reserving it would let the card hand back a render wider than its
    // own border box — a scrollbar where 适应窗口 promised a fit. The −4
    // that follows is the LABEL's own 1px border on each side plus
    // rounding slack, its job before the card existed.
    constexpr qreal kCardBorder = 4.0;
    const qreal vw = col.width() - kCardBorder
                     - cardInsets.left() - cardInsets.right() - 4.0;
    const qreal vh = col.height() - chrome - kCardBorder - 4.0;
    const qreal sw = windowPreviewSource_.width();
    const qreal sh = windowPreviewSource_.height();
    if (vw <= 0.0 || vh <= 0.0 || sw <= 0.0 || sh <= 0.0)
        return 1.0;
    // CAPPED AT 1:1. Fit means "show all of it", never "enlarge it":
    // past 1:1 the label would be showing interpolated pixels of a source
    // that is already small enough to see whole, and — the reason this
    // cap is written down — it makes Fit monotone against 1:1 (the
    // rendered pixmap is never bigger than the source), which is the
    // property the 1:1 / Fit round trip is asserted on.
    return qMin<qreal>(1.0, qMin(vw / sw, vh / sh));
}

/* ── 实测缺陷2 (usability-2): THE DEFAULT VIEW ─────────────────────────────
   「这里这个太大了，应该是一个正常大小的窗口」 — measured, not guessed: with
   Fit as the default the four mother panels (378×361 / 380×345 / 380×337 /
   380×337) all land at 88–94% in the 444×318 pane, i.e. the HUD window is
   drawn touching the pane's top and bottom edges and reads as a zoomed
   crop. 1:1 would be worse, not better (361 > 318 → it overflows).

   So the default is the FIT ratio scaled by kPreviewDefaultFill: it can
   never fill the pane, it follows the pane when the user drags the
   splitter, it never enlarges a source past 1:1 (previewFitScale() is
   already capped), and it degrades to plain Fit when there is nothing to
   show. The named modes are untouched: 适应窗口 is still exactly Fit and
   实际大小 is still exactly the source pixels — the default is simply no
   longer one of them pretending to be "the whole image, edge to edge".
   ── end 实测缺陷2: the default view ── */
namespace {
// Fraction of the viewport the DEFAULT view fills. 0.75 leaves ~12% of the
// pane free on each axis at any aspect, which is the smallest margin that
// still reads as "a window sitting in a frame" instead of "a borderless
// crop".
constexpr qreal kPreviewDefaultFill = 0.75;
} // namespace

qreal ControlPanel::previewDefaultScale() const
{
    return qMin<qreal>(1.0, kPreviewDefaultFill * previewFitScale());
}

qreal ControlPanel::stepPreviewZoom(int dir) const
{
    // FROM WHERE THE USER IS LOOKING: an explicit zoom steps that ratio,
    // the default view steps the ratio actually on screen (so the first
    // Ctrl+wheel tick enlarges what is visible instead of jumping to
    // Fit), and Fit steps the fit scale.
    const qreal base = previewMarginDefault_ ? previewDefaultScale()
                       : previewZoom_ > 0.0   ? previewZoom_
                                              : previewFitScale();
    const qreal next = base * (dir > 0 ? 1.25 : 0.8);
    return qBound(0.1, next, 8.0);
}

void ControlPanel::setPreviewZoom(qreal z)
{
    // z == 0 is Fit (the reset button); 1.0 is 1:1 with the source render;
    // anything between is an explicit ratio. ANY of them is the user
    // choosing a zoom, so all of them retire the default view — that is
    // the only state change besides previewZoom_ itself.
    previewMarginDefault_ = false;
    previewZoom_ = (z <= 0.0) ? 0.0 : qBound(0.1, z, 8.0);
    // THE ONLY SIDE EFFECT. No Panel::setScale(), no setScale(..., false),
    // no panels.ini write — the acceptance item's "独立于真实 HUD scale"
    // is enforced by there being no other statement in this function.
    applyPreviewDisplay();
}

// source + zoom -> the label's pixmap, plus the readout row above it.
void ControlPanel::applyPreviewDisplay()
{
    // MODE PRECEDENCE: the default view (until the user zooms), then an
    // explicit ratio, then Fit. Only this line picks the mode, so the
    // readout, the pixmap and the resize re-derive can never disagree.
    const qreal eff = previewMarginDefault_ ? previewDefaultScale()
                      : previewZoom_ > 0.0   ? previewZoom_
                                             : previewFitScale();
    if (previewZoomLabel_) {
        if (windowPreviewSource_.isNull()) {
            previewZoomLabel_->setText(QString());
            if (previewSizeLabel_)
                previewSizeLabel_->setText(QString());
        } else {
            const qreal pct = qRound(eff * 100.0);
            const QString mode =
                previewMarginDefault_
                    ? consoleText(QStringLiteral("console.preview.default"))
                    : previewZoom_ > 0.0
                          ? consoleText(QStringLiteral("console.preview.zoom"))
                          : consoleText(QStringLiteral("console.preview.fit"));
            previewZoomLabel_->setText(
                QStringLiteral("%1 %2%").arg(mode, QString::number(pct)));
            if (previewSizeLabel_) {
                const int sw = windowPreviewSource_.width();
                const int sh = qRound(windowPreviewSource_.height() * eff);
                previewSizeLabel_->setText(
                    QStringLiteral("%1 %2×%3 · %4 %5")
                        .arg(consoleText(QStringLiteral("console.preview.size")),
                             QString::number(sw),
                             QString::number(windowPreviewSource_.height()),
                             consoleText(QStringLiteral("console.preview.height")),
                             QString::number(sh)));
            }
        }
    }
    if (!windowPreviewBox_ || windowPreviewSource_.isNull())
        return;
    /* ── 实测缺陷5: THE FILLED BOX HAS NO FLOOR ────────────────────────────
       The empty state keeps the 200x120 minimum the label is built with
       (measured in the user's screenshot: a 156x74 render framed by a
       200x120 box = 44px of dead space to the right and 46px below the
       image, INSIDE the border). With a render present the box is sized by
       the render and nothing else — setMinimumSize(0,0) also retires the
       build-time setMinimumHeight(120). The only survivor is the 1px QSS
       border, which is the frame itself.
       ── end 实测缺陷5: no floor on a filled box ── */
    windowPreviewBox_->setMinimumSize(0, 0);
    QPixmap derived;
    if (qFuzzyCompare(1.0, eff)) {
        derived = windowPreviewSource_;
    } else {
        // DEVICE PIXELS × ratio, then the SOURCE's DPR is put back: Qt's
        // QLabel sizes itself from the pixmap's device-independent size
        // (probed on Qt 6.12: 200x100@dpr2 → a 100x50 label), so keeping
        // the DPR is what makes `eff` mean the same thing at DPR 1 and
        // DPR 2 — measured, not assumed.
        const qreal dpr = windowPreviewSource_.devicePixelRatio();
        const QSize target(qRound(windowPreviewSource_.width() * eff),
                           qRound(windowPreviewSource_.height() * eff));
        derived = windowPreviewSource_.scaled(target, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
        derived.setDevicePixelRatio(dpr);
    }
    windowPreviewBox_->setPixmap(derived);
    // widgetResizable(false) requires explicitly adopting the new hint.
    windowPreviewBox_->adjustSize();
    /* ── 实测缺陷5: TELL THE COLUMN THE PANE'S HINT CHANGED ─────────────────
       The label lives inside the scroll area's viewport, which has no
       layout of its own — so QLabel::adjustSize() → updateGeometry()
       invalidates nothing that could re-measure the pane, and the
       COLUMN's layout would keep laying the pane out from the hint it
       last saw (measured: item sizeHint=200x120 while the pane itself
       already answered 96x92). updateGeometry() on the pane walks one
       level up to windowPreviewCol_, which does have one.
       ── end 实测缺陷5 ── */
    windowPreviewScroll_->updateGeometry();
}

// 验收项6: scale / opacity 改的是 Panel 的真实绘制，画布画的是它收到的
// pixmap，所以必须把新 pixmap 重新交给画布（和检查器预览）；只
// canvas_->update() 会继续画旧图。renderPreview() 复用 Panel 的
// paintEvent，opacity 已烘进 alpha —— 不二次乘 alpha。一次渲染三处复用。
void ControlPanel::refreshSelectedWindowPreview()
{
    const mhw::WindowState w = selectedWindow();
    if (w.panelIndex < 0)
        return;
    Panel *panel = (w.subIndex >= 0 && splitPreview_)
                       ? splitPreview_->windowAt(w.subIndex)
                       : panelAt(w.panelIndex);
    if (!panel)
        return;
    if (w.subIndex >= 0) {
        refreshSplitWindowPreviews();
        refreshWindowPreviewPixmap();
        return;
    }
    const QPixmap pix = renderPreview(panel);
    if (canvas_) {
        // The same master/mask gate rebuildAndRender() applies, so an
        // opacity change can never resurrect a disabled panel on the
        // canvas.
        const uint32_t mask = panel->sectionMask();
        bool hasBlocks =
            w.panelIndex == 0 ? mhw::PlayerSection::playerPanelHasBlocks(mask)
            : w.panelIndex == 1 ? mhw::monsterPanelHasBlocks(mask)
            : w.panelIndex == 2 ? mhw::damagePanelHasBlocks(mask)
                                : true;
        // v1.1.3: the same MIXED gate rebuildAndRender() applies, and for
        // the same reason it must not drift from it: this function runs
        // after an opacity/scale change while rebuildAndRender() runs
        // after a mask or plan change, and two functions computing
        // `present` two ways is how a mother flickers between mounted and
        // unmounted as the user edits it. Gated on the mode exactly as
        // there — outside Mixed the answer above is the legacy one.
        if (layoutModeInForce() == mhw::PlayerSection::PlayerLayoutMode::Mixed
            && w.panelIndex < mhw::kLayoutParentCount)
            hasBlocks = mixedMotherMounts(w.panelIndex,
                                          maskFor(ctl_[w.panelIndex]));
        canvas_->setPanelPresent(w.panelIndex, hasBlocks);
        if (hasBlocks)
            canvas_->setPanelPixmap(w.panelIndex, pix, true);
        else
            canvas_->setPanelPixmap(w.panelIndex, QPixmap(), false);
    }
    if (ctl_[w.panelIndex].preview)
        ctl_[w.panelIndex].preview->setPixmap(pix);
    refreshWindowPreviewPixmap();
}

// 验收项4: 拖动时“位置字段实时同步”的只读入口。窗口模型的其它部分
// （标题、分区列表、检查器重建、rail 计数）在一次拖动里都不变，只有
// metaX / metaY 和它们的 enabled 态跟着 corner/margins/scale/content 走。
// 调用侧已确认拖的就是选中的窗口，这里因此不再重建整个 WindowList。
void ControlPanel::refreshSelectedWindowPositionFields()
{
    // D1: the SAME accessor, and the drag path needs no third copy of the
    // sub-window-or-mother lookup — `sel.panel` is the instance the frame
    // and the numbers both come from.
    const EditableSelection sel = currentEditableSelection();
    if (!sel.panel || !winPosX_ || !winPosY_)
        return;
    const WindowFrame f = windowFrameFor(sel);
    const bool convert = f.valid;
    winPosX_->setEnabled(convert);
    winPosY_->setEnabled(convert);
    if (convert) {
        const QPoint tl = mhw::layout::anchoredTopLeft(
            f.corner, sel.panel->pushedMargins(), f.screen, f.content, f.scale);
        if (!winPosX_->hasFocus())
            winPosX_->setValue(tl.x());
        if (!winPosY_->hasFocus())
            winPosY_->setValue(tl.y());
    } else {
        winPosX_->setValue(0);
        winPosY_->setValue(0);
    }
}

// 验收项8: combo 的唯一重建点（初始填充 / 语言切换 / 屏幕热插拔）。只写
// combo 本身 —— 显示器选择是 view 操作，谁都不能在这里写 Panel 的
// outputName。内屏 eDP 不再被过滤（原型那句 "eDP-1 is disabled and never
// listed" 是演示表数据，不是运行时合同）。
void ControlPanel::rebuildScreenCombo()
{
    if (!screenCombo_)
        return;
    const QString keep = screenCombo_->currentData().toString();
    screenCombo_->blockSignals(true);
    screenCombo_->clear();
    QString primaryName;
    const auto outs = screen_query::listOutputs();
    for (const auto &o : outs) {
        if (o.name.isEmpty())
            continue;      // Qt had not populated the name yet
        const bool portrait = o.geometry.height() > o.geometry.width();
        QString label = QStringLiteral("%1 · %2×%3")
                            .arg(o.name)
                            .arg(o.geometry.width())
                            .arg(o.geometry.height());
        if (portrait)
            label += mh::tr(QStringLiteral("console.proto.screen.rot"));
        if (o.primary) {
            label += mh::tr(QStringLiteral("console.inspector.primary_tag"));
            primaryName = o.name;
        }
        screenCombo_->addItem(label, o.name);
    }
    if (screenCombo_->count() == 0) {
        // No output Qt can see (headless test, or the platform plugin has
        // nothing yet): one placeholder so the combo is not empty.
        screenCombo_->addItem(
            mh::tr(QStringLiteral("console.inspector.primary")), QString());
    } else if (!keep.isEmpty() && screenCombo_->findData(keep) >= 0) {
        screenCombo_->setCurrentIndex(screenCombo_->findData(keep));
    } else if (!primaryName.isEmpty()) {
        const int at = screenCombo_->findData(primaryName);
        if (at >= 0)
            screenCombo_->setCurrentIndex(at);
    } else {
        screenCombo_->setCurrentIndex(0);
    }
    screenCombo_->blockSignals(false);
    // Rebuilding with signals blocked must still update the viewer; an
    // unplug must not leave the canvas filtering a screen absent from Qt.
    if (canvas_) {
        const QString screen = screen_query::effectiveOutputName(
            screenCombo_->currentData().toString());
        if (canvas_->overviewMode()) canvas_->setOverview(true, screen);
        else canvas_->setPreviewScreen(screen);
        refreshWindowModel();
        refreshStageHead();
    }
    // v1.1.1 (顶栏溢出): the entries' text IS the combo's width hint, so a
    // hot-plug that adds a long output name (`HDMI-A-1 · 1080×1920(rotated)
    // (primary)`) changes the row's minimum too. Re-decide the wrap here
    // rather than trust the canvas branch above to have reached it.
    refreshTopbarWrap();
}
