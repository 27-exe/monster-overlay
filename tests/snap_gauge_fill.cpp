// v0.10.10 off-screen probe: part-gauge FILL COLOUR acceptance.
//
// One MonsterPanel render per scenario, each with a SINGLE part card, so
// every track/bar in the output PNG is unambiguously attributable (the
// .mini gauge is the lowest 4px track in the image; the Row 1 flinch gauge
// sits above it; the tenderize strip between them).
//
// Scenario set (all World, Teostra schema id 18, part_schemas.cpp:138-147):
//
//   h1_teostra_head_broken      Breakable head, health==maxHealth (the
//                               refilled layer a broken Teostra head shows),
//                               counter>0 → isBroken. Flinch layer + active
//                               tenderize. THE changed card.
//   h2_teostra_head_unbroken    Same shape, isBroken=false: same-shape
//                               control. Both versions must paint #78909c.
//   t1_teostra_tail_broken_brk  Severable tail, counter>0 → broken with the
//                               "sev" tag: old code painted amber, new paints
//                               the broken grey.
//   w1_teostra_wings_untouched  Untouched Breakable wing with tenderize +
//                               flinch: fill #78909c, blue flinch, amber
//                               tenderize — the must-not-change card.
//   tl_teostra_tail_untouched   Untouched Breakable tail, no flinch, no
//                               tenderize: plain #78909c fill.
//   hn_teostra_horns_untouched  Untouched Severable horns, 75% fill.
//   full_teostra                All five parts on one panel (artifact +
//                               height comparison against the old build).
//
// Prints one machine-parsable line per scenario so the pixel analysis can
// be driven from the recorded PcEntry the painter received.

#include "ui/panel_monster.h"
#include "ui/panel_sections.h"
#include "core/string_table.h"
#include "monster/monster_types.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {

using mhw::GameId;
using mhw::MonsterSnapshot;
using mhw::PartSnapshot;
using mhw::PartType;

// ---- target palette (verified against panel_monster.cpp) ----------------
constexpr int kBrokenR = 113, kBrokenG = 113, kBrokenB = 122; // #71717A
constexpr int kUntR = 120, kUntG = 144, kUntB = 156;          // #78909c
constexpr int kAmberR = 246, kAmberG = 165, kAmberB = 34;     // #f6a522
constexpr int kPinkR = 244, kPinkG = 17, kPinkB = 98;         // #f41162
constexpr int kBlueR = 0x4B, kBlueG = 0x8E, kBlueB = 0xEE;    // #4B8EEE
constexpr int kTrackR = 10, kTrackG = 11, kTrackB = 12;       // #0a0b0c

struct PartSpec {
    const char *key;
    const char *name;
    PartType     type;
    bool         isSeverable{false};
    bool         isBreakable{false};
    bool         isBroken{false};
    bool         isPartSevered{false};
    float        health{0.0F};
    float        maxHealth{0.0F};
    float        flinch{0.0F};
    float        maxFlinch{0.0F};
    int          counter{0};
    float        tnDur{0.0F};
    float        tnMax{0.0F};
    int          index{-1};
};

MonsterSnapshot worldTeostra(const QVector<PartSpec> &parts)
{
    MonsterSnapshot m;
    m.game         = GameId::World;
    m.id           = 18;                       // Teostra, part_schemas.cpp:138
    m.internalName = QStringLiteral("Teostra");
    m.health       = 7500.0F;
    m.maxHealth    = 7500.0F;
    for (const auto &s : parts) {
        PartSnapshot p;
        p.index           = s.index;
        p.name            = QString::fromUtf8(s.name);
        p.partType        = s.type;
        p.isSeverable     = s.isSeverable;
        p.isBreakable     = s.isBreakable;
        p.isBroken        = s.isBroken;
        p.isPartSevered   = s.isPartSevered;
        p.health          = s.health;
        p.maxHealth       = s.maxHealth;
        p.flinch          = s.flinch;
        p.maxFlinch      = s.maxFlinch;
        p.counter         = s.counter;
        p.tenderizeDuration    = s.tnDur;
        p.tenderizeMaxDuration = s.tnMax;
        m.parts.append(p);
    }
    return m;
}

QImage renderPanel(MonsterPanel &panel)
{
    QImage img(panel.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    panel.render(&p, QPoint(), panel.rect());
    p.end();
    return img;
}

// Count of pixels matching an exact RGB (ignoring alpha — the fills are
// drawn opaque over the dark card background).
int countColor(const QImage &img, int r, int g, int b)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qRed(line[x]) == r && qGreen(line[x]) == g
                && qBlue(line[x]) == b)
                ++n;
        }
    }
    return n;
}

void reportScenario(const char *key, const PartSpec &spec,
                    const QString &outPath)
{
    MonsterSnapshot snap = worldTeostra({spec});

    // Fresh panel per scenario: buildPcList() MUTATES partTrack_ (15 s
    // AutoHide), so reusing one panel across scenarios pin or hide cards.
    MonsterPanel panel;
    panel.setScale(1.0, false);
    panel.setOpacity(1.0, false);
    panel.setMultiplayer(false);
    panel.show();
    QApplication::processEvents();
    panel.update(snap);
    QApplication::processEvents();
    panel.repaint();
    QApplication::processEvents();
    const QImage img = renderPanel(panel);
    if (!img.save(outPath))
        std::printf("SCENARIO %s SAVE_FAILED %s\n", key,
                    outPath.toLocal8Bit().constData());

    // Post-render: the PcEntry the painter actually consumed. Called AFTER
    // the render so the partTrack_ mutation it performs cannot change what
    // was painted above.
    const QVector<PcEntry> entries =
        buildPcList(panel, snap.parts, 0, true);
    QString ent;
    if (!entries.isEmpty()) {
        const PcEntry &e = entries.first();
        ent = QStringLiteral("name=%1 tagKind=%2 broken=%3 counter=%4 pct=%5 "
                             "flinchPct=%6 flinchMax=%7 tnDur=%8 value=%9")
                  .arg(e.name, e.tagKind.isEmpty()
                                   ? QStringLiteral("-") : e.tagKind)
                  .arg(e.broken ? 1 : 0)
                  .arg(e.counter)
                  .arg(static_cast<double>(e.pct), 0, 'f', 3)
                  .arg(static_cast<double>(e.flinchPct), 0, 'f', 3)
                  .arg(static_cast<double>(e.flinchMax), 0, 'f', 1)
                  .arg(static_cast<double>(e.tenderizeDuration), 0, 'f', 1)
                  .arg(e.value);
    }

    std::printf("SCENARIO %s IMG=%s SIZE=%dx%d %s "
                "PIX broken=%d untouched=%d amber=%d pink=%d blue=%d "
                "track=%d\n",
                key, outPath.toLocal8Bit().constData(),
                img.width(), img.height(),
                ent.toLocal8Bit().constData(),
                countColor(img, kBrokenR, kBrokenG, kBrokenB),
                countColor(img, kUntR, kUntG, kUntB),
                countColor(img, kAmberR, kAmberG, kAmberB),
                countColor(img, kPinkR, kPinkG, kPinkB),
                countColor(img, kBlueR, kBlueG, kBlueB),
                countColor(img, kTrackR, kTrackG, kTrackB));
    std::fflush(stdout);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    if (!::mhw::StringTable::instance().load(QStringLiteral("zh-CN")))
        std::printf("warning: zh-CN strings not loaded\n");

    QString outDir = QStringLiteral(".");
    QStringList positional;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a.startsWith(QStringLiteral("--out=")))
            outDir = a.mid(6);
        else if (!a.startsWith(QStringLiteral("-")))
            positional << a;
    }
    if (!positional.isEmpty())
        outDir = positional[0];

    // The probe is a diagnostic, not a test: create its output directory so a
    // fresh workspace path does not turn every scenario into SAVE_FAILED.
    QDir().mkpath(outDir);

    // ---- single-card scenarios ------------------------------------------
    // Teostra head (Breakable, thresholds "1"): the card the change is
    // about. A broken head REFILLS its layer to 100% (the reader's
    // isBroken formula: Health == MaxHealth && Counter > 0), so the .mini
    // fill is full width in every branch — only the COLOUR can differ.
    PartSpec headBroken;
    headBroken.key  = "h1_teostra_head_broken";
    headBroken.name = "头部";
    headBroken.type = PartType::Breakable;
    headBroken.isBreakable = true;
    headBroken.isBroken = true;
    headBroken.health = 1750.0F;      // refilled layer: 100%
    headBroken.maxHealth = 1750.0F;
    headBroken.flinch = 875.0F;       // 50% flinch layer
    headBroken.maxFlinch = 1750.0F;
    headBroken.counter = 1;
    headBroken.tnDur = 24.0F;         // active tenderize
    headBroken.tnMax = 30.0F;
    headBroken.index = -3;            // World normal key (-1 - slot)
    reportScenario("h1_teostra_head_broken", headBroken,
                   outDir + "/gauge_h1_head_broken.png");

    // Same-shape control: identical geometry, isBroken=false.
    PartSpec headOk = headBroken;
    headOk.key = "h2_teostra_head_unbroken";
    headOk.isBroken = false;
    reportScenario("h2_teostra_head_unbroken", headOk,
                   outDir + "/gauge_h2_head_unbroken.png");

    // Teostra tail (Severable, thresholds "1"), counter>0 → broken with the
    // "sev" tag. Old code painted this fill amber #f6a522.
    PartSpec tailBroken;
    tailBroken.key = "t1_teostra_tail_broken_sev";
    tailBroken.name = "尾巴";
    tailBroken.type = PartType::Severable;
    tailBroken.isSeverable = true;
    tailBroken.isBreakable = true;    // schema row 0 has thresholds "1"
    tailBroken.isBroken = true;
    tailBroken.isPartSevered = true;
    tailBroken.health = 1375.0F;
    tailBroken.maxHealth = 2750.0F;
    tailBroken.counter = 1;
    tailBroken.index = 1000;          // World severable key (1000 + s)
    reportScenario("t1_teostra_tail_broken_sev", tailBroken,
                   outDir + "/gauge_t1_tail_broken.png");

    // Teostra wings (Breakable, thresholds "1", tenderizeIds {1,4}):
    // untouched — must keep #78909c, and the amber tenderize + blue flinch
    // rows must survive.
    PartSpec wings;
    wings.key = "w1_teostra_wings_untouched";
    wings.name = "翼";
    wings.type = PartType::Breakable;
    wings.isBreakable = true;
    wings.health = 875.0F;            // 50%
    wings.maxHealth = 1750.0F;
    wings.flinch = 1312.0F;           // 75%
    wings.maxFlinch = 1750.0F;
    wings.tnDur = 12.0F;              // active tenderize
    wings.tnMax = 90.0F;
    wings.index = 1001;
    reportScenario("w1_teostra_wings_untouched", wings,
                   outDir + "/gauge_w1_wings_untouched.png");

    // Teostra normal tail (Breakable row 7, no thresholds): untouched, no
    // flinch layer, no tenderize → a plain full #78909c fill.
    PartSpec tailOk;
    tailOk.key = "tl_teostra_tail_untouched";
    tailOk.name = "尾巴";
    tailOk.type = PartType::Breakable;
    tailOk.isBreakable = true;
    tailOk.health = 2750.0F;
    tailOk.maxHealth = 2750.0F;
    tailOk.index = -4;
    reportScenario("tl_teostra_tail_untouched", tailOk,
                   outDir + "/gauge_tl_tail_untouched.png");

    // Teostra horns (Severable row 1): untouched, 75% fill, no flinch.
    PartSpec horns;
    horns.key = "hn_teostra_horns_untouched";
    horns.name = "角";
    horns.type = PartType::Severable;
    horns.isSeverable = true;
    horns.health = 1500.0F;
    horns.maxHealth = 2000.0F;
    horns.index = 1002;
    reportScenario("hn_teostra_horns_untouched", horns,
                   outDir + "/gauge_hn_horns_untouched.png");

    // ---- full-panel artifact (all five displayable cards) ---------------
    {
        MonsterSnapshot snap =
            worldTeostra({tailBroken, horns, headBroken, wings, tailOk});
        MonsterPanel panel;
        panel.setScale(1.0, false);
        panel.setOpacity(1.0, false);
        panel.setMultiplayer(false);
        panel.show();
        QApplication::processEvents();
        panel.update(snap);
        QApplication::processEvents();
        panel.repaint();
        QApplication::processEvents();
        const QImage img = renderPanel(panel);
        const QString path = outDir + "/gauge_full_teostra.png";
        if (!img.save(path))
            std::printf("SCENARIO full_teostra SAVE_FAILED %s\n",
                        path.toLocal8Bit().constData());
        std::printf("SCENARIO full_teostra IMG=%s SIZE=%dx%d "
                    "PIX broken=%d untouched=%d amber=%d pink=%d blue=%d "
                    "track=%d\n",
                    path.toLocal8Bit().constData(),
                    img.width(), img.height(),
                    countColor(img, kBrokenR, kBrokenG, kBrokenB),
                    countColor(img, kUntR, kUntG, kUntB),
                    countColor(img, kAmberR, kAmberG, kAmberB),
                    countColor(img, kPinkR, kPinkG, kPinkB),
                    countColor(img, kBlueR, kBlueG, kBlueB),
                    countColor(img, kTrackR, kTrackG, kTrackB));
        std::fflush(stdout);
    }

    return 0;
}
