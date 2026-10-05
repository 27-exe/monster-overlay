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
#include "ui/panel_sizing.h"
// The split's shared spacing vocabulary. Included here rather than in
// the header because this .cpp is the only translation unit that owns
// the constants (the header exposes none of them), and every other
// split .cpp includes it through its own header.
#include "panel_player_split_layout.h"

#include "rise/mhr_abnormalities.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
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
// v0.12 promise — with ONE exception, and it is this file's whole reason
// for being touched at all: the split's shared rhythm.
//
// THE EXCEPTION, spelled out. Every other split window inherits
// `.op { padding: 11px }` from the panel it came from. This window does
// not: it has always drawn its own outer breathing room (6px, the value
// af969b2 shipped it with), because its chrome is off and the pills
// carry their own dark bodies so the window edge is not the pill edge.
// Seven windows with one inset is the requirement, so this window now
// reads kSplitMargin like the other six. That costs 10px of width and
// 10px of height on the way in and is the single largest driver of the
// new pixel baselines for the two split images.
//
// Width is NOT a token: this panel sizes itself to content (see
// PlayerStatusStripPanel::contentWidth), because a status strip has no
// title row or bar to align with and a fixed 378 would just be the old
// panel's width with dead air on the right.
constexpr int kMargin      = mhw::split_layout::kSplitMargin;  // outer inset
constexpr int kPillH       = 18;    // HTML .pill height (panel_player.cpp:49)
constexpr int kPillRowGap  = mhw::split_layout::kSplitGapWithin;  // between wrapped rows
constexpr int kPillGap     = mhw::split_layout::kSplitGapWithin;  // horizontal gap in a row
constexpr int kBlockGap    = mhw::split_layout::kSplitGapBetween; // debuff↔buff blocks
constexpr int kMinPillW    = 78;    // a one-glyph timer still readable

// The columns dial's range. 1 = a single capsule per row (the tall,
// narrowest option — every pill keeps its full measured width); 6 is
// where a 3-col strip's natural row stops fitting most screens' left
// half, and beyond that the elision does the user no favour anyway.
// Below 1 is not a layout (it is a divide-by-zero), above 6 the window
// is wider than the panel it was carved out of.
constexpr int kMinColumns = mhw::kStatusStripMinColumns;
constexpr int kMaxColumns = mhw::kStatusStripMaxColumns;

// Absolute ceiling on the strip's window width, logical px.
//
// Retuned from the historical 420, which was sized for a ONE-CAPSULE
// reservation: 420 - 2*6 = 408 was a single wide pill, and the 3-column
// draw then divided that by 3 — that mismatch is exactly the bug being
// fixed. Under grid maths the same 420 is not "a wide pill", it is a
// ceiling on a whole row of pills.
//
// Measured, not guessed. Across all 95 Rise schema rows in both
// locale columns, the natural pill width (3+6+name+12+timer, the
// widest timer included) runs 97..232. A 3-column row therefore wants
// 3*w + 2*5 + 12 = 313..663:
//
//     natural pill   3-col window
//     97  (p50)          313
//     166 (p90)          520
//     190 (p95)          592
//     232 (max)          663
//
// 560 covers up to a 180px pill at the default 3 columns — p90 and the
// demo content included — and pins the widest case (663) by eliding the
// NAME column, never the timer (see drawPill's interior layout). Past
// 560 the strip is half again wider than the PlayerPanel it was carved
// out of (378) and starts covering the panels it anchors beside; the
// honest escape hatch for that trade is the [ / ] columns dial, not a
// bigger ceiling. A 4-column row stays unclamped up to a 133px pill.
constexpr int kStripMaxW = 560;   // a fully buffed hunter stays sane

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
//
// The TWO COLUMNS ARE MEASURED, NOT FIXED. The historical version drew
// the name into `box.width() * 0.55` and the timer into
// `box.x()+4 .. box.width()-8`, both as fractions of the slot — which
// is a second, independent geometric model that agreed with nothing.
// At the 23px slot the bug produced they overlapped by design: the
// name's right edge was at 0.55*23 and the timer started at
// 23-8-24 (a 9pt "12s"). At any slot width the two fractions can also
// cross (55px slot: name ends at 39, timer starts at 27).
//
// So the interior is laid out the same way the window is: each column
// takes the width it needs, and the leftover goes to the one that
// should absorb it —
//
//     [3px stripe][6][ name ........ ][4][ timer ][4]
//                                          ^-- its exact measured width
//
// The timer is never elided (a truncated countdown is a wrong
// countdown) and the name is elided into whatever is left. That is the
// same guarantee contentWidth()/gridSlotWidth() gives the whole
// capsule: reserve from the measurement, draw inside the reservation,
// and the two cannot disagree.
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

    // Horizontal padding inside the capsule, after the stripe. Both are
    // part of the pill arithmetic contentWidth() measures
    // (3 + 6 + name + 12 + timer: 6 here, 12 = 4 gutter + 4 + 4).
    const int kStripeGap = 6;
    const int kEdgePad    = 4;
    const int kGutter     = 4;

    // The two columns, measured with the fonts they will be drawn in —
    // a third font metric here is how a strip ends up one elided glyph
    // short on every row.
    QFont tFont(QStringLiteral("Chakra Petch"), 9);
    tFont.setStyleStrategy(QFont::PreferAntialias);
    const QFontMetrics nameMetrics(bFont);
    const QFontMetrics timerMetrics(tFont);

    const qreal textLeft  = stripe.right() + kStripeGap;
    const qreal textRight = box.right() - kEdgePad;
    const qreal timerW    = timerMetrics.horizontalAdvance(timer);
    const qreal nameW     = std::max(0.0, textRight - timerW - kGutter
                                         - textLeft);

    const QRectF nameRect(textLeft, box.y(), nameW, box.height());
    p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
               nameMetrics.elidedText(name, Qt::ElideRight,
                                      static_cast<int>(nameW)));

    p.setFont(tFont);
    p.setPen(QColor(210, 214, 216));
    const QRectF timerRect(textRight - timerW, box.y(), timerW,
                           box.height());
    p.drawText(timerRect, Qt::AlignRight | Qt::AlignVCenter, timer);
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
// colIdx / itemsInRow / cx), so the strip's wrap is pixel-identical
// to the panel's. The caller keeps ownership of what a pill says.
//
// SLOT WIDTH COMES FROM panel_sizing.h, NOT FROM HERE. This function
// used to compute
//
//     slotW = (width - pillGap * (itemsInRow - 1)) / itemsInRow
//
// which — although the formula is right — is fed a window width that
// was NOT derived from `itemsInRow` capsules (contentWidth reserved the
// width of ONE capsule), so a full 3-wide row got a third of a capsule
// (23px in the shipped World strip) while the trailing incomplete row
// got nearly a whole one. One pill per strip was readable, the rest
// were clipped: the reserved width and the drawn slot disagreed about
// what the same number meant.
//
// mhw::gridSlotWidth() is the exact inverse of the gridContentWidth()
// the window is reserved with, so the two now agree by construction
// rather than by coincidence. It also guards what the inline division
// did not: on a zero/negative budget it answers 0 rather than a
// negative slot.
//
// THE TRAILING ROW FILLS. With 4 pills at 3-per-row the window is
// 3*w + 2*gap + 2*margin wide, so the fourth capsule measures the
// leftover (the whole window) instead of a third of it. That is the
// "content adapts its length and width to fill" behaviour the strip is
// for, and it is why the division below is by `itemsInRow` and not by
// `pillsPerRow`: a row that holds fewer capsules is not a full row, and
// stretching them to fill it is correct here.
void drawPillGrid(QPainter &p, int top, int count, int pillsPerRow,
                  int rowGap, int pillGap, int rowH, int left, int width,
                  const std::function<void(QPainter &, int, const QRectF &)>
                      &paint)
{
    if (count <= 0 || pillsPerRow <= 0 || width <= 0)
        return;

    for (int i = 0; i < count; ++i) {
        const int rowIdx     = i / pillsPerRow;
        const int colIdx     = i % pillsPerRow;
        const int itemsInRow = std::min(pillsPerRow,
                                        count - rowIdx * pillsPerRow);
        // Floor at the minimum readable capsule so a row that cannot
        // fit still renders (clipped at the right edge) instead of
        // vanishing: a pill cut in half beats no pill. kMinPillW is
        // 3+6+name+12+timer with the thinnest readable pieces, so it
        // always leaves room for the timer inside the new interior
        // layout in drawPill().
        const int slotW = std::max(
            mhw::panel_sizing::gridSlotWidth(width, itemsInRow, pillGap),
            kMinPillW);
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

void PlayerStatusStripPanel::setPillsPerRow(int perRow)
{
    // Clamp first: a 0 would make statusStripRows() divide by zero, and
    // anything above kStatusStripMaxColumns produces a window wider than
    // the panel this one was carved out of.
    const int clamped = std::clamp(perRow, kMinColumns, kMaxColumns);
    if (clamped == columns_)
        return;
    columns_ = clamped;
    // Repaint re-derives width and height from the new dial value, and
    // setContentSize()'s idempotent guard makes the redundant call free.
    triggerUpdate();
}

void PlayerStatusStripPanel::keyPressEvent(QKeyEvent *e)
{
    // The `[` `]` columns dial moved to the base class in v0.13
    // (Panel::columns / nudgeColumns / kColumnsStep), so the branch
    // that used to claim those two keys here is gone: Panel::
    // keyPressEvent routes them through supportsColumns(), which this
    // strip answers true for and every non-row panel answers false
    // for.
    //
    // This override remains ONLY to keep the delegate explicit and
    // documented. It claims nothing the base would not have claimed,
    // so every key — Space (minimize), Esc (quit), the arrow margin
    // nudge, the -/= size dials, and [ ] — behaves exactly as the
    // v0.12 passthrough contract promised.
    Panel::keyPressEvent(e);
}

void PlayerStatusStripPanel::nudgeColumns(int delta)
{
    // One step in the dial's [1, 6] range. Same clamp setPillsPerRow()
    // applies, reached from the base class's key dispatch rather than
    // from a per-class key handler.
    setPillsPerRow(columns_ + Panel::kColumnsStep * (delta > 0 ? 1 : -1));
}

void PlayerStatusStripPanel::saveConfig()
{
    // Written on the way out of edit mode only, same trigger as every
    // other sub-window's.
    //
    // The QSettings instance is rebuilt here rather than inherited:
    // Panel::settings() is a TU-local free function in panel.cpp over a
    // function-local static, so a subclass cannot reach it — and
    // duplicating it in a header was deliberately not done either. Two
    // QSettings on the same IniFormat/UserScope/org/app params address
    // the same file, and each flushes on destruction, so this writes the
    // same bytes the base class would have. (Panel::saveConfig() is
    // called first so the base keys are already on disk before this one
    // adds to them.)
    Panel::saveConfig();
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("monster-overlay"),
                QStringLiteral("panels"));
    s.setValue(QStringLiteral("columns"), columns_);
    s.sync();
}

void PlayerStatusStripPanel::loadConfig()
{
    // Persisted since v0.12, unpersisted for one release before it. A
    // section missing this key reads the same default the constructor
    // installs, which is what makes the first read of an older
    // panels.ini behave exactly as before.
    Panel::loadConfig();
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("monster-overlay"),
                QStringLiteral("panels"));
    setPillsPerRow(s.value(QStringLiteral("columns"),
                           mhw::kStatusStripPillsPerRow).toInt());
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
    // Content width, not a chrome width — but the content is a GRID, so
    // this reserves the natural width of one full row and adds the
    // margin on both sides:
    //
    //     gridW  = perRow * widest + (perRow - 1) * gap      [panel_sizing]
    //     width  = gridW + 2 * kMargin
    //
    // and gridW / gridSlotWidth are exact inverses, so the draw side
    // (drawPillGrid) recovers the very slot the widest capsule was
    // measured at instead of a fraction of it. That inversion is the
    // whole point: it is what makes the strip "adapt its length and
    // width to fill" — one capsule adapts to 1 element, two to 2, seven
    // to 3+3+1 — without the reservation and the draw ever having to
    // be re-derived twice.
    //
    // The two clamps both survive, with the meanings the strip needs:
    //   * a lone 1-char pill stays readable (kMinPillW floor)
    //   * a hunter with twelve consumables does not produce a window
    //     wider than any screen corner anchor can sensibly serve
    //     (kStripMaxW ceiling)
    const int perRow = columns_;
    int widest = 0;
    const auto measure = [&widest](const QVector<mhw::StatusPill> &block) {
        // The pill's own fonts, the same ones drawPill() sets — measuring
        // with a different font is how a strip ends up one elided glyph
        // short on every row.
        const QFont nameFont(QStringLiteral("Chakra Petch"), 9, QFont::Medium);
        const QFontMetrics nameMetrics(nameFont);
        const QFont timerFont(QStringLiteral("Chakra Petch"), 9);
        const QFontMetrics timerMetrics(timerFont);
        // The same column stack drawPill() lays out inside one capsule:
        // stripe | 6 | name ... | 4 | timer | 4 →  3 + 6 + name + 12 + timer.
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

    // The dial is clamped into [1, 6] by setPillsPerRow, so this is a
    // defensive arm: a 0-column grid has no width to reserve, and
    // answering kMinPillW keeps the pill area positive rather than
    // dividing by it below.
    if (perRow <= 0)
        return std::max(2 * kMargin, kMinPillW);

    // One full row of capsules, the widest of which wins. gridItemsPerRow
    // is *not* used here: the row is exactly `prefix` wide even for a
    // single capsule (the window must not shrink to a 2-col grid just
    // because only one pill is active), which is the opposite of what
    // that helper's "fit as many as possible" contract is for.
    const int gridW = mhw::panel_sizing::gridContentWidth(
        perRow, perRow, widest, kPillGap);
    return std::clamp(gridW + 2 * kMargin,
                      std::max(2 * kMargin, kMinPillW),
                      kStripMaxW);
}

int PlayerStatusStripPanel::contentHeight() const
{
    // Panel's polymorphic extent hook, composed exactly the way
    // paintPanel() composes it: both blocks, the between-block gap only
    // when there are two blocks, and the margin once on each side.
    //
    // Deliberately NOT a re-derivation. The strip's height is the one
    // place where a second formula for the same number would bite —
    // contentWidth, blockHeight and setContentSize all have to agree on
    // what is being measured, and the two-gaps-one-gap rule above is
    // stated once here rather than twice.
    //
    // kMargin here is the WINDOW's own local margin, which is the same 11
    // contentMargin() reports — they must not drift, but they are not the
    // same token: the margin is a property of this window's paint, while
    // contentMargin() is the base's polymorphic answer to the question.
    const QVector<mhw::StatusPill> debuffs = debuffRows();
    const QVector<mhw::StatusPill> buffs = buffRows();
    const int debuffH = blockHeight(debuffs.size());
    const int buffH = blockHeight(buffs.size());
    const int contentH = debuffH
                       + ((!debuffs.isEmpty() && !buffs.isEmpty())
                              ? kBlockGap : 0)
                       + buffH;
    return 2 * kMargin + contentH;
}

int PlayerStatusStripPanel::blockHeight(int count) const
{
    // The same formula panel_player.cpp:714-726 uses for its debuff and
    // buff blocks: rows * kPillH + (rows - 1) * kPillRowGap. Two height
    // formulas for one wrap is exactly how the v0.10.3 tenderize strip
    // went invisible (draw gate and reservation gate disagreeing), so
    // this is the strip's ONLY arithmetiser and contentWidth() runs in
    // the same paint, from the same rows.
    const int rows = mhw::statusStripRows(count, columns_);
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
        drawPillGrid(p, y, debuffs.size(), columns_,
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
        drawPillGrid(p, y, buffs.size(), columns_,
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
