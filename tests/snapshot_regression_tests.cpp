// Offscreen pixel-regression suite for the overlay panels (v0.11 A-stage).
//
// Renders the same panel code paths the snap_* diagnostics use, into the
// same ARGB32 buffers, and compares every pixel against the committed
// baseline PNGs in tests/baseline/. Any drift — a moved label, a changed
// colour, a clipped gauge — fails the suite with the first differing pixel,
// so a "it still renders" eyeball check is no longer enough.
//
// Scope is deliberately the panels that the pre-existing snap_* tools
// already covered, so this adds an assertion where the pipeline existed
// but nothing enforced it:
//
//   player   world / rise      (snap_player_demo.cpp paths)
//   monster  + damage + pets   (snap_all_demo.cpp paths)
//   player   offline           (snap_player_offline.cpp path)
//   world    solo / multiplayer(snap_world_mp.cpp path)
//
// Locale: zh-CN is the baseline (the historical pre-i18n look). The suite
// loads exactly that, mirroring each snap tool's default.
//
// Refreshing the baselines after an INTENTIONAL visual change:
//   ./build/snapshot-regression-tests --write-baseline
// then commit the PNGs. Anything else that changes them must fail.
//
// Runs with QT_QPA_PLATFORM=offscreen (set by CMake). Exits 0 on pass,
// 1 on any pixel mismatch, 2 on a setup/usage error.

#include "ui/panel.h"
#include "ui/panel_player.h"
#include "ui/panel_monster.h"
#include "ui/panel_damage.h"
#include "ui/panel_pet_damage.h"
#include "core/string_table.h"
#include "player/player_types.h"
#include "rise/mhr_abnormalities.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QSize>
#include <QStringList>

#include <cstdio>
#include <cstring>

namespace {

constexpr const char *kBaselineDir =
#ifdef MHW_BASELINE_DIR
    MHW_BASELINE_DIR;
#else
    "tests/baseline";
#endif

struct Render {
    QImage img;
    bool ok{false};
};

// Mirrors snap_player_demo / snap_all_demo: scale+opacity forced, edit mode
// on (so setupDemoData() seeds a deterministic panel), then a full
// processEvents/repaint cycle so setContentSize() has finalised layout,
// then a render() into a transparent ARGB32 buffer.
Render renderPanel(Panel &panel, int fixedW = 0, int fixedH = 0)
{
    Render r;
    panel.setScale(1.0, false);
    panel.setOpacity(1.0, false);
    panel.setEditMode(true);
    if (fixedW > 0 && fixedH > 0)
        panel.setFixedSize(fixedW, fixedH);
    panel.show();
    QApplication::processEvents();
    panel.repaint();
    QApplication::processEvents();

    const QSize natural = (fixedW > 0 && fixedH > 0) ? QSize(fixedW, fixedH)
                                                     : panel.size();
    // Format_ARGB32 (NOT Premultiplied): the panel paints through
    // render() onto this buffer, and premultiplied alpha does not
    // round-trip losslessly through QImage::save()/load(), which made a
    // freshly-written baseline fail its own comparison. Straight ARGB32
    // keeps the byte-for-byte equality the suite needs.
    r.img = QImage(natural, QImage::Format_ARGB32);
    r.img.fill(Qt::transparent);
    QPainter p(&r.img);
    panel.render(&p, QPoint(), panel.rect());
    p.end();
    r.ok = true;
    return r;
}

int compareImage(const QString &path, const QImage &img, bool writeBaseline)
{
    if (writeBaseline) {
        QDir().mkpath(QString::fromLatin1(kBaselineDir));
        if (!img.save(path)) {
            std::fprintf(stderr, "FAIL %s: could not write baseline\n",
                         qPrintable(path));
            return 2;
        }
        return 0;
    }

    QImage baseline;
    if (!baseline.load(path)) {
        std::fprintf(stderr, "FAIL %s: baseline missing (run with --write-baseline)\n",
                     qPrintable(path));
        return 1;
    }
    if (baseline.size() != img.size()) {
        std::fprintf(stderr, "FAIL %s: size %dx%d != baseline %dx%d\n",
                     qPrintable(path), img.width(), img.height(),
                     baseline.width(), baseline.height());
        return 1;
    }

    int differing = 0;
    int firstX = -1, firstY = -1;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *a = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        const QRgb *b = reinterpret_cast<const QRgb *>(baseline.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (a[x] != b[x]) {
                ++differing;
                if (firstX < 0) {
                    firstX = x;
                    firstY = y;
                }
            }
        }
    }
    if (differing == 0) {
        std::printf("  ok   %s (%dx%d)\n", qPrintable(path), img.width(),
                    img.height());
        return 0;
    }
    const QRgb got = img.pixel(firstX, firstY);
    const QRgb want = baseline.pixel(firstX, firstY);
    std::fprintf(stderr,
                 "FAIL %s: %d differing pixel(s); first at (%d,%d) got ARGB=%u,%u,%u,%u want %u,%u,%u,%u\n",
                 qPrintable(path), differing, firstX, firstY,
                 qAlpha(got), qRed(got), qGreen(got), qBlue(got),
                 qAlpha(want), qRed(want), qGreen(want), qBlue(want));
    return 1;
}

QString baselinePath(const QString &name)
{
    return QString::fromLatin1(kBaselineDir) + QLatin1Char('/') + name
           + QStringLiteral(".png");
}

// ---------------------------------------------------------------------------
// Deterministic snapshot builders for the update(const GameSnapshot&) cases.
//
// The demo path (setGameForDemo -> setupDemoData) writes the panel's
// private fields directly; update() is a different entry point that takes
// the SAME structures the readers publish, so these builders produce
// structs shaped exactly like a reader's rather than the demo's. They are
// plain aggregate initialisers/assignments: no clock, no randomness, no
// pointer values, no hash-ordered containers. Every literal is a constant,
// so two runs in the same process and two runs across processes agree.
//
// Offsets mirror the World reader's kDebuffs table (player_reader.cpp), so
// the accent resolved by playerDebuffAccent(offset) inside update()'s
// bookkeeping loop is the one a live poll would produce.
// ---------------------------------------------------------------------------
mhw::PlayerAbnormality makeDebuff(int offset, const QString &name, float timer,
                                  float maxTimer)
{
    mhw::PlayerAbnormality d;
    d.offset   = offset;
    d.name     = name;
    d.timer    = timer;
    d.maxTimer = maxTimer;
    d.accent   = mhw::playerDebuffAccent(offset);
    return d;
}

mhw::WirebugSnapshot makeWirebug(int slot, bool isTemporary, float cooldown,
                                 float maxCooldown)
{
    mhw::WirebugSnapshot w;
    w.slot         = slot;
    w.isAvailable  = true;
    w.isTemporary  = isTemporary;
    w.cooldown     = cooldown;
    w.maxCooldown  = maxCooldown;
    return w;
}

// Melee sharpness for a weapon that reaches every level: thresholds are the
// cumulative per-level upper bounds, exactly the shape both readers publish.
mhw::SharpnessSnapshot makeSharpness(int level, int currentHits, int threshold,
                                    const int (&thresholds)[7])
{
    mhw::SharpnessSnapshot s;
    s.valid      = true;
    s.level      = level;
    s.currentHits = currentHits;
    s.maxHits    = thresholds[6];
    s.threshold  = threshold;
    for (int i = 0; i < 7; ++i)
        s.thresholds[i] = thresholds[i];
    return s;
}

// A Rise abnormality row. The accent is resolved from the schema table by
// id, exactly as the Rise reader does, so the pill colour cannot drift from
// what a live session shows.
mhw::PlayerAbnormalitySnapshot makeRiseAbnormality(
    const QString &id, const QString &name, float timer, float maxTimer,
    mhw::AbnormalityKind kind, bool infinite, bool buildup)
{
    mhw::PlayerAbnormalitySnapshot a;
    a.id         = id;
    a.name       = name;
    a.timer      = timer;
    a.maxTimer   = maxTimer;
    a.kind       = kind;
    a.isInfinite = infinite;
    a.isBuildup  = buildup;
    if (const mhw::RiseAbnormalitySchema *schema = mhw::riseFindAbnormality(
            id.toUtf8().constData()))
        a.accent = schema->accent;
    return a;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Overlay panel pixel-regression suite"));
    QCommandLineOption writeOpt(
        QStringList() << QStringLiteral("w") << QStringLiteral("write-baseline"),
        QStringLiteral("(re)generate the baseline PNGs and exit"));
    parser.addOption(writeOpt);
    parser.addHelpOption();
    parser.process(app);

    const bool writeBaseline = parser.isSet(writeOpt);

    // zh-CN is the frozen historical look: the same default every snap_*
    // tool uses, so the baselines describe the panel layout users have.
    if (!mhw::StringTable::instance().load(QStringLiteral("zh-CN"))) {
        std::fprintf(stderr, "FAIL: zh-CN strings did not load\n");
        return 2;
    }

    int failures = 0;

    {
        std::printf("player panel:\n");
        PlayerPanel world;
        world.setGameForDemo(mhw::GameId::World);
        if (Render r = renderPanel(world); r.ok)
            failures += compareImage(baselinePath("player_world"), r.img, writeBaseline);

        PlayerPanel rise;
        rise.setGameForDemo(mhw::GameId::Rise);
        if (Render r = renderPanel(rise); r.ok)
            failures += compareImage(baselinePath("player_rise"), r.img, writeBaseline);
    }

    // PlayerPanel::update(const GameSnapshot&) — the path every live poll
    // takes, and the one the two demo baselines above cannot see: their
    // setGameForDemo() seeds the panel through setupDemoData(), so nothing
    // there ever runs update(). Those two baselines pin the demo preview
    // only; the 10 field mirrors, the party local=true override and the
    // cross-frame debuff/buff timer bookkeeping inside update() were
    // completely unasserted. These two cases pin exactly that path.
    //
    // The snapshots are hand-written and fully deterministic — no clock,
    // no randomness, no pointers. Every field is either a hard-coded
    // literal or zero-initialised.
    //
    // Ordering is load-bearing: paintEvent() runs setupDemoData() on the
    // first paint after entering edit mode, and that overwrites every field
    // update() mirrors. Priming the demo seed with a throwaway render first
    // is what makes this a genuine update()-driven image; without it the
    // frame below is just player_world again.
    {
        std::printf("player panel update(GameSnapshot):\n");
        PlayerPanel world;
        world.setGameForDemo(mhw::GameId::World);
        (void)renderPanel(world);   // prime setupDemoData() one shot
        mhw::GameSnapshot snap;
        snap.game      = mhw::GameId::World;
        snap.attached  = true;
        snap.pid       = 24601;
        snap.status    = QStringLiteral("已连接 · 猎人状态");
        snap.zone      = mhw::Zone::GreatRavine;
        // Player struct: 7000/10000 = 70%.
        snap.player.valid      = true;
        snap.player.name       = QStringLiteral("Hunter");
        snap.player.health     = 7000.0F;
        snap.player.maxHealth  = 10000.0F;
        snap.player.stamina    = 100.0F;
        snap.player.maxStamina = 150.0F;
        snap.player.masterRank = 999;        // superseded by the party member below
        snap.player.weaponId  = 3;          // Longsword
        // Sharpness: a melee weapon at Green, so the prow's gauge + badge
        // render — that pinpoints the sharpness_ field mirror too.
        snap.player.sharpness = makeSharpness(4, 74, 60, {5, 15, 30, 60, 90, 120, 150});
        // Two debuffs, each renamed/edited through update()'s
        // debuffMaxTimers_ bookkeeping (offset-keyed max timers).
        snap.player.debuffs = {
            makeDebuff(0x5EC, QStringLiteral("火异常"), 33.0F, 60.0F),
            makeDebuff(0x620, QStringLiteral("爆破"), 41.0F, 60.0F),
        };
        // The local member's masterRank (888) overwrites the player
        // struct's 999 — a rule only update() applies.
        mhw::PartyMemberSnapshot self;
        self.name       = QStringLiteral("LocalHunter");
        self.masterRank = 888;
        self.weaponId   = 3;
        self.local      = true;
        snap.party      = {self};
        world.update(snap);
        if (Render r = renderPanel(world); r.ok)
            failures += compareImage(baselinePath("player_update_world"), r.img, writeBaseline);
    }

    // Same path under Rise: the game_ branch swaps the mantle row for the
    // wirebug row, so the update() figure has to be pinned for both games.
    // Rise's abnormality block renders from player_.abnormalities, so the
    // snapshot carries those entries instead of the World debuff/buff pills.
    {
        std::printf("player panel update(GameSnapshot):\n");
        PlayerPanel rise;
        rise.setGameForDemo(mhw::GameId::Rise);
        (void)renderPanel(rise);   // prime setupDemoData() one shot
        mhw::GameSnapshot snap;
        snap.game      = mhw::GameId::Rise;
        snap.attached  = true;
        snap.pid       = 31337;
        snap.status    = QStringLiteral("已连接 · 猎人状态");
        snap.zone      = mhw::Zone::RiseLoc3;
        snap.player.valid      = true;
        snap.player.name       = QStringLiteral("Hunter");
        snap.player.health     = 7000.0F;
        snap.player.maxHealth  = 10000.0F;
        // Rise reader semantics: raw stamina in thirtieths of the bar value.
        snap.player.stamina    = 3600.0F;   // -> 120 shown
        snap.player.maxStamina = 4500.0F;   // -> 150 shown
        snap.player.masterRank = 999;       // superseded by the party member below
        snap.player.weaponId  = 3;
        snap.player.sharpness = makeSharpness(3, 59, 30, {5, 15, 30, 60, 90, 120, 150});
        snap.player.wirebugs   = {
            makeWirebug(0, false, 12.0F, 30.0F),
            makeWirebug(1, true,   8.0F, 30.0F),
            makeWirebug(2, true,  20.0F, 30.0F),
        };
        // Rise's 状态 block: debuff rows first, then buffs.
        snap.player.abnormalities = {
            makeRiseAbnormality(QStringLiteral("ABN_POISON"), QStringLiteral("中毒"),
                                23.0F, 0.0F, mhw::AbnormalityKind::Debuff, false, false),
            makeRiseAbnormality(QStringLiteral("ABN_BLAST"), QStringLiteral("爆炸异常"),
                                41.0F, 0.0F, mhw::AbnormalityKind::Debuff, false, false),
            makeRiseAbnormality(QStringLiteral("ABN_FRENZY_BUILDUP"),
                                QStringLiteral("狂龙症（增长中）"),
                                43.0F, 120.0F, mhw::AbnormalityKind::Debuff, true, true),
            makeRiseAbnormality(QStringLiteral("ABN_DEMONDRUG"), QStringLiteral("鬼人药"),
                                1.0F, 0.0F, mhw::AbnormalityKind::Buff, true, false),
            makeRiseAbnormality(QStringLiteral("ABN_MIGHT_SEED"), QStringLiteral("怪力种子"),
                                142.0F, 0.0F, mhw::AbnormalityKind::Buff, false, false),
        };
        mhw::PartyMemberSnapshot self;
        self.name       = QStringLiteral("LocalHunter");
        self.masterRank = 888;
        self.weaponId   = 3;
        self.local      = true;
        snap.party      = {self};
        rise.update(snap);
        if (Render r = renderPanel(rise); r.ok)
            failures += compareImage(baselinePath("player_update_rise"), r.img, writeBaseline);
    }

    {
        std::printf("all-demo panels:\n");
        MonsterPanel monster;
        if (Render r = renderPanel(monster); r.ok)
            failures += compareImage(baselinePath("all_monster"), r.img, writeBaseline);

        DamagePanel damage;
        if (Render r = renderPanel(damage); r.ok)
            failures += compareImage(baselinePath("all_damage"), r.img, writeBaseline);

        PetDamagePanel pets;
        if (Render r = renderPanel(pets); r.ok)
            failures += compareImage(baselinePath("all_pets"), r.img, writeBaseline);
    }

    std::printf("%s (%d failing image(s))\n",
                failures == 0 ? "PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
