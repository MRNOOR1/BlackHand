#!/usr/bin/env bash
set -euo pipefail

BOARD_DIR="$(dirname "$0")"
BINARIES_DIR="${1}"

echo ">>> Black Hand OS post-image script"

# Copy config.txt from your rpi-firmware folder
if [ -f "${BOARD_DIR}/rpi-firmware/config.txt" ]; then
    cp "${BOARD_DIR}/rpi-firmware/config.txt" "${BINARIES_DIR}/config.txt"
    echo ">>> Copied config.txt"
fi

# Copy cmdline.txt
if [ -f "${BOARD_DIR}/rpi-firmware/cmdline.txt" ]; then
    cp "${BOARD_DIR}/rpi-firmware/cmdline.txt" "${BINARIES_DIR}/cmdline.txt"
    echo ">>> Copied cmdline.txt"
fi

# Copy custom overlays if present
if [ -d "${BOARD_DIR}/rpi-firmware/overlays" ]; then
    mkdir -p "${BINARIES_DIR}/overlays"
    cp "${BOARD_DIR}/rpi-firmware/overlays/"*.dtbo "${BINARIES_DIR}/overlays/" 2>/dev/null || true
    echo ">>> Copied custom overlays"
fi

# Generate the SD card image
support/scripts/genimage.sh -c "${BOARD_DIR}/genimage.cfg"

echo ">>> Done: ${BINARIES_DIR}/sdcard.img"