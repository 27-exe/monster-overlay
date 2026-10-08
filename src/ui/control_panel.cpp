#include "control_panel.h"

#include <vector>

#include "ui/panel.h"
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
#include <signal.h>
#include <sys/types.h>

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

// ─── the host ───────────────────────────────────────────────────────────

// How many grid columns the Rise wall is shifted by, relative to World's.
// One column: enough that a Rise preview is visibly its own arrangement,
// small enough that the cluster still starts near the corner and the walk
// still reads outward in table order.
constexpr int kRiseWallColumnShift = 1;

class SplitWindowPreviewHostImpl {
public:
    SplitWindowPreviewHostImpl()
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
        // The windows are still built here, because everything else about
        // them (the adapter, the demo seed, the section mask) does not depend
        // on the game at construction time. Only the wall does.
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
        }
    }

    // Re-render every window and push the pixmaps into the canvas.
    void renderAll(HudCanvas *canvas)
    {
        if (!canvas)
            return;
        for (size_t i = 0; i < entries_.size(); ++i) {
            const QPixmap pix = renderSplitWindow(entries_[i].window.get());
            canvas->setSubWindowPixmap(static_cast<int>(i), pix, true);
        }
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
    // construction, and the wall needs to know which game it is placing.
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
    void ensureBuilt(mhw::GameId game)
    {
        if (!impl_)
            impl_ = std::make_unique<SplitWindowPreviewHostImpl>();
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
        p->setSectionMask(ControlPanel::maskFor(ctl_[i]));
        // Master visibility: the inspector chip, which is the only thing
        // that says whether the surface mounts at all.
        p->setPanelEnabled(ctl_[i].master && ctl_[i].master->isChecked());
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

    // ---- v0.13: the Single / Split selector ----------------------------
    //
    // WHY HERE. The rail is the console's command column: it already
    // holds START, the game selector and the "workspace" group this
    // sits in, and the user's mental model of "console chooser" is
    // already trained on it. Putting the mode selector in the stage bar
    // would sit it next to the preview toggles (safe area / grid /
    // zoom), where every neighbour is a CANVAS control that takes
    // effect instantly — exactly the wrong neighbourhood for a control
    // whose effect arrives via a restart.
    //
    // TWO WIDGETS, NOT ONE. A combo alone would record a choice that
    // changes nothing: `--layout` is read once at overlay startup and
    // the window topology is derived from it, so picking an entry
    // mid-hunt would silently do nothing (the "the console is broken"
    // failure). A button alone would restart a live overlay with no way
    // to say WHERE the user wants to go. Combo + apply is the smallest
    // pair whose semantics are exactly the truth: pick, then commit,
    // and committing relaunches when it must.
    layoutCombo_ = new QComboBox();
    layoutCombo_->setObjectName("layoutCombo");
    layoutCombo_->setCursor(Qt::PointingHandCursor);
    layoutCombo_->addItem(
        mh::tr(QStringLiteral("console.layout.single")));
    layoutCombo_->addItem(
        mh::tr(QStringLiteral("console.layout.split")));
    // i18n: the two entries are static copy, but they are ITEM text, and
    // trSet() only handles a widget's Text property. A hook re-adds them
    // on a language switch instead — see retranslateUi().
    trHook([this]{
        if (!layoutCombo_) return;
        const int keep = layoutCombo_->currentIndex();
        layoutCombo_->blockSignals(true);
        layoutCombo_->setItemText(kLayoutComboSingleIndex,
            mh::tr(QStringLiteral("console.layout.single")));
        layoutCombo_->setItemText(kLayoutComboSplitIndex,
            mh::tr(QStringLiteral("console.layout.split")));
        layoutCombo_->setCurrentIndex(keep);
        layoutCombo_->blockSignals(false);
    });
    scrollLayout->addWidget(layoutCombo_);

    layoutApplyBtn_ = new QPushButton();
    trSet(layoutApplyBtn_, QStringLiteral("console.layout.apply"));
    layoutApplyBtn_->setObjectName("layoutApply");
    layoutApplyBtn_->setCursor(Qt::PointingHandCursor);
    layoutApplyBtn_->setEnabled(false);   // nothing to do until a change
    // The tooltip is the semantic contract, stated where the user asks
    // for it rather than in a manual: what this button does, and what
    // it does NOT (it cannot flip a running overlay in place).
    trTip(layoutApplyBtn_, QStringLiteral("console.layout.apply_tip"));
    scrollLayout->addWidget(layoutApplyBtn_);
    connect(layoutCombo_, &QComboBox::currentIndexChanged,
            this, [this]{ updateLayoutApplyEnabled(); });
    // The one place a user commits a mode. Everything the commit does
    // (persist, and restart the child when it must) is inside
    // applyLayoutChoice() — the lambda only forwards the choice, so the
    // semantics live in one named, commentable function.
    connect(layoutApplyBtn_, &QPushButton::clicked, this,
            [this]{ applyLayoutChoice(layoutComboMode()); });

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
    inspectorLayout->addWidget(inspectorStack_);
    topRow->addWidget(inspectorHost);

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
        refreshSplitWindowPreviews();
    });
    // Selection, so the footer can name the sub-window being dragged.
    connect(canvas_, &HudCanvas::subWindowSelected, this, [this](int){
        if (canvas_) canvas_->update();
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
    for (int i = 0; i < mhw::kPanelCount; ++i) {
        // v0.11.4: the adapter is owned by boundSources_ (a member), not
        // orphaned on the heap. The canvas keeps a non-owning
        // const PanelSource*, so ownership has to live on our side and
        // die with the console — see the member's comment.
        auto adapter = std::make_unique<PanelSourceAdapter>(panelAt(i));
        canvas_->bindPanel(i, adapter.get());
        boundSources_[i] = std::move(adapter);
    }
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
    for (int i = 0; i < mhw::kPanelCount; ++i)
        updatePanelSummary(i);                    // counts + nav summaries
    for (int i = 0; i < mhw::kPanelCount; ++i)
        updatePosLabel(i);                        // corner + margin readout
    refreshAutoDetect();                          // badge + GAME column
    riseCard_->refresh();                         // Rise-only management card
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

    // v0.11 K4-A1: the Rise-only REFramework management card (build, refresh
    // and the four button actions) lives in src/ui/rise_reframework_card.
    if (idx == 2) {
        riseCard_->build(content, vl);
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
    // The base panels, exactly as before.
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

    // The shared chain, called exactly as main.cpp calls it. The CLI
    // half is empty from the console's side: the console is not a
    // command line, so it never "passes a flag", it only states the
    // conf row it just read. That is what makes the console and the
    // overlay agree without the console having an opinion of its own
    // about precedence.
    const mhw::PlayerLayoutResolution resolved =
        mhw::resolveStartupLayoutMode(mhw::PlayerLayoutCliRequest{},
                                      confRow.source);

    // The mode the running build actually uses from here on: what the
    // resolver said, recorded so the apply button can tell "you picked
    // something new" apart from "you picked what is already in force".
    layoutModeInForce_ = resolved.mode;
    qInfo("monster-control: layout=%s (source=%d)",
          resolved.mode == mhw::PlayerSection::PlayerLayoutMode::Split
              ? "split" : "single",
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

    // v0.13: the canvas must describe the topology this mode names,
    // which is also the only place the sub-windows get built at
    // startup. Done AFTER the combo is set so the log line and the UI
    // agree on what was resolved.
    syncSplitPreviewToCanvas();
}

// The combo's one and only index converter. Kept as a named function
// rather than a literal at each site so a reordered combo (or a third
// mode, if one ever arrives) is one edit and not a hunt.
int ControlPanel::layoutComboIndexFor(
    mhw::PlayerSection::PlayerLayoutMode mode) const
{
    return mode == mhw::PlayerSection::PlayerLayoutMode::Split
               ? kLayoutComboSplitIndex
               : kLayoutComboSingleIndex;
}

// In-force mode as the combo spells it. Returns Single when the combo
// does not exist, so a caller with no UI still gets a sane answer.
mhw::PlayerSection::PlayerLayoutMode ControlPanel::layoutComboMode() const
{
    if (!layoutCombo_ || layoutCombo_->count() == 0)
        return mhw::PlayerSection::PlayerLayoutMode::Single;
    return layoutCombo_->currentIndex() == kLayoutComboSplitIndex
               ? mhw::PlayerSection::PlayerLayoutMode::Split
               : mhw::PlayerSection::PlayerLayoutMode::Single;
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
    // The mode first, always: setLayoutMode(Single) is what empties the
    // canvas's subSlots_, and it does so regardless of what follows.
    canvas_->setLayoutMode(layoutModeInForce());

    if (layoutModeInForce() != mhw::PlayerSection::PlayerLayoutMode::Split) {
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
    // The host is only ever built in split mode — the caller returns early
    // above when the mode is Single — so the mode here is not a choice, it
    // is what the caller already established. It is spelled out anyway,
    // because "the default happens to be right" is not something a reader
    // can verify, and it stops being right the moment a third mode exists.
    mhw::setPanelSettingsLayout(mhw::PlayerSection::PlayerLayoutMode::Split);

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
    const bool needsRebuild = !splitPreview_
                             || hostGame != currentGame();
    if (needsRebuild) {
        // Destroy FIRST. The old host's windows own PanelSourceAdapters
        // the canvas holds raw pointers to, and dropping the host frees
        // them; the new bindings below arrive before the next paint, so
        // the canvas never observes a freed source.
        splitPreview_.reset();
        splitPreview_ = std::make_unique<SplitWindowPreviewHost>();
        splitPreview_->ensureBuilt(currentGame());
        splitPreviewHostGame_ = currentGame();
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
    }
}

// Re-render the sub-windows' pixmaps after a mask change. A no-op in
// Single mode, where the canvas has nothing to show and the host is
// gone.
void ControlPanel::refreshSplitWindowPreviews()
{
    if (!canvas_ || !splitPreview_)
        return;
    if (layoutModeInForce() != mhw::PlayerSection::PlayerLayoutMode::Split)
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
        layoutModeInForce_ = mode;
        updateLayoutApplyEnabled();
        saveMaskToDisk();     // also rewrites the layout= row
        //
        // Nothing else has to move: the mode is a whole-canvas decision,
        // the four base panels' pixmaps are unchanged by a topology
        // switch, and the split windows either get built here (Split) or
        // dropped here (Single), both of which
        // syncSplitPreviewToCanvas() owns.
        syncSplitPreviewToCanvas();
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
    layoutModeInForce_ = mode;
    updateLayoutApplyEnabled();
    saveMaskToDisk();         // the row the relaunched child will read
    // v0.13: the canvas follows the mode immediately, so the user sees
    // the preview change before the child has even been asked to exit.
    syncSplitPreviewToCanvas();

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
        // v0.13: the sub-windows read the same sections the base panel
        // just stopped drawing, so their previews move too. A no-op in
        // Single mode (the host is null there), so the call is safe on
        // every mask change including the four the ctor makes.
        refreshSplitWindowPreviews();
        return;
    }

    const QPixmap pix = renderPreview(panel);
    if (lab)
        lab->setPixmap(pix);
    if (canvas_)
        canvas_->setPanelPixmap(idx, pix, true);
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
