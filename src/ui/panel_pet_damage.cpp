#include "panel_pet_damage.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace {

QString petDisplayName(const mhw::RiseDamageActor &actor,
                       const QString &petNameFallback)
{
    const QString producerName = actor.name.trimmed();
    if (!producerName.isEmpty())
        return producerName;

    const QString ownerName = actor.ownerName.trimmed();
    if (!ownerName.isEmpty())
        return QStringLiteral("%1 · %2").arg(ownerName, petNameFallback);
    return petNameFallback;
}

bool petRowLess(const mhw::PetDamageRow &lhs, const mhw::PetDamageRow &rhs)
{
    const auto &a = lhs.actor;
    const auto &b = rhs.actor;

    // Required identity order: local owner first, then owner identity,
    // owner display slot, and the reader's stable actor key.
    if (a.local != b.local)
        return a.local;
    if (a.ownerEntityIndex != b.ownerEntityIndex)
        return a.ownerEntityIndex < b.ownerEntityIndex;
    if (a.displaySlot != b.displaySlot)
        return a.displaySlot < b.displaySlot;
    if (const int keyOrder = QString::compare(a.key, b.key, Qt::CaseSensitive);
        keyOrder != 0) {
        return keyOrder < 0;
    }

    // Keys are expected to be unique. These final tie-breakers keep malformed
    // duplicate-key input deterministic rather than falling back to QVector
    // iteration order.
    if (a.entityIndex != b.entityIndex)
        return a.entityIndex < b.entityIndex;
    if (a.kind != b.kind)
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    if (const int nameOrder = QString::compare(lhs.displayName, rhs.displayName,
                                               Qt::CaseSensitive);
        nameOrder != 0) {
        return nameOrder < 0;
    }
    if (lhs.damage != rhs.damage)
        return lhs.damage < rhs.damage;
    return QString::compare(a.ownerName, b.ownerName, Qt::CaseSensitive) < 0;
}

qint64 addDamageClamped(qint64 total, qint64 damage)
{
    const qint64 nonNegative = std::max<qint64>(0, damage);
    const qint64 maximum = std::numeric_limits<qint64>::max();
    if (nonNegative > maximum - total)
        return maximum;
    return total + nonNegative;
}

} // namespace

namespace mhw {

QVector<PetDamageRow>
buildPetDamageRows(const QVector<RiseDamageActor> &actors,
                   const RiseDamageDisplayOptions &options,
                   const QString &petNameFallback)
{
    QVector<PetDamageRow> rows;
    rows.reserve(actors.size());

    for (const RiseDamageActor &actor : actors) {
        if (!isRisePetKind(actor.kind)
            || !isRiseDamageActorVisible(actor, options)) {
            continue;
        }

        PetDamageRow row;
        row.actor = actor;
        row.displayName = petDisplayName(actor, petNameFallback);
        row.damage = std::max<qint64>(0, actor.total);

        rows.append(std::move(row));
    }

    std::sort(rows.begin(), rows.end(), petRowLess);

    // Producer keys preserve raw hook identity, but the UI contract is one
    // aggregate row per proven owner. Sorting first makes the representative
    // key/name deterministic; unknown owners (<0) remain separate and honest.
    QVector<PetDamageRow> merged;
    merged.reserve(rows.size());
    for (const PetDamageRow &row : std::as_const(rows)) {
        if (row.actor.ownerEntityIndex >= 0 && !merged.isEmpty()
            && merged.last().actor.ownerEntityIndex == row.actor.ownerEntityIndex) {
            PetDamageRow &existing = merged.last();
            existing.damage = addDamageClamped(existing.damage, row.damage);
            existing.actor.total = existing.damage;
            existing.actor.physical = addDamageClamped(
                existing.actor.physical, row.actor.physical);
            existing.actor.elemental = addDamageClamped(
                existing.actor.elemental, row.actor.elemental);
            const quint64 remaining = std::numeric_limits<quint64>::max()
                                      - existing.actor.hits;
            existing.actor.hits += std::min(remaining, row.actor.hits);
            continue;
        }
        merged.append(row);
    }
    rows = std::move(merged);

    long double shareTotal = 0.0L;
    for (const PetDamageRow &row : rows)
        shareTotal += static_cast<long double>(row.damage);
    if (shareTotal > 0.0L) {
        for (PetDamageRow &row : rows) {
            row.share = static_cast<double>(
                static_cast<long double>(row.damage) / shareTotal);
        }
    }

    return rows;
}

int petOwnerColorSlot(const RiseDamageActor &actor)
{
    return actor.displaySlot >= 0 ? actor.displaySlot
                                  : actor.ownerEntityIndex;
}

qint64 totalPetDamage(const QVector<PetDamageRow> &rows)
{
    qint64 total = 0;
    for (const PetDamageRow &row : rows)
        total = addDamageClamped(total, row.damage);
    return total;
}

} // namespace mhw

#if !defined(MHW_PET_DAMAGE_DATA_TEST)

#include "core/string_table.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QRectF>

#include <cmath>

namespace {

constexpr int kPanelW = 300;
constexpr int kMargin = 10;
constexpr int kTitleH = 16;
constexpr int kSummaryH = 18;
constexpr int kRowH = 27;
constexpr int kRowGap = 4;
constexpr int kTitleGap = 5;
constexpr int kRowsGap = 7;
constexpr int kDamageW = 70;
constexpr int kPercentW = 36;

// HunterPie/DamagePanel owner colours: self purple, otherwise party slot
// pink/teal/sky/orange. Before roster metadata supplies displaySlot, the
// producer's ownerEntityIndex maps to the same four-colour palette.
const QColor kOwnerColors[] = {
    QColor(0xF2, 0x48, 0x91),
    QColor(0x50, 0xC5, 0xB7),
    QColor(0x49, 0xCF, 0xF5),
    QColor(0xFF, 0x80, 0x40),
};

QColor colorForOwner(int ownerColorSlot, bool localOwner)
{
    if (localOwner)
        return QColor(0xA7, 0x4F, 0xFF);
    constexpr int count = static_cast<int>(std::size(kOwnerColors));
    const int index = (ownerColorSlot % count + count) % count;
    return kOwnerColors[index];
}

QString panelWindowTitle()
{
    return mhw::StringTable::instance().tr(
        QStringLiteral("ui.pet_damage_title"));
}

QString panelHeader()
{
    return mhw::StringTable::instance().tr(
        QStringLiteral("ui.pet_damage_header"));
}

QString totalLabel()
{
    return mhw::StringTable::instance().tr(
        QStringLiteral("ui.pet_damage_total"));
}

QString petNameFallback()
{
    return mhw::StringTable::instance().tr(
        QStringLiteral("ui.pet_name_fallback"));
}

} // namespace

PetDamagePanel::PetDamagePanel(QWidget *parent)
    : Panel(QStringLiteral("pet-damage"), Corner::BottomLeft, parent)
{
    setWindowTitle(panelWindowTitle());
    setContentSize(kPanelW,
                   kMargin + kTitleH + kTitleGap + kSummaryH + kMargin);
}

void PetDamagePanel::updateRiseDamage(const mhw::RiseDamageSnapshot &snapshot)
{
    const mhw::RiseDamageLifecycleAction action =
        mhw::riseDamageLifecycleAction(snapshot);
    if (action == mhw::RiseDamageLifecycleAction::Keep) {
        triggerUpdate();
        return;
    }

    // Demo rows are never live state. Any authoritative feed transition
    // replaces them; edit-mode main wiring avoids delivering feed snapshots
    // once demo data has been primed.
    if (demoData_) {
        sourceActors_.clear();
        rows_.clear();
        demoData_ = false;
    }

    if (action == mhw::RiseDamageLifecycleAction::Clear) {
        sourceActors_.clear();
        rows_.clear();
        hasQuestEpoch_ = false;
        questActive_ = false;
        triggerUpdate();
        return;
    }

    const bool epochChanged = hasQuestEpoch_
                           && snapshot.questEpoch != questEpoch_;
    if (epochChanged) {
        sourceActors_.clear();
        rows_.clear();
    }

    questEpoch_ = snapshot.questEpoch;
    hasQuestEpoch_ = true;

    if (action == mhw::RiseDamageLifecycleAction::Record) {
        // Live empty data deliberately clears the panel. Never cap this list:
        // four owner aggregates is the normal extreme, not a layout limit.
        sourceActors_ = snapshot.actors;
        rebuildRows();
        questActive_ = true;
    } else {
        // Result states 3..7 freeze the final active rows. Their actor arrays
        // may already be empty, so consuming them would erase final totals.
        questActive_ = false;
    }

    triggerUpdate();
}

void PetDamagePanel::setDisplayOptions(
    const mhw::RiseDamageDisplayOptions &options)
{
    if (displayOptions_.showLocalPets == options.showLocalPets
        && displayOptions_.showOtherMembers == options.showOtherMembers
        && displayOptions_.showOtherPets == options.showOtherPets) {
        return;
    }

    displayOptions_ = options;
    rebuildRows();
    triggerUpdate();
}

void PetDamagePanel::retranslateUi()
{
    setWindowTitle(panelWindowTitle());
    if (demoData_ && editMode())
        resetDemoPrimed();
    else
        rebuildRows();
    triggerUpdate();
}

void PetDamagePanel::rebuildRows()
{
    rows_ = mhw::buildPetDamageRows(sourceActors_, displayOptions_,
                                    petNameFallback());
}

bool PetDamagePanel::hasContent() const
{
    return !rows_.isEmpty();
}

void PetDamagePanel::setupDemoData()
{
    sourceActors_.clear();

    auto appendDemo = [this](QString key, QString ownerName, bool local,
                             int ownerIndex, int displaySlot, qint64 damage) {
        mhw::RiseDamageActor actor;
        actor.key = std::move(key);
        actor.kind = mhw::RiseDamageActorKind::Pet;
        actor.ownerName = std::move(ownerName);
        actor.local = local;
        actor.ownerEntityIndex = ownerIndex;
        actor.displaySlot = displaySlot;
        actor.total = damage;
        sourceActors_.append(std::move(actor));
    };

    // The REFramework producer follows HunterPie and emits one aggregate pet
    // actor per owner; it does not expose separate Palico/Palamute totals.
    appendDemo(QStringLiteral("demo-pet-owner-0"), QStringLiteral("A27exe"),
               true, 0, 0, 30820);
    appendDemo(QStringLiteral("demo-pet-owner-1"),
               mhw::StringTable::instance().tr(
                   QStringLiteral("ui.demo.party.a")),
               false, 1, 1, 7210);
    appendDemo(QStringLiteral("demo-pet-owner-2"),
               mhw::StringTable::instance().tr(
                   QStringLiteral("ui.demo.party.b_short")),
               false, 2, 2, 4960);
    appendDemo(QStringLiteral("demo-pet-owner-3"),
               mhw::StringTable::instance().tr(
                   QStringLiteral("ui.demo.party.c")),
               false, 3, 3, 3350);

    rebuildRows();
    demoData_ = true;
}

void PetDamagePanel::paintPanel(QPainter &p)
{
    // 与 DamagePanel / MonsterPanel 同一口径：没有行数据就什么都不画。
    // 主循环用 hasVisibleContent()（本类里 == !rows_.isEmpty()）决定是否
    // 挂载，所以这段代码在正常流程中本就到不了；早退只为编辑/预览路径
    // 直接调用 paintPanel 时不画出空框。
    //
    // 这里曾经有一个 36px 的「等待中」占位块（ui.pet_damage_waiting），
    // 但因为这个面板的 hasContent() 从来都是按 rows 判定，占位永远不会
    // 被画出来——是纯粹的死代码，已删除。
    if (rows_.isEmpty())
        return;

    const int rowCount = static_cast<int>(rows_.size());
    const int rowsHeight = rowCount * kRowH
                         + std::max(0, rowCount - 1) * kRowGap;
    const int totalHeight = kMargin + kTitleH + kTitleGap + kSummaryH
                          + kRowsGap + rowsHeight + kMargin;
    setContentSize(kPanelW, totalHeight);
    drawV03Chrome(p, Panel::Accent::Damage);

    QFont titleFont(QStringLiteral("Chakra Petch"), 9, QFont::Bold);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    p.setFont(titleFont);
    p.setPen(QColor(245, 246, 247));
    p.drawText(QRectF(kMargin, kMargin, kPanelW - 2 * kMargin, kTitleH),
               Qt::AlignLeft | Qt::AlignVCenter, panelHeader());

    int y = kMargin + kTitleH + kTitleGap;
    const QRectF summaryRect(kMargin, y, kPanelW - 2 * kMargin, kSummaryH);
    p.setFont(QFont(QStringLiteral("Chakra Petch"), 8));
    p.setPen(QColor(150, 153, 155));
    p.drawText(summaryRect, Qt::AlignLeft | Qt::AlignVCenter,
               totalLabel());
    p.setFont(QFont(QStringLiteral("Chakra Petch"), 10, QFont::DemiBold));
    p.setPen(QColor(245, 246, 247));
    p.drawText(summaryRect, Qt::AlignRight | Qt::AlignVCenter,
               QString::number(mhw::totalPetDamage(rows_)));

    y += kSummaryH + kRowsGap;
    for (const mhw::PetDamageRow &rowData : rows_) {
        const QRectF rowRect(kMargin, y, kPanelW - 2 * kMargin, kRowH);
        const QColor ownerColor = colorForOwner(
            mhw::petOwnerColorSlot(rowData.actor), rowData.actor.local);

        p.setPen(QPen(QColor(42, 45, 47), 1));
        p.setBrush(QColor(16, 18, 20, 225));
        p.drawRoundedRect(rowRect, 3, 3);

        // Owner colour marker, matching the existing DamagePanel semantics.
        p.setPen(Qt::NoPen);
        p.setBrush(ownerColor);
        p.drawRoundedRect(QRectF(rowRect.left() + 4, rowRect.top() + 5,
                                 4, rowRect.height() - 10), 2, 2);

        const QRectF shareBar(rowRect.left() + 11, rowRect.bottom() - 5,
                              rowRect.width() - 16, 3);
        drawBarV03(p, shareBar, static_cast<float>(rowData.share),
                   ownerColor, 1);

        const int textLeft = static_cast<int>(rowRect.left()) + 13;
        const int textRight = static_cast<int>(rowRect.right()) - 5;
        const int percentLeft = textRight - kPercentW;
        const int damageLeft = percentLeft - kDamageW;
        const int nameWidth = std::max(0, damageLeft - textLeft - 5);
        const QRectF textBand(textLeft, rowRect.top() + 1,
                              rowRect.width() - 18, rowRect.height() - 7);

        QFont nameFont(QStringLiteral("Chakra Petch"), 9, QFont::DemiBold);
        p.setFont(nameFont);
        p.setPen(QColor(230, 232, 234));
        const QFontMetrics nameMetrics(nameFont);
        const QString shownName = nameMetrics.elidedText(
            rowData.displayName, Qt::ElideRight, nameWidth);
        p.drawText(QRectF(textLeft, textBand.top(), nameWidth,
                          textBand.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, shownName);

        QFont valueFont(QStringLiteral("Chakra Petch"), 9, QFont::DemiBold);
        p.setFont(valueFont);
        p.setPen(QColor(245, 246, 247));
        p.drawText(QRectF(damageLeft, textBand.top(), kDamageW,
                          textBand.height()),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(rowData.damage));

        const int roundedPercent = static_cast<int>(
            std::lround(rowData.share * 100.0));
        p.setFont(QFont(QStringLiteral("Chakra Petch"), 8));
        p.setPen(ownerColor.lighter(120));
        p.drawText(QRectF(percentLeft, textBand.top(), kPercentW,
                          textBand.height()),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1%").arg(roundedPercent));

        y += kRowH + kRowGap;
    }
}

#endif // !MHW_PET_DAMAGE_DATA_TEST
