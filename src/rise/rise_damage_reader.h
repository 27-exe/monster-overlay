#pragma once

#include "rise/rise_damage_types.h"

#include <QString>

namespace mhw {

// Reads one actor-damage JSON document written by the REFramework Lua script.
// Production uses alternating files under reframework/data; the default
// /tmp path remains only for compatibility with legacy producers. Protocol v2
// carries stable actor identities; legacy
// v1 player-only documents are converted to the same shared snapshot.
// Poll-friendly: call update() each tick; returns false if the file is
// missing, outside the +/-5 second freshness window, or unparseable.
class RiseDamageReader {
public:
    enum class Error {
        None,
        Missing,
        OpenDeniedOrFailed,
        Symlink,
        TooLarge,
        Parse,
        UnsupportedVersion,
        Stale,
    };

    static constexpr qint64 kMaximumFeedBytes = 1024 * 1024;

    explicit RiseDamageReader(QString path = QStringLiteral("/tmp/mhr_damage.json"));

    // Re-read the JSON file. Returns true if data was parsed successfully.
    bool update();

    [[nodiscard]] const RiseDamageSnapshot &snapshot() const { return snapshot_; }
    [[nodiscard]] const QString &path() const { return path_; }
    [[nodiscard]] Error lastError() const { return lastError_; }
    [[nodiscard]] const QString &lastErrorText() const { return lastErrorText_; }

private:
    bool fail(Error error, QString text);

    QString path_;
    RiseDamageSnapshot snapshot_;
    Error lastError_ = Error::None;
    QString lastErrorText_;
};

} // namespace mhw
