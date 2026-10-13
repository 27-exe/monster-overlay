// SPDX-License-Identifier: Apache-2.0
//
// v1.0.0 fidelity (root scroll): THE MECHANISM'S OWN TEST.
//
// The 27-file frozen candidate this round starts from has the root
// scroll — src/ui/console_root_scroll.{h,cpp} — but no test of it: no
// test target compiles that TU, and console_root_scroll.cpp was the one
// src TU the zero-reference census did not know (the gate's
// `zero-reference-gate` case failed on exactly that). This file closes
// it by compiling the TU DIRECTLY and asserting the four properties the
// class exists for, each one a measured behaviour rather than a copy of
// a number from the prototype:
//
//   1  NATURAL HEIGHT  the content's minimum height is its own layout's
//      natural height, re-read when the layout asks (a taller page
//      scrolls instead of being squeezed into the window — the whole
//      point of the class);
//   2  WIDTH FOLLOWS THE VIEWPORT  the content is never wider than the
//      scroll area's viewport, including the width the vertical
//      scrollbar itself takes away, which is what the page's
//      `documentElement.clientWidth` = innerWidth - scrollbar does;
//   3  NO SHRINK  a page whose natural height is BELOW the viewport is
//      not padded up to the viewport's minimum — the content keeps its
//      own hint and simply does not scroll;
//   4  RECOVERY  grow the window (or shrink the page) and the scroll
//      goes away again: the content returns to the viewport height, and
//      it comes back when the page grows again. A one-way mechanism
//      would leave the console permanently scrolled after a maximize.
//
// What this file deliberately does NOT do: build a ControlPanel, touch
// panels.ini, or read a screen. The class's contract is about a
// QScrollArea and a layout, so a fixed-height QWidget in a QVBoxLayout
// is the whole rig — nothing else can move the numbers.
//
// CONFIG: XDG_CONFIG_HOME is redirected into a QTemporaryDir before
// QApplication exists, same discipline as the other console tests, so
// even an accidental QSettings write lands in the temp dir.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLayout>
#include <QScrollBar>
#include <QString>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "ui/console_root_scroll.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const QString &what, const QString &detail = QString())
{
    ++checks;
    std::printf("%s: %s", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!detail.isEmpty())
        std::printf(" -- %s", qPrintable(detail));
    std::printf("\n");
    if (!ok)
        ++failures;
}

void pump(int ms = 150)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// The rig: a scroll area, its content widget, and the one child that
// decides the content's natural height. `filler` is a plain QWidget of a
// FIXED height, so the content's QVBoxLayout reports exactly that as its
// sizeHint and no font, no DPI and no theme can move it.
struct Rig {
    mhw::RootScrollArea *scroll = nullptr;
    QWidget *content = nullptr;
    QWidget *filler = nullptr;
};

Rig makeRig(int naturalHeight, int width, int height)
{
    Rig r;
    r.scroll = new mhw::RootScrollArea();
    r.content = new QWidget();
    auto *lay = new QVBoxLayout(r.content);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    r.filler = new QWidget(r.content);
    r.filler->setFixedHeight(naturalHeight);
    lay->addWidget(r.filler);
    r.scroll->setRootWidget(r.content);
    r.scroll->resize(width, height);
    r.scroll->show();
    pump();
    return r;
}

void setNatural(Rig &r, int naturalHeight)
{
    r.filler->setFixedHeight(naturalHeight);
    pump();
}

} // namespace

int main(int argc, char **argv)
{
    // A temp config home BEFORE QApplication: nothing here writes, and
    // this is the guard that keeps it that way.
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", "");
    qputenv("XDG_CACHE_HOME", (cfg.path() + QStringLiteral("/cache")).toUtf8());

    QApplication app(argc, argv);

    std::printf("console-root-scroll-tests: the mechanism, compiled "
                "directly from src/ui/console_root_scroll.cpp\n");

    // ── 1. natural height, and the scrollbar it earns ───────────────────
    {
        Rig r = makeRig(900, 1600, 600);
        check(r.scroll->naturalHeight() == 900,
              "1: naturalHeight() is the content's own layout hint",
              QStringLiteral("%1").arg(r.scroll->naturalHeight()));
        check(r.content->minimumHeight() == 900,
              "1: the content's MINIMUM height is that natural height — "
              "the only way a widgetResizable scroll area may exceed the "
              "viewport",
              QStringLiteral("%1").arg(r.content->minimumHeight()));
        check(r.content->height() == 900,
              "1: the content GREW to 900 instead of being squeezed into "
              "the 600px viewport (the page scrolls, the board does not "
              "shrink)",
              QStringLiteral("%1").arg(r.content->height()));
        QScrollBar *vbar = r.scroll->verticalScrollBar();
        check(vbar && vbar->isVisible() && vbar->maximum() > 0,
              "1: the vertical scrollbar is real and has range",
              vbar ? QStringLiteral("max=%1").arg(vbar->maximum())
                   : QStringLiteral("no vbar"));
        check(r.content->width() == r.scroll->viewport()->width(),
              "2: the content width is the VIEWPORT width, not the "
              "window width (the page loses the scrollbar's 15px the "
              "same way)",
              QStringLiteral("content=%1 viewport=%2")
                  .arg(r.content->width())
                  .arg(r.scroll->viewport()->width()));
        delete r.scroll;
    }

    // ── 2. width keeps following the viewport at a second width ─────────
    {
        Rig r = makeRig(900, 1600, 600);
        r.scroll->resize(900, 600);
        pump();
        check(r.content->width() == r.scroll->viewport()->width(),
              "2: after a resize to 900 the content width still tracks "
              "the viewport",
              QStringLiteral("content=%1 viewport=%2")
                  .arg(r.content->width())
                  .arg(r.scroll->viewport()->width()));
        delete r.scroll;
    }

    // ── 3. a page SHORTER than the viewport is not padded up ────────────
    {
        Rig r = makeRig(300, 1600, 600);
        check(r.scroll->naturalHeight() == 300,
              "3: naturalHeight() follows the hint down as well",
              QStringLiteral("%1").arg(r.scroll->naturalHeight()));
        check(r.content->minimumHeight() == 300,
              "3: the minimum is the hint, NOT the viewport — nothing "
              "fakes a tall page",
              QStringLiteral("%1").arg(r.content->minimumHeight()));
        check(r.content->height() == 600,
              "3: the content fills the viewport exactly (no gap, no "
              "scroll)",
              QStringLiteral("%1").arg(r.content->height()));
        QScrollBar *vbar = r.scroll->verticalScrollBar();
        check(vbar && !vbar->isVisible(),
              "3: no scrollbar appears while the page fits");
        delete r.scroll;
    }

    // ── 4. recovery: the page shrinks back into the viewport ────────────
    {
        Rig r = makeRig(900, 1600, 600);
        const bool scrolledAt900 =
            r.scroll->verticalScrollBar()->isVisible();
        setNatural(r, 300);
        check(r.content->minimumHeight() == 300,
              "4: a LayoutRequest re-reads the budget downward",
              QStringLiteral("%1").arg(r.content->minimumHeight()));
        check(r.content->height() == 600,
              "4: the content shrinks back to the viewport height once "
              "the page fits again",
              QStringLiteral("%1").arg(r.content->height()));
        check(!r.scroll->verticalScrollBar()->isVisible(),
              "4: the scrollbar goes away with it — the mechanism is "
              "not one-way");
        setNatural(r, 900);
        check(r.scroll->verticalScrollBar()->isVisible()
              && r.content->height() == 900,
              "4: and the page scrolls again when it grows back",
              QStringLiteral("height=%1").arg(r.content->height()));
        check(scrolledAt900,
              "4: (the first state of this rig did scroll, so the two "
              "changes above are what moved it)");

        // The other half of recovery: the WINDOW grows past the page.
        r.scroll->resize(1600, 1200);
        pump();
        check(r.content->height() == 1200
              && !r.scroll->verticalScrollBar()->isVisible(),
              "4: growing the window past the page returns the content "
              "to the viewport height — a maximized window is not left "
              "scrolled",
              QStringLiteral("height=%1").arg(r.content->height()));
        delete r.scroll;
    }

    if (failures == 0)
        std::printf("console-root-scroll-tests: ALL PASSED (%d checks)\n",
                    checks);
    else
        std::fprintf(stderr,
                     "console-root-scroll-tests: %d/%d FAILED\n",
                     failures, checks);
    return failures == 0 ? 0 : 1;
}
