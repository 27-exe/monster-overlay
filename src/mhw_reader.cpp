// SPDX-License-Identifier: Apache-2.0
// Core offsets and structures are derived from HunterPie/HunterPie (Apache-2.0).

#include "mhw_reader.h"
#include "player/player_types.h"

#include "core/string_table.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextStream>

#include <array>
#include <algorithm>

namespace mhw {
namespace {

// i18n status strings. diagnostics are read through the StringTable at the
// point of production (not cached), so a locale flip shows up in the next
// snapshot the overlay pulls — see core/string_table.h / docs/I18N.md.
inline QString trMessage(const QString &key) { return StringTable::instance().tr(key); }

#pragma pack(push, 1)
struct MonsterEnrage {
    std::int64_t reference;
    std::int64_t unknown0;
    std::int32_t unknown1;
    std::int32_t active;
    float buildup;
    float damageDone;
    float unknown3;
    float duration;
    float maxDuration;
};
#pragma pack(pop)
struct MonsterEnrageSimple { float duration; float maxDuration; };
static_assert(sizeof(MonsterEnrage) >= 40);

} // namespace

bool AddressMap::load(const QString &path, QString *error)
{
    addresses_.clear();
    offsets_.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = trMessage("reader.address_table_open_failed").arg(path, file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    qsizetype lineNumber = 0;
    while (!stream.atEnd()) {
        ++lineNumber;
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        const auto comment = line.indexOf('#');
        if (comment >= 0)
            line = line.left(comment).trimmed();

        const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (tokens.size() < 3)
            continue;

        const QString type = tokens[0];
        const std::string key = tokens[1].toStdString();
        if (type == QStringLiteral("Address")) {
            bool ok = false;
            const qulonglong value = tokens[2].toULongLong(&ok, 0);
            if (!ok) {
                if (error)
                    *error = trMessage("reader.address_table_bad_address").arg(lineNumber);
                return false;
            }
            addresses_[key] = static_cast<std::uintptr_t>(value);
        } else if (type == QStringLiteral("Offset")) {
            QString valueText = tokens.mid(2).join(QStringLiteral(" "));
            std::vector<std::uintptr_t> values;
            for (const QString &part : valueText.split(',', Qt::SkipEmptyParts)) {
                bool ok = false;
                const qulonglong value = part.trimmed().toULongLong(&ok, 0);
                if (!ok) {
                    if (error)
                        *error = trMessage("reader.address_table_bad_chain").arg(lineNumber);
                    return false;
                }
                values.push_back(static_cast<std::uintptr_t>(value));
            }
            offsets_[key] = std::move(values);
        }
    }

    if (addresses_.empty()) {
        if (error)
            *error = trMessage("reader.address_table_no_address");
        return false;
    }
    return true;
}

std::uintptr_t AddressMap::address(const QString &key) const
{
    const auto it = addresses_.find(key.toStdString());
    return it == addresses_.end() ? 0 : it->second;
}

const std::vector<std::uintptr_t> &AddressMap::offsets(const QString &key) const
{
    static const std::vector<std::uintptr_t> empty;
    const auto it = offsets_.find(key.toStdString());
    return it == offsets_.end() ? empty : it->second;
}

bool AddressMap::hasAddress(const QString &key) const
{
    return addresses_.contains(key.toStdString());
}

bool AddressMap::hasOffsets(const QString &key) const
{
    return offsets_.contains(key.toStdString());
}

MhwReader::MhwReader(QString mapPath, QString exeName)
    : mapPath_(std::move(mapPath)), exeName_(std::move(exeName))
{
    map_.load(mapPath_, &mapError_);
}

const QString &MhwReader::mapPath() const
{
    return mapPath_;
}

bool MhwReader::ensureAttached(GameSnapshot &snapshot)
{
    if (!mapError_.isEmpty()) {
        snapshot.status = mapError_;
        return false;
    }

    const auto pid = findGamePid(exeName_);
    if (!pid) {
        memory_.detach();
        imageBase_ = 0;
        snapshot.status = trMessage("reader.waiting_exe").arg(exeName_);
        return false;
    }

    if (!memory_.attached() || memory_.pid() != *pid) {
        QString error;
        if (!memory_.attach(*pid, &error)) {
            snapshot.pid = *pid;
            snapshot.status = trMessage("reader.ptrace_denied")
                                  .arg(*pid)
                                  .arg(error);
            return false;
        }
        imageBase_ = memory_.imageBase(&error, exeName_);
        if (imageBase_ == 0) {
            memory_.detach();
            snapshot.status = error;
            return false;
        }

        // P1 (v0.9.1): prove the read path works before claiming to be
        // attached. Attaching resolves only the PID and the image base, so
        // a denied read (yama ptrace_scope=1 + same-user non-descendant,
        // no CAP_SYS_PTRACE) used to surface as a silently empty HUD with
        // no reason shown anywhere. Eight bytes of the PE header are always
        // mapped (r--p) and reading them has no side effects.
        std::uint8_t headerProbe[8] = {};
        if (!memory_.readBytes(imageBase_, headerProbe, sizeof(headerProbe), &error)) {
            memory_.detach();
            imageBase_ = 0;
            snapshot.pid = *pid;
            snapshot.status = trMessage(QStringLiteral("reader.ptrace_denied"))
                                  .arg(*pid)
                                  .arg(error);
            return false;
        }
    }

    snapshot.attached = true;
    snapshot.pid = *pid;
    snapshot.imageBase = imageBase_;
    snapshot.status = trMessage("reader.world_connected")
                          .arg(*pid)
                          .arg(static_cast<qulonglong>(imageBase_), 0, 16);
    return true;
}

std::uintptr_t MhwReader::absolute(const QString &key) const
{
    return imageBase_ + map_.address(key);
}

QString MhwReader::readUtf8(std::uintptr_t address, std::size_t maxLength) const
{
    std::vector<char> buffer(maxLength + 1, '\0');
    if (!memory_.readBytes(address, buffer.data(), maxLength, nullptr))
        return {};
    const auto end = std::find(buffer.begin(), buffer.end(), '\0');
    return QString::fromUtf8(buffer.data(), static_cast<qsizetype>(std::distance(buffer.begin(), end))).trimmed();
}

GameSnapshot MhwReader::poll()
{
    GameSnapshot snapshot;
    if (!ensureAttached(snapshot))
        return snapshot;

    // v0.7.5: bump the per-instance poll counter so readPlayer()'s
    // mantle cache can compute ages in kMantleCacheTtl increments.
    ++pollTick_;
    snapshot.zone = readZone(nullptr);
    static Zone lastZone = Zone::Unknown;
    if (snapshot.zone != lastZone) {
        cachedArray_.clear();
        monsterCache_.clear();
        cachedArrayBase_ = 0;
        lastZone = snapshot.zone;
    }
    QString error;
    if (isHuntingZone(snapshot.zone)) {
        snapshot.monsters = readMonsters(&error);
    }
    snapshot.player = readPlayer(nullptr);
    // HunterPie: persistent identity (name, MR) comes from the save
    // header, which is valid whenever the player is logged in (zone !=
    // MainMenu). readPlayer() handles HP/ST/mantle; this fills the
    // identity fields that player_reader no longer puts in PlayerSnapshot.
    refreshPlayerIdentity(snapshot.player);
    // Sharpness — only emitted for melee weapons (0..10). Ranged
    // weapons (bow/hbg/lbg) keep valid=false so the panel can hide
    // the bar entirely.
    snapshot.player.sharpness = readSharpness(snapshot.player.weaponId, nullptr);
    snapshot.quest = readQuest(nullptr);
    // Clear stale quest data when we're in a non-hunting zone (e.g.
    // gathering hub). The quest struct in memory can retain the
    // previous quest's state/id/maxDeaths after returning to town.
    if (!isHuntingZone(snapshot.zone))
        snapshot.quest = {};

    // readParty always probes 4 slots; stale names in memory linger
    // after leaving a quest, which would keep the damage panel
    // pinned to the now-meaningless last-quest data. Force-clear
    // party whenever we're not in a hunting zone (where party
    // damage is actually meaningful).
    snapshot.party = isHuntingZone(snapshot.zone) ? readParty(nullptr)
                                                   : QVector<PartyMemberSnapshot>{};
    // v0.10.8: multiplayer now comes from the session structure's player
    // count (HunterPie MHWPlayer.GetParty) instead of the roster size.
    // readParty fills a 4-slot roster by name and never tags a member's
    // PartyMemberKind, so `party.size() > 1` counted companions and stale
    // slots as real hunters — a solo hunter with a palico got the
    // multiplayer treatment. `party` itself is untouched: DamagePanel owns
    // its roster semantics and still reads it as before.
    // The session count is only trustworthy if a failed read keeps the last
    // session we actually resolved. partySize 0 is HunterPie's SOLO value, so
    // letting a miss surface as 0 would re-show every fake full Health bar the
    // multiplayer gate removes — and it would do so during map transitions and
    // quest start, i.e. when the overlay matters most. `previousPlayerCount_`
    // only advances on a validated read, so one bad tick cannot flip the UI
    // back to solo semantics.
    const int rawPlayerCount =
        readSessionPlayerCount(nullptr).value_or(mhw::kSessionReadFailed);
    snapshot.playerCount = mhw::sanitizeSessionPlayerCount(rawPlayerCount, previousPlayerCount_);
    if (rawPlayerCount != mhw::kSessionReadFailed && rawPlayerCount >= 0
        && rawPlayerCount <= 4)
        previousPlayerCount_ = rawPlayerCount;
    snapshot.isMultiplayer = (snapshot.playerCount > 1);
    if (!error.isEmpty() && snapshot.monsters.isEmpty())
        snapshot.status += trMessage("reader.partial_read_failed").arg(error);
    return snapshot;
}

} // namespace mhw
