#include <QCoreApplication>
#include <QSize>

#include "ui/screen_query.h"

#include <cstdio>

// screen_query's ARITHMETIC, and the two rules that were being violated.
//
// A rotated output reports 1920x1080 as its panel mode and 1080x1920 as its
// geometry. detect() used to take the first and the second as a pair and
// divide: dpr = 1.7778. That number is not a device pixel ratio, it is
// "unrotated panel width divided by rotated logical width", and it passed
// a range check written as `dpr > 0.95 && dpr < 3.05` because 1.7778 is a
// perfectly reasonable-looking value.
//
// Both rules below are pure functions of their arguments, which is why they
// are the part worth testing: the subprocess probes need a live session and
// a compositor, and the DEFECTS were in what the code does with a number
// AFTER the probe returns.
//
// MUTATION: replace isLegalDpr()'s membership test with a range test
// (0.95 < dpr < 3.05) and check 2 turns red.

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL  %s\n", what); }
    else std::printf("PASS: %s\n", what);
}

// THE PRODUCTION FUNCTION, not a copy of it. A test that re-implements
// the rule proves the copy is right, not the code that runs.
bool sameShape(int pw, int ph, int lw, int lh)
{
    return screen_query::sameShapeAsLogical(QSize(pw, ph), QSize(lw, lh));
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    using screen_query::isLegalDpr;

    // ---- 1. THE DISCRETE VALUES ------------------------------------------
    // The header's own comment names these six. Anything else is not a DPR
    // this code path can produce, and treating it as one is the bug.
    for (double d : {1.0, 1.25, 1.5, 2.0, 2.5, 3.0}) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "dpr %.2f is a legal device pixel ratio", d);
        check(isLegalDpr(d), buf);
    }
    // Tolerance: a scaled screen can land just off a clean value. The
    // tolerance is 0.02 (kDprTolerance), so these sit clearly inside it
    // rather than on its boundary - a test that pins an exact edge is
    // testing the comparison operator, not the rule.
    for (double d : {1.01, 1.24, 1.49, 2.01, 2.99}) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "dpr %.2f is within tolerance of a clean value", d);
        check(isLegalDpr(d), buf);
    }

    // ---- 2. THE FABRICATED VALUE ----------------------------------------
    // 1920 / 1080, from a 1920x1080 panel mode paired with a 1080x1920
    // geometry. It sits inside the old range test and outside every real
    // value, which is precisely how it survived.
    check(!isLegalDpr(1920.0 / 1080.0),
          "dpr 1.7778 (1920/1080, a rotated panel mode divided by a "
          "rotated geometry) is REJECTED — it was inside the old range test");
    // Each of these clears every legal value by more than kDprTolerance
    // (0.02), so the assertion is about the membership rule rather than
    // about where the tolerance ends. A value sitting exactly on the
    // boundary would be testing the comparison, not the rule.
    for (double d : {1.778, 2.4, 1.03, 1.18, 2.75, 0.8}) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "dpr %.3f is rejected as not a discrete value", d);
        check(!isLegalDpr(d), buf);
    }

    // ---- 3. THE SHAPE CHECK ----------------------------------------------
    // A candidate mode size is only usable if it describes the SAME screen
    // as the logical size Qt reports. Cross-multiplied, so 1920x1080 and
    // 1080x1920 do not compare equal by accident of integer division.
    check(!sameShape(1920, 1080, 1080, 1920),
          "1920x1080 does not describe a 1080x1920 screen — this is the "
          "check that rejects the rotated panel mode");
    check(sameShape(2560, 1440, 2560, 1440),
          "2560x1440 does describe a 2560x1440 screen");
    check(sameShape(3840, 2160, 1920, 1080),
          "3840x2160 (a 2x panel mode) DOES describe a 1920x1080 logical "
          "screen — a legitimate scaling case must not be rejected");
    check(!sameShape(1920, 1600, 800, 800),
          "1920x1600 does not describe an 800x800 screen — a ratio that is "
          "close enough to look fine and is not");

    // ---- 4. the pair the old code would have built -----------------------
    // Stated as an end-to-end statement of the defect: given the two numbers
    // the probes produced on the reporter's machine, the contract is
    // "reject", and the old range test said "accept".
    {
        const int panelW = 1920, panelH = 1080;   // what xrandr reported
        const int logW = 1080, logH = 1920;      // what Qt reported
        const bool usable = sameShape(panelW, panelH, logW, logH);
        const double fabricated = qreal(panelW) / qreal(logW);
        check(!usable && !isLegalDpr(fabricated),
              "the reporter's exact pair is rejected on BOTH rules — so "
              "there is no path by which dpr 1.7778 survives");
    }

    std::printf("\n== %d checks, %d failures ==\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
