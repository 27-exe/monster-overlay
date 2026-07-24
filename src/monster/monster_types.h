#pragma once

#include <QString>
#include <QVector>
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
    float enrageSeconds{};
    float enrageMaxSeconds{};
    float enrageBuildup{};
    float enrageMaxBuildup{};
    bool enraged{};
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

} // namespace mhw