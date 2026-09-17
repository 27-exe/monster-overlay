// Offscreen snapshot of MonsterPanel + DamagePanel in demo mode.
// Mirrors snap_player_demo.cpp so the block-table refactor of these two
// panels can be verified with a pixel diff (before vs after) without
// bringing the layer-shell live overlay on screen.
//
// Renders each panel into a fixed oversized buffer (content paints into
// the top-left; the rest stays transparent) so the output dimensions
// are deterministic and a straight ImageChops diff works.
//
// i18n: `--locale <code>` renders the demo under another locale
// (default zh-CN = the historical, pre-i18n output):
//
//   snap-all-demo <out_prefix> [--locale en-US]

#include "ui/panel_monster.h"
#include "ui/panel_damage.h"
#include "core/string_table.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QStringList>

namespace {

// Splits `--locale <code>` / `--locale=<code>` out of argv; everything
// else is positional. Returns the locale (default zh-CN).
QString parseArgs(const QStringList &args, QStringList &positional)
{
    QString locale = QStringLiteral("zh-CN");
    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args[i];
        if (a == QStringLiteral("--locale") && i + 1 < args.size()) {
            locale = args[++i];
        } else if (a.startsWith(QStringLiteral("--locale="))) {
            locale = a.mid(9);
        } else {
            positional << a;
        }
    }
    return locale;
}

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

    QStringList positional;
    const QString locale = parseArgs(QCoreApplication::arguments(), positional);
    if (!::mhw::StringTable::instance().load(locale))
        qWarning("failed to load %s strings; falling back to keys",
                 locale.toLocal8Bit().constData());

    if (positional.isEmpty()) {
        qCritical("usage: snap_all_demo <out_prefix> [--locale <code>]");
        return 2;
    }
    const QString prefix = positional[0];

    MonsterPanel monster;
    snapPanel(monster, prefix + QStringLiteral("_monster.png"));

    DamagePanel damage;
    snapPanel(damage, prefix + QStringLiteral("_damage.png"));

    qInfo("wrote %s_{monster,damage}.png [locale=%s]",
          prefix.toLocal8Bit().constData(), locale.toLocal8Bit().constData());
    return 0;
}
