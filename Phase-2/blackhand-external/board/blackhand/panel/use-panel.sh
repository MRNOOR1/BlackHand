#!/bin/sh
# Select the panel init variant and (re)generate the firmware blob.
#
#   use-panel.sh st7789p     ST7789P sequence (DEFAULT) -> panel.txt
#   use-panel.sh nv3030b     NV3030B fallback           -> panel-nv3030b.txt
#
# Both variants drive the SAME 240x280, no-offset geometry (that geometry is
# MEASURED and lives in rpi-firmware/config.txt), so only the init blob changes.
# After switching, rebuild the kernel so the built-in panel.bin is relinked
# (CONFIG_EXTRA_FIRMWARE="panel.bin").
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/../overlay/lib/firmware/panel.bin"

case "${1:-st7789p}" in
    st7789p|default) SRC="$HERE/panel.txt" ;;
    nv3030b|fallback) SRC="$HERE/panel-nv3030b.txt" ;;
    *)
        echo "usage: $0 st7789p|nv3030b" >&2
        exit 2
        ;;
esac

python3 "$HERE/mkpanelbin.py" "$SRC" "$OUT"
echo "use-panel: installed $(basename "$SRC") -> $OUT"
echo "use-panel: rebuild the kernel to relink the built-in panel.bin"
