#!/bin/sh
# Tier 2 on-device checks. Runs ON the device: make device-test HOST=<ip>
pass=0 fail=0
ok()  { echo "  ok   $*"; pass=$((pass+1)); }
bad() { echo "  FAIL $*"; fail=$((fail+1)); }
check() { d="$1"; shift; if "$@" >/dev/null 2>&1; then ok "$d"; else bad "$d"; fi; }

echo "tier2-display"
check "panel driver bound (fb0)"     [ -d /sys/class/graphics/fb0 ]
check "fb0 is 240 pixels wide"       sh -c 'cut -d, -f1 /sys/class/graphics/fb0/virtual_size | grep -qx 240'
check "tmux session bh exists"       tmux -S /run/blackhand.sock has-session -t =bh
check "tty1 client attached"         sh -c 'tmux -S /run/blackhand.sock list-clients | grep -q tty1'
echo "  info fb0 $(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null), tty1 grid $(stty -F /dev/tty1 size 2>/dev/null)"
echo "  look: run 'fbfill cycle' and confirm red, green, blue, white, black fill the WHOLE panel"

echo "tier2-display: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
