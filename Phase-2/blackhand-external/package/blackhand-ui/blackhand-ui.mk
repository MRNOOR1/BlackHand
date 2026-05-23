################################################################################
# blackhand-ui
################################################################################

BLACKHAND_UI_VERSION = 0.2
BLACKHAND_UI_SITE = $(BR2_EXTERNAL_BLACKHAND_PATH)/package/blackhand-ui/src
BLACKHAND_UI_SITE_METHOD = local
BLACKHAND_UI_DEPENDENCIES = notcurses mpg123 alsa-lib dbus

$(eval $(cmake-package))
