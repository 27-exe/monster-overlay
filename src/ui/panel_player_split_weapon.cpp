// SPDX-License-Identifier: Apache-2.0
//
// PlayerWeaponStripPanel — PlayerPanel's weapon row as its own window.
// See panel_player_split_weapon.h for what this window is, why the
// sharpness gauge is in it again (it left, the user sent it back), and
// why the mask claim is `Weapon | Sharpness` while the two toggles
// still answer their own questions.
//
// The weapon-slot painter below is this file's own copy of drawWslot()
// (panel_player.cpp:224-236), which lives in that file's anonymous
// namespace and is unreachable from here. Writing it out again is the
// deliberate cost of the split being purely additive: hoisting it into
// a shared header would mean editing panel_player.cpp, which this
// change may not touch. The copy is confined to the block being
// extracted, so it disappears the day the two merge again.
//
// The gauge painter is NOT a copy. panel_player_sharpness.{h,cpp}
// exports it as a free function precisely so it can be reached from a
// second window, and calling it from here is the same call
// panel_player.cpp:966-968 makes. Its own rowY / rowH are different —
// the split window gives the block its own 30px row so the badge is
// never clipped by the row edge the way the panel's 26px prow let it
// be (numRect starts 2px ABOVE prowY there, see
// panel_player_sharpness.cpp:89-90).

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

#include <cmath>
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
// ---- Layout tokens --------------------------------------------------
// The merged window's size arithmetic lives in the header, in namespace
// weapon_row, where the model test can reach it — the width and height
// are decisions the user sees, and a test of them that cannot call the
// same functions paintPanel() calls is a test of nothing. What stays
// here is only what the PAINTER needs: the colours, the fonts, and the
// text shapes.

// Aliases, so the paint body below reads the same names it always did
// while the numbers stay defined once, in one place. Note the
// qualification: weapon_row lives in namespace mhw (see the header), so
// an unqualified `using weapon_row::kMargin;` here would not resolve.
using mhw::weapon_row::kMargin;
using mhw::weapon_row::kWslot;
using mhw::weapon_row::kWeaponGap;
using mhw::weapon_row::kIconTextGap;
// The gap between the two rows and the gauge row's height, both of which
// the paint places by hand — the header's arithmetic uses them too, so
// they are defined once there and aliased here rather than copied.
using mhw::weapon_row::kIdentityLineGap;
using mhw::weapon_row::kGaugeRowH;
// The slot's own height. The panel's prow is the same 26px, which is
// why the identity row is max(pitch, this) rather than the slot alone
// (mhw::weapon_row::identityRowHeight()).
constexpr int kWeaponH = mhw::weapon_row::kWslot;

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

// ---- One measurement, three consumers ---------------------------------
//
// The arithmetic itself — row heights, stream widths, and the two max()
// trees the window's size comes from — lives in the header, in namespace
// mhw::weapon_row, so player_weapon_strip_tests can assert on the actual
// numbers with a QtCore-only target. What is here is the measurement of
// the two strings, which needs the fonts the painter sets and therefore
// cannot leave this file.

struct WeaponRowMetrics {
    int nameW{0};        // identity stream's name column, measured
    int mrW{0};          // identity stream's MR column, measured
    int identityW{0};    // kWslot + gap + nameW + gap + mrW
    int identityRowH{0}; // max(ceil(font height), kWslot)
    int gaugeW{0};       // playerSharpnessBarTotalWidth(), or 0
};

// `withIdentity` / `withGauge` are the callers' own answers from the two
// gates: a bit that goes off must drop its stream from the width, the
// height AND the paint, and the three callers must not each re-derive
// which streams are on.
WeaponRowMetrics measureWeaponRow(const mhw::WeaponIdentityRow &row,
                                  bool withIdentity, bool withGauge)
{
    WeaponRowMetrics m;
    m.identityRowH = mhw::weapon_row::identityRowHeight();

    if (withIdentity) {
        const QFontMetrics nameMetrics(identityNameFont());
        const QFontMetrics mrMetrics(identityMrFont());
        m.nameW = nameMetrics.horizontalAdvance(nameText(row.name));
        m.mrW   = mrMetrics.horizontalAdvance(mrText(row.masterRank));
        m.identityW = mhw::weapon_row::identityStreamWidth(m.nameW, m.mrW);
    }
    if (withGauge)
        m.gaugeW = mhw::weapon_row::gaugeStreamWidth();

    return m;
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

    // v1.0.0: 「世界在开始界面就显示了我的昵称和 MR」.
    //
    // name / masterRank / weaponId are SAVE HEADER fields. The process has
    // them the instant the game loads a save, which is well before the
    // player enters a quest — so the window mounted on the title screen
    // and named a hunter who had not been sent anywhere. Not demo data:
    // setupDemoData() is gated behind editMode_ (panel.cpp:1153), and this
    // function never consults it. It is real data arriving earlier than it
    // is useful.
    //
    // maxHealth is the discriminator — zero until the in-game player struct
    // exists, positive once it does. Emitting the empty row here rather
    // than refusing inside resolveIdentity() keeps that function a pure
    // transform of the snapshot ("what did it say"), the contract its
    // 68-check suite pins. This function answers the different question
    // ("what should this window show"), and hasContent() downstream turns
    // the empty row into an unmounted surface.
    if (snap.player.maxHealth <= 0.0F)
        return WeaponIdentityRow{};

    row.weaponId   = id.weaponId;
    row.name       = id.name;
    row.hasName    = !id.name.isEmpty();
    row.masterRank = id.masterRank;

    // The sharpness block the panel mirrors from the same snapshot
    // (panel_player.cpp:580-582). Verbatim, so the painter's own
    // valid / level guards decide what gets drawn — this window adds no
    // second opinion about whether a gauge should show.
    row.sharpness  = snap.player.sharpness;
    return row;
}

} // namespace mhw

PlayerWeaponStripPanel::PlayerWeaponStripPanel(QWidget *parent, int subWindowIndex)
    : Panel(QStringLiteral("player-weapon"), Corner::TopLeft, parent,
        subWindowIndex)
{
    // No chrome, no title row: the weapon row is a fragment of the
    // player panel, not a panel. setChromeVisible(false) suppresses the
    // v0.3 rounded body, left accent stripe and border entirely, so the
    // window surface stays transparent apart from the slot's own dark
    // body and the text columns. isSubWindow() (in the header)
    // declares the same fact for the console's slot registry.
    setChromeVisible(false);

    // Own window-title key, the one the other split windows use.
    // The row draws no label of its own, so there is nothing more
    // specific to title it, and reusing ui.player_strip_title means
    // main.cpp's applyLocale re-sets it from exactly the same key the
    // others do instead of this file inventing a second one.
    setWindowTitle(mh::tr("ui.player_strip_title"));
}

void PlayerWeaponStripPanel::update(const mhw::GameSnapshot &snap)
{
    // Same shape as PlayerPanel::update(): cache what paintPanel() needs,
    // schedule one repaint. The panel's other mirrored fields (zone,
    // quest, connection, vitals) are not read here — this window draws
    // none of them, which is the whole point of the row being a
    // fragment. `sharpness` IS mirrored, and is the whole reason this
    // window is a window again rather than two of them.
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
    // Panel::setPanelEnabled). Reads `Weapon` and NOT `Sharpness`:
    // the console's weapon toggle hides the identity trio and leaves
    // the gauge drawing, which is the whole reason Sharpness became its
    // own bit back in abaa70e.
    return (sectionMask() & mhw::PlayerSection::Weapon) != 0;
}

bool PlayerWeaponStripPanel::sharpnessEnabled() const
{
    // The gauge's own gate. Same shape, same role, opposite bit: this is
    // what keeps "turn the weapon row off" from also answering "am I
    // about to bounce". Reads the bit AND the snapshot's own validity —
    // a ranged weapon has no gauge and must not reserve 174px of width
    // or 30px of height for one.
    if ((sectionMask() & mhw::PlayerSection::Sharpness) == 0)
        return false;
    return mhw::weaponIdentitySharpnessDrawable(row_.sharpness);
}

bool PlayerWeaponStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    if (!identityOrSharpnessOn())
        return false;
    // No name, no weapon AND no drawable sharpness: the reader has
    // nothing to say about who is playing, so the window must not exist
    // — the main loop's hasVisibleContent() answers false and the
    // layer-shell surface is never mounted. PlayerPanel is visible in
    // that state precisely because it has a "not connected" placeholder
    // to show; a fragment window has no such obligation, and an icon
    // next to "--" next to "MR --" would be three placeholders with
    // nothing behind them.
    //
    // A LIVE GAUGE IS DATA. This is the merge's one semantic change to
    // hasContent(), and it is the same answer the gauge's own window
    // gave: a melee weapon reads its sharpness every poll, so the bar
    // is worth a window even when both the id and the name fail to
    // resolve — the exact state the "icon + MR with no gauge" report
    // came out of.
    return !row_.isEmpty();
}

int PlayerWeaponStripPanel::contentHeight() const
{
    // The band for the streams that are on, plus the panel's own leading
    // gap and the margin at each end. All of the arithmetic is the
    // header's mhw::weapon_row::contentHeight() — this window and the
    // model test read the same three numbers.
    //
    //   both on : 11 + 9 + (26 + 6 + 30) + 11 = 93
    //   identity: 11 + 9 + 26 + 11            = 57
    //   gauge   : 11 + 9 + 30 + 11            = 61
    //   neither : 22 (2*kMargin, alive but unmounted)
    return mhw::weapon_row::contentHeight(weaponEnabled(),
                                          sharpnessEnabled());
}

int PlayerWeaponStripPanel::contentWidth() const
{
    if (!identityOrSharpnessOn())
        return 2 * mhw::weapon_row::kMargin;

    // One measurement pass, three consumers (this reservation, the
    // paint's placement, and the collapse branch) — see
    // measureWeaponRow() above for what is measured where.
    const WeaponRowMetrics m = measureWeaponRow(
        row_, weaponEnabled(), sharpnessEnabled());

    // The two rows SHARE one width budget instead of accumulating. That
    // is the whole compactness argument for merging the two windows: the
    // pair's widths added up to 331px (135 + 196), the merged window is
    // max(135, 174) + 22 = 196, because the identity row's leftover
    // width is exactly what the gauge row now gets for free.
    return mhw::weapon_row::contentWidth(m.identityW, m.gaugeW);
}

void PlayerWeaponStripPanel::setGameForDemo(mhw::GameId game)
{
    if (game_ == game)
        return;
    game_ = game;
    // Drop the one-shot demo seed so the next paint re-runs
    // setupDemoData() under the new game.
    resetDemoPrimed();
    triggerUpdate();
}

void PlayerWeaponStripPanel::setDemoGame(mhw::GameId game)
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

void PlayerWeaponStripPanel::setupDemoData()
{
    // Edit-mode preview: the same hunter the panel's own demo seeds
    // (panel_player.cpp:1291-1293 — Great Sword, MR 247, the localized
    // data.demo.player_name), plus a sharpness block that reads the way
    // a live melee weapon's does, so the edit preview shows the gauge
    // instead of a trio with a hole where the bar should be.
    //
    // Built as a snapshot and pushed through the same builder the live
    // path uses rather than filling the row by hand: the demo then
    // exercises the real transformation, and a fix to the identity or
    // sharpness merge lands in the preview too. `valid = true` is what
    // keeps resolveIdentity() off its not-attached branch, which would
    // collapse the weapon to -1 (player_view_model.cpp:41-46).
    mhw::SharpnessSnapshot shp;
    shp.valid      = true;
    shp.level      = 4;                 // green — mid-gauge, visibly "on"
    shp.thresholds[0] = 60;
    shp.thresholds[1] = 120;
    shp.thresholds[2] = 180;
    shp.thresholds[3] = 240;
    shp.thresholds[4] = 340;
    shp.thresholds[5] = 0;              // unreachable colours stay 0
    shp.thresholds[6] = 0;
    shp.maxHits    = 100;
    shp.currentHits = 40;

    // v0.13: seeded under `game_` rather than a hard-coded World, the
    // same seam the other five split windows gained. Both games' harness
    // is identical today (Great Sword, MR 247, the same sharpness
    // levels), so this changes nothing visible — it only stops a future
    // per-game weapon id from being resolved against the wrong table.
    mhw::PlayerSnapshot pl;
    pl.valid      = true;
    pl.weaponId   = 0;                 // Great Sword, panel_player.cpp:1291
    pl.masterRank = 247;               // panel_player.cpp:1292
    pl.name       = mh::tr("data.demo.player_name");
    pl.sharpness  = shp;
    mhw::GameSnapshot snap;
    snap.game   = game_;
    snap.player = pl;
    row_ = mhw::buildWeaponIdentityModel(snap);
    demoData_ = true;
}

void PlayerWeaponStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const bool withIdentity = weaponEnabled();
    const bool withGauge = sharpnessEnabled();

    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an unmounted-but-alive
    // window never renders a leftover sliver. The live path never reaches
    // here — the main loop asks hasVisibleContent() first. This is the
    // edit-preview / defensive branch.
    //
    // `h` is contentHeight(), which for this state IS 2*kMargin — the
    // two numbers are the same by construction now (see
    // weapon_row::contentHeight), so the collapsed branch cannot
    // disagree with the reservation the way the first draft's did.
    if (!withIdentity && !withGauge) {
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
    // slot carries its own dark body and the text columns have no
    // background in the source panel either, so a frame around them is
    // the shape the split exists to remove. This panel calls neither
    // drawV03Chrome() nor paintPanelChrome(), exactly like the other
    // split windows.
    const int innerW = drawnW - 2 * kMargin;

    // The content band, from the window's top edge: the panel's own
    // leading gap, then the band for the streams that are on. The slot
    // and both rows are laid out relative to it.
    const int bandY = kMargin + kWeaponGap;

    const WeaponRowMetrics m = measureWeaponRow(row_, withIdentity, withGauge);
    const QString nm = nameText(row_.name);
    const QString mr = mrText(row_.masterRank);

    // The left edge of both rows. Flush kMargin, not centred: HunterPie
    // sets the same HorizontalAlignment="Left" on the weapon cell, and a
    // gauge stretched or centred across width the NAME bought is the
    // panel's own mistake made in reverse. The consequence is a ragged
    // right edge in the default case — the identity stream measures 113
    // inside a 196px window — and that is preferred to floating two
    // unrelated rows around inside the frame.
    const int left = kMargin;

    // ---- weapon slot (identity row's left column) ----------------------
    // An unresolved weapon id (<0) leaves the slot empty but drawn: the
    // frame is the slot's identity and a missing read is not a missing
    // slot. Same branch as panel_player.cpp:949-951, including that an
    // empty path handed to Icon::render() produces a null pixmap the
    // painter already handles.
    //
    // Drawn on the identity row, and the identity row is the taller of
    // the glyph pitch and the slot (see identityRowHeight()), so there
    // is no slack to distribute and no centring arithmetic to get wrong:
    // the slot always fits its own row exactly, and the row always fits
    // the band. The vertical arithmetic that the first draft's plain
    // pitch got wrong is now impossible.
    if (withIdentity) {
        const QRectF slotRect(left, bandY, kWslot, kWeaponH);
        const QString wp = row_.weaponId >= 0
            ? mhw::Icon::weaponPath(row_.weaponId, 1) : QString();
        drawWeaponSlot(p, slotRect, mhw::Icon::render(wp, kWslot));
    }

    // ---- identity text (same row, right of the slot) -------------------
    // The panel's .prow is a flex row: MR follows the name with an 8px
    // gap and is NOT pushed to the right edge
    // (panel_player.cpp:955-997). That reading order is kept, and it is
    // what keeps the trio reading as one unit now that it is stacked
    // above the gauge instead of squeezed beside it.
    //
    // The row's rect is the measured width, drawn left-aligned and
    // vertically centred, so a name is never elided and never wrapped.
    if (withIdentity) {
        const int rowOneY = bandY;
        const int rowOneX = left + kWslot + kIconTextGap;

        p.setFont(identityNameFont());
        p.setPen(QColor(kNamePenR, kNamePenG, kNamePenB));
        p.drawText(QRectF(rowOneX, rowOneY, m.nameW, m.identityRowH),
                   Qt::AlignLeft | Qt::AlignVCenter, nm);

        p.setFont(identityMrFont());
        p.setPen(QColor(kMrPenR, kMrPenG, kMrPenB));
        p.drawText(QRectF(rowOneX + m.nameW + kIconTextGap, rowOneY,
                          m.mrW, m.identityRowH),
                   Qt::AlignLeft | Qt::AlignVCenter, mr);
    }

    // ---- sharpness gauge (row two) ------------------------------------
    // THE SAME PAINTER the panel calls (panel_player.cpp:966-968), with
    // the same three arguments it takes: a right edge, a row Y and a row
    // height. The right edge is the inner right margin, so the badge is
    // flush against it — the HunterPie arrangement, where the badge
    // rides at the gauge's right (WeaponSharpnessView.xaml:108-121).
    //
    // Its OWN row, kGaugeRowH tall: the badge is 30px and the panel's
    // 26px prow let it start 2px above the row and end 2px below it
    // (panel_player_sharpness.cpp:89-90). Not reproduced here on
    // purpose — this is a new window, not a baseline, and a badge
    // clipped by its own row edge is a defect the panel could not have
    // fixed without moving the gauge.
    //
    // rowY = the band's top plus the identity row and the gap when the
    // identity is present; the band's top when it is not. The band
    // height is defined as exactly the rows it contains (see
    // weaponBandHeight()), so this offset and the height can never
    // disagree and no stream can be drawn outside the window.
    if (withGauge) {
        const int rowTwoY = bandY
            + (withIdentity ? m.identityRowH + kIdentityLineGap : 0);
        const int gaugeRightX = left + innerW;
        drawPlayerSharpnessBar(p, row_.sharpness, gaugeRightX, rowTwoY,
                               kGaugeRowH);
    }
}
