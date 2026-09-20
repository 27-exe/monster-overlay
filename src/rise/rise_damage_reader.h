#pragma once

#include "rise/rise_damage_types.h"

#include <QString>

namespace mhw {

// Reads actor damage data written by the REFramework Lua script to
// /tmp/mhr_damage.json. Protocol v2 carries stable actor identities; legacy
// v1 player-only documents are converted to the same shared snapshot.
// Poll-friendly: call update() each tick; returns false if the file is
// missing, outside the +/-5 second freshness window, or unparseable.
class RiseDamageReader {
public:
    explicit RiseDamageReader(QString path = QStringLiteral("/tmp/mhr_damage.json"));

    // Re-read the JSON file. Returns true if data was parsed successfully.
    bool update();

    [[nodiscard]] const RiseDamageSnapshot &snapshot() const { return snapshot_; }

private:
    QString path_;
    RiseDamageSnapshot snapshot_;
};

} // namespace mhw
