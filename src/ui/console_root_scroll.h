// SPDX-License-Identifier: Apache-2.0
//
// v1.0.0 fidelity port (root scroll): THE PAGE SCROLLS, THE BOARD DOES
// NOT SHRINK.
//
// reference.html lets the document grow past the viewport: measured at
// 1600x1000 DPR1 the root is 1134.9375 tall for the landscape output and
// 1742.3125 for the portrait one, the page's own vertical scrollbar takes
// 15px of width (innerWidth 1600 vs clientWidth 1585), and the board
// never gives up a pixel of 1050x590.625 / 560x995.546875. The frozen
// page's arithmetic is HTML-ROOT-REMEASUREMENT.md; the Qt shell used to
// do the opposite — the window is fixed, the outer QSplitter squeezed
// the stage to 602px and the Inspector to 302px and called the squeezed
// result the layout.
//
// This class is the shell's root scroll container. It is a QScrollArea
// for one reason only: QScrollArea is the one Qt widget that owns a real
// scrollbar, and a real scrollbar is what produces the 15px — the QSS at
// control_panel_styles.cpp already spells `QScrollBar:vertical{width:
// 15px}` and reserves `#consoleRootScroll` / `#consoleRootContent` for
// exactly this wiring, so the width the console loses is the same width
// the page loses, decided by the same kind of object and never by
// subtracting 15 by hand.
//
// THE SIZING CONTRACT, IN THREE PARTS:
//
//  1. WIDTH follows the viewport. `setWidgetResizable(true)` is the only
//     mode that keeps the content's width on the viewport: when the
//     vertical scrollbar appears, the viewport (and with it the content)
//     goes 1600 -> 1585, which is the CSS `clientWidth` behaviour. The
//     `widgetResizable(false)` alternative was measured to leave the
//     content at its sizeHint WIDTH instead (a 100px-wide content stays
//     100px wide in a 1600px viewport), so it cannot be the root.
//
//  2. HEIGHT is NOT the viewport. With widgetResizable(true) Qt clamps
//     the content down to the viewport, which is the squeezed layout
//     this whole port exists to remove — unless the content states a
//     minimum height. So the content's minimum height is kept equal to
//     its own layout's natural height, re-read every time the layout
//     changes (resize, LayoutRequest). That number comes from the
//     CONTENT — `layout()->heightForWidth()` / `sizeHint()` — so it is
//     the shell's own budget, not a hardcoded DP and not a number copied
//     out of the prototype's measurements. When the content's natural
//     height exceeds the viewport, the widget grows and the scrollbar
//     appears; when it does not, nothing scrolls and the geometry is
//     exactly what it was before this class existed.
//
//  3. NOTHING IS RE-SAVED. The scroll area never touches the window's
//     geometry, never calls restoreGeometry/saveGeometry, and never
//     writes ConsoleLayoutStore. A stored splitter proportion is applied
//     by the shell before the first layout and is only ever scaled by
//     the splitter itself, exactly as it is without a scroll container.

#ifndef MHW_CONSOLE_ROOT_SCROLL_H
#define MHW_CONSOLE_ROOT_SCROLL_H

#include <QScrollArea>

class QWidget;

namespace mhw {

class RootScrollArea : public QScrollArea
{
    Q_OBJECT
public:
    explicit RootScrollArea(QWidget *parent = nullptr);

    // Takes ownership the QScrollArea way (setWidget reparents to the
    // viewport), names the content layer, and installs this object as the
    // content's event filter so a LayoutRequest re-reads the budget.
    void setRootWidget(QWidget *content);

    // The height the content is currently asking for. Exposed for the
    // probe and for tests; it is the same number syncNaturalHeight()
    // applies, and it is a READ of the layout, not a stored constant.
    [[nodiscard]] int naturalHeight() const;

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void syncNaturalHeight();
    bool syncing_ = false;
};

} // namespace mhw

#endif // MHW_CONSOLE_ROOT_SCROLL_H
