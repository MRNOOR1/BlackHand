################################################################################
# blackhand-lora
################################################################################

BLACKHAND_LORA_VERSION = 0.1
BLACKHAND_LORA_SITE = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/blackhand-lora/src
BLACKHAND_LORA_SITE_METHOD = local
BLACKHAND_LORA_DEPENDENCIES = blackhand-ipc

define BLACKHAND_LORA_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define BLACKHAND_LORA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/blackhand-lora $(TARGET_DIR)/usr/bin/blackhand-lora
endef

$(eval $(generic-package))
