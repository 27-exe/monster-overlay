// SPDX-License-Identifier: Apache-2.0
//
// PlayerMantlesStripPanel implementation. drawMantleBox() below is this
// file's own copy of the one in panel_player.cpp:239-323, which lives in
// that file's anonymous namespace and is unreachable from here. Writing
// it out again is the deliberate cost of the split being purely
// additive: hoisting it into a shared header would mean editing
// panel_player.cpp, which this change may not touch. The copy is
// confined to the block being extracted, so it disappears the day the two
// merge again (panel_player_split.h carries the same note for the status
// strip).

#include "panel_player_split_mantles.h"

#include "core/string_table.h"
#include "ui/icon.h"
#include "ui/panel_sections.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

// i18n lookup, defined the same way panel_player.cpp:31-33 defines it.
// This file asks for no keys of its own: a mantle box's text is its
// localized name (data.mantle.id.<n>, resolved by mhw::mantleName from the
// DATA domain, not the overlay chrome domain) plus a "%1s" number. The
// lookup is here for the base-class contract (Panel subclasses in this
// tree all carry one) and for the window title.
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- Layout tokens (panel_player.cpp:37-63, the ones this block owns) ---
constexpr int kPanelW   = 378;     // HTML: .op width:378px — the SOURCE
                                   // panel's width, carried only as the
                                   // reference the extracted numbers came
                                   // from. This window never sets a
                                   // content size of 378.
constexpr int kMargin   = 11;      // HTML: .op padding:11
constexpr int kMbW      = 66;      // HTML: .mb width:66px
constexpr int kMbH      = 60;      // .mb svg ~26px + cn + tm (tightened
                                   // 2026-07-27). The panel reserves this
                                   // for the block (panel_player.cpp:729).
constexpr int kMbGap    = 8;       // HTML gap:8 inside .mantlerow
                                   // (panel_player.cpp:1104)
constexpr int kGapSection = 9;     // HTML: .mantlerow margin-top:9
constexpr int kMantleGap  = 2;     // panel_player.cpp:728
                                   // (kGapSection - kGapBar, so the
                                   // leading gap inside this window is 2)

// ---- Mantle state borders (panel_player.cpp:90-91) ----
constexpr int kMbActiveR = 76;   constexpr int kMbActiveG = 175; constexpr int kMbActiveB = 80;
constexpr int kMbCoolR   = 50;   constexpr int kMbCoolG   = 50;  constexpr int kMbCoolB   = 50;

// panel_player.cpp:239-323 — the HTML .mb box: rounded frame coloured by
// state, centred 32px icon, a name + timer two-line stack, and a vertical
// progress strip on the right edge that fills bottom-up for an active
// mantle and empties as a cooling one recovers.
void drawMantleBox(QPainter &p, const QRectF &box, const QPixmap &icon,
                   const QString &name, const QString &timer,
                   bool active, float progress)
{
    p.setRenderHint(QPainter::Antialiasing);
    const QColor border = active
        ? QColor(kMbActiveR, kMbActiveG, kMbActiveB)
        : QColor(kMbCoolR,   kMbCoolG,   kMbCoolB);
    p.setPen(QPen(border, 1));
    p.setBrush(QColor(22, 24, 26));
    p.drawRoundedRect(box, 3, 3);

    const qreal iconBox = 32.0;
    const QRectF iconRect(box.x() + 4,
                          box.y() + (box.height() - iconBox) / 2.0,
                          iconBox, iconBox);
    if (!icon.isNull())
        p.drawPixmap(iconRect.toRect(), icon);

    // Vertical progress strip on the right edge — 10px wide so the timer
    // state is obvious at a glance, especially during the long cooldown
    // tail of a 270s mantle (e.g. Rocksteady).
    const qreal stripW = 10.0;
    const qreal padY = 3.0;
    const QRectF stripRect(box.right() - stripW - 4.0,
                          box.top() + padY,
                          stripW,
                          box.height() - 2 * padY);
    if (stripRect.width() > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(34, 36, 38));             // dark track
        p.drawRoundedRect(stripRect, 2.0, 2.0);
        const qreal clamped = std::clamp(progress, 0.0F, 1.0F);
        const qreal fillH = stripRect.height() * clamped;
        const QRectF fillRect(stripRect.x(),
                              stripRect.bottom() - fillH,
                              stripRect.width(),
                              fillH);
        QColor barColor = active
            ? QColor(110, 200, 230)                  // teal accent
            : QColor(80, 197, 183);                   // also teal during cool-up
        QLinearGradient grad(fillRect.bottomLeft(),
                             fillRect.topLeft());
        grad.setColorAt(0.0, barColor.darker(140));
        grad.setColorAt(1.0, barColor);
        p.setBrush(grad);
        p.drawRoundedRect(fillRect, 2.0, 2.0);
    }

    // Name + timer column (left of the vertical strip). Two-line stack,
    // vertically centred as a block so the visual mass matches the icon.
    const qreal colRight = stripRect.x() - 4.0;
    const qreal colLeft  = iconRect.right() + 6;
    const qreal colW     = std::max<qreal>(0.0, colRight - colLeft);

    const qreal lineGap  = 2.0;
    const qreal nameH    = 11.0;
    const qreal timerH   = 12.0;
    const qreal blockH   = nameH + lineGap + timerH;
    const qreal blockTop = box.y() + (box.height() - blockH) / 2.0;

    QFont cnFont(QStringLiteral("Chakra Petch"), 8);
    cnFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(cnFont);
    p.setPen(QColor(220, 222, 224));
    const QRectF cnRect(colLeft, blockTop, colW, nameH);
    p.drawText(cnRect, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(cnFont).elidedText(name, Qt::ElideRight,
                                                static_cast<int>(colW)));

    QFont tmFont(QStringLiteral("Chakra Petch"), 9, QFont::Medium);
    tmFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(tmFont);
    p.setPen(active ? QColor(110, 200, 230) : QColor(150, 150, 150));
    const QRectF tmRect(colLeft, blockTop + nameH + lineGap, colW, timerH);
    p.drawText(tmRect, Qt::AlignLeft | Qt::AlignVCenter, timer);
}

// The name + timer columns' width inside ONE box, in logical px. This is
// the number contentWidth() measures a mantle name against, and it is the
// same column arithmetic drawMantleBox() uses: 4 + icon 32 + 6 on the
// left, the 10px strip + 4 on the right, and 2 x 1px border. Measuring it
// separately from the painter's own arithmetic is how the panel ends up
// with a name that elides one glyph earlier than the measurement said.
[[nodiscard]] int mantleNameColumnWidth(int boxWidth)
{
    const qreal iconBox = 32.0;
    const qreal stripW = 10.0;
    const qreal colLeft = 4.0 + iconBox + 6.0;
    const qreal colRight = static_cast<qreal>(boxWidth) - stripW - 4.0;
    return static_cast<int>(std::max<qreal>(0.0, colRight - colLeft));
}

} // namespace

namespace mhw {

// The pure transformation.
//
// The World arm reads both mantle slots; the Rise arm reads none, on
// purpose. Rise has no mantle system, so the Rise reader never fills
// `mantleSlot*Id` — but relying on that is relying on a fact about a
// DIFFERENT module staying true, and it is the same trap the audit records
// twice (panel_player.cpp:649 gates the panel on `game_ != Rise` rather
// than on the data, and panel_player_metrics.h:56-60 replaced a ternary
// that made a third game inherit the World arm). So the switch is the
// seam, not the data: a Rise snapshot carrying World-shaped mantle fields
// answers empty, and a third game has to be given an arm explicitly
// rather than borrowing this one.
//
// `active` and the progress denominator follow panel_player.cpp:1110-1122
// and :1125-1137: a positive timer means running (progress = remaining /
// 120s, the common active duration); otherwise the slot is cooling and
// progress = 1 - cooldown / cooldownMax, which starts near 0 and fills
// back toward 1 as the mantle becomes available again.
MantleModel buildMantleModel(const mhw::GameSnapshot &snap)
{
    MantleModel model;

    // A slot with an unresolved id (<0) contributes nothing — the panel
    // keeps box 0 for slot 0 and box 1 for slot 1, so dropping an
    // unequipped slot must not renumber the equipped one into its place.
    switch (snap.game) {
    case mhw::GameId::World: {
        const mhw::PlayerSnapshot &pl = snap.player;
        const int ids[2]    = {pl.mantleSlot0Id, pl.mantleSlot1Id};
        const float tmrs[2]  = {pl.mantleSlot0Timer, pl.mantleSlot1Timer};
        const float cds[2]   = {pl.mantleSlot0Cooldown,
                                pl.mantleSlot1Cooldown};
        const float cdMax[2] = {pl.mantleSlot0CooldownMax,
                                pl.mantleSlot1CooldownMax};
        for (int i = 0; i < 2; ++i) {
            if (ids[i] < 0)
                continue;
            const bool active = (tmrs[i] > 0.0F);
            const float progress = active
                ? (tmrs[i] / mantleProgressDenominator())
                : ((cdMax[i] > 0.0F)
                       ? (1.0F - cds[i] / cdMax[i])
                       : 0.0F);
            MantleRow row;
            row.id = ids[i];
            row.active = active;
            row.name = mhw::mantleName(ids[i]);
            row.timer = QStringLiteral("%1s").arg(
                static_cast<int>(active ? tmrs[i] : cds[i]));
            row.progress = std::clamp(progress, 0.0F, 1.0F);
            model.boxes.append(row);
        }
        break;
    }
    case mhw::GameId::Rise:
        // No mantle system. Reads nothing on purpose — see the comment
        // above on why this is not "the Rise reader leaves the fields
        // alone".
        break;
    }

    return model;
}

} // namespace mhw

PlayerMantlesStripPanel::PlayerMantlesStripPanel(QWidget *parent)
    : Panel(QStringLiteral("player-mantles"), Corner::TopLeft, parent)
{
    // No chrome, no title row: two mantle boxes are a fragment of the
    // player panel, not a panel. setChromeVisible(false) suppresses the
    // v0.3 rounded body, left accent stripe and border entirely, so the
    // window surface stays transparent apart from the boxes' own dark
    // bodies. isSubWindow() (below) declares the same fact for the
    // console's slot registry.
    setChromeVisible(false);

    // Own window-title key, not ui.player_status_section (the label a
    // nameless status pill falls back to) and not ui.player_title (that
    // is PlayerPanel's own, and two windows answering one title key
    // makes the console's inspector row ambiguous).
    setWindowTitle(mh::tr("ui.player_strip_title"));
}

void PlayerMantlesStripPanel::update(const mhw::GameSnapshot &snap)
{
    // Same shape as PlayerPanel::update(): cache what paintPanel() needs,
    // schedule one repaint. PlayerPanel's other mirrored fields (zone,
    // quest, connection, MR/name/party) are not read here — this window
    // draws none of them, so the panel's ViewModel is not pulled in
    // either.
    rows_ = mhw::buildMantleModel(snap);

    // Demo boxes are never live state (the rule PetDamagePanel and the
    // pilot strip both apply): the first authoritative snapshot replaces
    // them outright, so an edit-mode preview cannot survive into a live
    // session.
    demoData_ = false;

    triggerUpdate();
}

void PlayerMantlesStripPanel::retranslateUi()
{
    // Cached strings first, exactly like the pilot's retranslateUi() and
    // for the same reason: main.cpp's applyLocale re-sets the title from
    // the same key, so the second set is the deliberate redundancy every
    // panel carries. The demo ids are resolved through the localized
    // data.mantle.id.<n> table when setupDemoData() builds them, so the
    // demo seed IS dropped here to rebuild the names in the new locale.
    setWindowTitle(mh::tr("ui.player_strip_title"));
    if (editMode())
        resetDemoPrimed();
    triggerUpdate();
}

bool PlayerMantlesStripPanel::mantlesEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing PlayerSection::Mantles means the
    // console's existing player-section toggle drives this window for
    // free — a user who has already turned the mantle row off in the
    // single panel gets the same layout here, with no new console UI.
    return (sectionMask() & mhw::PlayerSection::Mantles) != 0;
}

bool PlayerMantlesStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    if (!mantlesEnabled())
        return false;
    // Empty on Rise (the reader fills no mantle slot) and whenever both
    // slots are unequipped. Either way the window must not exist: the
    // main loop's hasVisibleContent() answers false and the layer-shell
    // surface is never mounted.
    return !rows_.isEmpty();
}

int PlayerMantlesStripPanel::contentHeight() const
{
    // One row of kMbH boxes. The panel reserves kMantleGap + kMbH (2 + 60,
    // panel_player.cpp:728-729) and this window keeps the same 60 so a
    // box drawn side by side with the panel is the same size.
    return kMargin + kMantleGap + kMbH + kMargin;
}

int PlayerMantlesStripPanel::contentWidth() const
{
    // Content width, not a chrome width: the two boxes laid out with the
    // panel's own 8px inter-box gap, sized so the widest localized name
    // fits in the widest box's name column without eliding more glyphs
    // than the panel's own layout would.
    //
    // Start from the panel's per-box width for the current slot count
    // (panel_player.cpp:1105 — slotW = (innerW - 8*(n-1)) / n with
    // innerW = 356), then take the name column out of it and compare with
    // the widest name we are about to draw. A shorter name keeps the
    // panel's box width (so the two windows line up); a longer one widens
    // the window rather than eliding a label the panel would show.
    const int n = rows_.boxes.size();
    if (n <= 0)
        return 2 * kMargin;
    if (!mantlesEnabled())
        return 2 * kMargin;

    const int panelInnerW = kPanelW - 2 * kMargin;      // 356
    const int slotW = (panelInnerW - kMbGap * (n - 1)) / n;

    const QFont nameFont(QStringLiteral("Chakra Petch"), 8);
    const QFontMetrics nameMetrics(nameFont);
    int nameNeeded = 0;
    for (const mhw::MantleRow &row : rows_.boxes)
        nameNeeded = std::max(nameNeeded,
                              nameMetrics.horizontalAdvance(row.name));
    const int colAvailable = mantleNameColumnWidth(slotW);
    const int extra = std::max(0, nameNeeded - colAvailable);

    // Two boxes (or one), each at least the panel's width, plus the gap
    // between them and the margin on both sides.
    return n * slotW + kMbGap * (n - 1) + 2 * kMargin + extra;
}

void PlayerMantlesStripPanel::setupDemoData()
{
    // Edit-mode preview: both slots equipped, one running and one cooling,
    // so the border colour, the teal stripe colour and the progress strip
    // in both directions are all visible. The ids are real table entries
    // so the localized name resolves exactly like a live read would
    // (0 = Ghillie, 3 = Rocksteady — the same pair PlayerPanel's own demo
    // seeds at panel_player.cpp:1327-1331), and the timers match that
    // seed's numbers so the two windows' previews agree.
    mhw::PlayerSnapshot pl;
    pl.valid = true;
    pl.mantleSlot0Id = 0;
    pl.mantleSlot0Timer = 42.0F;
    pl.mantleSlot0Cooldown = 0.0F;
    pl.mantleSlot1Id = 3;
    pl.mantleSlot1Cooldown = 96.0F;
    pl.mantleSlot1Timer = 0.0F;
    mhw::GameSnapshot snap;
    snap.game = mhw::GameId::World;
    snap.player = pl;
    rows_ = mhw::buildMantleModel(snap);
    demoData_ = true;
}

void PlayerMantlesStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an unmounted-but-alive
    // window never renders a leftover sliver. The live path never reaches
    // here — the main loop asks hasVisibleContent() first. This is the
    // edit-preview / defensive branch.
    if (!mantlesEnabled() || rows_.isEmpty()) {
        setContentSize(2 * kMargin, h);
        return;
    }

    // setContentSize has an idempotent guard inside Panel, so calling it
    // per repaint (and therefore per drag frame) is free.
    setContentSize(w, h);

    // Chrome: NONE. drawV03Chrome() is deliberately not called — the two
    // boxes carry their own dark bodies, and a panel frame around them is
    // the shape the split exists to remove. This panel calls neither
    // drawV03Chrome() nor paintPanelChrome(), exactly like the pilot
    // status strip.

    const int n = rows_.boxes.size();
    const int innerW = w - 2 * kMargin;
    const int slotW = (innerW - kMbGap * (n - 1)) / n;

    int y = kMargin + kMantleGap;
    for (int i = 0; i < n; ++i) {
        const mhw::MantleRow &row = rows_.boxes[i];
        const int cx = kMargin + i * (slotW + kMbGap);
        const QRectF box(cx, y, slotW, kMbH);
        // Icon path first: a missing resource yields a null pixmap, which
        // drawMantleBox() already handles by drawing the box without one.
        const QPixmap icon = mhw::Icon::render(
            mhw::Icon::mantlePath(row.id), 32);
        drawMantleBox(p, box, icon, row.name, row.timer, row.active,
                      row.progress);
    }
}
