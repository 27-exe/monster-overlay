// SPDX-License-Identifier: Apache-2.0
//
// DamageRowsStripPanel implementation. The rows' own body is NOT in
// this file: it is ui/panel_damage_blocks.h's drawDamageRows(), shared
// with DamagePanel the way drawDamageShareBar() and drawDamageChart()
// already are. What lives here is the window around them — the content
// model's cache, the demo seed, the width/height reservation and the
// mask read — and every one of those names the panel_damage.cpp line
// it mirrors.

#include "panel_damage_split_rows.h"

#include "core/string_table.h"
// DamageSection::Rows — the mask axis, the same bit DamagePanel reads
// for this block (panel_damage.cpp:210). Declared in namespace
// DamageSection, not mhw.
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"

// i18n lookup: panel_damage_blocks.h already defines the one spelling
// of damage_blocks::tr for every consumer of the shared drawing code,
// and that header is included from this window's own header — a second
// definition here would be a redefinition error. The title key this
// window asks for reaches the same table through it.

DamageRowsStripPanel::DamageRowsStripPanel(QWidget *parent,
                                           int subWindowIndex)
    : Panel(QStringLiteral("damage-rows"), Corner::TopRight, parent,
        subWindowIndex),
      m_vm(this)
{
    // No chrome, no title row: a row is a fragment of the damage panel,
    // not a panel. setChromeVisible(false) suppresses the v0.3 rounded
    // body, left accent stripe and border entirely — the base class's
    // paintPanelChrome() then returns without touching the painter, so
    // every pixel the rows themselves do not paint is untouched.
    setChromeVisible(false);

    // Window-title key. Reuses ui.damage_title — the damage PANEL's own
    // key — for the reason panel_damage_split_share.h gives at length:
    // a window's title has to name what it is a fragment of, and
    // "伤害统计" over a list of player rows is that. A new key would be
    // two new strings to say "part of 伤害统计" in one more way.
    setWindowTitle(damage_blocks::tr(QStringLiteral("ui.damage_title")));
}

void DamageRowsStripPanel::update(const mhw::GameSnapshot &snap)
{
    // Same shape as DamagePanel::update(): hand the snapshot to the
    // ViewModel, resolve what paintPanel() needs, schedule one repaint.
    // The connection is made in the constructor (below) so the VM's
    // own changed() signal drives the repaint, exactly as the panel's
    // does.
    m_vm.updateWorld(snap);
    rows_ = mhw::buildDamageRowsModel(m_vm);

    // Demo numbers are never live state (the rule every split window
    // applies): the first authoritative snapshot replaces them
    // outright, so an edit-mode preview cannot survive into a live
    // session and start reporting a party that does not exist.
    demoData_ = false;

    triggerUpdate();
}

void DamageRowsStripPanel::updateRiseDamage(
    const mhw::RiseDamageSnapshot &dmg)
{
    // Forwarded verbatim rather than re-derived, exactly as the share
    // and chart windows do: the Rise feed's own filter decides which
    // actors become rows, and this window does not own that policy.
    // Handing the panel and the window the same feed through the same
    // call is what keeps them from disagreeing about how many rows
    // there are.
    m_vm.updateRise(dmg);
    rows_ = mhw::buildDamageRowsModel(m_vm);
    demoData_ = false;
    triggerUpdate();
}

void DamageRowsStripPanel::retranslateUi()
{
    // One cached string, re-queried so a locale flip lands wherever
    // this is called from (main.cpp's applyLocale sets it from the same
    // key). No demo seed is dropped: the demo labels are resolved
    // through the ViewModel's own seed, which re-reads the table on
    // every seedDemoData() call — the reason panel_damage.cpp:606-608
    // refuses a function-local static for them.
    setWindowTitle(damage_blocks::tr(QStringLiteral("ui.damage_title")));
    if (editMode())
        resetDemoPrimed();
    triggerUpdate();
}

bool DamageRowsStripPanel::rowsEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing DamageSection::Rows means the
    // console's existing damage-section toggle drives this window for
    // free — a user who has already turned the rows off in the single
    // panel gets the same layout here, with no new console UI.
    return (sectionMask() & mhw::DamageSection::Rows) != 0;
}

bool DamageRowsStripPanel::hasContent() const
{
    // The mask bit is checked FIRST, ahead of the demo exception: a user
    // who has the rows off does not want an edit-mode surface for a
    // block that will never draw. (The ordering was the other way round
    // until tests/damage_split_windows_tests.cpp proved the result was
    // a preview window for a block the user had switched off.)
    if (!rowsEnabled())
        return false;
    if (demoData_ && editMode())
        return true;
    // With the mask bit on and a party that has hit nothing, the rows
    // are still four names and four weapon icons — the answer to "who
    // is here" — so only a genuinely EMPTY roster unmounts. This is the
    // opposite of DamageShareModel::isEmpty()'s rule, and the difference
    // is deliberate: a bar with nothing to divide is a bare grey track,
    // a row list with nothing to name is nothing.
    return !rows_.isEmpty();
}

int DamageRowsStripPanel::contentWidth() const
{
    // Content width, not a chrome width. The rows' slot in DamagePanel
    // is kPanelW - 2*kMargin (panel_damage.cpp:292) and this window adds
    // its own margins around the same slot, so
    //   380 - 2*11 + 2*11 == 380
    // which is the honest starting point: the user got 380px of rows
    // before, they get a 380px window now, and every dial from there is
    // a change they chose. The floor and the lock are applied later,
    // inside Panel::setContentSize().
    return mhw::damage_block::kRowsPanelW;
}

int DamageRowsStripPanel::contentHeight() const
{
    // Every row plus the split margin on both sides. The inter-row gaps
    // are inside blockHeight() (the panel's own rowsAreaH expression,
    // n*kRowH + (n-1)*kRowGap); the margins are not, for the same
    // reason the share window gives — a hole at the top that nothing
    // draws into.
    return 2 * mhw::split_layout::kSplitMargin
           + rows_.blockHeight();
}

void DamageRowsStripPanel::seedDemoModel()
{
    // Edit-mode preview: the same four-row party the panel's
    // setupDemoData() seeds (panel_damage.cpp:661-668), handed to this
    // window's own ViewModel so the model it paints is the model a test
    // can build. The panel's own demo DPS override (kDemoDps[4] at
    // :288-290) is a VIEW concern gated on editMode() && a short
    // history, which the seeded four-sample history satisfies.
    QVector<mhw::DamageViewModel::DemoRow> demoParty;
    // NOT static: the labels are localized, so the table is rebuilt on
    // every seed, exactly as panel_damage.cpp:606-608 says.
    demoParty.append({QStringLiteral("A27exe"), 0, 247, 0});
    demoParty.append({damage_blocks::tr(QStringLiteral("data.demo.party.a")), 1, 500, 1});
    demoParty.append({damage_blocks::tr(QStringLiteral("data.demo.party.b")), 12, 300, 2});
    demoParty.append({damage_blocks::tr(QStringLiteral("data.demo.party.c")), 4, 250, 3});
    // 6:51, the seeded quest timer — unchanged, and not drawn by this
    // window (the quest timer belongs to the panel's title row), but
    // passed so the ViewModel's seeded state is the same one the panel
    // would produce from the same call.
    m_vm.seedDemoData(demoParty, 411.0F);
    rows_ = mhw::buildDamageRowsModel(m_vm);
    demoData_ = true;
}

void DamageRowsStripPanel::setupDemoData()
{
    seedDemoModel();
}

void DamageRowsStripPanel::paintPanel(QPainter &p)
{
    // One gateset, three consumers: width, reservation, draw.
    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an unmounted-but-alive
    // window never renders a leftover sliver. The live path never reaches
    // here — the main loop asks hasVisibleContent() first. This is the
    // edit-preview / defensive branch.
    if (!rowsEnabled() || rows_.isEmpty()) {
        setContentSize(2 * mhw::split_layout::kSplitMargin, h);
        return;
    }

    setContentSize(w, h);

    // Re-read the width the window actually got. setContentSize() applied
    // the user's minimum-width floor to its arguments, but `w` is still
    // the pre-floor content number — using it here is exactly the bug the
    // floor was added to fix: the window would widen and the rows would
    // keep their old extent, leaving empty background to the right.
    //
    // The window only ever grows, so the same max() the base class
    // applied reproduces it without asking the compositor anything.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE. drawV03Chrome() is deliberately not called — a row
    // is a fragment of the damage panel, and a panel frame around a
    // list of players is the shape this split exists to remove. This
    // window calls neither drawV03Chrome() nor paintPanelChrome(),
    // exactly like the share, chart and player windows.
    const int margin = mhw::split_layout::kSplitMargin;
    const QRectF rowsRect(margin, margin,
                          drawnW - 2 * margin,
                          rows_.blockHeight());

    drawDamageRows(p, rowsRect, rows_);
}
