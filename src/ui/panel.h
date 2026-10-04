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
    // v0.12.1: THE BODY IS NOT CHROME. Turning the switch off used to
    // suppress all three drawV03Chrome() layers, and every sub-window the
    // v0.12 split carved out of PlayerPanel sets it — so all six of them
    // rendered with NO background at all. That is the user's report,
    // verbatim: 「只有那个 Player 玩家那个面板有半透明背景，其余的背景
    // 全部丢失」. Frame and surface answer two different questions —
    // "is this a panel, or a fragment of one?" (stripe + hairline) and
    // "what is the fragment drawn on top of?" (translucent body) — and
    // one boolean was only ever right for the panel.
    //
    // So the switch now governs the FRAME alone. paintEvent() paints the
    // rounded body for every window that turned the frame off, before
    // paintPanel(), so a fragment keeps its fragment shape (no stripe,
    // no border) while sitting on the same translucent #0c0e10 the panel
    // sits on. That is the whole point of splitting a panel into
    // windows rather than into bare canvases: each window is still a
    // legible thing over the game, just not a duplicate of the panel it
    // came from.
    //
    // WHY NOT A SECOND BOOL: the surface's own alpha is already
    // per-window state (`bgAlpha`, persisted under each settings key, so
    // the six of them already carry their own slider row), and "I am a
    // fragment, not a panel" is already the single declaration the six
    // subclasses make. `chromeVisible_ == false` IS that declaration —
    // deriving the surface from it adds no state, and it cannot be set
    // on a main panel without that panel also declaring itself a
    // fragment.
    //
    // WHY THIS EXISTS: a sub-window carved out of a panel (v0.12's
    // capsule strips — debuff / buff / mantle / wirebug / bars / weapon
    // / info, per design/01-hunterpie-split.md §10.2) must not carry the
    // panel's frame around it. It is also the lever for the measured
    // "378x50 title band" defect: a mask=0 PlayerPanel does not vanish,
    // it collapses to a title-only strip with ink spanning [0..366]
    // (panel_player.cpp `totalH = kMargin + qrowStreamH + kMargin` —
    // "title always drawn"). A sub-window does not inherit that shape
    // once the frame is off and it sizes itself to its own content.
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
    // ─── the founding note of the indirection, kept verbatim ────────────
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
    // ─── v0.12.1 amendment, and what the switch really governs ──────────
    //
    // The founding note above still holds for the FRAME: the stripe and
    // the border are the frame, and only paintPanelChrome() suppresses
    // them. drawV03Chrome() itself is UNTOUCHED and still paints all
    // three layers in one go; nothing inside it moved.
    //
    // What changed is where the BODY is drawn. `drawSubWindowBody()` is
    // the body layer on its own (rounded rect, QColor(12,14,16,
    // bgAlpha_), nothing else), paintEvent() calls it for every window
    // whose frame is off, and it runs BEFORE paintPanel() so a
    // fragment's own pixels composite on top of its surface rather than
    // behind it. drawV03Chrome() keeps calling it as its own first step,
    // so a stock Panel paints the body exactly once and exactly as
    // before — no double-fill, no alpha-on-alpha.
    //
    //     paintEvent()   (virus, chrome off)          paintEvent()   (stock)
    //     ├─ drawSubWindowBody()                    ├─ paintPanelChrome()  `── drawV03Chrome()
    //     └─ paintPanel()                           │   └─ drawSubWindowBody()
    //                                                └─ paintPanel()
    //
    // The two paths are therefore observationally identical for a stock
    // panel, which is what keeps the ten single-mode baselines byte-for-
    // byte: nothing in the chromeVisible_==true path executed or drew
    // anything differently. Only the six chrome-off windows gain pixels.
    //
    // v0.12.1: chrome switch entry point for subclasses. Honours
    // chromeVisible_; when false it draws NOTHING (not even a
    // background edge or a 1px border) — see drawSubWindowBody() below
    // for the surface, which is a separate question with a separate
    // owner. Subclasses that keep calling drawV03Chrome() directly get
    // the historical behaviour — the switch does not retroactively touch
    // them.
    void paintPanelChrome(QPainter &p, Accent accent) const;
    void drawV03Chrome(QPainter &p, Accent accent) const;
    // v0.12.1: the translucent rounded body, on its own — the layer
    // drawV03Chrome() paints first, factored out so paintEvent() can
    // paint it for a window whose FRAME has been switched off. Exactly
    // one body per window in either path; see the diagram above.
    void drawSubWindowBody(QPainter &p) const;
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

    // ─── minimum size: the floor under a content-sized panel ─────────
    //
    // A sub-window's width is normally whatever its content needs, which
    // is right for a pill grid and wrong for a pair of gauges: the HP bar
    // measured "150 / 150" and "100.0%" and came out 89px of ink, a
    // quarter of the 356px the same bar gets inside the single panel.
    // The user cannot reach layer-shell's own resize (the protocol has
    // no resize semantic and the compositor does not arbitrate our
    // extent), so the adjustment has to be a value we own.
    //
    // WHY A FLOOR, AND WHY A LOCK SITS NEXT TO IT
    // --------------------------------------------
    // The floor is the historical rule: at least this wide. It answers
    // "a gauge pair measured 89px of ink, a quarter of the 356px it had
    // inside the panel, so raise it", which is an asymmetry correction
    // and cannot clip anything because content still wins when larger.
    //
    // A floor alone cannot answer "make this exactly 300 wide", because
    // for a window whose content is ALREADY wider than 300 the answer
    // is content for every press — the dial the user reported dead. So
    // lockW_ / lockH_ (above) are the second, orthogonal half: 0 = off
    // and the floor applies; non-zero = the window IS that size.
    //
    // The lock is safe for content because it is handed to the draw side
    // as a WIDTH BUDGET, from which the wrap is re-derived (see
    // panel_sizing.h's wrappedColumns): a narrow lock stacks the
    // capsules instead of cropping them, which is also the same
    // direction the user asked for in "无法通过调整来调整成竖排排列".
    //
    // 0 is the default and is the whole migration story: a section that
    // has never been dialled has no key, reads 0, and max(w, 0) == w —
    // which is why the four shipped panels' pixel baselines are
    // unaffected by any of this.
    int  minWidth() const { return minW_; }
    int  minHeight() const { return minH_; }
    // The user's SET size, when there is one: a non-zero lockW_ makes
    // the window exactly this wide, whatever the content measured. See
    // panel_sizing.h's lockedWidth() and the block below for why a lock
    // sits next to the floor instead of replacing it.
    int  lockWidth() const { return lockW_; }
    int  lockHeight() const { return lockH_; }
    // The dial's step. 16px because it is about a fifth of one capsule,
    // so six or seven presses take a strip from its content width to a
    // 300px window without the first press jumping past what the user
    // could see they were aiming at.
    static constexpr int kMinSizeStep = 16;
    // 4096, not a screen dimension: a value larger than the display is
    // legal (the window is simply wider than the output) and clamping to
    // the screen here is what made margins unrecoverable — see the
    // matching note in loadConfig(). What must be rejected is a negative
    // or absurd one, and only that.
    static constexpr int kMaxMinSize = 4096;

protected:
    // Edit-mode keys `-` / `=` and their Shift forms. Virtual so a
    // sub-window can react to a size change it renders specially — the
    // bars panel's gauge has to re-measure its ink, which is not
    // something the base class can do for it.
    virtual void nudgeMinWidth(int delta);
    virtual void nudgeMinHeight(int delta);
    // Edit-mode keys `-` / `=` with Ctrl: the SET size rather than the
    // floor. Same shape as the pair above for the same reason — a
    // sub-window whose layout is derived from the window size (the
    // strips wrap) gets its hook here, while one that is not
    // (the base panels' fixed layouts) never overrides anything.
    virtual void nudgeLockWidth(int delta);
    virtual void nudgeLockHeight(int delta);

    // Set the lock to an exact value (the generic columns dial writes a
    // natural width, not a step). Not the same as nudgeLockWidth's
    // "one step from here", so it is its own function rather than a
    // delta of 0 — a 0 delta would read as "leave it alone" to anyone
    // who knows the other two.
    void setLockWidth(int w);
    void setLockHeight(int h);

    // ─── the width a panel will actually be, at its own content size ──
    //
    // The floor-then-lock funnel, exposed. A sub-window's paint path
    // needs this because its WRAP is a function of the width it is
    // actually going to get, not of the width its content asked for:
    // a wirebug strip locked to 90px is a one-column stack, and the
    // height that reserves for it is three rows, not one.
    //
    // Two guarantees the caller relies on:
    //
    //   1. appliedWidth(c) is an IDEMPOTENT endofunction, so
    //      setContentSize(appliedWidth(c), ...) does not move the
    //      window a second time — the funnel runs again inside
    //      setContentSize() and answers the same number.
    //   2. appliedWidth(c) == c when both the floor and the lock are 0,
    //      which is every state a pre-lock panels.ini can be in.
    [[nodiscard]] int appliedWidth(int contentWidth) const;
    [[nodiscard]] int appliedHeight(int contentHeight) const;

    // ─── the columns dial, promoted to the base class ─────────────────
    //
    // WHAT IT ANSWERS: how many items of this window's horizontal row go
    // side by side. 1 is a vertical column; `count` is one flat row.
    //
    // WHY HERE AND NOT PER-SUBCLASS: it was first written on
    // PlayerStatusStripPanel as setPillsPerRow() + its own [ ] handler,
    // and that comment argued it should stay local while only one panel
    // needed it. That stopped being true the moment the user asked why
    // 翔虫 and 衣装 — also horizontal rows — could not be turned
    // vertical: three windows now want the same key for the same
    // behaviour, and per-class keyPressEvent handlers mean three copies
    // of one passthrough contract that must each remember to forward
    // every key it does not claim.
    //
    // WHY A VIRTUAL AND NOT AN ABSTRACT ONE: the default body is empty
    // and that is load-bearing. The base panels (player / monster /
    // damage / pets) and the strips that are not a row (bars, weapon,
    // info) have no column axis to dial — a virtual with a no-op default
    // lets them keep answering false from supportsColumns(), which is
    // the one value that makes the `[` `]` keys reach
    // QMainWindow::keyPressEvent() unclaimed, exactly as they do today.
    // It also means `columns()` is a safe question to ask of ANY panel
    // from the console or a test without a type test.
    //
    // THE CONTRACT:
    //   * columns()        answers the DIAL position, or 0 when this
    //     window has no column axis.
    //   * nudgeColumns(delta) moves the dial by one step, clamped by the
    //     subclass, and is a no-op when columns() is 0.
    //   * supportsColumns() is the honest "would this dial do anything"
    //     question, so a console can grey the keys instead of letting a
    //     user press a dead key. It is default false and the two rows
    //     that support it override it to true alongside the dial.
    //   * itemCount() / naturalItemWidth() / itemGap() / rowPitch() /
    //     contentMargin() are what a LAYOUT reads, as opposed to the
    //     dial, so the wrap can be derived rather than re-measured per
    //     window. They are not the dial and never were: a window's item
    //     count is a property of its data, the dial is a property of the
    //     user's choice.
    virtual int columns() const { return 0; }
    virtual void nudgeColumns(int delta) { (void)delta; }
    virtual bool supportsColumns() const { return false; }

    // One column per press. 1 was the step the status strip shipped its
    // `[` / `]` handler with (panel_player_split.cpp:kColumnsStep), and it
    // stays: a strip's column count is a small integer where any larger
    // step would jump past the interesting range in one press — from the
    // default 3 to a vertical list is two presses, not one.
    static constexpr int kColumnsStep = 1;

    // ─── the generic row-layout vocabulary a column dial is derived from
    //
    // These five answer, for a window whose content is a horizontal
    // row of N items in a window `width` px wide: how many items there
    // are, how wide and how tall one is, how much air sits between two
    // of them, and how much padding the content inset has. They exist so
    // the columns dial does not have to be re-derived per subclass: the
    // wirebug strip, the mantles strip and the status strip all answer
    // them, and the wrap arithmetic in panel_sizing.h is shared.
    //
    // DEFAULTS ARE THE "NOT A ROW" ANSWER, which is why an override is
    // safe to add: a window that does not answer them gets 1 item per
    // row of 0 px, which is the collapsed single-frame case, and every
    // caller that reaches them is a window that DID override them.
    virtual int itemCount() const { return 0; }
    // Natural width of ONE item at full size, in logical px — the
    // number a column's budget is measured against (the widest
    // capsule's column arithmetic, the widest mantle box). NOT the
    // window's content width.
    virtual int naturalItemWidth() const { return 0; }
    // Horizontal gap between two adjacent items in a row.
    virtual int itemGap() const { return 0; }
    // Vertical pitch of one wrapped row: item height + row gap.
    virtual int rowPitch() const { return 0; }
    // The margin this window's content starts at on each side. The base
    // panels' margins are not this concept — they are chrome padding —
    // which is why these default to 0 and only the content strips
    // answer them.
    virtual int contentMargin() const { return 0; }

private:
    void applyGeometry();
    void setLayerKeyboardInteractivity(bool interactive);
    void nudgeMargins(int dx, int dy);
    void paintMinimized(QPainter &p);
    // max() against the floors, then lockedWidth/Height() against the
    // user's SET size, applied inside setContentSize() — the one funnel
    // both paintPanel() and a size nudge pass through, and the only
    // place where the value Space captures into normalSize_ has already
    // seen both. A panel that took the max itself would be correct today
    // and wrong the first time a subclass forgot.
    //
    // ORDER IS LOAD-BEARING: floor first, lock second. The alternative
    // order lets a floor wider than the lock survive the lock's
    // assignment, which reads as "the = key did nothing" a second time.
    //
    // Both are applied only through the two named functions in
    // panel_sizing.h, never inline: panel_player_split_bars.cpp,
    // panel_player_split_weapon.cpp and panel_player_split_info.cpp each
    // re-apply the floor to the width they draw into, and a third
    // spelling of one rule is how the strip's reserve/draw pair drifted
    // apart. Those call sites keep calling effectiveWidth(); the lock
    // they need is added beside it, so no caller changes meaning.
    void applyMinSizeFloor(int &w, int &h) const;
    int  minW_{0};
    int  minH_{0};
    // The SET size, 0 = off. Separate from minW_/minH_ rather than a
    // mode flag on them, for the reason given in the public block:
    // 0/0 is the pre-existing state every panels.ini on disk is in, and
    // keeping it a distinct pair of members means "no lock" can never be
    // mistaken for "a lock of zero width".
    int  lockW_{0};
    int  lockH_{0};

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
