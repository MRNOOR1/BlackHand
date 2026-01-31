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

# Verify overlays are present in boot.vfat
MTOOLS_DIR="${BINARIES_DIR}/../host/bin"
if [ -x "${MTOOLS_DIR}/mdir" ]; then
    if "${MTOOLS_DIR}/mdir" -i "${BINARIES_DIR}/boot.vfat" ::/overlays >/dev/null 2>&1; then
        echo ">>> Verified overlays in boot.vfat"
        "${MTOOLS_DIR}/mdir" -i "${BINARIES_DIR}/boot.vfat" ::/overlays
    else
        echo ">>> Warning: overlays not visible in boot.vfat"
    fi
fi

echo ">>> Done: ${BINARIES_DIR}/sdcard.img"
