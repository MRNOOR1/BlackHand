#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="blackhand-buildroot"
CONTAINER_NAME="blackhand-buildroot"

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required but not installed." >&2
  exit 1
fi

docker build -t "${IMAGE_NAME}" "${ROOT_DIR}/docker"

CUSTOM_DIR="${ROOT_DIR}/custom"
mkdir -p "${CUSTOM_DIR}"

BLACKHAND_EXTERNAL_DIR="${ROOT_DIR}/blackhand-external"

if [[ ! -d "${BLACKHAND_EXTERNAL_DIR}" ]]; then
  echo "Missing ${BLACKHAND_EXTERNAL_DIR}. Create the br2-external tree first." >&2
  exit 1
fi

: "${BLACKHAND_WIFI_SSID:=}"
: "${BLACKHAND_WIFI_PSK:=}"
WIFI_ENV="${BLACKHAND_EXTERNAL_DIR}/board/blackhand/wifi.env"
if [ ! -f "${WIFI_ENV}" ] && { [ -z "${BLACKHAND_WIFI_SSID}" ] || [ -z "${BLACKHAND_WIFI_PSK}" ]; }; then
  echo "WARNING: BLACKHAND_WIFI_SSID / BLACKHAND_WIFI_PSK are not set."
  echo "         Wi-Fi is the ONLY network path on the CM4 carrier (no ethernet,"
  echo "         and USB-C data goes to the modem). Without these the image ships"
  echo "         with no network and the P1 serial console is the only way in."
  echo "         Create ${WIFI_ENV} (gitignored, used by every build):"
  echo "           BLACKHAND_WIFI_SSID=MyNetwork"
  echo "           BLACKHAND_WIFI_PSK=my passphrase"
  echo "         or set them for this build only:"
  echo "           export BLACKHAND_WIFI_SSID='MyNetwork'"
  echo "           export BLACKHAND_WIFI_PSK='my passphrase'"
fi

# -e forwards the host value into the container. post-build.sh reads these from
# the environment (never argv, so they stay out of ps) and generates
# /etc/wpa_supplicant.conf at mode 0600 with a PBKDF2-hashed PSK. Without the
# -e flags the build still succeeds and silently produces an unreachable board.
docker run --rm -it \
  --name "${CONTAINER_NAME}" \
  -e BLACKHAND_WIFI_SSID \
  -e BLACKHAND_WIFI_PSK \
  -v "${CUSTOM_DIR}:/artifacts" \
  -v "${BLACKHAND_EXTERNAL_DIR}:/work/blackhand-external" \
  "${IMAGE_NAME}"
