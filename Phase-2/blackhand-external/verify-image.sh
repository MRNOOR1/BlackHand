#!/usr/bin/env bash
# ============================================================================
# Black Hand OS - Pre-flash verification script
# ============================================================================
# Run INSIDE the Docker build container after 'make' completes.
# Usage:  bash verify-image.sh   (from /work/buildroot)
# ============================================================================
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0; FAIL=0; WARN=0

pass() { echo -e "  ${GREEN}[PASS]${NC} $1"; PASS=$((PASS + 1)); }
fail() { echo -e "  ${RED}[FAIL]${NC} $1"; FAIL=$((FAIL + 1)); }
warn() { echo -e "  ${YELLOW}[WARN]${NC} $1"; WARN=$((WARN + 1)); }

if [ -d "/work/buildroot/output" ]; then
    BUILD_DIR="/work/buildroot/output"
elif [ -d "output" ]; then
    BUILD_DIR="$(pwd)/output"
else
    echo -e "${RED}ERROR: Cannot find build output directory.${NC}"
    exit 1
fi

IMAGES_DIR="${BUILD_DIR}/images"
TARGET_DIR="${BUILD_DIR}/target"

echo ""
echo "============================================"
echo " Black Hand OS - Pre-Flash Verification"
echo "============================================"
echo ""

# 1. Build artifacts
echo "── 1. Build Artifacts ──"
for f in sdcard.img Image bcm2711-rpi-4-b.dtb rootfs.ext4; do
    if [ -f "${IMAGES_DIR}/${f}" ]; then
        pass "${f} ($(du -h "${IMAGES_DIR}/${f}" | cut -f1))"
    else
        fail "${f} NOT FOUND"
    fi
done

# 2. Boot partition files
echo ""
echo "── 2. Boot Partition ──"

if [ -f "${IMAGES_DIR}/config.txt" ]; then
    pass "config.txt"
    grep -q "enable_dpi_lcd=1" "${IMAGES_DIR}/config.txt" && pass "DPI LCD enabled" || fail "enable_dpi_lcd=1 missing"
    grep -q "dpi_timings=" "${IMAGES_DIR}/config.txt" && pass "DPI timings set" || fail "dpi_timings missing"
    grep -q "arm_64bit=1" "${IMAGES_DIR}/config.txt" && pass "arm_64bit=1" || fail "arm_64bit=1 missing"
else
    fail "config.txt NOT FOUND"
fi

[ -f "${IMAGES_DIR}/cmdline.txt" ] && pass "cmdline.txt" || fail "cmdline.txt NOT FOUND"

for f in start4.elf fixup4.dat; do
    [ -f "${IMAGES_DIR}/${f}" ] && pass "${f}" || fail "${f} NOT FOUND — Pi won't boot"
done

# 3. DTB symbols
echo ""
echo "── 3. DTB Symbols ──"

DTB="${IMAGES_DIR}/bcm2711-rpi-4-b.dtb"
DTC=""
for c in "${BUILD_DIR}/host/bin/linux-dtc" "${BUILD_DIR}/host/bin/dtc" "$(which dtc 2>/dev/null || true)"; do
    [ -n "${c}" ] && [ -x "${c}" ] && { DTC="${c}"; break; }
done
if [ -n "${DTC}" ] && [ -f "${DTB}" ]; then
    "${DTC}" -I dtb -O dts "${DTB}" 2>/dev/null | grep -q '__symbols__' \
        && pass "DTB has __symbols__" \
        || fail "DTB missing __symbols__"
else
    warn "Cannot verify DTB symbols (dtc not found)"
fi

# 4. Rootfs — init + key binaries
echo ""
echo "── 4. Rootfs — Init & Binaries ──"

[ -f "${TARGET_DIR}/etc/inittab" ] && pass "inittab" || fail "inittab NOT FOUND"
[ -f "${TARGET_DIR}/etc/init.d/rcS" ] && [ -x "${TARGET_DIR}/etc/init.d/rcS" ] \
    && pass "rcS (executable)" || fail "rcS missing or not executable"

for bin in /sbin/init /bin/sh /usr/sbin/dropbear /usr/bin/fbv; do
    if [ -e "${TARGET_DIR}${bin}" ] || [ -L "${TARGET_DIR}${bin}" ]; then
        pass "$(basename ${bin})"
    else
        fail "$(basename ${bin}) NOT FOUND"
    fi
done

# 5. HyperPixel init tool
echo ""
echo "── 5. HyperPixel Init ──"

if [ -e "${TARGET_DIR}/usr/bin/hyperpixel4-init" ]; then
    pass "hyperpixel4-init binary installed"
else
    fail "hyperpixel4-init NOT FOUND — display will stay black"
fi

if [ -f "${TARGET_DIR}/etc/init.d/rcS" ] && grep -q "hyperpixel4-init" "${TARGET_DIR}/etc/init.d/rcS"; then
    pass "rcS calls hyperpixel4-init"
else
    fail "rcS does NOT call hyperpixel4-init"
fi

# Summary
echo ""
echo "============================================"
echo -e "  ${GREEN}PASS: ${PASS}${NC}  ${RED}FAIL: ${FAIL}${NC}  ${YELLOW}WARN: ${WARN}${NC}"
echo "============================================"

if [ "${FAIL}" -eq 0 ]; then
    echo -e "${GREEN}Ready to flash.${NC}"
    echo "  sudo dd if=${IMAGES_DIR}/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync"
    echo ""
    echo "After boot:"
    echo "  ls -la /dev/fb0"
    echo "  cat /sys/class/graphics/fb0/virtual_size"
    echo "  dd if=/dev/urandom of=/dev/fb0 bs=1M count=1   # should show noise on screen"
    exit 0
else
    echo -e "${RED}${FAIL} check(s) FAILED. Do NOT flash.${NC}"
    exit 1
fi
