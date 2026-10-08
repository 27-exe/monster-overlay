#include "panel_source.h"

#include "panel.h"

PanelSourceAdapter::PanelSourceAdapter(const Panel *p) : p_(p) {}

Corner PanelSourceAdapter::corner() const     { return p_->corner(); }
QMargins PanelSourceAdapter::margins() const  { return p_->margins(); }
QSize PanelSourceAdapter::contentSize() const { return p_->contentSize(); }
double PanelSourceAdapter::scale() const      { return p_->scale(); }
double PanelSourceAdapter::opacity() const    { return p_->opacity(); }

int PanelSourceAdapter::bgAlpha() const { return p_->bgAlpha(); }

// v0.12: asks the window, not a slot number. A base panel answers
// false; only a window that called setSubWindow(true) from its own
// constructor answers true. No 4-vs-many knowledge lives here, which
// is what keeps this file free of any registry/index concern.
bool PanelSourceAdapter::isSubWindow() const { return p_->isSubWindow(); }

// v0.15: the two screen answers, forwarded from the Panel verbatim.
//
// WHY THE ADAPTER FORWARDS THEM RATHER THAN THE CANVAS ASKING THE PANEL.
// HudCanvas holds a PanelSource*, never a Panel* — that is the whole
// point of the interface (see the ownership contract in the header). So
// "which screen is this window measured against" has to be answerable
// through the interface, or the canvas cannot ask it at all, and the
// drag clamp stays on the canvas's screen while Panel::marginLimits()
// stays on the primary. That is the 1480-px gap in the header comment.
//
// A null p_ cannot happen: the ctor takes a raw pointer and the binding
// side's contract (ControlPanel::boundSources_, and the split host's
// entries_) keeps the Panel alive for as long as the canvas can read the
// adapter. The other five accessors already dereference p_ without a
// guard; adding one here would be a second, weaker convention.
QString PanelSourceAdapter::outputName() const { return p_->outputName(); }

QString PanelSourceAdapter::previewFallbackOutput() const
{
    return p_->previewFallbackOutput();
}
