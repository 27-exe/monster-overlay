#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QMargins>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QWindow>

#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>

#include "ui/panel.h"
#include "ui/panel_source.h"
#include "ui/hud_canvas.h"
#include "ui/panel_settings_game.h"
#include "ui/screen_query.h"

#include <cstdio>

// 「显示器示例/窗口落到没有接的 DP 上」 — the IDENTITY half of
// acceptance item 8, and the seam the console and the canvas both call
// on every drag event: screen_query::effectiveOutputName().
//
// THREE RULES, each asserted against the production function:
//
//   1. An output Qt has RIGHT NOW comes back VERBATIM. The stored name
//      is the user's data (their panels.ini still says DP-1 even though
//      the dock is unplugged) and nothing may normalise it on read.
//   2. Offline or empty -> Qt's primary (eDP-1, the 内屏), falling to
//      the first live named screen when Qt reports no usable primary.
//      This is the rule that stops the overlay mounting on a phantom
//      output after a hot-unplug.
//   3. No outputs at all -> a deterministic empty string. Never a
//      guess, never a synthesised `screen@x,y` label.
//
// Plus the two properties the DRAG HOT PATH depends on:
//
//   * it must spawn NOTHING. detect() shells out to
//     hyprctl/xrandr/wlr-randr/kscreen-doctor with a 2 s timeout — fine
//     once per screen change, fatal per mouse-move. The PATH is
//     replaced by stub scripts that drop a marker file, so any
//     subprocess at all turns this check red.
//   * it must keep up. 5000 calls are timed with a very generous
//     ceiling; a QProcess spawn per call cannot stay under it.
//
// AND the Panel-side half of item 8: a stored outputName survives both
// being offline and the topology changing underneath it — the window
// re-resolves to the primary, the file keeps the intent, and the
// run-time clamp never writes a clamped margin back over the stored
// one (Panel::loadConfig reads verbatim, Panel::onOutputsChanged only
// pushes).
//
// Every QSettings write lands in a QTemporaryDir: HOME and
// XDG_CONFIG_HOME are redirected BEFORE the first QSettings exists, and
// the test refuses to construct a Panel if the resolved path is not
// inside the sandbox — the user's real panels.ini is never touched.
//
// MUTATION: make effectiveOutputName() return primaryScreen() even for
// an online request, and check 1 (verbatim) turns red; make it shell
// out through runCommand() and check the PATH marker turns red.

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL  %s\n", what); }
    else std::printf("PASS: %s\n", what);
}
void check(bool ok, const char *what, const QString &detail)
{
    ++checks;
    if (!ok) {
        std::printf("  FAIL  %s [%s]\n", what, qPrintable(detail));
        ++failures;
    } else std::printf("PASS: %s [%s]\n", what, qPrintable(detail));
}
void pump()
{
    for (int i = 0; i < 40; ++i) QCoreApplication::processEvents();
}

// ── the reporter's topology through Qt's own QPA seam ────────────────
// Non-primary first, primary second — the ordering that keeps the
// offscreen platform's own nameless screen out of the primary slot
// (tests/split_output_clamp_tests.cpp:100-130, same fixture).
const char *kPrimaryName = "eDP-1";   // 2560x1600, primary, the 内屏
const char *kOtherName   = "DP-1";    // 2560x1440, NOT primary

class FakeOutput : public QPlatformScreen
{
public:
    FakeOutput(const QString &name, const QRect &g) : m_name(name), m_g(g) {}
    QString name() const override { return m_name; }
    QRect   geometry() const override { return m_g; }
    int     depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }

private:
    QString m_name;
    QRect   m_g;
};

// Owned by Qt from the moment handleScreenAdded() returns; the DP-1
// pointer is handed to handleScreenRemoved() afterwards and never
// touched again.
FakeOutput *g_dp = nullptr;
FakeOutput *g_edp = nullptr;

void installFakeOutputs()
{
    QWindowSystemInterface::handleScreenAdded(
        g_dp = new FakeOutput(QString::fromLatin1(kOtherName),
                              QRect(0, 0, 2560, 1440)),
        /*isPrimary*/ false);
    QWindowSystemInterface::handleScreenAdded(
        g_edp = new FakeOutput(QString::fromLatin1(kPrimaryName),
                               QRect(0, 0, 2560, 1600)),
        /*isPrimary*/ true);
    pump();
}

// A real Panel — not a mock. Panel is abstract only on paintPanel(),
// and no Q_OBJECT of its own is needed: the base metaobject carries.
class ProbePanel : public Panel
{
public:
    ProbePanel(const QString &key, Corner corner) : Panel(key, corner) {}
protected:
    void paintPanel(QPainter &) override {}
};

screen_query::OutputInfo mkOut(const char *name, bool primary)
{
    screen_query::OutputInfo o;
    o.name = QString::fromLatin1(name);
    o.geometry = QRect(0, 0, 2560, 1600);
    o.primary = primary;
    return o;
}

QList<screen_query::OutputInfo> livePair()
{
    return { mkOut(kPrimaryName, true), mkOut(kOtherName, false) };
}
} // namespace

int main(int argc, char *argv[])
{
    // Redirect BEFORE anything can build a QSettings: the backend may
    // cache the writable-config location per process.
    QTemporaryDir home;
    if (!home.isValid()) {
        std::fprintf(stderr, "cannot create a sandbox HOME\n");
        return 2;
    }
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());

    QApplication app(argc, argv);

    std::printf("=== effectiveOutputName: the identity contract ===\n");

    // ── 0. sandbox guard — never write the user's real panels.ini ─────
    QSettings panels(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("monster-overlay"),
                     QStringLiteral("panels"));
    const bool sandboxed = panels.fileName().startsWith(home.path());
    check(sandboxed, "panels.ini resolves inside the temporary sandbox",
          panels.fileName());
    if (!sandboxed) {
        // Refuse to continue: every remaining Panel assertion would
        // write the developer's own config to do it.
        std::printf("%d checks, %d failures\n", checks, failures + 1);
        return 1;
    }

    // ── 1. the TEST-OVERRIDE overload (pure, no QPA needed) ───────────
    using screen_query::effectiveOutputName;
    {
        check(effectiveOutputName(QStringLiteral("DP-1"), livePair())
                  == QStringLiteral("DP-1"),
              "an online output name is returned verbatim");
        check(effectiveOutputName(QStringLiteral("eDP-1"), livePair())
                  == QStringLiteral("eDP-1"),
              "an online primary name is returned verbatim");
        check(effectiveOutputName(QStringLiteral("HDMI-A-9"), livePair())
                  == QStringLiteral("eDP-1"),
              "an offline output falls back to the Qt primary");
        check(effectiveOutputName(QString(), livePair())
                  == QStringLiteral("eDP-1"),
              "an empty request falls back to the Qt primary");

        const QList<screen_query::OutputInfo> noDp = { mkOut(kPrimaryName, true) };
        check(effectiveOutputName(QStringLiteral("DP-1"), noDp)
                  == QStringLiteral("eDP-1"),
              "missing DP falls back to the internal screen");

        // No primary flagged (Qt mid-hotplug): first live NAMED screen.
        const QList<screen_query::OutputInfo> noPrimary = {
            mkOut(kOtherName, false), mkOut(kPrimaryName, false)
        };
        check(effectiveOutputName(QString(), noPrimary)
                  == QStringLiteral("DP-1"),
              "with no primary flagged, the first named screen wins");

        // A flagged-but-nameless primary must not beat a real name.
        const QList<screen_query::OutputInfo> namelessPrimary = {
            mkOut("", true), mkOut(kOtherName, false)
        };
        check(effectiveOutputName(QString(), namelessPrimary)
                  == QStringLiteral("DP-1"),
              "a nameless primary yields to the first named screen");

        // No outputs at all: deterministic empty, in every shape.
        const QList<screen_query::OutputInfo> none;
        check(effectiveOutputName(QString(), none).isEmpty(),
              "no outputs -> empty, deterministically");
        check(effectiveOutputName(QStringLiteral("DP-1"), none).isEmpty(),
              "no outputs beats an online request, not the other way");

        // Nameless screens only: empty, and NEVER a synthesised
        // `screen@x,y` — that label is listOutputs() presentation, and
        // no compositor matches it.
        const QList<screen_query::OutputInfo> nameless = { mkOut("", false) };
        const QString n = effectiveOutputName(QString(), nameless);
        check(n.isEmpty() && !n.contains(QStringLiteral("screen@")),
              "nameless screens yield empty, never a synthetic label");
    }

    // ── 2. the LIVE overload, no subprocess, no wall clock blow-up ─────
    //
    // PATH becomes four stub scripts that each drop a marker file. If
    // effectiveOutputName() ever routes through detect()/runCommand(),
    // the marker appears. Set up before the first live call.
    QTemporaryDir bin;
    QString marker = bin.filePath(QStringLiteral("spawned"));
    const QStringList tools = { QStringLiteral("hyprctl"),
                                QStringLiteral("xrandr"),
                                QStringLiteral("wlr-randr"),
                                QStringLiteral("kscreen-doctor") };
    for (const QString &t : tools) {
        QFile f(bin.filePath(t));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write("#!/bin/sh\n");
            f.write("echo x >> \"$MO_SPAWN_MARKER\"\n");
            f.close();
            f.setPermissions(QFile::permissions(f.fileName())
                             | QFile::ExeOwner | QFile::ExeUser);
        }
    }
    qputenv("MO_SPAWN_MARKER", marker.toUtf8());
    qputenv("PATH", bin.path().toUtf8());

    installFakeOutputs();

    const QString primaryName = QGuiApplication::primaryScreen()
        ? QGuiApplication::primaryScreen()->name() : QString();
    check(primaryName == QString::fromLatin1(kPrimaryName),
          "fixture: the fake eDP-1 is Qt's primary", primaryName);

    {
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 5000; ++i) {
            (void)effectiveOutputName(QStringLiteral("DP-1"));
            (void)effectiveOutputName(QString());
            (void)effectiveOutputName(QStringLiteral("HDMI-A-9"));
        }
        const qint64 ms = t.elapsed();
        check(!QFile::exists(marker),
              "15000 hot-path calls spawn no compositor tool");
        check(ms < 2000,
              "15000 hot-path calls stay well under 2s",
              QString::number(ms) + QStringLiteral("ms"));
    }

    // ── 3. LIVE semantics with the fakes installed ────────────────────
    check(effectiveOutputName(QStringLiteral("DP-1"))
              == QStringLiteral("DP-1"),
          "live: an attached DP-1 resolves to itself");
    check(effectiveOutputName(QString()) == primaryName,
          "live: empty resolves to the Qt primary");
    check(effectiveOutputName(QStringLiteral("HDMI-A-9")) == primaryName,
          "live: an output Qt has never heard of resolves to the primary");
    check(screen_query::listOutputs().size() >= 3,
          "fixture: eDP-1 + DP-1 + the offscreen screen are all listed");

    // ── 4. the PANEL half: intent is data, resolution is runtime ──────
    std::printf("\n=== Panel: stored output intent survives offline outputs ===\n");
    mhw::setPanelSettingsGame(mhw::GameId::World);

    auto seed = [&](const char *key, const char *out,
                    int ml, int mt, int mr, int mb) {
        panels.beginGroup(mhw::panelSettingsScopeGroup(
            QString::fromLatin1(key)));
        panels.setValue(QStringLiteral("outputName"), QString::fromLatin1(out));
        panels.setValue(QStringLiteral("ml"), ml);
        panels.setValue(QStringLiteral("mt"), mt);
        panels.setValue(QStringLiteral("mr"), mr);
        panels.setValue(QStringLiteral("mb"), mb);
        panels.endGroup();
        panels.sync();
    };
    auto storedValue = [&](const char *key, const QString &field) {
        panels.sync();
        panels.beginGroup(mhw::panelSettingsScopeGroup(
            QString::fromLatin1(key)));
        const QString v = panels.value(field).toString();
        panels.endGroup();
        return v;
    };
    auto storedInt = [&](const char *key, const QString &field) {
        panels.sync();
        panels.beginGroup(mhw::panelSettingsScopeGroup(
            QString::fromLatin1(key)));
        const int v = panels.value(field).toInt();
        panels.endGroup();
        return v;
    };

    seed("player", "DP-1", 123, 45, 67, 89);
    seed("monster", "HDMI-A-9", 7, 8, 9, 10);

    seed("fallback-test", "HDMI-A-9", 4200, 2200, 0, 0);
    ProbePanel fallbackPanel(QStringLiteral("fallback-test"), Corner::TopLeft);
    PanelSourceAdapter fallbackSource(&fallbackPanel);
    HudCanvas fallbackCanvas;
    fallbackCanvas.resize(1180, 820);
    fallbackCanvas.setConsoleFidelityMode(true);
    fallbackCanvas.bindPanel(0, &fallbackSource);
    QPixmap content(fallbackPanel.size());
    content.fill(Qt::green);
    fallbackCanvas.setPanelPixmap(0, content, true);
    fallbackCanvas.show();
    pump();
    fallbackCanvas.grab();
    const QRectF fallbackRect = fallbackCanvas.motherWindowRects().at(0);
    check(!fallbackRect.isEmpty() && fallbackRect.left() >= 0 && fallbackRect.top() >= 0
              && fallbackRect.right() <= fallbackCanvas.width()
              && fallbackRect.bottom() <= fallbackCanvas.height(),
          "offline 4K margins are clamped in the preview just like the live surface");
    check(fallbackSource.margins().left() == 4200
              && storedInt("fallback-test", QStringLiteral("ml")) == 4200,
          "preview clamping preserves the stored external-screen position");

    ProbePanel online(QStringLiteral("player"), Corner::TopLeft);
    ProbePanel missing(QStringLiteral("monster"), Corner::TopRight);

    QWindow *onlineWin = online.windowHandle();
    check(onlineWin && onlineWin->screen()
              && onlineWin->screen()->name() == QString::fromLatin1(kOtherName),
          "an attached outputName mounts the window on that output",
          onlineWin && onlineWin->screen() ? onlineWin->screen()->name()
                                           : QStringLiteral("<no screen>"));
    check(online.outputName() == QStringLiteral("DP-1"),
          "the panel still carries its own outputName");

    QWindow *missingWin = missing.windowHandle();
    check(missingWin && missingWin->screen()
              && missingWin->screen() == QGuiApplication::primaryScreen(),
          "a missing outputName re-resolves to the primary (内屏)",
          missingWin && missingWin->screen() ? missingWin->screen()->name()
                                             : QStringLiteral("<no screen>"));
    check(missing.outputName() == QStringLiteral("HDMI-A-9"),
          "resolving did not overwrite the missing intent");

    check(storedValue("player", QStringLiteral("outputName"))
              == QStringLiteral("DP-1"),
          "panels.ini still stores DP-1 for the online panel");
    check(storedValue("player", QStringLiteral("ml")) == QStringLiteral("123")
              && storedInt("player", QStringLiteral("mt")) == 45,
          "panels.ini margins untouched by construction");
    check(storedValue("monster", QStringLiteral("outputName"))
              == QStringLiteral("HDMI-A-9"),
          "panels.ini still stores the never-attached HDMI-A-9");
    check(storedInt("monster", QStringLiteral("ml")) == 7,
          "the missing panel's margins are untouched too");

    // ── 5. hot-unplug: same intent, new resolution, no write-back ─────
    std::printf("\n=== hot-unplug DP-1 ===\n");
    if (g_dp) {
        QWindowSystemInterface::handleScreenRemoved(g_dp);
        g_dp = nullptr;
        pump();
    }
    check(effectiveOutputName(QStringLiteral("DP-1")) == primaryName,
          "after unplug, the same stored name resolves to the primary");

    QWindow *afterWin = online.windowHandle();
    check(afterWin && afterWin->screen()
              && afterWin->screen()->name() == primaryName,
          "the mounted window re-resolves onto the 内屏 after unplug",
          afterWin && afterWin->screen() ? afterWin->screen()->name()
                                         : QStringLiteral("<no screen>"));
    check(online.outputName() == QStringLiteral("DP-1"),
          "the panel's intent survives the unplug in memory");
    check(online.margins() == QMargins(123, 45, 67, 89),
          "run-time re-resolution never writes a clamped margin back");
    check(storedValue("player", QStringLiteral("outputName"))
              == QStringLiteral("DP-1"),
          "panels.ini still stores DP-1 after the unplug");
    check(storedInt("player", QStringLiteral("ml")) == 123,
          "panels.ini margins still hold the pre-unplug intent");

    // A panel built NOW with DP-1 as its intent: same fallback.
    seed("damage", "DP-1", 11, 22, 33, 44);
    ProbePanel after(QStringLiteral("damage"), Corner::BottomRight);
    QWindow *afterNewWin = after.windowHandle();
    check(afterNewWin && afterNewWin->screen()
              && afterNewWin->screen()->name() == primaryName,
          "a panel created while DP-1 is offline mounts on the 内屏");
    check(after.outputName() == QStringLiteral("DP-1"),
          "creation while offline still keeps the stored intent");
    check(storedValue("damage", QStringLiteral("outputName"))
              == QStringLiteral("DP-1"),
          "creating a panel while offline rewrites nothing on disk");

    // ── 6. hot-plug back: the intent resnaps onto its output ──────────
    QWindowSystemInterface::handleScreenAdded(
        g_dp = new FakeOutput(QString::fromLatin1(kOtherName),
                              QRect(0, 0, 2560, 1440)),
        /*isPrimary*/ false);
    pump();
    check(effectiveOutputName(QStringLiteral("DP-1"))
              == QStringLiteral("DP-1"),
          "after replug, the stored name resolves to its output again");
    QWindow *backWin = online.windowHandle();
    check(backWin && backWin->screen()
              && backWin->screen()->name() == QString::fromLatin1(kOtherName),
          "the mounted window snaps back onto DP-1 after replug");
    check(storedValue("player", QStringLiteral("outputName"))
              == QStringLiteral("DP-1"),
          "the whole round trip left panels.ini byte-identical");
    (void)g_edp;

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
