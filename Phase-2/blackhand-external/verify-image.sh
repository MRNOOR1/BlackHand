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

# 6. BlackHand init system
echo ""
echo "── 6. BlackHand Init System ──"

# PID 1 binary — the kernel executes this directly on boot
if [ -e "${TARGET_DIR}/sbin/blackhand-init" ]; then
    pass "blackhand-init (PID 1 binary)"
else
    fail "blackhand-init NOT FOUND — device will not boot with blackhand init"
fi

# service manager — first child spawned by PID 1
if [ -e "${TARGET_DIR}/usr/bin/blackhand-svc-mgr" ]; then
    pass "blackhand-svc-mgr (service manager)"
else
    warn "blackhand-svc-mgr NOT FOUND — services will not start automatically"
fi

# services config — read by service manager on startup
if [ -f "${TARGET_DIR}/etc/blackhand/services.conf" ]; then
    pass "services.conf"
    # verify all four services are listed
    grep -q "blackhand-audio"   "${TARGET_DIR}/etc/blackhand/services.conf" && pass "  services.conf: blackhand-audio listed"   || fail "  services.conf: blackhand-audio MISSING"
    grep -q "blackhand-storage" "${TARGET_DIR}/etc/blackhand/services.conf" && pass "  services.conf: blackhand-storage listed" || fail "  services.conf: blackhand-storage MISSING"
    grep -q "blackhand-modem"   "${TARGET_DIR}/etc/blackhand/services.conf" && pass "  services.conf: blackhand-modem listed"   || fail "  services.conf: blackhand-modem MISSING"
    grep -q "blackhand-ui"      "${TARGET_DIR}/etc/blackhand/services.conf" && pass "  services.conf: blackhand-ui listed"      || fail "  services.conf: blackhand-ui MISSING"
else
    fail "services.conf NOT FOUND — service manager will abort on boot"
fi

# 7. BlackHand service binaries
echo ""
echo "── 7. BlackHand Service Binaries ──"

for bin in \
    "/usr/bin/blackhand-audio" \
    "/usr/bin/blackhand-storage" \
    "/usr/bin/blackhand-modem" \
    "/usr/bin/blackhand-ui"; do
    if [ -e "${TARGET_DIR}${bin}" ]; then
        pass "$(basename ${bin})"
    else
        warn "$(basename ${bin}) NOT FOUND — service will fail to start"
    fi
done

# 8. BlackHand IPC Library
#
# libblackhand-ipc.a is a STATIC library — its code is copied directly into
# each service binary at link time. The .a file itself is not needed at runtime
# and Buildroot removes it from TARGET_DIR during its cleanup pass to save space.
# The correct place to verify it is STAGING_DIR (the cross-compilation sysroot),
# which is where the compiler finds it when building dependent services.
#
# STAGING_DIR path: output/host/<arch>-buildroot-linux-gnu/sysroot
# We locate it dynamically so this works regardless of toolchain tuple.
echo ""
echo "── 8. BlackHand IPC Library (staging) ──"

STAGING_DIR=$(ls -d "${BUILD_DIR}/host/"*"-buildroot-linux-gnu/sysroot" 2>/dev/null | head -1)

if [ -z "${STAGING_DIR}" ]; then
    warn "Cannot locate STAGING_DIR — toolchain may not be built yet"
else
    if [ -f "${STAGING_DIR}/usr/lib/libblackhand-ipc.a" ]; then
        pass "libblackhand-ipc.a (in staging — correct for static library)"
    else
        fail "libblackhand-ipc.a NOT FOUND in staging — services will fail to link"
    fi

    for hdr in ipc.h ipc_framing.h ipc_dispatch.h cJSON.h; do
        if [ -f "${STAGING_DIR}/usr/include/blackhand/${hdr}" ]; then
            pass "  header: ${hdr}"
        else
            fail "  header: ${hdr} NOT FOUND in staging — dependent packages cannot compile"
        fi
    done
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
