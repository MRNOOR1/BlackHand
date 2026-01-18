#!/usr/bin/env bash
set -euo pipefail

echo "=========================================="
echo "  Black Hand OS - Development Setup"
echo "=========================================="
echo

mkdir -p custom

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

  LATEST_STABLE=$(git tag \
    | grep -E "^[0-9]{4}\.[0-9]{2}(\.[0-9]+)?$" \
    | sort -V | tail -n 1)

  echo "Checking out latest stable Buildroot: $LATEST_STABLE"
  git checkout "$LATEST_STABLE"

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
