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
#include <QPainter>

#include <algorithm>

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
// The Quest half keeps the panel's own numbers verbatim (kQrowH 14 /
// kGapQrow 3, panel_player.cpp:41/:58) because its four rows are a table
// of values that must line up with each other. The Conn half gets its own
// pitch (kMetaFontSize / kMetaRowH = 11) and its own gap above — which is
// the entire content of 「重新设计排版」: in the panel both halves were
// charged the same 14px line by the shared qrow stream whether they needed
// it or not, and the 8px PID row was the one paying for rows it never
// used.
constexpr int kMargin       = 11;   // HTML: .op padding:11 (panel:39)
constexpr int kQrowH        = 14;   // HTML: .qrow line height (panel:41)
constexpr int kGapQrow      = 3;    // vertical between .qrow lines (panel:58)
constexpr int kMetaGap      = 3;    // the same 3px above the meta line
constexpr int kMetaFontSize = 11;   // NOT 14 — see the header comment
constexpr int kMetaRowH     = 11;   // the meta line's own pitch
constexpr int kLabelGap     = 8;    // minimum label/value separation
constexpr int kDotInset     = 12;   // panel_player.cpp:830, text past the dot

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

// The Quest stream's height in the panel's own pitch: four 14px rows with
// the 3px inter-row gap and the last row's trailing gap trimmed —
// panel_player.cpp:732-734 with n_qrow pinned to 4, because this window
// owns all four rows and has no Conn row to interleave into the count.
// This is the number the shared stream no longer produces, and the reason
// the two halves can have different pitches at all.
[[nodiscard]] constexpr int questStreamHeight()
{
    return 4 * kQrowH + 3 * kGapQrow;   // 56 + 9 = 65
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
    // Two streams, measured independently — the point of the whole
    // exercise. Not `n*14 + (n-1)*3` over five rows: the meta line pays
    // its own 11px pitch and its own 3px gap, and it pays the gap only
    // when something actually sits above it.
    const bool quest = questEnabled();
    const bool conn  = connEnabled();
    int h = 2 * kMargin;
    if (quest)
        h += questStreamHeight();
    if (quest && conn)
        h += kMetaGap;
    if (conn)
        h += kMetaRowH;
    return h;
}

int PlayerInfoStripPanel::contentWidth() const
{
    if (!infoEnabled())
        return 2 * kMargin;

    // The five fonts the paint sets. Measuring with anything else is how a
    // window ends up one elided glyph short on every row.
    const QFont fLabel  = makeFont(9);
    const QFont fValue  = makeFont(10, QFont::Medium);
    const QFont fTimer  = makeFont(12, QFont::Bold);
    const QFont fMeta   = makeFont(kMetaFontSize);
    const QFont fMetaR  = makeFont(kMetaFontSize, QFont::Medium);
    const QFontMetrics mLabel(fLabel);
    const QFontMetrics mValue(fValue);
    const QFontMetrics mTimer(fTimer);
    const QFontMetrics mMeta(fMeta);
    const QFontMetrics mMetaR(fMetaR);

    int widest = 0;
    auto need = [&widest](int labelW, int valueW) {
        widest = std::max(widest, labelW + kLabelGap + valueW);
    };

    if (questEnabled()) {
        const QString questValue = composeQuestValue(row_);
        need(mLabel.horizontalAdvance(mh::tr("ui.player_zone")),
             mValue.horizontalAdvance(row_.zoneLabel));
        need(mLabel.horizontalAdvance(mh::tr("ui.player_quest")),
             mValue.horizontalAdvance(questValue));
        need(mLabel.horizontalAdvance(mh::tr("ui.player_time_left")),
             mTimer.horizontalAdvance(row_.timeLeft));
        need(mLabel.horizontalAdvance(mh::tr("ui.player_carts")),
             mValue.horizontalAdvance(composeCartValue(row_)));
    }

    if (connEnabled()) {
        // kDotInset is the dot-and-indent the panel puts in front of the
        // meta text (panel_player.cpp:827-831), so the measured line
        // reserves it too.
        need(kDotInset + mMeta.horizontalAdvance(composeMetaLeft(row_)),
             mMetaR.horizontalAdvance(composeMetaRight(row_)));
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

    // ---- Quest stream: the panel's four qrows, in the panel's pitch ----
    if (quest) {
        const QFont fLabel = makeFont(9);
        const QFont fValue = makeFont(10, QFont::Medium);
        const QFont fTimer = makeFont(12, QFont::Bold);

        auto drawQrow = [&p, &y, innerLeft, innerW](
                            const QString &left, const QString &right,
                            const QFont &leftFont, const QFont &rightFont,
                            const QColor &leftCol, const QColor &rightCol) {
            const QRectF rect(innerLeft, y, innerW, kQrowH);
            p.setFont(leftFont);
            p.setPen(leftCol);
            p.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, left);
            p.setFont(rightFont);
            p.setPen(rightCol);
            p.drawText(rect, Qt::AlignRight | Qt::AlignVCenter, right);
            y += kQrowH + kGapQrow;
        };

        // 区域  +  古代树森林  (panel_player.cpp:866-882)
        drawQrow(mh::tr("ui.player_zone"), row_.zoneLabel,
                 fLabel, fValue,
                 QColor(kLabelR, kLabelG, kLabelB),
                 QColor(kZoneR, kZoneG, kZoneB));

        // 任务  +  MR ★6 · 进行中  (panel_player.cpp:884-912)
        drawQrow(mh::tr("ui.player_quest"), composeQuestValue(row_),
                 fLabel, fValue,
                 QColor(kLabelR, kLabelG, kLabelB),
                 QColor(kQuestR, kQuestG, kQuestB));

        // 剩余  +  41:37  — the one row that was always the panel's
        // header-sized value; here it is simply the first row, because
        // the timer is what a hunter glances at and the process metadata
        // is what a bug reporter reads.
        // (panel_player.cpp:914-925)
        drawQrow(mh::tr("ui.player_time_left"), row_.timeLeft,
                 fLabel, fTimer,
                 QColor(kLabelR, kLabelG, kLabelB),
                 QColor(kTimerR, kTimerG, kTimerB));

        // 猫车  +  0 / 3  (panel_player.cpp:927-939)
        drawQrow(mh::tr("ui.player_carts"), composeCartValue(row_),
                 fLabel, fValue,
                 QColor(kLabelR, kLabelG, kLabelB),
                 QColor(kCartR, kCartG, kCartB));

        y -= kGapQrow;   // last qrow has no trailing inter-row gap
    }

    // ---- Conn stream: the 11px meta line, at the foot ------------------
    if (conn) {
        if (quest)
            y += kMetaGap;

        const QRectF rect(innerLeft, y, innerW, kMetaRowH);
        const QColor metaCol(kMetaR, kMetaG, kMetaB);

        // The dot, in the panel's colour pair: green for a live attach,
        // red for a dead one. Drawn only when attached, because the
        // offline label already carries its own 「○」.
        if (row_.connected) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(kConnGreenR, kConnGreenG, kConnGreenB));
            p.drawEllipse(QPointF(innerLeft + 5, y + kMetaRowH / 2.0),
                          2.5, 2.5);
            p.setBrush(Qt::NoBrush);
        }

        p.setPen(metaCol);
        p.setFont(makeFont(kMetaFontSize));
        p.drawText(rect.adjusted(kDotInset, 0, 0, 0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   composeMetaLeft(row_));

        p.setFont(makeFont(kMetaFontSize, QFont::Medium));
        p.setPen(metaCol);
        p.drawText(rect, Qt::AlignRight | Qt::AlignVCenter,
                   composeMetaRight(row_));
    }
}
