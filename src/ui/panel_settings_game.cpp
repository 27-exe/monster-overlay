// SPDX-License-Identifier: Apache-2.0

#include "ui/panel_settings_game.h"

namespace mhw {
namespace {

// Set by main() before the first Panel is constructed. Defaults to
// World, which is the game the un-prefixed legacy sections belong to
// (see migrateLegacyPanelSection).
GameId g_panelSettingsGame = GameId::World;

// Set by main() alongside the game, BEFORE the first Panel exists.
// Defaults to Single for the same reason and with the same argument:
// a panel built while this held the wrong mode read — and on first save,
// wrote — the other mode's group. Single is the default because it is
// the mode every pre-v0.14 user was in, so an unset scope resolves to
// the namespace their layout is already stored in.
PlayerLayoutMode g_panelSettingsLayout = PlayerLayoutMode::Single;

// The key that declares "this section was already migrated". Written
// into the migrated group so a second process — or a second panel in
// the same process — cannot migrate it again.
//
// At the GAME level, not inside the panel's own section: the marker is
// migration bookkeeping, not panel state, and panels.ini is a file the
// user is invited to hand-edit. A key inside [world/single/player]
// would be one more line a reader has to know to skip.
//
// It carries the GAME prefix only, never the mode: the marker answers
// "has the flat spelling been folded in for this game", which is one
// question per game rather than one per (game, mode) pair. Writing one
// marker per mode would let a Single-mode run migrate the whole flat
// file (only the sections it constructed) and a later Split-mode run
// find its own marker missing — re-copying a section the previous run
// had already removed, which reads as "the migration undid itself".
QString legacyMarkerKey(GameId game, const QString &settingsKey)
{
    return panelSettingsGameGroup(game) + QStringLiteral("/legacy/") + settingsKey;
}

// Read the marker back. Written as `legacy/<key>` = 1, so it lands as a
// single KEY under [world] rather than a group — `[world/legacy/player]`
// with no trailing slash is a key named "player" inside group "world/
// legacy", which is why the read below is spelled with the group prefix
// and the bare key rather than as one absolute path.
//
// The probe that settled it (and the reason this helper exists rather
// than a literal at the call site): `contains("world/legacy/player/
// legacyMigrated")` is FALSE on the file the migration actually writes,
// and so is `contains("world/legacy/player")` — the value is under
// `world/legacy/player`, full stop. Asking the wrong question here
// would leave the idempotence guard permanently false.
bool legacyMarkerIsSet(const QSettings &settings, GameId game,
                       const QString &settingsKey)
{
    return settings.contains(legacyMarkerKey(game, settingsKey));
}

// Every legacy spelling a panels.ini can still be holding this window's
// section under, in the order they were written in — oldest first.
//
// v0.12 and earlier: ONE flat namespace, `[player]`.
// v0.13             : one domain per game, `[world/player]` and
//                     `[rise/player]`.
// v0.14 and later   : one domain per (game, mode),
//                     `[world/single/player]` etc.
//
// The function walks this list and folds the FIRST spelling it finds
// into the current scope. Finding one and migrating is the whole job;
// a second spelling for the same window in the same file is a file
// nobody produces, and guessing which of them the user meant is worse
// than leaving both alone for the user to resolve by hand.
//
// Why oldest-first: the flat spelling is the one that has been
// migratable since v0.13, so a user who has been running this overlay
// since before then has already had it folded into `[world/player]`.
// A v0.13-spelling section is therefore the LIVE one for anyone who
// ran v0.13, and it is what a v0.14 upgrade has to honour.
QStringList legacySpellings(GameId game, const QString &settingsKey)
{
    return {settingsKey,
            panelSettingsGameGroup(game) + QLatin1Char('/') + settingsKey};
}

} // namespace

QString panelSettingsGameGroup(GameId game)
{
    switch (game) {
    // A switch, not gameIdToString() inlined: a third GameId has to be
    // given an arm here explicitly rather than silently inheriting this
    // one. The two arms are value-identical to gameIdToString() today,
    // and the switch is what makes adding a game a compile-visible
    // decision instead of a string that happens to work.
    case GameId::World:
        return QStringLiteral("world");
    case GameId::Rise:
        return QStringLiteral("rise");
    }
    // Unreachable for a valid GameId. Returning World here would make a
    // third enumerator resolve silently, which is the failure the switch
    // exists to prevent; an empty group makes the prefix a visible
    // no-op rather than a plausible-looking wrong namespace.
    return QString();
}

QString panelLayoutModeSegment(PlayerLayoutMode mode)
{
    // The same two spellings parsePlayerLayoutMode() accepts, so a
    // hand-edited `[world/split/...]` and what the code writes can
    // never desynchronise.
    return playerLayoutModeToString(mode);
}

QString panelSettingsGroupFor(GameId game, PlayerLayoutMode mode,
                              const QString &settingsKey)
{
    return panelSettingsScopeFor(game, mode, settingsKey);
}

void setPanelSettingsGame(GameId game)
{
    g_panelSettingsGame = game;
}

GameId panelSettingsGame()
{
    return g_panelSettingsGame;
}

void setPanelSettingsLayout(PlayerLayoutMode mode)
{
    g_panelSettingsLayout = mode;
}

PlayerLayoutMode panelSettingsLayout()
{
    return g_panelSettingsLayout;
}

QString panelSettingsScopeFor(GameId game, PlayerLayoutMode mode,
                              const QString &settingsKey)
{
    return panelSettingsGameGroup(game) + QLatin1Char('/')
           + panelLayoutModeSegment(mode) + QLatin1Char('/') + settingsKey;
}

QString panelSettingsScopeGroup(const QString &settingsKey)
{
    return panelSettingsScopeFor(g_panelSettingsGame, g_panelSettingsLayout,
                                 settingsKey);
}

QString panelSettingsScopePrefix()
{
    return panelSettingsGameGroup(g_panelSettingsGame) + QLatin1Char('/')
           + panelLayoutModeSegment(g_panelSettingsLayout);
}

bool migrateLegacyPanelSection(QSettings &settings,
                               const PanelSettingsScope &scope,
                               const QString &settingsKey)
{
    const QString group = panelSettingsScopeFor(scope.game, scope.mode,
                                                settingsKey);

    // Does any key live under `group/`? This is the group-existence
    // question and it needs spelling carefully, because QSettings has no
    // hasGroup() and both of the obvious answers are wrong:
    //
    //   * childGroups() answers ONLY the top level — ["world", "rise"] —
    //     so `childGroups().contains("world/split/player")` is always
    //     false. A guard written that way never fires.
    //   * contains("world/split/player/") is also always false: Qt 6.11
    //     does not treat a trailing slash as a group prefix on a path
    //     that itself contains one.
    //
    // allKeys() flattened-prefix matching is the spelling that is right
    // for both a nested and a top-level group, which is what keeps the
    // guard honest for "world/single/player" as well as for "world".
    const QString groupPrefix = group + QLatin1Char('/');
    const bool groupExists = [&] {
        const QStringList keys = settings.allKeys();
        for (const QString &k : keys) {
            if (k.startsWith(groupPrefix))
                return true;
        }
        return false;
    }();

    // Already scoped: nothing to migrate, whatever the legacy section
    // holds. This guard is what makes the migration safe on the second
    // and later runs, and it is checked BEFORE the legacy section is
    // read at all so an already-migrated scope can never re-import one.
    //
    // This is the guarantee a whole-file rewrite would break: it would
    // copy [player] over an existing [world/single/player] and silently
    // destroy the layout the user edited for this mode since
    // migrating.
    //
    // THE MARKER IS THE OTHER HALF of the same guarantee, and it is the
    // half that matters once a section has been migrated AND the user
    // has since deleted every key in it. `groupExists` answers "is
    // there anything here now"; the marker answers "has this scope
    // already been folded in". Ask only the first and a user who
    // cleaned out a migrated section by hand gets it silently
    // re-created from a legacy spelling that is still on disk. Both are
    // asked, and either one is enough to say no.
    //
    // The marker is scoped to the GAME, not the (game, mode) domain:
    // both modes of one game descend from the same legacy section, and
    // a marker per mode would let a single-mode run migrate the file
    // (removing the two-domain spelling in the process) while a later
    // split-mode run found its own marker absent and re-imported
    // sections that no longer exist — reading as "the migration undid
    // itself".
    const bool alreadyMigrated = legacyMarkerIsSet(settings, scope.game,
                                                  settingsKey);
    if (groupExists || alreadyMigrated)
        return false;

    // WHY ONLY SINGLE MIGRATES, and why this is the first thing decided.
    //
    // Both legacy spellings predate the split: v0.12 and earlier had one
    // flat namespace, and v0.13 added the game but no mode. Every legacy
    // section is therefore a SINGLE-mode section, and its only honest
    // destination is `[<game>/single/<key>]`.
    //
    // The alternative — letting a split-mode run migrate too — loses the
    // user's layout in a way no second run can undo, and the mechanism is
    // worth spelling: in split mode the four MOTHER panels are still
    // constructed (they are re-parented, not removed), so their
    // loadConfig() runs and would fold `[world/player]` into
    // `[world/split/player]` and then delete the source. The next
    // single-mode run finds nothing to migrate and gives PlayerPanel
    // factory defaults — the position the user had dragged the panel to
    // is gone, and the only trace is a `[world/split/player]` nobody
    // reads.
    //
    // AND THE SPLIT WINDOWS have no legacy section to migrate at all:
    // they did not exist before v0.12, so their first run reading
    // defaults is the correct answer, not a gap. That is the same
    // principle the console's dirty-tracking rests on — never fill a
    // domain with defaults the user never chose, so the user can still
    // tell "default" from "I left it here".
    if (scope.mode != PlayerLayoutMode::Single)
        return false;

    // The bare section. It is a TOP-LEVEL group, so childGroups() is the
    // right question here — and the asymmetry with the check above is
    // deliberate rather than sloppy: the legacy spelling is flat, the
    // new one is nested, so one is a top-level name and the other a key
    // path.
    //
    // Not any more: since v0.14 there are TWO legacy spellings, and the
    // second one IS nested, so childGroups() alone cannot find it. The
    // probe that settles how to look for both (tests/panel_settings_
    // scope_tests documents the same finding): a `[world/player]`
    // section appears in allKeys() as "world/player/*", it appears in
    // beginGroup("world")->childGroups() as "player", and it does NOT
    // appear in top-level childGroups() at all. So a nested legacy
    // spelling is reachable through the SAME allKeys() prefix match the
    // group-existence guard above uses — and asking each candidate
    // spelling for its prefix is the one question that answers for both.
    //
    // The keys are read with an ABSOLUTE path (spelling + "/" + key),
    // never through a beginGroup() of the spelling itself: a nested
    // beginGroup would address the literal flattened path
    // `player/world/single/...` and the copy would land nowhere.
    const QStringList spellings = legacySpellings(scope.game, settingsKey);
    QStringList contents;
    QString sourceSpelling;
    for (const QString &spelling : spellings) {
        const QString prefix = spelling + QLatin1Char('/');
        const QStringList keys = settings.allKeys();
        for (const QString &k : keys) {
            if (k.startsWith(prefix)) {
                QStringList inGroup;
                for (const QString &j : keys) {
                    if (j.startsWith(prefix))
                        inGroup.append(j.mid(prefix.size()));
                }
                contents = inGroup;
                sourceSpelling = spelling;
                break;
            }
        }
        if (!sourceSpelling.isEmpty())
            break;
    }
    if (contents.isEmpty())
        return false;   // no legacy section is not a migration

    // Copy verbatim into the current scope's namespace.
    //
    // NOT a nested beginGroup(settingsKey) + beginGroup(group): QSettings
    // flattens the group path, so begging group "player" and then
    // "world/single/player" addresses the literal key path
    // player/world/single/player and the copy lands nowhere. Each key is
    // read from the source group with an absolute key (spelling + "/" +
    // key) and written the same way, which is the one spelling that
    // cannot nest by accident.
    for (const QString &k : contents) {
        const QVariant v = settings.value(sourceSpelling + QLatin1Char('/') + k);
        settings.setValue(group + QLatin1Char('/') + k, v);
    }

    // The marker, so nothing migrates this section twice.
    //
    // Written so legacyMarkerIsSet() can read it back with plain
    // contains(): `legacy/<key>` under [<game>] is a KEY, not a group,
    // which is why it must not be spelled `legacy/<key>/legacyMigrated`
    // — that form asks for a group the migration never created and
    // would leave the idempotence guard permanently false on EVERY
    // run, re-copying (and re-removing) a section the user has edited.
    settings.setValue(legacyMarkerKey(scope.game, settingsKey), 1);

    // Drop the legacy section. This is the step that makes the migration
    // observable, and it is why it must be last: the copy above has to
    // have completed before the original stops existing.
    //
    // Only the CURRENT GAME's section is ever removed. The other game's
    // sections are never touched — not read, not rewritten, not removed
    // — so a user who has already run both games keeps both layouts.
    //
    // remove() on a flat spelling drops a top-level group; remove() on
    // "world/player" drops that group from inside [world] without
    // touching [world/single/...] (the probe asserts both), which is
    // what makes the two-domain fold a one-way door too.
    settings.remove(sourceSpelling);
    settings.sync();

    return true;
}

// ─── the Mixed scope seed ─────────────────────────────────────────────
//
// The guarantees are spelled out at the declaration
// (ui/panel_settings_game.h); what follows is only the mechanism.

bool ensureMixedPanelSettingsScope(QSettings &settings, GameId game,
                                   PlayerSection::PlayerLayoutMode fromMode)
{
    // Already there. There is no source domain to copy FROM, and a
    // Mixed -> Mixed call is what a second click on the same combo entry
    // produces. No read, no write.
    if (fromMode == PlayerSection::PlayerLayoutMode::Mixed)
        return false;

    // panelSettingsScopeFor() prefixes whatever key it is handed, so an
    // EMPTY key is the domain prefix itself, trailing '/' included — the
    // same accessor every window spells its group through, never a
    // second spelling of the namespace.
    const QString srcPrefix = panelSettingsScopeFor(game, fromMode, QString());
    const QString dstPrefix = panelSettingsScopeFor(
        game, PlayerSection::PlayerLayoutMode::Mixed, QString());

    // A COPY of the key list, not a view: setValue() below mutates the
    // store the list came from, and QSettings' key enumeration is not a
    // snapshot a mutating loop may rely on.
    const QStringList keys = settings.allKeys();

    bool seeded = false;
    for (const QString &full : keys) {
        if (!full.startsWith(srcPrefix))
            continue;                     // another mode, or another game
        const QString tail = full.mid(srcPrefix.size());
        if (tail.isEmpty())
            continue;                     // the prefix is not itself a key
        const QString dst = dstPrefix + tail;
        if (settings.contains(dst))
            continue;                     // THE rule: an existing Mixed
                                          // value is authoritative
        settings.setValue(dst, settings.value(full));
        seeded = true;
    }
    return seeded;
}

void ensureMixedPanelSettingsScope(GameId game,
                                   PlayerSection::PlayerLayoutMode fromMode)
{
    // The same (format, scope, organization, application) tuple Panel
    // opens, so this resolves to the same panels.ini — no second store
    // for the same windows.
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("monster-overlay"),
                       QStringLiteral("panels"));
    // QSettings caches the file at construction; sync() before reading
    // so a console process that just saved a layout is the state being
    // seeded from, not a snapshot from process start.
    settings.sync();
    const bool seeded =
        ensureMixedPanelSettingsScope(settings, game, fromMode);
    // And a sync on the way out, so the domain a Panel constructs a
    // millisecond later opens the SEEDED file rather than one whose
    // write is still queued for the destructor.
    if (seeded)
        settings.sync();
}

} // namespace mhw
