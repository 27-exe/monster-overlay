// SPDX-License-Identifier: Apache-2.0
//
// PlayerInfoStripPanel implementation — the v0.12 「任务/进程」 window.
//
// Every drawing sub-routine below is this file's own copy of the one it
// names in panel_player.cpp, written out in full on purpose: those live in
// that file's anonymous namespace and are unreachable from here, and
// hoisting them into a shared header would mean editing a file this
// change may not touch. The copies are confined to the two blocks being
// extracted (Conn, panel_player.cpp:844-864; Quest, :866-939), so they
// disappear the day the two merge again — the same note
// panel_player_split.h carries for the pilot.

#include "panel_player_split_info.h"

#include "core/string_table.h"
#include "core/zone_id.h"
#include "rise/rise_zones.h"
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
#include "world/world_types.h"

#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <cmath>

// i18n lookup, defined the same way panel_player.cpp:31-33 defines it.
// The keys this file asks for are all existing ones — see the header for
// why the meta line needs no key of its own.
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- Layout tokens ------------------------------------------------------
//
// THE PITCH RULE (measured, not estimated — see rowPitch below): a text
// row must be as tall as ceil(QFontMetrics::height()) for the font it
// draws in. The shipped constants got this wrong in BOTH streams and the
// user saw both:
//
//   * the meta line was given kMetaRowH = 11 for an 11pt font whose
//     height() is 19.48. Offscreen render: the 11px band shows 11 ink
//     rows where a 25px band shows 17 — 6 rows of 「已连接」's glyphs are
//     cut off top and bottom. That is the 「上下被挡住」 report.
//   * the qrows were given kQrowH = 14 for a 10pt value whose
//     tightBoundingRect is 15px (古代树森林), so the zone value was
//     clipped by one row too — silently, because clipping into a QRectF
//     is Qt's default and never warns.
//
// So every height below is either measured from metrics or a spacing
// constant, and no font size is ever reused as a row height.
constexpr int kMargin       = mhw::split_layout::kSplitMargin;  // HTML: .op padding:11
constexpr int kLabelGap     = 8;    // label/value separation inside one cell
constexpr int kColGap       = 16;   // separation between the two cells on a line
constexpr int kGapLine      = mhw::split_layout::kSplitGapWithin;  // vertical between lines
constexpr int kDotInset     = 12;   // panel_player.cpp:830, text past the dot

// The point sizes, named so the pitch derivation and the draw read the
// same numbers. label/value/timer are the panel's own qrow fonts
// (panel_player.cpp:810-814). kMetaFontSize was 11: the ONE number in
// this file that is deliberately smaller than it was, because 11pt costs
// 398px of inner width for one line (see the header comment).
// The type scale reads the split's shared stack now. The VALUES are
// unchanged — this window was the one that arrived at 9/10/12 after its
// own clipping round, and that is exactly why it became the stack — so
// the pitch table above and every measure below still compute the same
// numbers. What changes is that a future edit to the split's label size
// has one home instead of four, and the two windows that were
// inconsistent with it (bars at 9/8, mantles at 9/8) move to it.
constexpr int kLabelPt      = mhw::split_layout::kSplitFontLabel;   // qFontL
constexpr int kValuePt      = mhw::split_layout::kSplitFontValue;   // qFontL10
constexpr int kTimerPt      = mhw::split_layout::kSplitFontAccent;  // qFontL12
constexpr int kMetaFontSize = mhw::split_layout::kSplitFontLabel;   // panel's conn row is 8pt

// ---- Colour palette (the four the Quest rows use) -----------------------
constexpr int kLabelR  = 146; constexpr int kLabelG  = 148; constexpr int kLabelB  = 149;  // --t3
constexpr int kZoneR   = 220; constexpr int kZoneG   = 222; constexpr int kZoneB   = 224;  // --t2
constexpr int kQuestR  = 255; constexpr int kQuestG  = 214; constexpr int kQuestB  = 107;  // #ffd66b
constexpr int kTimerR  = 80;  constexpr int kTimerG  = 197; constexpr int kTimerB  = 183;  // #50c5b7
constexpr int kCartR   = 255; constexpr int kCartG   = 118; constexpr int kCartB   = 118;  // #ff7676
constexpr int kMetaR   = 120; constexpr int kMetaG   = 122; constexpr int kMetaB   = 124;  // panel:860
// Conn dot: the panel's kConnGreen for a live attach and kConnRed for a
// dead one (panel_player.cpp:82-83, which only ever drew the green arm).
constexpr int kConnGreenR = 76;  constexpr int kConnGreenG = 175; constexpr int kConnGreenB = 80;
constexpr int kConnRedR   = 244; constexpr int kConnRedG   = 67;  constexpr int kConnRedB   = 54;

// A Chakra Petch font at `px`, antialiased — the same construction
// panel_player.cpp:794-807 uses for every qrow font. Measuring or drawing
// with anything else is how a window ends up one elided glyph short.
QFont makeFont(int px, QFont::Weight weight = QFont::Normal)
{
    QFont f(QStringLiteral("Chakra Petch"), px, weight);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

// THE SAFE ROW HEIGHT. Not a constant, because "the font size in pixels"
// is not a row height in Qt: makeFont(11) is an 11 POINT font, which at the
// panel's 96 logical dpi resolves to a 19.48px line box, and a row of 11
// cuts 6 rows of glyph ink off it. The formula is
//
//     rowPitch(f) == ceil(QFontMetrics(f).height())
//
// and the reason that exact choice among the three metrics on offer:
//
//   * ascent() + descent()  — measured EQUAL to height() for every size
//     Chakra Petch ships (11pt: 14.88 + 4.61 = 19.48), because
//     leading() is 0 for this family. So height() and ascent+descent
//     agree HERE, and code written against either survives a font whose
//     leading is non-zero only if it uses height() — that is the one
//     metric that already includes leading.
//   * tightBoundingRect() — is SMALLER and is the WRONG tool: it measures
//     the ink of the string you pass it, so 「剩余」 reports 12px while
//     「已连接」 reports 17px in the same 11pt font. A row height derived
//     from it would clip on the next string, which is a value the layout
//     cannot see. (It IS the right measure of how tall a specific string
//     draws — used by the tests to prove the old row was too short.)
//
// ceil because the box is a qreal and the reserved band is an int: 19.48
// reserved as 19 would still clip the 20th of a row that needs it.
[[nodiscard]] int rowPitch(const QFont &f)
{
    const QFontMetricsF m(f);
    return static_cast<int>(std::ceil(m.height()));
}

// panel_player.cpp:182-190
QString fmtMmSs(float seconds)
{
    if (seconds <= 0.0F)
        return QStringLiteral("--:--");
    const int total = static_cast<int>(seconds);
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0'));
}

// panel_player.cpp:109-122. Re-resolved per call, not cached, so a
// language switch lands on the next paint / next update() without any
// invalidate step.
QString questStateLabel(int state)
{
    switch (state) {
    case 0:  return mh::tr("ui.quest_state.idle");
    case 1:  return mh::tr("ui.quest_state.booked");
    case 2:  return mh::tr("ui.quest_state.in_progress");
    case 3:  return mh::tr("ui.quest_state.complete");
    case 4:  return mh::tr("ui.quest_state.failed");
    case 5:  return mh::tr("ui.quest_state.abandoned");
    default: return mh::tr("ui.quest_state.unknown").arg(state);
    }
}

// panel_player.cpp:124-180 — the per-game zone vocabulary dispatch. The
// branch asks the DATA layer whether the id has a name (has<game>ZoneName),
// never the translated label, so rewording a translation cannot move it.
// No `default:` arm on purpose: an unhandled GameId is a -Wswitch warning
// and then the outpost/area fallback, which is visible rather than wrong.
QString zoneLabel(mhw::ZoneRef z)
{
    if (mhw::isUnknownZone(z))
        return mh::tr("ui.zone_unknown_area");
    switch (z.game) {
    case mhw::GameId::Rise: {
        const mhw::RiseZone riseZone = static_cast<mhw::RiseZone>(z.id);
        if (mhw::hasRiseZoneName(riseZone))
            return QString::fromUtf8(mhw::riseZoneNameLocalized(riseZone));
        break;
    }
    case mhw::GameId::World: {
        const mhw::WorldZone worldZone = static_cast<mhw::WorldZone>(z.id);
        if (mhw::hasZoneName(worldZone))
            return QString::fromUtf8(mhw::zoneNameLocalized(worldZone));
        break;
    }
    }
    const int raw = z.id;
    if (raw >= 0 && raw <= 100)
        return mh::tr("ui.zone_outpost").arg(raw);
    return mh::tr("ui.zone_area").arg(raw);
}

// panel_player.cpp:893-899. The rank prefix is a literal (LR / HR / MR are
// the upstream abbreviations, not translatable words) and the anomaly
// branch reads the explicit flag rather than a numeric threshold.
QString questStars(const mhw::QuestSnapshot &q)
{
    QString prefix;
    if (q.rank == 1)      prefix = QStringLiteral("LR ");
    else if (q.rank == 2) prefix = QStringLiteral("HR ");
    else if (q.rank == 3) prefix = QStringLiteral("MR ");
    const QString stars = q.isAnomaly
        ? QStringLiteral("Lv%1").arg(q.stars)
        : QStringLiteral("★%1").arg(q.stars);
    return prefix + stars;
}

// The 「任务」 column value: "<rank> <stars> · <state>" while a quest runs,
// and the whole idle placeholder when none does. Split across the model's
// two fields (questName / questState) and composed HERE, because the
// measurement and the draw must agree on the string exactly — two
// spellings of one value is how a row ends up elided in one of them.
QString composeQuestValue(const mhw::InfoRow &row)
{
    if (row.questState.isEmpty())
        return row.questName;
    return QStringLiteral("%1 · %2").arg(row.questName).arg(row.questState);
}

// panel_player.cpp:928-932: the count, or the historical 0 / 3 once the
// quest is over.
QString composeCartValue(const mhw::InfoRow &row)
{
    if (!row.hasQuest)
        return QStringLiteral("0 / 3");
    return QStringLiteral("%1 / %2").arg(row.deaths).arg(row.maxDeaths);
}

// ---- the redesign's two lines -------------------------------------------
//
// v0.12 shipped four stacked qrows + one meta row, which is five lines to
// say what fits in three, and the window width was set by the LONGEST of
// them (the meta line at 11pt = 398px inner), so the other four lines sat
// in a 420px window carrying ~60-120px of content each. Measured dead
// space per line: 301 / 279 / 349 / 338 of 398px.
//
// The two things a hunter reads (「zone + quest」 and 「remaining time」)
// are now one line each, two columns per line, because both pairs fit
// side by side: 「剩余 41:37」 + 16 + 「猫车 0 / 3」 is 146px wide, while
// the meta line under them is 322px. Stacking them cost 2*21px of height
// to make the window 156px wider than its content needed.
//
// So the height is now
//     kMargin + questPitch + 3 + metaPitch + kMargin
// with the quest stream being TWO lines (zone|quest, time|carts) instead
// of four, and the meta stream still its own line at the foot — it is
// provenance, and moving it to the head would put PID above the timer.

// Which of the three lines a stream occupies, in stacking order.
enum class InfoLine : int {
    ZoneQuest = 0,   // 「区域」 left cell, 「任务」 right cell
    TimeCarts = 1,   // 「剩余」 left cell, 「猫车」 right cell
    Meta      = 2,   // dot + 已连接 · PID · BASE   party · #id
};

constexpr int kInfoLines = 3;

// The pitch every line must reserve, measured at the fonts the paint
// actually sets, via rowPitch() above:
//   ZoneQuest  — max(9pt label, 10pt value)          -> ceil(16.89) = 17
//   TimeCarts  — max(12pt timer, 10pt value)         -> ceil(20.78) = 21
//   Meta       — max(9pt left,  9pt medium right)    -> ceil(15.58) = 16
// A function of the fonts rather than three integer constants, so editing
// a font size cannot desync the height the way kMetaRowH = 11 did.
[[nodiscard]] int infoLinePitch(InfoLine line)
{
    switch (line) {
    case InfoLine::ZoneQuest:
        return std::max(rowPitch(makeFont(kLabelPt)), rowPitch(makeFont(kValuePt)));
    case InfoLine::TimeCarts:
        return std::max(rowPitch(makeFont(kTimerPt)), rowPitch(makeFont(kValuePt)));
    case InfoLine::Meta:
        return std::max(rowPitch(makeFont(kMetaFontSize)),
                       rowPitch(makeFont(kMetaFontSize, QFont::Medium)));
    }
    return 0;   // unreachable: the switch above covers every enumerator
}

// The meta line's left half: the connection word plus the process
// identity while attached, and the plain offline label when not. The
// panel only ever reached its Conn row on the has-data path, so it always
// printed the connected wording; this window is mounted without a game
// running, and claiming 已连接 over PID -- would be the lie the row exists
// to prevent.
QString composeMetaLeft(const mhw::InfoRow &row)
{
    if (!row.connected)
        return mh::tr("ui.player_status_offline");
    const QString mem = QStringLiteral("PID %1 · BASE 0x%2")
        .arg(row.pid > 0 ? QString::number(row.pid) : QStringLiteral("--"))
        .arg(row.imageBase != 0
                 ? QString::number(static_cast<qulonglong>(row.imageBase), 16)
                 : QStringLiteral("--"));
    return mh::tr("ui.player_connected").arg(mem);
}

// The meta line's right half: party size plus the quest id, panel_player
// .cpp:851-856.
QString composeMetaRight(const mhw::InfoRow &row)
{
    QString right = row.partyCount > 0
        ? mh::tr("ui.player_party_count").arg(row.partyCount)
        : QStringLiteral("--");
    if (row.hasQuest && row.questId > 0)
        right += QStringLiteral(" · #%1").arg(row.questId);
    return right;
}

// A line is drawn when it belongs to an enabled stream. The Quest stream
// owns the two table lines; the Conn stream owns the meta line. The
// stacking order never changes — a stream going off collapses its own
// lines out of the middle, it never reorders the ones that remain.
[[nodiscard]] bool lineBelongsToStream(InfoLine line, bool quest, bool conn)
{
    switch (line) {
    case InfoLine::ZoneQuest:
    case InfoLine::TimeCarts:
        return quest;
    case InfoLine::Meta:
        return conn;
    }
    return false;
}

// The window's height: one pitch per drawn line, one kGapLine between
// consecutive drawn lines, margin on both sides. Nothing else.
//
// This is the function the whole change is about. What it used to say —
//
//     h = 2*kMargin + 4*kQrowH + 3*kGapQrow + kMetaGap + kMetaRowH
//       = 22 + 56 + 9 + 3 + 11 = 101
//
// — reserved 14px for the four qrows and 11px for the meta line, so the
// 10pt zone value (15px of ink) and the 9pt meta text (17px of ink) were
// both drawn shorter than the glyphs they carry, and the clip was silent
// because drawText into a QRectF clips by default. The pitch now comes
// from QFontMetrics.height(), so the reservation and the draw agree by
// construction.
[[nodiscard]] int streamHeight(bool quest, bool conn)
{
    int h = 2 * kMargin;
    bool previous = false;
    for (int i = 0; i < kInfoLines; ++i) {
        const auto line = static_cast<InfoLine>(i);
        if (!lineBelongsToStream(line, quest, conn))
            continue;
        if (previous)
            h += kGapLine;
        h += infoLinePitch(line);
        previous = true;
    }
    return h;
}

} // namespace

namespace mhw {

// The pure transformation.
//
// Deliberately NOT dispatched on GameId. Quest and connection data are one
// struct each that BOTH readers fill with the same contract
// (quest_types.h's QuestSnapshot; GameSnapshot's own pid / imageBase /
// party / attached), so unlike the abnormality blocks there is no
// per-game vocabulary to select and a switch here would be an empty seam.
// The one per-game part — the zone NAME — keeps the dispatch the panel
// uses, inside zoneLabel() above.
//
// The party count is `snap.party.size()` rather than
// PlayerViewModel::resolveIdentity(): the only one of its four outputs this
// window draws is partyCount, and its local-member override exists for MR /
// name / weaponId, which are the weapon window's business. An empty party
// is the honest "not in a session" answer for a lone hunter.
InfoRow buildInfoModel(const mhw::GameSnapshot &snap)
{
    InfoRow row;

    row.zoneLabel = zoneLabel(snap.zone);

    const mhw::QuestSnapshot &q = snap.quest;
    // `active && id > 0` — the same test panel_player.cpp:854 puts on the
    // id before appending it to the meta line, and :900 on the quest text.
    // A reader that flips `active` without an id gets the idle wording
    // rather than a "#0" suffix.
    row.hasQuest = q.active && q.id > 0;
    row.questId  = q.id;
    if (row.hasQuest) {
        row.questName  = questStars(q);
        row.questState = questStateLabel(q.state);
    } else {
        row.questName  = mh::tr("ui.player_quest_idle");
        row.questState = QString();
    }

    row.timeLeft  = fmtMmSs(row.hasQuest ? q.timeLeftSeconds : 0.0F);
    row.deaths    = q.deaths;
    row.maxDeaths = q.maxDeaths;

    row.pid       = snap.pid;
    row.imageBase = static_cast<quint64>(snap.imageBase);
    row.partyCount = snap.party.size();
    row.connected = snap.attached;

    return row;
}

} // namespace mhw

PlayerInfoStripPanel::PlayerInfoStripPanel(QWidget *parent)
    : Panel(QStringLiteral("player-info"), Corner::TopLeft, parent)
{
    // No chrome, no title row: quest and process metadata are fragments of
    // the player panel, not a panel. setChromeVisible(false) suppresses
    // the v0.3 rounded body, left accent stripe and border entirely, so
    // the window surface stays transparent apart from the text itself.
    // isSubWindow() (below) declares the same fact for the console's slot
    // registry.
    setChromeVisible(false);

    // Own window-title key, the same one the other five split windows use
    // (ui.player_strip_title) — a sub-window is not PlayerPanel and must
    // not borrow ui.player_title, which would make the console's
    // inspector row ambiguous.
    setWindowTitle(mh::tr("ui.player_strip_title"));
}

void PlayerInfoStripPanel::update(const mhw::GameSnapshot &snap)
{
    // Same shape as PlayerPanel::update(): cache what paintPanel() needs,
    // schedule one repaint. The two raw inputs the localized labels are
    // resolved from are cached alongside the row so retranslateUi() can
    // re-resolve them without waiting a poll tick.
    quest_ = snap.quest;
    zone_  = snap.zone;
    row_   = mhw::buildInfoModel(snap);

    // A demo row is never live state (the rule every other split window
    // applies): the first authoritative snapshot replaces it outright, so
    // an edit-mode preview cannot survive into a live session and start
    // reporting a quest that does not exist.
    demoData_ = false;

    triggerUpdate();
}

void PlayerInfoStripPanel::retranslateUi()
{
    // Cached string first, exactly like every other panel's
    // retranslateUi(): main.cpp's applyLocale re-sets the title from the
    // same key, so the second set is the deliberate redundancy that keeps
    // the answer correct if this is ever called from anywhere else.
    setWindowTitle(mh::tr("ui.player_strip_title"));

    if (editMode()) {
        // The demo seed resolves its zone name and quest labels from the
        // locale pair at build time, so dropping it makes the next paint
        // re-seed in the new language — the same reason the mantle window
        // drops its demo (panel_player_split_mantles.cpp:271-278).
        resetDemoPrimed();
    } else {
        // Re-resolve the three localized label fields against the new
        // locale. Rebuilding the whole model would need the snapshot
        // again, which this window does not keep; the two raw inputs that
        // feed the labels are cached for exactly this.
        row_.zoneLabel = zoneLabel(zone_);
        if (row_.hasQuest) {
            row_.questName  = questStars(quest_);
            row_.questState = questStateLabel(quest_.state);
        } else {
            row_.questName  = mh::tr("ui.player_quest_idle");
            row_.questState = QString();
        }
    }
    triggerUpdate();
}

bool PlayerInfoStripPanel::questEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing PlayerSection::Quest keeps the
    // console's existing toggle driving this window for free.
    return (sectionMask() & mhw::PlayerSection::Quest) != 0;
}

bool PlayerInfoStripPanel::connEnabled() const
{
    // ... and PlayerSection::Conn for the meta line. Both bits are read
    // once in paintPanel()/contentHeight()/contentWidth() through these
    // two helpers, never the mask directly, so a bit that goes off drops
    // its stream from the width, the height and the paint at once.
    return (sectionMask() & mhw::PlayerSection::Conn) != 0;
}

bool PlayerInfoStripPanel::infoEnabled() const
{
    return questEnabled() || connEnabled();
}

bool PlayerInfoStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    if (!infoEnabled())
        return false;
    // NOT `!row_.isEmpty()`, and this is the one place this window
    // deliberately breaks from the other five. PID / BASE / party is our
    // own diagnostic surface — HunterPie has no notion of process
    // information at all — and the whole reason it exists is the
    // unreadable "why is my overlay empty" report
    // (panel_player.cpp:582-585). A hunter who has never entered a quest
    // still has that line, so an empty model collapses the QUEST STREAM,
    // never the window. Only a window whose two bits are both off must
    // not exist.
    return true;
}

int PlayerInfoStripPanel::contentHeight() const
{
    // One pitch per drawn line, one gap between consecutive drawn lines,
    // margin on both ends. The pitch comes from QFontMetrics.height(), so
    // a row is never shorter than the glyphs it draws — the property
    // kMetaRowH = 11 for an 11pt font did not have.
    //
    // Both streams on:  11 + 17 + 3 + 21 + 3 + 16 + 11 = 82
    //                     (was 101 with the same streams on)
    // Quest only:       11 + 17 + 3 + 21 + 11 = 63
    // Conn only:        11 + 16 + 11 = 38
    return streamHeight(questEnabled(), connEnabled());
}

int PlayerInfoStripPanel::contentWidth() const
{
    if (!infoEnabled())
        return 2 * kMargin;

    // The fonts the paint sets, and ONLY those: the cell lambda below
    // builds a QFontMetricsF from each one on demand, so a mismatch
    // between what is measured and what is drawn has no place to hide.
    // Measuring with anything else is how a window ends up one elided
    // glyph short on every row.
    const QFont fLabel  = makeFont(kLabelPt);
    const QFont fValue  = makeFont(kValuePt, QFont::Medium);
    const QFont fTimer  = makeFont(kTimerPt, QFont::Bold);
    const QFont fMeta   = makeFont(kMetaFontSize);
    const QFont fMetaR  = makeFont(kMetaFontSize, QFont::Medium);

    int widest = 0;
    auto need = [&widest](int w) { widest = std::max(widest, w); };

    // One cell's natural width, ceiled ONCE here and reused by the draw
    // (paintPanel measures the same terms and must land on the same
    // integer). Two roundings of one quantity is the reserve/draw drift
    // this file already carries a header comment about.
    auto cell = [](const QFont &lf, const QString &label,
                   const QFont &vf, const QString &value) {
        return static_cast<int>(std::ceil(
            QFontMetricsF(lf).horizontalAdvance(label) + kLabelGap
            + QFontMetricsF(vf).horizontalAdvance(value)));
    };

    // Line 1: 「区域」 | 「任务」 — the two cells side by side, which is
    // the whole point of the redesign: four stacked qrows into two lines.
    if (questEnabled()) {
        need(cell(fLabel, mh::tr("ui.player_zone"), fValue, row_.zoneLabel)
             + kColGap
             + cell(fLabel, mh::tr("ui.player_quest"), fValue,
                    composeQuestValue(row_)));

        // Line 2: 「剩余」 | 「猫车」. The timer stays 12pt/teal because
        // that is the glance-at row; carts is 10pt.
        need(cell(fLabel, mh::tr("ui.player_time_left"), fTimer, row_.timeLeft)
             + kColGap
             + cell(fLabel, mh::tr("ui.player_carts"), fValue,
                    composeCartValue(row_)));
    }

    if (connEnabled()) {
        // kDotInset is the dot-plus-indent the panel puts in front of the
        // meta text (panel_player.cpp:827-831), so the measured line
        // reserves it too — the same reservation the panel's own row made.
        need(static_cast<int>(std::ceil(
                 kDotInset + QFontMetricsF(fMeta).horizontalAdvance(
                                 composeMetaLeft(row_))
                 + kLabelGap
                 + QFontMetricsF(fMetaR).horizontalAdvance(
                       composeMetaRight(row_)))));
    }

    if (widest == 0)
        return 2 * kMargin;
    return widest + 2 * kMargin;
}

void PlayerInfoStripPanel::setupDemoData()
{
    // Edit-mode preview: attached, in a quest, in a four-man party — the
    // same numbers PlayerPanel's own demo seeds (panel_player.cpp:1259,
    // :1261-1262, :1266, :1294) so the two windows' previews agree and
    // neither has to be explained twice. Built as a snapshot and pushed
    // through the same builder the live path uses rather than filling the
    // row by hand, so the demo exercises the real transformation.
    mhw::GameSnapshot snap;
    snap.game     = mhw::GameId::World;
    snap.attached = true;
    snap.pid      = 12345;
    snap.imageBase = 0x7FFE00000000ULL;
    snap.party.resize(4);   // panel_player.cpp:1294, partyCount_ = 4
    snap.zone     = mhw::ZoneRef{mhw::GameId::World,
                                 static_cast<int>(mhw::WorldZone::AncientForest)};
    // Order matches QuestSnapshot: id, stars, isAnomaly, rank, state,
    // category, deaths, maxDeaths, timeLeftSeconds, maxTimerSeconds,
    // elapsedSeconds, active.
    snap.quest    = {66801, 6, false, 2, 2, 0, 0, 3, 2497.0F, 0.0F, 0.0F, true};

    quest_ = snap.quest;
    zone_  = snap.zone;
    row_   = mhw::buildInfoModel(snap);
    demoData_ = true;
}

void PlayerInfoStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an unmounted-but-alive
    // window never renders a leftover sliver. The live path never reaches
    // here — the main loop asks hasVisibleContent() first. This is the
    // both-bits-off / defensive branch.
    if (!infoEnabled()) {
        setContentSize(2 * kMargin, h);
        return;
    }

    // setContentSize has an idempotent guard inside Panel, so calling it
    // per repaint (and therefore per drag frame) is free.
    setContentSize(w, h);

    // Re-read the width the window actually got. setContentSize() applied
    // the user's minimum-width floor to its arguments, but `w` is still
    // the pre-floor content number — using it here is exactly the bug the
    // floor was added to fix: the window would widen and the rows would
    // keep their old width, leaving empty background to the right.
    // panel_player_split_bars.cpp:368-376 is where that was learned.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE. drawV03Chrome() is deliberately not called — the text
    // is the whole window, and a frame around it is the shape the split
    // exists to remove. This panel calls neither drawV03Chrome() nor
    // paintPanelChrome(), exactly like the other five split windows.

    const bool quest = questEnabled();
    const bool conn  = connEnabled();

    const int innerLeft = kMargin;
    const int innerW    = drawnW - 2 * kMargin;
    int y = kMargin;

    // One line of the window, advanced past when drawn. `pitch` is the
    // reservation — infoLinePitch, i.e. ceil(QFontMetrics.height()) for
    // the tallest font on that line — and it is the height the drawText
    // rect gets too, so what is reserved is what is drawn into. That
    // equality is the whole fix for 上下被挡住: the reserve and the draw
    // can no longer disagree, because they read the same function.
    auto advance = [&y](InfoLine line) {
        y += infoLinePitch(line) + kGapLine;
    };

    // A two-cell line: label+value on the left cell, label+value on the
    // right cell, kColGap between them. Each cell is LEFT-aligned inside
    // a fixed-width slot so the two columns line up; the slots are sized
    // by the cell's own measurement, so a value that grows pushes only
    // its own column.
    //
    // Elision: the value is elided into its cell when the cell is narrower
    // than the string. That is the escape valve that lets the width be
    // driven by the NATURAL line while the user's minimum-width floor
    // (minWidth) can still squeeze the content — the same rule
    // panel_player.cpp:1076 applies to its own name column.
    auto drawCell = [&p, &y, innerLeft](int slotLeft, int slotW,
                                       const QString &label, const QFont &labelFont,
                                       const QString &value, const QFont &valueFont,
                                       const QColor &labelCol, const QColor &valueCol,
                                       InfoLine line) {
        const int pitch = infoLinePitch(line);
        const QRectF rect(slotLeft, y, slotW, pitch);
        const int labelW = QFontMetricsF(labelFont).horizontalAdvance(label);
        p.setFont(labelFont);
        p.setPen(labelCol);
        p.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, label);
        const QRectF valueRect(slotLeft + labelW + kLabelGap, y,
                               slotW - labelW - kLabelGap, pitch);
        p.setFont(valueFont);
        p.setPen(valueCol);
        // Elide only when the cell is genuinely narrower than the string,
        // and decide that in the metric's own units. An unconditional
        // elidedText() is wrong at the boundary: QRectF::width() is an
        // int, so a value that needs exactly 37px inside 37px of slot
        // elides — the timer read 「41:…」 in the demo render. The branch
        // is what keeps "fits" and "must elide" from sharing a rounding
        // rule, and the elision still fires the moment the user's
        // minWidth floor squeezes the window below its natural width.
        const int avail = slotW - labelW - kLabelGap;
        const qreal need = QFontMetricsF(valueFont).horizontalAdvance(value);
        if (need <= avail) {
            p.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, value);
        } else {
            p.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetricsF(valueFont).elidedText(value, Qt::ElideRight,
                                                           avail));
        }
    };

    // ---- Line 1: 区域 | 任务 -------------------------------------------
    if (quest) {
        const QFont fLabel  = makeFont(kLabelPt);
        const QFont fValue  = makeFont(kValuePt, QFont::Medium);
        const QColor cLabel(kLabelR, kLabelG, kLabelB);
        const QColor cZone(kZoneR, kZoneG, kZoneB);
        const QColor cQuest(kQuestR, kQuestG, kQuestB);

        // ceil for the same reason the time cell's is: the advances are
        // qreal, and the cell must not be a fraction narrower than the
        // string it holds. contentWidth() applies the same ceil to the
        // same two terms, so the window the compositor gives and the
        // cell the draw sizes agree to the pixel.
        const int zoneW = static_cast<int>(std::ceil(
            QFontMetricsF(fLabel).horizontalAdvance(mh::tr("ui.player_zone"))
            + kLabelGap
            + QFontMetricsF(fValue).horizontalAdvance(row_.zoneLabel)));
        const int questW = innerW - kColGap - zoneW;

        drawCell(innerLeft, zoneW, mh::tr("ui.player_zone"), fLabel,
                 row_.zoneLabel, fValue, cLabel, cZone, InfoLine::ZoneQuest);
        drawCell(innerLeft + zoneW + kColGap, questW,
                 mh::tr("ui.player_quest"), fLabel,
                 composeQuestValue(row_), fValue, cLabel, cQuest,
                 InfoLine::ZoneQuest);
        advance(InfoLine::ZoneQuest);
    }

    // ---- Line 2: 剩余 | 猫车 -------------------------------------------
    if (quest) {
        const QFont fLabel  = makeFont(kLabelPt);
        const QFont fValue  = makeFont(kValuePt, QFont::Medium);
        const QFont fTimer  = makeFont(kTimerPt, QFont::Bold);
        const QColor cLabel(kLabelR, kLabelG, kLabelB);
        const QColor cTimer(kTimerR, kTimerG, kTimerB);
        const QColor cCart(kCartR, kCartG, kCartB);

        // The timer cell gets what its own string needs; carts takes the
        // rest. 「41:37」 at 12pt is 37px of advance, so ~69px is the
        // natural line-2 left half and the remaining width is the carts
        // column — which is dead space in every quest, and the point of
        // pairing them.
        //
        // ceil on the FIRST measurement, not on the cell: the advances
        // are qreal (12pt timer = 36.7px at this dpi), and a slot of 68
        // for a string that measures 36.7 + 8 + label is exactly the
        // case where the draw's own rect lands a fraction short and
        // Qt elides a value that "should" fit. Sizing the cell in the
        // metric's units keeps the fit decision on one side of the
        // rounding line.
        const int timeW = static_cast<int>(std::ceil(
            QFontMetricsF(fLabel).horizontalAdvance(mh::tr("ui.player_time_left"))
            + kLabelGap
            + QFontMetricsF(fTimer).horizontalAdvance(row_.timeLeft)));
        const int cartsW = innerW - kColGap - timeW;

        drawCell(innerLeft, timeW, mh::tr("ui.player_time_left"), fLabel,
                 row_.timeLeft, fTimer, cLabel, cTimer, InfoLine::TimeCarts);
        drawCell(innerLeft + timeW + kColGap, cartsW,
                 mh::tr("ui.player_carts"), fLabel,
                 composeCartValue(row_), fValue, cLabel, cCart,
                 InfoLine::TimeCarts);
        advance(InfoLine::TimeCarts);
    }

    // ---- Line 3: the Conn stream's meta line, at the foot ---------------
    if (conn) {
        const int pitch = infoLinePitch(InfoLine::Meta);
        const QRectF rect(innerLeft, y, innerW, pitch);
        const QColor metaCol(kMetaR, kMetaG, kMetaB);

        // The dot, in the panel's colour pair: green for a live attach,
        // red for a dead one. Drawn only when attached, because the
        // offline label already carries its own 「○」. Its centre is the
        // pitch's centre, not a hard-coded 11/2.
        if (row_.connected) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(kConnGreenR, kConnGreenG, kConnGreenB));
            p.drawEllipse(QPointF(innerLeft + 5, y + pitch / 2.0), 2.5, 2.5);
            p.setBrush(Qt::NoBrush);
        }

        // Left half, elided into the room the right half leaves it: the
        // meta text is the one string in this window that can grow
        // without bound (a 12-hex-digit base address), so it is the one
        // that must yield when the user narrows the window. The boundary
        // is decided in qreal units for the same reason drawCell's is.
        const QString metaLeft = composeMetaLeft(row_);
        const QString metaRight = composeMetaRight(row_);
        const int rightW = QFontMetricsF(makeFont(kMetaFontSize, QFont::Medium))
                               .horizontalAdvance(metaRight);
        const int leftW = innerW - rightW - kLabelGap;
        const int leftAvail = leftW - kDotInset;
        const qreal leftNeed =
            QFontMetricsF(makeFont(kMetaFontSize)).horizontalAdvance(metaLeft);

        p.setPen(metaCol);
        p.setFont(makeFont(kMetaFontSize));
        if (leftNeed <= leftAvail) {
            p.drawText(QRectF(innerLeft + kDotInset, y, leftAvail, pitch),
                       Qt::AlignLeft | Qt::AlignVCenter, metaLeft);
        } else {
            p.drawText(QRectF(innerLeft + kDotInset, y, leftAvail, pitch),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetricsF(makeFont(kMetaFontSize))
                           .elidedText(metaLeft, Qt::ElideRight, leftAvail));
        }

        p.setFont(makeFont(kMetaFontSize, QFont::Medium));
        p.drawText(rect, Qt::AlignRight | Qt::AlignVCenter, metaRight);
    }
}
