// SPDX-License-Identifier: Apache-2.0
//
// PlayerSharpnessStripPanel — the v0.12 split's seventh window, and the
// one that makes it TOTAL. See panel_player_split_sharpness.h for why
// the gauge had to leave PlayerPanel (the panel was left holding one
// 174px block inside a window that always draws a title row, and the
// user asked whether there was any point keeping it).
//
// Layout tokens: this file's own copies, named for the line they mirror
// in panel_player.cpp. The one number the painter itself owns is the
// composed width, which is precisely why playerSharpnessBarTotalWidth()
// is exported — so the reservation and the drawing cannot disagree
// (v0.12's lesson: measure what you draw, don't draw what you guessed).
//
// What this window is NOT: a copy of the gauge. drawPlayerSharpnessBar()
// is CALLED. The extraction that landed in the previous step made that
// possible, and this is the first split window that benefits from it —
// the six before it each had to re-implement their block's painter
// because those live in panel_player.cpp's anonymous namespace.

#include "panel_player_split_sharpness.h"

#include "core/game_snapshot.h"
#include "core/string_table.h"
#include "player/player_types.h"
#include "ui/panel_player_sharpness.h"
#include "ui/panel_sections.h"
#include "ui/panel_sizing.h"

#include <QColor>
#include <QRectF>

namespace {

// i18n lookup, defined the same way panel_player.cpp:31-33 and
// panel_player_split_weapon.cpp:34-38 define it.
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

// ---- Layout tokens (panel_player.cpp, the ones this block owns) ----
constexpr int kMargin    = 11;     // HTML: .op padding:11 (panel_player.cpp:39)
constexpr int kWeaponGap = 9;      // panel_player.cpp:747 (kGapSection)
constexpr int kWeaponH   = 26;     // panel_player.cpp:748 (prow height)
} // namespace

PlayerSharpnessStripPanel::PlayerSharpnessStripPanel(QWidget *parent)
    : Panel(QStringLiteral("player-sharpness"), Corner::TopLeft, parent)
{
    // No chrome, no title row: the gauge is a fragment of the player
    // panel, not a panel. setChromeVisible(false) suppresses the v0.3
    // rounded FRAME — the left accent stripe and the 1px border. It does
    // NOT suppress the translucent body any more: v0.12.1 decoupled the
    // two and paintEvent() draws the body for exactly this case, so the
    // gauge floats on the same #0c0e10 the panel floats on.
    // isSubWindow() (in the header) declares the same fact for the
    // console's slot registry.
    setChromeVisible(false);

    // Own window-title key — the same one the other six split windows
    // use. The gauge draws no label of its own, so there is nothing more
    // specific to title it, and reusing ui.player_strip_title means
    // main.cpp's applyLocale re-sets it from exactly the same key the
    // others do instead of this file inventing a second one.
    setWindowTitle(mh::tr("ui.player_strip_title"));
}

void PlayerSharpnessStripPanel::update(const mhw::GameSnapshot &snap)
{
    // Same shape as PlayerPanel::update(): cache what paintPanel() needs,
    // schedule one repaint. The panel's other nine mirrored fields and
    // its ViewModel-resolved identity are not read here — this window
    // draws neither a name nor an MR nor a weapon slot.
    sharpness_ = snap.player.sharpness;

    // Demo numbers are never live state (the rule every other split
    // window applies): the first authoritative snapshot replaces them
    // outright, so an edit-mode preview cannot survive into a live
    // session and start showing a hunter that does not exist.
    demoData_ = false;

    triggerUpdate();
}

void PlayerSharpnessStripPanel::retranslateUi()
{
    // Cached string first, exactly like the other split windows'
    // retranslateUi() and for the same reason: main.cpp's applyLocale
    // re-sets the title from the same key, so the second set is the
    // deliberate redundancy every panel carries.
    //
    // Nothing to re-seed in the demo path, and that is a fact about this
    // window's content rather than a gap: the demo sharpness is numbers
    // (thresholds array and a level), which no locale changes, so a
    // language switch cannot make it stale.
    setWindowTitle(mh::tr("ui.player_strip_title"));
    triggerUpdate();
}

bool PlayerSharpnessStripPanel::sharpnessEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing PlayerSection::Sharpness means the
    // console's sharpness toggle drives this window for free — the same
    // role barsEnabled() / mantlesEnabled() / wirebugEnabled() play.
    return (sectionMask() & mhw::PlayerSection::Sharpness) != 0;
}

bool PlayerSharpnessStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    if (!sharpnessEnabled())
        return false;
    // A ranged weapon and a broken weapon are both legitimate live
    // states in which the gauge draws nothing. "No data, no window" is
    // HunterPie's rule and it applies here unchanged: the window must
    // not exist then rather than show an empty frame the user can bump
    // into mid-hunt.
    //
    // The three-part guard mirrors drawPlayerSharpnessBar()'s own
    // must-not-draw conditions (invalid snapshot, level out of range,
    // and thresholds that never increase), so hasContent() can never
    // answer "yes" to a state the painter would answer "no" for. Only
    // the first two are checked here because the third needs the widths
    // computation and the painter's `return rightX` is the authoritative
    // answer for it — a window that mounts with a flat threshold bar is
    // the same visual outcome the old panel produced, so it is not a
    // regression worth duplicating arithmetic for.
    return sharpness_.valid && sharpness_.level >= 0 && sharpness_.level <= 6;
}

int PlayerSharpnessStripPanel::contentHeight() const
{
    // One identity-tall row. The panel reserves kGapSection + kWeaponH
    // (9 + 26, panel_player.cpp:747-748) for the same block; this window
    // keeps both numbers plus the margin at each end, so a gauge drawn
    // beside the panel's is the same height at the same distance from
    // the window's top edge.
    return kMargin + kWeaponGap + kWeaponH + kMargin;
}

int PlayerSharpnessStripPanel::contentWidth() const
{
    // Content width, not a chrome width: the [gauge + gap + badge]
    // block's own exported measurement, plus the margin on both sides.
    //
    // The measurement is the SHARED one — playerSharpnessBarTotalWidth()
    // is what the panel's centering math used and what the painter draws
    // from, so this reservation cannot drift from the drawn pixels by
    // construction. The 174 it returns is the number the v0.12 partial
    // split called "hard cost that buys nothing in its own window"; that
    // was a statement about a panel that was keeping five other blocks
    // in a 378px shell, and the same 174 in a window of its own is
    // exactly as wide as what it draws.
    if (!sharpnessEnabled())
        return 2 * kMargin;

    return playerSharpnessBarTotalWidth() + 2 * kMargin;
}

void PlayerSharpnessStripPanel::setupDemoData()
{
    // Edit-mode preview: the SAME hunter the panel's own demo seeds
    // (panel_player.cpp — a Purple-able Great Sword whose badge shows
    // `currentHits - thresholds[level - 1]` = 47), so the two windows'
    // previews agree and neither has to be explained twice.
    //
    // Built by hand rather than through a builder because there is no
    // builder for this — the panel's demo path fills `player_.sharpness`
    // field by field in setupDemoData() and there is no shared model to
    // route it through. Copying the one struct is the same relationship
    // the window has to the panel, and the panel is the authority.
    sharpness_.valid      = true;
    sharpness_.level      = 6;        // Purple
    sharpness_.currentHits = 177;     // whole-bar remainder (130 + 47)
    sharpness_.maxHits    = 190;
    sharpness_.threshold  = 130;      // thresholds[level - 1]
    sharpness_.thresholds[0] = 5;
    sharpness_.thresholds[1] = 15;
    sharpness_.thresholds[2] = 30;
    sharpness_.thresholds[3] = 60;
    sharpness_.thresholds[4] = 90;
    sharpness_.thresholds[5] = 130;
    sharpness_.thresholds[6] = 190;
    demoData_ = true;
}

void PlayerSharpnessStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an unmounted-but-alive
    // window never renders a leftover sliver. The live path never reaches
    // here — the main loop asks hasVisibleContent() first. This is the
    // edit-preview / defensive branch.
    if (!hasContent()) {
        setContentSize(2 * kMargin, h);
        return;
    }

    // setContentSize has an idempotent guard inside Panel, so calling it
    // per repaint (and therefore per drag frame) is free.
    setContentSize(w, h);

    // Re-read the width the window actually got. setContentSize()
    // applied the user's minimum-width floor to its arguments, but `w` is
    // still the pre-floor content number — using it here is exactly the
    // bug the floor was added to fix: the window would widen and the
    // gauge would stay 174px, leaving empty background to the right of
    // the badge. bars took exactly this hit and calls the same helper
    // (panel_player_split_bars.cpp:368-376).
    //
    // The window only ever grows, so the same max() the base class
    // applied reproduces it without asking the compositor anything.
    const int drawnW = mhw::panel_sizing::effectiveWidth(w, minWidth());

    // Chrome: NONE, of the frame kind. drawV03Chrome() is deliberately
    // NOT called — the gauge carries its own segments, badge, glow and
    // gap-fill, so a panel frame around it is the shape the split exists
    // to remove. The translucent BODY is still drawn, by paintEvent(),
    // for the same reason it is drawn for the other six windows
    // (v0.12.1: the frame is chrome, the surface is not).
    const int innerRight = drawnW - kMargin;
    const int rowY       = kMargin + kWeaponGap;
    const int rowH       = kWeaponH;

    // The gauge is right-anchored in the row, which is where the panel
    // put it: the painter takes the block's RIGHT edge and lays the
    // badge and the gauge back from it. In the panel the block was also
    // CENTRED — the two costs cancelled when the block was 174 wide and
    // the row had 352 — but anchoring is the only thing that keeps the
    // badge flush against the same margin as the window's own body when
    // the user widens the window with the `-` / `=` floor keys.
    //
    // The return value is discarded. It exists so the panel could trim
    // the name/MR rect out of the gauge's way, and there is no name in
    // this window.
    (void)drawPlayerSharpnessBar(p, sharpness_, innerRight, rowY, rowH);
}
