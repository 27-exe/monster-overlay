#pragma once

#include <QMargins>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <memory>
#include <vector>
#include <QVector>

namespace mhw {

// ─── Slot-count contract, v0.11.4 → v0.12 ────────────────────────────
//
// Two numbers, one floor, and one alias. The v0.11.4 file had a single
// `kPanelCount = 4` with a `static_assert(kPanelCount == 4)`, and the
// console's `std::array<PanelCtl, kPanelCount>` consumed it directly.
// Splitting panels into sub-windows (v0.12) would have forced that
// assert to move with every new sub-window, and the console's
// fixed-size arrays with it — see design/01-hunterpie-split.md §8.1
// and §8.3 ("multi-instance: an initializer registers N times").
//
// The split is:
//
//   kBasePanelCount  = 4   the four slots whose identity is FIXED and
//                          which the console and HudCanvas bind with
//                          std::array<…, kBasePanelCount>. Slot order
//                          is fixed: 0 Player, 1 Monster, 2 Damage,
//                          3 Pets. This is the number an assert may
//                          legitimately pin.
//   kPanelIndexBaseMax      exclusive upper bound of the base slots;
//                          == kBasePanelCount, spelled out so the next
//                          boundary does not have to re-derive it.
//   kPanelSubWindowBase     the FIRST index reserved for sub-windows.
//                          It is DELIBERATELY equal to the base count:
//                          no sub-window can ever alias a base slot,
//                          and no `kPanelCount`-sized array can ever be
//                          indexed by one.
//   kPanelSlotMax           exclusive upper bound for sub-window slots
//                          (kBasePanelCount … kPanelSlotMax-1), i.e. up
//                          to kMaxPanelSubWindows windows on top of the
//                          base four. A deliberately generous ceiling so
//                          the registry is not a second hard-coded 4.
//   kMaxPanelSubWindows     the capacity those extra indices cover.
//   kPanelCount     == 4   UNCHANGED NAME AND VALUE. Kept as an alias of
//                          kBasePanelCount so existing users —
//                          control_panel.h's std::array<PanelCtl,
//                          mhw::kPanelCount> and boundSources_,
//                          hud_canvas.h's std::array<Slot, …>,
//                          every `for (i < mhw::kPanelCount)` loop in
//                          control_panel.cpp / hud_canvas.cpp — compile
//                          and behave exactly as before. The alias is
//                          what makes this change a no-op for the
//                          console. Do NOT raise it: it is the BASE
//                          four, not the total window count.

// The four fixed slots. The console, the HUD preview and the accent
// table (panel_accent.h, "indexed by the fixed slot order shared with
// mhw::kPanelCount / PanelSource") all key off this.
inline constexpr int kBasePanelCount = 4;

// Boundary helpers. See isPanelIndex() below for the difference.
inline constexpr int kPanelIndexBaseMax    = kBasePanelCount;
inline constexpr int kPanelSubWindowBase   = kBasePanelCount;   // == base max
inline constexpr int kMaxPanelSubWindows   = 32;
inline constexpr int kPanelSlotMax         = kBasePanelCount + kMaxPanelSubWindows;

// Legacy/console-facing name. Same value as kBasePanelCount (== 4);
// both spellings are kept so a reader can tell whether a call site
// cares about "the four console slots" (prefer kBasePanelCount) or is
// just consuming the old contract.
inline constexpr int kPanelCount = kBasePanelCount;

static_assert(kBasePanelCount == 4,
              "the console/HUD contract requires four base panels");
// Base count and reserved sub-window floor must not collide: the moment
// kPanelSubWindowBase drops to 3 a sub-window would alias the pets slot
// and the console would drive a strip while dragging the pets panel.
static_assert(kPanelSubWindowBase >= kBasePanelCount,
              "sub-window slots must start at or above the base count");

// True for the four FIXED slots only (0 Player, 1 Monster, 2 Damage,
// 3 Pets) — the pre-v0.12 meaning, preserved verbatim. Everything that
// indexes a kPanelCount-sized array (control_panel.cpp's ctl_ /
// boundSources_, hud_canvas.cpp's slots_) must keep using THIS one,
// because those arrays have exactly four elements.
[[nodiscard]] constexpr bool isPanelIndex(int index)
{
    return index >= 0 && index < kBasePanelCount;
}

// True for a runtime-created sub-window slot (a capsule strip carved
// out of a panel), i.e. kBasePanelCount … kPanelSlotMax-1. A sub-window
// has no fixed slot identity: the registry assigns these, so code
// holding a sub-window index must not assume it is < 4.
[[nodiscard]] constexpr bool isSubWindowIndex(int index)
{
    return index >= kPanelSubWindowBase && index < kPanelSlotMax;
}

// True for anything a registry may hand out. Wider than both predicates
// above; use only when the caller is size-agnostic.
[[nodiscard]] constexpr bool isAnyPanelIndex(int index)
{
    return isPanelIndex(index) || isSubWindowIndex(index);
}

} // namespace mhw

// Forward-declared so the canvas header doesn't need the full Panel
// header (which transitively pulls in QMainWindow + layer-shell).
enum class Corner;
class Panel;

// Read-only snapshot of a Panel's geometry. The control console binds
// one of these per slot so the preview canvas can position the panel
// at the place it will actually sit on screen.
//
// This is intentionally a small interface — only what HudCanvas needs.
// If more state has to surface later, prefer adding accessors here
// rather than widening the bind surface.
//
// OWNERSHIP CONTRACT (v0.11.4 — previously undocumented, which is how
// the console leaked four of these): HudCanvas::bindPanel() stores this
// pointer WITHOUT taking ownership, and never deletes it. The BINDING
// side owns the adapter and must keep it alive for as long as the canvas
// can paint. The console does that with a member array of unique_ptrs
// (ControlPanel::boundSources_) exactly so the adapter — and the raw
// Panel* it caches — cannot outlive either. Do not pass a temporary or a
// stack object, and do not `new` one without storing it somewhere that
// has a deterministic destructor.
//
// ─── v0.12 SUB-WINDOW SUITABILITY (audited, not assumed) ─────────────
//
// The six accessors below are already the whole story for a sub-window.
// A strip carved out of a panel is still a Panel — it still resolves a
// Corner + QMargins, still owns a logical contentSize, and still
// composits scale/opacity/bgAlpha in its own paintEvent — so
// PanelSourceAdapter needs no new accessor and the geometry path a
// sub-window reports (corner/margins/contentSize/scale/opacity/bgAlpha)
// is exactly the path a base panel reports. What a sub-window does NOT
// need is a different coordinate model: layer-shell anchors are the
// same four corners for every window, which is why design §10.1's
// "Position{X,Y}" row is marked 不改.
//
// The one fact HudCanvas/console cannot recover from the six numbers is
// WHICH KIND of window it is looking at: a sub-window has no fixed slot
// identity, so a canvas that treats slot index as panel identity would
// key a strip to the wrong colour/accent and drag the wrong row.
// isSubWindow() answers that without a dynamic_cast and without any
// existing call site being forced to handle it (default = false).
class PanelSource {
public:
    virtual ~PanelSource() = default;
    virtual Corner corner() const = 0;
    virtual QMargins margins() const = 0;
    virtual QSize contentSize() const = 0;
    virtual double scale() const = 0;
    virtual double opacity() const { return 1.0; }
    virtual int bgAlpha() const { return 170; }
    // v0.12: true for a window carved out of a panel by the split
    // (a debuff / buff / mantle / wirebug strip). Base panels report
    // false — the default, so no existing adapter changes behaviour.
    virtual bool isSubWindow() const { return false; }
};

// Default adapter for a live mhw::Panel instance. Constructed with a
// raw pointer to the Panel; the adapter does not own the Panel, and
// (see the contract above) is not owned by the canvas it is bound to.
// It must be held by the binding side and destroyed only after the
// canvas can no longer read it.
class PanelSourceAdapter final : public PanelSource {
public:
    explicit PanelSourceAdapter(const Panel *p);
    Corner corner() const override;
    QMargins margins() const override;
    QSize contentSize() const override;
    double scale() const override;
    double opacity() const override;
    int bgAlpha() const override;
    bool isSubWindow() const override;
private:
    const Panel *p_;
};

namespace mhw {

// ─── v0.12: the sub-window registry ──────────────────────────────────
//
// WHAT THIS IS FOR
// ----------------
// v0.12 carves independently-toggleable blocks out of the four base
// panels and gives each its own layer-shell window — panel_player_split.h
// is the pilot (PlayerStatusStripPanel). Those windows have no fixed slot
// identity (the four base slots are pinned by static_assert above), so
// somebody has to hand out indices at runtime. This is that somebody.
//
// WHY AN ENTRY IS NOT A PanelCtl
// --------------------------------
// ControlPanel::PanelCtl is ten widget pointers plus a Panel*: the
// master chip, one SectionRow per section bit, the preview tile, the nav
// button, its summary, the count label, the count bar, three sliders and
// the screen combo. Filling one in means manufacturing an entire
// inspector for every sub-window — a UI decision this file has no basis
// to make (it cannot even see the type; control_panel.h includes THIS
// header, not the other way round) and one the console's rail has no
// room for. An entry below is therefore the minimum the pilot needs —
// slot, window, adapter, name, section labels, on/off — and nothing that
// could grow a slider or a preview tile.
//
// SLOT ALLOCATION — WHY IT CANNOT COLLIDE WITH A BASE SLOT
// --------------------------------------------------------
// Indices come from kPanelSubWindowBase … kPanelSlotMax-1 and are reused
// when an entry is removed, so a live set stays dense. The
// static_asserts at the top of this file pin kPanelSubWindowBase >=
// kBasePanelCount and kBasePanelCount == 4, so EVERY index handed out
// here is >= 4 and therefore fails isPanelIndex(). That is the whole
// safety argument: the two kPanelCount-sized arrays —
// std::array<PanelCtl, 4> in control_panel.h and
// std::array<HudCanvas::Slot, 4> in hud_canvas.h — cannot be indexed by
// an index this class returns, and every `for (i < mhw::kPanelCount)`
// loop in the codebase stays bounded at 4. A registry with zero entries
// (the state a stock v0.12 console starts in) executes no loop bodies
// and changes nothing at all.
//
// OWNERSHIP
// ---------
// The registry owns the PanelSourceAdapters (unique_ptrs) and does NOT
// own the Panels. This restates the PanelSourceAdapter contract above
// because this class is exactly where it would otherwise be re-broken:
// the registering side keeps the Panel alive for as long as the entry
// exists, and clear() / the destructor drops every adapter, so the raw
// Panel* an adapter caches is never dereferenced past the point where
// the panel could have been deleted. No adapter is ever handed to a
// temporary: what entries borrow from is a member of the registry that
// outlives them.
//
// No method here touches a Panel's members — it stores the pointer and
// hands it to PanelSourceAdapter. That is what keeps this header free of
// ui/panel.h (which would drag QMainWindow + layer-shell into every
// consumer, the reason the `class Panel;` forward declaration exists).
struct SubWindowEntry {
    int index{-1};
    Panel *panel{nullptr};
    // Borrowed pointer into this registry's adapter storage. Never null
    // while the entry is live. Not owning; see the ownership note above.
    const PanelSourceAdapter *adapter{nullptr};
    // Display name. Copied from the panel's own window title when the
    // caller passes none — a sub-window localizes that itself (see
    // PlayerStatusStripPanel::retranslateUi), so registering one adds no
    // console i18n key and the cached copy is refreshed, not translated,
    // on a language switch.
    QString title;
    // One label per section bit the window shows, in bit order. Already
    // localized by the caller; the registry only carries them.
    QStringList sectionLabels;
    // Console-side copy of the master switch. Mirrors
    // Panel::panelEnabled(); the live mount is still the panel's own
    // decision, this is the state an inspector row would read.
    bool enabled{true};
};

class SubWindowRegistry {
public:
    // Capacity is the sub-window ceiling from this header's constants —
    // deliberately NOT a second hard-coded 4.
    static constexpr int kCapacity = kMaxPanelSubWindows;

    // Registers `panel` and returns its slot index (>= kPanelSubWindowBase),
    // or -1 when it cannot be registered: a null panel, a panel already
    // registered under another index, or kCapacity live entries.
    // Non-owning in both directions (see the ownership note above).
    [[nodiscard("the slot index is the only handle for this sub-window")]]
    int add(Panel *panel, const QString &title,
            const QStringList &sectionLabels);

    // Drops the entry and the adapter it owns. An unknown index is a
    // silent no-op, so a teardown path can stay unconditional.
    void remove(int index);

    // Drops every entry and releases every adapter. Call it before the
    // sub-windows themselves are deleted.
    void clear();

    // Overwrites an entry's cached title. The panel is the authority on
    // its own (localized) name; this lets the console re-read it after a
    // language switch without the registry needing panel.h.
    void setTitle(int index, const QString &title);

    [[nodiscard]] int count() const { return entries_.size(); }
    [[nodiscard]] bool empty() const { return entries_.isEmpty(); }
    [[nodiscard]] bool full() const { return entries_.size() >= kCapacity; }
    [[nodiscard]] bool contains(int index) const;
    [[nodiscard]] const SubWindowEntry *entry(int index) const;
    [[nodiscard]] SubWindowEntry *entry(int index);
    // Slot index for an already-registered panel, or -1. Makes
    // double registration impossible rather than merely discouraged.
    [[nodiscard]] int indexOf(const Panel *panel) const;

    // Every live entry. NOT sorted by slot index — remove() swap-pops
    // the vector tail rather than shifting, so write ORDER is preserved
    // and slot order is not. Nothing may rely on position: an entry is
    // addressed by its `index` field or through entry(). A caller that
    // wants slot order sorts by index first.
    [[nodiscard]] const QVector<SubWindowEntry> &entries() const
    {
        return entries_;
    }

private:
    // Lowest unclaimed sub-window slot, or -1 when the ceiling is full.
    [[nodiscard]] int nextFreeIndex() const;

    QVector<SubWindowEntry> entries_;
    // NOT a QVector: Qt's containers detach by copying the element tail,
    // so QVector<unique_ptr> fails to compile on the first insertion
    // (unique_ptr is not copyable). std::vector moves instead. Also why
    // the registry is not a std::set or a sorted structure — the entries
    // are addressed by their `index` field, never by position, so a
    // swap-pop is enough and no ordering invariant has to be maintained.
    std::vector<std::unique_ptr<PanelSourceAdapter>> adapters_;
};

inline int SubWindowRegistry::add(Panel *panel, const QString &title,
                                  const QStringList &sectionLabels)
{
    if (!panel)                     return -1;   // nothing to bind
    if (indexOf(panel) >= 0)        return -1;   // already registered
    const int slot = nextFreeIndex();
    if (slot < 0)                   return -1;   // kCapacity reached

    // The adapter is created BEFORE the entry so the entry can never
    // exist with a null adapter: a registration that fails halfway
    // leaves no half-state to clean up elsewhere.
    adapters_.push_back(std::make_unique<PanelSourceAdapter>(panel));
    SubWindowEntry e;
    e.index         = slot;
    e.panel         = panel;
    e.adapter       = adapters_.back().get();
    e.title         = title;
    e.sectionLabels = sectionLabels;
    e.enabled       = true;   // the console drives this, see setTitle/enabled
    entries_.push_back(e);
    return slot;
}

inline void SubWindowRegistry::remove(int index)
{
    for (int i = 0; i < entries_.size(); ++i) {
        if (entries_[i].index != index)
            continue;
        // Release the adapter first: after this line nothing borrows it.
        // destroy-then-pop instead of QVector::remove: unique_ptr has
        // no copy constructor and every QList insertion/removal detaches
        // by copying the tail. Resetting in place (which runs the
        // deleter) and then popping the now-empty slot avoids any
        // container copying entirely. Slot ORDER carries no meaning
        // here — an entry is addressed by its `index` field, never by
        // position in this vector — which is why a plain vector and a
        // swap-pop is correct rather than a sorted structure.
        for (std::size_t a = 0; a < adapters_.size(); ++a) {
            if (adapters_[a].get() != entries_[i].adapter)
                continue;
            adapters_[a].reset();                       // run the deleter
            if (a != adapters_.size() - 1)
                adapters_[a] = std::move(adapters_.back());
            adapters_.pop_back();
            break;
        }
        entries_.remove(i);
        return;
    }
}

inline void SubWindowRegistry::clear()
{
    // Entries first, so no entry can point at an adapter that is alive
    // but unreachable from this registry.
    entries_.clear();
    adapters_.clear();
}

inline void SubWindowRegistry::setTitle(int index, const QString &title)
{
    if (SubWindowEntry *e = entry(index))
        e->title = title;
}

inline bool SubWindowRegistry::contains(int index) const
{
    return entry(index) != nullptr;
}

inline const SubWindowEntry *SubWindowRegistry::entry(int index) const
{
    for (const SubWindowEntry &e : entries_)
        if (e.index == index)
            return &e;
    return nullptr;
}

inline SubWindowEntry *SubWindowRegistry::entry(int index)
{
    for (SubWindowEntry &e : entries_)
        if (e.index == index)
            return &e;
    return nullptr;
}

inline int SubWindowRegistry::indexOf(const Panel *panel) const
{
    for (const SubWindowEntry &e : entries_)
        if (e.panel == panel)
            return e.index;
    return -1;
}

inline int SubWindowRegistry::nextFreeIndex() const
{
    // Linear scan over a ceiling of 32 with a live set that is bounded
    // by the number of sections the v0.12 split actually carves out
    // (single digits). A slot table would be the same code with more
    // state to keep consistent.
    for (int i = kPanelSubWindowBase; i < kPanelSlotMax; ++i)
        if (!contains(i))
            return i;
    return -1;
}

} // namespace mhw
