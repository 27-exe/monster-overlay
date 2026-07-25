#include "panel_damage.h"

#include "core/string_table.h"
#include "player/player_types.h"
#include "quest/quest_types.h"
#include "ui/formatters.h"
#include "ui/icon.h"

#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>

using mhw::Icon;

namespace {

constexpr int kPanelW = 320;
constexpr int kMargin = 10;
constexpr int kRowH = 22;
constexpr int kIconSize = 20;
constexpr int kChartH = 60;
constexpr int kMaxSamples = 900;
constexpr int kMaxPlayers = 4;
constexpr int kRowGap = 6;

// HunterPie default damage meter colors (PlayerConfigHelper.cs):
// slot 0 = #F24891 (pink), slot 1 = #50C5B7 (teal),
// slot 2 = #49CFF5 (sky blue), slot 3 = #FF8040 (orange).
// Self override = #A74FFF (purple) when isSelf.
const QColor kPlayerColors[] = {
    QColor(0xF2, 0x48, 0x91),  // slot 0 — pink
    QColor(0x50, 0xC5, 0xB7),  // slot 1 — teal
    QColor(0x49, 0xCF, 0xF5),  // slot 2 — sky blue
    QColor(0xFF, 0x80, 0x40),  // slot 3 — orange
};

} // namespace

namespace mh {
inline QString tr(const QString &key) { return mhw::StringTable::instance().tr(key); }
} // namespace mh

DamagePanel::DamagePanel(QWidget *parent)
    : Panel(QStringLiteral("dps"), Corner::BottomRight, parent)
{
    setWindowTitle(mh::tr("ui.damage_title"));
}

void DamagePanel::update(const mhw::GameSnapshot &snap)
{
    // Check for quest end: state changes from 2 (InQuest) to something else.
    const int qstate = snap.quest.state;
    if (qstate != 2 && !questEnded_) {
        questEnded_ = true;
        // Keep showing the last snapshot but don't record new samples.
        canvas()->update();
        return;
    }
    // Reset on new quest.
    if (qstate == 2 && questEnded_) {
        questEnded_ = false;
        history_.clear();
        tick_ = 0;
        firstHitTick_.clear();
        baselineDamage_.clear();
    }

    const bool wasEmpty = !hasData_;
    // During quest-end freeze, keep the frozen data visible — the
    // party will be empty (zone changed to non-hunting) but we
    // don't need fresh party data to render the last snapshot.
    // Clear only when back at lobby / ready screen (state 0 or 1).
    if (questEnded_) {
        if (qstate <= 1) {
            // Back at lobby / ready — wipe the frozen data.
            questEnded_ = false;
            hasData_ = false;
            history_.clear();
            tick_ = 0;
            firstHitTick_.clear();
            baselineDamage_.clear();
            canvas()->update();
            return;
        }
        // Still in settlement — keep frozen data, just repaint.
        canvas()->update();
        return;
    }

    hasData_ = !snap.party.isEmpty();
    if (!hasData_) {
        history_.clear();
        tick_ = 0;
        firstHitTick_.clear();
        baselineDamage_.clear();
        canvas()->update();
        return;
    }

    const int n = std::min(static_cast<int>(snap.party.size()), kMaxPlayers);
    names_.resize(n);
    weaponIds_.resize(n);
    masterRanks_.resize(n);
    slots_.resize(n);

    // Per-player first-hit tracking. Resize on party-size change.
    firstHitTick_.resize(n);
    baselineDamage_.resize(n);

    for (int i = 0; i < n; ++i) {
        names_[i] = snap.party[i].name;
        weaponIds_[i] = snap.party[i].weaponId;
        masterRanks_[i] = snap.party[i].masterRank;
        slots_[i] = snap.party[i].slot;

        // HunterPie: baseline captured when THIS player first deals damage.
        if (firstHitTick_[i] == 0 && snap.party[i].damage > 0) {
            firstHitTick_[i] = tick_;
            baselineDamage_[i] = snap.party[i].damage;
        }
    }

    // Record sample
    Sample s;
    s.tick = tick_++;
    s.damage.resize(n);
    for (int i = 0; i < n; ++i) {
        if (firstHitTick_[i] > 0)
            s.damage[i] = snap.party[i].damage - baselineDamage_[i];
        else
            s.damage[i] = 0;
    }
    history_.append(s);
    if (history_.size() > kMaxSamples)
        history_.removeFirst();

    canvas()->update();
}

void DamagePanel::paintPanel(QPainter &p)
{
    if (!hasData_)
        return;

    p.setPen(Qt::white);
    p.setFont(QFont(QStringLiteral("Work Sans"), 9));

    const int n = names_.size();
    const int rowsH = n * kRowH;
    const int totalH = kMargin + rowsH + kMargin + kChartH + kMargin + 16;
    setContentSize(kPanelW, totalH);

    int y = kMargin;

    // Player rows
    for (int i = 0; i < n; ++i) {
        const int dmg = history_.isEmpty() ? 0 : history_.last().damage.value(i, 0);

        // HunterPie DPS = total_damage / seconds_since_first_hit.
        // Each poll tick is 250 ms, so divide tick delta by 4 for seconds.
        int dps = 0;
        if (firstHitTick_[i] > 0 && tick_ > firstHitTick_[i]) {
            const int elapsedTicks = history_.last().tick - firstHitTick_[i];
            if (elapsedTicks > 0)
                dps = dmg * 4 / elapsedTicks;
        }

        // Weapon icon
        if (weaponIds_[i] >= 0) {
            const QPixmap wp = Icon::render(Icon::weaponPath(weaponIds_[i], 1), kIconSize);
            if (!wp.isNull())
                p.drawPixmap(kMargin, y + 1, wp);
        }

        // Name + MR
        p.setPen(kPlayerColors[(slots_.value(i, i) % kMaxPlayers + kMaxPlayers) % kMaxPlayers]);
        p.setFont(QFont(QStringLiteral("Work Sans"), 9, QFont::Bold));
        p.drawText(kMargin + kIconSize + 4, y + kRowH - 5,
                   QStringLiteral("%1  MR%2").arg(names_[i]).arg(masterRanks_[i]));

        // Damage + DPS (right-aligned)
        p.setPen(QColor(220, 220, 220));
        p.setFont(QFont(QStringLiteral("Work Sans"), 8));
        const QString dmgStr = questEnded_
            ? QStringLiteral("%1").arg(dmg)
            : QStringLiteral("%1  DPS %2").arg(dmg).arg(dps);
        p.drawText(QRectF(kMargin, y, kPanelW - 2 * kMargin, kRowH),
                   Qt::AlignRight | Qt::AlignVCenter, dmgStr);
        y += kRowH;
    }

    y += kMargin;

    // Line chart: cumulative damage over time
    const QRectF chartRect(kMargin + 30, y, kPanelW - 2 * kMargin - 30, kChartH);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 30, 160));
    p.drawRoundedRect(chartRect, 4, 4);

    if (history_.size() < 2) {
        p.setPen(QColor(150, 150, 150));
        p.setFont(QFont(QStringLiteral("Work Sans"), 8));
        p.drawText(chartRect, Qt::AlignCenter, QStringLiteral("等待数据..."));
        return;
    }

    // Compute Y range
    int maxDmg = 0;
    for (const auto &s : history_) {
        for (int i = 0; i < s.damage.size(); ++i)
            if (s.damage[i] > maxDmg) maxDmg = s.damage[i];
    }
    if (maxDmg == 0) maxDmg = 1;

    const int firstTick = history_.first().tick;
    const int lastTick = history_.last().tick;
    const int tickSpan = lastTick - firstTick;
    if (tickSpan <= 0) return;

    // Draw line per player
    for (int pi = 0; pi < n; ++pi) {
        p.setPen(QPen(kPlayerColors[pi % kMaxPlayers], 1.5));
        QPainterPath path;
        bool first = true;
        for (const auto &s : history_) {
            const float x = chartRect.x()
                + (float)(s.tick - firstTick) / (float)tickSpan * chartRect.width();
            const float d = s.damage.value(pi, 0);
            const float y = chartRect.bottom() - (d / (float)maxDmg) * chartRect.height();
            if (first) { path.moveTo(x, y); first = false; }
            else path.lineTo(x, y);
        }
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    // Legend label
    p.setPen(QColor(150, 150, 150));
    p.setFont(QFont(QStringLiteral("Work Sans"), 8));
    p.drawText(chartRect.adjusted(0, 4, 0, 0), Qt::AlignRight | Qt::AlignTop,
               QStringLiteral("%1").arg(maxDmg));
}

void DamagePanel::paintDemo(QPainter &p)
{
    p.setPen(QColor(120, 180, 255));
    p.setFont(QFont(QStringLiteral("Work Sans"), 10, QFont::Bold));
    p.drawText(kMargin, 18, QStringLiteral("伤害统计 (示例) — Damage"));

    const int demoPlayers = 3;
    const int totalH = kMargin + 16 + demoPlayers * kRowH + kMargin + kChartH + kMargin;
    setContentSize(kPanelW, totalH);

    int y = kMargin + 16 + kRowGap;

    const char *demoNames[] = {"A27exe  MR999", "队友B  MR500", "队友C  MR300"};
    const int demoDmg[] = {12840, 6420, 3210};

    for (int i = 0; i < demoPlayers; ++i) {
        p.setPen(kPlayerColors[(slots_.value(i, i) % kMaxPlayers + kMaxPlayers) % kMaxPlayers]);
        p.setFont(QFont(QStringLiteral("Work Sans"), 9, QFont::Bold));
        p.drawText(kMargin, y, QString::fromUtf8(demoNames[i]));

        p.setPen(QColor(220, 220, 220));
        p.setFont(QFont(QStringLiteral("Work Sans"), 8));
        p.drawText(QRectF(kMargin, y, kPanelW - 2 * kMargin, kRowH),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1  DPS %2").arg(demoDmg[i]).arg(demoDmg[i] / 10));
        y += kRowH;
    }
}