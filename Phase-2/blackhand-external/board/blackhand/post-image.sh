#!/usr/bin/env bash
set -euo pipefail

# Post-image hook.
# This runs after the image is created.
# Use it to:
# - copy artifacts to a host-mounted directory
# - add checksum files
# Example:
# IMAGE_DIR="$1"
# sha256sum "${IMAGE_DIR}/sdcard.img" > "${IMAGE_DIR}/sdcard.img.sha256"
