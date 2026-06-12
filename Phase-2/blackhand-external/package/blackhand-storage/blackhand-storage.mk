################################################################################
# blackhand-storage
################################################################################

BLACKHAND_STORAGE_VERSION = 0.1
BLACKHAND_STORAGE_SITE = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/blackhand-storage/src
BLACKHAND_STORAGE_SITE_METHOD = local
BLACKHAND_STORAGE_DEPENDENCIES = blackhand-ipc

define BLACKHAND_STORAGE_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define BLACKHAND_STORAGE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/blackhand-storage $(TARGET_DIR)/usr/bin/blackhand-storage
endef

$(eval $(generic-package))
