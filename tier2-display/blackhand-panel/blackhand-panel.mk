################################################################################
# blackhand-panel — no package build, only a kernel pre-build hook.
#
# The kernel resolves CONFIG_EXTRA_FIRMWARE_DIR="firmware" against its own
# source tree, so the blob is generated straight into $(LINUX_DIR)/firmware.
# To try the NV3030B fallback, point PANEL_SEQUENCE at panel-nv3030b.txt.
################################################################################

BLACKHAND_PANEL_DIR = $(BR2_EXTERNAL_BLACKHAND_PATH)/tier2-display/blackhand-panel
BLACKHAND_PANEL_SEQUENCE ?= $(BLACKHAND_PANEL_DIR)/panel.txt

ifeq ($(BR2_PACKAGE_BLACKHAND_PANEL),y)
define BLACKHAND_PANEL_GENERATE_FIRMWARE
	mkdir -p $(LINUX_DIR)/firmware
	python3 $(BLACKHAND_PANEL_DIR)/mkpanelbin.py \
		$(BLACKHAND_PANEL_SEQUENCE) $(LINUX_DIR)/firmware/panel.bin
endef
LINUX_PRE_BUILD_HOOKS += BLACKHAND_PANEL_GENERATE_FIRMWARE
endif
