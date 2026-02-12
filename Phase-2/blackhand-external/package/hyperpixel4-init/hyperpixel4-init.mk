################################################################################
# hyperpixel4-init
################################################################################

HYPERPIXEL4_INIT_VERSION = 0.1
HYPERPIXEL4_INIT_SITE = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/hyperpixel4-init/src
HYPERPIXEL4_INIT_SITE_METHOD = local

define HYPERPIXEL4_INIT_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(@D)/hyperpixel4-init $(@D)/hyperpixel4-init.c
endef

define HYPERPIXEL4_INIT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/hyperpixel4-init $(TARGET_DIR)/usr/bin/hyperpixel4-init
endef

$(eval $(generic-package))
