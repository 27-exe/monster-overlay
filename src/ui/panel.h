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
// Forward-declared with the default underlying type spelled out,
// because an enum-class declaration without one fixes its underlying
// type to `int` and a forward declaration that disagrees with the
// later definition is an ODR violation. GameId's real home is
// monster/monster_types.h (an enum class with no explicit type, so
// int); Panel must not include a monster header to name it.
enum class GameId : int;
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


    // ─── the polymorphic layout contract ─────────────────────────────
    //
    // What a split sub-window answers about its own geometry, so the
    // console's preview, main.cpp's layout arithmetic and the layout
    // consistency test can ask any window the same ten questions without
    // knowing which window it is holding.
    //
    // This is ONE section on purpose. The hooks used to sit in three
    // places — some under `protected:` beside the paint helpers, two
    // stranded next to the constructor, and several overrides buried in
    // the subclasses' own private sections — which is how a test came to
    // need three different tricks just to read a window's size. A single
    // public vocabulary has no such seams.
    //
    // Every default answers "I do not do that", never a number: the four
    // shipped panels answer 0 for all ten and are therefore unaffected by
    // these hooks existing at all. A 0 default is also a stronger promise
    // than an empty virtual body, because it makes "does this window have
    // a direction?" a question with an answer rather than a call that
    // silently does nothing.

    // ── the extent, before the user's floor and lock ──
    //
    // contentWidth() / contentHeight() are what the CONTENT asks for. The
    // floor (minW/minH) and the lock (lockW/lockH) are applied inside
    // setContentSize(), so a caller can ask "how wide does this want to
    // be?" without also having to model the user's dials.
    virtual int  contentWidth() const { return 0; }
    virtual int  contentHeight() const { return 0; }

    // ── the wrapped-row dial ──
    //
    // columns() is the user's current "how many per row", nudgeColumns()
    // applies a dial step, and supportsColumns() is the honest "would this
    // dial do anything" — the console greys the key instead of letting a
    // user press a dead one. Default false, and only the row-shaped
    // windows override it.
    virtual int  columns() const { return 0; }
    virtual void nudgeColumns(int delta) { (void)delta; }
    virtual bool supportsColumns() const { return false; }

    // One step of the columns dial. A named constant rather than a literal
    // at the call site because the [ / ] handler in Panel::keyPressEvent
    // and the subclass's own nudgeColumns() both apply it, and the two
    // must step by the same amount — a dial whose key and whose call
    // disagree is a dial that feels broken in a way no screenshot shows.
    static constexpr int kColumnsStep = 1;

    // ── what a LAYOUT reads to derive a wrap ──
    //
    // Not the dial: a window's item count is a property of its data, the
    // dial is a property of the user's choice. These four let the wrap be
    // derived rather than re-measured per window.
    virtual int  itemCount() const { return 0; }
    virtual int  naturalItemWidth() const { return 0; }
    virtual int  itemGap() const { return 0; }
    virtual int  rowPitch() const { return 0; }

    // ── the outer inset ──
    //
    // Reported so every window in the ring can be asked for the number
    // instead of the ring trusting that seven private constants happen to
    // agree. They did not: the status strip's own margin was 6 while
    // every other window's was 11, which is exactly the drift a single
    // query is here to prevent.
    virtual int  contentMargin() const { return 0; }

    // ── what the user has asked the window to be ──
    //
    // The floor and the lock, both readable from outside. A layout — or a
    // probe, or the console's preview — needs to answer "what will this
    // window actually be", and the answer is max(content, floor) with the
    // lock overriding both. Reporting only the content half is what makes
    // a stale floor invisible: the window is quietly four pixels taller
    // than anything the code says it should be.

    // The FLOOR: never narrower than this, however small the content.
    int  minWidth() const { return minW_; }
    int  minHeight() const { return minH_; }
    // The SET size: a non-zero lock makes the window exactly this wide
    // whatever the content measured. See panel_sizing.h's lockedWidth().
    int  lockWidth() const { return lockW_; }
    int  lockHeight() const { return lockH_; }

    // `subWindowIndex` is the window's row in its parent's split table, or
    // 0 for a base panel. It has to arrive HERE rather than through a
    // setter because loadConfig() runs inside this constructor, and the
    // index only affects loadConfig()'s first-run default — see
    // defaultMarginsForSubWindow() in panel.cpp for why a setter would
    // always be too late.
    //
    // Defaulted to 0 so the four base panels and every existing
    // constructor call compile and behave exactly as before.
    Panel(const QString &settingsKey, Corner corner, QWidget *parent = nullptr,
          int subWindowIndex = 0);

    // Schedule a repaint. Needed externally because the per-panel
    // update(Data) overloads shadow QWidget::update().
    void triggerUpdate() { update(); }

    void loadConfig();
    void saveConfig();
    void setEditMode(bool on);

    // Ask the compositor for keyboard focus, without a click.
    //
    // WHY THIS EXISTS. In edit mode every panel's layer-shell surface is
    // put into KeyboardInteractivityOnDemand (setEditMode), which makes
    // it ELIGIBLE for focus rather than focused. Eligibility is not focus:
    // the compositor grants it when the surface is clicked, and
    // mousePressEvent() is where that request has always been made. So a
    // user who launches `--edit` and presses an arrow key before touching
    // anything sends that key to the TERMINAL, which prints ^[[1;2B and
    // the window does not move — and Esc, which quits, fails the same way.
    //
    // It has to be a call rather than a constructor-side effect, because
    // WA_ShowWithoutActivating (panel.cpp) is deliberately on: the overlay
    // must not raise itself over a running game. That attribute is right
    // for the live HUD and wrong for the editor, where the user ASKED for
    // a tool that responds to the keyboard.
    //
    // Called for ONE window at startup, not all of them — see
    // main.cpp for why fifteen simultaneous requests is worse than none.
    void requestEditFocus();

    // Re-ask for focus once the surface is actually on screen.
    //
    // WHY requestActivate() ALONE WAS NOT ENOUGH. v0.15 called it from
    // main.cpp before app.exec() and it did nothing: on a live Hyprland
    // session the layer-shell surface is not exposed at that point, so
    // there is no window to activate. Deferring the same call into the
    // event loop is better but still unreliable - measured twice on the
    // same session, once granting and once not, with isExposed() flipping
    // between runs. A layer-shell surface with KeyboardInteractivityOnDemand
    // has no title, no taskbar entry and no window the user can click, so
    // the compositor has no reason to hand it focus on request alone.
    //
    // What does work is asking again at the moment the surface appears -
    // showEvent is the first place the mapping is guaranteed done - and
    // letting Qt's own activation path run, which retries until the window
    // can take it. Clicking still works and still wins, so this picks a
    // starting point rather than pinning the keyboard to one window.
    void requestEditFocusOnShow();

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

    // This sub-window's row in its parent's split table, or 0 for a base
    // panel. It answers ONE question — where the window sits the FIRST
    // time it is ever shown, before the user has dragged anything — and
    // it answers it as a cascade rather than a coordinate: see
    // defaultMarginsForSubWindow() in panel.cpp.
    //
    // It has to come from outside because Panel cannot know the table it
    // belongs to, and a base panel has no row. Default 0, so the four
    // shipped panels keep their own defaultMarginsFor() result unchanged.
    void setSubWindowIndex(int index) { subWindowIndex_ = index; }
    [[nodiscard]] int subWindowIndex() const { return subWindowIndex_; }

    // Edit-mode demo data is built once on first paint after entering
    // edit mode, then reused — keeps paint() O(display).
    virtual void setupDemoData() {}
    bool demoPrimed() const { return demoPrimed_; }
    void markDemoPrimed() { demoPrimed_ = true; }
    void resetDemoPrimed() { demoPrimed_ = false; }

    // ─── which game this window's DEMO DATA represents ─────────────────
    //
    // A virtual with a no-op base, and NOT a per-class overload set that
    // a caller dispatches on. The split preview host holds its windows
    // through std::unique_ptr<Panel> and has to retarget them in a loop
    // (SplitWindowPreviewHostImpl::syncAll), and a `Panel*` argument
    // matches an exact `syncGameForDemo(Panel*, GameId)` overload
    // BETTER than any Derived* one — overload resolution on a base
    // pointer has NO runtime dispatch, so every Derived* overload in
    // the set lost to the base one and was dead code from the day it
    // was written. The proof is a two-type probe: both entries printed
    // "NO-OP (Panel*) chosen". That is the root cause of the user's
    // 「Rise 预览显示衣装、World 预览显示翔虫」: not one of the twelve
    // Derived overloads ever ran, so every window's `game_` member sat
    // on its own initialiser — mantles' World, wirebug's Rise — and a
    // Rise preview painted mantles while a World one painted bugs.
    //
    // A virtual makes the dispatch the call site's problem the RIGHT
    // way: the base answers nothing (a panel whose demo is
    // game-agnostic), and a window whose setupDemoData() branches on
    // the game overrides this to store the member and drop the
    // one-shot seed via resetDemoPrimed() so the next paint re-runs
    // its own setupDemoData(). Live data is unaffected: update(snap)
    // overwrites the whole model.
    //
    // The same twelve windows keep their own non-virtual
    // setGameForDemo() for main.cpp, which calls them on concrete
    // stack objects. Overriding the virtual is the ONE thing that
    // makes the constructor-free path work.
    virtual void setDemoGame(mhw::GameId) {}

    // The game this window's DEMO is currently seeded for.
    //
    // WHY THIS EXISTS ALONGSIDE setDemoGame(). setDemoGame() takes an
    // argument, so reading back what it did needs a second virtual — and
    // the alternative, which this round tried first, is reading it through
    // hasVisibleContent(). That does not work: a rail switch destroys the
    // preview host and builds a new one, so any window handle taken before
    // the switch is dangling, and re-fetching one afterwards reaches Qt
    // state the new window has not realised yet. gdb caught a test doing
    // exactly that, inside QTabBar::sizeHint() under
    // Panel::hasVisibleContent().
    //
    // The seed is the CAUSE and the content is a SYMPTOM, so the cause is
    // what a test should read.
    //
    // DEFINED IN THE .cpp, and that placement is forced rather than
    // stylistic: mhw::GameId is only forward-declared here (Panel must not
    // include a monster header to name it), so an inline body could not
    // even name an enumerator - `mhw::GameId::World` is "not a member"
    // against an incomplete type, for every TU that includes panel.h
    // without the monster header. The value it returns is the namespace
    // default, which is also what setGameForDemo()'s no-op base starts
    // from: a window that never moved and a window that was told World
    // read identically, and a test must not depend on telling them apart.
    // Whether this window's position was ever SAVED, as opposed to being
    // whatever the first-run default happens to be.
    //
    // The wall in control_panel.cpp asks this before it lays a cell, so a
    // user who dragged a sub-window in the console keeps that position
    // instead of being handed the grid on the next rail switch.
    //
    // "Is this the default" is NOT a question the stored NUMBERS can
    // answer: the first-run default is itself a real coordinate (the
    // subWindowIndex cascade in defaultMarginsForSubWindow), so `ml == 20`
    // is indistinguishable from "never moved" and "moved to 20" — the
    // cascade was deliberately given the same numbers the wall would have
    // used. Only the FILE knows, and this asks it.
    //
    // Read-only, and deliberately not a setter: a caller able to mark a
    // window as moved would be the caller deciding the user's intent.
    // Declared here and defined in panel.cpp because settings() is a
    // TU-local handle, the same constraint that forces demoGame() out of
    // line.
    [[nodiscard]] bool hasStoredPosition() const;

    [[nodiscard]] virtual mhw::GameId demoGame() const;

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

    // The settings key this window persists under, e.g. "player-mantles".
    // Stable where a table index is not: the split table's row order is free
    // to change, and anything that hardcodes an index would silently start
    // reading a different window the day it does.
    [[nodiscard]] const QString &settingsKey() const { return key_; }

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

    // ─── the settings group this window persists into ──────────────────
    //
    // v0.14: every panel's section is per game AND per layout mode
    // (`world/single/player-status` under World in single mode,
    // `world/split/player-status` under World in split mode) so neither
    // the two games nor one game's two topologies can overwrite each
    // other's window geometry — the reason is in panel_settings_game.h,
    // the migration rule in panel.cpp.
    //
    // Why a protected method and not just reusing `key_`: this is the
    // spelling a SUBCLASS needs when it opens its own QSettings handle,
    // which the three column-dial windows do (menu of reasons in
    // panel_player_split.cpp's saveConfig). They used to reach for
    // QSettings(IniFormat, UserScope, "monster-overlay", "panels") and
    // the bare group name each, which is four spellings of one file
    // plus four of one prefix. Routing the prefix through here keeps
    // the concatenation in exactly one place.
    //
    // `settingsGroup()` is the group with the prefix applied. A caller
    // that wants to open the same file the base class uses can
    // reconstruct it from panelSettingsGame() / panelSettingsLayout()
    // and the four literals the base owns — but it should use this for
    // the group.
    [[nodiscard]] QString settingsGroup() const;

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
    // 0 = a base panel, or a sub-window's first row. Only loadConfig()
    // reads it, and only to pick a first-run default; see setSubWindowIndex().
    int subWindowIndex_ = 0;
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
