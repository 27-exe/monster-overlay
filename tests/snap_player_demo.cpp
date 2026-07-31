// Offscreen snapshot of PlayerPanel in edit/demo mode.
//
// Builds the same panel layout the live Qt app uses (panel_player.cpp)
// but in a single-shot QImage render so the result can be diffed
// without bringing the layer-shell live overlay onto the screen —
// layer-shell would steal keyboard focus and lock the user out of
// the desktop. See ~/.hermes/memory: "KDE/overlay 测试: layer-shell
// 接管屏幕会锁死他".

#include "ui/panel_player.h"
#include "core/string_table.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QStringList>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    if (!::mhw::StringTable::instance().load(QStringLiteral("zh-CN"))) {
        qWarning("failed to load zh-CN strings; falling back to keys");
    }

    if (argc < 2) {
        qCritical("usage: snap_player_demo <output.png>");
        return 2;
    }
    const QString outPath = QString::fromLocal8Bit(argv[1]);

    PlayerPanel panel;
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
    qInfo("wrote %s (%dx%d)", outPath.toLocal8Bit().constData(),
          img.width(), img.height());
    return 0;
}
