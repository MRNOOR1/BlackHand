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
    # Display checks live in section 5 — this panel is driven by the kernel
    # over SPI, not by the VideoCore's DPI block.
    grep -q "arm_64bit=1" "${IMAGES_DIR}/config.txt" && pass "arm_64bit=1" || fail "arm_64bit=1 missing"
else
    fail "config.txt NOT FOUND"
fi

[ -f "${IMAGES_DIR}/cmdline.txt" ] && pass "cmdline.txt" || fail "cmdline.txt NOT FOUND"

for f in start4.elf fixup4.dat; do
    [ -f "${IMAGES_DIR}/${f}" ] && pass "${f}" || fail "${f} NOT FOUND — Pi won't boot"
done

# gpu_mem<32 requires the cutdown GPU firmware (start4cd.elf / fixup4cd.dat).
# Without them the VideoCore silently refuses to boot — no HDMI, no serial,
# no LEDs, no SSH. Buildroot's rpi-firmware package does NOT install the cd
# variants, so lowering gpu_mem also means adding a fetch step in
# post-build.sh (mirror the BCM4345C0.hcd pattern). Keep gpu_mem>=64 unless
# you have done that.
GPU_MEM=$(grep -E '^\s*gpu_mem\s*=' "${IMAGES_DIR}/config.txt" 2>/dev/null | tail -1 | sed -E 's/.*=[[:space:]]*([0-9]+).*/\1/')
if [ -n "${GPU_MEM}" ] && [ "${GPU_MEM}" -lt 32 ] 2>/dev/null; then
    for f in start4cd.elf fixup4cd.dat; do
        [ -f "${IMAGES_DIR}/${f}" ] && pass "${f} (required by gpu_mem=${GPU_MEM})" \
            || fail "${f} NOT FOUND — gpu_mem=${GPU_MEM} REQUIRES it or Pi silently halts"
    done
fi

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

# fbv (framebuffer image viewer) was used to render the HyperPixel splash.
# Nothing draws splash images now — the panel is initialised by
# panel-mipi-dbi and the first thing on-screen is the notcurses UI.
for bin in /sbin/init /bin/sh /usr/sbin/dropbear; do
    if [ -e "${TARGET_DIR}${bin}" ] || [ -L "${TARGET_DIR}${bin}" ]; then
        pass "$(basename ${bin})"
    else
        fail "$(basename ${bin}) NOT FOUND"
    fi
done

# 5. SPI panel (Waveshare 1.83inch, 240x284 ST7789P)
echo ""
echo "── 5. SPI Panel ──"

CFG="${IMAGES_DIR}/config.txt"
CMD="${IMAGES_DIR}/cmdline.txt"

if [ -f "${CFG}" ]; then
    grep -q "dtoverlay=mipi-dbi-spi" "${CFG}" \
        && pass "config.txt loads mipi-dbi-spi overlay" \
        || fail "no mipi-dbi-spi overlay in config.txt — panel will not probe"

    # MISO is not brought out on this module. Without write-only the driver
    # tries to read the controller ID back and fails probe.
    grep -q "write-only" "${CFG}" \
        && pass "write-only set (module has no MISO)" \
        || fail "write-only MISSING — driver will try to read the panel and fail"

    grep -q "width=240" "${CFG}" && grep -q "height=284" "${CFG}" \
        && pass "panel geometry 240x284" \
        || fail "panel geometry is not 240x284"

    grep -q "y-offset=" "${CFG}" \
        && pass "y-offset set ($(grep -o 'y-offset=[0-9]*' "${CFG}" | head -1))" \
        || warn "no y-offset — a 284-row panel inside 320-row GRAM will sit misaligned"

    grep -q "dtparam=spi=on" "${CFG}" \
        && pass "SPI0 enabled" \
        || fail "dtparam=spi=on missing"

    # Strip comment lines before matching — the SPI panel's config.txt
    # legitimately mentions "HyperPixel" in the header comments explaining
    # what was replaced. We only care about live directives.
    if grep -vE '^\s*(#|$)' "${CFG}" | grep -qiE '(dtoverlay=hyperpixel|enable_dpi_lcd|dpi_timings)'; then
        fail "HyperPixel/DPI leftovers still active in config.txt"
    else
        pass "no HyperPixel/DPI directives in config.txt"
    fi
else
    fail "config.txt NOT FOUND in images dir"
fi

# The init blob. Without it panel-mipi-dbi probes and immediately fails.
if [ -f "${TARGET_DIR}/lib/firmware/panel.bin" ]; then
    if head -c 8 "${TARGET_DIR}/lib/firmware/panel.bin" | grep -q "MIPI DBI"; then
        pass "panel.bin present and has valid magic ($(wc -c < "${TARGET_DIR}/lib/firmware/panel.bin") bytes)"
    else
        fail "panel.bin present but magic is wrong — regenerate with board/blackhand/panel/mkpanelbin.py"
    fi
else
    fail "panel.bin MISSING from rootfs — panel will not initialise"
fi

# The panel driver is BUILT IN (=y), not a module — see the long note in
# bcm2711.config. So assert the opposite of what you might expect: a
# panel-mipi-dbi.ko on the target means the fragment did not take effect.
if find "${TARGET_DIR}/lib/modules" -name 'panel-mipi-dbi.ko*' 2>/dev/null | grep -q .; then
    fail "panel-mipi-dbi is a MODULE — expected built-in (CONFIG_DRM_PANEL_MIPI_DBI=y)"
else
    pass "panel-mipi-dbi is not a module (built in, as intended)"
fi

# Any compressed module is unloadable: busybox insmod cannot decompress, and
# the host depmod could not index .ko.xz files — which is what silently broke
# the panel driver when it was still a module.
if find "${TARGET_DIR}/lib/modules" -name '*.ko.xz' -o -name '*.ko.zst' -o -name '*.ko.gz' 2>/dev/null | grep -q .; then
    fail "compressed modules present — busybox cannot load them (need CONFIG_MODULE_COMPRESS_NONE=y)"
else
    pass "kernel modules are uncompressed"
fi

if [ -f "${TARGET_DIR}/etc/init.d/rcS" ] && grep -q "modprobe panel-mipi-dbi" "${TARGET_DIR}/etc/init.d/rcS"; then
    pass "rcS loads the panel driver"
else
    fail "rcS does NOT modprobe panel-mipi-dbi — screen will stay dark"
fi

if [ -x "${TARGET_DIR}/usr/bin/panel-doctor" ]; then
    pass "panel-doctor installed (blank-screen triage without a screen)"
else
    warn "panel-doctor not installed — a blank panel will be much harder to diagnose"
fi

# The UI needs a 40-column grid; 240px / 6px-wide font = 40 cols exactly.
if [ -f "${CMD}" ]; then
    if grep -q "fbcon=font:ProFont6x11" "${CMD}"; then
        pass "console font ProFont6x11 → 40 cols x 25 rows"
    elif grep -q "fbcon=font:" "${CMD}"; then
        warn "console font is $(grep -o 'fbcon=font:[A-Za-z0-9]*' "${CMD}") — UI needs >= 40 columns"
    else
        warn "no fbcon=font: set — kernel default may not give 40 columns"
    fi
else
    fail "cmdline.txt NOT FOUND in images dir"
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
