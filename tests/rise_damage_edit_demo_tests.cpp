// SPDX-License-Identifier: Apache-2.0
//
// Behavioural probe for the user's report, verbatim:
//   「崛起下,启动悬浮窗编辑的话,伤害统计那个窗会自动隐藏」
//
// WHAT THE BUG WAS. main.cpp's Rise arm guarded the three damage strips'
// edit-mode demo with `skipUpdate(damagePanel)` — the MOTHER panel's demo
// flag. The mother does not always paint: in Mixed with every damage row
// detached, damagePanelHasBlocks() answers false, the mother never mounts,
// and its demo is never primed, so that flag stayed false for the whole
// session. Every tick then fed the strips the empty `feedLost` snapshot,
// which wiped the demo their own first paint had just seeded, and the mount
// half hid them on hasVisibleContent().
//
// WHAT THIS PROVES, with pixels rather than reasoning:
//   OLD — the mother's flag is false while the mother is unmounted, so the
//         empty feed reaches the strip and the window goes away.
//   NEW — the strip's OWN flag is true after its own paint, the empty feed
//         never reaches it, and the rendered pixels are byte-identical to
//         the demo it seeded.
//
// The wiring half (that main.cpp really uses the per-strip guards) is pinned
// by tests/rise_damage_wiring_tests.cpp's source-level contract. This file
// pins the BEHAVIOUR those guards protect, which is the half a source scan
// cannot see.

#include "core/string_table.h"
#include "rise/rise_damage_types.h"
#include "ui/panel_damage.h"
#include "ui/panel_damage_split_chart.h"
#include "ui/panel_damage_split_rows.h"
#include "ui/panel_damage_split_share.h"

#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what, const QString &detail = QString())
{
    std::printf("%s %s%s%s\n", ok ? "PASS:" : "FAIL:", what,
                detail.isEmpty() ? "" : " -- ",
                detail.isEmpty() ? "" : qPrintable(detail));
    if (!ok)
        ++failures;
}

void pump()
{
    for (int i = 0; i < 40; ++i)
        QCoreApplication::processEvents();
}

// Pixels that are not the panel's own page colour. A window that kept its
// demo paints rows; a window that an empty feed wiped paints only chrome or
// nothing at all, so the ink count is the difference in one number.
int ink(const QImage &im)
{
    if (im.isNull())
        return 0;
    const QImage rgba = im.convertToFormat(QImage::Format_ARGB32);
    // Sample the histogram instead of guessing a background: the most
    // common pixel IS the background of whatever this panel draws.
    QHash<QRgb, int> hist;
    for (int y = 0; y < rgba.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgba.constScanLine(y));
        for (int x = 0; x < rgba.width(); ++x)
            ++hist[line[x]];
    }
    int bg = 0;
    for (int n : hist)
        if (n > bg)
            bg = n;
    int count = 0;
    for (int y = 0; y < rgba.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgba.constScanLine(y));
        for (int x = 0; x < rgba.width(); ++x)
            if (line[x] != hist.key(bg))
                ++count;
    }
    return count;
}

// The demo sequence main.cpp's first-show performs, spelled once so the two
// arms below differ ONLY in the guard.
template <typename Strip>
void firstShow(Strip &strip)
{
    strip.setEditMode(true);
    strip.setPanelEnabled(true);
    strip.setVisible(true);
    strip.show();
    pump();
}

} // namespace

int main(int argc, char *argv[])
{
    // HOME/XDG into a scratch dir: these panels read panels.ini through
    // Panel::loadConfig(), and a probe must never touch the user's own
    // configuration.
    QTemporaryDir home;
    if (!home.isValid()) {
        std::fprintf(stderr, "FAIL: no scratch HOME\n");
        return 2;
    }
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationName(QStringLiteral("monster-overlay"));
    if (!mhw::StringTable::instance().load(QStringLiteral("zh-CN")))
        std::printf("note: strings fall back to keys\n");

    std::printf("== rise_damage_edit_demo_tests ==\n");

    // The mother panel, deliberately NEVER shown: in Mixed with all three
    // damage rows detached this is exactly its state — damagePanelHasBlocks()
    // answers false, setPanelEnabled(false), no surface, no paint, no demo.
    DamagePanel mother;
    mother.setEditMode(true);
    mother.setPanelEnabled(false);

    const mhw::RiseDamageSnapshot feedLost;   // the empty "feed is gone" one

    // ── OLD: the guard is the MOTHER's flag ──────────────────────────────
    {
        DamageRowsStripPanel rows;
        firstShow(rows);
        QImage seeded = rows.grab().toImage();
        check(rows.demoPrimed(),
              "OLD premise: the strip's own paint primed its demo");
        check(rows.hasVisibleContent(),
              "OLD premise: …so it has content right after the first paint",
              QStringLiteral("ink=%1").arg(ink(seeded)));

        // main.cpp used to compute this once and use it for all three.
        const bool keepDamageDemo = /* skipUpdate(mother) = */
            true && mother.demoPrimed();
        check(!keepDamageDemo,
              "OLD premise: the mother never painted, so ITS flag is false",
              QStringLiteral("mother.demoPrimed=%1")
                  .arg(mother.demoPrimed()));

        if (!keepDamageDemo)
            rows.updateRiseDamage(feedLost);
        rows.setVisible(rows.panelEnabled() && rows.hasVisibleContent());

        check(!rows.hasVisibleContent(),
              "OLD: the empty feed wiped the strip's demo one tick later");
        check(!rows.isVisible(),
              "OLD: …and the window hid itself — the user's report");
    }

    // ── NEW: the guard is the STRIP's own flag ───────────────────────────
    {
        DamageRowsStripPanel rows;
        firstShow(rows);
        QImage seeded = rows.grab().toImage();
        check(rows.demoPrimed(),
              "NEW premise: the strip's own paint primed its demo");

        const bool keepRowsDemo = true && rows.demoPrimed();   // skipUpdate(rows)
        check(keepRowsDemo,
              "NEW: the guard is read off THIS window, so it is true after "
              "its own paint even though the mother is unmounted");

        if (!keepRowsDemo)
            rows.updateRiseDamage(feedLost);
        rows.setVisible(rows.panelEnabled() && rows.hasVisibleContent());

        check(rows.hasVisibleContent(),
              "NEW: the demo survived the live drive");
        check(rows.isVisible(),
              "NEW: …and the window stays mounted");
        QImage after = rows.grab().toImage();
        check(!after.isNull() && after == seeded,
              "NEW: the rendered pixels are byte-identical to the demo it "
              "seeded — nothing was overwritten",
              QStringLiteral("ink=%1 -> %2").arg(ink(seeded)).arg(ink(after)));
        check(ink(after) > 200,
              "NEW: …and it is real content, not an empty frame",
              QStringLiteral("ink=%1").arg(ink(after)));

        // Optional visual receipt: `rise-damage-edit-demo-tests <dir>` writes
        // the two frames beside the numbers above, so the report can show
        // what "wiped" and "intact" actually look like.
        if (argc > 1) {
            const QString dir = QString::fromLocal8Bit(argv[1]);
            const bool okA = seeded.save(dir + QLatin1String("/demo-seeded.png"));
            const bool okB = after.save(dir + QLatin1String("/demo-after.png"));
            std::printf("  wrote %s/demo-seeded.png=%s demo-after.png=%s\n",
                        qPrintable(dir), okA ? "yes" : "NO", okB ? "yes" : "NO");
        }
    }

    // ── the same two arms for the other two strips, in one line each ────
    // (the rows window is the one the user named; share and chart must
    // answer the same way or the fix is only half a fix)
    {
        DamageShareStripPanel share;
        DamageChartStripPanel chart;
        firstShow(share);
        firstShow(chart);
        check(share.demoPrimed() && chart.demoPrimed(),
              "share/chart premise: their own paints primed their demos");
        const bool keepShareDemo = true && share.demoPrimed();
        const bool keepChartDemo = true && chart.demoPrimed();
        if (!keepShareDemo)
            share.updateRiseDamage(feedLost);
        if (!keepChartDemo)
            chart.updateRiseDamage(feedLost);
        share.setVisible(share.panelEnabled() && share.hasVisibleContent());
        chart.setVisible(chart.panelEnabled() && chart.hasVisibleContent());
        check(share.isVisible() && chart.isVisible(),
              "NEW: share and chart stay mounted too",
              QStringLiteral("share=%1 chart=%2")
                  .arg(share.isVisible()).arg(chart.isVisible()));
    }

    std::printf("\nRESULT: %s (%d failures)\n",
                failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
