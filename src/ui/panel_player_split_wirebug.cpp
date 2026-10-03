// SPDX-License-Identifier: Apache-2.0
//
// PlayerWirebugStripPanel implementation. drawWirebug() below is this
// file's own copy of the one in panel_player.cpp:394-459, which lives in
// that file's anonymous namespace and is unreachable from here. Writing it
// out again is the deliberate cost of the split being purely additive:
// hoisting it into a shared header would mean editing panel_player.cpp,
// which this change may not touch. The copy is confined to the block being
// extracted, so it disappears the day the two merge again
// (panel_player_split.h carries the same note for the status strip).

#include "panel_player_split_wirebug.h"

#include "core/string_table.h"
#include "ui/panel_player_metrics.h"
#include "ui/panel_sections.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

// i18n lookup, defined the same way panel_player.cpp:31-33 defines it,
// and used for exactly the two keys panel_player.cpp:1161-1162 asks for:
// the wirebug label pair. The labels are the only localized text on this
// window (the rest is "%1s" and READY, which are the panel's own literals
// and therefore stay as they are — panel_player.cpp:455 writes READY
// unwrapped, and a ready wirebug is not drawn at all in this window).
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- Layout tokens (panel_player.cpp:37-60, the ones this block owns) ---
constexpr int kPanelW    = 378;     // HTML: .op width:378px — the SOURCE
                                    // panel's width, carried only as the
                                    // reference the extracted numbers came
                                    // from. This window never sets a
                                    // content size of 378.
constexpr int kMargin    = 11;      // HTML: .op padding:11
constexpr int kPillH     = 18;      // HTML: .pill height (b + span ~9px +
                                    // padding). The panel reserves it for
                                    // the whole row (panel_player.cpp:753).
constexpr int kPillRowGap = 4;      // Vertical gap between the pill rows
                                    // of a wrapping pill grid. Only one row
                                    // is reachable for wirebugs (four
                                    // source slots), so this token is the
                                    // arithmetic the same one row uses.
constexpr int kPillGap   = 5;       // Horizontal gap between capsules on
                                    // the row (panel_player.cpp:1149).
constexpr int kWirebugGap = 9;      // panel_player.cpp:752
                                    // (kGapSection, so the leading gap
                                    // inside this window is 9)
constexpr int kMaxCapsules = 4;     // mhw::kRiseWirebugSlotCap — four
                                    // source slots maximum, so the row
                                    // never wraps.

// The capsule's fonts, and the numbers contentWidth() measures against.
// panel_player.cpp:439 (name, 9px Medium) and :450 (timer, 9px regular).
constexpr int kCapsuleFontSize = 9;

// panel_player.cpp:388-393 — the wirebug capsule. Visually like .pill but
// the fill colour encodes the cooldown progress:
//   cooldown == 0  → teal "ready" (matches the live HUD indicator)
//   cooldown > 0   → amber "recovering", progress fills from left
// Temporary (environment / skill) wirebugs get a thinner left accent
// stripe so the default slot is still obvious at a glance.
void drawWirebug(QPainter &p, const QRectF &box, const QString &name,
                 float cooldown, float maxCooldown, bool temporary)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(22, 24, 26));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(box, 2, 2);

    const float pct = (maxCooldown > 0.0F)
        ? std::clamp(cooldown / maxCooldown, 0.0F, 1.0F) : 0.0F;
    const bool ready = (cooldown <= 0.001F);
    // Accent colour: teal when ready, amber while recovering. Slightly
    // muted when the wirebug is a temp slot so the default pops.
    const QColor accent = ready
        ? (temporary ? QColor(60, 160, 150)   : QColor(80, 197, 183))
        : (temporary ? QColor(200, 150, 30)   : QColor(255, 170, 60));

    // Progress fill: full capsule background for ready, left-fill for
    // recovering. A clipping path keeps the fill inside the rounded
    // corners.
    if (ready) {
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 36));
        p.drawRoundedRect(box, 2, 2);
    } else if (pct > 0.0F) {
        QRectF fillRect(box.x(), box.y(),
                        box.width() * pct, box.height());
        QPainterPath clip;
        clip.addRoundedRect(box, 2, 2);
        p.setClipPath(clip);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 70));
        p.drawRect(fillRect);
        p.setClipping(false);
    }

    // Left accent stripe (thinner for temporary wirebugs).
    const QRectF stripe(box.x(), box.y(), temporary ? 2.0 : 3.0, box.height());
    p.setBrush(accent);
    p.drawRect(stripe);

    // 1px outline.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(42, 45, 47), 1));
    p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);

    // Name on the left, timer on the right.
    QFont bFont(QStringLiteral("Chakra Petch"), kCapsuleFontSize, QFont::Medium);
    bFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(bFont);
    p.setPen(QColor(245, 246, 247));
    const qreal nameLeft = stripe.right() + 6;
    const QRectF nameRect(nameLeft, box.y(),
                          std::max(0.0, box.width() * 0.45F), box.height());
    p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(bFont).elidedText(name, Qt::ElideRight,
                                              static_cast<int>(nameRect.width())));

    QFont tFont(QStringLiteral("Chakra Petch"), kCapsuleFontSize);
    tFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(tFont);
    p.setPen(ready ? QColor(80, 197, 183) : QColor(210, 214, 216));
    const QString timerTxt = ready
        ? QStringLiteral("READY")
        : QStringLiteral("%1s").arg(static_cast<int>(cooldown));
    // A recovering wirebug always has a number; READY is unreachable in
    // this window because a ready wirebug is filtered out of the model
    // (see the header). The literal is kept so the painter stays a true
    // copy of the one it mirrors.
    p.drawText(QRectF(box.x() + 4, box.y(), box.width() - 8, box.height()),
               Qt::AlignRight | Qt::AlignVCenter, timerTxt);
}

// The name + timer columns' width inside ONE capsule, in logical px. This
// is the number contentWidth() measures a label against, and it is the
// same column arithmetic drawWirebug() uses: stripe | 6 | name | ... | 6
// | 6 | timer. Measuring it separately from the painter's arithmetic is
// how the panel ends up with a label that elides one glyph earlier than
// the measurement said.
[[nodiscard]] int capsuleColumnsWidth(const QString &name,
                                      const QString &timer)
{
    const QFont nameFont(QStringLiteral("Chakra Petch"), kCapsuleFontSize,
                         QFont::Medium);
    const QFontMetrics nameMetrics(nameFont);
    const QFont timerFont(QStringLiteral("Chakra Petch"), kCapsuleFontSize);
    const QFontMetrics timerMetrics(timerFont);
    const int nameW = nameMetrics.horizontalAdvance(name);
    const int timerW = timerMetrics.horizontalAdvance(timer);
    return 3 + 6 + nameW + 12 + timerW;
}

} // namespace

namespace mhw {

// The pure transformation: the recovering subset, in source order.
//
// The Rise arm reads the wirebug vector; the World arm reads none, on
// purpose. World has no wirebug system, so the World reader never fills
// `wirebugs` — but relying on that is relying on a fact about a DIFFERENT
// module, and it is the trap the audit records twice
// (panel_player.cpp:676 gates the panel on `game_ == Rise` rather than on
// the data, and panel_player_metrics.h:56-60 replaced a ternary that made
// a third game inherit the World arm). The switch is the seam: a World
// snapshot carrying a Rise-shaped wirebug vector answers empty, and a
// third game has to be given an arm explicitly rather than borrowing this
// one.
//
// The slot number is the entry's own `slot` field, never the vector index
// (panel_player_metrics.h:73-76 — the reader keeps a snapshot's original
// Mono-array slot even when None entries are omitted), so a wirebug that
// comes back ready does not renumber the capsules next to it.
// wirebugSlotLabel() resolves that slot into the display label at draw
// time, which is where panel_player.cpp:1161-1162 resolves it too.
WirebugModel buildWirebugModel(const mhw::GameSnapshot &snap)
{
    WirebugModel model;

    // The panel's own rule, applied once here so the model, the
    // reservation and the draw all see the same capsules: a wirebug whose
    // cooldown has run out earns no capsule at all ("冷却完了直接消失",
    // v0.7.2).
    switch (snap.game) {
    case mhw::GameId::World:
        break;
    case mhw::GameId::Rise:
        for (const auto &w : snap.player.wirebugs) {
            if (w.cooldown <= wirebugReadyEpsilon())
                continue;
            WirebugRow row;
            row.slot = w.slot;
            row.cooldown = w.cooldown;
            row.maxCooldown = w.maxCooldown;
            row.temporary = w.isTemporary;
            row.progress = (w.maxCooldown > 0.0F)
                ? std::clamp(w.cooldown / w.maxCooldown, 0.0F, 1.0F)
                : 0.0F;
            model.capsules.append(row);
        }
        break;
    }

    return model;
}

} // namespace mhw

PlayerWirebugStripPanel::PlayerWirebugStripPanel(QWidget *parent)
    : Panel(QStringLiteral("player-wirebug"), Corner::TopLeft, parent)
{
    // No chrome, no title row: a capsule row is a fragment of the player
    // panel, not a panel. setChromeVisible(false) suppresses the v0.3
    // rounded body, left accent stripe and border entirely, so the window
    // surface stays transparent apart from the capsules' own dark bodies.
    // isSubWindow() (below) declares the same fact for the console's slot
    // registry.
    setChromeVisible(false);

    // Own window-title key, not ui.wirebug_slot (that is the per-capsule
    // label, and a window titled 翔虫 next to a capsule labelled 翔虫
    // says nothing about which is which).
    setWindowTitle(mh::tr("ui.player_strip_title"));
}

void PlayerWirebugStripPanel::update(const mhw::GameSnapshot &snap)
{
    // Same shape as PlayerPanel::update(): cache what paintPanel() needs,
    // schedule one repaint. PlayerPanel's other mirrored fields (zone,
    // quest, connection, MR/name/party) are not read here — this window
    // draws none of them.
    rows_ = mhw::buildWirebugModel(snap);

    // Demo capsules are never live state (the rule PetDamagePanel and the
    // pilot strip both apply): the first authoritative snapshot replaces
    // them outright, so an edit-mode preview cannot survive into a live
    // session.
    demoData_ = false;

    triggerUpdate();
}

void PlayerWirebugStripPanel::retranslateUi()
{
    // Cached strings first, exactly like the pilot's retranslateUi() and
    // for the same reason: main.cpp's applyLocale re-sets the title from
    // the same key, so the second set is the deliberate redundancy every
    // panel carries. There is no demo seed to drop — the demo capsules
    // are numbered slots, and their labels come from the same tr() pair
    // the painter resolves, so they follow the locale on the next paint
    // without a rebuild.
    setWindowTitle(mh::tr("ui.player_strip_title"));
    triggerUpdate();
}

bool PlayerWirebugStripPanel::wirebugEnabled() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing PlayerSection::Wirebug means the
    // console's existing player-section toggle drives this window for
    // free — a user who has already turned the wirebug row off in the
    // single panel gets the same layout here, with no new console UI.
    return (sectionMask() & mhw::PlayerSection::Wirebug) != 0;
}

bool PlayerWirebugStripPanel::hasContent() const
{
    if (demoData_ && editMode())
        return true;
    if (!wirebugEnabled())
        return false;
    // Empty under World (the reader publishes nothing) and when every
    // wirebug is ready. Either way the window must not exist: the main
    // loop's hasVisibleContent() answers false and the layer-shell
    // surface is never mounted.
    return !rows_.isEmpty();
}

int PlayerWirebugStripPanel::contentHeight() const
{
    // One capsule row. The panel reserves kWirebugGap + kWirebugH (9 + 18,
    // panel_player.cpp:752-753) and this window keeps the same 18 so a
    // capsule drawn side by side with the panel's is the same size.
    return kMargin + kWirebugGap + kPillH + kMargin;
}

int PlayerWirebugStripPanel::contentWidth() const
{
    // Content width, not a chrome width: the capsules laid out with the
    // panel's own 5px inter-capsule gap, each as wide as its label +
    // timer needs. Start from the panel's per-capsule width for the
    // current count (panel_player.cpp:1153 — slotW = (innerW - 5*(n-1)) /
    // n with innerW = 356), then take the larger of that and what the
    // widest capsule actually needs, so the panel and this window agree
    // when they agree and this window never elides a label the panel
    // would have shown whole.
    const int n = rows_.capsules.size();
    if (n <= 0)
        return 2 * kMargin;
    if (!wirebugEnabled())
        return 2 * kMargin;

    const int panelInnerW = kPanelW - 2 * kMargin;      // 356
    const int slotW = (panelInnerW - kPillGap * (n - 1)) / n;

    const QString plain = mh::tr("ui.wirebug_slot");
    const QString numbered = mh::tr("ui.wirebug_slot_n");
    int widest = 0;
    for (const mhw::WirebugRow &row : rows_.capsules) {
        const QString name = wirebugSlotLabel(row.slot, plain, numbered);
        const QString timer = QStringLiteral("%1s").arg(
            static_cast<int>(row.cooldown));
        widest = std::max(widest, capsuleColumnsWidth(name, timer));
    }
    const int usedSlotW = std::max(slotW, widest);
    return n * usedSlotW + kPillGap * (n - 1) + 2 * kMargin;
}

void PlayerWirebugStripPanel::setupDemoData()
{
    // Edit-mode preview: three recovering wirebugs, one default and two
    // temporary, so the accent-stripe thickness, the two colour families
    // and the left-filling progress are all visible at once. The slots
    // and the cooldowns are the same ones PlayerPanel's own demo seeds
    // (panel_player.cpp:1309-1325), so the two windows' previews agree
    // and neither has to be explained twice.
    mhw::PlayerSnapshot pl;
    pl.valid = true;
    mhw::WirebugSnapshot w0;
    w0.slot = 0; w0.isAvailable = true;
    w0.cooldown = 12.0F; w0.maxCooldown = 30.0F;
    pl.wirebugs.append(w0);
    mhw::WirebugSnapshot w1;
    w1.slot = 1; w1.isAvailable = true; w1.isTemporary = true;
    w1.cooldown = 8.0F; w1.maxCooldown = 30.0F;
    pl.wirebugs.append(w1);
    mhw::WirebugSnapshot w2;
    w2.slot = 2; w2.isAvailable = true;
    w2.isTemporary = true;
    w2.cooldown = 20.0F; w2.maxCooldown = 30.0F;
    pl.wirebugs.append(w2);
    // A fourth, ready one: it must NOT produce a capsule, which is what
    // keeps the preview at the three-capsule row the panel's demo draws.
    mhw::WirebugSnapshot w3;
    w3.slot = 3; w3.isAvailable = true;
    w3.cooldown = 0.0F; w3.maxCooldown = 30.0F;
    pl.wirebugs.append(w3);

    mhw::GameSnapshot snap;
    snap.game = mhw::GameId::Rise;
    snap.player = pl;
    rows_ = mhw::buildWirebugModel(snap);
    demoData_ = true;
}

void PlayerWirebugStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const int h = contentHeight();
    const int w = contentWidth();

    // Nothing to show: a collapsed content size so an unmounted-but-alive
    // window never renders a leftover sliver. The live path never reaches
    // here — the main loop asks hasVisibleContent() first. This is the
    // edit-preview / defensive branch.
    if (!wirebugEnabled() || rows_.isEmpty()) {
        setContentSize(2 * kMargin, h);
        return;
    }

    // setContentSize has an idempotent guard inside Panel, so calling it
    // per repaint (and therefore per drag frame) is free.
    setContentSize(w, h);

    // Chrome: NONE. drawV03Chrome() is deliberately not called — the
    // capsules carry their own dark bodies, and a panel frame around them
    // is the shape the split exists to remove. This panel calls neither
    // drawV03Chrome() nor paintPanelChrome(), exactly like the pilot
    // status strip.

    const int n = rows_.capsules.size();
    const int innerW = w - 2 * kMargin;
    const int slotW = (innerW - kPillGap * (n - 1)) / n;

    int y = kMargin + kWirebugGap;
    for (int i = 0; i < n; ++i) {
        const mhw::WirebugRow &row = rows_.capsules[i];
        const int cx = kMargin + i * (slotW + kPillGap);
        const QRectF pillRect(cx, y, slotW, kPillH);
        // The label pair is re-resolved at the draw site, exactly as
        // panel_player.cpp:1161-1162 does, so a locale flip lands on the
        // next paint without the model being rebuilt. The slot number is
        // WirebugSnapshot::slot carried through the model, NOT the
        // capsule's index in this vector.
        const QString plain = mh::tr("ui.wirebug_slot");
        const QString numbered = mh::tr("ui.wirebug_slot_n");
        drawWirebug(p, pillRect,
                    wirebugSlotLabel(row.slot, plain, numbered),
                    row.cooldown, row.maxCooldown, row.temporary);
    }
}
