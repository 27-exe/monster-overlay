#!/usr/bin/env bash
# install-deps.sh — install Monster Overlay runtime dependencies on Arch Linux.
# Tested on Arch as of 2026-08.
#
# Idempotent: re-running is a no-op if every package is already installed.
set -euo pipefail

if command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --needed --noconfirm \
        qt6-base \
        qt6-declarative \
        qt6-wayland \
        layer-shell-qt
    echo "OK: Qt 6 + layer-shell-qt installed."
else
    echo "This installer only knows Arch. On other distros, install:"
    echo "  - Qt 6.11+ (qtbase, qtdeclarative, qtwayland)"
    echo "  - layer-shell-qt (KDE's Qt wrapper around zwlr_layer_shell_v1)"
fi
