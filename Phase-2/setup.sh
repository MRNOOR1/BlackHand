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

# ── Download cache ───────────────────────────────────────────────────────────
# Bind-mounted so tarballs survive the container and are visible from macOS.
# Buildroot re-fetches every source tarball otherwise — the kernel headers
# alone are 134 MB, and cdn.kernel.org throttles hard, so a cold cache costs
# ~25 minutes before a single line is compiled. Downloads are write-once and
# I/O-light, which is exactly the workload a macOS bind mount handles fine.
#
# Mounted at /work/dl and pointed at with BR2_DL_DIR rather than mounted at
# /work/buildroot/dl: on a first run the build volume is empty and the
# entrypoint git-clones into it, and git refuses to clone into a directory
# that already contains anything — a nested dl/ mount would make it non-empty
# and break the clone.
DL_DIR="${ROOT_DIR}/dl"
mkdir -p "${DL_DIR}"

BLACKHAND_EXTERNAL_DIR="${ROOT_DIR}/blackhand-external"

if [[ ! -d "${BLACKHAND_EXTERNAL_DIR}" ]]; then
  echo "Missing ${BLACKHAND_EXTERNAL_DIR}. Create the br2-external tree first." >&2
  exit 1
fi

# ── Build tree ───────────────────────────────────────────────────────────────
# A NAMED VOLUME, deliberately not a bind mount. Buildroot's output/ is
# millions of small files; on Docker Desktop for macOS a bind mount would make
# the build several times slower. A named volume lives on the VM's own ext4
# and runs at native speed, while still surviving --rm.
#
# Wipe it with:  docker volume rm blackhand-buildroot-tree
BUILD_VOLUME="blackhand-buildroot-tree"

docker run --rm -it \
  --name "${CONTAINER_NAME}" \
  -v "${CUSTOM_DIR}:/artifacts" \
  -v "${BLACKHAND_EXTERNAL_DIR}:/work/blackhand-external" \
  -v "${BUILD_VOLUME}:/work/buildroot" \
  -v "${DL_DIR}:/work/dl" \
  -e BR2_DL_DIR=/work/dl \
  "${IMAGE_NAME}"
