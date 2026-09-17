// SPDX-License-Identifier: Apache-2.0
//
// Offline coverage for the Rise player-abnormality pipeline (v0.8.4-r18
// player-abnormalities): the generated consumable/debuff schema table plus
// the pure evaluation core that mhr_reader.cpp drives.
//
// Pinned here:
//   1. Table completeness — 69 "Consumables" + 28 "Debuffs" rows (the two
//      AbnormalityGroup categories MHRPlayer.GetConsumableAbnormalities /
//      GetPlayerDebuffAbnormalities iterate), unique ids, resolved zh-cn
//      names, no hunting-horn songs.
//   2. Selector rows against HunterPie/Game/Rise/Data/AbnormalityData.xml
//      (offsets, DependsOn/WithValue, flags, MaxTimer, IsInfinite /
//      IsBuildup / IsInteger).
//   3. The timer conversion (MHRiseUtils.ToAbnormalitySeconds = raw / 60),
//      MaxTimer inversion and buildup handling.
//   4. Condition evaluation (Enum.HasFlag semantics, including flag == 0)
//      and the WithValue sub-id gate.
//   5. The display-text helper the panel renders verbatim.
//
// No QCoreApplication: everything here is QString-free except the timer text
// helper, which needs only QtCore's QString.
//
// v0.9 i18n (WS-B) extends this file with the en-us.xml columns: the table now
// carries name + nameEn per row and the two display helpers pick by
// mhw::StringTable::instance().isEnglish(). StringTable::load() reads Qt
// resources, so the binary now starts a QCoreApplication (needed for the
// resource system) and links the shipped qrc — the CMake target already does.

#include "rise/mhr_abnormalities.h"

#include "core/string_table.h"

#include <QCoreApplication>
#include <QString>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition) {
        std::printf("PASS: %s\n", message);
    } else {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

// Raw value that yields `seconds` after the /60 conversion.
constexpr float rawForSeconds(float seconds)
{
    return seconds * mhw::kRiseAbnormalityTimerMultiplier;
}

const mhw::RiseAbnormalitySchema *find(const char *id)
{
    return mhw::riseFindAbnormality(id);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::size_t consumableCount = 0;
    std::size_t debuffCount = 0;
    const mhw::RiseAbnormalitySchema *consumables =
        mhw::riseConsumableAbnormalities(consumableCount);
    const mhw::RiseAbnormalitySchema *debuffs =
        mhw::riseDebuffAbnormalities(debuffCount);

    // ---- 1. Table shape -------------------------------------------------
    check(consumables != nullptr && consumableCount == 69,
          "Consumables category carries 69 schemas (HunterPie Rise XML)");
    check(debuffs != nullptr && debuffCount == 28,
          "Debuffs category carries 28 schemas (HunterPie Rise XML)");
    check(consumableCount + debuffCount == 97,
          "97 schemas total — the exact Category counts of AbnormalityData.xml");

    bool allSigned = true;
    bool allOffsetsAligned = true;
    bool allFlagsResolved = true;
    bool allNamesResolved = true;
    bool noSongs = true;
    for (int table = 0; table < 2; ++table) {
        const mhw::RiseAbnormalitySchema *rows = table == 0 ? consumables : debuffs;
        const std::size_t count = table == 0 ? consumableCount : debuffCount;
        for (std::size_t i = 0; i < count; ++i) {
            const mhw::RiseAbnormalitySchema &s = rows[i];
            if (s.id == nullptr || s.id[0] == '\0' || s.nameKey == nullptr
                || s.nameKey[0] == '\0' || s.name == nullptr || s.name[0] == '\0'
                || s.nameEn == nullptr || s.nameEn[0] == '\0'
                || s.group == nullptr || s.group[0] == '\0')
                allNamesResolved = false;
            if (s.offset != 0 && (s.offset < 0 || s.offset % 4 != 0))
                allOffsetsAligned = false;
            if ((s.flagType == mhw::RiseAbnormalityFlagType::None) != (s.flag == 0))
                allFlagsResolved = false;
            if (std::strcmp(s.group, "Songs") == 0)
                noSongs = false;
            if (s.kind != (table == 0 ? mhw::RiseAbnormalityKind::Buff
                                      : mhw::RiseAbnormalityKind::Debuff))
                allSigned = false;
        }
    }
    check(allNamesResolved, "every schema carries id / nameKey / name / group");
    check(allOffsetsAligned, "every struct offset is non-negative and word-aligned");
    check(allFlagsResolved, "FlagType and Flag agree: bit present iff typed");
    check(noSongs, "no hunting-horn song rows (HH is a separate upstream path)");
    check(allSigned, "table membership and kind agree for every row");

    bool uniqueIds = true;
    for (std::size_t i = 0; i < consumableCount && uniqueIds; ++i) {
        for (std::size_t j = i + 1; j < consumableCount; ++j) {
            if (std::strcmp(consumables[i].id, consumables[j].id) == 0) {
                uniqueIds = false;
                break;
            }
        }
        for (std::size_t j = 0; j < debuffCount; ++j) {
            if (std::strcmp(consumables[i].id, debuffs[j].id) == 0) {
                uniqueIds = false;
                break;
            }
        }
    }
    check(uniqueIds, "schema ids are unique across both tables");

    // ---- 2. Selector rows ----------------------------------------------
    const mhw::RiseAbnormalitySchema *demonDrug = find("ABN_DEMONDRUG");
    check(demonDrug != nullptr && demonDrug->isInfinite
              && std::strcmp(demonDrug->name, "鬼人药") == 0
              && demonDrug->dependsOn == 0x7C && demonDrug->withValue == 5
              && demonDrug->offset == 0x0
              && demonDrug->flagType == mhw::RiseAbnormalityFlagType::None,
          "ABN_DEMONDRUG: infinite, level word @0x7C == 5, name 鬼人药");

    const mhw::RiseAbnormalitySchema *megaDrug = find("ABN_MEGA_DEMONDRUG");
    check(megaDrug != nullptr && megaDrug->dependsOn == 0x7C
              && megaDrug->withValue == 7 && megaDrug->isInfinite,
          "ABN_MEGA_DEMONDRUG shares the 0x7C level word at value 7");

    const mhw::RiseAbnormalitySchema *mightSeed = find("ABN_MIGHT_SEED");
    check(mightSeed != nullptr && mightSeed->offset == 0x90
              && mightSeed->dependsOn == 0x84 && mightSeed->withValue == 10
              && !mightSeed->isInfinite && !mightSeed->isInteger
              && !mightSeed->isBuildup && mightSeed->maxTimer == 0.0F
              && std::strcmp(mightSeed->name, "怪力种子") == 0,
          "ABN_MIGHT_SEED: 0x90, sub-id 0x84 == 10, plain float countdown");

    const mhw::RiseAbnormalitySchema *poison = find("ABN_POISON");
    check(poison != nullptr && poison->offset == 0x8A4
              && poison->flagType == mhw::RiseAbnormalityFlagType::RiseDebuff
              && poison->flag == (1ULL << 32)
              && std::strcmp(poison->name, "中毒") == 0,
          "ABN_POISON: Debuffs 0x8A4, DebuffConditions.Poison (1<<32)");

    const mhw::RiseAbnormalitySchema *venom = find("ABN_VENOM");
    check(venom != nullptr && venom->offset == poison->offset
              && venom->flag == (1ULL << 33),
          "ABN_VENOM shares the poison slot but gates on NoxiousPoison (1<<33)");

    const mhw::RiseAbnormalitySchema *sleep = find("ABN_SLEEP");
    check(sleep != nullptr && sleep->offset == 0x8AC && sleep->dependsOn == 0x810
              && sleep->withValue == 0,
          "ABN_SLEEP is gated on sub-id 0x810 == 0");

    const mhw::RiseAbnormalitySchema *windMantle = find("ABN_WINDMANTLE");
    check(windMantle != nullptr && windMantle->offset == 0x79C
              && windMantle->flagType == mhw::RiseAbnormalityFlagType::RiseCommon
              && windMantle->flag == (1ULL << 57)
              && windMantle->maxTimer == 15.0F
              && std::strcmp(windMantle->name, "风绕") == 0,
          "ABN_WINDMANTLE: CommonConditions.WindMantle (1<<57), MaxTimer 15");

    const mhw::RiseAbnormalitySchema *spiribird = find("ABN_SPIRIBIRDS_CALL");
    check(spiribird != nullptr && spiribird->offset == 0x17C
              && spiribird->maxTimer == 60.0F,
          "ABN_SPIRIBIRDS_CALL: 0x17C with MaxTimer 60 (elapsed -> remaining)");

    const mhw::RiseAbnormalitySchema *frostcraft = find("ABN_FROSTCRAFT");
    check(frostcraft != nullptr && frostcraft->offset == 0x7C8
              && frostcraft->isBuildup && !frostcraft->isInteger
              && frostcraft->maxBuildup == 100
              && std::strcmp(frostcraft->name, "寒气炼成") == 0,
          "ABN_FROSTCRAFT: buildup counter at 0x7C8, MaxBuildup 100");

    const mhw::RiseAbnormalitySchema *frenzyBuildup = find("ABN_FRENZY_BUILDUP");
    check(frenzyBuildup != nullptr && frenzyBuildup->offset == 0x91C
              && frenzyBuildup->isBuildup && frenzyBuildup->maxBuildup == 120,
          "ABN_FRENZY_BUILDUP: buildup counter at 0x91C, MaxBuildup 120");

    const mhw::RiseAbnormalitySchema *dereliction = find("ABN_DERELICTION_S");
    check(dereliction != nullptr && dereliction->isInteger && dereliction->isBuildup
              && dereliction->flag == (1ULL << 41),
          "ABN_DERELICTION_S: int buildup gated on DerelictionS (1<<41)");

    const mhw::RiseAbnormalitySchema *maxMight = find("ABN_MAXIMUM_MIGHT");
    check(maxMight != nullptr && maxMight->isInfinite
              && maxMight->flagType == mhw::RiseAbnormalityFlagType::RiseAction
              && maxMight->flag == (1U << 5),
          "ABN_MAXIMUM_MIGHT: ActionFlags.MaximumMight (1<<5), infinite");

    const mhw::RiseAbnormalitySchema *leeched = find("ABN_LEECHED");
    check(leeched != nullptr && leeched->isInfinite
              && leeched->flag == (1ULL << 44)
              && std::strcmp(leeched->name, "吸血异常") == 0,
          "ABN_LEECHED: infinite DebuffConditions.Leeched (1<<44)");

    // The two upstream entries with no Name attribute keep the mapper's
    // ABNORMALITY_UNKNOWN fallback (localized to "Unknown"); they exist so the
    // table is a faithful port, not because they are meant to be pretty.
    const mhw::RiseAbnormalitySchema *unknownConsumable = find("Consumables_F4");
    check(unknownConsumable != nullptr && unknownConsumable->offset == 0xFC
              && std::strcmp(unknownConsumable->nameKey, "ABNORMALITY_UNKNOWN") == 0,
          "Consumables_F4 keeps the nameless-entry fallback (UNKNOWN key, 0xFC)");

    check(find("ABN_DOES_NOT_EXIST") == nullptr,
          "unknown id lookup returns nullptr");

    // ---- 3. Conversion + MaxTimer inversion -----------------------------
    check(mhw::riseAbnormalitySeconds(60.0F) == 1.0F
              && mhw::riseAbnormalitySeconds(3600.0F) == 60.0F
              && mhw::riseAbnormalitySeconds(0.0F) == 0.0F,
          "ToAbnormalitySeconds divides raw sixtieths by 60");

    mhw::RiseAbnormalityConditions none;
    mhw::RiseAbnormalityEvaluation ev =
        mhw::riseEvaluateAbnormality(*mightSeed, none, 10, rawForSeconds(100.0F), true);
    check(ev.active && std::abs(ev.timer - 100.0F) < 0.001F,
          "MIGHT_SEED raw 6000 renders as 100s");

    ev = mhw::riseEvaluateAbnormality(*mightSeed, none, 10, 0.0F, true);
    check(!ev.active, "zero timer is not an active abnormality");

    ev = mhw::riseEvaluateAbnormality(*spiribird, none, 0, rawForSeconds(5.0F), true);
    check(ev.active && std::abs(ev.timer - 55.0F) < 0.001F,
          "MaxTimer entries invert elapsed -> remaining (60 - 5 = 55s)");

    ev = mhw::riseEvaluateAbnormality(*spiribird, none, 0, rawForSeconds(60.0F), true);
    check(!ev.active, "elapsed == MaxTimer leaves 0s and hides the entry");

    ev = mhw::riseEvaluateAbnormality(*spiribird, none, 0, rawForSeconds(90.0F), true);
    check(!ev.active, "elapsed beyond MaxTimer clamps to 0 and hides the entry");

    // DERELICTION_S carries a RiseCommon flag (DerelictionS = 1 << 41), so its
    // word must be set before the value read is even reached.
    mhw::RiseAbnormalityConditions derelictionOn;
    derelictionOn.common = 1ULL << 41;
    ev = mhw::riseEvaluateAbnormality(*dereliction, derelictionOn, 0, 30.0F, true);
    check(ev.active && ev.timer == 30.0F,
          "IsInteger + IsBuildup values are neither /60 nor inverted");

    ev = mhw::riseEvaluateAbnormality(*frostcraft, none, 0, 42.5F, true);
    check(ev.active && ev.timer == 42.5F,
          "IsBuildup floats stay raw (frostcraft 42.5 / 100)");

    // ---- 4. Condition gates ---------------------------------------------
    mhw::RiseAbnormalityConditions debuffOn;
    debuffOn.debuff = 1ULL << 32;   // DebuffConditions.Poison
    ev = mhw::riseEvaluateAbnormality(*poison, debuffOn, 0, rawForSeconds(12.0F), true);
    check(ev.active && std::abs(ev.timer - 12.0F) < 0.001F,
          "POISON shows while its DebuffConditions bit is set");

    debuffOn.debuff = 1ULL << 33;   // NoxiousPoison instead
    ev = mhw::riseEvaluateAbnormality(*poison, debuffOn, 0, rawForSeconds(12.0F), true);
    check(!ev.active, "a different DebuffConditions bit does not satisfy POISON");

    mhw::RiseAbnormalityConditions commonOn;
    commonOn.common = 1ULL << 57;   // CommonConditions.WindMantle
    ev = mhw::riseEvaluateAbnormality(*windMantle, commonOn, 0, rawForSeconds(4.0F), true);
    check(ev.active && std::abs(ev.timer - 11.0F) < 0.001F,
          "WINDMANTLE shows under its CommonConditions bit (15 - 4 = 11s)");

    mhw::RiseAbnormalityConditions actionOn;
    actionOn.action = 1U << 5;      // ActionFlags.MaximumMight
    ev = mhw::riseEvaluateAbnormality(*maxMight, actionOn, 0, 0.0F, false);
    check(ev.active && ev.timer == 1.0F,
          "infinite entries are active without any value read");

    ev = mhw::riseEvaluateAbnormality(*maxMight, none, 0, 0.0F, false);
    check(!ev.active, "infinite entries still need their flag/condition gate");

    // .NET Enum.HasFlag(0) is true — the reader must reproduce that for
    // entries whose Flag attribute parsed to an empty/'None' value.
    mhw::RiseAbnormalitySchema zeroFlagSchema = *mightSeed;
    zeroFlagSchema.flagType = mhw::RiseAbnormalityFlagType::RiseCommon;
    zeroFlagSchema.flag = 0;
    ev = mhw::riseEvaluateAbnormality(zeroFlagSchema, none, 10, rawForSeconds(3.0F), true);
    check(ev.active, "a RiseCommon schema with flag == 0 passes HasFlag semantics");

    // WithValue sub-id gate.
    ev = mhw::riseEvaluateAbnormality(*demonDrug, none, 4, 0.0F, false);
    check(!ev.active, "DEMONDRUG does not show for level word 4");
    ev = mhw::riseEvaluateAbnormality(*demonDrug, none, 5, 0.0F, false);
    check(ev.active, "DEMONDRUG shows for level word 5");
    ev = mhw::riseEvaluateAbnormality(*demonDrug, none, 7, 0.0F, false);
    check(!ev.active, "DEMONDRUG stays hidden when the mega variant is active");

    ev = mhw::riseEvaluateAbnormality(*sleep, none, 1, rawForSeconds(30.0F), true);
    check(!ev.active, "SLEEP requires its 0x810 sub-id to be exactly 0");

    // Failed value reads never fabricate an active countdown.
    ev = mhw::riseEvaluateAbnormality(*poison, debuffOn, 0, 0.0F, false);
    check(!ev.active, "a failed value read reports nothing (no fabricated timer)");

    // ---- 5. Panel display text ------------------------------------------
    check(mhw::riseAbnormalityTimerText(1.0F, true, false, 0.0F)
              == QStringLiteral("∞"),
          "infinite entries render as ∞");
    check(mhw::riseAbnormalityTimerText(43.0F, false, true, 120.0F)
              == QStringLiteral("43/120"),
          "buildup counters render as value/max");
    check(mhw::riseAbnormalityTimerText(43.0F, false, true, 0.0F)
              == QStringLiteral("43"),
          "buildup without MaxBuildup renders the bare value");
    check(mhw::riseAbnormalityTimerText(11.9F, false, false, 0.0F)
              == QStringLiteral("11s"),
          "countdowns render as integer seconds");

    // ---- 6. Locale-aware names (v0.9 i18n, WS-B) -------------------------
    // Every row carries the en-us.xml column; the two display helpers pick by
    // mhw::StringTable::instance().isEnglish(), with an empty locale reading
    // as Chinese (the pre-i18n behaviour).
    mhw::StringTable &strings = mhw::StringTable::instance();
    check(!strings.isEnglish(), "empty locale reads as Chinese by default");

    check(demonDrug && std::strcmp(demonDrug->nameEn, "Demondrug") == 0,
          "en column: ABNORMALITY_DEMONDRUG = Demondrug (en-us.xml)");
    check(megaDrug && std::strcmp(megaDrug->nameEn, "Mega Demondrug") == 0,
          "en column: ABNORMALITY_MEGA_DEMONDRUG = Mega Demondrug");
    check(mightSeed && std::strcmp(mightSeed->nameEn, "Might Seed") == 0,
          "en column: ABNORMALITY_MIGHT_SEED = Might Seed");
    check(poison && std::strcmp(poison->nameEn, "Poison") == 0,
          "en column: ABNORMALITY_POISON = Poison");
    check(frostcraft && std::strcmp(frostcraft->nameEn, "Frostcraft") == 0,
          "en column: ABNORMALITY_FROSTCRAFT_RISE = Frostcraft");
    check(unknownConsumable && std::strcmp(unknownConsumable->nameEn, "Unknown") == 0,
          "en column: the nameless-entry fallback is Unknown in en-us.xml too");

    check(mhw::riseAbnormalityDisplayName(*demonDrug) == QStringLiteral("鬼人药"),
          "zh locale: riseAbnormalityDisplayName returns the zh column");
    check(mhw::riseAilmentDisplayName(0) == QStringLiteral("麻痹")
              && mhw::riseAilmentDisplayName(16) == QStringLiteral("钢龙毒"),
          "zh locale: ailment slots 0/16 keep the pre-i18n labels");
    check(mhw::riseAilmentDisplayName(99) == QStringLiteral("异常99"),
          "zh locale: an unknown ailment slot keeps the historical fallback");

    check(strings.load(QStringLiteral("en-US")),
          "en-US loads from the bundled qrc");
    check(strings.isEnglish(), "isEnglish() true after loading en-US");
    check(mhw::riseAbnormalityDisplayName(*demonDrug) == QStringLiteral("Demondrug"),
          "en-US: riseAbnormalityDisplayName returns the en column");
    check(mhw::riseAbnormalityDisplayName(*poison) == QStringLiteral("Poison"),
          "en-US: ABN_POISON renders Poison");
    check(mhw::riseAbnormalityDisplayName(*windMantle) == QStringLiteral("Wind Mantle"),
          "en-US: ABN_WINDMANTLE renders Wind Mantle");
    check(mhw::riseAbnormalityDisplayName(*leeched) == QStringLiteral("Leeched"),
          "en-US: ABN_LEECHED renders Leeched");
    check(mhw::riseAilmentNameEn(0) != nullptr
              && std::strcmp(mhw::riseAilmentNameEn(0), "Paralysis") == 0,
          "en column: ailment slot 0 = Paralysis (MonsterData.xml AILMENT_PARALYSIS)");
    check(mhw::riseAilmentNameEn(4) != nullptr
              && std::strcmp(mhw::riseAilmentNameEn(4), "Poison") == 0,
          "en column: ailment slot 4 = Poison");
    check(mhw::riseAilmentNameEn(7) != nullptr
              && std::strcmp(mhw::riseAilmentNameEn(7), "Ride") == 0,
          "en column: ailment slot 7 = Ride (AILMENT_RIDE, not AILMENT_MOUNT)");
    check(mhw::riseAilmentNameEn(16) != nullptr
              && std::strcmp(mhw::riseAilmentNameEn(16), "Steel Fang") == 0,
          "en column: ailment slot 16 = Steel Fang (AILMENT_STEELFANG)");
    check(mhw::riseAilmentNameEn(17) == nullptr,
          "en column: slot 17 is outside the table");
    check(mhw::riseAilmentDisplayName(0) == QStringLiteral("Paralysis"),
          "en-US: ailment slot 0 renders Paralysis");
    check(mhw::riseAilmentDisplayName(12) == QStringLiteral("Pitfall Trap"),
          "en-US: ailment slot 12 renders Pitfall Trap");
    check(mhw::riseAilmentDisplayName(99) == QStringLiteral("Ailment 99"),
          "en-US: an unknown ailment slot renders the translated fallback");

    // Restore the default and re-assert the original zh values.
    check(strings.load(QStringLiteral("zh-CN")), "zh-CN loads again");
    check(!strings.isEnglish(), "isEnglish() false after restoring zh-CN");
    check(mhw::riseAbnormalityDisplayName(*demonDrug) == QStringLiteral("鬼人药")
              && mhw::riseAbnormalityDisplayName(*frostcraft) == QStringLiteral("寒气炼成"),
          "zh-CN restored: en-column switch is fully reversible");
    check(mhw::riseAilmentDisplayName(0) == QStringLiteral("麻痹")
              && mhw::riseAilmentDisplayName(3) == QStringLiteral("闪光")
              && mhw::riseAilmentDisplayName(7) == QStringLiteral("乘骑")
              && mhw::riseAilmentDisplayName(14) == QStringLiteral("捕获"),
          "zh-CN restored: ailment slots 0/3/7/14 are unchanged");

    if (failures == 0) {
        std::printf("rise-abnormality-tests: ALL PASSED\n");
    }
    return failures == 0 ? 0 : 1;
}
