# Load all package makefiles from this external tree.
include $(sort $(wildcard $(BR2_EXTERNAL_BLACKHAND_PATH)/package/*/*.mk))
