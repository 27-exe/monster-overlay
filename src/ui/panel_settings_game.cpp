// SPDX-License-Identifier: Apache-2.0

#include "ui/panel_settings_game.h"

namespace mhw {

namespace {

// Set by main() before the first Panel is constructed. Defaults to
// World, which is the game the un-prefixed legacy sections belong to
// (see migrateLegacyPanelSection).
GameId g_panelSettingsGame = GameId::World;

// The key that declares "this section was already migrated". Written
// into the migrated group so a second process — or a second panel in the
// same process — cannot migrate it again.
//
// At the GAME level, not inside the panel's own section: the marker is
// migration bookkeeping, not panel state, and panels.ini is a file the
// user is invited to hand-edit. A key inside [world/player] would be one
// more line a reader has to know to skip.
QString legacyMarkerKey(GameId game, const QString &settingsKey)
{
    return panelSettingsGameGroup(game) + QStringLiteral("/legacy/") + settingsKey;
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

QString panelSettingsGroupFor(GameId game, const QString &settingsKey)
{
    const QString prefix = panelSettingsGameGroup(game);
    if (prefix.isEmpty())
        return settingsKey;
    return prefix + QLatin1Char('/') + settingsKey;
}

void setPanelSettingsGame(GameId game)
{
    g_panelSettingsGame = game;
}

GameId panelSettingsGame()
{
    return g_panelSettingsGame;
}

bool migrateLegacyPanelSection(QSettings &settings, GameId game,
                               const QString &settingsKey)
{
    const QString group = panelSettingsGroupFor(game, settingsKey);

    // Does any key live under `group/`? This is the group-existence
    // question and it needs spelling carefully, because QSettings has no
    // hasGroup() and both of the obvious answers are wrong:
    //
    //   * childGroups() answers ONLY the top level — ["world", "rise"] —
    //     so `childGroups().contains("world/player")` is always false. A
    //     guard written that way never fires.
    //   * contains("world/player/") is also always false: Qt 6.11 does
    //     not treat a trailing slash as a group prefix on a path that
    //     itself contains one.
    //
    // allKeys() flattened-prefix matching is the spelling that is right
    // for both a nested and a top-level group, which is what keeps the
    // guard honest for "world/player" as well as for "world".
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
    // and later runs, and it is checked BEFORE the bare section is read
    // at all so an already-migrated game can never re-import one.
    //
    // This is the guarantee a whole-file rewrite would break: it would
    // copy [player] over an existing [world/player] and silently destroy
    // the layout the user edited for this game since migrating.
    if (groupExists)
        return false;

    // The bare section. It is a TOP-LEVEL group, so childGroups() is the
    // right question here — and the asymmetry with the check above is
    // deliberate rather than sloppy: the legacy spelling is flat, the
    // new one is nested, so one is a top-level name and the other a key
    // path.
    if (!settings.childGroups().contains(settingsKey, Qt::CaseSensitive))
        return false;

    // `allKeys()` inside the group: the enumeration that sees the
    // section's keys however they are flattened on disk.
    const QStringList contents = [&] {
        settings.beginGroup(settingsKey);
        const QStringList keys = settings.allKeys();
        settings.endGroup();
        return keys;
    }();
    if (contents.isEmpty())
        return false;   // an empty legacy section is not a migration

    // Copy verbatim into the current game's namespace.
    //
    // NOT a nested beginGroup(settingsKey) + beginGroup(group): QSettings
    // flattens the group path, so begging group "player" and then
    // "world/player" addresses the literal key path player/world/player
    // and the copy lands nowhere. Each key is read from the source group
    // with an absolute key (group + "/" + key) and written the same way,
    // which is the one spelling that cannot nest by accident.
    for (const QString &k : contents) {
        const QVariant v = settings.value(settingsKey + QLatin1Char('/') + k);
        settings.setValue(group + QLatin1Char('/') + k, v);
    }

    // The marker, so nothing migrates this section twice.
    settings.setValue(legacyMarkerKey(game, settingsKey), 1);

    // Drop the bare section. This is the step that makes the migration
    // observable, and it is why it must be last: the copy above has to
    // have completed before the original stops existing.
    //
    // Only the CURRENT game's section is ever removed. The other game's
    // sections are never touched — not read, not rewritten, not removed
    // — so a user who has already run both games keeps both layouts.
    settings.remove(settingsKey);
    settings.sync();

    return true;
}

} // namespace mhw
