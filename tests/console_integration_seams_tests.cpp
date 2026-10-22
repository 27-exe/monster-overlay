// SPDX-License-Identifier: Apache-2.0
//
// v1.0.0 integration: THE FOUR SEAMS THAT ONLY EXIST IN A REAL CONSOLE.
//
// The other console test files pin one artefact each — the key's
// spelling, the inspector's structure, the canvas's geometry. This file
// exists because four of the integration rules are PROPERTIES OF THE
// WIRING between those artefacts, and a property of the wiring cannot be
// observed by testing either end alone:
//
//   A  the canvas is in console-fidelity mode, the per-window `winVisible`
//      row is pushed into the canvas on every refresh AND is honoured by
//      the process that mounts the surface (Panel::setVisible), and the
//      stage's window-count chip counts what the board draws;
//   A' the view/display state contract: a click on a board window —
//      Single or Split — must leave the overview OPEN and the viewer
//      (previewScreen) UNCHANGED, because reference.html's board handler
//      writes `state.selected` and only `#panel-nav [data-view]` writes
//      `state.view`;
//   and the two axes a hidden window used to conflate: `enabled`
//      (sections all off → the `.hidden-window` placeholder) and
//      `visible` (off the board entirely).
//
// REAL INPUT, NOT SIGNALS. The overview rule in particular was previously
// only ever exercised by QMetaObject::invokeMethod on the panelSelected
// SIGNAL, which proves the slot is connected and proves nothing about
// what a click does — the slot's behaviour depends on the view state the
// SIGNAL cannot carry. Every click below is a QMouseEvent delivered to
// the widget the user would click, and the assertion reads the state
// AFTER the event loop has run.
//
// CONFIG ISOLATION. XDG_CONFIG_HOME is redirected into a QTemporaryDir
// before QApplication exists, so panels.ini — which this file writes on
// purpose (the hide/show round trip) — is the temp dir's file and never
// the developer's.

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFrame>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QString>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

#include "core/string_table.h"
#include "ui/console_fidelity_windows.h"
#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/console_root_scroll.h"
#include "ui/panel_player.h"
#include "ui/panel_settings_game.h"
#include "ui/panel_sections.h"
#include "ui/screen_query.h"
#include "ui/window_layout_geometry.h"
#include <QScreen>

namespace {

int failures = 0;
int checks = 0;

// QString rather than const char*: several of the assertions below name
// the corner they are checking, which is a runtime string. Every literal
// call site converts implicitly, so nothing else changes.
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

void note(const QString &what)
{
    std::printf("NOTE: %s\n", qPrintable(what));
}

void pump(int ms);

// Direct contract for the root-scroll sizing seam.
// real height-for-width rule, so this catches both failure modes: a scroll
// area that does not publish HfW to its parent, and one that keeps the old
// tall minimum after the content becomes short enough to fit again.
class TestHeightForWidthContent final : public QWidget
{
public:
    explicit TestHeightForWidthContent(int extra, QWidget *parent = nullptr)
        : QWidget(parent), extra_(extra)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *filler = new QWidget(this);
        filler->setMinimumHeight(1);
        layout->addWidget(filler);
    }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override
    {
        return qMax(1, width / 2 + extra_);
    }
    QSize sizeHint() const override
    {
        return QSize(600, heightForWidth(600));
    }
    void setExtra(int extra)
    {
        extra_ = extra;
        updateGeometry();
        if (layout())
            layout()->invalidate();
    }

private:
    int extra_;
};

void testRootScrollSizing()
{
    QWidget host;
    host.resize(600, 300);
    auto *scroll = new mhw::RootScrollArea(&host);
    scroll->setGeometry(host.rect());
    auto *content = new TestHeightForWidthContent(900);
    scroll->setRootWidget(content);
    host.show();
    pump(80);

    const int expectedWide = content->heightForWidth(scroll->viewport()->width());
    check(scroll->naturalHeight() == expectedWide,
          QStringLiteral("RootScrollArea reports content heightForWidth at its live viewport width"),
          QStringLiteral("natural=%1 expected=%2 viewportW=%3")
              .arg(scroll->naturalHeight()).arg(expectedWide)
              .arg(scroll->viewport()->width()));
    check(scroll->widget()->minimumHeight() == expectedWide,
          QStringLiteral("root content minimum follows the natural HfW budget"));
    check(scroll->verticalScrollBar()->isVisible(),
          QStringLiteral("a natural height greater than the viewport creates a real vertical scrollbar"));
    check(!scroll->horizontalScrollBar()->isVisible(),
          QStringLiteral("root HfW scrolling does not introduce horizontal overflow"));
    check(scroll->widget()->width() == scroll->viewport()->width(),
          QStringLiteral("root content width follows the scrollbar-reduced viewport"));
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    pump(20);
    check(scroll->verticalScrollBar()->value() == scroll->verticalScrollBar()->maximum(),
          QStringLiteral("the root scrollbar can reach its natural content bottom"));

    content->setExtra(1);
    pump(50);
    const int expectedShort = content->heightForWidth(scroll->viewport()->width());
    check(scroll->naturalHeight() == expectedShort,
          QStringLiteral("a later layout request re-reads a reduced natural height"),
          QStringLiteral("natural=%1 expected=%2")
              .arg(scroll->naturalHeight()).arg(expectedShort));
    check(scroll->widget()->minimumHeight() == expectedShort,
          QStringLiteral("the old tall minimum is released when content shrinks"));
    host.resize(600, 600);
    pump(50);
    check(!scroll->verticalScrollBar()->isVisible(),
          QStringLiteral("a viewport taller than natural content no longer scrolls"));
}

void pump(int ms = 150)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// A REAL press/release pair on the widget, at the local position the user
// would aim at. Same construction control_l2_smoke.cpp already uses, so
// this is a known-good delivery on the offscreen platform.
void realClick(QWidget *w, const QPointF &local)
{
    if (!w)
        return;
    const QPointF gp = w->mapToGlobal(QPoint(qRound(local.x()),
                                             qRound(local.y())));
    QMouseEvent press(QEvent::MouseButtonPress, local, gp, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, local, gp, Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QApplication::sendEvent(w, &release);
    QCoreApplication::processEvents();
}

// The position fields are wired to QSpinBox::editingFinished (Enter, or
// leaving an edited field) — control_panel.cpp:6627 — so a bare
// setValue() is not what the user's keystroke does and would leave the
// write untested. This types the digits and presses Enter through real
// QKeyEvents, which is the path the console's own handler serves.
bool typeInto(QSpinBox *box, int value)
{
    if (!box)
        return false;
    box->setFocus();
    box->selectAll();
    const QString digits = QString::number(value);
    for (const QChar c : digits) {
        QKeyEvent press(QEvent::KeyPress, c.unicode(), Qt::NoModifier,
                        QString(c));
        QApplication::sendEvent(box, &press);
    }
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Enter, Qt::KeypadModifier,
                    QStringLiteral("\r"));
    QApplication::sendEvent(box, &enter);
    QCoreApplication::processEvents();
    return box->value() == value;
}

void seedGame(const char *game)
{
    QSettings settings;
    settings.setValue(QStringLiteral("game"), QString::fromLatin1(game));
    settings.sync();
}

void setScope(mhw::GameId game, mhw::PlayerLayoutMode mode)
{
    mhw::setPanelSettingsGame(game);
    mhw::setPanelSettingsLayout(mode);
}

// The stage head's chip — reference.html's #window-count.
QLabel *countChip(ControlPanel *cp)
{
    return cp->findChild<QLabel *>(QStringLiteral("stageWindowCountChip"));
}

// What #window-count MUST say, recomputed from the same public model
// the console itself uses. refreshStageHead()'s screen-name fallback is
// three steps (canvas preview screen → Qt's primary → the first
// screen_query output) and this mirrors it exactly, so the assertion is
// "the chip equals f(model)" — a wiring claim — rather than a claim
// about what a particular platform names its outputs. Comparing against
// the number the chip prints is what proves the chip is DERIVED and not
// a frozen literal; the predicate itself is pinned separately, in
// console-fidelity-windows-tests.
int expectedCount(ControlPanel *cp, HudCanvas *canvas)
{
    QString screenName = canvas ? canvas->previewScreen() : QString();
    if (screenName.isEmpty()) {
        if (QScreen *s = QGuiApplication::primaryScreen())
            screenName = s->name();
    }
    const QList<screen_query::OutputInfo> outputs =
        screen_query::listOutputs();
    if (screenName.isEmpty() && !outputs.isEmpty())
        screenName = outputs.first().name;
    int n = 0;
    const mhw::WindowList list = cp->buildWindowStates();
    for (const mhw::WindowState &w : list)
        if (mhw::windowOnDisplay(w, screenName))
            ++n;
    return n;
}

int chipCount(ControlPanel *cp)
{
    QLabel *chip = countChip(cp);
    if (!chip)
        return -1;
    // 「3 个窗口」 — the digits are the leading run.
    const QString t = chip->text();
    QString digits;
    for (const QChar c : t) {
        if (c.isDigit())
            digits += c;
        else if (!digits.isEmpty())
            break;
    }
    return digits.isEmpty() ? -1 : digits.toInt();
}

} // namespace

int main(int argc, char *argv[])
{
    // LINE-BUFFERED, not block-buffered: this test drives a nested
    // event loop, and a hung exec() would otherwise take every printf
    // still sitting in a 4KB stdio buffer down with it — which is
    // exactly what made the first hang undiagnosable.
    setvbuf(stdout, nullptr, _IOLBF, 0);
    // BEFORE QApplication and before anything opens panels.ini.
    QTemporaryDir configRoot;
    qputenv("XDG_CONFIG_HOME", configRoot.path().toLocal8Bit());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationName(QStringLiteral("monster-control"));

    testRootScrollSizing();

    seedGame("world");
    setScope(mhw::GameId::World, mhw::PlayerLayoutMode::Single);

    // ==================================================================
    // A-1. THE CONSOLE'S OWN BOARD IS THE PROTOTYPE'S BOARD
    // ==================================================================
    ControlPanel cp;
    cp.show();
    pump();
    auto *canvas = cp.findChild<HudCanvas *>();
    check(canvas != nullptr, "A1: the console built a HudCanvas");
    if (canvas)
        check(canvas->consoleFidelityMode(),
              "A1: setConsoleFidelityMode(true) was applied to the console's "
              "canvas — the O2 paint/geometry gates are live in the shell");
    // And the DEFAULT is still false, which is what keeps every baseline
    // and every canvas->grab() suite that builds its own HudCanvas on the
    // pre-1.0.0 path.
    {
        HudCanvas fresh;
        check(!fresh.consoleFidelityMode(),
              "A1: a canvas nobody configured still defaults to fidelity "
              "OFF — the baseline path is untouched");
    }

    // ==================================================================
    // A-2. winVisible: written by the checkbox, pushed into the canvas,
    //      re-pushed after a refresh, counted by the chip.
    // ==================================================================
    if (canvas) {
        cp.selectWindow(0, -1);
        pump();
        auto *vis = cp.findChild<QCheckBox *>(QStringLiteral("metaVisible"));
        check(vis != nullptr && vis->isChecked(),
              "A2: the 显示该窗口 checkbox exists and starts checked (the "
              "key is absent, and absent means visible)");
        check(canvas->panelWindowVisible(0),
              "A2: a window with no stored row is visible on the board");

        const int before = chipCount(&cp);
        const int base = expectedCount(&cp, canvas);
        note(QStringLiteral("stage window-count chip reads %1; the model "
                            "says %2 on display '%3'")
                 .arg(before)
                 .arg(base)
                 .arg([&] {
                     QString n = canvas->previewScreen();
                     if (n.isEmpty())
                         if (QScreen *sc = QGuiApplication::primaryScreen())
                             n = sc->name();
                     if (n.isEmpty()) {
                         const auto o = screen_query::listOutputs();
                         if (!o.isEmpty())
                             n = o.first().name;
                     }
                     return n;
                 }()));
        check(before >= 0, "A2: the #window-count chip is readable");
        check(before == base,
              "A2: the chip equals what the model says — #window-count is "
              "derived from buildWindowStates() + windowOnDisplay(), not a "
              "frozen number",
              QStringLiteral("chip=%1 model=%2").arg(before).arg(base));
        // WHETHER any window counts is a fact about this platform's output
        // naming, not about the wiring. The two namespaces are Qt's
        // QScreen::name() (the tail of the windows' own→parent→primary
        // chain, and refreshStageHead's second fallback) and screen_query's
        // output name (its third). They are the SAME string on a real
        // machine — both are the wlr-output name — and they are not on the
        // offscreen platform (Qt says "", screen_query says "screen@0,0"),
        // and Panel::setOutputName() validates against the Qt list, so no
        // window can be pointed at the screen_query spelling from here.
        // Recorded rather than faked: the delta below runs when they meet
        // and is skipped with this reason when they do not; the predicate
        // itself is pinned independently in console-fidelity-windows-tests.
        if (base > 0)
            check(true, "A2: at least one window is on the drawn display");
        else
            note(QStringLiteral("base count is 0 on this platform: Qt "
                                "primary='%1' vs screen_query first='%2' — "
                                "the two output namespaces do not intersect "
                                "offscreen, so the count delta below is "
                                "reported as a note")
                     .arg(QGuiApplication::primaryScreen()
                              ? QGuiApplication::primaryScreen()->name()
                              : QString())
                     .arg(screen_query::listOutputs().isEmpty()
                              ? QString()
                              : screen_query::listOutputs().first().name));

        if (vis) {
            vis->setChecked(false);
            pump();
        }
        const mhw::WindowList after = cp.buildWindowStates();
        check(!after.isEmpty() && !after[0].visiblePersisted,
              "A2: the checkbox write landed in the model (visiblePersisted)");
        check(!canvas->panelWindowVisible(0),
              "A2: …and was PUSHED into the canvas — the board stopped "
              "drawing it on the same click");
        if (base > 0) {
            check(expectedCount(&cp, canvas) == base - 1,
                  "A2: hiding one window takes exactly one off the display's "
                  "count — the MODEL moved",
                  QStringLiteral("model=%1 base=%2")
                      .arg(expectedCount(&cp, canvas))
                      .arg(base));
        } else {
            note("count delta not observable here (base 0 — see the naming "
                 "note above); the chip==model equality below still runs");
        }
        check(chipCount(&cp) == expectedCount(&cp, canvas),
              "A2: …and the CHIP still equals the model after the hide — "
              "it counts visibleWindows(), not raw output matches",
              QStringLiteral("chip=%1 model=%2")
                  .arg(chipCount(&cp))
                  .arg(expectedCount(&cp, canvas)));

        // THE ROW IS DURABLE: the same scope the checkbox wrote to reads
        // hidden for the next process. The scope is spelled from the
        // model's own key rather than guessed — it is exactly the group
        // writeWindowVisible() put the row under.
        const QString hiddenGroup =
            mhw::panelSettingsScopeFor(mhw::GameId::World,
                                       mhw::PlayerLayoutMode::Single,
                                       after.isEmpty()
                                           ? QStringLiteral("player")
                                           : after[0].key);
        check(!mhw::readWindowVisible(hiddenGroup, QStringLiteral("x")),
              "A2: the row is on disk for the next process to read");
        check(!mhw::windowOnDisplay(after[0],
                                    after[0].outputResolved),
              "A2: windowOnDisplay() drops it from the count on the same "
              "three facts the chip uses");

        // THE RE-PUSH. A rebind resets the canvas side to its default
        // (a fresh SubSlot / a fresh panel flag is visible); the shell
        // has to put the filter back. Simulate exactly that reset, then
        // ask for a refresh.
        canvas->setPanelWindowVisible(0, true);
        check(canvas->panelWindowVisible(0),
              "A2: (setup) the canvas flag can be reset to visible, as a "
              "rebind does");
        cp.refreshWindowModel();
        pump();
        check(!canvas->panelWindowVisible(0),
              "A2: refreshWindowModel() RE-PUSHES the filter — a rebuild "
              "does not resurrect a hidden window");

        // SHOW AGAIN — and the helper removes the row rather than
        // writing true, so no spurious key is left behind.
        if (vis) {
            vis->setChecked(true);
            pump();
        }
        const mhw::WindowList shown = cp.buildWindowStates();
        check(shown[0].visiblePersisted,
              "A2: showing the window restores the model");
        check(canvas->panelWindowVisible(0),
              "A2: …and the board draws it again");
        check(chipCount(&cp) == before
                  && chipCount(&cp) == expectedCount(&cp, canvas),
              "A2: the count is back where it started after the show, and "
              "chip and model still agree",
              QStringLiteral("chip=%1 base=%2 model=%3")
                  .arg(chipCount(&cp))
                  .arg(before)
                  .arg(expectedCount(&cp, canvas)));

        // SCOPE CHANGE: the filter belongs to the scope that wrote it, so
        // a switch must not carry a stale hide across — and coming back
        // must find it again. (Switching game rebuilds everything: the
        // rail's game button is the real path.)
        {
            const QPushButton *dummy = nullptr;
            Q_UNUSED(dummy);
        }
        QPushButton *splitBtn = nullptr;
        QPushButton *singleBtn = nullptr;
        for (QPushButton *b : cp.findChildren<QPushButton *>()) {
            if (b->objectName() == QStringLiteral("layoutSegSplit"))
                splitBtn = b;
            if (b->objectName() == QStringLiteral("layoutSegSingle"))
                singleBtn = b;
        }
        note(splitBtn && singleBtn
                 ? "layout toggles found"
                 : "layout toggles NOT found by objectName — the rebuild "
                   "check below will be reported as skipped");
        if (splitBtn && singleBtn) {
            splitBtn->click();
            pump();
            // In Split the mother panels are not windows, so the filter
            // the Single scope wrote must not be sitting on any live slot.
            bool anySubHidden = false;
            for (int i = 0; i < canvas->subWindowCount(); ++i)
                if (!canvas->subWindowVisible(i))
                    anySubHidden = true;
            check(!anySubHidden,
                  "A2: after a rebuild every SPLIT binding starts visible — "
                  "the rebind reset is not carrying a stale index's flag");
            singleBtn->click();
            pump();
            const mhw::WindowList back = cp.buildWindowStates();
            check(!back.isEmpty() && back[0].visiblePersisted,
                  "A2: switching back re-reads the SAME row (absent = shown)");
            check(canvas->panelWindowVisible(0),
                  "A2: …and the board agrees after the round trip");
        }
    }

    // ==================================================================
    // A-3. THE FOURTH AXIS IS CONSUMED BY THE PROCESS THAT MOUNTS.
    //      Panel::loadConfig reads `winVisible`, Panel::setVisible's
    //      existing mount gate honours it. Private config, real Panel,
    //      real show().
    // ==================================================================
    {
        setScope(mhw::GameId::World, mhw::PlayerLayoutMode::Single);

        // (a) KEY ABSENT → default true → the gate is the pre-change
        //     expression, so this window mounts exactly as before.
        {
            PlayerPanel p;
            check(p.windowVisibleIntent(),
                  "A3: a group with no winVisible row reads intent=true");
            p.show();
            pump();
            check(p.isVisible(),
                  "A3: …and show() mounts it — the absent key is "
                  "behaviourally identical to the old code");
            const QString group = p.settingsGroup();
            check(mhw::readWindowVisible(group, p.settingsKey()),
                  "A3: readWindowVisible agrees with loadConfig (absent → true)");
        }

        const QString worldGroup =
            mhw::panelSettingsScopeFor(mhw::GameId::World,
                                       mhw::PlayerLayoutMode::Single,
                                       QStringLiteral("player"));

        // (b) HIDE → a NEW construction of the same window does not mount.
        mhw::writeWindowVisible(worldGroup, QStringLiteral("player"), false);
        {
            PlayerPanel p;
            check(!p.windowVisibleIntent(),
                  "A3: a fresh Panel reads winVisible=false from its own group");
            p.show();
            pump();
            check(!p.isVisible(),
                  "A3: show() on a hidden window mounts NOTHING — the "
                  "existing gate in Panel::setVisible honours the intent");
            check(p.settingsGroup() == worldGroup,
                  "A3: the window's group is the one the console wrote to",
                  p.settingsGroup());
        }

        // (c) FOUR-DOMAIN ISOLATION. Hiding world/single/player must not
        //     touch rise/single, world/split or rise/split — the four
        //     namespaces are four different groups and this is the check
        //     that says so with a real construction in each.
        {
            setScope(mhw::GameId::Rise, mhw::PlayerLayoutMode::Single);
            PlayerPanel rise;
            check(rise.settingsGroup() != worldGroup,
                  "A3: rise/single is a DIFFERENT group from world/single");
            check(rise.windowVisibleIntent(),
                  "A3: rise/single/player still reads intent=true");
            rise.show();
            pump();
            check(rise.isVisible(),
                  "A3: …and mounts — the hide was scoped to one domain");
        }
        {
            setScope(mhw::GameId::World, mhw::PlayerLayoutMode::Split);
            PlayerPanel split;
            check(split.windowVisibleIntent(),
                  "A3: world/split/player still reads intent=true");
            split.show();
            pump();
            check(split.isVisible(),
                  "A3: …and mounts — the other axis of the four domains is "
                  "untouched too");
        }
        {
            setScope(mhw::GameId::Rise, mhw::PlayerLayoutMode::Split);
            PlayerPanel riseSplit;
            check(riseSplit.windowVisibleIntent(),
                  "A3: rise/split/player still reads intent=true");
            riseSplit.show();
            pump();
            check(riseSplit.isVisible(),
                  "A3: …and mounts — all four domains are independent");
        }

        // (d) SHOW AGAIN removes the row (the helper's polarity: on →
        //     remove, not `winVisible=true`), and a new construction
        //     mounts.
        setScope(mhw::GameId::World, mhw::PlayerLayoutMode::Single);
        mhw::writeWindowVisible(worldGroup, QStringLiteral("player"), true);
        {
            QSettings probe(QSettings::IniFormat, QSettings::UserScope,
                            QStringLiteral("monster-overlay"),
                            QStringLiteral("panels"));
            probe.beginGroup(worldGroup);
            check(!probe.contains(QStringLiteral("winVisible")),
                  "A3: showing the window REMOVES the row — no "
                  "`winVisible=true` is ever written");
            probe.endGroup();
        }
        {
            PlayerPanel p;
            check(p.windowVisibleIntent(),
                  "A3: after the show, a fresh construction reads intent=true");
            p.show();
            pump();
            check(p.isVisible(),
                  "A3: …and mounts again — the round trip is complete");
        }
    }

    // ==================================================================
    // A-4. THE VIEW / DISPLAY STATE CONTRACT, AFTER REAL MOUSE EVENTS.
    // ==================================================================
    if (canvas) {
        setScope(mhw::GameId::World, mhw::PlayerLayoutMode::Single);
        auto *navOverview = cp.findChild<QFrame *>(
            QStringLiteral("navOverview"));
        check(navOverview != nullptr, "A4: the sidebar's 总览 card exists");

        // Does a synthetic press/release even reach the widget? A local
        // spy installed on the card answers the DELIVERY half separately
        // from "did the console act on it", so a failure below can be
        // attributed rather than guessed at. It returns false, so the
        // console's own filter runs after it as usual.
        struct ReleaseSpy : QObject {
            QObject *target = nullptr;
            int releases = 0;
            bool eventFilter(QObject *o, QEvent *e) override
            {
                if (o == target && e->type() == QEvent::MouseButtonRelease)
                    ++releases;
                return false;
            }
        };

        // The console's own starting view. It is the overview — a game or
        // mode switch lands there (resetViewStateAfterScopeChange) and the
        // ctor enters it — so this is recorded rather than assumed, and
        // the board-click half below makes the state explicit first
        // instead of relying on it.
        note(QStringLiteral("console view at this point: overview=%1")
                 .arg(int(canvas->overviewMode())));
        ReleaseSpy spy;
        spy.target = navOverview;
        if (navOverview)
            navOverview->installEventFilter(&spy);
        if (!canvas->overviewMode() && navOverview) {
            realClick(navOverview,
                      QPointF(navOverview->width() / 2.0,
                              navOverview->height() / 2.0));
            pump();
        }
        check(canvas->overviewMode(),
              "A4: the overview is OPEN before the board click");
        // (The nav clicks below are the ones that must be delivered; the
        // spy is read at the end of the block, after all of them.)

        // Make a board click OBSERVABLE: park the model on the damage
        // window and the canvas on "nothing", so any box that is hit has
        // to move the model for the probe to report success. Without this
        // the probe can hit a box, select it, and look like a miss.
        cp.selectWindow(2, -1);
        pump();
        canvas->setSelectedPanel(-1);
        canvas->setSelectedSubWindow(-1);

        const QString viewer = canvas->previewScreen();
        const int panelBefore = cp.selectedWindow().panelIndex;
        const int subBefore = cp.selectedWindow().subIndex;

        // SINGLE. The mother boxes are the windows and their rects are
        // not published, so the hit is FOUND with real clicks: a coarse
        // probe over the board, every point a genuine press/release,
        // stopping at the first one that moved the model.
        bool hit = false;
        QPointF hitAt;
        const int w = canvas->width();
        const int h = canvas->height();
        for (int y = 4; y < h - 4 && !hit; y += 6) {
            for (int x = 4; x < w - 4; x += 6) {
                realClick(canvas, QPointF(x, y));
                const mhw::WindowState now = cp.selectedWindow();
                if (now.panelIndex != panelBefore
                    || now.subIndex != subBefore) {
                    hit = true;
                    hitAt = QPointF(x, y);
                    break;
                }
            }
        }
        pump();
        check(hit,
              "A4: a real mouse press on a Single board box SELECTED a "
              "window (the probe is not vacuous)",
              hit ? QStringLiteral("hit at %1,%2").arg(qRound(hitAt.x()))
                        .arg(qRound(hitAt.y()))
                  : QString());
        check(cp.selectedWindow().panelIndex >= 0,
              "A4: …the inspector has a window to describe");
        check(canvas->overviewMode(),
              "A4: SINGLE — clicking a window in the overview LEAVES IT "
              "OPEN (reference.html's board handler writes state.selected, "
              "never state.view)");
        check(canvas->previewScreen() == viewer,
              "A4: …and the VIEWER is unchanged — a board click does not "
              "move the display",
              QStringLiteral("viewer=%1 was=%2")
                  .arg(canvas->previewScreen(), viewer));
        if (navOverview)
            check(navOverview->property("selected").toBool(),
                  "A4: the 总览 card is still the highlighted view entry — "
                  "the board click did not silently switch the rail");

        // SPLIT. The same contract in the other topology.
        QPushButton *splitBtn = nullptr;
        for (QPushButton *b : cp.findChildren<QPushButton *>())
            if (b->objectName() == QStringLiteral("layoutSegSplit"))
                splitBtn = b;
        if (splitBtn) {
            splitBtn->click();
            pump();
            note(QStringLiteral("canvas layoutMode after the segment click: "
                                "split=%1")
                     .arg(int(canvas->layoutMode()
                              == mhw::PlayerSection::PlayerLayoutMode::Split)));
            if (!canvas->overviewMode() && navOverview) {
                realClick(navOverview,
                          QPointF(navOverview->width() / 2.0,
                                  navOverview->height() / 2.0));
                pump();
            }
            check(canvas->overviewMode(),
                  "A4/Split: the overview is open for the split board");
            cp.selectWindow(0, -1);
            pump();
            canvas->setSelectedPanel(-1);
            canvas->setSelectedSubWindow(-1);
            const int sPanelBefore = cp.selectedWindow().panelIndex;
            const int sSubBefore = cp.selectedWindow().subIndex;
            const QString viewerSplit = canvas->previewScreen();

            // Prefer the published rects (subWindowRects()); fall back to
            // the same grid probe if the overview laid nothing out on this
            // platform, so the half is still exercised rather than
            // skipped for a geometry reason.
            canvas->grab();
            pump();
            QVector<QRectF> rects = canvas->subWindowRects();
            bool sHit = false;
            if (!rects.isEmpty()) {
                realClick(canvas, rects.first().center());
                pump();
                const mhw::WindowState now = cp.selectedWindow();
                sHit = now.subIndex != sSubBefore
                       || now.panelIndex != sPanelBefore;
            }
            if (!sHit) {
                for (int y = 4; y < h - 4 && !sHit; y += 6) {
                    for (int x = 4; x < w - 4; x += 6) {
                        realClick(canvas, QPointF(x, y));
                        const mhw::WindowState now = cp.selectedWindow();
                        if (now.subIndex != sSubBefore
                            || now.panelIndex != sPanelBefore) {
                            sHit = true;
                            hitAt = QPointF(x, y);
                            break;
                        }
                    }
                }
                pump();
            }
            check(sHit,
                  "A4/Split: a real click on a window box SELECTED it (the "
                  "other topology's non-vacuous hit)");
            check(canvas->overviewMode(),
                  "A4/Split: clicking a window in the overview LEAVES IT "
                  "OPEN — the same rule, the split topology");
            check(canvas->previewScreen() == viewerSplit,
                  "A4/Split: …and the VIEWER is unchanged",
                  QStringLiteral("viewer=%1 was=%2")
                      .arg(canvas->previewScreen(), viewerSplit));
        } else {
            note("layoutSegSplit not found — the Split half of A4 was not "
                 "exercised");
        }

        // Navigation names a destination, not a toggle. Repeated Overview
        // clicks keep the same view; a panel card is the exit from it.
        if (canvas->overviewMode() && navOverview) {
            realClick(navOverview,
                      QPointF(navOverview->width() / 2.0,
                              navOverview->height() / 2.0));
            pump();
            check(canvas->overviewMode(),
                  "A4: clicking 总览 again KEEPS the overview — navigation "
                  "is idempotent");
            check(navOverview->property("selected").toBool(),
                  "A4: the overview highlight still describes the view");
            auto *navPlayer = cp.findChild<QFrame *>(QStringLiteral("navPlayer"));
            check(navPlayer != nullptr,
                  "A4: player navigation exists for the opposite-view control");
            if (navPlayer) {
                const QString viewerBeforeNav = canvas->previewScreen();
                realClick(navPlayer,
                          QPointF(navPlayer->width() / 2.0,
                                  navPlayer->height() / 2.0));
                pump();
                check(!canvas->overviewMode(),
                      "A4: a real panel-nav click LEAVES the overview");
                check(!navOverview->property("selected").toBool(),
                      "A4: its highlight leaves with the view");
                check(canvas->previewScreen() == viewerBeforeNav,
                      "A4: panel navigation does not switch the viewer display");
            }
            realClick(navOverview,
                      QPointF(navOverview->width() / 2.0,
                              navOverview->height() / 2.0));
            pump();
            check(canvas->overviewMode(),
                  "A4: clicking 总览 from a panel view enters overview again");
        }
        if (navOverview)
            navOverview->removeEventFilter(&spy);
        check(spy.releases >= 1,
              "A4: every assertion above ran on events that REALLY "
              "reached the 总览 card — the probe is not vacuous",
              QStringLiteral("releases=%1").arg(spy.releases));
    }

    // ==================================================================
    // A-5. CHANGING A WINDOW'S OUTPUT DOES NOT MOVE THE VIEWER.
    // ==================================================================
    if (canvas) {
        if (cp.selectedWindow().panelIndex < 0)
            cp.selectWindow(0, -1);
        pump();
        auto *out = cp.findChild<QComboBox *>(QStringLiteral("metaOutput"));
        check(out != nullptr, "A5: the 目标显示器 combo exists");
        const QString viewerBefore = canvas->previewScreen();
        if (out && out->count() >= 2) {
            const int want = (out->currentIndex() + 1) % out->count();
            out->setCurrentIndex(want);
            pump();
            check(canvas->previewScreen() == viewerBefore,
                  "A5: retargeting the WINDOW's output leaves the board's "
                  "viewer (state.display) where it was",
                  QStringLiteral("viewer=%1 was=%2")
                      .arg(canvas->previewScreen(), viewerBefore));
        } else if (out) {
            note(QStringLiteral("only %1 output entry/entries on the "
                                "offscreen platform — A5's retarget was not "
                                "exercised, viewer recorded as %2")
                     .arg(out->count())
                     .arg(viewerBefore));
        }
    }

    // ==================================================================
    // D. POSITION INPUT IS THE LOGICAL TOP-LEFT — FOUR CORNERS, ROUND
    //    TRIPPED, AND THROUGH THE REAL SPIN BOX.
    // ==================================================================

    // D-1 PURE. The forward formula is pinned against the ORIGINAL
    // anchoredRect() body spelled out literally here — same clamp, same
    // cast, same screen.right()/bottom() edges — so the refactor into
    // ui/window_layout_geometry.h cannot quietly move the default
    // picture. Then each corner round-trips margins → top-left → margins
    // EXACTLY, with the two sides the corner does not own passed through.
    {
        const QRect screen(0, 0, 2560, 1440);
        const QSize content(320, 180);
        const qreal scale = 1.5;
        const Corner corners[] = {Corner::TopLeft, Corner::TopRight,
                                  Corner::BottomLeft, Corner::BottomRight};
        const char *names[] = {"TopLeft", "TopRight", "BottomLeft",
                               "BottomRight"};
        for (int i = 0; i < 4; ++i) {
            const QMargins m(20, 30, 40, 50);
            // THE ORIGINAL, LINE FOR LINE. int() truncation and max(1,)
            // included — this is the byte-for-byte claim.
            const int w0 = std::max(1, int(content.width() * scale));
            const int h0 = std::max(1, int(content.height() * scale));
            int ex = 0, ey = 0;
            switch (corners[i]) {
            case Corner::TopLeft:
                ex = screen.left() + m.left();
                ey = screen.top() + m.top();
                break;
            case Corner::TopRight:
                ex = screen.right() - w0 - m.right();
                ey = screen.top() + m.top();
                break;
            case Corner::BottomLeft:
                ex = screen.left() + m.left();
                ey = screen.bottom() - h0 - m.bottom();
                break;
            case Corner::BottomRight:
                ex = screen.right() - w0 - m.right();
                ey = screen.bottom() - h0 - m.bottom();
                break;
            }
            const QPoint tl = mhw::layout::anchoredTopLeft(corners[i], m,
                                                            screen, content,
                                                            scale);
            check(tl == QPoint(ex, ey),
                  QStringLiteral("D1: %1's logical top-left equals the "
                                  "ORIGINAL anchoredRect arithmetic")
                      .arg(QString::fromLatin1(names[i])),
                  QStringLiteral("got %1,%2 want %3,%4")
                      .arg(tl.x()).arg(tl.y()).arg(ex).arg(ey));

            const QRect rect =
                mhw::layout::anchoredRect(screen, corners[i], m, content,
                                          scale);
            check(rect == QRect(ex, ey, w0, h0),
                  QStringLiteral("D1: %1's drawn rect is identical to the "
                                  "old QRect(x,y,w,h) — no default drift")
                      .arg(QString::fromLatin1(names[i])));

            // The requested top-left, taken somewhere OTHER than the
            // stored one so the round trip is not a tautology.
            const QPoint want(ex + 137, ey - 61);
            const QMargins back =
                mhw::layout::marginsForTopLeft(corners[i], want, screen,
                                               content, scale, m);
            const QPoint again =
                mhw::layout::anchoredTopLeft(corners[i], back, screen,
                                             content, scale);
            check(again == want,
                  QStringLiteral("D1: %1 round-trips — margins → top-left "
                                  "→ margins → top-left is exact")
                      .arg(QString::fromLatin1(names[i])),
                  QStringLiteral("want %1,%2 got %3,%4")
                      .arg(want.x()).arg(want.y())
                      .arg(again.x()).arg(again.y()));
            // And the sides this corner does NOT own are untouched: an
            // edit means "move", never "resize". Left/right are owned by
            // the horizontal pair of the corner, top/bottom by the
            // vertical pair — so exactly one of each axis is rewritten.
            const bool ownsLeft = corners[i] == Corner::TopLeft
                                  || corners[i] == Corner::BottomLeft;
            const bool ownsTop = corners[i] == Corner::TopLeft
                                 || corners[i] == Corner::TopRight;
            check(ownsLeft ? back.right() == m.right()
                           : back.left() == m.left(),
                  QStringLiteral("D1: %1 rewrites only the horizontal side "
                                  "it owns — the other is passed through")
                      .arg(QString::fromLatin1(names[i])),
                  QStringLiteral("l=%1 r=%2 (was %3/%4)")
                      .arg(back.left()).arg(back.right())
                      .arg(m.left()).arg(m.right()));
            check(ownsTop ? back.bottom() == m.bottom()
                          : back.top() == m.top(),
                  QStringLiteral("D1: %1 rewrites only the vertical side it "
                                  "owns — the other is passed through")
                      .arg(QString::fromLatin1(names[i])),
                  QStringLiteral("t=%1 b=%2 (was %3/%4)")
                      .arg(back.top()).arg(back.bottom())
                      .arg(m.top()).arg(m.bottom()));
        }
        // A Right-anchored window whose X is typed: the OLD code wrote
        // left/top and the box never moved. Pinned as the property the
        // fix exists for.
        const QPoint typed(100, 100);
        const QMargins mr =
            mhw::layout::marginsForTopLeft(Corner::TopRight, typed, screen,
                                           content, scale, QMargins(20, 30, 40, 50));
        check(mr.right() != 40,
              "D1: typing X on a TopRight window rewrites `right` — the "
              "field finally reaches the anchor it describes");
        check(mhw::layout::anchoredTopLeft(Corner::TopRight, mr, screen,
                                           content, scale) == typed,
              "D1: …and the window now sits exactly at the typed X/Y");
    }

    // D-2 THROUGH THE REAL WIDGETS. Split, pick a window, type an X into
    // the real spin box, and require BOTH halves: the canvas's published
    // sub-window rect MOVED (the field reaches the paint), and the stored
    // margins moved by exactly the amount the corner implies (the model
    // agrees with the field). The second assertion is written as a
    // DIFFERENCE between two typings, so it needs neither the window's
    // content size nor which corner it is, and cannot be satisfied by a
    // conversion that is self-consistently wrong.
    if (canvas) {
        QPushButton *splitBtn = nullptr;
        for (QPushButton *b : cp.findChildren<QPushButton *>())
            if (b->objectName() == QStringLiteral("layoutSegSplit"))
                splitBtn = b;
        auto *spinX = cp.findChild<QSpinBox *>(QStringLiteral("metaX"));
        auto *spinY = cp.findChild<QSpinBox *>(QStringLiteral("metaY"));
        check(splitBtn != nullptr && spinX != nullptr && spinY != nullptr,
              "D2: the layout segment and both position boxes exist");
        if (splitBtn && spinX && spinY) {
            splitBtn->click();
            pump();
            canvas->grab();
            pump();
            const mhw::WindowList list0 = cp.buildWindowStates();
            int target = -1;
            for (int i = 0; i < list0.size(); ++i) {
                if (list0[i].subIndex >= 0
                    && list0[i].key.contains(QStringLiteral("chart"))) {
                    target = i;
                    break;
                }
            }
            for (int i = 0; target < 0 && i < list0.size(); ++i)
                if (list0[i].subIndex >= 0)
                    target = i;
            check(target >= 0, "D2: the split scope has a window to drive");
            if (target >= 0) {
                const int sub = list0[target].subIndex;
                cp.selectWindow(list0[target].panelIndex, sub);
                pump();
                check(spinX->isEnabled(),
                      "D2: the X box is ENABLED when the window's output "
                      "resolves — an unresolvable output disables the pair "
                      "instead of showing a fabricated frame");
                if (spinX->isEnabled()) {
                    // Opening preserves raw margins even when today's
                    // screen must clamp them. Establish an explicit edit
                    // before comparing two stored anchor deltas.
                    const int normalizedX = spinX->value() + 20;
                    check(typeInto(spinX, normalizedX),
                          "D2: normalize the runtime-clamped starting position by a real edit");
                    pump();
                    check(spinX->value() == normalizedX,
                          "D2: normalized coordinate is the one actually presented");
                    canvas->grab();
                    pump();
                    const QVector<QRectF> before = canvas->subWindowRects();
                    check(sub >= 0 && sub < before.size(),
                          "D2: the canvas published this window's rect");
                    if (sub >= 0 && sub < before.size()) {
                        const QRectF r0 = before[sub];
                        const int x1 = spinX->value();
                        const int x2 = x1 + 41;
                        QMargins m0;
                        bool have0 = false;
                        for (const mhw::WindowState &w : cp.buildWindowStates())
                            if (w.subIndex == sub) { m0 = w.margins; have0 = true; break; }
                        check(have0, "D2: the model has this window's margins");
                        const bool typed = typeInto(spinX, x2);
                        pump();
                        check(typed,
                              "D2: the digits and Enter really reached the "
                              "spin box (editingFinished is the wired path)",
                              QStringLiteral("value=%1 want=%2")
                                  .arg(spinX->value()).arg(x2));
                        QMargins m1;
                        bool have1 = false;
                        for (const mhw::WindowState &w : cp.buildWindowStates())
                            if (w.subIndex == sub) { m1 = w.margins; have1 = true; break; }
                        check(have1, "D2: …and still has them after the edit");
                        canvas->grab();
                        pump();
                        const QVector<QRectF> after = canvas->subWindowRects();
                        check(sub < after.size() && after[sub] != r0,
                              "D2: typing an X MOVED the drawn box — the "
                              "field reaches the paint (the old code wrote "
                              "`left` and a right-anchored box never moved)",
                              QStringLiteral("before=%1,%2 after=%3,%4")
                                  .arg(r0.x()).arg(r0.y())
                                  .arg(sub < after.size() ? after[sub].x() : -1.0)
                                  .arg(sub < after.size() ? after[sub].y() : -1.0));
                        if (have0 && have1) {
                            const bool leftMoved = m1.left() != m0.left();
                            const bool rightMoved = m1.right() != m0.right();
                            check(leftMoved != rightMoved,
                                  "D2: exactly ONE horizontal anchor moved — "
                                  "the corner's own side, not both",
                                  QStringLiteral("m0=(%1,%2,%3,%4) "
                                                 "m1=(%5,%6,%7,%8)")
                                      .arg(m0.left()).arg(m0.top())
                                      .arg(m0.right()).arg(m0.bottom())
                                      .arg(m1.left()).arg(m1.top())
                                      .arg(m1.right()).arg(m1.bottom()));
                            check(m1.top() == m0.top()
                                      && m1.bottom() == m0.bottom(),
                                  "D2: an X edit never touches the vertical "
                                  "anchors — the field MOVES, it does not "
                                  "resize");
                            const int dx = x2 - x1;
                            const bool ok = leftMoved
                                ? (m1.left() - m0.left()) == dx
                                : (m1.right() - m0.right()) == -dx;
                            check(ok,
                                  "D2: the stored anchor moved by exactly "
                                  "the typed delta (sign is the corner's), "
                                  "so the conversion is right not just "
                                  "self-consistent",
                                  QStringLiteral("dx=%1 dLeft=%2 dRight=%3")
                                      .arg(dx)
                                      .arg(m1.left() - m0.left())
                                      .arg(m1.right() - m0.right()));
                        }
                    }
                }
            }
        } else {
            note("D2 not exercised: the layout segment or the position "
                 "boxes were not found");
        }
    }

    // D-3 NOT EXERCISED HERE, STATED RATHER THAN ASSUMED: 「own output ≠
    // viewer」 needs two attached outputs. The offscreen platform has one,
    // so `w.outputResolved` and the viewer's screen are the same frame
    // and the two branches cannot be told apart. The code reads
    // `w.outputResolved` (see logicalSizeForOutput()'s note in
    // control_panel.cpp), and the branch is reported as untested rather
    // than claimed.

    // ==================================================================
    // C. THE MODAL SHELL AND THE TWO BUTTON GROUPS.
    //
    // The dialogs are stack-local and run their own nested event loop
    // (QDialog::exec), so the whole interaction is scheduled INSIDE that
    // loop: a QTimer::singleShot queued before click() fires once exec()
    // is pumping, and click() returns only when the dialog has closed.
    // Everything below therefore runs against the LIVE dialog, not a
    // mock of one.
    // ==================================================================
    {
        auto *settingsBtn =
            cp.findChild<QPushButton *>(QStringLiteral("settingsBtn"));
        check(settingsBtn != nullptr, "C: the 设置 entry exists");
        auto counted = [](QButtonGroup *g) {
            int n = 0;
            if (g)
                for (QAbstractButton *b : g->buttons())
                    if (b->isChecked())
                        ++n;
            return n;
        };
        if (settingsBtn) {
            auto step1 = [&] {
                QDialog *dlg = qobject_cast<QDialog *>(
                    QApplication::activeModalWidget());
                check(dlg && dlg->objectName()
                                  == QStringLiteral("settingsDialog"),
                      "C: the settings dialog opened as the active modal",
                      dlg ? dlg->objectName() : QString());
                if (!dlg)
                    return;
                std::printf("C: step1 running, dialog=%p\n",
                            static_cast<void *>(dlg));
                // The containers the QSS owner styles — all of them
                // must exist as findable widgets or their CSS silently
                // matches nothing. modalBackdrop is deliberately ABSENT
                // in the compact popup (验收项2): see the check below.
                check(dlg->findChild<QFrame *>(QStringLiteral("modalShell")),
                      "C: modalShell exists (the panel the content sits in)");
                check(dlg->findChild<QFrame *>(QStringLiteral("modalHead")),
                      "C: modalHead exists");
                check(dlg->findChild<QScrollArea *>(QStringLiteral("modalBody")),
                      "C: modalBody exists AND is a scroll area (the "
                      "prototype's .modal-body overflow:auto — long copy "
                      "scrolls inside the shell)");
                check(dlg->findChild<QFrame *>(QStringLiteral("modalActions")),
                      "C: modalActions exists");
                // PARENTAGE: the console's QSS is a widget stylesheet on
                // the ControlPanel, so only a CHILD dialog inherits it.
                check(dlg->parentWidget() == &cp,
                      "C: the dialog is parented to the console, so the "
                      "window stylesheet reaches it (the old "
                      "QDialog(parentWidget()) was null)");
                // v1.0.1 (验收项1 → 验收项2): THE SETTINGS DIALOG IS A
                // COMPACT POPUP OF ONE CONTENT LAYER. It used to be sized
                // to the whole client area, and then shrank inside a veil
                // that still wrapped it in an independent black frame with
                // 64px of black padding. Now the shell IS the dialog's
                // content: no veil element, no outer box, and a height
                // that hugs the copy (≤700). 帮助 / 导出 JSON keep the
                // full-client veil — see centerModalOnConsole(!compact).
                const QWidget *client = cp.centralWidget();
                check(client && dlg->width() <= client->width()
                      && dlg->height() <= client->height(),
                      "C: the settings dialog fits INSIDE the console's "
                      "client area — it no longer fills it",
                      QStringLiteral("dlg=%1x%2 client=%3x%4")
                          .arg(dlg->width()).arg(dlg->height())
                          .arg(client ? client->width() : -1)
                          .arg(client ? client->height() : -1));
                check(dlg->width() <= 680 && dlg->height() <= 700,
                      "C: …and it is the compact popup (≤680×700, 验收项1)",
                      QStringLiteral("%1x%2")
                          .arg(dlg->width()).arg(dlg->height()));
                // 验收项2: ONE LAYER. No second frame around the content,
                // and no 64px of black margin left inside the dialog.
                check(!dlg->findChild<QFrame *>(QStringLiteral("modalBackdrop")),
                      "C: the compact settings popup has NO separate veil "
                      "frame — one content shell only (验收项2)");
                if (QFrame *shell =
                        dlg->findChild<QFrame *>(QStringLiteral("modalShell"))) {
                    if (dlg->layout())
                        dlg->layout()->activate();
                    const QRect g = shell->geometry();
                    const bool fills = g.left() <= 1 && g.top() <= 1
                        && dlg->width()  - g.right()  <= 2
                        && dlg->height() - g.bottom() <= 2;
                    check(fills,
                          "C: modalShell FILLS the dialog — no outer black "
                          "frame, no 64px black padding (验收项2)",
                          QStringLiteral("shell=%1,%2 %3x%4 dlg=%5x%6")
                              .arg(g.x()).arg(g.y()).arg(g.width())
                              .arg(g.height()).arg(dlg->width())
                              .arg(dlg->height()));
                }
                // THE TWO GROUPS.
                auto *tg = dlg->findChild<QButtonGroup *>(
                    QStringLiteral("themeActionGroup"));
                auto *lg = dlg->findChild<QButtonGroup *>(
                    QStringLiteral("localeActionGroup"));
                check(tg && tg->buttons().size() == 2,
                      "C: the dark/light pair is ONE exclusive group of two");
                check(lg && lg->buttons().size() == 2,
                      "C: the 中文/English pair is ONE exclusive group of two");
                check(counted(tg) == 1 && counted(lg) == 1,
                      "C: each group starts with EXACTLY one checked button");
                // Click 深/浅色 first, then the other language — the exact
                // sequence the report asked to be exercised.
                for (QAbstractButton *b : tg->buttons())
                    if (!b->isChecked()) { b->click(); break; }
                pump(80);
                for (QAbstractButton *b : lg->buttons())
                    if (!b->isChecked()) { b->click(); break; }
                pump(80);
                check(counted(tg) == 1,
                      "C: after clicking the other theme, the THEME group "
                      "still has exactly one checked");
                check(counted(lg) == 1,
                      "C: …and after clicking the other language the "
                      "LANGUAGE group still has exactly one checked — the "
                      "two pairs no longer share an exclusive set");
                // A press on the shell's own padding keeps it open.
                if (QFrame *shell =
                        dlg->findChild<QFrame *>(QStringLiteral("modalShell"))) {
                    realClick(shell, QPointF(3, shell->height() - 3));
                    pump(60);
                    check(dlg->isVisible(),
                          "C: a press inside the shell's padding does NOT "
                          "close it (the prototype's e.target!==m)");
                }
                // A press on the VEIL used to be the dismiss path. The
                // compact popup has no veil (验收项2), so the dismiss path
                // is the 关闭 row: it sits OUTSIDE modalBody's scroll
                // area, which is what makes it reachable however long the
                // copy is (关闭按钮固定可达), and its click is the same
                // QDialog::accept the Esc path below reopens against.
                check(!dlg->findChild<QFrame *>(QStringLiteral("modalBackdrop")),
                      "C: …and with no veil there is nothing that can eat "
                      "a click meant for the content");
                if (auto *closeBtn = dlg->findChild<QPushButton *>(
                        QStringLiteral("dialogClose"))) {
                    check(closeBtn->isVisible()
                              && closeBtn->width() > 0
                              && closeBtn->height() > 0,
                          "C: the 关闭 button is visible and reachable "
                          "(it lives outside the scrolling body)");
                    closeBtn->click();
                    pump(60);
                    check(!dlg->isVisible(),
                          "C: the 关闭 button CLOSES the dialog, on the "
                          "same accept path Esc uses");
                }
            };
            // WATCHDOG: if any close path in step1 fails to land, the
            // nested exec() would never return and the whole test would
            // hang instead of FAILing. A hung test reports nothing; a
            // rejected-by-watchdog one reports exactly which assertion
            // was missing.
            auto watchdog = [&] {
                QTimer::singleShot(4000, [&] {
                    if (QDialog *d = qobject_cast<QDialog *>(
                            QApplication::activeModalWidget()))
                        d->reject();
                });
            };
            std::printf("C: opening the settings dialog (watchdog armed)\n");
            watchdog();
            QTimer::singleShot(0, step1);
            settingsBtn->click();   // blocks until the dialog is closed
            std::printf("C: the settings dialog closed, phase 1 done\n");
            pump(150);

            // REOPEN: everything is rebuilt per open, so a second open
            // after a close must answer the same questions — the stale
            // group / stale timer / stale translation callback class.
            auto step2 = [&] {
                QDialog *dlg = qobject_cast<QDialog *>(
                    QApplication::activeModalWidget());
                check(dlg && dlg->objectName()
                                  == QStringLiteral("settingsDialog"),
                      "C: reopening after a close builds a fresh dialog");
                if (!dlg)
                    return;
                std::printf("C: step2 running, dialog=%p\n",
                            static_cast<void *>(dlg));
                check(dlg->findChild<QFrame *>(QStringLiteral("modalShell")),
                      "C: …with a fresh shell");
                auto *tg = dlg->findChild<QButtonGroup *>(
                    QStringLiteral("themeActionGroup"));
                auto *lg = dlg->findChild<QButtonGroup *>(
                    QStringLiteral("localeActionGroup"));
                check(tg && counted(tg) == 1 && lg && counted(lg) == 1,
                      "C: …and exactly one checked per group again — no "
                      "stale QButtonGroup survives the close");
                // Close the way a user does: Esc.
                QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape,
                              Qt::NoModifier);
                QApplication::sendEvent(dlg, &esc);
            };
            std::printf("C: reopening the settings dialog\n");
            watchdog();
            QTimer::singleShot(0, step2);
            settingsBtn->click();
            std::printf("C: the settings dialog closed, phase 2 done\n");
            pump(150);
            check(!QApplication::activeModalWidget(),
                  "C: the second dialog closed on Esc — open/close/"
                  "open/close leaves nothing hanging");
        }
    }

    if (failures == 0)
        std::printf("console-integration-seams-tests: ALL PASSED (%d checks)\n",
                    checks);
    else
        std::fprintf(stderr,
                     "console-integration-seams-tests: %d/%d FAILED\n",
                     failures, checks);
    return failures == 0 ? 0 : 1;
}
