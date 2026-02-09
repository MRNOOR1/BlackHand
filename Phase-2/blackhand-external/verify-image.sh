#!/usr/bin/env bash
# ============================================================================
# Black Hand OS - Pre-flash verification script
# ============================================================================
# Run this INSIDE the Docker build container after 'make' completes.
# It mounts the built rootfs and boot images and checks that everything
# needed for display, init, SSH, and boot is present and correct.
#
# Usage:  bash /work/blackhand-external/../verify-image.sh
#    or:  bash verify-image.sh   (from /work/buildroot)
# ============================================================================
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0
FAIL=0
WARN=0

pass() { echo -e "  ${GREEN}[PASS]${NC} $1"; PASS=$((PASS + 1)); }
fail() { echo -e "  ${RED}[FAIL]${NC} $1"; FAIL=$((FAIL + 1)); }
warn() { echo -e "  ${YELLOW}[WARN]${NC} $1"; WARN=$((WARN + 1)); }

# ── Locate build output ──
if [ -d "/work/buildroot/output" ]; then
    BUILD_DIR="/work/buildroot/output"
elif [ -d "output" ]; then
    BUILD_DIR="$(pwd)/output"
else
    echo -e "${RED}ERROR: Cannot find build output directory.${NC}"
    echo "Run this script from /work/buildroot or ensure the build completed."
    exit 1
fi

IMAGES_DIR="${BUILD_DIR}/images"
TARGET_DIR="${BUILD_DIR}/target"

echo ""
echo "============================================"
echo " Black Hand OS - Pre-Flash Verification"
echo "============================================"
echo " Build dir: ${BUILD_DIR}"
echo ""

# ====================================================================
# 1. CHECK BUILD ARTIFACTS EXIST
# ====================================================================
echo "── 1. Build Artifacts ──"

if [ -f "${IMAGES_DIR}/sdcard.img" ]; then
    SIZE=$(du -h "${IMAGES_DIR}/sdcard.img" | cut -f1)
    pass "sdcard.img exists (${SIZE})"
else
    fail "sdcard.img NOT FOUND — build did not complete"
fi

if [ -f "${IMAGES_DIR}/Image" ]; then
    pass "Kernel Image exists"
else
    fail "Kernel Image NOT FOUND"
fi

if [ -f "${IMAGES_DIR}/bcm2711-rpi-4-b.dtb" ]; then
    pass "Device tree bcm2711-rpi-4-b.dtb exists"
else
    fail "Device tree bcm2711-rpi-4-b.dtb NOT FOUND"
fi

if [ -f "${IMAGES_DIR}/rootfs.ext4" ]; then
    pass "rootfs.ext4 exists"
else
    fail "rootfs.ext4 NOT FOUND"
fi

# ====================================================================
# 2. CHECK BOOT PARTITION FILES
# ====================================================================
echo ""
echo "── 2. Boot Partition Files ──"

if [ -f "${IMAGES_DIR}/config.txt" ]; then
    pass "config.txt exists"
    # Check content
    if grep -q "vc4-kms-dpi-hyperpixel4" "${IMAGES_DIR}/config.txt"; then
        pass "config.txt loads HyperPixel overlay"
    else
        fail "config.txt missing HyperPixel overlay"
    fi
    if grep -q "dtoverlay=vc4-kms-v3d" "${IMAGES_DIR}/config.txt"; then
        warn "config.txt loads vc4-kms-v3d separately (may conflict with HyperPixel)"
    else
        pass "config.txt does NOT load vc4-kms-v3d separately (correct)"
    fi
    if grep -q "arm_64bit=1" "${IMAGES_DIR}/config.txt"; then
        pass "config.txt sets arm_64bit=1"
    else
        fail "config.txt missing arm_64bit=1"
    fi
else
    fail "config.txt NOT FOUND in images dir"
fi

if [ -f "${IMAGES_DIR}/cmdline.txt" ]; then
    pass "cmdline.txt exists"
    if grep -q "console=tty1" "${IMAGES_DIR}/cmdline.txt"; then
        pass "cmdline.txt has console=tty1 (framebuffer output)"
    else
        warn "cmdline.txt missing console=tty1 — no kernel messages on display"
    fi
else
    fail "cmdline.txt NOT FOUND"
fi

# Firmware files
for f in start4.elf fixup4.dat; do
    if [ -f "${IMAGES_DIR}/${f}" ]; then
        pass "RPi firmware: ${f} exists"
    else
        fail "RPi firmware: ${f} NOT FOUND — Pi will not boot"
    fi
done

# ====================================================================
# 3. CHECK DEVICE TREE OVERLAYS
# ====================================================================
echo ""
echo "── 3. Device Tree Overlays ──"

if [ -d "${IMAGES_DIR}/overlays" ]; then
    OVERLAY_COUNT=$(ls -1 "${IMAGES_DIR}/overlays/"*.dtbo 2>/dev/null | wc -l)
    pass "overlays/ directory exists (${OVERLAY_COUNT} .dtbo files)"

    if [ -f "${IMAGES_DIR}/overlays/vc4-kms-dpi-hyperpixel4.dtbo" ]; then
        SIZE=$(stat -c%s "${IMAGES_DIR}/overlays/vc4-kms-dpi-hyperpixel4.dtbo" 2>/dev/null || stat -f%z "${IMAGES_DIR}/overlays/vc4-kms-dpi-hyperpixel4.dtbo" 2>/dev/null)
        pass "HyperPixel overlay exists (${SIZE} bytes)"
        if [ "${SIZE}" -lt 1000 ]; then
            warn "HyperPixel overlay is suspiciously small (${SIZE} bytes) — may be incomplete"
        fi
    else
        fail "vc4-kms-dpi-hyperpixel4.dtbo NOT FOUND — display will not work"
    fi
else
    fail "overlays/ directory NOT FOUND"
fi

# ====================================================================
# 4. CHECK ROOTFS — INIT SYSTEM
# ====================================================================
echo ""
echo "── 4. Root Filesystem — Init System ──"

if [ -f "${TARGET_DIR}/etc/inittab" ]; then
    pass "/etc/inittab exists"
    if grep -q "tty1.*respawn.*getty" "${TARGET_DIR}/etc/inittab"; then
        pass "inittab spawns getty on tty1 (HyperPixel login prompt)"
    else
        warn "inittab has no getty on tty1 — no login prompt on display"
    fi
    if grep -q "ttyAMA10.*respawn.*getty" "${TARGET_DIR}/etc/inittab"; then
        pass "inittab spawns getty on ttyAMA10 (serial debug)"
    else
        warn "inittab has no getty on ttyAMA10 — no serial console"
    fi
    if grep -q "rcS" "${TARGET_DIR}/etc/inittab"; then
        pass "inittab calls rcS"
    else
        fail "inittab does NOT call rcS"
    fi
else
    fail "/etc/inittab NOT FOUND — BusyBox init will use defaults (may not match your setup)"
fi

if [ -f "${TARGET_DIR}/etc/init.d/rcS" ]; then
    pass "/etc/init.d/rcS exists"
    if [ -x "${TARGET_DIR}/etc/init.d/rcS" ]; then
        pass "rcS is executable"
    else
        fail "rcS is NOT executable"
    fi
else
    fail "/etc/init.d/rcS NOT FOUND"
fi

if [ -f "${TARGET_DIR}/etc/init.d/rcK" ]; then
    pass "/etc/init.d/rcK exists"
else
    warn "/etc/init.d/rcK not found (non-critical)"
fi

# ====================================================================
# 5. CHECK ROOTFS — KEY BINARIES
# ====================================================================
echo ""
echo "── 5. Root Filesystem — Key Binaries ──"

declare -A BINARIES
BINARIES=(
    ["/sbin/init"]="BusyBox init (PID 1)"
    ["/sbin/getty"]="getty (console login)"
    ["/bin/sh"]="Shell"
    ["/bin/mount"]="mount"
    ["/sbin/ifconfig"]="ifconfig (networking)"
    ["/usr/sbin/dropbear"]="Dropbear SSH server"
    ["/usr/bin/evtest"]="evtest (touch input debug)"
    ["/usr/bin/fbv"]="fbv (framebuffer image viewer)"
    ["/usr/bin/htop"]="htop"
    ["/usr/bin/nano"]="nano editor"
)

for bin in "${!BINARIES[@]}"; do
    desc="${BINARIES[$bin]}"
    if [ -e "${TARGET_DIR}${bin}" ] || [ -L "${TARGET_DIR}${bin}" ]; then
        pass "${desc}: ${bin} exists"
    else
        fail "${desc}: ${bin} NOT FOUND"
    fi
done

# Check modprobe/kmod for module loading
if [ -e "${TARGET_DIR}/sbin/modprobe" ] || [ -L "${TARGET_DIR}/sbin/modprobe" ]; then
    pass "modprobe exists (needed for loading display modules)"
else
    warn "modprobe not found — kernel modules won't load dynamically"
fi

# ====================================================================
# 6. CHECK ROOTFS — KERNEL MODULES
# ====================================================================
echo ""
echo "── 6. Root Filesystem — Kernel Modules ──"

MODULES_DIR=$(find "${TARGET_DIR}/lib/modules" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | head -1)
if [ -n "${MODULES_DIR}" ]; then
    KVER=$(basename "${MODULES_DIR}")
    pass "Kernel modules installed for ${KVER}"

    # Check for key display/touch modules
    for mod in vc4 v3d drm i2c-gpio goodix; do
        if find "${MODULES_DIR}" -name "${mod}.ko*" 2>/dev/null | grep -q .; then
            pass "Module ${mod}.ko found (loadable)"
        else
            # Check if built-in to kernel
            BUILTIN_FILE="${MODULES_DIR}/modules.builtin"
            if [ -f "${BUILTIN_FILE}" ] && grep -q "/${mod}\.ko" "${BUILTIN_FILE}"; then
                pass "Module ${mod} is built-in to kernel"
            else
                warn "Module ${mod} not found as .ko or in modules.builtin"
            fi
        fi
    done
else
    warn "No kernel modules directory found (modules may be built-in)"
fi

# ====================================================================
# 7. CHECK ROOTFS — LIBRARIES
# ====================================================================
echo ""
echo "── 7. Root Filesystem — Libraries ──"

# Library names don't always match package names. Search broadly.
declare -A LIBS
LIBS=(
    ["alsa"]="libasound"
    ["sqlite3"]="libsqlite3"
    ["cjson"]="libcjson"
    ["lvgl"]="liblvgl or lv_"
    ["mpg123"]="libmpg123"
)

for key in "${!LIBS[@]}"; do
    desc="${LIBS[$key]}"
    if find "${TARGET_DIR}/usr/lib" -name "*${key}*" 2>/dev/null | grep -q .; then
        pass "Library ${desc} found"
    else
        warn "Library ${desc} not found"
    fi
done

# ====================================================================
# 8. CHECK ROOTFS — NETWORK CONFIG
# ====================================================================
echo ""
echo "── 8. Root Filesystem — Network Configuration ──"

if [ -f "${TARGET_DIR}/etc/hostname" ]; then
    HOSTNAME=$(cat "${TARGET_DIR}/etc/hostname")
    pass "Hostname: ${HOSTNAME}"
else
    warn "/etc/hostname not found"
fi

if [ -f "${TARGET_DIR}/etc/fstab" ]; then
    pass "/etc/fstab exists"
else
    warn "/etc/fstab not found"
fi

if [ -f "${TARGET_DIR}/etc/resolv.conf" ]; then
    pass "/etc/resolv.conf exists"
else
    warn "/etc/resolv.conf not found (DNS may not work)"
fi

# ====================================================================
# 9. CHECK ROOTFS — BUILD STAMP
# ====================================================================
echo ""
echo "── 9. Build Stamp ──"

if [ -f "${TARGET_DIR}/etc/blackhand/build-id" ]; then
    pass "Build ID file exists"
    cat "${TARGET_DIR}/etc/blackhand/build-id" | sed 's/^/       /'
else
    warn "Build ID file not found (post-build.sh may not have run)"
fi

# ====================================================================
# SUMMARY
# ====================================================================
echo ""
echo "============================================"
echo " RESULTS"
echo "============================================"
echo -e "  ${GREEN}PASS: ${PASS}${NC}"
echo -e "  ${RED}FAIL: ${FAIL}${NC}"
echo -e "  ${YELLOW}WARN: ${WARN}${NC}"
echo ""

if [ "${FAIL}" -eq 0 ]; then
    echo -e "${GREEN}All critical checks passed. Safe to flash.${NC}"
    echo ""
    echo "Flash with:"
    echo "  sudo dd if=${IMAGES_DIR}/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync"
    echo ""
    echo "After booting, verify display over serial (ttyAMA10 @ 115200):"
    echo "  ls /dev/fb0 /dev/dri/card0"
    echo "  dmesg | grep -iE 'vc4|dpi|drm|fb|goodix'"
    exit 0
else
    echo -e "${RED}${FAIL} critical check(s) FAILED. Do NOT flash until fixed.${NC}"
    exit 1
fi
