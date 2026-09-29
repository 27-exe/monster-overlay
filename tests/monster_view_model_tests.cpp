// SPDX-License-Identifier: Apache-2.0
//
// Direct unit tests for mhw::MonsterPartListBuilder — the 337-line
// translation unit S3-TEST-COVERAGE.md §4 B1 measured at ZERO ctest coverage
// ("改了 sanitizePartValue/sanitizeQ10/partDisplayLayers/
// MonsterPartListBuilder::build 任何规则，42 个 case 全绿").
//
// The class is QtCore-only (QString / QVector) plus the pure monster data
// headers and src/rise/mhr_part_names.h, so this target compiles the
// ViewModel's OWN two sources directly instead of linking mhw-ui — exactly
// the pattern player-view-model-tests and console-layout-store-tests use.
//
// What is asserted here, in the order the code runs:
//
//  A. CARD CONTENT — the per-field rules of MonsterPartListBuilder::build:
//       * the localized name fallback `部位 N` / `Part N` for a slot the
//         readers never named (an empty name must not paint an blank card);
//       * the per-PartType tag 斩/破 and its tagKind, plus the empty tag a
//         Flinch part correctly has;
//       * the tenderize strip is gated by the `onTenderize` SECTION BIT, in
//         this one place, so the reservation and the draw can never disagree
//         (that disagreement is what shipped the invisible v0.10.3 strip);
//       * flinchMax / breakMax are PRESENCE flags, distinct from a 0 % value
//         — a layer that exists and is empty is still reserved;
//       * primaryLayer/valueLayer per (game, PartType), and the HunterPie
//         Row 3 Conditional switch that flips the VALUE row to Flinch once a
//         part is severed/broken while the gauge keeps its source;
//       * pct is clamped, and 0 (not NaN) when the denominator is unusable;
//       * hasPrimaryGauge = !(World && multiplayer) — the v0.10.8 decision;
//       * counter/broken/severed travel through verbatim.
//
//  B. NaN SAFETY — the 2026-09-22 crash fix. A single non-finite read used
//     to reach Qt6's qRound() and abort the overlay inside paintPanel(). The
//     whole part is fed NaN/Inf here; the process must survive and every
//     derived field must read as "no layer", never as a garbage bar.
//
//  C. PARTAUTOHIDE — the 15 s change-based silence table, its slot-key
//     normalization and its two documented safety modes. This is the state
//     machine that had zero coverage because it used to live in a QWidget's
//     private member.
//
// The expected values below are read off the implementation, not derived
// from a design doc: this is a characterization suite whose job is to make a
// future behavioural change FAIL rather than pass silently.

#include "ui/viewmodel/monster_view_model.h"

#include "core/string_table.h"
#include "monster/monster_types.h"
#include "rise/mhr_part_names.h"

#include <QCoreApplication>
#include <QString>
#include <QVector>

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace {

int failures = 0;
int checks   = 0;

void check(bool cond, const std::string &what)
{
    ++checks;
    if (cond) {
        std::printf("PASS: %s\n", what.c_str());
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
    std::fflush(stdout);
    std::fflush(stderr);
}

[[nodiscard]] bool near(float a, float b)
{
    return std::fabs(a - b) < 0.001F;
}

// --------------------------------------------------------------- fixtures

mhw::PartSnapshot severPart(int index, const QString &name,
                            float health, float maxHealth)
{
    mhw::PartSnapshot p;
    p.index       = index;
    p.name        = name;
    p.partType    = mhw::PartType::Severable;
    p.isSeverable = true;
    p.health      = health;
    p.maxHealth   = maxHealth;
    return p;
}

mhw::PartSnapshot breakPart(int index, const QString &name,
                            float health, float maxHealth)
{
    mhw::PartSnapshot p;
    p.index       = index;
    p.name        = name;
    p.partType    = mhw::PartType::Breakable;
    p.isBreakable = true;
    p.health      = health;
    p.maxHealth   = maxHealth;
    return p;
}

mhw::PartSnapshot flinchPart(int index, const QString &name,
                             float flinch, float maxFlinch)
{
    mhw::PartSnapshot p;
    p.index     = index;
    p.name      = name;
    p.partType  = mhw::PartType::Flinch;
    p.flinch    = flinch;
    p.maxFlinch = maxFlinch;
    return p;
}

using Cards = QVector<PcEntry>;

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    mhw::StringTable &strings = mhw::StringTable::instance();

    // Load zh-CN BEFORE any build(): the card fields (部位 N, 斩, 破) are
    // resolved through StringTable at build time, so an unloaded table
    // would return the raw key and every tag assertion below would be
    // vacuous. The locale case further down flips to en-US and back.
    check(strings.load(QStringLiteral("zh-CN")),
          "locale: zh-CN loads from the bundled qrc before the first build");
    check(!strings.isEnglish(),
          "locale: zh-CN is not the English variant");

    // ======================================================================
    // A1. The localized name fallback (part the reader never named)
    // ======================================================================
    // displayableParts() on the panel side filters, but a part with an empty
    // name reaches build() when the reader dropped its label; the card must
    // still identify itself by INDEX rather than paint nothing.
    {
        mhw::MonsterPartListBuilder builder;
        Cards out = builder.build({flinchPart(7, QString(), 12, 24)}, 1000,
                                  false, false, mhw::GameId::Rise, false);
        check(out.size() == 1, "name: an unnamed part still produces a card");
        if (out.size() == 1)
            check(out[0].name == QStringLiteral("部位 7"),
                  "name: an empty name falls back to 部位 <index> in zh-CN");
    }

    // ======================================================================
    // A2. The per-PartType tag chip
    // ======================================================================
    {
        mhw::MonsterPartListBuilder builder;
        const Cards out = builder.build({severPart(3, "尾", 34000, 57000)},
                                        1000, false, false,
                                        mhw::GameId::Rise, false);
        check(out.size() == 1, "tag: one severable part -> one card");
        if (out.size() == 1) {
            check(out[0].tag == QStringLiteral("斩"),
                  "tag: a Severable part is tagged 斩 (zh-CN)");
            check(out[0].tagKind == QStringLiteral("sev"),
                  "tag: a Severable part's tagKind is sev");
        }
    }
    {
        mhw::MonsterPartListBuilder builder;
        const Cards out = builder.build({breakPart(1, "胴", 1250, 3500)},
                                        1000, false, false,
                                        mhw::GameId::Rise, false);
        if (out.size() == 1) {
            check(out[0].tag == QStringLiteral("破"),
                  "tag: a Breakable part is tagged 破 (zh-CN)");
            check(out[0].tagKind == QStringLiteral("brk"),
                  "tag: a Breakable part's tagKind is brk");
        } else {
            check(false, "tag: a Breakable part must produce a card");
        }
    }
    {
        mhw::MonsterPartListBuilder builder;
        const Cards out = builder.build({flinchPart(2, "腿", 12, 24)},
                                        1000, false, false,
                                        mhw::GameId::Rise, false);
        if (out.size() == 1) {
            check(out[0].tag.isEmpty() && out[0].tagKind.isEmpty(),
                  "tag: a Flinch-only part carries NO tag (it is neither "
                  "severed nor broken)");
        } else {
            check(false, "tag: a Flinch part must produce a card");
        }
    }

    // ======================================================================
    // A3. The tenderize strip is gated by the onTenderize section bit
    // ======================================================================
    // v0.10.3 shipped an invisible strip because the draw gate and the
    // height reservation disagreed. Both now read this ONE flag.
    {
        mhw::PartSnapshot p = severPart(3, "角", 1500, 2000);
        p.tenderizeDuration    = 24.0F;
        p.tenderizeMaxDuration = 30.0F;

        mhw::MonsterPartListBuilder on;
        const Cards withBit = on.build({p}, 1000, true, false,
                                       mhw::GameId::Rise, false);
        check(withBit.size() == 1
                  && near(withBit[0].tenderizeDuration, 24.0F)
                  && near(withBit[0].tenderizeMaxDuration, 30.0F),
              "tenderize: with the section bit ON the card carries "
              "duration/maxDuration");

        mhw::MonsterPartListBuilder off;
        const Cards without = off.build({p}, 1000, false, false,
                                        mhw::GameId::Rise, false);
        check(without.size() == 1
                  && near(without[0].tenderizeDuration, 0.0F)
                  && near(without[0].tenderizeMaxDuration, 0.0F),
              "tenderize: with the section bit OFF the card zeroes BOTH "
              "tenderize fields, so nothing reserves strip height");
    }

    // ======================================================================
    // A4. flinchMax / breakMax are presence flags, not 0 % values
    // ======================================================================
    // The header's own warning: collapsing "no layer" and "layer at 0 %"
    // into one test is exactly the v0.10.3 bug class.
    {
        mhw::PartSnapshot p = severPart(3, "尾", 34000, 57000);
        p.flinch    = 300.0F;
        p.maxFlinch = 600.0F;
        const Cards out = mhw::MonsterPartListBuilder{}.build(
            {p}, 1000, false, false, mhw::GameId::Rise, false);
        check(out.size() == 1 && near(out[0].flinchMax, 600.0F),
              "layers: a live flinch layer records maxFlinch as the denominator");
        check(out.size() == 1 && near(out[0].flinchPct, 0.5F),
              "layers: flinchPct is flinch/maxFlinch = 0.5");
    }
    {
        // Layer EXISTS but is currently EMPTY: the row must still be
        // reserved (flinchMax > 0) even though the fill fraction is 0.
        mhw::PartSnapshot p = severPart(3, "尾", 34000, 57000);
        p.flinch    = 0.0F;
        p.maxFlinch = 600.0F;
        const Cards out = mhw::MonsterPartListBuilder{}.build(
            {p}, 1000, false, false, mhw::GameId::Rise, false);
        check(out.size() == 1 && near(out[0].flinchMax, 600.0F),
              "layers: an EMPTY-but-present flinch layer keeps flinchMax > 0 "
              "(the row is reserved, not dropped)");
        check(out.size() == 1 && near(out[0].flinchPct, 0.0F),
              "layers: that same layer reports 0 % fill");
    }
    {
        // No flinch layer at all (World body part).
        mhw::PartSnapshot p = severPart(3, "胴", 34000, 57000);
        const Cards out = mhw::MonsterPartListBuilder{}.build(
            {p}, 1000, false, false, mhw::GameId::World, false);
        check(out.size() == 1 && near(out[0].flinchMax, 0.0F),
              "layers: no flinch layer leaves flinchMax at 0 (row omitted)");
    }
    {
        // Rise Severable part's break layer: only present when the part
        // actually has a breakable body.
        mhw::PartSnapshot withBody = severPart(3, "尾", 34000, 57000);
        withBody.breakHealth    = 900.0F;
        withBody.breakMaxHealth = 1800.0F;
        const Cards a = mhw::MonsterPartListBuilder{}.build(
            {withBody}, 1000, false, false, mhw::GameId::Rise, false);
        check(a.size() == 1 && near(a[0].breakMax, 1800.0F),
              "layers: a nonzero breakMaxHealth fills breakMax");
        check(a.size() == 1 && near(a[0].breakPct, 0.5F),
              "layers: breakPct is breakHealth/breakMaxHealth = 0.5");

        mhw::PartSnapshot noBody = severPart(3, "翼", 34000, 57000);
        const Cards b = mhw::MonsterPartListBuilder{}.build(
            {noBody}, 1000, false, false, mhw::GameId::Rise, false);
        check(b.size() == 1 && near(b[0].breakMax, 0.0F),
              "layers: breakMaxHealth 0 leaves breakMax at 0 (no break gauge)");
    }

    // ======================================================================
    // A5. primaryLayer / valueLayer / Row 3 Conditional
    // ======================================================================
    // HunterPie BossMonsterSeverablePartView.xaml:101-134: after a sever the
    // VALUE row flips to Flinch/MaxFlinch while the gauge keeps binding
    // Sever/MaxSever. Deleting that switch shows stale sever numbers on a
    // severed tail — nothing else in the suite would notice.
    {
        // Rise Severable, not severed: value = health pair.
        mhw::PartSnapshot p = severPart(3, "尾", 34000, 57000);
        p.flinch    = 11.0F;
        p.maxFlinch = 22.0F;
        const Cards out = mhw::MonsterPartListBuilder{}.build(
            {p}, 1000, false, false, mhw::GameId::Rise, false);
        check(out.size() == 1
                  && out[0].primaryLayer == PcEntry::Layer::Sever,
              "layers: a Rise Severable part's primary gauge binds Sever");
        check(out.size() == 1
                  && out[0].valueLayer == PcEntry::Layer::Sever,
              "layers: an UNSEVERED Severable part shows the Sever value row");
        check(out.size() == 1
                  && out[0].value == mhw::compactPartHealth(34000.0F, 57000.0F),
              "layers: that value row is the compacted sever pair 34k/57k");
        check(out.size() == 1 && near(out[0].pct, 34000.0F / 57000.0F),
              "layers: pct is health/maxHealth");
    }
    {
        // Same part, now severed: value row flips, gauge does not.
        mhw::PartSnapshot p = severPart(3, "尾", 34000, 57000);
        p.flinch       = 11.0F;
        p.maxFlinch    = 22.0F;
        p.isPartSevered = true;
        const Cards out = mhw::MonsterPartListBuilder{}.build(
            {p}, 1000, false, false, mhw::GameId::Rise, false);
        check(out.size() == 1 && out[0].severed,
              "row3: the severed flag travels onto the card");
        check(out.size() == 1
                  && out[0].primaryLayer == PcEntry::Layer::Sever,
              "row3: severing does NOT rebind the primary gauge");
        check(out.size() == 1
                  && out[0].valueLayer == PcEntry::Layer::Flinch,
              "row3: severing DOES flip the value row to Flinch");
        check(out.size() == 1
                  && out[0].value == mhw::compactPartHealth(11.0F, 22.0F),
              "row3: the flipped value row is the flinch pair 11/22");
    }
    {
        // Rise Breakable, broken: same conditional flip.
        mhw::PartSnapshot p = breakPart(1, "胴", 1250, 3500);
        p.flinch    = 77.0F;
        p.maxFlinch = 88.0F;
        p.isBroken  = true;
        const Cards out = mhw::MonsterPartListBuilder{}.build(
            {p}, 1000, false, false, mhw::GameId::Rise, false);
        check(out.size() == 1 && out[0].broken,
              "row3: the broken flag travels onto the card");
        check(out.size() == 1
                  && out[0].primaryLayer == PcEntry::Layer::Break,
              "row3: a Breakable part's primary gauge binds Break");
        check(out.size() == 1
                  && out[0].valueLayer == PcEntry::Layer::Flinch,
              "row3: breaking a Breakable part flips the value row to Flinch");
        check(out.size() == 1
                  && out[0].value == mhw::compactPartHealth(77.0F, 88.0F),
              "row3: the flipped value row is the flinch pair 77/88");
    }
    {
        // Flinch-only part: flinch on every row.
        mhw::PartSnapshot p = flinchPart(2, "腿", 12, 24);
        p.health    = 1.0F;
        p.maxHealth = 2.0F;
        const Cards out = mhw::MonsterPartListBuilder{}.build(
            {p}, 1000, false, false, mhw::GameId::Rise, false);
        check(out.size() == 1
                  && out[0].primaryLayer == PcEntry::Layer::Flinch
                  && out[0].valueLayer == PcEntry::Layer::Flinch,
              "layers: a Flinch-only part binds Flinch on both rows");
        check(out.size() == 1 && near(out[0].pct, 0.5F),
              "layers: its gauge reads flinch/maxFlinch, not the useless "
              "health pair");
        check(out.size() == 1
                  && out[0].value == mhw::compactPartHealth(12.0F, 24.0F),
              "layers: its value row is the compacted flinch pair");
    }
    {
        // The same dispatch on World: Severable -> Sever, Breakable ->
        // Break. The game switch must not silently collapse to one branch.
        const Cards sev = mhw::MonsterPartListBuilder{}.build(
            {severPart(1003, "尾", 100, 200)}, 1000, false, false,
            mhw::GameId::World, false);
        check(sev.size() == 1
                  && sev[0].primaryLayer == PcEntry::Layer::Sever,
              "layers: a WORLD Severable part still binds Sever");

        const Cards brk = mhw::MonsterPartListBuilder{}.build(
            {breakPart(1, "胴", 100, 200)}, 1000, false, false,
            mhw::GameId::World, false);
        check(brk.size() == 1
                  && brk[0].primaryLayer == PcEntry::Layer::Break,
              "layers: a WORLD Breakable part still binds Break");

        const Cards fl = mhw::MonsterPartListBuilder{}.build(
            {flinchPart(2, "腿", 3, 6)}, 1000, false, false,
            mhw::GameId::World, false);
        check(fl.size() == 1
                  && fl[0].primaryLayer == PcEntry::Layer::Flinch,
              "layers: a WORLD Flinch part binds Flinch");
    }

    // pct clamping and the unusable-denominator case.
    {
        mhw::PartSnapshot over = severPart(3, "尾", 5000, 2000);
        const Cards a = mhw::MonsterPartListBuilder{}.build(
            {over}, 1000, false, false, mhw::GameId::Rise, false);
        check(a.size() == 1 && near(a[0].pct, 1.0F),
              "layers: an over-100 % reading is clamped to a full gauge, "
              "not an overflowing bar");

        mhw::PartSnapshot zero = severPart(3, "尾", 0, 0);
        const Cards b = mhw::MonsterPartListBuilder{}.build(
            {zero}, 1000, false, false, mhw::GameId::Rise, false);
        // A 0/0 part is not "recent"-filtered (it is a snapshot problem, not
        // an AutoHide one): it still emits, with a 0 % gauge.
        check(b.size() == 1 && near(b[0].pct, 0.0F),
              "layers: a zero denominator yields pct 0 rather than NaN/Inf");
    }

    // counter / broken / severed travel through.
    {
        mhw::PartSnapshot p = breakPart(1, "胴", 1250, 3500);
        p.counter = 4;
        const Cards out = mhw::MonsterPartListBuilder{}.build(
            {p}, 1000, false, false, mhw::GameId::Rise, false);
        check(out.size() == 1 && out[0].counter == 4,
              "fields: the raw break counter reaches the card");
    }

    // ======================================================================
    // A6. hasPrimaryGauge — the v0.10.8 World-multiplayer decision
    // ======================================================================
    // Stale Health/MaxHealth on a non-host World client is why the whole
    // gauge is dropped rather than masked with "--/--".
    {
        const mhw::PartSnapshot p = severPart(3, "尾", 34000, 57000);
        const Cards riseMp = mhw::MonsterPartListBuilder{}.build(
            {p}, 1000, false, false, mhw::GameId::Rise, true);
        check(riseMp.size() == 1 && riseMp[0].hasPrimaryGauge,
              "gauge: RISE multiplayer KEEPS the primary gauge");

        const Cards worldMp =
            mhw::MonsterPartListBuilder{}.build(
                {p}, 1000, false, false, mhw::GameId::World, true);
        check(worldMp.size() == 1 && !worldMp[0].hasPrimaryGauge,
              "gauge: WORLD multiplayer DROPS the primary gauge");

        const Cards worldSolo =
            mhw::MonsterPartListBuilder{}.build(
                {p}, 1000, false, false, mhw::GameId::World, false);
        check(worldSolo.size() == 1 && worldSolo[0].hasPrimaryGauge,
              "gauge: WORLD solo KEEPS the primary gauge");
    }

    // ======================================================================
    // A7. Locale: the card strings come from the StringTable, so an en-US
    //     locale must relabel the SAME data.
    // ======================================================================
    {
        // The ZH assertions above already ran under zh-CN. Switch to en-US
        // and rebuild the SAME data: build() must relabel, because it reads
        // StringTable on every call rather than caching a QStringList (that
        // cache is what froze the v0.9 runtime language switch).
        check(strings.load(QStringLiteral("en-US")),
              "locale: en-US loads from the bundled qrc");
        check(strings.isEnglish(), "locale: isEnglish() is true for en-US");

        const Cards en = mhw::MonsterPartListBuilder{}.build(
            {severPart(3, "尾", 34000, 57000)}, 1000, false, false,
            mhw::GameId::Rise, false);
        check(en.size() == 1 && en[0].tag == QStringLiteral("SEV"),
              "locale: en-US tags a severable part SEV");
        check(en.size() == 1 && en[0].name == QStringLiteral("尾"),
              "locale: a NAMED part keeps its reader-provided name in any "
              "locale (the table only supplies the unnamed fallback)");

        const Cards enFallback =
            mhw::MonsterPartListBuilder{}.build(
                {flinchPart(7, QString(), 12, 24)}, 1000, false, false,
                mhw::GameId::Rise, false);
        check(enFallback.size() == 1
                  && enFallback[0].name == QStringLiteral("Part 7"),
              "locale: en-US falls back to Part <index>");

        // Restore zh-CN: the rest of the suite (and any later test binary in
        // the same ctest run is unaffected — this process is its own).
        check(strings.load(QStringLiteral("zh-CN")),
              "locale: zh-CN is restored");
    }

    // ======================================================================
    // B. NaN safety — the 2026-09-22 abort inside paintPanel()
    // ======================================================================
    // A slot that is mid-teardown can hand back NaN for one tick. The old
    // code passed it to qRound() (assert -> qFatal -> SIGABRT) and to
    // std::clamp(NaN,0,1) (returns NaN -> garbage bar widths). Every float
    // that reaches a gate, quantiser, clamp or divide must come through
    // sanitizePartValue()/sanitizeQ10() first.
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();

        mhw::PartSnapshot p = severPart(3, "尾", nan, inf);
        p.flinch            = nan;
        p.maxFlinch         = inf;
        p.breakHealth       = nan;
        p.breakMaxHealth    = inf;
        p.health            = nan;
        p.maxHealth         = inf;
        p.tenderizeDuration = nan;

        mhw::MonsterPartListBuilder builder;
        Cards out;
        try {
            out = builder.build({p}, 1000, true, false,
                                mhw::GameId::Rise, false);
        } catch (...) {
            check(false, "nan: build() must not throw on non-finite input");
        }
        check(true, "nan: build() completed (no Qt qRound assert / abort)");
        check(out.size() == 1, "nan: a NaN part still emits exactly one card");
        if (out.size() == 1) {
            check(near(out[0].flinchMax, 0.0F),
                  "nan: a non-finite flinch layer reads as NO layer (flinchMax 0)");
            check(near(out[0].breakMax, 0.0F),
                  "nan: a non-finite break layer reads as NO layer (breakMax 0)");
            check(near(out[0].pct, 0.0F),
                  "nan: the primary gauge fraction is 0, not NaN");
            check(out[0].value == mhw::compactPartHealth(0.0F, 0.0F),
                  "nan: the value row degrades to --/-- instead of garbage");
        }
    }

    // ======================================================================
    // C1. PartAutoHide — the 15 s change-based silence window
    // ======================================================================
    // HunterPie MonsterPartViewModel: AutoHidePartsDelay 15 s. The signature
    // is the eight fields the player perceives as movement; a card goes
    // quiet when NONE of them changed for 15 s, and comes back the instant
    // one does.
    {
        mhw::MonsterPartListBuilder builder;
        const Cards first = builder.build({severPart(3, "尾", 100, 200)},
                                          100000, false, false,
                                          mhw::GameId::Rise, false);
        check(first.size() == 1,
              "autohide: the first sighting of a part shows its card");

        const Cards inside = builder.build({severPart(3, "尾", 100, 200)},
                                           109999, false, false,
                                           mhw::GameId::Rise, false);
        check(inside.size() == 1,
              "autohide: 9.999 s after the change the card is still visible");

        // The window is `< 15000 ms`, not `<=`: at exactly 15 s the card
        // is already gone. Pin the exact boundary rather than a fuzzier
        // "still there at 15 s" so an off-by-one in the comparison is
        // caught — this is the one comparison a future edit is most likely
        // to fumble.
        const Cards atWindow = builder.build({severPart(3, "尾", 100, 200)},
                                            115000, false, false,
                                            mhw::GameId::Rise, false);
        check(atWindow.isEmpty(),
              "autohide: exactly 15000 ms of NO change hides the card "
              "(the comparison is strict `<`, not `<=`)");
    }

    // A changed value refreshes the stamp, so a part that keeps moving is
    // never silenced for being on screen a long time.
    {
        mhw::MonsterPartListBuilder builder;
        (void)builder.build({severPart(3, "尾", 100, 200)}, 100000,
                            false, false, mhw::GameId::Rise, false);
        const Cards moved = builder.build({severPart(3, "尾", 150, 200)},
                                          200000, false, false,
                                          mhw::GameId::Rise, false);
        check(moved.size() == 1,
              "autohide: a changed health value resets the 15 s window");
    }

    // A changed COUNTER also refreshes it — the player's 破 N feedback is
    // part of the signature.
    {
        mhw::MonsterPartListBuilder builder;
        (void)builder.build({breakPart(1, "胴", 1250, 3500)}, 100000,
                            false, false, mhw::GameId::Rise, false);
        mhw::PartSnapshot bumped = breakPart(1, "胴", 1250, 3500);
        bumped.counter = 1;
        const Cards out = builder.build({bumped}, 200000, false, false,
                                        mhw::GameId::Rise, false);
        check(out.size() == 1,
              "autohide: an incremented break counter resets the window");
    }

    // editMode disables AutoHide entirely: the console's layout preview and
    // the demo data must keep every card pinned. NOTE this is NOT the same
    // as the out-of-table degradation below — editMode skips the track
    // lookup, so an ALREADY-RECORDED slot is ignored and the card comes
    // back, whereas an unusable slot never reaches the lookup at all.
    {
        mhw::MonsterPartListBuilder builder;
        const Cards visible = builder.build({severPart(3, "尾", 100, 200)},
                                            100000, false, false,
                                            mhw::GameId::Rise, false);
        check(visible.size() == 1, "editMode: the part is visible first");

        // Silence it first (NOT in edit mode), then go to edit mode.
        check(builder.build({severPart(3, "尾", 100, 200)}, 300000,
                            false, false, mhw::GameId::Rise, false).isEmpty(),
              "editMode: the unchanged part really is hidden outside edit mode");

        const Cards inEdit = builder.build({severPart(3, "尾", 100, 200)},
                                           900000, false, true,
                                           mhw::GameId::Rise, false);
        check(inEdit.size() == 1,
              "editMode: a card hidden by the 15 s window REAPPEARS in "
              "editMode (AutoHide is bypassed, not just postponed)");
    }

    // resetAutoHide() is the v0.10.x-r2 fix: a NEW monster's parts must not
    // inherit the previous target's "already silent" state, which used to
    // make them invisible on their FIRST paint.
    {
        mhw::MonsterPartListBuilder builder;
        (void)builder.build({severPart(3, "尾", 100, 200)}, 100000,
                            false, false, mhw::GameId::Rise, false);
        check(builder.build({severPart(3, "尾", 100, 200)}, 115001,
                            false, false, mhw::GameId::Rise, false).isEmpty(),
              "autohide: the stale part really is hidden before the reset");

        builder.resetAutoHide();
        const Cards fresh = builder.build({severPart(3, "尾", 100, 200)},
                                          115001, false, false,
                                          mhw::GameId::Rise, false);
        check(fresh.size() == 1,
              "autohide: resetAutoHide() makes a new target's part visible "
              "immediately, even though nothing about it changed");
    }

    // ======================================================================
    // C2. The slot-key normalization — three reader index scales
    // ======================================================================
    // World severable 1000+s -> 100+s; World normal -1-n -> 1000+n; Rise i
    // -> i. Using the raw index used to put every World part outside the
    // 2048-entry table, so AutoHide silently did nothing on World at all.
    // Two parts must never alias onto one slot: if they did, refreshing ONE
    // would resurrect the OTHER.
    {
        mhw::MonsterPartListBuilder builder;
        const mhw::PartSnapshot a = severPart(1000, "尾", 100, 200);
        const mhw::PartSnapshot b = severPart(1001, "翼", 300, 400);
        check(builder.build({a, b}, 100000, false, false,
                            mhw::GameId::World, false).size() == 2,
              "slotkey: two World severable parts are two cards");

        // Both quiet by 200 s.
        check(builder.build({a, b}, 300000, false, false,
                            mhw::GameId::World, false).isEmpty(),
              "slotkey: unchanged World parts DO auto-hide (they are inside "
              "the table, not silently exempt)");

        // Move only the first; the second must stay hidden.
        const mhw::PartSnapshot movedA = severPart(1000, "尾", 150, 200);
        const Cards after = builder.build({movedA, b}, 300001, false, false,
                                          mhw::GameId::World, false);
        check(after.size() == 1 && after[0].name == QStringLiteral("尾"),
              "slotkey: refreshing one World part does NOT revive its "
              "neighbour (their 100+s slots are disjoint)");
    }
    {
        // World NORMAL parts: index -1-n -> 1000+n.
        mhw::MonsterPartListBuilder builder;
        const mhw::PartSnapshot a = breakPart(-1, "胴", 1250, 3500);
        const mhw::PartSnapshot b = breakPart(-2, "脚", 800, 900);
        check(builder.build({a, b}, 100000, false, false,
                            mhw::GameId::World, false).size() == 2,
              "slotkey: two World normal parts are two cards");
        check(builder.build({a, b}, 300000, false, false,
                            mhw::GameId::World, false).isEmpty(),
              "slotkey: unchanged World normal parts auto-hide");

        const mhw::PartSnapshot movedA = breakPart(-1, "胴", 1200, 3500);
        const Cards after = builder.build({movedA, b}, 300001, false, false,
                                          mhw::GameId::World, false);
        check(after.size() == 1 && after[0].name == QStringLiteral("胴"),
              "slotkey: 1000+n keeps the World normal parts disjoint too");
    }
    {
        // Rise parts: index i is already dense.
        mhw::MonsterPartListBuilder builder;
        const mhw::PartSnapshot a = severPart(3, "尾", 100, 200);
        const mhw::PartSnapshot b = severPart(4, "翼", 300, 400);
        check(builder.build({a, b}, 100000, false, false,
                            mhw::GameId::Rise, false).size() == 2,
              "slotkey: two Rise parts are two cards");
        check(builder.build({a, b}, 300000, false, false,
                            mhw::GameId::Rise, false).isEmpty(),
              "slotkey: unchanged Rise parts auto-hide");
        const mhw::PartSnapshot movedB = severPart(4, "翼", 350, 400);
        const Cards after = builder.build({a, movedB}, 300001, false, false,
                                          mhw::GameId::Rise, false);
        check(after.size() == 1 && after[0].name == QStringLiteral("翼"),
              "slotkey: the Rise slot key is one-to-one per part index");
    }

    // The documented safe failure mode: a key outside [0, 2048) leaves the
    // card ALWAYS VISIBLE rather than hiding it forever. A part array that
    // outgrows the table must show too much, never nothing.
    {
        mhw::MonsterPartListBuilder builder;
        const mhw::PartSnapshot outOfRange = severPart(60000, "异常", 100, 200);
        check(builder.build({outOfRange}, 100000, false, false,
                            mhw::GameId::Rise, false).size() == 1,
              "slotkey: an out-of-table index is visible on its first paint");
        const Cards later = builder.build({outOfRange}, 900000, false, false,
                                          mhw::GameId::Rise, false);
        check(later.size() == 1,
              "slotkey: an out-of-table index degrades to ALWAYS VISIBLE "
              "(the safe failure mode)");
    }

    // ======================================================================
    // C3. Ordering is preserved: the panel lays the grid out in this order.
    // ======================================================================
    {
        mhw::MonsterPartListBuilder builder;
        const Cards out = builder.build(
            {severPart(3, "尾", 34000, 57000),
             breakPart(1, "胴", 1250, 3500),
             flinchPart(2, "腿", 12, 24)},
            1000, false, false, mhw::GameId::Rise, false);
        check(out.size() == 3
                  && out[0].name == QStringLiteral("尾")
                  && out[1].name == QStringLiteral("胴")
                  && out[2].name == QStringLiteral("腿"),
              "order: build() preserves the snapshot's part order");
    }

    if (failures == 0)
        std::printf("monster-view-model-tests: ALL PASSED (%d checks)\n", checks);
    return failures == 0 ? 0 : 1;
}
