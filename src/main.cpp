#include "core/game_detector.h"
#include "core/game_snapshot.h"
#include "core/locale_conf.h"
#include "core/locale_sync.h"
#include "core/map_paths.h"
#include "core/steam_game_locator.h"
#include "core/string_table.h"
#include "monster/monster_types.h"
#include "mhw_reader.h"
#include "rise/mhr_reader.h"
#include "rise/rise_damage_reader.h"
#include "rise/rise_damage_roster.h"
#include "ui/panel_damage.h"
#include "ui/panel_monster.h"
#include "ui/panel_pet_damage.h"
#include "ui/panel_player.h"
#include "ui/panel_sections.h"
#include "monster/target_selector.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QResource>
#include <QTimer>
#include <functional>
#include <array>

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
    QApplication::setApplicationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationDisplayName(QStringLiteral("Monster Overlay"));
    QApplication::setApplicationVersion(QStringLiteral(MONSTER_VERSION));
    // Match the console's identity so both applications share one settings
    // tree and no user-specific name leaks into the shipped binaries.
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
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
        QStringLiteral("Monster Overlay \u2014 Linux HUD overlay for Monster Hunter: World and Rise"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption mapOption(
        {QStringLiteral("m"), QStringLiteral("map")},
        QStringLiteral("Address-map file; overrides the runtime search order"),
        QStringLiteral("path"));
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
    QCommandLineOption maskPetsOption(
        QStringLiteral("mask-pets"),
        QStringLiteral("Pet damage panel section mask (hex32)"),
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
    QCommandLineOption noPetsOption(
        QStringLiteral("no-pets"),
        QStringLiteral("Disable the pet damage panel entirely"));
    // Target game selector. auto = scan /proc for a running Monster Hunter
    // process (see core/game_detector.h), world/rise = force a specific
    // reader. Lets the control console relaunch the overlay against the
    // game the user picked without any IPC.
    QCommandLineOption gameOption(
        QStringLiteral("game"),
        QStringLiteral("Target game: auto, world or rise (default auto)"),
        QStringLiteral("game"),
        QStringLiteral("auto"));
    // v0.8: per-panel screen selection. Value is a QScreen::name()
    // (on Niri that string is the wlr-output id, e.g. eDP-1, DP-2,
    // HDMI-A-1). Empty / unset = "follow OS primary". Independent
    // per panel so the user can split panels across monitors. The
    // control console passes these flags through verbatim when it
    // spawns monster-overlay.
    QCommandLineOption outputPlayerOption(
        QStringLiteral("output-player"),
        QStringLiteral("Player panel QScreen name (default: OS primary)"),
        QStringLiteral("name"));
    QCommandLineOption outputMonsterOption(
        QStringLiteral("output-monster"),
        QStringLiteral("Monster panel QScreen name (default: OS primary)"),
        QStringLiteral("name"));
    QCommandLineOption outputDamageOption(
        QStringLiteral("output-damage"),
        QStringLiteral("Damage panel QScreen name (default: OS primary)"),
        QStringLiteral("name"));
    QCommandLineOption outputPetsOption(
        QStringLiteral("output-pets"),
        QStringLiteral("Pet damage panel QScreen name (default: OS primary)"),
        QStringLiteral("name"));

    parser.addOption(mapOption);
    parser.addOption(localeOption);
    parser.addOption(editOption);
    parser.addOption(pollOption);
    parser.addOption(maskPlayerOption);
    parser.addOption(maskMonsterOption);
    parser.addOption(maskDamageOption);
    parser.addOption(maskPetsOption);
    parser.addOption(noPlayerOption);
    parser.addOption(noMonsterOption);
    parser.addOption(noDamageOption);
    parser.addOption(noPetsOption);
    parser.addOption(gameOption);
    parser.addOption(outputPlayerOption);
    parser.addOption(outputMonsterOption);
    parser.addOption(outputDamageOption);
    parser.addOption(outputPetsOption);
    parser.process(app);

    // i18n startup locale, priority: --locale > conf(locale= row) > system
    // locale (v0.9.2: zh* -> zh-CN, everything else -> en-US, so a first run
    // follows the desktop instead of always opening in zh-CN). The conf row
    // exists only for an explicit choice — the console's EN/CH chip or its
    // own --locale (core/locale_conf.h); the overlay only reads it here and
    // in the poll loop below.
    const QString confPath = mhw::localeConfPath();
    const QString startupLocale = mhw::resolveStartupLocale(parser.value(localeOption), confPath);
    if (!mhw::StringTable::instance().load(startupLocale)) {
        qWarning() << "Failed to load UI strings for" << startupLocale
                   << "; falling back to key names.";
    }
    // Prime the change detector AFTER the startup decision so the first
    // poll cannot undo --locale with a pre-existing conf value.
    mhw::LocaleSyncState localeSync = mhw::localeSyncInit(confPath);

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
    const uint32_t maskPets    = parseMask(maskPetsOption);

    mhw::RiseDamageDisplayOptions riseDamageDisplayOptions;
    riseDamageDisplayOptions.showOtherMembers =
        (maskDamage & mhw::DamageSection::OtherMembers) != 0u;
    riseDamageDisplayOptions.showLocalPets =
        (maskPets & mhw::PetDamageSection::LocalPets) != 0u;
    riseDamageDisplayOptions.showOtherPets =
        (maskPets & mhw::PetDamageSection::OtherPets) != 0u;

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
    // Window/app title names the product and the game it is reading.
    QApplication::setApplicationDisplayName(
        isRise ? QStringLiteral("Monster Overlay \u00b7 Rise")
               : QStringLiteral("Monster Overlay \u00b7 World"));

    PlayerPanel playerPanel;
    MonsterPanel monsterPanel;
    DamagePanel damagePanel;
    PetDamagePanel petDamagePanel;

    playerPanel.setEditMode(editMode);
    monsterPanel.setEditMode(editMode);
    damagePanel.setEditMode(editMode);
    petDamagePanel.setEditMode(editMode);

    // L1: apply section masks BEFORE show() so the first paint reflects
    // them — no one-frame flash of "all visible" if a mask is restrictive.
    playerPanel.setSectionMask(maskPlayer);
    monsterPanel.setSectionMask(maskMonster);
    damagePanel.setSectionMask(maskDamage);
    petDamagePanel.setSectionMask(maskPets);
    damagePanel.setRiseDisplayOptions(riseDamageDisplayOptions);
    petDamagePanel.setDisplayOptions(riseDamageDisplayOptions);
    // Master gate: maps to the control console's "面板启用"
    // toggle. When the flag is set, the panel does not show its
    // layer-shell surface at all — independent of the section mask
    // above. setVisible() in the live loop will be a no-op for the
    // disabled panel.
    playerPanel.setPanelEnabled(!parser.isSet(noPlayerOption));
    monsterPanel.setPanelEnabled(!parser.isSet(noMonsterOption));
    damagePanel.setPanelEnabled(!parser.isSet(noDamageOption));
    // v0.10.1: the pet damage surface is Rise-only. World never mounts it,
    // regardless of --mask-pets / --no-pets; the console hides the matching
    // controls in World mode for the same reason.
    petDamagePanel.setPanelEnabled(isRise && !parser.isSet(noPetsOption));


    // v0.8: CLI --output-* overrides whatever the persisted panels.ini
    // already has. Pass-through with persist=false (we don't write the
    // CLI value back — that lets the user override per-run without
    // polluting the saved config; the next console-driven save is what
    // commits the choice to disk).
    if (parser.isSet(outputPlayerOption))
        playerPanel.setOutputName(parser.value(outputPlayerOption), false);
    if (parser.isSet(outputMonsterOption))
        monsterPanel.setOutputName(parser.value(outputMonsterOption), false);
    if (parser.isSet(outputDamageOption))
        damagePanel.setOutputName(parser.value(outputDamageOption), false);
    if (parser.isSet(outputPetsOption))
        petDamagePanel.setOutputName(parser.value(outputPetsOption), false);

    playerPanel.show();
    monsterPanel.show();
    damagePanel.show();
    // Never flash an empty fourth frame at startup. In edit mode Rise can
    // show its demo immediately; live mode waits for a visible pet row.
    petDamagePanel.setVisible(isRise && editMode);

    // Reader factory: both readers emit the same GameSnapshot, so the UI
    // loop below stays game-agnostic.
    //
    // P0 (v0.9.1) release-path fix: the maps are resolved at RUNTIME.
    // Priority: explicit --map > <appdir>/data > $XDG_DATA_HOME and
    // $XDG_DATA_DIRS (+ /monster-overlay/data) > the development build-tree
    // fallback, which only resolves inside a checkout and is computed at
    // runtime — no machine-specific path is compiled into the binary.
    // Before this, a released binary used the build machine's absolute
    // source path and every other machine failed to open a map (empty HUD,
    // no data).
    const QString explicitMap =
        parser.isSet(mapOption) ? parser.value(mapOption) : QString();
    const QStringList dataDirs = mhw::defaultDataSearchDirs();
    const QStringList worldCandidates = mhw::worldMapCandidates(
        explicitMap, QStringLiteral("MonsterHunterWorld.421810.map"), dataDirs,
        mhw::developmentMapFallback(
            QStringLiteral("MonsterHunterWorld.421810.map")));
    const QString worldMapPath = mhw::MhwReader::selectLoadableMap(worldCandidates);
    mhw::MhwReader worldReader(worldMapPath);
    // Rise uses the same complete candidate list as monster-doctor. A running
    // game keeps the live compatibility probe; offline selection takes the
    // first map whose content loads rather than stopping at the first glob.
    const QStringList riseCandidates = mhw::riseMapCandidates(
        explicitMap, dataDirs,
        mhw::developmentMapFallback(
            QStringLiteral("MonsterHunterRise.16.0.2.0.map")));
    const QString riseMapPath = mhw::MhrReader::findBestMap(riseCandidates);
    mhw::MhrReader riseReader(riseMapPath);
    // REFramework 1.5.9.1 sandboxes Lua files under reframework/data and has
    // no rename primitive. The producer alternates two slots; read both and
    // select the greatest valid sequence. Keep /tmp as the compatibility
    // fallback for older producer builds.
    const QString riseInstallDir = mhw::findRiseInstallDir();
    const QString riseDataDir = riseInstallDir.isEmpty()
        ? QString()
        : QDir(riseInstallDir).filePath(QStringLiteral("reframework/data"));
    std::array<mhw::RiseDamageReader, 3> riseDamageReaders{
        mhw::RiseDamageReader(riseDataDir.isEmpty()
                                  ? QStringLiteral("/tmp/mhr_damage_a.json")
                                  : QDir(riseDataDir).filePath(
                                        QStringLiteral("mhr_damage_a.json"))),
        mhw::RiseDamageReader(riseDataDir.isEmpty()
                                  ? QStringLiteral("/tmp/mhr_damage_b.json")
                                  : QDir(riseDataDir).filePath(
                                        QStringLiteral("mhr_damage_b.json"))),
        mhw::RiseDamageReader(QStringLiteral("/tmp/mhr_damage.json")),
    };
    std::function<mhw::GameSnapshot()> pollGame =
        isRise ? std::function<mhw::GameSnapshot()>([&riseReader] { return riseReader.poll(); })
               : std::function<mhw::GameSnapshot()>([&worldReader] { return worldReader.poll(); });
    std::uintptr_t displayedMonsterAddress = 0;
    // P1 (v0.9.1): the reader's status string is the only explanation for
    // an empty panel (map missing/invalid, no game process, denied read).
    // Mirror changes to stderr so `--poll` runs and pasted logs carry the
    // reason instead of an unexplained "not connected".
    QString lastReaderStatus;
    mhw::RiseDamageReader::Error lastRiseDamageFeedError =
        mhw::RiseDamageReader::Error::None;
    QTimer timer;

    // i18n: runtime locale switch. The console is the only writer of the
    // `locale=` row in monster-overlay.conf; we poll the file from the tick
    // below, throttled to ~1 Hz (a locale flip is a once-per-session event,
    // so 1 s of latency is fine — see core/locale_sync.h). On a change:
    // reload the StringTable, re-set the window titles, let every panel
    // re-query its cached strings (Panel::retranslateUi), then repaint.
    // No process restart, no IPC.
    QElapsedTimer localePollClock;
    localePollClock.start();
    auto applyLocale = [&](const QString &next) {
        if (!mhw::StringTable::instance().load(next)) {
            qWarning("Locale '%s' failed to load; keeping '%s'",
                     qPrintable(next),
                     qPrintable(mhw::StringTable::instance().currentLocale()));
            return;
        }
        qInfo("UI locale -> %s", qPrintable(next));
        auto &table = mhw::StringTable::instance();
        playerPanel.setWindowTitle(table.tr(QStringLiteral("ui.player_title")));
        monsterPanel.setWindowTitle(table.tr(QStringLiteral("ui.monster_title")));
        damagePanel.setWindowTitle(table.tr(QStringLiteral("ui.damage_title")));
        petDamagePanel.setWindowTitle(table.tr(QStringLiteral("ui.pet_damage_title")));
        playerPanel.retranslateUi();
        monsterPanel.retranslateUi();
        damagePanel.retranslateUi();
        petDamagePanel.retranslateUi();
    };

    QObject::connect(&timer, &QTimer::timeout, [&] {
        // i18n: conf poll. Cheap no-op in the steady state (one mtime stat),
        // so it can sit in front of the exception barrier — a locale flip
        // must still land when the game read path is throwing.
        if (localePollClock.elapsed() >= 1000) {
            localePollClock.restart();
            const QString nextLocale = mhw::localeSyncPoll(
                localeSync, confPath, mhw::StringTable::instance().currentLocale());
            if (!nextLocale.isEmpty())
                applyLocale(nextLocale);
        }

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
        if (snap.status != lastReaderStatus) {
            qInfo("reader status: %s", qPrintable(snap.status));
            lastReaderStatus = snap.status;
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
            // Rise damage arrives out-of-band. Edit-mode panels own their demo
            // state once primed; live snapshots must not erase that preview.
            const bool keepDamageDemo = skipUpdate(damagePanel);
            const bool keepPetDemo = skipUpdate(petDamagePanel);
            if (!keepDamageDemo || !keepPetDemo) {
                const mhw::RiseDamageReader *newestReader = nullptr;
                // Report the most informative failure, not simply the first
                // one. A Missing feed is the least interesting verdict (a
                // producer that has not written yet looks like that), so it
                // starts at the front and any peer holding a real error —
                // TooLarge, Parse, Stale, Symlink ... — takes over. With two
                // equally-informative errors the primary slot A wins, which
                // is where the producer writes first.
                const mhw::RiseDamageReader *diagnosticReader =
                    &riseDamageReaders.front();
                for (auto &reader : riseDamageReaders) {
                    if (reader.update()) {
                        if (!newestReader
                            || reader.snapshot().sequence
                                   > newestReader->snapshot().sequence
                            || (reader.snapshot().sequence
                                    == newestReader->snapshot().sequence
                                && reader.snapshot().timestampMs
                                       > newestReader->snapshot().timestampMs)) {
                            newestReader = &reader;
                        }
                        continue;
                    }
                    // No reader succeeded so far: keep whichever failure is
                    // most informative. Missing is the least interesting
                    // (a producer that has not written yet looks exactly
                    // like a working one), so a peer holding a real error
                    // takes over from it; the first real error then sticks.
                    if (diagnosticReader->lastError()
                            == mhw::RiseDamageReader::Error::Missing
                        && reader.lastError()
                               != mhw::RiseDamageReader::Error::Missing) {
                        diagnosticReader = &reader;
                    }
                }

                if (newestReader) {
                    if (lastRiseDamageFeedError !=
                        mhw::RiseDamageReader::Error::None) {
                        qInfo("rise damage feed: available (%s)",
                              qPrintable(newestReader->path()));
                    }
                    lastRiseDamageFeedError =
                        mhw::RiseDamageReader::Error::None;
                    auto damageSnapshot = newestReader->snapshot();
                    mhw::enrichRiseDamageSnapshot(damageSnapshot, snap);
                    if (!keepDamageDemo)
                        damagePanel.updateRiseDamage(damageSnapshot);
                    if (!keepPetDemo)
                        petDamagePanel.updateRiseDamage(damageSnapshot);
                } else {
                    const auto feedError = diagnosticReader->lastError();
                    if (feedError != lastRiseDamageFeedError) {
                        const char *prefix =
                            feedError == mhw::RiseDamageReader::Error::Missing
                                ? "rise damage feed"
                                : "rise damage feed warning";
                        if (feedError == mhw::RiseDamageReader::Error::Missing) {
                            qInfo("%s: %s", prefix,
                                  qPrintable(diagnosticReader->lastErrorText()));
                        } else {
                            qWarning("%s: %s", prefix,
                                     qPrintable(diagnosticReader->lastErrorText()));
                        }
                        lastRiseDamageFeedError = feedError;
                    }
                    // Missing, malformed, or stale (>5 s) feed is an explicit
                    // lifecycle event. Deliver an invalid snapshot so live
                    // panels clear old quest data instead of keeping it forever.
                    const mhw::RiseDamageSnapshot feedLost;
                    if (!keepDamageDemo)
                        damagePanel.updateRiseDamage(feedLost);
                    if (!keepPetDemo)
                        petDamagePanel.updateRiseDamage(feedLost);
                }
            }
            // Rise 与 World 同口径：没有可用数据就不挂载面板。
            // 以前这里无条件 setVisible(true)，配合 DamagePanel 内部的
            // `hasContent() == hasData_ || riseMode_` 画一个「等待伤害数据」
            // 占位块。现在占位已删，改由面板自己回答“有没有东西可画”，
            // 主循环只做同一个判断。
            damagePanel.setVisible(damagePanel.panelEnabled()
                                   && damagePanel.hasVisibleContent());
            damagePanel.triggerUpdate();
            // Rise owns the companion surface. World still force-disables it
            // at construction and in its branch.
            petDamagePanel.setVisible(petDamagePanel.panelEnabled()
                                      && petDamagePanel.hasVisibleContent());
            petDamagePanel.triggerUpdate();
        } else if (skipUpdate(damagePanel)) {
            petDamagePanel.setVisible(false);
            damagePanel.triggerUpdate();
            damagePanel.setVisible(!snap.party.isEmpty() || showAll);
        } else {
            petDamagePanel.setVisible(false);
            // Always deliver empty/non-hunting snapshots too. DamagePanel owns
            // the hunt lifecycle; skipping these updates leaves the previous
            // chart and DPS tick counter alive into the next quest.
            const mhw::GameSnapshot damageSnap = [&] {
                mhw::GameSnapshot filtered = snap;
                filtered.party = mhw::visibleDamageParty(
                    snap.party, riseDamageDisplayOptions.showOtherMembers);
                return filtered;
            }();
            damagePanel.update(damageSnap);
            damagePanel.setVisible(!damageSnap.party.isEmpty() || showAll);
        }
    });
    timer.start(pollMs);  // consistent poll rate regardless of mode

    const int code = app.exec();

    if (editMode) {
        playerPanel.saveConfig();
        monsterPanel.saveConfig();
        damagePanel.saveConfig();
        petDamagePanel.saveConfig();
    }

    return code;
}
