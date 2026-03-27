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

# Build stamp for SSH verification
mkdir -p "${TARGET_DIR}/etc/blackhand"
BUILD_TIME_UTC="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
BUILD_COMMIT="unknown"
if command -v git >/dev/null 2>&1; then
  BUILD_COMMIT="$(git -C "${BR2_EXTERNAL_BLACKHAND_PATH}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
fi
printf "build_time_utc=%s\nbuild_commit=%s\n" "${BUILD_TIME_UTC}" "${BUILD_COMMIT}" \
  > "${TARGET_DIR}/etc/blackhand/build-id"

# Compile and install custom device tree overlay for HyperPixel touch
OVERLAY_SRC="${BR2_EXTERNAL_BLACKHAND_PATH}/board/blackhand/dt-overlays/hyperpixel4-touch-overlay.dts"
OVERLAY_DIR="${TARGET_DIR}/boot/overlays"
if [ -f "${OVERLAY_SRC}" ]; then
    mkdir -p "${OVERLAY_DIR}"
    DTC="${HOST_DIR}/bin/dtc"
    if [ ! -x "${DTC}" ]; then
        DTC="$(command -v dtc 2>/dev/null || true)"
    fi
    if [ -n "${DTC}" ] && [ -x "${DTC}" ]; then
        echo ">>> Compiling hyperpixel4-touch overlay"
        "${DTC}" -@ -I dts -O dtb \
            -o "${OVERLAY_DIR}/hyperpixel4-touch.dtbo" \
            "${OVERLAY_SRC}" 2>/dev/null || \
            echo "WARNING: dtc overlay compile failed (will retry at boot if overlay exists)"
    else
        echo "WARNING: dtc not found, cannot compile touch overlay"
    fi
fi

echo ">>> Post-build complete"
