# Each tier's packages live in tierN-*/<package>/<package>.mk.
include $(sort $(wildcard $(BR2_EXTERNAL_BLACKHAND_PATH)/tier*/*/*.mk))
