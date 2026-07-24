#include "core/game_snapshot.h"
#include "core/string_table.h"
#include "mhw_reader.h"
#include "ui/panel_damage.h"
#include "ui/panel_monster.h"
#include "ui/panel_player.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTimer>

#include <cstdio>

void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const char *prefix = "INFO";
    switch (type) {
    case QtDebugMsg:    prefix = "DEBUG"; break;
    case QtInfoMsg:     prefix = "INFO";  break;
    case QtWarningMsg:  prefix = "WARN";  break;
    case QtCriticalMsg: prefix = "CRIT";  break;
    case QtFatalMsg:    prefix = "FATAL"; break;
    }
    std::fprintf(stderr, "[%s] %s\n", prefix, qPrintable(msg));
    std::fflush(stderr);
}

int main(int argc, char **argv)
{
    qInstallMessageHandler(messageHandler);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("mhw-linux-overlay"));
    QApplication::setApplicationDisplayName(QStringLiteral("MHW Linux Overlay"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QApplication::setOrganizationName(QStringLiteral("a27exe"));
    app.setQuitOnLastWindowClosed(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Monster Hunter: World native Linux overlay (v0.2 panel UI)"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption mapOption(
        {QStringLiteral("m"), QStringLiteral("map")},
        QStringLiteral("HunterPie legacy map path"),
        QStringLiteral("path"),
        QString::fromUtf8(MHW_DEFAULT_MAP));
    QCommandLineOption localeOption(
        QStringLiteral("locale"),
        QStringLiteral("UI locale (e.g. zh-CN)"),
        QStringLiteral("code"));
    QCommandLineOption editOption(
        QStringLiteral("edit"),
        QStringLiteral("Enter edit mode: drag panels, scroll to scale, save on exit"));
    QCommandLineOption pollOption(
        QStringLiteral("poll"),
        QStringLiteral("Polling interval in ms"),
        QStringLiteral("ms"),
        QStringLiteral("250"));

    parser.addOption(mapOption);
    parser.addOption(localeOption);
    parser.addOption(editOption);
    parser.addOption(pollOption);
    parser.process(app);

    if (!mhw::StringTable::instance().load(
            parser.isSet(localeOption) ? parser.value(localeOption)
                                        : QStringLiteral("zh-CN"))) {
        qWarning() << "Failed to load UI strings; falling back to key names.";
    }

    const bool editMode = parser.isSet(editOption);
    const int pollMs = qBound(30, parser.value(pollOption).toInt(), 5000);

    PlayerPanel playerPanel;
    MonsterPanel monsterPanel;
    DamagePanel damagePanel;

    playerPanel.setEditMode(editMode);
    monsterPanel.setEditMode(editMode);
    damagePanel.setEditMode(editMode);

    playerPanel.show();
    monsterPanel.show();
    damagePanel.show();

    mhw::MhwReader reader(parser.value(mapOption));
    QTimer timer;

    QObject::connect(&timer, &QTimer::timeout, [&] {
        const mhw::GameSnapshot snap = reader.poll();

        // In edit mode, always show all panels so the user can
        // position them even without game data.
        const bool showAll = editMode;

        // Player panel: only meaningful when we have a live player.
        if (snap.player.valid || showAll) {
            playerPanel.setVisible(true);
            if (snap.player.valid) {
                playerPanel.update(snap.player);
                // Feed the local player's weapon id (lives in party data).
                for (const auto &member : snap.party) {
                    if (member.local) {
                        playerPanel.setWeaponId(member.weaponId);
                        break;
                    }
                }
            } else if (editMode) {
                // In edit mode without game data, repaint to commit
                // the layer-shell surface so margin changes apply.
                playerPanel.triggerUpdate();
            }
        } else {
            playerPanel.setVisible(false);
        }

        // Monster panel displays the first live large monster only (v0.2).
        // Multiple monsters are planned for a later version.
        if (!snap.monsters.isEmpty() || showAll) {
            monsterPanel.setVisible(true);
            if (!snap.monsters.isEmpty())
                monsterPanel.update(snap.monsters.first());
            else if (editMode)
                monsterPanel.triggerUpdate();
        } else {
            monsterPanel.setVisible(false);
        }

        // Damage panel only shown when party damage data exists.
        if (!snap.party.isEmpty() || showAll) {
            damagePanel.setVisible(true);
            if (!snap.party.isEmpty())
                damagePanel.update(snap);
            else if (editMode)
                damagePanel.triggerUpdate();
        } else {
            damagePanel.setVisible(false);
        }
    });
    timer.start(pollMs);  // consistent poll rate regardless of mode

    const int code = app.exec();

    if (editMode) {
        playerPanel.saveConfig();
        monsterPanel.saveConfig();
        damagePanel.saveConfig();
    }

    return code;
}
