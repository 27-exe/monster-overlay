// SPDX-License-Identifier: Apache-2.0
#pragma once

// window_layout_plan.h — WHICH sub-window rows are detached from which
// mother panel, as ONE value the whole run shares.
//
// WHY A PLAN AND NOT ANOTHER MODE FLAG
// ------------------------------------
// v1.0.1 answered "is this window its own window?" with a single
// PlayerLayoutMode: Single = everything merged, Split = everything
// carved out. Mixed is the third topology the user actually asked for —
// 「把状态、血条、斩味拆出去，衣装和翔虫留在主面板里」 — and a mode
// enum cannot express "six rows, four of them mine". The mode still
// decides WHICH topology is in force; this plan decides, inside Mixed,
// which of the three split tables' rows are windows and which stay with
// the mother panel.
//
// So the plan is a set of ROWS, never a set of bits:
//
//   * a row is one entry of kPlayerSplitTargets / kMonsterSplitTargets
//     / kDamageSplitTargets — the SAME tables Split already uses, so
//     Mixed cannot claim a block Split would not have claimed;
//   * OtherMembers and Tenderize are FILTER bits (they decide which rows
//     a panel draws, not a block of their own), which is why no row
//     claims them and why they survive every detach in the mother mask;
//   * the mother's retained mask is `original & ~<claims of the rows it
//     gave up>`, so retained ∪ detached covers the original exactly and
//     the two never overlap. Nothing here invents a bit definition: every
//     mask value comes out of the three tables above.
//
// LAYERING: QtCore only (QString / QByteArray / QStringList), inline over
// the pure constexpr tables in ui/panel_sections.h. No QWidget, no
// QSettings, no config file, no main.cpp — the overlay's wiring of this
// plan into live windows is a later stage and must not have to touch a
// model header to find a fact.
//
// TWO ORDERS EXIST HERE ON PURPOSE — DO NOT CONFLATE THEM:
//
//   * parent SLOT ids follow the repository's existing convention:
//     control_panel.cpp's SplitWindowRow spells it "0 player, 1 monster,
//     2 damage", and that is the numbering
//     parent-qa/mixed-topology-matrix.json carries for its 4096
//     reference cases. kLayoutParent* below is that numbering.
//   * the REGISTRY is also the CANONICAL KEY ORDER, and it follows
//     panel_sections.h's own declaration order: kPlayerSplitTargets
//     (line ~377), kDamageSplitTargets (~1186), kMonsterSplitTargets
//     (~1500). tests/window_layout_plan_tests.cpp pins that order:
//     a shuffled "monster-conditions,damage-share,player-bars,
//     player-status" must serialise back in registry order.
//
// Registry order is therefore NOT parent-index order. Entries are found
// by settingsKey or by (parentSlot, tableRow) — never by array
// position.

#include <QByteArray>
#include <QString>
#include <QStringList>
// LAYERING, SAID PRECISELY: QtCore only, and <QtGlobal> IS QtCore — it is
// what pulls qlogging.h in (qglobal.h:48), the declarations qWarning() and
// qPrintable() live behind. The model header reports the one thing a pure
// model may not swallow: a hand-edited conf row it refused to apply. No
// QWidget, no QSettings, no config file, no main.cpp.
#include <QtGlobal>

#include <cstdint>
#include <optional>

#include "monster/monster_types.h"
#include "ui/panel_sections.h"

namespace mhw {

// ---- the three mother panels, as parent slots --------------------------
//
// Only panels that HAVE a split table appear here. pets has no
// kDamageSplitTargets-style row list (see PetDamageSection), so it has
// no parent slot and a plan can neither detach nor retain anything for
// it — an out-of-range parent answers "nothing detached" and returns the
// mask untouched rather than silently claiming a row.
inline constexpr int kLayoutParentPlayer  = 0;
inline constexpr int kLayoutParentMonster = 1;
inline constexpr int kLayoutParentDamage  = 2;
inline constexpr int kLayoutParentCount   = 3;

// The row count of one parent's split table, from the TABLE itself —
// adding a row upstream (kCount + 1) widens the plan without a second
// number being written here. Unknown parent -> 0 rows.
[[nodiscard]] constexpr int layoutParentRowCount(int parentSlot) noexcept
{
    switch (parentSlot) {
    case kLayoutParentPlayer:
        return PlayerSection::PlayerSplitSlot::kCount;
    case kLayoutParentMonster:
        return MonsterSplitSlot::kCount;
    case kLayoutParentDamage:
        return DamageSplitSlot::kCount;
    }
    return 0;
}

// ---- the registry ------------------------------------------------------
//
// One entry per split-table row: which mother panel it was carved out
// of, which row of that panel's table claims it, and the settingsKey
// (exact spelling — the same string the window's own panels.ini group
// uses, see Panel::settingsGroup()) the row is stored and parsed as.
struct WindowLayoutRow {
    int parentSlot;
    int tableRow;
    const char *settingsKey;
};

constexpr WindowLayoutRow kWindowLayoutRows[] = {
    // ---- kPlayerSplitTargets, 6 rows ----
    {kLayoutParentPlayer, PlayerSection::PlayerSplitSlot::Status,
     "player-status"},
    {kLayoutParentPlayer, PlayerSection::PlayerSplitSlot::Mantles,
     "player-mantles"},
    {kLayoutParentPlayer, PlayerSection::PlayerSplitSlot::Wirebug,
     "player-wirebug"},
    {kLayoutParentPlayer, PlayerSection::PlayerSplitSlot::Bars,
     "player-bars"},
    {kLayoutParentPlayer, PlayerSection::PlayerSplitSlot::Weapon,
     "player-weapon"},
    {kLayoutParentPlayer, PlayerSection::PlayerSplitSlot::Info,
     "player-info"},
    // ---- kDamageSplitTargets, 3 rows (declaration order: panel_sections.h
    //      declares the damage table BEFORE the monster one) ----
    {kLayoutParentDamage, DamageSplitSlot::Share, "damage-share"},
    {kLayoutParentDamage, DamageSplitSlot::Chart, "damage-chart"},
    {kLayoutParentDamage, DamageSplitSlot::Rows, "damage-rows"},
    // ---- kMonsterSplitTargets, 2 rows ----
    {kLayoutParentMonster, MonsterSplitSlot::Identity, "monster-identity"},
    {kLayoutParentMonster, MonsterSplitSlot::Conditions,
     "monster-conditions"},
};

constexpr int kWindowLayoutRowCount =
    static_cast<int>(sizeof(kWindowLayoutRows)
                     / sizeof(kWindowLayoutRows[0]));

// Bit i of a plan's row mask = kWindowLayoutRows[i] is detached. The
// registry must stay smaller than a uint32_t or the whole storage model
// has to change rather than quietly wrap.
static_assert(kWindowLayoutRowCount < 32,
              "window_layout_plan packs the registry into a uint32_t");

// ---- lookup helpers ----------------------------------------------------

// Exact spelling only. "PLAYER-BARS" is not a key and is not folded into
// one: a plan is written by hand-editable conf rows and by a command
// line, and a near-miss silently docking would be the same defect as the
// old --layout=mixed falling back to Single.
[[nodiscard]] inline int windowLayoutRowIndexByKey(const QString &key)
{
    for (int i = 0; i < kWindowLayoutRowCount; ++i)
        if (key == QLatin1String(kWindowLayoutRows[i].settingsKey))
            return i;
    return -1;
}

// (parent, row) -> registry index, or -1 when that combination is not a
// row of that parent's table. Out of range is answered "no such row"
// rather than clamped, so detached() can never light up a neighbour.
[[nodiscard]] constexpr int windowLayoutRowIndex(int parentSlot,
                                                 int tableRow) noexcept
{
    if (parentSlot < 0 || parentSlot >= kLayoutParentCount)
        return -1;
    if (tableRow < 0 || tableRow >= layoutParentRowCount(parentSlot))
        return -1;
    for (int i = 0; i < kWindowLayoutRowCount; ++i)
        if (kWindowLayoutRows[i].parentSlot == parentSlot
            && kWindowLayoutRows[i].tableRow == tableRow)
            return i;
    return -1;
}

// The row's claim out of the REAL table — never a bit spelled twice.
[[nodiscard]] constexpr uint32_t windowLayoutRowMask(int parentSlot,
                                                     int tableRow) noexcept
{
    switch (parentSlot) {
    case kLayoutParentPlayer:
        if (tableRow >= 0 && tableRow < PlayerSection::PlayerSplitSlot::kCount)
            return PlayerSection::kPlayerSplitTargets[tableRow].mask;
        return 0u;
    case kLayoutParentMonster:
        if (tableRow >= 0 && tableRow < MonsterSplitSlot::kCount)
            return kMonsterSplitTargets[tableRow].mask;
        return 0u;
    case kLayoutParentDamage:
        if (tableRow >= 0 && tableRow < DamageSplitSlot::kCount)
            return kDamageSplitTargets[tableRow].mask;
        return 0u;
    }
    return 0u;
}

constexpr uint32_t kWindowLayoutAllRows =
    (uint32_t{1} << kWindowLayoutRowCount) - 1u;

// ---- the plan ----------------------------------------------------------

// A set of detached registry rows, stored as one bit per registry entry.
//
// Default-constructed = EMPTY = every row merged into its mother, which
// is the Single topology and also "no data yet". It is the safe default
// because a plan that means nothing must not carve windows out of a
// panel nobody asked to split.
struct WindowLayoutPlan {
    WindowLayoutPlan() = default;

    // Is this row its own window? An unknown (parent, row) is always
    // false — a plan never invents a row.
    [[nodiscard]] bool detached(int parentSlot, int tableRow) const noexcept
    {
        const int idx = windowLayoutRowIndex(parentSlot, tableRow);
        return idx >= 0 && (rows_ & (uint32_t{1} << idx)) != 0u;
    }

    // What the mother panel still draws: the user's own mask minus the
    // claims of the rows it gave up. Untouched bits — including the
    // OtherMembers / Tenderize filter bits, which no row claims — stay
    // with the mother, so retained ∪ (rows' claims) covers original and
    // the two are disjoint by construction.
    //
    // An unknown parent removes nothing and returns originalMask, so a
    // parent that has no table (pets, or a bad slot) degrades to
    // "nothing was detached here" instead of to an emptied panel.
    [[nodiscard]] uint32_t retainedMask(int parentSlot,
                                        uint32_t originalMask) const noexcept
    {
        uint32_t removed = 0u;
        for (const WindowLayoutRow &row : kWindowLayoutRows) {
            if (row.parentSlot != parentSlot)
                continue;
            if (detached(row.parentSlot, row.tableRow))
                removed |= windowLayoutRowMask(row.parentSlot, row.tableRow);
        }
        return originalMask & ~removed;
    }

    // Detached keys in CANONICAL (registry) order — never QSet/hash
    // order, never input order. Empty plan -> empty list.
    [[nodiscard]] QStringList detachedKeys() const
    {
        QStringList keys;
        keys.reserve(kWindowLayoutRowCount);
        for (const WindowLayoutRow &row : kWindowLayoutRows) {
            if (detached(row.parentSlot, row.tableRow))
                keys << QString::fromLatin1(row.settingsKey);
        }
        return keys;
    }

    // Unknown key: false, and NOTHING is normalised into place — no case
    // folding, no nearest-match, no write. `on == false` on a known key
    // is always true (clearing an already-clear row is not an error).
    bool setDetached(const QString &settingsKey, bool on)
    {
        const int idx = windowLayoutRowIndexByKey(settingsKey);
        if (idx < 0)
            return false;
        const uint32_t bit = uint32_t{1} << idx;
        if (on)
            rows_ |= bit;
        else
            rows_ &= ~bit;
        return true;
    }

    bool operator==(const WindowLayoutPlan &) const = default;

    // Every row detached — Split's default, and Mixed's safe fallback
    // when nothing has been configured. Built from the registry so a new
    // row is detached by default too, rather than silently merged.
    [[nodiscard]] static WindowLayoutPlan allDetached() noexcept
    {
        return WindowLayoutPlan(kWindowLayoutAllRows);
    }

private:
    explicit WindowLayoutPlan(uint32_t rows) noexcept : rows_(rows) {}

    uint32_t rows_{0u};
};

// ---- the mode -> default plan ------------------------------------------
//
// Single and Split stay the quick presets they have always been: their
// plans are fixed and are NOT read from (and do not write) the Mixed
// conf row. That is what keeps `--layout single` a promise instead of a
// suggestion.
[[nodiscard]] inline WindowLayoutPlan
defaultWindowLayoutPlan(PlayerSection::PlayerLayoutMode mode)
{
    if (mode == PlayerSection::PlayerLayoutMode::Single)
        return WindowLayoutPlan();
    // Split AND Mixed-with-no-data: all rows detached. For Mixed that is
    // the VISIBLE fallback — a Mixed run with nothing configured must
    // not render the Single topology while claiming to be Mixed.
    return WindowLayoutPlan::allDetached();
}

// ---- parse / serialise -------------------------------------------------

// "player-bars,monster-conditions" -> the plan.
//
//   * the EMPTY string is a legal, explicit "nothing detached" (the row
//     `layout.windows.world=` means ALL MERGED);
//   * anything else is a comma list whose tokens are trimmed and must
//     each be a known, non-duplicate key. An empty token ("a,,b"), an
//     unknown key and a duplicate are all REJECTED — and rejected
//     ATOMICALLY: *ok = false and the returned plan is empty, so a
//     half-applied list can never depend on where the bad token sat.
//     Deterministic on purpose: conf rows are hand-editable, and a
//     partial read would make "which windows are up" depend on a typo's
//     position rather than on the user's intent.
//   * a whitespace-only string is NOT the empty string (the empty row is
//     "" and nothing else).
//
// The caller decides what ok=false means: resolveWindowLayoutPlan()
// falls back to the mode default; a CLI pre-parse reports it to the user.
//
// NOT [[nodiscard]] on purpose: a caller that only wants to know whether
// a hand-edited row was well formed (without using the plan) is a real
// caller — the flag IS the answer it came for.
inline WindowLayoutPlan
parseWindowLayoutPlan(const QString &commaKeys, bool *ok = nullptr)
{
    WindowLayoutPlan plan;
    bool good = true;

    if (!commaKeys.isEmpty()) {
        uint32_t seen = 0u;
        const QStringList tokens = commaKeys.split(QLatin1Char(','));
        for (const QString &raw : tokens) {
            const QString token = raw.trimmed();
            const int idx = token.isEmpty() ? -1
                                            : windowLayoutRowIndexByKey(token);
            if (idx < 0 || (seen & (uint32_t{1} << idx)) != 0u) {
                good = false;
                break;
            }
            seen |= uint32_t{1} << idx;
            plan.setDetached(token, true);
        }
        if (!good)
            plan = WindowLayoutPlan();
    }

    if (ok)
        *ok = good;
    return plan;
}

// The plan -> "player-bars,monster-conditions": registry order, comma
// separated, empty list as the empty string. Stable across runs and
// across hash-seed changes, because it walks the registry.
[[nodiscard]] inline QString
serializeWindowLayoutPlan(const WindowLayoutPlan &plan)
{
    return plan.detachedKeys().join(QLatin1Char(','));
}

// ---- the game-specific conf row -----------------------------------------

// The conf key this game stores its plan under: layout.windows.world /
// layout.windows.rise. Two games, two rows, one file — the same
// isolation the [world/…] / [rise/…] settings sections give geometry.
[[nodiscard]] inline QString layoutWindowsRowKey(GameId game)
{
    switch (game) {
    case GameId::World:
        return QStringLiteral("layout.windows.world");
    case GameId::Rise:
        return QStringLiteral("layout.windows.rise");
    }
    // Unreachable for a valid GameId. An empty key rather than World's:
    // a third enumerator would then produce "=" (an unparseable line)
    // instead of silently reading World's saved list.
    return QString();
}

// One complete line, trailing newline included:
// "layout.windows.world=player-bars,monster-conditions\n".
//
// The empty plan is still written ("layout.windows.world=\n"), because
// an absent row means "nothing configured -> Mixed's visible fallback"
// while an empty row means "the user wants everything merged". Those are
// two different states and only serialisation keeps them apart.
//
// The CONSUMER replaces exactly this game's row in the conf text and
// leaves every other line — the other game's row, unknown keys, locale
// rows — byte for byte alone. That is the codec's row rule, applied to
// this row.
[[nodiscard]] inline QByteArray
serializeWindowLayoutPlanRow(GameId game, const WindowLayoutPlan &plan)
{
    QByteArray out = layoutWindowsRowKey(game).toLatin1();
    out += '=';
    out += serializeWindowLayoutPlan(plan).toLatin1();
    out += '\n';
    return out;
}

namespace window_layout_plan_detail {

// ---- D-5: a conf row that does not parse must SAY SO, once -------------
//
// Reading past an unusable row is the right READ rule (parseConsoleLayoutRow
// scans past it too: the FIRST usable row for this game wins) and it was
// the wrong LOG rule: the user hand-edited `layout.windows.<game>=`, the
// line never took effect, and nothing at all reached the log — the run
// silently landed on Mixed's all-detached fallback instead of their list.
// That is the same defect class as a swallowed CLI flag.
//
// FREQUENCY, because resolveWindowLayoutPlan() is called from more than
// one place and may be called again on a mode/game change: each DISTINCT
// bad line is reported ONCE per process (a bounded de-dup list, not a
// per-call guard), and the list is capped at kMaxReportedUnusableConfRows
// so a caller that somehow re-reads an ever-changing file cannot turn one
// typo into a flood. The cap itself is announced the first time it bites
// and then stays quiet. Tests reach this state directly (clear the list)
// because a process-global de-dup must be resettable to be assertable.
inline QStringList reportedUnusableConfRows;
inline bool unusableConfRowsCapReported = false;
inline constexpr int kMaxReportedUnusableConfRows = 16;

inline void reportUnusableConfRow(const QString &line)
{
    if (reportedUnusableConfRows.contains(line))
        return; // already said so for this exact line
    if (reportedUnusableConfRows.size() >= kMaxReportedUnusableConfRows) {
        if (!unusableConfRowsCapReported) {
            unusableConfRowsCapReported = true;
            qWarning("Ignoring further invalid layout.windows conf rows: "
                     "%d distinct ones already reported, the rest stay "
                     "silent for this run",
                     kMaxReportedUnusableConfRows);
        }
        return;
    }
    reportedUnusableConfRows.append(line);
    // The line itself names the key it lives under, so the report needs no
    // game parameter: `layout.windows.world=player-bars,bogus` says which
    // game and which spelling the user meant.
    qWarning("Ignoring invalid conf row '%s' (known keys only, no "
             "duplicates, no empty tokens); the line was NOT applied and "
             "the scan continues at the next usable row for this game",
             qPrintable(line));
}

} // namespace window_layout_plan_detail

// ---- resolve: cli > the game's Mixed row > the mode default -------------

// Precedence, exactly:
//
//   1. EXPLICIT cliKeys win, in every mode — an option the user typed on
//      the command line outranks a saved value. An unparseable list does
//      not win, does not abort, AND DOES NOT FALL THROUGH TO THE FILE:
//      `cliKeys.has_value()` short-circuits step 2, so a mixed-up CLI
//      list discards this game's valid `layout.windows.<game>=` row as
//      well and the run lands on the mode default. That is the rule (a
//      half-parsed CLI must not half-apply), but it is a SECOND thing the
//      user loses, and the caller that pre-validates with
//      parseWindowLayoutPlan() must say BOTH in its warning — see
//      main.cpp's "Ignoring invalid --detached-windows" qWarning, which
//      spells out that the conf row goes with it.
//   2. Only Mixed reads the conf: layout.windows.<this game>=, the FIRST
//      usable row for this game. No row for this game, or only unusable
//      rows -> the mode default (Mixed's visible ALL-detached fallback),
//      never a guessed subset. An explicit empty row means ALL MERGED.
//      An UNUSABLE row is scanned past (the read rule above) and reported
//      once by window_layout_plan_detail::reportUnusableConfRow() — a
//      silently dropped hand-edit is a defect, not a housekeeping detail.
//   3. Single and Split are the legacy quick presets. They do not read
//      the Mixed row at all, so a saved Mixed list can never leak into
//      `--layout single`.
//
// World and Rise resolve independently: reading World never returns
// Rise's list, whatever the file contains.
[[nodiscard]] inline WindowLayoutPlan
resolveWindowLayoutPlan(GameId game, PlayerSection::PlayerLayoutMode mode,
                        const QString &confText,
                        const std::optional<QString> &cliKeys = std::nullopt)
{
    // 1. Explicit CLI.
    if (cliKeys.has_value()) {
        bool ok = false;
        const WindowLayoutPlan cli = parseWindowLayoutPlan(*cliKeys, &ok);
        if (ok)
            return cli;
        return defaultWindowLayoutPlan(mode);
    }

    // 2. Only Mixed consults the file, and only its own game's row.
    if (mode != PlayerSection::PlayerLayoutMode::Mixed)
        return defaultWindowLayoutPlan(mode);

    const QString prefix = layoutWindowsRowKey(game) + QLatin1Char('=');
    const QStringList lines = confText.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.endsWith(QLatin1Char('\r'))
                                 ? rawLine.chopped(1)
                                 : rawLine;
        if (!line.startsWith(prefix))
            continue;
        bool rowOk = false;
        const WindowLayoutPlan row =
            parseWindowLayoutPlan(line.mid(prefix.size()), &rowOk);
        if (rowOk)
            return row;      // first USABLE row wins, mirroring
        //                        parseConsoleLayoutRow: keep scanning past
        //                        a line that does not parse — but SAY so
        //                        once, or the user's own hand-edited line
        //                        dies without a trace in the log.
        window_layout_plan_detail::reportUnusableConfRow(line);
    }
    return defaultWindowLayoutPlan(mode);
}

// ---- the one rule the overlay's mount gate reads -------------------------

// enabled = !disabledByFlag && (the retained mask still has a block to
// draw || nothing was detached from this parent).
//
// The second arm is what reproduces seed Single: a maskless single panel
// still mounts (`mode != Split || hasBlocks`), while a Split/Mixed
// mother left holding only a filter bit — Tenderize, OtherMembers — does
// not mount an 18px empty frame. --no-player still wins outright.
[[nodiscard]] inline bool planMotherPanelEnabled(const WindowLayoutPlan &plan,
                                                 int parentSlot,
                                                 bool disabledByFlag,
                                                 bool maskHasBlocks)
{
    if (disabledByFlag)
        return false;
    if (maskHasBlocks)
        return true;
    for (const WindowLayoutRow &row : kWindowLayoutRows) {
        if (row.parentSlot != parentSlot)
            continue;
        if (plan.detached(row.parentSlot, row.tableRow))
            return false;
    }
    return true;
}

// ---- the registry is checked against the tables it indexes ---------------
namespace window_layout_plan_detail {

// Every (parent, tableRow) of every parent appears exactly once: a
// coverage bitmap per parent that must equal (rowCount bits set), with
// every row inside range. A new upstream row that nobody added here
// fails the build instead of silently staying merged forever.
constexpr bool coversEveryRowExactlyOnce()
{
    for (int parent = 0; parent < kLayoutParentCount; ++parent) {
        uint32_t seen = 0u;
        for (const WindowLayoutRow &row : kWindowLayoutRows) {
            if (row.parentSlot != parent)
                continue;
            if (row.tableRow < 0
                || row.tableRow >= layoutParentRowCount(parent))
                return false;
            const uint32_t bit = uint32_t{1} << row.tableRow;
            if ((seen & bit) != 0u)
                return false; // claimed twice
            seen |= bit;
        }
        const int rows = layoutParentRowCount(parent);
        const uint32_t want = (uint32_t{1} << rows) - 1u;
        if (seen != want)
            return false;
    }
    return true;
}

// No row's claim may spill outside its panel's own section mask, and two
// rows of one parent may never claim the same bit — the disjointness the
// mother/detached union proof needs, re-checked at the registry level.
constexpr bool claimsAreInsideAndDisjoint()
{
    for (int parent = 0; parent < kLayoutParentCount; ++parent) {
        uint32_t kAll = 0u;
        switch (parent) {
        case kLayoutParentPlayer:
            kAll = PlayerSection::kAll;
            break;
        case kLayoutParentMonster:
            kAll = MonsterSection::kAll;
            break;
        case kLayoutParentDamage:
            kAll = DamageSection::kAll;
            break;
        }
        uint32_t unionOfClaims = 0u;
        uint32_t sumOfClaims   = 0u;
        for (const WindowLayoutRow &row : kWindowLayoutRows) {
            if (row.parentSlot != parent)
                continue;
            const uint32_t m =
                windowLayoutRowMask(row.parentSlot, row.tableRow);
            if (m == 0u)
                return false;                        // an empty row
            if ((m & ~kAll) != 0u)
                return false;                        // outside the panel
            unionOfClaims |= m;
            sumOfClaims += m;
        }
        if (unionOfClaims != sumOfClaims)
            return false; // some bit claimed by two rows
    }
    return true;
}

} // namespace window_layout_plan_detail

static_assert(window_layout_plan_detail::coversEveryRowExactlyOnce(),
              "every split-table row must have exactly one registry entry");
static_assert(window_layout_plan_detail::claimsAreInsideAndDisjoint(),
              "registry row masks must stay inside their panel's kAll and "
              "be pairwise disjoint");

} // namespace mhw
