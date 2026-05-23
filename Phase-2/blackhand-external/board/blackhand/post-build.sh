#!/usr/bin/env bash
set -euo pipefail

TARGET_DIR="$1"

echo ">>> Black Hand OS post-build script"

# Create mount points
mkdir -p "${TARGET_DIR}/boot"
mkdir -p "${TARGET_DIR}/mnt/data"
mkdir -p "${TARGET_DIR}/data/notes"
mkdir -p "${TARGET_DIR}/data/music"
mkdir -p "${TARGET_DIR}/data/voice-memos"
mkdir -p "${TARGET_DIR}/data/alarms"
mkdir -p "${TARGET_DIR}/data/contacts"

# Build stamp for SSH verification
mkdir -p "${TARGET_DIR}/etc/blackhand"
BUILD_TIME_UTC="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
BUILD_COMMIT="unknown"
if command -v git >/dev/null 2>&1; then
  BUILD_COMMIT="$(git -C "${BR2_EXTERNAL_BLACKHAND_PATH}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
fi
printf "build_time_utc=%s\nbuild_commit=%s\n" "${BUILD_TIME_UTC}" "${BUILD_COMMIT}" \
  > "${TARGET_DIR}/etc/blackhand/build-id"

# Compile and install custom device tree overlays
OVERLAY_DIR="${TARGET_DIR}/boot/overlays"
mkdir -p "${OVERLAY_DIR}"

DTC="${HOST_DIR}/bin/dtc"
if [ ! -x "${DTC}" ]; then
    DTC="$(command -v dtc 2>/dev/null || true)"
fi

compile_overlay() {
    local src="$1" out="$2" name="$3"
    if [ -f "${src}" ]; then
        if [ -n "${DTC}" ] && [ -x "${DTC}" ]; then
            echo ">>> Compiling ${name} overlay"
            "${DTC}" -@ -I dts -O dtb -o "${out}" "${src}" 2>/dev/null || \
                echo "WARNING: dtc failed for ${name} overlay"
        else
            echo "WARNING: dtc not found, cannot compile ${name} overlay"
        fi
    fi
}

compile_overlay \
    "${BR2_EXTERNAL_BLACKHAND_PATH}/board/blackhand/dt-overlays/hyperpixel4-touch-overlay.dts" \
    "${OVERLAY_DIR}/hyperpixel4-touch.dtbo" \
    "hyperpixel4-touch"

compile_overlay \
    "${BR2_EXTERNAL_BLACKHAND_PATH}/board/blackhand/dt-overlays/blackhand-fixes-overlay.dts" \
    "${OVERLAY_DIR}/blackhand-fixes.dtbo" \
    "blackhand-fixes"

# Generate dropbear SSH host key if it doesn't exist.
# Dropbear needs this to complete the SSH handshake. Without it the
# connection is closed immediately after the client sends its key exchange.
# We generate it here at build time so it's baked into the image and
# available on first boot — no manual setup needed on the device.
DROPBEAR_KEY_DIR="${TARGET_DIR}/etc/dropbear"
DROPBEAR_KEY="${DROPBEAR_KEY_DIR}/dropbear_ed25519_host_key"

if [ ! -f "${DROPBEAR_KEY}" ]; then
    echo ">>> Generating dropbear ed25519 host key"
    # Buildroot's dropbear package sometimes creates /etc/dropbear as a plain
    # file instead of a directory. Always remove it so mkdir -p succeeds.
    # rm -rf is safe here because we only enter this block when the key file
    # doesn't exist — there's nothing worth keeping inside.
    rm -rf "${DROPBEAR_KEY_DIR}"
    mkdir -p "${DROPBEAR_KEY_DIR}"

    # dropbearkey is a HOST tool built by Buildroot — it lives in HOST_DIR/bin.
    # The device does not have dropbearkey installed. We generate the key here
    # at build time using the host tool and bake it directly into the image.
    # This means the key is ready on first boot with no runtime generation needed.
    HOST_DROPBEARKEY="${HOST_DIR}/bin/dropbearkey"

    if [ -x "${HOST_DROPBEARKEY}" ]; then
        "${HOST_DROPBEARKEY}" -t ed25519 -f "${DROPBEAR_KEY}"
        chmod 600 "${DROPBEAR_KEY}"
        echo ">>> Dropbear host key generated successfully"
    else
        echo "WARNING: ${HOST_DROPBEARKEY} not found — SSH will not work on device"
        echo "         Run: make dropbear-rebuild to ensure host tools are built"
    fi
else
    echo ">>> Dropbear host key already exists, skipping generation"
fi

# ── BCM4345C0 Bluetooth firmware ────────────────────────────────────────────
# The Pi 4 onboard BT chip (BCM4345C0) requires this firmware file.
# CONFIG_BT_HCIUART_BCM loads it automatically at boot via request_firmware().
# Without it the chip runs without firmware: pairing is unreliable,
# A2DP is broken, and bluetoothctl hangs waiting for the adapter.
BT_FW_DIR="${TARGET_DIR}/lib/firmware/brcm"
BT_FW_FILE="${BT_FW_DIR}/BCM4345C0.hcd"

if [ ! -f "${BT_FW_FILE}" ]; then
    echo ">>> Fetching BCM4345C0 Bluetooth firmware..."
    mkdir -p "${BT_FW_DIR}"
    # Use curl --fail so a GitHub HTML error page doesn't silently replace the
    # binary. wget -q accepts any 200 response, including redirect HTML pages.
    if command -v curl >/dev/null 2>&1; then
        curl -L --fail --silent --show-error \
            -o "${BT_FW_FILE}" \
            "https://github.com/RPi-Distro/bluez-firmware/raw/master/broadcom/BCM4345C0.hcd" \
            && echo ">>> BCM4345C0.hcd installed ($(wc -c < "${BT_FW_FILE}") bytes)" \
            || { echo "WARNING: Could not fetch BT firmware — Bluetooth will be unreliable"; rm -f "${BT_FW_FILE}"; }
    else
        echo "WARNING: curl not found — place BCM4345C0.hcd in board/blackhand/overlay/lib/firmware/brcm/ manually"
    fi
else
    echo ">>> BCM4345C0.hcd already present, skipping download"
fi

# Create the device-specific symlink the kernel searches for first.
# Without it the kernel falls back to BCM4345C0.hcd (which works), but
# creating the symlink makes the lookup deterministic and matches how
# Raspberry Pi OS ships the firmware.
BT_FW_SYMLINK="${BT_FW_DIR}/BCM4345C0.raspberrypi,4-model-b.hcd"
if [ -f "${BT_FW_FILE}" ] && [ ! -e "${BT_FW_SYMLINK}" ]; then
    ln -sf BCM4345C0.hcd "${BT_FW_SYMLINK}"
    echo ">>> Created BT firmware symlink (device-specific name → BCM4345C0.hcd)"
fi

echo ">>> Post-build complete"
