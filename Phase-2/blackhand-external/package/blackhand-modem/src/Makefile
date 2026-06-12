# ============================================================================
# blackhand-modem — Makefile (Phase 3)
# ============================================================================

CC     ?= $(CROSS_COMPILE)gcc
override CFLAGS += -I$(STAGING_DIR)/usr/include/blackhand
LIBS    = -L$(STAGING_DIR)/usr/lib -lblackhand-ipc -lpthread

SRCS = main.c serial_utility.c Modem.c urc.c modem_dispatch.c modem_storage.c

all:
	$(CC) $(CFLAGS) -o blackhand-modem $(SRCS) $(LIBS)

clean:
	rm -f blackhand-modem
