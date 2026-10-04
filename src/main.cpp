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
#include "ui/panel_player_split_info.h"
#include "ui/panel_player_split_sharpness.h"
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
    const mhw::PanelMaskConfRow confMaskPlayer  = {confMasks.playerValid,  confMasks.player};
    const mhw::PanelMaskConfRow confMaskMonster = {confMasks.monsterValid, confMasks.monster};
    const mhw::PanelMaskConfRow confMaskDamage  = {confMasks.damageValid,  confMasks.damage};
    const mhw::PanelMaskConfRow confMaskPets    = {confMasks.petsValid,    confMasks.pets};

    const uint32_t maskPlayer  = mhw::resolveStartupPanelMask(
                                     cliMaskPlayer,  confMaskPlayer,
                                     mhw::PanelMaskSlot::Player).value;
    const uint32_t maskMonster = mhw::resolveStartupPanelMask(
                                     cliMaskMonster, confMaskMonster,
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
        mhw::resolveStartupLayoutMode(
            mhw::PlayerLayoutCliRequest{
                parser.isSet(layoutOption), parser.value(layoutOption),
                mhw::splitPlayerStripFlagSense(parser.isSet(splitPlayerStripOption))},
            mhw::PlayerLayoutConfRow{mhw::PlayerLayoutSource::Default});
    // Single is the default and the pixel-identity guarantee: it resolves
    // maskPlayerForPanel to exactly maskPlayer, so every pre-v0.12 user gets
    // one big window and the baselines stay byte-identical.
    const mhw::PlayerSection::PlayerLayoutMode playerSplitLayoutMode =
        playerSplitLayout.mode;
    const uint32_t maskPlayerForPanel = mhw::PlayerSection::playerPanelMaskFor(
        playerSplitLayoutMode, maskPlayer);
    if (playerSplitLayoutMode == mhw::PlayerSection::PlayerLayoutMode::Split) {
        qInfo("v0.12 split: layout=split; %d player sub-window(s); player "
              "mask %08x -> %08x",
              mhw::PlayerSection::kPlayerSplitTargetCount, maskPlayer,
              maskPlayerForPanel);
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
    PlayerStatusStripPanel playerStatusStripPanel;
    PlayerBarsStripPanel playerBarsStripPanel;
    PlayerMantlesStripPanel playerMantlesStripPanel;
    PlayerWirebugStripPanel playerWirebugStripPanel;
    PlayerWeaponStripPanel playerWeaponStripPanel;
    PlayerInfoStripPanel playerInfoStripPanel;
    PlayerSharpnessStripPanel playerSharpnessStripPanel;

    playerPanel.setEditMode(editMode);
    monsterPanel.setEditMode(editMode);
    damagePanel.setEditMode(editMode);
    petDamagePanel.setEditMode(editMode);
    playerStatusStripPanel.setEditMode(editMode);
    playerBarsStripPanel.setEditMode(editMode);
    playerMantlesStripPanel.setEditMode(editMode);
    playerWirebugStripPanel.setEditMode(editMode);
    playerWeaponStripPanel.setEditMode(editMode);
    playerInfoStripPanel.setEditMode(editMode);
    playerSharpnessStripPanel.setEditMode(editMode);

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
    monsterPanel.setSectionMask(maskMonster);
    damagePanel.setSectionMask(maskDamage);
    petDamagePanel.setSectionMask(maskPets);
    playerStatusStripPanel.setSectionMask(maskPlayer);
    playerBarsStripPanel.setSectionMask(maskPlayer);
    playerMantlesStripPanel.setSectionMask(maskPlayer);
    playerWirebugStripPanel.setSectionMask(maskPlayer);
    playerWeaponStripPanel.setSectionMask(maskPlayer);
    playerInfoStripPanel.setSectionMask(maskPlayer);
    playerSharpnessStripPanel.setSectionMask(maskPlayer);
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
    playerStatusStripPanel.setPanelEnabled(playerSplitOn);
    playerBarsStripPanel.setPanelEnabled(playerSplitOn);
    playerMantlesStripPanel.setPanelEnabled(playerSplitOn);
    playerWirebugStripPanel.setPanelEnabled(playerSplitOn);
    playerWeaponStripPanel.setPanelEnabled(playerSplitOn);
    playerInfoStripPanel.setPanelEnabled(playerSplitOn);
    playerSharpnessStripPanel.setPanelEnabled(playerSplitOn);


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
    //     per-object call at all; the strip's setGameForDemo() is the extra
    //     step it needs because its demo DOES vary by game.
    if (playerSplitOn)
        playerStatusStripPanel.setGameForDemo(gameId);

    playerPanel.show();
    monsterPanel.show();
    damagePanel.show();
    // Never flash an empty fourth frame at startup. In edit mode Rise can
    // show its demo immediately; live mode waits for a visible pet row.
    petDamagePanel.setVisible(isRise && editMode);
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
        playerSharpnessStripPanel.setVisible(editMode);
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
        playerInfoStripPanel.retranslateUi();
        playerSharpnessStripPanel.retranslateUi();
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
        const bool playerPanelHasBlocks = maskPlayerForPanel != 0;

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
            const bool keepSharpnessDemo = skipUpdate(playerSharpnessStripPanel);
            if (!keepSharpnessDemo)
                playerSharpnessStripPanel.update(snap);
            playerSharpnessStripPanel.setVisible(
                playerSharpnessStripPanel.panelEnabled()
                && playerSharpnessStripPanel.hasVisibleContent());
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
            damagePanel.setVisible(damagePanel.panelEnabled()
                                   && damagePanel.hasVisibleContent());
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
                damagePanel.setVisible(!damageSnap.party.isEmpty() || showAll);
            } else {
                damagePanel.update(damageSnap);
                damagePanel.setVisible(!damageSnap.party.isEmpty() || showAll);
            }
        }
    });
    timer.start(pollMs);  // consistent poll rate regardless of mode

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
            playerSharpnessStripPanel.saveConfig();
        }
    }

    return code;
}
