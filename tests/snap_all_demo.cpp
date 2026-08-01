// Offscreen snapshot of MonsterPanel + DamagePanel in demo mode.
// Mirrors snap_player_demo.cpp so the block-table refactor of these two
// panels can be verified with a pixel diff (before vs after) without
// bringing the layer-shell live overlay on screen.
//
// Renders each panel into a fixed oversized buffer (content paints into
// the top-left; the rest stays transparent) so the output dimensions
// are deterministic and a straight ImageChops diff works.

#include "ui/panel_monster.h"
#include "ui/panel_damage.h"
#include "core/string_table.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QString>

namespace {
void snapPanel(Panel &panel, const QString &path)
{
    panel.setEditMode(true);   // triggers setupDemoData() on first paint
    panel.setFixedSize(420, 1000);
    panel.show();
    panel.repaint();

    QImage img(panel.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    panel.render(&p, QPoint(), panel.rect());
    p.end();

    if (!img.save(path)) {
        qCritical("save failed: %s", path.toLocal8Bit().constData());
    }
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    if (!::mhw::StringTable::instance().load(QStringLiteral("zh-CN")))
        qWarning("failed to load zh-CN strings; falling back to keys");

    if (argc < 2) {
        qCritical("usage: snap_all_demo <out_prefix>");
        return 2;
    }
    const QString prefix = QString::fromLocal8Bit(argv[1]);

    MonsterPanel monster;
    snapPanel(monster, prefix + QStringLiteral("_monster.png"));

    DamagePanel damage;
    snapPanel(damage, prefix + QStringLiteral("_damage.png"));

    return 0;
}
