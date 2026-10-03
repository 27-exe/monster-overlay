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
// ONE number and one assert. The v0.11.4 file had a single
// `kPanelCount = 4` with a `static_assert(kPanelCount == 4)`, and the
// console's `std::array<PanelCtl, kPanelCount>` consumed it directly.
//
// v0.12 originally widened this into a five-constant boundary block
// (kBasePanelCount / kPanelIndexBaseMax / kPanelSubWindowBase /
// kMaxPanelSubWindows / kPanelSlotMax) to make room for a runtime
// sub-window registry. That registry never gained a caller — the
// main.cpp wiring deciding WHEN a sub-window exists was never written
// — so the whole block was reverted in the stage-1 dead-code pass and
// the file goes back to its single number. isSubWindowIndex() /
// isAnyPanelIndex() went with it; see the note further down.
//
//   kPanelCount     == 4   the four slots whose identity is FIXED and
//                          which the console and HudCanvas bind with
//                          std::array<…, kPanelCount>. Slot order is
//                          fixed: 0 Player, 1 Monster, 2 Damage,
//                          3 Pets. This is the number an assert may
//                          legitimately pin, and the ONLY number here.
//                          Do NOT raise it: it is the BASE four, not
//                          a total window count.
//
// The console, the HUD preview and the accent table (panel_accent.h,
// "indexed by the fixed slot order shared with mhw::kPanelCount /
// PanelSource") all key off this.

// v0.12: kBasePanelCount used to sit here as a "preferred" spelling of
// the same 4, with kPanelCount kept as a legacy alias of it. With
// nothing left that justified a second name, the alias was kept (it has
// ~20 call sites) and the preferred spelling was dropped.
inline constexpr int kPanelCount = 4;

static_assert(kPanelCount == 4,
              "the console/HUD contract requires four base panels");

// True for the four FIXED slots only (0 Player, 1 Monster, 2 Damage,
// 3 Pets) — the pre-v0.12 meaning, preserved verbatim. Everything that
// indexes a kPanelCount-sized array (control_panel.cpp's ctl_ /
// boundSources_, hud_canvas.cpp's slots_) must keep using THIS one,
// because those arrays have exactly four elements.
//
// v0.12: isSubWindowIndex() and isAnyPanelIndex() used to sit right
// below this predicate, both sizing a sub-window slot range out of the
// now-removed boundary constants. isSubWindowIndex() had zero callers;
// isAnyPanelIndex() was only reached through isSubWindowIndex(), so it
// went too. Neither had a caller even while the registry existed.
[[nodiscard]] constexpr bool isPanelIndex(int index)
{
    return index >= 0 && index < kPanelCount;
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

// ─── v0.12: the sub-window registry (REMOVED) ───────────────────────
//
// SubWindowEntry + SubWindowRegistry used to live here. Both were
// deleted in the v0.12 stage-1 dead-code pass: the registry was only
// reachable from ControlPanel::registerSubWindow() (deleted alongside
// it in control_panel.{h,cpp}) and nothing ever called that — the
// main.cpp wiring deciding WHEN a sub-window exists was never written.
// A registry with zero callers is a permanent no-op that still has to
// be owned, documented and type-checked.
//
// If a sub-window ever needs a runtime slot index again, re-derive it
// from kPanelCount as a boundary, NOT by widening it: every
// `std::array<…, mhw::kPanelCount>` in this codebase has exactly four
// elements, so any index a unit test would hand out has to live
// outside 0..kPanelCount-1 and must be rejected by isPanelIndex()
// before it reaches one of those arrays. That is the property to
// re-establish, and it is cheaper to re-establish than the five
// constants and 150 lines of registry it took to express the first
// time. The live v0.12 slot concept does NOT go through here — see
// PlayerSection::PlayerSplitSlot / kPlayerSplitTargets in
// src/ui/panel_sections.h.

} // namespace mhw
