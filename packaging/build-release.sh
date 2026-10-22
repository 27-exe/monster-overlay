#!/usr/bin/env bash
# build-release.sh — assemble the release tarball from a built tree.
#
# The v0.10.10 package was assembled by hand; this script makes the same
# package reproducible, so a release is one command and one diff.
#
#   ./packaging/build-release.sh                 # version from CMakeLists.txt
#   BUILD_DIR=/path/to/build OUT_DIR=/tmp ./packaging/build-release.sh
#
# The REFramework payload is THIRD-PARTY and deliberately not in git: point
# REFRAMEWORK_PAYLOAD_DIR at a directory holding MHRISE-*.zip + LICENSE +
# README.txt (the previous release's package carries one).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD_DIR:-$REPO/build}"
OUT_DIR="${OUT_DIR:-$HOME/Downloads}"
PAYLOAD="${REFRAMEWORK_PAYLOAD_DIR:-$HOME/Downloads/monster-overlay-v0.10.10-linux-x86_64/third_party/REFramework}"

VERSION="$(sed -n 's/^project(monster_overlay VERSION \([0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt")"
if [ -z "$VERSION" ]; then
    echo "build-release.sh: cannot read the version from CMakeLists.txt" >&2
    exit 1
fi
PKG="monster-overlay-v${VERSION}-linux-x86_64"
STAGE="${OUT_DIR}/${PKG}"

echo "== monster-overlay ${VERSION} =="
for exe in monster-overlay monster-control monster-doctor; do
    if [ ! -x "${BUILD}/${exe}" ]; then
        echo "missing ${BUILD}/${exe} — build first (ninja -C ${BUILD})" >&2
        exit 1
    fi
    got="$("${BUILD}/${exe}" --version 2>/dev/null | head -1 | awk '{print $NF}')"
    if [ "$got" != "$VERSION" ]; then
        echo "${exe} reports version '${got}', expected '${VERSION}'" >&2
        exit 1
    fi
done

rm -rf "$STAGE"
mkdir -p "$STAGE"/{data,reframework/autorun,third_party/REFramework}

install -m 755 "${BUILD}/monster-overlay" "${BUILD}/monster-control" \
                "${BUILD}/monster-doctor" "$STAGE/"
install -m 644 "${REPO}"/data/*.map "$STAGE/data/"
install -m 644 "${REPO}"/reframework/*.lua "$STAGE/reframework/autorun/"
install -m 644 "${REPO}/LICENSE" "${REPO}/NOTICE" "$STAGE/"
install -m 755 "${REPO}/packaging/install-deps.sh" "${REPO}/packaging/install.sh" "$STAGE/"
install -m 644 "${REPO}/packaging/README.md" "${REPO}/packaging/README.zh-CN.md" "$STAGE/"

if [ ! -d "$PAYLOAD" ]; then
    echo "no REFramework payload at ${PAYLOAD} — set REFRAMEWORK_PAYLOAD_DIR" >&2
    exit 1
fi
install -m 644 "${PAYLOAD}"/* "$STAGE/third_party/REFramework/"

# SHA256SUMS: every file in the package, sorted, "./"-prefixed — the same
# shape the v0.10.10 package shipped, so `sha256sum -c SHA256SUMS` works
# from inside the extracted directory.
( cd "$STAGE" && find . -type f ! -name SHA256SUMS | sort \
    | xargs sha256sum > SHA256SUMS )

tar -czf "${OUT_DIR}/${PKG}.tar.gz" -C "$OUT_DIR" "$PKG"

echo "package: ${OUT_DIR}/${PKG}.tar.gz"
echo "size:    $(du -h "${OUT_DIR}/${PKG}.tar.gz" | cut -f1)"
echo "sha256:  $(sha256sum "${OUT_DIR}/${PKG}.tar.gz" | cut -d' ' -f1)"
echo
echo "verify with:"
echo "  tar -xzf ${OUT_DIR}/${PKG}.tar.gz && cd ${PKG} && sha256sum -c SHA256SUMS"
