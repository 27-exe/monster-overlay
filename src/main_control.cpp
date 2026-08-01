// Standalone launcher for the MHW overlay control console.
//
// Plain Qt window (NOT layer-shell), so it's safe to focus and use
// alongside the live overlay without stealing keyboard or mapping an
// extra Wayland overlay surface. Renders the three overlay panels
// off-screen (WA_DontShowOnScreen) using the same QPainter code the
// live overlay uses, with mock data seeded by setEditMode(true).
//
// No connection to a running mhw-overlay process yet — pure preview.

#include "ui/control_panel.h"
#include "core/string_table.h"

#include <QApplication>
#include <QDebug>
#include <QString>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mhw-control"));

    if (!::mhw::StringTable::instance().load(QStringLiteral("zh-CN")))
        qWarning("failed to load zh-CN strings; falling back to keys");

    ControlPanel cp;
    cp.show();
    app.processEvents();

    // Self-test mode: --snap <path> renders the whole window to a PNG and
    // exits. Lets the agent verify the layout without a real compositor
    // (run with QT_QPA_PLATFORM=offscreen) and without stealing the user's
    // desktop focus.
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--snap") && i + 1 < argc) {
            const QString path = QString::fromLocal8Bit(argv[i + 1]);
            const QPixmap grab = cp.grab();
            if (!grab.save(path)) {
                qCritical("snap save failed: %s", path.toLocal8Bit().constData());
                return 3;
            }
            qInfo("snapped %s (%dx%d)", path.toLocal8Bit().constData(),
                  grab.width(), grab.height());
            return 0;
        }
    }
    return app.exec();
}
