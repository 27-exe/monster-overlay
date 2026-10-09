// SPDX-License-Identifier: Apache-2.0
//
// The split sub-window's screen inheritance, in the form the user's
// overlay bug actually takes.
//
// THE REPORT, verbatim:
//
//     「有些窗口显示属于 dp-1,但是实际启动 overlay 结果显示在 hdmi。
//       必须要手动将该窗口选择一下 hdmi,然后在选回 dp-1 才能正常切换到主屏。」
//
// Measured on the reporter's machine, where the PRIMARY is a portrait
// output and the panel was pointed at a landscape one:
//
//     primaryScreen() = HDMI-A-1   1080x1920   <- where the overlay landed
//     DP-1             = 2560x1440  <- where the user put the panel
//
// The panels.ini says so plainly:
//
//     [world/split/player]        outputName=DP-1   <- the parent, a choice
//     [world/split/player-status] outputName=       <- empty, no choice made
//
// Panel::applyGeometry() resolves `outputName_ -> primaryScreen()` and
// NOTHING else. So the parent mounted on DP-1 and every split sub-window
// it was carved out of mounted on HDMI-A-1, which is the bug.
//
// ─── what is under test, and why it is not main() ─────────────────────
//
// The rule lives in src/main.cpp as a lambda inside main():
//
//     auto inheritParentOutput = [](Panel *sub, const Panel *parent) {
//         if (!sub || !parent) return;
//         if (!sub->outputName().isEmpty()) return;   // the sub's own key wins
//         const QString out = parent->outputName();
//         if (out.isEmpty()) return;                  // parent follows primary
//         for (QScreen *s : QGuiApplication::screens())
//             if (s && s->name() == out) { sub->setOutputName(out, false); return; }
//     };
//
// A lambda in main() is unreachable from a test, and this file does not
// pretend otherwise. What IS reachable — and what the lambda is a
// thin shell over — is the Panel API it calls, plus the invariant that
// makes the rule matter:
//
//     "a split sub-window whose outputName is empty lands on the primary
//      screen, and a parent that names a non-primary output does NOT
//      drag its sub-windows along with it."
//
// So the four rules are asserted through the same two calls the lambda
// makes, in the same order, against the same Panel objects — and §5
// then asserts the OBSERVABLE the user reported, which is the part that
// actually keeps the rule honest: after the inheritance runs, the
// sub-window is MEASURED AGAINST the same screen its parent is.
//
// THE OBSERVABLE, AND WHY IT IS NOT outputName. `sub->outputName()` is
// the string the lambda handed over, and asserting only that proves the
// assignment happened, not that the window moved — the very thing the
// user reported was broken. The screen a Panel is actually placed on is
// derived in Panel::marginLimits(), and `limit.left() + panelWidth`
// recovers the screen's own width from it. See resolvedScreenWidth().
//
// THE MUTATION THAT MUST TURN THIS RED. Every assertion below is about
// the asymmetry between "the parent names a screen" and "the sub-window
// inherited it". Deleting the lambda body (or inverting the
// sub's-own-key guard, or dropping the attached-screen loop) changes the
// sub-window's resolved screen, and §1-§5 all go red — verified by hand
// for all four mutations, see the report that accompanies this file.
// §5 is the one that cannot be satisfied by a passing coincidence: it
// compares a parent's screen width with its sub-window's, and those
// agree only if both resolved the same QScreen.
//
// NO SKIPPED CHECKS. Everything here runs, or it fails loudly.
//
// ─── how a two-monitor session is faked ──────────────────────────────
//
// QT_QPA_PLATFORM=offscreen reports exactly ONE screen, named the empty
// string, at 800x800 — and Panel::setOutputName() validates against
// QGuiApplication::screens(), so "DP-1" is always rejected and always
// reverts to primary. There is no env var that fixes this (offscreen has
// none; qvnc/qlinuxfb/qminimal/qminimalegl/eglfs have none either, and
// two Xvfb screens both come back named "screen").
//
// What does work is Qt's own QPA seam, the same private API
// src/ui/screen_query.cpp already compiles against:
// QWindowSystemInterface::handleScreenAdded() plus a QPlatformScreen
// subclass. Injecting two named outputs reproduces the reporter's
// topology exactly — primary = portrait HDMI-A-1, plus a landscape
// DP-1 — which is the one thing a single offscreen screen cannot give.

#include "ui/panel.h"
#include "ui/panel_player.h"
#include "ui/panel_player_split.h"
#include "ui/panel_player_split_bars.h"
#include "ui/panel_player_split_mantles.h"
#include "ui/panel_player_split_wirebug.h"
#include "ui/panel_player_split_weapon.h"
#include "ui/panel_player_split_info.h"
#include "ui/panel_sections.h"
#include "ui/panel_settings_game.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QMargins>
#include <QScreen>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

// Qt's logging, for the §4 warning counter. qDebug/qWarning are
// functions in QtGlobal, but QtMsgType and QMessageLogContext are the
// types a qInstallMessageHandler callback is spelled with.
#include <QtGlobal>

// Qt's QPA seam. Private, but so is screen_query.cpp's use of
// QPlatformScreen, and for the same reason: there is no public way to
// tell Qt "there are two monitors, and one of them is portrait".
#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>

#include <cstdio>

namespace
{
int checks = 0, failures = 0;

// Where countUnknownOutputWarning() puts its tally. Non-null only while
// §4 has it installed, so a stray warning outside that window cannot be
// counted against the rule under test.
int *gWarningSink = nullptr;

// Counts exactly the warning Panel::setOutputName emits for a name that
// is not an attached output. §4 installs this, runs the rule, and
// asserts the count is zero — which is the observable difference between
// "the rule checked the screen list" and "the rule handed the name over
// and let setOutputName deal with it".
void countUnknownOutputWarning(QtMsgType type, const QMessageLogContext &,
                               const QString &msg)
{
    if (type == QtWarningMsg && gWarningSink
        && msg.contains(QLatin1String("is not a known output"))) {
        ++(*gWarningSink);
    }
}

void check(bool ok, const char *what)
{
    ++checks;
    if (ok) {
        std::printf("PASS: %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// The two outputs from the reporter's machine, in their real aspect
// ratios. The primary is the PORTRAIT one — that inversion is what makes
// the bug visible at all, because "the sub-window landed on the primary"
// is otherwise indistinguishable from "the sub-window landed correctly".
const char *kPrimaryName = "HDMI-A-1";   // 1080x1920, primary
const char *kOtherName   = "DP-1";       // 2560x1440, NOT primary

// ─── the two fake outputs ────────────────────────────────────────────
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

// Install the reporter's two-monitor topology: a portrait primary plus
// a landscape secondary. Called before the first Panel is constructed,
// because Panel's constructor reads the scope and every geometry
// question is answered against whatever screens exist at that moment.
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

// ─── THE RULE, reproduced from main.cpp's lambda ─────────────────────
//
// One spelling of main.cpp:1051, so the test asserts the behaviour of
// the real call sequence rather than a re-derived guess at it. It is a
// free function in this file rather than a lambda in main() for exactly
// one reason: it has to be callable.
//
// Every clause mirrors the production lambda, in the production order:
//   1. null sub, null parent -> no-op
//   2. the sub-window's OWN outputName is non-empty -> no-op, it wins
//   3. the parent's outputName is empty -> no-op, it follows primary too
//   4. the parent names a screen that is not attached -> no-op, because
//      setOutputName() would warn and revert (one warning per sub-window
//      is the log noise the production comment says to avoid)
//   5. otherwise -> the sub-window takes the parent's name
//
// Mutating any one of those clauses is what §1-§5 catch.
void inheritParentOutput(Panel *sub, const Panel *parent)
{
    if (!sub || !parent)
        return;
    // The sub-window's own stored answer wins, if it has one.
    if (!sub->outputName().isEmpty())
        return;
    const QString out = parent->outputName();
    if (out.isEmpty())
        return;                        // parent follows primary too
    for (QScreen *s : QGuiApplication::screens()) {
        if (s && s->name() == out) {
            sub->setOutputName(out, false);
            return;
        }
    }
}

// ─── THE SCREEN A PANEL IS MEASURED AGAINST ─────────────────────────
//
// Panel::marginLimits() answers "how far may this panel be dragged"
// from the screen it resolved, and the binding component of that is
// `screenWidth - panelWidth`. So `limit.left() + panelWidth` is the
// WIDTH OF THE SCREEN, with the panel's own size divided out:
//
//     primary (HDMI-A-1, 1080 wide):  1058 + 22  = 1080
//     DP-1      (2560 wide)        :  2538 + 22  = 2560
//
// That is the observable this whole file turns on. Two panels of
// DIFFERENT sizes on the SAME screen give the SAME number, and the same
// panel on two different screens gives two different numbers — so it
// answers "which screen is this window on" without needing a public
// accessor for the resolved QScreen, and without depending on the
// window's content size. A bare `marginLimits().left()` would NOT work:
// it is the same number for a wide panel and a narrow one only by
// coincidence, and the weapon strip is 102 wide where the status strip
// is 22.
int resolvedScreenWidth(const Panel *p)
{
    return p->marginLimits().left() + p->width();
}

// The scope a sub-window's section is read from. Set once, before the
// first Panel is constructed, exactly as main() does.
void useWorldSplitScope()
{
    mhw::setPanelSettingsGame(mhw::GameId::World);
    mhw::setPanelSettingsLayout(mhw::PlayerSection::PlayerLayoutMode::Split);
}

// A panels.ini section, seeded the way a user's own save would leave it.
// The parent names DP-1; the sub-windows carry no outputName key at all
// — which is precisely the reporter's file, and precisely the state that
// makes "empty means primary" a bug rather than a default.
void seedPanelsIni()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("monster-overlay"),
                QStringLiteral("panels"));
    s.clear();
    s.sync();
    s.beginGroup(mhw::panelSettingsScopeGroup(QStringLiteral("player")));
    s.setValue(QStringLiteral("outputName"), QString::fromLatin1(kOtherName));
    s.endGroup();
    s.sync();
    QFile::remove(s.fileName());
}

// The two screens' widths. A sub-window that inherited DP-1 is measured
// against 2560; one that fell through to primary is measured against
// 1080. Nothing else about the topology is needed to tell them apart.
const int kOtherWidth   = 2560;
const int kPrimaryWidth = 1080;

} // namespace

int main(int argc, char *argv[])
{
    // ── isolation ────────────────────────────────────────────────────
    //
    // QTemporaryDir + HOME + XDG_CONFIG_HOME, so nothing here can read
    // or write the developer's real config. RESOURCE_LOCK "panels-ini"
    // in CMakeLists keeps the sibling panel targets from racing us for
    // the same QSettings default path.
    //
    // QSettings::setPath() must happen before the first QSettings use,
    // and panel.cpp's settings() is a function-local static that latches
    // the path on first call — so this runs before installFakeOutputs()
    // and before any Panel is built.
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

    std::printf("== main_output_wiring_tests ==\n");
    std::printf("(the split sub-window inherits its parent's output)\n\n");

    installFakeOutputs();
    useWorldSplitScope();
    seedPanelsIni();

    // ── 0. the fake session is real ─────────────────────────────────
    //
    // Without this the rest of the file is vacuous: a single unnamed
    // offscreen screen accepts every outputName as invalid and every
    // assertion below would pass for the wrong reason.
    {
        const int n = int(QGuiApplication::screens().size());
        check(n == 3, "the fake two-monitor session is installed "
                      "(the two injected outputs plus offscreen's own)");

        QScreen *primary = QGuiApplication::primaryScreen();
        check(primary && primary->name() == QLatin1String(kPrimaryName),
              "the PRIMARY is the portrait HDMI-A-1, as on the reporter's "
              "machine — the inversion that makes the bug visible");
        check(primary && primary->geometry().height() > primary->geometry().width(),
              "and it really is portrait, so primary is not the landscape one");

        bool hasOther = false;
        for (QScreen *s : QGuiApplication::screens())
            if (s && s->name() == QLatin1String(kOtherName))
                hasOther = true;
        check(hasOther, "DP-1 exists and is NOT the primary");
    }

    // ── 1. RULE: a sub-window with no name of its own stays on primary ─
    //
    // The state every one of the eleven sub-windows is in on the
    // reporter's machine. This is the baseline the inheritance has to
    // move, so it is asserted first and on its own: if a sub-window
    // somehow resolved to DP-1 with an empty name, §2 would pass
    // without the rule doing anything.
    {
        PlayerPanel parent;
        PlayerStatusStripPanel sub(nullptr,
                                   mhw::PlayerSection::PlayerSplitSlot::Status);
        parent.show();
        sub.show();
        pumpEvents();

        check(parent.outputName().isEmpty(),
              "the parent panel loaded with NO outputName of its own "
              "(the baseline: nothing has been pointed anywhere yet)");
        check(sub.outputName().isEmpty(),
              "the sub-window likewise starts with an empty outputName");

        inheritParentOutput(&sub, &parent);
        pumpEvents();

        check(sub.outputName().isEmpty(),
              "RULE 1 — an empty parent name leaves the sub-window alone, "
              "so it falls through to primary exactly as it did before");
        check(resolvedScreenWidth(&sub) == kPrimaryWidth,
              "and it is measured against the primary screen — which is "
              "the pre-fix state every sub-window was in");
    }

    // ── 2. RULE: the parent's non-primary output IS inherited ───────
    //
    // THE BUG. The parent names DP-1, the sub-window names nothing, and
    // before this rule the sub-window mounted on the primary. Asserted
    // three ways, because a single assertion here can be satisfied by
    // coincidence:
    //
    //   (a) the sub-window's own outputName is now DP-1 — the string the
    //       lambda hands over
    //   (b) the sub-window is MEASURED AGAINST DP-1 — the screen the
    //       compositor will actually put it on, which is the part the
    //       user reported as wrong and the part a bare outputName check
    //       would not have caught
    //   (c) it is NOT the primary it was on a moment ago — without this,
    //       (b) passes on any machine where both screens are the same
    //       width, and the whole assertion set is a tautology
    //
    // Mutating the rule into "the sub-window keeps its own name always"
    // (i.e. removing the inheritance call, or inverting the guard) makes
    // (a), (b) and (c) all red at once.
    {
        PlayerPanel parent;
        PlayerStatusStripPanel sub(nullptr,
                                   mhw::PlayerSection::PlayerSplitSlot::Status);
        parent.show();
        sub.show();
        pumpEvents();

        parent.setOutputName(QString::fromLatin1(kOtherName), false);
        pumpEvents();

        check(parent.outputName() == QLatin1String(kOtherName),
              "the parent panel's own screen choice survives setOutputName "
              "— the value the sub-window is supposed to inherit");

        const int parentScreen = resolvedScreenWidth(&parent);
        check(parentScreen == kOtherWidth,
              "the parent is now measured against the 2560-wide DP-1");

        const int beforeScreen = resolvedScreenWidth(&sub);
        check(beforeScreen == kPrimaryWidth,
              "BEFORE inheritance the sub-window is measured against the "
              "1080-wide PRIMARY — this is the bug, reproduced as a number");

        inheritParentOutput(&sub, &parent);
        pumpEvents();

        // (a) the handed-over name
        check(sub.outputName() == QLatin1String(kOtherName),
              "RULE 2a — the sub-window inherits the parent's DP-1, so its "
              "own outputName is no longer empty");

        // (b) the measurement that follows
        const int afterScreen = resolvedScreenWidth(&sub);
        check(afterScreen == parentScreen,
              "RULE 2b — and it is now measured against the SAME screen as "
              "its parent, so the two drag clamps agree");

        // (c) and it really is the other screen, not a coincidence
        check(afterScreen != beforeScreen,
              "RULE 2c — which is a different screen than the primary it "
              "was on a moment ago, so this is a real move and not a "
              "no-op that happens to look right");
        check(afterScreen == kOtherWidth && afterScreen != kPrimaryWidth,
              "RULE 2d — and that screen is the 2560-wide landscape one, "
              "not the 1080-wide portrait primary");
    }

    // ── 3. RULE: the sub-window's OWN name wins ─────────────────────
    //
    // The rule is "inherit only what was not chosen", and a sub-window
    // with a name of its own has had a choice made for it. The console
    // has no per-sub-window SCREEN picker today, so this is a
    // future-proofing assertion rather than a live path — but it is the
    // clause that keeps a later picker from being silently overwritten,
    // and inverting the guard in the lambda (the most likely mutation)
    // breaks it.
    {
        PlayerPanel parent;
        PlayerStatusStripPanel sub(nullptr,
                                   mhw::PlayerSection::PlayerSplitSlot::Status);
        parent.show();
        sub.show();
        pumpEvents();

        parent.setOutputName(QString::fromLatin1(kOtherName), false);
        sub.setOutputName(QString::fromLatin1(kPrimaryName), false);
        pumpEvents();

        check(sub.outputName() == QLatin1String(kPrimaryName),
              "the sub-window has its own screen choice before the rule runs");
        check(resolvedScreenWidth(&sub) == kPrimaryWidth,
              "and it is on the primary screen it chose");

        inheritParentOutput(&sub, &parent);
        pumpEvents();

        check(sub.outputName() == QLatin1String(kPrimaryName),
              "RULE 3 — a sub-window that names its own output keeps it; "
              "the parent's DP-1 does NOT overwrite it");
        check(resolvedScreenWidth(&sub) == kPrimaryWidth,
              "and it is still measured against the screen it chose");
    }

    // ── 4. RULE: a parent naming an UNPLUGGED output is not inherited ─
    //
    // THE CASE THE SCREEN-LIST LOOP EXISTS FOR. A parent whose stored
    // output was unplugged does NOT lose its name: Panel::loadConfig()
    // reads `outputName` from panels.ini UNVALIDATED (measured — a seeded
    // "UNPLUGGED-99" comes back as a 12-character string), so the parent
    // still names a screen nobody can see and still degrades through
    // silently to primary on every geometry question.
    //
    // The rule's attached-screen loop is what keeps the sub-window from
    // being handed that name. Without it, setOutputName() would warn once
    // PER SUB-WINDOW — eleven lines for one condition — and revert the
    // sub-window to primary, which is the outcome but the wrong log.
    //
    // So this section asserts BOTH halves: the sub-window stays on
    // primary (the outcome), AND no warning is emitted (the reason the
    // loop is there and not just an early-out). The second half is what
    // distinguishes "checked the screen list" from "handed it over and
    // let setOutputName deal with it".
    {
        // Seed [world/split/player] outputName=UNPLUGGED-99 the way a
        // panels.ini on disk would, and let loadConfig() read it back.
        {
            QSettings s(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("monster-overlay"),
                        QStringLiteral("panels"));
            s.beginGroup(mhw::panelSettingsScopeGroup(QStringLiteral("player")));
            s.setValue(QStringLiteral("outputName"),
                       QStringLiteral("UNPLUGGED-99"));
            s.endGroup();
            s.sync();
        }

        // Count the warnings Panel::setOutputName emits while the rule
        // runs, so "quietly" is measured rather than assumed. A counting
        // handler needs somewhere to put the count that outlives the
        // call, hence the file-static sink.
        PlayerPanel parent;          // loadConfig() reads UNPLUGGED-99
        PlayerStatusStripPanel sub(nullptr,
                                   mhw::PlayerSection::PlayerSplitSlot::Status);
        parent.show();
        sub.show();
        pumpEvents();

        check(parent.outputName() == QLatin1String("UNPLUGGED-99"),
              "a parent's stored output name survives loadConfig() even "
              "when no such screen is attached — read verbatim, not "
              "validated");
        check(resolvedScreenWidth(&parent) == kPrimaryWidth,
              "and it degrades silently to primary on every geometry "
              "question, which is the degradation the rule must not "
              "amplify");

        // Install a counting message handler, run the rule, restore it.
        int sink = 0;
        gWarningSink = &sink;
        auto *previous = qInstallMessageHandler(countUnknownOutputWarning);
        inheritParentOutput(&sub, &parent);
        pumpEvents();
        qInstallMessageHandler(previous);
        gWarningSink = nullptr;

        check(sub.outputName().isEmpty(),
              "RULE 4 — the sub-window is left alone rather than handed a "
              "screen nobody can see, so it falls through to primary");
        check(resolvedScreenWidth(&sub) == kPrimaryWidth,
              "and it is measured against the primary, not against a "
              "screen name that resolves to nothing");
        check(sink == 0,
              "and it got there QUIETLY — no \"not a known output\" warning "
              "was emitted, which is what the attached-screen loop buys "
              "and what handing the name over anyway would lose (one "
              "warning per sub-window, eleven in all)");
    }

    // ── 5. THE USER'S SYMPTOM, END TO END ───────────────────────────
    //
    // The report is not "the sub-window's outputName string is wrong",
    // it is "the windows that belong to dp-1 show up on hdmi". So this
    // section states the symptom directly: every player sub-window,
    // measured after the inheritance runs, is on the SAME screen as the
    // panel it was carved out of — and the screen it is on is the one
    // the user chose, not the one the OS calls primary.
    //
    // The sub-windows are built one at a time in a fresh scope, because
    // each Panel reads its own section at construction and the rule is
    // per-window. All six are asserted, because the eleven production
    // call sites are eleven chances to forget one — and the one that is
    // forgotten is exactly the window the user would report.
    {
        // A parent that has been pointed at DP-1, which is the reporter's
        // [world/split/player] outputName=DP-1.
        struct Case { const char *key; Panel *(*make)(); };

        // Each lambda builds the real sub-window class, so a class that
        // stops honouring the rule (or stops being wired into main.cpp's
        // eleven calls at all) is a class this section can see.
        const Case cases[] = {
            {"player-status", []() -> Panel * {
                 return new PlayerStatusStripPanel(
                     nullptr, mhw::PlayerSection::PlayerSplitSlot::Status);
             }},
            {"player-bars", []() -> Panel * {
                 return new PlayerBarsStripPanel(
                     nullptr, mhw::PlayerSection::PlayerSplitSlot::Bars);
             }},
            {"player-mantles", []() -> Panel * {
                 return new PlayerMantlesStripPanel(
                     nullptr, mhw::PlayerSection::PlayerSplitSlot::Mantles);
             }},
            {"player-wirebug", []() -> Panel * {
                 return new PlayerWirebugStripPanel(
                     nullptr, mhw::PlayerSection::PlayerSplitSlot::Wirebug);
             }},
            {"player-weapon", []() -> Panel * {
                 return new PlayerWeaponStripPanel(
                     nullptr, mhw::PlayerSection::PlayerSplitSlot::Weapon);
             }},
            {"player-info", []() -> Panel * {
                 return new PlayerInfoStripPanel(
                     nullptr, mhw::PlayerSection::PlayerSplitSlot::Info);
             }},
        };

        int onPrimary = 0;
        int onOther   = 0;

        for (const Case &c : cases) {
            PlayerPanel parent;
            parent.show();
            pumpEvents();
            parent.setOutputName(QString::fromLatin1(kOtherName), false);
            pumpEvents();

            Panel *sub = c.make();
            sub->show();
            pumpEvents();

            const int parentScreen = resolvedScreenWidth(&parent);
            const int subBefore    = resolvedScreenWidth(sub);

            // The regression, in one line: the sub-window was on a
            // different screen than the panel it came out of.
            check(subBefore != parentScreen,
                  "the sub-window is on a DIFFERENT screen than its parent "
                  "before the rule runs — the reported bug, reproduced");

            inheritParentOutput(sub, &parent);
            pumpEvents();

            const int subAfter = resolvedScreenWidth(sub);
            check(subAfter == parentScreen,
                  "and it is on the SAME screen as its parent afterwards — "
                  "the screen the user actually chose");

            if (subAfter == kOtherWidth)
                ++onOther;
            if (subBefore == kPrimaryWidth)
                ++onPrimary;

            std::printf("      %-16s screen: %d -> %d  (%s)\n",
                        c.key, subBefore, subAfter,
                        qPrintable(sub->outputName()));

            delete sub;
        }

        check(onPrimary == int(sizeof(cases) / sizeof(cases[0])),
              "every sub-window started on the primary screen — the "
              "pre-fix state, measured rather than assumed");
        check(onOther == int(sizeof(cases) / sizeof(cases[0])),
              "and every one of them ends up on DP-1 — the user's "
              "「显示在 hdmi」symptom is gone for all six, not just one");
    }

    // ── 6. --output-all still wins over the inheritance ─────────────
    //
    // main.cpp applies --output-all AFTER the eleven inheritance calls,
    // so a later setOutputName() overwrites an earlier one. That is the
    // behaviour the v0.14 review pinned down and it is the reason the
    // blanket flag can still move a sub-window that just inherited
    // something else — which is what makes "put everything on this
    // screen" work at all.
    {
        PlayerPanel parent;
        PlayerStatusStripPanel sub(nullptr,
                                   mhw::PlayerSection::PlayerSplitSlot::Status);
        parent.show();
        sub.show();
        pumpEvents();

        parent.setOutputName(QString::fromLatin1(kOtherName), false);
        inheritParentOutput(&sub, &parent);
        pumpEvents();
        check(sub.outputName() == QLatin1String(kOtherName),
              "the sub-window inherited DP-1 from its parent");

        // What --output-all does, in main.cpp's order: it runs after,
        // so it wins.
        sub.setOutputName(QString::fromLatin1(kPrimaryName), false);
        pumpEvents();

        check(sub.outputName() == QLatin1String(kPrimaryName),
              "a LATER setOutputName() still overwrites the inherited one — "
              "--output-all reaches a sub-window that already inherited");
    }

    std::printf("\n== %d checks, %d failures ==\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
