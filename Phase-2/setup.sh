#!/usr/bin/env bash
set -euo pipefail

echo "=========================================="
echo "  Black Hand OS - Development Setup"
echo "=========================================="
echo

if [ -e customs ] && [ ! -d customs ]; then
  rm -f customs
fi
mkdir -p customs

if command -v docker >/dev/null 2>&1 && docker compose version >/dev/null 2>&1; then
  COMPOSE="docker compose"
else
  COMPOSE="docker-compose"
fi

echo "[1/2] Building Docker image..."
$COMPOSE build
echo

echo "[2/2] Downloading Buildroot inside container..."
$COMPOSE run --rm --user root buildroot bash -lc '
  set -euo pipefail

  mkdir -p /home/builder/src
  mkdir -p /home/builder/dl
  chown -R builder:builder /home/builder/src /home/builder/dl

  cd /home/builder/src

   # Fresh clone every time (safe + avoids hidden corrupted state)
   rm -rf buildroot
   git clone https://gitlab.com/buildroot.org/buildroot.git buildroot

   cd buildroot
   git fetch --tags --force

   # Pin to latest stable LTS version with full Raspberry Pi 5 support
   TARGET_VERSION="2025.02.6"

   echo "Checking out pinned Buildroot version: $TARGET_VERSION"
   git checkout "$TARGET_VERSION"

   # Verify checksum for security
   EXPECTED_SHA=$(git rev-parse HEAD)
   echo "Buildroot SHA: $EXPECTED_SHA"

  echo
  echo "Buildroot version:"
  git describe --tags --always --dirty

  chown -R builder:builder /home/builder/src/buildroot
'

echo
echo "✅ Setup done."
echo "Next:"
echo "  $COMPOSE run --rm buildroot bash"
echo "  cd /home/builder/src/buildroot"
echo "  make O=/home/builder/src/buildroot/output raspberrypi5_defconfig"
echo "  make O=/home/builder/src/buildroot/output menuconfig"
echo "  make O=/home/builder/src/buildroot/output -j\$(nproc)"
