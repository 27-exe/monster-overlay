#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/control_panel_styles.h"
#include "ui/ui_theme.h"

#include <cstdio>

// 验收项 3：双主题滑动条背景透明，只有细轨道与正确填充/把手。
//
// NOT a source-text test. The stylesheet is the real
// consoleStyleSheet(), the widgets are real QS sliders inside a real
// QWidget tree, and every assertion reads BACK PIXELS from a rendered
// frame of both themes:
//
//   * the ground under each slider must be the HOST's magenta, i.e.
//     QSlider paints nothing — before the fix
//     `QWidget{background:@page}` (control_panel_styles.cpp:230)
//     matched every QSlider and filled its whole rect with one grey
//     slab, the "残留灰色/浅色大矩形" the user photographed.
//   * the fill band must stay ~4px tall (the groove's height), never a
//     full-height stripe: sub-page/add-page are height-capped now.
//   * the fill colours must be the THEME's own tokens — @clay on the
//     filled side and @line on the empty side, dark and light
//     spellings asserted separately, because "correct in dark" is the
//     half-measurement that hid this bug.
//   * the handle must be the theme's clay, 14px wide, and a DISABLED
//     slider must keep the same transparent ground (only its handle
//     turns @fg3).
//
// The magenta host ground is the instrument: a transparent slider
// shows it, an opaque @page slab cannot — see the same technique in
// tests/hud_canvas_split_tests.cpp (countMagenta).
//
// MUTATION: delete the `QSlider{background:transparent}` rule from
// control_panel_styles.cpp and check 1 turns red in BOTH themes.

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL  %s\n", what); }
    else std::printf("PASS: %s\n", what);
}
void check(bool ok, const char *what, const QString &detail)
{
    ++checks;
    if (!ok) {
        std::printf("  FAIL  %s [%s]\n", what, qPrintable(detail));
        ++failures;
    } else std::printf("PASS: %s [%s]\n", what, qPrintable(detail));
}
void pump()
{
    for (int i = 0; i < 40; ++i) QCoreApplication::processEvents();
}

const QColor kHost(255, 0, 255);

struct ThemeTokens {
    const char *label;
    QColor page;    // what the old widget background painted
    QColor clay;    // sub-page fill + handle
    QColor line;    // add-page (unfilled) track
    QColor fg3;     // disabled handle
};

// The exact hexes control_panel_styles.cpp's Pal spells out for each
// theme (dark = reference.html:19-49, light = :50-74). Duplicated on
// purpose: a test that asked the stylesheet for its own answer would
// agree with a stylesheet that had drifted.
const ThemeTokens kDark  { "dark",  QColor("#181816"), QColor("#d97757"),
                           QColor("#3d3d36"), QColor("#87867f") };
const ThemeTokens kLight { "light", QColor("#f4f2ed"), QColor("#9c4623"),
                           QColor("#d8d4c9"), QColor("#6d6b63") };

// Count pixels equal to `c` inside `r` of an image scaled by `dpr`.
int countColor(const QImage &img, const QRectF &r, const QColor &c,
               int dpr)
{
    int n = 0;
    const int x0 = qRound(r.left() * dpr), x1 = qRound(r.right() * dpr);
    const int y0 = qRound(r.top() * dpr), y1 = qRound(r.bottom() * dpr);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (img.pixelColor(x, y) == c) ++n;
    return n;
}

// Run of pixels equal to `c` along one row, longest in `r`.
int longestRun(const QImage &img, int y, const QRectF &r,
               const QColor &c, int dpr)
{
    int best = 0, cur = 0;
    const int x0 = qRound(r.left() * dpr), x1 = qRound(r.right() * dpr);
    y *= dpr;
    for (int x = x0; x <= x1; ++x) {
        if (img.pixelColor(x, y) == c) ++cur;
        else { if (cur > best) best = cur; cur = 0; }
    }
    if (cur > best) best = cur;
    return best;
}

QColor at(const QImage &img, const QPointF &p, int dpr)
{
    return img.pixelColor(qRound(p.x() * dpr), qRound(p.y() * dpr));
}

// Render one real host with a REAL sheet and a REAL pair of sliders,
// then read the pixels back.
void runTheme(const ThemeTokens &tk)
{
    std::printf("\n=== QSlider ground / track, theme: %s ===\n", tk.label);
    setUiTheme(QString::fromLatin1(tk.label) == QStringLiteral("dark"));

    QWidget host;
    host.setObjectName(QStringLiteral("sliderHost"));
    // The console's own sheet, plus a magenta ground on the host so a
    // transparent slider is visible AS transparent. The 1-id host rule
    // outranks the sheet's 0-id `QWidget{background:@page}`.
    host.setStyleSheet(consoleStyleSheet()
                       + QStringLiteral("\nQWidget#sliderHost"
                                        "{background:#ff00ff;}"));
    auto *lay = new QVBoxLayout(&host);
    lay->setContentsMargins(24, 24, 24, 24);
    lay->setSpacing(20);

    auto *on = new QSlider(Qt::Horizontal);
    on->setRange(0, 100);
    on->setValue(40);
    on->setFixedHeight(40);
    lay->addWidget(on);

    auto *off = new QSlider(Qt::Horizontal);
    off->setRange(0, 100);
    off->setValue(40);
    off->setFixedHeight(40);
    off->setEnabled(false);
    lay->addWidget(off);

    host.resize(340, 200);
    host.show();
    pump();

    const QPixmap pm = host.grab();
    const QImage img = pm.toImage();
    const int dpr = qMax(1, qRound(img.width() / qreal(host.width())));

    QRectF rOn(on->mapTo(&host, QPoint(0, 0)), on->size());
    QRectF rOff(off->mapTo(&host, QPoint(0, 0)), off->size());

    // ── 1. THE REGRESSION: no @page slab anywhere in either slider ────
    const int slabOn = countColor(img, rOn, tk.page, dpr);
    const int slabOff = countColor(img, rOff, tk.page, dpr);
    check(slabOn == 0,
          (QStringLiteral("enabled slider paints no @page rectangle")
           + QStringLiteral(" [") + tk.label
           + QStringLiteral("] count=") + QString::number(slabOn))
              .toUtf8().constData());
    check(slabOff == 0,
          (QStringLiteral("disabled slider paints no @page rectangle")
           + QStringLiteral(" [") + tk.label
           + QStringLiteral("] count=") + QString::number(slabOff))
              .toUtf8().constData());

    // ── 2. the ground under both sliders is the host's magenta ────────
    const QPointF tlOn = rOn.topLeft() + QPointF(2, 2);
    const QPointF brOn = QPointF(rOn.right() - 2, rOn.bottom() - 2);
    check(at(img, tlOn, dpr) == kHost && at(img, brOn, dpr) == kHost,
          (QStringLiteral("enabled slider corners are transparent")
           + QStringLiteral(" [") + tk.label + QStringLiteral("]"))
              .toUtf8().constData(),
          at(img, tlOn, dpr).name(QColor::HexArgb)
              + QStringLiteral(" / ")
              + at(img, brOn, dpr).name(QColor::HexArgb));
    const QPointF tlOff = rOff.topLeft() + QPointF(2, 2);
    const QPointF brOff = QPointF(rOff.right() - 2, rOff.bottom() - 2);
    check(at(img, tlOff, dpr) == kHost && at(img, brOff, dpr) == kHost,
          (QStringLiteral("disabled slider corners are transparent")
           + QStringLiteral(" [") + tk.label + QStringLiteral("]"))
              .toUtf8().constData(),
          at(img, tlOff, dpr).name(QColor::HexArgb)
              + QStringLiteral(" / ")
              + at(img, brOff, dpr).name(QColor::HexArgb));

    const int midY = qRound(rOn.center().y());
    const int rightColX = qRound(rOn.right()) - 8;
    const int leftColX = qRound(rOn.left()) + 8;

    // ── 3. the track is a ~4px band, not a full-height stripe ─────────
    // The right column is past the handle, so it samples the ADD-PAGE
    // (unfilled) colour and nothing else paints there.
    QRectF col(QPointF(rightColX, rOn.top()), QPointF(rightColX, rOn.bottom()));
    const int bandRight = countColor(img, col, tk.line, dpr);
    check(bandRight >= 3 && bandRight <= 6,
          (QStringLiteral("unfilled track band is the 4px groove, "
                          "not the whole widget [") + tk.label
           + QStringLiteral("] px=") + QString::number(bandRight))
              .toUtf8().constData());

    // Above/below that band: host magenta, i.e. no residue at all.
    const QColor above = at(img, QPointF(rightColX, rOn.top() + 2), dpr);
    const QColor below = at(img, QPointF(rightColX, rOn.bottom() - 2), dpr);
    check(above == kHost && below == kHost,
          (QStringLiteral("nothing paints above/below the track")
           + QStringLiteral(" [") + tk.label + QStringLiteral("]"))
              .toUtf8().constData(),
          above.name(QColor::HexArgb) + QStringLiteral(" / ")
              + below.name(QColor::HexArgb));

    // ── 4. the filled side is THIS theme's clay ───────────────────────
    const QColor fillLeft = at(img, QPointF(leftColX, midY), dpr);
    check(fillLeft == tk.clay,
          (QStringLiteral("sub-page fill is the theme's clay")
           + QStringLiteral(" [") + tk.label + QStringLiteral("]"))
              .toUtf8().constData(),
          fillLeft.name());

    // The unfilled side is THIS theme's line colour (checked as a band
    // above; assert the exact token at the centre row too).
    const QColor emptyRight = at(img, QPointF(rightColX, midY), dpr);
    check(emptyRight == tk.line,
          (QStringLiteral("add-page track is the theme's line colour")
           + QStringLiteral(" [") + tk.label + QStringLiteral("]"))
              .toUtf8().constData(),
          emptyRight.name());

    // ── 5. the handle: theme clay, 14px wide ──────────────────────────
    // The handle's X is not the widget centre: at value 40 of 0..100 it
    // sits at 40% of (width - handleWidth) + half a handle. Measured
    // from the rendered row 5px ABOVE the groove centre — the 4px
    // sub-page does not reach that row, so the only clay there is the
    // handle itself. A 14px round handle yields ~10px of chord that far
    // out; a full-height fill would run the entire row.
    const int handleRun = longestRun(img, midY - 5, rOn, tk.clay, dpr);
    check(handleRun >= 8 && handleRun <= 18,
          (QStringLiteral("handle is a ~14px knob, not a full-height "
                          "stripe [") + tk.label
           + QStringLiteral("] px=") + QString::number(handleRun))
              .toUtf8().constData());
    const int handleX = qRound(rOn.left()
        + (on->value() / 100.0) * (rOn.width() - 14.0) + 7.0);
    const QColor handle = at(img, QPointF(handleX, midY), dpr);
    check(handle == tk.clay,
          (QStringLiteral("handle centre is the theme's clay")
           + QStringLiteral(" [") + tk.label + QStringLiteral("]"))
              .toUtf8().constData(),
          handle.name());

    // ── 6. disabled: same transparent ground, @fg3 handle ─────────────
    const QColor offHandle = at(img,
        QPointF(qRound(rOff.left()
                       + (off->value() / 100.0) * (rOff.width() - 14.0) + 7.0),
                qRound(rOff.center().y())), dpr);
    check(offHandle == tk.fg3,
          (QStringLiteral("disabled handle is the theme's @fg3")
           + QStringLiteral(" [") + tk.label + QStringLiteral("]"))
              .toUtf8().constData(),
          offHandle.name());
    const QRectF colOff(QPointF(rightColX, rOff.top()),
                        QPointF(rightColX, rOff.bottom()));
    const int bandOff = countColor(img, colOff, tk.line, dpr);
    check(bandOff >= 3 && bandOff <= 6,
          (QStringLiteral("disabled track band stays 4px")
           + QStringLiteral(" [") + tk.label
           + QStringLiteral("] px=") + QString::number(bandOff))
              .toUtf8().constData());
    // The disabled FILL (sub-page, left of the handle) is @line in both
    // themes — the `::sub-control:horizontal:disabled` spelling must
    // still paint, not just stop poisoning the ground.
    const QColor offFill = at(img,
        QPointF(leftColX, rOff.center().y()), dpr);
    check(offFill == tk.line,
          (QStringLiteral("disabled sub-page fill still paints @line")
           + QStringLiteral(" [") + tk.label + QStringLiteral("]"))
              .toUtf8().constData(),
          offFill.name());
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    runTheme(kDark);
    runTheme(kLight);

    // The two themes must actually differ on screen, or the loop above
    // would be one assertion run twice.
    setUiTheme(true);
    const QString darkSheet = consoleStyleSheet();
    setUiTheme(false);
    const QString lightSheet = consoleStyleSheet();
    check(darkSheet != lightSheet,
          "dark and light sheets differ (theme switch is live)");
    check(darkSheet.contains(QStringLiteral("QSlider{background:transparent"))
              && lightSheet.contains(
                  QStringLiteral("QSlider{background:transparent")),
          "both themes carry the transparent QSlider ground rule");

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
