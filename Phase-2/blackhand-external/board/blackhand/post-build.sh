#!/usr/bin/env bash
set -euo pipefail

TARGET_DIR="$1"

echo ">>> Black Hand OS post-build script"

# Create mount points
mkdir -p "${TARGET_DIR}/boot"
mkdir -p "${TARGET_DIR}/mnt/data"

# Build stamp for SSH verification
mkdir -p "${TARGET_DIR}/etc/blackhand"
BUILD_TIME_UTC="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
BUILD_COMMIT="unknown"
if command -v git >/dev/null 2>&1; then
  BUILD_COMMIT="$(git -C "${BR2_EXTERNAL_BLACKHAND_PATH}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
fi
printf "build_time_utc=%s\nbuild_commit=%s\n" "${BUILD_TIME_UTC}" "${BUILD_COMMIT}" \
  > "${TARGET_DIR}/etc/blackhand/build-id"

echo ">>> Post-build complete"
