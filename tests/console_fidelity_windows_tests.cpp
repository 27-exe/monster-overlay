// SPDX-License-Identifier: Apache-2.0
//
// The window model's own translation unit — src/ui/console_fidelity_windows.cpp
// — is the O1 port's new TU and the zero-reference-gate's complaint: a src
// .cpp with no ctest target reaching it. This is the "write the test" answer
// to the gate, not a snapshot update and not an EXEMPT row.
//
// What is pinned here, and why each rule can break silently:
//
// resolveWindowOutput(own, parent, primary)
//   * The chain is own → parent → primary and NOTHING else. The prototype's
//     `resolveOutput(w)` (reference.html:421) answers
//     `w.output==='follow' ? panels[w.panel].output : w.output` — a window
//     that named a screen keeps it, and only a window that named none
//     inherits. A branch that tested "is this output attached" instead of
//     "is it empty" would silently pin a window to a screen the user never
//     picked, which is the hardcoded-DP-1 class of bug this file exists to
//     keep out.
//   * Empty must be the ONLY "not set" spelling: every reader in the tree
//     (panel.cpp:611, main.cpp:1077 / main.cpp:1231, hud_canvas.cpp:1116)
//     tests isEmpty(), so a different spell here would disagree with all four.
//
// displayLabel(name, screens)
//   * Empty input → empty output, never a bare "" entry that would render as
//     a label with nothing behind it.
//   * A hit prints the prototype's `DP-1 · 2560×1440` (U+00B7 between name
//     and size, U+00D7 in the size) — the same spelling as
//     ControlPanel's SCREEN dropdown, so the two cannot drift.
//   * A MISS (the output was unplugged since the pick) returns the bare name.
//     Falling back to some other screen's size would be a lie about where
//     the window is; returning empty would erase the user's choice.
//
// readWindowVisible / writeWindowVisible
//   * ONE POLARITY, stated once: the stored value MEANS VISIBLE and the
//     default is true. The first spelling of the reader negated it
//     (`hidden = value(key,false); return !hidden`), so a window the user
//     had JUST hidden read back as visible and the inspector's checkbox
//     never stuck — a write that silently does nothing, which is exactly
//     what the O1 acceptance rule "no no-op control" forbids.
//   * Absent = visible. An existing install with no such row must read every
//     window as shown.
//   * Show writes NOTHING (the key is removed if present, nothing is added
//     if it was not), so a console that never touches the switch leaves no
//     new row in anyone's panels.ini — the same "no spurious key" rule
//     Panel::saveConfig() holds itself to.
//
// The settings are isolated with QSettings::setPath() into a per-run
// directory under TMPDIR (scratch, never the user's ~/.config): this test
// writes panels.ini and must not be able to read or damage the real one.

#include "ui/console_fidelity_windows.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRect>
#include <QSettings>
#include <QSize>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int checks = 0;
int failures = 0;

void check(bool cond, const char *what)
{
    ++checks;
    if (!cond) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

} // namespace

int main(int argc, char **argv)
{
    // Isolation FIRST: QSettings resolves the UserScope/IniFormat directory
    // once, on first construction, so the path has to be redirected before
    // anything (including this TU's thread_local handle) opens the file.
    // QTemporaryDir lives under TMPDIR, which the harness points at scratch.
    QTemporaryDir tmp(QDir::tempPath()
                      + QStringLiteral("/console-fidelity-windows-tests-XXXXXX"));
    if (!tmp.isValid()) {
        std::printf("FAIL: could not create a private settings directory\n");
        return 1;
    }
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       tmp.path());

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("probe"));

    // ── windowVisibleSettingsKey ────────────────────────────────────────
    // A distinct spelling from the QWidget property `visible` and from the
    // `visible` key panel.cpp once deleted: reusing either would let a
    // widget's own state and this flag collide through two readers.
    check(mhw::windowVisibleSettingsKey() == QStringLiteral("winVisible"),
          "key: the window switch is spelled `winVisible`");
    check(mhw::windowVisibleSettingsKey() != QStringLiteral("visible"),
          "key: it is NOT the QWidget property name `visible`");

    // ── resolveWindowOutput ─────────────────────────────────────────────
    check(mhw::resolveWindowOutput(QStringLiteral("HDMI-A-1"),
                                   QStringLiteral("DP-1"),
                                   QStringLiteral("eDP-1"))
              == QStringLiteral("HDMI-A-1"),
          "chain: a window's own answer always wins");
    check(mhw::resolveWindowOutput(QString(), QStringLiteral("DP-1"),
                                   QStringLiteral("eDP-1"))
              == QStringLiteral("DP-1"),
          "chain: no own answer falls through to the parent panel");
    check(mhw::resolveWindowOutput(QString(), QString(),
                                   QStringLiteral("eDP-1"))
              == QStringLiteral("eDP-1"),
          "chain: no own and no parent falls through to the primary");
    check(mhw::resolveWindowOutput(QString(), QString(), QString())
              == QString(),
          "chain: an entirely empty chain is empty, not a guessed name");
    // The shape test the "is it a real output name" branch would fail: a
    // name is either there or it is not, and shape is never consulted.
    check(mhw::resolveWindowOutput(QStringLiteral("not-a-real-output"),
                                   QStringLiteral("DP-1"),
                                   QStringLiteral("eDP-1"))
              == QStringLiteral("not-a-real-output"),
          "chain: a non-empty answer is returned verbatim, shape untouched");

    // ── displayLabel ────────────────────────────────────────────────────
    QList<screen_query::OutputInfo> screens;
    {
        screen_query::OutputInfo dp;
        dp.name = QStringLiteral("DP-1");
        dp.geometry = QRect(0, 0, 2560, 1440);
        screens.append(dp);
        screen_query::OutputInfo hdmi;
        hdmi.name = QStringLiteral("HDMI-A-1");
        hdmi.geometry = QRect(2560, 0, 1080, 1920);
        screens.append(hdmi);
        screen_query::OutputInfo weird;
        weird.name = QStringLiteral("UNKNOWN-1");
        weird.geometry = QRect();   // a name Qt reports with no geometry
        screens.append(weird);
    }

    // `\xc2\xb7` is U+00B7 MIDDLE DOT and `\xc3\x97` is U+00D7
    // MULTIPLICATION SIGN — the two glyphs reference.html's
    // labelDisplay() emits. Split across two literals because a C++
    // hex escape runs into the digits that follow it.
    check(mhw::displayLabel(QString(), screens).isEmpty(),
          "label: an empty output name labels to nothing");
    check(mhw::displayLabel(QStringLiteral("DP-1"), screens)
              == QString::fromUtf8("DP-1 \xc2\xb7 2560\xc3\x97" "1440"),
          "label: a hit is `<name> · <w>x<h>` in the prototype's glyphs");
    check(mhw::displayLabel(QStringLiteral("HDMI-A-1"), screens)
              == QString::fromUtf8("HDMI-A-1 \xc2\xb7 1080\xc3\x97" "1920"),
          "label: a second hit resolves its own entry, not the first one");
    check(mhw::displayLabel(QStringLiteral("UNKNOWN-1"), screens)
              == QStringLiteral("UNKNOWN-1"),
          "label: a hit with no geometry is the bare name, not `name · 0x0`");
    check(mhw::displayLabel(QStringLiteral("DP-7"), screens)
              == QStringLiteral("DP-7"),
          "label: a MISS (unplugged since the pick) keeps the bare name");
    check(mhw::displayLabel(QStringLiteral("DP-1"), QList<screen_query::OutputInfo>())
              == QStringLiteral("DP-1"),
          "label: a miss against an empty list keeps the bare name too");

    // ── readWindowVisible / writeWindowVisible ──────────────────────────
    // The scope is the same `[game/mode/<key>]` group name the caller
    // already resolved; the module never derives one itself.
    const QString scope = QStringLiteral("rise/split/monster-identity");
    const QString key = QStringLiteral("monster-identity");

    // Absent means visible: an install that never carried the row reads
    // every window as shown.
    check(mhw::readWindowVisible(scope, key),
          "visible: a group with no row reads as VISIBLE (default true)");

    // Hiding is a real write, and it must be the value the reader takes
    // at face value. This is the regression the original
    // `return !value(key,false)` half of the pair failed: the writer
    // stored `false` and the reader negated it straight back to true.
    mhw::writeWindowVisible(scope, key, false);
    check(!mhw::readWindowVisible(scope, key),
          "visible: hiding a window makes the very next read answer HIDDEN");

    // …and it survives a re-read through a fresh handle (a fresh QSettings
    // is what the console builds on every rebuild of the window model).
    {
        QSettings fresh(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("monster-overlay"),
                        QStringLiteral("panels"));
        fresh.beginGroup(scope);
        const bool present = fresh.contains(
            mhw::windowVisibleSettingsKey());
        const bool stored = fresh.value(mhw::windowVisibleSettingsKey(), true)
                                .toBool();
        fresh.endGroup();
        check(present, "visible: a hide really added the row to the file");
        check(!stored,
              "visible: the stored value is `false`, and the reader does "
              "not flip it");
    }

    // Showing REMOVES the row rather than writing `true`: the file must not
    // accumulate rows for windows nobody ever hid.
    mhw::writeWindowVisible(scope, key, true);
    check(mhw::readWindowVisible(scope, key),
          "visible: showing it again reads as VISIBLE");
    {
        QSettings fresh(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("monster-overlay"),
                        QStringLiteral("panels"));
        fresh.beginGroup(scope);
        const bool present = fresh.contains(
            mhw::windowVisibleSettingsKey());
        fresh.endGroup();
        check(!present,
              "visible: showing a window REMOVES the row instead of "
              "storing `true`");
    }

    // The "no spurious key" rule from the other side: asking to show a
    // window that was never hidden must not create the row in the first
    // place, so a console that never touches the switch writes nothing.
    const QString untouched = QStringLiteral("world/single/player");
    mhw::writeWindowVisible(untouched, QStringLiteral("player"), true);
    {
        QSettings fresh(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("monster-overlay"),
                        QStringLiteral("panels"));
        fresh.beginGroup(untouched);
        const bool any = fresh.childKeys().contains(
            mhw::windowVisibleSettingsKey());
        fresh.endGroup();
        check(!any,
              "visible: showing a window that was never hidden adds no row");
    }

    // A DIFFERENT window is independent. The scope IS the window's address
    // - `[game/mode/<key>]`, one group per window, exactly as
    // Panel::settingsGroup() spells it - so `key` is only there for the
    // caller's convenience (the reader deliberately ignores it: the group
    // already names the window). Hiding one window must leave the next
    // window's group reading VISIBLE, or the switch would be global and
    // one layout domain would answer for all four.
    const QString otherScope = QStringLiteral("rise/split/monster-conditions");
    check(mhw::readWindowVisible(otherScope,
                                 QStringLiteral("monster-conditions")),
          "visible: hiding one window leaves the NEXT window reading VISIBLE");

    // An empty scope is "no window", and the honest answer is shown — a
    // write there must be a no-op rather than a row under a group nobody
    // will ever read.
    check(mhw::readWindowVisible(QString(), key),
          "visible: an empty scope reads as VISIBLE");
    mhw::writeWindowVisible(QString(), key, false);
    check(mhw::readWindowVisible(QString(), key),
          "visible: an empty scope cannot be hidden by a write");

    // The file this all went to is the TEMP one, not the user's. This is
    // the isolation assertion: if it ever fails, the test has been writing
    // panels.ini under the real ~/.config.
    {
        const QString expected = QDir(tmp.path()).filePath(
            QStringLiteral("monster-overlay/panels.ini"));
        QSettings fresh(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("monster-overlay"),
                        QStringLiteral("panels"));
        const QString actual = QDir::cleanPath(fresh.fileName());
        check(actual == QDir::cleanPath(expected),
              "isolation: panels.ini resolves inside the private temp dir");
        // ...and the writes really landed THERE: the private file exists.
        // If the redirect ever stopped working, `actual` would name the
        // user's file and this would be the second, independent witness.
        check(QFile::exists(actual),
              "isolation: the private file was actually created");
        QSettings readBack(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("monster-overlay"),
                           QStringLiteral("panels"));
        readBack.beginGroup(scope);
        const bool stillHidden = readBack.contains(
            mhw::windowVisibleSettingsKey());
        readBack.endGroup();
        check(!stillHidden,
              "isolation: the group this test hid lives in the PRIVATE "
              "file (the show above removed it there, and nowhere else)");
    }

    // ── windowOnDisplay: the #window-count predicate ────────────────────
    //
    // reference.html:476 writes `visibleWindows().length` into
    // #window-count, and refreshStageHead() used to count on
    // `outputResolved == screenName` alone — so a window the user had
    // hidden, or one whose panel master was off, still incremented the
    // chip over a board that was not drawing it. Three ANDed facts, four
    // states, one matrix; each row below is a state the real console can
    // be in.
    {
        const QString dp1 = QStringLiteral("DP-1");
        mhw::WindowState allOn;
        allOn.outputResolved = dp1;
        check(mhw::windowOnDisplay(allOn, dp1),
              "count: a live window on the drawn display IS counted");

        // The display axis, on its own: a window on the other output is
        // not part of this board's picture in EITHER direction.
        mhw::WindowState elsewhere = allOn;
        elsewhere.outputResolved = QStringLiteral("HDMI-A-1");
        check(!mhw::windowOnDisplay(elsewhere, dp1),
              "count: a window resolved to another output is NOT counted");
        check(!mhw::windowOnDisplay(allOn, QStringLiteral("eDP-1")),
              "count: …and the drawn display's own mismatch fails too");

        // The panel master (prototype `panel.visible`). Distinct from the
        // window's own switch on purpose: one is the eye on the rail, the
        // other is 显示该窗口, and they have separate writers.
        mhw::WindowState masterOff = allOn;
        masterOff.panelEnabled = false;
        check(!mhw::windowOnDisplay(masterOff, dp1),
              "count: a window whose PANEL MASTER is off is NOT counted");
        // And the master gate is NOT the window switch read backwards:
        // with the window's own switch still true, only the master arm
        // can be producing the false.
        check(masterOff.visiblePersisted,
              "count: the master-off row leaves the WINDOW switch true — "
              "the two axes are read, not confused");

        // The window's own switch (the row writeWindowVisible writes).
        mhw::WindowState hidden = allOn;
        hidden.visiblePersisted = false;
        check(!mhw::windowOnDisplay(hidden, dp1),
              "count: a window the user HID is NOT counted");
        // Both off at once is still one false, not a special case.
        mhw::WindowState bothOff = hidden;
        bothOff.panelEnabled = false;
        check(!mhw::windowOnDisplay(bothOff, dp1),
              "count: master off AND window hidden is one false, not two");

        // THE AXIS THAT MUST NOT GATE IT. sectionsOn == 0 is the
        // `.hidden-window` placeholder — a drawn box with the word
        // `hidden` where the body would be. isShown()
        // (reference.html:443) and visibleWindows() are different
        // questions and the chip follows the second, so an all-off
        // window still counts as being ON the board.
        mhw::WindowState placeholder = allOn;
        placeholder.sectionsOn = 0;
        placeholder.sectionsTotal = 5;
        check(mhw::windowOnDisplay(placeholder, dp1),
              "count: a window with EVERY section off still IS counted — "
              "the `.hidden-window` placeholder is a drawn window, and "
              "`isShown()` is a different question from `visibleWindows()`");

        // Game capability is not this function's job: a World scope never
        // EMITS a pets row (buildWindowStates skips it), so the loop the
        // chip runs never sees one. Pinned here as the property it is —
        // the predicate answers only for rows that exist.
        check(mhw::windowOnDisplay(allOn, dp1),
              "count: the predicate is over rows that exist — game "
              "capability is filtered by buildWindowStates() before it runs");
    }

    if (failures == 0)
        std::printf("console-fidelity-windows-tests: ALL PASSED (%d checks)\n",
                    checks);
    else
        std::printf("console-fidelity-windows-tests: %d/%d FAILED\n",
                    failures, checks);
    return failures == 0 ? 0 : 1;
}
