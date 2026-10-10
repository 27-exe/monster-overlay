// SPDX-License-Identifier: Apache-2.0
//
// MonsterAilStripPanel implementation. Every drawing sub-routine below
// is this file's own small copy of the one it names in panel_monster.cpp,
// written out in full on purpose: drawSc() lives in that file's
// anonymous namespace and is unreachable from here, and hoisting it into
// a shared header would mean editing a file this change may not touch.
// The copy is confined to the block being extracted, so it disappears
// the day the two merge again.

#include "panel_monster_split_ail.h"

#include "core/game_tables.h"
#include "core/string_table.h"
#include "monster/monster_types.h"
#include "ui/formatters.h"
#include "ui/icon.h"
// MonsterSection::Ail — the mask axis, the same bit MonsterPanel reads
// for this block (panel_monster.cpp:967).
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm (kSplitMargin, kSplitGapWithin) this window
// answers contentMargin() and builds its grid from.
#include "ui/panel_player_split_layout.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QPixmap>
#include <QPainter>

#include <algorithm>
#include <cmath>

// i18n lookup, defined the same way panel_monster.cpp:59-63 defines it.
// This file asks for no keys of its own — the card names arrive already
// translated on the snapshot, and the timer text is a bare number with a
// unit. The lookup is here so a future label goes through the table
// rather than an inline zh literal.
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- Layout tokens (panel_monster.cpp:94-101, the ones this block owns) --
constexpr int kMargin  = mhw::split_layout::kSplitMargin;
constexpr int kScGap   = 5;    // .srow gap (panel_monster.cpp:95)
constexpr int kScPadX  = 6;    // .sc padding (panel_monster.cpp:96)
constexpr int kScPadY  = 4;    // panel_monster.cpp:97
constexpr int kScTopFont  = 9; // .sc .top (panel_monster.cpp:98)
constexpr int kScTmFont   = 9; // .sc .tm (panel_monster.cpp:99)
constexpr int kScMiniH    = 5; // .sc .mini height (panel_monster.cpp:100)
constexpr int kScIconSize = 11; // .sc .top svg (panel_monster.cpp:101)

// The cell height the panel's two sites agree on: kScPadY + kScTopFont +
// 4 + kScMiniH + kScPadY. Spelled out here as the sum of the tokens
// above plus the same 4 the panel's `kScCellH` uses, so the window's
// row pitch and the panel's cannot drift apart independently.
constexpr int kScCellH = kScPadY + kScTopFont + 4 + kScMiniH + kScPadY;

// Reader values reach these drawings twice sanitized: once in the model,
// once by the clamp they drive. Same helper as panel_monster.cpp:70.
inline float sanitizeQ10(float v) { return std::isfinite(v) ? v : 0.0F; }

// panel_monster.cpp:229-285, "drawSc" — the .sc card: background +
// state border, optional svg icon, the elided name, the right-aligned
// timer text, and the 5px mini track with its coloured fill. Copied
// verbatim apart from being reachable from this translation unit, and
// apart from the name column's reserved width, which this window
// measures from its own card width rather than the panel's.
void drawSc(QPainter &p, const QRectF &cell, const mhw::MonsterAilCard &e)
{
    p.setRenderHint(QPainter::Antialiasing);

    // 1) Card background + border. .sc[data-state=active] uses --sc,
    // otherwise --line.
    const QColor borderCol = e.active ? e.sc : QColor(42, 45, 47);
    p.setPen(QPen(borderCol, 1));
    p.setBrush(QColor(29, 32, 34));          // --cell
    p.drawRoundedRect(cell, 2, 2);

    // 2) .top: optional svg + name, baseline-aligned.
    const int topY     = static_cast<int>(cell.top()) + kScPadY;
    const int topRectH = kScTopFont + 2;
    const int iconSize = kScIconSize;
    const int iconX    = static_cast<int>(cell.x()) + kScPadX;
    const int iconY    = topY + (topRectH - iconSize) / 2;
    if (!e.iconPath.isEmpty()) {
        const QPixmap icon = mhw::Icon::render(e.iconPath, iconSize);
        if (!icon.isNull())
            p.drawPixmap(iconX, iconY, icon);
    }

    QFont nmFont(QStringLiteral("Chakra Petch"), kScTopFont);
    nmFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(nmFont);
    p.setPen(e.active ? e.sc : QColor(200, 205, 208));   // --t2
    const int nameX = iconX + (e.iconPath.isEmpty() ? 0 : iconSize + 4);
    const int nameRight = static_cast<int>(cell.right()) - kScPadX - 36;
    p.drawText(QRectF(nameX, topY, nameRight - nameX, topRectH),
               Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(nmFont).elidedText(
                   e.name, Qt::ElideRight, nameRight - nameX));

    // 3) .tm: 9px Chakra Bold, color --sc, right:5 top:4 inside the cell.
    QFont tmFont(QStringLiteral("Chakra Petch"), kScTmFont, QFont::Bold);
    tmFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(tmFont);
    p.setPen(e.active ? e.sc : QColor(140, 142, 144));
    p.drawText(QRectF(static_cast<int>(cell.right()) - kScPadX - 32,
                      topY, 32, topRectH),
               Qt::AlignRight | Qt::AlignVCenter, e.tm);

    // 4) .mini: 5px track + coloured fill, anchored to the bottom padding.
    const int miniY = static_cast<int>(cell.bottom()) - kScPadY - kScMiniH;
    const QRectF miniRect(cell.x() + kScPadX, miniY,
                          cell.width() - 2 * kScPadX, kScMiniH);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(10, 11, 12));
    p.drawRect(miniRect);
    const float clamped = std::clamp(e.pct, 0.0F, 1.0F);
    if (clamped > 0.001F) {
        p.setBrush(e.sc);
        p.drawRect(miniRect.x(), miniRect.y(),
                   miniRect.width() * clamped, miniRect.height());
    }
}

} // namespace

namespace mhw {

MonsterAilModel buildMonsterAilModel(
    const QVector<mhw::MonsterAilmentSnapshot> &ailments,
    mhw::GameId game, bool editMode, qint64 nowMs,
    MonsterAilmentTracker &tracker)
{
    MonsterAilModel model;
    model.cards.reserve(ailments.size());

    for (const auto &a : ailments) {
        // One call to the shared visibility rule, so the reservation and
        // the draw can never disagree about which cards exist — the
        // v0.10.3 lesson (a strip drawn at one gate and reserved at
        // another). It ADVANCES the tracker, which is why this function
        // takes a mutable reference and the caller must run it once per
        // paint.
        if (!tracker.isVisible(a, editMode, nowMs))
            continue;

        MonsterAilCard c;
        c.name = a.name;
        // --sc + icon path are looked up by ailment id; trap items keep
        // their distinctive yellow/green from HunterPie. World and Rise
        // use UNRELATED id tables — Rise 4 is poison, World 2 is
        // paralysis — so this asks the game-keyed registry rather than
        // branching here.
        const mhw::AilmentStyle style = mhw::ailmentStyle(game, a.id);
        c.sc = QColor::fromRgba(style.statusColorRgba);
        c.iconPath = style.trapIconPath;

        // The card is "active" the moment the monster is being affected:
        // the ailment has triggered (timer counting down), or the
        // build-up is in progress (mini bar moving). Same rule as the
        // panel's :944-947.
        const bool timerRunning = a.timer > 0.0F;
        const bool buildupKnown = a.buildup > 0.0F && a.maxBuildup > 0.0F;
        c.active = timerRunning || buildupKnown;

        // Mini progress + timer text. Order matches the HTML examples:
        //   active + maxTimer     → "Ns" (countdown)
        //   else + maxBuildup     → "N%" (buildup fill)
        if (timerRunning && a.maxTimer > 0.0F) {
            // HunterPie exposes the remaining seconds directly; round UP
            // so a 0.4s tail still shows "1s" instead of a sticky "0s"
            // (the card disappears the moment timer hits 0 anyway).
            c.pct = std::clamp(sanitizeQ10(a.timer) / sanitizeQ10(a.maxTimer),
                               0.0F, 1.0F);
            c.tm  = QStringLiteral("%1s").arg(
                        static_cast<int>(std::ceil(sanitizeQ10(a.timer))));
        } else if (buildupKnown) {
            c.pct = std::clamp(sanitizeQ10(a.buildup)
                                   / sanitizeQ10(a.maxBuildup),
                               0.0F, 1.0F);
            c.tm  = QStringLiteral("%1%").arg(static_cast<int>(c.pct * 100));
        } else {
            c.pct = 0.0F;
            c.tm  = QString::fromLatin1("");
        }
        model.cards.append(c);
    }
    return model;
}

} // namespace mhw

MonsterAilStripPanel::MonsterAilStripPanel(QWidget *parent, int subWindowIndex)
    : Panel(QStringLiteral("monster-ail"), Corner::TopLeft, parent,
        subWindowIndex)
{
    // No chrome, no title row: a card grid is a fragment of the monster
    // panel, not a panel. The translucent body still comes from
    // Panel::paintEvent(), which is what keeps a fragment legible over
    // the game — see panel.h's v0.12.1 note on what the chrome switch
    // governs (the FRAME alone, never the surface).
    setChromeVisible(false);

    // Own window-title key (distinct from ui.monster_title, the panel's
    // own). The grid shows no cap of its own, so the key is the one
    // main.cpp's applyLocale re-sets on a locale flip.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
}

void MonsterAilStripPanel::update(const mhw::MonsterSnapshot &m)
{
    // The same identity-change reset the panel's update() issues on both
    // of its trackers (panel_monster.cpp:790-796): switching target
    // replaces the whole snapshot, and a stale (sig, stampMs) from the
    // previous monster would judge the new monster's card "already
    // silent" (hidden on its first paint) or "already fresh" (pinned for
    // a spurious 15 s).
    if (m.address != cards_.address) { tracker_.resetAutoHide(); }
    // `address` is refreshed on every tick and is the same key the panel
    // uses, so this fires exactly once per target switch.
    cards_.address = m.address;

    // ONE clock sample for this whole paint, exactly like the panel's
    // `const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();`
    // (panel_monster.cpp:887) — so every card in one frame is compared
    // against one timestamp instead of N.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    cards_ = mhw::buildMonsterAilModel(m.ailments, m.game, editMode(), nowMs,
                                       tracker_);
    cards_.address = m.address;
    game_ = m.game;

    // Demo cards are never live state: the first authoritative snapshot
    // replaces them outright, so an edit-mode preview cannot survive
    // into a live session.
    demoData_ = false;

    triggerUpdate();
}

void MonsterAilStripPanel::retranslateUi()
{
    // Cached strings first, for the reason the pilot's retranslateUi()
    // states: main.cpp's applyLocale re-sets the title from the same
    // key, so the second set is deliberate redundancy. The card names
    // arrive on the snapshot and are re-resolved by the reader, so there
    // is nothing locale-dependent cached here to rebuild.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
    triggerUpdate();
}

void MonsterAilStripPanel::setGameForDemo(mhw::GameId game)
{
    if (game_ == game)
        return;
    game_ = game;
    // Drop the one-shot demo seed so the next paint re-runs
    // setupDemoData() with the new game's ailment ids and palette.
    resetDemoPrimed();
    triggerUpdate();
}

void MonsterAilStripPanel::setDemoGame(mhw::GameId game)
{
    // Delegate to the non-virtual the live overlay calls. The two names
    // exist because the two call sites need two different dispatch
    // mechanisms: main.cpp constructs this window as a concrete stack
    // object and calls setGameForDemo() directly; the Console's split
    // preview host holds one as std::unique_ptr<Panel> and can only
    // reach it virtually. Both land in the same body, so the preview and
    // the live overlay cannot disagree about which game the demo
    // represents — the disagreement the user reported as
    // 「Rise 预览显示衣装、World 预览显示翔虫」.
    setGameForDemo(game);
}

void MonsterAilStripPanel::setupDemoData()
{
    // Edit-mode preview: the four cards the panel's own demo seeds
    // (panel_monster.cpp:1457-1467), so the preview and the panel agree
    // on what a real hunt looks like:
    //   - 麻痹:        active, timer counts down (12s remaining)
    //   - 麻痹陷阱:    active, timer counts down (8s remaining)
    //   - 落穴:        active, timer counts down (60s remaining)
    //   - 毒:          not active, but has 30% build-up and 2 triggers
    //
    // THE IDS ARE GAME-KEYED, AND THIS IS THE BUG THE USER REPORTED.
    //
    // The rows above are the panel's own World ids, and the panel gets
    // away with them because MonsterPanel::setupDemoData() NEVER writes
    // m.game — its snapshot stays GameId::World and every id resolves.
    //
    // This window carries `game_` (moved by setDemoGame()/setGameForDemo,
    // because the ailments' palette is game-keyed) while seeding the demo
    // with the PANEL's ids. Under the Rise rail those ids ask the Rise
    // palette and two of the four are not in it — Rise 落穴 is id 12, not
    // 15, and Rise 麻痹陷阱 is id 13, not 14. ailmentStyle() then answers
    // its neutral grey for both, and the card that should have carried
    // the trap SVG carries no icon at all: the 「全变成灰色框框」 report,
    // visible only in split mode because the mother panel never left
    // World.
    //
    // So seed each game's OWN ids, which asks the ids and the palette the
    // same question. The labels are locale-keyed and game-independent
    // (data.demo.ail.*), so only the ids move — to the ids of the same
    // four ailments in the other game.
    struct Ail { int id; QString name; float timer; float maxT;
                float bu; float maxB; int cnt; bool active; };
    const Ail demoAilWorld[] = {
        { 2, mh::tr("data.demo.ail.paralysis"), 12.0F, 20.0F,  0.0F, 0.0F, 1, true },
        {14, mh::tr("data.demo.ail.shock_trap"),  8.0F,  8.0F,  0.0F, 0.0F, 0, true },
        {15, mh::tr("data.demo.ail.pitfall"),   60.0F, 60.0F,  0.0F, 0.0F, 0, true },
        { 1, mh::tr("data.demo.ail.poison"),     0.0F,  0.0F, 30.0F, 100.0F, 2, false},
    };
    // The same four ailments in Rise's numbering
    // (src/rise/part_schemas.cpp kRiseAilmentStyles): 眩晕 2,
    // 麻痹陷阱 13 (yellow + trap icon), 落穴 12 (green + trap icon),
    // 毒 4. Every id is in the Rise table, so every card resolves and
    // the two traps keep their icons.
    const Ail demoAilRise[] = {
        { 2, mh::tr("data.demo.ail.paralysis"), 12.0F, 20.0F,  0.0F, 0.0F, 1, true },
        {13, mh::tr("data.demo.ail.shock_trap"),  8.0F,  8.0F,  0.0F, 0.0F, 0, true },
        {12, mh::tr("data.demo.ail.pitfall"),   60.0F, 60.0F,  0.0F, 0.0F, 0, true },
        { 4, mh::tr("data.demo.ail.poison"),     0.0F,  0.0F, 30.0F, 100.0F, 2, false},
    };
    const Ail *demoAil = demoAilWorld;
    int demoAilCount = int(sizeof(demoAilWorld) / sizeof(demoAilWorld[0]));
    if (game_ == mhw::GameId::Rise) {
        demoAil = demoAilRise;
        demoAilCount = int(sizeof(demoAilRise) / sizeof(demoAilRise[0]));
    }

    QVector<mhw::MonsterAilmentSnapshot> ailments;
    for (int i = 0; i < demoAilCount; ++i) {
        const Ail &a = demoAil[i];
        mhw::MonsterAilmentSnapshot ail;
        ail.id         = a.id;
        ail.name       = a.name;
        ail.active     = a.active;
        ail.timer      = a.timer;
        ail.maxTimer   = a.maxT;
        ail.buildup    = a.bu;
        ail.maxBuildup = a.maxB;
        ail.counter    = a.cnt;
        ailments.append(ail);
    }
    // Edit mode, so every card is pinned — the same rule the panel's
    // tracker applies (`bool recent = true` for the edit-mode demo).
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    cards_ = mhw::buildMonsterAilModel(ailments, game_, true, nowMs, tracker_);
    demoData_ = true;
}

bool MonsterAilStripPanel::ailEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing MonsterSection::Ail means the
    // console's existing monster toggle drives this window for free.
    return (sectionMask() & mhw::MonsterSection::Ail) != 0;
}

bool MonsterAilStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    // With the mask bit off, or with every card auto-hidden, there is
    // nothing to show, so the window must not exist: the main loop's
    // hasVisibleContent() answers false and the layer-shell surface is
    // never mounted.
    if (!ailEnabled())
        return false;
    return !cards_.isEmpty();
}

void MonsterAilStripPanel::setCardsPerRow(int perRow)
{
    // Clamped to the range the width arithmetic can express: 0 would
    // make monsterAilRows() divide by zero and contentWidth() answer a
    // 0-wide grid, and above 6 the window is wider than the panel it
    // was carved out of.
    const int clamped = std::clamp(perRow, mhw::kMonsterAilMinColumns,
                                  mhw::kMonsterAilMaxColumns);
    if (clamped == columns_)
        return;
    columns_ = clamped;
    triggerUpdate();
}

void MonsterAilStripPanel::nudgeColumns(int delta)
{
    setCardsPerRow(columns_ + Panel::kColumnsStep * (delta > 0 ? 1 : -1));
}

int MonsterAilStripPanel::naturalItemWidth() const
{
    // The panel's own .srow column width: (innerW - gap*(cols-1)) / cols,
    // with its innerW = kPanelWidth - 2*kPanelPad. Carried as the
    // NATURAL width because that is what a layout compares against.
    constexpr int kPanelWidth = 380;
    constexpr int kPanelPad   = 9;
    const int innerW = kPanelWidth - 2 * kPanelPad;
    const int perRow = columns_;
    const int w = (innerW - kScGap * (perRow - 1)) / perRow;
    return w > 0 ? w : 0;
}

int MonsterAilStripPanel::itemGap() const
{
    return kScGap;
}

int MonsterAilStripPanel::rowPitch() const
{
    // Card height plus the gap between two wrapped rows — the height a
    // row actually costs, which is what a layout needs to reserve.
    return kScCellH + kScGap;
}

int MonsterAilStripPanel::slotWidth() const
{
    // The exact inverse of contentWidth(): with `drawnW` available
    // between the two insets, one card is drawn at this width so the
    // columns fill it. Uses panel_sizing's own gridSlotWidth rather
    // than a second spelling of the division.
    const int available = contentWidth() - 2 * kMargin;
    return mhw::panel_sizing::gridSlotWidth(available, columns_, kScGap);
}

int MonsterAilStripPanel::gridBlockHeight() const
{
    // The grid's own height, not including the outer margins: rows of
    // cards with the gap between adjacent pairs. Through
    // panel_sizing's gridContentHeight so the arithmetic is the same one
    // every panel uses.
    const int rows = mhw::monsterAilRows(cards_.cards.size(), columns_);
    return mhw::panel_sizing::gridContentHeight(rows, kScCellH, kScGap);
}

int MonsterAilStripPanel::contentWidth() const
{
    const int count = cards_.cards.size();
    if (count <= 0 || !ailEnabled())
        return 2 * kMargin;
    // What the grid's CELLS need: `columns_` cards of the natural width
    // plus (columns_-1) gaps. Through panel_sizing's gridContentWidth,
    // the same function the panels use, so a reservation made here and a
    // slot width computed with gridSlotWidth are exact inverses.
    const int perRow = std::min(columns_, count);
    return mhw::panel_sizing::gridContentWidth(count, perRow,
                                               naturalItemWidth(), kScGap)
           + 2 * kMargin;
}

int MonsterAilStripPanel::contentHeight() const
{
    // With no cards the window answers the collapsed size the same way
    // every grid does (panel_sizing's gridEmptySize), never a 0-height
    // surface.
    if (cards_.isEmpty())
        return mhw::panel_sizing::gridEmptySize(kMargin).height;
    return gridBlockHeight() + 2 * kMargin;
}

void MonsterAilStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an
    // unmounted-but-alive window never renders a leftover sliver. The
    // live path never reaches here — the main loop asks
    // hasVisibleContent() first.
    if (!ailEnabled() || cards_.isEmpty()) {
        setContentSize(2 * kMargin, h);
        return;
    }

    setContentSize(w, h);

    // Re-read the width the window actually got: setContentSize()
    // applied the user's minimum-width floor, but `w` is still the
    // pre-floor content number.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE. drawV03Chrome() is deliberately not called — the
    // translucent body still comes from Panel::paintEvent().
    const int innerW = drawnW - 2 * kMargin;
    const int innerX = kMargin;

    const int cellW = mhw::panel_sizing::gridSlotWidth(innerW, columns_,
                                                       kScGap);
    // A budget too small to hold even the gaps has no room for a card.
    // panel_sizing's gridOverflows() handshake exists for exactly this
    // case, and answering 0 here rather than drawing a negative-width
    // rect is what keeps it honest.
    if (cellW <= 0)
        return;

    const int perRow = columns_ > 0 ? columns_ : 1;
    int y = kMargin;
    for (int i = 0; i < cards_.cards.size(); ++i) {
        const int row = i / perRow;
        const int col = i % perRow;
        if (col == 0 && row > 0)
            y += kScCellH + kScGap;
        const int cx = innerX + col * (cellW + kScGap);
        drawSc(p, QRectF(cx, y, cellW, kScCellH), cards_.cards[i]);
    }
}
