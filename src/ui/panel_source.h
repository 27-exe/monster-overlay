#pragma once

#include <QMargins>
#include <QSize>
#include <QtGlobal>

namespace mhw {

// Shared slot-count contract for the control console and HUD preview.
// Slot order is fixed: 0 Player, 1 Monster, 2 Damage, 3 Pets.
inline constexpr int kPanelCount = 4;

[[nodiscard]] constexpr bool isPanelIndex(int index)
{
    return index >= 0 && index < kPanelCount;
}

static_assert(kPanelCount == 4, "the console/HUD contract requires four panels");

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
class PanelSource {
public:
    virtual ~PanelSource() = default;
    virtual Corner corner() const = 0;
    virtual QMargins margins() const = 0;
    virtual QSize contentSize() const = 0;
    virtual double scale() const = 0;
    virtual double opacity() const { return 1.0; }
    virtual int bgAlpha() const { return 170; }
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
private:
    const Panel *p_;
};
