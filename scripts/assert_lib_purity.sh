#!/usr/bin/env bash
# S3 library-split purity assertions. Every count must be 0.
#
# WHAT THIS PROVES
#   The binary-layer coupling between the two games is gone: no archive
#   carries the other game's code, no archive's externally-visible symbol
#   surface exposes a reader or a game data table, and no archive has an
#   undefined reference into a game-specific symbol it does not own.
#
# WHY THE PATTERNS ARE SHAPED THIS WAY
#   The first version of this gate listed only CamelCase spellings
#   (mhw::Mhr, kRise..., WorldZone). That missed mhw::riseAilmentNameEn and
#   friends entirely: injecting src/rise/mhr_abnormalities.cpp into
#   monster-core exported 24 symbols, none of which the patterns named, and
#   the gate still reported 0. A gate that cannot fail on the thing it exists
#   to catch is worse than none, because it is read as proof.
#
#   So the kernel pattern matches whole NAME STEMS case-insensitively — any
#   symbol containing "rise", "mhr", "world" or "monsterhunter" — rather than
#   a list of spellings someone remembered to type. The three functions whose
#   names legitimately mention a game (riseMapCandidates, worldMapCandidates,
#   findRiseInstallDir) are excluded by name below; that is a deliberate,
#   enumerated exception, not a gap in the pattern. The verbatim output of a
#   run is kept outside the tree (experiment/workspaces/monster-overlay-arch-audit),
#   so nothing here points at a path that is not in the repo.
#
#   gmower-style stems are checked against DEFINED symbols only. The
#   undefined-reference assertions stay narrow on purpose: a library
#   legitimately references the contract vocabulary it implements.
set -u

BUILD_DIR="${1:-$PWD/build}"
if [ ! -d "$BUILD_DIR" ]; then
    printf 'SKIP  no build directory at %s — run ninja first\n' "$BUILD_DIR"
    exit 0
fi
cd "$BUILD_DIR" || exit 2

fail=0
report() {  # $1=label $2=count
    if [ "$2" -eq 0 ]; then
        printf 'PASS  %-56s 0\n' "$1"
    else
        printf 'FAIL  %-56s %s\n' "$1" "$2"
        fail=1
    fi
}

# Symbols whose names mention a game but are generic helpers, driven by a
# game-supplied string. Enumerated explicitly so that ADDING another one
# fails the gate and forces a decision, instead of being silently excluded.
#
# These two remain in the kernel because taking the file name as a parameter
# is what makes them game-agnostic: the kernel does not know whose map is
# being searched for, only that a caller asked for one. A third game's map
# search is a call to these, not an edit here.
#
# findRiseInstallDir used to be in this list. It left the kernel in 67bcf4b
# (its Steam appId and exe name are Rise's facts, so it moved to
# src/rise/rise_install_locator.cpp). It is deliberately NOT re-listed: it is
# no longer defined in this archive, so leaving it out proves that.
GENERIC_GAME_NAMED='riseMapCandidates|worldMapCandidates'

# Case-insensitive "does this name mention a game" test over defined symbols,
# minus the enumerated exceptions. Prints the offending symbols so a failure
# names them instead of only counting.
gamed() {  # $1=archive
    nm -C --defined-only "$1" 2>/dev/null \
        | grep -E '^[0-9a-f]+ [TDBRCW] ' \
        | grep -Eiv "$GENERIC_GAME_NAMED" \
        | grep -Ei 'rise|mhr|world|monsterhunter|huntingzone|qurio'
}
u() { nm -C --undefined-only "$1" 2>/dev/null | grep -Ec "($2)"; }

# --- 1. the kernel carries no game symbol at all -----------------------------
count=$(gamed libmonster-core.a | wc -l)
report "libmonster-core.a: no game-named symbol" "$count"
if [ "$count" -ne 0 ]; then
    gamed libmonster-core.a | head -5 | sed 's/^/        /'
fi

# --- 2. each game library carries only its own game's names ------------------
# Rise names, checked case-insensitively as whole stems rather than a list of
# spellings — see the header for why a remembered list was not enough.
count_rise_in_world=$(nm -C --defined-only libgame-world.a 2>/dev/null \
    | grep -E '^[0-9a-f]+ [TDBRCW] ' | grep -Eic 'rise|mhr|monsterhunter|reframework')
report "libgame-world.a: no Rise symbol" "$count_rise_in_world"
if [ "$count_rise_in_world" -ne 0 ]; then
    nm -C --defined-only libgame-world.a 2>/dev/null | grep -E '^[0-9a-f]+ [TDBRCW] ' \
        | grep -Ei 'rise|mhr|monsterhunter|reframework' | head -3 | sed 's/^/        /'
fi

# World names. Qurio is deliberately NOT in this pattern: Qurio is a
# Rise-only monster, so the "Q" reads like a World marker but its three
# symbols here are genuine Rise code, and flagging them would be the same
# class of false alarm that makes people stop reading a gate.
count_world_in_rise=$(nm -C --defined-only libgame-rise.a 2>/dev/null \
    | grep -E '^[0-9a-f]+ [TDBRCW] ' \
    | grep -Ei 'mhw::Mhw|WorldZone|mhw::zoneName|mhw::isHuntingZone|kPartSchemas|kCrownThresholds' \
    | wc -l)
report "libgame-rise.a: no World symbol" "$count_world_in_rise"
if [ "$count_world_in_rise" -ne 0 ]; then
    nm -C --defined-only libgame-rise.a 2>/dev/null | grep -E '^[0-9a-f]+ [TDBRCW] ' \
        | grep -Ei 'mhw::Mhw|WorldZone|mhw::zoneName|mhw::isHuntingZone|kPartSchemas|kCrownThresholds' \
        | head -3 | sed 's/^/        /'
fi

# --- 3. the UI carries no reader --------------------------------------------
count=$(nm -C --defined-only libmhw-ui.a 2>/dev/null \
    | grep -E '^[0-9a-f]+ [TDBRCW] ' \
    | grep -Eic 'MhwReader|MhrReader|AddressMap|readMonsters|readParty|followPointerChain')
report "libmhw-ui.a: no reader symbol" "$count"

# --- 4. no reverse dependency -------------------------------------------------
report "libgame-world.a references no Rise symbol" \
    "$(u libgame-world.a 'mhw::(Mhr|Rise|rise|ReFramework)')"
report "libgame-rise.a references no World symbol" \
    "$(u libgame-rise.a 'mhw::(Mhw|World|world|zoneName|isHuntingZone)')"

echo
echo "=== archive sizes (was: monster-core 20664 KiB, mhw-reader 6520 KiB) ==="
ls -l libmonster-core.a libgame-rise.a libgame-world.a libmhw-ui.a \
    | awk '{printf "  %-20s %8.1f KiB\n", $NF, $5/1024}'

exit $fail
