################################################################################
# blackhand-stt
################################################################################

BLACKHAND_STT_VERSION = 0.1
BLACKHAND_STT_SITE = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/blackhand-stt/src
BLACKHAND_STT_SITE_METHOD = local
BLACKHAND_STT_DEPENDENCIES =

define BLACKHAND_STT_BUILD_CMDS
	$(MAKE) -C $(@D)
endef

define BLACKHAND_STT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/blackhand-stt $(TARGET_DIR)/usr/bin/blackhand-stt
endef

$(eval $(generic-package))
