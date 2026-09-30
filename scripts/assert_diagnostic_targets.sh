#!/usr/bin/env bash
# Build every EXCLUDE_FROM_ALL diagnostic target, and run none of them.
#
# WHY THIS EXISTS
#   Those targets are excluded from the default build because they are only
#   MEANINGFUL with a running game: running them in CI would hang or fail.
#   That reasoning is right for RUNNING them and wrong for COMPILING them,
#   and the gap cost real time. rise-probe was broken by the ZoneRef change
#   and stayed broken for the whole refactor: a default `ninja` never
#   compiled it, and ctest reported 47/47 green while the target could not
#   build at all.
#
#   Compiling is cheap and needs no game. This catches the whole class of
#   "default build cannot see this" breakage.
#
# KEEP IN SYNC
#   MHW_DIAGNOSTIC_TARGETS in CMakeLists.txt must list every EXCLUDE_FROM_ALL
#   target. A new one that is missing here is a target CI cannot see.
set -u

BUILD_DIR="${1:-$PWD/build}"
if [ ! -d "$BUILD_DIR" ]; then
    printf 'SKIP  no build directory at %s — run cmake first\n' "$BUILD_DIR"
    exit 0
fi

TARGETS="snap-world-mp snap-gauge-fill snap-empty-state snap-player-offline rise-probe"

fail=0
for t in $TARGETS; do
    # Only build: these are interactive/game-bound tools by design.
    if cmake --build "$BUILD_DIR" --target "$t" >/tmp/mhw-diag-build.log 2>&1; then
        printf 'PASS  %-24s built\n' "$t"
    else
        printf 'FAIL  %-24s build failed\n' "$t"
        # Show the compiler's own words, not a summary of them.
        grep -E 'error:|FAILED:' /tmp/mhw-diag-build.log | head -3 | sed 's/^/        /'
        fail=1
    fi
done

exit $fail
