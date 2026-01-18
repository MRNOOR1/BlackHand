#!/usr/bin/env bash
set -e

if command -v docker >/dev/null 2>&1 && docker compose version >/dev/null 2>&1; then
  COMPOSE="docker compose"
else
  COMPOSE="docker-compose"
fi

CMD="${1:-build}"

if [ "$CMD" = "shell" ]; then
  echo "Entering build container..."
  echo "Inside run: cd /home/builder/src/buildroot"
  $COMPOSE run --rm buildroot bash
  exit 0
fi

$COMPOSE run --rm buildroot bash -lc "
  set -e
  cd /home/builder/src/buildroot

  make raspberrypi5_defconfig
  make -j\$(nproc)

  cp output/images/sdcard.img /home/builder/custom/
  echo '✅ Image copied to custom/sdcard.img'
"
