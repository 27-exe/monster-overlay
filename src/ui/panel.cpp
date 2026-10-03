#include "panel.h"

#include "panel_accent.h"
#include "panel_sizing.h"

#include <LayerShellQt/Window>

#include <QCoreApplication>
#include <QGuiApplication>
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

QSettings &settings()
{
    static QSettings s(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("monster-overlay"),
                       QStringLiteral("panels"));
    return s;
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

} // namespace

Panel::Panel(const QString &settingsKey, Corner corner, QWidget *parent)
    : QMainWindow(parent)
    , key_(settingsKey)
    , corner_(corner)
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
    QScreen *targetScreen = nullptr;
    if (!outputName_.isEmpty()) {
        const auto screens = QGuiApplication::screens();
        for (QScreen *s : screens) {
            if (s && s->name() == outputName_) { targetScreen = s; break; }
        }
    }
    if (!targetScreen)
        targetScreen = QGuiApplication::primaryScreen();
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

void Panel::loadConfig()
{
    const QMargins def = defaultMarginsFor(corner_);
    settings().beginGroup(key_);
    // Read the stored value verbatim — NO clampMargin here. Clamping at
    // read time is what made a 4K position unrecoverable: the console
    // opens with fewer screens attached and the mere act of reading
    // destroyed the position the user chose on the output they weren't
    // carrying. The screen-relative clamp is applied at push time
    // (pushMargins) instead, so the stored intent survives a monitor
    // unplug and is re-applied the moment that output returns.
    // .toInt() on a corrupt value yields 0, which is a safe default.
    margins_.setLeft(settings().value(QStringLiteral("ml"), def.left()).toInt());
    margins_.setTop(settings().value(QStringLiteral("mt"), def.top()).toInt());
    margins_.setRight(settings().value(QStringLiteral("mr"), def.right()).toInt());
    margins_.setBottom(settings().value(QStringLiteral("mb"), def.bottom()).toInt());
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
    scale_ = settings().value(QStringLiteral("scale"), kDefaultScale).toDouble();
    // Minimum size floors, read verbatim like the margins above and
    // clamped only to a sane range. NO clamp against the screen: a
    // floor wider than the attached display is still the user's
    // intent, and silently reducing it here is the same data loss that
    // made a 4K margin unrecoverable. A corrupt value yields 0 from
    // .toInt(), which reads as "no floor" — the same fallback every
    // other key here relies on.
    minW_ = std::clamp(settings().value(QStringLiteral("minW"), 0).toInt(),
                       0, kMaxMinSize);
    minH_ = std::clamp(settings().value(QStringLiteral("minH"), 0).toInt(),
                       0, kMaxMinSize);
    opacity_ = settings().value(QStringLiteral("opacity"), 1.0).toDouble();
    bgAlpha_ = std::clamp(settings().value(QStringLiteral("bgAlpha"), 170).toInt(), 0, 255);
    // v0.8: per-panel screen selection. Empty = use whatever the OS calls
    // primary. A non-empty value matches QScreen::name() in
    // QGuiApplication::screens() — on Niri that string is exactly the
    // output name (eDP-1 / DP-1 / HDMI-A-1 …), so the value is also
    // what wlr-randr / kscreen-doctor would print.
    outputName_ = settings().value(QStringLiteral("outputName")).toString();
    settings().endGroup();

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
    settings().beginGroup(key_);
    settings().setValue(QStringLiteral("ml"), margins_.left());
    settings().setValue(QStringLiteral("mt"), margins_.top());
    settings().setValue(QStringLiteral("mr"), margins_.right());
    settings().setValue(QStringLiteral("mb"), margins_.bottom());
    settings().setValue(QStringLiteral("scale"), scale_);
    settings().setValue(QStringLiteral("minW"), minW_);
    settings().setValue(QStringLiteral("minH"), minH_);
    settings().setValue(QStringLiteral("opacity"), opacity_);
    settings().setValue(QStringLiteral("bgAlpha"), bgAlpha_);
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
    settings().endGroup();
    settings().sync();
}

void Panel::saveAppearance()
{
    settings().beginGroup(key_);
    settings().setValue(QStringLiteral("scale"), scale_);
    settings().setValue(QStringLiteral("minW"), minW_);
    settings().setValue(QStringLiteral("minH"), minH_);
    settings().setValue(QStringLiteral("opacity"), opacity_);
    settings().setValue(QStringLiteral("bgAlpha"), bgAlpha_);
    // v0.5: also persist margins so the overlay's loadConfig()
    // picks up the position the user set in the console. Same
    // read-verbatim rule as loadConfig(): the console's preview panels are
    // shown on the screens the user has attached NOW, so writing the raw
    // (unclamped) margins_ here preserves a position chosen on an output
    // that happens to be unplugged at the moment the console is open.
    // The screen-relative clamp is applied at push time, not at rest.
    settings().setValue(QStringLiteral("ml"), margins_.left());
    settings().setValue(QStringLiteral("mt"), margins_.top());
    settings().setValue(QStringLiteral("mr"), margins_.right());
    settings().setValue(QStringLiteral("mb"), margins_.bottom());
    // v0.8: persist per-panel screen selection. The control console
    // writes this from setOutputName(); loadConfig() reads it back
    // on the overlay's next start.
    settings().setValue(QStringLiteral("outputName"), outputName_);
    settings().endGroup();
    settings().sync();
}

void Panel::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    // KWin 6.7 auto-blurs all semi-transparent windows; there is no
    // client-side opt-out (org_kde_kwin_blur protocol was removed,
    // replaced by ext_background_effect_v1 which Qt/KDE dont bind yet).
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
    // any of those would still land a pixel, and "chromeless" has to
    // mean "the sub-window's own capsule pixels are the only pixels".
    //
    // PIXEL-PARITY NOTE: with chromeVisible_ == true (the default, and
    // the value all four shipped panels run with) this call is exactly
    // drawV03Chrome(p, accent) — same painter, same arguments, same
    // order in paintEvent. Nothing between here and the backing store
    // can distinguish the two paths, which is why switching a subclass
    // from drawV03Chrome() to paintPanelChrome() is behaviour-neutral
    // today and becomes the opt-out tomorrow.
    if (chromeVisible_)
        drawV03Chrome(p, accent);
}

void Panel::paintEvent(QPaintEvent *)
{
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
    // alphaBufferSize > 0 → QWaylandWindow::isOpaque() is false → KWin
    // enables blending. This is the only path that puts alpha into the
    // actual buffer the compositor sees.
    p.setOpacity(opacity_);

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

    // `-` and `=` dial the minimum width; with Shift they dial the
    // minimum height. They sit here, after the live-mode bail-out, so a
    // stray press in-game cannot resize an overlay over the action, and
    // BEFORE the arrow switch so a subclass overriding keyPressEvent still
    // gets every key it did not claim — the same passthrough contract the
    // strip's [ ] dial relies on.
    //
    // Why this pair and not , and . or Shift+arrows: `-` and `=` are the
    // unshifted and shifted faces of ONE physical key on a US layout, so
    // the two halves cost a single hand position, and they read as "minus
    // / plus" the way the arrow keys read as "left / right". Shift means
    // the same thing it already means for the arrows — the other axis of
    // the same quantity — instead of a mode the user has to be inside.
    {
        const bool shift = (e->modifiers() & Qt::ShiftModifier) != 0;
        int delta = 0;
        if (e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore)
            delta = -kMinSizeStep;
        else if (e->key() == Qt::Key_Equal || e->key() == Qt::Key_Plus)
            delta = kMinSizeStep;
        if (delta != 0) {
            if (shift)
                nudgeMinHeight(delta);
            else
                nudgeMinWidth(delta);
            return;
        }
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
    const QScreen *screen = nullptr;
    if (!outputName_.isEmpty()) {
        const auto screens = QGuiApplication::screens();
        for (QScreen *s : screens) {
            if (s && s->name() == outputName_) { screen = s; break; }
        }
    }
    if (!screen)
        screen = QGuiApplication::primaryScreen();

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
        QScreen *targetScreen = nullptr;
        if (!outputName_.isEmpty()) {
            const auto screens = QGuiApplication::screens();
            for (QScreen *s : screens) {
                if (s && s->name() == outputName_) { targetScreen = s; break; }
            }
        }
        if (!targetScreen)
            targetScreen = QGuiApplication::primaryScreen();
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
        const auto screens = QGuiApplication::screens();
        bool ok = false;
        for (QScreen *s : screens) {
            if (s && s->name() == name) { ok = true; break; }
        }
        if (!ok) {
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
    outputName_ = name;
    applyGeometry();
    if (persist)
        saveAppearance();
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

// -- v0.3 visual helpers (mirrors monster-overlay-concept.html tokens) --

QColor Panel::accentColor(Accent a) const
{
    // Values come from the shared panel-accent table (panel_accent.h) so
    // the preview and the live overlay cannot drift apart again. Keep the
    // per-arm comments pointing at the HTML tokens they mirror.
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
    const QRectF r(0.0, 0.0,
                   static_cast<qreal>(logicalSize_.width()),
                   static_cast<qreal>(logicalSize_.height()));
    constexpr double kRadius = 2.0;
    const QColor accentCol = accentColor(accent);

    // Panel body — #16181a base + a darker bottom edge for depth
    p.setPen(Qt::NoPen);
    // Deep semi-transparent background. No blur simulation: the panel is a
    // stable dark tint over the game scene, while its data rows carry the
    // coloured progress information.
    p.setBrush(QColor(12, 14, 16, bgAlpha_));        // #0c0e10, user-adjustable
    p.drawRoundedRect(r, kRadius, kRadius);

    // Top gloss line — REMOVED 2026-07-27 (added subtle smudge on top edge
    // that read as blur rather than crisp glass; the panel body's ~28%
    // alpha now gives all the translucency we need.)

    // Left accent stripe (4px wide, vertically centered with margin).
    // 2026-07-27: bumped from 2px → 4px so it reads as a deliberate
    // accent on the now-glassy panel rather than disappearing.
    const double stripeTop    = r.top() + 8;
    const double stripeBottom = r.bottom() - 8;
    p.setBrush(accentCol);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(r.left(), stripeTop, 4.0, stripeBottom - stripeTop),
                      1.0, 1.0);

    // 1px border (HTML: --border #2a2d2f)
    p.setBrush(Qt::NoBrush);
    p.setPen(QColor(42, 45, 47, 255));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
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
    sectionMask_  = 0xFFFFFFFFu;
    scale_        = kDefaultScale;
    opacity_      = 0.85;
    bgAlpha_      = 170;
    margins_      = defaultMarginsFor(corner_);
    // The size floors are user intent, so "reset" has to clear them: a
    // window left at minW=356 would otherwise survive a factory reset
    // and the user would have to rediscover the same sixteen presses.
    minW_ = 0;
    minH_ = 0;
    if (logicalSize_.isValid())
        setContentSize(logicalSize_.width(), logicalSize_.height());
    update();   // paintEvent re-composites at the reset alpha (0.85)
    saveConfig();
}

void Panel::applyMinSizeFloor(int &w, int &h) const
{
    // The shared helper, not a bare std::max here: a sub-window's paint
    // path has to reach the same answer (see effectiveWidth's note), and
    // two spellings of one rule is how the strip's reserve/draw pair
    // drifted apart in the first place. minW_/minH_ are already clamped
    // to [0, kMaxMinSize] on read, so the "negative floor means absent"
    // branch inside the helper is belt-and-braces for a direct caller.
    w = mhw::panel_sizing::effectiveWidth(w, minW_);
    h = mhw::panel_sizing::effectiveHeight(h, minH_);
}

void Panel::nudgeMinWidth(int delta)
{
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
    const int next = std::clamp(minH_ + delta, 0, kMaxMinSize);
    if (next == minH_)
        return;
    minH_ = next;
    update();
}
