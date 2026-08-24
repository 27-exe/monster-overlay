#include "core/game_detector.h"
#include "core/game_snapshot.h"
#include "core/string_table.h"
#include "monster/monster_types.h"
#include "mhw_reader.h"
#include "rise/mhr_reader.h"
#include "rise/rise_damage_reader.h"
#include "ui/panel_damage.h"
#include "ui/panel_monster.h"
#include "ui/panel_player.h"
#include "monster/target_selector.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QResource>
#include <QTimer>
#include <algorithm>
#include <functional>

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
    // Force the Fusion style: the Kvantum style plugin (kvantum 1.1.8 +
    // Layan) auto-requests KWin blur-behind for every translucent top-level
    // via BlurHelper::update() (blurhelper.cpp:361 enableBlurBehind(win,true)).
    // With Fusion the plugin never loads -> no ext_background_effect_v1
    // set_blur_region request -> crisp see-through panels on Plasma 6.7.
    // Overlay is pure QPainter-drawn, so no visual change from losing Kvantum.
    app.setStyle(QStringLiteral("Fusion"));
    QApplication::setApplicationName(QStringLiteral("mhw-linux-overlay"));
    QApplication::setApplicationDisplayName(QStringLiteral("MHW Linux Overlay"));
    QApplication::setApplicationVersion(QStringLiteral("0.7.5"));
    QApplication::setOrganizationName(QStringLiteral("a27exe"));
    app.setQuitOnLastWindowClosed(true);

    // Register font families.
    //   Work Sans    — UI font (already shipped in qrc /fonts).
    //   Chakra Petch — display font from the HTML v8 design spec
    //                  (numerals / chart labels / titles).
    //
    // Two paths are tried in order for each font:
    //   1. Load from qrc resource via addApplicationFontFromData
    //      (binary self-contained — works without system fontconfig).
    //   2. Fall back to addApplicationFont on the qrc path.
    //      (older Qt platforms where FromData isn't supported).
    // If both fail, log a warning but continue — Qt will fall back to
    // a system font at draw time, so the app still works.
    auto tryLoad = [](const QString &qrcPath, const char *label) {
        // 1) Read bytes via QResource. QFile::open on a qrc path can
        //    fail with OpenError on Qt 6.11 if called too early in main;
        //    QResource::data() is the documented safe way to read
        //    embedded resource bytes regardless of init order.
        QResource res(qrcPath);
        if (res.isValid()) {
            const QByteArray bytes = QByteArray(reinterpret_cast<const char *>(res.data()),
                                                 static_cast<int>(res.size()));
            const int id = QFontDatabase::addApplicationFontFromData(bytes);
            if (id >= 0) {
                qInfo("Font %s loaded from qrc bytes (%lld B), id=%d, "
                      "families=%s",
                      label,
                      static_cast<long long>(bytes.size()), id,
                      qPrintable(QFontDatabase::applicationFontFamilies(id)
                                     .join(", ")));
                return;
            }
            qWarning("Font %s: FromData(%lld B) returned %d",
                     label,
                     static_cast<long long>(bytes.size()), id);
        } else {
            qWarning("Font %s: QResource(%s) invalid",
                     label, qPrintable(qrcPath));
        }
        // 2) Fallback to file-path API.
        const int id = QFontDatabase::addApplicationFont(qrcPath);
        if (id >= 0) {
            qInfo("Font %s loaded via addApplicationFont path, id=%d",
                  label, id);
            return;
        }
        qWarning("Font %s FAILED to load from %s — will rely on "
                 "system fontconfig",
                 label, qPrintable(qrcPath));
    };
    tryLoad(QStringLiteral(":/fonts/fonts/ChakraPetch-Regular.ttf"),
            "ChakraPetch-Regular");
    tryLoad(QStringLiteral(":/fonts/fonts/ChakraPetch-Medium.ttf"),
            "ChakraPetch-Medium");
    tryLoad(QStringLiteral(":/fonts/fonts/ChakraPetch-SemiBold.ttf"),
            "ChakraPetch-SemiBold");
    tryLoad(QStringLiteral(":/fonts/fonts/ChakraPetch-Bold.ttf"),
            "ChakraPetch-Bold");
    tryLoad(QStringLiteral(":/fonts/fonts/WorkSans.ttf"),  "WorkSans");
    tryLoad(QStringLiteral(":/fonts/fonts/WorkSans-Medium.ttf"), "WorkSans-Medium");
    tryLoad(QStringLiteral(":/fonts/fonts/WorkSans-SemiBold.ttf"), "WorkSans-SemiBold");
    tryLoad(QStringLiteral(":/fonts/fonts/WorkSans-Light.ttf"),   "WorkSans-Light");
    tryLoad(QStringLiteral(":/fonts/fonts/WorkSans-ExtraLight.ttf"), "WorkSans-ExtraLight");
    QApplication::setFont(QFont(QStringLiteral("Work Sans"), 10));

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
    // L1: per-panel section masks (hex 32-bit). Default = all sections
    // visible (0xFFFFFFFF). Lets the control console pass the user's
    // current toggle state as CLI flags without any IPC. Bit layout
    // matches mhw::{Player,Monster,Damage}Section in ui/panel_sections.h.
    QCommandLineOption maskPlayerOption(
        QStringLiteral("mask-player"),
        QStringLiteral("Player panel section mask (hex32)"),
        QStringLiteral("hex32"));
    QCommandLineOption maskMonsterOption(
        QStringLiteral("mask-monster"),
        QStringLiteral("Monster panel section mask (hex32)"),
        QStringLiteral("hex32"));
    QCommandLineOption maskDamageOption(
        QStringLiteral("mask-damage"),
        QStringLiteral("Damage panel section mask (hex32)"),
        QStringLiteral("hex32"));
    // Disable a whole panel. Independent of --mask-* — this hides the
    // layer-shell surface entirely (no chrome, no title row), while
    // the mask flags control which sub-blocks are rendered inside an
    // enabled panel. Used by the control console's "面板启用"
    // master toggle: when off, the panel simply never appears.
    QCommandLineOption noPlayerOption(
        QStringLiteral("no-player"),
        QStringLiteral("Disable the player panel entirely"));
    QCommandLineOption noMonsterOption(
        QStringLiteral("no-monster"),
        QStringLiteral("Disable the monster panel entirely"));
    QCommandLineOption noDamageOption(
        QStringLiteral("no-damage"),
        QStringLiteral("Disable the damage panel entirely"));
    // Target game selector. auto = scan /proc for a running Monster Hunter
    // process (see core/game_detector.h), world/rise = force a specific
    // reader. Lets the control console relaunch the overlay against the
    // game the user picked without any IPC.
    QCommandLineOption gameOption(
        QStringLiteral("game"),
        QStringLiteral("Target game: auto, world or rise (default auto)"),
        QStringLiteral("game"),
        QStringLiteral("auto"));

    parser.addOption(mapOption);
    parser.addOption(localeOption);
    parser.addOption(editOption);
    parser.addOption(pollOption);
    parser.addOption(maskPlayerOption);
    parser.addOption(maskMonsterOption);
    parser.addOption(maskDamageOption);
    parser.addOption(noPlayerOption);
    parser.addOption(noMonsterOption);
    parser.addOption(noDamageOption);
    parser.addOption(gameOption);
    parser.process(app);

    if (!mhw::StringTable::instance().load(
            parser.isSet(localeOption) ? parser.value(localeOption)
                                        : QStringLiteral("zh-CN"))) {
        qWarning() << "Failed to load UI strings; falling back to key names.";
    }

    const bool editMode = parser.isSet(editOption);
    const int pollMs = qBound(30, parser.value(pollOption).toInt(), 5000);
    // L1: parse hex section masks; unset = 0xFFFFFFFF (everything visible).
    auto parseMask = [&](const QCommandLineOption &opt) -> uint32_t {
        if (!parser.isSet(opt)) return 0xFFFFFFFFu;
        bool ok = false;
        const uint32_t v = parser.value(opt).toUInt(&ok, 16);
        if (!ok) {
            qWarning("Ignoring invalid --mask-* hex value '%s'",
                     qPrintable(parser.value(opt)));
            return 0xFFFFFFFFu;
        }
        return v;
    };
    const uint32_t maskPlayer  = parseMask(maskPlayerOption);
    const uint32_t maskMonster = parseMask(maskMonsterOption);
    const uint32_t maskDamage  = parseMask(maskDamageOption);

    // Resolve the target game. auto scans for a running process; an
    // explicit world/rise forces the matching reader. Default to World
    // when nothing is running so the overlay still starts and waits.
    mhw::GameId gameId = mhw::GameId::World;
    {
        const QString gameArg = parser.value(gameOption).toLower();
        if (gameArg == QStringLiteral("world")) {
            gameId = mhw::GameId::World;
        } else if (gameArg == QStringLiteral("rise")) {
            gameId = mhw::GameId::Rise;
        } else {
            if (gameArg != QStringLiteral("auto"))
                qWarning("Ignoring invalid --game value '%s'; using auto",
                         qPrintable(parser.value(gameOption)));
            const auto detected = mhw::detectGame();
            gameId = detected ? detected->game : mhw::GameId::World;
        }
    }
    const bool isRise = (gameId == mhw::GameId::Rise);
    // Window/app title reflects the active game: "MHW Overlay" vs
    // "MHR Overlay".
    QApplication::setApplicationDisplayName(
        isRise ? QStringLiteral("MHR Overlay") : QStringLiteral("MHW Overlay"));

    PlayerPanel playerPanel;
    MonsterPanel monsterPanel;
    DamagePanel damagePanel;

    playerPanel.setEditMode(editMode);
    monsterPanel.setEditMode(editMode);
    damagePanel.setEditMode(editMode);

    // L1: apply section masks BEFORE show() so the first paint reflects
    // them — no one-frame flash of "all visible" if a mask is restrictive.
    playerPanel.setSectionMask(maskPlayer);
    monsterPanel.setSectionMask(maskMonster);
    damagePanel.setSectionMask(maskDamage);
    // Master gate: maps to the control console's "面板启用"
    // toggle. When the flag is set, the panel does not show its
    // layer-shell surface at all — independent of the section mask
    // above. setVisible() in the live loop will be a no-op for the
    // disabled panel.
    playerPanel.setPanelEnabled(!parser.isSet(noPlayerOption));
    monsterPanel.setPanelEnabled(!parser.isSet(noMonsterOption));
    damagePanel.setPanelEnabled(!parser.isSet(noDamageOption));

    playerPanel.show();
    monsterPanel.show();
    damagePanel.show();

    // Reader factory: both readers emit the same GameSnapshot, so the UI
    // loop below stays game-agnostic. World keeps the existing map path
    // (HunterPie legacy map); Rise auto-selects the map matching the
    // running game version from the data directory, falling back to the
    // compile-time default when nothing validates.
    mhw::MhwReader worldReader(parser.value(mapOption));
    QString riseMapPath = mhw::MhrReader::findBestMap(
        QFileInfo(QString::fromUtf8(MHR_DEFAULT_MAP)).absolutePath());
    if (riseMapPath.isEmpty())
        riseMapPath = QString::fromUtf8(MHR_DEFAULT_MAP);
    mhw::MhrReader riseReader(riseMapPath);
    // Rise party damage comes from the REFramework Lua script writing
    // /tmp/mhr_damage.json (RiseDamageReader's default path). Only the
    // Rise branch below ever polls it; World ignores it entirely.
    mhw::RiseDamageReader riseDamageReader;
    std::function<mhw::GameSnapshot()> pollGame =
        isRise ? std::function<mhw::GameSnapshot()>([&riseReader] { return riseReader.poll(); })
               : std::function<mhw::GameSnapshot()>([&worldReader] { return worldReader.poll(); });
    std::uintptr_t displayedMonsterAddress = 0;
    QTimer timer;

    QObject::connect(&timer, &QTimer::timeout, [&] {
        // C1 (v0.7.5 audit): exception barrier around the entire tick.
        // Any unexpected throw from the read path (bad_alloc in a
        // readArray that slipped past its clamp, std::bad_variant_access
        // etc.) must NOT terminate the overlay process — degrade to a
        // skipped frame instead. readArray clamping is the first line
        // of defence; this is the last.
        mhw::GameSnapshot snap;
        try {
            snap = pollGame();
        } catch (const std::exception &e) {
            qWarning("poll tick skipped (exception): %s", e.what());
            return;
        } catch (...) {
            qWarning("poll tick skipped (unknown exception)");
            return;
        }
        const bool showAll = editMode;

        // Once edit-mode demo data is seeded, each panel keeps its
        // mock state internally and we only need a paint kick — the
        // real `reader.poll()` snapshot would clobber attached_ /
        // weaponId_ / party stats back to "not running" every tick.
        auto skipUpdate = [&](const Panel &p) {
            return editMode && p.demoPrimed();
        };

        if (snap.player.valid || showAll) {
            playerPanel.setVisible(true);
            if (skipUpdate(playerPanel)) {
                playerPanel.triggerUpdate();
            } else {
                playerPanel.update(snap);
            }
        } else {
            // v0.1 behavior: keep player panel visible with a
            // "not connected" placeholder.
            playerPanel.update(snap);
            playerPanel.setVisible(true);
        }

        monsterPanel.setMultiplayer(snap.isMultiplayer);
        if (!snap.monsters.isEmpty() || showAll) {
            monsterPanel.setVisible(true);
            if (skipUpdate(monsterPanel)) {
                monsterPanel.triggerUpdate();
            } else if (!snap.monsters.isEmpty()) {
                // HunterPie defaults to TargetMode.LockOn. In our single-panel
                // layout, keep the current live monster stable while unlocked;
                // never switch merely because another monster enraged.
                const int selected = mhw::selectMonsterTarget(
                    snap.monsters, displayedMonsterAddress);
                if (selected >= 0) {
                    displayedMonsterAddress = snap.monsters[selected].address;
                    monsterPanel.update(snap.monsters[selected]);
                }
            }
        } else {
            monsterPanel.setVisible(false);
        }

        if (isRise) {
            // Rise party damage arrives out-of-band via /tmp/mhr_damage.json.
            // Feeding it only on a successful parse keeps the placeholder
            // (riseMode_) on screen until real data shows up. The panel
            // always has content in Rise mode — placeholder or chart.
            if (riseDamageReader.update())
                damagePanel.updateRiseDamage(riseDamageReader.snapshot());
            damagePanel.setVisible(true);
            damagePanel.triggerUpdate();
        } else if (skipUpdate(damagePanel)) {
            damagePanel.triggerUpdate();
            damagePanel.setVisible(!snap.party.isEmpty() || showAll);
        } else {
            // Always deliver empty/non-hunting snapshots too. DamagePanel owns
            // the hunt lifecycle; skipping these updates leaves the previous
            // chart and DPS tick counter alive into the next quest.
            damagePanel.update(snap);
            damagePanel.setVisible(!snap.party.isEmpty() || showAll);
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
