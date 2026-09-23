// Offscreen probe for the v0.10.8 World multiplayer part-card layout.
//
// Renders MonsterPanel's .pgrid twice from the SAME synthetic World part
// data — once with setMultiplayer(false) and once with setMultiplayer(true)
// — so a pixel diff proves exactly three things and nothing else:
//
//   1. single-player output is byte-identical to the pre-change render
//      (no regression on the path almost every user sees);
//   2. the multiplayer cards lose the .mini gauge and the value row
//      (the redundancy the user reported), not the header/tag/tenderize;
//   3. the multiplayer cards do not leave a hole where the gauge was:
//      the panel is SHORTER because the reserved height matches the drawn
//      height, which is the v0.10.3 tenderize failure mode in reverse.
//
// World part data is synthetic but structurally faithful: a severable tail
// (health/maxHealth = sever layer), a breakable part (threshold-derived
// cumulative pair), a flinch-only part, and a severable part with an ACTIVE
// tenderize. The point is the layout arithmetic, not real memory values.
//
//   snap-world-mp <out_prefix>
//
// Prints the panel height for both states; the difference is the proof for
// point 3 above.

#include "ui/panel_monster.h"
#include "core/string_table.h"
#include "monster/monster_types.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {

using mhw::GameId;
using mhw::MonsterSnapshot;
using mhw::PartSnapshot;
using mhw::PartType;

// v0.10.8: renders the part grid from synthetic data instead of the demo
// seed. Edit mode is deliberately NOT enabled: MonsterPanel replays
// setupDemoData() on every retranslate/paint while editMode() is true, which
// would overwrite the snapshot pushed through update(). The panel therefore
// stays in live-render mode and only needs show() plus one update().
class ProbePanel : public MonsterPanel {
public:
    using MonsterPanel::MonsterPanel;
    void prime() { show(); }
};

MonsterSnapshot makeWorldMonster()
{
    MonsterSnapshot m;
    m.game      = GameId::World;
    m.id        = 65;
    m.internalName = QStringLiteral("Zinogre");
    m.maxHealth = 5000.0F;
    m.health    = 5000.0F;

    // Severable tail — on World health/maxHealth IS the sever layer.
    PartSnapshot tail;
    tail.index       = 1000;      // World severable key (1000 + s)
    tail.name        = QStringLiteral("Tail");
    tail.partType    = PartType::Severable;
    tail.isSeverable = true;
    tail.health      = 2750.0F;
    tail.maxHealth   = 2750.0F;
    tail.counter     = 1;
    m.parts.append(tail);

    // Breakable head — cumulative threshold pair (applyBreakable shape).
    PartSnapshot head;
    head.index       = -1;        // World normal key (-1 - slot)
    head.name        = QStringLiteral("Head");
    head.partType    = PartType::Breakable;
    head.isBreakable = true;
    head.health      = 1800.0F;
    head.maxHealth   = 3500.0F;
    head.flinch      = 900.0F;
    head.maxFlinch   = 1200.0F;
    m.parts.append(head);

    // Flinch-only leg.
    PartSnapshot leg;
    leg.index    = -2;
    leg.name     = QStringLiteral("Leg");
    leg.partType = PartType::Flinch;
    leg.flinch   = 400.0F;
    leg.maxFlinch = 600.0F;
    m.parts.append(leg);

    // Severable horn with an ACTIVE tenderize — the strip must survive the
    // multiplayer cut, because it is read from its own local table.
    PartSnapshot horn;
    horn.index                = 1001;
    horn.name                 = QStringLiteral("Horn");
    horn.partType             = PartType::Severable;
    horn.isSeverable          = true;
    horn.health               = 1500.0F;
    horn.maxHealth            = 2000.0F;
    horn.tenderizeDuration    = 24.0F;
    horn.tenderizeMaxDuration = 30.0F;
    m.parts.append(horn);

    return m;
}

QImage render(MonsterPanel &panel)
{
    QImage img(panel.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    panel.render(&p, QPoint(), panel.rect());
    p.end();
    return img;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Load the locale BEFORE constructing any panel: tr() is read at the
    // draw site and the panel caches its title/seed labels in its ctor, so
    // a late load leaves raw i18n keys painted into the snapshot.
    if (!::mhw::StringTable::instance().load(QStringLiteral("zh-CN")))
        std::printf("warning: zh-CN strings not loaded; keys will show raw\n");

    QStringList positional;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (!a.startsWith(QStringLiteral("-")))
            positional << a;
    }

    if (positional.isEmpty()) {
        std::printf("usage: snap-world-mp <out_prefix>\n");
        return 2;
    }
    const QString prefix = positional[0];

    const MonsterSnapshot world = makeWorldMonster();

    ProbePanel solo;
    solo.prime();
    solo.update(world);
    solo.setMultiplayer(false);
    QApplication::processEvents();
    solo.repaint();
    QApplication::processEvents();
    const QImage soloImg = render(solo);

    ProbePanel multi;
    multi.prime();
    multi.update(world);
    multi.setMultiplayer(true);
    QApplication::processEvents();
    multi.repaint();
    QApplication::processEvents();
    const QImage multiImg = render(multi);

    const QString soloPath  = prefix + QStringLiteral("_solo.png");
    const QString multiPath = prefix + QStringLiteral("_multi.png");
    if (!soloImg.save(soloPath))
        std::printf("save failed: %s\n", soloPath.toLocal8Bit().constData());
    if (!multiImg.save(multiPath))
        std::printf("save failed: %s\n", multiPath.toLocal8Bit().constData());

    std::printf("solo  panel %dx%d\n", soloImg.width(), soloImg.height());
    std::printf("multi panel %dx%d\n", multiImg.width(), multiImg.height());
    std::printf("height delta (solo - multi) = %d px\n",
                soloImg.height() - multiImg.height());
    return 0;
}
