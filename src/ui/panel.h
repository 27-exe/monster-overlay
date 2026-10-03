#pragma once

#include <QColor>
#include <QMainWindow>
#include <QMargins>
#include <QRectF>
#include <QSettings>
#include <QSize>
#include <QString>
#include <QWidget>
#include <cstdint>


namespace mhw {
struct GameSnapshot;
}

// Which screen corner a panel is anchored to. Wayland layer-shell
// cannot place windows at arbitrary screen coordinates — position is
// defined solely by the anchor corner plus margins (offset from the
// anchored edges). Keyboard arrow keys nudge margins in edit mode.
enum class Corner {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

// Base class for all overlay panels. Handles layer-shell setup
// (anchor corner + margins), position persistence (via QSettings
// INI), and keyboard-driven positioning in --edit mode.
class Panel : public QMainWindow {
    Q_OBJECT
public:
    Panel(const QString &settingsKey, Corner corner, QWidget *parent = nullptr);

    // Schedule a repaint. Needed externally because the per-panel
    // update(Data) overloads shadow QWidget::update().
    void triggerUpdate() { update(); }

    void loadConfig();
    void saveConfig();
    void setEditMode(bool on);
    bool editMode() const { return editMode_; }

    // Section visibility: per-panel bitmask of independently toggleable
    // blocks (see ui/panel_sections.h). Default = all sections on so
    // live mode is unaffected. The control panel flips individual bits.
    void setSectionMask(uint32_t mask) { sectionMask_ = mask; update(); }
    [[nodiscard]] uint32_t sectionMask() const { return sectionMask_; }

    // Master visibility: a separate flag from the section mask. When
    // false, the panel does not show its layer-shell surface at all
    // (no chrome, no title row, no empty content — truly unmounted).
    // This is independent of sectionMask_ on purpose: turning every
    // section off keeps the panel alive so the user can see where it
    // sits, but disabling the panel itself removes it entirely. The
    // control console's master toggle maps to this flag, not the
    // section mask.
    void setPanelEnabled(bool on);
    [[nodiscard]] bool panelEnabled() const { return panelEnabled_; }

    // ─── chrome (background + accent stripe + border) ───────────────────
    //
    // v0.12: explicit switch for the panel chrome that drawV03Chrome()
    // paints — the dark rounded background, the 4px left accent stripe
    // and the 1px border. Default = true, so a stock Panel paints
    // exactly the pixels v0.11.4 painted; nothing about the four
    // existing panels changes unless someone flips this.
    //
    // WHY THIS EXISTS: a sub-window carved out of a panel (v0.12's
    // capsule strips — debuff / buff / mantle / wirebug, per
    // design/01-hunterpie-split.md §10.2) draws its own capsule
    // geometry and must NOT carry panel chrome around it. It is also
    // the lever for the measured "378x50 title band" defect: a
    // mask=0 PlayerPanel does not vanish, it collapses to a title-only
    // strip with ink spanning [0..366] (panel_player.cpp `totalH =
    // kMargin + kTitleH` — "title always drawn"). A sub-window does
    // not inherit that shape once chrome is off and it sizes itself to
    // its own content.
    //
    // Per-instance, not static: the same binary hosts chromed windows
    // and chromeless ones.
    //
    // NOT persisted in panels.ini: chrome is a fact about how the
    // subclass paints (sub-windows set it in their own constructor),
    // not one of the user's position/appearance tunables — same
    // separation that led to dropping the `visible` key (see
    // saveConfig() in panel.cpp).
    void setChromeVisible(bool on);
    [[nodiscard]] bool chromeVisible() const { return chromeVisible_; }

    // ─── sub-window identity ────────────────────────────────────────
    //
    // v0.12: a window carved out of a panel by the split (debuff /
    // buff / mantle / wirebug capsule strip). Self-declared, not
    // inferred: the base answers false, and a sub-window subclass
    // overrides it to return true —
    //
    //     bool DebuffStrip::isSubWindow() const override { return true; }
    //
    // An override rather than a protected setter on purpose: it needs no
    // new state, no friend, no widening of Panel's API surface, and it
    // reads as the compile-time fact it is (a class either is a
    // sub-window or is not).
    //
    // REMOVED (v0.12 cleanup): the line naming the registry that assigned
    // kPanelSubWindowBase … kPanelSlotMax-1 as "the consumer". That
    // registry never got a caller, and with it the whole slot-index
    // vocabulary went; the slot identity a split window needs now lives
    // in PlayerSection::PlayerSplitSlot, next to the bit-to-window table
    // it is keyed by (ui/panel_sections.h). The hook itself stays: it is
    // the compile-time answer to "am I a sub-window", and stripping it
    // would mean editing the one panel that overrides it.
    //
    // Default false. No base-panel code path reads it today, which is
    // the point: the four shipped panels keep answering "no" and every
    // existing PanelSourceAdapter keeps reporting isSubWindow()==false.
    [[nodiscard]] virtual bool isSubWindow() const { return false; }

    // Edit-mode demo data is built once on first paint after entering
    // edit mode, then reused — keeps paint() O(display).
    virtual void setupDemoData() {}
    bool demoPrimed() const { return demoPrimed_; }
    void markDemoPrimed() { demoPrimed_ = true; }
    void resetDemoPrimed() { demoPrimed_ = false; }

    // i18n — runtime locale switch entry point. Called by the overlay's
    // conf-watch loop (src/main.cpp) right after StringTable::load()
    // swapped the locale: strings re-read at the draw site in paintPanel()
    // follow automatically, but everything cached at construction time
    // (window title, demo seeds) must be re-queried here. Panels with
    // such cached strings override this; the base version just repaints.
    // Additive: the console's ControlPanel keeps its own retranslateUi().
    virtual void retranslateUi() { update(); }

    void setVisible(bool visible);
    [[nodiscard]] QSize contentSize() const { return logicalSize_; }

    // Read-only geometry/state for the control console's preview canvas.
    // The console paints the panels at the position they will actually
    // occupy on screen: anchor corner + persisted margins, scaled by the
    // user's zoom. Keeping these as getters (not setters) means the
    // canvas is always a passive view, never a driver of state.
    [[nodiscard]] Corner corner() const { return corner_; }
    [[nodiscard]] QMargins margins() const { return margins_; }
    [[nodiscard]] double scale() const { return scale_; }
    [[nodiscard]] double opacity() const { return opacity_; }
    [[nodiscard]] int bgAlpha() const { return bgAlpha_; }

    // v0.5+: explicit setters for the control console's
    // Appearance fold. Both clamp into the same range the wheel
    // editor and the constructor enforce; both apply via
    // setContentSize / setWindowOpacity; both persist only when
    // editMode_ is true (caller can flip persist=false to skip
    // disk for ephemeral drives like the canvas preview repaint).
    void setScale(qreal s, bool persist = true);
    void setOpacity(qreal a, bool persist = true);
    void setBgAlpha(int a, bool persist = true);

    // v0.5 position editing: set margins directly (clamped to [0,8000]
    // per side, or tighter where a screen-relative bound applies —
    // see marginLimits() for how the ceiling is derived).
    // persist=true writes to QSettings immediately (used by the live
    // overlay's edit mode); the console passes persist=false and writes
    // on exit instead.
    //
    // Note on what is NOT persisted: the screen-relative clamp is a
    // runtime view, not a stored value. A margin captured on a 4K output
    // is left in QSettings as the user set it, so re-plugging that output
    // restores the position instead of silently rewriting it to one that
    // only fits the screen that happened to be attached last.
    void setMargins(QMargins m, bool persist = true);

    // v0.8: per-panel screen selection. Pass an empty string to revert
    // to "follow OS primary". The name must match QScreen::name() in
    // QGuiApplication::screens() — on Niri that string is exactly the
    // wlr-output name (eDP-1 / DP-1 / HDMI-A-1 / …).
    void setOutputName(QString name, bool persist = true);
    [[nodiscard]] QString outputName() const { return outputName_; }

    // ─── margins, bounds, and what the compositor actually sees ────────
    //
    // Public read-only geometry accessors the control console needs to
    // describe a panel the same way the overlay sees it. margins() alone
    // is the user's stored intent; these answer "where does that land on
    // the screen that is attached right now", which is what the console
    // must show the user (see Panel::marginLimits in panel.cpp for why
    // the two differ, and why neither is clamped at rest).
    [[nodiscard]] QMargins marginLimits() const;
    [[nodiscard]] QMargins clampMarginsToScreen(const QMargins &m) const;
    [[nodiscard]] QMargins pushedMargins() const;

    // Reset mask, scale, opacity, and margins to factory values
    // and re-sync the layer-shell surface. Always persists.
    void resetToDefaults();
    void saveAppearance();
    // Toggle the compositing effect for off-screen render() calls.
    // render() + enabled QGraphicsOpacityEffect produces a=0 (broken);
    // the console's renderPreview disables the effect, renders at full
    // alpha, and lets HudCanvas apply srcOpac itself.
    void setCompositingEnabled(bool on);

    // Public read of the panel's own "do I have anything to show" verdict.
    //
    // Every data-driven panel answers this differently — MonsterPanel from
    // its monster id, DamagePanel from the party/chart, PetDamagePanel from
    // its rebuilt rows — and the answer is what decides whether mounting the
    // layer-shell surface produces a real frame or an empty one. Exposing it
    // lets the main loop apply ONE rule to every panel (no data -> not
    // visible) instead of each branch re-deriving its own, which is how the
    // Rise damage panel ended up permanently mounted with a placeholder
    // while the World one hid correctly.
    [[nodiscard]] bool hasVisibleContent() const { return hasContent(); }

protected:
    virtual void paintPanel(QPainter &p) = 0;
    virtual void paintDemo(QPainter &) {}
    virtual bool hasContent() const { return true; }
    virtual void onSnapshot(const mhw::GameSnapshot &) { update(); }
    QWidget *canvas() { return this; }
    void setContentSize(int w, int h);

    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void showEvent(QShowEvent *e) override;

    // v0.3 visual helpers — match monster-overlay-concept.html tokens.
    enum class Accent { Player, Monster, Damage };
    //
    // v0.12: `drawV03Chrome()` is kept UNMODIFIED and remains the
    // background/stripe/border painter. The chrome switch lives in
    // `paintPanelChrome()` below, which every subclass should call
    // INSTEAD of drawV03Chrome():
    //
    //     void paintPanelChrome(QPainter &p, Accent a) const
    //     {
    //         if (chromeVisible_)
    //             drawV03Chrome(p, a);
    //     }
    //
    // That indirection is deliberate. drawV03Chrome() is called from
    // four subclasses I am NOT allowed to edit in this change (they are
    // being split concurrently), and the "default path must be
    // pixel-identical" constraint only holds if every existing call
    // site keeps resolving to the same drawing code. Subclasses that
    // want to opt out call paintPanelChrome(); the four that exist
    // today are untouched, so their pixels cannot move.
    //
    // v0.12: chrome switch entry point for subclasses. Honours
    // chromeVisible_; when false it draws NOTHING (not even a
    // background edge or a 1px border), so a chromeless sub-window
    // leaves every pixel it does not explicitly paint untouched.
    // Subclasses that keep calling drawV03Chrome() directly get the
    // historical behaviour — the switch does not retroactively touch
    // them.
    void paintPanelChrome(QPainter &p, Accent accent) const;
    void drawV03Chrome(QPainter &p, Accent accent) const;
    QColor accentColor(Accent a) const;

    // ─── the one default scale ───────────────────────────────────────
    //
    // 1.0 = "paint the layout as authored, no zoom". Declared here, next
    // to the base class that owns it, because three places must agree on
    // it and two of them are in a different TU's control:
    //
    //   1. the `scale_` member initialiser (panel.h, ~line 315),
    //   2. Panel::loadConfig()'s "key absent" default (panel.cpp),
    //   3. Panel::resetToDefaults()'s factory value (panel.cpp).
    //
    // They disagreed until now: (1) and (3) said 1.0, (2) said 2.0 — a
    // v0.3 leftover from when a panel was a 378px-wide chrome window and
    // doubling it oversized nothing. On a v0.12 sub-window (~91px wide)
    // the same 2.0 turns 7pt text into 14pt inside a 182px surface, which
    // is the "zoom too big, text unusable" defect the user reported.
    //
    // WHY 1.0 AND NOT SOMETHING ELSE: it is the value that needs no
    // per-panel tuning, because 1.0 reproduces the authored CSS/HTML
    // geometry every panel was measured against (kPanelW = 378 for
    // player/monster/damage, kPillW tokens for the strips), and it is
    // what all four existing sections in the user's own panels.ini sit
    // near once the console's SCALE slider has been touched (0.8 / 0.91 /
    // 0.99 / 1.0). The range [0.5, 3.0] stays available for anyone who
    // wants bigger — that is what the wheel and the slider are for; a
    // default's job is to be correct, not to make a point.
    //
    // Changing this constant is the ONLY way to move the default. Never
    // re-introduce a literal at a call site: the whole defect was a
    // literal that outlived its commit's justification.
    static constexpr double kDefaultScale = 1.0;
    // Status-coloured bar (HTML: --c-hi gradient over --c).
    void drawBarV03(QPainter &p, const QRectF &rect, float pct,
                    const QColor &c, int radius = 3) const;

    // Minimized: show a small colored block with the panel's
    // first letter instead of the full layout. Toggle with Space.
    bool minimized() const { return minimized_; }

private:
    void applyGeometry();
    void setLayerKeyboardInteractivity(bool interactive);
    void nudgeMargins(int dx, int dy);
    void paintMinimized(QPainter &p);

    // Push pushedMargins() (declared public) at the layer-shell surface.
    // Called from applyGeometry() so a surface (re)created because an
    // output came back — or because hypr-output-failsafe rebuilt it —
    // lands inside the screen it now resolves on instead of staying
    // off-screen, and from onOutputsChanged() so a hot-plug event
    // re-derives the margin against the output we can see now.
    void pushMargins() const;

    // Store `m` (clamped) and push it. Returns true when the stored value
    // actually changed, so callers can skip redundant QSettings writes.
    // This is the mutator path: a user-driven move SHOULD be clamped
    // before it reaches disk, because the user chose it while looking at
    // a real screen.
    [[nodiscard]] bool commitMargins(const QMargins &m);

    // Hot-plug re-push. Re-resolves to the screen we now point at and
    // re-pushes the screen-relative margins so a position stored on a
    // bigger output does not stay off-screen on the smaller one that
    // replaced it. Never touches margins_, so the user's stored intent is
    // preserved for when the original output comes back. Connected to
    // QGuiApplication's screenAdded / screenRemoved /
    // primaryScreenChanged in the ctor.
    void onOutputsChanged();

    bool demoPrimed_{false};

    // See setSectionMask(). Each subclass interprets the bits via the
    // matching namespace in panel_sections.h (PlayerSection / ...).
    uint32_t sectionMask_{0xFFFFFFFFu};
    bool panelEnabled_{true};
    // Chrome on/off — see setChromeVisible(). Default true is what makes
    // this change a no-op for the four shipped panels.
    bool chromeVisible_{true};

    QString key_;
    Corner corner_;
    QSize logicalSize_;
    QMargins margins_;
    double scale_{Panel::kDefaultScale};
    double opacity_{0.85};
    int bgAlpha_{170};   // panel background alpha (0-255), default ~67%
    // v0.8: per-panel screen selection. Empty = follow primaryScreen().
    // Set by setOutputName() / loaded by loadConfig() from
    // [panels.ini <key>]/outputName. Matched against QScreen::name()
    // in applyGeometry() — on Niri that string IS the wlr-output name.
    QString outputName_;
    bool editMode_{false};
    bool minimized_{false};
    QSize normalSize_;   // remembered full-layout size
};
