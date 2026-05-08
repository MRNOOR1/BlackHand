# ============================================================================
# blackhand-modem — Makefile
#
# at_parser.c and sms.c are helper modules (Phase 2 stubs). They are listed
# as separate compilation units so they can grow independently without
# needing the Makefile to change.
# ============================================================================

CC     ?= $(CROSS_COMPILE)gcc
override CFLAGS += -I$(STAGING_DIR)/usr/include/blackhand
LIBS    = -L$(STAGING_DIR)/usr/lib -lblackhand-ipc

SRCS = modem_service.c ipc_dispatch.c at_parser.c sms.c

all:
	$(CC) $(CFLAGS) -o blackhand-modem $(SRCS) $(LIBS)

clean:
	rm -f blackhand-modem
