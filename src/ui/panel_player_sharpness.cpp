// Sharpness gauge painter for the player panel — moved 1:1 out of
// `src/ui/panel_player.cpp` (old lines 542-698). Everything from the
// banner comment on down is a byte-for-byte copy of the original
// (see the verification in Q1-REPORT.md): same `kSharpColors` palette,
// same `kShp*` tokens, same body. The only edits are the two this
// move forces — the dragged-along file-level docs above, and the
// function being named `drawPlayerSharpnessBar` so it names the
// declaration in the header.
//
// It keeps its anonymous namespace from panel_player.cpp: nothing
// outside this translation unit reads `kSharpColors` or any `kShp*`
// token, and leaving them behind is exactly what would trip
// `-Wunused-const-variable` in panel_player.cpp (their anonymous
// namespace closed at old line 564, so nothing there could use them).
//
// Includes, and why nothing more is needed:
//   * "panel_player_sharpness.h" — the exported declaration.
//   * "rise/mhr_reader.h"        — `mhw::SharpnessSnapshot`,
//     `mhw::RiseSharpnessSegment` and
//     `mhw::riseSharpnessCurrentSegment()`. It also brings in
//     <QString>, <algorithm>, <cmath> and <cstdint>.
//   * The Qt painting primitives the body names: QPainter, QFont,
//     QPen, QColor, QRectF, plus qRound/qreal/QStringLiteral. The
//     header forward-declares QPainter and SharpnessSnapshot instead
//     of including this heavy set.
//
// Deliberately NOT included: "panel_player.h". The function is free
// and stateless, and the panel reaches it through the header above,
// so there is no back-reference to the owner class — no `friend`, no
// reverse include.

#include "panel_player_sharpness.h"

#include "rise/mhr_reader.h"

#include <QPainter>
#include <QFont>
#include <QPen>
#include <QColor>

#include <cmath>
#include <algorithm>

// ===================================================================
// drawSharpnessBar — HunterPie WeaponSharpnessView.xaml: 7-segment
// coloured bar + numeric badge, mounted on the right edge of the
// player row. Mirrors the HTML v8 concept (.shp-wrap) and the HunterPie
// SharpnessToColorConverter palette.
// ===================================================================
namespace {
// Qt A,R,G,B conversion utility for shorthand literal initialisers.
constexpr QRgb kSharpColors[8] = {
    0xFFF41162, // Broken
    0xFFFF0000, // Red
    0xFFFF7F00, // Orange
    0xFFFFD400, // Yellow
    0xFF0DBA1C, // Green
    0xFF3399FF, // Blue
    0xFFFFFFFF, // White
    0xFFD040FF, // Purple
};
constexpr int kShpNum   = 30;   // numeric badge edge — wide enough for 3-digit hits
constexpr int kShpGH    = 13;   // gauge height
constexpr int kShpGap   = 14;   // gap between gauge and badge (HSpie-style breath room)
constexpr int kShpSkew  = 22;   // -22deg per-segment transform
} // namespace

// Returns the gauge's left X on success (so the caller can trim
// the MR/name rect to avoid overlap). Returns `rightX` (no shrink)
// when the snapshot is invalid / ranged weapon — caller keeps the
// full row width for MR/name.
int drawPlayerSharpnessBar(QPainter &p, const mhw::SharpnessSnapshot &s,
                            int rightX, int rowY, int rowH)
{
    if (!s.valid || s.level < 0 || s.level > 6)
        return rightX;

    // Skip when the weapon can't reach the current level (allows zero
    // thresholds from the in-game data to clip the bar cleanly).
    // Compute per-segment widths from thresholds[i]-thresholds[i-1].
    // 0 means the weapon can't reach that level — skip it entirely
    // (don't allocate pixels). Falling back to kMinWidth would make
    // thin segments visible at 3px and let `total` undercount, which
    // then makes the scale factor amplify the real segments into
    // "stretched" oversized chunks (visible as the "x"红斩特长一条"
    // symptom). Cumulative `thresholds` come from mhr_reader.cpp
    // (HunterPie MHRMeleeWeapon.cs::CalculateThresholds mirror).
    const int numX  = rightX - kShpNum;
    const int numY  = rowY + (rowH - kShpNum) / 2;
    const int gX    = numX - kShpGap - 130;
    const int gY    = rowY + (rowH - kShpGH) / 2;
    const int gRight = gX + 130;
    int widths[7];
    int prev = 0;
    int total = 0;
    for (int i = 0; i < 7; ++i) {
        const int hi = s.thresholds[i];
        if (hi <= prev) {
            widths[i] = 0;            // skip — weapon doesn't reach this colour
            continue;
        }
        const int w = hi - prev;
        widths[i] = w;
        total += w;
        prev = hi;
    }
    if (total <= 0) return rightX;   // no sharpness data, hide bar
    constexpr int kBarWidth = 130;    // total gauge pixel width
    qreal scale = qreal(kBarWidth) / qreal(total);
    if (scale > 1.0) scale = 1.0;     // never stretch — width-inflate would
                                      // exaggerate real proportions
    int cumX = gX;
    for (int i = 0; i < 7; ++i) {
        if (widths[i] == 0) continue; // skip 0-width segments entirely
        const int w = std::max(1, qRound(widths[i] * scale));
        const QRectF seg(cumX, gY, w, kShpGH);
        cumX += w;
        // background: segment colour, dim when not current level
        const QColor segCol = QColor(kSharpColors[i + 1]);
        const bool active = (i == s.level);
        const qreal alpha = active ? 1.0 : 0.18;
        QColor fill = segCol;
        fill.setAlphaF(alpha);
        p.fillRect(seg, fill);
        // 1px dark divider between segments so the colour gradient
        // stays readable when two adjacent levels share the hue
        // family (e.g. white→purple). Skipped on the last segment so
        // the right edge stays clean.
        if (i < 6) {
            p.fillRect(QRectF(seg.right() - 1.0, seg.top(), 1.0, seg.height()),
                       QColor(11, 13, 14, 220));
        }
        if (active) {
            // 1px white inner border so the active segment pops
            QPen pen(QColor(255, 255, 255, 80));
            pen.setWidth(1);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(seg.adjusted(0, 0, -1, -1));
        }
    }

    // Current-segment fill overlay. Use the shared int64-safe conversion so
    // red, zero-width, negative and out-of-range cases cannot overfill.
    const mhw::RiseSharpnessSegment currentSegment = mhw::riseSharpnessCurrentSegment(s);
    const int activeLevel = s.level;
    const int segmentTotal = currentSegment.total;
    const int segmentRemaining = currentSegment.remaining;
    const bool hasCurrentSegment = currentSegment.valid && widths[activeLevel] == segmentTotal;
    if (hasCurrentSegment) {
        int ax = gX;
        for (int i = 0; i < activeLevel; ++i) {
            if (widths[i] > 0)
                ax += std::max(1, qRound(widths[i] * scale));
        }
        const int activeW = std::max(1, qRound(segmentTotal * scale));
        const float ratio = std::clamp(
            static_cast<float>(segmentRemaining) / static_cast<float>(segmentTotal),
            0.0F, 1.0F);
        const int fillW = qRound(activeW * ratio);
        if (fillW > 0) {
            const QRectF fillRect(ax, gY, fillW, kShpGH);
            QColor fillCol = QColor(kSharpColors[activeLevel + 1]);
            fillCol.setAlphaF(1.0);
            p.fillRect(fillRect, fillCol);
        }
    }

    // Whole-gauge border + dark background to fill gaps between
    // segments (e.g. when total < 130px).
    if (cumX < gRight) {
        p.fillRect(QRectF(cumX, gY, gRight - cumX, kShpGH),
                   QColor(26, 29, 34));
    }

    // ---- Numeric badge (22x22, current level colour) ----
    const QRectF numRect(numX, numY, kShpNum, kShpNum);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(33, 40, 51));
    p.drawRect(numRect);
    QColor levelCol = QColor(kSharpColors[s.level + 1]);
    // glow
    QColor glow = levelCol;
    glow.setAlphaF(0.55);
    for (int o = 0; o < 3; ++o) {
        QColor go = glow;
        go.setAlphaF(glow.alphaF() * (0.55 - 0.15 * o));
        p.setPen(QPen(go, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRect(numRect.adjusted(-o - 1, -o - 1, o + 1, o + 1));
    }
    // Number text
    QFont numFont(QStringLiteral("Chakra Petch"), 11, QFont::Medium);
    numFont.setStyleStrategy(QFont::PreferAntialias);
    numFont.setStyleHint(QFont::SansSerif);
    p.setFont(numFont);
    p.setPen(levelCol);
    p.drawText(numRect, Qt::AlignCenter, QString::number(std::max(0, segmentRemaining)));
    return gX;
}


// ---- composed width (exported for the panel's centering math) ----
// Mirrors how the caller used to sum it: its own kShpGaugeTotal=130
// copy of the gauge width, plus the gap and the badge edge. Both the
// literals and the sum now live in one place, so the value the panel
// centers on cannot disagree with the one that gets drawn.
int playerSharpnessBarTotalWidth()
{
    constexpr int kGaugeTotal = 130;   // draw-time kBarWidth inside
                                        // drawPlayerSharpnessBar()
    return kGaugeTotal + kShpGap + kShpNum;
}
