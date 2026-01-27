################################################################################
# blackhand-ui
################################################################################

BLACKHAND_UI_VERSION = 0.1
BLACKHAND_UI_SITE = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/blackhand-ui/src
BLACKHAND_UI_SITE_METHOD = local
BLACKHAND_UI_DEPENDENCIES =

define BLACKHAND_UI_BUILD_CMDS
	$(MAKE) -C $(@D)
endef

define BLACKHAND_UI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/blackhand-ui $(TARGET_DIR)/usr/bin/blackhand-ui
endef

$(eval $(generic-package))
