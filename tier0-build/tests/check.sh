#!/bin/sh
# Tier 0 checks: repo structure and script syntax. Run from the repo root.
fail=0
err() { echo "FAIL: $*"; fail=1; }

for d in tier*-*/; do
    d=${d%/}
    [ -f "$d/README.md" ] || err "$d has no README.md"
    [ -d "$d/tests" ]     || err "$d has no tests/"
done

# Each tier number must appear exactly once.
dups=$(ls -d tier*-* | sed 's/^tier\([0-9]*\)-.*/\1/' | sort | uniq -d)
[ -z "$dups" ] || err "duplicate tier numbers: $dups"

# A Buildroot option set in two tiers means two owners; the later one wins
# silently in the merged defconfig.
dups=$(cat tier*/buildroot.fragment | grep -o '^BR2_[A-Za-z0-9_]*' | sort | uniq -d)
[ -z "$dups" ] || err "Buildroot options set by more than one tier: $dups"
dups=$(cat tier*/linux.fragment | grep -o '^CONFIG_[A-Z0-9_]*' | sort | uniq -d)
[ -z "$dups" ] || err "kernel options set by more than one tier: $dups"

# Start-up scripts must not share a number.
dups=$(ls tier*/overlay/etc/init.d/ | grep '^S[0-9]' | cut -c1-3 | sort | uniq -d)
[ -z "$dups" ] || err "start-up scripts share a number: $dups"

for f in tier*/overlay/etc/init.d/* tier*/overlay/usr/bin/* tier*/tests/*.sh; do
    [ -f "$f" ] || continue
    sh -n "$f" || err "shell syntax: $f"
done
for f in tier*/*.sh; do
    bash -n "$f" || err "bash syntax: $f"
done

[ "$fail" -eq 0 ] && echo "PASS: tier0 structure and script syntax"
exit "$fail"
