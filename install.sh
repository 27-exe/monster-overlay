#!/usr/bin/env bash
# install.sh — install Monster Overlay: the three binaries, the address maps,
# the bundled REFramework payload and the Rise damage producer.
#
# Layout (everything hangs off one prefix, default ~/.local):
#   <prefix>/bin/                            monster-overlay, monster-control, monster-doctor
#   <prefix>/bin/third_party/REFramework/   the REFramework release zip
#   <prefix>/bin/reframework/autorun/       mhr-overlay-damage.lua
#   <XDG_DATA_HOME>/monster-overlay/data/   the *.map address maps
#   <XDG_DATA_HOME>/monster-overlay/        LICENSE, NOTICE (Apache-2.0 attribution)
#
# Why the payload and the Lua sit next to the binaries instead of in the data
# dir: the executables resolve both relative to their own path and have no
# search path, so installing the binaries alone leaves Rise damage tracking
# silent and unrepairable. The .map files DO honour XDG_DATA_HOME, so those go
# to the shared data dir rather than a hardcoded ~/.local/share.
#
# Safe to re-run: files are overwritten, and anything an earlier run installed
# that this run does not reinstall (a superseded producer, a renamed map) is
# removed first. Nothing this script never installed is deleted — with one
# deliberate exception: another producer for the same IPC channel, which would
# break Rise damage tracking (see purge_stale_producers below).
#
# Options:
#   --help | -h     this text
#   --prefix DIR    install under DIR instead of $HOME/.local
#   --dry-run       print every action, change nothing
#   --uninstall     remove what a previous run of this script installed
set -euo pipefail

# Debian and Ubuntu ship dash as /bin/sh, where BASH_SOURCE does not exist and
# `set -u` turns that into a bare "unbound variable" before this script has
# said anything useful. Ask for bash instead.
if [ -z "${BASH_VERSION:-}" ]; then
    echo "install.sh: this script needs bash — Debian/Ubuntu's /bin/sh is dash." >&2
    echo "              run it as:  bash \"$0\"" >&2
    exit 1
fi

PROG="${0##*/}"

# An unmatched glob must expand to nothing, not to its own literal text —
# otherwise `install ... data/*.map` on an empty data/ directory installs a
# file named "*.map" (or fails halfway through, leaving a partial install).
shopt -s nullglob

usage() {
    cat <<EOF
$PROG — install Monster Overlay into a prefix

Usage: $PROG [options]

Options:
  --prefix DIR    install under DIR instead of \$HOME/.local
  --dry-run       print every action, change nothing
  --uninstall     remove only what a previous run of this script installed
  -h, --help      show this message

What goes where (defaults in brackets):
  binaries, REFramework payload, Lua producer   <prefix>/bin/...
  address maps, LICENSE, NOTICE                 \${XDG_DATA_HOME:-<prefix>/share}/monster-overlay/...

Qt 6.11+ and layer-shell-qt are prerequisites; install them once with:
  sudo ./install-deps.sh
EOF
}

# ---------------------------------------------------------------- arguments --
# Parsed up front so that --help, --dry-run and --uninstall never touch disk.
PREFIX="${HOME}/.local"
UNINSTALL=0
DRY_RUN=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        --prefix)
            [ "$#" -ge 2 ] || { echo "$PROG: --prefix needs a directory" >&2; exit 2; }
            PREFIX="$2"; shift 2 ;;
        --prefix=*) PREFIX="${1#--prefix=}"; shift ;;
        --dry-run)   DRY_RUN=1; shift ;;
        --uninstall) UNINSTALL=1; shift ;;
        --) shift; break ;;
        -*) echo "$PROG: unknown option '$1' — try --help" >&2; exit 2 ;;
        *)  echo "$PROG: unexpected argument '$1' — try --help" >&2; exit 2 ;;
    esac
done

BIN_DIR="${PREFIX%/}/bin"
DATA_ROOT="${XDG_DATA_HOME:-${PREFIX%/}/share}"
DATA_DIR="${DATA_ROOT%/}/monster-overlay/data"
APP_DIR="${DATA_ROOT%/}/monster-overlay"
PAYLOAD_DIR="${BIN_DIR}/third_party/REFramework"
LUA_DIR="${BIN_DIR}/reframework/autorun"
MANIFEST="${BIN_DIR}/monster-overlay-install-manifest.json"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

LUA_NAME="mhr-overlay-damage.lua"
# REFramework's autorun/ means "load every .lua in this directory", so two
# producers publishing the same IPC slots each believe they own them. Any .lua
# in the target directory that declares this schema is one of ours by
# construction, which is what makes the cleanup below safe to do unattended.
PRODUCER_SIGNAL="mhr-overlay-damage"

# ------------------------------------------------------------------ helpers --

log()  { printf '%s\n' "$*"; }
note() { printf '%s\n' "$*" >&2; }

mkdir_p() { # $1 = directory
    [ -d "$1" ] && return 0
    [ "$DRY_RUN" -eq 1 ] && { log "  would create  $1"; return 0; }
    mkdir -p -- "$1"
}

put() { # $1 = mode, $2 = source, $3 = destination
    if [ "$DRY_RUN" -eq 1 ]; then
        log "  would install  $2 -> $3"
    else
        install -m "$1" -- "$2" "$3"
    fi
}

rm_p() { # $1 = path
    [ -e "$1" ] || [ -L "$1" ] || return 0
    if [ "$DRY_RUN" -eq 1 ]; then
        log "  would remove   $1"
    else
        rm -f -- "$1"
    fi
}

in_list() { # $1 = needle, rest = haystack
    local needle="$1" item
    shift
    for item in "$@"; do
        if [ "$item" = "$needle" ]; then return 0; fi
    done
    return 1
}

# Read one array out of the install manifest. Written by this script and read
# by nothing else, so a line-oriented scan is enough — and unlike piping the
# file through grep -o, it stays correct when a path contains a comma.
manifest_list() { # $1 = manifest path, $2 = key
    [ -f "$1" ] || return 0
    awk -v key="$2" '
        $0 ~ "^[ \t]*\"" key "\"[ \t]*:" { inarr = ($0 ~ /\[/); next }
        inarr && $0 ~ /\]/ { inarr = 0 }
        inarr { if (match($0, /"[^"]*"/)) print substr($0, RSTART + 1, RLENGTH - 2) }
    ' "$1"
}

emit_array() { # one entry per argument
    local total=$# i=0 item
    for item in "$@"; do
        i=$((i + 1))
        if [ "$i" -lt "$total" ]; then printf '    "%s",\n' "$item"
        else printf '    "%s"\n' "$item"; fi
    done
}

# The manifest is this script's memory of what it owns. It is deliberately NOT
# named reframework/monster-overlay-install.json: that path is the game
# directory's manifest, owned and validated by RiseReFrameworkManager, and
# writing it here would make the console refuse to install REFramework.
write_manifest() {
    if [ "$DRY_RUN" -eq 1 ]; then
        log "  would write    ${MANIFEST}"
        return 0
    fi
    {
        printf '{\n'
        printf '  "format": "monster-overlay-install-manifest",\n'
        printf '  "version": 1,\n'
        printf '  "directories": [\n'
        emit_array "${ALL_DIRS[@]}"
        printf '  ],\n'
        printf '  "files": [\n'
        emit_array "${RECORDED_FILES[@]}"
        printf '  ],\n'
        printf '  "lua": [\n'
        emit_array "${RECORDED_LUA[@]}"
        printf '  ]\n'
        printf '}\n'
    } > "${MANIFEST}"
    log "  manifest       ${MANIFEST}"
}

# ------------------------------------------------------------- the installer --

# Clean out what an earlier run left behind that this run will not recreate.
# Two separate rules, because they answer different questions:
#   * recorded by us, not reinstalled  -> a genuine orphan from a version bump
#   * declares the producer schema     -> a duplicate producer, which is a bug
purge_stale_producers() {
    local existing base recorded_lua
    recorded_lua="$(manifest_list "$MANIFEST" lua)"
    for existing in "${LUA_DIR}"/*.lua; do
        if [ "$existing" = "${LUA_DIR}/${LUA_NAME}" ]; then continue; fi
        base="${existing##*/}"
        if printf '%s\n' "$recorded_lua" | grep -Fxq -- "$existing"; then
            log "  removing       ${existing}  (recorded by an earlier install, not reinstalled)"
        elif grep -qs -- "$PRODUCER_SIGNAL" "$existing"; then
            log "  removing       ${existing}  (a second producer for $PRODUCER_SIGNAL would fight the current one)"
        else
            log "  leaving        ${existing}  (not installed by this script)"
            continue
        fi
        rm_p "$existing"
    done
}

purge_stale_maps() {
    local existing base recorded
    recorded="$(manifest_list "$MANIFEST" files)"
    for existing in "${DATA_DIR}"/*.map; do
        base="${existing##*/}"
        if in_list "$base" "${NEW_MAPS[@]}"; then continue; fi
        # Only remove a map this script recorded installing. A map that merely
        # matches the naming pattern is somebody else's data — an older build's
        # map left by hand, or a version the user dropped in deliberately. The
        # reader opens one pinned filename per game, so a leftover is inert:
        # there is nothing to gain from deleting it and a file to lose.
        if printf '%s\n' "$recorded" | grep -Fxq -- "$existing"; then
            log "  removing       ${existing}  (recorded by an earlier install, not reinstalled)"
            rm_p "$existing"
        else
            log "  leaving        ${existing}  (not installed by this script)"
        fi
    done
}

purge_recorded_files() {
    local f
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        if in_list "$f" "${NEW_FILES[@]}"; then continue; fi
        if [ -e "$f" ] || [ -L "$f" ]; then
            log "  removing       ${f}  (installed by an earlier run, not by this one)"
            rm_p "$f"
        fi
    done < <(manifest_list "$MANIFEST" files)
}

note_payload_leftovers() {
    local existing base
    for existing in "${PAYLOAD_DIR}"/*; do
        base="${existing##*/}"
        if in_list "$base" "${NEW_PAYLOAD[@]}"; then continue; fi
        log "  NOTE: ${existing} is not installed by this script; leaving it in place."
    done
}

do_install() {
    local src dest mode b m p

    # --- preflight: never leave a half-installed prefix behind ---------------
    # Checked before the first directory is created, so an incomplete release
    # archive fails before anything lands in ~/.local/bin.
    MAP_SOURCES=("${SCRIPT_DIR}"/data/*.map)
    if [ "${#MAP_SOURCES[@]}" -eq 0 ]; then
        note "$PROG: no address maps in ${SCRIPT_DIR}/data — extracted the release archive?"
        exit 1
    fi
    for b in monster-overlay monster-control monster-doctor; do
        if [ ! -f "${SCRIPT_DIR}/${b}" ]; then
            note "$PROG: missing ${SCRIPT_DIR}/${b} — extracted the release archive?"
            exit 1
        fi
    done
    PAYLOAD_SOURCES=("${SCRIPT_DIR}"/third_party/REFramework/*)
    LUA_SOURCES=("${SCRIPT_DIR}"/reframework/*.lua)
    LICENSE_SOURCES=("${SCRIPT_DIR}"/LICENSE "${SCRIPT_DIR}"/NOTICE)
    LICENSES_DIR_SOURCES=("${SCRIPT_DIR}"/LICENSES/*.txt)

    # --- what this run will install (used by the cleanup and the manifest) ---
    NEW_FILES=(); NEW_MAPS=(); NEW_PAYLOAD=(); NEW_LUA=()
    for b in monster-overlay monster-control monster-doctor; do
        NEW_FILES+=("${BIN_DIR}/${b}")
    done
    for m in "${MAP_SOURCES[@]}"; do
        NEW_FILES+=("${DATA_DIR}/${m##*/}"); NEW_MAPS+=("${m##*/}")
    done
    for p in "${PAYLOAD_SOURCES[@]}"; do
        NEW_FILES+=("${PAYLOAD_DIR}/${p##*/}"); NEW_PAYLOAD+=("${p##*/}")
    done
    for p in "${LUA_SOURCES[@]}"; do
        NEW_FILES+=("${LUA_DIR}/${p##*/}"); NEW_LUA+=("${p##*/}")
    done
    for p in "${LICENSE_SOURCES[@]}" "${LICENSES_DIR_SOURCES[@]}"; do
        NEW_FILES+=("${APP_DIR}/${p##*/}")
    done

    ALL_DIRS=("$BIN_DIR" "$PAYLOAD_DIR" "$LUA_DIR" "$DATA_DIR" "$APP_DIR")

    # --- cleanup first, so a version bump cannot stack two producers --------
    purge_recorded_files
    purge_stale_producers
    purge_stale_maps

    # --- create and fill -----------------------------------------------------
    local d
    for d in "${ALL_DIRS[@]}"; do mkdir_p "$d"; done

    for b in monster-overlay monster-control monster-doctor; do
        put 755 "${SCRIPT_DIR}/${b}" "${BIN_DIR}/${b}"
        REPORT+=("  ${BIN_DIR}/${b}")
        RECORDED_FILES+=("${BIN_DIR}/${b}")
    done

    for m in "${MAP_SOURCES[@]}"; do
        put 644 "$m" "${DATA_DIR}/${m##*/}"
        REPORT+=("  ${DATA_DIR}/${m##*/}")
        RECORDED_FILES+=("${DATA_DIR}/${m##*/}")
    done

    if [ "${#PAYLOAD_SOURCES[@]}" -gt 0 ]; then
        for p in "${PAYLOAD_SOURCES[@]}"; do
            put 644 "$p" "${PAYLOAD_DIR}/${p##*/}"
            REPORT+=("  ${PAYLOAD_DIR}/${p##*/}   (REFramework payload)")
            RECORDED_FILES+=("${PAYLOAD_DIR}/${p##*/}")
        done
    else
        SKIPPED+=("  ${PAYLOAD_DIR}/  (source missing: ${SCRIPT_DIR}/third_party/REFramework)")
        note "$PROG: no REFramework payload to install — Rise damage tracking has no producer to install."
    fi

    if [ "${#LUA_SOURCES[@]}" -gt 0 ]; then
        for p in "${LUA_SOURCES[@]}"; do
            put 644 "$p" "${LUA_DIR}/${p##*/}"
            REPORT+=("  ${LUA_DIR}/${p##*/}   (Rise damage producer)")
            RECORDED_FILES+=("${LUA_DIR}/${p##*/}")
            RECORDED_LUA+=("${LUA_DIR}/${p##*/}")
        done
    else
        SKIPPED+=("  ${LUA_DIR}/  (source missing: ${SCRIPT_DIR}/reframework/${LUA_NAME})")
        note "$PROG: Rise damage producer not found — the console cannot repair the game-side install."
    fi

    # The NOTICE records the HunterPie and icon-map credits; Apache-2.0 §4(d)
    # requires it to travel with the product, so it ships even though nothing
    # reads it at runtime.
    local any_license=0
    for p in "${LICENSE_SOURCES[@]}"; do
        [ -f "$p" ] || continue
        any_license=1
        put 644 "$p" "${APP_DIR}/${p##*/}"
        REPORT+=("  ${APP_DIR}/${p##*/}")
        RECORDED_FILES+=("${APP_DIR}/${p##*/}")
    done
    for p in "${LICENSES_DIR_SOURCES[@]}"; do
        any_license=1
        put 644 "$p" "${APP_DIR}/${p##*/}"
        REPORT+=("  ${APP_DIR}/${p##*/}")
        RECORDED_FILES+=("${APP_DIR}/${p##*/}")
    done
    [ "$any_license" -eq 1 ] || SKIPPED+=("  ${APP_DIR}/{LICENSE,NOTICE}  (source missing)")

    note_payload_leftovers
    write_manifest
}

report() {
    log ""
    log "Installed:"
    if [ "${#REPORT[@]}" -gt 0 ]; then
        printf '%s\n' "${REPORT[@]}"
    else
        log "  (nothing)"
    fi

    if [ "${#SKIPPED[@]}" -gt 0 ]; then
        log ""
        log "Skipped:"
        printf '%s\n' "${SKIPPED[@]}"
    fi

    # The default prefix is only on PATH on some distros; do not let the
    # binaries land somewhere the shell will never look.
    case ":${PATH}:" in
        *":${BIN_DIR}:"*) ;;
        *)
            log ""
            log "NOTE: ${BIN_DIR} is not on your PATH. Either add it once:"
            log "  echo 'export PATH=\"${BIN_DIR}:\$PATH\"' >> ~/.profile"
            log "or run the binaries by their full path above." ;;
    esac

    # Name the dependency step as a prerequisite rather than a first command.
    # Listed first it reads as "do this, then launch"; listed second it reads
    # as "launch, and if that fails, deal with this" — which is the order a
    # first-time user would try them in, and it fails on a machine with no Qt.
    if ! command -v qmake6 >/dev/null 2>&1 \
       && ! { command -v pkg-config >/dev/null 2>&1 && pkg-config --exists Qt6Core 2>/dev/null; }; then
        log ""
        note "$PROG: no Qt 6 install is visible on this machine (qmake6 and"
        note "      pkg-config both come up empty), so the binaries will not"
        note "      start yet. Install the runtime dependencies first:"
        note "        sudo ./install-deps.sh"
    fi

    log ""
    log "Run:"
    log "  monster-control         # launch the control console"
    log "  monster-doctor          # overlay shows nothing? writes a report to attach to your issue"
    log ""
    log "If those will not start, the Qt 6.11+ / layer-shell-qt dependencies are"
    log "missing — install them once:"
    log "  sudo ./install-deps.sh"

    if [ "${#SKIPPED[@]}" -gt 0 ]; then
        log ""
        note "$PROG: finished with ${#SKIPPED[@]} skipped piece(s) — see 'Skipped:' above."
        note "      Rise damage tracking will not work until the payload and producer are present."
        exit 1
    fi
}

do_uninstall() {
    if [ ! -f "$MANIFEST" ]; then
        note "$PROG: no manifest at ${MANIFEST} — nothing this script installed."
        if [ -e "${BIN_DIR}/monster-control" ]; then
            note "      ${BIN_DIR}/monster-control exists, so it was installed without a manifest"
            note "      (or by a build of this script that predates it). Remove it by hand."
        fi
        exit 0
    fi

    local f removed=0 d
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        if [ -e "$f" ] || [ -L "$f" ]; then
            log "  removing  $f"
            rm_p "$f"
            removed=$((removed + 1))
        fi
    done < <(manifest_list "$MANIFEST" files)

    # Directories only go if we emptied them, and innermost first.
    local dirs=()
    while IFS= read -r d; do
        [ -n "$d" ] || continue
        dirs=("$d" "${dirs[@]}")
    done < <(manifest_list "$MANIFEST" directories)
    for d in ${dirs[@]+"${dirs[@]}"}; do
        if [ -d "$d" ]; then
            if rmdir -- "$d" 2>/dev/null; then
                log "  removing  $d/ (empty)"
            else
                log "  keeping   $d/ (not empty — has files this script did not install)"
            fi
        fi
    done
    rm_p "$MANIFEST"

    log ""
    log "Removed ${removed} file(s) recorded in ${MANIFEST}"
    if [ "$DRY_RUN" -eq 1 ]; then
        log "(dry run: nothing was actually deleted)"
    fi
    # Anything still sitting in those directories was never ours.
    local leftover
    for leftover in "${LUA_DIR}"/*.lua "${DATA_DIR}"/*.map "${PAYLOAD_DIR}"/*; do
        [ -e "$leftover" ] || continue
        log "  left: ${leftover} (not installed by this script)"
    done
}

# --------------------------------------------------------------------- main --

REPORT=(); SKIPPED=(); RECORDED_FILES=(); RECORDED_LUA=()
NEW_FILES=(); NEW_MAPS=(); NEW_PAYLOAD=(); NEW_LUA=()
MAP_SOURCES=(); PAYLOAD_SOURCES=(); LUA_SOURCES=(); LICENSE_SOURCES=(); LICENSES_DIR_SOURCES=()
ALL_DIRS=()

if [ "$UNINSTALL" -eq 1 ]; then
    do_uninstall
else
    do_install
    report
fi
