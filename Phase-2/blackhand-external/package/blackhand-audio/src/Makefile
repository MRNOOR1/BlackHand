# ============================================================================
# blackhand-audio — Makefile
#
# Compiles main.c (the audio service) into the blackhand-audio binary.
# Depends on blackhand-ipc (headers and static library) and alsa-lib.
#
# CFLAGS includes -I$(STAGING_DIR)/usr/include/blackhand so the IPC headers
# (ipc.h, ipc_framing.h, ipc_dispatch.h, cJSON.h) can be found. Buildroot
# sets STAGING_DIR to the cross-compilation sysroot before invoking make.
#
# LIBS links against libblackhand-ipc.a (static) and libasound (shared).
# ============================================================================

# CC is set by Buildroot via TARGET_CONFIGURE_OPTS — do not override.
# CFLAGS is also passed by Buildroot on the command line (highest priority).
# ?= would be silently ignored for CFLAGS. Use 'override +=' to APPEND
# our extra flags on top of whatever Buildroot already set.
CC     ?= $(CROSS_COMPILE)gcc
override CFLAGS += -I$(STAGING_DIR)/usr/include/blackhand
LIBS    = -L$(STAGING_DIR)/usr/lib -lblackhand-ipc -lasound

all:
	$(CC) $(CFLAGS) -o blackhand-audio main.c $(LIBS)

clean:
	rm -f blackhand-audio
