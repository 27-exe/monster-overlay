#include "core/game_detector.h"
#include "core/game_capabilities.h"
#include "core/game_profile.h"
#include "core/game_snapshot.h"
#include "core/locale_conf.h"
#include "core/locale_sync.h"
#include "core/map_paths.h"
// v0.12: read the conf mask rows once at startup instead of deriving four
// masks from four separate file reads.
#include "ui/viewmodel/panel_mask_fallback.h"
#include "core/steam_game_locator.h"
#include "core/string_table.h"
#include "monster/monster_types.h"
#include "rise/rise_install_locator.h"
#include "world/world_reader.h"
#include "rise/mhr_reader.h"
#include "rise/rise_damage_reader.h"
#include "rise/rise_damage_roster.h"
#include "ui/panel_damage.h"
#include "ui/panel_monster.h"
#include "ui/panel_pet_damage.h"
#include "ui/panel_player.h"
#include "ui/panel_player_split.h"
#include "ui/panel_settings_game.h"
#include "ui/viewmodel/console_layout_mode_codec.h"
// v0.12: the split's other three windows — HP/ST bars, mantles and
// wirebug. Same includes shape as the pilot strip above them, and the
// same reason for all four: the windows are declared in the header
// (panel_sections.h's kPlayerSplitTargets) but constructed here, so this
// is the one place that knows all four names.
#include "ui/panel_player_split_bars.h"
#include "ui/panel_player_split_mantles.h"
#include "ui/panel_player_split_wirebug.h"
// v0.12: the weapon identity trio (icon + name + MR). Same shape as the
// four above and the same reason — the window is declared by
// kPlayerSplitTargets (its row claims PlayerSection::Weapon) but
// constructed here, so this is the one place that knows all five names.
// The sharpness gauge does NOT travel with it: its table row deliberately
// leaves PlayerSection::Sharpness behind, and panel_player.cpp's prow
// block reads that bit on its own.
#include "ui/panel_player_split_weapon.h"
// v0.12 third wave: the monster panel's blocks and the damage panel's two
// width-hungry blocks. Same reason again — each is declared by its split
// table row in panel_sections.h but constructed here, so this file is the
// one place that knows all the new names.
//
// v0.13: the monster's five are now TWO. The user asked for the
// grouping 「图标，激怒，血量一块；异常状态和部位一块」, so
// kMonsterSplitTargets has two rows and this file constructs the two
// windows that answer for them. The old five single-block headers are
// still in the tree (the parent session deletes them in one go), but
// nothing here includes or names them any more.
#include "ui/panel_monster_split_identity.h"
#include "ui/panel_monster_split_conditions.h"
#include "ui/panel_damage_split_share.h"
#include "ui/panel_damage_split_chart.h"
#include "ui/panel_damage_split_rows.h"
#include "ui/panel_player_split_info.h"
#include "ui/panel_sections.h"
// The four conf mask rows are parsed with the console's own codec, so
// both sides of the console <-> overlay handshake share one format
// contract. Both this codec and ui/viewmodel/panel_mask_fallback.h live
// under ui/ rather than src/core/, which is what keeps the include
// direction one-way: core knows nothing about panels, and a panel concern
// that happens to be widget-free still belongs to the UI layer. The
// fallback header takes plain bool + uint32_t and never includes ui/ from
// the other side.
#include "ui/viewmodel/panel_mask_codec.h"
#include "monster/target_selector.h"
#include "reader_factory.h"

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
#include <memory>

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
    // v0.12: the window-topology switch, unified. ONE value answers
    // "which windows exist" — one big panel, or several small windows —
    // and the per-section mapping lives in a compile-time table
    // (ui/panel_sections.h → PlayerSection::kPlayerSplitTargets).
    //
    // What this replaces: one boolean flag per extracted section
    // (`--split-player-strip`, then `--split-wirebug`, then
    // `--split-mantles`, …), whose count grows linearly with every window
    // and which say nothing about each other. Those semantics are now:
    //   --layout single   the v0.11.4 topology, pixel for pixel (DEFAULT)
    //   --layout split    the sections the table names move to their own
    //                      windows
    //
    // Why a flag and not a new mask bit: the mask describes *which blocks
    // a user chose to see*, and both bits already mean that for exactly
    // one panel. Reusing them to also mean "which window draws them"
    // would put two unrelated questions on one boolean, so
    // "--mask-player 0" would answer the window question as well as the
    // visibility one and the console's saved toggles would silently change
    // the overlay's window topology. A separate flag answers exactly one
    // question and cannot interact with the console's row (see also the
    // "why not a conf row" note beside resolveStartupPanelMask: a
    // persisted value that a later default could override is how a user's
    // saved state gets discarded).
    //
    // COMPATIBILITY: `--split-player-strip` is kept as a deprecated alias
    // for `--layout split`. It is a store_true boolean, so it can only
    // turn the split ON — never off again against an explicit
    // `--layout single`, which is what keeps the pair unambiguous.
    QCommandLineOption layoutOption(
        QStringLiteral("layout"),
        QStringLiteral("Player window topology: single (one panel, the "
                       "default) or split (small windows per section "
                       "group)"),
        QStringLiteral("layout"));
    QCommandLineOption splitPlayerStripOption(
        QStringLiteral("split-player-strip"),
        QStringLiteral("Deprecated alias for --layout split. The debuff/"
                       "buff capsules move into their own window."));
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


    // v0.14: one screen for everything. The four per-panel options above
    // only ever reached the four BASE panels, so in `--layout split` the
    // thirteen sub-windows kept landing on whichever screen layer-shell
    // picked by default — and because a layer-shell surface is positioned
    // by (anchor corner + margins) with no move gesture, a sub-window on
    // the wrong output could not be moved to another screen at all. The
    // user reported exactly that.
    //
    // Read AFTER the per-panel options so a specific --output-monster
    // still wins over a blanket --output-all.
    QCommandLineOption outputAllOption(
        QStringLiteral("output-all"),
        QStringLiteral("Screen (QScreen::name(), e.g. DP-1) for every "
                       "window, including split sub-windows."),
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
    parser.addOption(splitPlayerStripOption);
    parser.addOption(layoutOption);
    parser.addOption(gameOption);
    parser.addOption(outputPlayerOption);
    parser.addOption(outputMonsterOption);
    parser.addOption(outputDamageOption);
    parser.addOption(outputPetsOption);
    parser.addOption(outputAllOption);
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
    //
    // v0.12 bug fix: this used to be "no --mask-* flag -> 0xFFFFFFFF", so the
    // conf the control console writes (the four mask rows, preserved
    // verbatim alongside `locale=` by mhw::serializePanelMasks) reached a live
    // overlay ONLY through the flags the console appends when it spawns the
    // child. Running ./monster-overlay by hand — the workflow README.md and
    // docs/USAGE.md both teach — silently discarded every saved toggle. The
    // console-spawned path is untouched.
    //
    // The priority chain now mirrors resolveStartupLocale's shape
    // (core/locale_conf.h): explicit CLI > conf row > everything visible.
    // A conf row that is missing or malformed degrades to the default
    // exactly as before, so a truncated conf can never make the overlay
    // unstartable; an explicit CLI value that fails to parse still warns
    // and keeps the default (unchanged behaviour).
    //
    // Per-slot CLI parse. This stays in main.cpp (out of the pure header)
    // because it is where QCommandLineParser and the warning live; the
    // header only ever sees the resulting value, which is what makes the
    // decision testable without a parser.
    const auto panelMaskCliRequest =
        [&](const QCommandLineOption &opt) -> mhw::PanelMaskCliRequest {
        if (!parser.isSet(opt))
            return {};                            // absent -> not explicit
        bool ok = false;
        const uint32_t v = parser.value(opt).toUInt(&ok, 16);
        if (!ok) {
            qWarning("Ignoring invalid --mask-* hex value '%s'",
                     qPrintable(parser.value(opt)));
            return {true, false, 0xFFFFFFFFu};     // explicit but unusable
        }
        return {true, true, v};
    };
    const mhw::PanelMaskCliRequest cliMaskPlayer  = panelMaskCliRequest(maskPlayerOption);
    const mhw::PanelMaskCliRequest cliMaskMonster = panelMaskCliRequest(maskMonsterOption);
    const mhw::PanelMaskCliRequest cliMaskDamage  = panelMaskCliRequest(maskDamageOption);
    const mhw::PanelMaskCliRequest cliMaskPets    = panelMaskCliRequest(maskPetsOption);

    // One conf read, one parse, four resolutions: opening and scanning the
    // file once per mask would quadruple the I/O for no behavioural gain,
    // and the four masks are only needed here.
    const QString confMaskText = mhw::readPanelMaskConfText(confPath);
    const mhw::PanelMaskSet confMasks = mhw::parsePanelMasks(confMaskText);
    const mhw::PanelMaskConfRow confMaskPlayer = {confMasks.playerValid,  confMasks.player};
    const mhw::PanelMaskConfRow confMaskMonster = {confMasks.monsterValid, confMasks.monster};
    const mhw::PanelMaskConfRow confMaskDamage  = {confMasks.damageValid,  confMasks.damage};
    const mhw::PanelMaskConfRow confMaskPets    = {confMasks.petsValid,    confMasks.pets};

    // ─── the one-time schema migration ───────────────────────────────────
    //
    // v0.12 added PlayerSection::Sharpness = 1u << 8. A conf file written by
    // v0.11 stores `player=ff` — eight bits, no bit 8, because the bit did
    // not exist yet. But a v0.12 user who opens the console and TURNS THE
    // GAUGE OFF also stores `player=ff`, because clearing bit 8 is exactly
    // how "off" is written. One value, two intents, and no function of the
    // value alone can tell them apart.
    //
    // So the file now declares its own schema. `maskVersion=1` means "these
    // four values were written by a build that knows about the bit, and a
    // clear bit 8 is a real choice". Its ABSENCE means "written before the
    // bit existed", and that — only that — is what triggers the one-time
    // migration below.
    //
    // The overlay does not write the conf file: locale_sync.h's single-writer
    // contract reserves that to the console. So if the user runs the overlay
    // directly and never opens the console, the migration is re-applied on
    // each start — which is the same visible outcome as before this change
    // (the gauge reads as on), not a regression. Opening the console and
    // saving anything writes maskVersion=1 and ends the migration for good.
    uint32_t playerMaskConfValue = confMaskPlayer.value;
    if (confMasks.playerValid && confMasks.needsSchemaMigration()) {
        const uint32_t migrated =
            mhw::PlayerSection::migrateOldSharpnessBit(confMasks.player);
        qInfo("overlay: mask conf has no %s= (pre-v0.12 schema); migrating "
              "the Sharpness bit once: %08x -> %08x",
              mhw::kPanelMaskVersionKey, confMasks.player, migrated);
        playerMaskConfValue = migrated;
    }
    const mhw::PanelMaskConfRow migratedMaskPlayer = {confMasks.playerValid,
                                                      playerMaskConfValue};

    // v0.12 third wave: the SAME one-time migration for the monster panel.
    //
    // MonsterSection::Stamina (1u << 6) was split out of Hp, so a conf
    // written before that has bit 6 clear — and reading it literally hides
    // the stamina bar for every user who upgrades. migrateOldMonster-
    // StaminaBit() is the guard, and it is only reachable from here: a
    // pure function nobody calls is dead code that reads like a fix.
    //
    // Its guard is narrower than the player one because the new bit sits
    // INSIDE the old bit range rather than above it — the pre-split set
    // is 63 non-zero masks, not "everything below bit 8" — so the sweep
    // in monster_split_models_tests.cpp covers both ranges exhaustively.
    uint32_t monsterMaskConfValue = confMaskMonster.value;
    if (confMaskMonster.valid && confMasks.needsSchemaMigration()) {
        const uint32_t migrated = mhw::MonsterSection::migrateOldMonsterStaminaBit(
            confMasks.monster);
        qInfo("overlay: mask conf has no %s= (pre-v0.12 schema); migrating "
              "the monster Stamina bit once: %08x -> %08x",
              mhw::kPanelMaskVersionKey, confMasks.monster, migrated);
        monsterMaskConfValue = migrated;
    }
    const mhw::PanelMaskConfRow migratedMaskMonster = {confMaskMonster.valid,
                                                       monsterMaskConfValue};

    const uint32_t maskPlayer  = mhw::resolveStartupPanelMask(
                                     cliMaskPlayer,  migratedMaskPlayer,
                                     mhw::PanelMaskSlot::Player).value;
    const uint32_t maskMonster = mhw::resolveStartupPanelMask(
                                     cliMaskMonster, migratedMaskMonster,
                                     mhw::PanelMaskSlot::Monster).value;
    const uint32_t maskDamage  = mhw::resolveStartupPanelMask(
                                     cliMaskDamage,  confMaskDamage,
                                     mhw::PanelMaskSlot::Damage).value;
    const uint32_t maskPets    = mhw::resolveStartupPanelMask(
                                     cliMaskPets,    confMaskPets,
                                     mhw::PanelMaskSlot::Pets).value;

    mhw::RiseDamageDisplayOptions riseDamageDisplayOptions;
    riseDamageDisplayOptions.showOtherMembers =
        (maskDamage & mhw::DamageSection::OtherMembers) != 0u;
    riseDamageDisplayOptions.showLocalPets =
        (maskPets & mhw::PetDamageSection::LocalPets) != 0u;
    riseDamageDisplayOptions.showOtherPets =
        (maskPets & mhw::PetDamageSection::OtherPets) != 0u;

    // v0.12: the window topology. ONE enum value decides which windows
    // exist, and everything below it is derived from it plus the
    // declarative table in ui/panel_sections.h
    // (PlayerSection::kPlayerSplitTargets), so the two halves cannot
    // drift apart:
    //
    //     PlayerLayoutMode mode
    //       -> playerSplitLayoutMode          (resolved below)
    //       -> maskPlayerForPanel = playerPanelMaskFor(mode, maskPlayer)
    //            -> PlayerPanel's mask        (the panel stops drawing)
    //       -> playerSubWindowMask(slot, maskPlayer)   (per window)
    //            -> each split window         (it starts drawing)
    //
    // There is NO BIT ARITHMETIC IN THIS FILE. `maskPlayer & ~(Debuff|Buff)`
    // was hand-written here in v0.12; it now lives once, in the table, and
    // is reached through playerPanelMaskFor(). Adding a third window
    // changes neither this function nor the mask expressions below.
    //
    // Why derived-from-the-mask rather than a second runtime setter on
    // PlayerPanel: mask is a STARTUP value (control_panel.cpp re-pushes it
    // into the console's own previews; the live loop never re-pushes), so
    // deriving it here is stable for the whole session, and a derived value
    // cannot be half-applied — either the bits are cleared before
    // playerPanel.setSectionMask() runs, or they are not.
    //
    // DEFAULT SINGLE: mode == Single resolves to maskPlayerForPanel ==
    // maskPlayer (see the static_assert under the table), the strip is
    // never constructed, and PlayerPanel paints byte-for-byte what
    // v0.11.4 painted. That is the pixel-identical guarantee, and its
    // gate is playerPanelMaskFor()'s Single arm.
    const mhw::PlayerLayoutResolution playerSplitLayout =
        // v0.15: READ THE CONF ROW. It was hard-coded to Default here, so
        // the overlay ignored `layout=split` in monster-overlay.conf
        // entirely and always booted Single unless the console passed
        // --layout. Measured before this line:
        //
        //     $ printf 'layout=split\n...' > monster-overlay.conf
        //     $ monster-overlay --edit --game world
        //     [INFO] v0.12 split: layout=single (default)
        //
        // which is half of the user's report "I started edit from the
        // console in split mode and got the big panels": the console half
        // was fixed in v0.14 (consoleLayoutArgv now spells --layout=split
        // with its dashes), but anything that started the overlay by hand
        // — a launcher, a script, a muscle-memory command line — was still
        // ignored. The console is not the only way this program is run.
        //
        // The conf text is the one already read for the masks, so the row
        // is parsed out of the same bytes rather than a second read of a
        // file that could have changed in between.
        mhw::resolveStartupLayoutMode(
            mhw::PlayerLayoutCliRequest{
                parser.isSet(layoutOption), parser.value(layoutOption),
                mhw::splitPlayerStripFlagSense(parser.isSet(splitPlayerStripOption))},
            // PlayerLayoutConfRow is an alias for PlayerLayoutSource (see
            // panel_sections.h), and parseConsoleLayoutRow returns the
            // console's own row wrapper — so the source is what crosses
            // over, not the struct.
            mhw::parseConsoleLayoutRow(confMaskText).source);
    // Single is the default and the pixel-identity guarantee: it resolves
    // maskPlayerForPanel to exactly maskPlayer, so every pre-v0.12 user gets
    // one big window and the baselines stay byte-identical.
    const mhw::PlayerSection::PlayerLayoutMode playerSplitLayoutMode =
        playerSplitLayout.mode;
    const uint32_t maskPlayerForPanel = mhw::PlayerSection::playerPanelMaskFor(
        playerSplitLayoutMode, maskPlayer);
    // v0.12 third wave: the monster and damage panels derive their own the
    // same way — the mask each PANEL receives is the user's mask minus the
    // rows that moved out, so a block is drawn by exactly one surface.
    //
    // Without this the panel keeps every bit in Split mode and draws the
    // HP bar and the stamina bar alongside the new windows that draw the
    // same two bars. The Single arm of each *PanelMaskFor() is `return
    // mask` verbatim, which is what keeps the ten single-mode baselines
    // byte-identical.
    const uint32_t maskMonsterForPanel =
        mhw::monsterPanelMaskFor(playerSplitLayoutMode, maskMonster);
    const uint32_t maskDamageForPanel =
        mhw::damagePanelMaskFor(playerSplitLayoutMode, maskDamage);
    if (playerSplitLayoutMode == mhw::PlayerSection::PlayerLayoutMode::Split) {
        qInfo("v0.12 split: layout=split; %d player sub-window(s); player "
              "mask %08x -> %08x; %d damage sub-window(s); damage mask "
              "%08x -> %08x",
              mhw::PlayerSection::kPlayerSplitTargetCount, maskPlayer,
              maskPlayerForPanel,
              mhw::DamageSplitSlot::kCount, maskDamage,
              mhw::damagePanelMaskFor(mhw::PlayerSection::PlayerLayoutMode::Split,
                                      maskDamage));
        // v0.15: the damage half of that line is not decoration. The
        // per-player roster used to be a console-preview window while
        // DamagePanel kept drawing the rows itself, so the console and the
        // overlay disagreed and the user saw a window one of them never
        // mounted. Printing BOTH masks makes the handoff visible on a real
        // run: if `damage mask` keeps a bit that no window claims, the same
        // disagreement is back and the log will say so before a pixel is
        // painted.
    } else {
        qInfo("v0.12 split: layout=single (default); player mask %08x", maskPlayer);
    }

    // Resolve the target game. auto scans for a running process; an
    // explicit world/rise forces the matching reader. Default to World
    // when nothing is running so the overlay still starts and waits.
    mhw::GameId gameId = mhw::GameId::World;
    {
        const QString gameArg = parser.value(gameOption).toLower();
        if (mhw::gameIdFromString(gameArg, &gameId)) {
            // recognised "world" / "rise"
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
    //
    // This was `isRise ? "Monster Overlay · Rise" : "Monster Overlay ·
    // World"` — a ternary whose only two arms made World the default for a
    // game nobody wrote an arm for. The name is per-game DATA, and
    // gameDisplayName() (core/game_capabilities.cpp:77) already answers it
    // through a switch with no default, so a third GameId is a -Wswitch
    // warning on that switch instead of a silent "World" here.
    //
    // Chinese window titles would name the game entirely in Chinese, so the
    // zh locale keeps its game name from the same table rather than a
    // hand-written zh string.
    QApplication::setApplicationDisplayName(
        QStringLiteral("Monster Overlay · %1").arg(mhw::gameDisplayName(gameId)));

    // v0.14: the panels' layout namespace is per game AND per layout
    // mode. Every panel's settings section is now
    // `[<game>/<mode>/<key>]` inside panels.ini, so the two games'
    // window geometry, scale, floors, margins and columns dials cannot
    // overwrite each other, and neither can one game's single-mode
    // layout overwrite its split-mode one. Why prefixing rather than a
    // file per game, why the mode is a segment rather than a second
    // file, and why both axes are injected here rather than asked of
    // Panel — panel_settings_game.h argues all three.
    //
    // This MUST run before the first Panel is constructed. LoadConfig()
    // is called from the Panel ctor, so a panel built before this line
    // would read (and on its first save, migrate) the WRONG game's
    // namespace — the two games are constructed in one burst below and
    // the event loop has not started yet, so one call is sufficient and
    // there is no mid-session game change to guard against: the reader
    // is immutable for this process and switching games restarts it.
    mhw::setPanelSettingsGame(gameId);

    // AND THE MODE, for the same reason and in the same breath.
    //
    // THE MODE IS PART OF A WINDOW'S ADDRESS, not a detail. The split's
    // sub-windows (player-status, player-bars, monster-identity, …) do
    // not exist in single mode, and the four mother panels hold a
    // different mask and therefore a different size in each. Storing
    // both under one section is what made the two topologies fight:
    // the user's `[rise/monster] mt=212 mr=1130` was a single-mode
    // position sitting next to `[rise/player-bars] ml=2260`, and
    // reading the game alone could not tell which was which. Their
    // verdict: 「双方布局会互相影响，分开」.
    //
    // Resolved ABOVE, from the same --layout host that decides the
    // topology, so the namespace and the windows that read it cannot
    // disagree: `--layout=split` both splits the panel AND points it at
    // [world/split/...]. Deriving it here from a second flag would be
    // how the two drift.
    //
    // One-shot, like the game: `--layout` is read once at startup and
    // nothing in a running overlay changes it. A mid-flight change would
    // retarget the namespace underneath live windows, which is the
    // failure the v0.13 rail-switch bug taught.
    mhw::setPanelSettingsLayout(playerSplitLayoutMode);

    PlayerPanel playerPanel;
    MonsterPanel monsterPanel;
    DamagePanel damagePanel;
    PetDamagePanel petDamagePanel;
    // v0.12: the split's strip. Stack-allocated like the four base panels
    // (Panel's ctor is heavy — layer-shell surface + config — so ownership
    // follows the same lifetime, and there is nothing to delete). Constructed
    // UNCONDITIONALLY but never shown unless the switch is on: Panel::setVisible
    // has a hard master gate below, and the live loop's setVisible() call is
    // itself behind the switch, so with the default-off path the object exists
    // and draws nothing — it never reaches show() and never mounts a surface.
    //
    // Why not construct them inside the Split-mode branch: it would need a
    // different lifetime (unique_ptr / optional), and the split's next
    // sub-windows (mantles, wirebug) would each need the same dance. One
    // always-existing, never-shown object is the same trick the four base
    // panels already rely on for the disabled state (setPanelEnabled(false)
    // leaves the panel alive and unmounted).
    //
    // v0.12: the same object, for the split's three further windows. A
    // window per section group, each stack-allocated like the four base
    // panels and the strip: Panel's ctor is heavy (layer-shell surface +
    // config), so ownership follows the same lifetime and there is
    // nothing to delete. Their table rows are kPlayerSplitTargets[1..3],
    // and PlayerSplitSlot names the index each main.cpp block below uses
    // for playerSubWindowMask().
    // v0.13: each sub-window carries its row in its parent's split table
    // as a CONSTRUCTOR argument, spelled from the enum rather than from a
    // literal. It has to be here because the base Panel constructor calls
    // loadConfig(), and the index exists solely to pick that call's
    // first-run default — a setter after construction is a no-op for the
    // one thing the value is for.
    //
    // The argument is what makes a first run a CASCADE instead of a pile:
    // defaultMarginsFor(TopLeft) is (20,20) for every window on earth, so
    // eleven of them hard-coding TopLeft used to stack on one pixel. The
    // user's panels.ini showed exactly that: four windows with identical
    // ml=20 mt=20, which is the fingerprint of "never been dragged".
    //
    // PlayerSplitSlot is nested in PlayerSection; MonsterSplitSlot and
    // DamageSplitSlot sit at mhw level. That asymmetry is the existing
    // layout of panel_sections.h and is recorded here so the next person
    // does not have to rediscover it by compiler error.
    PlayerStatusStripPanel playerStatusStripPanel(
        nullptr, mhw::PlayerSection::PlayerSplitSlot::Status);
    PlayerBarsStripPanel playerBarsStripPanel(
        nullptr, mhw::PlayerSection::PlayerSplitSlot::Bars);
    PlayerMantlesStripPanel playerMantlesStripPanel(
        nullptr, mhw::PlayerSection::PlayerSplitSlot::Mantles);
    PlayerWirebugStripPanel playerWirebugStripPanel(
        nullptr, mhw::PlayerSection::PlayerSplitSlot::Wirebug);
    PlayerWeaponStripPanel playerWeaponStripPanel(
        nullptr, mhw::PlayerSection::PlayerSplitSlot::Weapon);
    MonsterIdentityStripPanel monsterIdentityStripPanel(
        nullptr, mhw::MonsterSplitSlot::Identity);
    MonsterConditionsStripPanel monsterConditionsStripPanel(
        nullptr, mhw::MonsterSplitSlot::Conditions);
    DamageShareStripPanel damageShareStripPanel(
        nullptr, mhw::DamageSplitSlot::Share);
    DamageChartStripPanel damageChartStripPanel(
        nullptr, mhw::DamageSplitSlot::Chart);
    // v0.15: the per-player roster as its own window. This is the piece
    // that made the console and the overlay disagree: the preview built
    // it, the live overlay did not, and DamagePanel kept drawing the rows
    // itself — so the user saw a window the console promised and the
    // overlay never mounted. Now it is in kDamageSplitTargets, so
    // damagePanelMaskFor() hands Rows to this window in Split mode and the
    // panel draws only what stayed.
    DamageRowsStripPanel damageRowsStripPanel(
        nullptr, mhw::DamageSplitSlot::Rows);
    PlayerInfoStripPanel playerInfoStripPanel(
        nullptr, mhw::PlayerSection::PlayerSplitSlot::Info);

    playerPanel.setEditMode(editMode);
    monsterPanel.setEditMode(editMode);
    damagePanel.setEditMode(editMode);
    petDamagePanel.setEditMode(editMode);
    playerStatusStripPanel.setEditMode(editMode);
    playerBarsStripPanel.setEditMode(editMode);
    playerMantlesStripPanel.setEditMode(editMode);
    playerWirebugStripPanel.setEditMode(editMode);
    playerWeaponStripPanel.setEditMode(editMode);
    monsterIdentityStripPanel.setEditMode(editMode);
    monsterConditionsStripPanel.setEditMode(editMode);
    damageShareStripPanel.setEditMode(editMode);
    damageChartStripPanel.setEditMode(editMode);
    damageRowsStripPanel.setEditMode(editMode);
    playerInfoStripPanel.setEditMode(editMode);

    // L1: apply section masks BEFORE show() so the first paint reflects
    // them — no one-frame flash of "all visible" if a mask is restrictive.
    //
    // v0.12: the player mask is the DERIVED one (maskPlayerForPanel).
    // See the split block above: with the switch off it is identical to
    // maskPlayer, so this line is a no-op for the default path — the panel
    // receives exactly the value v0.11.4 handed it.
    //
    // The strip gets maskPlayer VERBATIM, uncleared. Its two gates
    // (PlayerStatusStripPanel::debuffRows() / ::buffRows(),
    // panel_player_split.cpp:419 and :426) read PlayerSection::Debuff / ::
    // Buff, so the user's existing toggles keep working: someone who already
    // turned the buff row off in the console gets no buff window instead of
    // an empty one. The panel's bits were cleared only because the WINDOW
    // topology changed, never because of anything the user toggled.
    //
    // v0.12: the same verbatim-mask rule for the other three windows.
    // Each of them intersects the CLEAN mask with its own section bit
    // internally — PlayerBarsStripPanel::barsEnabled() reads
    // PlayerSection::Bars, ::mantlesEnabled() reads ::Mantles and
    // ::wirebugEnabled() reads ::Wirebug (the same role the pilot's
    // debuffRows() plays) — so handing them maskPlayer rather than
    // maskPlayerForPanel is what keeps the user's per-section toggles
    // working: a hunter who switched 衣装 off in the console gets no
    // mantle window, not an empty one, and the panel's mask and the
    // window's mask stay answers to two different questions.
    //
    // There is NO third expression here. Every mask below is either
    // maskPlayerForPanel (one call) or maskPlayer (verbatim), because
    // the table in panel_sections.h is the only thing that knows which
    // section belongs to which window, and it derives both.
    playerPanel.setSectionMask(maskPlayerForPanel);
    monsterPanel.setSectionMask(maskMonsterForPanel);
    damagePanel.setSectionMask(maskDamageForPanel);
    petDamagePanel.setSectionMask(maskPets);
    playerStatusStripPanel.setSectionMask(maskPlayer);
    playerBarsStripPanel.setSectionMask(maskPlayer);
    playerMantlesStripPanel.setSectionMask(maskPlayer);
    playerWirebugStripPanel.setSectionMask(maskPlayer);
    playerWeaponStripPanel.setSectionMask(maskPlayer);
    // The sub-windows take the USER’s mask, not the derived one: what they
    // draw is filtered by the rows the table says each one owns
    // (monsterSubWindowMask), so a bit another window claims never
    // reaches them. The panel is the one that gets the reduced mask.
    //
    // v0.13: this is now MORE important than it was, because the two
    // monster windows carry several blocks each. The Identity window is
    // handed the full user mask and divides it back out per block
    // (identityInfoEnabled / hpEnabled / staminaEnabled / enrageEnabled),
    // which is exactly how 「把激怒条单独关掉」 works: the console's 激怒
    // toggle still writes MonsterSection::Enrage, and the window reads
    // that bit at every consumer of the enrage row. The Conditions
    // window does the same with Ail and Parts.
    monsterIdentityStripPanel.setSectionMask(maskMonster);
    monsterConditionsStripPanel.setSectionMask(maskMonster);
    damageShareStripPanel.setSectionMask(maskDamage);
    damageChartStripPanel.setSectionMask(maskDamage);
    damageRowsStripPanel.setSectionMask(maskDamage);
    playerInfoStripPanel.setSectionMask(maskPlayer);
    damagePanel.setRiseDisplayOptions(riseDamageDisplayOptions);
    petDamagePanel.setDisplayOptions(riseDamageDisplayOptions);
    // Master gate: maps to the control console's "面板启用"
    // toggle. When the flag is set, the panel does not show its
    // layer-shell surface at all — independent of the section mask
    // above. setVisible() in the live loop will be a no-op for the
    // disabled panel.
    //
    // v0.14: AND THE SPLIT'S OWN GATE, because a mother panel whose
    // every block has left for a window has nothing to mount. The user
    // found the leftover: 「分块模式下有一个纯空的只显示player的面板」.
    //
    // The one place the four are decided, each with its own reasoning:
    //
    //   PLAYER — playerPanelMaskFor(Split, mask) is empty BY
    //     CONSTRUCTION: every PlayerSection bit is claimed by a row of
    //     kPlayerSplitTargets (panel_sections.h records that table's
    //     exhaustiveness), so the panel's derived mask is 0 and it
    //     would paint a bare chrome frame with a PLAYER title. Gate it
    //     entirely — there is no block for it to hold.
    //
    //   MONSTER — the ONE that must NOT be gated wholesale, and this is
    //     the asymmetry worth reading. MonsterSection::Tenderize is
    //     deliberately NOT a row of kMonsterSplitTargets (the table
    //     names its omission in a comment): since v0.7.4 PR C the
    //     勾爪软化 countdown is a strip INSIDE each .pc card, drawn and
    //     height-reserved by pcHasTenderize(), so it cannot be
    //     re-parented without drawing it twice. monsterPanelMaskFor()
    //     therefore KEEPS that bit, and MonsterPanel still has real
    //     content to draw in split mode — for a monster whose parts are
    //     being tenderized. Gate it unconditionally and the user loses
    //     the one thing the split did not move; never gate it and they
    //     get an empty frame for the other 99% of the hunt. So it is
    //     gated on the mask, which is exactly "does this panel still
    //     own a block".
    //
    //   DAMAGE — same asymmetry as MONSTER, one row out of four.
    //     kDamageSplitTargets claims Share and Chart, so Rows (the
    //     player rows) and OtherMembers (the party rows) stay in the
    //     panel; the derived mask is non-zero whenever either is on.
    //     Gated on the mask for the same reason as MONSTER.
    //
    //   PETS — untouched, and NOT because it is an oversight.
    //     PetDamageSection has no split table at all (its whole bit
    //     space stays in the panel whatever the mode), so the mask is
    //     identical in both modes and the gate below is a no-op for it.
    //     Adding a pets split later is adding a row to a table plus one
    //     line there, not a special case here.
    //
    // WHY THE MASTER GATE AND NOT A VISIBILITY CALL. panelEnabled_ is
    // the axis that removes the layer-shell surface
    // (panel.cpp's setVisible returns early for a disabled panel), so
    // this is the switch that stops an empty frame from existing at
    // all rather than hiding it after it has been created and shown.
    playerPanel.setPanelEnabled(!parser.isSet(noPlayerOption)
                                && (playerSplitLayoutMode
                                        != mhw::PlayerSection::PlayerLayoutMode::Split
                                    || mhw::PlayerSection::playerPanelHasBlocks(
                                           maskPlayerForPanel)));
    // v0.16: monsterPanelHasBlocks / damagePanelHasBlocks, not `!= 0`.
    // MonsterPanel's split-mode mask is MonsterSection::Tenderize and
    // DamagePanel's is DamageSection::OtherMembers — both are live
    // toggles that draw nothing of their own, so a `!= 0` gate keeps the
    // panel ENABLED with no block inside it. Panel::setVisible() only
    // refuses to mount a DISABLED panel, so an enabled-but-empty panel
    // mounts as chrome: 18px for the monster one (2*kPanelPad) and 45
    // for the damage one (kMargin + 14 + 9 + kMargin), both with the
    // accent stripe and border painted. That is the one-line floating
    // window reported on both games.
    //
    // This is the same decision the main loop makes every tick, moved
    // to startup so the surface is never created rather than hidden
    // afterwards. The two cannot drift: both call the same predicate on
    // the same derived mask.
    monsterPanel.setPanelEnabled(!parser.isSet(noMonsterOption)
                                 && (playerSplitLayoutMode
                                         != mhw::PlayerSection::PlayerLayoutMode::Split
                                     || mhw::monsterPanelHasBlocks(
                                            maskMonsterForPanel)));
    damagePanel.setPanelEnabled(!parser.isSet(noDamageOption)
                                && (playerSplitLayoutMode
                                        != mhw::PlayerSection::PlayerLayoutMode::Split
                                    || mhw::damagePanelHasBlocks(
                                           maskDamageForPanel)));
    // v0.10.1: the pet damage surface is Rise-only. World never mounts it,
    // regardless of --mask-pets / --no-pets; the console hides the matching
    // controls in World mode for the same reason.
    // v0.13: the pet-damage panel is gated on the FEED, not on the game
    // literal. The reason is evidence, not symmetry:
    //
    //  * `petDamagePanel.updateRiseDamage()` is fed by
    //    mhw::RiseDamageReader, and `riseDamageReaders` is only populated
    //    above `if (isRise)`. Under World the three readers do not exist
    //    and nothing ever hands the panel a snapshot.
    //  * `mhw::enrichRiseDamageSnapshot()` returns immediately unless
    //    `game.game == GameId::Rise` (src/rise/rise_damage_roster.cpp:87),
    //    so the owner/name resolution that turns a raw pet JSON actor into
    //    a readable row is Rise-only too.
    //  * The JSON vocabulary itself is a Lua producer protocol with
    //    no World counterpart (reader_descriptors.cpp names Rise's
    //    install dir; nothing in world/*.cpp mentions a damage feed).
    //
    // So the panel genuinely has no World data source — this is not a
    // capability guess. What changed is only WHAT the gate is spelled
    // on: the old `isRise &&` bundled two questions into one expression
    // ("does this game have the data?" and "did the user disable it via
    // --no-pets?"). Separating them matters because sectionMask_ and
    // panelEnabled_ are already two separate axes everywhere else in
    // Panel: the MASK bits control CONTENT (which pet rows are drawn)
    // and panelEnabled controls MOUNTING (whether the surface exists at
    // all). Writing `isRise &&` here made "the game has no pets" look
    // like a user toggle, so a World run reported the panel as
    // user-disabled and the console's pets card drew its "Rise only"
    // placeholder for the wrong reason.
    //
    // The mask half is already correct and stays: maskPets is passed to
    // setSectionMask() above and reads PetDamageSection::LocalPets /
    // ::OtherPets, which filter rows rather than the window.
    petDamagePanel.setPanelEnabled(!parser.isSet(noPetsOption));

    // v0.12: THE MASTER GATES for the whole split — one predicate, four
    // windows, and this is the single place the default path is decided.
    // With the switch off (the default) each window's panelEnabled_ is
    // false, so Panel::setVisible() returns early at panel.cpp:355
    // ("!panelEnabled_ && visible -> return") for EVERY caller forever,
    // including the live loop's setVisible() below. Each window therefore
    // never mounts a layer-shell surface, never paints, and is
    // indistinguishable from not existing at all — while still not
    // needing a conditional lifetime.
    //
    // The gate is the LAYOUT MODE and nothing else, so the split is one
    // user decision: it is not derivable from the section mask, which
    // would let `--mask-player 0` silently change window topology.
    //
    // Each of the four compounds with its own two gates on top, all three
    // of which must be crossed before a single pixel appears:
    //   1. this line             (the switch)
    //   2. setSectionMask        (the user's own per-section toggles)
    //   3. hasVisibleContent()   (HunterPie's "no data, no window")
    //
    // Extending to a fifth window: one line here, one row in
    // kPlayerSplitTargets, one PlayerSplitSlot alias. Nothing else.
    const bool playerSplitOn = playerSplitLayoutMode
        == mhw::PlayerSection::PlayerLayoutMode::Split;
    // v0.12 third wave: the same master gate for the monster and damage
    // windows. ONE predicate per panel — the mode is a property of the
    // layout, not of the panel, so three panels in one mode share one
    // bool rather than each re-deriving it.
    const bool monsterSplitOn = playerSplitOn;
    const bool damageSplitOn = playerSplitOn;
    playerStatusStripPanel.setPanelEnabled(playerSplitOn);
    playerBarsStripPanel.setPanelEnabled(playerSplitOn);
    playerMantlesStripPanel.setPanelEnabled(playerSplitOn);
    playerWirebugStripPanel.setPanelEnabled(playerSplitOn);
    playerWeaponStripPanel.setPanelEnabled(playerSplitOn);
    monsterIdentityStripPanel.setPanelEnabled(monsterSplitOn);
    monsterConditionsStripPanel.setPanelEnabled(monsterSplitOn);
    damageShareStripPanel.setPanelEnabled(damageSplitOn);
    damageChartStripPanel.setPanelEnabled(damageSplitOn);
    damageRowsStripPanel.setPanelEnabled(damageSplitOn);
    playerInfoStripPanel.setPanelEnabled(playerSplitOn);


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

    // ─── the split sub-windows inherit their parent's screen ───────────
    //
    // THE GAP THIS FILLS. Panel::applyGeometry() resolves
    // outputName_ -> primaryScreen() and NOTHING else. A split
    // sub-window's `outputName` is loaded from panels.ini by
    // Panel::loadConfig() inside its own constructor, exactly like a base
    // panel's — but on the reporter's machine every one of the eleven
    // keys is EMPTY, so all eleven resolved to primaryScreen():
    //
    //     ~/.config/monster-overlay/panels.ini
    //     [world/split/player]        outputName=DP-1     <- the parent
    //     [world/split/player-status] outputName=          <- empty
    //     ... 10 more, all empty
    //
    // and their primary is a PORTRAIT HDMI-A-1 while the console is set
    // to a landscape DP-1. So the console drew every sub-window against
    // DP-1 (its preview fallback, pushed by
    // ControlPanel::pushPreviewScreenToSplitWindows) while the live
    // overlay mounted all eleven on HDMI-A-1. The console's push is a
    // MEASUREMENT — Panel::marginLimits() reads it so the drag clamp
    // measures the screen the preview draws — and a measurement is not a
    // placement: Panel::applyGeometry() deliberately ignores it, because
    // a preview screen must never decide where a live overlay lands.
    // This block is the placement that measurement was standing in for.
    //
    // WHY INHERIT THE PARENT AND NOT A "SESSION SCREEN". Three answers
    // were available for a sub-window whose own key is empty:
    //
    //   a) inherit the parent panel's screen            <- CHOSEN
    //   b) inherit one "screen this session selected"
    //   c) leave it on primaryScreen()
    //
    // (b) is rejected because the overlay has no such selection. The
    // console has one (which SCREEN dropdown is in focus) and pushes it
    // into its previews; the overlay is a different process with no
    // dropdown, no focus and no IPC carrying that fact. Inventing one
    // from the parent is (a); inventing one from nothing is a guess.
    //
    // (c) is the status quo and is the bug: it puts eleven windows on an
    // output the user never chose for them, on surfaces a layer-shell
    // compositor gives no move gesture to.
    //
    // (a) is right because the parent panel is the only screen choice
    // the user actually made that a sub-window can be derived from. The
    // split table already says which parent each row was carved out of
    // (kSplitWindowRows' parentSlot), so "this block came out of that
    // panel" is an existing fact, not a new one — the same derivation
    // playerSubWindowMask() already uses for the mask.
    //
    // AND (a) HAS A SECOND, WIDER TIER, for the case where the parent
    // named no screen at all. See the comment on inheritLayoutOutput()
    // below for why that is still (a) and not (b).
    //
    // The sub-window's OWN key still wins. loadConfig() ran in the
    // constructor and already filled outputName_, and already applied it
    // via applyGeometry() there; an empty key here means either "no key"
    // or "key written empty", and since the console has no per-sub-window
    // SCREEN picker at all, neither spelling can be a user's deliberate
    // choice of primary. A future per-sub-window picker would populate
    // the key, and the `if (sub->outputName().isEmpty())` guard below
    // would then leave it alone — which is why that guard is the check
    // and not a comment.
    //
    // The parent's name is validated against the attached screens before
    // it is handed over. Panel::setOutputName() warns and reverts to
    // primary for an unknown output, which is the right outcome and the
    // wrong log: a parent whose stored output is unplugged would emit one
    // warning PER SUB-WINDOW (eleven lines) for a condition the parent
    // itself already degrades through silently. One check here keeps the
    // unplugged case exactly as quiet as it was.
    //
    // persist=false throughout: nothing in the launch path writes the
    // user's config. The console's saveAllSplitAndBaseWindows() is what
    // commits a screen choice to disk, and a derived value is not a
    // choice.
    auto inheritParentOutput = [](Panel *sub, const Panel *parent) {
        if (!sub || !parent)
            return;
        // The sub-window's own stored answer wins, if it has one.
        if (!sub->outputName().isEmpty())
            return;
        const QString out = parent->outputName();
        if (out.isEmpty())
            return;                     // parent follows primary too
        for (QScreen *s : QGuiApplication::screens()) {
            if (s && s->name() == out) {
                sub->setOutputName(out, false);
                return;
            }
        }
    };

    // ─── the second tier: the layout's screen, not just the parent's ───
    //
    // THE REMAINING GAP, WITH THE MEASURED NUMBERS. inheritParentOutput
    // above leaves a sub-window on primaryScreen() when its OWN parent
    // names no screen. On the reporter's file that is exactly 2 of 11:
    //
    //     [world/split/player]  outputName=DP-1   -> its 6 sub-windows OK
    //     [world/split/dps]     outputName=DP-1   -> its 3 sub-windows OK
    //     [world/split/monster] outputName=       -> EMPTY
    //       monster-identity      -> HDMI-A-1 (primary)   <- the complaint
    //       monster-conditions    -> HDMI-A-1 (primary)   <- the complaint
    //
    // The two windows the user is still seeing on HDMI are carved out of
    // the one mother panel whose split value is empty, so there is
    // nothing for the first tier to hand over and both fall through to
    // primary. The user's 「我全部都设置成 dp1」 is literally true — they
    // set every mother panel they could reach, and the two windows that
    // still went wrong are the ones whose mother they could not.
    //
    // WHY THIS IS STILL (a) AND NOT (b). The objection to a "session
    // screen" is that it invents a selection the overlay never made. This
    // invents nothing: every name it can ever return is a name the user
    // typed into this SAME panels.ini group, in this SAME layout mode,
    // for one of the FOUR panels that make up the layout being drawn.
    // The unit of the fallback is the layout, not the session — the
    // overlay is drawing one split layout out of one [world/split/*]
    // namespace, and a mother panel's screen choice inside that
    // namespace is the user stating where THIS LAYOUT lives. A
    // sub-window of that layout that names nothing inherits the answer
    // from the layout it is part of. That is the same derivation
    // (a) already uses, one level up.
    //
    // WHY IT DOES NOT TOUCH "empty = follow primary". That semantic is
    // about the panel the key BELONGS to, and it is preserved exactly:
    // monsterPanel itself still resolves its own empty outputName to
    // primaryScreen() — nothing here calls setOutputName() on a mother
    // panel, and Panel::applyGeometry() is untouched. The rule being
    // added is about a sub-window that has no key of its own, which
    // today falls to primary by DEFAULT rather than by CHOICE: the
    // console has no per-sub-window SCREEN picker at all (there is no
    // way to type a value there), so an empty sub-window key can only
    // mean "not set yet", never "I want primary". With no picker there
    // is no expression of that wish to override.
    //
    // WHY IT DOES NOT MAKE SPLIT AND SINGLE INTERFERE. Both tiers read
    // only outputName() of the four panels standing right here, and the
    // mode is fixed at line 671 before any of them is constructed — so
    // in a split run these are the [<game>/split/*] values and the
    // [<game>/single/*] values are never read. In a single run the
    // sub-windows are never mounted (this whole block is gated on
    // playerSplitOn) and the mode says single, so even a stray call
    // would read the single group. The user's 「分块和整块应该是分开的，
    // 不会互相干扰」 is about the two modes writing over each other's
    // stored values; this writes to neither — persist=false throughout,
    // and panels.ini is byte-identical afterwards.
    //
    // THE NEGATIVE CONTROL. When all four mother panels name no screen
    // there is nothing to inherit, and the fallback returns empty, which
    // leaves every sub-window on primaryScreen() — the pre-v0.12
    // behaviour, verbatim. The wider tier never fires on a file that
    // made no choice, so a single-screen user (whose every outputName is
    // empty because there is only one screen to name) is untouched.
    //
    // UNPLUGGED SCREENS. Only names that resolve against
    // QGuiApplication::screens() are returned, so a mother panel whose
    // stored output is gone is skipped rather than handed over — which
    // keeps the quiet-degradation property the first tier's screen loop
    // was written for, and keeps the sub-window on primary where it
    // would have been anyway. A session where SOME mother panels name a
    // live screen and others name a dead one prefers the live one.
    auto inheritLayoutOutput = [](const Panel *const *panels, int count) {
        for (int i = 0; i < count; ++i) {
            const Panel *p = panels[i];
            if (!p)
                continue;
            const QString out = p->outputName();
            if (out.isEmpty())
                continue;
            for (QScreen *s : QGuiApplication::screens()) {
                if (s && s->name() == out)
                    return out;
            }
        }
        return QString();
    };

    // Gated on the switch, the same way the windows' own construction
    // effects are: in Single mode the eleven objects exist but nothing
    // mounts, and setOutputName() calls applyGeometry() — which would
    // spend eleven layer-shell property pushes on windows that will
    // never be shown.
    if (playerSplitOn) {
        // Tier 2, resolved ONCE before any sub-window is touched: the
        // first mother panel in this layout that names an attached
        // screen. Empty when no mother panel names one, in which case
        // every sub-window below keeps the primary it had before.
        //
        // The order is the console's rail order (player, monster,
        // damage, pets) and it matters only for which screen wins when
        // the four disagree — a state no user has been measured in. A
        // layout that names one screen for two panels and another for
        // the other two is a layout the user made deliberately, and the
        // per-parent tier above already handles the panels that named
        // their own; this only reaches the sub-windows of a panel that
        // named nothing.
        const Panel *const mothers[] = {&playerPanel, &monsterPanel,
                                        &damagePanel, &petDamagePanel};
        const QString layoutOutput =
            inheritLayoutOutput(mothers, int(sizeof(mothers) / sizeof(mothers[0])));

        inheritParentOutput(&playerStatusStripPanel, &playerPanel);
        inheritParentOutput(&playerBarsStripPanel, &playerPanel);
        inheritParentOutput(&playerMantlesStripPanel, &playerPanel);
        inheritParentOutput(&playerWirebugStripPanel, &playerPanel);
        inheritParentOutput(&playerWeaponStripPanel, &playerPanel);
        inheritParentOutput(&playerInfoStripPanel, &playerPanel);
        inheritParentOutput(&monsterIdentityStripPanel, &monsterPanel);
        inheritParentOutput(&monsterConditionsStripPanel, &monsterPanel);
        inheritParentOutput(&damageShareStripPanel, &damagePanel);
        inheritParentOutput(&damageChartStripPanel, &damagePanel);
        inheritParentOutput(&damageRowsStripPanel, &damagePanel);

        // Tier 2, applied only to what tier 1 left behind. The guard is
        // what keeps this a FALLBACK and not an override: a sub-window
        // that just took its own parent's screen is skipped, and so is
        // one that had a key of its own. When layoutOutput is empty
        // there is nothing to do and the loop is a no-op.
        if (!layoutOutput.isEmpty()) {
            for (Panel *sub : {
                     (Panel *)&playerStatusStripPanel,
                     (Panel *)&playerBarsStripPanel,
                     (Panel *)&playerMantlesStripPanel,
                     (Panel *)&playerWirebugStripPanel,
                     (Panel *)&playerWeaponStripPanel,
                     (Panel *)&playerInfoStripPanel,
                     (Panel *)&monsterIdentityStripPanel,
                     (Panel *)&monsterConditionsStripPanel,
                     (Panel *)&damageShareStripPanel,
                     (Panel *)&damageChartStripPanel,
                     (Panel *)&damageRowsStripPanel,
                 }) {
                if (sub && sub->outputName().isEmpty())
                    sub->setOutputName(layoutOutput, false);
            }
        }
    }

    // v0.15: --output-all, applied AFTER the four per-panel options, and it
    // therefore WINS over them. That is the opposite of what this comment
    // used to say, and the old text was wrong about its own code: a later
    // setOutputName() overwrites an earlier one, so "read after" is the
    // reason the blanket wins, not the reason the specific loses. The
    // v0.14 review caught it with a probe:
    //
    //     --output-monster=DP-2 --output-all=eDP-1  ->  monster is eDP-1
    //
    // The blanket winning is the behaviour worth keeping. --output-all
    // answers "put everything on this screen" for one invocation, and a
    // per-panel flag left over from another command should not silently
    // carve one window out of it — that is the "the overlay is on my
    // secondary screen and I cannot move it" case this option exists to
    // end. To target one panel, do not pass --output-all.
    //
    // It reaches the sub-windows too, which is the whole point: the four
    // per-panel options were written in v0.8 when "a panel" meant "one of
    // four big windows", and when the split arrived in v0.12 nothing
    // extended them. A sub-window's output was therefore unreachable from
    // the command line, and unreachable from the UI as well, because
    // layer-shell has no move gesture — an output is fixed by the surface's
    // anchor and its margins.
    if (parser.isSet(outputAllOption)) {
        const QString out = parser.value(outputAllOption);
        playerPanel.setOutputName(out, false);
        monsterPanel.setOutputName(out, false);
        damagePanel.setOutputName(out, false);
        petDamagePanel.setOutputName(out, false);
        // Every split sub-window, in one pass.
        //
        // Nothing checks this list. An earlier version of this comment
        // claimed a static_assert in control_panel.cpp verifies it against
        // kSplitWindowRows; none does, and none could - the split table
        // lives in control_panel.cpp's anonymous namespace, so main.cpp
        // cannot see it and its asserts cannot reach a list in another
        // translation unit. The asserts there constrain kSplitWindowRows'
        // own length against the three split tables, which is a real check
        // and not this one. So the honest consequence: a window added to
        // the split table without a line here silently misses
        // --output-all, and no build will say so.
        for (Panel *sub : {
                 (Panel *)&playerStatusStripPanel,
                 (Panel *)&playerBarsStripPanel,
                 (Panel *)&playerMantlesStripPanel,
                 (Panel *)&playerWirebugStripPanel,
                 (Panel *)&playerWeaponStripPanel,
                 (Panel *)&playerInfoStripPanel,
                 (Panel *)&monsterIdentityStripPanel,
                 (Panel *)&monsterConditionsStripPanel,
                 (Panel *)&damageShareStripPanel,
                 (Panel *)&damageChartStripPanel,
                 (Panel *)&damageRowsStripPanel,
             }) {
            sub->setOutputName(out, false);
        }
        qInfo("overlay: --output-all=%s applied to every window",
              qPrintable(out));
    }

    // v0.12: retarget the strip's edit-mode demo preview at the game this
    // run resolved. There is no equivalent call for playerPanel here today —
    // the console's own ControlPanel::switchGame is what calls
    // setGameForDemo() on ITS preview panels (control_panel.cpp:1806) —
    // and deliberately none is added: telling PlayerPanel it is Rise when it
    // has always been told nothing would swap the World mantles demo for the
    // Rise wirebugs demo in every `--edit --game rise` launch, which is a
    // default-path change.
    //
    // Gated on the layout mode, so on the default path nothing here runs at
    // all. And inert in live mode the same way PlayerPanel's call is: the
    // first update(snap) overwrites game_ from the snapshot anyway, so only
    // the edit preview cares.
    //
    // The other three windows need NO call here, and that is a fact about
    // their demo seeds rather than a gap in the wiring:
    //   * 衣装 / HP+ST seed a World player snapshot, 翔虫 seeds a Rise one,
    //     each hard-coded in setupDemoData(), because each of the three
    //     mirrors exactly one block of exactly one game's panel. A game
    //     switch would mean a whole different seed, not a retarget of this
    //     one — and in the wrong game the model is empty, hasContent()
    //     answers false and the window stays unmounted, which is the same
    //     "no data, no window" rule the live path uses.
    //   * The base class seeds demo data itself: Panel::paintEvent calls
    //     setupDemoData() on the first paint once editMode_ is set
    //     (panel.cpp:503-506). So all four windows are drivable with no
    //     per-object call at all; setGameForDemo() is the extra
    //     step each one needs because its demo DOES vary by game.
    //
    // v0.13: this was ONE window's call. Five of the six sub-windows
    // hard-coded a game in their demo seed — 衣装 built a World snapshot,
    // 翔虫 a Rise one — which is precisely the "衣装 and 翔虫 at the same
    // time" the user reported: both windows seeded their own game's data
    // regardless of which game the run was actually reading, and both
    // answered hasContent() from it. Every window now takes the game from
    // the same resolved value the reader is built from.
    //
    // Ungated by playerSplitOn on purpose: setGameForDemo() only stores a
    // member and drops the demo seed, neither of which mounts a surface
    // or touches panels.ini, so the default path pays for it only in six
    // stores.
    playerStatusStripPanel.setGameForDemo(gameId);
    playerBarsStripPanel.setGameForDemo(gameId);
    playerMantlesStripPanel.setGameForDemo(gameId);
    playerWirebugStripPanel.setGameForDemo(gameId);
    playerWeaponStripPanel.setGameForDemo(gameId);
    playerInfoStripPanel.setGameForDemo(gameId);
    monsterIdentityStripPanel.setGameForDemo(gameId);
    monsterConditionsStripPanel.setGameForDemo(gameId);

    // v0.14: THE SHOW HALF OF THE SAME GATE. setPanelEnabled(false)
    // above is what makes the panel refuse to mount, but the REFUSAL
    // is inside Panel::setVisible() — and show() on a QWidget goes
    // through setVisible(), so it is already covered. The probe that
    // settles it (tests/panel_settings_scope_tests.cpp asserts both):
    // a disabled Panel whose show() was called reports isVisible() ==
    // false and mounts no layer-shell surface, because
    // qt_widget_private->show_helper() calls setVisible(true), which
    // hits Panel's override and returns at panel.cpp's guard.
    //
    // Left as show() rather than rewritten as a conditional, on
    // purpose: the four base panels' first-show path is byte-identical
    // to what every single-mode baseline was painted against, and a
    // `if (playerPanel.panelEnabled())` here would be a second spelling
    // of a decision setPanelEnabled() already made. One gate, one
    // place.
    playerPanel.show();
    monsterPanel.show();
    damagePanel.show();
    // Never flash an empty fourth frame at startup. In edit mode the
    // panel's own demo seed (panel_pet_damage.cpp's setupDemoData) makes
    // it immediately drivable; live mode waits for a visible pet row.
    //
    // v0.13: the `isRise &&` was removed for the SAME reason as the
    // enable gate above. With the game taken out of the equation here,
    // World edit mode now shows the pet panel's demo exactly like every
    // other panel does — which is what makes the preview honest: the
    // console already hides its pets card in World mode with a "Rise
    // only" placeholder (control_panel.cpp:2223-2246), so the overlay's
    // edit preview following the console rather than inventing a second
    // opinion is the behaviour the user asked for ("every panel has two
    // modes, and the preview must stay in sync").
    //
    // Live mode is unaffected in either direction: the poll loop's
    // setVisible(panelEnabled() && hasVisibleContent()) at :1175 answers
    // false for a World run because the demo is only seeded on the first
    // EDIT-mode paint and updateRiseDamage() is never called (no
    // riseDamageReaders exist), so the panel stays unmounted.
    petDamagePanel.setVisible(editMode);
    // v0.12: the four windows' first-show. Two things make this safe on the
    // default path, where the answer is simply "nothing happens":
    //   * setPanelEnabled(false) was applied above, so Panel::setVisible()
    //     bails at panel.cpp:355 and no surface is mounted; and
    //   * hasVisibleContent() is false before the first poll (nothing
    //     seeded, or in edit mode the demo is only seeded on the first
    //     paint — and this call precedes it).
    // In edit mode with the split on, the demo is seeded by the first
    // paint, so each window appears immediately like the other four panels
    // do.
    //
    // Two windows are still invisible in edit mode, and that is correct
    // rather than a wiring miss: 衣装 seeds a World snapshot and 翔虫 a
    // Rise one, so each answers false to hasContent() for the other game
    // and stays unmounted until that game is selected.
    if (playerSplitOn) {
        playerStatusStripPanel.setVisible(editMode);
        playerBarsStripPanel.setVisible(editMode);
        playerMantlesStripPanel.setVisible(editMode);
        playerWirebugStripPanel.setVisible(editMode);
        playerWeaponStripPanel.setVisible(editMode);
        playerInfoStripPanel.setVisible(editMode);
    }
    if (monsterSplitOn) {
        monsterIdentityStripPanel.setVisible(editMode);
        monsterConditionsStripPanel.setVisible(editMode);
    }
    if (damageSplitOn) {
        damageShareStripPanel.setVisible(editMode);
        damageChartStripPanel.setVisible(editMode);
        damageRowsStripPanel.setVisible(editMode);
    }

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

    // S5: the reader comes from the factory, so main() does not know which
    // class it received or how that game locates its map. Registering a new
    // game is a case in reader_factory.cpp rather than another arm here.
    //
    // S2 kept this construction lazy and behind `isRise` because both
    // readers loaded an offset map and Rise also resolved a Steam install
    // directory. The factory preserves that: exactly one reader is built,
    // for the game this run selected.
    //
    // isRise survives below for the genuinely Rise-only parts — the
    // REFramework damage feed and the pet-damage panel gates. Those are
    // per-game FEATURES, which read honestly as per-game checks; see
    // game_capabilities.h for why they are not capability lookups.
    mhw::GameReaderPtr reader = mhw::createReader(gameId, explicitMap, dataDirs);

    // REFramework 1.5.9.1 sandboxes Lua files under reframework/data and has
    // no rename primitive. The producer alternates two slots; read both and
    // select the greatest valid sequence. Keep /tmp as the compatibility
    // fallback for older producer builds.
    std::array<std::unique_ptr<mhw::RiseDamageReader>, 3> riseDamageReaders;
    if (isRise) {
        const QString riseInstallDir = mhw::findRiseInstallDir();
        const QString riseDataDir = riseInstallDir.isEmpty()
            ? QString()
            : QDir(riseInstallDir).filePath(QStringLiteral("reframework/data"));
        riseDamageReaders = {
            std::make_unique<mhw::RiseDamageReader>(
                riseDataDir.isEmpty()
                    ? QStringLiteral("/tmp/mhr_damage_a.json")
                    : QDir(riseDataDir).filePath(
                          QStringLiteral("mhr_damage_a.json"))),
            std::make_unique<mhw::RiseDamageReader>(
                riseDataDir.isEmpty()
                    ? QStringLiteral("/tmp/mhr_damage_b.json")
                    : QDir(riseDataDir).filePath(
                          QStringLiteral("mhr_damage_b.json"))),
            std::make_unique<mhw::RiseDamageReader>(
                QStringLiteral("/tmp/mhr_damage.json")),
        };
    }

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
    //
    // Proposing and applying are two phases (v0.11.4). localeSyncPoll()
    // only reports what the conf asks for; the request is retired by
    // localeSyncAccept() below, which runs AFTER a successful
    // StringTable::load(). A load that fails (missing / unreadable locale
    // directory) therefore leaves the request outstanding and the next
    // poll re-offers it, instead of being swallowed forever by the poll's
    // mtime short-circuit — the flip lands the moment its resource files
    // are back. The price is one qWarning per poll while the requested
    // locale stays unloadable, which is deliberate: a UI stuck on the
    // wrong language with no explanation was the bug being fixed.
    QElapsedTimer localePollClock;
    localePollClock.start();
    auto applyLocale = [&](const QString &next) {
        if (!mhw::StringTable::instance().load(next)) {
            qWarning("Locale '%s' failed to load; keeping '%s'",
                     qPrintable(next),
                     qPrintable(mhw::StringTable::instance().currentLocale()));
            return;
        }
        // The switch is live — retire the poll's proposal. Reading the
        // locale back from the table (instead of `next`) keeps the
        // detector in sync with what the UI actually shows.
        mhw::localeSyncAccept(localeSync,
                              mhw::StringTable::instance().currentLocale());
        qInfo("UI locale -> %s", qPrintable(next));
        auto &table = mhw::StringTable::instance();
        playerPanel.setWindowTitle(table.tr(QStringLiteral("ui.player_title")));
        monsterPanel.setWindowTitle(table.tr(QStringLiteral("ui.monster_title")));
        damagePanel.setWindowTitle(table.tr(QStringLiteral("ui.damage_title")));
        petDamagePanel.setWindowTitle(table.tr(QStringLiteral("ui.pet_damage_title")));
        // v0.12: the four split windows have their own cached strings.
        // Unconditional because the objects always exist and
        // retranslateUi() on a never-shown panel only touches the title
        // string and a repaint request — it is the same shape as the
        // other four calls and costs one tr() on the default path.
        //
        // The pilot's setWindowTitle() below is the deliberate redundancy
        // its own retranslateUi() documents: one panel, one title key
        // (ui.player_strip_title). The three new windows need NO line
        // here at all — they each set their own title from that same key
        // inside their own retranslateUi(), and adding three more
        // identical tr() calls would be three more chances to hand a
        // window the wrong title.
        playerStatusStripPanel.setWindowTitle(
            table.tr(QStringLiteral("ui.player_strip_title")));
        playerPanel.retranslateUi();
        monsterPanel.retranslateUi();
        damagePanel.retranslateUi();
        petDamagePanel.retranslateUi();
        playerStatusStripPanel.retranslateUi();
        playerBarsStripPanel.retranslateUi();
        playerMantlesStripPanel.retranslateUi();
        playerWirebugStripPanel.retranslateUi();
        playerWeaponStripPanel.retranslateUi();
        monsterIdentityStripPanel.retranslateUi();
        monsterConditionsStripPanel.retranslateUi();
        damageShareStripPanel.retranslateUi();
        damageChartStripPanel.retranslateUi();
        damageRowsStripPanel.retranslateUi();
        playerInfoStripPanel.retranslateUi();
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
            snap = reader->poll();
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

        // Does PlayerPanel still own anything in this layout? In Split the
        // answer is currently no — every section is in a window of its own,
        // including the sharpness bar — and a panel with a zero mask would
        // otherwise mount as an empty shell, because PlayerPanel::
        // hasContent() is unconditionally true (it is a whole panel, not a
        // fragment) and nothing else in the loop consults the mask.
        //
        // Read from maskPlayerForPanel rather than recomputed: that IS the
        // mask playerPanel was handed at line 568, so "does it own
        // anything" and "what is it allowed to draw" cannot disagree.
        //
        // v0.16: `!= 0` became playerPanelHasBlocks(). PlayerSection has no
        // non-block bit, so the value is unchanged here — this panel was
        // already correct, by the accident of every bit being a block. The
        // spelling matters anyway: the clamp inside the predicate is what
        // makes the split's own default (0xFFFFFFFF, bits 9-31 set) answer
        // false, and a bare `!= 0` would answer true for it.
        const bool playerPanelHasBlocks = mhw::PlayerSection::playerPanelHasBlocks(
            maskPlayerForPanel);

        if (snap.player.valid || showAll) {
            playerPanel.setVisible(playerPanelHasBlocks);
            if (skipUpdate(playerPanel)) {
                playerPanel.triggerUpdate();
            } else {
                playerPanel.update(snap);
            }
        } else {
            // v0.1 behavior: keep player panel visible with a
            // "not connected" placeholder.
            playerPanel.update(snap);
            playerPanel.setVisible(playerPanelHasBlocks);
        }

        // v0.12: the split's four windows. Three independent conditions in
        // one expression, in the order that makes each one cheap to reason
        // about:
        //
        //   layout mode         — the switch. With it Single (default) the whole
        //                        block is skipped: no update(), no visibility
        //                        change, no mask read. PlayerPanel, which kept
        //                        every block this whole time, is the only
        //                        thing that ever drew them.
        //   panelEnabled()      — the master gate set at startup (== Split),
        //                        AND-ed here because the main loop is also
        //                        where a mask-driven visibility change lands.
        //   hasVisibleContent() — HunterPie's "no data, no window" rule: an
        //                        idle hunter (all bars at zero, both mantle
        //                        slots unequipped, every wirebug ready) collapses
        //                        the window rather than leaving a grabbable
        //                        empty frame.
        //   skipUpdate()        — the SAME edit-mode demo guard the four base
        //                        panels use. Without it, reader.poll() would
        //                        clobber the demo rows with an empty
        //                        snapshot and the preview would vanish.
        //
        // Note the ordering asymmetry with the base panels: none of the four
        // gets a "keep it visible with a placeholder" branch. A strip with
        // nothing to say has nothing to say — it hides, which is the whole
        // point of extracting it into its own window.
        //
        // Written as one line per window rather than a loop over the table,
        // deliberately: the table is a MASK table (it knows which bits leave
        // the panel) and these are typed, virtual, owned objects. A loop
        // would need a heterogeneous container and an per-object update()
        // override to do what four call sites do plainly.
        if (playerSplitOn) {
            const bool keepStripDemo = skipUpdate(playerStatusStripPanel);
            if (!keepStripDemo)
                playerStatusStripPanel.update(snap);
            playerStatusStripPanel.setVisible(
                playerStatusStripPanel.panelEnabled()
                && playerStatusStripPanel.hasVisibleContent());

            const bool keepBarsDemo = skipUpdate(playerBarsStripPanel);
            if (!keepBarsDemo)
                playerBarsStripPanel.update(snap);
            playerBarsStripPanel.setVisible(
                playerBarsStripPanel.panelEnabled()
                && playerBarsStripPanel.hasVisibleContent());

            const bool keepMantlesDemo = skipUpdate(playerMantlesStripPanel);
            if (!keepMantlesDemo)
                playerMantlesStripPanel.update(snap);
            playerMantlesStripPanel.setVisible(
                playerMantlesStripPanel.panelEnabled()
                && playerMantlesStripPanel.hasVisibleContent());

            const bool keepWirebugDemo = skipUpdate(playerWirebugStripPanel);
            if (!keepWirebugDemo)
                playerWirebugStripPanel.update(snap);
            playerWirebugStripPanel.setVisible(
                playerWirebugStripPanel.panelEnabled()
                && playerWirebugStripPanel.hasVisibleContent());

            // Same three-line shape as the four above. One difference
            // worth stating: this window has no per-game demo to retarget
            // (there is no setGameForDemo() call for it anywhere), and
            // that is a fact about its content rather than a gap in the
            // wiring — a weapon icon and a player name read the same in
            // World and in Rise, which is the one block in the panel that
            // is entirely game-agnostic.
            const bool keepWeaponDemo = skipUpdate(playerWeaponStripPanel);
            if (!keepWeaponDemo)
                playerWeaponStripPanel.update(snap);
            playerWeaponStripPanel.setVisible(
                playerWeaponStripPanel.panelEnabled()
                && playerWeaponStripPanel.hasVisibleContent());
            const bool keepInfoDemo = skipUpdate(playerInfoStripPanel);
            if (!keepInfoDemo)
                playerInfoStripPanel.update(snap);
            playerInfoStripPanel.setVisible(
                playerInfoStripPanel.panelEnabled()
                && playerInfoStripPanel.hasVisibleContent());
        }
        monsterPanel.setMultiplayer(snap.isMultiplayer);
        if (!snap.monsters.isEmpty() || showAll) {
            // v0.14: panelEnabled(), not a bare true. The monster panel
            // is the one mother panel that keeps a block in split mode
            // (MonsterSection::Tenderize, which cannot be re-parented
            // out of the .pc card), so it is a live surface in split —
            // but only when it has something to draw, and the master
            // gate above is what decides that. A bare `true` here would
            // ask it to mount regardless, which is how an empty frame
            // gets shown after the gate already said no. Set through
            // Panel::setVisible()'s existing "disabled panels never
            // mount" guard rather than a second condition, so the
            // decision stays in one place.
            //
            // v0.16: AND THAT v0.14 COMMENT IS WRONG. Tenderize is NOT a
            // block — panel_sections.h says so on the table's own comment
            // and panel_monster.cpp agrees: its six block gates
            // (onInfo / onHp / stamDrawn / rageDrawn / onAil / onParts)
            // all read false for it, because the 软化 countdown is a strip
            // INSIDE each .pc card and not a section of the panel. So in
            // Split mode the panel's mask is 0x20, which is non-zero,
            // which mounted it, and the height table then answered
            // 2*kPanelPad = 18px for a drawV03Chrome(Accent::Monster)
            // that paints the background, the accent stripe and the
            // border anyway. 18 pixels of floating frame is the one-line
            // window the user reported on both World and Rise:
            // 「hdmi 都有一个一行的悬浮窗」.
            //
            // The console showed nothing because it previews the eleven
            // sub-windows, not the mother panel — so the frame existed
            // only in a live run, which is why it was reported as
            // invisible to the console.
            //
            // monsterPanelHasBlocks() is the corrected test: it clamps to
            // MonsterSection::kAll and then subtracts the bits that
            // toggle something but draw nothing. The mask itself, the
            // panel's gates and every baseline are untouched — only the
            // mount decision changes.
            monsterPanel.setVisible(monsterPanel.panelEnabled()
                                    && mhw::monsterPanelHasBlocks(
                                           maskMonsterForPanel));
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

        // v0.12 third wave: the monster panel’s blocks — the SAME
        // selected monster the panel is showing, so a window and the
        // panel it was carved from can never disagree about which
        // monster’s HP bar is on screen.
        //
        // v0.13: TWO windows now, and each carries several blocks. The
        // Identity window draws portrait+HP+stamina+enrage; the
        // Conditions window draws ailments+parts. Neither receives a
        // per-block mask here — both receive `maskMonster` once at
        // startup and read their own bits at every consumer, which is
        // what lets the user close the enrage bar without closing the
        // monster.
        //
        // v0.17: this block used to sit INSIDE the
        // `if (!snap.monsters.isEmpty() || showAll)` gate above, inside
        // the `selected >= 0` branch inside that. Both guards are wrong
        // for a window: "the reader reports no monster" is precisely the
        // state a window must be told about, and an empty monster list
        // is what the player sees on the walk back to camp after a
        // quest. Skipping the block left each window holding the last
        // quest's cached model, so hasContent() kept answering true and
        // the surface stayed mounted showing the dead monster's HP
        // until the next quest replaced the snapshot — the reported
        // 「打完一把回到据点不清空」.
        //
        // So the block now runs on EVERY tick, and the no-monster case
        // is a first-class arm rather than an absence. The mother panel
        // is unaffected: its own gate already hides it, and in Split it
        // is disabled outright (Tenderize is not a block, v0.16).
        if (monsterSplitOn) {
            // -1 when there is no live monster: the list is empty, or
            // every entry in it is dead. Computed here rather than
            // borrowed from the branch above because that branch is
            // conditional and this one is not.
            const int selected = mhw::selectMonsterTarget(
                snap.monsters, displayedMonsterAddress);
            if (selected >= 0)
                displayedMonsterAddress = snap.monsters[selected].address;

            const bool keepIdentityDemo = skipUpdate(monsterIdentityStripPanel);
            if (keepIdentityDemo) {
                // Edit mode: the demo owns the window's state until the
                // mode is left, exactly as the four base panels do.
                monsterIdentityStripPanel.triggerUpdate();
            } else if (selected >= 0) {
                monsterIdentityStripPanel.update(snap.monsters[selected]);
            } else {
                monsterIdentityStripPanel.clear();
            }
            monsterIdentityStripPanel.setVisible(
                monsterIdentityStripPanel.panelEnabled()
                && monsterIdentityStripPanel.hasVisibleContent());

            const bool keepConditionsDemo =
                skipUpdate(monsterConditionsStripPanel);
            if (keepConditionsDemo) {
                monsterConditionsStripPanel.triggerUpdate();
            } else if (selected >= 0) {
                monsterConditionsStripPanel.update(snap.monsters[selected]);
            } else {
                monsterConditionsStripPanel.clear();
            }
            // v0.13: the parts grid's World-multiplayer suppression is a
            // per-session fact the builder reads, so the Conditions
            // window is told about it here — exactly as MonsterPanel
            // is told, above.
            monsterConditionsStripPanel.setMultiplayer(snap.isMultiplayer);
            monsterConditionsStripPanel.setVisible(
                monsterConditionsStripPanel.panelEnabled()
                && monsterConditionsStripPanel.hasVisibleContent());
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
                //
                // riseDamageReaders holds owning pointers (S2), so this
                // borrows the pointed-to readers. The slots are created
                // together in the isRise block above and never nulled, which
                // front() still relies on.
                const mhw::RiseDamageReader *diagnosticReader =
                    riseDamageReaders.front().get();
                for (auto &readerPtr : riseDamageReaders) {
                    auto &reader = *readerPtr;
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
            //
            // v0.16: AND the block question too, which hasVisibleContent()
            // cannot answer. In Split mode Rows | Share | Chart have all left
            // for their own windows (v0.15 moved Rows into
            // kDamageSplitTargets), so damagePanelMaskFor() hands this panel
            // 0x08 = OtherMembers — a live toggle that is the roster FILTER,
            // not a block. panel_damage.cpp never reads it: its gates are
            // onRows / onShare / onChart (:209-212) and nothing else. The
            // height table then answers kMargin + 14 + 9 + kMargin = 45 and
            // paintPanel draws the chrome, the 「伤害统计 DAMAGE」 title and
            // the quest timer, and nothing else — a frame whose only content
            // is its own title.
            //
            // damagePanelHasBlocks() is the corrected test. What it does NOT
            // do is switch the filter off: riseDisplayOptions.showOtherMembers
            // is derived from the USER's mask at :491-492, not from this
            // derived one, so the three split windows keep honouring the
            // toggle through the same damageSnap. Only the mount decision
            // changes; the masks and every baseline are untouched.
            damagePanel.setVisible(damagePanel.panelEnabled()
                                   && damagePanel.hasVisibleContent()
                                   && mhw::damagePanelHasBlocks(
                                          maskDamageForPanel));
            damagePanel.triggerUpdate();
            // Rise 与 World 判据不同是刻意的，不是同一逻辑写了两遍：
            // Rise 由 DamageViewModel 自答 hasData()（它维护 quest 生命周期
            // 状态，主循环代答会与 EpochReset 的时序打架）；World 的 feed 是
            // 单帧快照、没有跨帧状态，所以由主循环按刚投递的那份 party 裁决
            // ——visibleDamageParty 会把「其他成员」过滤掉，用未过滤的
            // snap.party 判可见会在过滤到空时留下一个空框。这是两套不同来源
            // 的数据，差异是真实语义，不该压成一个表达式。
            // Rise owns the companion surface. World still force-disables it
            // at construction and in its branch.
            petDamagePanel.setVisible(petDamagePanel.panelEnabled()
                                      && petDamagePanel.hasVisibleContent());
            petDamagePanel.triggerUpdate();
        } else {
            petDamagePanel.setVisible(false);
            // Always deliver empty/non-hunting snapshots too. DamagePanel owns
            // the hunt lifecycle; skipping these updates leaves the previous
            // chart and DPS tick counter alive into the next quest.
            //
            // Built before the two World branches so both of them gate
            // visibility on the very snapshot the panel was just handed —
            // the skipUpdate branch asks "did the user enable edit mode /
            // is there a real party?" and must not re-derive that from an
            // unfiltered roster the panel never saw.
            const mhw::GameSnapshot damageSnap = [&] {
                mhw::GameSnapshot filtered = snap;
                filtered.party = mhw::visibleDamageParty(
                    snap.party, riseDamageDisplayOptions.showOtherMembers);
                return filtered;
            }();
            if (skipUpdate(damagePanel)) {
                damagePanel.triggerUpdate();
                damagePanel.setVisible(damagePanel.panelEnabled()
                                       && (!damageSnap.party.isEmpty() || showAll)
                                       && mhw::damagePanelHasBlocks(
                                              maskDamageForPanel));
            } else {
                damagePanel.update(damageSnap);
                damagePanel.setVisible(damagePanel.panelEnabled()
                                       && (!damageSnap.party.isEmpty() || showAll)
                                       && mhw::damagePanelHasBlocks(
                                              maskDamageForPanel));
            }

        // v0.12 third wave: the damage panel's two width-hungry blocks —
        // the share bar and the line chart. They take the same party
        // snapshot the panel takes, so a row that appears in one appears
        // in both. Rows stays with the panel deliberately: it is the DPS
        // panel's substance, and a panel left holding only a title row is
        // the "is this panel still worth having" question the player split
        // already answered once.
        if (damageSplitOn) {
            const bool keepShareDemo = skipUpdate(damageShareStripPanel);
            if (!keepShareDemo)
                damageShareStripPanel.update(damageSnap);
            damageShareStripPanel.setVisible(
                damageShareStripPanel.panelEnabled()
                && damageShareStripPanel.hasVisibleContent());

            const bool keepChartDemo = skipUpdate(damageChartStripPanel);
            if (!keepChartDemo)
                damageChartStripPanel.update(damageSnap);
            damageChartStripPanel.setVisible(
                damageChartStripPanel.panelEnabled()
                && damageChartStripPanel.hasVisibleContent());

            // v0.15: the roster window's drive. Without this it is
            // constructed, masked, and never shown - and because
            // damagePanelMaskFor() has already taken Rows AWAY from
            // DamagePanel, "never shown" is not "the panel still draws
            // it": in Split mode the per-player roster was simply gone
            // from the screen. Constructing a window is not driving one.
            const bool keepRowsDemo = skipUpdate(damageRowsStripPanel);
            if (!keepRowsDemo)
                damageRowsStripPanel.update(damageSnap);
            damageRowsStripPanel.setVisible(
                damageRowsStripPanel.panelEnabled()
                && damageRowsStripPanel.hasVisibleContent());
        }
        }
    });
    timer.start(pollMs);  // consistent poll rate regardless of mode

    // v0.15: EDIT MODE TAKES KEYBOARD FOCUS, ONCE.
    //
    // The report was 「使用 overlay 的命令启动,现在完全无法使用,键盘输入被
    // 转译成 ^[[1;2B…」 with the arrow keys in the escape sequences. The
    // arrow-key handler was never the problem - Panel::keyPressEvent has
    // dispatched Left/Right/Up/Down since v0.5 - the events never arrived.
    // The surface was eligible (KeyboardInteractivityOnDemand via
    // setEditMode) but never focused, and the only requestActivate() in the
    // codebase was inside mousePressEvent: click a panel first, or your keys
    // go to the terminal. WA_ShowWithoutActivating, which the live HUD
    // needs so it never raises itself over the game, guarantees show() alone
    // will not do it either. Esc failed the same way, invisibly.
    //
    // This call is the FIRST ask and it is deliberately not the only one.
    // Measured on the live session: requestActivate() before app.exec() is a
    // no-op, because the layer-shell surface is not exposed yet - exposed=0,
    // hasFocus=0. Deferring it into the event loop is better but was not
    // reliable either, twice on one session with opposite results and
    // isExposed() flipping between runs. So the actual request lives in
    // Panel::showEvent (requestEditFocusOnShow), which is the one moment the
    // mapping is guaranteed done.
    //
    // ONE window, not fifteen: fifteen simultaneous activation requests are
    // a contest the compositor has no reason to resolve in the user's
    // favour, and the loser is whichever window was being moved. Every panel
    // still runs its own showEvent, so each asks for itself when it appears;
    // this line only makes the FIRST one ask before the loop starts, which
    // matters for a panel that was already visible from a previous mode.
    if (editMode) {
        Panel *focusCandidates[] = {&playerPanel, &monsterPanel,
                                    &damagePanel, &petDamagePanel};
        for (Panel *p : focusCandidates) {
            if (p->isVisible()) {
                p->requestEditFocus();
                break;
            }
        }
    }

    const int code = app.exec();

    if (editMode) {
        playerPanel.saveConfig();
        monsterPanel.saveConfig();
        damagePanel.saveConfig();
        petDamagePanel.saveConfig();
        // v0.12: persist the four windows' own position / scale / opacity.
        // Each has its own settingsKey ("player-status" / "player-bars" /
        // "player-mantles" / "player-wirebug") — reusing "player" would
        // make two windows fight over one position / scale / opacity
        // section — so each writes a group that did not exist before and
        // cannot touch the four existing sections. Calling it on the
        // default path is harmless (it writes the panel's un-moved
        // defaults to a new group nobody reads). Guarded on the switch
        // anyway to keep the default path free of new writes to
        // panels.ini, which is the same reason --output-* uses persist=false.
        //
        // Only the switch is gated, NOT hasVisibleContent(): a window that
        // was never shown (a mantle window in a Rise session) still has a
        // draggable position the user never touched, and writing it makes
        // the next launch find the same numbers the previous one saved.
        if (playerSplitOn) {
            playerStatusStripPanel.saveConfig();
            playerBarsStripPanel.saveConfig();
            playerMantlesStripPanel.saveConfig();
            playerWirebugStripPanel.saveConfig();
            playerWeaponStripPanel.saveConfig();
            playerInfoStripPanel.saveConfig();
            monsterIdentityStripPanel.saveConfig();
            monsterConditionsStripPanel.saveConfig();
            damageShareStripPanel.saveConfig();
            damageRowsStripPanel.saveConfig();
            damageChartStripPanel.saveConfig();
        }
    }

    return code;
}