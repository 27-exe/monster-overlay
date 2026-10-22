// ─── console_output_ownership_tests ─────────────────────────────────────
//
// WHAT THIS FILE ASSERTS, AND WHY IT NEEDS TWO REAL SCREENS.
//
// The user-approved contract (reference.html:421, and the parent's
// OUTPUT-OWNERSHIP-DECISION.md) is:
//
//     the viewer (the SCREEN dropdown / #display-select) is PRESENTATION
//     only, and a window's effective target is
//         own output  →  parent panel's output  →  primary
//
// Every one of the four defects this file was written for is a place where
// the VIEWER leaked into one of the three answers above, or where the
// readouts stopped describing the picture. None of them can be observed on
// one screen: with a single output, "the display I am looking at" and "the
// display this window lives on" are the same string by accident, and the
// assertions pass vacuously. So this fixture injects the reporter's own
// two-monitor topology through Qt's QPA seam (QWindowSystemInterface +
// QPlatformScreen), exactly as tests/main_output_wiring_tests.cpp and
// tests/console_scope_persistence_tests.cpp already do:
//
//     DP-1       2560x1440  NOT primary
//     HDMI-A-1   1080x1920  primary, PORTRAIT
//
// ISOLATION. HOME and XDG_CONFIG_HOME point at a QTemporaryDir and
// QSettings::setPath() is redirected BEFORE QApplication is constructed,
// so every read and every write in this file lands in a scratch directory.
// The developer's real ~/.config/monster-overlay/panels.ini is never
// opened. RESOURCE_LOCK "panels-ini" in CMakeLists keeps the sibling
// panel targets from racing the same default path.
//
// WHAT IT DELIBERATELY DOES NOT DO: start the game, start the overlay, or
// touch Steam. Everything is the console process itself.
//
// MUTATIONS (what turning each one red looks like):
//   * push the CANVAS's preview screen into the sub-window fallback again
//     → §2 (clamp follows the viewer) and §4 (saved ml off its own screen)
//   * drop refreshStageHead() from the screenCombo handler
//     → §3 (both chips stale after a viewer switch)
//   * drop the panel-view filter from HudCanvas::layoutSubWindows
//     → §5 (the board paints boxes the chip does not count)
//   * drop the scope from refreshStageHead's count
//     → §5 again, from the chip's side

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMargins>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>

#include <cstdio>

#include "core/string_table.h"
#include "ui/console_fidelity_windows.h"
#include "ui/control_panel.h"
#include "ui/hud_canvas.h"
#include "ui/panel.h"
#include "ui/panel_settings_game.h"

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

void note(const QString &s) { std::printf("NOTE: %s\n", qPrintable(s)); }

void pump(int ms = 120)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// ── the reporter's topology ────────────────────────────────────────────
const char *kPrimaryName = "HDMI-A-1";   // 1080x1920, primary, portrait
const char *kOtherName   = "DP-1";       // 2560x1440, NOT primary

class FakeOutput : public QPlatformScreen
{
public:
    FakeOutput(const QString &name, const QRect &g) : m_name(name), m_g(g) {}
    QString name() const override { return m_name; }
    QRect geometry() const override { return m_g; }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }

private:
    QString m_name;
    QRect m_g;
};

// The NON-primary goes in first so that asking for the primary second
// leaves the offscreen platform's own nameless screen off the primary
// slot — see tests/console_scope_persistence_tests.cpp:261.
void installFakeOutputs()
{
    QWindowSystemInterface::handleScreenAdded(
        new FakeOutput(QString::fromLatin1(kOtherName), QRect(0, 0, 2560, 1440)),
        /*isPrimary*/ false);
    QWindowSystemInterface::handleScreenAdded(
        new FakeOutput(QString::fromLatin1(kPrimaryName), QRect(0, 0, 1080, 1920)),
        /*isPrimary*/ true);
    pump(80);
}

QScreen *screenByName(const QString &name)
{
    if (name.isEmpty())
        return nullptr;
    for (QScreen *s : QGuiApplication::screens())
        if (s && s->name() == name)
            return s;
    return nullptr;
}

QString screenLabelOf(const QString &name)
{
    QScreen *s = screenByName(name);
    if (!s)
        return name;
    return QStringLiteral("%1 · %2×%3")
        .arg(name)
        .arg(s->geometry().width())
        .arg(s->geometry().height());
}

// ── small widget probes ────────────────────────────────────────────────
QComboBox *screenCombo(ControlPanel &cp)
{
    return cp.findChild<QComboBox *>(QStringLiteral("screenCombo"));
}

int indexOfOutput(QComboBox *c, const QString &name)
{
    return c ? c->findData(name) : -1;
}

// The REAL control, driven through its own signal: this is the user
// picking a display in the topbar, not a call to setPreviewScreen().
void pickViewer(ControlPanel &cp, const QString &name)
{
    QComboBox *c = screenCombo(cp);
    if (!c)
        return;
    const int at = indexOfOutput(c, name);
    if (at < 0 || at == c->currentIndex())
        return;
    c->setCurrentIndex(at);
    pump(150);
}

QString viewerOf(const ControlPanel &, HudCanvas *canvas)
{
    return canvas ? canvas->previewScreen() : QString();
}

int chipCount(ControlPanel &cp)
{
    QLabel *chip = cp.findChild<QLabel *>(QStringLiteral("stageWindowCountChip"));
    if (!chip)
        return -1;
    QString digits;
    for (const QChar ch : chip->text()) {
        if (ch.isDigit())
            digits += ch;
        else if (!digits.isEmpty())
            break;
    }
    return digits.isEmpty() ? -1 : digits.toInt();
}

QString screenChipText(ControlPanel &cp)
{
    QLabel *chip = cp.findChild<QLabel *>(QStringLiteral("stageScreenChip"));
    return chip ? chip->text() : QString();
}

// The board's own picture: how many boxes the last paint left standing,
// ACROSS BOTH ARMS OF THE BOARD. A window it filtered out keeps the empty
// rect each arm resets at the top of paintEvent — not painted, not
// hit-testable — so this is the number of boxes a user can see, which is
// the number the chip must read.
//
// v1.0.0 (OUTPUT-CONTRACT-COMPLETION): the four-slot arm counts too. It
// used to be sub-windows only, which was true in Split (where no mother
// is painted) and false everywhere else: in Single the whole picture IS
// the four-slot arm, and a Rise Split overview has exactly one mother box
// (pets — the panel with no split table). Counting only the sub half made
// every "boxes == chip" assertion below compare the chip to a PART of the
// board.
int paintedBoxes(HudCanvas *canvas)
{
    if (!canvas)
        return -1;
    canvas->grab();               // force a real paint pass (offscreen)
    int n = 0;
    for (const QRectF &r : canvas->subWindowRects())
        if (!r.isEmpty())
            ++n;
    for (const QRectF &r : canvas->motherWindowRects())
        if (!r.isEmpty())
            ++n;
    return n;
}

// The four-slot arm's share alone, with the panel each painted box
// belongs to (read back from paintEvent's own rects, so a box counted
// here is a box the click handler hit-tests). `paintedPanels` receives
// one bool per slot; pass nullptr when only the total matters.
int paintedMotherBoxes(HudCanvas *canvas, QVector<bool> *paintedPanels)
{
    if (!canvas)
        return -1;
    canvas->grab();
    const QVector<QRectF> rects = canvas->motherWindowRects();
    if (paintedPanels) {
        paintedPanels->clear();
        for (const QRectF &r : rects)
            paintedPanels->append(!r.isEmpty());
    }
    int n = 0;
    for (const QRectF &r : rects)
        if (!r.isEmpty())
            ++n;
    return n;
}

void realClick(QWidget *w, const QPointF &p)
{
    if (!w)
        return;
    const QPointF global = w->mapToGlobal(p.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, p, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, p, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QApplication::sendEvent(w, &release);
}

void clickNavCard(ControlPanel &cp, const char *objectName)
{
    QFrame *card = cp.findChild<QFrame *>(QString::fromLatin1(objectName));
    if (!card) {
        note(QStringLiteral("nav card %1 missing").arg(QLatin1String(objectName)));
        return;
    }
    realClick(card, QPointF(qMax(1.0, card->width() / 2.0),
                            qMax(1.0, card->height() / 2.0)));
    pump(150);
}

// ── the model snapshot: everything "ownership" means ───────────────────
struct WinSnap
{
    QString key;
    QString own, resolved;
    QMargins margins;
    bool panelEnabled, visible;
};

QVector<WinSnap> snapWindows(ControlPanel &cp)
{
    QVector<WinSnap> out;
    for (const mhw::WindowState &w : cp.buildWindowStates()) {
        WinSnap s;
        s.key = w.key;
        s.own = w.outputOwn;
        s.resolved = w.outputResolved;
        s.margins = w.margins;
        s.panelEnabled = w.panelEnabled;
        s.visible = w.visiblePersisted;
        out.push_back(s);
    }
    return out;
}

QString snapText(const QVector<WinSnap> &v)
{
    QString s;
    for (const WinSnap &w : v)
        s += w.key + QLatin1Char('|') + w.own + QLatin1Char('|')
             + w.resolved + QLatin1Char('|')
             + QStringLiteral("%1,%2,%3,%4").arg(w.margins.left())
                     .arg(w.margins.top())
                     .arg(w.margins.right())
                     .arg(w.margins.bottom())
             + QLatin1Char('|') + (w.panelEnabled ? '1' : '0')
             + (w.visible ? '1' : '0') + QLatin1Char('\n');
    return s;
}

QString readFileBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// The horizontal edge the corner actually binds, and the number the bound
// must equal on the window's OWN screen: screen width minus the panel's
// own extent. marginLimits() derives exactly that (panel.cpp:1457-1465).
int horizontalBound(Panel *p)
{
    if (!p)
        return -1000;
    const QMargins m = p->marginLimits();
    switch (p->corner()) {
    case Corner::TopLeft:
    case Corner::BottomLeft:
        return m.left();
    default:
        return m.right();
    }
}

int ownScreenWidth(Panel *p, const QString &resolved)
{
    if (!p)
        return -1;
    QScreen *s = screenByName(resolved);
    if (!s)
        return -1;
    return s->geometry().width() - p->width();
}

// The number of windows a viewer should see, spelled from the model —
// the same three questions mhw::windowOnDisplay() answers plus the view
// scope reference.html:442 applies in the panel view only.
//
// v1.0.0 (OUTPUT-CONTRACT-COMPLETION): THE SCOPE IS THE PANEL, NOT THE
// ROW SHAPE. `d.panel === state.view` does not care whether the row is a
// split row or a mother row, and the board's four-slot arm now honours
// it too — so scoping only `subIndex >= 0` rows would have counted a
// Single-mode mother the board does not paint (and, in Rise Split, a
// pets mother in someone else's panel view).
int expectedCount(const mhw::WindowList &list, const QString &screen,
                  bool overview, int scope)
{
    int n = 0;
    for (const mhw::WindowState &w : list) {
        if (!mhw::windowOnDisplay(w, screen))
            continue;
        if (!overview && scope >= 0 && w.panelIndex != scope)
            continue;
        ++n;
    }
    return n;
}

// The same predicate restricted to the four-slot arm's rows (subIndex
// < 0), i.e. the number of MOTHER boxes this view should paint. In Split
// that is 0 everywhere except the one panel with no split table; in
// Single it is the whole picture. Spelled separately so a test can pin
// each arm rather than only their sum.
int expectedMotherCount(const mhw::WindowList &list, const QString &screen,
                        bool overview, int scope)
{
    int n = 0;
    for (const mhw::WindowState &w : list) {
        if (w.subIndex >= 0)
            continue;
        if (!mhw::windowOnDisplay(w, screen))
            continue;
        if (!overview && scope >= 0 && w.panelIndex != scope)
            continue;
        ++n;
    }
    return n;
}

QString modelScreenName(HudCanvas *canvas)
{
    // refreshStageHead()'s own three-step fallback, restated here so an
    // assertion never depends on reading the private chain it mirrors.
    QString screenName = canvas ? canvas->previewScreen() : QString();
    if (screenName.isEmpty())
        if (QScreen *s = QGuiApplication::primaryScreen())
            screenName = s->name();
    return screenName;
}

// ── the real controls these later sections drive ───────────────────────
//
// Both are user gestures, not setters: the game segment runs the whole
// rebuild a user's switch runs, and the topbar layout segment commits
// through applyLayoutChoice(). A section that drove an internal setter
// would be testing the model against itself.
void clickGameButton(ControlPanel &cp, const QString &key)
{
    const QString want = mhw::StringTable::instance().tr(key);
    for (QPushButton *button : cp.findChildren<QPushButton *>()) {
        if (button->objectName() == QLatin1String("gameBtn")
            && button->text() == want) {
            button->click();
            pump(400);
            return;
        }
    }
    ++failures;
    std::printf("FAIL: game button for %s not found\n", qPrintable(key));
}

// Returns the segment's checked state after the click, so a caller can
// assert the mode that actually committed rather than the one it asked
// for (applyLayoutChoice() refuses only when the overlay child is
// running, which it never is in this fixture).
bool clickLayoutButton(ControlPanel &cp, const char *objectName)
{
    QPushButton *b =
        cp.findChild<QPushButton *>(QString::fromLatin1(objectName));
    if (!b) {
        ++failures;
        std::printf("FAIL: layout segment %s not found\n", objectName);
        return false;
    }
    b->click();
    pump(400);
    return b->isChecked();
}

// The digits a count label prints ("6 窗" -> 6), -1 when there are none.
int digitsIn(const QString &text)
{
    QString digits;
    for (const QChar ch : text) {
        if (ch.isDigit())
            digits += ch;
        else if (!digits.isEmpty())
            break;
    }
    return digits.isEmpty() ? -1 : digits.toInt();
}

// The rail row's own count label — the prototype's `[data-count]`, which
// is a DIFFERENT statement from the stage chip (per-panel vs whole view)
// and must be the same model underneath.
QString navCountOf(ControlPanel &cp, const char *cardName)
{
    QFrame *card =
        cp.findChild<QFrame *>(QString::fromLatin1(cardName));
    QLabel *label =
        card ? card->findChild<QLabel *>(QStringLiteral("navCount")) : nullptr;
    return label ? label->text() : QString();
}

// ── the contract, one (view × viewer) cell at a time ───────────────────
struct ViewCase
{
    bool overview;
    const char *card;      // nav card objectName
    const char *what;
};

// Drives a REAL nav card, then — for BOTH injected outputs — checks the
// three-way agreement this whole file is about:
//
//   chip   == expectedCount(model, this display, this view scope)
//   boxes  == chip                  (PAINTED, both arms of the board)
//   mothers == expectedMotherCount  (the four-slot arm's own share)
//
// plus the ghost half of the scope claim: in a panel view no four-slot
// box OUTSIDE the selected panel may be painted, because an unpainted
// rect is an un-hittable one (paintEvent resets every lastTarget_, and
// the press handler tests that same rect).
void sweepViews(ControlPanel &cp, HudCanvas *canvas, const char *label,
                const QVector<ViewCase> &cases)
{
    const QString viewers[2] = {QString::fromLatin1(kPrimaryName),
                                QString::fromLatin1(kOtherName)};
    for (const ViewCase &c : cases) {
        clickNavCard(cp, c.card);
        const bool isOverview = canvas->overviewMode();
        check(isOverview == c.overview,
              "the real nav card put the console in the view it names",
              QStringLiteral("%1 | %2 | overview=%3")
                  .arg(QLatin1String(label), QLatin1String(c.what))
                  .arg(isOverview));
        int scope = -1;
        if (!isOverview) {
            QFrame *card = cp.findChild<QFrame *>(
                QString::fromLatin1(c.card));
            scope = card ? card->property("panelIndex").toInt() : -1;
            check(card != nullptr && scope >= 0,
                  "…and that card names the panel the board must scope to",
                  QStringLiteral("%1 -> panelIndex=%2")
                      .arg(QLatin1String(c.card))
                      .arg(scope));
        }
        for (const QString &viewer : viewers) {
            pickViewer(cp, viewer);
            const mhw::WindowList list = cp.buildWindowStates();
            const QString screen = modelScreenName(canvas);
            const int expect = expectedCount(list, screen, isOverview, scope);
            const int expectMothers =
                expectedMotherCount(list, screen, isOverview, scope);
            const int chip = chipCount(cp);
            const int boxes = paintedBoxes(canvas);
            QVector<bool> paintedPanels;
            const int mothers = paintedMotherBoxes(canvas, &paintedPanels);
            const QString cell =
                QStringLiteral("%1 | %2 | viewer=%3 screen=%4")
                    .arg(QLatin1String(label), QLatin1String(c.what),
                         viewer, screen);
            check(chip == expect,
                  "the chip is the model under this display + view scope",
                  QStringLiteral("%1 -> chip=%2 model=%3")
                      .arg(cell, QString::number(chip),
                           QString::number(expect)));
            check(boxes == chip,
                  "…and the board paints EXACTLY the boxes the chip counts",
                  QStringLiteral("%1 -> boxes=%2 chip=%3")
                      .arg(cell, QString::number(boxes),
                           QString::number(chip)));
            check(mothers == expectMothers,
                  "…the four-slot arm alone paints exactly its model rows",
                  QStringLiteral("%1 -> mothers=%2 model=%3")
                      .arg(cell, QString::number(mothers),
                           QString::number(expectMothers)));
            int ghosts = 0;
            if (!isOverview && scope >= 0)
                for (int i = 0; i < paintedPanels.size(); ++i)
                    if (paintedPanels[i] && i != scope)
                        ++ghosts;
            check(ghosts == 0,
                  "…and no four-slot box outside the selected panel was "
                  "painted (so none is hittable)",
                  QStringLiteral("%1 -> %2 stray box(es)")
                      .arg(cell, QString::number(ghosts)));
        }
    }
}
} // namespace

int main(int argc, char *argv[])
{
    // ── isolation, before anything can latch a path ─────────────────────
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

    // Pin the game axis BEFORE the first ControlPanel: without a saved
    // `game` row the console calls detectGame(), which scans /proc, and a
    // machine with Rise mapped would put every write in [rise/…] while
    // this file reads [world/…]. Same reason, same fix as
    // console_scope_persistence_tests.cpp:587.
    {
        QSettings gameScope;
        gameScope.setValue(QStringLiteral("game"), QStringLiteral("world"));
        gameScope.sync();
    }

    std::printf("== console-output-ownership-tests ==\n");
    std::printf("(private 2-output fixture; the viewer is presentation only)\n\n");

    // The conf row that puts this console into Split — the topology the
    // four defects were measured in (eleven sub-window boxes).
    {
        const QString path = home.path()
            + QStringLiteral("/monster-overlay/monster-overlay.conf");
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream out(&f);
            out.setEncoding(QStringConverter::Utf8);
            out << QStringLiteral("layout=split\n");
            out.flush();
        }
    }

    const QString iniPath =
        home.path() + QStringLiteral("/monster-overlay/panels.ini");

    // EXPLICIT configuration, written before the console is built:
    //   * the player mother names DP-1        → its six split windows follow
    //   * the monster mother names HDMI-A-1   → its split windows follow
    //   * the damage mother names nothing     → follows the chain's tail
    //   * player-mantles names its OWN output → the own step, which must
    //     outrank both parent and viewer
    //   * everything else has no row at all   → pure defaults
    {
        QSettings s(iniPath, QSettings::IniFormat);
        s.clear();
        s.beginGroup(QStringLiteral("world/split/player"));
        s.setValue(QStringLiteral("outputName"), QStringLiteral("DP-1"));
        s.endGroup();
        s.beginGroup(QStringLiteral("world/split/monster"));
        s.setValue(QStringLiteral("outputName"), QStringLiteral("HDMI-A-1"));
        s.endGroup();
        s.beginGroup(QStringLiteral("world/split/player-mantles"));
        s.setValue(QStringLiteral("outputName"), QStringLiteral("DP-1"));
        s.setValue(QStringLiteral("ml"), 111);
        s.setValue(QStringLiteral("mt"), 222);
        s.setValue(QStringLiteral("mr"), 333);
        s.setValue(QStringLiteral("mb"), 444);
        s.endGroup();
        s.sync();
    }

    // ── 1. THE FIXTURE IS REAL ─────────────────────────────────────────
    installFakeOutputs();
    const QString iniBefore = readFileBytes(iniPath);

    {
        const int n = int(QGuiApplication::screens().size());
        check(n >= 2, "the private two-output session is installed",
              QStringLiteral("%1 screens").arg(n));
        QScreen *primary = QGuiApplication::primaryScreen();
        check(primary && primary->name() == QLatin1String(kPrimaryName),
              "the PRIMARY is the portrait HDMI-A-1 — the inversion that makes "
              "a viewer/ownership mix-up observable",
              primary ? primary->name() : QStringLiteral("(none)"));
        check(screenByName(QLatin1String(kOtherName)) != nullptr,
              "and the landscape DP-1 is attached too");
        if (primary)
            note(QStringLiteral("geometry: %1 %2×%3 (primary) / %4")
                     .arg(primary->name())
                     .arg(primary->geometry().width())
                     .arg(primary->geometry().height())
                     .arg(screenLabelOf(QLatin1String(kOtherName))));
    }

    ControlPanel cp;
    cp.show();
    pump(400);

    HudCanvas *canvas = cp.findChild<HudCanvas *>();
    check(canvas != nullptr, "the console built its board");
    if (!canvas)
        return 1;
    check(canvas->consoleFidelityMode(),
          "the board is in fidelity mode (the v1.0.0 path under test)");
    check(canvas->layoutMode() == mhw::PlayerSection::PlayerLayoutMode::Split,
          "the conf row put the console in Split — the topology with eleven "
          "sub-window boxes");
    check(indexOfOutput(screenCombo(cp), QLatin1String(kOtherName)) >= 0
              && indexOfOutput(screenCombo(cp), QLatin1String(kPrimaryName)) >= 0,
          "the SCREEN dropdown offers both injected outputs by name");

    mhw::WindowList list = cp.buildWindowStates();
    check(!list.isEmpty(), "the window model has rows",
          QStringLiteral("%1 windows").arg(list.size()));

    // Seed the console onto the World split domain explicitly, in case the
    // ctor's own game detection landed elsewhere on this machine.
    if (canvas->layoutMode() != mhw::PlayerSection::PlayerLayoutMode::Split
        || list.isEmpty()) {
        note(QStringLiteral("fixture did not reach Split — model empty"));
    }

    // ── 2. THE CHAIN: own → parent → primary, and the VIEWER ABSENT ────
    std::printf("\n-- 2. the ownership chain, on both viewers --\n");
    {
        // A follow window per parent, plus the one that names its own.
        QString followKey, ownKey;
        QString followResolved, ownResolved;
        for (const mhw::WindowState &w : list) {
            if (w.subIndex < 0)
                continue;
            if (ownKey.isEmpty() && !w.outputOwn.isEmpty()) {
                ownKey = w.key;
                ownResolved = w.outputResolved;
            }
            if (followKey.isEmpty()
                && w.outputOwn.isEmpty()
                && w.outputResolved != QLatin1String(kPrimaryName)) {
                followKey = w.key;
                followResolved = w.outputResolved;
            }
        }
        check(!followKey.isEmpty(),
              "a window that FOLLOWS its parent (own output empty) exists to probe",
              QStringLiteral("resolved=%1").arg(followResolved));
        check(!ownKey.isEmpty(),
              "and a window that NAMES its own output exists to probe",
              QStringLiteral("key=%1 own=%2").arg(ownKey, ownResolved));

        // DIAGNOSTIC: what the console actually pushed into each window.
        {
            QStringList rows;
            for (const mhw::WindowState &w : list) {
                if (w.subIndex < 0)
                    continue;
                Panel *pp = cp.splitPreviewWindow(w.key);
                rows << QStringLiteral("%1(parent %2): fallback='%3' own='%4' resolved='%5'")
                            .arg(w.key).arg(w.panelIndex)
                            .arg(pp ? pp->previewFallbackOutput() : QString())
                            .arg(w.outputOwn, w.outputResolved);
            }
            note(rows.join(QLatin1Char('\n')));
        }

        Panel *follow = followKey.isEmpty() ? nullptr
                                            : cp.splitPreviewWindow(followKey);
        Panel *own = ownKey.isEmpty() ? nullptr : cp.splitPreviewWindow(ownKey);
        if (follow) {
            // The core regression: the bound must be a function of the
            // window's own chain and of NOTHING the viewer can change.
            for (const QString &viewer : {QString::fromLatin1(kPrimaryName),
                                          QString::fromLatin1(kOtherName)}) {
                pickViewer(cp, viewer);
                note(QStringLiteral("%1: bound=%2 ownScreen=%3 viewer=%4 "
                                    "previewFallback='%5'")
                         .arg(followKey)
                         .arg(horizontalBound(follow))
                         .arg(ownScreenWidth(follow, followResolved))
                         .arg(viewer, follow->previewFallbackOutput()));
            }
            pickViewer(cp, QString::fromLatin1(kOtherName));
            const int onOther = horizontalBound(follow);
            pickViewer(cp, QString::fromLatin1(kPrimaryName));
            const int onPrimary = horizontalBound(follow);
            check(onOther == onPrimary,
                  "P1: a Follow window's clamp does NOT move with the viewer",
                  QStringLiteral("viewer=%1 -> %2, viewer=%3 -> %4")
                      .arg(QLatin1String(kOtherName)).arg(onOther)
                      .arg(QLatin1String(kPrimaryName)).arg(onPrimary));
            const int want = ownScreenWidth(follow, followResolved);
            check(want > 0 && onPrimary == want,
                  "…and it is the bound of the window's OWN resolved screen "
                  "(own → parent → primary), not of the screen being previewed",
                  QStringLiteral("bound=%1 own-screen=%2 resolved=%3")
                      .arg(onPrimary).arg(want).arg(followResolved));
        }
        if (own) {
            // An explicit own screen stays independent of the parent AND of
            // the viewer — reference.html:421's first step.
            pickViewer(cp, QString::fromLatin1(kOtherName));
            const int a = horizontalBound(own);
            pickViewer(cp, QString::fromLatin1(kPrimaryName));
            const int b = horizontalBound(own);
            const int want = ownScreenWidth(own, ownResolved);
            check(a == b && want > 0 && a == want,
                  "an EXPLICIT own window's bound matches its own screen on "
                  "both viewers (own outranks parent and viewer)",
                  QStringLiteral("own=%1 resolved=%2 bound=%3/%4 screen=%5")
                      .arg(own->outputName(), ownResolved)
                      .arg(a).arg(b).arg(want));
        }

        // The chain, stated per window, on every model row: the resolved
        // answer must be own → parent → primary and never the viewer.
        //
        // The PARENT's output is read out of panels.ini rather than out of
        // buildWindowStates(): in Split the model has no mother row (that
        // absence is exactly what §5's board/chip invariant is about), so
        // the file the console itself reads is the only way a test can
        // state step 2 independently of the code under test.
        static const char *kMotherKey[4] = { "player", "monster",
                                             "damage", "pets" };
        auto parentOutputOf = [&iniPath](int panelIndex) -> QString {
            if (panelIndex < 0 || panelIndex > 3)
                return QString();
            QSettings ini(iniPath, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("world/split/")
                           + QLatin1String(kMotherKey[panelIndex]));
            const QString v = ini.value(QStringLiteral("outputName")).toString();
            ini.endGroup();
            return v;
        };
        int chainOk = 0, chainBad = 0;
        for (const mhw::WindowState &w : list) {
            const QString parentOutput =
                w.subIndex >= 0 ? parentOutputOf(w.panelIndex) : QString();
            const QString want = !w.outputOwn.isEmpty()
                ? w.outputOwn
                : (!parentOutput.isEmpty() ? parentOutput : w.primaryOutput);
            if (w.outputResolved == want)
                ++chainOk;
            else {
                ++chainBad;
                note(QStringLiteral("%1: resolved='%2' want='%3'")
                         .arg(w.key, w.outputResolved, want));
            }
            // The viewer may never be the answer: if resolved equals the
            // display being previewed while matching none of the three
            // legal sources, the view leaked into the ownership chain.
            if (!w.outputOwn.isEmpty() || !parentOutput.isEmpty())
                if (w.outputResolved == canvas->previewScreen()
                    && w.outputResolved == w.primaryOutput
                    && w.outputResolved != w.outputOwn
                    && w.outputResolved != parentOutput)
                    ++chainBad;
        }
        check(chainBad == 0,
              "every window's outputResolved is own → parent → primary "
              "(the viewer contributes nothing)",
              QStringLiteral("%1 ok / %2 wrong").arg(chainOk).arg(chainBad));
    }

    // ── 3. SWITCHING THE VIEWER WRITES NOTHING ─────────────────────────
    std::printf("\n-- 3. viewer switches are view-only --\n");
    {
        const QString modelBefore = snapText(snapWindows(cp));
        const QString ini3 = readFileBytes(iniPath);

        // Overview first (the console's own starting view), then the panel
        // view reached through the REAL nav card.
        if (!canvas->overviewMode())
            clickNavCard(cp, "navOverview");
        check(canvas->overviewMode(),
              "the console is in the overview before the switch");
        pickViewer(cp, QString::fromLatin1(kOtherName));
        pickViewer(cp, QString::fromLatin1(kPrimaryName));
        pickViewer(cp, QString::fromLatin1(kOtherName));

        clickNavCard(cp, "navPlayer");
        check(!canvas->overviewMode(),
              "a real click on the player nav card leaves the overview");
        pickViewer(cp, QString::fromLatin1(kPrimaryName));
        pickViewer(cp, QString::fromLatin1(kOtherName));

        check(snapText(snapWindows(cp)) == modelBefore,
              "P1/B1: no window's outputOwn / outputResolved / margins / "
              "enabled / visible moved when the viewer did");
        check(readFileBytes(iniPath) == ini3,
              "…and panels.ini is byte-identical — the viewer is not persisted");
        check(readFileBytes(iniPath) == iniBefore,
              "…and still byte-identical to the seed, i.e. nothing at all "
              "was flushed during the switches");
    }

    // ── 4. REAL KEY EVENTS CANNOT WRITE PAST THE WINDOW'S OWN SCREEN ───
    std::printf("\n-- 4. real QKeyEvents + a real save --\n");
    {
        // A player sub-window: TopLeft corner, so `left` is the bound the
        // corner actually binds (panel.cpp:1461) and the arrow key moves it.
        QString key;
        int subIdx = -1, panelIdx = -1;
        QString resolved;
        for (const mhw::WindowState &w : list) {
            if (w.subIndex >= 0 && w.panelIndex == 0
                && w.outputOwn.isEmpty()) {
                key = w.key;
                subIdx = w.subIndex;
                panelIdx = w.panelIndex;
                resolved = w.outputResolved;
                break;
            }
        }
        Panel *p = key.isEmpty() ? nullptr : cp.splitPreviewWindow(key);
        if (p && subIdx >= 0) {
            const QMargins seed = p->margins();
            const int ownMax = ownScreenWidth(p, resolved);
            const int idxOwn = indexOfOutput(screenCombo(cp), resolved);
            const int idxElse =
                (idxOwn == indexOfOutput(screenCombo(cp),
                                          QLatin1String(kOtherName)))
                    ? indexOfOutput(screenCombo(cp), QLatin1String(kPrimaryName))
                    : indexOfOutput(screenCombo(cp), QLatin1String(kOtherName));

            auto nudge = [&](int comboIndex) -> QMargins {
                if (comboIndex >= 0) {
                    screenCombo(cp)->setCurrentIndex(comboIndex);
                    pump(200);
                }
                p->setMargins(seed, /*persist=*/false);
                cp.selectWindow(panelIdx, subIdx);
                pump(120);
                canvas->setEditing(true);
                pump(80);
                for (int i = 0; i < 400; ++i) {
                    QKeyEvent e(QEvent::KeyPress, Qt::Key_Right,
                                Qt::ShiftModifier);
                    QApplication::sendEvent(canvas, &e);
                    if ((i % 100) == 99)
                        QCoreApplication::processEvents();
                }
                pump(150);
                canvas->setEditing(false);
                return p->margins();
            };

            const QMargins onOwnViewer = nudge(idxOwn);
            const QMargins onElseViewer = nudge(idxElse);
            note(QStringLiteral("%1 (resolved %2): after 400×Shift+Right, "
                                "left=%3 with the viewer on its own screen, "
                                "left=%4 with the viewer elsewhere; its own "
                                "screen allows %5")
                     .arg(key, resolved)
                     .arg(onOwnViewer.left()).arg(onElseViewer.left())
                     .arg(ownMax));

            check(onOwnViewer.left() == onElseViewer.left(),
                  "P1: through REAL QKeyEvents the stored margin no longer "
                  "depends on which screen the VIEWER is on",
                  QStringLiteral("own-viewer=%1 other-viewer=%2")
                      .arg(onOwnViewer.left()).arg(onElseViewer.left()));
            check(ownMax > 0 && onElseViewer.left() <= ownMax,
                  "…and the value stored with the viewer elsewhere still fits "
                  "the window's OWN screen (the off-screen write is gone)",
                  QStringLiteral("stored=%1 own-screen max=%2")
                      .arg(onElseViewer.left()).arg(ownMax));

            // The same claim through the write the console performs on
            // save / close: ControlPanel::saveAllSplitAndBaseWindows().
            p->setMargins(seed, /*persist=*/false);
            if (idxElse >= 0) {
                screenCombo(cp)->setCurrentIndex(idxElse);
                pump(200);
            }
            cp.selectWindow(panelIdx, subIdx);
            pump(120);
            canvas->setEditing(true);
            pump(80);
            for (int i = 0; i < 400; ++i) {
                QKeyEvent e(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
                QApplication::sendEvent(canvas, &e);
                if ((i % 100) == 99)
                    QCoreApplication::processEvents();
            }
            pump(150);
            canvas->setEditing(false);
            const int inMemory = p->margins().left();
            cp.saveAllSplitAndBaseWindows();
            pump(250);

            int flushed = -1000000;
            {
                QSettings s(iniPath, QSettings::IniFormat);
                s.beginGroup(QStringLiteral("world/split/") + key);
                if (s.contains(QStringLiteral("ml")))
                    flushed = s.value(QStringLiteral("ml")).toInt();
                s.endGroup();
            }
            note(QStringLiteral("saved ml=%1 for %2 (own screen max %3)")
                     .arg(flushed).arg(key).arg(ownMax));
            check(ownMax > 0 && flushed <= ownMax,
                  "P1: the ml the console flushed is inside the window's OWN "
                  "resolved screen — the 1480px off-screen write cannot happen",
                  QStringLiteral("ml=%1 own-screen max=%2").arg(flushed).arg(ownMax));
            check(inMemory == flushed,
                  "…and what was flushed is exactly what was in memory",
                  QStringLiteral("memory=%1 file=%2").arg(inMemory).arg(flushed));
            p->setMargins(seed, /*persist=*/false);
        } else {
            check(false, "a player sub-window with an empty own output exists",
                  QStringLiteral("key=%1 sub=%2").arg(key).arg(subIdx));
        }
    }

    // ── 5. CHIP == BOARD, for every view × every viewer ────────────────
    std::printf("\n-- 5. the window-count chip equals the painted boxes --\n");
    {
        // reference.html:442 applies the display filter in BOTH views and
        // the panel scope only in the panel view; the chip and the board
        // must both implement that, and must agree on the answer.
        struct Case { bool overview; const char *navCard; const char *what; };
        const Case cases[] = {
            { true,  "navOverview", "overview" },
            { false, "navPlayer",   "panel view (player)" },
            { false, "navMonster",  "panel view (monster)" },
            { false, "navDamage",   "panel view (damage)" },
        };
        for (const Case &c : cases) {
            clickNavCard(cp, c.navCard);
            pump(120);
            const bool isOverview = canvas->overviewMode();
            check(isOverview == c.overview,
                  "the nav card switched the view as asked",
                  QStringLiteral("%1 overview=%2").arg(c.what).arg(int(isOverview)));
            for (const QString &viewer : {QString::fromLatin1(kOtherName),
                                          QString::fromLatin1(kPrimaryName)}) {
                pickViewer(cp, viewer);
                list = cp.buildWindowStates();
                const QString screen = modelScreenName(canvas);
                // The scope is the panel the card selected, not a literal 0
                // for every card — recover it from the card's own property
                // the console stamps on it (control_panel.cpp:4373).
                int realScope = -1;
                if (!isOverview) {
                    if (QFrame *card = cp.findChild<QFrame *>(
                            QString::fromLatin1(c.navCard)))
                        realScope = card->property("panelIndex").toInt();
                }
                const int expect = expectedCount(
                    list, screen, isOverview,
                    realScope >= 0 ? realScope : 0);
                const int chip = chipCount(cp);
                const int boxes = paintedBoxes(canvas);
                const QString screenChip = screenChipText(cp);

                note(QStringLiteral("%1 / viewer %2: chip=%3 boxes=%4 model=%5 "
                                    "screenChip='%6'")
                         .arg(QLatin1String(c.what), viewer)
                         .arg(chip).arg(boxes).arg(expect).arg(screenChip));

                check(screenChip.startsWith(viewer),
                      "P2: the screen chip names the display just picked "
                      "(it is refreshed by the switch, not by a later event)",
                      QStringLiteral("chip='%1' viewer=%2").arg(screenChip, viewer));
                check(chip == expect,
                      "…and the count chip equals the model under the same "
                      "display + view filter",
                      QStringLiteral("chip=%1 model=%2").arg(chip).arg(expect));
                check(boxes == chip,
                      "P2/P3: the board paints EXACTLY the boxes the chip counts",
                      QStringLiteral("boxes=%1 chip=%2").arg(boxes).arg(chip));
            }
        }
        // Leave the console in a known state for §6.
        clickNavCard(cp, "navPlayer");
        pickViewer(cp, QString::fromLatin1(kOtherName));
    }

    // ── 6. A WINDOW'S OWN OUTPUT IS THE ONLY THING THAT MOVES IT ───────
    std::printf("\n-- 6. explicit own output, and only that --\n");
    {
        // The inspector's 目标显示器 control (objectName `metaOutput`) is
        // reference.html:491/:501 — it writes the SELECTED WINDOW's own
        // output and nothing else. Driving the real combo rather than a
        // private setter is what makes the isolation claim about the
        // console's own write path.
        QString key;
        int subIdx = -1, panelIdx = -1;
        QString beforeOwn, siblingKey;
        for (const mhw::WindowState &w : list) {
            if (w.subIndex >= 0 && w.panelIndex == 0 && w.outputOwn.isEmpty()) {
                if (key.isEmpty()) {
                    key = w.key;
                    subIdx = w.subIndex;
                    panelIdx = w.panelIndex;
                    beforeOwn = w.outputOwn;
                } else {
                    siblingKey = w.key;   // same parent, different window
                    break;
                }
            }
        }
        QComboBox *mo = cp.findChild<QComboBox *>(QStringLiteral("metaOutput"));
        if (key.isEmpty() || siblingKey.isEmpty() || !mo || mo->count() < 2) {
            check(false,
                  "a follow window, a sibling under the same parent, and the "
                  "inspector's 目标显示器 combo all exist to probe",
                  QStringLiteral("key=%1 sibling=%2 combo=%3")
                      .arg(key, siblingKey)
                      .arg(mo ? mo->count() : -1));
        } else {
            pickViewer(cp, QString::fromLatin1(kOtherName));
            const QString viewerBefore = viewerOf(cp, canvas);
            QString siblingBefore;
            for (const mhw::WindowState &w : cp.buildWindowStates())
                if (w.key == siblingKey)
                    siblingBefore = w.outputResolved;

            cp.selectWindow(panelIdx, subIdx);
            pump(150);
            const int wantIdx = mo->findData(
                QString::fromLatin1(kPrimaryName));
            const int at = wantIdx >= 0 ? wantIdx
                                        : (mo->currentIndex() + 1) % mo->count();
            mo->setCurrentIndex(at);
            pump(250);

            QString ownAfter, resolvedAfter, siblingAfter;
            for (const mhw::WindowState &w : cp.buildWindowStates()) {
                if (w.key == key) {
                    ownAfter = w.outputOwn;
                    resolvedAfter = w.outputResolved;
                } else if (w.key == siblingKey) {
                    siblingAfter = w.outputResolved;
                }
            }
            check(ownAfter != beforeOwn && !ownAfter.isEmpty(),
                  "P1: the write landed on that one window's own output",
                  QStringLiteral("was '%1' now '%2'").arg(beforeOwn, ownAfter));
            check(resolvedAfter == ownAfter,
                  "…and its resolved target followed its OWN output "
                  "(own outranks parent and viewer)",
                  QStringLiteral("own=%1 resolved=%2").arg(ownAfter, resolvedAfter));
            check(viewerOf(cp, canvas) == viewerBefore,
                  "…while the VIEWER stayed where it was (a window write is "
                  "not a view change)",
                  QStringLiteral("viewer=%1 was=%2")
                      .arg(viewerOf(cp, canvas), viewerBefore));
            check(siblingAfter == siblingBefore,
                  "…and a SIBLING under the same parent was not dragged along "
                  "— the parent's output is untouched by a child's write",
                  QStringLiteral("%1: '%2' -> '%3'")
                      .arg(siblingKey, siblingBefore, siblingAfter));

            // The bound follows the window's own screen now, on both viewers.
            if (Panel *p = cp.splitPreviewWindow(key)) {
                const int want = ownScreenWidth(p, resolvedAfter);
                pickViewer(cp, QString::fromLatin1(kOtherName));
                const int a = horizontalBound(p);
                pickViewer(cp, QString::fromLatin1(kPrimaryName));
                const int b = horizontalBound(p);
                check(want > 0 && a == want && b == want,
                      "…and the retargeted window's clamp is its own screen's "
                      "on both viewers",
                      QStringLiteral("bound=%1/%2 own screen=%3")
                          .arg(a).arg(b).arg(want));
            }

            // Restore, so a re-run of this binary starts from the seed.
            cp.selectWindow(panelIdx, subIdx);
            pump(120);
            mo->setCurrentIndex(mo->findData(QString()));
            pump(200);
        }
    }

    // ── 7. THE FOLLOW CLAMP, IN BOTH DIRECTIONS ───────────────────────────
    //
    // §4 measured ONE direction: a window whose own screen is the wide
    // DP-1 while the viewer sat on the narrow HDMI-A-1. Clamping against
    // the viewer there is merely too STRICT — the value it refuses is one
    // the user could never have wanted either. The other direction is the
    // one that WRITES OFF THE SCREEN: a window that belongs to the narrow
    // HDMI-A-1 with the viewer on the wide DP-1, where the viewer's bound
    // (2560 - extent) admits a value the window's own screen (1080 -
    // extent) does not. Both are now driven end to end — real QKeyEvents
    // to the bound, then the real save — and the bound is COMPUTED from
    // the attached screen's geometry and the window's own extent (scale
    // included), never copied from a number a previous run printed.
    std::printf("\n-- 7. the follow clamp, both directions --\n");
    {
        struct Dir
        {
            int panel;
            const char *own;      // the screen the window RESOLVES to
            const char *other;    // the screen that must NOT bound it
            const char *what;
        };
        const Dir dirs[] = {
            { 1, kPrimaryName, kOtherName,
              "parent=HDMI-A-1 (narrow) / viewer=DP-1 (wide)" },
            { 0, kOtherName, kPrimaryName,
              "parent=DP-1 (wide) / viewer=HDMI-A-1 (narrow)" },
        };
        for (const Dir &d : dirs) {
            const mhw::WindowList now = cp.buildWindowStates();
            QString key, resolved;
            int subIdx = -1;
            for (const mhw::WindowState &w : now) {
                if (w.subIndex >= 0 && w.panelIndex == d.panel
                    && w.outputOwn.isEmpty()) {
                    key = w.key;
                    subIdx = w.subIndex;
                    resolved = w.outputResolved;
                    break;
                }
            }
            Panel *p = key.isEmpty() ? nullptr : cp.splitPreviewWindow(key);
            if (!p || subIdx < 0) {
                check(false, "a follow window exists under this parent",
                      QStringLiteral("panel=%1 key=%2")
                          .arg(d.panel).arg(key));
                continue;
            }
            check(resolved == QLatin1String(d.own),
                  "…and it resolves to its PARENT's screen (chain, not viewer)",
                  QStringLiteral("%1 -> resolved=%2 want=%3")
                      .arg(key, resolved, QLatin1String(d.own)));

            // THE BOUND, DERIVED: the attached screen's geometry minus the
            // window's own extent at its own scale. Two independent
            // spellings of the extent (the widget's size, which is what
            // Panel::marginLimits() uses, and contentSize×scale, which is
            // what it is built from) must agree, or the fixture is lying
            // about the window it is measuring.
            const QScreen *ownScreen = screenByName(resolved);
            const QScreen *otherScreen =
                screenByName(QLatin1String(d.other));
            const int extent = qRound(p->contentSize().width() * p->scale());
            const int limit = ownScreen
                ? ownScreen->geometry().width() - extent : -1;
            const int otherLimit = otherScreen
                ? otherScreen->geometry().width() - extent : -1;
            check(ownScreen && otherScreen && p->size().width() == extent,
                  "the window's widget extent is contentSize×scale, and both "
                  "screens are measurable",
                  QStringLiteral("%1: widget=%2 content×scale=%3")
                      .arg(key).arg(p->size().width()).arg(extent));
            check(limit > 0 && otherLimit > 0 && otherLimit != limit,
                  "the two screens give DIFFERENT bounds, so this direction "
                  "is observable (not a tautology)",
                  QStringLiteral("%1 own=%2 %3=%4")
                      .arg(key).arg(limit)
                      .arg(QLatin1String(d.other)).arg(otherLimit));

            // WHICH COMPONENT BOUNDS IT: the anchored horizontal edge, and
            // therefore which arrow key walks toward the bound (a right
            // corner's margin shrinks when the window moves right).
            const bool boundIsLeft =
                (p->corner() == Corner::TopLeft
                 || p->corner() == Corner::BottomLeft);
            const Qt::Key walkKey = boundIsLeft ? Qt::Key_Right
                                                : Qt::Key_Left;
            const char *component = boundIsLeft ? "ml" : "mr";
            const QMargins seed = p->margins();

            auto walkToBound = [&](const QString &viewer) -> int {
                const int at = indexOfOutput(screenCombo(cp), viewer);
                if (at >= 0) {
                    screenCombo(cp)->setCurrentIndex(at);
                    pump(200);
                }
                p->setMargins(seed, /*persist=*/false);
                cp.selectWindow(d.panel, subIdx);
                pump(120);
                canvas->setEditing(true);
                pump(80);
                for (int i = 0; i < 400; ++i) {
                    QKeyEvent e(QEvent::KeyPress, walkKey, Qt::ShiftModifier);
                    QApplication::sendEvent(canvas, &e);
                    if ((i % 100) == 99)
                        QCoreApplication::processEvents();
                }
                pump(150);
                canvas->setEditing(false);
                return boundIsLeft ? p->margins().left()
                                   : p->margins().right();
            };

            // The dangerous viewer: the one whose screen is NOT this
            // window's. Its value must still stop at the window's own
            // bound — that is the off-screen write, and this is the
            // direction §4 could not see.
            const QString otherViewer = QLatin1String(d.other);
            const QString ownViewer = QLatin1String(d.own);
            const int withOtherViewer = walkToBound(otherViewer);
            const int withOwnViewer = walkToBound(ownViewer);
            note(QStringLiteral("%1 [%2]: %3=%4 after 400 key events on the "
                                "other viewer, %5 on its own; own screen "
                                "allows %6, %7 allows %8")
                     .arg(QLatin1String(d.what), key,
                          QLatin1String(component))
                     .arg(withOtherViewer).arg(withOwnViewer)
                     .arg(limit).arg(QLatin1String(d.other)).arg(otherLimit));

            check(withOtherViewer == limit,
                  "the value stops EXACTLY at this window's own screen bound "
                  "even when the VIEWER's screen would allow more",
                  QStringLiteral("%1 %2=%3 own bound=%4 other bound=%5")
                      .arg(key, QLatin1String(component))
                      .arg(withOtherViewer).arg(limit).arg(otherLimit));
            check((withOtherViewer < otherLimit) == (limit < otherLimit),
                  "the observed bound follows the target screen in both "
                  "directions, whether tighter or wider than the viewer",
                  QStringLiteral("value=%1 own bound=%2 viewer bound=%3")
                      .arg(withOtherViewer).arg(limit).arg(otherLimit));
            check(withOwnViewer == withOtherViewer,
                  "…and which screen the VIEWER is on changes nothing",
                  QStringLiteral("own-viewer=%1 other-viewer=%2")
                      .arg(withOwnViewer).arg(withOtherViewer));

            // THE REAL SAVE, READ BACK FROM THIS WINDOW'S OWN GROUP.
            p->setMargins(seed, /*persist=*/false);
            const int at = indexOfOutput(screenCombo(cp), otherViewer);
            if (at >= 0) {
                screenCombo(cp)->setCurrentIndex(at);
                pump(200);
            }
            cp.selectWindow(d.panel, subIdx);
            pump(120);
            canvas->setEditing(true);
            pump(80);
            for (int i = 0; i < 400; ++i) {
                QKeyEvent e(QEvent::KeyPress, walkKey, Qt::ShiftModifier);
                QApplication::sendEvent(canvas, &e);
                if ((i % 100) == 99)
                    QCoreApplication::processEvents();
            }
            pump(150);
            canvas->setEditing(false);
            const int inMemory = boundIsLeft ? p->margins().left()
                                             : p->margins().right();
            cp.saveAllSplitAndBaseWindows();
            pump(250);
            int flushed = -1000000;
            {
                QSettings s(iniPath, QSettings::IniFormat);
                s.beginGroup(QStringLiteral("world/split/") + key);
                if (s.contains(QLatin1String(component)))
                    flushed = s.value(QLatin1String(component)).toInt();
                s.endGroup();
            }
            note(QStringLiteral("saved %1=%2 for %3 (own screen bound %4)")
                     .arg(QLatin1String(component))
                     .arg(flushed).arg(key).arg(limit));
            check(flushed == inMemory && flushed <= limit,
                  "the flushed value is exactly the in-memory one AND inside "
                  "this window's own screen",
                  QStringLiteral("%1=%2 memory=%3 bound=%4")
                      .arg(QLatin1String(component))
                      .arg(flushed).arg(inMemory).arg(limit));
            check(flushed == limit && ((flushed < otherLimit) == (limit < otherLimit)),
                  "saved geometry reaches the TARGET screen bound, not the "
                  "viewer screen bound, in both screen-size directions",
                  QStringLiteral("flushed=%1 own bound=%2 viewer bound=%3")
                      .arg(flushed).arg(limit).arg(otherLimit));
            p->setMargins(seed, /*persist=*/false);
        }
    }

    // ── 8. THE SINGLE PANEL VIEW'S SCOPE (World) ─────────────────────────
    //
    // reference.html:442's `d.panel === state.view` applies to the ONE
    // list visibleWindows() builds, and that list's rows in Single ARE the
    // mother panels — so a panel view must paint ONE of them, not all of
    // them. The overview is the cross-panel picture and paints every row
    // on this display; four views × two displays, all driven through real
    // nav-card clicks.
    std::printf("\n-- 8. Single: panel view scope, all views x both displays --\n");
    {
        check(clickLayoutButton(cp, "layoutSegSingle"),
              "the real LAYOUT segment commits Single");
        check(canvas->layoutMode()
                  == mhw::PlayerSection::PlayerLayoutMode::Single,
              "…and the board is in Single (sub-windows gone)");
        const QVector<ViewCase> cases = {
            { true,  "navOverview", "overview" },
            { false, "navPlayer",   "panel view (player)" },
            { false, "navMonster",  "panel view (monster)" },
            { false, "navDamage",   "panel view (damage)" },
        };
        sweepViews(cp, canvas, "world / single", cases);

        // The scope, spelled as paint rects rather than as a count: the
        // selected panel's box and no other's.
        // BLOCK-USABILITY-FIX: the sweep above LEAVES the viewer on the
        // last display it walked (HDMI-A-1), and the monster mother is
        // configured for its own resolved output. Navigation no longer
        // re-points the viewer — that is the new product contract — so
        // this positive paint check drives the SCREEN dropdown itself,
        // exactly as a user would, and pins that the switch moves the
        // VIEW ONLY: the ownership snapshot (own output, resolved
        // output, margins, flags) is byte-identical before and after.
        {
            const QVector<WinSnap> before = snapWindows(cp);
            QString resolved;
            for (const mhw::WindowState &w : cp.buildWindowStates())
                if (w.panelIndex == 1 && w.subIndex < 0
                    && !w.outputResolved.isEmpty()) {
                    resolved = w.outputResolved;
                    break;
                }
            check(!resolved.isEmpty(),
                  "the monster mother names the display it is configured for",
                  QStringLiteral("resolved='%1'").arg(resolved));
            pickViewer(cp, resolved);
            check(modelScreenName(canvas) == resolved,
                  "…and the SCREEN dropdown really moved the viewer there",
                  QStringLiteral("screen=%1 resolved=%2")
                      .arg(modelScreenName(canvas), resolved));
            check(snapText(snapWindows(cp)) == snapText(before),
                  "…switching the VIEWER changed no window's own output, "
                  "resolved output, margins or flags — view-only, as the "
                  "ownership contract says",
                  QStringLiteral("screen=%1").arg(modelScreenName(canvas)));
        }
        clickNavCard(cp, "navMonster");
        QVector<bool> painted;
        const int mothers = paintedMotherBoxes(canvas, &painted);
        check(mothers == 1 && painted.size() == mhw::kPanelCount
                  && painted[1] && !painted[0] && !painted[2],
              "the monster panel view paints EXACTLY the monster box "
              "(the other three have no box to hit)",
              QStringLiteral("mothers=%1 [player=%2 monster=%3 damage=%4]")
                  .arg(mothers)
                  .arg(painted.value(0)).arg(painted.value(1))
                  .arg(painted.value(2)));
        // And the count the rail reports is the same model.
        check(digitsIn(navCountOf(cp, "navPlayer")) == 1,
              "the rail's per-panel count in Single is one window per panel",
              navCountOf(cp, "navPlayer"));
    }

    // ── 9. RISE: the pets window, in Single ──────────────────────────────
    std::printf("\n-- 9. Rise: the pets window is a real row in Single --\n");
    mhw::WindowState petsFromSingle;
    bool havePetsFromSingle = false;
    {
        clickGameButton(cp, QStringLiteral("console.game.rise"));
        pump(400);
        const QVector<ViewCase> cases = {
            { true,  "navOverview", "overview" },
            { false, "navPlayer",   "panel view (player)" },
            { false, "navMonster",  "panel view (monster)" },
            { false, "navDamage",   "panel view (damage)" },
            { false, "navPets",     "panel view (pets)" },
        };
        sweepViews(cp, canvas, "rise / single", cases);

        const mhw::WindowList riseList = cp.buildWindowStates();
        for (const mhw::WindowState &w : riseList)
            if (w.panelIndex == 3) {
                petsFromSingle = w;
                havePetsFromSingle = true;
            }
        check(havePetsFromSingle && petsFromSingle.subIndex == -1,
              "Rise Single has the pets row, as a mother row",
              QStringLiteral("rows=%1 subIndex=%2")
                  .arg(riseList.size()).arg(petsFromSingle.subIndex));
        check(!petsFromSingle.title.isEmpty()
                  && petsFromSingle.title != petsFromSingle.key,
              "…named by the table's short-name key, not by the raw "
              "settingsKey (win.short.pet, not pet-damage)",
              QStringLiteral("key=%1 title=%2")
                  .arg(petsFromSingle.key, petsFromSingle.title));
        check(digitsIn(navCountOf(cp, "navPets")) == 1,
              "…and the rail's pets row counts it", navCountOf(cp, "navPets"));

        // The pets view's own box, spelled as paint.
        // BLOCK-USABILITY-FIX: same as section 8 — the sweep leaves the
        // viewer on its last display, the pets mother resolves to its own
        // output, and the paint check below therefore drives the SCREEN
        // dropdown to that display first. The switch is view-only and is
        // asserted as such; no product code changes to make an old
        // assumption true.
        {
            const QVector<WinSnap> before = snapWindows(cp);
            QString resolved;
            for (const mhw::WindowState &w : riseList)
                if (w.panelIndex == 3 && !w.outputResolved.isEmpty()) {
                    resolved = w.outputResolved;
                    break;
                }
            check(!resolved.isEmpty(),
                  "the pets mother names the display it is configured for",
                  QStringLiteral("resolved='%1'").arg(resolved));
            pickViewer(cp, resolved);
            check(modelScreenName(canvas) == resolved,
                  "…and the SCREEN dropdown really moved the viewer there",
                  QStringLiteral("screen=%1 resolved=%2")
                      .arg(modelScreenName(canvas), resolved));
            check(snapText(snapWindows(cp)) == snapText(before),
                  "…switching the VIEWER changed no window's own output, "
                  "resolved output, margins or flags — view-only, as the "
                  "ownership contract says",
                  QStringLiteral("screen=%1").arg(modelScreenName(canvas)));
        }
        clickNavCard(cp, "navPets");
        QVector<bool> painted;
        const int mothers = paintedMotherBoxes(canvas, &painted);
        check(mothers == 1 && painted.value(3),
              "the pets panel view paints the pets box",
              QStringLiteral("mothers=%1 panel3=%2")
                  .arg(mothers).arg(painted.value(3)));
    }

    // ── 10. RISE: the same window, in Split ──────────────────────────────
    //
    // PetDamageSection has NO split table, so the split host has no rows
    // for panel 3 and the four-slot arm is the only arm that can draw it.
    // The row must exist in the model (or the overview, the chip, the rail
    // and the inspector cannot see the window the overlay mounts), it must
    // be painted in the overview, it must NOT leak into another panel's
    // view, and every field of it must be the SAME Panel's — not a copy of
    // some other window's config.
    std::printf("\n-- 10. Rise Split: the pets row, painted and scoped --\n");
    {
        check(clickLayoutButton(cp, "layoutSegSplit"),
              "the real LAYOUT segment commits Split");
        check(canvas->layoutMode()
                  == mhw::PlayerSection::PlayerLayoutMode::Split,
              "…and the board is back in Split");

        const mhw::WindowList splitList = cp.buildWindowStates();
        int petsRows = 0, motherRows = 0;
        mhw::WindowState pets;
        for (const mhw::WindowState &w : splitList) {
            if (w.subIndex >= 0)
                continue;
            ++motherRows;
            if (w.panelIndex == 3) {
                pets = w;
                ++petsRows;
            }
        }
        check(motherRows == 1 && petsRows == 1 && pets.subIndex == -1,
              "Rise Split has EXACTLY ONE mother row — the pets window "
              "(no split table) — and no mother row for the split panels",
              QStringLiteral("rows=%1 mothers=%2 pets=%3")
                  .arg(splitList.size()).arg(motherRows).arg(petsRows));
        // v1.1.5: eleven rows minus the one this game does not have —
        // 衣装 is World-only, so a Rise host mounts ten split windows.
        // The model lists exactly what the host mounts (that is the
        // v1.1.3 contract this file pins), so the total is eleven rows:
        // ten split windows + the pets row.
        check(splitList.size() == 11,
              "ten split windows (eleven rows minus Rise-absent 衣装) + the "
              "pets row = eleven rows",
              QStringLiteral("%1").arg(splitList.size()));
        check(havePetsFromSingle
                  && pets.key == petsFromSingle.key
                  && pets.title == petsFromSingle.title
                  && pets.margins == petsFromSingle.margins
                  && pets.scale == petsFromSingle.scale
                  && pets.opacity == petsFromSingle.opacity
                  && pets.outputOwn == petsFromSingle.outputOwn
                  && pets.outputResolved == petsFromSingle.outputResolved
                  && pets.panelEnabled == petsFromSingle.panelEnabled
                  && pets.visiblePersisted == petsFromSingle.visiblePersisted,
              "…and every field of that row is the SAME Panel read as in "
              "Single — nothing is fabricated or copied for Split",
              QStringLiteral("key=%1 title=%2 own=%3 resolved=%4")
                  .arg(pets.key, pets.title, pets.outputOwn,
                       pets.outputResolved));

        // The sweep: overview + all five panel views × both displays.
        const QVector<ViewCase> cases = {
            { true,  "navOverview", "overview" },
            { false, "navPlayer",   "panel view (player)" },
            { false, "navMonster",  "panel view (monster)" },
            { false, "navDamage",   "panel view (damage)" },
            { false, "navPets",     "panel view (pets)" },
        };
        sweepViews(cp, canvas, "rise / split", cases);
        // The preceding sweep ends on DP-1, which legitimately contains
        // no follow-primary pets/player windows in this fixture.
        pickViewer(cp, QLatin1String(kPrimaryName));

        // WHICH box, not just how many — the three split mothers must stay
        // off the board (their windows are the sub boxes) and the pets box
        // must be exactly where its view says it is.
        clickNavCard(cp, "navOverview");
        QVector<bool> painted;
        int n = paintedMotherBoxes(canvas, &painted);
        check(n == 1 && painted.value(3) && !painted.value(0)
                  && !painted.value(1) && !painted.value(2),
              "the Rise Split OVERVIEW paints the pets box — the window the "
              "old model omitted from the overview entirely",
              QStringLiteral("mothers=%1 [player=%2 monster=%3 damage=%4 "
                             "pets=%5]")
                  .arg(n).arg(painted.value(0)).arg(painted.value(1))
                  .arg(painted.value(2)).arg(painted.value(3)));

        clickNavCard(cp, "navPlayer");
        n = paintedMotherBoxes(canvas, &painted);
        int splitBoxes = 0;
        for (const QRectF &r : canvas->subWindowRects())
            if (!r.isEmpty())
                ++splitBoxes;
        check(n == 0 && splitBoxes > 0,
              "the player panel view paints ONLY its split boxes — the "
              "pets mother does not leak into it",
              QStringLiteral("mothers=%1 split=%2").arg(n).arg(splitBoxes));

        clickNavCard(cp, "navPets");
        n = paintedMotherBoxes(canvas, &painted);
        splitBoxes = 0;
        for (const QRectF &r : canvas->subWindowRects())
            if (!r.isEmpty())
                ++splitBoxes;
        check(n == 1 && painted.value(3) && splitBoxes == 0,
              "and the pets panel view paints exactly one box — the pets "
              "mother, with no split box of any other panel",
              QStringLiteral("mothers=%1 split=%2").arg(n).arg(splitBoxes));

        // The rail agrees with the model it is supposed to read from.
        check(digitsIn(navCountOf(cp, "navPets")) == 1,
              "the rail counts the Split pets window once (it used to say 0 "
              "窗 with the window on the board)",
              navCountOf(cp, "navPets"));
        // v1.1.5: five, not six — the rail counts the windows the board
        // actually mounts, and a Rise host does not mount 衣装 (World-only).
        check(digitsIn(navCountOf(cp, "navPlayer")) == 5,
              "the rail still counts the player split windows Rise mounts "
              "(five: 衣装 is World-only)",
              navCountOf(cp, "navPlayer"));
    }

    // ── 11. BOUNDARIES: what was NOT checked ─────────────────────────────
    note(QStringLiteral("not exercised here: overview read-only behaviour, "
                        "hot-plug (an output unplugged mid-session), the "
                        "overlay mount side in src/main.cpp, and any "
                        "single-screen (one output) fixture — those stay "
                        "with their own suites."));

    std::printf("\n== %d checks, %d failures ==\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
