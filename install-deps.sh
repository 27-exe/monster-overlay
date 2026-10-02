#!/usr/bin/env bash
# install-deps.sh — install the runtime dependencies the overlay needs but does
# not bundle: Qt 6.11+ (Core, Gui, Widgets, Svg), the Wayland client glue, the
# layer-shell wrapper, and libarchive.
#
# The Qt 6.11 floor is not a preference. CMakeLists.txt calls
# find_package(Qt6 6.11 ...), so anything older fails the linker with missing
# Qt_6.11_PRIVATE_API symbols, and libQt6Svg.so.6 is a hard DT_NEEDED of every
# binary — monster-doctor, which only links Core, links it too.
#
# bsdtar is needed at runtime as well: RiseReFrameworkManager shells out to it
# five times to read and verify the REFramework release zip. On Debian/Ubuntu it
# comes from libarchive-tools, not libarchive (which is the shared library).
#
# HOW THIS WORKS — there is deliberately no "distro → Qt version" table.
# ----------------------------------------------------------------------------
# A table like that is a promise about a moving target, and this script has
# already shipped two broken versions of it: Fedora 42 was once listed as
# 6.8.2 (when 42 actually ships 6.10.x and 40 was the 6.8.2 one), and openSUSE
# Tumbleweed was assumed to carry Qt 6.11 while the main OSS repo tracked an
# older point release. Every fix to such a table is itself a guess that expires
# at the next distro release.
#
# So the distribution is used for exactly one thing: choosing *package names*.
# Whether the result actually satisfies the minimum is decided afterwards, by
# asking the thing that was installed — `qmake6 -query QT_VERSION`, falling
# back to `pkg-config --modversion Qt6Core`. The number that comes back is what
# the script reports on. No matter what the package manager claimed, if the
# installed Qt is older than 6.11 the script says which version it measured,
# says why the binaries will not run, and prints the concrete fix.
#
# Idempotent: re-running is a no-op if every package is already installed.
#
# Options: --help | -h
set -euo pipefail

# Debian and Ubuntu ship dash as /bin/sh, where BASH_SOURCE does not exist and
# `set -u` aborts on it before this script has printed anything useful.
if [ -z "${BASH_VERSION:-}" ]; then
    echo "install-deps.sh: this script needs bash — Debian/Ubuntu's /bin/sh is dash." >&2
    echo "                 run it as:  bash \"$0\"" >&2
    exit 1
fi

PROG="${0##*/}"

# The floor the linker enforces (CMakeLists.txt: find_package(Qt6 6.11 ...)).
QT_MIN="6.11"

usage() {
    cat <<EOF
$PROG — install the overlay's runtime dependencies

Usage: sudo $PROG

Installs, per distribution, the Qt 6 runtime modules, the Wayland client glue,
the layer-shell wrapper and libarchive/bsdtar.

The distribution only decides which package names to install. After installing,
the script asks the installed toolchain which version it actually got
(\`qmake6 -query QT_VERSION\`, falling back to \`pkg-config --modversion Qt6Core\`)
and fails if it is older than Qt $QT_MIN.

It then verifies the shared libraries every binary links at runtime
(libQt6Svg.so.6, libLayerShellQtInterface.so.6) and the bsdtar helper, naming
the package that provides anything missing.
EOF
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    "") ;;
    *) echo "$PROG: unexpected argument '$1' — try --help" >&2; exit 2 ;;
esac

# sudo is not guaranteed to exist (a root shell, a container, a distro without
# it installed). Calling it unconditionally dies with "sudo: command not found"
# and exit 127, which reads as "the script is broken" rather than "we are root".
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "$PROG: not running as root and no sudo found." >&2
        echo "      Re-run as root, or install sudo." >&2
        exit 1
    fi
fi

# ---------------------------------------------------------------------------
# Reading the state the binaries will actually meet. No guessing below.
# ---------------------------------------------------------------------------

# Ask the installed Qt what version it is.
#
# qmake6 is the authority: it belongs to the same Qt the headers and libs come
# from, so `qmake6 -query QT_VERSION` is the version the linker will resolve
# against. pkg-config is the fallback for the distros that keep qmake6 in a
# separate tools package (Debian/Ubuntu: qt6-base-dev-tools).
#
# Prints the version (e.g. 6.11.2) or nothing at all if Qt 6 is not installed
# or neither tool can see it.
probe_qt_version() {
    local v=""

    if command -v qmake6 >/dev/null 2>&1; then
        v="$(qmake6 -query QT_VERSION 2>/dev/null || true)"
    fi

    if [ -z "$v" ] && command -v pkg-config >/dev/null 2>&1; then
        if pkg-config --exists Qt6Core 2>/dev/null; then
            v="$(pkg-config --modversion Qt6Core 2>/dev/null || true)"
        fi
    fi

    v="${v//[[:space:]]/}"
    # Keep the leading dotted-number part only: version strings can carry
    # suffixes such as 6.11.2~git33.1d..., which arithmetic comparison would
    # otherwise choke on.
    v="${v%%[!0-9.]*}"
    printf '%s\n' "$v"
}

# True when version $1 >= version $2, comparing dot-separated numeric
# components; an absent component counts as 0 (so 6.11 == 6.11, 6.9 < 6.11).
version_ge() {
    local a="${1:-0}" b="${2:-0}"
    local -a A=() B=()
    IFS='.' read -r -a A <<< "$a"
    IFS='.' read -r -a B <<< "$b"

    local n=${#A[@]}
    (( ${#B[@]} > n )) && n=${#B[@]}

    local i x y
    for (( i = 0; i < n; i++ )); do
        x="10#${A[i]:-0}"
        y="10#${B[i]:-0}"
        (( x > y )) && return 0
        (( x < y )) && return 1
    done
    return 0
}

# Is library $1 loadable, i.e. will `ldd` find it?
#
# The ldconfig cache is what the dynamic linker and ldd both consult, so this
# answer matches what the binaries will do at startup. The pattern requires the
# soname to be a whole word so a plain libQt6Svg.so does not satisfy a lookup
# for libQt6Svg.so.6.
lib_is_loadable() {
    local soname="$1" cache dir found

    if command -v ldconfig >/dev/null 2>&1; then
        # Capture the cache, then search the variable. Piping straight into
        # `grep -q` would be shorter, but grep -q exits at its first match and
        # ldconfig then dies of SIGPIPE — under `set -o pipefail` that 141
        # becomes the function's result and a library that IS present is
        # reported as missing.
        cache="$(ldconfig -p 2>/dev/null || true)"

        # One cache entry per line: "TAB soname SPACE (libc6,...) => path".
        # Require the soname to sit on a delimiter so libQt6Svg.so does not
        # satisfy a lookup for libQt6Svg.so.6.
        local entry name
        while IFS= read -r entry; do
            name="${entry#"${entry%%[![:space:]]*}"}"   # strip leading blanks
            name="${name%%[[:space:]]*}"                 # up to the first blank
            if [ "$name" = "$soname" ]; then
                return 0
            fi
        done <<< "$cache"
        return 1
    fi

    # No ldconfig (musl images, some containers): fall back to a filesystem
    # lookup so "not found" is reported as "not found" rather than as OK.
    for dir in /usr/lib /usr/lib64 /usr/local/lib /lib /lib64 /usr/lib/x86_64-linux-gnu /usr/lib64/x86_64-linux-gnu; do
        [ -d "$dir" ] || continue
        found="$(find "$dir" -maxdepth 3 -name "$soname" -print -quit 2>/dev/null || true)"
        [ -n "$found" ] && return 0
    done
    return 1
}

# Report an unmet dependency and stop. The first argument is a one-line summary
# of what is wrong; the rest are the detail lines printed under "Fix it:".
report_shortfall() {
    local problem="$1"
    shift

    echo "" >&2
    echo "$PROG: dependency check FAILED — the overlay will not start." >&2
    echo "" >&2
    echo "  $problem" >&2

    if [ "$#" -gt 0 ]; then
        echo "" >&2
        echo "  Fix it:" >&2
        local line
        for line in "$@"; do
            echo "    $line" >&2
        done
    fi
    echo "" >&2
    exit 1
}

# Read a field out of /etc/os-release without parsing it as code.
os_release_field() {
    local field="$1" f val
    for f in /etc/os-release /usr/lib/os-release; do
        [ -r "$f" ] || continue
        val="$(sed -n "s/^${field}=//p" "$f" 2>/dev/null | head -n1)"
        val="${val%\"}" ; val="${val#\"}"
        val="${val%\'}" ; val="${val#\'}"
        [ -n "$val" ] && { printf '%s\n' "$val"; return 0; }
    done
    return 0
}

distro_matches() {
    grep -qsiE "$1" /etc/os-release /usr/lib/os-release 2>/dev/null
}

# The Qt version gate. $1 = short distro label for the message (may be empty),
# $2 = this distro's package list for a Qt 6.11 install.
verify_qt_version() {
    local distro_desc="$1" pkgs="$2"
    local have extra=() line
    have="$(probe_qt_version)"

    echo ""
    if [ -z "$have" ]; then
        report_shortfall \
            "Qt was installed, but the version cannot be determined." \
            "Neither qmake6 nor pkg-config can see a Qt 6 install, so there is" \
            "nothing to measure. Usually the Qt tools package is missing:" \
            "    Arch:           qt6-base                  (ships qmake6)" \
            "    Fedora:         qt6-qtbase-devel qt6-tools" \
            "    openSUSE:       qt6-base-devel  qt6-tools" \
            "    Debian/Ubuntu:  qt6-base-dev-tools" \
            "" \
            "The overlay needs Qt $QT_MIN or newer; install the tools package" \
            "and re-run $PROG."
    fi

    echo "Measured Qt version: $have  (qmake6 -query QT_VERSION, or pkg-config --modversion Qt6Core)"

    if version_ge "$have" "$QT_MIN"; then
        echo "OK: Qt $have ≥ $QT_MIN — verified from the installed toolchain, not assumed."
        echo ""
        return 0
    fi

    # Too old. Distro-specific escape hatches first — for a fixed release this is
    # the only thing that helps — then the generic advice.
    if distro_matches 'tumbleweed'; then
        extra=(
            ""
            "By default openSUSE Tumbleweed follows the Qt in its main OSS"
            "repository, which can trail the latest Qt 6 release. KDE ships newer"
            "builds in the KDE:Qt6 repository. Switch to it:"
            ""
            "  sudo zypper addrepo --refresh \\"
            "      https://download.opensuse.org/repositories/KDE:Qt6/openSUSE_Tumbleweed/KDE:Qt6.repo KDE:Qt6"
            "  sudo zypper dist-upgrade --from KDE:Qt6 --allow-vendor-change"
            "  sudo $PROG"
            ""
            "This is not automatic on purpose: pulling in a KDE repository is your"
            "call, not this script's. (Tumbleweed's OSS repo has also moved to newer"
            "Qt on its own before, which is exactly why the version is measured"
            "here instead of assumed.)"
        )
    elif distro_matches 'opensuse|suse' && ! distro_matches 'tumbleweed'; then
        local suse_rel
        suse_rel="$(os_release_field PRETTY_NAME)"
        [ -z "$suse_rel" ] && suse_rel="this openSUSE release"
        extra=(
            ""
            "$suse_rel is a fixed release: its Qt will not move on its own, so"
            "re-running this script changes nothing. KDE publishes newer Qt in"
            "the KDE:Qt6 repository. Switch to it:"
            ""
            "  sudo zypper addrepo --refresh \\"
            "      https://download.opensuse.org/repositories/KDE:Qt6/openSUSE_Leap_16.0/KDE:Qt6.repo KDE:Qt6"
            "  sudo zypper dist-upgrade --from KDE:Qt6 --allow-vendor-change"
            "  sudo $PROG"
            ""
            "  (replace Leap_16.0 with your release, e.g. openSUSE_Leap_15.6)"
            ""
            "Mixing a KDE repo into a stable release can pull in other KDE"
            "libraries. The prebuilt tarball from the README bundles its own Qt"
            "and avoids that trade-off."
        )
    fi

    echo "" >&2
    echo "ERROR: the installed Qt is $have, but this overlay requires Qt $QT_MIN or newer." >&2
    if [ -n "$distro_desc" ]; then
        echo "       (detected $distro_desc)" >&2
    fi
    echo "" >&2
    echo "  The install itself went through — your distribution simply does not" >&2
    echo "  carry Qt $QT_MIN yet, or is still on the older Qt it shipped with." >&2
    echo "  Installing Qt $have here would succeed and the binaries would then" >&2
    echo "  fail to start with missing Qt_${QT_MIN} versioned symbols, so this" >&2
    echo "  script stops before that happens." >&2
    echo "" >&2
    echo "  The package list a Qt $QT_MIN system needs on your distribution is:" >&2
    echo "" >&2
    echo "      $pkgs" >&2
    echo "" >&2
    if [ "${#extra[@]}" -gt 0 ]; then
        for line in "${extra[@]}"; do
            echo "  $line" >&2
        done
        echo "" >&2
    fi
    echo "  Also fine: the prebuilt tarball from the README bundles the Qt it" >&2
    echo "  needs, so no system Qt satisfies the requirement at all." >&2
    exit 1
}

# Check the libraries the binaries link at runtime.
# $1 = distro label, $2 = package name for the Qt 6 layer-shell wrapper,
# $3 = package name for bsdtar.
verify_runtime_libs() {
    local distro="$1" ls_pkg="$2" bsdtar_pkg="$3"
    local missing=() m lines=()

    lib_is_loadable "libQt6Svg.so.6"    || missing+=("libQt6Svg.so.6")
    lib_is_loadable "libLayerShellQtInterface.so.6" || missing+=("libLayerShellQtInterface.so.6")

    if [ "${#missing[@]}" -gt 0 ]; then
        for m in "${missing[@]}"; do
            case "$m" in
                libQt6Svg.so.6)
                    lines+=(
                        "Missing: libQt6Svg.so.6 — every binary links it (monster-doctor"
                        "          too, even though it only uses Qt Core). It comes from"
                        "          your distribution's Qt Svg package:"
                        "            Arch:           qt6-svg"
                        "            Fedora:         qt6-qtsvg"
                        "            openSUSE/TW:    qt6-svg"
                        "            Debian/Ubuntu:  qt6-svg"
                        "          Install it and re-run $PROG."
                    )
                    ;;
                libLayerShellQtInterface.so.6)
                    lines+=(
                        "Missing: libLayerShellQtInterface.so.6 — the layer-shell wrapper"
                        "          that puts the overlay on the Wayland overlay layer. On"
                        "          your distribution that library is provided by:"
                        "            $ls_pkg"
                        "          Watch the Qt5-era name trap: 'layer-shell-qt' without the"
                        "          6 is the Qt 5 build on several distributions and ships"
                        "          libLayerShellQtInterface.so.5, which does not satisfy"
                        "          these binaries. Install it and re-run $PROG."
                    )
                    ;;
            esac
        done
        report_shortfall \
            "On $distro something the overlay links against at runtime is not present." \
            "${lines[@]}"
    fi

    if ! command -v bsdtar >/dev/null 2>&1; then
        report_shortfall \
            "On $distro the bsdtar command is missing." \
            "REFramework release verification shells out to it at runtime, so" \
            "monster-doctor's install/repair paths cannot work without it." \
            "" \
            "Install: $bsdtar_pkg" \
            "Note: on Debian/Ubuntu bsdtar ships in 'libarchive-tools', while" \
            "'libarchive' there is only the shared library."
    fi

    echo "OK: runtime links resolved — libQt6Svg.so.6, libLayerShellQtInterface.so.6, bsdtar."
    echo ""
}

# ---------------------------------------------------------------------------
# Per-distribution installs.
#
# Each function does exactly two things: install the right *names* for this
# distro, then measure and report the truth. The version is never read off the
# distro's name.
# ---------------------------------------------------------------------------

install_arch() {
    $SUDO pacman -S --needed --noconfirm \
        qt6-base \
        qt6-declarative \
        qt6-svg \
        qt6-wayland \
        layer-shell-qt \
        libarchive

    verify_qt_version "" \
        "qt6-base qt6-declarative qt6-svg qt6-wayland layer-shell-qt libarchive"
    verify_runtime_libs "Arch Linux" "layer-shell-qt" "libarchive"
}

install_fedora() {
    # qt6-qtsvg rather than qt6-base: Fedora splits the Qt 6 modules into
    # one package each, so the base module does not pull Svg in. libarchive
    # provides bsdtar as a subpackage dependency here.
    $SUDO dnf install -y \
        qt6-qtbase \
        qt6-qtdeclarative \
        qt6-qtsvg \
        qt6-qtwayland \
        layer-shell-qt6 \
        libarchive

    verify_qt_version "Fedora" \
        "qt6-qtbase qt6-qtdeclarative qt6-qtsvg qt6-qtwayland layer-shell-qt6 libarchive"
    verify_runtime_libs "Fedora" "layer-shell-qt6" "libarchive"
}

install_suse() {
    # layer-shell-qt6, not layer-shell-qt: that name is the Qt 5 build on
    # openSUSE, and it does not ship libLayerShellQtInterface.so.6.
    $SUDO zypper install -y \
        qt6-base \
        qt6-declarative \
        qt6-svg \
        qt6-wayland \
        layer-shell-qt6 \
        libarchive

    verify_qt_version "openSUSE" \
        "qt6-base qt6-declarative qt6-svg qt6-wayland layer-shell-qt6 libarchive"
    verify_runtime_libs "openSUSE" "layer-shell-qt6" "libarchive"
}

# No package manager this script knows how to drive. Guessing package names for
# an unknown distro is worse than saying nothing: a wrong name installs an
# unrelated package or fails late, after the user has already waited.
report_unknown_distro() {
    local pretty="$1"
    echo "" >&2
    echo "$PROG: don't recognise this distribution ($pretty)." >&2
    echo "       Looked for pacman, dnf and zypper and found none, so no package" >&2
    echo "       names are being guessed." >&2
    echo "" >&2
    echo "  Install these yourself, from your own package manager or from source:" >&2
    echo "" >&2
    echo "    * Qt $QT_MIN or newer — Core, Gui, Widgets, Svg, Wayland. Check what" >&2
    echo "      you have with:  qmake6 -query QT_VERSION" >&2
    echo "    * layer-shell-qt, the Qt 6 build. It must ship" >&2
    echo "      libLayerShellQtInterface.so.6 — the Qt 5 build installs" >&2
    echo "      libLayerShellQtInterface.so.5 and will not work here." >&2
    echo "    * libarchive plus the bsdtar command. (Debian/Ubuntu: bsdtar is in" >&2
    echo "      libarchive-tools; 'libarchive' there is only the shared library.)" >&2
    echo "" >&2
    echo "  Then verify with:" >&2
    echo "      ldconfig -p | grep -E 'libQt6Svg.so|libLayerShellQtInterface.so'" >&2
    echo "" >&2
    echo "  Easiest route: the prebuilt tarball from the README bundles its own Qt" >&2
    echo "  and needs none of the above." >&2
    exit 1
}

PRETTY_NAME="$(os_release_field PRETTY_NAME)"
[ -n "$PRETTY_NAME" ] || PRETTY_NAME="no /etc/os-release"

if command -v pacman >/dev/null 2>&1; then
    install_arch
elif command -v dnf >/dev/null 2>&1; then
    # No release-number check here on purpose. Fedora's Qt moves with its
    # releases, and any "Fedora N has Qt X" line is a guess that expires.
    # Install, then measure what landed.
    install_fedora
elif command -v zypper >/dev/null 2>&1; then
    # Tumbleweed and Leap use the same package names, so install both the same
    # way and let verify_qt_version report the truth. The failure message picks
    # the right KDE:Qt6 repository for whichever flavour this actually is.
    install_suse
else
    report_unknown_distro "$PRETTY_NAME"
fi
