#pragma once

#include <QString>
#include <QVector>
#include <array>
#include <cstdint>

namespace mhw {

// v0.7.5: moved here from core/game_snapshot.h so MonsterSnapshot can
// carry which game produced it. The monster panel colours/icons ailment
// cards by id, and World/Rise use DIFFERENT id tables — without this
// flag a Rise monster's sleep slot would be tinted with World's
// paralysis amber.
enum class GameId { World, Rise };

// v0.7.4 (PR B — monster-state-and-tenderize-20260803):
// Mirrors HunterPie-v2 MHWMonsterPart.Type dispatch (PartType.cs).
// UpdateSeverableData / UpdateFlinchData / UpdateBreakableData each populate
// different fields, so the UI needs to know which set is meaningful.
enum class PartType {
    Flinch,      // No sever flag, no break thresholds → only flinch bar matters.
    Severable,   // IsSeverable → only sever bar (Health / MaxHealth) matters.
    Breakable,   // BreakThresholds non-empty → cumulative threshold math on
                 // Health / MaxHealth, plus flinch bar for the current layer.
};

// v0.7.4: Tenderize state lives on the part (HunterPie MHWMonsterPart.Tenderize
// / MaxTenderize), not on a separate slot model. Each part writes its own
// tenderize* fields when the runtime walks the 10 TenderizeInfoStructure slots
// and matches slot.PartId against this part's PartSchema.tenderizeIds.
struct PartSnapshot {
    int index{-1};
    QString name;
    PartType partType{PartType::Flinch};
    float health{};
    float maxHealth{};
    float extraHealth{};
    float extraMaxHealth{};
    float flinch{};
    float maxFlinch{};
    int counter{};
    int firstThreshold{0};
    float tenderizeDuration{};          // 0.0 → no active tenderize on this part
    float tenderizeMaxDuration{};       // paired with tenderizeDuration
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

// v0.7.4: Tenderize data is folded into PartSnapshot (see above).
// MonsterSnapshot no longer carries a separate tenderize slot vector —
// the runtime writes each PartSnapshot.tenderizeDuration directly.

struct MonsterSnapshot {
    std::uintptr_t address{};
    int id{-1};
    GameId game{GameId::World};   // v0.7.5: ids (esp. ailments) are game-specific
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

    // Qurio (Rise only)
    bool qurioActive{false};
    float qurioThreshold{0.0F};
    float qurioMaxThreshold{0.0F};
    struct QurioPart { bool active; float health; float maxHealth; };
    QVector<QurioPart> qurioParts;
};

// One schema entry per Part in HunterPie's MonsterData.xml.
//
// v0.7.4 (PR A — monster-state-and-tenderize-20260803):
// Added tenderizeIds / tenderizeCount fields so the runtime can map a
// Clutch Claw Tenderize slot's PartId back to one (or several) schema
// parts. HunterPie XML's max is 4 ids per part (em26:10=[2,6,8,9],
// em97:1=[0,4,8,9]) so a std::array<uint32_t,4> covers the empirical
// upper bound; sentinel=0xFFFFFFFFu is never written by HunterPie XML.
//
// TenderizeIds semantics (mirrors HunterPie MHWMonster.GetMonsterPartTenderizes
// in HunterPie-v2/HunterPie.Integrations/Datasources/MonsterHunterWorld/
// Entity/Enemy/MHWMonster.cs:413-433): runtime walks the 10 in-memory
// TenderizeInfoStructure slots, takes each slot's PartId, and updates
// every schema part whose tenderizeIds contains it. The IDs here are
// therefore slot indices (0..9), not part Ids.
struct PartSchema {
    int id;
    bool isSeverable;
    const char* name;
    const char* thresholds;
    std::array<std::uint32_t, 4> tenderizeIds{};   // HunterPie XML TenderizeIds
    std::uint32_t tenderizeCount{0};              // # of valid entries in tenderizeIds
};

// Generated from data/MonsterHunterWorld.421810.map / MonsterData.xml.
extern const QHash<int, QVector<PartSchema>> kPartSchemas;

// Generated from HunterPie/Localization zh-cn.xml (AilmentData).
extern const QHash<int, QString> kAilmentNames;

// Generated from HunterPie Game/World/Data/MonsterData.xml <Crowns>.
// Per-monster crown size thresholds: {Mini, Silver, Gold}.
extern const QHash<int, std::array<float, 3>> kCrownThresholds;

// v0.7.5: Rise ailment slot names (HunterPie Game/Rise/Data/MonsterData.xml
// <Ailments>). World uses kAilmentNames; the two id tables are UNRELATED.
extern const QHash<int, QString> kRiseAilmentNames;

// Generated from HunterPie Game/World/Data/MonsterData.xml <Monster Capture=N>.
// Each entry is the in-game HP percentage at which that monster becomes
// capturable (0 = uncapturable, e.g. Elder Dragons).
extern const QHash<int, int> kMonsterCaptureThresholds;

} // namespace mhw