#!/bin/sh
# Tier 1 on-device checks. Runs ON the device: make device-test HOST=<ip>
pass=0 fail=0
ok()  { echo "  ok   $*"; pass=$((pass+1)); }
bad() { echo "  FAIL $*"; fail=$((fail+1)); }
check() { d="$1"; shift; if "$@" >/dev/null 2>&1; then ok "$d"; else bad "$d"; fi; }

echo "tier1-boot"
check "PID 1 is blackhand-init"        [ "$(cat /proc/1/comm)" = blackhand-init ]
check "service manager running"        pidof blackhand-svc-mgr
check "root filesystem is read-write"  sh -c 'grep " / " /proc/mounts | grep -q " rw"'
check "/boot mounted (vfat)"           sh -c 'grep -q " /boot vfat " /proc/mounts'
check "/data exists"                   [ -d /data ]
check "boot log reached ready"         grep -q "Black Hand OS ready" /var/log/blackhand/last-boot.log
check "build stamp present"            grep -q "^tier=" /etc/blackhand/build-id
check "wlan0 has an IPv4 address"      sh -c 'ifconfig wlan0 | grep -q "inet "'
check "dropbear running"               pidof dropbear
check "dropbear has password auth off" sh -c 'tr "\0" " " < /proc/$(pidof dropbear | cut -d" " -f1)/cmdline | grep -q -- " -s"'
check "host key generated on device"   [ -s /etc/dropbear/dropbear_ed25519_host_key ]
check "tty2 debug shell compiled out"  sh -c '! grep -q /dev/tty2 /sbin/blackhand-init'

# PID 1 must restart the service manager if it dies.
old=$(pidof blackhand-svc-mgr)
kill "$old" 2>/dev/null
n=0; new=""
while [ "$n" -lt 10 ]; do sleep 1; new=$(pidof blackhand-svc-mgr); [ -n "$new" ] && [ "$new" != "$old" ] && break; n=$((n+1)); done
check "service manager respawned after kill" [ -n "$new" -a "$new" != "$old" ]

echo "tier1-boot: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
