#pragma once

#include <QMargins>
#include <QSize>
#include <QtGlobal>

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
class PanelSource {
public:
    virtual ~PanelSource() = default;
    virtual Corner corner() const = 0;
    virtual QMargins margins() const = 0;
    virtual QSize contentSize() const = 0;
    virtual double scale() const = 0;
    virtual double opacity() const { return 1.0; }
    virtual int bgAlpha() const { return 170; }
    virtual bool blurEnabled() const { return true; }
};

// Default adapter for a live mhw::Panel instance. Constructed with a
// raw pointer to the Panel; the adapter does not own the Panel.
class PanelSourceAdapter final : public PanelSource {
public:
    explicit PanelSourceAdapter(const Panel *p);
    Corner corner() const override;
    QMargins margins() const override;
    QSize contentSize() const override;
    double scale() const override;
    double opacity() const override;
    int bgAlpha() const override;
    bool blurEnabled() const override;
private:
    const Panel *p_;
};
