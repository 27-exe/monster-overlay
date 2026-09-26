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
