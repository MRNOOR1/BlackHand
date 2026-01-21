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

  # Custom kernel config for phone features (enable audio drivers)
  make raspberrypi5_defconfig
  # Enable HyperPixel display and USB audio drivers
  sed -i 's/# CONFIG_DRM_PANEL_RASPBERRYPI_TOUCHSCREEN is not set/CONFIG_DRM_PANEL_RASPBERRYPI_TOUCHSCREEN=y/' .config
  sed -i 's/# CONFIG_SND_USB_AUDIO is not set/CONFIG_SND_USB_AUDIO=y/' .config
  sed -i 's/# CONFIG_SND_SOC_RPI_SIMPLE_SOUND is not set/CONFIG_SND_SOC_RPI_SIMPLE_SOUND=y/' .config

  # Incremental build: check if output exists
  if [ ! -d output ]; then
    make -j\$(nproc)
  else
    echo 'Output exists, attempting incremental build...'
    make -j\$(nproc) || (echo 'Incremental build failed, cleaning and rebuilding...'; make clean; make -j\$(nproc))
  fi

  # Retry copy on failure
  for i in {1..3}; do
    if cp output/images/sdcard.img /home/builder/custom/; then
      echo '✅ Image copied to custom/sdcard.img'
      break
    else
      echo \"Copy attempt \$i failed, retrying...\"
      sleep 2
    fi
  done
"
