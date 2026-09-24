#!/usr/bin/env bash
# install.sh — install monster-overlay + monster-control + monster-doctor into
# ~/.local/bin, the data/*.map files into
# ${XDG_DATA_HOME:-~/.local/share}/monster-overlay/data/, and the bundled
# REFramework payload next to the binaries.
#
# The REFramework archive MUST land in ${BIN_DIR}/third_party/REFramework/:
# the installer resolves it relative to the running binary and has no search
# path, so installing only the binaries would leave Rise damage tracking
# without its producer. The .map files DO honour XDG_DATA_HOME, so those still
# go to the shared data dir (it used to hardcode ~/.local/share, which broke
# custom-XDG setups).
#
# Idempotent: re-running overwrites existing files.
set -euo pipefail

BIN_DIR="${HOME}/.local/bin"
DATA_ROOT="${XDG_DATA_HOME:-${HOME}/.local/share}"
DATA_DIR="${DATA_ROOT}/monster-overlay/data"
PAYLOAD_DIR="${BIN_DIR}/third_party/REFramework"

mkdir -p "${BIN_DIR}" "${DATA_DIR}" "${PAYLOAD_DIR}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

install -m 755 "${SCRIPT_DIR}/monster-overlay"  "${BIN_DIR}/monster-overlay"
install -m 755 "${SCRIPT_DIR}/monster-control"  "${BIN_DIR}/monster-control"
install -m 755 "${SCRIPT_DIR}/monster-doctor"   "${BIN_DIR}/monster-doctor"
install -m 644 "${SCRIPT_DIR}/data"/*.map        "${DATA_DIR}/"

if [ -d "${SCRIPT_DIR}/third_party/REFramework" ]; then
    install -m 644 "${SCRIPT_DIR}"/third_party/REFramework/* \
        "${PAYLOAD_DIR}/"
fi

echo "Installed:"
echo "  ${BIN_DIR}/monster-overlay"
echo "  ${BIN_DIR}/monster-control"
echo "  ${BIN_DIR}/monster-doctor"
echo "  ${BIN_DIR}/third_party/REFramework/       (Rise damage producer)"
echo "  ${DATA_DIR}/MonsterHunterWorld.421810.map"
echo "  ${DATA_DIR}/MonsterHunterRise.16.0.2.0.map"
echo
echo "Run:"
echo "  ./install-deps.sh   # one-time: install Qt 6 + layer-shell-qt via pacman"
echo "  monster-control     # launch the control console"
echo "  monster-doctor      # overlay shows nothing? writes a report to attach to your issue"
