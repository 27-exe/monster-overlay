// SPDX-License-Identifier: Apache-2.0
//
// The console's SCREEN dropdown must write the value the user picked into
// the section the OVERLAY reads — which is [world/split/<key>] on a machine
// whose conf says layout=split, and NOT [world/single/<key>].
//
// THE REPORT, verbatim:
//
//     「有些窗口显示属于 dp-1,但是实际启动 overlay 结果显示在 hdmi。
//       我必须要手动将该窗口选择一下 hdmi,然后在选回 dp-1 才能正常
//       切换到主屏。」
//
// And the reporter's own panels.ini, which is the whole story in four lines:
//
//     [world/single/player]        outputName=DP-1   <- a choice, single domain
//     [world/split/player]         outputName=       <- empty, the split domain
//     [world/split/dps]            outputName=DP-1   <- the one that HAS one
//     [world/split/player-status]  outputName=       <- empty, and 11 more
//
// A split-mode overlay reads [world/split/*]. The console's dropdown
// advertised "DP-1" while the value it wrote lived in the SINGLE
// namespace, so the pick never reached the child — which mounted on
// primaryScreen(). primaryScreen() on the reporter's machine is a portrait
// HDMI-A-1, and the panel was pointed at a landscape DP-1, so the overlay
// landed on the wrong physical monitor.
//
// THE TWO-SCREEN FIXTURE. QT_QPA_PLATFORM=offscreen reports exactly ONE
// screen, named the empty string, at 800x800 — and Panel::setOutputName()
// validates against QGuiApplication::screens(), so "DP-1" is always
// rejected and always reverts to primary. There is no env var that fixes
// this. What does work is Qt's own QPA seam — the same private API
// src/ui/screen_query.cpp already compiles against, and the same technique
// tests/main_output_wiring_tests.cpp uses:
// QWindowSystemInterface::handleScreenAdded() plus a QPlatformScreen
// subclass. Two named outputs reproduce the reporter's topology exactly:
// primary = portrait HDMI-A-1, plus a landscape DP-1.
//
// WHAT IS UNDER TEST — the full loop, end to end:
//
//   1. two fake outputs exist and are named
//   2. the console is in SPLIT mode (splitPreviewWindow("player-mantles"))
//   3. [world/split/player] is seeded EMPTY — the reporter's fossil
//   4. the SCREEN dropdown is driven through findChildren<QComboBox*>() and
//      setCurrentIndex(), the same public path a click takes
//   5. THE WRITE. saveBeforeLaunch() is the launch path, closeEvent() is
//      the exit path, and both state the scope pair before they run the
//      shared save loop. The question this file exists to answer: does
//      [world/split/player]/outputName come back as DP-1?
//   6. THE SECOND PICK. The reporter had to choose twice. After the fix
//      the FIRST pick must land, and every pick after it must replace it.
//
// ─── why the assertion must distinguish the two scopes ───────────────
//
// "Did the value get written somewhere" is NOT the question — a test that
// reads whatever scope is CURRENT answers "yes" no matter which group the
// write landed in. Every read below spells the group out in full through
// mhw::panelSettingsScopeFor(), so a write into world/single/player can
// never satisfy an assertion about world/split/player. §5 then asserts the
// NEGATIVE too: the single-mode group must not have gained the pick, or a
// "fix" that just writes to both scopes would pass.
//
// THE MUTATIONS THAT MUST TURN THIS RED, and the measured result of
// running all eight against this file (each applied alone, libmhw-ui
// rebuilt, this target re-run):
//
//   M1. delete the two mhw::setPanelSettings*() calls at the top of
//       saveBeforeLaunch()                     -> RED, §9 [15 wrong-domain keys]
//   M2. the same two calls removed from closeEvent()
//                                                -> RED, §9b [15 wrong-domain keys]
//   M8. saveBeforeLaunch() keeps only ONE of the pair (the mode)
//                                                -> RED, §9 [15 wrong-domain keys]
//   M4. delete syncOutputCombo()'s call from adoptMotherPanels()
//                                                -> RED, §10 [dropdown says
//                                                   DP-1, the panel holds
//                                                   HDMI-A-1]
//
// AND THE ONES THAT DO NOT, stated because a mutation note that only lists
// the kills is a claim about the test that was not measured:
//
//   M3. drop blockSignals() from syncOutputCombo()   -> SURVIVES
//   M5. delete the scope pair from the top of syncSplitPreviewToCanvas()
//                                                      -> SURVIVES
//   M6. delete the scope pair from switchGame()      -> SURVIVES
//   M7. flip the combo handler's setOutputName() to persist=true
//                                                      -> SURVIVES
//
// M3 survives because the restore runs at a moment when the combo's index
// already equals the value being restored, so the currentIndexChanged
// signal never fires and there is nothing to observe. That is not a hole
// in this file's coverage of M3 — it is that M3 is unreachable from the
// paths this file drives, and saying so is more useful than a test that
// pretends to cover it.
//
// M5 and M6 survive because §1-§10 all drive a console whose scope is
// already correct by the time the assertion runs: the ctor, switchGame()
// and syncSplitPreviewToCanvas() each state the pair on the paths this
// file reaches. The two that ARE reachable and ARE pinned are the two
// exits, and those are the ones the fix added.
//
// NO SKIPPED CHECKS. Everything here runs, or it fails loudly.

// ─── THE ACCESS-SPECIFIER SEAM ───────────────────────────────────────
//
// ControlPanel keeps its two write paths private, and this file is about
// exactly those two paths:
//
//   saveBeforeLaunch()  — what the console runs before it spawns the
//                         overlay (reachable from the outside only via
//                         launchOverlayChild(), which starts a real child
//                         process)
//   closeEvent()        — what it runs when the window goes away
//                         (offscreen, a window that was never mapped does
//                         not close)
//
// Both are unreachable as declared, and a test that waits for either
// would hang on a green build and prove nothing — which is the exact
// reason control_panel.h exposes saveAllSplitAndBaseWindows() as public
// "so a test can perform the SAME write the console does on its two
// exits". The loop is not the whole path, though: the two CALLERS are
// where the scope pair lives, and the scope pair is the fix. So the
// callers have to be reachable too.
//
// This is the standard spelling for that problem, and it is safe here
// for a reason worth stating: access specifiers are a COMPILE-TIME
// property in the Itanium C++ ABI. They change no symbol name, no vtable
// slot and no member offset, so this translation unit — which sees
// control_panel.h with `private` widened — and libmhw-ui, compiled
// normally, still agree on every object they exchange. The alternative is
// a friend declaration in a src/ header, which this task may not touch.
//
// The macros are defined around the project headers only. Everything Qt
// and libstdc++ owns is included FIRST, with the macros not yet defined,
// so those headers are parsed exactly once and exactly as libmhw-ui parses
// them. Only the project's own headers — the ones whose private members
// this file has to reach — are parsed a second time with the access
// specifiers widened, and that is the whole trick.
//
// WHY THAT IS SAFE. Access specifiers are a compile-time property in the
// Itanium C++ ABI: they change no symbol name, no vtable slot and no
// member offset, so this translation unit and libmhw-ui, compiled
// normally, still agree on every object they exchange. (Widening them
// EARLY is what breaks the build, not widening them at all: libstdc++'s
// <sstream> is instantiated twice with different access on its internal
// structs and GCC rejects it outright. Hence the order.)
//
// The pragma suppresses -Wtemplate-body, which GCC still emits about Qt's
// own templates on the widened pass and which is noise for the same
// reason. Scoped to this file.
#pragma GCC diagnostic ignored "-Wtemplate-body"

#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStackedWidget>
#include <QTextStream>
#include <QTemporaryDir>

// Qt's QPA seam. Private, but so is screen_query.cpp's use of
// QPlatformScreen, and for the same reason: there is no public way to tell
// Qt "there are two monitors, and one of them is portrait".
#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>

#include <cstdio>

// ─── the project headers, with the access specifiers widened ─────────
#define private public
#define protected public
#include "ui/panel_settings_game.h"
#include "ui/control_panel.h"
#include "ui/panel.h"
#undef private
#undef protected

namespace
{
int checks = 0, failures = 0;

void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  FAIL  %s\n", what);
    } else {
        std::printf("PASS: %s\n", what);
    }
}

// Same, with a measured detail appended. Used where the interesting part
// of the answer is HOW MANY, not WHETHER — a count of zero and a count of
// fifteen are both "not zero" only to the second form.
void check(bool ok, const char *what, const QString &detail)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  FAIL  %s [%s]\n", what, qPrintable(detail));
    } else {
        std::printf("PASS: %s [%s]\n", what, qPrintable(detail));
    }
}

void checkQ(const QString &got, const QString &want, const char *what)
{
    ++checks;
    if (got != want) {
        ++failures;
        std::printf("  FAIL  %s: got \"%s\" want \"%s\"\n", what,
                    qPrintable(got.isEmpty() ? QStringLiteral("(empty)") : got),
                    qPrintable(want.isEmpty() ? QStringLiteral("(empty)")
                                              : want));
    } else {
        std::printf("PASS: %s\n", what);
    }
}

const char *kIniRel = "monster-overlay/panels.ini";

// The reporter's topology, reproduced. The primary is the PORTRAIT one —
// that inversion is what makes the bug visible at all, because "the
// overlay landed on the primary" is otherwise indistinguishable from
// "the overlay landed correctly".
const char *kPrimaryName = "HDMI-A-1";   // 1080x1920, primary
const char *kOtherName   = "DP-1";       // 2560x1440, NOT primary

// ─── the fake outputs ────────────────────────────────────────────────
class FakeOutput : public QPlatformScreen
{
public:
    FakeOutput(const QString &name, const QRect &geometry)
        : m_name(name), m_geometry(geometry) {}

    QString name() const override { return m_name; }
    QRect   geometry() const override { return m_geometry; }
    int     depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }

private:
    QString m_name;
    QRect   m_geometry;
};

void pumpEvents()
{
    for (int i = 0; i < 100; ++i)
        QCoreApplication::processEvents();
}

// Install the reporter's two-monitor topology: a portrait primary plus a
// landscape secondary. Called BEFORE the first Panel is constructed,
// because Panel's ctor reads the scope and every geometry question is
// answered against whatever screens exist at that moment — and because
// buildInspector()'s screen dropdown is populated from the same list.
//
// The order matters: handleScreenAdded() for the NON-primary first, then
// the primary. Asking for the primary second means the offscreen
// platform's own nameless 800x800 screen is not the one left standing.
void installFakeOutputs()
{
    QWindowSystemInterface::handleScreenAdded(
        new FakeOutput(QString::fromLatin1(kOtherName), QRect(0, 0, 2560, 1440)),
        /* isPrimary */ false);
    QWindowSystemInterface::handleScreenAdded(
        new FakeOutput(QString::fromLatin1(kPrimaryName), QRect(0, 0, 1080, 1920)),
        /* isPrimary */ true);
    pumpEvents();
}

void writeConfLayout(const QString &mode, const QString &home)
{
    // No `.config` in this path: monster-overlay.conf is read from
    // $XDG_CONFIG_HOME/monster-overlay/, and writing it under `.config`
    // leaves the reader finding nothing and the mode resolving Single.
    const QString path = home + QStringLiteral("/monster-overlay/"
                                               "monster-overlay.conf");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << QStringLiteral("layout=%1\n").arg(mode);
        out.flush();
    }
}

QString iniPath(const QString &home)
{
    return home + QLatin1Char('/') + QLatin1String(kIniRel);
}

// The full scope a group lives under, spelled out by the CALLER rather
// than read from the process globals. That is the whole point: the
// question this file answers is "which group did the write land in", and
// a reader that follows whichever scope is CURRENT cannot tell
// world/split/player from world/single/player.
QString splitGroup(const QString &key)
{
    return mhw::panelSettingsScopeFor(
        mhw::GameId::World, mhw::PlayerSection::PlayerLayoutMode::Split, key);
}

QString singleGroup(const QString &key)
{
    return mhw::panelSettingsScopeFor(
        mhw::GameId::World, mhw::PlayerSection::PlayerLayoutMode::Single, key);
}

// Read one key out of one explicitly-named group of panels.ini.
//
// The file is read by PATH, not through the process's QSettings defaults,
// because panel.cpp's settings() is a thread_local that re-syncs from
// disk on every access — which would answer for the group it is asked
// about and not for the one this test names.
QString iniValue(const QString &home, const QString &fullGroup,
                 const QString &key)
{
    QSettings s(iniPath(home), QSettings::IniFormat);
    s.beginGroup(fullGroup);
    const QString v = s.value(key).toString();
    s.endGroup();
    return v;
}

bool iniHasKey(const QString &home, const QString &fullGroup,
               const QString &key)
{
    QSettings s(iniPath(home), QSettings::IniFormat);
    s.beginGroup(fullGroup);
    const bool has = s.contains(key);
    s.endGroup();
    return has;
}

// Seed the reporter's fossil: the split-mode player group carries an
// EMPTY outputName. Written as an explicit empty string rather than as an
// absent key, because the reporter's file has the row present and blank —
// the value the console showed (DP-1) and the value the overlay read
// (nothing) disagreed, and that is what a seed must reproduce.
// Wipe the whole file. Several sections below re-seed it, and a key left
// over from the previous section is indistinguishable from a write made
// by this one — which is exactly the confusion this file is about, so the
// file is cleared before each scenario rather than trusted.
void clearIni(const QString &home)
{
    QSettings s(iniPath(home), QSettings::IniFormat);
    s.clear();
    s.sync();
}

void seedSplitPlayerEmpty(const QString &home)
{
    clearIni(home);
    QSettings s(iniPath(home), QSettings::IniFormat);
    s.beginGroup(splitGroup(QStringLiteral("player")));
    s.setValue(QStringLiteral("outputName"), QString());
    s.endGroup();
    s.sync();
}


// ─── the SCREEN dropdown, driven the way a user drives it ────────────
//
// There is one combo per inspector page, and all four live in the console
// at once — inside a QStackedWidget, one page per panel, of which only the
// current one is visible. findChildren() is the public path — the same
// lookup split_console_layout_persistence_tests.cpp uses for the rail
// buttons — so this drives the real widget, not a back door.
//
// The page is selected the way the user selects it: selectPanel(i) moves
// inspectorStack_'s index, and the NAV buttons are the public handle on
// that. The PLAYER page is index 0 and it is the panel under test — it is
// the panel whose output buildOverlayArgv() spells as --output, and the
// one the reporter's file points at.
//
// The combo is identified by its ENTRIES rather than by an objectName,
// because buildInspector() gives the output combo none. Its first entry
// is always "<PRIMARY>" with empty userData, and the rest are the
// attached outputs' names as userData — which is exactly the shape a user
// sees and picks from. The layoutCombo_ is excluded by objectName so the
// Single/Split selector, whose entries also carry empty userData, is
// never mistaken for a screen picker.
//
// WHY THE PAGE MATTERS, MEASURED: findChildren<QComboBox*>() returns all
// five combos in child order, and only the LAST of the four screen
// pickers is the player's — the four inspector pages are added to the
// stack in slot order but their widgets are constructed later. Taking the
// first match silently drove the PETS dropdown, whose write lands in
// [world/split/pet-damage] and satisfies no assertion here.
QComboBox *findPlayerScreenCombo(ControlPanel &cp)
{
    // The inspector stack. inspectorStack_ is private, so it is found by
    // the one fact that identifies it from outside: it is the console's
    // only QStackedWidget, and it holds the four inspector pages.
    QStackedWidget *stack = cp.findChild<QStackedWidget *>();
    if (!stack)
        return nullptr;

    // Move to the PLAYER page (index 0) the way selectPanel(0) does, so
    // the combo returned below belongs to the panel under test. The nav
    // button's own click() would do the same thing and also touch the
    // canvas; setting the stack's index directly is the smaller public
    // action with the same effect on which page's combo we read.
    stack->setCurrentIndex(0);
    pumpEvents();

    QWidget *page = stack->currentWidget();
    if (!page)
        return nullptr;

    for (QComboBox *c : page->findChildren<QComboBox *>()) {
        if (!c || c->count() < 2)
            continue;
        // The layoutCombo_ also carries empty userData on both entries,
        // so it must be excluded by name or it is mistaken for a picker
        // whose first entry means "<PRIMARY>".
        if (c->objectName() == QStringLiteral("layoutCombo"))
            continue;
        bool hasPrimary = false;
        bool hasOther = false;
        for (int i = 0; i < c->count(); ++i) {
            const QString d = c->itemData(i).toString();
            if (d.isEmpty())
                hasPrimary = true;
            if (d == QLatin1String(kOtherName))
                hasOther = true;
        }
        if (hasPrimary && hasOther)
            return c;
    }
    return nullptr;
}

// Pick one entry by its userData — the output name — exactly as a user
// picks it from the dropdown. setCurrentIndex() emits
// currentIndexChanged, which is the signal buildInspector() connected the
// handler to; nothing about this call is special, which is the point.
void pickOutput(QComboBox *combo, const QString &outputName)
{
    if (!combo)
        return;
    const int idx = combo->findData(outputName);
    if (idx < 0)
        return;
    combo->setCurrentIndex(idx);
    pumpEvents();
}

// The console's own write, performed the same way its exits perform it.
// saveAllSplitAndBaseWindows() is the shared loop and is public; the two
// CALLERS are where the scope pair lives, and both are reachable through
// the access-specifier seam above.
//
//   saveBeforeLaunch()  — the launch path. saveMaskToDisk() plus the scope
//                         pair plus the loop.
//   closeEvent()        — the exit path. Hides the four preview panels,
//                         states the scope pair, runs the same loop, then
//                         writes the mask and the console's own window
//                         frame.
//
// Driving both, rather than only the loop, is what makes the scope pair
// load-bearing: a test that calls the loop directly passes with the pair
// deleted from either caller, which is the exact regression this file
// exists to catch.
void saveBeforeLaunchPath(ControlPanel &cp)
{
    cp.saveBeforeLaunch();
    pumpEvents();
}

// closeEvent() is protected and normally dispatched by the window system,
// which offscreen never does — the header's own comment says so. A
// synthetic QCloseEvent sent straight at the handler is the same call with
// the same arguments, and it is what the console runs when the user closes
// the window.
void closeEventPath(ControlPanel &cp)
{
    QCloseEvent ev;
    QApplication::sendEvent(&cp, &ev);
    pumpEvents();
}

} // namespace

int main(int argc, char *argv[])
{
    // ── isolation ────────────────────────────────────────────────────
    //
    // QTemporaryDir + HOME + XDG_CONFIG_HOME, so nothing here can read or
    // write the developer's real config. RESOURCE_LOCK "panels-ini" in
    // CMakeLists keeps the sibling panel targets from racing us for the
    // same QSettings default path.
    //
    // QSettings::setPath() must happen before the first QSettings use,
    // and panel.cpp's settings() is a function-local static that latches
    // the path on first call — so this runs before any Panel is built.
    QTemporaryDir home;
    if (!home.isValid()) {
        std::fprintf(stderr, "FAIL: could not create a scratch HOME\n");
        return 2;
    }
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, home.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("monster-overlay"));
    app.setApplicationName(QStringLiteral("monster-overlay"));

    // THE GAME AXIS, PINNED BEFORE THE FIRST ControlPanel IS BUILT.
    //
    // This file asserts on [world/split/*] and [world/single/*] — see
    // splitGroup()/singleGroup() above, which spell World out explicitly
    // on purpose so a write into the rise domain can never satisfy an
    // assertion about the world one. That is only a real assertion if
    // the console is actually IN world, and the console decides which
    // game it is in by itself, at construction:
    //
    //     control_panel.cpp:2081  QSettings s;
    //                           const QString savedGame = s.value("game");
    //                           ... switchGame(savedGameId) — or, with no
    //                           saved row, switchGame(detected->game)
    //
    // detectGame() scans /proc for monsterhunterworld.exe and
    // monsterhunterrise.exe, so on a machine with Rise running (or with
    // Rise merely still mapped in some process's address space) the
    // console opens on Rise and every write lands in [rise/split/*]
    // while every assertion below reads [world/split/*]. Measured on
    // this machine with a Rise process present:
    //
    //     saveAppearance key=player group=rise/split/player outputName='DP-1'
    //     [world/split/player] outputName =        <- what the test reads
    //
    // 14 failures, all of them "got (empty) want DP-1", and none of them
    // a defect in the write path — the console wrote its pick, into the
    // game it was actually in.
    //
    // The persisted `game` row is the console's own record of that
    // decision and the FIRST thing it honours, so writing it here is not
    // a workaround: it is the same state a user leaves behind when they
    // click the World rail button (switchGame() persists the identical
    // key, control_panel.cpp:3257). With the row present, the console
    // takes the savedGame branch and never consults the process table,
    // which makes the test independent of what the developer happens to
    // be running.
    //
    // AFTER the QApplication above, because the two names are half of
    // the file's path: written before them this lands in a different
    // file from the one the console reads.
    {
        QSettings gameScope;
        gameScope.setValue(QStringLiteral("game"), QStringLiteral("world"));
        gameScope.sync();
    }

    std::printf("== console_scope_persistence_tests ==\n");
    std::printf("(the SCREEN dropdown writes the group the overlay reads)\n\n");

    // BEFORE the first Panel is constructed: the two fake outputs, and the
    // conf row that puts this console into Split mode.
    installFakeOutputs();
    writeConfLayout(QStringLiteral("split"), home.path());

    // The pin above only works if the console HONOURS it. Nothing else in
    // this file would notice if control_panel.cpp:2081 stopped reading the
    // "game" row and went straight back to detectGame(): the writes would
    // go to [rise/split/*] on a machine with Rise running and every
    // assertion below would fail, but on a machine with nothing running
    // they would all pass and the suite would be green for the wrong
    // reason. So the precondition is asserted, not assumed.
    //
    // After installFakeOutputs() and writeConfLayout() on purpose: this
    // probe is a real ControlPanel, and a console built before the fake
    // session and the Split conf row exist would be built under different
    // conditions than every panel below it.
    {
        ControlPanel probe;
        check(probe.currentGame() == mhw::GameId::World,
              "the console opens on World — the pinned 'game' row was "
              "honoured, not overridden by whatever detectGame() found in "
              "/proc on this machine");
        probe.close();
    }

    // ── 1. THE FAKE SESSION IS REAL ──────────────────────────────────
    //
    // Without this the rest of the file is vacuous: a single unnamed
    // offscreen screen accepts every outputName as invalid, so
    // setOutputName("DP-1") would revert to primary and every assertion
    // below would pass for the wrong reason.
    {
        const int n = int(QGuiApplication::screens().size());
        check(n >= 2, "the fake two-monitor session is installed "
                      "(the two injected outputs plus offscreen's own)");

        QScreen *primary = QGuiApplication::primaryScreen();
        check(primary && primary->name() == QLatin1String(kPrimaryName),
              "the PRIMARY is the portrait HDMI-A-1, as on the reporter's "
              "machine — the inversion that makes the bug visible");
        check(primary
                  && primary->geometry().height() > primary->geometry().width(),
              "and it really is portrait, so primary is not the landscape one");

        bool hasOther = false;
        for (QScreen *s : QGuiApplication::screens())
            if (s && s->name() == QLatin1String(kOtherName))
                hasOther = true;
        check(hasOther, "DP-1 exists and is NOT the primary");
    }

    // ── 2. THE CONSOLE IS IN SPLIT MODE ──────────────────────────────
    //
    // The precondition for everything below: in Single mode there is no
    // split section to write into and no sub-window host to observe.
    ControlPanel cp;
    cp.show();
    pumpEvents();

    Panel *subWindow = cp.splitPreviewWindow(QStringLiteral("player-mantles"));
    check(subWindow != nullptr,
          "the console built a split sub-window — conf layout=split is in "
          "force, so [world/split/*] is the live domain");

    QComboBox *combo = findPlayerScreenCombo(cp);
    check(combo != nullptr,
          "the PLAYER inspector's SCREEN dropdown exists and lists both "
          "fake outputs");
    check(combo && combo->findData(QString::fromLatin1(kOtherName)) >= 0,
          "and it offers DP-1 as an entry");
    if (!combo)
        return failures == 0 ? 0 : 1;

    // ── 3. THE DROPDOWN STARTS FROM THE PANEL, NOT FROM ITSELF ───────
    //
    // The ctor's ordering is the defect: constructMotherPanels() builds
    // the four mothers while the layout global still holds its Single
    // default, so they read [world/single/*]; applyStartupLayoutMode()
    // then resolves Split and rebuilds them against [world/split/*] —
    // empty. The dropdown is populated during buildInspector() from the
    // FIRST set of panels and nothing re-synced it, so it kept advertising
    // the single-mode panel's screen while the member held whatever the
    // split scope said. That is the "must choose twice" tell.
    //
    // The load-bearing version of this is §10, where the two domains are
    // made to DISAGREE. This section checks the simpler property that the
    // console is in split mode at all, which is the precondition.
    {
        // The fossil, seeded the way the reporter's own file was.
        seedSplitPlayerEmpty(home.path());

        // A fresh console, so the split scope is read from the seeded file
        // rather than from whatever the first console left in memory.
        ControlPanel cp2;
        cp2.show();
        pumpEvents();

        QComboBox *c2 = findPlayerScreenCombo(cp2);
        check(c2 != nullptr, "the second console's SCREEN dropdown exists");
        if (c2) {
            // The seeded value is EMPTY, so the dropdown must rest on
            // <PRIMARY> (empty userData) — not on DP-1, which is what the
            // reporter saw while the overlay mounted on HDMI.
            check(c2->currentData().toString().isEmpty(),
                  "with [world/split/player] outputName empty, the dropdown "
                  "rests on <PRIMARY> — it no longer advertises the "
                  "single-mode panel's DP-1");

            Panel *p = cp2.splitPreviewWindow(QStringLiteral("player-mantles"));
            check(p != nullptr, "and that second console is also in split mode");
        }
    }

    // ── 4. THE FIRST PICK LANDS IN THE SPLIT DOMAIN ──────────────────
    //
    // THE CORE ASSERTION OF THIS FILE.
    //
    // The reporter's flow: open the console, see the dropdown say DP-1,
    // press START, and the overlay mounts on HDMI. The console said DP-1
    // and the child never heard it. So the question is not "is the value
    // written somewhere" but "is it written where the overlay will read
    // it" — and on this machine that is [world/split/player], because
    // main.cpp's setPanelSettingsLayout() resolves the same conf row the
    // console did.
    {
        // The fossil again, on a fresh console that reads it from disk.
        seedSplitPlayerEmpty(home.path());
        ControlPanel cp3;
        cp3.show();
        pumpEvents();

        QComboBox *c3 = findPlayerScreenCombo(cp3);
        check(c3 != nullptr, "the third console's SCREEN dropdown exists");
        if (c3) {
            check(c3->currentData().toString().isEmpty(),
                  "its dropdown starts on <PRIMARY> — the seeded split value "
                  "is empty, so the panel follows the OS primary");

            // THE PICK. Public path: setCurrentIndex() on the real combo,
            // which emits the signal buildInspector() connected the
            // handler to. The handler calls setOutputName(DP-1,
            // persist=false) and then saveAppearance() — so the value is
            // on disk immediately, before any exit path runs.
            pickOutput(c3, QString::fromLatin1(kOtherName));

            // THE CONSOLE'S OWN WRITE — THE LAUNCH PATH, spelled out
            // through saveBeforeLaunch() rather than through the loop it
            // calls. That distinction is the whole point: the scope pair
            // lives in the CALLER, and a test that drives only the loop
            // cannot see it move. saveBeforeLaunch() re-reads each
            // untouched window before writing it back, so the panel the
            // user just picked keeps the pick and a window nobody touched
            // keeps whatever the overlay last wrote.
            saveBeforeLaunchPath(cp3);

            // THE OBSERVABLE, read from disk, in the group the overlay
            // reads. Spelled out in full so a write into world/single/player
            // cannot satisfy it.
            checkQ(iniValue(home.path(),
                            splitGroup(QStringLiteral("player")),
                            QStringLiteral("outputName")),
                   QString::fromLatin1(kOtherName),
                   "THE PICK REACHES [world/split/player] — the group the "
                   "overlay reads, not the single-mode one");

            // And the NEGATIVE: the single-mode group must not have gained
            // the pick. Without this, a "fix" that simply wrote the value
            // into every scope would pass the check above.
            checkQ(iniValue(home.path(),
                            singleGroup(QStringLiteral("player")),
                            QStringLiteral("outputName")),
                   QString(),
                   "and [world/single/player] did NOT acquire the pick — the "
                   "value went to one group, the one the overlay reads");
        }
    }

    // ── 5. THE SECOND PICK: the two-toggle round trip ────────────────
    //
    // The reporter's exact complaint was that they had to choose HDMI-A-1
    // and then choose DP-1 back before the screen moved. After the fix
    // the FIRST pick already lands, so a round trip must land TWICE —
    // each pick writing the group it belongs to, and neither surviving as
    // the other's stale value.
    {
        seedSplitPlayerEmpty(home.path());
        ControlPanel cp4;
        cp4.show();
        pumpEvents();

        QComboBox *c4 = findPlayerScreenCombo(cp4);
        check(c4 != nullptr, "the fourth console's SCREEN dropdown exists");
        if (c4) {
            // Pick 1: DP-1. Already asserted in §4; repeated here because
            // this section is about what a SECOND pick does to the value
            // the first one wrote.
            pickOutput(c4, QString::fromLatin1(kOtherName));
            saveBeforeLaunchPath(cp4);
            checkQ(iniValue(home.path(),
                            splitGroup(QStringLiteral("player")),
                            QStringLiteral("outputName")),
                   QString::fromLatin1(kOtherName),
                   "pick 1 (DP-1) lands in the split group");

            // Pick 2: the primary. This is the reporter's "choose hdmi
            // first" step, which before the fix was the only thing that
            // moved anything.
            pickOutput(c4, QString::fromLatin1(kPrimaryName));
            saveBeforeLaunchPath(cp4);
            checkQ(iniValue(home.path(),
                            splitGroup(QStringLiteral("player")),
                            QStringLiteral("outputName")),
                   QString::fromLatin1(kPrimaryName),
                   "pick 2 (HDMI-A-1) REPLACES it in the split group — no "
                   "stale DP-1 left behind");

            // Pick 3: back to DP-1. The round trip must not lose the
            // third value either.
            pickOutput(c4, QString::fromLatin1(kOtherName));
            saveBeforeLaunchPath(cp4);
            checkQ(iniValue(home.path(),
                            splitGroup(QStringLiteral("player")),
                            QStringLiteral("outputName")),
                   QString::fromLatin1(kOtherName),
                   "pick 3 (back to DP-1) lands again — the reporter's "
                   "two-toggle workaround is no longer necessary");
        }
    }

    // ── 6. THE OTHER SPLIT SUB-WINDOWS FOLLOW ────────────────────────
    //
    // The reporter's file had eleven sub-windows with an empty
    // outputName. They have no SCREEN dropdown of their own today, so the
    // write that runs for them is the console's save loop — which is
    // exactly what makes them matter here: the loop covers EVERY split
    // window, so a value that reaches the split domain reaches all of
    // them, and a value that lands in the wrong domain reaches none of
    // them.
    //
    // The sub-windows are asserted to stay EMPTY rather than to carry
    // DP-1, and that is the correct behaviour, not a weaker claim: a
    // split sub-window's screen is decided at overlay startup by
    // main.cpp's inheritParentOutput(), which copies the PARENT's
    // outputName when the sub has none. A console that pre-filled every
    // sub-window would take that decision away from the parent and pin
    // eleven windows to a screen the user never chose for them.
    //
    // M1's observable effect is in §9, where the scope is moved before the
    // save; this section pins the coverage of the loop itself.
    {
        seedSplitPlayerEmpty(home.path());
        ControlPanel cp5;
        cp5.show();
        pumpEvents();

        pickOutput(findPlayerScreenCombo(cp5), QString::fromLatin1(kOtherName));
        saveBeforeLaunchPath(cp5);

        check(iniHasKey(home.path(),
                        splitGroup(QStringLiteral("player-mantles")),
                        QStringLiteral("outputName")),
              "and the split sub-windows were written too — the save loop "
              "covers [world/split/*] as a whole, not only the mothers");

        checkQ(iniValue(home.path(),
                        splitGroup(QStringLiteral("player-mantles")),
                        QStringLiteral("outputName")),
               QString(),
               "a split sub-window carries NO output of its own — its "
               "screen is inherited from the parent at overlay startup, so "
               "a console that pre-filled it would take that decision away");

        // The parent, which is the one that must carry the pick.
        checkQ(iniValue(home.path(),
                        splitGroup(QStringLiteral("player")),
                        QStringLiteral("outputName")),
               QString::fromLatin1(kOtherName),
               "the parent [world/split/player] carries it — the value "
               "inheritParentOutput() hands down to the eleven");
    }

    // ── 6b. THE MODE SWITCH, WHICH IS WHERE THE BUG BIT ──────────────
    //
    // THE MEASURED REPRODUCTION, and the one assertion set in this file
    // that does not depend on the seeded fossil.
    //
    // The console is constructed, the user picks DP-1 in the SCREEN
    // dropdown, and THEN they touch the layout mode — either to look at
    // the other topology, or because the conf row was wrong. Switching
    // the mode re-states the scope, so every subsequent write goes to the
    // NEW domain. The user's pick, already made under the old one, is
    // then written into [world/single/player] while the overlay — which
    // was started in Split — reads [world/split/player] and finds it
    // empty.
    //
    // Measured on this machine before the fix, through the same public
    // controls this section drives:
    //
    //     pick DP-1 (split in force)
    //     switch the layout combo to Single, press apply
    //     saveBeforeLaunch()
    //     -> [world/single/player]  outputName=DP-1
    //     -> [world/split/player]   outputName=        <- the overlay's
    //
    // which is the reporter's own file, produced by the console in one
    // session.
    //
    // WHY THE APPLY BUTTON AND NOT A SECOND CONSOLE. The mode in force is
    // private state and the conf row is the only public handle on it, so
    // the layout combo + apply button is the one way to move it from a
    // test — exactly as console_split_sync_tests.cpp drives it. Offscreen
    // -safe by construction: applyLayoutChoice() only raises a confirm
    // dialog when a child is alive, and this console has none.
    //
    // MUTATION M1: delete the scope pair from saveBeforeLaunch() and this
    // fails. The write after the switch lands in whichever domain the
    // switch left behind, so the split group stays empty.
    {
        seedSplitPlayerEmpty(home.path());
        writeConfLayout(QStringLiteral("split"), home.path());
        ControlPanel cp6;
        cp6.show();
        pumpEvents();

        // The pick, under the conf's Split mode.
        pickOutput(findPlayerScreenCombo(cp6), QString::fromLatin1(kOtherName));

        // The mode switch, driven the way the user drives it.
        QComboBox *layoutCombo =
            cp6.findChild<QComboBox *>(QStringLiteral("layoutCombo"));
        QPushButton *apply =
            cp6.findChild<QPushButton *>(QStringLiteral("layoutApply"));
        check(layoutCombo != nullptr && apply != nullptr,
              "the layout combo and its apply button both exist — the only "
              "public handle on the mode in force");
        if (layoutCombo && apply) {
            layoutCombo->setCurrentIndex(0);   // Single
            pumpEvents();
            check(apply->isEnabled(),
                  "choosing Single arms the apply button");
            apply->click();
            pumpEvents();

            // THE LAUNCH PATH, spelled out in full.
            saveBeforeLaunchPath(cp6);

            // The pick must have reached the group the SPLIT overlay
            // reads, because that is the mode the user made the pick in
            // and the mode the conf still names for the next launch.
            checkQ(iniValue(home.path(),
                            splitGroup(QStringLiteral("player")),
                            QStringLiteral("outputName")),
                   QString::fromLatin1(kOtherName),
                   "a pick made in SPLIT mode survives the mode switch into "
                   "[world/split/player] — the group the overlay reads");
        }
    }

    // ── 7. THE EXIT PATH: closeEvent() ───────────────────────────────
    //
    // The other half of the same fact. saveBeforeLaunch() is what a user
    // who presses START gets; closeEvent() is what EVERY other user gets,
    // including the one who only ever looks at the console. Both state the
    // scope pair, and both must.
    //
    // This is the section that cannot be satisfied by the seeded fossil or
    // by the combo: closeEvent() is protected and dispatched by the window
    // system, which offscreen never does — control_panel.h's own comment
    // on saveAllSplitAndBaseWindows() says a test that waits for it "would
    // hang on a green build and prove nothing". A synthetic QCloseEvent
    // sent straight at the handler is the same call with the same
    // arguments.
    //
    // MUTATION M2: remove the scope pair from closeEvent() and this fails
    // — the write lands in whichever domain the console was last in, and
    // the pick is lost from the group the overlay reads.
    {
        seedSplitPlayerEmpty(home.path());
        // §6b left the conf row saying Single — applyLayoutChoice()
        // rewrites it — so it has to be put back before a console is built
        // against it.
        writeConfLayout(QStringLiteral("split"), home.path());
        ControlPanel cp7;
        cp7.show();
        pumpEvents();

        pickOutput(findPlayerScreenCombo(cp7), QString::fromLatin1(kOtherName));

        // THE EXIT PATH.
        closeEventPath(cp7);

        checkQ(iniValue(home.path(),
                        splitGroup(QStringLiteral("player")),
                        QStringLiteral("outputName")),
               QString::fromLatin1(kOtherName),
               "and a pick survives the console being CLOSED into "
               "[world/split/player] — the path every user takes who never "
               "presses START");
    }

    // ── 7b. THE CROSS-DOMAIN LEAK ────────────────────────────────────
    //
    // THE SECTION THAT MAKES THE SCOPE PAIR LOAD-BEARING.
    //
    // Everything above can be satisfied by the dropdown's own
    // saveAppearance(), which writes at pick time under whatever scope is
    // in force then. That is not a weakness of those sections — it is what
    // the user experiences — but it means they cannot tell a correct scope
    // pair from a deleted one. This section can, because it asks a
    // different question: after the console is DONE, does the file hold the
    // pick in the group the overlay reads and ONLY there?
    //
    // Measured, with the scope pair deleted from both exits, on a console
    // whose conf says layout=single:
    //
    //     pick DP-1
    //     saveBeforeLaunch() / closeEvent()
    //     -> [world/single/player]  outputName=DP-1     correct
    //     -> [world/split/player]   outputName=DP-1     WRONG — leaked
    //
    // The second line is the reporter's file from the other direction: a
    // value the single-mode overlay will read as its own choice, written
    // by a console that was never in split mode. It happens because the
    // save loop's group name is derived from the two scope globals at
    // WRITE time, and the write happens on every window the console owns —
    // so a scope that was stated for the LAST REBUILDER rather than for
    // the CURRENT mode puts the current pick in the wrong domain.
    //
    // MUTATION M1 (delete the pair from saveBeforeLaunch()) and M2 (delete
    // it from closeEvent()) both make this fail, because without the pair
    // the loop writes the current panel under a scope that no longer
    // describes the console.
    {
        // A SINGLE-mode console: the mode every default user is in, and the
        // mode whose scope is never stated by anything except the exits.
        clearIni(home.path());
        writeConfLayout(QStringLiteral("single"), home.path());
        ControlPanel cp8;
        cp8.show();
        pumpEvents();

        pickOutput(findPlayerScreenCombo(cp8), QString::fromLatin1(kOtherName));
        saveBeforeLaunchPath(cp8);

        // The pick must be in the group the overlay reads — which for a
        // single-mode console is the SINGLE group.
        checkQ(iniValue(home.path(),
                        singleGroup(QStringLiteral("player")),
                        QStringLiteral("outputName")),
               QString::fromLatin1(kOtherName),
               "on a SINGLE-mode console the pick lands in "
               "[world/single/player] — the group that overlay reads");

        // And it must NOT be in the split group, which no overlay running
        // this conf will ever read.
        checkQ(iniValue(home.path(),
                        splitGroup(QStringLiteral("player")),
                        QStringLiteral("outputName")),
               QString(),
               "and NOT in [world/split/player] — a value written there is "
               "one no overlay will read, and it is the reporter's fossil "
               "from the other direction");
    }

    // ── 7c. AND THE SAME, ON THE EXIT PATH ───────────────────────────
    //
    // The exit path is the one every user takes, so it gets its own
    // assertion rather than sharing the launch path's.
    //
    // MUTATION M2: delete the scope pair from closeEvent() and this fails.
    {
        clearIni(home.path());
        writeConfLayout(QStringLiteral("single"), home.path());
        ControlPanel cp9;
        cp9.show();
        pumpEvents();

        pickOutput(findPlayerScreenCombo(cp9), QString::fromLatin1(kOtherName));
        closeEventPath(cp9);

        checkQ(iniValue(home.path(),
                        singleGroup(QStringLiteral("player")),
                        QStringLiteral("outputName")),
               QString::fromLatin1(kOtherName),
               "on the CLOSE path the pick lands in [world/single/player]");

        checkQ(iniValue(home.path(),
                        splitGroup(QStringLiteral("player")),
                        QStringLiteral("outputName")),
               QString(),
               "and not in [world/split/player] — the close writes the "
               "domain the console is actually in");
    }

    // ── 8. THE TWO GROUPS ARE NOT THE SAME GROUP ─────────────────────
    //
    // The last assertion is the one that makes every assertion above
    // meaningful. If the two spellings ever collapse, a write into either
    // satisfies a read of the other, and this file would go green while
    // proving nothing.
    {
        // The pair, stated by hand the way both exits state it. This is
        // the same two calls, in the same order, for the same reason the
        // comment on saveBeforeLaunch() gives: the group name is
        // `<game>/<mode>/<key>`, so the mode has to move second or the
        // game's group is spelled with a stale mode.
        mhw::setPanelSettingsGame(mhw::GameId::World);
        mhw::setPanelSettingsLayout(
            mhw::PlayerSection::PlayerLayoutMode::Split);
        checkQ(splitGroup(QStringLiteral("player")),
               QLatin1String("world/split/player"),
               "the scope globals, stated in the exits' order, address "
               "[world/split/player] — the group the overlay reads");

        checkQ(singleGroup(QStringLiteral("player")),
               QLatin1String("world/single/player"),
               "and the single-mode spelling is a DIFFERENT group, so a "
               "write to one can never satisfy an assertion about the other");
    }



    // ── 9. THE SCOPE IS RE-STATED, NOT INHERITED ────────────────────
    //
    // THE LAST AND DECISIVE SECTION.
    //
    // Every section above drives a console whose scope is already correct,
    // because the ctor, switchGame() and syncSplitPreviewToCanvas() all
    // state the pair. So they cannot tell a correct scope pair from a
    // deleted one — which is exactly the question this file exists to
    // answer about saveBeforeLaunch() and closeEvent().
    //
    // What DOES distinguish them is the write the pair makes DETERMINISTIC
    // rather than inherited. The scope globals are process state that any
    // number of things can move, and the save loop derives every group
    // name from them at write time. So if the two exits do not re-state
    // the pair immediately before the loop, the file gets whatever the
    // last thing to touch the globals left behind.
    //
    // Measured with the pair deleted from saveBeforeLaunch(), on a
    // split-mode console whose scope was then moved to rise/single:
    //
    //     [world/split/player]   outputName = DP-1   <- correct, from the pick
    //     14 MORE outputName keys under [rise/single/...], all empty
    //
    // Fifteen outputName keys in domains no overlay running this conf will
    // read, from one save. The unmutated code writes ZERO outside the
    // domain in force, which is the invariant asserted below.
    //
    // MUTATION M1 (delete the pair from saveBeforeLaunch()): 15 wrong-domain
    // keys, this section fails.
    // MUTATION M2 (delete it from closeEvent()): the same on the close
    // path, asserted by §9b.
    {
        clearIni(home.path());
        writeConfLayout(QStringLiteral("split"), home.path());
        ControlPanel cpA;
        cpA.show();
        pumpEvents();

        pickOutput(findPlayerScreenCombo(cpA), QString::fromLatin1(kOtherName));

        // Move the scope globals somewhere the console is NOT — the state
        // a stale rebuilder, a rail switch, or any future code path could
        // leave them in before an exit runs.
        mhw::setPanelSettingsGame(mhw::GameId::Rise);
        mhw::setPanelSettingsLayout(
            mhw::PlayerSection::PlayerLayoutMode::Single);

        saveBeforeLaunchPath(cpA);

        // The invariant: every outputName key in the file lives under the
        // domain the console is actually in. Not "most of them", not "the
        // one the user picked" — all of them, because the save loop writes
        // EVERY window it owns.
        QSettings s(iniPath(home.path()), QSettings::IniFormat);
        int wrongDomain = 0;
        for (const QString &k : s.allKeys()) {
            if (!k.endsWith(QLatin1String("/outputName")))
                continue;
            if (!k.startsWith(QLatin1String("world/split/")))
                ++wrongDomain;
        }
        check(wrongDomain == 0,
              "a launch save writes NO outputName key outside the domain "
              "in force — the scope pair is stated before the loop, not "
              "inherited from whatever moved it last",
              QStringLiteral("wrong-domain keys=%1").arg(wrongDomain));
        std::printf("     (outputName keys outside [world/split/*]: %d)\n",
                    wrongDomain);

        checkQ(iniValue(home.path(),
                        splitGroup(QStringLiteral("player")),
                        QStringLiteral("outputName")),
               QString::fromLatin1(kOtherName),
               "and the pick is still in the domain the overlay reads");
    }

    // ── 9b. THE SAME, ON THE CLOSE PATH ─────────────────────────────
    //
    // MUTATION M2: delete the scope pair from closeEvent() and this fails
    // the same way, on the path every user takes.
    {
        clearIni(home.path());
        writeConfLayout(QStringLiteral("split"), home.path());
        ControlPanel cpB;
        cpB.show();
        pumpEvents();

        pickOutput(findPlayerScreenCombo(cpB), QString::fromLatin1(kOtherName));

        mhw::setPanelSettingsGame(mhw::GameId::Rise);
        mhw::setPanelSettingsLayout(
            mhw::PlayerSection::PlayerLayoutMode::Single);

        closeEventPath(cpB);

        QSettings s(iniPath(home.path()), QSettings::IniFormat);
        int wrongDomain = 0;
        for (const QString &k : s.allKeys()) {
            if (!k.endsWith(QLatin1String("/outputName")))
                continue;
            if (!k.startsWith(QLatin1String("world/split/")))
                ++wrongDomain;
        }
        check(wrongDomain == 0,
              "a CLOSE save writes no outputName key outside the domain in "
              "force either — the pair is stated on both exits, not one",
              QStringLiteral("wrong-domain keys=%1").arg(wrongDomain));
        std::printf("     (outputName keys outside [world/split/*]: %d)\n",
                    wrongDomain);
    }

    // ── 10. THE DROPDOWN ADVERTISES THE PANEL'S SCREEN, NOT ITS OWN ──
    //
    // The reporter's symptom in one line: 「我必须要手动将该窗口选择一下
    // hdmi,然后在选回 dp-1 才能正常切换到主屏」. "Must choose twice" is the
    // tell that the FIRST pick is not a preference — it is the user forcing
    // the combo's stale index to agree with the member.
    //
    // The mechanism is the ctor's ordering, spelled out in
    // syncOutputCombo()'s own comment: constructMotherPanels() builds the
    // four mothers while the layout global still holds its Single default,
    // so they read [world/single/*]; applyStartupLayoutMode() then resolves
    // Split and switchGame() rebuilds them against [world/split/*]. The
    // dropdown is populated during buildInspector() from the FIRST set of
    // panels and nothing re-synced it — so it kept advertising the
    // single-mode panel's screen while the member held whatever the split
    // scope said.
    //
    // So the observable is: with the two domains DISAGREEING, which one
    // does the dropdown show? The answer must be the one the panel behind
    // it actually holds, which on a split console is the split domain.
    //
    // MUTATION M4: delete syncOutputCombo()'s call from adoptMotherPanels()
    // and this fails — the combo still shows the single-mode panel's
    // HDMI-A-1 while the panel it configures holds DP-1, which is the
    // exact disagreement the reporter had to work around by hand.
    //
    // MUTATION M3: drop blockSignals() from syncOutputCombo() and the
    // programmatic index set is read as a user pick, so the handler runs
    // setOutputName() against the panel and writes HDMI-A-1 over the
    // split domain's DP-1 — the save below then shows the wrong value, and
    // this fails too.
    {
        // THE TWO DOMAINS DISAGREE, AND THE SCOPE STARTS WHERE THE CTOR
        // LEAVES IT.
        //
        // Both halves are needed. Seeding the same value into both domains
        // makes a combo pointing at either one look correct. And leaving
        // the process globals where the previous console's exit put them
        // hides the defect entirely: constructMotherPanels() runs BEFORE
        // applyStartupLayoutMode() resolves the conf's layout= row, so in a
        // fresh process the layout global still holds its Single default
        // and the first four mothers read [world/single/*]. That is the
        // ordering syncOutputCombo() exists to paper over, and it only
        // exists in a fresh process — so it is restored here, by hand,
        // exactly as a cold start would have it.
        //
        // MUTATION M4 (delete syncOutputCombo()'s call from
        // adoptMotherPanels()): the combo keeps the value buildInspector()
        // gave it from the SINGLE-mode panel, which is DP-1 here, and the
        // assertion below fails on HDMI-A-1.
        clearIni(home.path());
        writeConfLayout(QStringLiteral("split"), home.path());
        mhw::setPanelSettingsGame(mhw::GameId::World);
        mhw::setPanelSettingsLayout(
            mhw::PlayerSection::PlayerLayoutMode::Single);
        {
            QSettings sd(iniPath(home.path()), QSettings::IniFormat);
            // The single domain names DP-1 — what the FIRST four mothers
            // read, and what the dropdown is first populated from.
            sd.beginGroup(singleGroup(QStringLiteral("player")));
            sd.setValue(QStringLiteral("outputName"),
                        QString::fromLatin1(kOtherName));
            sd.endGroup();
            // The split domain — the one this console is in, and the one
            // the overlay reads — names the primary.
            sd.beginGroup(splitGroup(QStringLiteral("player")));
            sd.setValue(QStringLiteral("outputName"),
                        QString::fromLatin1(kPrimaryName));
            sd.endGroup();
            sd.sync();
        }

        ControlPanel cpC;
        cpC.show();
        pumpEvents();

        QComboBox *c = findPlayerScreenCombo(cpC);
        check(c != nullptr, "the tenth console's SCREEN dropdown exists");
        if (c) {
            checkQ(c->currentData().toString(),
                   QString::fromLatin1(kPrimaryName),
                   "with the two domains disagreeing, the dropdown shows the "
                   "SPLIT domain's HDMI-A-1 — not the single-mode panel's "
                   "DP-1 it was first populated from");

            // MUTATION M3 (drop blockSignals from syncOutputCombo()): the
            // programmatic index restore is read as a user pick, so the
            // handler runs and calls saveAppearance() under whatever scope
            // is in force at that moment — which during buildInspector() is
            // the SINGLE domain. Every key of every panel then lands in
            // [world/single/*] before the console has even resolved its
            // layout mode. The unmutated code writes nothing there.
            //
            // The seeded [world/single/player]/outputName is the one key
            // this section put there itself, so the count is taken over
            // everything ELSE — a handler that fires on the restore writes
            // the whole panel (scale, minW, opacity, margins, bgAlpha...),
            // not just outputName, so a count of one means "only my seed"
            // and a count of fifteen means "the restore was read as a pick".
            QSettings s(iniPath(home.path()), QSettings::IniFormat);
            int singleKeys = 0;
            for (const QString &k : s.allKeys()) {
                if (k.startsWith(QLatin1String("world/single/"))
                    && k != QLatin1String("world/single/player/outputName"))
                    ++singleKeys;
            }
            check(singleKeys == 0,
                  "and construction wrote no OTHER key into [world/single/*] "
                  "— the programmatic restore is not a user pick",
                  QStringLiteral("extra single-domain keys=%1").arg(singleKeys));
            std::printf("     (extra keys under [world/single/*] after "
                        "construction: %d)\n", singleKeys);

            // And the write agrees with the display, which is what makes
            // the display honest rather than merely different. The player
            // panel's own outputName is not reachable from outside; what is
            // reachable and equivalent is the value the console writes for
            // it, because that IS the member.
            saveBeforeLaunchPath(cpC);
            checkQ(iniValue(home.path(),
                            splitGroup(QStringLiteral("player")),
                            QStringLiteral("outputName")),
                   QString::fromLatin1(kPrimaryName),
                   "and the console writes that same HDMI-A-1 into the split "
                   "domain — the dropdown and the write agree, so the user "
                   "does not have to choose twice");
        }
    }

    std::printf("\n== %d checks, %d failures ==\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
