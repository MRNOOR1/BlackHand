#!/usr/bin/env bash
set -euo pipefail

# Post-build hook.
# This runs after the target rootfs is staged but before the image is created.
# Use it to:
# - add or tweak files in the target directory
# - fix permissions
# - drop build metadata into /etc
# Example:
# TARGET_DIR="$1"
# echo "Black Hand OS" > "${TARGET_DIR}/etc/issue"
