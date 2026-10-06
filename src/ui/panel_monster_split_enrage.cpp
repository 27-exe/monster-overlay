// SPDX-License-Identifier: Apache-2.0
//
// MonsterEnrageStripPanel implementation. Every drawing sub-routine
// below is this file's own small copy of the one it names in
// panel_monster.cpp, written out in full on purpose: those live in that
// file's anonymous namespace and are unreachable from here, and hoisting
// them into a shared header would mean editing a file this change may
// not touch. The copy is confined to the block being extracted, so it
// disappears the day the two merge again.

#include "panel_monster_split_enrage.h"

#include "core/string_table.h"
#include "monster/monster_types.h"
// MonsterSection::Enrage — the mask axis, the same bit MonsterPanel
// reads for this block (panel_monster.cpp:966).
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"
// The shared split rhythm (kSplitMargin, the gap grades) this window
// answers contentMargin() and builds its block height from.
#include "ui/panel_player_split_layout.h"

#include <QElapsedTimer>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>

#include <algorithm>
#include <cmath>

// i18n lookup, defined the same way panel_monster.cpp:59-63 defines it.
// This file asks for no keys of its own — the bar's text is "MAX" (an
// untranslated token, exactly as the panel draws it) or a bare integer
// percentage. The lookup is here so a future label goes through the
// table rather than an inline zh literal.
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- Layout tokens (panel_monster.cpp:76-99, the ones this block owns) --
constexpr int kMargin   = mhw::split_layout::kSplitMargin;
constexpr int kRageBarH = 6;    // .bar.er visual text band; small strip
                                // with the MAX/% reading centred in it
                                // (panel_monster.cpp:83-85).
constexpr int kBarVPad  = 3;    // Vertical padding inside .bar
                                // (panel_monster.cpp:89). Visual height
                                // = kRageBarH + 2*kBarVPad = 12, which is
                                // the panel's own comment: "so it reads as
                                // a meter, not a second HP bar".
// The pulse period, in ms. panel_monster.cpp:125's kPulsePeriodMs = 1600
// (HTML's .erpulse 1.6s). The window runs its own clock rather than
// borrowing the panel's because the two can be mounted at different
// times and the panel is not in the window's dependency set.
constexpr int kPulsePeriodMs = 1600;

// The bar text's font pair and inset. The panel's .bar.er uses 8px and an
// 8px inset on the right.
constexpr int kRageFontSize = 8;
constexpr int kBarTextInset = 8;

// ---- Colour palette (panel_monster.cpp:110-111) ----
constexpr int kErBar1R = 230, kErBar1G =  74, kErBar1B = 25;   // --c
constexpr int kErBar2R = 255, kErBar2G = 112, kErBar2B = 67;   // --c2
// The right-aligned text colour while the monster is actually enraged.
// panel_monster.cpp:104's kEnrageR/G/B, shared with the .hex stroke.
constexpr int kEnrageR = 255, kEnrageG = 112, kEnrageB = 67;

// panel_monster.cpp:200-221, "drawBarV". Copied verbatim apart from
// being reachable from this translation unit.
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

// Reader values reach these drawings twice sanitized: once in the model,
// once by the clamp they drive. A NaN or Inf that slips through produces
// a percentage like "-2147483648%" instead of a bar, because the cast of
// a non-finite float is platform-defined. Same helper as
// panel_monster.cpp:70.
inline float sanitizeQ10(float v) { return std::isfinite(v) ? v : 0.0F; }

} // namespace

namespace mhw {

MonsterEnrageModel buildMonsterEnrageModel(
    const mhw::MonsterSnapshot &m)
{
    MonsterEnrageModel model;

    // The panel's own two expressions, read field for field
    // (panel_monster.cpp:1314-1320 for the fill and :1346 for the text):
    //
    //   ragePct = maxBuildup > 0 ? clamp(buildup / maxBuildup)
    //                            : (enraged ? 1.0 : 0.0)
    //   text     = (enraged && enrageSeconds > 0) ? "MAX" : "<pct>%"
    //
    // Note what that means for the readings: MAX is decided by the
    // TIMER, not by the fill. A monster enraged with no live seconds is
    // 100 % filled and reads "100%", not "MAX" — which is why the model
    // carries `pulsing` separately from `reading` and why the fill is
    // computed in its own arm rather than inferred from the text. Getting
    // this backwards is how a window ends up printing MAX forever after
    // a rage ends.
    model.pulsing = m.enraged && m.enrageSeconds > 0.0F;
    if (sanitizeQ10(m.enrageMaxBuildup) > 0.0F) {
        model.pct = std::clamp(sanitizeQ10(m.enrageBuildup)
                                   / sanitizeQ10(m.enrageMaxBuildup),
                               0.0F, 1.0F);
    } else {
        // No buildup pair: the panel falls back to a binary on `enraged`,
        // which is the only fill it can honestly express.
        model.pct = m.enraged ? 1.0F : 0.0F;
    }

    // Whether a meter exists at all — the panel's `showRage`
    // (:983: `enrageMaxBuildup > 0 || enraged`), which is what decides
    // whether the block is drawn and therefore whether this window
    // mounts.
    const bool showRage = sanitizeQ10(m.enrageMaxBuildup) > 0.0F
                          || m.enraged;
    model.reading = showRage
        ? (model.pulsing ? MonsterEnrageReading::Max
                         : MonsterEnrageReading::Percent)
        : MonsterEnrageReading::Empty;
    return model;
}

} // namespace mhw

MonsterEnrageStripPanel::MonsterEnrageStripPanel(QWidget *parent, int subWindowIndex)
    : Panel(QStringLiteral("monster-enrage"), Corner::TopLeft, parent,
        subWindowIndex)
{
    // No chrome, no title row: an enrage meter is a fragment of the
    // monster panel, not a panel. The translucent body still comes from
    // Panel::paintEvent(), which is what keeps a fragment legible over
    // the game — see panel.h's v0.12.1 note on what the chrome switch
    // governs (the FRAME alone, never the surface).
    setChromeVisible(false);

    // Own window-title key (distinct from ui.monster_title, the panel's
    // own). The bar shows no label, so the key is the one main.cpp's
    // applyLocale re-sets on a locale flip.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
}

void MonsterEnrageStripPanel::update(const mhw::MonsterSnapshot &m)
{
    // Same shape as MonsterPanel::update(): cache what paintPanel()
    // needs, schedule one repaint — and start or stop the pulse clock on
    // the same condition the panel's own update() does (:799-811), so a
    // non-enraged monster costs nothing.
    model_ = mhw::buildMonsterEnrageModel(m);
    demoData_ = false;
    triggerUpdate();
}

void MonsterEnrageStripPanel::retranslateUi()
{
    // Cached strings first, for the reason the pilot's retranslateUi()
    // states: main.cpp's applyLocale re-sets the title from the same
    // key, so the second set is deliberate redundancy. No demo seed is
    // dropped — the demo path writes numbers only.
    setWindowTitle(mh::tr("ui.monster_strip_title"));
    triggerUpdate();
}

void MonsterEnrageStripPanel::setGameForDemo(mhw::GameId game)
{
    if (game_ == game)
        return;
    game_ = game;
    // Drop the one-shot demo seed so the next paint re-runs
    // setupDemoData() with the new game.
    resetDemoPrimed();
    triggerUpdate();
}

void MonsterEnrageStripPanel::setupDemoData()
{
    // Edit-mode preview: the meter mid-buildup, which exercises both the
    // percentage reading and the gradient fill. The demo is NOT seeded
    // enraged, deliberately: an always-pulsing preview window is exactly
    // the kind of thing a user dismisses as noise, and the pulse path is
    // reachable from a live snapshot.
    //
    // Same buildup values MonsterPanel::setupDemoData() uses, so the
    // preview and the panel agree.
    mhw::MonsterSnapshot m;
    m.address          = 0xDEADBEEFULL;
    m.id               = 1;
    m.game             = game_;
    m.enraged          = false;
    m.enrageMaxBuildup = 100.0F;
    m.enrageBuildup    = 62.0F;
    model_ = mhw::buildMonsterEnrageModel(m);
    demoData_ = true;
}

bool MonsterEnrageStripPanel::enrageEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing MonsterSection::Enrage means the
    // console's existing monster toggle drives this window for free.
    return (sectionMask() & mhw::MonsterSection::Enrage) != 0;
}

bool MonsterEnrageStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    // With the mask bit off, or with no rage meter at all (no buildup
    // pair and not enraged), there is nothing to show, so the window
    // must not exist: the main loop's hasVisibleContent() answers false
    // and the layer-shell surface is never mounted.
    if (!enrageEnabled())
        return false;
    return !model_.isEmpty();
}

int MonsterEnrageStripPanel::barBlockHeight() const
{
    // panel_monster.cpp's rageH = kRageBarH + 2 * kBarVPad, plus the
    // margin at both ends. Same arithmetic, same numbers, one owner.
    return kMargin + kRageBarH + 2 * kBarVPad + kMargin;
}

int MonsterEnrageStripPanel::contentWidth() const
{
    // Content width, not a chrome width: the widest text the bar can
    // show, measured in the same font the paint sets, with the bar's 8px
    // inset on both sides and the margin on both sides. "100%" is wider
    // than "MAX" in this font, so the percentage bounds the width; both
    // are measured anyway so a font substitution cannot surprise it.
    const QFont rageFont(QStringLiteral("Chakra Petch"), kRageFontSize);
    const QFontMetrics rageMetrics(rageFont);

    if (!enrageEnabled())
        return 2 * kMargin;
    const int widest = std::max(
        rageMetrics.horizontalAdvance(QStringLiteral("MAX")),
        rageMetrics.horizontalAdvance(QStringLiteral("100%")));
    if (widest <= 0)
        return 2 * kMargin;
    return widest + 2 * kBarTextInset + 2 * kMargin;
}

double MonsterEnrageStripPanel::pulsePhase() const
{
    // A stopped clock answers 0, which the pulse cosine below turns into
    // the bar's brightest stop — the same value a freshly-started clock
    // answers. So a bar that is not counting down never flickers.
    if (!phaseClock_.isValid())
        return 0.0;
    const qint64 elapsed = phaseClock_.elapsed();
    return static_cast<double>(elapsed % kPulsePeriodMs) / kPulsePeriodMs;
}

void MonsterEnrageStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = barBlockHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an
    // unmounted-but-alive window never renders a leftover sliver. The
    // live path never reaches here — the main loop asks
    // hasVisibleContent() first.
    if (!enrageEnabled() || model_.isEmpty()) {
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
    const int y      = kMargin;

    // ---- the enrage meter ----
    // The pulse, verbatim from the panel's .bar.er block: scale the
    // red/orange fill's brightness between 0.89 and 1.0 with a cosine.
    // The clock is (re)started here rather than in update() because the
    // paint is where the phase is consumed and a stopped monster's
    // panel is never painted — see the note on pulsePhase().
    if (model_.pulsing) {
        if (!phaseClock_.isValid())
            phaseClock_.start();
    } else if (phaseClock_.isValid()) {
        phaseClock_.invalidate();
    }
    const float pulse = 0.89F + 0.11F
        * static_cast<float>(std::cos(pulsePhase() * 2.0 * std::acos(-1.0)));

    QColor hi(kErBar1R, kErBar1G, kErBar1B);
    QColor lo(kErBar2R, kErBar2G, kErBar2B);
    const auto pulseChan = [pulse](int c) {
        return static_cast<int>(c * pulse);
    };
    hi.setRed(pulseChan(hi.red()));
    hi.setGreen(pulseChan(hi.green()));
    hi.setBlue(pulseChan(hi.blue()));
    lo.setRed(pulseChan(lo.red()));
    lo.setGreen(pulseChan(lo.green()));
    lo.setBlue(pulseChan(lo.blue()));

    const QRectF barRect(innerX, y, innerW, kRageBarH + 2 * kBarVPad);
    drawBarV(p, barRect, model_.pct, hi, lo);

    {
        QFont rageFont(QStringLiteral("Chakra Petch"), kRageFontSize);
        rageFont.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(rageFont);
        const QRectF textRect(innerX, y + kBarVPad, innerW, kRageBarH);
        if (model_.reading == mhw::MonsterEnrageReading::Max) {
            p.setPen(QColor(kEnrageR, kEnrageG, kEnrageB));
            p.drawText(textRect.adjusted(0, 0, -kBarTextInset, 0),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QStringLiteral("MAX"));
        } else {
            p.setPen(QColor(245, 246, 247));
            p.drawText(textRect.adjusted(0, 0, -kBarTextInset, 0),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QStringLiteral("%1%").arg(
                           static_cast<int>(model_.pct * 100)));
        }
    }
}
