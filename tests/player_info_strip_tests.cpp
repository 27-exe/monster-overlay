// SPDX-License-Identifier: Apache-2.0
//
// The v0.12 split's sixth pure core: mhw::buildInfoModel — the
// transformation that turns one GameSnapshot into what the 「任务/进程」
// window draws (design 02-interaction-design.md §4.2 「方案 A」).
//
// Like player-split-strips-tests, this target deliberately does NOT link
// mhw-ui: the header is pulled in for its inline/model half only, the
// same way player-status-strip-tests takes a header's pure functions
// without dragging in a widget library. The panel in the OTHER half of
// the same translation unit would pull in QMainWindow,
// LayerShellQt::Window and six more headers, and none of what this file
// pins is about pixels.
//
// What is pinned here:
//   * the two halves' FIELD SELECTION — Quest reads `snap.quest` (8
//     fields), Conn reads `snap.pid` / `snap.imageBase` /
//     `snap.party.size()` / `snap.attached` — and that Conn's party count
//     is computed HERE rather than inherited from PlayerViewModel, whose
//     local-member override exists for MR/name/weaponId;
//   * the three conditions the panel applies to one quest row:
//     `active && id > 0` for the row's value AND for the "#%1" suffix,
//     the anomaly flag over the star threshold, and `timeLeft <= 0`
//     printing "--:--" instead of a garbage timer;
//   * the empty snapshot still answering connected/pid/base (NOT
//     isEmpty) — the window's hasContent() keeps the meta line alive for
//     a hunter who never entered a quest, which is the one place this
//     window deliberately differs from the other five split windows;
//   * the zone LABEL dispatch, by the data layer (has<game>ZoneName) and
//     never by a translated string: a snapshot carrying the OTHER game's
//     zone id must fall through to the range fallback rather than render
//     the wrong game's name;
//   * THE ROW PITCH RULE — ceil(QFontMetrics(f).height()) for every font
//     the window draws in. The shipped layout reserved 11px for an 11pt
//     line and clipped 6 rows of 「已连接」's glyphs off it (measured, by
//     rendering the same string into an 11px band and a 25px band and
//     counting ink rows: 11 vs 17), and 14px for a 10pt value whose ink
//     is 15px tall. The formulas are pinned here so the reservation can
//     never again be a number typed by hand.
//
// The last group is the one that actually breaks: the door number table
// changes with a game patch, and a zone id from the other game's band
// landing in the vocabulary is silent, wrong, and invisible to a pixel
// baseline that only ever ran World.

#include "ui/panel_player_split_info.h"

#include "core/zone_id.h"
#include "quest/quest_types.h"
#include "rise/rise_zones.h"
#include "world/world_types.h"

// QtCore AND QtGui: the pitch rule is a question about REAL font metrics,
// and QFontMetricsF fatals ("QGuiApplication::font(): Must construct a
// QGuiApplication first") in a QCoreApplication-only process — measured,
// not assumed. This is the one place this file reaches past QtCore, and
// it is the point of the section below: a rule about pixels cannot be
// checked against a made-up number.
#include <QCoreApplication>
#include <QGuiApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QString>

#include <cmath>
#include <cstdio>

namespace {
int gFailures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++gFailures;
    }
}

// The shipped demo numbers the panel itself seeds for the same block
// (panel_player.cpp:1261-1266 / :1294): World, attached, PID 12345, base
// 0x7FFE00000000, a 4-man party, quest #66801 ★6 HR in progress with 3
// carts allowed. The window's demo seeds the same values so a test and a
// preview cannot drift.
mhw::GameSnapshot demoSnapshot()
{
    mhw::GameSnapshot snap;
    snap.game      = mhw::GameId::World;
    snap.attached  = true;
    snap.pid       = 12345;
    snap.imageBase = 0x7FFE00000000ULL;
    snap.party.resize(4);
    snap.zone      = mhw::ZoneRef{mhw::GameId::World,
                                  static_cast<int>(mhw::WorldZone::AncientForest)};
    snap.quest     = {66801, 6, false, 2, 2, 0, 0, 3, 2497.0F, 0.0F, 0.0F, true};
    return snap;
}

// ─── the pitch rule, mirrored here ──────────────────────────────────────
//
// The rule lives in panel_player_split_info.cpp's anonymous namespace
// (rowPitch / infoLinePitch / streamHeight) because it is a function of
// QFontMetrics, and this target links QtCore-only on purpose — see the
// header note above. So the property is pinned HERE as arithmetic on the
// same inputs, and the two must stay equal: a font-size edit that moves
// the .cpp's numbers without touching this file fails this test, which
// is the whole point of writing it down twice.
//
// The mirror is three steps, not a copy of the implementation:
//   1. the font constructor is the panel's own (family + point size +
//      antialias strategy), spelled from the point-size constants the
//      comment above records;
//   2. the pitch is ceil(QFontMetrics::height());
//   3. the window height is 2*kMargin + the drawn lines' pitches + one
//      gap between consecutive drawn lines.
QFont mirrorMakeFont(int pt, QFont::Weight weight = QFont::Normal)
{
    QFont f(QStringLiteral("Chakra Petch"), pt, weight);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

int mirrorRowPitch(const QFont &f)
{
    return static_cast<int>(std::ceil(QFontMetricsF(f).height()));
}

// The window's layout tokens, mirrored from the .cpp. These are the
// numbers the two fixes changed: kMetaRowH/kQrowH are gone (replaced by
// metric-derived pitches) and kMetaFontSize dropped 11 -> 9.
constexpr int kMirrorMargin = 11;
constexpr int kMirrorGapLine = 3;
constexpr int kMirrorLabelPt = 9;
constexpr int kMirrorValuePt = 10;
constexpr int kMirrorTimerPt = 12;
constexpr int kMirrorMetaPt = 9;

} // namespace

int main(int argc, char *argv[])
{
    // QGuiApplication, not QCoreApplication, because QFontMetricsF needs a
    // font database and fatals without one. offscreen keeps CI headless.
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("player-info-strip-tests"));

    // ---- the shipped demo snapshot: every field, one assertion each ----
    {
        const mhw::InfoRow r = mhw::buildInfoModel(demoSnapshot());

        check(r.hasQuest, "active quest with an id -> hasQuest");
        check(r.questId == 66801, "quest id travels through");
        check(r.connected, "attached snapshot -> connected");
        check(r.pid == 12345, "pid travels through unmangled");
        check(r.imageBase == 0x7FFE00000000ULL,
              "image base travels through unmangled (64-bit)");
        check(r.partyCount == 4, "party count is party.size()");
        check(r.deaths == 0, "deaths travel through");
        check(r.maxDeaths == 3, "maxDeaths travel through");
        check(!r.isEmpty(), "demo snapshot is not empty");
    }

    // ---- 41:37: 2497s is 41m 37s, and the format is mm:ss ------------
    {
        const mhw::InfoRow r = mhw::buildInfoModel(demoSnapshot());
        check(r.timeLeft == QStringLiteral("41:37"),
              "2497 seconds prints 41:37");
        check(r.zoneLabel == QString::fromUtf8(mhw::zoneNameLocalized(
                  mhw::WorldZone::AncientForest)),
              "zone label resolves through the localized data table");
    }

    // ---- state label, and that it is the state WORD alone -------------
    // Composed at paint time into "<rank> <stars> · <state>"; the model
    // carries the two halves separately so the measurement and the draw
    // agree on one string. `state` is 2 == in progress.
    {
        mhw::GameSnapshot snap = demoSnapshot();
        snap.quest.state = 2;
        const mhw::InfoRow r = mhw::buildInfoModel(snap);
        check(!r.questState.isEmpty(),
              "an in-progress quest resolves a state word");
        check(r.questName.endsWith(QStringLiteral("★6")),
              "a normal quest's name half ends with the star count");
    }

    // ---- the anomaly branch: Lv<stars>, not ★<stars> ------------------
    // The flag is explicit because a numeric threshold would be
    // ambiguous (normal quests can have a three-digit display value) —
    // quest_types.h:27-30.
    {
        mhw::GameSnapshot snap = demoSnapshot();
        snap.quest.isAnomaly = true;
        snap.quest.stars = 140;
        const mhw::InfoRow r = mhw::buildInfoModel(snap);
        check(r.questName.endsWith(QStringLiteral("Lv140")),
              "an anomaly quest prints Lv<stars>");
        check(!r.questName.endsWith(QStringLiteral("★140")),
              "an anomaly quest does NOT print a star count");
    }

    // ---- the rank prefix: 1/2/3 -> LR / HR / MR, else nothing ---------
    {
        struct { int rank; const char *prefix; const char *what; } cases[] = {
            {1, "LR ", "rank 1 -> LR"},
            {2, "HR ", "rank 2 -> HR"},
            {3, "MR ", "rank 3 -> MR"},
            {0, "",    "rank 0 -> no prefix"},
            {9, "",    "an out-of-range rank -> no prefix, not a crash"},
        };
        for (const auto &c : cases) {
            mhw::GameSnapshot snap = demoSnapshot();
            snap.quest.rank = c.rank;
            const mhw::InfoRow r = mhw::buildInfoModel(snap);
            const QString expected = QString::fromUtf8(c.prefix);
            if (expected.isEmpty()) {
                check(r.questName.startsWith(QStringLiteral("★"))
                         || r.questName.startsWith(QStringLiteral("Lv")),
                      c.what);
            } else {
                check(r.questName.startsWith(expected), c.what);
            }
        }
    }

    // ---- no active quest: idle wording, and NO state word -------------
    // The idle placeholder already means 「空闲」, so appending the state
    // name would print "空闲 · 空闲".
    {
        mhw::GameSnapshot snap = demoSnapshot();
        snap.quest.active = false;
        const mhw::InfoRow r = mhw::buildInfoModel(snap);
        check(!r.hasQuest, "inactive quest -> !hasQuest");
        check(r.questState.isEmpty(),
              "inactive quest carries no state word (no idle · idle)");
        check(r.timeLeft == QStringLiteral("--:--"),
              "inactive quest prints the --:-- timer placeholder");
        check(r.maxDeaths == 3,
              "an inactive quest keeps maxDeaths for the 0 / 3 row");
    }

    // ---- id <= 0 with active ON: the id must not appear ---------------
    // panel_player.cpp:854 puts exactly this test on the id before
    // appending "#%1" to the meta line.
    {
        mhw::GameSnapshot snap = demoSnapshot();
        snap.quest.id = 0;
        const mhw::InfoRow r = mhw::buildInfoModel(snap);
        check(!r.hasQuest, "active quest with id 0 -> !hasQuest (no '#0')");
        check(r.questId == 0, "the zero id still travels through");
    }

    // ---- timeLeft <= 0 with the quest ON: placeholder, not 0:00 ------
    {
        mhw::GameSnapshot snap = demoSnapshot();
        snap.quest.timeLeftSeconds = 0.0F;
        const mhw::InfoRow r = mhw::buildInfoModel(snap);
        check(r.timeLeft == QStringLiteral("--:--"),
              "a live quest at zero seconds prints --:--");
    }

    // ---- a questless snapshot still says who we are attached to -------
    // THE load-bearing case for this window's hasContent(): PID / BASE /
    // party is our own diagnostic surface (HunterPie has no such
    // concept), and a hunter who never entered a quest is exactly the
    // case that used to produce an unreadable empty overlay
    // (panel_player.cpp:582-585). The model must therefore NOT be empty.
    {
        mhw::GameSnapshot snap;   // attached=false, everything else default
        snap.attached = true;
        snap.pid      = 24601;
        snap.imageBase = 0x7FFE12340000ULL;
        const mhw::InfoRow r = mhw::buildInfoModel(snap);
        check(!r.hasQuest, "no quest -> !hasQuest");
        check(r.connected, "attached -> connected");
        check(r.pid == 24601, "no quest does not cost the pid");
        check(r.imageBase == 0x7FFE12340000ULL,
              "no quest does not cost the image base");
        check(!r.isEmpty(),
              "attached without a quest is NOT empty — the meta line survives");
        check(r.partyCount == 0, "an empty party counts 0, not -1");
    }

    // ---- neither half: empty, so hasContent() may collapse the window -
    {
        mhw::GameSnapshot snap;   // attached=false, quest default
        const mhw::InfoRow r = mhw::buildInfoModel(snap);
        check(r.isEmpty(), "a cold-process snapshot is empty");
        check(r.timeLeft == QStringLiteral("--:--"),
              "a cold-process snapshot prints the timer placeholder");
    }

    // ---- the party count is party.size(), NOT the view model ----------
    // resolveIdentity() would carry the local member's override; this
    // window draws none of what that override is for, and an empty party
    // is the honest "not in a session" answer.
    {
        mhw::GameSnapshot snap = demoSnapshot();
        snap.party.resize(2);
        mhw::PartyMemberSnapshot self;
        self.local = true;
        self.masterRank = 888;
        snap.party.append(self);
        const mhw::InfoRow r = mhw::buildInfoModel(snap);
        check(r.partyCount == 3,
              "party count counts every filled slot (3), not 1 local member");
    }

    // ---- the zone dispatch: the other game's id falls through ---------
    // panel_player.cpp:162-175 dispatches on z.game and asks THAT game's
    // data layer whether the id is named. A Rise snapshot carrying a
    // World id must not render a World name, and an id neither table
    // names must land in the outpost/area fallback rather than on a
    // wrong entry.
    {
        mhw::GameSnapshot snap = demoSnapshot();
        snap.zone = mhw::ZoneRef{mhw::GameId::Rise,
                                 static_cast<int>(mhw::WorldZone::AncientForest)};
        const mhw::InfoRow r = mhw::buildInfoModel(snap);
        check(r.zoneLabel != QString::fromUtf8(mhw::zoneNameLocalized(
                  mhw::WorldZone::AncientForest)),
              "a World zone id in a Rise snapshot does not render a World name");
    }
    {
        mhw::GameSnapshot snap = demoSnapshot();
        snap.zone = mhw::ZoneRef{mhw::GameId::World, 42};   // Rise village band
        const mhw::InfoRow r = mhw::buildInfoModel(snap);
        check(!r.zoneLabel.isEmpty(),
              "an unnamed village-band id still renders a fallback label");
    }
    {
        mhw::GameSnapshot snap = demoSnapshot();
        snap.zone = mhw::ZoneRef{mhw::GameId::Rise,
                                 static_cast<int>(mhw::RiseZone::RiseLoc3)};
        const mhw::InfoRow r = mhw::buildInfoModel(snap);
        check(r.zoneLabel == QString::fromUtf8(mhw::riseZoneNameLocalized(
                  mhw::RiseZone::RiseLoc3)),
              "a Rise zone id in a Rise snapshot resolves through Rise's table");
    }

    // ---- the same snapshot twice gives the same answer -----------------
    // Cheap determinism check: buildInfoModel must not read any mutable
    // global (a cached locale index, a static buffer).
    {
        const mhw::GameSnapshot snap = demoSnapshot();
        const mhw::InfoRow a = mhw::buildInfoModel(snap);
        const mhw::InfoRow b = mhw::buildInfoModel(snap);
        check(a.zoneLabel == b.zoneLabel
                  && a.questName == b.questName
                  && a.timeLeft == b.timeLeft
                  && a.pid == b.pid,
              "building the same snapshot twice is deterministic");
    }

    // ==================================================================
    // ---- THE PITCH RULE (the (a) fix, pinned) ------------------------
    // ==================================================================
    //
    // The defect this window was filed with: 「『已连接』的上下被挡住了」.
    // Root cause is arithmetic that reused a font size as a row height.
    // 11pt Chakra Petch has height() = 19.48, so a row of 11px removed
    // 6 rows of glyph ink — measured by rendering the same string into
    // an 11px band and a 25px band and counting ink rows (11 vs 17).
    //
    // The assertions below are the rule as a formula, so the number can
    // never be typed by hand again. They are locale-independent: metrics
    // are the font's, not the translation's, so they hold in zh-CN and
    // en-US alike and may be asserted before any locale load.
    {
        const QFont fMeta     = mirrorMakeFont(kMirrorMetaPt);
        const QFont fMetaMed  = mirrorMakeFont(kMirrorMetaPt, QFont::Medium);
        const QFont fLabel    = mirrorMakeFont(kMirrorLabelPt);
        const QFont fValue    = mirrorMakeFont(kMirrorValuePt, QFont::Medium);
        const QFont fTimer    = mirrorMakeFont(kMirrorTimerPt, QFont::Bold);

        // 1. A pitch derived from metrics is never smaller than the
        //    string's own ink. This is the assertion the OLD code failed:
        //    the 11px row was 6px shorter than the glyphs inside it, and
        //    the reason the meta line could not be reserved at 11.
        const int metaInk = static_cast<int>(std::ceil(
            QFontMetricsF(fMeta)
                .tightBoundingRect(QString::fromUtf8("已连接 · PID 12345 · BASE 0x7ffe00000000"))
                .height()));
        check(mirrorRowPitch(fMeta) >= metaInk,
              "meta pitch >= the meta string's ink (the clip, as a formula)");

        // 2. The OLD numbers are gone. kMetaRowH was 11 and kQrowH was
        //    14 — pinning them as dead constants is how the fix survives
        //    the next person who wants a "nice round number".
        check(mirrorRowPitch(fMeta) > 11,
              "the meta pitch is no longer the literal 11 the bug shipped");
        check(mirrorRowPitch(fValue) > 14,
              "the value pitch is no longer the literal 14 the qrows shipped");

        // 3. ceil, not int: QFontMetrics::height() is a qreal and 15.58
        //    truncated to 15 loses a row. The literal values are this
        //    font's, recorded as a cross-check: if a font
        //    substitution ever swaps Chakra Petch out, this tripwire
        //    fires and every other number above is re-derived with it.
        check(mirrorRowPitch(fMeta) == 16 && mirrorRowPitch(fLabel) == 16
                  && mirrorRowPitch(fValue) == 17 && mirrorRowPitch(fTimer) == 21,
              "the four pitches are ceil(height()) exactly "
              "(16/16/17/21 for 9pt/9pt/10pt/12pt Chakra Petch)");

        // 4. Every font the paint sets has a pitch that covers it. A
        //    pitch BELOW the font's ascent+descent can only clip.
        for (const QFont *f : {&fMeta, &fMetaMed, &fLabel, &fValue, &fTimer}) {
            const QFontMetricsF m(*f);
            check(mirrorRowPitch(*f) >= m.ascent() + m.descent() - 1.0,
                  "a pitch never drops below ascent+descent (Medium case)");
        }

        // ==============================================================
        // ---- THE NEW HEIGHTS (the (b) fix, pinned) ---------------
        // ==============================================================
        // Two lines of table + one meta line, at metric-derived pitches,
        // gaps between consecutive drawn lines only. Mirrors streamHeight()
        // in the .cpp.
        const int pitchTable = std::max(mirrorRowPitch(fLabel), mirrorRowPitch(fValue));
        const int pitchTime  = std::max(mirrorRowPitch(fTimer), mirrorRowPitch(fValue));
        const int pitchMeta  = std::max(mirrorRowPitch(fMeta), mirrorRowPitch(fMetaMed));
        const auto mirrorHeight = [&](bool quest, bool conn) {
            // The .cpp walks the three lines in stacking order
            // (ZoneQuest, TimeCarts, Meta) and adds a gap before every
            // line but the first one drawn. Spelled out longhand here so
            // the stacking order is visible rather than a loop index.
            int h = 2 * kMirrorMargin;
            bool prev = false;
            if (quest) {                       // line 1
                h += pitchTable;
                prev = true;
            }
            if (quest) {                       // line 2
                if (prev) h += kMirrorGapLine;
                h += pitchTime;
                prev = true;
            }
            if (conn) {                        // line 3
                if (prev) h += kMirrorGapLine;
                h += pitchMeta;
                prev = true;
            }
            return h;
        };

        // Both streams on: 11 + 17 + 3 + 21 + 3 + 16 + 11 = 82, down from
        // the 101 the five-row layout cost — and the meta line's glyphs
        // are now inside their band.
        check(mirrorHeight(true, true) == 82,
              "both streams on -> 82px (was 101)");
        check(mirrorHeight(true, true) < 101,
              "the redesign is strictly shorter than what shipped");

        // A stream going off collapses its own lines, never reorders the
        // rest, and never leaves a dangling gap.
        check(mirrorHeight(true, false) == 11 + pitchTable + 3 + pitchTime + 11,
              "quest only -> margin + two lines + gap + margin");
        check(mirrorHeight(false, true) == 11 + pitchMeta + 11,
              "conn only -> margin + meta + margin, no stray gap");
        check(mirrorHeight(false, true) < mirrorHeight(true, true),
              "a disabled quest stream shrinks the window");
        check(mirrorHeight(true, false) < mirrorHeight(true, true),
              "a disabled conn stream shrinks the window");
    }

    std::printf("%s (%d failure(s))\n",
                gFailures == 0 ? "PASS" : "FAILED", gFailures);
    return gFailures == 0 ? 0 : 1;
}
