// SPDX-License-Identifier: Apache-2.0
//
// DamageChartStripPanel implementation. The chart's own body is NOT in
// this file: it is ui/panel_damage_blocks.h's drawDamageChart(), shared
// verbatim with DamagePanel, and it takes its coordinates NORMALIZED
// from buildDamageChartModel() so a window of any width draws the same
// curve the panel drew. What lives here is the window around it — the
// model cache, the demo seed, the width/height reservation and the mask
// read — and every one of those names the panel_damage.cpp line it
// mirrors.

#include "panel_damage_split_chart.h"

#include "core/string_table.h"
// DamageSection::Chart — the mask axis, the same bit DamagePanel reads
// for this block (panel_damage.cpp:215). Declared in namespace
// DamageSection, not mhw.
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"

// std::isfinite on the quest timer. panel_damage.cpp:242 reaches it
// through its own include set; this file spells it rather than relying
// on whatever Qt header happens to pull <cmath> in today.
#include <cmath>

// i18n lookup: the shared header defines damage_blocks::tr — named
// for the header rather than `mh` because a header that spell `mh::tr`
// collides with every .cpp that already has its own copy (see the note in
// panel_damage_blocks.h).
// for every consumer of the shared drawing code, and that header comes
// in through this window's own header — a second definition here would
// be a redefinition error.

DamageChartStripPanel::DamageChartStripPanel(QWidget *parent, int subWindowIndex)
    : Panel(QStringLiteral("damage-chart"), Corner::TopRight, parent,
        subWindowIndex),
      m_vm(this)
{
    // No chrome, no title row: a chart is a fragment of the damage
    // panel, not a panel. setChromeVisible(false) suppresses the v0.3
    // rounded body, left accent stripe and border entirely — the base
    // class's paintPanelChrome() then returns without touching the
    // painter, so every pixel the chart does not paint is untouched.
    setChromeVisible(false);

    // Window-title key. Reuses ui.damage_title — the damage PANEL's own
    // key — on purpose rather than asking for a new one, for the same
    // reason the six player split windows all answer
    // ui.player_strip_title: a window's title has to name what it is a
    // fragment of, and that one string already does. It also keeps the
    // i18n key-parity gate at zero cost for this change (the parent
    // session owns src/resources/i18n/**).
    setWindowTitle(damage_blocks::tr(QStringLiteral("ui.damage_title")));
}

void DamageChartStripPanel::update(const mhw::GameSnapshot &snap)
{
    // Same shape as DamagePanel::update(): hand the snapshot to the
    // ViewModel, resolve what paintPanel() needs, schedule one repaint.
    m_vm.updateWorld(snap);
    rows_ = mhw::buildDamageChartModel(m_vm);

    // Demo curves are never live state: the first authoritative snapshot
    // replaces them outright.
    demoData_ = false;

    triggerUpdate();
}

void DamageChartStripPanel::updateRiseDamage(
    const mhw::RiseDamageSnapshot &dmg)
{
    // Forwarded verbatim rather than re-derived: the Rise feed's own
    // filter (riseDisplayOptions_) decides which actors become rows.
    m_vm.updateRise(dmg);
    rows_ = mhw::buildDamageChartModel(m_vm);
    demoData_ = false;
    triggerUpdate();
}

void DamageChartStripPanel::retranslateUi()
{
    // One cached string, re-queried so a locale flip lands wherever this
    // is called from. The wait placeholder is read at draw time, and the
    // demo path seeds numbers, so there is nothing else to rebuild.
    setWindowTitle(damage_blocks::tr(QStringLiteral("ui.damage_title")));
    triggerUpdate();
}

bool DamageChartStripPanel::chartEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing DamageSection::Chart means the
    // console's existing damage-section toggle drives this window for
    // free.
    return (sectionMask() & mhw::DamageSection::Chart) != 0;
}

bool DamageChartStripPanel::hasContent() const
{
    // The mask bit first, ahead of the demo exception, for the reason
    // the sibling share window gives: a user who has the chart off does
    // not want an edit-mode surface for it.
    if (!chartEnabled())
        return false;
    if (demoData_ && editMode())
        return true;
    // With the mask bit on and a history the chart cannot draw from, the
    // chart would be only its background and a placeholder, and a
    // placeholder is not a reason to mount a surface.
    return !rows_.isEmpty();
}

int DamageChartStripPanel::contentWidth() const
{
    // Content width, not a chrome width: the width of the chart's slot
    // in DamagePanel (panel_damage.cpp:434) — kPanelW 380 minus its two
    // margins, plus this window's own. The y-axis label column stays
    // kChartLeft wide whatever this is, so the extra width goes to the
    // plot area, which is what the user asked the window for.
    return mhw::damage_block::kChartPanelW;
}

void DamageChartStripPanel::seedDemoModel()
{
    // Edit-mode preview: the same four-row party and the same synthetic
    // 8-sample history the panel's setupDemoData() seeds, so the
    // preview's curves are the ones the block it came from would have
    // drawn.
    QVector<mhw::DamageViewModel::DemoRow> demoParty;
    // NOT static: the labels are localized, so the table is rebuilt on
    // every seed, exactly as panel_damage.cpp:606-608 says.
    demoParty.append({QStringLiteral("A27exe"), 0, 247, 0});
    demoParty.append({damage_blocks::tr(QStringLiteral("data.demo.party.a")), 1, 500, 1});
    demoParty.append({damage_blocks::tr(QStringLiteral("data.demo.party.b")), 12, 300, 2});
    demoParty.append({damage_blocks::tr(QStringLiteral("data.demo.party.c")), 4, 250, 3});
    // 6:51, the seeded quest timer — now drawn by this window's own
    // timer strip, so the console preview shows the same clock the
    // panel's title row used to carry. Passed either way so the
    // ViewModel's seeded state is the one the panel would produce from
    // the same call.
    m_vm.seedDemoData(demoParty, 411.0F);
    rows_ = mhw::buildDamageChartModel(m_vm);
    demoData_ = true;
}

void DamageChartStripPanel::setupDemoData()
{
    seedDemoModel();
}

void DamageChartStripPanel::paintPanel(QPainter &p)
{
    // One gateset, three consumers: width, reservation, draw.
    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an unmounted-but-alive
    // window never renders a leftover sliver. The live path never reaches
    // here — the main loop asks hasVisibleContent() first. This is the
    // edit-preview / defensive branch.
    if (!chartEnabled() || rows_.isEmpty()) {
        setContentSize(2 * mhw::split_layout::kSplitMargin, h);
        return;
    }

    setContentSize(w, h);

    // Re-read the width the window actually got, for the same reason the
    // sibling share window does: `w` is the pre-floor content number and
    // the floor is what the window is actually sized to.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE. drawV03Chrome() is deliberately not called — a chart
    // is a fragment of the damage panel, and a panel frame around a plot
    // area is the shape this split exists to remove. The chart's own
    // translucent background (part of drawDamageChart) is the surface.
    const int margin = mhw::split_layout::kSplitMargin;

    // ── The quest timer strip, v0.16 ────────────────────────────────────
    //
    // The right-aligned half of the panel's title row
    // (panel_damage.cpp:227-252), which the split left orphaned. Drawn
    // FIRST, at the strip contentHeight() reserves above the chart, so
    // the reservation and the draw stay each other's inverse.
    //
    // The value crosses a reader boundary, so a non-finite float is
    // possible: `Inf > 0` passes the gate and then casts to int as
    // platform-defined garbage (typically INT_MIN). One truncation feeds
    // both digits — the previous code truncated twice (once inside /60,
    // once before % 60) and could disagree with itself on negatives.
    // Copied rather than re-derived so the two surfaces cannot drift on
    // the one value they share.
    const float elapsedSeconds = m_vm.lastElapsedSeconds();
    if (std::isfinite(elapsedSeconds) && elapsedSeconds > 0.0F) {
        const int totalSeconds = static_cast<int>(elapsedSeconds);
        const int mm = totalSeconds / 60;
        const int ss = totalSeconds % 60;
        const QRectF timerRect(margin, margin,
                               drawnW - 2 * margin, kTimerStripH);
        p.setFont(QFont(QStringLiteral("Chakra Petch"), 8));
        p.setPen(QColor(150, 150, 150));
        p.drawText(timerRect, Qt::AlignRight | Qt::AlignVCenter,
                   damage_blocks::tr(QStringLiteral("ui.damage_quest_timer"))
                       .arg(mm, 2, 10, QChar('0'))
                       .arg(ss, 2, 10, QChar('0')));
    }

    // The chart sits below the strip, not at the margin: the strip is
    // part of this window's content now, exactly as the panel's title
    // row pushed the chart down 14+9 inside the panel.
    const QRectF chartRect(margin, margin + kTimerStripH,
                           drawnW - 2 * margin,
                           mhw::damage_block::kChartH);

    drawDamageChart(p, chartRect, rows_);
}
