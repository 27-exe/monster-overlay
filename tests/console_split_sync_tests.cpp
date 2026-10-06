// SPDX-License-Identifier: Apache-2.0
//
// v0.13: the console's own preview must follow the rail and the mode switch.
//
// WHAT THIS FILE PINS
// -------------------
// The user reported three symptoms that are three bugs in two halves of
// the same console:
//
//   «仍然 Rise 预览不同步»  (rail switch does not move the preview)
//       The sub-window host was constructed ONCE, on the first call to
//       ControlPanel::syncSplitPreviewToCanvas() after startup, and every
//       later call took the `if (!splitPreview_)` fast path and did
//       nothing. The fourteen split windows had already read their
//       QSettings group inside their Panel constructor (loadConfig() ->
//       settingsGroup()), so re-pointing the game at them afterwards was
//       a no-op: switchGame() called splitPreview_->syncAll(), which
//       re-seeded demo data and re-pushed section masks but could not
//       re-run a constructor.
//
//       The user's disk was the evidence. Their panels.ini had a
//       `[rise/...]` group with ZERO sub-window sections beside a
//       `[world/...]` group with all thirteen, because the host that
//       would have written the Rise sections was constructed while the
//       namespace still said World, and nothing ever rebuilt it. src/main.cpp:586
//       is the inverse proof: the overlay sets the game BEFORE
//       constructing its windows, and its `[rise]/[world]` sections are both
//       complete for exactly that reason.
//
//   «切换完全没反应，点击切换后在控制台中也不会自动变为分块预览»
//       applyLayoutChoice() moved layoutModeInForce_ but never pushed the
//       new mode into the canvas on the NO-CHILD arm — the arm the user
//       actually hits, since the console is usually open with the overlay
//       not running. The restart arm called syncSplitPreviewToCanvas();
//       the no-restart arm returned right after writing the conf row. So
//       the combo and the live preview disagreed until the console was
//       reopened.
//
//   AND A THIRD, found while pinning the second: THE CONF ROW WAS WRITTEN
//       ONE MODE BEHIND. saveMaskToDisk() serialises the `layout=` row
//       FROM layoutModeInForce_, and it ran BEFORE the mode moved on both
//       arms of applyLayoutChoice(). Every apply therefore wrote the mode
//       the user was LEAVING, so the file on disk and the preview in
//       front of the user described different topologies.
//
// The three contracts, in order of how much a wrong answer costs:
//
//   A. RAIL SWITCH REBUILDS THE GAME DOMAIN AND THE HOST. With the mode
//      in force = Split, switching the rail must (1) re-point the panel
//      settings namespace to the new game, and (2) destroy and rebuild
//      the sub-window host, so new Panel constructors run and read the
//      new game's sections. Both are asserted DIRECTLY:
//        * (1) through mhw::panelSettingsGame(), the same global the
//          constructors read, and
//        * (2) through the CANVAS'S OWN PAINTED PIXELS. The fixture
//          stores a different `scale` under each game's group (world 2.5,
//          rise 1.5); scale is read exactly once, inside the Panel
//          constructor; and a sub-window's box in the canvas is sized
//          from that window's contentWidth()/contentHeight(). So a canvas
//          painted by a world-built host and a canvas painted by a
//          rise-built host are different images, and "the pixels the
//          user is looking at changed when they clicked the rail" is the
//          whole contract, observed at the one place that matters. An
//          unrebuilt host repaints itself into the identical image.
//      A World→Rise→World round trip rebuilds again in both directions,
//      so the fix cannot be a one-shot.
//
//   B. SINGLE MODE DOES NOT BUILD A HOST AT ALL. The preview windows are
//      layer-shell surfaces; keeping fourteen of them alive for a
//      preview that is not drawing them is the leak
//      HudCanvas::setLayoutMode(Single) exists to prevent. With the mode
//      Single, a rail switch must leave the canvas's subWindowCount() at
//      0 — and this is asserted from the other side of the mode switch
//      too, so a Single→Split→Single round trip is covered.
//
//   C. MODE APPLY MOVES THE CANVAS AND THE CONF TOGETHER, immediately.
//      Both halves are asserted through the same reader the console uses
//      (parseConsoleLayoutRow + resolveStartupLayoutMode, the shared
//      chain main.cpp also calls): after apply, the canvas's mode is the
//      new mode AND the conf resolves to it through the Conf row — not
//      through the default. Returning to Single then writes NO row, which
//      is the property that keeps an old conf byte-for-byte single.
//
// WHY THIS LINKS mhw-ui: ControlPanel is a Q_OBJECT widget that
// constructs fourteen Panel subclasses, and every one of those carries
// Q_OBJECT + AUTOMOC. Compiling the sources into this target would run
// moc twice over the same headers. The target therefore links the same
// way monster-control-rise-rf-smoke does.
//
// THE MUTATION DISCIPLINE. Each contract above carries the one-line
// change that must make it FAIL, stated at the assertion that carries it.
// The real output of running all four is in the task report; they are:
//   A: drop syncSplitPreviewToCanvas() from switchGame()
//      -> rise keeps World's 2.5 (rise=298, world=298)
//   A: drop mhw::setPanelSettingsGame() from syncSplitPreviewToCanvas()
//      -> same symptom, assert fails
//   A: collapse needsRebuild back to `if (!splitPreview_)` (the
//      "改域 + syncAll" dead end the parent session already tried)
//      -> same symptom, assert fails
//   C: drop syncSplitPreviewToCanvas() from applyLayoutChoice()'s
//      no-child arm -> the canvas stays Single
//   C: restore the old write-before-mode ordering -> the conf resolves
//      through the DEFAULT instead of the Conf row (source=3)

#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/panel_settings_game.h"
#include "ui/panel_sections.h"
#include "ui/viewmodel/console_layout_mode_codec.h"
#include "core/string_table.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <set>

namespace {

int checks = 0;
int failures = 0;

void check(bool cond, const char *what, const QString &detail = QString())
{
    ++checks;
    if (cond) {
        std::printf("PASS: %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL: %s", what);
        if (!detail.isEmpty())
            std::fprintf(stderr, " -- %s", qPrintable(detail));
        std::fprintf(stderr, "\n");
        ++failures;
    }
}

// Writes a conf the way the parent session left the user's: the layout
// row is the only line that matters to the resolver, and an ABSENT row
// resolves Single. Used to set up each contract's starting conf.
void writeConfRow(const QString &path, const QString &row)
{
    // mkpath FIRST: the conf lives one directory deep under the
    // redirected XDG_CONFIG_HOME and the parent does not exist on a
    // fresh temp dir. A silent open() failure here leaves no conf at
    // all, which reads as "no layout row" and makes every Split-mode
    // contract fail as Single — a fixture bug that looks exactly like a
    // product bug.
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        check(false, "the fixture conf could be created",
              f.errorString());
        return;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << row;
    out.flush();
    f.close();
}

// QSettings monster-control.conf: the console's own persisted game.
void seedGame(const char *game)
{
    QSettings s;
    s.setValue(QStringLiteral("game"), QString::fromLatin1(game));
    s.sync();
}

// The path mhw::localeConfPath() resolves to under the redirected
// XDG_CONFIG_HOME — the file the console reads `layout=` from and writes
// it back to.
QString confPath(const QTemporaryDir &home)
{
    return home.path()
           + QStringLiteral("/monster-overlay/monster-overlay.conf");
}

// The panels.ini QSettings (IniFormat, UserScope, "monster-overlay",
// "panels") resolves to, under the redirected XDG_CONFIG_HOME.
QString iniPath(const QTemporaryDir &home)
{
    return home.path() + QStringLiteral("/monster-overlay/panels.ini");
}

// Clicks the rail's game button by its localized label — the same lookup
// control_reframework_smoke.cpp uses, so the test drives the real widget
// path rather than calling switchGame() directly.
void clickGameButton(ControlPanel *cp, const QString &key)
{
    const QString want = mhw::StringTable::instance().tr(key);
    for (QPushButton *button : cp->findChildren<QPushButton *>()) {
        if (button->objectName() == QStringLiteral("gameBtn")
            && button->text() == want) {
            button->click();
            QCoreApplication::processEvents();
            return;
        }
    }
    check(false, qPrintable(QStringLiteral("game button '%1' was found").arg(key)));
}

// The console's mode-in-force is private; the canvas's layoutMode() is
// the observable half of it and is what the user actually sees.
HudCanvas *findCanvas(ControlPanel *cp)
{
    return cp->findChild<HudCanvas *>();
}

// Pull the mode combo to the other entry and press apply, exactly as the
// user does. Offscreen-safe by construction: applyLayoutChoice() only
// raises a confirm dialog when an overlay CHILD is alive, and this test
// drives the console with no child (the same state the user is in when
// they open the console before START).
void chooseLayoutAndApply(ControlPanel *cp, bool split)
{
    QPushButton *apply = cp->findChild<QPushButton *>(QStringLiteral("layoutApply"));
    QComboBox *combo = cp->findChild<QComboBox *>(QStringLiteral("layoutCombo"));
    if (!apply || !combo) {
        check(false, "the layout combo and apply button both exist");
        return;
    }
    combo->setCurrentIndex(split ? 1 : 0);
    QCoreApplication::processEvents();
    check(apply->isEnabled(),
          split ? "choosing Split arms the apply button"
                : "choosing Single arms the apply button");
    apply->click();
    QCoreApplication::processEvents();
}


// The console's split-window host is a class local to control_panel.cpp,
// so a test cannot reach the windows it owns — and the accessor that would
// expose them lives in Task B's header. What IS available, and is a
// stronger assertion anyway, is the canvas's OWN PAINTED OUTPUT.
//
// A sub-window's box in the canvas is sized from the window's
// contentWidth()/contentHeight() (hud_canvas.cpp's paintSubWindowCluster),
// which are the window's own numbers at its own scale. The fixture stores
// a different `scale` under each game's group (world 2.5, rise 1.5), and
// scale is read exactly once — inside the Panel constructor. So a canvas
// painted with a world-built host and a canvas painted with a rise-built
// host draw DIFFERENT PIXELS, and "the pixels changed when I clicked the
// rail" is the whole contract, observed at the one place the user looks.
//
// grab() is inherited from QWidget and maps no window (offscreen), so
// this never touches the desktop.
QImage canvasPixels(ControlPanel *cp)
{
    HudCanvas *canvas = cp->findChild<HudCanvas *>();
    if (!canvas)
        return QImage();
    return canvas->grab().toImage();
}

// Two painted canvases differ, reported as a fraction so a one-box change
// and a full repaint are distinguishable in the output.
qreal pixelDifference(const QImage &a, const QImage &b)
{
    if (a.isNull() || b.isNull() || a.size() != b.size())
        return 1.0;
    qreal diff = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb *ra = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *rb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x)
            if (ra[x] != rb[x])
                diff += 1.0;
    }
    return diff / qreal(a.width() * a.height());
}

// Writes one key into one group of a clone of the developer's panels.ini
// scaffold, so a test can give each game a DIFFERENT persisted scale.
void writeIniGroup(const QString &path, const QString &group,
                   const QString &key, const QString &value)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite | QIODevice::Text))
        return;
    QTextStream io(&f);
    io.setEncoding(QStringConverter::Utf8);
    QStringList lines;
    while (!io.atEnd())
        lines.append(io.readLine());
    // Drop an existing key for the group, then append the group section
    // with the one key. INI ordering is irrelevant to QSettings' reader.
    QStringList kept;
    bool skipping = false;
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1Char('['))) {
            skipping = (line.trimmed() != QLatin1Char('[') + group
                                   + QLatin1Char(']'));
            kept.append(line);
            continue;
        }
        if (skipping)
            kept.append(line);
        else if (!line.trimmed().startsWith(key + QLatin1Char('=')))
            kept.append(line);
    }
    if (!kept.isEmpty() && !kept.last().isEmpty())
        kept.append(QString());
    kept.append(QLatin1Char('[') + group + QLatin1Char(']'));
    kept.append(key + QLatin1Char('=') + value);
    f.resize(0);
    io.seek(0);
    for (const QString &line : kept)
        io << line << QLatin1Char('\n');
    f.close();
}

// ─── A + B: the rail switch rebuilds the host and the namespace ────────
void testRailSwitchRebuildsTheHost()
{
    std::printf("\nA+B. rail switch: game domain + sub-window host rebuild\n");

    QTemporaryDir home;
    if (!home.isValid()) {
        check(false, "temp config home");
        return;
    }
    qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
    // A conf row that puts the console into Split mode is the precondition
    // for the whole contract: in Single there is by contract no host.
    writeConfRow(confPath(home), QStringLiteral("layout=split\n"));
    seedGame("world");
    // A WORLD SECTOR WITH A DISTINCTIVE SCALE. The console's preview
    // windows read their persisted scale inside their Panel constructor,
    // so a value stored under one game's group and not the other's makes
    // the two games' PREVIEWS PAINT DIFFERENTLY. That is what makes the
    // rebuild assertion below about the geometry actually in force rather
    // than about an object identity: a canvas painted by a world-built
    // host and a canvas painted by a rise-built host are different images.
    writeIniGroup(iniPath(home), QStringLiteral("world/player-status"),
                  QStringLiteral("scale"), QStringLiteral("2.5"));
    writeIniGroup(iniPath(home), QStringLiteral("rise/player-status"),
                  QStringLiteral("scale"), QStringLiteral("1.5"));

    ControlPanel cp;
    cp.show();
    QCoreApplication::processEvents();

    HudCanvas *canvas = findCanvas(&cp);
    check(canvas != nullptr, "the console has a canvas");
    check(canvas && canvas->layoutMode()
              == mhw::PlayerSection::PlayerLayoutMode::Split,
          "startup reads layout=split and pushes it into the canvas");
    if (!canvas)
        return;
    const int before = canvas->subWindowCount();
    check(before > 0, "a Split-mode console has sub-window bindings",
          QStringLiteral("count=%1").arg(before));

    // THE GAME DOMAIN, before the switch: console started on World.
    check(mhw::panelSettingsGame() == mhw::GameId::World,
          "the console's panel-settings namespace follows startup game");

    // THE BASELINE PAINT, captured before anything is clicked. Every
    // comparison below is against this image, so the console must have
    // actually painted: a canvas that never paints makes both the pass
    // and the failure meaningless.
    const QImage worldPixels = canvasPixels(&cp);
    check(!worldPixels.isNull() && worldPixels.width() > 0,
          "the Split-mode canvas paints real pixels",
          QStringLiteral("%1x%2").arg(worldPixels.width())
                                 .arg(worldPixels.height()));

    // THE MUTATION FOR CONTRACT A. Delete the syncSplitPreviewToCanvas()
    // call from switchGame() and this whole block fails:
    //   * mhw::panelSettingsGame() stays World, and
    //   * the canvas repaints itself into the SAME image, because the
    //     sub-windows behind it are the ones built for World and they
    //     never get rebuilt.
    clickGameButton(&cp, QStringLiteral("console.game.rise"));

    check(mhw::panelSettingsGame() == mhw::GameId::Rise,
          "after the rail switch the panel-settings namespace is Rise");

    // THE DIRECT EVIDENCE. Not "did an object change" but "did the thing
    // the user is LOOKING AT change when they clicked the rail". A host
    // that was never rebuilt paints the identical image; the user's
    // «Rise 预览不同步» in one assertion.
    const QImage risePixels = canvasPixels(&cp);
    const qreal diff = pixelDifference(worldPixels, risePixels);
    check(diff > 0.0,
          "the canvas PAINTS DIFFERENTLY after switching to Rise — the "
          "sub-windows were rebuilt with Rise's own geometry",
          QStringLiteral("%1% of pixels changed").arg(diff * 100.0, 0, 'f', 1));
    std::printf("     (%.1f%% of the canvas changed when the rail "
                "moved to Rise)\n", double(diff * 100.0));

    check(canvas->subWindowCount() == before,
          "and the bindings survive the rebuild unchanged in COUNT",
          QStringLiteral("before=%1 after=%2")
              .arg(before).arg(canvas->subWindowCount()));

    // And back, because the bug was symmetric: a round trip must rebuild
    // again rather than cache either side.
    const QImage beforeReturn = canvasPixels(&cp);
    clickGameButton(&cp, QStringLiteral("console.game.world"));
    check(mhw::panelSettingsGame() == mhw::GameId::World,
          "the ride back re-points the namespace to World");
    check(pixelDifference(beforeReturn, canvasPixels(&cp)) > 0.0,
          "and the canvas changes again — the round trip rebuilds rather "
          "than caching either side");
    check(canvas->subWindowCount() == before,
          "the bindings are re-pushed again");
}

// ─── D: Single mode never builds a host ────────────────────────────────
void testSingleModeBuildsNoHost()
{
    std::printf("\nD. single mode: a rail switch must not build a host\n");

    QTemporaryDir home;
    if (!home.isValid()) {
        check(false, "temp config home");
        return;
    }
    qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
    // NO layout= row at all: the conf the parent session left behind
    // after clearing it. That is the user's exact reproduction.
    QDir().mkpath(QFileInfo(iniPath(home)).absolutePath());
    seedGame("world");

    ControlPanel cp;
    cp.show();
    QCoreApplication::processEvents();

    HudCanvas *canvas = findCanvas(&cp);
    check(canvas != nullptr, "the console has a canvas");
    if (!canvas)
        return;

    // The three-part config the task asks to prove separately: with no
    // layout row the combo says Single, the canvas is Single, and there
    // are no sub-windows.
    check(canvas->layoutMode() == mhw::PlayerSection::PlayerLayoutMode::Single,
          "no layout row resolves Single in the canvas");
    check(canvas->subWindowCount() == 0,
          "and the canvas holds no sub-window bindings");

    clickGameButton(&cp, QStringLiteral("console.game.rise"));
    check(canvas->layoutMode() == mhw::PlayerSection::PlayerLayoutMode::Single,
          "a rail switch does not change the mode in Single");
    check(canvas->subWindowCount() == 0,
          "a Single-mode rail switch builds NO sub-window host",
          QStringLiteral("count=%1").arg(canvas->subWindowCount()));
}

// ─── C: mode apply moves the canvas immediately ────────────────────────
void testModeApplyMovesTheCanvas()
{
    std::printf("\nC. mode apply: canvas follows immediately, child or not\n");

    QTemporaryDir home;
    if (!home.isValid()) {
        check(false, "temp config home");
        return;
    }
    qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());
    QDir().mkpath(QFileInfo(iniPath(home)).absolutePath());
    seedGame("world");

    ControlPanel cp;
    cp.show();
    QCoreApplication::processEvents();

    HudCanvas *canvas = findCanvas(&cp);
    check(canvas != nullptr, "the console has a canvas");
    if (!canvas)
        return;
    check(canvas->layoutMode() == mhw::PlayerSection::PlayerLayoutMode::Single,
          "opens on Single (no conf row, no child running)");
    check(canvas->subWindowCount() == 0,
          "and paints exactly the four Single-mode panels");

    // THE MUTATION FOR CONTRACT C. In applyLayoutChoice(), the
    // no-child arm returns right after saveMaskToDisk() +
    // layoutModeInForce_ = mode, with no syncSplitPreviewToCanvas() call.
    // Deleting the call this contract depends on reproduces the user's
    // 「切换完全没反应」 on the no-child path exactly.
    chooseLayoutAndApply(&cp, /*split=*/true);

    check(canvas->layoutMode() == mhw::PlayerSection::PlayerLayoutMode::Split,
          "after apply, the canvas's mode is Split IMMEDIATELY");
    check(canvas->subWindowCount() > 0,
          "and its sub-window bindings are there with it",
          QStringLiteral("count=%1").arg(canvas->subWindowCount()));

    // THE CONF ROW the relaunched child will read. Read with the SAME
    // reader the console uses at startup (parseConsoleLayoutRow +
    // resolveStartupLayoutMode), not as a string match: a row is only
    // "the conf says split" if the resolver that will act on it agrees.
    {
        QFile f(confPath(home));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            in.setEncoding(QStringConverter::Utf8);
            const mhw::PlayerLayoutResolution readBack =
                mhw::resolveStartupLayoutMode(
                    mhw::PlayerLayoutCliRequest{},
                    mhw::parseConsoleLayoutRow(in.readAll()).source);
            check(readBack.mode == mhw::PlayerSection::PlayerLayoutMode::Split,
                  "the conf the console just wrote resolves SPLIT through "
                  "the shared chain the overlay also calls",
                  QStringLiteral("source=%1").arg(int(readBack.source)));
            check(readBack.source
                      == mhw::PlayerLayoutSource::Conf,
                  "and the source is the conf row, not the default");
        }
    }

    // And back to Single, so the round trip is covered and Single's
    // clear-on-enter is asserted from the console's own path.
    chooseLayoutAndApply(&cp, /*split=*/false);
    check(canvas->layoutMode() == mhw::PlayerSection::PlayerLayoutMode::Single,
          "applying Single moves the canvas back");
    check(canvas->subWindowCount() == 0,
          "and Single's entry clears the sub-windows");

    // AND THE CONF IS A SET, NOT A PILE. Going back to Single must drop
    // the row entirely (Single is the resolver's default, so writing
    // `layout=single` would add a hand-editable "single" that looks like
    // a deliberate choice) — asserted through the same reader the console
    // uses, at save time, not from a cached string list.
    QString afterSingle;
    {
        QFile f(confPath(home));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            in.setEncoding(QStringConverter::Utf8);
            afterSingle = in.readAll();
        }
    }
    check(!afterSingle.contains(QStringLiteral("layout=")),
          "returning to Single writes NO layout row (the default is not "
          "written down)",
          afterSingle);
    // And the mask rows survived every rewrite on the way.
    for (const QString &need : {QStringLiteral("player="),
                                QStringLiteral("monster="),
                                QStringLiteral("damage="),
                                QStringLiteral("pets="),
                                QStringLiteral("maskVersion=1")})
        check(afterSingle.contains(need),
              qPrintable(QStringLiteral("the %1 row survives the round trip")
                             .arg(need)));
}

} // namespace

int main(int argc, char *argv[])
{
    QTemporaryDir configRoot;
    qputenv("XDG_CONFIG_HOME", configRoot.path().toLocal8Bit());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("monster-overlay"));
    QApplication::setApplicationName(QStringLiteral("monster-control"));
    QApplication::setApplicationVersion(QStringLiteral("9.9.9-test"));

    std::printf("console_split_sync_tests: the console's preview follows "
                "the rail and the mode switch\n");
    std::printf("=======================================================\n");

    testRailSwitchRebuildsTheHost();
    testSingleModeBuildsNoHost();
    testModeApplyMovesTheCanvas();

    std::printf("=======================================================\n");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
