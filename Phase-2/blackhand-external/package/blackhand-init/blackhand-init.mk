################################################################################
# blackhand-init
################################################################################

BLACKHAND_INIT_VERSION = 0.1
BLACKHAND_INIT_SITE = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/blackhand-init/src
BLACKHAND_INIT_SITE_METHOD = local
BLACKHAND_INIT_DEPENDENCIES =

define BLACKHAND_INIT_BUILD_CMDS
	$(MAKE) -C $(@D)
endef

define BLACKHAND_INIT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/blackhand-init $(TARGET_DIR)/sbin/blackhand-init
endef

$(eval $(generic-package))
