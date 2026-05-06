################################################################################
# blackhand-ipc
################################################################################

BLACKHAND_IPC_VERSION = 0.1
BLACKHAND_IPC_SITE = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/blackhand-ipc/src
BLACKHAND_IPC_SITE_METHOD = local
BLACKHAND_IPC_DEPENDENCIES =
BLACKHAND_IPC_INSTALL_STAGING = YES

define BLACKHAND_IPC_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

# STAGING install — makes headers and library available to OTHER packages
# that depend on blackhand-ipc at compile time (e.g. blackhand-audio).
# ipc_framing.h is kept as a compatibility shim so existing services that
# include it directly do not need to change their source.
define BLACKHAND_IPC_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/libblackhand-ipc.a    $(STAGING_DIR)/usr/lib/libblackhand-ipc.a
	$(INSTALL) -D -m 0644 $(@D)/ipc.h                 $(STAGING_DIR)/usr/include/blackhand/ipc.h
	$(INSTALL) -D -m 0644 $(@D)/ipc_framing.h         $(STAGING_DIR)/usr/include/blackhand/ipc_framing.h
	$(INSTALL) -D -m 0644 $(@D)/ipc_dispatch.h        $(STAGING_DIR)/usr/include/blackhand/ipc_dispatch.h
	$(INSTALL) -D -m 0644 $(@D)/cJSON.h               $(STAGING_DIR)/usr/include/blackhand/cJSON.h
endef

# TARGET install — puts headers on the device rootfs so verify-image.sh can
# check them, and keeps libblackhand-ipc.a visible at runtime (useful for
# on-device builds or debugging; harmless if not needed).
define BLACKHAND_IPC_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/libblackhand-ipc.a    $(TARGET_DIR)/usr/lib/libblackhand-ipc.a
	$(INSTALL) -D -m 0644 $(@D)/ipc.h                 $(TARGET_DIR)/usr/include/blackhand/ipc.h
	$(INSTALL) -D -m 0644 $(@D)/ipc_framing.h         $(TARGET_DIR)/usr/include/blackhand/ipc_framing.h
	$(INSTALL) -D -m 0644 $(@D)/ipc_dispatch.h        $(TARGET_DIR)/usr/include/blackhand/ipc_dispatch.h
	$(INSTALL) -D -m 0644 $(@D)/cJSON.h               $(TARGET_DIR)/usr/include/blackhand/cJSON.h
endef

$(eval $(generic-package))
