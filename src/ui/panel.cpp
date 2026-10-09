#include "panel.h"

#include "panel_accent.h"
#include "panel_settings_game.h"
#include "panel_sizing.h"

#include <LayerShellQt/Window>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QDebug>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QScreen>
#include <QTimer>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kMinScale = 0.5;
constexpr double kMaxScale = 3.0;
constexpr double kScaleStep = 0.1;
constexpr int kBorderWidth = 2;
// The panel body's corner radius, named because two functions now draw
// it: drawV03Chrome() composes it with the stripe and the border, and
// drawSubWindowBody() draws it on its own for a window whose frame has
// been switched off. Two copies of one literal is exactly how the body
// of a fragment drifts out of roundness with the panel it came from.
constexpr double kChromeRadius = 2.0;

// ─── the panels.ini handle ────────────────────────────────────────────
//
// THIS USED TO BE A FUNCTION-LOCAL STATIC. It was not, and the reason it
// was not is measured, not theorised. A two-process probe (one process
// writes 100, a second writes 777, the first reads again) printed:
//
//     PARENT before child : shared=100 fresh=777
//     PARENT after  child : shared=100 fresh=777
//     file on disk        : ml=777
//     VERDICT: the static handle is STALE, a fresh handle is not
//
// A `static QSettings` snapshots the file ONCE, at construction. It only
// re-reads on sync() — and sync() is not called on the read path, so
// after any other process touches panels.ini this object answers from
// its own snapshot for the rest of its life. That is precisely the
// console/overlay split this project has: the console and the overlay
// are separate processes writing the same file, so the static guarantee
// was "cheap object reuse" traded against "every value I read may be a
// lie", and the file on disk was never wrong.
//
// The failure was not merely cosmetic. The console's
// saveAllSplitAndBaseWindows() wrote its stale members back over the
// overlay's real layout on exit, so the overlay's positions were
// destroyed rather than ignored.
//
// THE FIX: this returns a QSettings& that is re-synced from disk on every
// call. Two properties matter:
//
//   1. Reference return, so `settings().beginGroup(g); ... settings().endGroup()`
//      keeps working. beginGroup() and endGroup() must land on the SAME
//      object or the group state is lost, and that pattern is spelled
//      this way in four places in this file. A value-returning
//      `QSettings settings()` would have made every one of them a bug.
//   2. The object is not rebuilt per call: QSettings' destructor flushes,
//      and rebuilding per call would make a destructor's flush a write
//      path. So the object lives across calls (thread_local, matching
//      the fact that panels are constructed on the GUI thread) and is
//      REFRESHED by an explicit sync() at each access.
//
// The sync() cost is one INI re-read per access, which is why this is
// only called from the configuration paths (hasStoredPosition,
// loadConfig, saveConfig, saveAppearance) and never from paintEvent or
// any other repaint path — see the hot-path note in Panel::paintEvent.
//
// Cost of the static was one object construction per process. Cost of
// the re-sync is one file read per access. Both are irrelevant next to
// a configuration read that must be TRUE.
QSettings &settings()
{
    // thread_local, not static: the file is the same, but a future
    // non-GUI reader (a worker thread doing a config load) must not
    // share group state with the GUI thread. Qt's own QSettings is not
    // thread-safe for concurrent use of one instance.
    thread_local QSettings s(QSettings::IniFormat, QSettings::UserScope,
                             QStringLiteral("monster-overlay"),
                             QStringLiteral("panels"));
    // The whole fix. QSettings caches the file contents in memory and
    // only re-reads on sync(); a plain `return s;` is exactly the stale
    // static that just cost the user their layout. This re-read makes
    // the next value()/contains() see whatever another process has
    // written since we last looked.
    s.sync();
    return s;
}

// ─── the game-scoped settings namespace (v0.14) ──────────────────────
//
// Panels used to all live in ONE flat namespace. One `[player-status]`
// section, one `[player-wirebug]` section, and no way to say which
// game they were tuned for. The user found the result immediately: a
// single preview showed 衣装 (World-only) and 翔虫 (Rise-only) at the
// same time, because both games' data landed in the same section and
// both split windows read it.
//
// So every section became per-game: `[world/player-status]` under World,
// `[rise/player-status]` under Rise, in the SAME file. Why group
// prefixing rather than a file per game, and why the game is injected
// rather than asked of Panel — panel_settings_game.h argues both.
//
// v0.14 added the SECOND axis. Per-game fixed the games from each other
// but left one game's single-mode and split-mode geometry in the same
// section, and the user found that immediately too: 「双方布局会互相
// 影响」. So the namespace is now GAME × MODE —
// `[world/single/player-status]`, `[world/split/player-status]`,
// `[rise/single/player-status]`, `[rise/split/player-status]` — and
// both axes are launch-time facts (main() resolves the game from --game
// / process detection and the mode from --layout, then calls
// mhw::setPanelSettingsGame / setPanelSettingsLayout before the first
// Panel exists).
//
// The un-prefixed spelling a pre-v0.13 file uses, and the two-domain
// spelling a pre-v0.14 file uses, are both migrated ONCE, on the first
// panel to read them — mhw::migrateLegacyPanelSection, called from
// Panel::loadConfig below. Both land in SINGLE, because single is the
// only topology that existed when they were written. The migration's
// guarantees and the test that pins them are in
// panel_settings_game.h.
QString scopedKey(const QString &key)
{
    // v0.14: the group is now GAME × MODE. The two globals are read
    // here, at the one place the concatenation is spelled, so a
    // Panel cannot resolve its key against a stale half of the pair.
    return mhw::panelSettingsScopeGroup(key);
}


// Default margin (offset from the anchored edges) per corner. Chosen
// so the three panels (player=top-left, monster=top-right,
// damage=bottom-right) do not overlap on a typical screen.
QMargins defaultMarginsFor(Corner corner)
{
    switch (corner) {
    case Corner::TopLeft:     return QMargins(20, 20, 0, 0);
    case Corner::TopRight:    return QMargins(0, 20, 20, 0);
    case Corner::BottomLeft:  return QMargins(20, 0, 0, 20);
    case Corner::BottomRight: return QMargins(0, 0, 20, 20);
    }
    return QMargins(20, 20, 0, 0);
}

// ─── First-run placement of a sub-window ───────────────────────────────
//
// Every split window used to hard-code Corner::TopLeft, and a TopLeft
// default is (20,20) — SAME FOR ALL OF THEM. So a first run stacked
// eleven windows on one pixel. The user's own panels.ini says it: four
// windows with identical `ml=20 mt=20`, three of them byte-identical,
// which is the fingerprint of "never been dragged, using the default".
//
// The fix is a cascade, not a sprinkle of hand-picked coordinates. A
// hand-picked set is right at one resolution and wrong at every other,
// and it cannot answer "why is THIS window HERE". A cascade answers both
// questions from the table the split is already driven by:
//
//   sub-window i of parent P sits offset from P by (i * kCascadeStepX,
//   i * kCascadeStepY)
//
// which is exactly the relation the split expresses ("this block was
// carved out of that panel") and which needs no screen size, no output
// enumeration, and no per-window magic. The step is deliberately larger
// than any sub-window's own margin so two neighbours never touch, and
// small enough that six of them stay within a 1080p screen.
//
// It is a DEFAULT only. A stored value still wins outright — this path is
// reached only when the section has no `ml`/`mt` at all, so a user who has
// ever dragged a window keeps their layout, and a user who has not gets
// something that looks arranged rather than something that looks broken.
//
// `indexInParent` is the row of the window in its own split table, so it
// is 0 for the first sub-window of each parent. That means different
// parents cascade from their own origin — a monster sub-window does not
// inherit the player cascade's offset — and it is what keeps two clusters
// from drifting on top of one another as rows are added.
constexpr int kCascadeStepX = 24;
constexpr int kCascadeStepY = 24;

QMargins defaultMarginsForSubWindow(Corner corner, int indexInParent)
{
    QMargins m = defaultMarginsFor(corner);
    if (indexInParent <= 0)
        return m;
    const int dx = indexInParent * kCascadeStepX;
    const int dy = indexInParent * kCascadeStepY;
    // Only the two axes the corner actually READS get the cascade: a
    // TopLeft panel is placed by its left+top margins, and the other two
    // components are unused. Adding to them would be dead arithmetic that
    // looks like a position and saves into the file as noise.
    switch (corner) {
    case Corner::TopLeft:
        m.setLeft(m.left() + dx);
        m.setTop(m.top() + dy);
        break;
    case Corner::TopRight:
        m.setRight(m.right() + dx);
        m.setTop(m.top() + dy);
        break;
    case Corner::BottomLeft:
        m.setLeft(m.left() + dx);
        m.setBottom(m.bottom() + dy);
        break;
    case Corner::BottomRight:
        m.setRight(m.right() + dx);
        m.setBottom(m.bottom() + dy);
        break;
    }
    return m;
}

// Absolute ceiling for a margin component whose anchored edge is not
// the binding constraint — i.e. the two components a corner anchor
// never reads (see Panel::clampMarginsToScreen). Kept at the historical
// 8000 so nothing that used to round-trip through QSettings changes
// meaning; the screen-relative bound is always tighter where it applies.
constexpr int kMaxMargin = 8000;

// Clamp a margin component into [0, limit].
//
// `limit` is caller-supplied because the meaningful upper bound is
// screen-relative, not absolute. The old body was
// `std::clamp(v, 0, 8000)`, and that flat ceiling is exactly what let a
// panel positioned on a 4K output survive the unplug as a 3000px margin
// against a 1080p screen: every layer-shell anchor then resolves the
// panel entirely off the remaining screen, and nothing ever re-derived a
// smaller bound, so it stayed lost. Panel::marginLimits() supplies the
// real ceiling; this function only enforces the range.
int clampMargin(int v, int limit)
{
    return std::clamp(v, 0, std::max(limit, 0));
}

// Look up one output name in today's QGuiApplication::screens().
// Returns nullptr when the name is empty or names an output that is
// not attached — "cannot measure this", never "guess a different one".
// The one spelling of the walk; see Panel::resolveOutputScreen().
QScreen *screenByName(const QString &name)
{
    if (name.isEmpty())
        return nullptr;
    for (QScreen *s : QGuiApplication::screens()) {
        if (s && s->name() == name)
            return s;
    }
    return nullptr;
}

} // namespace

QScreen *Panel::resolveOutputScreen(const QString &fallbackName) const
{
    // ONE resolution for the whole class. The doc block lives in
    // panel.h beside the declaration; the short version is
    //   1. outputName_      — the panel's own screen choice
    //   2. fallbackName     — only when outputName_ is empty
    //   3. primaryScreen()
    //
    // Why step 2 exists rather than a second outputName: a split
    // sub-window never names a screen of its own (panels.ini has no
    // outputName key for any of them), so without it every sub-window
    // resolved straight to primaryScreen() and got clamped against
    // whatever output Qt calls primary — a portrait HDMI-A-1 measuring
    // a window the console previews on a landscape DP-1. Measured:
    // asked left=2538, got left=1058, short by 1480 px. See
    // setPreviewFallbackOutput().
    //
    // Deliberately NOT applied by applyGeometry()/onOutputsChanged():
    // those two push the screen to the real layer-shell surface, and a
    // preview screen must never decide where a live overlay lands. The
    // fallback is a measurement-only concern, so only the geometry
    // question (marginLimits) reads it. Passing the default empty name
    // reproduces the old behaviour of every one of those call sites
    // exactly.
    //
    // 1. The panel's OWN choice wins: a window the user pointed at an
    //    output is measured against that output, whatever the preview
    //    happens to be showing.
    if (QScreen *own = screenByName(outputName_))
        return own;
    // 2. No own choice (or it names an output that is gone): the
    //    caller-supplied preview screen.
    if (QScreen *preview = screenByName(fallbackName))
        return preview;
    // 3. Nothing named resolved: primary, as it has always been.
    return QGuiApplication::primaryScreen();
}

Panel::Panel(const QString &settingsKey, Corner corner, QWidget *parent,
             int subWindowIndex)
    : QMainWindow(parent)
    , key_(settingsKey)
    , corner_(corner)
    , subWindowIndex_(subWindowIndex)
    , margins_(defaultMarginsFor(corner))
{
    setObjectName(QStringLiteral("mhw-panel-%1").arg(settingsKey));
    // v0.3: layer-shell with a transparent surface lets us composite
    // qpa-level alpha. Setting WA_OpaquePaintEvent caused SVG/PNG
    // icons to drop out (any QPixmap painted with Qt::transparent
    // alpha was discarded by the compositor). Keep the window
    // chromeless and transparent; the per-pixel chrome still draws
    // an opaque background inside each panel.
    setAttribute(Qt::WA_TranslucentBackground);
    // Don't let Qt raise / activate the panel window when it transitions
    // visible→shown (e.g. when we enter a hunting zone and the monster
    // / damage panels pop up).  KWin otherwise steals keyboard focus
    // from the game and adds an entry for our panel to the task bar.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setMouseTracking(true);

    // Safety-net default size so the surface is never 0x0. Subclasses
    // call setContentSize() once they know their real content size.
    setFixedSize(320, 120);

    // Load persisted config, then set up the layer-shell surface
    // (anchors + margins) BEFORE the window is shown. Showing first
    // and configuring layer-shell after triggers "already has
    // shell integration" warnings.
    loadConfig();

    // Hot-plug watch (v0.11.4). The margin bound is screen-relative
    // (see marginLimits), so the value that is correct depends on which
    // outputs happen to be attached — and that set changes without the
    // overlay being restarted. Re-push on every add/remove/primary change
    // rather than only at construction: this is what makes a panel that
    // was parked off-screen on a 2-monitor layout come back when the user
    // unplugs the secondary, and lets hypr-output-failsafe's periodic
    // surface rebuild land inside the output it now resolves on.
    // Qt emits screenAdded/screenRemoved on the QGuiApplication, which
    // exists for the whole process lifetime, so no guard is needed on
    // the receiver side.
    connect(qApp, &QGuiApplication::screenAdded,
            this, &Panel::onOutputsChanged);
    connect(qApp, &QGuiApplication::screenRemoved,
            this, &Panel::onOutputsChanged);
    connect(qApp, &QGuiApplication::primaryScreenChanged,
            this, &Panel::onOutputsChanged);

    applyGeometry();

}

void Panel::applyGeometry()
{
    // Ensure a native window exists before touching layer-shell state.
    QWindow *native = windowHandle();
    if (!native) {
        (void)winId();
        native = windowHandle();
    }
    if (!native)
        return;

    LayerShellQt::Window *layer = LayerShellQt::Window::get(native);
    if (!layer)
        return;

    // v0.8 (screen selection): pick the target QScreen. Resolution order:
    //   1. outputName_ (set by setOutputName() / CLI --output-*)
    //   2. fall back to QGuiApplication::primaryScreen()
    // Then push it both to the QWindow (Qt paints/widgets use it) AND to
    // LayerShellQt::Window::setScreen() — the latter is what triggers
    // the wlr-layer-shell `set_output` event on the compositor. Without
    // setScreen(), Niri/Sway/Hyprland/KWin all pick an output for the
    // surface based on the initial geometry — which on multi-monitor
    // Wayland sessions often resolves to the wrong screen and there is
    // no later hook to redirect it. setCloseOnDismissed(false) keeps
    // the surface alive if the chosen output briefly disconnects, so a
    // dock undock / monitor sleep doesn't kill the overlay.
    //
    // No preview fallback here, on purpose: this screen is pushed to a
    // REAL layer-shell surface, and the screen a console happens to be
    // previewing must never decide where a live overlay lands. The
    // fallback only feeds marginLimits() — see resolveOutputScreen().
    QScreen *targetScreen = resolveOutputScreen();
    if (targetScreen) {
        if (native->screen() != targetScreen)
            native->setScreen(targetScreen);
        if (layer->screen() != targetScreen)
            layer->setScreen(targetScreen);
        layer->setCloseOnDismissed(false);
        layer->setWantsToBeOnActiveScreen(false);
    }

    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    // Default: KeyboardInteractivityNone — the surface is invisible
    // to the compositor's focus chain. Show/hide never steals focus,
    // no taskbar entry, game stays fullscreen. Clicking the panel
    // dynamically switches to OnDemand (see mousePressEvent).
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setExclusiveZone(-1);
    layer->setScope(QStringLiteral("monster-overlay"));
    layer->setActivateOnShow(false);

    // Anchor the panel to its corner. This is the KEY difference from
    // the buggy version: without anchors, layer-shell expands the
    // overlay to fill the entire screen (locking the user out).
    LayerShellQt::Window::Anchors anchors;
    switch (corner_) {
    case Corner::TopLeft:
        anchors |= LayerShellQt::Window::AnchorTop;
        anchors |= LayerShellQt::Window::AnchorLeft;
        break;
    case Corner::TopRight:
        anchors |= LayerShellQt::Window::AnchorTop;
        anchors |= LayerShellQt::Window::AnchorRight;
        break;
    case Corner::BottomLeft:
        anchors |= LayerShellQt::Window::AnchorBottom;
        anchors |= LayerShellQt::Window::AnchorLeft;
        break;
    case Corner::BottomRight:
        anchors |= LayerShellQt::Window::AnchorBottom;
        anchors |= LayerShellQt::Window::AnchorRight;
        break;
    }
    layer->setAnchors(anchors);
    // Every path that writes a layer-shell surface goes through
    // pushMargins() (not a raw setMargins), so the value the compositor
    // sees is always the stored intent clamped to the output the surface
    // just resolved on. That is what makes surface recreation self-heal:
    // when hypr-output-failsafe rebuilds this panel weeks after the
    // position was recorded, the margin is re-derived against the output
    // we can see now instead of being replayed blindly.
    pushMargins();

    // Tell the compositor the desired surface size (the free,
    // non-anchored dimensions for a corner anchor).
    layer->setDesiredSize(size());
}


bool Panel::hasStoredPosition() const
{
    // settings() is a TU-local handle onto the same file every Panel opens,
    // so the group has to be spelled HERE rather than at the call site.
    // beginGroup/endGroup rather than a scoped copy because QSettings has
    // no group-scoped contains(): contains() answers for the CURRENT
    // group, and the current group is whatever the last caller left.
    //
    // One reference across the three accesses, for the same reason
    // saveConfig() binds one: each settings() call re-syncs from disk,
    // and a beginGroup()/endGroup() pair split across two of them would
    // leave the group state in an object that was re-read in between.
    QSettings &s = settings();
    s.beginGroup(scopedKey(key_));
    const bool stored = s.contains(QStringLiteral("ml"))
                        || s.contains(QStringLiteral("mt"));
    s.endGroup();
    return stored;
}

mhw::GameId Panel::demoGame() const
{
    // The namespace default, named through the complete type - panel.h
    // cannot write it inline because it only forward-declares GameId.
    return mhw::GameId::World;
}

QString Panel::settingsGroup() const
{
    // The one place a scoped group name is derived from a panel's own
    // key. panel.cpp's scopedKey() above is the same function for
    // the base class's own three accesses; the subclasses that open
    // their own QSettings handle (the three column-dial windows) call
    // this so the prefix is spelled exactly once per access path rather
    // than four times.
    //
    // v0.14: TWO globals, not one. The game selects [world|rise] and the
    // layout mode selects [single|split]; see panel_settings_game.h for
    // why the mode is part of a window's address at all (the split and
    // the single are different windows whose positions used to
    // overwrite each other).
    return mhw::panelSettingsScopeGroup(key_);
}

void Panel::loadConfig()
{
    // Reading the file is not the panel forming an opinion: after a reload
    // the members hold the disk's values, so the window has been given
    // nothing new here and must be eligible for a reload again. Without
    // this, one drag would pin the window's value forever and the console
    // would stop seeing the overlay's writes for exactly that window.
    configTouched_ = false;

    // A sub-window's first run is a CASCADE from its parent, not the same
    // corner default as everyone else — see defaultMarginsForSubWindow()
    // for why eleven TopLeft windows on one pixel was the previous
    // behaviour. A stored value still wins outright.
    const QMargins def = (subWindowIndex_ > 0)
        ? defaultMarginsForSubWindow(corner_, subWindowIndex_)
        : defaultMarginsFor(corner_);
    // v0.14: migrate a legacy section into this (game, mode) scope's
    // namespace BEFORE reading it, so a user's existing layout is not
    // silently abandoned (its keys would otherwise read as "absent").
    // Idempotent and one-shot: see panel_settings_game.h for the
    // guarantees and why the handle is passed rather than opened here.
    //
    // Both legacy spellings are handled by this one call, in the order
    // the file was written in: a pre-v0.13 flat `[player]` folds into
    // `[<game>/single/player]` (the two-domain pre-v0.13 spelling
    // folded first, and a fully flat spelling then folds into the same
    // target), and a pre-v0.14 `[<game>/<key>]` folds into
    // `[<game>/single/<key>]` — SINGLE because that is the only topology
    // that existed when it was written.
    const mhw::PanelSettingsScope scope{mhw::panelSettingsGame(),
                                        mhw::panelSettingsLayout()};
    // ONE handle for the whole load: the migration writes (setValue +
    // remove + its own sync) and the ~20 reads below must all see the
    // same snapshot of the file, and must all be inside one beginGroup /
    // endGroup pair. Calling settings() per access would re-read the
    // file between the migration and the read that depends on it, and
    // would re-read between two keys of the same section.
    QSettings &s = settings();
    if (mhw::migrateLegacyPanelSection(s, scope, key_)) {
        qInfo("panels.ini: migrated the legacy section for '%s' into [%s]; "
              "the other games' and the other mode's sections are untouched",
              qPrintable(key_), qPrintable(scopedKey(key_)));
    }
    s.beginGroup(scopedKey(key_));
    // Read the stored value verbatim — NO clampMargin here. Clamping at
    // read time is what made a 4K position unrecoverable: the console
    // opens with fewer screens attached and the mere act of reading
    // destroyed the position the user chose on the output they weren't
    // carrying. The screen-relative clamp is applied at push time
    // (pushMargins) instead, so the stored intent survives a monitor
    // unplug and is re-applied the moment that output returns.
    // .toInt() on a corrupt value yields 0, which is a safe default.
    margins_.setLeft(s.value(QStringLiteral("ml"), def.left()).toInt());
    margins_.setTop(s.value(QStringLiteral("mt"), def.top()).toInt());
    margins_.setRight(s.value(QStringLiteral("mr"), def.right()).toInt());
    margins_.setBottom(s.value(QStringLiteral("mb"), def.bottom()).toInt());
    // The single source of truth for "what scale does a panel start at".
    // It lives in panel.h as an inline constexpr so EVERY read of it — this
    // line, the member initialiser, resetToDefaults() — resolves to the
    // same value. Before this, the member initialiser said 1.0 and this
    // line said 2.0, so a panel's scale depended on whether it had ever
    // been through saveConfig(): the very first construction on a fresh
    // install painted at 1.0, and the first loadConfig() after a write
    // leapt to 2.0. (The 2.0 read default was a v0.3 leftover — it was
    // introduced in 371cf4f when the panels were still 378px-wide chrome
    // windows. It stayed on long after, which is why the [player-status]
    // section the v0.12 split created has scale=2 written into it: the
    // sub-window is ~91px wide, so x2 made 7pt text 14pt inside a 182px
    // window and the strip was unusable. Reported by the user.)
    scale_ = s.value(QStringLiteral("scale"), kDefaultScale).toDouble();
    // Minimum size floors, read verbatim like the margins above and
    // clamped only to a sane range. NO clamp against the screen: a
    // floor wider than the attached display is still the user's
    // intent, and silently reducing it here is the same data loss that
    // made a 4K margin unrecoverable. A corrupt value yields 0 from
    // .toInt(), which reads as "no floor" — the same fallback every
    // other key here relies on.
    minW_ = std::clamp(s.value(QStringLiteral("minW"), 0).toInt(),
                       0, kMaxMinSize);
    minH_ = std::clamp(s.value(QStringLiteral("minH"), 0).toInt(),
                       0, kMaxMinSize);
    // The SET size, same read-verbatim / clamp-to-sane-range rule. Its
    // key names are the same ones with a `lock` prefix so an existing
    // panels.ini keeps exactly the meaning it had: a section with no
    // lockW/lockH keys reads 0/0, which is "no lock", and the section's
    // geometry is decided by its content and its floor exactly as
    // before. Not sharing the key with minW/minH is deliberate — two
    // quantities, two keys, no reinterpretation on upgrade.
    lockW_ = std::clamp(s.value(QStringLiteral("lockW"), 0).toInt(),
                        0, kMaxMinSize);
    lockH_ = std::clamp(s.value(QStringLiteral("lockH"), 0).toInt(),
                        0, kMaxMinSize);
    opacity_ = s.value(QStringLiteral("opacity"), 1.0).toDouble();
    bgAlpha_ = std::clamp(s.value(QStringLiteral("bgAlpha"), 170).toInt(), 0, 255);
    // v0.8: per-panel screen selection. Empty = use whatever the OS calls
    // primary. A non-empty value matches QScreen::name() in
    // QGuiApplication::screens() — on Niri that string is exactly the
    // output name (eDP-1 / DP-1 / HDMI-A-1 …), so the value is also
    // what wlr-randr / kscreen-doctor would print.
    outputName_ = s.value(QStringLiteral("outputName")).toString();
    s.endGroup();

    scale_ = std::clamp(scale_, kMinScale, kMaxScale);
    opacity_ = std::clamp(opacity_, 0.1, 1.0);
    // Runtime config is observable through the console; do not log each
    // console construction because it floods the TUI output.

    // Note: visibility is NOT applied here. The window must not be
    // shown until after applyGeometry() configures the layer-shell
    // surface (anchors + margins); showing first triggers "already
    // has a shell integration" warnings. main.cpp shows the panels.
    // Opacity is NOT applied via setWindowOpacity here (see setOpacity
    // for why). paintEvent composites it at the pixel level instead, so
    // the persisted alpha takes effect on the very first paint.
}

void Panel::saveConfig()
{
    // ONE reference for the whole write, not settings() per setValue.
    // settings() syncs on every access, so calling it between two
    // setValue()s flushes the first to disk before the second exists —
    // a crash (or a reader) in that window sees half a position. Binding
    // the handle once makes this one buffered write, flushed by the one
    // sync() at the end, which is the shape the file was always written
    // in before settings() learned to re-read.
    QSettings &s = settings();
    s.beginGroup(scopedKey(key_));
    s.setValue(QStringLiteral("ml"), margins_.left());
    s.setValue(QStringLiteral("mt"), margins_.top());
    s.setValue(QStringLiteral("mr"), margins_.right());
    s.setValue(QStringLiteral("mb"), margins_.bottom());
    s.setValue(QStringLiteral("scale"), scale_);
    s.setValue(QStringLiteral("minW"), minW_);
    s.setValue(QStringLiteral("minH"), minH_);
    // The SET size, written next to the floor it sits beside. Only
    // written when non-zero: a key absent from the ini is exactly the
    // state a pre-lock section is in, and emitting `lockW=0` for every
    // panel would rewrite every user's file on first exit for no
    // information.
    if (lockW_ != 0)
        s.setValue(QStringLiteral("lockW"), lockW_);
    if (lockH_ != 0)
        s.setValue(QStringLiteral("lockH"), lockH_);
    s.setValue(QStringLiteral("opacity"), opacity_);
    s.setValue(QStringLiteral("bgAlpha"), bgAlpha_);

    // A `visible` key used to be written here. It was removed because
    // nothing ever read it back, and the value it wrote was actively
    // misleading: it stored isVisible(), which is the *runtime* answer
    // (a panel the master switch disabled reports false, and so does a
    // panel with no data yet), not the user's intent. Meanwhile the real
    // master gate — panelEnabled_, driven by --no-player/monster/damage/
    // pets from the console — was never persisted at all. So the key
    // claimed the opposite of what the console's master toggle showed.
    //
    // Persisting a master switch would be a behaviour change, not a bug
    // fix: the panel is mounted/unmounted by argv right now, so a stored
    // value has no reader to honour it and adding one means deciding
    // what wins when argv and the ini disagree. The console's own
    // master toggle survives through saveMaskToDisk() (0 = all off),
    // which is already the durable record of that decision. Dropping the
    // key keeps panels.ini to values that have exactly one meaning, one
    // writer and one reader. Stale `visible=false` lines in existing
    // configs are now simply ignored, which is the harmless outcome.
    s.endGroup();
    s.sync();
}

void Panel::saveAppearance()
{
    // Same one-reference rule as saveConfig(): this writes ~10 keys, and
    // settings() per setValue would flush them one at a time.
    QSettings &s = settings();
    s.beginGroup(scopedKey(key_));
    s.setValue(QStringLiteral("scale"), scale_);
    s.setValue(QStringLiteral("minW"), minW_);
    s.setValue(QStringLiteral("minH"), minH_);
    // SET size, same non-zero-only rule as saveConfig().
    if (lockW_ != 0)
        s.setValue(QStringLiteral("lockW"), lockW_);
    if (lockH_ != 0)
        s.setValue(QStringLiteral("lockH"), lockH_);
    s.setValue(QStringLiteral("opacity"), opacity_);
    s.setValue(QStringLiteral("bgAlpha"), bgAlpha_);
    // v0.5: also persist margins so the overlay's loadConfig() picks up
    // the position the user set in the console. Same
    // read-verbatim rule as loadConfig(): the console's preview panels are
    // shown on the screens the user has attached NOW, so writing the raw
    // (unclamped) margins_ here preserves a position chosen on an output
    // that happens to be unplugged at the moment the console is open.
    // The screen-relative clamp is applied at push time, not at rest.
    s.setValue(QStringLiteral("ml"), margins_.left());
    s.setValue(QStringLiteral("mt"), margins_.top());
    s.setValue(QStringLiteral("mr"), margins_.right());
    s.setValue(QStringLiteral("mb"), margins_.bottom());
    // v0.8: persist per-panel screen selection. The control console
    // writes this from setOutputName(); loadConfig() reads it back
    // on the overlay's next start.
    s.setValue(QStringLiteral("outputName"), outputName_);
    s.endGroup();
    s.sync();
}

void Panel::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    // KWin 6.7 auto-blurs all semi-transparent windows; there is no
    // client-side opt-out (org_kde_kwin_blur protocol was removed,
    // replaced by ext_background_effect_v1 which Qt/KDE dont bind yet).
    //
    // v0.15: and the editor's first chance at keyboard focus. Everywhere
    // else this window is deliberately kept out of the focus chain - see
    // WA_ShowWithoutActivating in the constructor - so a show event is the
    // one moment in the whole startup sequence where the surface is known
    // to be mapped. main.cpp's earlier request, made before app.exec(),
    // measured as a no-op for exactly that reason (exposed=0).
    requestEditFocusOnShow();
}

void Panel::setCompositingEnabled(bool)
{
    // No-op since opacity is now pixel-level in paintEvent.
    // Kept for API compatibility with renderPreview().
}

void Panel::setEditMode(bool on)
{
    editMode_ = on;
    setCursor(on ? Qt::SizeAllCursor : Qt::ArrowCursor);
    // Edit mode needs keyboard from the start (arrow keys, Esc).
    // applyGeometry() resets to None, so override afterwards.
    applyGeometry();
    setLayerKeyboardInteractivity(on);
    update();
}

void Panel::setVisible(bool visible)
{
    // Master visibility gate. When panelEnabled_ is false the panel is
    // completely unmounted regardless of what the caller asks for. This
    // is what the control console's master toggle drives: flipping the
    // toggle off should not leave a 32-40px chrome sliver in the
    // corner, it should remove the layer-shell surface entirely. The
    // section mask is a separate axis (each sub independently visible /
    // hidden) — the master gate is strictly above that.
    if (!panelEnabled_ && visible)
        return;

    // Hide on first hide is fine; show without activating is the
    // important path. Without WA_ShowWithoutActivating, KWin raises
    // the panel into focus as soon as it appears, which (a) makes
    // the task bar pop up with an entry for our window and (b) takes
    // keyboard focus away from the game whenever we go visible.
    // Setting this once is enough; Qt honours it for every show.
    QMainWindow::setVisible(visible);
}

void Panel::setPanelEnabled(bool on)
{
    if (panelEnabled_ == on)
        return;
    panelEnabled_ = on;
    if (!on)
        setVisible(false);   // honor the new gate immediately
    else
        update();            // caller will follow up with setVisible(true)
}

void Panel::setContentSize(int w, int h)
{
    // The floor goes on FIRST, before logicalSize_ is recorded, so every
    // downstream reader — the compositor request below, and the
    // normalSize_ that Space captures when the panel folds — sees the
    // size the user actually asked for rather than the content's
    // opinion of it.
    applyMinSizeFloor(w, h);
    logicalSize_ = QSize(w, h);
    // Actual window size tracks the zoom so painted content is
    // never clipped at scale != 1.0.
    const int aw = static_cast<int>(std::lround(w * scale_));
    const int ah = static_cast<int>(std::lround(h * scale_));

    // Idempotent guard: paintDemo/paintPanel call setContentSize() on
    // every repaint, and a drag fires update() each frame. Re-issuing
    // setFixedSize() + setDesiredSize() every frame makes the
    // compositor reconfigure the surface repeatedly — that's the
    // flicker. Only touch the compositor when the size actually
    // changed.
    if (size() == QSize(aw, ah))
        return;

    setFixedSize(aw, ah);

    // Re-derive the margins against the REAL panel extent. The old extent
    // (the 320x120 ctor placeholder, or a previous scale's size) produced
    // a looser bound than the surface actually needs — a left margin of
    // W-320 is legal for a 320px panel but lets a 378px panel hang 58px
    // off the right edge. Only runs when the size really changed, so the
    // every-repaint setContentSize() calls stay free.
    pushMargins();

    // Keep the layer-shell desired size in sync so the compositor
    // allocates the right surface dimensions.
    QWindow *native = windowHandle();
    if (native) {
        LayerShellQt::Window *layer = LayerShellQt::Window::get(native);
        if (layer)
            layer->setDesiredSize(QSize(aw, ah));
    }
}

void Panel::setChromeVisible(bool on)
{
    if (chromeVisible_ == on)
        return;
    chromeVisible_ = on;
    update();
}

void Panel::paintPanelChrome(QPainter &p, Accent accent) const
{
    // The whole decision is one boolean read. When chrome is off this
    // function returns without touching the painter at all — it does
    // NOT emit a transparent fill, a zero-alpha rect or a cleared pen:
    // any of those would still land a pixel, and a fragment's frame has
    // to mean "the sub-window's own capsule pixels are the only frame
    // pixels it has".
    //
    // PIXEL-PARITY NOTE: with chromeVisible_ == true (the default, and
    // the value all four shipped panels run with) this call is exactly
    // drawV03Chrome(p, accent) — same painter, same arguments, same
    // order in paintEvent. Nothing between here and the backing store
    // can distinguish the two paths, which is why switching a subclass
    // from drawV03Chrome() to paintPanelChrome() is behaviour-neutral
    // today and becomes the opt-out tomorrow.
    //
    // What this DOES NOT cover any more: the translucent BODY. See
    // drawSubWindowBody() — that is painted by paintEvent() for exactly
    // the windows this function stops drawing for, so a fragment keeps
    // its surface and loses only its frame.
    if (chromeVisible_)
        drawV03Chrome(p, accent);
}

void Panel::paintEvent(QPaintEvent *)
{
    // HOT-PATH NOTE: this function and every update()/repaint path it
    // feeds must NOT call settings(). The panels.ini handle now re-syncs
    // from disk on each access (see settings() above), which is correct
    // for a configuration read and would be a per-frame file read here.
    // Everything this paints is read from MEMBER state (scale_, opacity_,
    // bgAlpha_, margins_, chromeVisible_, editMode_) that loadConfig()
    // filled once at construction — no repaint reaches for the file, and
    // that invariant is what keeps a 60fps overlay a 60fps overlay.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);


    // Edit-mode border so the user knows this panel is interactive.
    // Drawn in actual (device) coordinates, before any scaling.
    if (editMode_) {
        p.setPen(QPen(QColor(120, 180, 255), kBorderWidth, Qt::DashLine));
        p.drawRect(rect().adjusted(1, 1, -1, -1));
    }

    // Apply the user's zoom (edit-mode wheel scroll). paintPanel
    // then draws in logical coordinates; the window size is kept in
    // sync via setContentSize() so nothing is clipped.
    if (scale_ != 1.0)
        p.scale(scale_, scale_);

    // Composite at the user's opacity. Probe-verified: QPainter::setOpacity
    // writes per-pixel alpha into the ARGB backing store (a=127 at 0.5,
    // a=76 at 0.3). With WA_TranslucentBackground the wayland surface has
    // alphaBufferSize > 0 → QWaylandWindow::isOpaque() == false → KWin
    // enables blending. This is the only path that puts alpha into the
    // actual buffer the compositor sees.
    p.setOpacity(opacity_);

    // The fragment's surface. A window that turned the frame off
    // (chromeVisible_ == false — every v0.12 split sub-window) loses its
    // stripe and its border, NOT its body: it is still a thing that sits
    // on the game scene and still needs the same translucent #0c0e10 the
    // panel sits on, or the six of them render as bare canvasses and the
    // user's own words are 「只有那个 Player 玩家那个面板有半透明背景，
    // 其余的背景全部丢失」.
    //
    // PIXEL-ORDER, and why each position is what it is:
    //   * after setOpacity() — so the user's opacity slider governs the
    //     body exactly as it governs every drawn pixel of a stock panel
    //     (whose body lands after the same setOpacity, inside paintPanel).
    //     Painting it before would make a fragment the one window in the
    //     app whose surface ignores the slider.
    //   * after the zoom — so it lands in logical coordinates like
    //     everything drawV03Chrome() draws, and the background cannot
    //     drift from the content at scale != 1 (the v0.7.5 reproducer).
    //   * before paintPanel() — so a fragment's own pixels composite on
    //     top of its surface rather than behind it.
    //   * before the edit-mode demo border? No: that one is drawn in
    //     DEVICE coordinates at the very top of this function, so it
    //     stays as it was.
    //
    // PIXEL-PARITY for the shipping panels: none of them sets
    // chromeVisible(false), so this branch is not taken by any of the ten
    // single-mode baselines and their pixels do not move. The split
    // windows each gain one rounded rect of translucent fill, which is
    // precisely the defect being fixed.
    //
    // drawV03Chrome() is NOT called here and does not need to be: it
    // starts with this same drawSubWindowBody() and then adds the stripe
    // and the border, so a stock panel's body is still drawn exactly once.
    if (!chromeVisible_)
        drawSubWindowBody(p);

    // Minimized: small colored block with panel initial.
    if (minimized_) {
        paintMinimized(p);
        return;
    }

    // In edit mode, draw demo content so the panel stays visible
    // and identifiable for positioning. In live mode, paintPanel()
    // handles both the connected (real data) and disconnected
    // ("not connected" placeholder) cases itself.
    //
    // Demo content shown in edit mode only: setupDemoData() seeds
    // `names_` / `firstHitTick_` / `baselineDamage_` / `history_`
    // with hard-coded test data. Calling it in live mode causes the
    // carry-over drop-out detection to latch those demo names as
    // "previously-seen players" the moment `update(snap)` arrives
    // with a smaller party, producing phantom rows that only clear
    // when the next quest starts. (v0.7.5 audit reproducer — the
    // bug the user filed as "extra 测试用的长昵称测试C rows in 2p").
    // Demo gate is therefore edit-mode-only; live mode defers entirely
    // to update(snap).
    if (!demoPrimed_ && editMode_) {
        demoPrimed_ = true;
        setupDemoData();
    }

    paintPanel(p);
}
void Panel::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    // Dynamically enter the focus chain: switch layer-shell keyboard
    // interactivity from None → OnDemand, then request activation.
    // The compositor grants keyboard focus; Esc/Space become usable.
    setLayerKeyboardInteractivity(true);
    if (QWindow *w = windowHandle())
        w->requestActivate();
    setFocus();
}

void Panel::focusOutEvent(QFocusEvent *e)
{
    QMainWindow::focusOutEvent(e);
    // Drop out of the focus chain so the next show/hide cycle is
    // invisible to the compositor (no focus steal, no taskbar entry).
    if (!editMode_)
        setLayerKeyboardInteractivity(false);
}

void Panel::requestEditFocus()
{
    if (!editMode_ || !isVisible())
        return;
    // Enter the focus chain first, then ask. Setting OnDemand alone only
    // makes the surface eligible; requestActivate() is what the compositor
    // answers, and doing it in this order matches mousePressEvent() so both
    // paths are the same two calls rather than two subtly different ones.
    setLayerKeyboardInteractivity(true);
    if (QWindow *w = windowHandle())
        w->requestActivate();
    setFocus();
}

void Panel::requestEditFocusOnShow()
{
    if (!editMode_)
        return;
    // The surface is mapped by the time a show event arrives, which is the
    // one moment in the whole startup sequence where requestActivate() has
    // something to act on. Activation is best-effort by nature on
    // Wayland: the compositor may decline, and then the user's click still
    // works - mousePressEvent() is unchanged and still asks.
    setLayerKeyboardInteractivity(true);
    activateWindow();
    if (QWindow *w = windowHandle())
        w->requestActivate();
    setFocus();
}

void Panel::setLayerKeyboardInteractivity(bool interactive)
{
    QWindow *native = windowHandle();
    if (!native)
        return;
    LayerShellQt::Window *layer = LayerShellQt::Window::get(native);
    if (!layer)
        return;
    layer->setKeyboardInteractivity(interactive
        ? LayerShellQt::Window::KeyboardInteractivityOnDemand
        : LayerShellQt::Window::KeyboardInteractivityNone);
}

void Panel::keyPressEvent(QKeyEvent *e)
{
    // Space: toggle minimize — works in BOTH edit and live mode.
    if (e->key() == Qt::Key_Space) {
        minimized_ = !minimized_;
        if (minimized_) {
            normalSize_ = logicalSize_;
            setContentSize(32, 32);
        } else {
            setContentSize(normalSize_.width(), normalSize_.height());
        }
        update();
        return;
    }

    // Esc: graceful quit — works in BOTH edit and live mode.
    if (e->key() == Qt::Key_Escape) {
        saveConfig();
        QCoreApplication::quit();
        return;
    }

    if (!editMode_)
        return QMainWindow::keyPressEvent(e);

    // `-` and `=` dial the size; with Shift they dial the other axis
    // and with Ctrl they dial the SET size instead of the floor. They
    // sit here, after the live-mode bail-out, so a stray press in-game
    // cannot resize an overlay over the action, and BEFORE the arrow
    // switch so a subclass overriding keyPressEvent still gets every key
    // it did not claim — the same passthrough contract the strip's
    // [ ] dial relies on.
    //
    // Why this pair and not , and . or Shift+arrows: `-` and `=` are the
    // unshifted and shifted faces of ONE physical key on a US layout, so
    // the two halves cost a single hand position, and they read as "minus
    // / plus" the way the arrow keys read as "left / right". Shift means
    // the same thing it already means for the arrows — the other axis of
    // the same quantity — instead of a mode the user has to be inside.
    //
    // Ctrl is the SET-size modifier, and it is the same key for the same
    // reason: it names a DIFFERENT quantity on the same two faces rather
    // than introducing a third key the user has to find. The reported
    // defect was that `=` alone did nothing on a window whose content
    // already exceeded every floor; `=`+Ctrl is the press that has a
    // guaranteed visible effect on every window, because a lock has no
    // content-width precondition. The floor pair stays exactly where it
    // was and keeps its meaning, so nothing a user already learned is
    // re-interpreted.
    {
        const bool shift = (e->modifiers() & Qt::ShiftModifier) != 0;
        const bool ctrl = (e->modifiers() & Qt::ControlModifier) != 0;
        int delta = 0;
        if (e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore)
            delta = -kMinSizeStep;
        else if (e->key() == Qt::Key_Equal || e->key() == Qt::Key_Plus)
            delta = kMinSizeStep;
        if (delta != 0) {
            if (ctrl)
                shift ? nudgeLockHeight(delta) : nudgeLockWidth(delta);
            else
                shift ? nudgeMinHeight(delta) : nudgeMinWidth(delta);
            return;
        }
    }

    // `[` and `]` are the COLUMNS dial: one item per row (a vertical
    // column) versus many (a flat row). Same axis the status strip's
    // pills dial has had since v0.12, now answered by every window that
    // has a row to lay out.
    //
    // The dispatch is the whole point of putting these on the base class:
    // supportsColumns() is false for the four base panels and for the
    // strips that are not a row, so for them this is a plain
    // unimplemented key that falls through to
    // QMainWindow::keyPressEvent() exactly as it does today. No subclass
    // has to forward a key it does not claim, because no subclass owns
    // the key any more.
    //
    // The claim is CONDITIONAL and so is the early return: a window with
    // no column axis must reach QMainWindow::keyPressEvent() here, which
    // is the same place it reached before these keys existed on the base
    // class at all.
    if (supportsColumns()
        && (e->key() == Qt::Key_BracketLeft
            || e->key() == Qt::Key_BracketRight)) {
        const int step = (e->key() == Qt::Key_BracketLeft)
                             ? -kColumnsStep
                             :  kColumnsStep;
        nudgeColumns(step);
        return;
    }

    // Arrow keys: nudge panel margins (edit mode only).
    constexpr int kStep = 10;
    constexpr int kBigStep = 50;
    const int step = (e->modifiers() & Qt::ShiftModifier) ? kBigStep : kStep;

    int dx = 0, dy = 0;
    switch (e->key()) {
    case Qt::Key_Left:  dx = -step; break;
    case Qt::Key_Right: dx =  step; break;
    case Qt::Key_Up:    dy = -step; break;
    case Qt::Key_Down:  dy =  step; break;
    default:
        return QMainWindow::keyPressEvent(e);
    }

    if (dx != 0 || dy != 0)
        nudgeMargins(dx, dy);
}

// ─── margin bounds ─────────────────────────────────────────────────────
//
// Why the bound is derived here rather than in loadConfig(): the two
// questions have different answers.
//
//   * "What did the user choose?" — a storage question. answered by
//     QSettings, verbatim, with no clamping.
//   * "Where can that choice land on the screen we can see right now?"
//     — a geometry question. answered by marginLimits().
//
// Conflating them is what made a 4K position unrecoverable: the console
// reads panels.ini on whatever screens are attached at that moment, and
// if the read clamped the stored value, merely opening the console on a
// laptop panel destroyed the position the user had set up on the 4K
// output. So QSettings keeps the user's value and every *applied*
// geometry is clamped instead.

QMargins Panel::marginLimits() const
{
    // Same resolution order as applyGeometry(): the panel's own output
    // when it names a screen we can actually see, else the primary.
    //
    // ...with the one addition this fix is about. A split sub-window has
    // no outputName_ of its own, so the empty-name branch used to land
    // on primaryScreen() unconditionally — which on the reporter's
    // machine is a PORTRAIT HDMI-A-1 (1080 wide) while the console
    // previews a landscape DP-1 (2560 wide). The drag clamp therefore
    // measured the window against the wrong screen and a drag that
    // asked for left=2538 came back as left=1058, 1480 px short: the
    // window could not be dragged past the middle of the screen it was
    // being drawn on, because it was clamped by the middle of a
    // DIFFERENT one. Handing resolveOutputScreen() the preview screen
    // makes the bound match the screen the preview actually shows.
    //
    // This does not change the four base panels' behaviour: each of
    // them either names an output (its own choice wins over the
    // fallback) or has no preview fallback set, and an empty fallback
    // name resolves exactly as before.
    const QScreen *screen = resolveOutputScreen(previewFallbackOutput_);

    // The panel points at an output that is not attached right now (the
    // 4K monitor is unplugged), or we genuinely have no screen
    // (offscreen platform). Both mean "we cannot measure the target", so
    // we must NOT invent a bound: clamping here would rewrite a position
    // that is perfectly right for the output we just cannot see. Fall
    // back to the historical flat ceiling and let the value round-trip.
    if (!screen)
        return QMargins(kMaxMargin, kMaxMargin, kMaxMargin, kMaxMargin);

    // QScreen::geometry() is in logical pixels (DIPs), which is the same
    // coordinate space layer-shell margins are interpreted in, so the
    // two are directly comparable — no devicePixelRatio conversion.
    const QRect g = screen->geometry();
    const QSize extent = size();
    if (extent.isEmpty())
        return QMargins(kMaxMargin, kMaxMargin, kMaxMargin, kMaxMargin);

    // A corner anchor resolves the panel from the two anchored edges
    // only. Split the screen's axes by which edge is binding, and give
    // each binding component the room from its own edge to the far edge
    // of the screen minus the panel's own extent. The two inert
    // components (never read by the compositor) keep the flat ceiling —
    // bounding them would silently rewrite the user's margin on those
    // sides for no geometric reason.
    QMargins limits(kMaxMargin, kMaxMargin, kMaxMargin, kMaxMargin);
    const int slackX = std::max(0, g.width()  - extent.width());
    const int slackY = std::max(0, g.height() - extent.height());
    switch (corner_) {
    case Corner::TopLeft:     limits.setLeft(slackX);  limits.setTop(slackY);    break;
    case Corner::TopRight:    limits.setRight(slackX); limits.setTop(slackY);    break;
    case Corner::BottomLeft:  limits.setLeft(slackX);  limits.setBottom(slackY); break;
    case Corner::BottomRight: limits.setRight(slackX); limits.setBottom(slackY); break;
    }
    return limits;
}

QMargins Panel::clampMarginsToScreen(const QMargins &m) const
{
    const QMargins limits = marginLimits();
    QMargins out = m;
    out.setLeft(clampMargin(out.left(), limits.left()));
    out.setTop(clampMargin(out.top(), limits.top()));
    out.setRight(clampMargin(out.right(), limits.right()));
    out.setBottom(clampMargin(out.bottom(), limits.bottom()));
    return out;
}

QMargins Panel::pushedMargins() const
{
    return clampMarginsToScreen(margins_);
}

void Panel::onOutputsChanged()
{
    // Re-resolve the surface's target screen. A panel bound to an output
    // that just went away falls back to the primary here (same order as
    // applyGeometry), and a panel bound to an output that just came back
    // snaps back onto it. Without this the surface keeps the output it
    // was created with and a panel that reads its stored 4K margin
    // against a 1080p primary stays lost.
    if (QWindow *native = windowHandle()) {
        // Same reasoning as applyGeometry(): no preview fallback. This
        // screen is pushed at a real layer-shell surface.
        QScreen *targetScreen = resolveOutputScreen();
        if (targetScreen && native->screen() != targetScreen)
            native->setScreen(targetScreen);
        if (LayerShellQt::Window *layer = LayerShellQt::Window::get(native)) {
            if (targetScreen && layer->screen() != targetScreen)
                layer->setScreen(targetScreen);
            // The whole point of the slot: re-derive the margin against
            // the screen we can measure NOW.
            layer->setMargins(pushedMargins());
        }
    }
    update();
}

void Panel::pushMargins() const
{
    // No window yet (ctor ordering: loadConfig() → applyGeometry() runs
    // before anything is mapped). applyGeometry() re-invokes this once a
    // native window exists, so an empty early call is not a lost update.
    QWindow *native = windowHandle();
    if (!native)
        return;
    LayerShellQt::Window *layer = LayerShellQt::Window::get(native);
    if (!layer)
        return;
    layer->setMargins(pushedMargins());
}

bool Panel::commitMargins(const QMargins &m)
{
    const QMargins clamped = clampMarginsToScreen(m);
    if (clamped == margins_)
        return false;
    margins_ = clamped;
    pushMargins();
    return true;
}

void Panel::nudgeMargins(int dx, int dy)
{
    QMargins m = margins_;
    switch (corner_) {
    case Corner::TopLeft:
        m.setLeft(m.left() + dx);
        m.setTop(m.top() + dy);
        break;
    case Corner::TopRight:
        m.setRight(m.right() - dx);
        m.setTop(m.top() + dy);
        break;
    case Corner::BottomLeft:
        m.setLeft(m.left() + dx);
        m.setBottom(m.bottom() - dy);
        break;
    case Corner::BottomRight:
        m.setRight(m.right() - dx);
        m.setBottom(m.bottom() - dy);
        break;
    }
    // Route through the shared clamp so a drag that wanders off a
    // screen's edge stops dead instead of storing an unreachable value.
    // saveConfig() below persists the CLAMPED vector, which is the point:
    // the last thing the overlay did is a position it could actually
    // reach, and that is what comes back next launch.
    if (!commitMargins(m))
        return;                     // clamped to the same value: no-op
    update();

    // Persist immediately so a SIGTERM / crash doesn't lose position.
    // Saves only happen in edit mode (this method isn't called
    // otherwise), so live mode never pays this cost.
    saveConfig();
}

void Panel::setMargins(QMargins m, bool persist)
{
    // The single point where a NEW value arrives — keyboard nudge, wheel,
    // a drag routed in from HudCanvas, resetToDefaults. Stamping here
    // rather than at each call site is what makes "has this window been
    // given a new value since it was built" a fact about the window
    // instead of an assumption made by whoever is about to save it.
    configTouched_ = true;
    // Screen-relative clamp, not the absolute 8000 — see marginLimits().
    if (!commitMargins(m))
        return;
    update();
    if (persist)
        saveConfig();
}

// v0.8: switch this panel to a different Wayland output. Empty name =
// revert to "follow OS primary". Validating against
// QGuiApplication::screens() up front lets the control console catch a
// typo before the overlay tries to apply it (the surface already exists
// at that point, but at least the user sees a warning).
//
// persist=true writes the value through saveAppearance() — the same
// path the console's other setters use, so a quit without save still
// remembers the screen the user picked. persist=false is for the live
// console's preview-only path.
void Panel::setOutputName(QString name, bool persist)
{
    if (name == outputName_)
        return;
    if (!name.isEmpty()) {
        // Same lookup the resolution path uses, so "is this a real
        // output?" and "which screen is this?" cannot disagree.
        if (!screenByName(name)) {
            qWarning("Panel::setOutputName: '%s' is not a known output; "
                     "available: %s. Reverting to primary.",
                     qPrintable(name),
                     qPrintable([&]() {
                         QStringList n;
                         for (QScreen *s : QGuiApplication::screens())
                             if (s) n << s->name();
                         return n.join(QLatin1String(", "));
                     }()));
            name = QString();
        }
    }
    // v0.17: picking a screen IS giving this window a new value, and the
    // console's exit path decides what to re-read purely by this flag.
    //
    // Without the stamp, choosing a screen in the console and then closing
    // it LOST the choice: saveAllSplitAndBaseWindows() sees a panel nobody
    // "touched", calls loadConfig() on it, and pulls the OLD value back off
    // disk - which for a window that had never been given an output is the
    // empty string meaning "follow the primary screen". The empty value is
    // then written straight back, so the pick survives neither the exit nor
    // the next launch.
    //
    // It looked like the two layout modes interfering - the user saw the
    // windows that went missing be exactly the ones showing a different
    // screen in Single mode - but the file said otherwise: every split\*
    // outputName was EMPTY, including the two that had been set, while
    // every single\* one held DP-1. The modes were never talking to each
    // other; one reload was erasing the split picks before they were
    // written.
    configTouched_ = true;
    outputName_ = name;
    applyGeometry();
    if (persist)
        saveAppearance();
}

void Panel::setPreviewFallbackOutput(const QString &name)
{
    // Deliberately no validation, no applyGeometry(), no persistence.
    //
    // No validation: the preview screen is whatever the console is
    // currently drawing, and it can be a name that is not attached
    // (a just-unplugged output). resolveOutputScreen() already treats
    // an unresolvable fallback as "not there" and carries on to
    // primary, which is the correct degradation for a measurement.
    //
    // No applyGeometry(): this is not the panel's own screen choice and
    // must never move a layer-shell surface. Only marginLimits() reads
    // it — see the declaration in panel.h.
    //
    // No persistence: it is preview state that the console re-pushes on
    // every rebuild, so writing it to panels.ini would put a key on
    // every split window that loadConfig() then has to ignore.
    previewFallbackOutput_ = name;
}

void Panel::paintMinimized(QPainter &p)
{
    // Small 32×32 rounded square with the panel's first letter.
    const QColor bg(40, 40, 50, 100);
    const QColor fg(160, 180, 220, 120);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(0, 0, 32, 32, 6, 6);
    p.setPen(QPen(QColor(120, 180, 255, 60), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(0.5, 0.5, 31, 31, 6, 6);
    // First letter of the settings key as identifier.
    const QString letter = key_.isEmpty() ? QStringLiteral("?")
                                          : key_.left(1).toUpper();
    p.setPen(fg);
    p.setFont(QFont(QStringLiteral("Work Sans"), 12, QFont::Bold));
    p.drawText(QRectF(0, 0, 32, 32), Qt::AlignCenter, letter);
}

void Panel::wheelEvent(QWheelEvent *e)
{
    configTouched_ = true;   // a new value arrived here
    if (!editMode_)
        return;
    const double delta = (e->angleDelta().y() > 0) ? kScaleStep : -kScaleStep;
    scale_ = std::clamp(scale_ + delta, kMinScale, kMaxScale);
    // Re-sync the window size to the new zoom (keeps the logical
    // content size, rescales the actual window).
    if (logicalSize_.isValid())
        setContentSize(logicalSize_.width(), logicalSize_.height());
    update();
    saveConfig();
}

// -- v0.3 visual helpers (mirror monster-overlay-concept.html tokens) --

QColor Panel::accentColor(Accent a) const
{
    // Values come from the shared panel-accent table (panel_accent.h) so
    // the preview and the live overlay cannot drift apart again. Keep
    // the per-arm comments pointing at the HTML tokens they mirror.
    switch (a) {
    case Accent::Player:  return mhw::panel_accent::forSlot(0);  // --accent-purple
    case Accent::Monster: return mhw::panel_accent::forSlot(1);  // --enrage orange
    case Accent::Damage:  return mhw::panel_accent::forSlot(2);  // --accent-teal
    }
    // PetDamagePanel also reaches this through Accent::Damage, so slot 3
    // shares that teal. Anything else is a caller error; the damage
    // accent keeps the default arm non-crashing.
    return mhw::panel_accent::forSlot(2);
}

void Panel::drawV03Chrome(QPainter &p, Accent accent) const
{
    // HTML tokens:
    //   --bg-panel #16181a + 1px --border #2a2d2f + 2px radius
    //   2px left accent stripe (orange for monster, purple for player, teal for damage)
    //   1px top gloss gradient (transparent → white-6% → transparent)
    //
    // Paint in *logical* coordinates, not device. paintEvent applies
    // `p.scale(scale_, scale_)` before paintPanel() runs (line 310-311),
    // so reading `rect()` here would pick up the device-pixel rect and
    // the background would be drawn at 1/scale of the content — the
    // "缩小后背景跟文字错位" reproducer (v0.7.5 audit). logicalSize_ is
    // the unscaled canvas size; combined with (0,0) origin it produces
    // a rect that the scaled painter maps back to the full device
    // surface exactly the way content rects do.
    //
    // v0.12.1: three layers, in the same order they always were — the
    // body (now a call, see drawSubWindowBody), then the left accent
    // stripe, then the 1px border. Nothing about the drawing moved; the
    // body just became addressable on its own so a fragment window can
    // get it without the other two.
    drawSubWindowBody(p);

    // Left accent stripe (4px wide, vertically centered with margin).
    // 2026-07-27: bumped from 2px → 4px so it reads as a deliberate
    // accent on the now-glassy panel rather than disappearing.
    const QRectF r(0.0, 0.0,
                   static_cast<qreal>(logicalSize_.width()),
                   static_cast<qreal>(logicalSize_.height()));
    const double stripeTop    = r.top() + 8;
    const double stripeBottom = r.bottom() - 8;
    p.setPen(Qt::NoPen);
    p.setBrush(accentColor(accent));
    p.drawRoundedRect(QRectF(r.left(), stripeTop, 4.0, stripeBottom - stripeTop),
                      1.0, 1.0);

    // 1px border (HTML: --border #2a2d2f)
    p.setBrush(Qt::NoBrush);
    p.setPen(QColor(42, 45, 47, 255));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), kChromeRadius, kChromeRadius);
}

void Panel::drawSubWindowBody(QPainter &p) const
{
    // Panel body — #16181a base + a darker bottom edge for depth
    p.setPen(Qt::NoPen);
    // Deep semi-transparent background. No blur simulation: the panel is a
    // stable dark tint over the game scene, while its data rows carry the
    // coloured progress information.
    p.setBrush(QColor(12, 14, 16, bgAlpha_));        // #0c0e10, user-adjustable
    p.drawRoundedRect(QRectF(0.0, 0.0,
                             static_cast<qreal>(logicalSize_.width()),
                             static_cast<qreal>(logicalSize_.height())),
                      kChromeRadius, kChromeRadius);
}

void Panel::drawBarV03(QPainter &p, const QRectF &rect, float pct,
                        const QColor &c, int radius) const
{
    // HTML: --c track #1d2022, fill linear-gradient(180deg, --c-hi, --c)
    const float clamped = std::clamp(pct, 0.0F, 1.0F);

    // Track
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(29, 32, 34, 255));           // --bg-cell
    p.drawRoundedRect(rect, radius, radius);

    // Fill (hi → c vertical gradient)
    if (clamped <= 0.0F)
        return;
    QRectF fill(rect.x(), rect.y(), rect.width() * clamped, rect.height());
    QLinearGradient grad(fill.topLeft(), fill.bottomLeft());
    grad.setColorAt(0.0, c.lighter(125));
    grad.setColorAt(1.0, c);
    p.setBrush(grad);
    p.drawRoundedRect(fill, radius, radius);

    // 1px top inner highlight
    QLinearGradient hi(rect.topLeft(), rect.bottomLeft());
    hi.setColorAt(0.0, QColor(255, 255, 255, 80));
    hi.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillRect(QRectF(fill.x(), fill.y(), fill.width(), 1), hi);
}

// v0.5 P0: explicit setters for the control console's Appearance fold.
// Both clamp into the same range the wheel editor and the constructor
// enforce. scale applies via setContentSize; opacity is composited at the
// pixel level by paintEvent's QPainter::setOpacity (setWindowOpacity is
// unsupported on layer-shell / offscreen, see setOpacity). Both
// persist only when editMode_ is true (caller can flip persist=false
// to skip disk for ephemeral drives like the canvas preview repaint).
// Pattern: the same shape as the wheel zoom but exposed to caller code.

void Panel::setScale(qreal s, bool persist)
{
    configTouched_ = true;   // a new value arrived here
    const qreal clamped = std::clamp(s, kMinScale, kMaxScale);
    if (qFuzzyCompare(clamped + 1.0, scale_ + 1.0))
        return;                                // no-op
    scale_ = clamped;
    if (logicalSize_.isValid())
        setContentSize(logicalSize_.width(), logicalSize_.height());
    update();
    if (persist && editMode_)
        saveConfig();
}

void Panel::setOpacity(qreal a, bool persist)
{
    configTouched_ = true;   // a new value arrived here
    const qreal clamped = std::clamp(a, 0.1, 1.0);
    if (qFuzzyCompare(clamped, opacity_))
        return;
    opacity_ = clamped;
    update();
    if (persist && editMode_)
        saveConfig();
}

void Panel::setBgAlpha(int a, bool persist)
{
    configTouched_ = true;   // a new value arrived here
    const int clamped = std::clamp(a, 0, 255);
    if (clamped == bgAlpha_)
        return;
    bgAlpha_ = clamped;
    update();
    if (persist && editMode_)
        saveConfig();
}

void Panel::resetToDefaults()
{
    configTouched_ = true;   // a new value arrived here
    sectionMask_  = 0xFFFFFFFFu;
    scale_        = kDefaultScale;
    opacity_      = 0.85;
    bgAlpha_      = 170;
    margins_      = defaultMarginsFor(corner_);
    // The size floors are user intent, so "reset" has to clear them: a
    // window left at minW=356 would otherwise survive a factory reset
    // and the user would have to rediscover the same sixteen presses.
    // The SET size is cleared for the same reason, one sentence for the
    // same defect: a locked window must not survive a reset either.
    minW_ = 0;
    minH_ = 0;
    lockW_ = 0;
    lockH_ = 0;
    if (logicalSize_.isValid())
        setContentSize(logicalSize_.width(), logicalSize_.height());
    update();   // paintEvent re-composites at the reset alpha (0.85)
    saveConfig();
}

void Panel::applyMinSizeFloor(int &w, int &h) const
{
    // The shared helpers, not a bare std::max here: a sub-window's paint
    // path has to reach the same answer (see effectiveWidth's note), and
    // two spellings of one rule is how the strip's reserve/draw pair
    // drifted apart in the first place. minW_/minH_ are already clamped
    // to [0, kMaxMinSize] on read, so the "negative floor means absent"
    // branch inside the helper is belt-and-braces for a direct caller.
    //
    // Floor first, lock second. Both are identities when their value is
    // 0, which is what makes the pre-lock path bit-for-bit identical:
    // a section with no lockW/lockH keys runs this exact code and
    // max(w, 0) == w == lockedWidth(w, 0).
    w = mhw::panel_sizing::effectiveWidth(w, minW_);
    h = mhw::panel_sizing::effectiveHeight(h, minH_);
    w = mhw::panel_sizing::lockedWidth(w, lockW_);
    h = mhw::panel_sizing::lockedHeight(h, lockH_);
}

void Panel::nudgeMinWidth(int delta)
{
    configTouched_ = true;   // a new value arrived here
    const int next = std::clamp(minW_ + delta, 0, kMaxMinSize);
    if (next == minW_)
        return;
    minW_ = next;
    // Repaint re-derives the content size and setContentSize() applies
    // the new floor, so the window follows on the next frame. The same
    // idempotent guard that stops the compositor flicker applies here.
    update();
}

void Panel::nudgeMinHeight(int delta)
{
    configTouched_ = true;   // a new value arrived here
    const int next = std::clamp(minH_ + delta, 0, kMaxMinSize);
    if (next == minH_)
        return;
    minH_ = next;
    update();
}

void Panel::nudgeLockWidth(int delta)
{
    configTouched_ = true;   // a new value arrived here
    // THE FIRST PRESS STARTS FROM THE WINDOW AS IT IS NOW, not from 0.
    //
    // This is the difference between the lock dial being usable and being
    // a trap. A naive `lockW_ + delta` starts at 0, so the first
    // Ctrl+= on a 300px-wide wirebug strip sets the lock to 16 and the
    // window collapses to 16 — the opposite of what any size dial should
    // do on its first press. Reading the current width instead means the
    // first press is one 16px step from where the user was looking,
    // which is the same relative behaviour the margin arrows have.
    //
    // logicalSize_ rather than size(): size() is the device-pixel extent
    // (w * scale_), and a lock is a logical measure the paint path
    // compares against the content's own logical number. On a fresh
    // panel logicalSize_ may be unset (-1), which the clamp handles as
    // "start from nothing".
    const int from = (lockW_ != 0) ? lockW_ : logicalSize_.width();
    const int next = std::clamp(from + delta, 0, kMaxMinSize);
    if (next == lockW_)
        return;
    lockW_ = next;
    // 0 is "no lock", so dialling below the step clears it rather than
    // parking a 0 that reads as a zero-width window.
    update();
}

void Panel::nudgeLockHeight(int delta)
{
    configTouched_ = true;   // a new value arrived here
    const int from = (lockH_ != 0) ? lockH_ : logicalSize_.height();
    const int next = std::clamp(from + delta, 0, kMaxMinSize);
    if (next == lockH_)
        return;
    lockH_ = next;
    update();
}

void Panel::setLockWidth(int w)
{
    configTouched_ = true;   // a new value arrived here
    const int next = std::clamp(w, 0, kMaxMinSize);
    if (next == lockW_)
        return;
    lockW_ = next;
    update();
}

void Panel::setLockHeight(int h)
{
    configTouched_ = true;   // a new value arrived here
    const int next = std::clamp(h, 0, kMaxMinSize);
    if (next == lockH_)
        return;
    lockH_ = next;
    update();
}

int Panel::appliedWidth(int contentWidth) const
{
    // The funnel, callable from a paint path. Order and guards mirror
    // applyMinSizeFloor() exactly — this is not a second spelling of the
    // rule, it is the SAME funnel reached from the other side, and the
    // idempotency the header promises (appliedWidth(appliedWidth(c)) ==
    // appliedWidth(c)) is what makes setContentSize(appliedWidth(c), ..)
    // correct rather than a double application.
    int w = mhw::panel_sizing::effectiveWidth(contentWidth, minW_);
    return mhw::panel_sizing::lockedWidth(w, lockW_);
}

int Panel::appliedHeight(int contentHeight) const
{
    int h = mhw::panel_sizing::effectiveHeight(contentHeight, minH_);
    return mhw::panel_sizing::lockedHeight(h, lockH_);
}
