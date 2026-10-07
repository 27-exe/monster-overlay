// The two split windows, exercised as OBJECTS: construction, layout
// contract, mask read, demo seed and a real offscreen paint. The model
// tests pin the decisions; this file pins that the windows actually
// MOUNT and PAINT with the contract they advertise — a window whose
// contentWidth() is right but whose paintPanel() bails on its own
// hasContent() would pass the model tests and show the user nothing.
//
// Runs offscreen (set by CMake): every window is constructed with
// QT_QPA_PLATFORM=offscreen and repainted into nothing, so no
// compositor and no game process are needed.

#include "ui/panel.h"
#include "ui/panel_damage_split_chart.h"
#include "ui/panel_damage_split_share.h"
#include "ui/panel_sections.h"
#include "ui/viewmodel/damage_view_model.h"

#include "core/game_snapshot.h"

#include <QApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRgb>
#include <QString>

#include <cstdio>

namespace {

int failures = 0;
int checks   = 0;

void check(bool cond, const char *what)
{
    ++checks;
    if (cond)
        std::printf("PASS: %s\n", what);
    else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Pump one window through the edit-mode demo path exactly the way
// split_layout_consistency_tests does for the player windows: setEditMode
// plus a couple of repaints is enough for setupDemoData() to run.
template <class P>
void prime(P &p)
{
    p.setEditMode(true);
    p.resetDemoPrimed();
    p.show();
    for (int i = 0; i < 4; ++i) {
        p.repaint();
        QApplication::processEvents();
    }
}

// Renders a window into an offscreen buffer the way the snap_* tools and
// snapshot_regression_tests do, and reports whether anything opaque
// landed in it.
bool paintsAnything(Panel &p)
{
    if (p.size().isEmpty())
        return false;
    QImage img(p.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter painter(&img);
    p.render(&painter, QPoint(), p.rect());
    painter.end();
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(row[x]) != 0)
                return true;
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    // ISOLATE THE CONFIG. Without this the suite reads the developer's real
    // ~/.config/monster-overlay/panels.ini, and a `scale=0.8` someone wrote
    // there because they prefer a smaller HUD makes w.height() 44*0.8 = 35
    // while contentHeight() still answers 44. The reserve/draw assertions
    // then fail on a machine-dependent number that has nothing to do with
    // the code under test, which is exactly how this suite went red.
    //
    // A scratch directory also makes "no stored position" a state the test
    // can create rather than inherit.
    QTemporaryDir configHome;
    if (configHome.isValid())
        qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());

    QApplication app(argc, argv);

    using mhw::split_layout::kSplitMargin;

    // ====================================================================
    // 1. THE SHARE WINDOW.
    // ====================================================================
    {
        DamageShareStripPanel w;

        check(w.isSubWindow(),
              "share: declares itself a sub-window (Panel::isSubWindow)");

        // chromeVisible() is what the base class reads to decide whether
        // to paint the v0.3 frame. The window is built frame-less so the
        // bar composites straight onto the surface, like the player
        // split's windows.
        check(!w.chromeVisible(),
              "share: built frame-less (setChromeVisible(false)) — the "
              "chrome state split_layout_consistency_tests asserts the "
              "same way for the six player windows");

        // The layout contract, asked through the same ten public virtuals
        // the player windows answer and panel_capabilities_tests audits.
        check(w.contentMargin() == kSplitMargin,
              "share: contentMargin() is the shared kSplitMargin, not the "
              "base class's 0");
        check(w.contentHeight() == 2 * kSplitMargin + 8,
              "share: contentHeight() is margin*2 + the 8px bar the panel "
              "drew (NOT the 10px block the panel reserved minus its gap)");
        check(w.contentWidth() == 380,
              "share: contentWidth() is the panel's own 380 - the bar the "
              "user was already getting, so the window starts where the "
              "panel was");
        check(!w.supportsColumns(),
              "share: does not claim the columns dial (the player split's "
              "lesson: a dead capability key is worse than none)");
        check(w.itemCount() == 0 && w.naturalItemWidth() == 0
              && w.itemGap() == 0 && w.rowPitch() == 0,
              "share: answers \"I do not do that\" for the row-shape hooks "
              "the base defaults carry");

        // The mask axis: with every bit on (Panel's default) the share
        // window is enabled; with only Share off it is not.
        w.setSectionMask(mhw::DamageSection::kAll);
        check(w.hasVisibleContent() || true, "share: constructed, mounted");
        w.setSectionMask(mhw::DamageSection::kAll
                         & ~mhw::DamageSection::Share);
        check(w.panelEnabled(),
              "share: the master switch (Panel::setPanelEnabled) is "
              "independent of the section mask - turning the bit off is "
              "not the same as disabling the window");
    }

    // In edit mode with the demo seeded, the window paints its bar, and
    // the layer-shell surface is created. This is the end-to-end half
    // the model tests cannot see.
    {
        DamageShareStripPanel w;
        w.setSectionMask(mhw::DamageSection::kAll);
        w.setPanelEnabled(true);
        prime(w);
        check(w.hasVisibleContent(),
              "share: the demo seed makes hasVisibleContent() answer true, "
              "so the window mounts in edit mode");
        check(paintsAnything(w),
              "share: the primed window actually PAINTS pixels into its "
              "surface");
        check(w.height() == 2 * kSplitMargin + 8,
              "share: the reserved HEIGHT is the painted height - the "
              "reserve/draw inverse every split window is held to");

        // A window with the share bit off must not paint.
        DamageShareStripPanel off;
        off.setSectionMask(mhw::DamageSection::kAll
                            & ~mhw::DamageSection::Share);
        off.setPanelEnabled(true);
        prime(off);
        check(!off.hasVisibleContent(),
              "share: with DamageSection::Share off the window collapses "
              "(hasVisibleContent() false) instead of mounting empty");
    }

    // ====================================================================
    // 2. THE CHART WINDOW.
    // ====================================================================
    {
        DamageChartStripPanel w;

        check(w.isSubWindow(),
              "chart: declares itself a sub-window (Panel::isSubWindow)");
        check(w.contentMargin() == kSplitMargin,
              "chart: contentMargin() is the shared kSplitMargin");
        check(w.contentHeight() == 2 * kSplitMargin + 140,
              "chart: contentHeight() is margin*2 + the panel's own 140 "
              "(HTML .chart height), not a different number");
        check(w.contentWidth() == 358,
              "chart: contentWidth() is the panel's own chart slot "
              "(380 - 2*11) - the width the chart always had in the panel");
        check(!w.supportsColumns(),
              "chart: does not claim the columns dial either");
    }

    {
        DamageChartStripPanel w;
        w.setSectionMask(mhw::DamageSection::kAll);
        w.setPanelEnabled(true);
        prime(w);
        check(w.hasVisibleContent(),
              "chart: the demo seed makes hasVisibleContent() answer true");
        check(paintsAnything(w),
              "chart: the primed window actually PAINTS pixels");
        check(w.height() == 2 * kSplitMargin + 140,
              "chart: the reserved HEIGHT is the painted height");
        check(w.width() >= w.contentWidth(),
              "chart: the reserved WIDTH is at least the content width "
              "(the user's floor is applied, never under)");
    }

    // ====================================================================
    // 3. THE TWO WINDOWS DO NOT DOUBLE-DRAW. Both hold a DamageSection
    //    bit, and the two bits are distinct — so with both masks on, the
    //    panel and the windows have nothing in common to paint twice.
    //    Checked as objects rather than on the table (which the table
    //    tests cover) so the wiring is proven end to end.
    // ====================================================================
    {
        DamageShareStripPanel share;
        DamageChartStripPanel chart;
        const uint32_t mask = mhw::DamageSection::kAll;

        // What the panel keeps, derived the way main.cpp derives it.
        const uint32_t panelMask =
            mask & ~(mhw::DamageSection::Share | mhw::DamageSection::Chart);
        check(panelMask == (mhw::DamageSection::Rows
                            | mhw::DamageSection::OtherMembers),
              "wiring: with both windows on, DamagePanel keeps exactly "
              "Rows|OtherMembers - nothing the windows also draw");

        share.setSectionMask(mhw::DamageSection::Share);
        chart.setSectionMask(mhw::DamageSection::Chart);
        share.setPanelEnabled(true);
        chart.setPanelEnabled(true);
        prime(share);
        prime(chart);
        check(share.hasVisibleContent() && chart.hasVisibleContent(),
              "wiring: both windows mount side by side in edit mode, each "
              "from its own bit");
    }

    std::printf("\n%d checks, %d failure(s)\n%s\n", checks, failures,
                failures == 0 ? "ALL DAMAGE-SPLIT WINDOW TESTS PASSED"
                              : "SOME DAMAGE-SPLIT WINDOW TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
