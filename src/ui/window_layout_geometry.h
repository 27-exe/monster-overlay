// SPDX-License-Identifier: Apache-2.0
//
// v1.0.0 integration (stage D): CORNER ↔ LOGICAL TOP-LEFT, ONE FORMULA.
//
// WHY THIS FILE EXISTS. The console's position fields are the
// prototype's #meta-x / #meta-y: 「坐标是 <该屏> 内的逻辑坐标」 — the
// window's LOGICAL TOP-LEFT inside its own target output. They used to
// read `margins.left()` / `margins.top()` directly and write the same
// two numbers back, which is only the same thing for ONE of the four
// corners. Panel::margins() is (left, top, right, bottom) where the
// trailing pair is the ANCHOR's distance from the opposite edges, so a
// TopRight window's X lives in `right`, a Bottom window's Y lives in
// `bottom` — and the inspector showed a TopRight window at X = its left
// margin while typing a new X wrote the left margin and left the drawn
// box exactly where it was. 「右锚点窗输入 X 不动」 is that defect.
//
// WHAT IS IN HERE. Exactly two directions of one arithmetic, in the
// window's own output's logical frame:
//
//     margins  →  logical top-left        anchoredTopLeft()
//     logical top-left  →  margins        marginsForTopLeft()
//
// Both are pure, integer, header-only, and the scaled extent is spelled
// with THE SAME EXPRESSION the original anchoredRect() used —
// `max(1, int(content * scale))` — because hud_canvas.cpp's own
// anchoredRect() now delegates here and the two must be byte-for-byte
// equivalent, including the rounding. Nothing about the DEFAULT layout
// may change: a 1px drift in the old default is a baseline failure, not
// an improvement to slip in beside a bug fix.
//
// THE SCREEN RECT IS AN INPUT. The caller passes the (0,0)-anchored
// logical rectangle of the window's OWN output — `w.outputResolved`'s
// size, never "whichever screen the console happens to be previewing",
// because the field's unit is defined by the target output. The formula
// uses screen.left()/right()/top()/bottom() exactly as the original did
// (right() = left + width - 1), so the arithmetic round-trips exactly:
// X → m.right → X is integer-exact with no fudge factor anywhere.

#ifndef MHW_WINDOW_LAYOUT_GEOMETRY_H
#define MHW_WINDOW_LAYOUT_GEOMETRY_H

#include "panel.h"

#include <QMargins>
#include <QPoint>
#include <QRect>
#include <QSize>

#include <algorithm>

namespace mhw::layout {

// The drawn extent, in logical pixels. IDENTICAL to the original
// anchoredRect()'s first two lines — same clamp, same cast, same
// order — so delegating to it cannot move the default picture by a pixel.
[[nodiscard]] inline QSize scaledExtent(const QSize &content, qreal scale)
{
    return QSize(std::max(1, int(content.width() * scale)),
                 std::max(1, int(content.height() * scale)));
}

// margins → the window's logical top-left inside `screen`.
[[nodiscard]] inline QPoint anchoredTopLeft(Corner corner,
                                            const QMargins &m,
                                            const QRect &screen,
                                            const QSize &content,
                                            qreal scale)
{
    const QSize e = scaledExtent(content, scale);
    switch (corner) {
    case Corner::TopLeft:
        return QPoint(screen.left() + m.left(), screen.top() + m.top());
    case Corner::TopRight:
        return QPoint(screen.right() - e.width() - m.right(),
                      screen.top() + m.top());
    case Corner::BottomLeft:
        return QPoint(screen.left() + m.left(),
                      screen.bottom() - e.height() - m.bottom());
    case Corner::BottomRight:
        return QPoint(screen.right() - e.width() - m.right(),
                      screen.bottom() - e.height() - m.bottom());
    }
    return QPoint(screen.left() + m.left(), screen.top() + m.top());
}

// The logical top-left → margins. `keep` is the CURRENT margins: the two
// sides this corner does not own are copied through untouched, so an edit
// that means "move this window" preserves the other two anchors exactly
// the way the old code preserved right/bottom. The two sides this corner
// DOES own are overwritten from the requested position — and because the
// forward and inverse use the same `scaledExtent()` and the same
// screen.right()/bottom(), the round trip is integer-exact.
[[nodiscard]] inline QMargins marginsForTopLeft(Corner corner,
                                                const QPoint &topLeft,
                                                const QRect &screen,
                                                const QSize &content,
                                                qreal scale,
                                                const QMargins &keep)
{
    const QSize e = scaledExtent(content, scale);
    int left = keep.left();
    int top = keep.top();
    int right = keep.right();
    int bottom = keep.bottom();
    switch (corner) {
    case Corner::TopLeft:
        left = topLeft.x() - screen.left();
        top = topLeft.y() - screen.top();
        break;
    case Corner::TopRight:
        right = screen.right() - e.width() - topLeft.x();
        top = topLeft.y() - screen.top();
        break;
    case Corner::BottomLeft:
        left = topLeft.x() - screen.left();
        bottom = screen.bottom() - e.height() - topLeft.y();
        break;
    case Corner::BottomRight:
        right = screen.right() - e.width() - topLeft.x();
        bottom = screen.bottom() - e.height() - topLeft.y();
        break;
    }
    return QMargins(left, top, right, bottom);
}

// The whole rectangle, for the one caller that wants it (hud_canvas's
// anchoredRect) and for tests that assert the drawn box directly.
[[nodiscard]] inline QRect anchoredRect(const QRect &screen, Corner corner,
                                        const QMargins &m,
                                        const QSize &content, qreal scale)
{
    const QSize e = scaledExtent(content, scale);
    const QPoint tl = anchoredTopLeft(corner, m, screen, content, scale);
    return QRect(tl, e);
}

} // namespace mhw::layout

#endif // MHW_WINDOW_LAYOUT_GEOMETRY_H
