# ============================================================================
# blackhand-storage — Makefile (Phase 3, JSON file storage)
# ============================================================================

CC     ?= $(CROSS_COMPILE)gcc
override CFLAGS += -I$(STAGING_DIR)/usr/include/blackhand
LIBS    = -L$(STAGING_DIR)/usr/lib -lblackhand-ipc

all:
	$(CC) $(CFLAGS) -o blackhand-storage storage_service.c $(LIBS)

clean:
	rm -f blackhand-storage
