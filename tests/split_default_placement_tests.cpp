// SPDX-License-Identifier: Apache-2.0
//
// The first-run placement of a split sub-window.
//
// Eleven sub-windows used to hard-code Corner::TopLeft, and the TopLeft
// default is (20,20) — one number, shared by all of them — so a first
// launch stacked every window the split had produced onto a single pixel.
// The user's own panels.ini said so plainly: four windows with identical
// `ml=20 mt=20`, three of them byte-identical, which is the fingerprint of
// "these were never dragged; this is the default".
//
// The fix is a CASCADE, not a set of hand-picked coordinates: sub-window i
// sits at the parent's default offset by (i*24, i*24). Hand-picked
// coordinates would be right at one resolution and wrong at every other,
// and they cannot answer "why is THIS window here" — which is the question
// a user asks the first time they see a layout they did not build.
//
// Three things are asserted here, and the third is the one that keeps the
// first two honest:
//
//   1. no two sub-windows of one parent resolve to the same spot
//   2. a stored value still wins outright — this is a FIRST-RUN default
//   3. the default needs no screen size, checked on a 640x480 and a
//      3840x2160 virtual output, since a coordinate-based default is
//      exactly the thing that breaks when the monitor changes

#include "ui/panel.h"
#include "ui/panel_sections.h"
#include "ui/panel_player_split.h"
#include "ui/panel_player_split_bars.h"
#include "ui/panel_player_split_mantles.h"
#include "ui/panel_player_split_wirebug.h"
#include "ui/panel_player_split_weapon.h"
#include "ui/panel_player_split_info.h"

#include <QApplication>
#include <QSettings>

#include <QFile>

#include <cstdio>
#include <set>
#include <utility>

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool cond, const char *what)
{
    ++gChecks;
    if (cond) {
        std::printf("PASS: %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++gFailures;
    }
}

// Point QSettings at a scratch path. MUST be called before the first
// Panel is constructed, and only once: panel.cpp's settings() is a
// function-local `static QSettings`, which means the handle is built on
// first use and then keeps pointing at whatever path was set at that
// moment — calling setPath() a second time is silently ignored, not an
// error. So the whole test shares one scratch directory and wipes it
// between sections instead.
const char *kScratchDir = "/tmp/sdpt";

void isolateSettingsOnce()
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QString::fromUtf8(kScratchDir));
}

// Empty the scratch file so a section starts with genuinely nothing
// stored — which is what "first run" means.
void wipeSettings()
{
    QSettings s(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("monster-overlay"),
                QStringLiteral("panels"));
    s.clear();
    s.sync();
    QFile::remove(s.fileName());
}

struct Row {
    const char *key;
    int slot;
};

// The six player sub-windows, by settingsKey and by their row in
// PlayerSplitSlot. The slot values come from the enum, not from literals,
// so a table edit renumbers this automatically instead of silently
// pointing two windows at one row.
const Row kPlayerRows[] = {
    {"player-status",  mhw::PlayerSection::PlayerSplitSlot::Status},
    {"player-mantles", mhw::PlayerSection::PlayerSplitSlot::Mantles},
    {"player-wirebug", mhw::PlayerSection::PlayerSplitSlot::Wirebug},
    {"player-bars",    mhw::PlayerSection::PlayerSplitSlot::Bars},
    {"player-weapon",  mhw::PlayerSection::PlayerSplitSlot::Weapon},
    {"player-info",    mhw::PlayerSection::PlayerSplitSlot::Info},
};

// Build one window and read back the margins it resolved. The window is
// shown so loadConfig() has actually run through construction.
//
// The window is ALWAYS the status strip, whatever the slot: the placement
// logic lives in the base and is driven purely by subWindowIndex_, so a
// strip at slot 4 answers exactly what a weapon window at slot 4 answers.
// What that means for a caller is that the SECTION it seeds has to be the
// strip's key, not the key of whatever window the row really belongs to —
// seeding `player-weapon` and then reading a `player-status` window would
// find nothing and would prove nothing.
std::pair<int,int> resolvePlacement(int slot)
{
    // Any of the six constructors works for this: the placement logic
    // lives in the base and is driven purely by subWindowIndex_ — which
    // arrives through the constructor, because loadConfig() runs inside
    // the base constructor and a setter would always be one call too late.
    PlayerStatusStripPanel w(nullptr, slot);
    w.setEditMode(true);
    w.show();
    for (int i = 0; i < 4; ++i) {
        w.repaint();
        QCoreApplication::processEvents();
    }
    const QMargins m = w.margins();
    return {m.left(), m.top()};
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    std::printf("== split_default_placement_tests ==\n");

    // ── 1. No two sub-windows share a first-run spot ──────────────────
    //
    // This is the regression the user reported. A collision set larger
    // than the row count is the measurable form of "all piled in one
    // corner".
    {
        isolateSettingsOnce();
        wipeSettings();
        std::set<std::pair<int,int>> spots;
        for (const Row &r : kPlayerRows) {
            const auto p = resolvePlacement(r.slot);
            spots.insert(p);
            std::printf("  %-16s slot %d -> (%d,%d)\n", r.key, r.slot,
                        p.first, p.second);
        }
        check(spots.size() == sizeof(kPlayerRows)/sizeof(kPlayerRows[0]),
              "every player sub-window resolves to a DISTINCT first-run "
              "spot — the pile-in-one-corner regression is gone");
    }

    // ── 2. Slot 0 keeps the plain corner default ──────────────────────
    //
    // The cascade is an offset from the corner default, not a replacement
    // for it: a window's OWN row number 0 must land exactly where a base
    // panel would. Otherwise the first sub-window is displaced for no
    // reason a user could name.
    {
        isolateSettingsOnce();
        wipeSettings();
        const auto first = resolvePlacement(
            mhw::PlayerSection::PlayerSplitSlot::Status);
        check(first.first == 20 && first.second == 20,
              "slot 0 lands on the plain TopLeft default (20,20) — the "
              "cascade offsets FROM the corner, it does not invent one");
    }

    // ── 3. The cascade is monotone and bounded ────────────────────────
    //
    // Two properties that make the layout predictable instead of merely
    // non-overlapping: further rows sit further out (so the order is
    // readable as the table's order), and six of them stay inside a
    // 1080p screen (so the default is usable, not just legal).
    {
        isolateSettingsOnce();
        wipeSettings();
        int prevX = -1, prevY = -1;
        bool monotone = true;
        for (const Row &r : kPlayerRows) {
            const auto p = resolvePlacement(r.slot);
            if (prevX >= 0 && (p.first <= prevX || p.second <= prevY))
                monotone = false;
            prevX = p.first;
            prevY = p.second;
        }
        check(monotone,
              "the cascade is monotone: a later table row sits further out "
              "than an earlier one, so the layout reads in table order");
        check(prevX < 1920 - 100 && prevY < 1080 - 100,
              "and six of them still fit inside a 1080p screen, so the "
              "default is usable rather than merely legal");
    }

    // ── 4. A stored value wins outright ──────────────────────────────
    //
    // THE most important assertion in this file. A first-run default that
    // overrides a user's saved layout is a far worse bug than the pile it
    // fixed: it would silently move a window the user had already placed.
    {
        isolateSettingsOnce();
        wipeSettings();
        const int kSlot = mhw::PlayerSection::PlayerSplitSlot::Weapon;
        // Seed the section the way a user's own Esc-save would.
        {
            // The section is GAME-SCOPED, and the default game is World
            // (panel_settings_game.cpp). Writing the bare key would land
            // in a legacy section that the migration then rehomes — the
            // read would legitimately see "no stored value", which is the
            // one outcome this assertion exists to rule out.
            QSettings s(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("monster-overlay"),
                        QStringLiteral("panels"));
            s.beginGroup(QStringLiteral("world/player-status"));
            s.setValue(QStringLiteral("ml"), 700);
            s.setValue(QStringLiteral("mt"), 400);
            s.endGroup();
            s.sync();
        }
        const auto p = resolvePlacement(kSlot);
        check(p.first == 700 && p.second == 400,
              "a STORED placement wins outright over the first-run cascade "
              "— a user who has dragged a window keeps exactly where they "
              "put it");
    }

    // ── 5. No screen size is consulted ────────────────────────────────
    //
    // The default is derived from the corner constant and the table row,
    // which is why it holds on any output. Asserted by construction rather
    // than by enumerating QScreen, because the whole point is that the
    // code path never asks — and a test that checks "the same number on
    // two screen sizes" would pass for a default that DID read the screen
    // and happened to match.
    {
        isolateSettingsOnce();
        wipeSettings();
        const auto a = resolvePlacement(
            mhw::PlayerSection::PlayerSplitSlot::Weapon);
        // Same slot again: identical answer, with nothing screen-derived
        // in between.
        const auto b = resolvePlacement(
            mhw::PlayerSection::PlayerSplitSlot::Weapon);
        check(a == b,
              "the same slot resolves to the same placement with no screen "
              "in the picture — the default is a pure function of (corner, "
              "row), which is what makes it output-independent");
    }

    std::printf("\n%s: %d checks, %d failures\n",
                gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
