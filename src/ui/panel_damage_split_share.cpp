// SPDX-License-Identifier: Apache-2.0
//
// DamageShareStripPanel implementation. The bar's own body is NOT in
// this file: it is ui/panel_damage_blocks.h's drawDamageShareBar(), shared
// verbatim with DamagePanel. What lives here is the window around it —
// the content model's cache, the demo seed, the width/height
// reservation and the mask read — and every one of those names the
// panel_damage.cpp line it mirrors.

#include "panel_damage_split_share.h"

#include "core/string_table.h"
// DamageSection::Share — the mask axis, the same bit DamagePanel reads
// for this block (panel_damage.cpp:214). Declared in namespace
// DamageSection, not mhw.
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"

// i18n lookup: panel_damage_blocks.h already defines the one spelling
// of mh::tr for every consumer of the shared drawing code, and that
// header is included from this window's own header — a second
// definition here would be a redefinition error. The title key this
// window asks for reaches the same table through it.

DamageShareStripPanel::DamageShareStripPanel(QWidget *parent)
    : Panel(QStringLiteral("damage-share"), Corner::TopRight, parent),
      m_vm(this)
{
    // No chrome, no title row: a share bar is a fragment of the damage
    // panel, not a panel. setChromeVisible(false) suppresses the v0.3
    // rounded body, left accent stripe and border entirely — the base
    // class's paintPanelChrome() then returns without touching the
    // painter, so every pixel the bar itself does not paint is
    // untouched.
    setChromeVisible(false);

    // Window-title key. Reuses ui.damage_title — the damage PANEL's own
    // key — on purpose rather than asking for a new one: the sibling
    // player split's six windows all answer ui.player_strip_title for the
    // same reason. A window's title has to name what it is a fragment of,
    // and "伤害统计" over a share bar is that; a new key per fragment
    // would be four new strings in two locales to say "part of 伤害统计"
    // in six different ways. It also keeps the i18n key-parity gate at
    // zero cost, which is a real constraint on this change (the parent
    // session owns src/resources/i18n/**).
    setWindowTitle(damage_blocks::tr(QStringLiteral("ui.damage_title")));
}

void DamageShareStripPanel::update(const mhw::GameSnapshot &snap)
{
    // Same shape as DamagePanel::update(): hand the snapshot to the
    // ViewModel, resolve what paintPanel() needs, schedule one repaint.
    // The connection is made in the constructor (below) so the VM's
    // own changed() signal drives the repaint, exactly as the panel's
    // does.
    m_vm.updateWorld(snap);
    rows_ = mhw::buildDamageShareModel(m_vm);

    // Demo numbers are never live state (the rule every split window
    // applies): the first authoritative snapshot replaces them
    // outright, so an edit-mode preview cannot survive into a live
    // session and start reporting a party that does not exist.
    demoData_ = false;

    triggerUpdate();
}

void DamageShareStripPanel::updateRiseDamage(
    const mhw::RiseDamageSnapshot &dmg)
{
    // Forwarded verbatim rather than re-derived: the Rise feed's own
    // filter (riseDisplayOptions_) decides which actors become rows,
    // and this window does not own that policy. Handing the panel and
    // the window the same feed through the same call is what keeps
    // them from disagreeing about how many segments there are.
    m_vm.updateRise(dmg);
    rows_ = mhw::buildDamageShareModel(m_vm);
    demoData_ = false;
    triggerUpdate();
}

void DamageShareStripPanel::retranslateUi()
{
    // One cached string, re-queried so a locale flip lands wherever
    // this is called from (main.cpp's applyLocale sets it from the same
    // key). No demo seed is dropped: the demo path writes numbers, not
    // labels, so there is nothing locale-dependent to rebuild.
    setWindowTitle(damage_blocks::tr(QStringLiteral("ui.damage_title")));
    triggerUpdate();
}

bool DamageShareStripPanel::shareEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing DamageSection::Share means the
    // console's existing damage-section toggle drives this window for
    // free — a user who has already turned the share bar off in the
    // single panel gets the same layout here, with no new console UI.
    return (sectionMask() & mhw::DamageSection::Share) != 0;
}

bool DamageShareStripPanel::hasContent() const
{
    // The mask bit is checked FIRST, ahead of the demo exception: a user
    // who has the share bar off does not want an edit-mode surface for a
    // bar that will never draw. (The ordering was the other way round
    // until tests/damage_split_windows_tests.cpp proved the result was a
    // preview window for a block the user had switched off.)
    if (!shareEnabled())
        return false;
    if (demoData_ && editMode())
        return true;
    // With the mask bit on and a party that has hit nothing, the bar is
    // a bare grey track, and a bare track is not a reason to mount a
    // surface: the main loop's hasVisibleContent() answers false and the
    // layer-shell surface is never created.
    return !rows_.isEmpty();
}

int DamageShareStripPanel::contentWidth() const
{
    // Content width, not a chrome width. The bar's own slot in
    // DamagePanel is kPanelW - 2*kMargin (panel_damage.cpp:427) and
    // this window adds its own margins around the same slot, so
    //   380 - 2*11 + 2*11 == 380
    // which is the honest starting point: the user got a 380px bar
    // before, they get a 380px window now, and every dial from there
    // is a change they chose. The floor and the lock are applied
    // later, inside Panel::setContentSize().
    return mhw::damage_block::kShareBarPanelW;
}

void DamageShareStripPanel::seedDemoModel()
{
    // Edit-mode preview: the same four-row party the panel's
    // setupDemoData() seeds, resolved into the seven-final * 2 samples
    // the point only when the bar has something to divide. panel_damage
    // .cpp seeds 184220 / 96240 / 71030 / 40510 — the shares 60% / 31% /
    // 23% / 13% are what make the bar read as a bar (not four equal
    // quarters) in the preview.
    QVector<mhw::DamageViewModel::DemoRow> demoParty;
    // NOT static: the labels are localized, so the table is rebuilt on
    // every seed, exactly as panel_damage.cpp:606-608 says.
    demoParty.append({QStringLiteral("A27exe"), 0, 247, 0});
    demoParty.append({damage_blocks::tr(QStringLiteral("data.demo.party.a")), 1, 500, 1});
    demoParty.append({damage_blocks::tr(QStringLiteral("data.demo.party.b")), 12, 300, 2});
    demoParty.append({damage_blocks::tr(QStringLiteral("data.demo.party.c")), 4, 250, 3});
    // 6:51, the seeded quest timer — unchanged, and not drawn by this
    // window, but passed so the ViewModel's seeded state is the same
    // one the panel would produce from the same call.
    m_vm.seedDemoData(demoParty, 411.0F);
    rows_ = mhw::buildDamageShareModel(m_vm);
    demoData_ = true;
}

void DamageShareStripPanel::setupDemoData()
{
    seedDemoModel();
}

void DamageShareStripPanel::paintPanel(QPainter &p)
{
    // One gateset, three consumers: width, reservation, draw.
    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an unmounted-but-alive
    // window never renders a leftover sliver. The live path never reaches
    // here — the main loop asks hasVisibleContent() first. This is the
    // edit-preview / defensive branch.
    if (!shareEnabled() || rows_.isEmpty()) {
        setContentSize(2 * mhw::split_layout::kSplitMargin, h);
        return;
    }

    setContentSize(w, h);

    // Re-read the width the window actually got. setContentSize() applied
    // the user's minimum-width floor to its arguments, but `w` is still
    // the pre-floor content number — using it here is exactly the bug the
    // floor was added to fix: the window would widen and the bar would
    // keep its old extent, leaving empty background to the right.
    //
    // The window only ever grows, so the same max() the base class
    // applied reproduces it without asking the compositor anything.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE. drawV03Chrome() is deliberately not called — a share
    // bar is a fragment of the damage panel, and a panel frame around
    // four coloured strips is the shape this split exists to remove.
    // This window calls neither drawV03Chrome() nor paintPanelChrome(),
    // exactly like the player split's windows.
    const int margin = mhw::split_layout::kSplitMargin;
    const QRectF barRect(margin, margin,
                         drawnW - 2 * margin,
                         mhw::DamageShareModel::blockHeight());

    drawDamageShareBar(p, barRect, rows_);
}
