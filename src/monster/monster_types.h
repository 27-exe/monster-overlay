#pragma once

#include <QString>
#include <QVector>
#include <array>
#include <cstdint>

namespace mhw {

struct PartSnapshot {
    int index{-1};
    QString name;
    float health{};
    float maxHealth{};
    float extraHealth{};
    float extraMaxHealth{};
    float flinch{};
    float maxFlinch{};
    int counter{};
    int firstThreshold{0};
    bool isSeverable{false};
    bool isBreakable{false};
    bool isBroken{};
};

struct MonsterAilmentSnapshot {
    int id{-1};
    QString name;
    bool active{};
    float timer{};
    float maxTimer{};
    float buildup{};
    float maxBuildup{};
    int counter{};
};

struct MonsterSnapshot {
    std::uintptr_t address{};
    int id{-1};
    QString internalName;
    float health{};
    float maxHealth{};
    float stamina{};
    float maxStamina{};
    float size{1.0F};           // sizeModifier(+0x7730) × sizeMultiplier(+0x184)
    float enrageSeconds{};
    float enrageMaxSeconds{};
    float enrageBuildup{};
    float enrageMaxBuildup{};
    bool enraged{};
    int doubleLinkedListIndex{-1}; // HunterPie: Monster + 0x1228C
    bool isLockOnTarget{};         // LOCKON chain index equals the above
    bool isManuallyTargeted{};   // legacy OR alias: isManualTargeted || isQuestTargeted
    bool isManualTargeted{};    // HunterPie manual map pin (player pinned)
    bool isQuestTargeted{};     // HunterPie quest pin (capture / investigation)
                                // overlay picks this monster regardless of
                                // maxHealth.
    QVector<PartSnapshot> parts;
    QVector<MonsterAilmentSnapshot> ailments;
};

// One schema entry per Part in HunterPie's MonsterData.xml.
struct PartSchema {
    int id;
    bool isSeverable;
    const char* name;
    const char* thresholds;
};

// Generated from data/MonsterHunterWorld.421810.map / MonsterData.xml.
extern const QHash<int, QVector<PartSchema>> kPartSchemas;

// Generated from HunterPie/Localization zh-cn.xml (AilmentData).
extern const QHash<int, QString> kAilmentNames;

// Generated from HunterPie Game/World/Data/MonsterData.xml <Crowns>.
// Per-monster crown size thresholds: {Mini, Silver, Gold}.
extern const QHash<int, std::array<float, 3>> kCrownThresholds;

// Generated from HunterPie Game/World/Data/MonsterData.xml <Monster Capture=N>.
// Each entry is the in-game HP percentage at which that monster becomes
// capturable (0 = uncapturable, e.g. Elder Dragons).
extern const QHash<int, int> kMonsterCaptureThresholds;

} // namespace mhw