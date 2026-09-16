// Snapshot of MonsterPanel in empty (no-data) state.
// Verifies symptom7 fix: chrome background box + placeholder text both render.
#include "ui/panel_monster.h"
#include "core/string_table.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QString>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (!::mhw::StringTable::instance().load(QStringLiteral("zh-CN")))
        qWarning("failed to load zh-CN strings; falling back to keys");
    if (argc < 2) {
        qCritical("usage: snap_empty <out_path>");
        return 2;
    }

    MonsterPanel panel;
    // No setEditMode → demoPrimed stays false → paintPanel sees hasData_=false.
    // Use setFixedSize to a known oversized buffer so we can capture the
    // full content even if the layer-shell desired-size dance happens.
    panel.setFixedSize(820, 800);
    panel.show();
    panel.repaint();

    QImage img(panel.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    panel.render(&p, QPoint(), panel.rect());
    p.end();

    if (!img.save(QString::fromLocal8Bit(argv[1]))) {
        qCritical("save failed");
        return 1;
    }
    qInfo("saved %s (%dx%d)", argv[1], img.width(), img.height());
    return 0;
}
