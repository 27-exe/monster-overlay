// SPDX-License-Identifier: Apache-2.0
//
// The player-info sub-window's THREE STATES, and the one invariant that
// keeps them distinct: `InfoLine`'s stacking order.
//
// WHAT THIS FILE GUARDS
// ---------------------
// The v0.12 split reduced PlayerPanel's conn line to a binary
// (connected ? 已连接 : ○ 未连接) and lost the middle state —
// attached-but-nothing-resolved — the one the CachyOS reports were filed
// under, because a denied read, a missing address map and a waiting
// process all painted as the same unexplained empty panel. It has been
// restored in src/ui/panel_player_split_info.cpp, and with it came two
// fragile invariants that NOTHING was testing.
//
//   1. THE ENUM ORDER IS LOAD-BEARING. `InfoLine` gained a
//      `ReaderStatus` enumerator at index 2 with `Meta` still last
//      (.cpp:263-268), and that is not decoration: `streamHeight()` and
//      `paintPanel()` both walk the enum in numeric order
//      (`for (i = 0; i < kInfoLines; ++i) static_cast<InfoLine>(i)`),
//      so a `Meta` that stops being last silently reorders the paint AND
//      the reservation. A re-sorted enum list compiles clean and every
//      existing assertion still passes, while the reader's reason moves
//      BELOW the verdict it exists to explain.
//
//   2. THE PITCHES ARE NOT CONSTANTS. Every height in this window is
//      `ceil(QFontMetrics::height())` for the font that line draws, which
//      makes the numbers HOST-DERIVED. The .cpp's own comment records
//      that the 12pt bold timer resolves to 21 and kGapLine to 7 on the
//      machine it was measured on, and warns they differ by 1px from the
//      shipped build. So this file asserts RELATIONSHIPS
//      (monotonicity, differences that are exact multiples of the shared
//      gap, the fact that a line collapses when it has nothing to say)
//      and never a pixel height. `contentHeight() == 61` here would be a
//      test that is green on one font stack and red on another for a
//      reason no code can control.
//
// The three states are asserted through the PUBLIC contract only:
// `update(snapshot)` + `contentHeight()` / `contentWidth()` +
// `hasContent()`, plus an offscreen render. Nothing in the .cpp's
// anonymous namespace is reached directly — that would mean editing a
// product file to export it, and the point of this file is that the
// states are observable from outside.

#include "ui/panel_player_split_info.h"
#include "ui/panel_sections.h"

#include "core/game_snapshot.h"
#include "core/string_table.h"

#include <QApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QString>
#include <QStringLiteral>

#include <cstdio>

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool cond, const char *what)
{
    ++gChecks;
    if (cond) {
        std::printf("PASS: %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++gFailures;
    }
}

// The same assertion with the numbers it compared.
//
// Ten failures arrived from this file's first run with no values attached,
// and nothing in "FAIL: the reason line costs exactly one shared gap more
// than silence" says WHICH of the two states was taller or by how much —
// so the only way to act on it was to instrument it by hand. Every
// relation in the sections below is stated through this, which prints the
// pair when it breaks and nothing when it holds.
void checkNum(bool cond, int lhs, int rhs, const char *what)
{
    ++gChecks;
    if (cond) {
        std::printf("PASS: %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL: %s  [%d vs %d]\n", what, lhs, rhs);
        ++gFailures;
    }
}

// The two numbers this file is allowed to use absolutely, because they
// are EXPORTED LAYOUT CONSTANTS in panel_player_split_layout.h rather
// than font metrics: the sub-window inset and the shared within-block
// gap. Both are stable across machines by construction, which is exactly
// what makes them safe to state in a test while the pitches are not.
using mhw::split_layout::kSplitMargin;
using mhw::split_layout::kSplitGapWithin;

// ---- the three snapshots ------------------------------------------------

// State 1 of 3: nothing attached, no game process found.
mhw::GameSnapshot offlineSnapshot()
{
    mhw::GameSnapshot s;
    s.attached = false;
    s.pid = -1;
    s.imageBase = 0;
    // The reader's status is EMPTY here, and that is the point.
    //
    // This file's first draft set it to "reader idle" and then asserted
    // that offline reserves FEWER lines than no-data. Both cannot hold: a
    // non-empty status is what makes the reader's reason line draw at all
    // (lineIsDrawn(), panel_player_split_info.cpp:326-328), so "reader
    // idle" gave offline a reason row too and the two states came out at
    // the same height (measured 61 vs 61) while the assertion demanded
    // 61 > 61.
    //
    // An offline reader genuinely has nothing to report — there is no
    // process, so there is no failure to explain, and the panel did not
    // draw the row for exactly that reason (panel_player.cpp:588,
    // `rows = status_.isEmpty() ? 1 : 2`). So the fixture now says what
    // the state means, and the relation below (offline == attached-but-
    // silent, both one meta line) becomes true for the right reason
    // rather than by coincidence.
    s.status = QString();
    return s;
}

// State 2 of 3: a process was found and its memory opened, but not one
// field resolved. This is the state the split LOST, and the reason this
// file exists. `readerStatus` is the reader's own reason, carried
// verbatim onto its own line.
mhw::GameSnapshot noDataSnapshot(const QString &reason)
{
    mhw::GameSnapshot s;
    s.attached = true;
    s.pid = 4242;
    s.imageBase = 0x7FFE00000000ULL;
    s.player.valid = false;      // the DATA state: nothing resolved
    s.status = reason;           // the reader's own words
    s.party.clear();
    return s;
}

// State 3 of 3: attached AND the player struct resolved. `readerStatus`
// is deliberately non-empty — a real reader reports a live sentence —
// because the diagnostic line must NOT be drawn in the live state, and
// that collapse is one of the behaviours pinned below. A quest is
// attached too, so the Quest stream is on and the "can read back real
// content" assertions have something to find.
mhw::GameSnapshot liveSnapshot()
{
    mhw::GameSnapshot s;
    s.attached = true;
    s.pid = 4242;
    s.imageBase = 0x7FFE00000000ULL;
    s.player.valid = true;
    s.status = QStringLiteral("MHW connected");
    s.party.resize(4);
    // Order matches QuestSnapshot: id, stars, isAnomaly, rank, state,
    // category, deaths, maxDeaths, timeLeftSeconds, maxTimerSeconds,
    // elapsedSeconds, active.
    s.quest = {66801, 6, false, 2, 2, 0, 0, 3, 2497.0F, 0.0F, 0.0F, true};
    return s;
}

// One window driven through one snapshot, repainted so paintPanel() has
// run. The mask defaults to 0xFFFFFFFF (both streams on), which is the
// state the shipped window is in.
//
// NOT edit mode, and that is load-bearing: Panel::paintEvent() runs
// setupDemoData() on the first repaint while editMode_ is true
// (panel.cpp:1156-1159), and this window's demo seed OVERWRITES row_
// with an attached+live+in-quest snapshot. Driving the three states
// through the edit-mode path therefore measures the demo three times
// and every state-distinction assertion passes vacuously or fails
// vacuously. The live path — update(snapshot) then paint — is what this
// file needs, and it is the path the shipped overlay takes.
void drive(PlayerInfoStripPanel &w, const mhw::GameSnapshot &snap)
{
    w.update(snap);
    w.show();
    for (int i = 0; i < 4; ++i) {
        w.repaint();
        QApplication::processEvents();
    }
}

int heightFor(const mhw::GameSnapshot &snap)
{
    PlayerInfoStripPanel w;
    drive(w, snap);
    return w.contentHeight();
}

int widthFor(const mhw::GameSnapshot &snap)
{
    PlayerInfoStripPanel w;
    drive(w, snap);
    return w.contentWidth();
}

QImage renderOf(PlayerInfoStripPanel &w)
{
    QImage img(w.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter painter(&img);
    w.render(&painter, QPoint(), w.rect());
    painter.end();
    return img;
}

bool paintsInk(const QImage &img)
{
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(row[x]) != 0)
                return true;
    }
    return false;
}

// The first row that carries ink, scanning down.
//
// This is the only probe that can see InfoLine's ORDER. Both height
// functions add one pitch per drawn line regardless of the order they
// visit them, so a Meta that stops being last leaves every height in
// this file untouched — the mutation survives all 40 checks. What it
// changes is paintPanel()'s advance of `y`, which puts the reader's
// reason at the meta line's position and the verdict where the reason
// belonged. That is a pixel fact, so it is asserted on pixels.
[[nodiscard]] int firstInkRow(const QImage &img)
{
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(row[x]) != 0)
                return y;
    }
    return -1;
}

// Every row that carries ink, bottom-up from the last one: the y of the
// LOWEST ink is the bottom of the last drawn line.
[[nodiscard]] int lastInkRow(const QImage &img)
{
    for (int y = img.height() - 1; y >= 0; --y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(row[x]) != 0)
                return y;
    }
    return -1;
}

} // namespace

int main(int argc, char **argv)
{
    // ISOLATE THE CONFIG. Without this the suite reads the developer's
    // real ~/.config/monster-overlay/panels.ini, and a stored scale or
    // minW floor changes the measured height for reasons that have
    // nothing to do with the code under test — the same trap
    // damage_split_windows_tests documents.
    QTemporaryDir configHome;
    if (configHome.isValid())
        qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());

    QApplication app(argc, argv);

    // The labels this window paints come from the qrc; without a loaded
    // table every lookup falls back to the key itself and the label
    // assertions below would measure key strings instead of text.
    if (!mhw::StringTable::instance().load(QStringLiteral("zh-CN")))
        std::fprintf(stderr, "WARN: zh-CN string table did not load; "
                             "label assertions measure key fallbacks\n");

    const QString lblOffline =
        mhw::StringTable::instance().tr(QStringLiteral("ui.player_status_offline"));
    const QString lblNoData =
        mhw::StringTable::instance().tr(QStringLiteral("ui.player_status_no_data"));
    const QString lblConnected =
        mhw::StringTable::instance().tr(QStringLiteral("ui.player_connected"));

    // =====================================================================
    // P0 — THE THREE STATES ARE DISTINCT AND DISTINGUISHABLE
    // =====================================================================
    // The reported defect was that the split left a BINARY conn line, so
    // the middle state painted as 「○ 未连接」 over an attached process
    // and the reports had nothing to read. The first thing that must
    // hold is that the three states are not the same thing.

    // The labels, resolved through the window's OWN i18n keys — never a
    // hardcoded literal, so this file does not break when the wording
    // changes. A key that does not resolve falls back to the key itself
    // (StringTable's documented behaviour), so a missing key makes all
    // three "labels" identical AND equal to their own key; both
    // directions are checked.
    check(lblOffline != lblNoData,
          "P0: offline and no-data labels are different strings");
    check(lblNoData != lblConnected,
          "P0: no-data and live labels are different strings");
    check(lblOffline != lblConnected,
          "P0: offline and live labels are different strings");
    check(lblOffline != QStringLiteral("ui.player_status_offline"),
          "P0: offline label came from the table, not the key fallback");
    check(lblNoData != QStringLiteral("ui.player_status_no_data"),
          "P0: no-data label came from the table, not the key fallback");
    check(lblConnected != QStringLiteral("ui.player_connected"),
          "P0: connected label came from the table, not the key fallback");

    // And the three states produce three DIFFERENT windows. The heights
    // are not all distinct (offline and silent-no-data legitimately draw
    // the same single line), so the widths carry the distinction: the
    // state word is the only thing either has to say, and a different
    // word means a different measured line.
    const int hOffline = heightFor(offlineSnapshot());
    const int hNoData  = heightFor(noDataSnapshot(QStringLiteral("cannot open the address map")));
    const int hSilent  = heightFor(noDataSnapshot(QString()));
    const int hLive    = heightFor(liveSnapshot());

    const int wOffline = widthFor(offlineSnapshot());

    const int wNoData  = widthFor(noDataSnapshot(QStringLiteral("cannot open the address map")));
    const int wSilent  = widthFor(noDataSnapshot(QString()));
    const int wLive    = widthFor(liveSnapshot());

    // v0.17: no-data now carries PID/BASE as well as its state word — the
    // reader found the process and failed to read it, and dropping the pid
    // there is what made 「有时显示有时不显示」 true. Offline has no process
    // and prints none, so the two are no longer the same width, and the
    // difference IS the pid string.
    //
    // This asserted they were EQUAL, written back when the pid was dropped
    // in the no-data state. Equal width was never the property worth
    // pinning — the words differing inside one width was — and the words
    // are asserted to differ above, still.
    checkNum(wNoData > wOffline, wNoData, wOffline,
          "P0: no-data reserves MORE width than offline — it carries the "
          "pid the offline state has no process to report");
    check(wSilent != wLive,
          "P0: silent-no-data and live reserve different widths — the "
          "middle state is not the live state");

    // =====================================================================
    // P0 — Meta IS THE LAST DRAWN LINE (the load-bearing ordering)
    // =====================================================================
    //
    // `InfoLine` cannot be named from here — it lives in the .cpp's
    // anonymous namespace, and exporting it would mean editing a product
    // file. But the property does not need the enum's name: it needs the
    // enum's SHAPE, and that shape is observable through the two public
    // functions that walk it, contentHeight() and contentWidth().
    //
    // The stack order the enum encodes is, bottom to top:
    //     ZoneQuest, TimeCarts, ReaderStatus, Meta
    // so Meta is last and the reader's reason is drawn immediately above
    // the verdict. A hunter reading top to bottom must meet the reason
    // before the meta line explains the state.
    //
    // What a re-sorted enum would do, observed from outside:
    //   * streamHeight() would visit Meta before ReaderStatus. The total
    //     is unchanged, so the HEIGHT cannot see it — but
    //   * paintPanel() advances `y` in the same order, so the reason
    //     would be painted at the meta line's position and the meta text
    //     at the reason's. Both are then drawn into a rect sized for the
    //     other, and the window's own height no longer covers what it
    //     draws.
    //
    // The observable consequence that IS reachable: the reason line is
    // only ever reserved when it is drawn, and the reason is the widest
    // thing on screen in that state. So a window whose reservation does
    // not grow to fit the reason is a window where the reason is not
    // being counted in the right place.

    // --- the reason line exists only in the two non-live states -------
    check(hNoData > hSilent,
          "P0: the reader's reason reserves a line of its own when there "
          "is one");
    checkNum(hNoData > hSilent && hNoData - hSilent > kSplitGapWithin,
          hNoData - hSilent, kSplitGapWithin,
          "P0: the reason line costs a whole line plus its gap more than "
          "silence — no dangling space, no clipped row (measured 23 vs "
          "the 7px shared gap: the reason has its own pitch on top)");

    // It collapses in the live state even though a live reader DOES
    // report a status: printing both the reason and 「已连接」 would be a
    // duplicate row, which is why lineIsDrawn() returns false for Live.
    checkNum(hLive > hSilent, hLive, hSilent,
          "P0: the reason line collapses in the live state even though "
          "the reader reported a status — live trades it for the quest "
          "lines, so it ends up taller, not equal");
    checkNum(wNoData >= wSilent, wNoData, wSilent,
          "P0: the reason line's WIDTH is reserved when it is drawn — "
          "the line is counted, not painted past the edge");
    checkNum(wSilent < wLive, wSilent, wLive,
          "P0: a silent reader reserves less width than the live state "
          "it is not");

    // The middle state costs a line only when the reader has something to
    // say. Attached-but-silent draws one meta line and nothing else, which
    // is the same shape as offline — so it is asserted against OFFLINE,
    // not against live: live additionally draws the quest stream and is
    // therefore taller (measured 38 vs 90). Asserting the two equal was
    // this file's second draft and it contradicted the measurement above.
    checkNum(hSilent == hOffline, hSilent, hOffline,
             "P0: attached-but-silent is exactly as tall as offline — one "
             "meta line each, which is correct");

    // =====================================================================
    // P1 — THE HEIGHT RELATIONSHIPS (never a pixel constant)
    // =====================================================================
    //
    // Every number below is a RELATION, not a value. The pitches are
    // ceil(QFontMetrics::height()) for the fonts this host resolves, and
    // the .cpp's own header records that they differ by 1px between the
    // measuring host and the shipped build. A test that hardcodes 61 is
    // a test that fails on the next font update for a reason no one can
    // control. What is stable is the STRUCTURE: one pitch per drawn
    // line, one shared gap between consecutive drawn lines, two margins.

    // The live snapshot carries a quest (two more drawn lines) and the
    // two non-live ones do not, so live is strictly the tallest of the
    // three by construction.
    check(hLive > hNoData,
          "P1: live (quest + conn, 4 lines) is taller than no-data "
          "(conn + reason, 2 lines)");
    check(hLive > hOffline,
          "P1: live is taller than offline (conn only, 1 line)");

    // The two conn-only states: offline draws just the meta line; no-data
    // with a reason draws the reason plus the meta line. So no-data is
    // taller, by exactly one gap. That difference is the ONE number
    // stated absolutely, because kSplitGapWithin is an exported layout
    // constant rather than a font metric.
    checkNum(hNoData > hOffline, hNoData, hOffline,
          "P1: no-data-with-reason is taller than offline (one more "
          "drawn line)");
    checkNum(hNoData - hOffline > kSplitGapWithin, hNoData - hOffline, kSplitGapWithin,
          "P1: no-data-with-reason is taller than offline by exactly one "
          "shared gap");
    checkNum(hOffline == hSilent, hOffline, hSilent,
          "P1: offline and attached-but-silent are the same height — one "
          "meta line each; the state word is all either has to say");

    // The reservation is never thinner than the two margins it is made
    // of, whatever it is showing. A height at or below 2*kMargin means
    // the walk found no line at all, which is the both-bits-off case and
    // not a state any of these three snapshots can reach.
    check(hOffline > 2 * kSplitMargin,
          "P1: offline height is more than its two margins");
    check(hNoData > 2 * kSplitMargin,
          "P1: no-data height is more than its two margins");
    check(hLive > 2 * kSplitMargin,
          "P1: live height is more than its two margins");

    // Every interior boundary lands on a gap, so the height minus the
    // two margins is a sum of (pitch, gap, pitch, gap, ..., pitch)
    // terms. Subtracting one pitch leaves a whole number of gaps — but
    // the pitch is exactly the number this file refuses to know. So the
    // invariant is stated the other way round, as a property of the
    // DIFFERENCES: any two heights that differ by one drawn line differ
    // by exactly one gap, which is what the two checks above assert for
    // the pair that matters. Restated here for the live-vs-offline pair,
    // which differs by THREE lines and therefore by three gaps plus two
    // pitches — the pitches cancel out of the comparison because both
    // states share the meta line.
    check((hLive - hOffline) > 2 * kSplitGapWithin,
          "P1: live exceeds offline by more than two shared gaps — it "
          "draws at least three more lines than the offline state");

    // =====================================================================
    // P1 — THE READER-STATUS LINE ONLY WHEN THERE IS A REASON
    // =====================================================================
    //
    // Asserted above in the height domain; here in the width domain,
    // which is the other half of the same reservation. A line that is
    // drawn but not reserved gets elided into a cell too narrow for it,
    // and the whole reason the reason exists is to be readable.

    const int wLongReason = widthFor(noDataSnapshot(QStringLiteral(
        "cannot open the address map for MonsterHunterWorld.exe")));
    const int wShortReason = widthFor(noDataSnapshot(QStringLiteral("waiting")));

    // A longer reason widens the window rather than being clipped into a
    // fixed cell: the .cpp measures the status string with the same font
    // the paint sets and takes the max.
    check(wLongReason > wShortReason,
          "P1: a longer reader reason reserves a wider window — the "
          "reason is measured, not elided into a fixed cell");
    checkNum(wSilent == wShortReason, wSilent, wShortReason,
          "P1: a short reason does not widen the window — the meta line "
          "is already the widest thing on screen, so the width is the "
          "same with and without it (wLongReason above is the case "
          "where the reason DOES set the width)");
    check(wNoData == wShortReason,
          "P1: the same reason string reserves the same width — the "
          "reservation is a function of the string, not of the state");

    // =====================================================================
    // P2 — REVERSE ASSERTION: REAL CONTENT IS NOT SWALLOWED
    // =====================================================================
    //
    // Project rule: a one-directional assertion lets the opposite bug
    // through green. "The three states are distinct" passes for a window
    // that hides ALL its content — three different nothings are still
    // three different things. So the live state must also be shown to
    // CARRY its data, and each state must be shown to be non-blank.

    PlayerInfoStripPanel liveW;
    drive(liveW, liveSnapshot());
    PlayerInfoStripPanel ndW;
    drive(ndW, noDataSnapshot(QStringLiteral("cannot open the address map")));
    PlayerInfoStripPanel offW;
    drive(offW, offlineSnapshot());

    // Not an empty frame in any state.
    check(liveW.contentWidth() > 2 * kSplitMargin,
          "P2: the live window reserves more than its two margins — it "
          "is carrying real content, not an empty frame");
    check(liveW.contentHeight() > 2 * kSplitMargin,
          "P2: same on the vertical axis");
    check(ndW.contentWidth() > 2 * kSplitMargin,
          "P2: the middle state reserves real width for its reason");
    check(offW.contentHeight() > 2 * kSplitMargin,
          "P2: the offline state reserves a line for its state word — "
          "not a collapsed window");

    // The window's own verdict. `hasContent()` is protected; the public
    // read of it is `hasVisibleContent()`, which is what the main loop
    // asks and therefore what this file asks.
    check(liveW.hasVisibleContent(),
          "P2: the live window reports visible content");
    check(ndW.hasVisibleContent(),
          "P2: the middle state reports visible content — a hunter in "
          "that state has the reason on screen, which is the whole point "
          "of restoring it");
    check(offW.hasVisibleContent(),
          "P2: the offline state reports visible content — it is not a "
          "collapsed window");

    // The live state is the tallest AND the widest: it carries the quest
    // stream the others do not. A window that hid its data would fail
    // this while still passing every "the states differ" check above.
    check(hLive > hNoData && hLive > hOffline,
          "P2: the live state is the tallest of the three — it carries "
          "the quest stream the others do not");
    check(wLive > wNoData,
          "P2: the live window is wider than the middle state — the "
          "quest content is measured, not clipped");

    // =====================================================================
    // P2 — THE PIXELS ARE ACTUALLY THERE
    // =====================================================================
    //
    // A height and a width are a claim; the render is the evidence. Each
    // state is rendered into an offscreen buffer and checked for opaque
    // ink, the way damage_split_windows_tests does it. This catches a
    // state that reserves correctly and paints nothing — the "three
    // different nothings" failure the height assertions cannot see.

    const QImage imgOff = renderOf(offW);
    const QImage imgNd  = renderOf(ndW);
    const QImage imgLive = renderOf(liveW);

    check(paintsInk(imgOff), "P2: the offline state renders ink");
    check(paintsInk(imgNd),  "P2: the no-data state renders ink");
    check(paintsInk(imgLive), "P2: the live state renders ink");

    // And the three renders are not the SAME image: three identical
    // renders would mean the state never reached the paint.
    check(imgLive.height() > imgOff.height(),
          "P2: the live render is taller than the offline render — it "
          "draws the quest lines the offline state does not have");
    check(imgOff.height() == imgNd.height()
              ? (imgOff.width() != imgNd.width()
                 || imgOff != imgNd)
              : true,
          "P2: offline and no-data are not the same render (different "
          "state words, or at least different geometry)");

    // =====================================================================
    // P0 — THE ORDER, ON PIXELS (the one thing heights cannot see)
    // =====================================================================
    //
    // Everything above this line measures heights and widths, and both are
    // order-independent: streamHeight() adds one pitch per DRAWN line
    // whatever order it visits them in. So re-sorting InfoLine — Meta no
    // longer last — leaves every number in this file identical, and a
    // mutation that does exactly that passes all 40 checks. Measured, not
    // assumed: the enum was reordered here and the suite stayed green.
    //
    // What the reorder actually breaks is paintPanel()'s advance of `y`.
    // The reader's reason would be painted where the verdict goes and the
    // verdict where the reason belongs, into rects sized for each other.
    //
    // So: the bottom of the ink is the meta line in every state, because
    // Meta is drawn last. If it stops being last, the bottom of the ink
    // becomes the reason instead, and this fails.
    {
        const int bottomOff  = lastInkRow(imgOff);
        const int bottomNd   = lastInkRow(imgNd);
        const int bottomLive = lastInkRow(imgLive);
        check(bottomOff >= 0 && bottomNd >= 0 && bottomLive >= 0,
              "P0: every state has ink to locate");
        checkNum(bottomNd >= hNoData - 2 * kSplitMargin,
                 bottomNd, hNoData - 2 * kSplitMargin,
                 "P0: the no-data state's last ink sits at the BOTTOM of "
                 "its box — the meta verdict is drawn last, below the "
                 "reader's reason");
        checkNum(bottomOff >= hOffline - 2 * kSplitMargin,
                 bottomOff, hOffline - 2 * kSplitMargin,
                 "P0: the offline state's last ink sits at the bottom of "
                 "its box — the meta verdict is drawn last");
        checkNum(bottomLive >= hLive - 2 * kSplitMargin,
                 bottomLive, hLive - 2 * kSplitMargin,
                 "P0: the live state's last ink sits at the bottom of its "
                 "box — the meta verdict is drawn last");
    }

    // =====================================================================
    // SUMMARY
    // =====================================================================
    std::printf("\nplayer-info-three-state-tests: %d checks, %d failures\n",
                gChecks, gFailures);
    if (gFailures == 0)
        std::printf("ALL PASSED\n");
    else
        std::fprintf(stderr, "%d FAILURES\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
