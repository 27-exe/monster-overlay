#include "section_row.h"
#include "ui_theme.h"

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRectF>
#include <QString>

namespace {

// ---------------------------------------------------------------------------
// The prototype's `.section-item` metrics, verbatim.
//
// reference.html:228-230 + the CDP capture in o4-verification:
//   min-height 29px, padding 0 7px, gap 7px, border 1px, radius 5px,
//   font-size 10px, checkbox 13×13. The measured row is 199.92×29 and the
//   checkbox starts 12px inside the row's left border edge, the label
//   35px inside it (html-controls.json: checkbox x=657.34 / row x=645.34,
//   text x=680.34).
//
//   12px = padding(7) + the UA's own margin-left(4px) + the checkbox's
//   sub-pixel origin (the row itself starts at a fractional x, so the
//   integer snapshot lands 1px further right than the exact 11.34).
//   Chrome's UA margin is NOT inside the 7px padding — it is added after
//   it, so the port must reserve it explicitly.
// ---------------------------------------------------------------------------
constexpr int kRowMinHeight = 29;      // min-height:29px
constexpr int kRowPadH     = 7;        // padding:0 7px
constexpr int kRowGap      = 7;        // gap:7px
constexpr int kRowRadius   = 5;        // border-radius:5px
constexpr int kBorderWidth = 1;        // border:1px solid
constexpr int kCheckBox    = 13;       // input 13×13 (measured rect)
constexpr int kFontPx      = 10;       // font-size:10px

// Chrome's UA stylesheet gives `input[type=checkbox]` margin-left:4px /
// margin-right:3px (html-controls.json: checkbox ml=4px, mr=3px). Those
// margins sit OUTSIDE the flex container's 7px padding, and the padding
// itself is inside the 1px border, so the box's left edge lands at
// border(1) + padding(7) + margin(4) = 12 from the row's border box —
// exactly the measured offset (checkbox x=657.34 - row x=645.34). The
// label then starts at 12 + 13 + margin-right(3) + gap(7) = 35, which is
// the measured text offset (680.34 - 645.34).
constexpr int kCheckBoxMarginLeft  = 4;  // input margin-left:4px
constexpr int kCheckBoxMarginRight = 3;  // input margin-right:3px

// Horizontal placement, in widget-local coordinates, of the checkbox and
// the label. Both are the MEASURED HTML offsets (12 / 35), derived from
// the tokens above rather than from any layout arithmetic, so the row
// lands where the prototype does even if flex maths shifts a pixel.
//
// NOTE ON THE PNG COORDINATES: the HTML row screenshots are clipped at
// x=645.34 (the row's own border box), so they carry a 5px prefix of page
// background before the row's first pixel. The checkbox ink therefore
// appears at PNG column 17 while its row-local x is 12 — the constants
// below are the row-local values.
constexpr int kCheckBoxX = kBorderWidth + kRowPadH + kCheckBoxMarginLeft;      // 12
constexpr int kLabelX    = kCheckBoxX + kCheckBox + kCheckBoxMarginRight
                           + kRowGap;                                          // 35

// Chrome's unchecked UA checkbox: a white (#ffffff) square with a neutral
// grey (#767676) 1px border and a ~2px corner radius — the SAME in both
// themes (html-dark/light-unchecked-checkbox.png are pixel-identical apart
// from the row fill behind them). The prototype sets no colour-scheme and
// no rule targets an unchecked input, so the UA default is the reference;
// --line is NOT used here.
constexpr int kBoxRadius    = 2;   // UA corner radius, measured
constexpr int kBoxBorder    = 1;   // UA border width, measured
constexpr QRgb kBoxFace     = 0xffffffff;   // #ffffff unchecked face
constexpr QRgb kBoxEdge     = 0xff767676;   // #767676 unchecked border

// The checked face is --clay with the UA's own antialiased tick: a check
// mark whose polyline runs LEFT TIP -> bottom CORNER -> right TIP (the
// corner is the lowest point; the short arm rises up-LEFT from it).
// Re-fitted against the frozen crops html-{dark,light}-{checked,focus}.png
// (bright-white pixel predicate |c-255|<=40 and min>170, inside the 13×13
// box at row-local x12..24 / y8..20):
//   before: 12 bright px, bbox (16,10)-(21,17) — too small, shifted right,
//           the corner sat at the TOP of the left arm (inverted check);
//   after:  light 19/19 px, pixel-exact; dark 20/21 (only the AA edge
//           pixel (14,15) of the left tip stays sub-threshold).
// The fit was searched with a standalone rasteriser that replicates this
// exact paint recipe (scratch/o4-finish-20261013/tickfit.cpp);
// 2.0 was the narrow width that matched both themes (1.7 lost 3-5 px).
constexpr qreal kTickWidth  = 2.0;
constexpr qreal kTickAx     = 3.5;   // box-local, left tip (short arm end)
constexpr qreal kTickAy     = 7.0;
constexpr qreal kTickBx     = 4.75;  // box-local, the bottom corner (vertex)
constexpr qreal kTickBy     = 9.5;
constexpr qreal kTickCx     = 10.0;  // box-local, right tip (long arm end)
constexpr qreal kTickCy     = 3.4;

// The prototype's label font is the HTML `--sans` stack
// (reference.html: `system-ui, -apple-system, "Segoe UI", Roboto,
// "Noto Sans SC", "Microsoft YaHei", sans-serif`, 10px/400). Passing the
// whole stack to QFont lets fontconfig resolve it exactly the way the
// browser does, so the CJK label picks the same face the HTML measured
// (CSS.getPlatformFontsForNode → "Microsoft YaHei", glyphCount 4) instead
// of whatever Qt's default substitution lands on.
const QStringList &sansStack()
{
    static const QStringList stack{
        QStringLiteral("system-ui"),
        QStringLiteral("-apple-system"),
        QStringLiteral("Segoe UI"),
        QStringLiteral("Roboto"),
        QStringLiteral("Noto Sans SC"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("sans-serif"),
    };
    return stack;
}

// color-mix(in srgb, A p%, B) — sRGB-linear-free component mix, which is
// exactly what CSS color-mix() does for these two opaque colours.
QColor mix(const QColor &a, const QColor &b, qreal pa)
{
    const qreal pb = 1.0 - pa;
    return QColor(qRound(a.red()   * pa + b.red()   * pb),
                  qRound(a.green() * pa + b.green() * pb),
                  qRound(a.blue()  * pa + b.blue()  * pb));
}

} // namespace

SectionRow::SectionRow(const QString &zh, const QString &key,
                       int icon, QWidget *parent)
    : QWidget(parent), zhText_(zh), keyText_(key), icon_(icon)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, false);
    // The prototype's row IS the checkbox's label element, so it is
    // tabbable: Tab lands on it and Space toggles it (handled in
    // keyPressEvent). StrongFocus (not TabFocus) so it can also take
    // focus programmatically, which is what the console does.
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(kRowMinHeight);

    // display:flex; align-items:center; gap:7px; padding:0 7px.
    //
    // The layout is NOT what positions the checkbox or the label — both
    // are drawn at the measured kCheckBoxX / kLabelX in paintEvent /
    // refreshTheme(). The layout exists only to host the collapsed ASCII
    // key label the L2 smoke test looks up. The visible label is kept OUT
    // of the layout on purpose: a layout-managed child gets its geometry
    // recomputed on every activation (and the probe reads label->x()
    // directly), so putting it in the flex row would let the layout's own
    // arithmetic override the measured 35px offset.
    auto *hl = new QHBoxLayout(this);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);

    zh_ = new QLabel(zhText_, this);
    zh_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    zh_->setTextFormat(Qt::PlainText);
    // Placed by refreshTheme(); see resizeEvent for the width re-fit.

    // The prototype's row shows only the checkbox and the label — no
    // per-section icon and no right-hand ASCII key. The key label is
    // therefore kept as a child (it is the stable ASCII identifier the
    // console's L2 smoke test locates rows by, via
    // findChildren<QLabel>() + text match) but is collapsed to zero
    // width and paints nothing, so it contributes no pixels.
    key_ = new QLabel(keyText_.toUpper(), this);
    key_->setFixedWidth(0);
    key_->setFixedHeight(0);
    key_->setStyleSheet(QStringLiteral(
        "color:transparent;background:transparent;border:none;"));
    hl->addWidget(key_, 0);
    setLayout(hl);

    refreshTheme();
}

SectionRow::Palette SectionRow::paletteFor(bool dark)
{
    if (dark) {
        // :root — dark
        return {
            /* surface2 */ QColor(0x2d, 0x2d, 0x28),   // #2d2d28
            /* line     */ QColor(0x3d, 0x3d, 0x36),   // #3d3d36
            /* fg       */ QColor(0xec, 0xe9, 0xdf),   // #ece9df
            /* fg2      */ QColor(0xb8, 0xb5, 0xaa),   // #b8b5aa
            /* clay     */ QColor(0xd9, 0x77, 0x57),   // #d97757
            /* clayLight*/ QColor(0xe3, 0x9a, 0x7e),   // #e39a7e
        };
    }
    // :root[data-theme="light"]
    return {
        /* surface2 */ QColor(0xf0, 0xee, 0xe8),       // #f0eee8
        /* line     */ QColor(0xd8, 0xd4, 0xc9),       // #d8d4c9
        /* fg       */ QColor(0x24, 0x24, 0x1f),       // #24241f
        /* fg2      */ QColor(0x5b, 0x59, 0x52),       // #5b5952
        /* clay     */ QColor(0x9c, 0x46, 0x23),       // #9c4623
        /* clayLight*/ QColor(0xb8, 0x5c, 0x38),       // #b85c38
    };
}

void SectionRow::setAccent(const QColor &c)
{
    // The prototype's .section-item has no per-panel accent: the only
    // accent it uses is --clay on the checkbox, and that is theme-wide.
    // The parameter is accepted and stored so the caller is unchanged.
    accent_ = c;
    update();
}

void SectionRow::setDisplayText(const QString &text)
{
    zhText_ = text;
    if (zh_)
        zh_->setText(zhText_);
}

void SectionRow::refreshTheme()
{
    if (!zh_)
        return;
    // color:var(--fg) when checked, var(--fg-2) when not. font-size:10px.
    // The prototype's row inherits the --sans stack (font-weight 400),
    // so the label asks for the same families the CSS names; a colour-only
    // stylesheet keeps QSS from substituting its own default face for the
    // CJK label.
    const Palette pal = paletteFor(isDarkTheme());
    QFont f;
    f.setFamilies(sansStack());
    f.setPixelSize(kFontPx);
    f.setWeight(QFont::Normal);
    zh_->setFont(f);
    zh_->setStyleSheet(QStringLiteral("color:%1;background:transparent;"
                                      "border:none;")
                           .arg((on_ ? pal.fg : pal.fg2).name()));
    placeLabel();
    update();
}

void SectionRow::placeLabel()
{
    // Place the label at the MEASURED HTML offset (35px inside the row's
    // left border edge) and give it the rest of the row's width so long
    // localized strings are not clipped. The label is deliberately NOT
    // layout-managed — see the constructor.
    if (!zh_)
        return;
    const int w = qMax(0, width() - kLabelX);
    zh_->setGeometry(kLabelX, 0, w, height());
}

void SectionRow::setChecked(bool c)
{
    if (on_ == c)
        return;
    on_ = c;
    // The prototype has no transition on .section-item (only .toast and
    // .board-window animate), so the colour change is immediate — a
    // screenshot taken mid-flight would otherwise catch an interpolated
    // frame that the HTML can never produce.
    refreshTheme();
}

void SectionRow::paintEvent(QPaintEvent *)
{
    const Palette pal = paletteFor(isDarkTheme());

    // ---- the pill --------------------------------------------------------
    // box-sizing:border-box — the 1px border is drawn INSIDE the widget
    // rect, so the fill covers the inner area and the border strokes the
    // outline. Measured in the prototype: the checked row's border
    // resolves to (147,93,72) dark / (183,134,110) light, i.e.
    // color-mix(--clay 55%, --line); the fill is
    // color-mix(--clay 10%, --surface-2).
    const QColor fill  = on_ ? mix(pal.clay, pal.surface2, 0.10) : pal.surface2;
    const QColor edge  = on_ ? mix(pal.clay, pal.line,     0.55) : pal.line;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // rect() is a QRect, and QRect::adjusted() rounds every delta to an
    // int — so the old `rect().adjusted(h,h,-h,-h)` with h = 0.5 became
    // adjusted(0,0,0,0): the stroke straddled the pixel boundary instead
    // of sitting on it, which is why the top border read #69493a instead
    // of the exact #935d48 mix. Converting to QRectF first keeps the
    // half-pixel inset real.
    //
    // The widget rect is also built from width()/height(), NOT from
    // QRect::rect(): a QRect's bottom-right corner is INCLUSIVE, so
    // QRectF(rect()) is (199,28) for a 200×29 widget and the pill would
    // come out a pixel short on the right and bottom edges.
    const qreal h = kBorderWidth / 2.0;
    const QRectF outerF = QRectF(0.0, 0.0, width(), height())
                              .adjusted(h, h, -h, -h);

    QPainterPath pill;
    pill.addRoundedRect(outerF, kRowRadius, kRowRadius);
    p.fillPath(pill, fill);
    p.setPen(QPen(edge, kBorderWidth));
    p.drawPath(pill);

    // ---- the checkbox ----------------------------------------------------
    // `box` is the 13×13 DOM rect placed at the measured offset. Its ink
    // spans local x 12..24 and y 8..20 — verified against the HTML crop,
    // where the box's ink occupies PNG columns 17..29 in a clip that starts
    // 5px before the row's border box (so PNG 17 == local 12).
    const QRectF box(kCheckBoxX, (height() - kCheckBox) / 2.0,
                     kCheckBox, kCheckBox);

    if (on_) {
        // Chrome's checked rendering: a solid clay square with a white
        // tick. Measured from the audit PNGs: fill exactly #d97757 dark /
        // #9c4623 light, tick #ffffff. The face fills the whole rect (the
        // UA's checked box has no separate border colour).
        p.setPen(Qt::NoPen);
        p.setBrush(pal.clay);
        p.drawRoundedRect(box, kBoxRadius, kBoxRadius);

        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(Qt::white, kTickWidth, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
        p.drawPolyline(QPolygonF()
                       << QPointF(box.left() + kTickAx, box.top() + kTickAy)
                       << QPointF(box.left() + kTickBx, box.top() + kTickBy)
                       << QPointF(box.left() + kTickCx, box.top() + kTickCy));
    } else {
        // No rule targets an unchecked input, so the prototype shows the
        // UA default: a WHITE square with a neutral grey (#767676) 1px
        // border, identical in both themes (the two unchecked crops are
        // pixel-identical apart from the row fill behind them).
        //
        // The border is FILLED (outer rounded rect minus inner rounded
        // rect) rather than stroked: a 1px QPen centred on a path is
        // rasterised with its own half-pixel blend, which put the ink on
        // local 13..23 instead of the measured 12..24. Filling the band
        // between two exact rects lands every pixel.
        QPainterPath outer, inner;
        outer.addRoundedRect(box, kBoxRadius, kBoxRadius);
        inner.addRoundedRect(box.adjusted(kBoxBorder, kBoxBorder,
                                          -kBoxBorder, -kBoxBorder),
                             qMax(0, kBoxRadius - kBoxBorder),
                             qMax(0, kBoxRadius - kBoxBorder));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(kBoxEdge));
        p.drawPath(outer.subtracted(inner));
        p.setBrush(QColor(kBoxFace));
        p.drawPath(inner);
    }

    // ---- :focus-visible ring ---------------------------------------------
    // input:focus-visible { outline:2px solid var(--clay-light);
    //                       outline-offset:2px; }  (reference.html:86-87)
    //
    // The ring belongs to the CHECKBOX, not to the row. Measured from
    // html-dark/light-focus.png: the ring's ink spans local x 8..28 and
    // y 4..24 — it starts 4px left/above the checkbox's ink box (12,8) and
    // ends 4px right/below its ink box end (24,20), with a 2px stroke and
    // a 2px gap, i.e. ink = box + 2(gap) + 2(stroke) = box + 4. Chrome
    // draws it square-cornered (the corner pixels are fully covered, not
    // blended), so no rounding is applied.
    //
    // drawRect's pen is CENTRED on the rect path, so a 2px pen paints one
    // pixel either side of it: the path must sit box+3 out for the ink to
    // land box+4 out. (The earlier adjusted(-4..4) put the path at box+4
    // and the ink one pixel further still — bbox (7,3)-(29,25), gap 3px.)
    if (keyboardFocusVisible_) {
        const QRectF ring = box.adjusted(-3.0, -3.0, 3.0, 3.0);
        p.setPen(QPen(pal.clayLight, 2.0, Qt::SolidLine, Qt::FlatCap,
                      Qt::MiterJoin));
        p.setBrush(Qt::NoBrush);
        p.drawRect(ring);
    }
}

void SectionRow::mousePressEvent(QMouseEvent *) {}

void SectionRow::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
        // A mouse click gives the row focus, but the browser would NOT
        // report :focus-visible for it — no ring, and Space only reaches
        // the row once a key has actually been used.
        keyboardFocusVisible_ = false;
        setChecked(!on_);
        emit stateChanged(on_ ? Qt::Checked : Qt::Unchecked);
    }
}

void SectionRow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        // The first Space keydown is what the browser treats as a
        // keyboard interaction, so this is where :focus-visible turns on.
        keyboardFocusVisible_ = true;
        setChecked(!on_);
        emit stateChanged(on_ ? Qt::Checked : Qt::Unchecked);
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

void SectionRow::keyReleaseEvent(QKeyEvent *e)
{
    // The HTML checkbox toggles on keydown; the keyup carries no state
    // change, so releasing Space must be silent. Auto-repeat keydowns are
    // filtered in keyPressEvent, so holding Space still emits once.
    if (e->key() == Qt::Key_Space) {
        e->accept();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void SectionRow::focusInEvent(QFocusEvent *e)
{
    // Chrome paints :focus-visible only when focus arrived from the
    // keyboard. Qt's reason map is not 1:1 with the browser's heuristic,
    // but Tab / Backtab / Shortcut / MenuBar / Popup are all keyboard
    // paths, while Mouse and ActiveWindow (the focus a click gives) are
    // not. OtherFocusReason is what a programmatic setFocus() reports, so
    // it does NOT enable the ring either — matching the capture, where the
    // row was focused programmatically and no ring was drawn.
    switch (e->reason()) {
    case Qt::TabFocusReason:
    case Qt::BacktabFocusReason:
    case Qt::ShortcutFocusReason:
    case Qt::MenuBarFocusReason:
    case Qt::PopupFocusReason:
        keyboardFocusVisible_ = true;
        update();
        break;
    default:
        break;
    }
    QWidget::focusInEvent(e);
}

void SectionRow::focusOutEvent(QFocusEvent *e)
{
    // Losing focus drops :focus-visible whatever the reason was.
    keyboardFocusVisible_ = false;
    QWidget::focusOutEvent(e);
}

void SectionRow::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    // The label's width is tied to the row's width (it is not in a
    // layout), so a stretch has to re-fit it or the text stays clipped at
    // the previous width.
    placeLabel();
}
