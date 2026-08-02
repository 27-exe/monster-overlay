#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

namespace mhw {

// Reads per-player damage data written by the REFramework Lua script
// (reframework/mhr-overlay-damage.lua) to /tmp/mhr_damage.json.
// Poll-friendly: call update() each tick; returns false if the file
// is missing, stale (>5s old), or unparseable.
struct RiseDamageEntry {
    int     slot{0};
    QString name;
    float   total{0.0F};
    float   physical{0.0F};
    float   elemental{0.0F};
    int     hits{0};
    bool    isLocal{false};
};

struct RiseDamageSnapshot {
    bool valid{false};
    bool questActive{false};
    qint64 timestamp{0};
    QVector<RiseDamageEntry> players;
};

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
