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
