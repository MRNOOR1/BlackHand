#!/usr/bin/env bash
set -euo pipefail

TARGET_DIR="$1"

echo ">>> Black Hand OS post-build script"

# Create mount points
mkdir -p "${TARGET_DIR}/boot"
mkdir -p "${TARGET_DIR}/mnt/data"

echo ">>> Post-build complete"
