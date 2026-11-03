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
for f in sdcard.img Image bcm2711-rpi-cm4.dtb rootfs.ext4; do
    if [ -f "${IMAGES_DIR}/${f}" ]; then
        pass "${f} ($(du -h "${IMAGES_DIR}/${f}" | cut -f1))"
    else
        fail "${f} NOT FOUND"
    fi
done

# 2. Boot partition files
echo ""
echo "── 2. Boot Partition ──"

CFG="${IMAGES_DIR}/config.txt"
if [ -f "${CFG}" ]; then
    pass "config.txt"
    grep -q "arm_64bit=1" "${CFG}" && pass "arm_64bit=1" || fail "arm_64bit=1 missing"

    # SPI panel (panel-mipi-dbi) — see spi-display-bringup.md §5.1, §7.4
    grep -q "^dtoverlay=mipi-dbi-spi" "${CFG}" \
        && pass "mipi-dbi-spi overlay present" \
        || fail "dtoverlay=mipi-dbi-spi missing — SPI panel will not bind"
    grep -q "^dtparam=write-only" "${CFG}" \
        && pass "write-only set (MISO not wired)" \
        || fail "dtparam=write-only missing — probe will hang reading controller ID"
    grep -Eq "width=240([,[:space:]]|$)" "${CFG}" \
        && pass "width=240" || fail "width=240 missing"
    grep -Eq "height=304([,[:space:]]|$)" "${CFG}" \
        && pass "height=304" || fail "height=304 missing"
    grep -Eq "y-offset=0([,[:space:]]|$)" "${CFG}" \
        && pass "y-offset=0 (measured, no offset)" || fail "y-offset=0 missing/incorrect"

    # No HyperPixel/DPI leftovers in the boot config
    if grep -Eq "enable_dpi_lcd|dpi_timings|dpi_mode|hyperpixel" "${CFG}"; then
        fail "config.txt still contains HyperPixel/DPI directives"
    else
        pass "config.txt free of HyperPixel/DPI directives"
    fi
else
    fail "config.txt NOT FOUND"
fi

CMDLINE="${IMAGES_DIR}/cmdline.txt"
if [ -f "${CMDLINE}" ]; then
    pass "cmdline.txt"
    grep -q "fbcon=font:10x18" "${CMDLINE}" \
        && pass "console font 10x18" \
        || fail "fbcon=font:10x18 missing — wrong console geometry"
else
    fail "cmdline.txt NOT FOUND"
fi

for f in start4.elf fixup4.dat; do
    [ -f "${IMAGES_DIR}/${f}" ] && pass "${f}" || fail "${f} NOT FOUND — Pi won't boot"
done

# 3. DTB symbols
echo ""
echo "── 3. DTB Symbols ──"

DTB="${IMAGES_DIR}/bcm2711-rpi-cm4.dtb"
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

for bin in /sbin/init /bin/sh /usr/sbin/dropbear /usr/bin/fbfill; do
    if [ -e "${TARGET_DIR}${bin}" ] || [ -L "${TARGET_DIR}${bin}" ]; then
        pass "$(basename ${bin})"
    else
        fail "$(basename ${bin}) NOT FOUND"
    fi
done

# 4b. tmux-hosted UI session
echo ""
echo "── 4b. tmux UI Session ──"

[ -e "${TARGET_DIR}/usr/bin/tmux" ] && pass "tmux" || fail "tmux NOT FOUND — check BR2_PACKAGE_TMUX=y"
[ -f "${TARGET_DIR}/etc/tmux.conf" ] && pass "tmux.conf" || fail "tmux.conf NOT FOUND"
[ -f "${TARGET_DIR}/usr/bin/bh-session" ] && [ -x "${TARGET_DIR}/usr/bin/bh-session" ] \
    && pass "bh-session (executable)" || fail "bh-session missing or not executable"
[ -f "${TARGET_DIR}/usr/share/terminfo/t/tmux-256color" ] \
    && pass "tmux-256color terminfo" \
    || fail "tmux-256color terminfo NOT FOUND — check BR2_PACKAGE_NCURSES_ADDITIONAL_TERMINFO"
# The UI is no longer a svc-mgr service, so services.conf can no longer prove it
# gets launched. rcS is the only thing that starts it now — if this block is
# removed the device boots with no UI at all and every other check still passes.
grep -q "bh-session --detached" "${TARGET_DIR}/etc/init.d/rcS" \
    && grep -q "new-session -d .* -s bh" "${TARGET_DIR}/usr/bin/bh-session" \
    && pass "rcS creates the detached 'bh' session (via bh-session --detached)" \
    || fail "rcS does NOT create the tmux session — nothing will start the UI"
# tty1 (the phone's own screen) is a tmux client spawned by PID 1; without it
# the UI runs but is only reachable over SSH. grep -a: init is a binary.
grep -aq "/usr/bin/bh-session" "${TARGET_DIR}/sbin/blackhand-init" \
    && pass "init attaches bh-session on tty1" \
    || fail "init does NOT reference bh-session — UI invisible on the phone screen"

# 5. SPI panel firmware + display sanity — see spi-display-bringup.md §4, §7.4
echo ""
echo "── 5. SPI Panel (panel-mipi-dbi) ──"

# panel.bin ships in the rootfs overlay and is also linked into the kernel via
# CONFIG_EXTRA_FIRMWARE. The overlay copy is the reliable place to inspect it.
PANEL_BIN="${TARGET_DIR}/lib/firmware/panel.bin"
if [ -f "${PANEL_BIN}" ]; then
    pass "panel.bin present ($(du -h "${PANEL_BIN}" | cut -f1))"
    # Format: offset 0, 8-byte magic "MIPI DBI" (panel-mipi-dbi.c). Reject a bad
    # blob here rather than discovering it via a dead panel on the bench.
    if [ "$(head -c 8 "${PANEL_BIN}")" = "MIPI DBI" ]; then
        pass "panel.bin has valid 'MIPI DBI' magic"
    else
        fail "panel.bin magic invalid — kernel will reject the init blob"
    fi
else
    fail "panel.bin NOT FOUND in ${TARGET_DIR}/lib/firmware — panel init blob missing"
fi

# Kernel modules MUST NOT be compressed: busybox modprobe/insmod cannot handle
# .ko.xz/.gz/.zst and depmod won't index them (spi-display-bringup.md §8.3).
COMPRESSED_KO="$(find "${TARGET_DIR}/lib/modules" \
    \( -name '*.ko.xz' -o -name '*.ko.gz' -o -name '*.ko.zst' \) 2>/dev/null | head -1 || true)"
if [ -n "${COMPRESSED_KO}" ]; then
    fail "compressed kernel modules present (e.g. ${COMPRESSED_KO}) — busybox cannot load them"
else
    pass "no compressed kernel modules (*.ko.xz/.gz/.zst)"
fi

# No HyperPixel/DPI leftovers anywhere in the staged tree.
if [ -e "${TARGET_DIR}/usr/bin/hyperpixel4-init" ]; then
    fail "hyperpixel4-init binary still in rootfs — obsolete DPI tool"
else
    pass "no hyperpixel4-init binary in rootfs"
fi
# cmdline.txt is a single directive line (no comments), so an -i scan is safe.
# config.txt is covered by the directive check in section 2 (RPi directives are
# lowercase, matched case-sensitively) — its explanatory comment legitimately
# names HyperPixel, so we do not -i scan it here.
HP_CMDLINE="$(grep -Ei "hyperpixel|enable_dpi_lcd|dpi_timings" "${IMAGES_DIR}/cmdline.txt" 2>/dev/null || true)"
HP_DTBO="$(find "${IMAGES_DIR}/overlays" -iname '*hyperpixel*' 2>/dev/null | head -1 || true)"
if [ -n "${HP_CMDLINE}" ] || [ -n "${HP_DTBO}" ]; then
    fail "HyperPixel/DPI leftovers found in boot files (${HP_CMDLINE} ${HP_DTBO})"
else
    pass "no HyperPixel/DPI leftovers in boot files"
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
    # verify the services are listed
    grep -q "blackhand-audio"   "${TARGET_DIR}/etc/blackhand/services.conf" && pass "  services.conf: blackhand-audio listed"   || fail "  services.conf: blackhand-audio MISSING"
    grep -q "blackhand-modem"   "${TARGET_DIR}/etc/blackhand/services.conf" && pass "  services.conf: blackhand-modem listed"   || fail "  services.conf: blackhand-modem MISSING"
    grep -q "^\[blackhand-ui\]" "${TARGET_DIR}/etc/blackhand/services.conf" && fail "  services.conf: [blackhand-ui] present — tmux already runs the UI (two supervisors)" || pass "  services.conf: UI not a service (tmux hosts it)"
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
