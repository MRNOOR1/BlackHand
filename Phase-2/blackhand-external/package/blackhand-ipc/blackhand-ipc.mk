################################################################################
# blackhand-ipc
################################################################################

BLACKHAND_IPC_VERSION = 0.1
BLACKHAND_IPC_SITE = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/blackhand-ipc/src
BLACKHAND_IPC_SITE_METHOD = local
BLACKHAND_IPC_DEPENDENCIES =

define BLACKHAND_IPC_BUILD_CMDS
	$(MAKE) -C $(@D)
endef

define BLACKHAND_IPC_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/libblackhand-ipc.a $(TARGET_DIR)/usr/lib/libblackhand-ipc.a
	$(INSTALL) -D -m 0644 $(@D)/ipc.h $(TARGET_DIR)/usr/include/blackhand/ipc.h
	$(INSTALL) -D -m 0644 $(@D)/ipc_framing.h $(TARGET_DIR)/usr/include/blackhand/ipc_framing.h
	$(INSTALL) -D -m 0644 $(@D)/ipc_dispatch.h $(TARGET_DIR)/usr/include/blackhand/ipc_dispatch.h
	$(INSTALL) -D -m 0644 $(@D)/cJSON.h $(TARGET_DIR)/usr/include/blackhand/cJSON.h
endef

$(eval $(generic-package))
