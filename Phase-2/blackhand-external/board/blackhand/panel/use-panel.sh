#!/usr/bin/env bash
# Select which panel this image drives.
#
#   ./use-panel.sh rev2    ST7789P, 240x284, y-offset 18   (touch module / Rev2)
#   ./use-panel.sh rev1    NV3030B, 240x280, y-offset 20   (non-touch Rev1)
#
# Controller, resolution and RAM offset must change TOGETHER. Swapping only
# the init blob leaves the geometry wrong, which shows up as a lit panel with
# a shifted, wrapping image — a confusing way to fail. This script keeps the
# three in step.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BLOB="${HERE}/../overlay/lib/firmware/panel.bin"
CFG="${HERE}/../rpi-firmware/config.txt"

case "${1:-}" in
  rev2) SRC=panel.txt          ; H=284 ; YOFF=18 ; NAME="ST7789P 240x284" ;;
  rev1) SRC=panel-nv3030b.txt  ; H=280 ; YOFF=20 ; NAME="NV3030B 240x280" ;;
  *)    echo "usage: $0 rev1|rev2" >&2; exit 2 ;;
esac

python3 "${HERE}/mkpanelbin.py" "${HERE}/${SRC}" "${BLOB}"

# BSD sed (macOS) and GNU sed disagree on -i; write via a temp file instead.
tmp="$(mktemp)"
sed -e "s/height=[0-9]*/height=${H}/" \
    -e "s/y-offset=[0-9]*/y-offset=${YOFF}/" "${CFG}" > "${tmp}"
mv "${tmp}" "${CFG}"

echo "panel   : ${NAME}  (from ${SRC})"
grep -E 'width=|y-offset=' "${CFG}" | sed 's/^/config  : /'
echo
echo "The blob is linked into the kernel image (CONFIG_EXTRA_FIRMWARE), so a"
echo "rootfs-only rebuild will NOT pick this up. In the build container run:"
echo "    make linux-rebuild all"
echo "then push just the kernel — Image lives on the FAT boot partition:"
echo "    scp output/images/Image root@<pi>:/boot/Image && ssh root@<pi> 'sync; reboot'"
