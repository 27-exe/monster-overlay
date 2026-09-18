// Offscreen snapshot of the PlayerPanel no-data states (v0.9.1 P1).
//
// Regression aid for the diagnostic gap that made the CachyOS reports
// unreadable: a reader that could not open its address map, one that found
// no game process, and one that was denied memory reads all painted the same
// empty panel. paintPanel() now draws the reader's status string, so this
// tool renders the three states side by side for visual inspection:
//
//   1. disconnected, status "waiting for MonsterHunterWorld.exe"
//   2. disconnected, status "read denied: EPERM"      (the Proton 11 case)
//   3. attached but no data resolved, status present
//
// Usage: snap-player-offline <out.png> [--locale zh-CN|en-US]
//
// Not a CTest: rendering is a visual aid, and it needs a real QPA platform
// (the panels are layer-shell windows). Build it explicitly:
//   cmake --build build --target snap-player-offline

#include "core/game_snapshot.h"
#include "core/string_table.h"
#include "ui/panel_player.h"

#include <QApplication>
#include <QImage>
#include <iterator>
#include <QPainter>
#include <QString>
#include <QStringList>

namespace {

QString parseLocale(const QStringList &args, QStringList &positional)
{
    QString locale = QStringLiteral("zh-CN");
    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args[i];
        if (a == QStringLiteral("--locale") && i + 1 < args.size())
            locale = args[++i];
        else if (a.startsWith(QStringLiteral("--locale=")))
            locale = a.mid(9);
        else
            positional << a;
    }
    return locale;
}

// Render one state and return the painted image.
QImage renderState(bool attached, const QString &status)
{
    mhw::GameSnapshot snap;
    snap.attached = attached;
    snap.status = status;
    snap.pid = 12345;

    PlayerPanel panel;
    panel.setFixedSize(760, 220);
    panel.show();
    panel.update(snap);
    panel.repaint();

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

    QStringList positional;
    const QString locale = parseLocale(QCoreApplication::arguments(), positional);
    if (!mhw::StringTable::instance().load(locale))
        qWarning("failed to load %s strings; falling back to keys",
                 locale.toLocal8Bit().constData());
    if (positional.isEmpty()) {
        qCritical("usage: snap-player-offline <out.png> [--locale <code>]");
        return 2;
    }

    const QString waiting = mhw::StringTable::instance().tr(
        QStringLiteral("ui.reader.waiting_exe")).arg(QStringLiteral("monsterhunterworld.exe"));
    const QString denied = mhw::StringTable::instance().tr(
        QStringLiteral("ui.reader.ptrace_denied"))
        .arg(12345)
        .arg(QStringLiteral("process_vm_readv PID 12345 @ 0x140000000: "
                            "Operation not permitted (1)"));
    const QString mapError = mhw::StringTable::instance().tr(
        QStringLiteral("ui.reader.address_table_open_failed"))
        .arg(QStringLiteral("/home/someone/else/data/MonsterHunterWorld.421810.map"),
             QStringLiteral("No such file or directory"));

    const struct { const char *label; bool attached; QString status; } states[] = {
        {"waiting-for-exe", false, waiting},
        {"read-denied", false, denied},
        {"map-missing", false, mapError},
        {"attached-no-data", true, denied},
    };

    QImage composite(780, static_cast<int>(std::size(states)) * 230 + 10,
                     QImage::Format_ARGB32_Premultiplied);
    composite.fill(QColor(24, 24, 28));
    QPainter cp(&composite);
    for (std::size_t i = 0; i < std::size(states); ++i) {
        const QImage img = renderState(states[i].attached, states[i].status);
        cp.drawImage(10, 10 + static_cast<int>(i) * 230, img);
        cp.setPen(QColor(200, 200, 200));
        cp.drawText(12, 4 + static_cast<int>(i) * 230, QString::fromLatin1(states[i].label));
    }
    cp.end();

    if (!composite.save(positional[0])) {
        qCritical("save failed");
        return 1;
    }
    qInfo("saved %s (attached=%d status rows rendered) [locale=%s]",
          positional[0].toLocal8Bit().constData(), int(true),
          locale.toLocal8Bit().constData());
    return 0;
}
