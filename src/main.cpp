#include "overlay_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QProcess>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("mhw-linux-overlay"));
    QApplication::setApplicationDisplayName(QStringLiteral("MHW Linux Overlay"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("a27exe"));
    app.setQuitOnLastWindowClosed(true);

    QStringList parserArguments = QCoreApplication::arguments();
    QStringList launchCommand;
    const qsizetype launchIndex = parserArguments.indexOf(QStringLiteral("--launch"));
    if (launchIndex >= 0) {
        launchCommand = parserArguments.mid(launchIndex + 1);
        parserArguments = parserArguments.mid(0, launchIndex);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Monster Hunter: World native Linux overlay prototype"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption mapOption({QStringLiteral("m"), QStringLiteral("map")},
                                 QStringLiteral("HunterPie legacy map path"),
                                 QStringLiteral("path"),
                                 QString::fromUtf8(MHW_DEFAULT_MAP));
    QCommandLineOption demoOption(QStringLiteral("demo"), QStringLiteral("Render mock data without reading MHW"));
    parser.addOption(mapOption);
    parser.addOption(demoOption);
    parser.process(parserArguments);

    if (launchIndex >= 0 && launchCommand.isEmpty())
        parser.showHelp(2);

    OverlayWindow window(parser.value(mapOption), parser.isSet(demoOption));
    window.show();

    QProcess child;
    if (!launchCommand.isEmpty()) {
        child.setProcessChannelMode(QProcess::ForwardedChannels);
        child.setWorkingDirectory(QDir::currentPath());
        QObject::connect(&child, &QProcess::errorOccurred, &app, [&child](QProcess::ProcessError) {
            qCritical().noquote() << QStringLiteral("启动游戏命令失败：%1").arg(child.errorString());
            QCoreApplication::exit(3);
        });
        QObject::connect(&child,
                         qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                         &app,
                         [](int exitCode, QProcess::ExitStatus) { QCoreApplication::exit(exitCode); });
        child.start(launchCommand.first(), launchCommand.mid(1));
    }

    return app.exec();
}
