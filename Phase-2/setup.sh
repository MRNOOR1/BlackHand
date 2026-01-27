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

docker run --rm -it \
  --name "${CONTAINER_NAME}" \
  -v "${CUSTOM_DIR}:/artifacts" \
  -v "${BLACKHAND_EXTERNAL_DIR}:/work/blackhand-external" \
  "${IMAGE_NAME}"
