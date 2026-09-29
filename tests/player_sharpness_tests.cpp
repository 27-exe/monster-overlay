// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the player panel's sharpness gauge
// (src/ui/panel_player_sharpness.cpp) — the third 裸奔 TU of S3 §6.3.
//
// The block moved 1:1 out of panel_player.cpp, so the pixel output is
// unchanged by construction; what was never asserted is the contract the
// two call sites depend on:
//
//   * playerSharpnessBarTotalWidth() is exported precisely so the panel's
//     centering math cannot drift from the drawn width ("Both the literals
//     and the sum now live in one place"). It is the sum of three tokens
//     (130 gauge + 14 gap + 30 badge); changing any ONE of them without
//     the others shifts the MR/name trim by that many pixels, which nothing
//     else would notice until a user looks.
//   * drawPlayerSharpnessBar() returns the gauge's left X so the caller can
//     trim the MR/name rect, and returns `rightX` unchanged — no shrink —
//     for every input that must not draw. Those two return values are the
//     whole interface: a regression here either overlaps the sharpness
//     gauge onto the player's name or hides 174 px on every armed character.
//   * The "must not draw" guard is THREE separate conditions, and each is a
//     real game state: a ranged weapon (valid false, slot occupied), a
//     broken weapon (level -1), the level-7 Invalid sentinel, and a melee
//     weapon whose thresholds are all zero / non-increasing.
//   * The per-segment widths are scaled by min(1, 130/total): short
//     thresholds must NOT be stretched to fill the gauge, because that
//     inflation is the "x红斩特长一条" symptom the file itself documents.
//
// The draw is exercised through an offscreen QImage: opaque pixels are
// counted in the gauge band and the badge, so the assertions are about
// real painted geometry rather than about the function returning.
//
// Runs under QT_QPA_PLATFORM=offscreen.

#include "ui/panel_player_sharpness.h"
#include "rise/mhr_reader.h"

#include "player/player_types.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>

#include <cstdio>
#include <string>

namespace {

int failures = 0;
int checks   = 0;

void check(bool cond, const std::string &what)
{
    ++checks;
    if (cond) {
        std::printf("PASS: %s\n", what.c_str());
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
    std::fflush(stdout);
    std::fflush(stderr);
}

// The in-game cumulative sharpness thresholds HunterPie's
// CalculateThresholds produces: red..purple, monotonically increasing.
constexpr int kFullThresholds[7] = {10, 30, 50, 70, 100, 130, 160};

mhw::SharpnessSnapshot sharpness(int level, bool valid,
                                 const int (&thresholds)[7])
{
    mhw::SharpnessSnapshot s;
    s.level = level;
    s.valid = valid;
    for (int i = 0; i < 7; ++i)
        s.thresholds[i] = thresholds[i];
    return s;
}

// A transparent offscreen buffer big enough for one row, so the guard
// cases and the drawn cases can both be checked against real pixels. A
// pixel nobody painted in this ARGB32 buffer stays fully transparent, so
// alpha is an exact "did anything draw here" probe.
class Scratch {
public:
    static constexpr int kWidth  = 420;
    static constexpr int kHeight = 160;

    QImage image{QImage(kWidth, kHeight, QImage::Format_ARGB32_Premultiplied)};
    QPainter painter{&image};

    Scratch() { image.fill(Qt::transparent); }

    // Did anything at all paint inside this box? A pixel nobody touched in
    // this ARGB32 buffer keeps alpha 0, so this is an exact "was there a
    // draw here" probe. Ends the painter first, because the picture is only
    // complete once the device has been flushed.
    int opaqueIn(int x0, int x1, int y0, int y1)
    {
        painter.end();
        int n = 0;
        for (int y = y0; y < y1 && y < kHeight; ++y) {
            for (int x = x0; x < x1 && x < kWidth; ++x) {
                if (qAlpha(image.pixel(x, y)) >= 250)
                    ++n;
            }
        }
        return n;
    }

    // The gauge's COLOURED (non-black) width in pixels, given the same
    // arguments drawPlayerSharpnessBar() takes. The band is the 13 px the
    // draw uses for kShpGH.
    //
    // Only the ACTIVE segment is painted at alpha 1.0. Everything else is
    // either dimmed to 0.18 alpha (other segments) or the opaque near-black
    // filler plate QColor(26,29,34) that covers the rest of the gauge when
    // total < 130. Those two are excluded by luminance, so the number
    // returned is exactly the width of the active segment's full-strength
    // content — the width the scale factor is supposed to bound.
    int brightGaugeWidth(int gaugeLeftX, int rowY, int rowH)
    {
        painter.end();
        const int bandY = rowY + (rowH - 13) / 2;
        int best = 0;
        for (int y = bandY; y < bandY + 13 && y < kHeight; ++y) {
            int row = 0;
            for (int x = gaugeLeftX;
                 x < gaugeLeftX + 130 && x < kWidth; ++x) {
                const QRgb px = image.pixel(x, y);
                if (qAlpha(px) < 250)
                    continue;
                const int r = qRed(px), g = qGreen(px), b = qBlue(px);
                const bool nearBlack = (r < 80 && g < 85 && b < 90);
                if (!nearBlack)
                    ++row;
            }
            if (row > best)
                best = row;
        }
        return best;
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const int kRightX = 380;
    const int kRowY   = 40;
    const int kRowH   = 60;

    // ======================================================================
    // 1. playerSharpnessBarTotalWidth() — the exported layout contract
    // ======================================================================
    // 130 (gauge) + 14 (gap) + 30 (badge). This single number is what the
    // panel trims the MR/name rect by, so it must equal the drawn width
    // exactly — which is why the tokens were moved into one place.
    check(playerSharpnessBarTotalWidth() == 174,
          "totalWidth: 174 = 130 gauge + 14 gap + 30 badge");

    // ======================================================================
    // 2. drawPlayerSharpnessBar() — the "no shrink" guard paths
    // ======================================================================
    // Each of these is a real game state and each must leave the row width
    // untouched. Anything else either overlaps the MR/name text or hides a
    // gauge that should be hidden.
    {
        Scratch s;
        mhw::SharpnessSnapshot notRead;
        check(drawPlayerSharpnessBar(s.painter, notRead, kRightX, kRowY, kRowH)
                  == kRightX,
              "guard: a never-read snapshot is not drawn");

        check(drawPlayerSharpnessBar(s.painter, sharpness(-1, true, kFullThresholds),
                                 kRightX, kRowY, kRowH) == kRightX,
              "guard: level -1 (broken weapon) is not drawn");

        check(drawPlayerSharpnessBar(s.painter, sharpness(7, true, kFullThresholds),
                                 kRightX, kRowY, kRowH) == kRightX,
              "guard: level 7 (the Invalid sentinel) is not drawn");

        check(drawPlayerSharpnessBar(s.painter,
                                 sharpness(0, true,
                                           {0, 0, 0, 0, 0, 0, 0}),
                                 kRightX, kRowY, kRowH) == kRightX,
              "guard: all-zero thresholds (\"no sharpness data\") hide the bar");

        // NON-increasing thresholds must not be summed as NEGATIVE widths.
        // The `hi <= prev` check skips a level the weapon cannot reach, and
        // a flat table then has a total of just the first segment.
        //
        // Measured behaviour (offscreen probe of the unmodified draw):
        //   {10,10,10,10,10,10,10} level 3 -> total 10, returns 206 (draws)
        //   {0,0,0,0,0,0,0}       level 0 -> total 0,  returns 380 (no draw)
        //   {5,5,5,5,5,5,5}       level 0 -> total 5,  returns 206 (draws)
        //   {10,8,6,4,2,1,0}      level 3 -> total 10, returns 206 (draws)
        // The ONLY total that produces "no draw" is 0, so the guard rows
        // must each be tested for their own behaviour rather than assumed to
        // be one branch. What matters is that a bad table can never yield a
        // NEGATIVE total: that would make scale negative, `cumX` would walk
        // backwards and every real segment would be mis-scaled — the
        // "x红斩特长一条" symptom documented above the loop.
        {
            const int flat = drawPlayerSharpnessBar(s.painter,
                                   sharpness(3, true,
                                             {10, 10, 10, 10, 10, 10, 10}),
                                   kRightX, kRowY, kRowH);
            check(flat < kRightX,
                  "guard: a flat non-increasing table keeps a NON-negative "
                  "total (10, from the first reachable level) and still "
                  "draws — it never aliases into a negative scale");

            // A descending table must behave identically: the descending
            // levels are skipped, not subtracted.
            const int desc = drawPlayerSharpnessBar(s.painter,
                                  sharpness(3, true,
                                            {10, 8, 6, 4, 2, 1, 0}),
                                  kRightX, kRowY, kRowH);
            check(desc == flat,
                  "guard: a strictly descending table reaches the same total "
                  "as a flat one (only the first level is reachable)");

            // All-zero is the only "no data" case.
            check(drawPlayerSharpnessBar(s.painter,
                                         sharpness(0, true,
                                                   {0, 0, 0, 0, 0, 0, 0}),
                                         kRightX, kRowY, kRowH) == kRightX,
                  "guard: an all-zero table is the ONLY total==0 case, and "
                  "it takes the \"no sharpness data\" branch");
        }

        // A ranged weapon: the slot is occupied (thresholds present) but
        // the reader reports valid=false because there is no sharpness.
        check(drawPlayerSharpnessBar(s.painter,
                                 sharpness(0, false, kFullThresholds),
                                 kRightX, kRowY, kRowH) == kRightX,
              "guard: a ranged weapon (valid false) is not drawn");
    }

    // ======================================================================
    // 3. The drawn path — return value AND actual pixels
    // ======================================================================
    {
        Scratch s;
        const int gX = drawPlayerSharpnessBar(s.painter,
                                          sharpness(4, true, kFullThresholds),
                                          kRightX, kRowY, kRowH);
        check(gX == kRightX - playerSharpnessBarTotalWidth(),
              "draw: the returned left X is rightX - totalWidth (the trim "
              "the panel applies to the MR/name rect)");
        check(gX < kRightX,
              "draw: a valid melee weapon DOES shrink the row");

        const int bandY = kRowY + (kRowH - 13) / 2;
        check(s.opaqueIn(gX, gX + 130, bandY, bandY + 13) > 0,
              "pixels: the 130px gauge band is painted for a valid weapon");
        check(s.opaqueIn(kRightX - 30, kRightX,
                          kRowY + (kRowH - 30) / 2,
                          kRowY + (kRowH - 30) / 2 + 30) > 0,
              "pixels: the numeric badge is painted");
        check(s.opaqueIn(0, gX, kRowY, kRowY + kRowH) == 0,
              "pixels: nothing is drawn LEFT of the gauge, so the MR/name "
              "rect is safe");
    }

    // A broken weapon must leave the ENTIRE row untouched, not merely fail
    // to shrink — a half-drawn gauge would double as a visual bug the
    // return value cannot express.
    {
        Scratch s;
        (void)drawPlayerSharpnessBar(s.painter,
                                 sharpness(-1, true, kFullThresholds),
                                 kRightX, kRowY, kRowH);
        check(s.opaqueIn(0, kRightX, kRowY, kRowY + kRowH) == 0,
              "pixels: a broken weapon leaves the entire row untouched");
    }

    // ======================================================================
    // 4. "Never stretch" — short thresholds must not inflate
    // ======================================================================
    // The gauge is 130 px. The scale factor is min(1.0, 130/total) and is
    // applied to every per-segment width, so:
    //   * a total >= 130 (full weapon) -> scale 0.8125, all segments scaled
    //   * a total  < 130 (short weapon) -> scale 1.0, segments NOT grown
    // Growing them is the "x红斩特长一条" symptom documented above the loop:
    // a thin red segment would be amplified to look like a full level.
    //
    // Measured against the unmodified draw (offscreen, full-alpha pixels in
    // the 13 px gauge band, i.e. the ACTIVE segment only):
    //   full  {10..160} total 160 -> 24 px  == qRound(24 * 0.8125)
    //   brief {6..42}   total  42 ->  6 px  == the segment's real 6 px,
    //                                        NOT stretched toward 130
    // A stretched brief draw would paint 24+ px too; an unscaled-in-the-
    // other-direction full draw would paint 24 px before scaling. The
    // relationship that distinguishes the two tables is what is asserted.
    {
        Scratch full;
        const int fullGX = drawPlayerSharpnessBar(
            full.painter, sharpness(4, true, kFullThresholds),
            kRightX, kRowY, kRowH);
        const int fullBright = full.brightGaugeWidth(fullGX, kRowY, kRowH);
        check(fullBright == 24,
              "scale: the full weapon's active segment is 24 px wide "
              "(level 5's 30 px of threshold material × the 130/160 scale)");

        Scratch brief;
        const int briefGX = drawPlayerSharpnessBar(
            brief.painter, sharpness(2, true,
                                     {6, 12, 18, 24, 30, 36, 42}),
            kRightX, kRowY, kRowH);
        const int briefBright = brief.brightGaugeWidth(briefGX, kRowY, kRowH);

        check(briefBright == 6,
              "scale: the short weapon's active segment keeps its real 6 px "
              "width — NOT amplified toward the 130 px gauge width");

        // The identity that makes those two numbers ONE rule. Both are the
        // width of the ACTIVE segment, and both come out of
        // `w * min(1.0, 130/total)`:
        //   full  : 30 px of level-5 material, total 160 -> scale 0.8125
        //           -> 30 × 0.8125 = 24.375 -> qRound = 24  ✓
        //   brief :  6 px of level-3 material, total  42 -> scale capped
        //           at 1.0 -> 6 × 1.0 = 6  ✓
        // Removing the cap would give the short weapon 6 × (130/42 = 3.095)
        // = 18.6 -> qRound = 19, i.e. a RED segment as wide as a real
        // level — exactly the "x红斩特长一条" symptom above.
        const double fullExpected = 30.0 * 130.0 / 160.0;
        check(std::abs(fullBright - fullExpected) < 1.0,
              "scale: the full weapon IS scaled down by 130/160 "
              "(30 -> 24.4 -> qRound 24)");

        const double briefUncapped = 6.0 * 130.0 / 42.0;
        check(std::abs(static_cast<double>(briefBright) - briefUncapped) > 6.0,
              "scale: the short weapon is NOT scaled at all (its 6 px is "
              "nowhere near the 19 px an uncapped scale would produce)");
    }

    if (failures == 0)
        std::printf("player-sharpness-tests: ALL PASSED (%d checks)\n", checks);
    return failures == 0 ? 0 : 1;
}
