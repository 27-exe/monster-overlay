#include "ui/panel_pet_damage.h"

#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace {

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,      \
                         #condition);                                          \
            return false;                                                      \
        }                                                                      \
    } while (false)

mhw::RiseDamageActor actor(QString key, mhw::RiseDamageActorKind kind,
                           bool local, int ownerEntityIndex, int displaySlot,
                           qint64 total, QString name = {},
                           QString ownerName = {})
{
    mhw::RiseDamageActor value;
    value.key = std::move(key);
    value.kind = kind;
    value.local = local;
    value.ownerEntityIndex = ownerEntityIndex;
    value.displaySlot = displaySlot;
    value.total = total;
    value.name = std::move(name);
    value.ownerName = std::move(ownerName);
    return value;
}

bool near(double actual, double expected)
{
    return std::abs(actual - expected) < 1e-9;
}

bool filtersKindsAndOwnerScope()
{
    const QVector<mhw::RiseDamageActor> actors{
        actor(QStringLiteral("player"), mhw::RiseDamageActorKind::Player,
              true, 0, 0, 900),
        actor(QStringLiteral("companion"), mhw::RiseDamageActorKind::Companion,
              false, 1, 1, 800),
        actor(QStringLiteral("unknown"), mhw::RiseDamageActorKind::Unknown,
              false, 2, 2, 700),
        actor(QStringLiteral("local-pet"), mhw::RiseDamageActorKind::Pet,
              true, 3, 3, 60, QStringLiteral("Local")),
        actor(QStringLiteral("other-cat"), mhw::RiseDamageActorKind::Palico,
              false, 4, 1, 40, QStringLiteral("Cat")),
        actor(QStringLiteral("other-dog"), mhw::RiseDamageActorKind::Palamute,
              false, 5, 2, 20, QStringLiteral("Dog")),
    };

    mhw::RiseDamageDisplayOptions options;
    options.showLocalPets = true;
    options.showOtherPets = false;
    auto rows = mhw::buildPetDamageRows(actors, options,
                                        QStringLiteral("Buddy"));
    CHECK(rows.size() == 1);
    CHECK(rows[0].actor.key == QStringLiteral("local-pet"));
    CHECK(near(rows[0].share, 1.0));

    options.showLocalPets = false;
    options.showOtherPets = true;
    rows = mhw::buildPetDamageRows(actors, options,
                                   QStringLiteral("Buddy"));
    CHECK(rows.size() == 2);
    CHECK(rows[0].actor.key == QStringLiteral("other-cat"));
    CHECK(rows[1].actor.key == QStringLiteral("other-dog"));
    CHECK(near(rows[0].share, 2.0 / 3.0));
    CHECK(near(rows[1].share, 1.0 / 3.0));

    options.showLocalPets = false;
    options.showOtherPets = false;
    CHECK(mhw::buildPetDamageRows(actors, options,
                                  QStringLiteral("Buddy")).isEmpty());
    return true;
}

bool sortsDeterministicallyByOwnerThenSlotThenKey()
{
    QVector<mhw::RiseDamageActor> first{
        actor(QStringLiteral("remote"), mhw::RiseDamageActorKind::Palico,
              false, 0, 0, 1),
        actor(QStringLiteral("z"), mhw::RiseDamageActorKind::Pet,
              true, 4, 2, 1),
        actor(QStringLiteral("c"), mhw::RiseDamageActorKind::Palamute,
              true, 1, 3, 1),
        actor(QStringLiteral("b"), mhw::RiseDamageActorKind::Palico,
              true, 1, 2, 1),
        actor(QStringLiteral("a"), mhw::RiseDamageActorKind::Pet,
              true, 1, 2, 1),
    };
    QVector<mhw::RiseDamageActor> second = first;
    std::reverse(second.begin(), second.end());

    const auto rowsA = mhw::buildPetDamageRows(first, {},
                                               QStringLiteral("Buddy"));
    const auto rowsB = mhw::buildPetDamageRows(second, {},
                                               QStringLiteral("Buddy"));
    const QVector<QString> expected{
        QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
        QStringLiteral("z"), QStringLiteral("remote"),
    };

    CHECK(rowsA.size() == expected.size());
    CHECK(rowsB.size() == expected.size());
    for (qsizetype i = 0; i < expected.size(); ++i) {
        CHECK(rowsA[i].actor.key == expected[i]);
        CHECK(rowsB[i].actor.key == expected[i]);
    }
    return true;
}

bool computesVisiblePetTotalAndPercentages()
{
    const QVector<mhw::RiseDamageActor> actors{
        actor(QStringLiteral("twenty"), mhw::RiseDamageActorKind::Pet,
              true, 0, 0, 20),
        actor(QStringLiteral("thirty"), mhw::RiseDamageActorKind::Palico,
              true, 0, 1, 30),
        actor(QStringLiteral("fifty"), mhw::RiseDamageActorKind::Palamute,
              false, 1, 2, 50),
    };
    const auto rows = mhw::buildPetDamageRows(actors, {},
                                              QStringLiteral("Buddy"));
    CHECK(rows.size() == 3);
    CHECK(mhw::totalPetDamage(rows) == 100);
    CHECK(near(rows[0].share, 0.2));
    CHECK(near(rows[1].share, 0.3));
    CHECK(near(rows[2].share, 0.5));

    const QVector<mhw::RiseDamageActor> nonPositive{
        actor(QStringLiteral("negative"), mhw::RiseDamageActorKind::Pet,
              true, 0, 0, -10),
        actor(QStringLiteral("zero"), mhw::RiseDamageActorKind::Palico,
              false, 1, 1, 0),
    };
    const auto zeroRows = mhw::buildPetDamageRows(
        nonPositive, {}, QStringLiteral("Buddy"));
    CHECK(zeroRows.size() == 2);
    CHECK(mhw::totalPetDamage(zeroRows) == 0);
    CHECK(zeroRows[0].damage == 0);
    CHECK(zeroRows[1].damage == 0);
    CHECK(near(zeroRows[0].share, 0.0));
    CHECK(near(zeroRows[1].share, 0.0));
    return true;
}

bool usesOwnerAggregateNamesAndLocalizedFallbacks()
{
    QVector<mhw::RiseDamageActor> actors{
        actor(QStringLiteral("generic"), mhw::RiseDamageActorKind::Pet,
              true, 0, 0, 1, QStringLiteral("   "), QStringLiteral("Alice")),
        actor(QStringLiteral("cat"), mhw::RiseDamageActorKind::Palico,
              true, 0, 1, 1),
        actor(QStringLiteral("dog"), mhw::RiseDamageActorKind::Palamute,
              false, 1, 2, 1, QStringLiteral("Mochi"), QStringLiteral("Bob")),
    };
    for (int i = 0; i < 5; ++i) {
        actors.append(actor(QStringLiteral("extra-%1").arg(i),
                            mhw::RiseDamageActorKind::Pet,
                            false, 10 + i, i, i + 1,
                            QStringLiteral("Extra %1").arg(i)));
    }

    const auto englishRows = mhw::buildPetDamageRows(
        actors, {}, QStringLiteral("Buddy"));
    CHECK(englishRows.size() == 8);
    CHECK(englishRows[0].displayName == QStringLiteral("Alice · Buddy"));
    CHECK(englishRows[1].displayName == QStringLiteral("Buddy"));
    CHECK(englishRows[2].displayName == QStringLiteral("Mochi"));

    const auto chineseRows = mhw::buildPetDamageRows(
        actors, {}, QStringLiteral("伙伴"));
    CHECK(chineseRows.size() == 8);
    CHECK(chineseRows[0].displayName == QStringLiteral("Alice · 伙伴"));
    CHECK(chineseRows[1].displayName == QStringLiteral("伙伴"));
    CHECK(chineseRows[2].displayName == QStringLiteral("Mochi"));
    return true;
}

bool choosesOwnerColorSlotAcrossRosterJoin()
{
    auto pet = actor(QStringLiteral("owner-3"),
                     mhw::RiseDamageActorKind::Pet,
                     false, 3, -1, 1);
    CHECK(mhw::petOwnerColorSlot(pet) == 3);

    // Before the owner joins the roster, displaySlot is unavailable and the
    // stable owner entity maps the colour. Once roster metadata arrives, the
    // party slot must win so the pet matches its owner's DamagePanel colour.
    pet.displaySlot = 1;
    CHECK(mhw::petOwnerColorSlot(pet) == 1);
    return true;
}

} // namespace

int main()
{
    if (!filtersKindsAndOwnerScope())
        return 1;
    if (!sortsDeterministicallyByOwnerThenSlotThenKey())
        return 1;
    if (!computesVisiblePetTotalAndPercentages())
        return 1;
    if (!usesOwnerAggregateNamesAndLocalizedFallbacks())
        return 1;
    if (!choosesOwnerColorSlotAcrossRosterJoin())
        return 1;

    std::fprintf(stderr, "PASS pet damage data behavior\n");
    return 0;
}
