// SPDX-License-Identifier: Apache-2.0
//
// PlayerWeaponStripPanel — the weapon identity trio as its own window.
// See panel_player_split_weapon.h for what this window is, why the
// sharpness gauge is NOT in it, and why the mask claim is `Weapon`
// without `Sharpness`.
//
// The weapon-slot painter below is this file's own copy of drawWslot()
// (panel_player.cpp:224-236), which lives in that file's anonymous
// namespace and is unreachable from here. Writing it out again is the
// deliberate cost of the split being purely additive: hoisting it into
// a shared header would mean editing panel_player.cpp, which this
// change may not touch. The copy is confined to the block being
// extracted, so it disappears the day the two merge again.

#include "panel_player_split_weapon.h"

#include "core/string_table.h"
#include "ui/icon.h"
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPixmap>
#include <QPen>
#include <QRectF>
#include <QString>

#include <algorithm>

// i18n lookup, defined the same way panel_player.cpp:31-33 defines it.
// This file asks for no keys of its own beyond the window title and the
// two data keys the base classes and the demo path use: the player name
// is the reader's own string, and the "--" / "MR --" placeholders are
// the stable constants in the header rather than locale strings (the
// panel spells the same two literals inline — panel_player.cpp:977 /
// :986 — and docs/I18N.md's "no fallback in code" rule is about keys,
// which these are not).
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- Layout tokens (panel_player.cpp:37-63, the ones this block owns) --
constexpr int kMargin     = 11;     // HTML: .op padding:11
constexpr int kWslot      = 26;     // HTML: .wslot 26x26 (panel_player.cpp:46)
constexpr int kWeaponGap  = 9;      // panel_player.cpp:724 (kGapSection)
constexpr int kWeaponH    = 26;     // panel_player.cpp:725 (prow height)
constexpr int kIconTextGap = 8;     // panel_player.cpp:960 (.prow gap:8)

// The .wslot's border and body colours (panel_player.cpp:227-228). The
// border is (58,61,63) and the body (14,16,18).
constexpr int kSlotBorderR = 58;  constexpr int kSlotBorderG = 61;
constexpr int kSlotBorderB = 63;
constexpr int kSlotBodyR = 14;    constexpr int kSlotBodyG = 16;
constexpr int kSlotBodyB = 18;

// The name column's pen: #f5f6f7 (panel_player.cpp:1002).
constexpr int kNamePenR = 245;  constexpr int kNamePenG = 246;
constexpr int kNamePenB = 247;
// The MR column's pen: #929495 (panel_player.cpp:1006).
constexpr int kMrPenR = 146;  constexpr int kMrPenG = 148;
constexpr int kMrPenB = 149;

// The two fonts the panel's prow used, kept as this file's own copies
// with the same values so a trio drawn side by side with the panel's is
// pixel-identical. The sizes came from the HTML v8 spec and are layout,
// not taste.
QFont identityNameFont()
{
    QFont f(QStringLiteral("Chakra Petch"), 12, QFont::Medium);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont identityMrFont()
{
    QFont f(QStringLiteral("Chakra Petch"), 9);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

// "MR 340" / "MR --". Same construction as panel_player.cpp:983-986,
// including that a rank of 0 or below prints the placeholder rather
// than "MR 0" — the panel's own arm of that ternary.
QString mrText(int masterRank)
{
    return masterRank > 0 ? QStringLiteral("MR %1").arg(masterRank)
                          : QStringLiteral("MR --");
}

// The name column's text: the player's name, or the same "--" the panel
// falls back to when no local player has resolved yet
// (panel_player.cpp:975-977).
QString nameText(const QString &name)
{
    return name.isEmpty() ? mhw::weaponIdentityNoName() : name;
}

// panel_player.cpp:224-236 — the 26x26 weapon slot: 1px light border,
// dark recessed body, icon inset by 1px so the border stays visible.
void drawWeaponSlot(QPainter &p, const QRectF &slotRect, const QPixmap &icon)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(kSlotBorderR, kSlotBorderG, kSlotBorderB), 1));
    p.setBrush(QColor(kSlotBodyR, kSlotBodyG, kSlotBodyB));
    p.drawRoundedRect(slotRect, 3, 3);
    if (!icon.isNull()) {
        const QRectF iconRect(slotRect.x() + 1, slotRect.y() + 1,
                              slotRect.width() - 2, slotRect.height() - 2);
        p.drawPixmap(iconRect.toRect(), icon);
    }
}

} // namespace

namespace mhw {

WeaponIdentityRow buildWeaponIdentityModel(const mhw::GameSnapshot &snap)
{
    WeaponIdentityRow row;

    // One PlayerViewModel per call, not a member: the class is stateless
    // (resolveIdentity() is const and reads no member), a local is the
    // same thing with no lifetime to reason about, and it keeps this
    // header free of a widget-construction-order dependency. The panel
    // owns one as a member only because it is a QWidget that outlives
    // the call.
    const PlayerViewModel vm;
    const PlayerViewModel::PlayerIdentity id = vm.resolveIdentity(snap);

    row.weaponId   = id.weaponId;
    row.name       = id.name;
    row.hasName    = !id.name.isEmpty();
    row.masterRank = id.masterRank;
    return row;
}

} // namespace mhw

PlayerWeaponStripPanel::PlayerWeaponStripPanel(QWidget *parent)
    : Panel(QStringLiteral("player-weapon"), Corner::TopLeft, parent)
{
    // No chrome, no title row: the identity trio is a fragment of the
    // player panel, not a panel. setChromeVisible(false) suppresses the
    // v0.3 rounded body, left accent stripe and border entirely, so the
    // window surface stays transparent apart from the slot's own dark
    // body and the two text columns. isSubWindow() (in the header)
    // declares the same fact for the console's slot registry.
    setChromeVisible(false);

    // Own window-title key, the one the other four split windows use.
    // The trio draws no label of its own, so there is nothing more
    // specific to title it, and reusing ui.player_strip_title means
    // main.cpp's applyLocale re-sets it from exactly the same key the
    // others do instead of this file inventing a second one.
    setWindowTitle(mh::tr("ui.player_strip_title"));
}

void PlayerWeaponStripPanel::update(const mhw::GameSnapshot &snap)
{
    // Same shape as PlayerPanel::update(): cache what paintPanel() needs,
    // schedule one repaint. The panel's other mirrored fields (zone,
    // quest, connection, vitals, sharpness) are not read here — this
    // window draws none of them, which is the whole point of the trio
    // being a fragment.
    row_ = mhw::buildWeaponIdentityModel(snap);

    // Demo identity is never live state (the rule PetDamagePanel and the
    // pilot strip both apply): the first authoritative snapshot replaces
    // it outright, so an edit-mode preview cannot survive into a live
    // session and start naming a hunter that does not exist.
    demoData_ = false;

    triggerUpdate();
}

void PlayerWeaponStripPanel::retranslateUi()
{
    // Cached strings first, exactly like the other split windows'
    // retranslateUi() and for the same reason: main.cpp's applyLocale
    // re-sets the title from the same key, so the second set is the
    // deliberate redundancy every panel carries.
    //
    // The one thing this window has that bars / mantles / wirebug do
    // not: a demo NAME, seeded from data.demo.player_name. Without a
    // rebuild here, an `--edit` launch would keep a stale-language
    // hunter until the panel was re-created — the very leak
    // retranslateUi() exists to stop (panel_player.cpp's own demo seed
    // re-reads the key for the same reason). The guard is what makes it
    // safe to call unconditionally: before the demo is primed there is
    // nothing to rebuild, and a live window clears demoData_ on its
    // first update() so the rebuild can never overwrite real data.
    if (demoData_ && editMode())
        setupDemoData();

    setWindowTitle(mh::tr("ui.player_strip_title"));
    triggerUpdate();
}

bool PlayerWeaponStripPanel::weaponEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reads `Weapon` and NOT `Sharpness`: the
    // sharpness bar has had its own bit since this window was added, so
    // the console's weapon toggle hides the trio and leaves the bar in
    // PlayerPanel drawing. panel_sections.h records the same decision
    // from the table's side (the row claims Weapon, and the bar is named
    // in the "NOT in this table" note).
    return (sectionMask() & mhw::PlayerSection::Weapon) != 0;
}

bool PlayerWeaponStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    if (!weaponEnabled())
        return false;
    // No name AND no weapon: the reader has nothing to say about who is
    // playing, so the window must not exist — the main loop's
    // hasVisibleContent() answers false and the layer-shell surface is
    // never mounted. PlayerPanel is visible in that state precisely
    // because it has a "not connected" placeholder to show; a fragment
    // window has no such obligation, and an icon next to "--" next to
    // "MR --" would be three placeholders with nothing behind them.
    return !row_.isEmpty();
}

int PlayerWeaponStripPanel::contentHeight() const
{
    // One identity row. The panel reserves kWeaponGap + kWeaponH (9 + 26,
    // panel_player.cpp:724-725) for the same block; this window keeps
    // both numbers plus the margin at each end, so a trio drawn side by
    // side with the panel's is the same height at the same distance from
    // the window's top edge.
    return kMargin + kWeaponGap + kWeaponH + kMargin;
}

int PlayerWeaponStripPanel::contentWidth() const
{
    // Content width, not a chrome width: the fixed columns with the
    // panel's own 8px gaps, plus the margin on both sides.
    //
    // The name is measured, never elided and never re-wrapped — that is
    // the whole reason the trio is worth its own window. In the panel
    // it sat in a 49px band between the icon and the centred gauge and
    // took the elide branch on every non-empty name; here the same
    // measurement is the reservation, so the two agree by construction
    // (the v0.10.3 lesson, and the reason the width comes from the same
    // helpers paintPanel() calls).
    if (!weaponEnabled())
        return 2 * kMargin;

    const QFontMetrics nameMetrics(identityNameFont());
    const QFontMetrics mrMetrics(identityMrFont());

    const int nameW = nameMetrics.horizontalAdvance(nameText(row_.name));
    const int mrW   = mrMetrics.horizontalAdvance(mrText(row_.masterRank));

    return kWslot + kIconTextGap + nameW + kIconTextGap + mrW + 2 * kMargin;
}

void PlayerWeaponStripPanel::setupDemoData()
{
    // Edit-mode preview: the same hunter the panel's own demo seeds
    // (panel_player.cpp:1291-1293 — Great Sword, MR 247, the localized
    // data.demo.player_name), so the two windows' previews agree and
    // neither has to be explained twice.
    //
    // Built as a snapshot and pushed through the same builder the live
    // path uses rather than filling the row by hand: the demo then
    // exercises the real transformation, and a fix to the identity
    // merge lands in the preview too. `valid = true` is what keeps
    // resolveIdentity() off its not-attached branch, which would
    // collapse the weapon to -1 (player_view_model.cpp:41-46).
    mhw::PlayerSnapshot pl;
    pl.valid      = true;
    pl.weaponId   = 0;                 // Great Sword, panel_player.cpp:1291
    pl.masterRank = 247;               // panel_player.cpp:1292
    pl.name       = mh::tr("data.demo.player_name");
    mhw::GameSnapshot snap;
    snap.game   = mhw::GameId::World;
    snap.player = pl;
    row_ = mhw::buildWeaponIdentityModel(snap);
    demoData_ = true;
}

void PlayerWeaponStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an unmounted-but-alive
    // window never renders a leftover sliver. The live path never reaches
    // here — the main loop asks hasVisibleContent() first. This is the
    // edit-preview / defensive branch.
    if (!weaponEnabled() || row_.isEmpty()) {
        setContentSize(2 * kMargin, h);
        return;
    }

    // setContentSize has an idempotent guard inside Panel, so calling it
    // per repaint (and therefore per drag frame) is free.
    setContentSize(w, h);

    // Re-read the width the window actually got. setContentSize()
    // applied the user's minimum-width floor to its arguments, but `w`
    // is still the pre-floor content number — using it here is the bug
    // the floor was added to fix: the window would widen to 300px while
    // the icon and the name stayed at the left edge, leaving empty
    // background to the right. bars took exactly this hit and calls the
    // same helper (panel_player_split_bars.cpp:368-376).
    //
    // The window only ever grows, so the same max() the base class
    // applied reproduces it without asking the compositor anything.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE. drawV03Chrome() is deliberately not called — the
    // slot carries its own dark body and the two text columns have no
    // background in the source panel either, so a frame around them is
    // the shape the split exists to remove. This panel calls neither
    // drawV03Chrome() nor paintPanelChrome(), exactly like the other
    // four.
    const int innerW = drawnW - 2 * kMargin;
    const int rowY   = kMargin + kWeaponGap;
    const int rowH   = kWeaponH;

    // The trio's own width, from the same measurements contentWidth()
    // reserved. Centred in the available row: at the natural size the
    // window and the content are the same width and this is a no-op;
    // when the user has widened the window with the `-` / `=` floor
    // keys, the fragment sits in the middle of its window instead of
    // hugging the left edge of a mostly-empty surface. That mirrors what
    // the panel did with the gauge it centred in the same row
    // (panel_player.cpp:966) and is the only sensible answer to "more
    // width than the content needs" for fixed-size glyphs.
    const QFontMetrics nameMetrics(identityNameFont());
    const QFontMetrics mrMetrics(identityMrFont());
    const QString nm = nameText(row_.name);
    const QString mr = mrText(row_.masterRank);
    const int nameW = nameMetrics.horizontalAdvance(nm);
    const int mrW   = mrMetrics.horizontalAdvance(mr);
    const int trioW = kWslot + kIconTextGap + nameW + kIconTextGap + mrW;
    const int left  = kMargin + std::max(0, (innerW - trioW) / 2);

    // ---- weapon slot ----
    // An unresolved weapon id (<0) leaves the slot empty but drawn: the
    // frame is the slot's identity and a missing read is not a missing
    // slot. Same branch as panel_player.cpp:949-951, including that an
    // empty path handed to Icon::render() produces a null pixmap the
    // painter already handles.
    const QRectF slotRect(left, rowY, kWslot, rowH);
    const QString wp = row_.weaponId >= 0
        ? mhw::Icon::weaponPath(row_.weaponId, 1) : QString();
    drawWeaponSlot(p, slotRect, mhw::Icon::render(wp, kWslot));

    // ---- name, then MR glued after it ----
    // The panel's .prow is a flex row: MR follows the name with an 8px
    // gap and is NOT pushed to the right edge
    // (panel_player.cpp:955-997). This window keeps that reading order,
    // which is also what makes the centring above meaningful — the trio
    // moves as one block.
    const int nameLeftX = static_cast<int>(slotRect.right()) + kIconTextGap;

    p.setFont(identityNameFont());
    p.setPen(QColor(kNamePenR, kNamePenG, kNamePenB));
    p.drawText(QRectF(nameLeftX, rowY, nameW, rowH),
               Qt::AlignLeft | Qt::AlignVCenter, nm);

    p.setFont(identityMrFont());
    p.setPen(QColor(kMrPenR, kMrPenG, kMrPenB));
    p.drawText(QRectF(nameLeftX + nameW + kIconTextGap, rowY, mrW, rowH),
               Qt::AlignLeft | Qt::AlignVCenter, mr);
}
