################################################################################
# blackhand-modem
################################################################################

BLACKHAND_MODEM_VERSION = 0.1
BLACKHAND_MODEM_SITE = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/blackhand-modem/src
BLACKHAND_MODEM_SITE_METHOD = local
BLACKHAND_MODEM_DEPENDENCIES =

define BLACKHAND_MODEM_BUILD_CMDS
	$(MAKE) -C $(@D)
endef

define BLACKHAND_MODEM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/blackhand-modem $(TARGET_DIR)/usr/bin/blackhand-modem
endef

$(eval $(generic-package))
