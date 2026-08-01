#include "panel_source.h"

#include "panel.h"

PanelSourceAdapter::PanelSourceAdapter(const Panel *p) : p_(p) {}

Corner PanelSourceAdapter::corner() const     { return p_->corner(); }
QMargins PanelSourceAdapter::margins() const  { return p_->margins(); }
QSize PanelSourceAdapter::contentSize() const { return p_->contentSize(); }
double PanelSourceAdapter::scale() const      { return p_->scale(); }
