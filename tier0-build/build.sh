#!/usr/bin/env bash
# Build an image containing tiers 0..TIER. Runs inside the build container
# (see the root Makefile); never on the host.
#
#   build.sh TIER
#
# Each tier may provide any of these; build.sh stacks them in tier order:
#   buildroot.fragment   Buildroot options  -> appended to the defconfig
#   linux.fragment       kernel options     -> BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES
#   overlay/             rootfs files       -> BR2_ROOTFS_OVERLAY
#   post-build.sh        rootfs fix-ups     -> BR2_ROOTFS_POST_BUILD_SCRIPT
#   services.conf        supervised daemons -> /etc/blackhand/services.conf
#   config.txt           boot config lines  -> /boot/config.txt
set -euo pipefail

TIER="${1:?usage: build.sh TIER}"
case "$TIER" in [0-9]) ;; *) echo "build.sh: TIER must be 0-9" >&2; exit 2 ;; esac

BUILDROOT_TAG=2024.02.13
REPO=/work/blackhand
CACHE=/work/cache
BR="$CACHE/buildroot-$BUILDROOT_TAG"
OUT="$CACHE/output-tier$TIER"
GEN="$OUT/blackhand"

if [ ! -d "$BR" ]; then
    git clone --depth 1 --branch "$BUILDROOT_TAG" \
        https://github.com/buildroot/buildroot.git "$BR"
fi

TIERS=()
for t in $(seq 0 "$TIER"); do
    d=$(echo "$REPO"/tier"$t"-*)
    [ -d "$d" ] || { echo "build.sh: no folder for tier $t" >&2; exit 1; }
    TIERS+=("$d")
done

rm -rf "$GEN"
mkdir -p "$GEN/overlay/etc/blackhand"

overlays="" posts="" kfrags=""
: > "$GEN/defconfig"
: > "$GEN/overlay/etc/blackhand/services.conf"
: > "$GEN/config.txt"
for d in "${TIERS[@]}"; do
    [ -f "$d/buildroot.fragment" ] && cat "$d/buildroot.fragment" >> "$GEN/defconfig"
    [ -f "$d/linux.fragment" ]     && kfrags="$kfrags $d/linux.fragment"
    [ -d "$d/overlay" ]            && overlays="$overlays $d/overlay"
    [ -f "$d/post-build.sh" ]      && posts="$posts $d/post-build.sh"
    [ -f "$d/services.conf" ]      && cat "$d/services.conf" >> "$GEN/overlay/etc/blackhand/services.conf"
    [ -f "$d/config.txt" ]         && cat "$d/config.txt" >> "$GEN/config.txt"
done

commit=$(git -c safe.directory='*' -C "$REPO" describe --always --dirty 2>/dev/null || echo unknown)
printf 'tier=%s\ncommit=%s\nbuilt=%s\n' "$TIER" "$commit" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    > "$GEN/overlay/etc/blackhand/build-id"

# Tier 1 makes the bootable image; below it there is only a root filesystem.
post_image=""
[ "$TIER" -ge 1 ] && post_image="$REPO/tier1-boot/post-image.sh"

cat >> "$GEN/defconfig" <<EOF
BR2_DL_DIR="$CACHE/dl"
BR2_CCACHE=y
BR2_CCACHE_DIR="$CACHE/ccache"
BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES="${kfrags# }"
BR2_ROOTFS_OVERLAY="${overlays# } $GEN/overlay"
BR2_ROOTFS_POST_BUILD_SCRIPT="${posts# }"
BR2_ROOTFS_POST_IMAGE_SCRIPT="$post_image"
BR2_ROOTFS_POST_SCRIPT_ARGS="$GEN"
EOF

make -C "$BR" O="$OUT" BR2_EXTERNAL="$REPO" BR2_DEFCONFIG="$GEN/defconfig" defconfig

# Kconfig silently drops options it does not know or cannot satisfy. Fail
# instead, so a fragment never claims something the image does not have.
missing=0
while IFS= read -r line; do
    case "$line" in BR2_*=*) ;; *) continue ;; esac
    grep -qxF "$line" "$OUT/.config" || { echo "build.sh: dropped by Kconfig: $line" >&2; missing=1; }
done < <(grep -h '^BR2_' "${TIERS[@]/%//buildroot.fragment}" 2>/dev/null)
[ "$missing" -eq 0 ] || exit 1

make -C "$BR" O="$OUT"

mkdir -p "$REPO/output"
if [ "$TIER" -ge 1 ]; then
    cp "$OUT/images/sdcard.img" "$REPO/output/tier$TIER-sdcard.img"
    echo ">>> output/tier$TIER-sdcard.img"
else
    cp "$OUT/images/rootfs.ext4" "$REPO/output/tier0-rootfs.ext4"
    echo ">>> output/tier0-rootfs.ext4"
fi
