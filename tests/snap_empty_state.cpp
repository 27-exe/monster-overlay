// Snapshot of MonsterPanel in empty (no-data) state.
// Verifies symptom7 fix: chrome background box + placeholder text both render.
//
// i18n: `--locale <code>` renders the placeholder under another locale
// (default zh-CN = the historical, pre-i18n output):
//
//   snap-empty-state <out.png> [--locale en-US]

#include "ui/panel_monster.h"
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
        qCritical("usage: snap_empty <out_path> [--locale <code>]");
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

    const QString outPath = positional[0];
    if (!img.save(outPath)) {
        qCritical("save failed");
        return 1;
    }
    qInfo("saved %s (%dx%d) [locale=%s]", outPath.toLocal8Bit().constData(),
          img.width(), img.height(), locale.toLocal8Bit().constData());
    return 0;
}
