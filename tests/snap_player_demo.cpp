// Offscreen snapshot of PlayerPanel in edit/demo mode.
//
// Builds the same panel layout the live Qt app uses (panel_player.cpp)
// but in a single-shot QImage render so the result can be diffed
// without bringing the layer-shell live overlay onto the screen —
// layer-shell would steal keyboard focus and lock the user out of
// the desktop. See ~/.hermes/memory: "KDE/overlay 测试: layer-shell
// 接管屏幕会锁死他".
//
// i18n: `--locale <code>` renders the same demo under another locale
// (default zh-CN keeps the pre-i18n behaviour byte-identical), which is
// how the zh/en snapshot pairs are produced. Positional arguments keep
// their original meaning:
//
//   snap-player-demo <out.png> [world|rise] [--locale en-US]

#include "ui/panel_player.h"
#include "core/string_table.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QStringList>

namespace {

// Splits `--locale <code>` / `--locale=<code>` out of argv; everything
// else is positional and keeps its historical order. Returns the locale
// (default zh-CN) and fills `positional`.
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

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QStringList positional;
    const QString locale = parseArgs(QCoreApplication::arguments(), positional);
    if (!::mhw::StringTable::instance().load(locale)) {
        qWarning("failed to load %s strings; falling back to keys",
                 locale.toLocal8Bit().constData());
    }

    if (positional.isEmpty()) {
        qCritical("usage: snap_player_demo <output.png> [world|rise] [--locale <code>]");
        return 2;
    }
    const QString outPath = positional[0];
    // v0.8.4-r18 player-abnormalities: the optional second argument selects
    // the previewed game so the Rise 「状态」 block (abnormalities) can be
    // snapshot without the control panel's rail. Defaults to World, the
    // historical behaviour of this tool.
    const QString gameArg = positional.size() > 1 ? positional[1].toLower()
                                                  : QStringLiteral("world");
    const bool rise = gameArg == QStringLiteral("rise");

    PlayerPanel panel;
    panel.setGameForDemo(rise ? mhw::GameId::Rise : mhw::GameId::World);
    panel.setEditMode(true);     // triggers setupDemoData() on first paint
    // Set fixed size to the same geometry HTML v8 uses (.op width:378,
    // plus padding). The actual height is computed inside paintPanel
    // via setContentSize(); we preallocate a tall buffer so render()
    // doesn't get clipped before the first paint runs.
    panel.setFixedSize(420, 720);
    panel.show();
    panel.repaint();

    // After the first paint, setContentSize() has adjusted the widget
    // to its natural size (e.g. 378 x 360). Render that size so we
    // capture the full panel, not just the window we allocated.
    const QSize natural = panel.size();
    qInfo("natural size = %dx%d", natural.width(), natural.height());

    QImage img(natural, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    panel.render(&p, QPoint(), panel.rect());
    p.end();

    if (!img.save(outPath)) {
        qCritical("save failed: %s", outPath.toLocal8Bit().constData());
        return 3;
    }
    qInfo("wrote %s (%dx%d) [locale=%s]", outPath.toLocal8Bit().constData(),
          img.width(), img.height(), locale.toLocal8Bit().constData());
    return 0;
}
