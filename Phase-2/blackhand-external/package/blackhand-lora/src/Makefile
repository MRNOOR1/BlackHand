# ============================================================================
# blackhand-lora — Makefile
#
# Compiles the LoRa service (SX1276 over spidev + JSON-RPC over a Unix socket)
# into the blackhand-lora binary. Depends on blackhand-ipc (headers + static
# library) only; the SX1276 is driven directly through spidev/gpio ioctls, so
# there are no external radio libraries.
#
# CFLAGS gets -I$(STAGING_DIR)/usr/include/blackhand for the IPC headers
# (ipc.h, ipc_framing.h, ipc_dispatch.h, cJSON.h). Buildroot passes its own
# CFLAGS on the command line, so we 'override +=' to append ours.
#
# LIBS links libblackhand-ipc.a (static) and pthread (the RX poll thread).
# ============================================================================

CC     ?= $(CROSS_COMPILE)gcc
override CFLAGS += -I$(STAGING_DIR)/usr/include/blackhand
LIBS    = -L$(STAGING_DIR)/usr/lib -lblackhand-ipc -lpthread

SRCS = main.c ipc_dispatch.c sx1276.c rx_queue.c

all:
	$(CC) $(CFLAGS) -o blackhand-lora $(SRCS) $(LIBS)

clean:
	rm -f blackhand-lora
