################################################################################
# blackhand-audio
################################################################################

BLACKHAND_AUDIO_VERSION = 0.1
BLACKHAND_AUDIO_SITE = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/blackhand-audio/src
BLACKHAND_AUDIO_SITE_METHOD = local
BLACKHAND_AUDIO_DEPENDENCIES =

define BLACKHAND_AUDIO_BUILD_CMDS
	$(MAKE) -C $(@D)
endef

define BLACKHAND_AUDIO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/blackhand-audio $(TARGET_DIR)/usr/bin/blackhand-audio
endef

$(eval $(generic-package))
