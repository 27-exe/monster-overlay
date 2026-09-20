#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace mhw {

enum class RiseDamageActorKind {
    Player,
    Companion,
    Pet,
    Palico,
    Palamute,
    Unknown,
};

struct RiseDamageActor {
    QString key;
    RiseDamageActorKind kind{RiseDamageActorKind::Unknown};

    int entityIndex{-1};
    int displaySlot{-1};
    int ownerEntityIndex{-1};
    int sourceTypeRaw{-1};

    QString name;
    QString ownerName;

    qint64 total{0};
    qint64 physical{0};
    qint64 elemental{0};
    quint64 hits{0};

    bool local{false};
    bool present{true};
    bool connected{true};
};

struct RiseDamageDisplayOptions {
    bool showLocalPets{true};
    bool showOtherMembers{true};
    bool showOtherPets{true};
};

struct RiseDamageSnapshot {
    bool valid{false};
    bool questActive{false};
    bool questStateValid{true};
    bool collectionPaused{false};
    bool training{false};
    int questState{0};
    int questEpoch{0};
    qint64 timestampMs{0};
    quint64 sequence{0};
    QVector<RiseDamageActor> actors;
    int droppedUnknownEvents{0};
    int ambiguousPetEvents{0};
    QHash<int, int> unknownAttackerTypes;
};

enum class RiseDamageLifecycleAction {
    Keep,    // transient source/state failure: preserve the last visible frame
    Record,  // active quest or training-room collection
    Freeze,  // quest result states 3..7
    Clear,   // stale/invalid feed or lobby/ready/unknown inactive state
};

inline constexpr RiseDamageLifecycleAction
riseDamageLifecycleAction(const RiseDamageSnapshot &snapshot)
{
    if (!snapshot.valid)
        return RiseDamageLifecycleAction::Clear;
    if (!snapshot.questStateValid || snapshot.collectionPaused)
        return RiseDamageLifecycleAction::Keep;
    if (snapshot.questActive)
        return RiseDamageLifecycleAction::Record;
    if (snapshot.questState >= 3 && snapshot.questState <= 7)
        return RiseDamageLifecycleAction::Freeze;
    return RiseDamageLifecycleAction::Clear;
}

inline bool isRiseDamageActorVisible(const RiseDamageActor &actor,
                                     const RiseDamageDisplayOptions &options)
{
    switch (actor.kind) {
    case RiseDamageActorKind::Player:
        return actor.local || options.showOtherMembers;
    case RiseDamageActorKind::Companion:
        return options.showOtherMembers;
    case RiseDamageActorKind::Pet:
    case RiseDamageActorKind::Palico:
    case RiseDamageActorKind::Palamute:
        return actor.local ? options.showLocalPets : options.showOtherPets;
    case RiseDamageActorKind::Unknown:
        return false;
    }
    return false;
}

inline bool isRisePetKind(RiseDamageActorKind kind)
{
    return kind == RiseDamageActorKind::Pet
        || kind == RiseDamageActorKind::Palico
        || kind == RiseDamageActorKind::Palamute;
}

} // namespace mhw
