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

# Copy VideoCore firmware.
#
# Buildroot's rpi-firmware package installs start4.elf/fixup4.dat into
# ${BINARIES_DIR}/rpi-firmware/. genimage packs them into boot.vfat by name,
# so we must copy them up one directory first. Fail loudly if they are
# missing — genimage's own error ("input file not found") does not name
# the file clearly.
if [ ! -d "${BINARIES_DIR}/rpi-firmware" ]; then
    echo ">>> ERROR: ${BINARIES_DIR}/rpi-firmware not found — is BR2_PACKAGE_RPI_FIRMWARE=y?"
    exit 1
fi
cp "${BINARIES_DIR}/rpi-firmware/"*.elf "${BINARIES_DIR}/"
cp "${BINARIES_DIR}/rpi-firmware/"*.dat "${BINARIES_DIR}/"
for _need in start4.elf fixup4.dat; do
    if [ ! -f "${BINARIES_DIR}/${_need}" ]; then
        echo ">>> ERROR: ${_need} missing from ${BINARIES_DIR}/rpi-firmware/"
        echo ">>>        Check BR2_PACKAGE_RPI_FIRMWARE_VARIANT_PI4=y in the defconfig."
        exit 1
    fi
done
echo ">>> Copied firmware files (start4.elf, fixup4.dat)"

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

# ============================================================
# CRITICAL: Recompile DTB with symbols (-@) for overlay support
# ============================================================
# Buildroot passes DTC_FLAGS=-@ as an environment variable, but the
# RPi kernel Makefile overrides it internally. The result is a DTB
# without __symbols__, which means the firmware silently ignores ALL
# dtoverlay= lines in config.txt.
#
# Workaround: decompile the DTB and recompile with -@ to inject symbols.
# HOST_DIR may not be set — derive from BINARIES_DIR (output/images -> output/host)
HOST_DIR_DERIVED="$(dirname "${BINARIES_DIR}")/host"
DTC=""
for candidate in \
    "${HOST_DIR:-}/bin/linux-dtc" \
    "${HOST_DIR:-}/bin/dtc" \
    "${HOST_DIR_DERIVED}/bin/linux-dtc" \
    "${HOST_DIR_DERIVED}/bin/dtc" \
    "$(which dtc 2>/dev/null || true)"; do
    if [ -n "${candidate}" ] && [ -x "${candidate}" ]; then
        DTC="${candidate}"
        break
    fi
done
echo ">>> Using dtc: ${DTC:-NOT FOUND}"

DTB="${BINARIES_DIR}/bcm2711-rpi-4-b.dtb"
if [ -x "${DTC}" ] && [ -f "${DTB}" ]; then
    echo ">>> Recompiling DTB with overlay symbols (-@)..."
    DTB_TMP="${DTB}.tmp"
    "${DTC}" -I dtb -O dts "${DTB}" 2>/dev/null | \
        "${DTC}" -@ -I dts -O dtb -o "${DTB_TMP}" 2>/dev/null
    if [ -f "${DTB_TMP}" ]; then
        mv "${DTB_TMP}" "${DTB}"
        # Verify symbols were added
        if "${DTC}" -I dtb -O dts "${DTB}" 2>/dev/null | grep -q '__symbols__'; then
            echo ">>> DTB now has __symbols__ — overlays will work"
        else
            echo ">>> WARNING: DTB still missing __symbols__!"
        fi
    else
        echo ">>> WARNING: DTB recompile failed, using original"
    fi
else
    echo ">>> WARNING: dtc not found or DTB missing — cannot add overlay symbols"
fi

# Generate the SD card image
support/scripts/genimage.sh -c "${BOARD_DIR}/genimage.cfg"

echo ">>> Done: ${BINARIES_DIR}/sdcard.img"