#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <array>
#include <cstdint>
#include <optional>

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
    // v0.10.3-r5 break-layer fix (Rise): PartSnapshot used to carry a single
    // health/maxHealth pair, and MhrReader::readMonsterParts picked ONE of the
    // three Rise layers (sever / break / flinch) for it via the partType
    // switch. For a Severable part that sever-first dispatch silently threw
    // away the break layer — the player could see "how much HP until the tail
    // is cut off" but not "how many breakable-body hits are left to flush the
    // current break layer". HunterPie keeps both: MHRPartStructure carries
    // Health/MaxHealth (break layer), Sever/MaxSever and Flinch/MaxFlinch as
    // six independent fields (MHRPartStructure.cs) and MHRMonsterPart.Update
    // assigns all six from one data record (MHRMonsterPart.cs:124-129), so a
    // Severable part shows Body HP and Sever HP side by side.
    //
    // breakHealth/breakMaxHealth carry that break layer alongside. Semantics:
    //   * Severable part → health/maxHealth stays the sever layer (unchanged,
    //     isBroken / isPartSevered / Row 3 Conditional / AutoHide signatures
    //     all read it), and breakHealth/breakMaxHealth hold the break layer.
    //   * Breakable part → health/maxHealth already IS the break layer, so
    //     the duplicate fields are left at 0 (that layer is already exposed).
    //   * Flinch part    → both stay 0 (no break layer exists).
    // extraHealth/extraMaxHealth are NOT reused: the World path
    // (monster_reader.cpp:691-692, 788-789) already uses them for the
    // severable-part secondary HP.
    // v0.10.x-r3 UI-template alignment (HunterPie MonsterPartContextHandler.cs:104):
    // `IsPartSevered = MaxSever == Sever && (Breaks > 0 || Flinch != MaxFlinch)`.
    // Severable parts need this signal distinct from isBroken so the UI can
    // drive the Row 3 Conditional text (Sever/MaxSever -> Flinch/MaxFlinch)
    // the same way HunterPie's BossMonsterSeverablePartView.xaml:101-134 does.
    // Lives on the snapshot (reader computes it) so the UI never recomputes.
    bool isPartSevered{};
    // v0.10.3-r5 break-layer fix (see the long comment above): the break
    // layer of a Severable part. 0 when the part has no break layer, which is
    // also true for Breakable parts — there health/maxHealth already IS the
    // break layer, and for Flinch parts no break layer exists at all. So a
    // nonzero breakMaxHealth is exactly "this Severable part also has a
    // breakable body". Both are raw reader values (not display-formatted).
    float breakHealth{};
    float breakMaxHealth{};
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
    float size{};              // 0 = not read (unknown); panel hides the chip.
                               // World: sizeModifier(+0x7730) ÷ sizeMultiplier(+0x184),
                               // Rise: SizeMultiplier × UnkMultiplier (crown ratio).
                               // v0.8.4-r19 monster-identity: this used to default to
                               // 1.0F, which made a failed read indistinguishable from
                               // a genuine 100 % monster — every Rise monster showed a
                               // dead "1.00×". A real 1.0 is still reported as 1.0.
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
    const char* name;      // zh display name: frozen pre-i18n literal for
                           // normal entry, official zh-cn.xml name for
                           // statePart entries (v0.9 placeholder cleanup)
    // v0.9 i18n (WS-A): English display name, resolved from HunterPie's
    // en-us.xml `Monsters/Shared/Part` table via the part's String key in
    // Game/World/Data/MonsterData.xml. A null/empty nameEn falls back to
    // `name`.
    //
    // v0.9 placeholder cleanup: true marks the entries readMonsters() must
    // NOT map into the part tables — state-transition variants, PART_UNKNOWN
    // sentinels, exotic enum entries. scripts/gen_schema.py freezes it as
    // the pre-cleanup `name.startsWith("PART_")` skip set, so the runtime
    // skip set and the normal-table slot mapping stay byte-identical even
    // though those names are real display strings now.
    //
    // Both fields sit BEFORE `thresholds` (not appended last) on purpose: a
    // 7-field aggregate whose nested std::array initializer sits before the
    // final member is not brace-elided by gcc-15 ("could not convert
    // '<brace-enclosed initializer list>'"), which breaks every
    // QList<PartSchema> literal in part_schemas.cpp. The default initializer
    // keeps pre-existing aggregate initializers valid.
    const char* nameEn = nullptr;
    bool statePart = false;
    const char* thresholds;
    std::array<std::uint32_t, 4> tenderizeIds{};   // HunterPie XML TenderizeIds
    std::uint32_t tenderizeCount{0};              // # of valid entries in tenderizeIds
};

// Generated from data/MonsterHunterWorld.421810.map / MonsterData.xml.
extern const QHash<int, QVector<PartSchema>> kPartSchemas;

// v0.9 i18n (WS-A): locale-aware lookups for the World data-name tables.
// Each keeps the pre-i18n zh literal as its default and picks the English
// column only when mhw::StringTable::isEnglish() is true, so an unloaded
// StringTable (tests, pre-load frames) behaves exactly like the old code.
// Defined in monster_reader.cpp — declared here so the reader tests (and any
// panel that wants a locale-aware label) can call them without duplicating
// the selection rule.
//
// Localized part label: `nameEn` when English and present, else `name`
// (zh). The reader-level statePart skip is separate — see PartSchema.
QString partDisplayName(const PartSchema &ps);

// Localized World monster name for the "000".."101" id key; unknown keys are
// returned unchanged.
QString monsterDisplayName(const QString &idKey);

// Localized World monster-ailment label; unknown ids keep the historical
// "异常%1" (zh) / "Ailment %1" (en) fallback.
QString monsterAilmentDisplayName(int id);

// Generated from HunterPie/Localization zh-cn.xml (AilmentData).
// v0.9 i18n (WS-A): frozen pre-i18n zh-CN column; kAilmentNamesEn below is
// the English column, selected at the call site via StringTable::isEnglish().
extern const QHash<int, QString> kAilmentNames;

// v0.9 i18n (WS-A): World monster-ailment English names. Ids are HunterPie
// Game/World/Data/MonsterData.xml <Ailments> ids; the strings come from
// en-us.xml <Ailments><Rise><Ailment Id="AILMENT_*"> (the AILMENT_* keys are
// shared between the games' ailment enums, World's MonsterData.xml names the
// same keys). Ids 12/19/21/24 are absent from the zh table (they fall back to
// "异常%1" there) and are supplied here so the English path shows the real
// official name instead of "Ailment N". The Rise slot table
// (kRiseAilmentNames) is untouched by WS-A.
extern const QHash<int, QString> kAilmentNamesEn;

// Generated from HunterPie Game/World/Data/MonsterData.xml <Crowns>.
// Per-monster crown size thresholds: {Mini, Silver, Gold}.
extern const QHash<int, std::array<float, 3>> kCrownThresholds;

// Generated from HunterPie Game/Rise/Data/MonsterData.xml <Crowns>.
// Only monsters with a <Crowns> element are present. A zero component means
// that individual threshold attribute was absent in the XML.
extern const QHash<int, std::array<float, 3>> kRiseCrownThresholds;

// Rise MonsterData.xml has no Capture=N attribute. It explicitly marks only
// IsNotCapturable=true monsters; all other Rise capture thresholds are unknown
// to this static metadata and must not inherit World thresholds.
extern const QSet<int> kRiseNotCapturableMonsterIds;

// Returns nullptr when the game-specific XML has no crown entry. In particular,
// this does not supply World defaults for Rise IDs with missing <Crowns>.
const std::array<float, 3> *crownThresholdsFor(GameId game, int monsterId);

// 0 is an explicit non-capturable result. std::nullopt means no static capture
// threshold is available for this game/monster combination.
std::optional<int> captureThresholdFor(GameId game, int monsterId);

// v0.7.5: Rise ailment slot names (HunterPie Game/Rise/Data/MonsterData.xml
// <Ailments>). World uses kAilmentNames; the two id tables are UNRELATED.
extern const QHash<int, QString> kRiseAilmentNames;

// Generated from HunterPie Game/World/Data/MonsterData.xml <Monster Capture=N>.
// Each entry is the in-game HP percentage at which that monster becomes
// capturable (0 = uncapturable, e.g. Elder Dragons).
extern const QHash<int, int> kMonsterCaptureThresholds;

inline const std::array<float, 3> *crownThresholdsFor(GameId game, int monsterId)
{
    const auto &thresholds = game == GameId::Rise ? kRiseCrownThresholds : kCrownThresholds;
    const auto it = thresholds.constFind(monsterId);
    return it == thresholds.cend() ? nullptr : &it.value();
}

inline std::optional<int> captureThresholdFor(GameId game, int monsterId)
{
    if (game == GameId::Rise) {
        if (kRiseNotCapturableMonsterIds.contains(monsterId))
            return 0;
        return std::nullopt;
    }
    const auto it = kMonsterCaptureThresholds.constFind(monsterId);
    return it == kMonsterCaptureThresholds.cend()
        ? std::nullopt
        : std::optional<int>{it.value()};
}

} // namespace mhw