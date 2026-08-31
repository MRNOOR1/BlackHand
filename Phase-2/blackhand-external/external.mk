# Load all package makefiles from this external tree.
include $(sort $(wildcard $(BR2_EXTERNAL_BLACKHAND_PATH)/package/*/*.mk))

# ── Built-in panel init blob ────────────────────────────────────────────────
# bcm2711.config sets CONFIG_EXTRA_FIRMWARE="panel.bin" so the ST7789P init
# sequence is linked into the kernel image. A built-in driver probes before
# the rootfs is mounted, so request_firmware() cannot reach /lib/firmware —
# the blob has to be inside vmlinux.
#
# CONFIG_EXTRA_FIRMWARE_DIR is "firmware", which the kernel resolves relative
# to $(srctree), so the file must be in place before the kernel builds.
# Deferred expansion in the define means $(LINUX_DIR) resolves at recipe
# time, so include order with linux.mk does not matter.
#
# The same panel.bin also ships in the rootfs overlay at
# /lib/firmware/panel.bin — harmless, and it keeps the =m fallback in rcS
# usable if this is ever reverted.
define BLACKHAND_INSTALL_PANEL_FIRMWARE
	$(INSTALL) -D -m 0644 \
		$(BR2_EXTERNAL_BLACKHAND_PATH)/board/blackhand/overlay/lib/firmware/panel.bin \
		$(LINUX_DIR)/firmware/panel.bin
endef
LINUX_PRE_BUILD_HOOKS += BLACKHAND_INSTALL_PANEL_FIRMWARE
