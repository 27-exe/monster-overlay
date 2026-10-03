// SPDX-License-Identifier: Apache-2.0
//
// PlayerStatusStripPanel — the v0.12 "block → window" pilot.
// See panel_player_split.h for what this panel is and why it is purely
// additive to PlayerPanel.
//
// Extraction basis: experiment/monster-overlay-v012/design/
// 01-hunterpie-split.md §2.1 / §6.3 / §7.1 / §10.2.

#include "panel_player_split.h"

#include "core/string_table.h"
#include "player/player_types.h"
#include "ui/panel_sections.h"

#include "rise/mhr_abnormalities.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QRectF>

#include <algorithm>
#include <functional>

// i18n lookup. Same definition as panel_player.cpp's, ahead of the
// anonymous namespace so the free helpers below can resolve keys exactly
// like paintPanel() does. See docs/I18N.md for the "no fallback in code"
// rule: every string that reaches the screen is a key.
namespace mh {
inline QString tr(const QString &key)
{
    return mhw::StringTable::instance().tr(key);
}
} // namespace mh

namespace {

// ---- Layout tokens -----------------------------------------------------------
//
// Same numeric values as panel_player.cpp so the strip and the block it
// replaced wrap and space identically. Kept as this file's own constants:
// retuning the strip must not be able to move PlayerPanel, which is the
// v0.12 promise.
//
// Width is NOT a token: this panel sizes itself to content (see
// PlayerStatusStripPanel::contentWidth), because a status strip has no
// title row or bar to align with and a fixed 378 would just be the old
// panel's width with dead air on the right.
constexpr int kMargin      = 6;     // outer breathing room (chrome is off)
constexpr int kPillH       = 18;    // HTML .pill height (panel_player.cpp:49)
constexpr int kPillRowGap  = 4;     // wrapped-row pitch (panel_player.cpp:57)
constexpr int kPillGap     = 5;     // horizontal gap inside a row
constexpr int kBlockGap    = 9;     // between the debuff and buff blocks
constexpr int kMinPillW    = 78;    // a one-glyph timer still readable
constexpr int kStripMaxW   = 420;   // a fully buffed hunter stays sane

// ---- Colour palette ----------------------------------------------------------
//
// Buff family default. PlayerPanel answers accentFor()'s None arm with
// purple (167,79,255), which is right for a DEBUFF whose trait was never
// resolved. A buff row and a debuff row on one strip must be
// distinguishable at a glance though, and HunterPie's AbnormalityTray
// colours positive states green — so the strip overrides ONLY the
// unresolved-Buff case, and every resolved trait keeps the same mapping
// PlayerPanel uses. That keeps the shared families (red attack buffs,
// cyan drinks) identical to the single-panel preview.
constexpr int kBuffR = 102;
constexpr int kBuffG = 187;
constexpr int kBuffB = 106;

// One World pill's timer text. Reads straight from the row the reader
// published, so the strip and the panel show the same seconds for the
// same entry.
QString worldTimerText(float timer)
{
    return QStringLiteral("%1s").arg(static_cast<int>(timer));
}

// ---- Pill drawing ------------------------------------------------------------
//
// Declared as mirrors of panel_player.cpp's anonymous-namespace helpers,
// reimplemented here on purpose (see the header's "borrows nothing"
// note). The tokens and the draw order are copied from that file; the
// comments below name the source lines so a future reader can diff the
// two.

// panel_player.cpp:326-361 — HTML .pill: dark body, 3px accent stripe,
// 1px --line outline, name left, timer right.
void drawPill(QPainter &p, const QRectF &box, const QColor &pc,
              const QString &name, const QString &timer)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(22, 24, 26));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(box, 2, 2);

    // 3px left accent stripe (HTML: border-left:3px solid var(--pc))
    const QRectF stripe(box.x(), box.y(), 3, box.height());
    p.setBrush(pc);
    p.drawRect(stripe);

    // 1px --line outline
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(42, 45, 47), 1));
    p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);

    QFont bFont(QStringLiteral("Chakra Petch"), 9, QFont::Medium);
    bFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(bFont);
    p.setPen(QColor(245, 246, 247));
    const QRectF nameRect(stripe.right() + 6, box.y(),
                          box.width() * 0.55F, box.height());
    p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(bFont).elidedText(
                   name, Qt::ElideRight,
                   static_cast<int>(nameRect.width())));

    QFont tFont(QStringLiteral("Chakra Petch"), 9);
    tFont.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(tFont);
    p.setPen(QColor(210, 214, 216));
    p.drawText(QRectF(box.x() + 4, box.y(), box.width() - 8, box.height()),
               Qt::AlignRight | Qt::AlignVCenter, timer);
}

// panel_player.cpp:373-386 — the single place a trait becomes a colour.
QColor accentFor(mhw::AbnormalityAccent accent)
{
    switch (accent) {
    case mhw::AbnormalityAccent::Blast:     return QColor(255, 87, 34);
    case mhw::AbnormalityAccent::Fire:      return QColor(255, 87, 34);
    case mhw::AbnormalityAccent::Defense:   return QColor(255, 193, 7);
    case mhw::AbnormalityAccent::Sleep:     return QColor(120, 120, 220);
    case mhw::AbnormalityAccent::Paralysis: return QColor(180, 130, 220);
    case mhw::AbnormalityAccent::Attack:    return QColor(244, 67, 54);
    case mhw::AbnormalityAccent::Drink:     return QColor(0, 188, 212);
    case mhw::AbnormalityAccent::None:      break;
    }
    return QColor(167, 79, 255);
}

// panel_player.cpp:488-502 — the wrapping grid arithmetic (rowIdx /
// colIdx / itemsInRow / slotW / cx), so the strip's 3-per-row wrap is
// pixel-identical to the panel's. The caller keeps ownership of what a
// pill says.
void drawPillGrid(QPainter &p, int top, int count, int pillsPerRow,
                  int rowGap, int pillGap, int rowH, int left, int width,
                  const std::function<void(QPainter &, int, const QRectF &)>
                      &paint)
{
    for (int i = 0; i < count; ++i) {
        const int rowIdx     = i / pillsPerRow;
        const int colIdx     = i % pillsPerRow;
        const int itemsInRow = std::min(pillsPerRow,
                                        count - rowIdx * pillsPerRow);
        const int slotW = (width - pillGap * (itemsInRow - 1)) / itemsInRow;
        const int cx    = left + colIdx * (slotW + pillGap);
        const QRectF box(cx, top + rowIdx * (rowH + rowGap), slotW, rowH);
        paint(p, i, box);
    }
}

// The name column of a strip pill with no display name of its own.
// panel_player.cpp:1204 uses the 「状态」 label for a nameless debuff and
// :1223 the 增益 label for a nameless buff; the strip reuses those two
// keys rather than adding its own, because no locale has a third wording
// for the same situation.
QString fallbackName(bool debuff)
{
    return debuff ? mh::tr("ui.player_status_section")
                  : mh::tr("ui.player_buff_fallback");
}

} // namespace

namespace mhw {

// The pure block → model transformation.
//
// World fills PlayerSnapshot::debuffs / ::buffs (memory-offset rows);
// Rise fills PlayerSnapshot::abnormalities (schema rows) and leaves the
// two World vectors empty. That is a real data difference, not the same
// data spelled twice, so the two arms are separate and the switch keeps a
// third game honest (-Wswitch, no default arm — the seam documented in
// the header, and the same one panel_player.cpp:162 uses for zone labels).
//
// Neither arm filters: the readers already publish only active entries.
// panel_player.cpp renders every row it is handed for these blocks (its
// Debuff/Buff section BITS gate the block, not individual rows), so a
// strip that filtered independently would disagree with the panel about
// what is active.
StatusStripModel buildStatusStripModel(const mhw::GameSnapshot &snap)
{
    StatusStripModel model;

    switch (snap.game) {
    case mhw::GameId::World: {
        model.debuffs.reserve(snap.player.debuffs.size());
        for (const auto &d : snap.player.debuffs) {
            model.debuffs.append(
                StatusPill{d.name, worldTimerText(d.timer), d.accent});
        }
        model.buffs.reserve(snap.player.buffs.size());
        for (const auto &b : snap.player.buffs) {
            model.buffs.append(
                StatusPill{b.name, worldTimerText(b.timer), b.accent});
        }
        break;
    }
    case mhw::GameId::Rise: {
        // Debuff block first (the ones a hunter has to react to), then
        // the buff block — the same order panel_player.cpp:665-677
        // builds its riseStatusRows in.
        for (int pass = 0; pass < 2; ++pass) {
            const bool wantDebuff = (pass == 0);
            QVector<StatusPill> &target =
                wantDebuff ? model.debuffs : model.buffs;
            for (const auto &a : snap.player.abnormalities) {
                if ((a.kind == mhw::AbnormalityKind::Debuff) != wantDebuff)
                    continue;
                target.append(StatusPill{
                    a.name,
                    riseAbnormalityTimerText(a.timer, a.isInfinite,
                                             a.isBuildup, a.maxTimer),
                    a.accent});
            }
        }
        break;
    }
    }

    return model;
}

} // namespace mhw

PlayerStatusStripPanel::PlayerStatusStripPanel(QWidget *parent)
    : Panel(QStringLiteral("player-status"), Corner::TopLeft, parent)
{
    // A strip is a pure fragment: HunterPie's AbnormalityTray draws no
    // panel title either, and the only thing the base class would add
    // that this window does not want is the v0.3 chrome (rounded body +
    // left accent stripe + border). Panel::setChromeVisible(false) —
    // landed by the parent agent in the same v0.12 step — suppresses all
    // of it: the base class's paintPanelChrome() returns without
    // touching the painter when the switch is off (panel.cpp:415-440),
    // so the window surface stays transparent apart from the pills' own
    // dark backgrounds. isSubWindow() (below) declares the other half of
    // the same fact for the slot registry.
    setChromeVisible(false);

    // Top-left anchored, beside PlayerPanel in the reading order the user
    // already has. Own settingsKey ("player-status"), so panels.ini holds
    // this panel's margins / scale / opacity separately from "player".
    //
    // v0.12: own window-title key too (ui.player_strip_title), distinct from
    // the ui.player_status_section label a nameless pill falls back to.
    // Reusing the label as the title would make the two resolve to the same
    // word in every locale ("状态"), and the title then stops saying which
    // window you are looking at. main.cpp's applyLocale re-sets the title
    // from the same key on a locale flip.
    setWindowTitle(mh::tr("ui.player_strip_title"));
}

void PlayerStatusStripPanel::setGameForDemo(mhw::GameId game)
{
    if (game_ == game)
        return;
    game_ = game;
    // Drop the one-shot demo seed so the next paint re-runs
    // setupDemoData() with the new game's flavour (World debuff/buff
    // vectors vs Rise abnormalities).
    resetDemoPrimed();
    triggerUpdate();
}

void PlayerStatusStripPanel::retranslateUi()
{
    // Cached strings first, exactly like PlayerPanel::retranslateUi() is
    // re-issued by main.cpp with the same key the panel's constructor
    // used — main.cpp's applyLocale does
    // `setWindowTitle(table.tr("ui.player_strip_title"))` before it calls
    // this, so the second set here is the deliberate redundancy every panel
    // carries: it keeps the answer correct if retranslateUi() is ever called
    // from anywhere else, and it costs one tr() lookup.
    setWindowTitle(mh::tr("ui.player_strip_title"));
    if (editMode())
        resetDemoPrimed();
    triggerUpdate();
}

void PlayerStatusStripPanel::update(const mhw::GameSnapshot &snap)
{
    // Same shape as PlayerPanel::update(): cache what paintPanel() needs,
    // schedule one repaint. The whole PlayerSnapshot is not needed here —
    // only the two blocks this panel is.
    //
    // The player-identity resolver (mhw::PlayerViewModel) is NOT used:
    // this panel shows no MR / name / weapon / partyCount, so pulling it
    // in would be dead weight rather than reuse.
    rows_ = mhw::buildStatusStripModel(snap);
    game_ = snap.game;

    // Demo rows are never live state (same rule PetDamagePanel applies):
    // the first authoritative snapshot replaces them outright, so an
    // edit-mode preview cannot survive into a real session.
    demoData_ = false;

    triggerUpdate();
}

bool PlayerStatusStripPanel::hasContent() const
{
    // Live mode: nothing to show means the window must not exist — the
    // main loop's hasVisibleContent() answers false and the layer-shell
    // surface is never mounted, which is HunterPie's first and third
    // layer at once (§6.3).
    //
    // Edit mode is the exception and it is deliberate: a design-mode
    // surface has to stay mounted and draggable, so once the demo is
    // seeded the answer is true even though the rows are synthetic — the
    // "window still there, content hidden" layer of the same mechanism.
    // demoData_ (not the base's demoPrimed_) is the flag, because it
    // says the rows ON SCREEN are synthetic, which is precisely what
    // needs the keep-alive.
    if (demoData_ && editMode())
        return true;
    return !rows_.isEmpty();
}

void PlayerStatusStripPanel::setupDemoData()
{
    // Edit-mode preview: enough capsules to exercise the wrap (a second
    // pill row) and both blocks, with labels from the data domain so the
    // preview follows the active locale. Same id-resolves-the-accent rule
    // PlayerPanel::setupDemoData() follows — no second colour table lives
    // in the UI.
    rows_ = {};
    const bool rise = (game_ == mhw::GameId::Rise);

    if (rise) {
        // Resolve the accent from the SAME schema table the reader reads
        // (riseFindAbnormality(id)->accent), so the preview and a live
        // session cannot disagree about a pill's family.
        const auto seed = [this](const char *id, const QString &name,
                                 float timer, float maxTimer,
                                 mhw::AbnormalityKind kind, bool infinite,
                                 bool buildup) {
            mhw::PlayerAbnormalitySnapshot a;
            a.id = QString::fromUtf8(id);
            a.name = name;
            a.timer = timer;
            a.maxTimer = maxTimer;
            a.kind = kind;
            a.isInfinite = infinite;
            a.isBuildup = buildup;
            if (const mhw::RiseAbnormalitySchema *schema =
                    mhw::riseFindAbnormality(id))
                a.accent = schema->accent;
            const QString text = mhw::riseAbnormalityTimerText(
                a.timer, a.isInfinite, a.isBuildup, a.maxTimer);
            (kind == mhw::AbnormalityKind::Debuff ? rows_.debuffs
                                                  : rows_.buffs)
                .append(mhw::StatusPill{a.name, text, a.accent});
        };
        using Kind = mhw::AbnormalityKind;
        seed("ABN_POISON", mh::tr("data.demo.abn.poison"), 23.0F, 0.0F,
             Kind::Debuff, false, false);
        seed("ABN_BLAST", mh::tr("data.demo.abn.blast"), 41.0F, 0.0F,
             Kind::Debuff, false, false);
        seed("ABN_FRENZY_BUILDUP", mh::tr("data.demo.abn.frenzy"), 43.0F,
             120.0F, Kind::Debuff, false, true);
        seed("ABN_BLEED", mh::tr("data.demo.abn.bleed"), 12.0F, 0.0F,
             Kind::Debuff, false, false);
        seed("ABN_DEMONDRUG", mh::tr("data.demo.abn.demon_drug"), 1.0F, 0.0F,
             Kind::Buff, true, false);
        seed("ABN_MIGHT_SEED", mh::tr("data.demo.abn.might_seed"), 142.0F,
             0.0F, Kind::Buff, false, false);
        seed("ABN_SPIRIBIRDS_CALL", mh::tr("data.demo.abn.spiribird_call"),
             58.0F, 60.0F, Kind::Buff, false, false);
        seed("ABN_BUTTERFLAME", mh::tr("data.demo.abn.butterflame"), 95.0F,
             0.0F, Kind::Buff, false, false);
    } else {
        // Resolve the accent through the same playerDebuffAccent() /
        // playerBuffAccent() lookups PlayerPanel::setupDemoData() uses,
        // so a demo pill's family is the family a live read would give.
        const auto seed = [this](bool debuff, int offset,
                                 const QString &name, float timer,
                                 mhw::AbnormalityAccent fallback =
                                     mhw::AbnormalityAccent::None) {
            mhw::AbnormalityAccent accent =
                debuff ? mhw::playerDebuffAccent(offset)
                       : mhw::playerBuffAccent(offset, 0, 0);
            if (accent == mhw::AbnormalityAccent::None)
                accent = fallback;
            (debuff ? rows_.debuffs : rows_.buffs)
                .append(mhw::StatusPill{name, worldTimerText(timer), accent});
        };
        seed(true, 0x5DC, mh::tr("data.demo.debuff.poison"), 12.0F);
        seed(true, 0x620, mh::tr("data.demo.debuff.blast"), 41.0F);
        // Synthetic rows: no World table entry exists for paralysis or
        // sleep, so state the family explicitly instead of forging an
        // offset that would later map to the wrong one.
        seed(true, -1, mh::tr("data.demo.debuff.paralysis"), 17.0F,
             mhw::AbnormalityAccent::Paralysis);
        seed(true, -1, mh::tr("data.demo.debuff.sleep"), 28.0F,
             mhw::AbnormalityAccent::Sleep);
        seed(true, 0x60C, mh::tr("data.demo.debuff.defense_down"), 60.0F);
        seed(false, 0x3C, mh::tr("data.demo.buff.attack_up"), 90.0F);
        seed(false, 0x6CC, mh::tr("data.demo.buff.demon_drug"), 300.0F);
        seed(false, 0x6D0, mh::tr("data.demo.buff.armor_skin"), 300.0F);
        seed(false, 0x690, mh::tr("data.demo.buff.dash_juice"), 45.0F);
    }

    demoData_ = true;
}

QVector<mhw::StatusPill> PlayerStatusStripPanel::debuffRows() const
{
    // The section mask is a separate axis from the master switch (see
    // Panel::setPanelEnabled). Reusing PlayerSection::Debuff / ::Buff
    // here means the console's existing player-section toggles drive this
    // panel's two blocks for free — a user who has already turned the
    // buff row off in the single panel gets the same layout in the strip,
    // with no new console UI.
    if (!(sectionMask() & mhw::PlayerSection::Debuff))
        return {};
    return rows_.debuffs;
}

QVector<mhw::StatusPill> PlayerStatusStripPanel::buffRows() const
{
    if (!(sectionMask() & mhw::PlayerSection::Buff))
        return {};
    return rows_.buffs;
}

int PlayerStatusStripPanel::contentWidth() const
{
    // Content width, not a chrome width: pick the widest capsule the
    // strip is about to draw and add the margin on both sides. `min`/`max`
    // clamp it so a single 1-char pill is still readable and a hunter with
    // twelve consumables does not produce a window wider than any screen
    // corner anchor can sensibly serve.
    int widest = 0;
    const auto measure = [&widest](const QVector<mhw::StatusPill> &block) {
        // The pill's own fonts, the same ones drawPill() sets — measuring
        // with a different font is how a strip ends up one elided glyph
        // short on every row.
        const QFont nameFont(QStringLiteral("Chakra Petch"), 9, QFont::Medium);
        const QFontMetrics nameMetrics(nameFont);
        const QFont timerFont(QStringLiteral("Chakra Petch"), 9);
        const QFontMetrics timerMetrics(timerFont);
        // The column stack drawPill() lays out inside one capsule:
        // stripe | 6 | name (55%) | ... | 6 | 6 | timer.
        for (const mhw::StatusPill &pill : block) {
            const int nameW = nameMetrics.horizontalAdvance(pill.name);
            const int timerW = timerMetrics.horizontalAdvance(pill.timer);
            const int pillW = 3 + 6 + nameW + 12 + timerW;
            widest = std::max(widest, std::max(pillW, kMinPillW));
        }
    };
    measure(debuffRows());
    measure(buffRows());

    if (widest == 0)
        return 2 * kMargin;
    return std::clamp(widest + 2 * kMargin, 2 * kMargin + kMinPillW,
                      kStripMaxW);
}

int PlayerStatusStripPanel::blockHeight(int count) const
{
    // The same formula panel_player.cpp:714-726 uses for its debuff and
    // buff blocks: rows * kPillH + (rows - 1) * kPillRowGap. Two height
    // formulas for one wrap is exactly how the v0.10.3 tenderize strip
    // went invisible (draw gate and reservation gate disagreeing), so
    // this is the strip's ONLY arithmetiser and contentWidth() runs in
    // the same paint, from the same rows.
    const int rows = mhw::statusStripRows(count);
    if (rows <= 0)
        return 0;
    return rows * kPillH + (rows - 1) * kPillRowGap;
}

void PlayerStatusStripPanel::paintPanel(QPainter &p)
{
    // ---- One gateset, three consumers: width, reservation, draw. ------
    const QVector<mhw::StatusPill> debuffs = debuffRows();
    const QVector<mhw::StatusPill> buffs = buffRows();
    const int debuffH = blockHeight(debuffs.size());
    const int buffH = blockHeight(buffs.size());

    // Nothing to show: a collapsed content size so an unmounted-but-alive
    // window never renders a leftover sliver. The live path never reaches
    // here — the main loop asks hasVisibleContent() first. This is the
    // edit-preview / defensive branch.
    if (debuffs.isEmpty() && buffs.isEmpty()) {
        setContentSize(2 * kMargin, 2 * kMargin);
        return;
    }

    const int contentH = debuffH
                       + ((!debuffs.isEmpty() && !buffs.isEmpty())
                              ? kBlockGap : 0)
                       + buffH;
    const int w = contentWidth();
    // setContentSize has an idempotent guard inside Panel, so calling it
    // per repaint (and therefore per drag frame) is free — that is the
    // guard panel.cpp:386-393 exists for.
    setContentSize(w, 2 * kMargin + contentH);

    // Chrome: NONE. drawV03Chrome() is deliberately not called — a status
    // strip is a fragment of the player panel, not a panel, and drawing
    // the rounded body + left accent stripe + border here would put a
    // panel frame around two rows of capsules. The base class already
    // made this switchable (Panel::setChromeVisible, set to false in the
    // ctor above); this panel takes the same route the four base panels
    // would take if they needed to, i.e. it simply never draws chrome
    // itself, so nothing here has to change when the split's remaining
    // sub-windows follow.
    //
    // NOTE ON THE BASE-CLASS CONTRACT: a chromeless subclass should call
    // Panel::paintPanelChrome(p, accent) rather than drawV03Chrome()
    // when it DOES want chrome, because paintPanelChrome() is the switch
    // that honours setChromeVisible(false). This panel wants none at all,
    // so it calls neither — which is why deleting the two
    // setChromeVisible/isSubWindow lines above is the only edit needed if
    // that base-class change is ever reverted.

    int y = kMargin;
    const int gridW = w - 2 * kMargin;

    if (!debuffs.isEmpty()) {
        drawPillGrid(p, y, debuffs.size(), mhw::kStatusStripPillsPerRow,
                     kPillRowGap, kPillGap, kPillH, kMargin, gridW,
                     [&debuffs](QPainter &p, int i, const QRectF &box) {
            const mhw::StatusPill &pill = debuffs[i];
            const QString name = pill.name.isEmpty() ? fallbackName(true)
                                                     : pill.name;
            drawPill(p, box, accentFor(pill.accent), name, pill.timer);
        });
        y += debuffH;
    }

    if (!buffs.isEmpty()) {
        if (!debuffs.isEmpty())
            y += kBlockGap;
        drawPillGrid(p, y, buffs.size(), mhw::kStatusStripPillsPerRow,
                     kPillRowGap, kPillGap, kPillH, kMargin, gridW,
                     [&buffs](QPainter &p, int i, const QRectF &box) {
            const mhw::StatusPill &pill = buffs[i];
            const QString name = pill.name.isEmpty() ? fallbackName(false)
                                                     : pill.name;
            QColor accent = accentFor(pill.accent);
            // Buff family default is green, not the debuff purple — see
            // the palette comment above. Only the unresolved case is
            // overridden; a resolved trait keeps PlayerPanel's mapping.
            if (pill.accent == mhw::AbnormalityAccent::None)
                accent = QColor(kBuffR, kBuffG, kBuffB);
            drawPill(p, box, accent, name, pill.timer);
        });
    }
}
