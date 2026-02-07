#!/usr/bin/env bash
set -euo pipefail

BOARD_DIR="$(dirname "$0")"
BINARIES_DIR="${1}"

echo ">>> Black Hand OS post-image script"

# Copy config.txt
if [ -f "${BOARD_DIR}/rpi-firmware/config.txt" ]; then
    cp "${BOARD_DIR}/rpi-firmware/config.txt" "${BINARIES_DIR}/config.txt"
    echo ">>> Copied config.txt"
fi

# Copy cmdline.txt
if [ -f "${BOARD_DIR}/rpi-firmware/cmdline.txt" ]; then
    cp "${BOARD_DIR}/rpi-firmware/cmdline.txt" "${BINARIES_DIR}/cmdline.txt"
    echo ">>> Copied cmdline.txt"
fi

# Copy firmware files (.elf, .dat)
if [ -d "${BINARIES_DIR}/rpi-firmware" ]; then
    cp "${BINARIES_DIR}/rpi-firmware/"*.elf "${BINARIES_DIR}/" 2>/dev/null || true
    cp "${BINARIES_DIR}/rpi-firmware/"*.dat "${BINARIES_DIR}/" 2>/dev/null || true
    echo ">>> Copied firmware files"
fi

# ============================================================
# CRITICAL FIX: Copy ALL overlays from Buildroot's rpi-firmware
# ============================================================
if [ -d "${BINARIES_DIR}/rpi-firmware/overlays" ]; then
    mkdir -p "${BINARIES_DIR}/overlays"
    cp -r "${BINARIES_DIR}/rpi-firmware/overlays/"* "${BINARIES_DIR}/overlays/"
    echo ">>> Copied $(ls -1 ${BINARIES_DIR}/overlays/*.dtbo 2>/dev/null | wc -l) overlays from rpi-firmware"
else
    echo ">>> WARNING: rpi-firmware/overlays not found!"
fi

# Copy custom overlays ON TOP (your additions override defaults if same name)
if [ -d "${BOARD_DIR}/rpi-firmware/overlays" ]; then
    cp "${BOARD_DIR}/rpi-firmware/overlays/"*.dtbo "${BINARIES_DIR}/overlays/" 2>/dev/null || true
    echo ">>> Copied custom overlays"
fi

# Generate the SD card image
support/scripts/genimage.sh -c "${BOARD_DIR}/genimage.cfg"

echo ">>> Done: ${BINARIES_DIR}/sdcard.img"