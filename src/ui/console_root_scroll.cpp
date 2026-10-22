// SPDX-License-Identifier: Apache-2.0
#include "ui/console_root_scroll.h"

#include <QEvent>
#include <QFrame>
#include <QLayout>
#include <QResizeEvent>
#include <QWidget>

namespace mhw {

RootScrollArea::RootScrollArea(QWidget *parent)
    : QScrollArea(parent)
{
    // The names are the ones control_panel_styles.cpp already reserves:
    // `QWidget#consoleRootScroll,QWidget#consoleRootContent{background:
    // @page}` and `QScrollArea#consoleRootScroll{border:none;...}`. The
    // border:none matters arithmetically: with a frame the viewport
    // would be 1600 - 15 - 2 = 1584, and the page it is standing in for
    // is 1585.
    setObjectName(QStringLiteral("consoleRootScroll"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    // AsNeeded on both axes: a horizontal scrollbar must not appear just
    // because the content is wider than the viewport at some transient
    // width — the page has an overflow rule, the shell has a scrollbar
    // only when the content really does not fit.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::NoFocus);
}

void RootScrollArea::setRootWidget(QWidget *content)
{
    if (!content) return;
    // The content layer, not the whole central widget: the QMainWindow
    // shell (menu bar, window chrome, the toast's central-widget
    // parenting) has to stay inside the client area and keep painting
    // while the layer below it scrolls.
    content->setObjectName(QStringLiteral("consoleRootContent"));
    content->installEventFilter(this);
    setWidget(content);
    syncNaturalHeight();
}

void RootScrollArea::setPageScrollEnabled(bool on)
{
    if (pageScroll_ == on)
        return;
    pageScroll_ = on;
    // Re-apply the budget right away: setRootWidget() may already have
    // run (in which case the content is carrying the PREFERRED height as
    // an explicit minimum right now), and a call made before it just
    // leaves the flag for setRootWidget()'s own sync to read.
    syncNaturalHeight();
}

int RootScrollArea::naturalHeight() const
{
    const QWidget *content = widget();
    if (!content || !content->layout()) return 0;
    const int vw = viewport()->width();
    int h = -1;
    if (content->hasHeightForWidth()) h = content->heightForWidth(vw);
    if (h < 0) h = content->sizeHint().height();
    return qMax(h, content->minimumSizeHint().height());
}

void RootScrollArea::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    syncNaturalHeight();
}

bool RootScrollArea::eventFilter(QObject *watched, QEvent *event)
{
    // QScrollArea's own filter has to run first: it is what refreshes the
    // scroll bars on the content's LayoutRequest/Resize.
    const bool consumed = QScrollArea::eventFilter(watched, event);
    if (!syncing_ && watched == widget()
        && (event->type() == QEvent::LayoutRequest
            || event->type() == QEvent::Resize)) {
        syncNaturalHeight();
    }
    return consumed;
}

void RootScrollArea::syncNaturalHeight()
{
    QWidget *content = widget();
    if (!content || !content->layout()) return;
    // THE BUDGET, in one expression: the preferred height while the page
    // is allowed to scroll (the fidelity mode), the layout's own floor
    // when it is not (see setPageScrollEnabled()). `minimumSizeHint()`
    // is QWidget's "layout->totalMinimumSize()" — the height the panes
    // below cannot go under — so the floor still pushes the content past
    // the viewport exactly when the window is smaller than the console
    // itself and there is nothing left to give.
    const int h = pageScroll_ ? naturalHeight()
                              : content->minimumSizeHint().height();
    if (h <= 0 || content->minimumHeight() == h) return;
    // Re-entrancy guard: setMinimumHeight() queues a layout, which comes
    // back here as a LayoutRequest. The flag is dropped before the call
    // so a genuine later change is never swallowed by a stale one.
    syncing_ = true;
    content->setMinimumHeight(h);
    syncing_ = false;
}

} // namespace mhw
