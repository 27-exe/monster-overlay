#pragma once

#include "panel.h"
#include "rise/rise_damage_types.h"

#include <QString>
#include <QVector>

namespace mhw {

// Pure display model shared by the panel and its data-behaviour test.
struct PetDamageRow {
    RiseDamageActor actor;
    QString displayName;
    qint64 damage{0};
    double share{0.0};
};

[[nodiscard]] QVector<PetDamageRow>
buildPetDamageRows(const QVector<RiseDamageActor> &actors,
                   const RiseDamageDisplayOptions &options,
                   const QString &petNameFallback);

// A pet producer is an owner-level aggregate. Before roster metadata joins,
// displaySlot is -1; keep its colour stable via ownerEntityIndex, then adopt
// the owner's real party slot as soon as it becomes available.
[[nodiscard]] int petOwnerColorSlot(const RiseDamageActor &actor);

[[nodiscard]] qint64 totalPetDamage(const QVector<PetDamageRow> &rows);

} // namespace mhw

class PetDamagePanel : public Panel {
    Q_OBJECT
public:
    explicit PetDamagePanel(QWidget *parent = nullptr);
    void updateRiseDamage(const mhw::RiseDamageSnapshot &);
    void setDisplayOptions(const mhw::RiseDamageDisplayOptions &);
    void retranslateUi() override;

protected:
    void paintPanel(QPainter &) override;
    void setupDemoData() override;
    bool hasContent() const override;

private:
    void rebuildRows();

    QVector<mhw::RiseDamageActor> sourceActors_;
    QVector<mhw::PetDamageRow> rows_;
    mhw::RiseDamageDisplayOptions displayOptions_;
    int questEpoch_{0};
    bool hasQuestEpoch_{false};
    bool questActive_{false};
    bool demoData_{false};
};
