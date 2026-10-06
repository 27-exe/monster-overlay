// SPDX-License-Identifier: Apache-2.0
//
// MonsterStaminaStripPanel implementation. Every drawing sub-routine
// below is this file's own small copy of the one it names in
// panel_monster.cpp, written out in full on purpose: those live in that
// file's anonymous namespace and are unreachable from here, and hoisting
// them into a shared header would mean editing a file this change may
// not touch. The copy is confined to the block being extracted, so it
// disappears the day the two merge again.

#include "panel_monster_split_stamina.h"

#include "core/string_table.h"
#include "monster/monster_types.h"
// MonsterSection::Stamina — the mask axis, the bit this window exists
// for (see the header's opening comment).
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm (kSplitMargin, the gap grades) this window
// answers contentMargin() and builds its block height from.
#include "ui/panel_player_split_layout.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>

#include <algorithm>
#include <cmath>

// i18n lookup, defined the same way panel_monster.cpp:59-63 defines it.
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- Layout tokens (panel_monster.cpp:76-99, the ones this block owns) --
constexpr int kMargin   = mhw::split_layout::kSplitMargin;
constexpr int kStamBarH = 10;   // .bar.st — monster stamina (fatigue)
                                // meter. HunterPie draws this gauge 10px
                                // tall directly under the HP bar
                                // (panel_monster.cpp:86-88).
constexpr int kBarVPad  = 3;    // Vertical padding inside .bar
                                // (panel_monster.cpp:89). Visual height
                                // = kStamBarH + 2*kBarVPad = 16.

// The bar's label and number font pair, and the insets they sit at. The
// panel's .bar.st block uses 8px for both, an 8px inset on the label's
// left and an 8px inset on the number's right.
constexpr int kStamFontSize  = 8;
constexpr int kBarTextInset  = 8;

// ---- Colour palette (panel_monster.cpp:112) ----
// stamina amber. Only ONE colour family: unlike the HP bar there is no
// low/high split, the panel draws this bar in a single amber with a
// lighter top stop.
constexpr int kStamR = 246, kStamG = 165, kStamB = 34;   // stamina:

// panel_monster.cpp:200-221, "drawBarV". The rounded track, the
// gradient fill. Copied verbatim apart from being reachable from this
// translation unit.
void drawBarV(QPainter &p, const QRectF &rect, float pct,
              const QColor &hi, const QColor &lo)
{
    const float clamped = std::clamp(pct, 0.0F, 1.0F);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(29, 32, 34));        // --bg-cell
    p.drawRoundedRect(rect, 2, 2);
    if (clamped > 0.001F) {
        const QRectF fill(rect.x(), rect.y(),
                          rect.width() * clamped, rect.height());
        QLinearGradient grad(fill.topLeft(), fill.bottomLeft());
        grad.setColorAt(0.0, hi);
        grad.setColorAt(1.0, lo);
        p.setBrush(grad);
        p.drawRoundedRect(fill, 2, 2);
    }
}

} // namespace

namespace mhw {

MonsterStaminaModel buildMonsterStaminaModel(const mhw::MonsterSnapshot &m)
{
    MonsterStaminaModel model;
    // maxStamina == 0 is the panel's own condition for hiding the row
    // entirely (panel_monster.cpp's `stamDrawn`), so the window's
    // "nothing to show" answer and the panel's "nothing to draw"
    // answer are the same test. A non-finite reader value must not
    // reach the clamp: sanitizeQ10 does that job in the panel and the
    // same fold is applied here, so a NaN cannot turn into a bar that
    // claims a percent.
    const float maxSafe = std::isfinite(m.maxStamina) ? m.maxStamina : 0.0F;
    const float curSafe = std::isfinite(m.stamina)      ? m.stamina      : 0.0F;
    model.stamina.known = maxSafe > 0.0F;
    model.stamina.pct   = maxSafe > 0.0F
                              ? std::clamp(curSafe / maxSafe, 0.0F, 1.0F)
                              : 0.0F;

    // The numbers, formatted once, here. groupNumber() is the panel's
    // own grouping, so the window and the panel cannot drift on it.
    model.stamina.valueText = model.stamina.known
        ? QStringLiteral("%1 / %2")
              .arg(mhw::groupNumber(static_cast<int>(curSafe)))
              .arg(mhw::groupNumber(static_cast<int>(maxSafe)))
        : QString::fromLatin1(mhw::monsterStaminaPlaceholderValue());
    return model;
}

} // namespace mhw

MonsterStaminaStripPanel::MonsterStaminaStripPanel(QWidget *parent, int subWindowIndex)
    : Panel(QStringLiteral("monster-stamina"), Corner::TopLeft, parent,
        subWindowIndex)
{
    // No chrome, no title row: a fatigue meter is a fragment of the
    // monster panel, not a panel. The translucent body still comes from
    // Panel::paintEvent(), which is what keeps a fragment legible over
    // the game — see panel.h's v0.12.1 note on what the chrome switch
    // governs (the FRAME alone, never the surface).
    setChromeVisible(false);

    // Own window-title key (distinct from ui.monster_title, the panel's
    // own). The bar shows a 体力 label inside it, not a title, so the
    // key is the one main.cpp's applyLocale re-sets on a locale flip.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
}

void MonsterStaminaStripPanel::update(const mhw::MonsterSnapshot &m)
{
    // Same shape as MonsterPanel::update(): cache what paintPanel()
    // needs, schedule one repaint.
    model_ = mhw::buildMonsterStaminaModel(m);

    // Demo numbers are never live state: the first authoritative
    // snapshot replaces them outright, so an edit-mode preview cannot
    // survive into a live session.
    demoData_ = false;

    triggerUpdate();
}

void MonsterStaminaStripPanel::retranslateUi()
{
    // Cached strings first, exactly like the pilot's retranslateUi() and
    // for the same reason: main.cpp's applyLocale re-sets the title from
    // the same key, so the second set is the deliberate redundancy every
    // panel carries. No demo seed is dropped: the demo path writes
    // numbers, and the 体力 label is re-read from the table at paint
    // time rather than cached in the model.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
    triggerUpdate();
}

void MonsterStaminaStripPanel::setGameForDemo(mhw::GameId game)
{
    if (game_ == game)
        return;
    game_ = game;
    // Drop the one-shot demo seed so the next paint re-runs
    // setupDemoData() with the new game.
    resetDemoPrimed();
    triggerUpdate();
}

void MonsterStaminaStripPanel::setupDemoData()
{
    // Edit-mode preview: the meter at a plausible live value, mid-range
    // so the amber fill is visible. Numbers only — the 体力 label comes
    // from the table at paint time — so there is nothing to re-seed on
    // a locale flip.
    //
    // Same numbers MonsterPanel::setupDemoData() uses (700 / 1,000), so
    // the preview and the panel agree on what a real monster looks like.
    mhw::MonsterSnapshot m;
    m.address     = 0xDEADBEEFULL;
    m.id          = 1;
    m.game        = game_;
    m.maxStamina  = 1000.0F;
    m.stamina     = 700.0F;
    model_ = mhw::buildMonsterStaminaModel(m);
    demoData_ = true;
}

bool MonsterStaminaStripPanel::staminaEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing MonsterSection::Stamina means the
    // console's monster-section toggle drives this window for free.
    return (sectionMask() & mhw::MonsterSection::Stamina) != 0;
}

bool MonsterStaminaStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    // With the mask bit off, or with the fatigue pair unresolved, there
    // is nothing to show, so the window must not exist: the main loop's
    // hasVisibleContent() answers false and the layer-shell surface is
    // never mounted.
    if (!staminaEnabled())
        return false;
    return !model_.isEmpty();
}

int MonsterStaminaStripPanel::barBlockHeight() const
{
    // panel_monster.cpp's stamH = kStamBarH + 2 * kBarVPad, plus the
    // margin at both ends. Same arithmetic, same numbers, one owner.
    return kMargin + kStamBarH + 2 * kBarVPad + kMargin;
}

int MonsterStaminaStripPanel::barContentWidth() const
{
    // Content width, not a chrome width: the 体力 label and the number
    // pair, measured in the same font the paint sets, with the bar's
    // 8px inset separating them and its 8px inset on both outer edges,
    // plus the margin on both sides. Measured from the strings the paint
    // will actually draw — the label from the table (so a locale switch
    // resizes the window honestly rather than clipping) and the numbers
    // from the model.
    const QFont stamFont(QStringLiteral("Chakra Petch"), kStamFontSize);
    const QFontMetrics stamMetrics(stamFont);

    if (!staminaEnabled())
        return 2 * kMargin;
    const int widest =
        stamMetrics.horizontalAdvance(mh::tr("ui.monster_stamina"))
        + 2 * kBarTextInset
        + stamMetrics.horizontalAdvance(model_.stamina.valueText);
    if (widest <= 0)
        return 2 * kMargin;
    return widest + 2 * kMargin;
}

int MonsterStaminaStripPanel::contentWidth() const
{
    return barContentWidth();
}

void MonsterStaminaStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = barBlockHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an
    // unmounted-but-alive window never renders a leftover sliver. The
    // live path never reaches here — the main loop asks
    // hasVisibleContent() first.
    if (!staminaEnabled() || model_.isEmpty()) {
        setContentSize(2 * kMargin, h);
        return;
    }

    setContentSize(w, h);

    // Re-read the width the window actually got: setContentSize()
    // applied the user's minimum-width floor, but `w` is still the
    // pre-floor content number. Using it directly is exactly the bug the
    // floor was added to fix — the window would widen and the meter
    // would stay narrow, leaving empty background to the right.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE. drawV03Chrome() is deliberately not called — the
    // translucent body still comes from Panel::paintEvent().
    const int innerW = drawnW - 2 * kMargin;
    const int innerX = kMargin;
    const int y      = kMargin;

    // ---- the fatigue meter ----
    const float pct = model_.stamina.pct;
    const QColor hi(kStamR, kStamG, kStamB);
    const QRectF barRect(innerX, y, innerW, kStamBarH + 2 * kBarVPad);
    drawBarV(p, barRect, pct, hi.lighter(115), hi);

    {
        QFont stamFont(QStringLiteral("Chakra Petch"), kStamFontSize);
        stamFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(stamFont);
        p.setPen(QColor(245, 246, 247));
        const QRectF textRect(innerX, y + kBarVPad, innerW, kStamBarH);
        // Left label, right numbers — the panel's .bar.st layout, verbatim.
        p.drawText(textRect.adjusted(kBarTextInset, 0, 0, 0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   mh::tr("ui.monster_stamina"));
        p.drawText(textRect.adjusted(0, 0, -kBarTextInset, 0),
                   Qt::AlignRight | Qt::AlignVCenter,
                   model_.stamina.valueText);
    }
}
