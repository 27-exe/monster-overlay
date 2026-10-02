#pragma once

#include "core/game_snapshot.h"
#include <QPixmap>
#include <QSize>
#include <QString>

namespace mhw {

// Loads SVG icons from Qt resources (:/icons/...) and renders them to
// QPixmap at any size with an optional tint color.
class Icon {
public:
    // Load and cache an icon by resource path (e.g. ":/icons/crowns/crown_king.svg").
    // Returns false if the resource doesn't exist.
    static bool load(const QString &path);

    // Render a cached icon at the given size. If `tint` is valid (non-null
    // color), the icon is recolored using SourceIn composition.
    static QPixmap render(const QString &path, int size,
                          const QColor &tint = QColor());

    static QPixmap renderRect(const QString &path, const QSize &target,
                              const QColor &tint = QColor());

    // Convenience: weapon icon path from HunterPie weapon enum ID (0..13).
    // rank is the upgrade tier (1..12); higher rank = shinier icon.
    // Returns an EMPTY string for every unresolved id (<0 or >13) — the
    // caller must treat that as "no weapon data", never as a path.
    static QString weaponPath(int weaponId, int rank);

    // Explicit placeholder for a weapon slot whose weaponId did not
    // resolve. There is no unknown-weapon asset in the qrc (see
    // assets/icons.qrc — the only Unknown.* file is a monster portrait,
    // :/icons/Monsters/Unknown.png), so the marker is drawn procedurally:
    // a muted grey "?" centred in a transparent `size`×`size` pixmap.
    // Its colour is deliberately dimmer than any of the 14 weapon SVGs so
    // the placeholder can never be mistaken for a real weapon glyph.
    static QPixmap renderUnknownWeapon(int size);

    // Mantle icon path from the in-game item ID.
    static QString mantlePath(int itemId);

    // Monster head icon path from the in-game monster ID (e.g. 1 = Rathian).
    // Falls back to Unknown.png when the ID has no matching icon file.
    static QString monsterPath(int monsterId, GameId game = GameId::World);
};

} // namespace mhw