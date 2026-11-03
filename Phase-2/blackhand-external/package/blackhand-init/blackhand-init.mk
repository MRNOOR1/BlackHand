################################################################################
# blackhand-init
################################################################################

BLACKHAND_INIT_VERSION = 0.1
BLACKHAND_INIT_SITE = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/blackhand-init/src
BLACKHAND_INIT_SITE_METHOD = local
BLACKHAND_INIT_DEPENDENCIES =

# Opt-in rescue shell (BR2_PACKAGE_BLACKHAND_INIT_DEBUG_SHELL). Off by default
# so production images keep the unauthenticated tty2 shell compiled out.
BLACKHAND_INIT_EXTRA_CFLAGS =
ifeq ($(BR2_PACKAGE_BLACKHAND_INIT_DEBUG_SHELL),y)
BLACKHAND_INIT_EXTRA_CFLAGS += -DBLACKHAND_DEBUG_SHELL
endif

define BLACKHAND_INIT_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) \
		CFLAGS="$(TARGET_CFLAGS) -O2 -Wall -Wextra $(BLACKHAND_INIT_EXTRA_CFLAGS)" \
		-C $(@D)
endef

define BLACKHAND_INIT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/blackhand-init    $(TARGET_DIR)/sbin/blackhand-init
	$(INSTALL) -D -m 0755 $(@D)/blackhand-svc-mgr $(TARGET_DIR)/usr/bin/blackhand-svc-mgr
endef

$(eval $(generic-package))
