#!/bin/sh
# Tier 3 on-device checks. Runs ON the device: make device-test HOST=<ip>
pass=0 fail=0
ok()  { echo "  ok   $*"; pass=$((pass+1)); }
bad() { echo "  FAIL $*"; fail=$((fail+1)); }
check() { d="$1"; shift; if "$@" >/dev/null 2>&1; then ok "$d"; else bad "$d"; fi; }

echo "tier3-plumbing"
check "blackhand-storage running"   pidof blackhand-storage
check "storage socket exists"       [ -S /run/bh-storage.sock ]
check "/data not group/other readable" sh -c '[ -z "$(find /data -perm -o=r 2>/dev/null)" ]'

old=$(pidof blackhand-storage)
kill "$old" 2>/dev/null
n=0; new=""
while [ "$n" -lt 10 ]; do sleep 1; new=$(pidof blackhand-storage); [ -n "$new" ] && [ "$new" != "$old" ] && break; n=$((n+1)); done
check "storage restarted after kill" [ -n "$new" -a "$new" != "$old" ]

echo "tier3-plumbing: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
